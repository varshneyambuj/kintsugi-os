/*
 * Copyright 2026 Kintsugi OS Project. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Authors:
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2020 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus, superstippi@gmx.de
 *       Stefano Ceccherini, burton666@libero.it
 *       DarkWyrm, bpmagic@columbus.rr.com
 *       Marc Flerackers, mflerackers@androme.be
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file ScrollBar.cpp
 * @brief Implementation of BScrollBar, a scroll bar control
 *
 * BScrollBar implements horizontal and vertical scroll bars that control the
 * position of an associated BScrollView target. It manages thumb size, step
 * sizes, and range, keeping them synchronized with the target view's content
 * area.
 *
 * @see BScrollView, BView, BControlLook
 */


#include <ScrollBar.h>

#include <algorithm>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ControlLook.h>
#include <LayoutUtils.h>
#include <Message.h>
#include <OS.h>
#include <Shape.h>
#include <Window.h>

#include <binary_compatibility/Interface.h>


//#define TRACE_SCROLLBAR
#ifdef TRACE_SCROLLBAR
#	define TRACE(x...) printf(x)
#else
#	define TRACE(x...)
#endif


/**
 * @brief Logical directions used when drawing or identifying arrow buttons.
 *
 * Values are passed to BControlLook drawing helpers and used internally to
 * map a screen region to the arrow it represents.
 */
typedef enum {
	ARROW_LEFT = 0,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	ARROW_NONE
} arrow_direction;


/** @brief Maximum permitted thumb (knob) size in pixels. */
#define SCROLL_BAR_MAXIMUM_KNOB_SIZE	50
/** @brief Minimum permitted thumb (knob) size in pixels. */
#define SCROLL_BAR_MINIMUM_KNOB_SIZE	9

/** @brief Scripting command code: scroll the bar by a value. */
#define SBC_SCROLLBYVALUE	0
/** @brief Scripting command code: toggle double-arrow mode. */
#define SBC_SETDOUBLE		1
/** @brief Scripting command code: set proportional thumb mode. */
#define SBC_SETPROPORTIONAL	2
/** @brief Scripting command code: set the visual style. */
#define SBC_SETSTYLE		3

// Quick constants for determining which arrow is down and are defined with
// respect to double arrow mode. ARROW1 and ARROW4 refer to the outer pair of
// arrows and ARROW2 and ARROW3 refer to the inner ones. ARROW1 points left/up
// and ARROW4 points right/down.
/** @brief Index of the first (outermost left/top) arrow button. */
#define ARROW1	0
/** @brief Index of the second (inner left/top) arrow button used in double-arrow mode. */
#define ARROW2	1
/** @brief Index of the third (inner right/bottom) arrow button used in double-arrow mode. */
#define ARROW3	2
/** @brief Index of the fourth (outermost right/bottom) arrow button. */
#define ARROW4	3
/** @brief Sentinel value indicating the drag thumb is the active element. */
#define THUMB	4
/** @brief Sentinel value indicating no arrow button is currently active. */
#define NOARROW	-1


/** @brief Initial delay in microseconds before auto-scroll repeating begins. */
static const bigtime_t kRepeatDelay = 300000;


// Because the R5 version kept a lot of data on server-side, we need to kludge
// our way into binary compatibility

/**
 * @brief Private implementation data for BScrollBar.
 *
 * Holds all state that the original BeOS R5 ABI kept server-side, including
 * thumb geometry, the auto-scroll repeater thread, arrow-button enabled
 * states, and the system scroll_bar_info settings.  One instance is owned
 * by every BScrollBar object.
 *
 * @note This class must not be exposed in the public header because its size
 *       is part of the binary-compatibility strategy.
 */
class BScrollBar::Private {
public:
	/**
	 * @brief Construct the private data block for a given scroll bar.
	 *
	 * Reads system-wide scroll_bar_info settings and scales the minimum knob
	 * size to the current plain-font size so the thumb remains legible at
	 * different font sizes.
	 *
	 * @param scrollBar The owning BScrollBar; stored as a back-pointer.
	 */
	Private(BScrollBar* scrollBar)
	:
	fScrollBar(scrollBar),
	fEnabled(true),
	fRepeaterThread(-1),
	fExitRepeater(false),
	fRepeaterDelay(0),
	fThumbFrame(0.0, 0.0, -1.0, -1.0),
	fDoRepeat(false),
	fClickOffset(0.0, 0.0),
	fThumbInc(0.0),
	fStopValue(0.0),
	fUpArrowsEnabled(true),
	fDownArrowsEnabled(true),
	fBorderHighlighted(false),
	fButtonDown(NOARROW)
	{
#ifdef TEST_MODE
		fScrollBarInfo.proportional = true;
		fScrollBarInfo.double_arrows = true;
		fScrollBarInfo.knob = 0;
		fScrollBarInfo.min_knob_size = 15;
#else
		get_scroll_bar_info(&fScrollBarInfo);
#endif

		fScrollBarInfo.min_knob_size = (int32)(fScrollBarInfo.min_knob_size *
				(be_plain_font->Size() / 12.0f));
	}

	/**
	 * @brief Destroy the private data block.
	 *
	 * Signals the auto-scroll repeater thread to exit and waits for it to
	 * terminate before returning, ensuring no dangling thread accesses the
	 * deleted object.
	 */
	~Private()
	{
		if (fRepeaterThread >= 0) {
			status_t dummy;
			fExitRepeater = true;
			wait_for_thread(fRepeaterThread, &dummy);
		}
	}

	/**
	 * @brief Draw a single arrow button on the scroll bar.
	 *
	 * Delegates to BControlLook::DrawScrollBarButton() after computing the
	 * appropriate pressed/enabled flags.
	 *
	 * @param owner     The BScrollBar that owns this button.
	 * @param direction Which arrow direction to render.
	 * @param frame     Bounding rectangle for the button in the owner's
	 *                  coordinate system.
	 * @param down      Pass true when the button is in the pressed state.
	 */
	void DrawScrollBarButton(BScrollBar* owner, arrow_direction direction,
		BRect frame, bool down = false);

	/**
	 * @brief Thread entry point for the auto-scroll repeater.
	 *
	 * Called by spawn_thread(); casts \a data to a Private pointer and
	 * delegates to ButtonRepeaterThread().
	 *
	 * @param data Pointer to the owning BScrollBar::Private instance.
	 * @return Always returns 0.
	 */
	static int32 button_repeater_thread(void* data);

	/**
	 * @brief Auto-scroll repeater thread body.
	 *
	 * Waits for the initial kRepeatDelay to elapse, then repeatedly adjusts
	 * the scroll-bar value by fThumbInc every 25 ms until fExitRepeater is
	 * set.  The loop also handles page-scroll stopping logic when the thumb
	 * would pass the cursor position.
	 *
	 * @return Always returns 0.
	 */
	int32 ButtonRepeaterThread();

	/** @brief Back-pointer to the owning BScrollBar. */
	BScrollBar*			fScrollBar;
	/** @brief Whether the scroll bar is interactive (mirrors window-active state). */
	bool				fEnabled;

	// TODO: This should be a static, initialized by
	// _init_interface_kit() at application startup-time,
	// like BMenu::sMenuInfo
	/** @brief Cached system-wide scroll bar preferences (double arrows, knob style, etc.). */
	scroll_bar_info		fScrollBarInfo;

	/** @brief Thread ID of the active auto-scroll repeater, or -1 when idle. */
	thread_id			fRepeaterThread;
	/** @brief Set to true to signal the repeater thread to exit cleanly. */
	volatile bool		fExitRepeater;
	/** @brief Absolute timestamp (system_time) after which repeating begins. */
	bigtime_t			fRepeaterDelay;

	/** @brief Current bounding rectangle of the drag thumb in view coordinates. */
	BRect				fThumbFrame;
	/** @brief Whether the repeater thread should fire its next scroll step. */
	volatile bool		fDoRepeat;
	/** @brief Offset from the cursor to the thumb's top-left corner at drag start. */
	BPoint				fClickOffset;

	/** @brief Scroll amount applied per repeater tick (positive = forward). */
	float				fThumbInc;
	/** @brief Value at which page-scroll auto-repeat should stop. */
	float				fStopValue;

	/** @brief Whether the up/left arrow buttons are currently enabled. */
	bool				fUpArrowsEnabled;
	/** @brief Whether the down/right arrow buttons are currently enabled. */
	bool				fDownArrowsEnabled;

	/** @brief Whether the scroll bar's border is drawn in the focused/highlighted style. */
	bool				fBorderHighlighted;

	/** @brief Index of the arrow button currently pressed, or NOARROW / THUMB. */
	int8				fButtonDown;
};


// This thread is spawned when a button is initially pushed and repeatedly scrolls
// the scrollbar by a little bit after a short delay

