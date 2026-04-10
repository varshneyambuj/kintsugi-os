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
 *     Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2009-2012 Axel Dörfler / Copyright 2009 Stephan Aßmus.
 *   All rights reserved. Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 *       Stephan Aßmus <superstippi@gmx.de>
 */


/**
 * @file ToolTipManager.cpp
 * @brief Implementation of BToolTipManager, the global tooltip coordinator
 *
 * BToolTipManager is a singleton that tracks the mouse position and shows or
 * hides tooltip windows after the appropriate hover delay. Views register
 * tooltips via BView::SetToolTip().
 *
 * @see BToolTip, BView
 */


#include <ToolTipManager.h>
#include <ToolTipWindow.h>

#include <pthread.h>

#include <Autolock.h>
#include <LayoutBuilder.h>
#include <MessageRunner.h>
#include <Screen.h>

#include <WindowPrivate.h>
#include <ToolTip.h>


/** @brief pthread_once control variable ensuring the singleton is created exactly once. */
static pthread_once_t sManagerInitOnce = PTHREAD_ONCE_INIT;
BToolTipManager* BToolTipManager::sDefaultInstance;

/** @brief Internal message code requesting that the current tooltip be hidden after a delay. */
static const uint32 kMsgHideToolTip = 'hide';
/** @brief Internal message code requesting that the current tooltip be shown or repositioned. */
static const uint32 kMsgShowToolTip = 'show';
/** @brief Internal message code querying which tooltip is currently displayed. */
static const uint32 kMsgCurrentToolTip = 'curr';
/** @brief Internal message code sent by a BMessageRunner to close the tooltip window. */
static const uint32 kMsgCloseToolTip = 'clos';


namespace BPrivate {


/**
 * @brief Internal BView that hosts a BToolTip's content view inside a tooltip window.
 *
 * ToolTipView handles pointer and keyboard events to implement the hide-on-move
 * and sticky-tooltip behaviours. It also computes the on-screen placement of the
 * tooltip window via ResetWindowFrame().
 */
class ToolTipView : public BView {
public:
								ToolTipView(BToolTip* tip);
	virtual						~ToolTipView();

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();

	virtual	void				FrameResized(float width, float height);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);

			void				HideTip();
			void				ShowTip();

			void				ResetWindowFrame();
			void				ResetWindowFrame(BPoint where);

			BToolTip*			Tip() const { return fToolTip; }
			bool				IsTipHidden() const { return fHidden; }

private:
	/** @brief The tooltip whose content view is hosted by this container. */
			BToolTip*			fToolTip;
	/** @brief Last known screen position of the mouse cursor, used for repositioning. */
			BPoint				fWhere;
	/** @brief True when a hide request is pending and the window will close shortly. */
			bool				fHidden;
};


/**
 * @brief Construct a ToolTipView that hosts the content view of @a tip.
 *
 * Acquires a reference to the tooltip, sets up system tooltip colours, installs
 * a BGroupLayout with half-item insets, and embeds the tooltip's own view.
 *
 * @param tip  The BToolTip whose View() is added as a child.
 */
ToolTipView::ToolTipView(BToolTip* tip)
	:
	BView("tool tip", B_WILL_DRAW | B_FRAME_EVENTS),
	fToolTip(tip),
	fHidden(false)
{
	fToolTip->AcquireReference();
	SetViewUIColor(B_TOOL_TIP_BACKGROUND_COLOR);
	SetHighUIColor(B_TOOL_TIP_TEXT_COLOR);

	BGroupLayout* layout = new BGroupLayout(B_VERTICAL);
	layout->SetInsets(B_USE_HALF_ITEM_SPACING);
	SetLayout(layout);

	AddChild(fToolTip->View());
}


/**
 * @brief Destroy the ToolTipView and release the reference to the tooltip.
 */
ToolTipView::~ToolTipView()
{
	fToolTip->ReleaseReference();
}


/**
 * @brief Subscribe to pointer and keyboard events after attachment to a window.
 *
 * Sets the event mask so the view receives global pointer and keyboard events,
 * then notifies the tooltip that it has been attached.
 *
 * @see   DetachedFromWindow()
 */
