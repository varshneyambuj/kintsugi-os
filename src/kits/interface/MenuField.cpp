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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2006-2016 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus, superstippi@gmx.de
 *       Marc Flerackers, mflerackers@androme.be
 *       John Scipione, jscipione@gmail.com
 *       Ingo Weinhold, bonefish@cs.tu-berlin.de
 */


/**
 * @file MenuField.cpp
 * @brief Implementation of BMenuField, a labeled pop-up menu control
 *
 * BMenuField combines a text label with a compact pop-up menu button. When
 * clicked, it opens a BPopUpMenu beneath the button. It supports layout
 * integration and provides scripting access to the current menu selection.
 *
 * @see BMenu, BPopUpMenu, BControl
 */


#include <MenuField.h>

#include <algorithm>

#include <stdio.h>
	// for printf in TRACE
#include <stdlib.h>
#include <string.h>

#include <AbstractLayoutItem.h>
#include <Archivable.h>
#include <BMCPrivate.h>
#include <ControlLook.h>
#include <LayoutUtils.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MenuItemPrivate.h>
#include <MenuPrivate.h>
#include <Message.h>
#include <MessageFilter.h>
#include <Window.h>

#include <binary_compatibility/Interface.h>
#include <binary_compatibility/Support.h>


#ifdef CALLED
#	undef CALLED
#endif
#ifdef TRACE
#	undef TRACE
#endif

//#define TRACE_MENU_FIELD
#ifdef TRACE_MENU_FIELD
#	include <FunctionTracer.h>
	static int32 sFunctionDepth = -1;
#	define CALLED(x...)	FunctionTracer _ft(printf, this, __PRETTY_FUNCTION__, sFunctionDepth)
#	define TRACE(x...)	{ BString _to; \
							_to.Append(' ', (sFunctionDepth + 1) * 2); \
							printf("%s", _to.String()); printf(x); }
#else
#	define CALLED(x...)
#	define TRACE(x...)
#endif


/** @brief Minimum pixel width for the embedded menu bar, determined empirically on BeOS R5. */
static const float kMinMenuBarWidth = 20.0f;
	// found by experimenting on BeOS R5


namespace {
	/** @brief Archive field key for storing a layout item's frame rectangle. */
	const char* const kFrameField = "BMenuField:layoutItem:frame";
	/** @brief Archive field key for the MenuBarLayoutItem archivable. */
	const char* const kMenuBarItemField = "BMenuField:barItem";
	/** @brief Archive field key for the LabelLayoutItem archivable. */
	const char* const kLabelItemField = "BMenuField:labelItem";
}


//	#pragma mark - LabelLayoutItem


class BMenuField::LabelLayoutItem : public BAbstractLayoutItem {
public:
								LabelLayoutItem(BMenuField* parent);
								LabelLayoutItem(BMessage* archive);

			BRect				FrameInParent() const;

	virtual	bool				IsVisible();
	virtual	void				SetVisible(bool visible);

	virtual	BRect				Frame();
	virtual	void				SetFrame(BRect frame);

			void				SetParent(BMenuField* parent);
	virtual	BView*				View();

	virtual	BSize				BaseMinSize();
	virtual	BSize				BaseMaxSize();
	virtual	BSize				BasePreferredSize();
	virtual	BAlignment			BaseAlignment();

	virtual status_t			Archive(BMessage* into, bool deep = true) const;
	static	BArchivable*		Instantiate(BMessage* from);

private:
			BMenuField*			fParent;
			BRect				fFrame;
};


//	#pragma mark - MenuBarLayoutItem


class BMenuField::MenuBarLayoutItem : public BAbstractLayoutItem {
public:
								MenuBarLayoutItem(BMenuField* parent);
								MenuBarLayoutItem(BMessage* from);

			BRect				FrameInParent() const;

	virtual	bool				IsVisible();
	virtual	void				SetVisible(bool visible);

	virtual	BRect				Frame();
	virtual	void				SetFrame(BRect frame);

			void				SetParent(BMenuField* parent);
	virtual	BView*				View();

	virtual	BSize				BaseMinSize();
	virtual	BSize				BaseMaxSize();
	virtual	BSize				BasePreferredSize();
	virtual	BAlignment			BaseAlignment();

	virtual status_t			Archive(BMessage* into, bool deep = true) const;
	static	BArchivable*		Instantiate(BMessage* from);

private:
			BMenuField*			fParent;
			BRect				fFrame;
};


//	#pragma mark - LayoutData


/**
 * @brief Internal POD structure that caches all computed layout metrics.
 *
 * Recalculated by _ValidateLayoutData() whenever valid is @c false.
 * Stored as a heap-allocated pointer in BMenuField so that the public
 * header does not need to expose internal types.
 *
 * @see BMenuField::_ValidateLayoutData(), BMenuField::LayoutInvalidated()
 */
struct BMenuField::LayoutData {
	/**
	 * @brief Constructs a LayoutData with all metrics in the invalid state.
	 */
	LayoutData()
		:
		label_layout_item(NULL),
		menu_bar_layout_item(NULL),
		previous_height(-1),
		valid(false)
	{
	}

	/** @brief Layout item for the label portion; @c NULL until CreateLabelLayoutItem() is called. */
	LabelLayoutItem*	label_layout_item;
	/** @brief Layout item for the menu bar portion; @c NULL until CreateMenuBarLayoutItem() is called. */
	MenuBarLayoutItem*	menu_bar_layout_item;
	/** @brief Last recorded view height, used in FrameResized() to detect height changes. */
	float				previous_height;	// used in FrameResized() for
											// invalidation
	/** @brief Cached font metrics for the current font, filled by _ValidateLayoutData(). */
	font_height			font_info;
	/** @brief Pixel width of the label string (ceil), or 0 if no label. */
	float				label_width;
	/** @brief Pixel height of the label text (ascent + descent, ceil), or 0 if no label. */
	float				label_height;
	/** @brief Computed overall minimum (== preferred) size of the BMenuField. */
	BSize				min;
	/** @brief Minimum size of the embedded menu bar as reported by BMenuBar::MinSize(). */
	BSize				menu_bar_min;
	/** @brief True when the cached values are up to date; set to false by LayoutInvalidated(). */
	bool				valid;
};


// #pragma mark - MouseDownFilter

namespace {

/**
 * @brief Internal message filter that suppresses B_MOUSE_DOWN events.
 *
 * Installed as a common window filter while the pop-up menu is tracking so
 * that stray mouse-down events do not re-trigger menu activation.
 *
 * @see BMenuField::MouseDown(), BMenuField::MouseUp()
 */
class MouseDownFilter : public BMessageFilter
{
public:
								MouseDownFilter();
	virtual						~MouseDownFilter();

	virtual	filter_result		Filter(BMessage* message, BHandler** target);
};


/**
 * @brief Constructs a MouseDownFilter that intercepts messages from any source.
 */
MouseDownFilter::MouseDownFilter()
	:
	BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE)
{
}


/**
 * @brief Destroys the MouseDownFilter.
 */
MouseDownFilter::~MouseDownFilter()
{
}


/**
 * @brief Skips B_MOUSE_DOWN messages; dispatches all other messages normally.
 *
 * @param message The incoming message to evaluate.
 * @param target  The intended handler (not modified).
 * @return B_SKIP_MESSAGE for B_MOUSE_DOWN, B_DISPATCH_MESSAGE otherwise.
 */
filter_result
MouseDownFilter::Filter(BMessage* message, BHandler** target)
{
	return message->what == B_MOUSE_DOWN ? B_SKIP_MESSAGE : B_DISPATCH_MESSAGE;
}

};



// #pragma mark - BMenuField


/**
 * @brief Constructs a BMenuField in frame-based (non-layout) mode.
 *
 * Creates the label and an embedded pop-up menu bar sized to the given
 * @a frame. The menu bar grows or shrinks with the view. Use this
 * constructor for legacy frame-based window management.
 *
 * @param frame        The position and size of the entire control in its
 *                     parent's coordinate system.
 * @param name         The internal view name, used for scripting.
 * @param label        The text label displayed to the left of the menu.
 * @param menu         The BMenu (typically a BPopUpMenu) to embed.
 * @param resizingMode How the view resizes with its parent (e.g. B_FOLLOW_LEFT).
 * @param flags        View flags (e.g. B_WILL_DRAW | B_NAVIGABLE).
 *
 * @see BMenuField(BRect, const char*, const char*, BMenu*, bool, uint32, uint32)
 */
BMenuField::BMenuField(BRect frame, const char* name, const char* label,
	BMenu* menu, uint32 resizingMode, uint32 flags)
	:
	BView(frame, name, resizingMode, flags)
{
	CALLED();

	TRACE("frame.width: %.2f, height: %.2f\n", frame.Width(), frame.Height());

	InitObject(label);

	frame.OffsetTo(B_ORIGIN);
	_InitMenuBar(menu, frame, false);

	InitObject2();
}


/**
 * @brief Constructs a BMenuField in frame-based mode with optional fixed menu-bar width.
 *
 * Same as the five-parameter frame constructor but allows the caller to pin
 * the menu bar to a fixed width via @a fixedSize. When @a fixedSize is
 * @c true the menu bar always spans the full remaining width of the view.
 *
 * @param frame        The position and size of the control.
 * @param name         The internal view name.
 * @param label        The label text.
 * @param menu         The BMenu to embed.
 * @param fixedSize    If @c true the menu bar has a fixed (full) width;
 *                     if @c false it sizes to its content.
 * @param resizingMode How the view resizes with its parent.
 * @param flags        View flags.
 *
 * @see BMenuField(BRect, const char*, const char*, BMenu*, uint32, uint32)
 */