/**
 * @brief Static thread entry point for the auto-scroll repeater.
 *
 * Forwards execution to the instance method ButtonRepeaterThread() on the
 * BScrollBar::Private object pointed to by \a data.
 *
 * @param data Opaque pointer cast to BScrollBar::Private*.
 * @return Return value of ButtonRepeaterThread().
 */
int32
BScrollBar::Private::button_repeater_thread(void* data)
{
	BScrollBar::Private* privateData = (BScrollBar::Private*)data;
	return privateData->ButtonRepeaterThread();
}


/**
 * @brief Auto-scroll repeater thread body.
 *
 * Waits until fRepeaterDelay elapses, then loops every 25 ms: while
 * fDoRepeat is true it advances the scroll-bar value by fThumbInc.  When
 * scrolling through the empty trough (NOARROW mode) it stops as soon as the
 * thumb reaches fStopValue.  The thread terminates when fExitRepeater is set,
 * then clears fRepeaterThread to -1 inside the looper lock so the next mouse
 * press can start a fresh thread.
 *
 * @return Always 0.
 */
int32
BScrollBar::Private::ButtonRepeaterThread()
{
	// Wait a bit before auto scrolling starts. As long as the user releases
	// and presses the button again while the repeat delay has not yet
	// triggered, the value is pushed into the future, so we need to loop such
	// that repeating starts at exactly the correct delay after the last
	// button press.
	while (fRepeaterDelay > system_time() && !fExitRepeater)
		snooze_until(fRepeaterDelay, B_SYSTEM_TIMEBASE);

	// repeat loop
	while (!fExitRepeater) {
		if (fScrollBar->LockLooper()) {
			if (fDoRepeat) {
				float value = fScrollBar->Value() + fThumbInc;
				if (fButtonDown == NOARROW) {
					// in this case we want to stop when we're under the mouse
					if (fThumbInc > 0.0 && value <= fStopValue)
						fScrollBar->SetValue(value);
					if (fThumbInc < 0.0 && value >= fStopValue)
						fScrollBar->SetValue(value);
				} else
					fScrollBar->SetValue(value);
			}

			fScrollBar->UnlockLooper();
		}

		snooze(25000);
	}

	// tell scrollbar we're gone
	if (fScrollBar->LockLooper()) {
		fRepeaterThread = -1;
		fScrollBar->UnlockLooper();
	}

	return 0;
}


//	#pragma mark - BScrollBar


/**
 * @brief Construct a layout-unaware BScrollBar with an explicit frame.
 *
 * Creates a scroll bar in the traditional (non-layout) mode.  The resizing
 * mode is set automatically: B_FOLLOW_TOP_BOTTOM|B_FOLLOW_RIGHT for vertical
 * bars, B_FOLLOW_LEFT_RIGHT|B_FOLLOW_BOTTOM for horizontal ones.
 *
 * @param frame     Position and size of the scroll bar in the parent's
 *                  coordinate system.
 * @param name      View name; may be NULL.
 * @param target    View to scroll; may be NULL and set later via SetTarget().
 * @param min       Minimum scroll value (usually 0).
 * @param max       Maximum scroll value.
 * @param direction B_HORIZONTAL or B_VERTICAL.
 * @see SetTarget()
 * @see SetRange()
 */
BScrollBar::BScrollBar(BRect frame, const char* name, BView* target,
	float min, float max, orientation direction)
	:
	BView(frame, name, B_FOLLOW_NONE,
		B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_FRAME_EVENTS),
	fMin(min),
	fMax(max),
	fSmallStep(1.0f),
	fLargeStep(10.0f),
	fValue(0),
	fProportion(0.0f),
	fTarget(NULL),
	fOrientation(direction)
{
	SetViewColor(B_TRANSPARENT_COLOR);

	fPrivateData = new BScrollBar::Private(this);

	SetTarget(target);
	SetEventMask(B_NO_POINTER_HISTORY);

	_UpdateThumbFrame();
	_UpdateArrowButtons();

	SetResizingMode(direction == B_VERTICAL
		? B_FOLLOW_TOP_BOTTOM | B_FOLLOW_RIGHT
		: B_FOLLOW_LEFT_RIGHT | B_FOLLOW_BOTTOM);
}


/**
 * @brief Construct a layout-aware BScrollBar without an explicit frame.
 *
 * Use this constructor when the scroll bar will be managed by a BLayout.
 * The frame is determined at layout time; explicit resizing modes are not set.
 *
 * @param name      View name; may be NULL.
 * @param target    View to scroll; may be NULL and set later via SetTarget().
 * @param min       Minimum scroll value (usually 0).
 * @param max       Maximum scroll value.
 * @param direction B_HORIZONTAL or B_VERTICAL.
 * @see SetTarget()
 * @see SetRange()
 */
BScrollBar::BScrollBar(const char* name, BView* target,
	float min, float max, orientation direction)
	:
	BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_FRAME_EVENTS),
	fMin(min),
	fMax(max),
	fSmallStep(1.0f),
	fLargeStep(10.0f),
	fValue(0),
	fProportion(0.0f),
	fTarget(NULL),
	fOrientation(direction)
{
	SetViewColor(B_TRANSPARENT_COLOR);

	fPrivateData = new BScrollBar::Private(this);

	SetTarget(target);
	SetEventMask(B_NO_POINTER_HISTORY);

	_UpdateThumbFrame();
	_UpdateArrowButtons();
}


/**
 * @brief Reconstruct a BScrollBar from an archived BMessage.
 *
 * Restores range, step sizes, current value, orientation, and proportion
 * from the archive fields written by Archive().  The scroll target is not
 * archived and must be re-attached after instantiation if needed.
 *
 * @param data Archive message produced by Archive().
 * @see Archive()
 * @see Instantiate()
 */
BScrollBar::BScrollBar(BMessage* data)
	:
	BView(data),
	fTarget(NULL)
{
	fPrivateData = new BScrollBar::Private(this);

	// TODO: Does the BeOS implementation try to find the target
	// by name again? Does it archive the name at all?
	if (data->FindFloat("_range", 0, &fMin) < B_OK)
		fMin = 0.0f;

	if (data->FindFloat("_range", 1, &fMax) < B_OK)
		fMax = 0.0f;

	if (data->FindFloat("_steps", 0, &fSmallStep) < B_OK)
		fSmallStep = 1.0f;

	if (data->FindFloat("_steps", 1, &fLargeStep) < B_OK)
		fLargeStep = 10.0f;

	if (data->FindFloat("_val", &fValue) < B_OK)
		fValue = 0.0;

	int32 orientation;
	if (data->FindInt32("_orient", &orientation) < B_OK) {
		fOrientation = B_VERTICAL;
	} else
		fOrientation = (enum orientation)orientation;

	if ((Flags() & B_SUPPORTS_LAYOUT) == 0) {
		// just to make sure
		SetResizingMode(fOrientation == B_VERTICAL
			? B_FOLLOW_TOP_BOTTOM | B_FOLLOW_RIGHT
			: B_FOLLOW_LEFT_RIGHT | B_FOLLOW_BOTTOM);
	}

	if (data->FindFloat("_prop", &fProportion) < B_OK)
		fProportion = 0.0;

	_UpdateThumbFrame();
	_UpdateArrowButtons();
}


/**
 * @brief Destroy the BScrollBar.
 *
 * Detaches this scroll bar from its target view (clearing the target's
 * fVerScroller or fHorScroller pointer) and then releases all private data,
 * including the auto-scroll repeater thread if it is still running.
 *
 * @see SetTarget()
 */
BScrollBar::~BScrollBar()
{
	SetTarget((BView*)NULL);
	delete fPrivateData;
}


/**
 * @brief Create a new BScrollBar from an archived BMessage.
 *
 * @param data Archive message, typically obtained from BMessage::Instantiate().
 * @return A heap-allocated BScrollBar on success, or NULL if \a data does
 *         not represent a valid BScrollBar archive.
 * @see Archive()
 */
BArchivable*
BScrollBar::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "BScrollBar"))
		return new BScrollBar(data);
	return NULL;
}


/**
 * @brief Archive the BScrollBar state into a BMessage.
 *
 * Stores the scroll range ("_range"), small and large step sizes ("_steps"),
 * current value ("_val"), orientation ("_orient"), and thumb proportion
 * ("_prop").  The scroll target is not archived.
 *
 * @param data  Message to write the archive fields into.
 * @param deep  If true, child views are recursively archived (passed to
 *              BView::Archive()).
 * @return B_OK on success, or the first error code encountered.
 * @see Instantiate()
 */
