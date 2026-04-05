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
 *   Copyright 2001-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marc Flerackers, mflerackers@androme.be
 *       Stefano Ceccherini, burton666@libero.it
 *       Stephan Aßmus, superstippi@gmx.de
 */


/**
 * @file MenuBar.cpp
 * @brief Implementation of BMenuBar, the top-level menu bar for a window
 *
 * BMenuBar is a specialized BMenu that sits at the top of a BWindow. It
 * displays its top-level items horizontally and opens submenus downward. It
 * handles mouse tracking and keyboard shortcut dispatch for its entire menu
 * hierarchy.
 *
 * @see BMenu, BMenuItem, BWindow
 */


#include <MenuBar.h>

#include <math.h>

#include <Application.h>
#include <Autolock.h>
#include <ControlLook.h>
#include <LayoutUtils.h>
#include <MenuItem.h>
#include <Window.h>

#include <AppMisc.h>
#include <binary_compatibility/Interface.h>
#include <MenuPrivate.h>
#include <TokenSpace.h>
#include <InterfaceDefs.h>

#include "BMCPrivate.h"


using BPrivate::gDefaultTokens;


/** @brief Transient data block passed from the window thread to the tracking thread. */
struct menubar_data {
	BMenuBar*	menuBar;   /**< @brief The menu bar initiating the tracking session. */
	int32		menuIndex; /**< @brief Index of the item to pre-select, or -1 for none. */

	bool		sticky;    /**< @brief Whether the menu bar starts in sticky mode. */
	bool		showMenu;  /**< @brief Whether to open the selected submenu immediately. */

	bool		useRect;   /**< @brief Whether @c rect contains a valid constraint rectangle. */
	BRect		rect;      /**< @brief Optional extra rectangle used to keep sticky mode active. */
};


/**
 * @brief Construct a BMenuBar with an explicit frame rectangle.
 *
 * @param frame        The position and size of the menu bar in its parent's
 *                     coordinate system.
 * @param name         The view name.
 * @param resizingMode Combination of B_FOLLOW_* flags controlling automatic
 *                     resizing when the parent is resized.
 * @param layout       Item layout, typically B_ITEMS_IN_ROW for a horizontal bar.
 * @param resizeToFit  If true the bar resizes itself to exactly fit its items.
 */
BMenuBar::BMenuBar(BRect frame, const char* name, uint32 resizingMode,
		menu_layout layout, bool resizeToFit)
	:
	BMenu(frame, name, resizingMode, B_WILL_DRAW | B_FRAME_EVENTS
		| B_FULL_UPDATE_ON_RESIZE, layout, resizeToFit),
	fBorder(B_BORDER_FRAME),
	fTrackingPID(-1),
	fPrevFocusToken(-1),
	fMenuSem(-1),
	fLastBounds(NULL),
	fTracking(false)
{
	_InitData(layout);
}


/**
 * @brief Construct a layout-aware BMenuBar without an explicit frame.
 *
 * This constructor is intended for use with the layout system. The bar's
 * frame is managed by the layout engine rather than set at construction time.
 *
 * @param name   The view name.
 * @param layout Item layout, typically B_ITEMS_IN_ROW.
 * @param flags  Additional view flags merged with B_WILL_DRAW, B_FRAME_EVENTS,
 *               and B_SUPPORTS_LAYOUT.
 */
BMenuBar::BMenuBar(const char* name, menu_layout layout, uint32 flags)
	:
	BMenu(BRect(), name, B_FOLLOW_NONE,
		flags | B_WILL_DRAW | B_FRAME_EVENTS | B_SUPPORTS_LAYOUT,
		layout, false),
	fBorder(B_BORDER_FRAME),
	fTrackingPID(-1),
	fPrevFocusToken(-1),
	fMenuSem(-1),
	fLastBounds(NULL),
	fTracking(false)
{
	_InitData(layout);
}


/**
 * @brief Construct a BMenuBar from an archived BMessage.
 *
 * Restores border style and item layout from the archive before delegating
 * the rest of the reconstruction to BMenu.
 *
 * @param archive The archive message produced by Archive().
 * @see Instantiate()
 * @see Archive()
 */