BMenuField::BMenuField(BRect frame, const char* name, const char* label,
	BMenu* menu, bool fixedSize, uint32 resizingMode, uint32 flags)
	:
	BView(frame, name, resizingMode, flags)
{
	InitObject(label);

	fFixedSizeMB = fixedSize;

	frame.OffsetTo(B_ORIGIN);
	_InitMenuBar(menu, frame, fixedSize);

	InitObject2();
}


/**
 * @brief Constructs a BMenuField in layout mode (no frame, fixed-size menu bar).
 *
 * Use this constructor when the control will be managed by a BLayout. The
 * view has no name and B_FRAME_EVENTS is added to @a flags automatically.
 * The menu bar is always fixed-width (full available width).
 *
 * @param name  The internal view name.
 * @param label The label text, or @c NULL for no label.
 * @param menu  The BMenu to embed.
 * @param flags View flags; B_FRAME_EVENTS is OR-ed in unconditionally.
 *
 * @see BMenuField(const char*, const char*, BMenu*, bool, uint32)
 */
BMenuField::BMenuField(const char* name, const char* label, BMenu* menu,
	uint32 flags)
	:
	BView(name, flags | B_FRAME_EVENTS)
{
	InitObject(label);

	_InitMenuBar(menu, BRect(0, 0, 100, 15), true);

	InitObject2();
}


/**
 * @brief Constructs a BMenuField in layout mode with configurable menu-bar sizing.
 *
 * Like the three-parameter layout constructor but exposes @a fixedSize so the
 * caller can allow the menu bar to shrink to its content width.
 *
 * @param name      The internal view name.
 * @param label     The label text, or @c NULL for no label.
 * @param menu      The BMenu to embed.
 * @param fixedSize If @c true the menu bar spans the full available width.
 * @param flags     View flags; B_FRAME_EVENTS is OR-ed in unconditionally.
 */
BMenuField::BMenuField(const char* name, const char* label, BMenu* menu,
	bool fixedSize, uint32 flags)
	:
	BView(name, flags | B_FRAME_EVENTS)
{
	InitObject(label);

	fFixedSizeMB = fixedSize;

	_InitMenuBar(menu, BRect(0, 0, 100, 15), fixedSize);

	InitObject2();
}


/**
 * @brief Constructs an anonymous BMenuField in layout mode.
 *
 * Convenience constructor for layout-managed fields; the view name is set to
 * @c NULL and the menu bar is fixed-width.
 *
 * @param label The label text, or @c NULL for no label.
 * @param menu  The BMenu to embed.
 * @param flags View flags; B_FRAME_EVENTS is OR-ed in unconditionally.
 */
BMenuField::BMenuField(const char* label, BMenu* menu, uint32 flags)
	:
	BView(NULL, flags | B_FRAME_EVENTS)
{
	InitObject(label);

	_InitMenuBar(menu, BRect(0, 0, 100, 15), true);

	InitObject2();
}


/**
 * @brief Constructs a BMenuField from an archived BMessage.
 *
 * Restores the label, divider position, and alignment from the archive.
 * If the archive is not managed by a BUnarchiver (legacy archives), the
 * menu bar is restored inline via _InitMenuBar(const BMessage*).
 *
 * @param data The BMessage produced by Archive().
 *
 * @see Archive(), Instantiate(), AllUnarchived()
 */
BMenuField::BMenuField(BMessage* data)
	:
	BView(BUnarchiver::PrepareArchive(data))
{
	BUnarchiver unarchiver(data);
	const char* label = NULL;
	data->FindString("_label", &label);

	InitObject(label);

	data->FindFloat("_divide", &fDivider);

	int32 align;
	if (data->FindInt32("_align", &align) == B_OK)
		SetAlignment((alignment)align);

	if (!BUnarchiver::IsArchiveManaged(data))
		_InitMenuBar(data);

	unarchiver.Finish();
}


/**
 * @brief Destroys the BMenuField and releases all owned resources.
 *
 * Frees the label string, waits for the menu-tracking thread to finish,
 * deletes the LayoutData structure, and deletes the MouseDownFilter.
 */
BMenuField::~BMenuField()
{
	free(fLabel);

	status_t dummy;
	if (fMenuTaskID >= 0)
		wait_for_thread(fMenuTaskID, &dummy);

	delete fLayoutData;
	delete fMouseDownFilter;
}


/**
 * @brief Creates a BMenuField from an archive message (BArchivable hook).
 *
 * @param data The archive message previously produced by Archive().
 * @return A newly allocated BMenuField, or @c NULL if the archive is invalid.
 *
 * @see Archive(), BMenuField(BMessage*)
 */
BArchivable*
BMenuField::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "BMenuField"))
		return new BMenuField(data);

	return NULL;
}


/**
 * @brief Archives the BMenuField's state into a BMessage.
 *
 * Stores the label ("_label"), disabled state ("_disable"), alignment
 * ("_align"), divider position ("_divide"), fixed-size flag ("be:fixeds"),
 * and pop-up marker visibility ("be:dmark") in addition to the base
 * BView archive data.
 *
 * @param data The BMessage to archive into.
 * @param deep If @c true, child views are archived recursively.
 * @return B_OK on success, or an error code from BView::Archive() or
 *         BMessage::AddString()/AddBool()/AddFloat()/AddInt32().
 *
 * @see Instantiate(), AllArchived()
 */
status_t
BMenuField::Archive(BMessage* data, bool deep) const
{
	BArchiver archiver(data);
	status_t ret = BView::Archive(data, deep);

	if (ret == B_OK && Label())
		ret = data->AddString("_label", Label());

	if (ret == B_OK && !IsEnabled())
		ret = data->AddBool("_disable", true);

	if (ret == B_OK)
		ret = data->AddInt32("_align", Alignment());
	if (ret == B_OK)
		ret = data->AddFloat("_divide", Divider());

	if (ret == B_OK && fFixedSizeMB)
		ret = data->AddBool("be:fixeds", true);

	bool dmark = false;
	if (_BMCMenuBar_* menuBar = dynamic_cast<_BMCMenuBar_*>(fMenuBar))
		dmark = menuBar->IsPopUpMarkerShown();

	data->AddBool("be:dmark", dmark);

	return archiver.Finish(ret);
}


/**
 * @brief Finishes archiving by storing the layout items (BArchivable hook).
 *
 * Called after all children have been archived. Adds the MenuBarLayoutItem
 * and LabelLayoutItem to @a into using their respective field keys so that
 * layout geometry is preserved across archive/unarchive cycles.
 *
 * @param into The archive message being built.
 * @return B_OK on success, or the first error encountered.
 *
 * @see AllUnarchived(), Archive()
 */
status_t
BMenuField::AllArchived(BMessage* into) const
{
	status_t err;
	if ((err = BView::AllArchived(into)) != B_OK)
		return err;

	BArchiver archiver(into);

	BArchivable* menuBarItem = fLayoutData->menu_bar_layout_item;
	if (archiver.IsArchived(menuBarItem))
		err = archiver.AddArchivable(kMenuBarItemField, menuBarItem);

	if (err != B_OK)
		return err;

	BArchivable* labelBarItem = fLayoutData->label_layout_item;
	if (archiver.IsArchived(labelBarItem))
		err = archiver.AddArchivable(kLabelItemField, labelBarItem);

	return err;
}


/**
 * @brief Completes unarchiving by restoring layout items and the menu bar (BArchivable hook).
 *
 * Reconstructs the embedded menu bar from the archive, then re-attaches
 * the MenuBarLayoutItem and LabelLayoutItem and sets their parent pointer
 * back to this BMenuField.
 *
 * @param from The archive message produced by Archive()/AllArchived().
 * @return B_OK on success, or an error forwarded from BView or BUnarchiver.
 *
 * @see AllArchived(), BMenuField(BMessage*)
 */
status_t
BMenuField::AllUnarchived(const BMessage* from)
{
	BUnarchiver unarchiver(from);

	status_t err = B_OK;
	if ((err = BView::AllUnarchived(from)) != B_OK)
		return err;

	_InitMenuBar(from);

	if (unarchiver.IsInstantiated(kMenuBarItemField)) {
		MenuBarLayoutItem*& menuItem = fLayoutData->menu_bar_layout_item;
		err = unarchiver.FindObject(kMenuBarItemField,
			BUnarchiver::B_DONT_ASSUME_OWNERSHIP, menuItem);

		if (err == B_OK)
			menuItem->SetParent(this);
		else
			return err;
	}

	if (unarchiver.IsInstantiated(kLabelItemField)) {
		LabelLayoutItem*& labelItem = fLayoutData->label_layout_item;
		err = unarchiver.FindObject(kLabelItemField,
			BUnarchiver::B_DONT_ASSUME_OWNERSHIP, labelItem);

		if (err == B_OK)
			labelItem->SetParent(this);
	}

	return err;
}


/**
 * @brief Draws the label and the menu-bar frame for the dirty region.
 *
 * Delegates to the private helpers _DrawLabel() and _DrawMenuBar(), each of
 * which clips to @a updateRect before rendering.
 *
 * @param updateRect The portion of the view that needs repainting.
 *
 * @see _DrawLabel(), _DrawMenuBar()
 */
void
BMenuField::Draw(BRect updateRect)
{
	_DrawLabel(updateRect);
	_DrawMenuBar(updateRect);
}


/**
 * @brief Synchronises the view's low color with the parent when added to a window.
 *
 * If the view has a parent, it adopts the parent's colors and aligns the low
 * color to the parent's UI color. If there is no parent, it falls back to the
 * system colors so that the label background always blends seamlessly.
 *
 * @see AllAttached(), BView::AttachedToWindow()
 */