status_t
BScrollBar::Archive(BMessage* data, bool deep) const
{
	status_t err = BView::Archive(data, deep);
	if (err != B_OK)
		return err;

	err = data->AddFloat("_range", fMin);
	if (err != B_OK)
		return err;

	err = data->AddFloat("_range", fMax);
	if (err != B_OK)
		return err;

	err = data->AddFloat("_steps", fSmallStep);
	if (err != B_OK)
		return err;

	err = data->AddFloat("_steps", fLargeStep);
	if (err != B_OK)
		return err;

	err = data->AddFloat("_val", fValue);
	if (err != B_OK)
		return err;

	err = data->AddInt32("_orient", (int32)fOrientation);
	if (err != B_OK)
		return err;

	err = data->AddFloat("_prop", fProportion);

	return err;
}


/**
 * @brief Called after all descendants have been attached to the window.
 *
 * Delegates to BView::AllAttached(). Override in a subclass if post-attach
 * initialisation that depends on the complete view hierarchy is required.
 */
void
BScrollBar::AllAttached()
{
	BView::AllAttached();
}


/**
 * @brief Called after all descendants have been detached from the window.
 *
 * Delegates to BView::AllDetached(). Override in a subclass for any
 * cleanup that must happen after the full hierarchy has been removed.
 */
void
BScrollBar::AllDetached()
{
	BView::AllDetached();
}


/**
 * @brief Called when the view is added to a window's view hierarchy.
 *
 * Delegates to BView::AttachedToWindow(). Subclasses may override this to
 * perform window-specific initialisation.
 *
 * @see DetachedFromWindow()
 */
void
BScrollBar::AttachedToWindow()
{
	BView::AttachedToWindow();
}


/**
 * @brief Called when the view is removed from a window's view hierarchy.
 *
 * Delegates to BView::DetachedFromWindow(). Subclasses may override this to
 * release window-specific resources.
 *
 * @see AttachedToWindow()
 */
void
BScrollBar::DetachedFromWindow()
{
	BView::DetachedFromWindow();
}


/**
 * @brief Paint the scroll bar into the current update region.
 *
 * Draws, in order: the outer border, all arrow buttons (one or two pairs
 * depending on the double-arrow setting), the trough background on both
 * sides of the thumb, and finally the thumb itself.  All drawing is
 * delegated to the current be_control_look instance so that the appearance
 * follows the active GUI theme.
 *
 * @param updateRect  The invalid rectangle that needs to be repainted,
 *                    in the view's own coordinate system.
 */
void
BScrollBar::Draw(BRect updateRect)
{
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color text = ui_color(B_PANEL_TEXT_COLOR);

	uint32 flags = 0;
	bool scrollingEnabled = fMin < fMax
		&& fProportion >= 0.0f && fProportion < 1.0f;
	if (scrollingEnabled)
		flags |= BControlLook::B_PARTIALLY_ACTIVATED;

	if (!fPrivateData->fEnabled || !scrollingEnabled)
		flags |= BControlLook::B_DISABLED;

	bool isFocused = fPrivateData->fBorderHighlighted;
	if (isFocused)
		flags |= BControlLook::B_FOCUSED;

	bool doubleArrows = _DoubleArrows();

	// draw border
	BRect rect = Bounds();
	be_control_look->DrawScrollBarBorder(this, rect, updateRect, base, flags,
		fOrientation);

	// inset past border
	rect.InsetBy(1, 1);

	// draw arrows buttons
	BRect thumbBG = rect;
	if (fOrientation == B_HORIZONTAL) {
		BRect buttonFrame(rect.left, rect.top,
			rect.left + rect.Height(), rect.bottom);

		be_control_look->DrawScrollBarButton(this, buttonFrame, updateRect,
			base, text, flags | (fPrivateData->fButtonDown == ARROW1
				? BControlLook::B_ACTIVATED : 0),
			BControlLook::B_LEFT_ARROW, fOrientation,
			fPrivateData->fButtonDown == ARROW1);

		if (doubleArrows) {
			buttonFrame.OffsetBy(rect.Height() + 1, 0.0f);
			be_control_look->DrawScrollBarButton(this, buttonFrame, updateRect,
				base, text, flags | (fPrivateData->fButtonDown == ARROW2
					? BControlLook::B_ACTIVATED : 0),
				BControlLook::B_RIGHT_ARROW, fOrientation,
				fPrivateData->fButtonDown == ARROW2);

			buttonFrame.OffsetTo(rect.right - ((rect.Height() * 2) + 1),
				rect.top);
			be_control_look->DrawScrollBarButton(this, buttonFrame, updateRect,
				base, text, flags | (fPrivateData->fButtonDown == ARROW3
					? BControlLook::B_ACTIVATED : 0),
				BControlLook::B_LEFT_ARROW, fOrientation,
				fPrivateData->fButtonDown == ARROW3);

			thumbBG.left += rect.Height() * 2 + 2;
			thumbBG.right -= rect.Height() * 2 + 2;
		} else {
			thumbBG.left += rect.Height() + 1;
			thumbBG.right -= rect.Height() + 1;
		}

		buttonFrame.OffsetTo(rect.right - rect.Height(), rect.top);
		be_control_look->DrawScrollBarButton(this, buttonFrame, updateRect,
			base, text, flags | (fPrivateData->fButtonDown == ARROW4
				? BControlLook::B_ACTIVATED : 0),
			BControlLook::B_RIGHT_ARROW, fOrientation,
			fPrivateData->fButtonDown == ARROW4);
	} else {
		BRect buttonFrame(rect.left, rect.top, rect.right,
			rect.top + rect.Width());

		be_control_look->DrawScrollBarButton(this, buttonFrame, updateRect,
			base, text, flags | (fPrivateData->fButtonDown == ARROW1
				? BControlLook::B_ACTIVATED : 0),
			BControlLook::B_UP_ARROW, fOrientation,
			fPrivateData->fButtonDown == ARROW1);

		if (doubleArrows) {
			buttonFrame.OffsetBy(0, rect.Width() + 1);
			be_control_look->DrawScrollBarButton(this, buttonFrame,
				updateRect, base, text, flags | (fPrivateData->fButtonDown == ARROW2
					? BControlLook::B_ACTIVATED : 0),
				BControlLook::B_DOWN_ARROW, fOrientation,
				fPrivateData->fButtonDown == ARROW2);

			buttonFrame.OffsetTo(rect.left, rect.bottom
				- ((rect.Width() * 2) + 1));
			be_control_look->DrawScrollBarButton(this, buttonFrame,
				updateRect, base, text, flags | (fPrivateData->fButtonDown == ARROW3
					? BControlLook::B_ACTIVATED : 0),
				BControlLook::B_UP_ARROW, fOrientation,
				fPrivateData->fButtonDown == ARROW3);

			thumbBG.top += rect.Width() * 2 + 2;
			thumbBG.bottom -= rect.Width() * 2 + 2;
		} else {
			thumbBG.top += rect.Width() + 1;
			thumbBG.bottom -= rect.Width() + 1;
		}

		buttonFrame.OffsetTo(rect.left, rect.bottom - rect.Width());
		be_control_look->DrawScrollBarButton(this, buttonFrame, updateRect,
			base, text, flags | (fPrivateData->fButtonDown == ARROW4
				? BControlLook::B_ACTIVATED : 0),
			BControlLook::B_DOWN_ARROW, fOrientation,
			fPrivateData->fButtonDown == ARROW4);
	}

	// fill background besides the thumb
	rect = fPrivateData->fThumbFrame;
	if (fOrientation == B_HORIZONTAL) {
		BRect leftOfThumb(thumbBG.left, thumbBG.top,
			rect.left - 1, thumbBG.bottom);
		BRect rightOfThumb(rect.right + 1, thumbBG.top,
			thumbBG.right, thumbBG.bottom);
		be_control_look->DrawScrollBarBackground(this, leftOfThumb,
			rightOfThumb, updateRect, base, flags, fOrientation);
	} else {
		BRect topOfThumb(thumbBG.left, thumbBG.top,
			thumbBG.right, rect.top - 1);
		BRect bottomOfThumb(thumbBG.left, rect.bottom + 1,
			thumbBG.right, thumbBG.bottom);
		be_control_look->DrawScrollBarBackground(this, topOfThumb,
			bottomOfThumb, updateRect, base, flags, fOrientation);
	}

	// draw thumb
	rect = fPrivateData->fThumbFrame;
	be_control_look->DrawScrollBarThumb(this, rect, updateRect,
		ui_color(B_SCROLL_BAR_THUMB_COLOR), flags, fOrientation,
		fPrivateData->fScrollBarInfo.knob);
}


/**
 * @brief Called when the view's position within its parent changes.
 *
 * Delegates to BView::FrameMoved(). Override in a subclass if the scroll
 * bar needs to respond to position changes.
 *
 * @param newPosition The view's new left-top corner in the parent's
 *                    coordinate system.
 */