BMenuBar::BMenuBar(BMessage* archive)
	:
	BMenu(archive),
	fBorder(B_BORDER_FRAME),
	fTrackingPID(-1),
	fPrevFocusToken(-1),
	fMenuSem(-1),
	fLastBounds(NULL),
	fTracking(false)
{
	int32 border;

	if (archive->FindInt32("_border", &border) == B_OK)
		SetBorder((menu_bar_border)border);

	menu_layout layout = B_ITEMS_IN_COLUMN;
	archive->FindInt32("_layout", (int32*)&layout);

	_InitData(layout);
}


/**
 * @brief Destroy the BMenuBar.
 *
 * Waits for any in-progress tracking thread to finish before releasing
 * resources.
 */
BMenuBar::~BMenuBar()
{
	if (fTracking) {
		status_t dummy;
		wait_for_thread(fTrackingPID, &dummy);
	}

	delete fLastBounds;
}


/**
 * @brief Create a BMenuBar instance from an archived BMessage.
 *
 * @param data The archive message to instantiate from.
 * @return A newly allocated BMenuBar on success, or NULL if @a data does not
 *         represent a valid BMenuBar archive.
 * @see Archive()
 */
BArchivable*
BMenuBar::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "BMenuBar"))
		return new BMenuBar(data);

	return NULL;
}


/**
 * @brief Archive the BMenuBar into a BMessage.
 *
 * Stores the border style (only when it differs from the default
 * B_BORDER_FRAME) in addition to all data archived by BMenu.
 *
 * @param data  The message to archive into.
 * @param deep  If true, menu items are archived recursively.
 * @return B_OK on success, or an error code on failure.
 * @see Instantiate()
 */
status_t
BMenuBar::Archive(BMessage* data, bool deep) const
{
	status_t err = BMenu::Archive(data, deep);

	if (err < B_OK)
		return err;

	if (Border() != B_BORDER_FRAME)
		err = data->AddInt32("_border", Border());

	return err;
}


// #pragma mark -


/**
 * @brief Hook called when the bar is added to a window.
 *
 * Registers this bar as the key menu bar of its window so that keyboard
 * shortcuts are routed through it, then delegates to BMenu::AttachedToWindow()
 * and snapshots the current bounds for later resize-delta tracking.
 *
 * @see BWindow::SetKeyMenuBar()
 */
void
BMenuBar::AttachedToWindow()
{
	Window()->SetKeyMenuBar(this);

	BMenu::AttachedToWindow();

	*fLastBounds = Bounds();
}


/**
 * @brief Hook called when the bar is removed from its window.
 *
 * Delegates directly to BMenu::DetachedFromWindow().
 */
void
BMenuBar::DetachedFromWindow()
{
	BMenu::DetachedFromWindow();
}


/**
 * @brief Hook called after all views in the hierarchy have been attached.
 *
 * Delegates directly to BMenu::AllAttached().
 */
void
BMenuBar::AllAttached()
{
	BMenu::AllAttached();
}


/**
 * @brief Hook called after all views in the hierarchy have been detached.
 *
 * Delegates directly to BMenu::AllDetached().
 */
void
BMenuBar::AllDetached()
{
	BMenu::AllDetached();
}


/**
 * @brief Hook called when the parent window gains or loses focus.
 *
 * @param state True if the window is now active, false if it lost activation.
 */
void
BMenuBar::WindowActivated(bool state)
{
	BView::WindowActivated(state);
}


/**
 * @brief Set or remove keyboard focus for this menu bar.
 *
 * @param state True to give focus, false to relinquish it.
 */
void
BMenuBar::MakeFocus(bool state)
{
	BMenu::MakeFocus(state);
}


// #pragma mark -


/**
 * @brief Resize the menu bar to its preferred size.
 *
 * Delegates directly to BMenu::ResizeToPreferred().
 */
void
BMenuBar::ResizeToPreferred()
{
	BMenu::ResizeToPreferred();
}


/**
 * @brief Return the preferred width and height of the menu bar.
 *
 * @param width  Set to the preferred width in pixels.
 * @param height Set to the preferred height in pixels.
 */
void
BMenuBar::GetPreferredSize(float* width, float* height)
{
	BMenu::GetPreferredSize(width, height);
}