void
BMenuField::AttachedToWindow()
{
	CALLED();

	// Our low color must match the parent's view color.
	if (Parent() != NULL) {
		AdoptParentColors();

		float tint = B_NO_TINT;
		color_which which = ViewUIColor(&tint);

		if (which == B_NO_COLOR)
			SetLowColor(ViewColor());
		else
			SetLowUIColor(which, tint);
	} else
		AdoptSystemColors();
}


/**
 * @brief Resizes the control to fit the menu bar after the whole hierarchy is attached.
 *
 * In non-fixed-size mode, if the menu bar would be narrower than
 * @c kMinMenuBarWidth, the overall view is widened to accommodate the first
 * menu item. The view height is always forced to match the menu bar height
 * plus the vertical margin on both sides.
 *
 * @see AttachedToWindow(), BView::AllAttached()
 */
void
BMenuField::AllAttached()
{
	CALLED();

	TRACE("width: %.2f, height: %.2f\n", Frame().Width(), Frame().Height());

	float width = Bounds().Width();
	if (!fFixedSizeMB && _MenuBarWidth() < kMinMenuBarWidth) {
		// The menu bar is too narrow, resize it to fit the menu items
		BMenuItem* item = fMenuBar->ItemAt(0);
		if (item != NULL) {
			float right;
			fMenuBar->GetItemMargins(NULL, NULL, &right, NULL);
			width = item->Frame().Width() + kVMargin + _MenuBarOffset() + right;
		}
	}

	ResizeTo(width, fMenuBar->Bounds().Height() + kVMargin * 2);

	TRACE("width: %.2f, height: %.2f\n", Frame().Width(), Frame().Height());
}


/**
 * @brief Opens the pop-up menu and starts the tracking thread on a mouse-down event.
 *
 * Starts the embedded menu bar's tracking mode, spawns the internal
 * _MenuTask() thread that monitors tracking state, installs the
 * MouseDownFilter to suppress re-entrant clicks, and extends the view's
 * mouse-event mask for the duration of the interaction.
 *
 * @param where The mouse position in the view's coordinate system (unused directly).
 *
 * @see MouseUp(), _MenuTask(), _thread_entry()
 */
void
BMenuField::MouseDown(BPoint where)
{
	BRect bounds = fMenuBar->ConvertFromParent(Bounds());

	fMenuBar->StartMenuBar(-1, false, true, &bounds);

	fMenuTaskID = spawn_thread((thread_func)_thread_entry,
		"_m_task_", B_NORMAL_PRIORITY, this);
	if (fMenuTaskID >= 0 && resume_thread(fMenuTaskID) == B_OK) {
		if (fMouseDownFilter->Looper() == NULL)
			Window()->AddCommonFilter(fMouseDownFilter);

		SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
	}
}


/**
 * @brief Opens the pop-up menu in response to keyboard activation keys.
 *
 * Space, right-arrow, and down-arrow open the first menu item when the
 * control has focus and is enabled. All other keys are forwarded to
 * BView::KeyDown().
 *
 * @param bytes    Pointer to the UTF-8 byte sequence of the key pressed.
 * @param numBytes Number of bytes in the sequence.
 *
 * @see MakeFocus(), MouseDown()
 */
void
BMenuField::KeyDown(const char* bytes, int32 numBytes)
{
	switch (bytes[0]) {
		case B_SPACE:
		case B_RIGHT_ARROW:
		case B_DOWN_ARROW:
		{
			if (!IsEnabled())
				break;

			BRect bounds = fMenuBar->ConvertFromParent(Bounds());

			fMenuBar->StartMenuBar(0, true, true, &bounds);

			bounds = Bounds();
			bounds.right = fDivider;

			Invalidate(bounds);
		}

		default:
			BView::KeyDown(bytes, numBytes);
	}
}


/**
 * @brief Grants or removes keyboard focus and repaints the focus indicator.
 *
 * Calls the base-class implementation and then invalidates the entire view
 * so that the focus ring around the label is drawn or erased.
 *
 * @param focused @c true to acquire focus, @c false to relinquish it.
 *
 * @see KeyDown(), WindowActivated()
 */
void
BMenuField::MakeFocus(bool focused)
{
	if (IsFocus() == focused)
		return;

	BView::MakeFocus(focused);

	if (Window() != NULL)
		Invalidate(); // TODO: use fLayoutData->label_width
}


/**
 * @brief Forwards unhandled messages to BView::MessageReceived().
 *
 * BMenuField does not process any messages directly; all message handling
 * is delegated to the base class.
 *
 * @param message The incoming BMessage to process.
 *
 * @see BView::MessageReceived()
 */
void
BMenuField::MessageReceived(BMessage* message)
{
	BView::MessageReceived(message);
}


/**
 * @brief Repaints the focus ring when the window gains or loses activation.
 *
 * The focus highlight is only visible when the window is active, so the
 * view must be repainted whenever the activation state changes while this
 * control holds keyboard focus.
 *
 * @param active @c true if the window has just become active.
 *
 * @see MakeFocus(), BView::WindowActivated()
 */
void
BMenuField::WindowActivated(bool active)
{
	BView::WindowActivated(active);

	if (IsFocus())
		Invalidate();
}


/**
 * @brief Forwards mouse-move events to the base class.
 *
 * @param point   Current mouse position in view coordinates.
 * @param code    Transit code (B_ENTERED_VIEW, B_INSIDE_VIEW, etc.).
 * @param message Drag-and-drop message, or @c NULL.
 *
 * @see BView::MouseMoved()
 */
void
BMenuField::MouseMoved(BPoint point, uint32 code, const BMessage* message)
{
	BView::MouseMoved(point, code, message);
}


/**
 * @brief Removes the MouseDownFilter and forwards the event after menu tracking ends.
 *
 * Called when the mouse button is released. Unregisters the common filter
 * that was suppressing extra mouse-down events during tracking.
 *
 * @param where Mouse position in view coordinates at button release.
 *
 * @see MouseDown(), MouseDownFilter
 */
void
BMenuField::MouseUp(BPoint where)
{
	Window()->RemoveCommonFilter(fMouseDownFilter);
	BView::MouseUp(where);
}


/**
 * @brief Called when the view is removed from its window.
 *
 * Forwards to BView::DetachedFromWindow() for standard cleanup.
 *
 * @see BView::DetachedFromWindow(), AllDetached()
 */
void
BMenuField::DetachedFromWindow()
{
	BView::DetachedFromWindow();
}


/**
 * @brief Called after the entire view hierarchy has been detached from the window.
 *
 * Forwards to BView::AllDetached() for standard cleanup.
 *
 * @see BView::AllDetached(), DetachedFromWindow()
 */
void
BMenuField::AllDetached()
{
	BView::AllDetached();
}


/**
 * @brief Forwards the frame-moved notification to the base class.
 *
 * @param newPosition The view's new origin in its parent's coordinate system.
 *
 * @see BView::FrameMoved(), FrameResized()
 */
void
BMenuField::FrameMoved(BPoint newPosition)
{
	BView::FrameMoved(newPosition);
}


/**
 * @brief Adjusts the menu bar and repaints after the view is resized.
 *
 * In fixed-size mode the menu bar is explicitly resized to stay flush with
 * the right edge. When the height changes and a label is present, the whole
 * view is invalidated because the label vertical position also shifts.
 *
 * @param newWidth  The new pixel width of the view's bounds.
 * @param newHeight The new pixel height of the view's bounds.
 *
 * @see FrameMoved(), BView::FrameResized()
 */
void
BMenuField::FrameResized(float newWidth, float newHeight)
{
	BView::FrameResized(newWidth, newHeight);

	if (fFixedSizeMB) {
		// we have let the menubar resize itself, but
		// in fixed size mode, the menubar is supposed to
		// be at the right end of the view always. Since
		// the menu bar is in follow left/right mode then,
		// resizing ourselfs might have caused the menubar
		// to be outside now
		fMenuBar->ResizeTo(_MenuBarWidth(), fMenuBar->Frame().Height());
	}

	if (newHeight != fLayoutData->previous_height && Label()) {
		// The height changed, which means the label has to move and we
		// probably also invalidate a part of the borders around the menu bar.
		// So don't be shy and invalidate the whole thing.
		Invalidate();
	}

	fLayoutData->previous_height = newHeight;
}


/**
 * @brief Returns the BMenu embedded in this control.
 *
 * @return Pointer to the BMenu (typically a BPopUpMenu) set at construction.
 *
 * @see MenuBar(), _AddMenu()
 */
BMenu*
BMenuField::Menu() const
{
	return fMenu;
}


/**
 * @brief Returns the internal BMenuBar that hosts the pop-up menu.
 *
 * @return Pointer to the _BMCMenuBar_ child view used as the menu container.
 *
 * @see Menu(), CreateMenuBarLayoutItem()
 */
BMenuBar*
BMenuField::MenuBar() const
{
	return fMenuBar;
}


/**
 * @brief Returns the top-level BMenuItem displayed in the menu bar.
 *
 * This is the item at index 0 of the embedded menu bar, which shows the
 * currently selected entry of the pop-up menu.
 *
 * @return Pointer to the first BMenuItem, or @c NULL if the bar is empty.
 *
 * @see Menu(), MenuBar()
 */
BMenuItem*
BMenuField::MenuItem() const
{
	return fMenuBar->ItemAt(0);
}


