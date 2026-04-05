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
 *   Copyright 2001-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus, superstippi@gmx.de
 *       Marc Flerackers, mflerackers@androme.be
 *       John Scipione, jcipione@gmail.com
 */


/**
 * @file BMCPrivate.cpp
 * @brief Private implementation classes for BMenuField's embedded menu control
 *
 * Contains BMCMenuBar and BMCPopUpMenu, internal helper classes used by
 * BMenuField to display a compact pop-up menu button within a control layout.
 *
 * @see BMenuField, BMenuBar, BPopUpMenu
 */


#include <BMCPrivate.h>

#include <algorithm>

#include <ControlLook.h>
#include <LayoutUtils.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Window.h>


/** @brief Pixel width reserved for the pop-up indicator triangle at the right edge. */
static const float kPopUpIndicatorWidth = 13.0f;


#if __GNUC__ == 2


// This is kept only for binary compatibility with BeOS R5. This class was
// used in their BMenuField implementation and we may come across some archived
// BMenuField that needs it.
class _BMCItem_: public BMenuItem {
public:
	_BMCItem_(BMessage* data);
	static BArchivable* Instantiate(BMessage *data);
};


_BMCItem_::_BMCItem_(BMessage* data)
	:
	BMenuItem(data)
{
}


/*static*/ BArchivable*
_BMCItem_::Instantiate(BMessage *data) {
	if (validate_instantiation(data, "_BMCItem_"))
		return new _BMCItem_(data);

	return NULL;
}


#endif


//	#pragma mark - _BMCFilter_


/**
 * @brief Construct a message filter for a BMenuField.
 *
 * Intercepts messages of the given \a what code delivered to child views of
 * the BMenuField and redirects them to the parent BMenuField handler.
 *
 * @param menuField  The BMenuField that owns this filter.
 * @param what       The message constant this filter should intercept.
 */
_BMCFilter_::_BMCFilter_(BMenuField* menuField, uint32 what)
	:
	BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE, what),
	fMenuField(menuField)
{
}


/**
 * @brief Destroy the _BMCFilter_.
 */
_BMCFilter_::~_BMCFilter_()
{
}


/**
 * @brief Intercept B_MOUSE_DOWN messages and re-route them to the parent BMenuField.
 *
 * Translates the hit point from the child view's coordinate system to the
 * parent's, then redirects the handler to the BMenuField so that clicks on
 * child views (such as the embedded menu bar) are handled centrally.
 *
 * @param message  The incoming BMessage to examine.
 * @param handler  In/out pointer to the message handler; may be replaced.
 * @return B_DISPATCH_MESSAGE always, so the redirected message is still sent.
 */
filter_result
_BMCFilter_::Filter(BMessage* message, BHandler** handler)
{
	if (message->what == B_MOUSE_DOWN) {
		if (BView* view = dynamic_cast<BView*>(*handler)) {
			BPoint point;
			message->FindPoint("be:view_where", &point);
			view->ConvertToParent(&point);
			message->ReplacePoint("be:view_where", point);
			*handler = fMenuField;
		}
	}

	return B_DISPATCH_MESSAGE;
}


//	#pragma mark - _BMCMenuBar_


/**
 * @brief Construct a frame-based _BMCMenuBar_ for a BMenuField.
 *
 * Used when the BMenuField is created with an explicit BRect. The \a fixedSize
 * flag determines whether the bar resizes with its content or is pinned to the
 * field's width.
 *
 * @param frame      Initial frame rectangle in the parent's coordinate system.
 * @param fixedSize  If true, the bar width is fixed to the menu field's width.
 * @param menuField  The owning BMenuField.
 */
_BMCMenuBar_::_BMCMenuBar_(BRect frame, bool fixedSize, BMenuField* menuField)
	:
	BMenuBar(frame, "_mc_mb_", B_FOLLOW_LEFT | B_FOLLOW_TOP, B_ITEMS_IN_ROW,
		!fixedSize),
	fMenuField(menuField),
	fFixedSize(fixedSize),
	fShowPopUpMarker(true),
	fIsInside(false)
{
	_Init();
}


/**
 * @brief Construct a layout-based _BMCMenuBar_ for a BMenuField.
 *
 * Used when the BMenuField participates in a layout manager. The bar is
 * always fixed-size in this mode and relies on the layout for sizing.
 *
 * @param menuField  The owning BMenuField.
 */
_BMCMenuBar_::_BMCMenuBar_(BMenuField* menuField)
	:
	BMenuBar("_mc_mb_", B_ITEMS_IN_ROW),
	fMenuField(menuField),
	fFixedSize(true),
	fShowPopUpMarker(true),
	fIsInside(false)
{
	_Init();
}


/**
 * @brief Unarchive constructor: restore a _BMCMenuBar_ from a BMessage.
 *
 * Reads the resize-to-fit flag from the archive to reconstruct fFixedSize.
 *
 * @param data  The archive message produced by Archive().
 * @see Instantiate()
 */