void
ToolTipView::AttachedToWindow()
{
	SetEventMask(B_POINTER_EVENTS | B_KEYBOARD_EVENTS, 0);
	fToolTip->AttachedToWindow();
}


/**
 * @brief Clean up when the view is removed from its window.
 *
 * Removes the tooltip's content view without deleting it (ownership stays with
 * the tooltip object), then notifies the tooltip that it has been detached.
 * The BToolTipManager is locked across this operation to avoid races.
 *
 * @see   AttachedToWindow()
 */
void
ToolTipView::DetachedFromWindow()
{
	BToolTipManager* manager = BToolTipManager::Manager();
	manager->Lock();

	RemoveChild(fToolTip->View());
		// don't delete this one!
	fToolTip->DetachedFromWindow();

	manager->Unlock();
}


/**
 * @brief Recompute the tooltip window's on-screen position when the view resizes.
 *
 * Calls ResetWindowFrame() with the last known cursor position so that the
 * window snaps to the correct location after a size change.
 *
 * @param width   New view width (unused; frame is derived from PreferredSize()).
 * @param height  New view height (unused).
 */
void
ToolTipView::FrameResized(float width, float height)
{
	ResetWindowFrame();
}


/**
 * @brief Handle cursor movement over or near the tooltip window.
 *
 * For a sticky tooltip, the window follows the cursor. For a normal tooltip,
 * entering the view closes the window immediately, and any other movement
 * schedules a delayed hide via HideTip().
 *
 * @param where        Current cursor position in the view's coordinate system.
 * @param transit      B_ENTERED_VIEW, B_EXITED_VIEW, or B_INSIDE_VIEW.
 * @param dragMessage  Drag payload (unused).
 * @see   HideTip(), ShowTip()
 */
void
ToolTipView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	if (fToolTip->IsSticky()) {
		ResetWindowFrame(ConvertToScreen(where));
	} else if (transit == B_ENTERED_VIEW) {
		// close instantly if the user managed to enter
		Window()->Quit();
	} else {
		// close with the preferred delay in case the mouse just moved
		HideTip();
	}
}


/**
 * @brief Hide the tooltip immediately on any key press (unless it is sticky).
 *
 * @param bytes     Raw key bytes (unused beyond the sticky check).
 * @param numBytes  Number of bytes in @a bytes.
 */
void
ToolTipView::KeyDown(const char* bytes, int32 numBytes)
{
	if (!fToolTip->IsSticky())
		HideTip();
}


/**
 * @brief Schedule the tooltip window to close after the hide delay.
 *
 * Uses BMessageRunner to send kMsgCloseToolTip to the window after
 * BToolTipManager::HideDelay() microseconds. Subsequent calls before the timer
 * fires are ignored because fHidden is set immediately.
 *
 * @see   ShowTip(), BToolTipManager::HideDelay()
 */
void
ToolTipView::HideTip()
{
	if (fHidden)
		return;

	BMessage quit(kMsgCloseToolTip);
	BMessageRunner::StartSending(Window(), &quit,
		BToolTipManager::Manager()->HideDelay(), 1);
	fHidden = true;
}


/**
 * @brief Cancel a pending hide so the tooltip remains visible.
 *
 * Resets the fHidden flag. Any in-flight kMsgCloseToolTip message will be
 * ignored by ToolTipWindow::MessageReceived() because IsTipHidden() returns
 * false.
 *
 * @see   HideTip()
 */
void
ToolTipView::ShowTip()
{
	fHidden = false;
}


/**
 * @brief Reposition the tooltip window using the last known cursor location.
 *
 * Convenience wrapper that calls ResetWindowFrame(BPoint) with fWhere.
 *
 * @see   ResetWindowFrame(BPoint)
 */
void
ToolTipView::ResetWindowFrame()
{
	ResetWindowFrame(fWhere);
}