void
BScrollBar::FrameMoved(BPoint newPosition)
{
	BView::FrameMoved(newPosition);
}


/**
 * @brief Called when the view's size changes.
 *
 * Recalculates the thumb frame to reflect the new bar length so that the
 * thumb stays correctly positioned and proportioned after a resize.
 *
 * @param newWidth  New width of the view.
 * @param newHeight New height of the view.
 */
void
BScrollBar::FrameResized(float newWidth, float newHeight)
{
	_UpdateThumbFrame();
}


/**
 * @brief Handle messages delivered to the scroll bar.
 *
 * Processes two message codes beyond the default BView behaviour:
 * - B_VALUE_CHANGED: reads the "value" int32 field and calls ValueChanged().
 * - B_MOUSE_WHEEL_CHANGED: converts wheel deltas to a scroll step and calls
 *   ScrollWithMouseWheelDelta() (needed because BView's default handler only
 *   forwards wheel events to attached scroll bars, which a scroll bar itself
 *   does not have).
 *
 * All other messages are passed to BView::MessageReceived().
 *
 * @param message The incoming message.
 */
void
BScrollBar::MessageReceived(BMessage* message)
{
	switch(message->what) {
		case B_VALUE_CHANGED:
		{
			int32 value;
			if (message->FindInt32("value", &value) == B_OK)
				ValueChanged(value);

			break;
		}

		case B_MOUSE_WHEEL_CHANGED:
		{
			// Must handle this here since BView checks for the existence of
			// scrollbars, which a scrollbar itself does not have
			float deltaX = 0.0f;
			float deltaY = 0.0f;
			message->FindFloat("be:wheel_delta_x", &deltaX);
			message->FindFloat("be:wheel_delta_y", &deltaY);

			if (deltaX == 0.0f && deltaY == 0.0f)
				break;

			if (deltaX != 0.0f && deltaY == 0.0f)
				deltaY = deltaX;

			ScrollWithMouseWheelDelta(this, deltaY);
		}

		default:
			BView::MessageReceived(message);
	}
}


/**
 * @brief Handle a mouse-button press inside the scroll bar.
 *
 * Determines what was clicked — the thumb, an arrow button, or the empty
 * trough — and begins the appropriate action:
 * - Secondary button anywhere: jump to the clicked position (absolute scroll).
 * - Primary button on thumb: begin thumb drag.
 * - Primary button on an arrow: scroll by one step and start the auto-scroll
 *   repeater thread.
 * - Primary button in the trough: page-scroll toward the click and start the
 *   repeater, which stops when the thumb reaches the cursor.
 *
 * Holding Shift while clicking an arrow scrolls by fLargeStep instead of
 * fSmallStep.
 *
 * @param where Click position in the view's coordinate system.
 * @see MouseMoved()
 * @see MouseUp()
 */
void
BScrollBar::MouseDown(BPoint where)
{
	if (!fPrivateData->fEnabled || fMin == fMax)
		return;

	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);

	int32 buttons;
	if (Looper() == NULL || Looper()->CurrentMessage() == NULL
		|| Looper()->CurrentMessage()->FindInt32("buttons", &buttons) != B_OK) {
		buttons = B_PRIMARY_MOUSE_BUTTON;
	}

	if (buttons & B_SECONDARY_MOUSE_BUTTON) {
		// special absolute scrolling: move thumb to where we clicked
		fPrivateData->fButtonDown = THUMB;
		fPrivateData->fClickOffset
			= fPrivateData->fThumbFrame.LeftTop() - where;
		if (Orientation() == B_HORIZONTAL) {
			fPrivateData->fClickOffset.x
				= -fPrivateData->fThumbFrame.Width() / 2;
		} else {
			fPrivateData->fClickOffset.y
				= -fPrivateData->fThumbFrame.Height() / 2;
		}

		SetValue(_ValueFor(where + fPrivateData->fClickOffset));
		return;
	}

	// hit test for the thumb
	if (fPrivateData->fThumbFrame.Contains(where)) {
		fPrivateData->fButtonDown = THUMB;
		fPrivateData->fClickOffset
			= fPrivateData->fThumbFrame.LeftTop() - where;
		Invalidate(fPrivateData->fThumbFrame);
		return;
	}

	// hit test for arrows or empty area
	float scrollValue = 0.0;

	// pressing the shift key scrolls faster
	float buttonStepSize
		= (modifiers() & B_SHIFT_KEY) != 0 ? fLargeStep : fSmallStep;

	fPrivateData->fButtonDown = _ButtonFor(where);
	switch (fPrivateData->fButtonDown) {
		case ARROW1:
			scrollValue = -buttonStepSize;
			break;

		case ARROW2:
			scrollValue = buttonStepSize;
			break;

		case ARROW3:
			scrollValue = -buttonStepSize;
			break;

		case ARROW4:
			scrollValue = buttonStepSize;
			break;

		case NOARROW:
			// we hit the empty area, figure out which side of the thumb
			if (fOrientation == B_VERTICAL) {
				if (where.y < fPrivateData->fThumbFrame.top)
					scrollValue = -fLargeStep;
				else
					scrollValue = fLargeStep;
			} else {
				if (where.x < fPrivateData->fThumbFrame.left)
					scrollValue = -fLargeStep;
				else
					scrollValue = fLargeStep;
			}
			_UpdateTargetValue(where);
			break;
	}
	if (scrollValue != 0.0) {
		SetValue(fValue + scrollValue);
		Invalidate(_ButtonRectFor(fPrivateData->fButtonDown));

		// launch the repeat thread
		if (fPrivateData->fRepeaterThread == -1) {
			fPrivateData->fExitRepeater = false;
			fPrivateData->fRepeaterDelay = system_time() + kRepeatDelay;
			fPrivateData->fThumbInc = scrollValue;
			fPrivateData->fDoRepeat = true;
			fPrivateData->fRepeaterThread = spawn_thread(
				fPrivateData->button_repeater_thread, "scroll repeater",
				B_NORMAL_PRIORITY, fPrivateData);
			resume_thread(fPrivateData->fRepeaterThread);
		} else {
			fPrivateData->fExitRepeater = false;
			fPrivateData->fRepeaterDelay = system_time() + kRepeatDelay;
			fPrivateData->fDoRepeat = true;
		}
	}
}


/**
 * @brief Handle mouse movement during a drag or hover.
 *
 * While the thumb is being dragged, maps the current cursor position to a
 * scroll value and calls SetValue().  While an arrow button is held, suspends
 * or resumes the auto-scroll repeater based on whether the cursor is still
 * inside the button's rectangle.  While performing a trough page-scroll,
 * updates the stop value so the thumb tracks the cursor.
 *
 * @param where       Current cursor position in view coordinates.
 * @param code        Transit code (B_INSIDE_VIEW, B_OUTSIDE_VIEW, etc.).
 * @param dragMessage Message associated with a drag-and-drop operation, or
 *                    NULL when none is in progress.
 * @see MouseDown()
 * @see MouseUp()
 */
void
BScrollBar::MouseMoved(BPoint where, uint32 code, const BMessage* dragMessage)
{
	if (!fPrivateData->fEnabled || fMin >= fMax || fProportion >= 1.0f
		|| fProportion < 0.0f) {
		return;
	}

	if (fPrivateData->fButtonDown != NOARROW) {
		if (fPrivateData->fButtonDown == THUMB) {
			SetValue(_ValueFor(where + fPrivateData->fClickOffset));
		} else {
			// suspend the repeating if the mouse is not over the button
			bool repeat = _ButtonRectFor(fPrivateData->fButtonDown).Contains(
				where);
			if (fPrivateData->fDoRepeat != repeat) {
				fPrivateData->fDoRepeat = repeat;
				Invalidate(_ButtonRectFor(fPrivateData->fButtonDown));
			}
		}
	} else {
		// update the value at which we want to stop repeating
		if (fPrivateData->fDoRepeat) {
			_UpdateTargetValue(where);
			// we might have to turn arround
			if ((fValue < fPrivateData->fStopValue
					&& fPrivateData->fThumbInc < 0)
				|| (fValue > fPrivateData->fStopValue
					&& fPrivateData->fThumbInc > 0)) {
				fPrivateData->fThumbInc = -fPrivateData->fThumbInc;
			}
		}
	}
}


/**
 * @brief Handle a mouse-button release inside the scroll bar.
 *
 * Invalidates the thumb or the previously pressed arrow button so it is
 * redrawn in its released state, then clears the active-button state and
 * signals the auto-scroll repeater thread to stop.
 *
 * @param where Release position in the view's coordinate system (unused but
 *              required by the BView override signature).
 * @see MouseDown()
 * @see MouseMoved()
 */