_BMCMenuBar_::_BMCMenuBar_(BMessage* data)
	:
	BMenuBar(data),
	fMenuField(NULL),
	fFixedSize(true),
	fShowPopUpMarker(true),
	fIsInside(false)
{
	SetFlags(Flags() | B_FRAME_EVENTS);

	bool resizeToFit;
	if (data->FindBool("_rsize_to_fit", &resizeToFit) == B_OK)
		fFixedSize = !resizeToFit;
}


/**
 * @brief Destroy the _BMCMenuBar_.
 */
_BMCMenuBar_::~_BMCMenuBar_()
{
}


//	#pragma mark - _BMCMenuBar_ public methods


/**
 * @brief Create a new _BMCMenuBar_ from an archived BMessage.
 *
 * @param data  The archive message to instantiate from.
 * @return A new _BMCMenuBar_ if \a data is valid, or NULL if validation fails.
 * @see Archive()
 */
BArchivable*
_BMCMenuBar_::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "_BMCMenuBar_"))
		return new _BMCMenuBar_(data);

	return NULL;
}


/**
 * @brief Finish attaching the menu bar to its window.
 *
 * Resolves the owning BMenuField from the parent view, restores the window's
 * key menu bar (to prevent this embedded bar from hijacking it), configures
 * the resizing mode for non-layout use, and records the initial width for
 * FrameResized() delta calculations.
 */
void
_BMCMenuBar_::AttachedToWindow()
{
	fMenuField = static_cast<BMenuField*>(Parent());

	// Don't cause the KeyMenuBar to change by being attached
	BMenuBar* menuBar = Window()->KeyMenuBar();
	BMenuBar::AttachedToWindow();
	Window()->SetKeyMenuBar(menuBar);

	if (fFixedSize && (Flags() & B_SUPPORTS_LAYOUT) == 0)
		SetResizingMode(B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP);

	SetLowUIColor(B_CONTROL_BACKGROUND_COLOR);

	fPreviousWidth = Bounds().Width();
}


/**
 * @brief Draw the menu bar background, pop-up indicator, and menu items.
 *
 * In non-layout (legacy) mode the bar is first resized to match the menu
 * field's allocated width. The control-look background is then painted with
 * the current enabled/focused/hovered flags, followed by the menu items.
 *
 * @param updateRect  The rectangle that needs repainting.
 */
void
_BMCMenuBar_::Draw(BRect updateRect)
{
	if ((Flags() & B_SUPPORTS_LAYOUT) == 0) {
		if (fFixedSize) {
			// Set the width of the menu bar because the menu bar bounds may have
			// been expanded by the selected menu item.
			ResizeTo(fMenuField->_MenuBarWidth(), Bounds().Height());
		} else {
			// For compatability with BeOS R5:
			//  - Set to the minimum of the menu bar width set by the menu frame
			//    and the selected menu item width.
			//  - Set the height to the preferred height ignoring the height of the
			//    menu field.
			float height;
			BMenuBar::GetPreferredSize(NULL, &height);
			ResizeTo(std::min(Bounds().Width(), fMenuField->_MenuBarWidth()),
				height);
		}
	}

	BRect rect(Bounds());
	uint32 flags = 0;
	if (!IsEnabled())
		flags |= BControlLook::B_DISABLED;
	if (IsFocus())
		flags |= BControlLook::B_FOCUSED;
	if (fIsInside)
		flags |= BControlLook::B_HOVER;

	be_control_look->DrawMenuFieldBackground(this, rect,
		updateRect, LowColor(), fShowPopUpMarker, flags);

	DrawItems(updateRect);
}


/**
 * @brief Track mouse movement to update the hover state and trigger a repaint.
 *
 * Sets fIsInside based on whether the pointer is within the view bounds, and
 * calls Invalidate() when the state changes so the hover highlight is updated.
 *
 * @param where        Current mouse position in view coordinates.
 * @param code         Transit code (B_ENTERED_VIEW, B_EXITED_VIEW, etc.).
 * @param dragMessage  Drag-and-drop message, or NULL if none.
 */
void
_BMCMenuBar_::MouseMoved(BPoint where, uint32 code, const BMessage* dragMessage)
{
	bool inside = (code != B_EXITED_VIEW) && Bounds().Contains(where);
	if (inside != fIsInside) {
		fIsInside = inside;
		Invalidate();
	}
}


/**
 * @brief Handle a resize of the menu bar and keep the parent BMenuField in sync.
 *
 * Computes the width delta since the last resize and invalidates the
 * appropriate portion of the parent BMenuField so that its border decorations
 * are repainted cleanly.
 *
 * @param width   New width of the menu bar.
 * @param height  New height of the menu bar.
 */
void
_BMCMenuBar_::FrameResized(float width, float height)
{
	// we need to take care of cleaning up the parent menu field
	float diff = width - fPreviousWidth;
	fPreviousWidth = width;

	if (Window() != NULL && diff != 0) {
		BRect dirty(fMenuField->Bounds());
		if (diff > 0) {
			// clean up the dirty right border of
			// the menu field when enlarging
			dirty.right = Frame().right + kVMargin;
			dirty.left = dirty.right - diff - kVMargin * 2;
			fMenuField->Invalidate(dirty);
		} else if (diff < 0) {
			// clean up the dirty right line of
			// the menu field when shrinking
			dirty.left = Frame().right - kVMargin;
			fMenuField->Invalidate(dirty);
		}
	}

	BMenuBar::FrameResized(width, height);
}