/**
 * @brief Compute and apply the optimal on-screen frame for the tooltip window.
 *
 * Uses the tooltip's requested alignment and mouse-relative location as a
 * starting point, then adjusts the position and size so that the window fits
 * entirely within the screen frame. Falls back to alternative alignments when
 * the preferred side does not have enough room, and clamps the window to the
 * screen as a last resort.
 *
 * @param where  The current cursor position in screen coordinates.
 */
void
ToolTipView::ResetWindowFrame(BPoint where)
{
	fWhere = where;

	if (Window() == NULL)
		return;

	BSize size = PreferredSize();

	BScreen screen(Window());
	BRect screenFrame = screen.Frame().InsetBySelf(2, 2);
	BPoint offset = fToolTip->MouseRelativeLocation();

	// Ensure that the tip can be placed on screen completely

	if (size.width > screenFrame.Width())
		size.width = screenFrame.Width();

	if (size.width > where.x - screenFrame.left
		&& size.width > screenFrame.right - where.x) {
		// There is no space to put the tip to the left or the right of the
		// cursor, it can either be below or above it
		if (size.height > where.y - screenFrame.top
			&& where.y - screenFrame.top > screenFrame.Height() / 2) {
			size.height = where.y - offset.y - screenFrame.top;
		} else if (size.height > screenFrame.bottom - where.y
			&& screenFrame.bottom - where.y > screenFrame.Height() / 2) {
			size.height = screenFrame.bottom - where.y - offset.y;
		}
	}

	// Find best alignment, starting with the requested one

	BAlignment alignment = fToolTip->Alignment();
	BPoint location = where;
	bool doesNotFit = false;

	switch (alignment.horizontal) {
		case B_ALIGN_LEFT:
			location.x -= size.width + offset.x;
			if (location.x < screenFrame.left) {
				location.x = screenFrame.left;
				doesNotFit = true;
			}
			break;
		case B_ALIGN_CENTER:
			location.x -= size.width / 2 - offset.x;
			if (location.x < screenFrame.left) {
				location.x = screenFrame.left;
				doesNotFit = true;
			} else if (location.x + size.width > screenFrame.right) {
				location.x = screenFrame.right - size.width;
				doesNotFit = true;
			}
			break;

		default:
			location.x += offset.x;
			if (location.x + size.width > screenFrame.right) {
				location.x = screenFrame.right - size.width;
				doesNotFit = true;
			}
			break;
	}

	if ((doesNotFit && alignment.vertical == B_ALIGN_MIDDLE)
		|| (alignment.vertical == B_ALIGN_MIDDLE
			&& alignment.horizontal == B_ALIGN_CENTER))
		alignment.vertical = B_ALIGN_BOTTOM;

	// Adjust the tooltip position in cases where it would be partly out of the
	// screen frame. Try to fit the tooltip on the requested side of the
	// cursor, if that fails, try the opposite side, and if that fails again,
	// give up and leave the tooltip under the mouse cursor.
	bool firstTry = true;
	while (true) {
		switch (alignment.vertical) {
			case B_ALIGN_TOP:
				location.y = where.y - size.height - offset.y;
				if (location.y < screenFrame.top) {
					alignment.vertical = firstTry ? B_ALIGN_BOTTOM
						: B_ALIGN_MIDDLE;
					firstTry = false;
					continue;
				}
				break;

			case B_ALIGN_MIDDLE:
				location.y -= size.height / 2 - offset.y;
				if (location.y < screenFrame.top)
					location.y = screenFrame.top;
				else if (location.y + size.height > screenFrame.bottom)
					location.y = screenFrame.bottom - size.height;
				break;

			default:
				location.y = where.y + offset.y;
				if (location.y + size.height > screenFrame.bottom) {
					alignment.vertical = firstTry ? B_ALIGN_TOP
						: B_ALIGN_MIDDLE;
					firstTry = false;
					continue;
				}
				break;
		}
		break;
	}

	where = location;

	// Cut off any out-of-screen areas

	if (screenFrame.left > where.x) {
		size.width -= where.x - screenFrame.left;
		where.x = screenFrame.left;
	} else if (screenFrame.right < where.x + size.width)
		size.width = screenFrame.right - where.x;

	if (screenFrame.top > where.y) {
		size.height -= where.y - screenFrame.top;
		where.y = screenFrame.top;
	} else if (screenFrame.bottom < where.y + size.height)
		size.height -= screenFrame.bottom - where.y;

	// Change window frame

	Window()->ResizeTo(size.width, size.height);
	Window()->MoveTo(where);
}