void
BScrollBar::MouseUp(BPoint where)
{
	if (fPrivateData->fButtonDown == THUMB)
		Invalidate(fPrivateData->fThumbFrame);
	else
		Invalidate(_ButtonRectFor(fPrivateData->fButtonDown));

	fPrivateData->fButtonDown = NOARROW;
	fPrivateData->fExitRepeater = true;
	fPrivateData->fDoRepeat = false;
}


/**
 * @brief Called when the owning window gains or loses activation.
 *
 * Enables or disables the scroll bar to match the window's active state and
 * triggers a full redraw so the visual appearance reflects the change.
 *
 * @param active True when the window becomes active, false when it loses focus.
 */
void
BScrollBar::WindowActivated(bool active)
{
	fPrivateData->fEnabled = active;
	Invalidate();
}


/**
 * @brief Set the current scroll position.
 *
 * Clamps \a value to [fMin, fMax], rounds it to the nearest integer, and
 * updates the thumb frame and arrow-button enabled states.  If the clamped
 * value differs from the current value, ValueChanged() is called so that the
 * target view is scrolled accordingly.
 *
 * NaN and infinite values are silently ignored.
 *
 * @param value Desired scroll position.
 * @see Value()
 * @see ValueChanged()
 * @see SetRange()
 */
void
BScrollBar::SetValue(float value)
{
	if (value > fMax)
		value = fMax;
	else if (value < fMin)
		value = fMin;
	else if (isnan(value) || isinf(value))
		return;

	value = roundf(value);
	if (value == fValue)
		return;

	TRACE("BScrollBar(%s)::SetValue(%.1f)\n", Name(), value);

	fValue = value;

	_UpdateThumbFrame();
	_UpdateArrowButtons();

	ValueChanged(fValue);
}


/**
 * @brief Return the current scroll position.
 *
 * @return The current value, always in the range [fMin, fMax].
 * @see SetValue()
 */
float
BScrollBar::Value() const
{
	return fValue;
}


/**
 * @brief Respond to a change in the scroll-bar value.
 *
 * Scrolls the target view (if any) so that its bounds origin matches
 * \a newValue along the bar's axis, then calls SetValue() to keep the
 * internal state consistent and synchronise the thumb position.
 *
 * This hook is called by SetValue() and also by MessageReceived() when a
 * B_VALUE_CHANGED message arrives from the target BScrollView.
 *
 * @param newValue The new scroll position.
 * @see SetValue()
 * @see SetTarget()
 */
void
BScrollBar::ValueChanged(float newValue)
{
	TRACE("BScrollBar(%s)::ValueChanged(%.1f)\n", Name(), newValue);

	if (fTarget != NULL) {
		// cache target bounds
		BRect targetBounds = fTarget->Bounds();
		// if vertical, check bounds top and scroll if different from newValue
		if (fOrientation == B_VERTICAL && targetBounds.top != newValue)
			fTarget->ScrollBy(0.0, newValue - targetBounds.top);

		// if horizontal, check bounds left and scroll if different from newValue
		if (fOrientation == B_HORIZONTAL && targetBounds.left != newValue)
			fTarget->ScrollBy(newValue - targetBounds.left, 0.0);
	}

	TRACE(" -> %.1f\n", newValue);

	SetValue(newValue);
}


/**
 * @brief Set the thumb size relative to the visible content area.
 *
 * \a value represents the ratio of the visible area to the total content
 * area: 1.0 means the entire content is visible (bar is disabled), 0.0 is a
 * special case that causes the thumb size to be derived from fLargeStep.
 * Values outside [0, 1] are clamped.
 *
 * If the change alters the bar's enabled/disabled state, the entire bar is
 * invalidated to trigger a full redraw.
 *
 * @param value Proportion in [0.0, 1.0].
 * @see Proportion()
 * @see SetRange()
 */
void
BScrollBar::SetProportion(float value)
{
	if (value < 0.0f)
		value = 0.0f;
	else if (value > 1.0f)
		value = 1.0f;

	if (value == fProportion)
		return;

	TRACE("BScrollBar(%s)::SetProportion(%.1f)\n", Name(), value);

	bool oldEnabled = fPrivateData->fEnabled && fMin < fMax
		&& fProportion < 1.0f && fProportion >= 0.0f;

	fProportion = value;

	bool newEnabled = fPrivateData->fEnabled && fMin < fMax
		&& fProportion < 1.0f && fProportion >= 0.0f;

	_UpdateThumbFrame();

	if (oldEnabled != newEnabled)
		Invalidate();
}


/**
 * @brief Return the current thumb proportion.
 *
 * @return The last value set by SetProportion(), in [0.0, 1.0].
 * @see SetProportion()
 */
float
BScrollBar::Proportion() const
{
	return fProportion;
}


/**
 * @brief Set the minimum and maximum scroll values.
 *
 * If \a min > \a max, or either value is NaN or infinite, both are reset to
 * 0.  Both values are rounded to the nearest integer.  If the current value
 * falls outside the new range, SetValue() is called to clamp and synchronise
 * it; otherwise only the thumb frame is recalculated.
 *
 * @param min New minimum scroll value.
 * @param max New maximum scroll value.
 * @see GetRange()
 * @see SetValue()
 */
void
BScrollBar::SetRange(float min, float max)
{
	if (min > max || isnanf(min) || isnanf(max)
		|| isinff(min) || isinff(max)) {
		min = 0.0f;
		max = 0.0f;
	}

	min = roundf(min);
	max = roundf(max);

	if (fMin == min && fMax == max)
		return;

	TRACE("BScrollBar(%s)::SetRange(min=%.1f, max=%.1f)\n", Name(), min, max);

	fMin = min;
	fMax = max;

	if (fValue < fMin || fValue > fMax)
		SetValue(fValue);
	else
		_UpdateThumbFrame();
}


/**
 * @brief Retrieve the current scroll range.
 *
 * Either output pointer may be NULL if the caller does not need that value.
 *
 * @param min Receives the minimum scroll value; may be NULL.
 * @param max Receives the maximum scroll value; may be NULL.
 * @see SetRange()
 */
void
BScrollBar::GetRange(float* min, float* max) const
{
	if (min != NULL)
		*min = fMin;

	if (max != NULL)
		*max = fMax;
}


/**
 * @brief Set the small and large scroll step sizes.
 *
 * The small step is used when an arrow button is clicked; the large step is
 * used for page scrolling (trough clicks) and as the default proportion base
 * when SetProportion() has never been called.  Both values are rounded to
 * the nearest integer.
 *
 * @param smallStep Amount to scroll per arrow-button click.
 * @param largeStep Amount to scroll per trough click (page scroll).
 * @note Unlike R5, steps may be set before the view is attached to a window.
 * @see GetSteps()
 */
void
BScrollBar::SetSteps(float smallStep, float largeStep)
{
	// Under R5, steps can be set only after being attached to a window,
	// probably because the data is kept server-side. We'll just remove
	// that limitation... :P

	// The BeBook also says that we need to specify an integer value even
	// though the step values are floats. For the moment, we'll just make
	// sure that they are integers
	smallStep = roundf(smallStep);
	largeStep = roundf(largeStep);
	if (fSmallStep == smallStep && fLargeStep == largeStep)
		return;

	TRACE("BScrollBar(%s)::SetSteps(small=%.1f, large=%.1f)\n", Name(),
		smallStep, largeStep);

	fSmallStep = smallStep;
	fLargeStep = largeStep;

	if (fProportion == 0.0) {
		// special case, proportion is based on fLargeStep if it was never
		// set, so it means we need to invalidate here
		_UpdateThumbFrame();
		Invalidate();
	}

	// TODO: test use of fractional values and make them work properly if
	// they don't
}


/**
 * @brief Retrieve the current small and large scroll step sizes.
 *
 * Either output pointer may be NULL if the caller does not need that value.
 *
 * @param smallStep Receives the small step value; may be NULL.
 * @param largeStep Receives the large step value; may be NULL.
 * @see SetSteps()
 */
void
BScrollBar::GetSteps(float* smallStep, float* largeStep) const
{
	if (smallStep != NULL)
		*smallStep = fSmallStep;

	if (largeStep != NULL)
		*largeStep = fLargeStep;
}


/**
 * @brief Attach the scroll bar to a target view.
 *
 * Clears the back-pointer on the previous target (if any), then stores the
 * new target and sets its fVerScroller or fHorScroller pointer to this scroll
 * bar so that BScrollView and BView's wheel-scroll logic can find it.
 * Passing NULL detaches the scroll bar without setting a new target.
 *
 * @param target View to scroll, or NULL to detach.
 * @see Target()
 * @see SetTarget(const char*)
 */