/**
 * @brief Return the minimum layout size of the menu bar.
 *
 * @return The smallest BSize the bar may be given by the layout engine.
 */
BSize
BMenuBar::MinSize()
{
	return BMenu::MinSize();
}


/**
 * @brief Return the maximum layout size of the menu bar.
 *
 * The width is unconstrained (B_SIZE_UNLIMITED) so the bar can expand to
 * fill the full window width, while the height is taken from BMenu::MaxSize().
 *
 * @return The largest BSize the bar should be given by the layout engine.
 */
BSize
BMenuBar::MaxSize()
{
	BSize size = BMenu::MaxSize();
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(),
		BSize(B_SIZE_UNLIMITED, size.height));
}


/**
 * @brief Return the preferred layout size of the menu bar.
 *
 * @return The ideal BSize for the bar as determined by BMenu.
 */
BSize
BMenuBar::PreferredSize()
{
	return BMenu::PreferredSize();
}


/**
 * @brief Hook called when the bar's frame position changes.
 *
 * @param newPosition The new top-left corner of the bar in its parent's
 *                    coordinate system.
 */
void
BMenuBar::FrameMoved(BPoint newPosition)
{
	BMenu::FrameMoved(newPosition);
}


/**
 * @brief Hook called when the bar's frame size changes.
 *
 * Invalidates only the pixel strips along the right and bottom edges that
 * changed, avoiding a full redraw. Updates the cached bounds snapshot used
 * for future comparisons.
 *
 * @param newWidth  The new width of the bar in pixels.
 * @param newHeight The new height of the bar in pixels.
 */
void
BMenuBar::FrameResized(float newWidth, float newHeight)
{
	// invalidate right border
	if (newWidth != fLastBounds->Width()) {
		BRect rect(min_c(fLastBounds->right, newWidth), 0,
			max_c(fLastBounds->right, newWidth), newHeight);
		Invalidate(rect);
	}

	// invalidate bottom border
	if (newHeight != fLastBounds->Height()) {
		BRect rect(0, min_c(fLastBounds->bottom, newHeight) - 1,
			newWidth, max_c(fLastBounds->bottom, newHeight));
		Invalidate(rect);
	}

	fLastBounds->Set(0, 0, newWidth, newHeight);

	BMenu::FrameResized(newWidth, newHeight);
}


// #pragma mark -


/**
 * @brief Make the menu bar and its children visible.
 *
 * Delegates directly to BView::Show().
 */
void
BMenuBar::Show()
{
	BView::Show();
}


/**
 * @brief Hide the menu bar and its children.
 *
 * Delegates directly to BView::Hide().
 */
void
BMenuBar::Hide()
{
	BView::Hide();
}


/**
 * @brief Draw the menu bar within the given update rectangle.
 *
 * Triggers a relayout when needed, then draws the bottom border, the menu
 * bar background, and finally all visible menu items.
 *
 * @param updateRect The portion of the bar that needs to be redrawn.
 */
void
BMenuBar::Draw(BRect updateRect)
{
	if (_RelayoutIfNeeded()) {
		Invalidate();
		return;
	}

	BRect rect(Bounds());
	rgb_color base = LowColor();
	uint32 flags = 0;

	be_control_look->DrawBorder(this, rect, updateRect, base,
		B_PLAIN_BORDER, flags, BControlLook::B_BOTTOM_BORDER);

	be_control_look->DrawMenuBarBackground(this, rect, updateRect, base,
		0, fBorders);

	DrawItems(updateRect);
}


// #pragma mark -


/**
 * @brief Handle an incoming message.
 *
 * Delegates directly to BMenu::MessageReceived().
 *
 * @param message The message to process.
 */
void
BMenuBar::MessageReceived(BMessage* message)
{
	BMenu::MessageReceived(message);
}


/**
 * @brief Handle a mouse-button-down event.
 *
 * Ignores the event if tracking is already in progress. When the window is
 * not active or not in front, activates it according to the current mouse
 * focus mode before starting menu tracking.
 *
 * @param where The position of the cursor in view coordinates.
 */