// #pragma mark -


/**
 * @brief Construct the floating tooltip window positioned near @a where.
 *
 * Creates a borderless, non-focusable window, installs a ToolTipView hosting
 * @a tip, and calls ResetWindowFrame() to compute the final on-screen position.
 *
 * @param tip    The BToolTip to display.
 * @param where  The screen position of the mouse cursor when the tip was triggered.
 * @param owner  Opaque pointer identifying the view that owns this tooltip; used
 *               by ShowTip() to detect when the same owner re-triggers the tip.
 */
ToolTipWindow::ToolTipWindow(BToolTip* tip, BPoint where, void* owner)
	:
	BWindow(BRect(0, 0, 250, 10).OffsetBySelf(where), "tool tip",
		B_BORDERED_WINDOW_LOOK, kMenuWindowFeel,
		B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_AVOID_FRONT | B_AVOID_FOCUS),
	fOwner(owner)
{
	SetLayout(new BGroupLayout(B_VERTICAL));

	BToolTipManager* manager = BToolTipManager::Manager();
	ToolTipView* view = new ToolTipView(tip);

	manager->Lock();
	AddChild(view);
	manager->Unlock();

	// figure out size and location
	view->ResetWindowFrame(where);
}


/**
 * @brief Dispatch tooltip window messages to the hosting ToolTipView.
 *
 * Handles the four internal tooltip message codes:
 * - kMsgHideToolTip: schedules a delayed close via ToolTipView::HideTip().
 * - kMsgCurrentToolTip: replies with the current BToolTip pointer and owner.
 * - kMsgShowToolTip: repositions and un-hides the tooltip.
 * - kMsgCloseToolTip: quits the window if the tip is still marked hidden.
 *
 * @param message  The BMessage to handle.
 */