/**
 * @brief Sets the text label displayed to the left of the menu bar.
 *
 * Does nothing if the new label is identical to the current one. Frees the
 * old label string, duplicates the new one, invalidates the view for an
 * immediate repaint, and invalidates the layout so the preferred size is
 * recalculated.
 *
 * @param label The new label text, or @c NULL to remove the label.
 *
 * @see Label(), SetDivider(), InvalidateLayout()
 */
void
BMenuField::SetLabel(const char* label)
{
	if ((fLabel != NULL && label != NULL && strcmp(fLabel, label) == 0)
		|| (fLabel == NULL && label == NULL)) {
		// labels are the same, do not set label
		return;
	}

	if (fLabel != NULL) {
		free(fLabel);
		fLabel = NULL;
	}
	if (label != NULL)
		fLabel = strdup(label);

	if (Window())
		Invalidate();

	InvalidateLayout();
}


/**
 * @brief Returns the current label text.
 *
 * @return Pointer to the null-terminated label string, or @c NULL if no label is set.
 *
 * @see SetLabel()
 */
const char*
BMenuField::Label() const
{
	return fLabel;
}


/**
 * @brief Enables or disables the control and its embedded menu bar.
 *
 * Propagates the enabled state to the embedded menu bar and repaints both
 * the bar and the overall view so the dimmed appearance is updated immediately.
 *
 * @param on @c true to enable the control, @c false to disable it.
 *
 * @see IsEnabled(), SetLabel()
 */
void
BMenuField::SetEnabled(bool on)
{
	if (fEnabled == on)
		return;

	fEnabled = on;
	fMenuBar->SetEnabled(on);

	if (Window()) {
		fMenuBar->Invalidate(fMenuBar->Bounds());
		Invalidate(Bounds());
	}
}


/**
 * @brief Returns whether the control is currently enabled.
 *
 * @return @c true if the control accepts user input, @c false if it is disabled.
 *
 * @see SetEnabled()
 */
bool
BMenuField::IsEnabled() const
{
	return fEnabled;
}


/**
 * @brief Sets the horizontal alignment of the label text.
 *
 * @param label One of B_ALIGN_LEFT, B_ALIGN_CENTER, or B_ALIGN_RIGHT.
 *
 * @see Alignment(), SetLabel()
 */
void
BMenuField::SetAlignment(alignment label)
{
	fAlign = label;
}


/**
 * @brief Returns the current horizontal alignment of the label text.
 *
 * @return The alignment constant set by SetAlignment().
 *
 * @see SetAlignment()
 */
alignment
BMenuField::Alignment() const
{
	return fAlign;
}


/**
 * @brief Sets the pixel position of the divider between label and menu bar.
 *
 * The divider determines how much of the view width is allocated to the label
 * versus the menu bar. In layout mode the divider is controlled by the layout
 * system and calling this method will only trigger a relayout. In frame mode
 * the menu bar is repositioned and resized immediately, and the dirty region is
 * invalidated.
 *
 * @param position The new divider position in view coordinates (rounded to the
 *                 nearest integer pixel).
 *
 * @see Divider(), SetAlignment(), DoLayout()
 */
void
BMenuField::SetDivider(float position)
{
	position = roundf(position);

	float delta = fDivider - position;
	if (delta == 0.0f)
		return;

	fDivider = position;

	if ((Flags() & B_SUPPORTS_LAYOUT) != 0) {
		// We should never get here, since layout support means, we also
		// layout the divider, and don't use this method at all.
		Relayout();
	} else {
		BRect dirty(fMenuBar->Frame());

		fMenuBar->MoveTo(_MenuBarOffset(), kVMargin);

		if (fFixedSizeMB)
			fMenuBar->ResizeTo(_MenuBarWidth(), dirty.Height());

		dirty = dirty | fMenuBar->Frame();
		dirty.InsetBy(-kVMargin, -kVMargin);

		Invalidate(dirty);
	}
}


/**
 * @brief Returns the current divider position between label and menu bar.
 *
 * @return The divider's x-coordinate in the view's coordinate system.
 *
 * @see SetDivider()
 */
float
BMenuField::Divider() const
{
	return fDivider;
}


/**
 * @brief Displays the small pop-up arrow marker inside the menu bar button.
 *
 * Has no effect if the embedded menu bar is not a _BMCMenuBar_ instance.
 *
 * @see HidePopUpMarker(), _BMCMenuBar_::TogglePopUpMarker()
 */
void
BMenuField::ShowPopUpMarker()
{
	if (_BMCMenuBar_* menuBar = dynamic_cast<_BMCMenuBar_*>(fMenuBar)) {
		menuBar->TogglePopUpMarker(true);
		menuBar->Invalidate();
	}
}


/**
 * @brief Hides the small pop-up arrow marker inside the menu bar button.
 *
 * Has no effect if the embedded menu bar is not a _BMCMenuBar_ instance.
 *
 * @see ShowPopUpMarker(), _BMCMenuBar_::TogglePopUpMarker()
 */
void
BMenuField::HidePopUpMarker()
{
	if (_BMCMenuBar_* menuBar = dynamic_cast<_BMCMenuBar_*>(fMenuBar)) {
		menuBar->TogglePopUpMarker(false);
		menuBar->Invalidate();
	}
}


/**
 * @brief Resolves a scripting specifier to the appropriate BHandler.
 *
 * Delegates directly to BView for standard property handling.
 *
 * @param message   The scripting message.
 * @param index     The specifier index within the message.
 * @param specifier The specifier extracted from @a message.
 * @param form      The specifier form (e.g. B_NAME_SPECIFIER).
 * @param property  The property name being accessed.
 * @return The BHandler that should handle the message.
 *
 * @see GetSupportedSuites(), BView::ResolveSpecifier()
 */
BHandler*
BMenuField::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 form, const char* property)
{
	return BView::ResolveSpecifier(message, index, specifier, form, property);
}


/**
 * @brief Fills @a data with the scripting suites supported by BMenuField.
 *
 * Delegates to BView::GetSupportedSuites(); BMenuField does not add its own
 * scripting suite beyond what BView provides.
 *
 * @param data The BMessage to populate with suite information.
 * @return B_OK on success, or an error code from BView.
 *
 * @see ResolveSpecifier(), BView::GetSupportedSuites()
 */
status_t
BMenuField::GetSupportedSuites(BMessage* data)
{
	return BView::GetSupportedSuites(data);
}


/**
 * @brief Resizes the control and its embedded menu bar to their preferred sizes.
 *
 * First asks the menu bar to resize itself to its preferred size, then calls
 * BView::ResizeToPreferred() to resize the outer view accordingly, and finally
 * invalidates to trigger a repaint.
 *
 * @see GetPreferredSize(), PreferredSize()
 */
void
BMenuField::ResizeToPreferred()
{
	CALLED();

	TRACE("fMenuBar->Frame().width: %.2f, height: %.2f\n",
		fMenuBar->Frame().Width(), fMenuBar->Frame().Height());

	fMenuBar->ResizeToPreferred();

	TRACE("fMenuBar->Frame().width: %.2f, height: %.2f\n",
		fMenuBar->Frame().Width(), fMenuBar->Frame().Height());

	BView::ResizeToPreferred();

	Invalidate();
}


/**
 * @brief Returns the preferred (minimum) width and height of the control.
 *
 * Validates the cached layout data and returns the computed minimum size
 * as the preferred size. Either output parameter may be @c NULL if the
 * caller does not need that dimension.
 *
 * @param[out] _width  Receives the preferred width, or is ignored if @c NULL.
 * @param[out] _height Receives the preferred height, or is ignored if @c NULL.
 *
 * @see MinSize(), PreferredSize(), _ValidateLayoutData()
 */
void
BMenuField::GetPreferredSize(float* _width, float* _height)
{
	CALLED();

	_ValidateLayoutData();

	if (_width)
		*_width = fLayoutData->min.width;

	if (_height)
		*_height = fLayoutData->min.height;
}


/**
 * @brief Returns the minimum layout size of the control.
 *
 * Composes the computed minimum size with any explicit minimum set by the
 * caller via SetExplicitMinSize().
 *
 * @return The minimum BSize, composed with any explicit override.
 *
 * @see MaxSize(), PreferredSize(), _ValidateLayoutData()
 */
BSize
BMenuField::MinSize()
{
	CALLED();

	_ValidateLayoutData();
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), fLayoutData->min);
}


/**
 * @brief Returns the maximum layout size of the control.
 *
 * The height maximum equals the minimum height; the width maximum is
 * B_SIZE_UNLIMITED so the control can expand horizontally in a layout.
 *
 * @return The maximum BSize, composed with any explicit override.
 *
 * @see MinSize(), PreferredSize()
 */
BSize
BMenuField::MaxSize()
{
	CALLED();

	_ValidateLayoutData();

	BSize max = fLayoutData->min;
	max.width = B_SIZE_UNLIMITED;

	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), max);
}


/**
 * @brief Returns the preferred layout size of the control.
 *
 * The preferred size equals the minimum size; the control does not
 * benefit from extra space beyond its minimum.
 *
 * @return The preferred BSize, composed with any explicit override.
 *
 * @see MinSize(), MaxSize()
 */
BSize
BMenuField::PreferredSize()
{
	CALLED();

	_ValidateLayoutData();
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), fLayoutData->min);
}


/**
 * @brief Creates (or returns the existing) layout item for the label portion.
 *
 * The returned LabelLayoutItem can be placed independently in a BLayout so
 * that multiple BMenuField instances can align their labels in a grid. Only
 * one instance is created; subsequent calls return the same object.
 *
 * @return The LabelLayoutItem owned by this BMenuField.
 *
 * @see CreateMenuBarLayoutItem(), LabelLayoutItem
 */