/**
 * @brief Transfer keyboard focus to or from this menu bar.
 *
 * Guards against redundant focus changes by checking the current focus state
 * before forwarding to BMenuBar::MakeFocus().
 *
 * @param focused  true to acquire focus, false to release it.
 */
void
_BMCMenuBar_::MakeFocus(bool focused)
{
	if (IsFocus() == focused)
		return;

	BMenuBar::MakeFocus(focused);
}


/**
 * @brief Handle internal and inherited messages for the menu bar.
 *
 * The 'TICK' message is sent by a BMessageRunner when the pop-up menu has
 * been open too long; it synthesizes a B_ESCAPE key event to close it.
 * All other messages are forwarded to BMenuBar::MessageReceived().
 *
 * @param message  The BMessage to process.
 */
void
_BMCMenuBar_::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case 'TICK':
		{
			BMenuItem* item = ItemAt(0);

			if (item != NULL && item->Submenu() != NULL
				&& item->Submenu()->Window() != NULL) {
				BMessage message(B_KEY_DOWN);

				message.AddInt8("byte", B_ESCAPE);
				message.AddInt8("key", B_ESCAPE);
				message.AddInt32("modifiers", 0);
				message.AddInt8("raw_char", B_ESCAPE);

				Window()->PostMessage(&message, this, NULL);
			}
		}
		// fall through
		default:
			BMenuBar::MessageReceived(message);
			break;
	}
}


/**
 * @brief Set the maximum pixel width for item content, accounting for item margins.
 *
 * Subtracts the left and right item margins from \a width before forwarding to
 * BMenuBar::SetMaxContentWidth() so that the pop-up indicator is not
 * overdrawn by the item label.
 *
 * @param width  The desired maximum content width in pixels.
 */
void
_BMCMenuBar_::SetMaxContentWidth(float width)
{
	float left;
	float right;
	GetItemMargins(&left, NULL, &right, NULL);

	BMenuBar::SetMaxContentWidth(width - (left + right));
}


/**
 * @brief Enable or disable the menu bar and its owning BMenuField together.
 *
 * Forwards the enabled state to the parent BMenuField before calling the
 * base-class implementation so that both are kept in sync.
 *
 * @param enabled  true to enable, false to disable.
 */
void
_BMCMenuBar_::SetEnabled(bool enabled)
{
	fMenuField->SetEnabled(enabled);

	BMenuBar::SetEnabled(enabled);
}


/**
 * @brief Return the minimum size needed to display the menu bar.
 *
 * Adds the pop-up indicator width to the BMenuBar preferred size and composes
 * the result with any explicit minimum size set on the view.
 *
 * @return The minimum BSize for this view.
 */
BSize
_BMCMenuBar_::MinSize()
{
	BSize size;
	BMenuBar::GetPreferredSize(&size.width, &size.height);
	if (fShowPopUpMarker) {
		// account for popup indicator + a few pixels margin
		size.width += kPopUpIndicatorWidth;
	}

	return BLayoutUtils::ComposeSize(ExplicitMinSize(), size);
}


/**
 * @brief Return the maximum size of the menu bar.
 *
 * Limits the maximum width to the preferred width (unlike the unlimited default
 * BMenuBar behaviour) so the BMenuField can constrain its embedded bar.
 *
 * @return The maximum BSize for this view.
 */
BSize
_BMCMenuBar_::MaxSize()
{
	// The maximum width of a normal BMenuBar is unlimited, but we want it
	// limited.
	BSize size;
	BMenuBar::GetPreferredSize(&size.width, &size.height);

	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), size);
}


//	#pragma mark - _BMCMenuBar_ private methods


/**
 * @brief Shared initialisation called by all constructors.
 *
 * Sets the B_FRAME_EVENTS and B_FULL_UPDATE_ON_RESIZE flags, applies a
 * B_BORDER_CONTENTS border style, and adjusts the item margins so that the
 * label is vertically centred and the pop-up indicator fits within the right
 * margin.
 */
void
_BMCMenuBar_::_Init()
{
	SetFlags(Flags() | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE);
	SetBorder(B_BORDER_CONTENTS);

	float left, top, right, bottom;
	GetItemMargins(&left, &top, &right, &bottom);

#if 0
	// TODO: Better fix would be to make BMenuItem draw text properly
	// centered
	font_height fontHeight;
	GetFontHeight(&fontHeight);
	top = ceilf((Bounds().Height() - ceilf(fontHeight.ascent)
		- ceilf(fontHeight.descent)) / 2) + 1;
	bottom = top - 1;
#else
	// TODO: Fix content location properly. This is just a quick fix to
	// make the BMenuField label and the super-item of the BMenuBar
	// align vertically.
	top++;
	bottom--;
#endif

	left = right = be_control_look->DefaultLabelSpacing();

	SetItemMargins(left, top,
		right + (fShowPopUpMarker ? kPopUpIndicatorWidth : 0), bottom);
}