void
BMenuBar::MouseDown(BPoint where)
{
	if (fTracking)
		return;

	uint32 buttons;
	GetMouse(&where, &buttons);

	BWindow* window = Window();
	if (!window->IsActive() || !window->IsFront()) {
		if ((mouse_mode() == B_FOCUS_FOLLOWS_MOUSE)
			|| ((mouse_mode() == B_CLICK_TO_FOCUS_MOUSE)
				&& ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0))) {
			// right-click to bring-to-front and send-to-back
			// (might cause some regressions in FFM)
			window->Activate();
			window->UpdateIfNeeded();
		}
	}

	StartMenuBar(-1, false, false);
}


/**
 * @brief Handle a mouse-button-up event.
 *
 * Delegates directly to BView::MouseUp().
 *
 * @param where The position of the cursor in view coordinates at the time of
 *              the release.
 */
void
BMenuBar::MouseUp(BPoint where)
{
	BView::MouseUp(where);
}


// #pragma mark -


/**
 * @brief Resolve a scripting specifier to a target handler.
 *
 * Delegates directly to BMenu::ResolveSpecifier().
 *
 * @param msg       The scripting message.
 * @param index     The specifier index.
 * @param specifier The current specifier message.
 * @param form      The specifier form constant.
 * @param property  The property name string.
 * @return The handler that should process the scripting message.
 */
BHandler*
BMenuBar::ResolveSpecifier(BMessage* msg, int32 index, BMessage* specifier,
	int32 form, const char* property)
{
	return BMenu::ResolveSpecifier(msg, index, specifier, form, property);
}


/**
 * @brief Fill a BMessage with the scripting suites this object supports.
 *
 * Delegates directly to BMenu::GetSupportedSuites().
 *
 * @param data The message to populate with suite information.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BMenuBar::GetSupportedSuites(BMessage* data)
{
	return BMenu::GetSupportedSuites(data);
}


// #pragma mark -


/**
 * @brief Set which edges of the menu bar draw a visible border.
 *
 * @param border One of B_BORDER_FRAME, B_BORDER_CONTENTS, or B_BORDER_EACH_ITEM.
 * @see Border()
 */
void
BMenuBar::SetBorder(menu_bar_border border)
{
	fBorder = border;
}


/**
 * @brief Return the current border style of the menu bar.
 *
 * @return The current menu_bar_border value.
 * @see SetBorder()
 */
menu_bar_border
BMenuBar::Border() const
{
	return fBorder;
}


/**
 * @brief Set which individual sides of the bar draw their border lines.
 *
 * @param borders Bitfield of BControlLook border constants (e.g.
 *                B_TOP_BORDER | B_BOTTOM_BORDER).
 * @see Borders()
 */
void
BMenuBar::SetBorders(uint32 borders)
{
	fBorders = borders;
}


/**
 * @brief Return the bitmask of active border sides.
 *
 * @return A bitfield of BControlLook border constants.
 * @see SetBorders()
 */
uint32
BMenuBar::Borders() const
{
	return fBorders;
}


// #pragma mark -


/**
 * @brief Dispatch a binary-compatibility perform code.
 *
 * Handles perform codes for MinSize, MaxSize, PreferredSize, LayoutAlignment,
 * HasHeightForWidth, GetHeightForWidth, SetLayout, LayoutInvalidated, and
 * DoLayout. Unrecognized codes are forwarded to BMenu::Perform().
 *
 * @param code  The perform code constant (PERFORM_CODE_*).
 * @param _data Pointer to the code-specific data structure.
 * @return B_OK for recognized codes, or the result from BMenu::Perform().
 */
status_t
BMenuBar::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_MIN_SIZE:
			((perform_data_min_size*)_data)->return_value
				= BMenuBar::MinSize();
			return B_OK;

		case PERFORM_CODE_MAX_SIZE:
			((perform_data_max_size*)_data)->return_value
				= BMenuBar::MaxSize();
			return B_OK;

		case PERFORM_CODE_PREFERRED_SIZE:
			((perform_data_preferred_size*)_data)->return_value
				= BMenuBar::PreferredSize();
			return B_OK;

		case PERFORM_CODE_LAYOUT_ALIGNMENT:
			((perform_data_layout_alignment*)_data)->return_value
				= BMenuBar::LayoutAlignment();
			return B_OK;

		case PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH:
			((perform_data_has_height_for_width*)_data)->return_value
				= BMenuBar::HasHeightForWidth();
			return B_OK;

		case PERFORM_CODE_GET_HEIGHT_FOR_WIDTH:
		{
			perform_data_get_height_for_width* data
				= (perform_data_get_height_for_width*)_data;
			BMenuBar::GetHeightForWidth(data->width, &data->min, &data->max,
				&data->preferred);
			return B_OK;
		}

		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BMenuBar::SetLayout(data->layout);
			return B_OK;
		}

		case PERFORM_CODE_LAYOUT_INVALIDATED:
		{
			perform_data_layout_invalidated* data
				= (perform_data_layout_invalidated*)_data;
			BMenuBar::LayoutInvalidated(data->descendants);
			return B_OK;
		}

		case PERFORM_CODE_DO_LAYOUT:
		{
			BMenuBar::DoLayout();
			return B_OK;
		}
	}

	return BMenu::Perform(code, _data);
}