BLayoutItem*
BMenuField::CreateLabelLayoutItem()
{
	if (fLayoutData->label_layout_item == NULL)
		fLayoutData->label_layout_item = new LabelLayoutItem(this);

	return fLayoutData->label_layout_item;
}


/**
 * @brief Creates (or returns the existing) layout item for the menu bar portion.
 *
 * The returned MenuBarLayoutItem can be placed independently in a BLayout.
 * On first call the embedded menu bar is configured to use full available
 * width. Subsequent calls return the same object.
 *
 * @return The MenuBarLayoutItem owned by this BMenuField.
 *
 * @see CreateLabelLayoutItem(), MenuBarLayoutItem
 */
BLayoutItem*
BMenuField::CreateMenuBarLayoutItem()
{
	if (fLayoutData->menu_bar_layout_item == NULL) {
		// align the menu bar in the full available space
		fMenuBar->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
			B_ALIGN_VERTICAL_UNSET));
		fLayoutData->menu_bar_layout_item = new MenuBarLayoutItem(this);
	}

	return fLayoutData->menu_bar_layout_item;
}


/**
 * @brief Dispatches binary-compatibility perform codes for layout and archive hooks.
 *
 * Implements the BView perform() protocol so that layout-aware host environments
 * can call MinSize(), MaxSize(), PreferredSize(), LayoutAlignment(),
 * HasHeightForWidth(), GetHeightForWidth(), SetLayout(), LayoutInvalidated(),
 * DoLayout(), AllUnarchived(), and AllArchived() through an opaque perform code
 * rather than a vtable slot. All unrecognised codes are forwarded to BView::Perform().
 *
 * @param code  One of the PERFORM_CODE_* constants defined in binary_compatibility/Interface.h.
 * @param _data Pointer to the matching perform_data_* structure.
 * @return B_OK if the code was handled, otherwise the result from BView::Perform().
 *
 * @see MinSize(), MaxSize(), PreferredSize(), LayoutAlignment()
 */
status_t
BMenuField::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_MIN_SIZE:
			((perform_data_min_size*)_data)->return_value
				= BMenuField::MinSize();
			return B_OK;

		case PERFORM_CODE_MAX_SIZE:
			((perform_data_max_size*)_data)->return_value
				= BMenuField::MaxSize();
			return B_OK;

		case PERFORM_CODE_PREFERRED_SIZE:
			((perform_data_preferred_size*)_data)->return_value
				= BMenuField::PreferredSize();
			return B_OK;

		case PERFORM_CODE_LAYOUT_ALIGNMENT:
			((perform_data_layout_alignment*)_data)->return_value
				= BMenuField::LayoutAlignment();
			return B_OK;

		case PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH:
			((perform_data_has_height_for_width*)_data)->return_value
				= BMenuField::HasHeightForWidth();
			return B_OK;

		case PERFORM_CODE_GET_HEIGHT_FOR_WIDTH:
		{
			perform_data_get_height_for_width* data
				= (perform_data_get_height_for_width*)_data;
			BMenuField::GetHeightForWidth(data->width, &data->min, &data->max,
				&data->preferred);
			return B_OK;
		}

		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BMenuField::SetLayout(data->layout);
			return B_OK;
		}

		case PERFORM_CODE_LAYOUT_INVALIDATED:
		{
			perform_data_layout_invalidated* data
				= (perform_data_layout_invalidated*)_data;
			BMenuField::LayoutInvalidated(data->descendants);
			return B_OK;
		}

		case PERFORM_CODE_DO_LAYOUT:
		{
			BMenuField::DoLayout();
			return B_OK;
		}

		case PERFORM_CODE_ALL_UNARCHIVED:
		{
			perform_data_all_unarchived* data
				= (perform_data_all_unarchived*)_data;
			data->return_value = BMenuField::AllUnarchived(data->archive);
			return B_OK;
		}

		case PERFORM_CODE_ALL_ARCHIVED:
		{
			perform_data_all_archived* data
				= (perform_data_all_archived*)_data;
			data->return_value = BMenuField::AllArchived(data->archive);
			return B_OK;
		}
	}

	return BView::Perform(code, _data);
}


/**
 * @brief Marks the cached layout data stale when the layout is invalidated.
 *
 * Called by the layout system whenever the view's layout needs to be
 * recalculated. Sets @c fLayoutData->valid to @c false so that the next call
 * to _ValidateLayoutData() recomputes sizes from scratch.
 *
 * @param descendants If @c true, child views were also invalidated.
 *
 * @see _ValidateLayoutData(), DoLayout()
 */
void
BMenuField::LayoutInvalidated(bool descendants)
{
	CALLED();

	fLayoutData->valid = false;
}


/**
 * @brief Performs the actual layout of the embedded menu bar within the view's bounds.
 *
 * Does nothing if the view does not support layout (B_SUPPORTS_LAYOUT flag
 * absent). If a child layout is set, delegates to BView::DoLayout(). Otherwise
 * computes the divider from the layout items (if valid) or from the label
 * width, then positions and sizes the menu bar within the remaining space and
 * invalidates the previously dirty region.
 *
 * @see LayoutInvalidated(), _ValidateLayoutData(), SetDivider()
 */
void
BMenuField::DoLayout()
{
	// Bail out, if we shan't do layout.
	if ((Flags() & B_SUPPORTS_LAYOUT) == 0)
		return;

	CALLED();

	// If the user set a layout, we let the base class version call its
	// hook.
	if (GetLayout() != NULL) {
		BView::DoLayout();
		return;
	}

	_ValidateLayoutData();

	// validate current size
	BSize size(Bounds().Size());
	if (size.width < fLayoutData->min.width)
		size.width = fLayoutData->min.width;

	if (size.height < fLayoutData->min.height)
		size.height = fLayoutData->min.height;

	// divider
	float divider = 0;
	if (fLayoutData->label_layout_item != NULL
		&& fLayoutData->menu_bar_layout_item != NULL
		&& fLayoutData->label_layout_item->Frame().IsValid()
		&& fLayoutData->menu_bar_layout_item->Frame().IsValid()) {
		// We have valid layout items, they define the divider location.
		divider = fabs(fLayoutData->menu_bar_layout_item->Frame().left
			- fLayoutData->label_layout_item->Frame().left);
	} else if (fLayoutData->label_width > 0) {
		divider = fLayoutData->label_width
			+ be_control_look->DefaultLabelSpacing();
	}

	// menu bar
	BRect dirty(fMenuBar->Frame());
	BRect menuBarFrame(divider + kVMargin, kVMargin, size.width - kVMargin,
		size.height - kVMargin);

	// place the menu bar and set the divider
	BLayoutUtils::AlignInFrame(fMenuBar, menuBarFrame);

	fDivider = divider;

	// invalidate dirty region
	dirty = dirty | fMenuBar->Frame();
	dirty.InsetBy(-kVMargin, -kVMargin);

	Invalidate(dirty);
}


/** @brief Reserved for future binary-compatible extension (slot 1). */
void BMenuField::_ReservedMenuField1() {}
/** @brief Reserved for future binary-compatible extension (slot 2). */
void BMenuField::_ReservedMenuField2() {}
/** @brief Reserved for future binary-compatible extension (slot 3). */
void BMenuField::_ReservedMenuField3() {}


/**
 * @brief Initialises all member variables to their default state.
 *
 * Called from every constructor before the menu bar is created. Sets all
 * pointers to NULL, booleans to their defaults, allocates the LayoutData
 * structure and the MouseDownFilter, then applies the label and computes
 * the initial divider position (half the frame width if a label is supplied).
 *
 * @param label The initial label text, or @c NULL for no label.
 *
 * @see InitObject2(), _InitMenuBar()
 */
void
BMenuField::InitObject(const char* label)
{
	CALLED();

	fLabel = NULL;
	fMenu = NULL;
	fMenuBar = NULL;
	fAlign = B_ALIGN_LEFT;
	fEnabled = true;
	fFixedSizeMB = false;
	fMenuTaskID = -1;
	fLayoutData = new LayoutData;
	fMouseDownFilter = new MouseDownFilter();

	SetLabel(label);

	if (label)
		fDivider = floorf(Frame().Width() / 2.0f);
	else
		fDivider = 0;
}


/**
 * @brief Finalises the menu bar setup after _InitMenuBar() has been called.
 *
 * In non-fixed-size mode the menu bar is resized to its preferred height and
 * the computed available width. A _BMCFilter_ is installed on the menu bar to
 * intercept B_MOUSE_DOWN events before they reach the standard handler.
 *
 * @see InitObject(), _InitMenuBar()
 */
void
BMenuField::InitObject2()
{
	CALLED();

	if (!fFixedSizeMB) {
		float height;
		fMenuBar->GetPreferredSize(NULL, &height);
		fMenuBar->ResizeTo(_MenuBarWidth(), height);
	}

	TRACE("frame(%.1f, %.1f, %.1f, %.1f) (%.2f, %.2f)\n",
		fMenuBar->Frame().left, fMenuBar->Frame().top,
		fMenuBar->Frame().right, fMenuBar->Frame().bottom,
		fMenuBar->Frame().Width(), fMenuBar->Frame().Height());

	fMenuBar->AddFilter(new _BMCFilter_(this, B_MOUSE_DOWN));
}


/**
 * @brief Renders the text label clipped to the label area and the update rectangle.
 *
 * Validates the layout data, determines the label bounding rectangle from
 * either the layout item or the divider, and calls be_control_look to draw
 * the label text. When the menu is open, the label background is highlighted
 * with B_MENU_SELECTED_BACKGROUND_COLOR as on BeOS R5.
 *
 * @param updateRect The dirty rectangle passed to Draw().
 *
 * @see Draw(), _DrawMenuBar(), _ValidateLayoutData()
 */