void
BScrollBar::SetTarget(BView* target)
{
	if (fTarget) {
		// unset the previous target's scrollbar pointer
		if (fOrientation == B_VERTICAL)
			fTarget->fVerScroller = NULL;
		else
			fTarget->fHorScroller = NULL;
	}

	fTarget = target;
	if (fTarget) {
		if (fOrientation == B_VERTICAL)
			fTarget->fVerScroller = this;
		else
			fTarget->fHorScroller = this;
	}
}


/**
 * @brief Attach the scroll bar to a view identified by name.
 *
 * Looks up the named view in the owning window and, if found, attaches it as
 * the scroll target.  The scroll bar must already be attached to a window
 * before this overload is called; passing a NULL name is a no-op.
 *
 * @param targetName Name of the view to attach as the scroll target.
 * @note Requires the scroll bar to be attached to a window; calls debugger()
 *       if it is not.
 * @see Target()
 * @see SetTarget(BView*)
 */
void
BScrollBar::SetTarget(const char* targetName)
{
	// NOTE 1: BeOS implementation crashes for targetName == NULL
	// NOTE 2: BeOS implementation also does not modify the target
	// if it can't be found
	if (targetName == NULL)
		return;

	if (Window() == NULL)
		debugger("Method requires window and doesn't have one");

	BView* target = Window()->FindView(targetName);
	if (target != NULL)
		SetTarget(target);
}


/**
 * @brief Return the current scroll target.
 *
 * @return Pointer to the target BView, or NULL if none has been set.
 * @see SetTarget()
 */
BView*
BScrollBar::Target() const
{
	return fTarget;
}


/**
 * @brief Change the scroll bar's orientation at runtime.
 *
 * Switching between B_HORIZONTAL and B_VERTICAL invalidates the layout and
 * triggers a full repaint.  Has no effect if the orientation is unchanged.
 *
 * @param direction B_HORIZONTAL or B_VERTICAL.
 * @see Orientation()
 */
void
BScrollBar::SetOrientation(orientation direction)
{
	if (fOrientation == direction)
		return;

	fOrientation = direction;
	InvalidateLayout();
	Invalidate();
}


/**
 * @brief Return the scroll bar's current orientation.
 *
 * @return B_HORIZONTAL or B_VERTICAL.
 * @see SetOrientation()
 */
orientation
BScrollBar::Orientation() const
{
	return fOrientation;
}


/**
 * @brief Set whether the scroll bar's border is drawn in the highlighted style.
 *
 * The highlighted border is typically used when the scroll bar has keyboard
 * focus.  Only the single border pixel is invalidated, keeping the repaint
 * area minimal.
 *
 * @param highlight True to enable the focus highlight, false to remove it.
 * @return B_OK on success.
 */
status_t
BScrollBar::SetBorderHighlighted(bool highlight)
{
	if (fPrivateData->fBorderHighlighted == highlight)
		return B_OK;

	fPrivateData->fBorderHighlighted = highlight;

	BRect dirty(Bounds());
	if (fOrientation == B_HORIZONTAL)
		dirty.bottom = dirty.top;
	else
		dirty.right = dirty.left;

	Invalidate(dirty);

	return B_OK;
}


/**
 * @brief Report the preferred width and height for this scroll bar.
 *
 * For a vertical bar, the preferred width is the standard scroll-bar width
 * and the preferred height is the minimum usable height.  For a horizontal
 * bar the roles are swapped.  Either output pointer may be NULL.
 *
 * @param _width  Receives the preferred width; may be NULL.
 * @param _height Receives the preferred height; may be NULL.
 * @see MinSize()
 * @see PreferredSize()
 */
void
BScrollBar::GetPreferredSize(float* _width, float* _height)
{
	if (fOrientation == B_VERTICAL) {
		if (_width)
			*_width = be_control_look->GetScrollBarWidth(B_VERTICAL);

		if (_height)
			*_height = _MinSize().Height();
	} else if (fOrientation == B_HORIZONTAL) {
		if (_width)
			*_width = _MinSize().Width();

		if (_height)
			*_height = be_control_look->GetScrollBarWidth(B_HORIZONTAL);
	}
}


/**
 * @brief Resize the scroll bar to its preferred dimensions.
 *
 * Delegates to BView::ResizeToPreferred(), which queries GetPreferredSize()
 * and adjusts the view frame accordingly.
 */
void
BScrollBar::ResizeToPreferred()
{
	BView::ResizeToPreferred();
}



/**
 * @brief Grant or remove keyboard focus from this scroll bar.
 *
 * Delegates to BView::MakeFocus(). Override in a subclass if the scroll bar
 * needs custom focus-change behaviour (e.g., calling SetBorderHighlighted()).
 *
 * @param focus True to give focus, false to remove it.
 */
void
BScrollBar::MakeFocus(bool focus)
{
	BView::MakeFocus(focus);
}


/**
 * @brief Return the minimum layout size of the scroll bar.
 *
 * Composes the explicit minimum size (set by the programmer) with the
 * intrinsic minimum computed by _MinSize(), returning the larger of each
 * dimension.
 *
 * @return The minimum BSize for use by BLayout.
 * @see MaxSize()
 * @see PreferredSize()
 */
BSize
BScrollBar::MinSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), _MinSize());
}


/**
 * @brief Return the maximum layout size of the scroll bar.
 *
 * The fixed dimension (height for a horizontal bar, width for a vertical bar)
 * is set to the preferred size.  The scrolling dimension is set to
 * B_SIZE_UNLIMITED so the layout can expand the bar as much as desired.
 *
 * @return The maximum BSize for use by BLayout.
 * @see MinSize()
 * @see PreferredSize()
 */
BSize
BScrollBar::MaxSize()
{
	BSize maxSize;
	GetPreferredSize(&maxSize.width, &maxSize.height);
	if (fOrientation == B_HORIZONTAL)
		maxSize.width = B_SIZE_UNLIMITED;
	else
		maxSize.height = B_SIZE_UNLIMITED;
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), maxSize);
}


/**
 * @brief Return the preferred layout size of the scroll bar.
 *
 * Composes the explicit preferred size (if any) with the value returned by
 * GetPreferredSize().
 *
 * @return The preferred BSize for use by BLayout.
 * @see MinSize()
 * @see MaxSize()
 * @see GetPreferredSize()
 */
BSize
BScrollBar::PreferredSize()
{
	BSize preferredSize;
	GetPreferredSize(&preferredSize.width, &preferredSize.height);
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), preferredSize);
}


/**
 * @brief Report the scripting suites this scroll bar supports.
 *
 * Delegates to BView::GetSupportedSuites(). Subclasses that add scripting
 * properties should override this method and append their suite information
 * before calling the base implementation.
 *
 * @param message Message to fill with suite names and property descriptions.
 * @return B_OK on success, or an error code on failure.
 * @see ResolveSpecifier()
 */
status_t
BScrollBar::GetSupportedSuites(BMessage* message)
{
	return BView::GetSupportedSuites(message);
}


/**
 * @brief Resolve a scripting specifier to the appropriate handler.
 *
 * Delegates to BView::ResolveSpecifier(). Override in a subclass to handle
 * additional scroll-bar-specific scripting properties.
 *
 * @param message   The scripting message being resolved.
 * @param index     Index of the current specifier in the message.
 * @param specifier The specifier message extracted from \a message.
 * @param what      Specifier type constant (e.g. B_NAME_SPECIFIER).
 * @param property  Name of the property being accessed.
 * @return The BHandler that should handle the scripted operation.
 * @see GetSupportedSuites()
 */
BHandler*
BScrollBar::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 what, const char* property)
{
	return BView::ResolveSpecifier(message, index, specifier, what, property);
}


/**
 * @brief Execute a binary-compatibility perform operation.
 *
 * Handles the standard set of layout-related perform codes
 * (PERFORM_CODE_MIN_SIZE, PERFORM_CODE_MAX_SIZE, PERFORM_CODE_PREFERRED_SIZE,
 * PERFORM_CODE_LAYOUT_ALIGNMENT, PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH,
 * PERFORM_CODE_GET_HEIGHT_FOR_WIDTH, PERFORM_CODE_SET_LAYOUT,
 * PERFORM_CODE_LAYOUT_INVALIDATED, PERFORM_CODE_DO_LAYOUT) by calling the
 * corresponding BScrollBar virtual methods and writing the return value into
 * the appropriately typed data struct pointed to by \a _data.  Unknown codes
 * are forwarded to BView::Perform().
 *
 * @param code  Identifies the operation to perform (PERFORM_CODE_* constant).
 * @param _data Operation-specific input/output data struct; cast determined
 *              by \a code.
 * @return B_OK if the code was handled, or the result of BView::Perform()
 *         for unrecognised codes.
 */