void
ToolTipWindow::MessageReceived(BMessage* message)
{
	ToolTipView* view = static_cast<ToolTipView*>(ChildAt(0));

	switch (message->what) {
		case kMsgHideToolTip:
			view->HideTip();
			break;

		case kMsgCurrentToolTip:
		{
			BToolTip* tip = view->Tip();

			BMessage reply(B_REPLY);
			reply.AddPointer("current", tip);
			reply.AddPointer("owner", fOwner);

			if (message->SendReply(&reply) == B_OK)
				tip->AcquireReference();
			break;
		}

		case kMsgShowToolTip:
		{
			BPoint where;
			if (message->FindPoint("where", &where) == B_OK)
				view->ResetWindowFrame(where);
			view->ShowTip();
			break;
		}

		case kMsgCloseToolTip:
			if (view->IsTipHidden())
				Quit();
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


}	// namespace BPrivate


// #pragma mark -


/**
 * @brief Return the global BToolTipManager singleton.
 *
 * Creates the singleton on first call using pthread_once() for thread safety.
 * Subsequent calls return the cached pointer without taking the once-lock,
 * relying on the assumption that pointer reads are atomic on the target
 * architecture.
 *
 * @return Pointer to the single BToolTipManager instance.
 */
/*static*/ BToolTipManager*
BToolTipManager::Manager()
{
	// Note: The check is not necessary; it's just faster than always calling
	// pthread_once(). It requires reading/writing of pointers to be atomic
	// on the architecture.
	if (sDefaultInstance == NULL)
		pthread_once(&sManagerInitOnce, &_InitSingleton);

	return sDefaultInstance;
}


/**
 * @brief Show the given tooltip near the specified screen position.
 *
 * If a tooltip window is already open for the same tip or the same owner, the
 * existing window is repositioned rather than re-created. If a different tip is
 * being shown, the old window receives a hide request and a new tooltip window
 * is opened.
 *
 * @param tip    The BToolTip to display; may be NULL to only hide the current tip.
 * @param where  The screen position used to place the tooltip window.
 * @param owner  Opaque pointer identifying the requesting view; used to detect
 *               same-owner re-triggers without comparing tip pointers.
 * @see   HideTip()
 */
void
BToolTipManager::ShowTip(BToolTip* tip, BPoint where, void* owner)
{
	BToolTip* current = NULL;
	void* currentOwner = NULL;
	BMessage reply;
	if (fWindow.SendMessage(kMsgCurrentToolTip, &reply) == B_OK) {
		reply.FindPointer("current", (void**)&current);
		reply.FindPointer("owner", &currentOwner);
	}

	// Release reference from the message
	if (current != NULL)
		current->ReleaseReference();

	if (current == tip || currentOwner == owner) {
		BMessage message(kMsgShowToolTip);
		message.AddPoint("where", where);
		fWindow.SendMessage(&message);
		return;
	}

	fWindow.SendMessage(kMsgHideToolTip);

	if (tip != NULL) {
		BWindow* window = new BPrivate::ToolTipWindow(tip, where, owner);
		window->Show();

		fWindow = BMessenger(window);
	}
}


/**
 * @brief Request that the currently visible tooltip be hidden.
 *
 * Sends kMsgHideToolTip to the active tooltip window. The window will close
 * itself after the configured hide delay.
 *
 * @see   ShowTip(), SetHideDelay()
 */
void
BToolTipManager::HideTip()
{
	fWindow.SendMessage(kMsgHideToolTip);
}


/**
 * @brief Set the delay before a tooltip is shown after the cursor stops moving.
 *
 * The value is clamped to the range [10 ms, 3 s].
 *
 * @param time  The desired show delay in microseconds.
 * @see   ShowDelay()
 */
void
BToolTipManager::SetShowDelay(bigtime_t time)
{
	// between 10ms and 3s
	if (time < 10000)
		time = 10000;
	else if (time > 3000000)
		time = 3000000;

	fShowDelay = time;
}


/**
 * @brief Return the current show delay in microseconds.
 *
 * @return The delay between the cursor stopping and the tooltip appearing.
 * @see   SetShowDelay()
 */
bigtime_t
BToolTipManager::ShowDelay() const
{
	return fShowDelay;
}


/**
 * @brief Set the delay before a tooltip window closes after a hide request.
 *
 * The value is clamped to the range [0, 500 ms].
 *
 * @param time  The desired hide delay in microseconds.
 * @see   HideDelay()
 */
void
BToolTipManager::SetHideDelay(bigtime_t time)
{
	// between 0 and 0.5s
	if (time < 0)
		time = 0;
	else if (time > 500000)
		time = 500000;

	fHideDelay = time;
}


/**
 * @brief Return the current hide delay in microseconds.
 *
 * @return The delay between a hide request and the tooltip window closing.
 * @see   SetHideDelay()
 */
bigtime_t
BToolTipManager::HideDelay() const
{
	return fHideDelay;
}


/**
 * @brief Construct the BToolTipManager with default show and hide delays.
 *
 * The default show delay is 750 ms and the default hide delay is 50 ms.
 * Construction is performed by _InitSingleton() via pthread_once(); callers
 * must use Manager() to obtain the instance.
 *
 * @see   Manager(), _InitSingleton()
 */
BToolTipManager::BToolTipManager()
	:
	fLock("tool tip manager"),
	fShowDelay(750000),
	fHideDelay(50000)
{
}


/**
 * @brief Destroy the BToolTipManager.
 *
 * In practice the singleton lives for the lifetime of the application and is
 * never destroyed through normal program flow.
 */
BToolTipManager::~BToolTipManager()
{
}


/**
 * @brief pthread_once callback that allocates the singleton instance.
 *
 * Called exactly once by Manager() via pthread_once(). Assigns the newly
 * created BToolTipManager to sDefaultInstance.
 *
 * @see   Manager()
 */
/*static*/ void
BToolTipManager::_InitSingleton()
{
	sDefaultInstance = new BToolTipManager();
}