// #pragma mark -


void BMenuBar::_ReservedMenuBar1() {}
void BMenuBar::_ReservedMenuBar2() {}
void BMenuBar::_ReservedMenuBar3() {}
void BMenuBar::_ReservedMenuBar4() {}


BMenuBar&
BMenuBar::operator=(const BMenuBar &)
{
	return *this;
}


// #pragma mark -


/**
 * @brief Begin a menu-tracking session, optionally pre-selecting an item.
 *
 * Creates a semaphore used as a window-close gate, spawns the tracking thread
 * via _TrackTask(), and sends the session parameters to it. Must be called
 * from the window's thread.
 *
 * @param menuIndex   Index of the top-level item to open first, or -1 to let
 *                    the user choose.
 * @param sticky      If true the menus stay open until an item is explicitly
 *                    chosen or the bar is clicked again.
 * @param showMenu    If true the submenu under @a menuIndex is opened
 *                    immediately.
 * @param specialRect Optional rectangle; while the mouse remains inside it
 *                    the bar stays in sticky mode. Pass NULL to disable.
 * @note The function is a no-op when tracking is already active.
 * @see _TrackTask()
 */
void
BMenuBar::StartMenuBar(int32 menuIndex, bool sticky, bool showMenu,
	BRect* specialRect)
{
	if (fTracking)
		return;

	BWindow* window = Window();
	if (window == NULL)
		debugger("MenuBar must be added to a window before it can be used.");

	BAutolock lock(window);
	if (!lock.IsLocked())
		return;

	fPrevFocusToken = -1;
	fTracking = true;

	// We are called from the window's thread,
	// so let's call MenusBeginning() directly
	window->MenusBeginning();

	fMenuSem = create_sem(0, "window close sem");
	_set_menu_sem_(window, fMenuSem);

	fTrackingPID = spawn_thread(_TrackTask, "menu_tracking", B_DISPLAY_PRIORITY, NULL);
	if (fTrackingPID >= 0) {
		menubar_data data;
		data.menuBar = this;
		data.menuIndex = menuIndex;
		data.sticky = sticky;
		data.showMenu = showMenu;
		data.useRect = specialRect != NULL;
		if (data.useRect)
			data.rect = *specialRect;

		resume_thread(fTrackingPID);
		send_data(fTrackingPID, 0, &data, sizeof(data));
	} else {
		fTracking = false;
		_set_menu_sem_(window, B_NO_MORE_SEMS);
		delete_sem(fMenuSem);
	}
}


/**
 * @brief Thread entry point that drives the menu-tracking loop.
 *
 * Receives a menubar_data block from the spawning thread, configures the
 * BMenuBar accordingly, and calls _Track(). On completion it posts
 * _MENUS_DONE_ to the window (instead of calling MenusEnded() directly,
 * since this is not the window thread), cleans up the semaphore, and exits.
 *
 * @param arg Unused; the session data is received via receive_data().
 * @return Always returns 0.
 * @see StartMenuBar()
 * @see _Track()
 */