void
BMenuField::_DrawLabel(BRect updateRect)
{
	CALLED();

	_ValidateLayoutData();

	const char* label = Label();
	if (label == NULL)
		return;

	BRect rect;
	if (fLayoutData->label_layout_item != NULL)
		rect = fLayoutData->label_layout_item->FrameInParent();
	else {
		rect = Bounds();
		rect.right = fDivider;
	}

	if (!rect.IsValid() || !rect.Intersects(updateRect))
		return;

	uint32 flags = 0;
	if (!IsEnabled())
		flags |= BControlLook::B_DISABLED;

	// save the current low color
	PushState();
	rgb_color textColor;

	BPrivate::MenuPrivate menuPrivate(fMenuBar);
	if (menuPrivate.State() != MENU_STATE_CLOSED) {
		// highlight the background of the label grey (like BeOS R5)
		SetLowColor(ui_color(B_MENU_SELECTED_BACKGROUND_COLOR));
		BRect fillRect(rect.InsetByCopy(0, kVMargin));
		FillRect(fillRect, B_SOLID_LOW);
		textColor = ui_color(B_MENU_SELECTED_ITEM_TEXT_COLOR);
	} else
		textColor = ui_color(B_PANEL_TEXT_COLOR);

	be_control_look->DrawLabel(this, label, rect, updateRect, LowColor(), flags,
		BAlignment(fAlign, B_ALIGN_MIDDLE), &textColor);

	// restore the previous low color
	PopState();
}


/**
 * @brief Renders the decorative frame around the embedded menu bar.
 *
 * Computes the frame by insetting the menu bar's frame by the vertical margin,
 * clips to @a updateRect, then delegates to be_control_look->DrawMenuFieldFrame()
 * with the appropriate disabled/focused flags.
 *
 * @param updateRect The dirty rectangle passed to Draw().
 *
 * @see Draw(), _DrawLabel()
 */
void
BMenuField::_DrawMenuBar(BRect updateRect)
{
	CALLED();

	BRect rect(fMenuBar->Frame().InsetByCopy(-kVMargin, -kVMargin));
	if (!rect.IsValid() || !rect.Intersects(updateRect))
		return;

	uint32 flags = 0;
	if (!IsEnabled())
		flags |= BControlLook::B_DISABLED;

	if (IsFocus() && Window()->IsActive())
		flags |= BControlLook::B_FOCUSED;

	be_control_look->DrawMenuFieldFrame(this, rect, updateRect,
		fMenuBar->LowColor(), LowColor(), flags);
}


/**
 * @brief Recursively sets be_plain_font on a menu and all its submenus.
 *
 * Called once during construction to ensure a consistent look across all
 * levels of the pop-up menu hierarchy.
 *
 * @param menu The root BMenu to initialise; its submenus are visited recursively.
 *
 * @see _AddMenu(), _InitMenuBar()
 */
void
BMenuField::InitMenu(BMenu* menu)
{
	menu->SetFont(be_plain_font);

	int32 index = 0;
	BMenu* subMenu;

	while ((subMenu = menu->SubmenuAt(index++)) != NULL)
		InitMenu(subMenu);
}


/**
 * @brief Static thread entry point that forwards to _MenuTask().
 *
 * Provides the C-callable @c thread_func signature required by spawn_thread().
 *
 * @param arg The BMenuField instance cast to @c void*.
 * @return The return value of _MenuTask().
 *
 * @see _MenuTask(), MouseDown()
 */
/*static*/ int32
BMenuField::_thread_entry(void* arg)
{
	return static_cast<BMenuField*>(arg)->_MenuTask();
}


/**
 * @brief Monitors the menu bar's tracking state and invalidates the label area.
 *
 * Locks the looper, triggers an initial repaint to show the open state, then
 * polls fMenuBar->fTracking every 20 ms until tracking ends. A final
 * invalidation is issued to restore the normal label appearance.
 *
 * @return 0 always.
 *
 * @see _thread_entry(), MouseDown()
 */
int32
BMenuField::_MenuTask()
{
	if (!LockLooper())
		return 0;

	Invalidate();
	UnlockLooper();

	bool tracking;
	do {
		snooze(20000);
		if (!LockLooper())
			return 0;

		tracking = fMenuBar->fTracking;

		UnlockLooper();
	} while (tracking);

	if (LockLooper()) {
		Invalidate();
		UnlockLooper();
	}

	return 0;
}


/**
 * @brief Synchronises the view's frame and divider with the layout items' frames.
 *
 * Called by LabelLayoutItem::SetFrame() and MenuBarLayoutItem::SetFrame()
 * whenever the layout engine repositions one of the two items. Recomputes the
 * divider as the horizontal distance between the two item frames, then moves
 * and resizes the outer view to span both items. If the size did not change, a
 * manual Relayout() is triggered because ResizeTo() will not do so.
 *
 * @see LabelLayoutItem::SetFrame(), MenuBarLayoutItem::SetFrame(), DoLayout()
 */
void
BMenuField::_UpdateFrame()
{
	CALLED();

	if (fLayoutData->label_layout_item == NULL
		|| fLayoutData->menu_bar_layout_item == NULL) {
		return;
	}

	BRect labelFrame = fLayoutData->label_layout_item->Frame();
	BRect menuFrame = fLayoutData->menu_bar_layout_item->Frame();

	if (!labelFrame.IsValid() || !menuFrame.IsValid())
		return;

	// update divider
	fDivider = menuFrame.left - labelFrame.left;

	// update our frame
	MoveTo(labelFrame.left, labelFrame.top);
	BSize oldSize = Bounds().Size();
	ResizeTo(menuFrame.left + menuFrame.Width() - labelFrame.left,
		menuFrame.top + menuFrame.Height() - labelFrame.top);
	BSize newSize = Bounds().Size();

	// If the size changes, ResizeTo() will trigger a relayout, otherwise
	// we need to do that explicitly.
	if (newSize != oldSize)
		Relayout();
}


/**
 * @brief Creates and configures the embedded _BMCMenuBar_ from a menu and frame.
 *
 * In layout mode a frameless _BMCMenuBar_ is created; in frame mode the bar
 * is positioned at _MenuBarOffset() with the vertical margin applied. The bar's
 * explicit alignment is set to full-width or left-aligned depending on
 * @a fixedSize, the menu is added via _AddMenu(), and be_plain_font is applied.
 *
 * @param menu      The BMenu to attach to the newly created menu bar.
 * @param frame     The initial frame in the view's local coordinates (frame mode only).
 * @param fixedSize If @c true the bar spans the full available width.
 *
 * @see _InitMenuBar(const BMessage*), _AddMenu(), InitObject2()
 */
void
BMenuField::_InitMenuBar(BMenu* menu, BRect frame, bool fixedSize)
{
	CALLED();

	if ((Flags() & B_SUPPORTS_LAYOUT) != 0) {
		fMenuBar = new _BMCMenuBar_(this);
	} else {
		frame.left = _MenuBarOffset();
		frame.top = kVMargin;
		frame.right -= kVMargin;
		frame.bottom -= kVMargin;

		TRACE("frame(%.1f, %.1f, %.1f, %.1f) (%.2f, %.2f)\n",
			frame.left, frame.top, frame.right, frame.bottom,
			frame.Width(), frame.Height());

		fMenuBar = new _BMCMenuBar_(frame, fixedSize, this);
	}

	if (fixedSize) {
		// align the menu bar in the full available space
		fMenuBar->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
			B_ALIGN_VERTICAL_UNSET));
	} else {
		// align the menu bar left in the available space
		fMenuBar->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
			B_ALIGN_VERTICAL_UNSET));
	}

	AddChild(fMenuBar);

	_AddMenu(menu);

	fMenuBar->SetFont(be_plain_font);
}


/**
 * @brief Restores the embedded menu bar from an archive message.
 *
 * Reads the fixed-size flag ("be:fixeds"), locates the existing "_mc_mb_"
 * child view (present if the BView archive already recreated it), or creates
 * a fresh one. Then reconnects the first submenu, restores the enabled state,
 * and re-applies the pop-up marker visibility.
 *
 * @param archive The BMessage archive passed to the BMenuField(BMessage*) constructor.
 *
 * @see _InitMenuBar(BMenu*, BRect, bool), _AddMenu(), AllUnarchived()
 */
void
BMenuField::_InitMenuBar(const BMessage* archive)
{
	bool fixed;
	if (archive->FindBool("be:fixeds", &fixed) == B_OK)
		fFixedSizeMB = fixed;

	fMenuBar = (BMenuBar*)FindView("_mc_mb_");
	if (fMenuBar == NULL) {
		_InitMenuBar(new BMenu(""), BRect(0, 0, 100, 15), fFixedSizeMB);
		InitObject2();
	} else {
		fMenuBar->AddFilter(new _BMCFilter_(this, B_MOUSE_DOWN));
			// this is normally done in InitObject2()
	}

	_AddMenu(fMenuBar->SubmenuAt(0));

	bool disable;
	if (archive->FindBool("_disable", &disable) == B_OK)
		SetEnabled(!disable);

	bool dmark = false;
	archive->FindBool("be:dmark", &dmark);
	_BMCMenuBar_* menuBar = dynamic_cast<_BMCMenuBar_*>(fMenuBar);
	if (menuBar != NULL)
		menuBar->TogglePopUpMarker(dmark);
}