status_t
BScrollBar::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_MIN_SIZE:
			((perform_data_min_size*)_data)->return_value
				= BScrollBar::MinSize();

			return B_OK;

		case PERFORM_CODE_MAX_SIZE:
			((perform_data_max_size*)_data)->return_value
				= BScrollBar::MaxSize();

			return B_OK;

		case PERFORM_CODE_PREFERRED_SIZE:
			((perform_data_preferred_size*)_data)->return_value
				= BScrollBar::PreferredSize();

			return B_OK;

		case PERFORM_CODE_LAYOUT_ALIGNMENT:
			((perform_data_layout_alignment*)_data)->return_value
				= BScrollBar::LayoutAlignment();

			return B_OK;

		case PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH:
			((perform_data_has_height_for_width*)_data)->return_value
				= BScrollBar::HasHeightForWidth();

			return B_OK;

		case PERFORM_CODE_GET_HEIGHT_FOR_WIDTH:
		{
			perform_data_get_height_for_width* data
				= (perform_data_get_height_for_width*)_data;
			BScrollBar::GetHeightForWidth(data->width, &data->min, &data->max,
				&data->preferred);

			return B_OK;
		}

		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BScrollBar::SetLayout(data->layout);

			return B_OK;
		}

		case PERFORM_CODE_LAYOUT_INVALIDATED:
		{
			perform_data_layout_invalidated* data
				= (perform_data_layout_invalidated*)_data;
			BScrollBar::LayoutInvalidated(data->descendants);

			return B_OK;
		}

		case PERFORM_CODE_DO_LAYOUT:
		{
			BScrollBar::DoLayout();

			return B_OK;
		}
	}

	return BView::Perform(code, _data);
}


void BScrollBar::_ReservedScrollBar1() {}
void BScrollBar::_ReservedScrollBar2() {}
void BScrollBar::_ReservedScrollBar3() {}
void BScrollBar::_ReservedScrollBar4() {}


BScrollBar&
BScrollBar::operator=(const BScrollBar&)
{
	return *this;
}


/**
 * @brief Determine whether double-arrow mode is active and there is room for it.
 *
 * Even if the system setting requests double arrows, this method returns false
 * when the bar is too short to display four arrow buttons plus a minimum-size
 * thumb, gracefully falling back to single-arrow mode.
 *
 * @return True when double-arrow buttons should be drawn and used for hit
 *         testing, false otherwise.
 */
bool
BScrollBar::_DoubleArrows() const
{
	if (!fPrivateData->fScrollBarInfo.double_arrows)
		return false;

	// if there is not enough room, switch to single arrows even though
	// double arrows is specified
	if (fOrientation == B_HORIZONTAL) {
		return Bounds().Width() > (Bounds().Height() + 1) * 4
			+ fPrivateData->fScrollBarInfo.min_knob_size * 2;
	} else {
		return Bounds().Height() > (Bounds().Width() + 1) * 4
			+ fPrivateData->fScrollBarInfo.min_knob_size * 2;
	}
}


/**
 * @brief Recalculate and update the thumb's bounding rectangle.
 *
 * Computes the thumb size from the current proportion (or from fLargeStep
 * when proportion is 0), clamps it to the minimum knob size, and then
 * positions the thumb within the trough based on the current value relative
 * to [fMin, fMax].  If the frame changes and the view is attached to a
 * window, the union of the old and new frames is invalidated so only the
 * affected area is redrawn.
 *
 * Called by SetValue(), SetProportion(), SetRange(), FrameResized(), and the
 * archive constructor.
 */
void
BScrollBar::_UpdateThumbFrame()
{
	BRect bounds = Bounds();
	bounds.InsetBy(1.0, 1.0);

	BRect oldFrame = fPrivateData->fThumbFrame;
	fPrivateData->fThumbFrame = bounds;
	float minSize = fPrivateData->fScrollBarInfo.min_knob_size;
	float maxSize;
	float buttonSize;

	// assume square buttons
	if (fOrientation == B_VERTICAL) {
		maxSize = bounds.Height();
		buttonSize = bounds.Width() + 1.0;
	} else {
		maxSize = bounds.Width();
		buttonSize = bounds.Height() + 1.0;
	}

	if (_DoubleArrows()) {
		// subtract the size of four buttons
		maxSize -= buttonSize * 4;
	} else {
		// subtract the size of two buttons
		maxSize -= buttonSize * 2;
	}
	// visual adjustments (room for darker line between thumb and buttons)
	maxSize--;

	float thumbSize;
	if (fPrivateData->fScrollBarInfo.proportional) {
		float proportion = fProportion;
		if (fMin >= fMax || proportion > 1.0 || proportion < 0.0)
			proportion = 1.0;

		if (proportion == 0.0) {
			// Special case a proportion of 0.0, use the large step value
			// in that case (NOTE: fMin == fMax already handled above)
			// This calculation is based on the assumption that "large step"
			// scrolls by one "page size".
			proportion = fLargeStep / (2 * (fMax - fMin));
			if (proportion > 1.0)
				proportion = 1.0;
		}
		thumbSize = maxSize * proportion;
		if (thumbSize < minSize)
			thumbSize = minSize;
	} else
		thumbSize = minSize;

	thumbSize = floorf(thumbSize + 0.5);
	thumbSize--;

	// the thumb can be scrolled within the remaining area "maxSize - thumbSize - 1.0"
	float offset = 0.0;
	if (fMax > fMin) {
		offset = floorf(((fValue - fMin) / (fMax - fMin))
			* (maxSize - thumbSize - 1.0));
	}

	if (_DoubleArrows()) {
		offset += buttonSize * 2;
	} else
		offset += buttonSize;

	// visual adjustments (room for darker line between thumb and buttons)
	offset++;

	if (fOrientation == B_VERTICAL) {
		fPrivateData->fThumbFrame.bottom = fPrivateData->fThumbFrame.top
			+ thumbSize;
		fPrivateData->fThumbFrame.OffsetBy(0.0, offset);
	} else {
		fPrivateData->fThumbFrame.right = fPrivateData->fThumbFrame.left
			+ thumbSize;
		fPrivateData->fThumbFrame.OffsetBy(offset, 0.0);
	}

	if (Window() != NULL && fPrivateData->fThumbFrame != oldFrame) {
		BRect invalid = oldFrame.IsValid()
			? oldFrame | fPrivateData->fThumbFrame
			: fPrivateData->fThumbFrame;
		// account for those two dark lines
		if (fOrientation == B_HORIZONTAL)
			invalid.InsetBy(-2.0, 0.0);
		else
			invalid.InsetBy(0.0, -2.0);

		Invalidate(invalid);
	}
}


/**
 * @brief Map a view-coordinate point to the corresponding scroll value.
 *
 * Converts the position of the thumb's top-left corner (as given by
 * \a where) into the scroll value it represents, accounting for arrow-button
 * sizes and double-arrow mode.  Used during thumb dragging and secondary-
 * button absolute scrolling.
 *
 * @param where  Point in the view's coordinate system, typically the current
 *               cursor position offset by fClickOffset.
 * @return The scroll value that corresponds to \a where, rounded to the
 *         nearest integer and in the range [fMin, fMax+1].
 */
float
BScrollBar::_ValueFor(BPoint where) const
{
	BRect bounds = Bounds();
	bounds.InsetBy(1.0f, 1.0f);

	float offset;
	float thumbSize;
	float maxSize;
	float buttonSize;

	if (fOrientation == B_VERTICAL) {
		offset = where.y;
		thumbSize = fPrivateData->fThumbFrame.Height();
		maxSize = bounds.Height();
		buttonSize = bounds.Width() + 1.0f;
	} else {
		offset = where.x;
		thumbSize = fPrivateData->fThumbFrame.Width();
		maxSize = bounds.Width();
		buttonSize = bounds.Height() + 1.0f;
	}

	if (_DoubleArrows()) {
		// subtract the size of four buttons
		maxSize -= buttonSize * 4;
		// convert point to inside of area between buttons
		offset -= buttonSize * 2;
	} else {
		// subtract the size of two buttons
		maxSize -= buttonSize * 2;
		// convert point to inside of area between buttons
		offset -= buttonSize;
	}
	// visual adjustments (room for darker line between thumb and buttons)
	maxSize--;
	offset++;

	return roundf(fMin + (offset / (maxSize - thumbSize)
		* (fMax - fMin + 1.0f)));
}


/**
 * @brief Identify which arrow button (if any) contains a given point.
 *
 * Performs hit-testing against all arrow-button rectangles in order:
 * ARROW1, then (in double-arrow mode) ARROW2 and ARROW3, then ARROW4.
 *
 * @param where Point to test in the view's coordinate system.
 * @return ARROW1, ARROW2, ARROW3, or ARROW4 if \a where is inside one of
 *         the buttons, or NOARROW if the point falls in the trough or outside
 *         the bar.
 * @see _ButtonRectFor()
 */