/*static*/ int32
BMenuBar::_TrackTask(void* arg)
{
	menubar_data data;
	thread_id id;
	receive_data(&id, &data, sizeof(data));

	BMenuBar* menuBar = data.menuBar;
	if (data.useRect)
		menuBar->fExtraRect = &data.rect;
	menuBar->_SetStickyMode(data.sticky);

	int32 action;
	menuBar->_Track(&action, data.menuIndex, data.showMenu);

	menuBar->fTracking = false;
	menuBar->fExtraRect = NULL;

	// We aren't the BWindow thread, so don't call MenusEnded() directly
	BWindow* window = menuBar->Window();
	window->PostMessage(_MENUS_DONE_);

	_set_menu_sem_(window, B_BAD_SEM_ID);
	delete_sem(menuBar->fMenuSem);
	menuBar->fMenuSem = B_BAD_SEM_ID;

	return 0;
}


/**
 * @brief Run the interactive tracking loop for the menu bar.
 *
 * Polls mouse position and button state, handles item selection, submenu
 * delegation, and sticky/non-sticky mode transitions. Returns when the user
 * makes a final choice or dismisses the menu hierarchy.
 *
 * @param action      Receives the final MENU_STATE_* constant that ended
 *                    tracking, or NULL to discard.
 * @param startIndex  Index of the item to select at the start of tracking, or
 *                    -1 for no pre-selection.
 * @param showMenu    If true, open the submenu of @a startIndex immediately.
 * @return The chosen BMenuItem, or NULL if the user dismissed without choosing.
 * @see StartMenuBar()
 */
BMenuItem*
BMenuBar::_Track(int32* action, int32 startIndex, bool showMenu)
{
	// TODO: Cleanup, merge some "if" blocks if possible
	BMenuItem* item = NULL;
	fState = MENU_STATE_TRACKING;
	fChosenItem = NULL;
		// we will use this for keyboard selection

	BPoint where;
	uint32 buttons;
	if (LockLooper()) {
		if (startIndex != -1) {
			be_app->ObscureCursor();
			_SelectItem(ItemAt(startIndex), true, false);
		}
		GetMouse(&where, &buttons);
		UnlockLooper();
	}

	while (fState != MENU_STATE_CLOSED) {
		bigtime_t snoozeAmount = 40000;
		if (!LockLooper())
			break;

		item = dynamic_cast<_BMCMenuBar_*>(this) != NULL ? ItemAt(0)
			: _HitTestItems(where, B_ORIGIN);

		if (_OverSubmenu(fSelected, ConvertToScreen(where))
			|| fState == MENU_STATE_KEY_TO_SUBMENU) {
			// call _Track() from the selected sub-menu when the mouse cursor
			// is over its window
			BMenu* submenu = fSelected->Submenu();
			UnlockLooper();
			snoozeAmount = 30000;
			submenu->_SetStickyMode(_IsStickyMode());
			int localAction;
			fChosenItem = submenu->_Track(&localAction);

			// The mouse could have meen moved since the last time we
			// checked its position, or buttons might have been pressed.
			// Unfortunately our child menus don't tell
			// us the new position.
			// TODO: Maybe have a shared struct between all menus
			// where to store the current mouse position ?
			// (Or just use the BView mouse hooks)
			BPoint newWhere;
			if (LockLooper()) {
				GetMouse(&newWhere, &buttons);
				UnlockLooper();
			}

			// Needed to make BMenuField child menus "sticky"
			// (see ticket #953)
			if (localAction == MENU_STATE_CLOSED) {
				if (fExtraRect != NULL && fExtraRect->Contains(where)
					&& point_distance(newWhere, where) < 9) {
					// 9 = 3 pixels ^ 2 (since point_distance() returns the
					// square of the distance)
					_SetStickyMode(true);
					fExtraRect = NULL;
				} else
					fState = MENU_STATE_CLOSED;
			}
			if (!LockLooper())
				break;
		} else if (item != NULL) {
			if (item->Submenu() != NULL && item != fSelected) {
				if (item->Submenu()->Window() == NULL) {
					// open the menu if it's not opened yet
					_SelectItem(item);
				} else {
					// Menu was already opened, close it and bail
					_SelectItem(NULL);
					fState = MENU_STATE_CLOSED;
					fChosenItem = NULL;
				}
			} else {
				// No submenu, just select the item
				_SelectItem(item);
			}
		} else if (item == NULL && fSelected != NULL
			&& !_IsStickyMode() && Bounds().Contains(where)) {
			_SelectItem(NULL);
			fState = MENU_STATE_TRACKING;
		}

		UnlockLooper();

		if (fState != MENU_STATE_CLOSED) {
			BPoint newWhere = where;
			uint32 newButtons = buttons;

			do {
				// If user doesn't move the mouse or change buttons loop
				// here so that we don't interfere with keyboard menu
				// navigation
				snooze(snoozeAmount);
				if (!LockLooper())
					break;

				GetMouse(&newWhere, &newButtons);
				UnlockLooper();
			} while (newWhere == where && newButtons == buttons
				&& fState == MENU_STATE_TRACKING);

			if (newButtons != 0 && _IsStickyMode()) {
				if (item == NULL || (item->Submenu() != NULL
						&& item->Submenu()->Window() != NULL)) {
					// clicked outside the menu bar or on item with already
					// open sub menu
					fState = MENU_STATE_CLOSED;
				} else
					_SetStickyMode(false);
			} else if (newButtons == 0 && !_IsStickyMode()) {
				if ((fSelected != NULL && fSelected->Submenu() == NULL)
					|| item == NULL) {
					// clicked on an item without a submenu or clicked and
					// released the mouse button outside the menu bar
					fChosenItem = fSelected;
					fState = MENU_STATE_CLOSED;
				} else
					_SetStickyMode(true);
			}
			where = newWhere;
			buttons = newButtons;
		}
	}

	if (LockLooper()) {
		if (fSelected != NULL)
			_SelectItem(NULL);

		if (fChosenItem != NULL)
			fChosenItem->Invoke();

		_RestoreFocus();
		UnlockLooper();
	}

	if (_IsStickyMode())
		_SetStickyMode(false);

	_DeleteMenuWindow();

	if (action != NULL)
		*action = fState;

	return fChosenItem;
}