/**
 * @brief Attaches a BMenu to the embedded menu bar as the top-level item.
 *
 * Calls InitMenu() to apply be_plain_font recursively, then determines
 * which item to display: the marked item in radio mode, or the first
 * enabled non-separator item otherwise. A shallow archive copy of the chosen
 * item is made, stripped of its install state, and added to fMenuBar as a
 * submenu wrapper. If no suitable item is found the menu itself is added
 * directly.
 *
 * @param menu The BMenu to attach; does nothing if @c NULL or if fMenuBar is @c NULL.
 *
 * @see InitMenu(), _InitMenuBar()
 */
void
BMenuField::_AddMenu(BMenu* menu)
{
	if (menu == NULL || fMenuBar == NULL)
		return;

	fMenu = menu;
	InitMenu(menu);

	BMenuItem* item = NULL;
	if (!menu->IsRadioMode() || (item = menu->FindMarked()) == NULL) {
		// find the first enabled non-seperator item
		int32 itemCount = menu->CountItems();
		for (int32 i = 0; i < itemCount; i++) {
			item = menu->ItemAt((int32)i);
			if (item == NULL || !item->IsEnabled()
				|| dynamic_cast<BSeparatorItem*>(item) != NULL) {
				item = NULL;
				continue;
			}
			break;
		}
	}

	if (item == NULL) {
		fMenuBar->AddItem(menu);
		return;
	}

	// build an empty copy of item

	BMessage data;
	status_t result = item->Archive(&data, false);
	if (result != B_OK) {
		fMenuBar->AddItem(menu);
		return;
	}

	BArchivable* object = instantiate_object(&data);
	if (object == NULL) {
		fMenuBar->AddItem(menu);
		return;
	}

	BMenuItem* newItem = static_cast<BMenuItem*>(object);

	// unset parameters
	BPrivate::MenuItemPrivate newMenuItemPrivate(newItem);
	newMenuItemPrivate.Uninstall();

	// set the menu
	newMenuItemPrivate.SetSubmenu(menu);
	fMenuBar->AddItem(newItem);
}


/**
 * @brief Recomputes and caches the layout metrics if the cache is stale.
 *
 * Populates fLayoutData with the current font height, label pixel dimensions,
 * minimum menu bar size, and the resulting overall minimum size of the control.
 * In frame mode the current fDivider is also factored into the minimum width.
 * Sets fLayoutData->valid to @c true and calls ResetLayoutInvalidation() when done.
 *
 * @note Must be called at the start of every size-query method (MinSize(),
 *       GetPreferredSize(), etc.) and before DoLayout().
 *
 * @see LayoutInvalidated(), DoLayout(), MinSize()
 */
void
BMenuField::_ValidateLayoutData()
{
	CALLED();

	if (fLayoutData->valid)
		return;

	// cache font height
	font_height& fh = fLayoutData->font_info;
	GetFontHeight(&fh);

	const char* label = Label();
	if (label != NULL) {
		fLayoutData->label_width = ceilf(StringWidth(label));
		fLayoutData->label_height = ceilf(fh.ascent) + ceilf(fh.descent);
	} else {
		fLayoutData->label_width = 0;
		fLayoutData->label_height = 0;
	}

	// compute the minimal divider
	float divider = 0;
	if (fLayoutData->label_width > 0) {
		divider = fLayoutData->label_width
			+ be_control_look->DefaultLabelSpacing();
	}

	// If we shan't do real layout, we let the current divider take influence.
	if ((Flags() & B_SUPPORTS_LAYOUT) == 0)
		divider = std::max(divider, fDivider);

	// get the minimal (== preferred) menu bar size
	// TODO: BMenu::MinSize() is using the ResizeMode() to decide the
	// minimum width. If the mode is B_FOLLOW_LEFT_RIGHT, it will use the
	// parent's frame width or window's frame width. So at least the returned
	// size is wrong, but apparantly it doesn't have much bad effect.
	fLayoutData->menu_bar_min = fMenuBar->MinSize();

	TRACE("menu bar min width: %.2f\n", fLayoutData->menu_bar_min.width);

	// compute our minimal (== preferred) size
	BSize min(fLayoutData->menu_bar_min);
	min.width += 2 * kVMargin;
	min.height += 2 * kVMargin;

	if (divider > 0)
		min.width += divider;

	if (fLayoutData->label_height > min.height)
		min.height = fLayoutData->label_height;

	fLayoutData->min = min;

	fLayoutData->valid = true;
	ResetLayoutInvalidation();

	TRACE("width: %.2f, height: %.2f\n", min.width, min.height);
}


/**
 * @brief Returns the x-coordinate at which the menu bar starts inside the view.
 *
 * Ensures at least kVMargin even when the divider is zero (no label).
 *
 * @return The left edge of the menu bar in view-local coordinates.
 *
 * @see _MenuBarWidth(), SetDivider()
 */
float
BMenuField::_MenuBarOffset() const
{
	return std::max(fDivider + kVMargin, kVMargin);
}


/**
 * @brief Returns the available pixel width for the menu bar.
 *
 * Computed as the view's total width minus the menu-bar start offset and the
 * right-side margin.
 *
 * @return The usable width for the embedded menu bar.
 *
 * @see _MenuBarOffset(), DoLayout()
 */
float
BMenuField::_MenuBarWidth() const
{
	return Bounds().Width() - (_MenuBarOffset() + kVMargin);
}


// #pragma mark - BMenuField::LabelLayoutItem


/**
 * @brief Constructs a LabelLayoutItem associated with the given BMenuField.
 *
 * @param parent The owning BMenuField; must not be @c NULL.
 */
BMenuField::LabelLayoutItem::LabelLayoutItem(BMenuField* parent)
	:
	fParent(parent),
	fFrame()
{
}


/**
 * @brief Constructs a LabelLayoutItem from an archived BMessage.
 *
 * Restores the layout frame from the kFrameField key. The parent pointer
 * is not stored in the archive and must be set afterwards via SetParent().
 *
 * @param from The archive message produced by Archive().
 *
 * @see Archive(), Instantiate(), SetParent()
 */
BMenuField::LabelLayoutItem::LabelLayoutItem(BMessage* from)
	:
	BAbstractLayoutItem(from),
	fParent(NULL),
	fFrame()
{
	from->FindRect(kFrameField, &fFrame);
}


/**
 * @brief Returns the layout item's frame translated into the parent view's coordinates.
 *
 * Converts the absolute screen-space fFrame by subtracting the parent
 * BMenuField's own origin so that the rect is usable by Draw() and friends.
 *
 * @return The label area in the parent view's local coordinate system.
 *
 * @see Frame()
 */
BRect
BMenuField::LabelLayoutItem::FrameInParent() const
{
	return fFrame.OffsetByCopy(-fParent->Frame().left, -fParent->Frame().top);
}


/**
 * @brief Returns whether the label area is currently visible.
 *
 * @return @c true if the parent BMenuField is not hidden.
 *
 * @see SetVisible()
 */
bool
BMenuField::LabelLayoutItem::IsVisible()
{
	return !fParent->IsHidden(fParent);
}


/**
 * @brief Visibility changes are not permitted for this item.
 *
 * The label visibility is tied to the parent BMenuField and cannot be
 * toggled independently; this override is intentionally a no-op.
 *
 * @param visible Ignored.
 */
void
BMenuField::LabelLayoutItem::SetVisible(bool visible)
{
	// not allowed
}


/**
 * @brief Returns the absolute frame of this layout item.
 *
 * @return The item frame in the layout's coordinate system.
 *
 * @see SetFrame(), FrameInParent()
 */
BRect
BMenuField::LabelLayoutItem::Frame()
{
	return fFrame;
}


/**
 * @brief Updates the layout item's frame and triggers a parent frame update.
 *
 * Stores the new frame and calls BMenuField::_UpdateFrame() so that the
 * parent view moves and resizes itself to span both layout items.
 *
 * @param frame The new absolute frame assigned by the layout engine.
 *
 * @see Frame(), BMenuField::_UpdateFrame()
 */
void
BMenuField::LabelLayoutItem::SetFrame(BRect frame)
{
	fFrame = frame;
	fParent->_UpdateFrame();
}


/**
 * @brief Sets the parent BMenuField after unarchiving.
 *
 * Must be called when a LabelLayoutItem is reconstructed from an archive
 * (when fParent is @c NULL) before the item is used.
 *
 * @param parent The BMenuField that owns this layout item.
 *
 * @see LabelLayoutItem(BMessage*)
 */
void
BMenuField::LabelLayoutItem::SetParent(BMenuField* parent)
{
	fParent = parent;
}


/**
 * @brief Returns the BView associated with this layout item.
 *
 * @return The parent BMenuField, which is the view drawn for this item.
 *
 * @see SetParent()
 */
BView*
BMenuField::LabelLayoutItem::View()
{
	return fParent;
}


/**
 * @brief Returns the minimum size needed to render the label text.
 *
 * Returns BSize(-1, -1) if there is no label. Otherwise returns the
 * label pixel width plus the default label spacing, and the font height.
 *
 * @return The minimum BSize for the label area.
 *
 * @see BaseMaxSize(), BasePreferredSize()
 */
BSize
BMenuField::LabelLayoutItem::BaseMinSize()
{
	fParent->_ValidateLayoutData();

	if (fParent->Label() == NULL)
		return BSize(-1, -1);

	return BSize(fParent->fLayoutData->label_width
			+ be_control_look->DefaultLabelSpacing(),
		fParent->fLayoutData->label_height);
}


/**
 * @brief Returns the maximum size of the label area (equals the minimum).
 *
 * The label area does not grow beyond its preferred size; this keeps the
 * divider in a fixed position.
 *
 * @return Same as BaseMinSize().
 *
 * @see BaseMinSize()
 */