int32
BScrollBar::_ButtonFor(BPoint where) const
{
	BRect bounds = Bounds();
	bounds.InsetBy(1.0f, 1.0f);

	float buttonSize = fOrientation == B_VERTICAL
		? bounds.Width() + 1.0f
		: bounds.Height() + 1.0f;

	BRect rect(bounds.left, bounds.top,
		bounds.left + buttonSize, bounds.top + buttonSize);

	if (fOrientation == B_VERTICAL) {
		if (rect.Contains(where))
			return ARROW1;

		if (_DoubleArrows()) {
			rect.OffsetBy(0.0, buttonSize);
			if (rect.Contains(where))
				return ARROW2;

			rect.OffsetTo(bounds.left, bounds.bottom - 2 * buttonSize);
			if (rect.Contains(where))
				return ARROW3;
		}
		rect.OffsetTo(bounds.left, bounds.bottom - buttonSize);
		if (rect.Contains(where))
			return ARROW4;
	} else {
		if (rect.Contains(where))
			return ARROW1;

		if (_DoubleArrows()) {
			rect.OffsetBy(buttonSize, 0.0);
			if (rect.Contains(where))
				return ARROW2;

			rect.OffsetTo(bounds.right - 2 * buttonSize, bounds.top);
			if (rect.Contains(where))
				return ARROW3;
		}
		rect.OffsetTo(bounds.right - buttonSize, bounds.top);
		if (rect.Contains(where))
			return ARROW4;
	}

	return NOARROW;
}


/**
 * @brief Return the bounding rectangle of an arrow button.
 *
 * Computes the rectangle for the given ARROW1–ARROW4 index, adjusted for
 * the current orientation and double-arrow layout.  Results are undefined
 * for THUMB and NOARROW.
 *
 * @param button  Arrow index: ARROW1, ARROW2, ARROW3, or ARROW4.
 * @return The button's bounding rectangle in the view's coordinate system.
 * @see _ButtonFor()
 */
BRect
BScrollBar::_ButtonRectFor(int32 button) const
{
	BRect bounds = Bounds();
	bounds.InsetBy(1.0f, 1.0f);

	float buttonSize = fOrientation == B_VERTICAL
		? bounds.Width() + 1.0f
		: bounds.Height() + 1.0f;

	BRect rect(bounds.left, bounds.top,
		bounds.left + buttonSize - 1.0f, bounds.top + buttonSize - 1.0f);

	if (fOrientation == B_VERTICAL) {
		switch (button) {
			case ARROW1:
				break;

			case ARROW2:
				rect.OffsetBy(0.0, buttonSize);
				break;

			case ARROW3:
				rect.OffsetTo(bounds.left, bounds.bottom - 2 * buttonSize + 1);
				break;

			case ARROW4:
				rect.OffsetTo(bounds.left, bounds.bottom - buttonSize + 1);
				break;
		}
	} else {
		switch (button) {
			case ARROW1:
				break;

			case ARROW2:
				rect.OffsetBy(buttonSize, 0.0);
				break;

			case ARROW3:
				rect.OffsetTo(bounds.right - 2 * buttonSize + 1, bounds.top);
				break;

			case ARROW4:
				rect.OffsetTo(bounds.right - buttonSize + 1, bounds.top);
				break;
		}
	}

	return rect;
}


/**
 * @brief Update the stop value used by the auto-scroll repeater.
 *
 * Computes the scroll value that would place the thumb centred under \a where
 * and stores it in fPrivateData->fStopValue.  The repeater thread checks this
 * value and halts page-scrolling once the thumb reaches it.
 *
 * @param where Current cursor position in the view's coordinate system.
 */
void
BScrollBar::_UpdateTargetValue(BPoint where)
{
	if (fOrientation == B_VERTICAL) {
		fPrivateData->fStopValue = _ValueFor(BPoint(where.x, where.y
			- fPrivateData->fThumbFrame.Height() / 2.0));
	} else {
		fPrivateData->fStopValue = _ValueFor(BPoint(where.x
			- fPrivateData->fThumbFrame.Width() / 2.0, where.y));
	}
}


/**
 * @brief Refresh the enabled state of the arrow buttons and repaint as needed.
 *
 * Compares the current value against fMin and fMax to determine whether the
 * up/left and down/right buttons should be enabled.  When a state changes,
 * only the affected button rectangles are invalidated so the redraw is kept
 * minimal.  In double-arrow mode both buttons of each direction are updated.
 *
 * Called by SetValue() and SetRange().
 */
void
BScrollBar::_UpdateArrowButtons()
{
	bool upEnabled = fValue > fMin;
	if (fPrivateData->fUpArrowsEnabled != upEnabled) {
		fPrivateData->fUpArrowsEnabled = upEnabled;
		Invalidate(_ButtonRectFor(ARROW1));
		if (_DoubleArrows())
			Invalidate(_ButtonRectFor(ARROW3));
	}

	bool downEnabled = fValue < fMax;
	if (fPrivateData->fDownArrowsEnabled != downEnabled) {
		fPrivateData->fDownArrowsEnabled = downEnabled;
		Invalidate(_ButtonRectFor(ARROW4));
		if (_DoubleArrows())
			Invalidate(_ButtonRectFor(ARROW2));
	}
}


/**
 * @brief Apply system-wide scroll_bar_info settings to a specific BScrollBar.
 *
 * Updates the double-arrow flag, proportional mode, knob style, and minimum
 * knob size stored in the bar's private data.  If the double-arrow setting
 * changes, the thumb frame is offset immediately to compensate.
 *
 * @param info  Pointer to the new scroll_bar_info settings to apply.
 * @param bar   The BScrollBar to update.
 * @return B_OK on success.
 * @retval B_BAD_VALUE If either pointer is NULL, the knob style is out of
 *                     range [0, 2], or the min_knob_size is outside
 *                     [SCROLL_BAR_MINIMUM_KNOB_SIZE, SCROLL_BAR_MAXIMUM_KNOB_SIZE].
 */
status_t
control_scrollbar(scroll_bar_info* info, BScrollBar* bar)
{
	if (bar == NULL || info == NULL)
		return B_BAD_VALUE;

	if (bar->fPrivateData->fScrollBarInfo.double_arrows
			!= info->double_arrows) {
		bar->fPrivateData->fScrollBarInfo.double_arrows = info->double_arrows;

		int8 multiplier = (info->double_arrows) ? 1 : -1;

		if (bar->fOrientation == B_VERTICAL) {
			bar->fPrivateData->fThumbFrame.OffsetBy(0, multiplier
				* B_H_SCROLL_BAR_HEIGHT);
		} else {
			bar->fPrivateData->fThumbFrame.OffsetBy(multiplier
				* B_V_SCROLL_BAR_WIDTH, 0);
		}
	}

	bar->fPrivateData->fScrollBarInfo.proportional = info->proportional;

	// TODO: Figure out how proportional relates to the size of the thumb

	// TODO: Add redraw code to reflect the changes

	if (info->knob >= 0 && info->knob <= 2)
		bar->fPrivateData->fScrollBarInfo.knob = info->knob;
	else
		return B_BAD_VALUE;

	if (info->min_knob_size >= SCROLL_BAR_MINIMUM_KNOB_SIZE
			&& info->min_knob_size <= SCROLL_BAR_MAXIMUM_KNOB_SIZE) {
		bar->fPrivateData->fScrollBarInfo.min_knob_size = info->min_knob_size;
	} else
		return B_BAD_VALUE;

	return B_OK;
}


/**
 * @brief Compute the intrinsic minimum size of the scroll bar.
 *
 * For a horizontal bar the minimum width accommodates two arrow buttons
 * (each B_V_SCROLL_BAR_WIDTH wide) plus two minimum knob lengths.
 * For a vertical bar the roles are swapped.
 *
 * This value is composed with any explicit minimum size by MinSize().
 *
 * @return The intrinsic minimum BSize.
 * @see MinSize()
 */
BSize
BScrollBar::_MinSize() const
{
	BSize minSize;
	if (fOrientation == B_HORIZONTAL) {
		minSize.width = 2 * B_V_SCROLL_BAR_WIDTH
			+ 2 * fPrivateData->fScrollBarInfo.min_knob_size;
		minSize.height = B_H_SCROLL_BAR_HEIGHT;
	} else {
		minSize.width = B_V_SCROLL_BAR_WIDTH;
		minSize.height = 2 * B_H_SCROLL_BAR_HEIGHT
			+ 2 * fPrivateData->fScrollBarInfo.min_knob_size;
	}

	return minSize;
}