/**
 * @brief Move keyboard focus to the menu bar, saving the previous focus owner.
 *
 * Records the token of the currently focused view so that _RestoreFocus() can
 * return focus there later. Does nothing if focus has already been stolen.
 *
 * @see _RestoreFocus()
 */
void
BMenuBar::_StealFocus()
{
	// We already stole the focus, don't do anything
	if (fPrevFocusToken != -1)
		return;

	BWindow* window = Window();
	if (window != NULL && window->Lock()) {
		BView* focusView = window->CurrentFocus();
		if (focusView != NULL && focusView != this)
			fPrevFocusToken = _get_object_token_(focusView);
		MakeFocus();
		window->Unlock();
	}
}


/**
 * @brief Return keyboard focus to the view that held it before tracking began.
 *
 * Looks up the previously focused view by its saved token and calls
 * MakeFocus() on it. If the token is no longer valid, focus is simply
 * cleared from the menu bar.
 *
 * @see _StealFocus()
 */
void
BMenuBar::_RestoreFocus()
{
	BWindow* window = Window();
	if (window != NULL && window->Lock()) {
		BHandler* handler = NULL;
		if (fPrevFocusToken != -1
			&& gDefaultTokens.GetToken(fPrevFocusToken, B_HANDLER_TOKEN,
				(void**)&handler) == B_OK) {
			BView* view = dynamic_cast<BView*>(handler);
			if (view != NULL && view->Window() == window)
				view->MakeFocus();
		} else if (IsFocus())
			MakeFocus(false);

		fPrevFocusToken = -1;
		window->Unlock();
	}
}


/**
 * @brief Perform one-time initialization shared by all constructors.
 *
 * Computes item margins from the current plain font size, initialises the
 * border-sides bitmask to all borders, allocates the bounds snapshot, enables
 * hidden-item tracking, and sets the low and view colors for drawing.
 *
 * @param layout The item layout passed to the constructor (currently unused
 *               beyond what the BMenu base already handles).
 */
void
BMenuBar::_InitData(menu_layout layout)
{
	const float fontSize = be_plain_font->Size();
	float lr = fontSize * 2.0f / 3.0f, tb = fontSize / 6.0f;
	SetItemMargins(lr, tb, lr, tb);

	fBorders = BControlLook::B_ALL_BORDERS;
	fLastBounds = new BRect(Bounds());
	_SetIgnoreHidden(true);
	SetLowUIColor(B_MENU_BACKGROUND_COLOR);
	SetViewColor(B_TRANSPARENT_COLOR);
}