BSize
BMenuField::LabelLayoutItem::BaseMaxSize()
{
	return BaseMinSize();
}


/**
 * @brief Returns the preferred size of the label area (equals the minimum).
 *
 * @return Same as BaseMinSize().
 *
 * @see BaseMinSize()
 */
BSize
BMenuField::LabelLayoutItem::BasePreferredSize()
{
	return BaseMinSize();
}


/**
 * @brief Returns the layout alignment for the label item.
 *
 * Uses full width and full height so that the label fills the cell
 * assigned by the layout engine.
 *
 * @return BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_USE_FULL_HEIGHT).
 */
BAlignment
BMenuField::LabelLayoutItem::BaseAlignment()
{
	return BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_USE_FULL_HEIGHT);
}


/**
 * @brief Archives the LabelLayoutItem into a BMessage.
 *
 * Stores the base class data and the current fFrame under kFrameField.
 *
 * @param into The BMessage to archive into.
 * @param deep If @c true, children are archived (unused; no children).
 * @return B_OK on success, or an error from BAbstractLayoutItem::Archive().
 *
 * @see Instantiate(), LabelLayoutItem(BMessage*)
 */
status_t
BMenuField::LabelLayoutItem::Archive(BMessage* into, bool deep) const
{
	BArchiver archiver(into);
	status_t err = BAbstractLayoutItem::Archive(into, deep);

	if (err == B_OK)
		err = into->AddRect(kFrameField, fFrame);

	return archiver.Finish(err);
}


/**
 * @brief Instantiates a LabelLayoutItem from an archive message (BArchivable hook).
 *
 * @param from The archive message produced by Archive().
 * @return A newly allocated LabelLayoutItem, or @c NULL if validation fails.
 *
 * @see Archive(), LabelLayoutItem(BMessage*)
 */
BArchivable*
BMenuField::LabelLayoutItem::Instantiate(BMessage* from)
{
	if (validate_instantiation(from, "BMenuField::LabelLayoutItem"))
		return new LabelLayoutItem(from);

	return NULL;
}


// #pragma mark - BMenuField::MenuBarLayoutItem


/**
 * @brief Constructs a MenuBarLayoutItem associated with the given BMenuField.
 *
 * Sets an explicit unlimited maximum width so that the menu bar area can
 * expand horizontally without bound inside a layout.
 *
 * @param parent The owning BMenuField; must not be @c NULL.
 */
BMenuField::MenuBarLayoutItem::MenuBarLayoutItem(BMenuField* parent)
	:
	fParent(parent),
	fFrame()
{
	// by default the part right of the divider shall have an unlimited maximum
	// width
	SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
}


/**
 * @brief Constructs a MenuBarLayoutItem from an archived BMessage.
 *
 * Restores the layout frame from kFrameField. The parent pointer is not
 * stored and must be restored via SetParent() after construction.
 *
 * @param from The archive message produced by Archive().
 *
 * @see Archive(), Instantiate(), SetParent()
 */
BMenuField::MenuBarLayoutItem::MenuBarLayoutItem(BMessage* from)
	:
	BAbstractLayoutItem(from),
	fParent(NULL),
	fFrame()
{
	from->FindRect(kFrameField, &fFrame);
}


/**
 * @brief Returns the layout item's frame translated into the parent view's coordinates.
 *
 * @return The menu bar area in the parent view's local coordinate system.
 *
 * @see Frame()
 */
BRect
BMenuField::MenuBarLayoutItem::FrameInParent() const
{
	return fFrame.OffsetByCopy(-fParent->Frame().left, -fParent->Frame().top);
}


/**
 * @brief Returns whether the menu bar area is currently visible.
 *
 * @return @c true if the parent BMenuField is not hidden.
 *
 * @see SetVisible()
 */
bool
BMenuField::MenuBarLayoutItem::IsVisible()
{
	return !fParent->IsHidden(fParent);
}


/**
 * @brief Visibility changes are not permitted for this item.
 *
 * The menu bar visibility is tied to the parent BMenuField; this override
 * is intentionally a no-op.
 *
 * @param visible Ignored.
 */
void
BMenuField::MenuBarLayoutItem::SetVisible(bool visible)
{
	// not allowed
}


/**
 * @brief Returns the absolute frame of this layout item.
 *
 * @return The item frame in the layout's coordinate system.
 *
 * @see SetFrame(), FrameInParent()
 */
BRect
BMenuField::MenuBarLayoutItem::Frame()
{
	return fFrame;
}


/**
 * @brief Updates the layout item's frame and triggers a parent frame update.
 *
 * Stores the new frame and calls BMenuField::_UpdateFrame() so that the
 * parent view repositions and resizes itself.
 *
 * @param frame The new absolute frame assigned by the layout engine.
 *
 * @see Frame(), BMenuField::_UpdateFrame()
 */
void
BMenuField::MenuBarLayoutItem::SetFrame(BRect frame)
{
	fFrame = frame;
	fParent->_UpdateFrame();
}


/**
 * @brief Sets the parent BMenuField after unarchiving.
 *
 * Must be called when a MenuBarLayoutItem is reconstructed from an archive.
 *
 * @param parent The BMenuField that owns this layout item.
 *
 * @see MenuBarLayoutItem(BMessage*)
 */
void
BMenuField::MenuBarLayoutItem::SetParent(BMenuField* parent)
{
	fParent = parent;
}


/**
 * @brief Returns the BView associated with this layout item.
 *
 * @return The parent BMenuField, which is the view drawn for this item.
 *
 * @see SetParent()
 */
BView*
BMenuField::MenuBarLayoutItem::View()
{
	return fParent;
}


/**
 * @brief Returns the minimum size of the menu bar area.
 *
 * Queries the parent's cached menu bar minimum size and adds the vertical
 * margin on both horizontal and vertical sides.
 *
 * @return The minimum BSize for the menu bar area (including margins).
 *
 * @see BaseMaxSize(), BasePreferredSize(), BMenuField::_ValidateLayoutData()
 */
BSize
BMenuField::MenuBarLayoutItem::BaseMinSize()
{
	fParent->_ValidateLayoutData();

	BSize size = fParent->fLayoutData->menu_bar_min;
	size.width += 2 * kVMargin;
	size.height += 2 * kVMargin;

	return size;
}


/**
 * @brief Returns the maximum size of the menu bar area.
 *
 * The height is constrained to the minimum; the width is unlimited so that
 * the menu bar expands to fill all available horizontal space.
 *
 * @return The maximum BSize with an unlimited width.
 *
 * @see BaseMinSize()
 */
BSize
BMenuField::MenuBarLayoutItem::BaseMaxSize()
{
	BSize size(BaseMinSize());
	size.width = B_SIZE_UNLIMITED;

	return size;
}


/**
 * @brief Returns the preferred size of the menu bar area (equals the minimum).
 *
 * @return Same as BaseMinSize().
 *
 * @see BaseMinSize()
 */
BSize
BMenuField::MenuBarLayoutItem::BasePreferredSize()
{
	return BaseMinSize();
}


/**
 * @brief Returns the layout alignment for the menu bar item.
 *
 * Uses full width and full height so that the menu bar fills its cell.
 *
 * @return BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_USE_FULL_HEIGHT).
 */
BAlignment
BMenuField::MenuBarLayoutItem::BaseAlignment()
{
	return BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_USE_FULL_HEIGHT);
}


/**
 * @brief Archives the MenuBarLayoutItem into a BMessage.
 *
 * Stores the base class data and fFrame under kFrameField.
 *
 * @param into The BMessage to archive into.
 * @param deep If @c true, children are archived (unused; no children).
 * @return B_OK on success, or an error from BAbstractLayoutItem::Archive().
 *
 * @see Instantiate(), MenuBarLayoutItem(BMessage*)
 */
status_t
BMenuField::MenuBarLayoutItem::Archive(BMessage* into, bool deep) const
{
	BArchiver archiver(into);
	status_t err = BAbstractLayoutItem::Archive(into, deep);

	if (err == B_OK)
		err = into->AddRect(kFrameField, fFrame);

	return archiver.Finish(err);
}


/**
 * @brief Instantiates a MenuBarLayoutItem from an archive message (BArchivable hook).
 *
 * @param from The archive message produced by Archive().
 * @return A newly allocated MenuBarLayoutItem, or @c NULL if validation fails.
 *
 * @see Archive(), MenuBarLayoutItem(BMessage*)
 */
BArchivable*
BMenuField::MenuBarLayoutItem::Instantiate(BMessage* from)
{
	if (validate_instantiation(from, "BMenuField::MenuBarLayoutItem"))
		return new MenuBarLayoutItem(from);
	return NULL;
}


/**
 * @brief Binary-compatibility trampoline for BMenuField::InvalidateLayout().
 *
 * This C-linkage stub is exported under both the GCC 2 mangled name
 * (InvalidateLayout__10BMenuFieldb) and the GCC 4+ mangled name so that
 * code compiled against older headers can still call InvalidateLayout().
 * It forwards to BMenuField::Perform(PERFORM_CODE_LAYOUT_INVALIDATED, ...).
 *
 * @param field       The BMenuField instance whose layout should be invalidated.
 * @param descendants If @c true, child views are also invalidated.
 */
extern "C" void
B_IF_GCC_2(InvalidateLayout__10BMenuFieldb, _ZN10BMenuField16InvalidateLayoutEb)(
	BMenuField* field, bool descendants)
{
	perform_data_layout_invalidated data;
	data.descendants = descendants;

	field->Perform(PERFORM_CODE_LAYOUT_INVALIDATED, &data);
}
