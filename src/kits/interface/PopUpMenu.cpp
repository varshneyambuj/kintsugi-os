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
 *   Copyright 2001-2025 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marc Flerackers (mflerackers@androme.be)
 *       Stefano Ceccherini (burton666@libero.it)
 *       John Scipione (jscipione@gmail.com)
 */


/**
 * @file PopUpMenu.cpp
 * @brief Implementation of BPopUpMenu, a standalone pop-up menu
 *
 * BPopUpMenu extends BMenu to open as a context menu at a specific screen location,
 * typically in response to a right-click or button press. It runs synchronously,
 * blocking until the user selects an item or dismisses the menu, and returns the
 * selected item.
 *
 * @see BMenu, BMenuItem, BMenuField
 */


#include <PopUpMenu.h>

#include <new>

#include <Application.h>
#include <Looper.h>
#include <MenuItem.h>
#include <Window.h>

#include <binary_compatibility/Interface.h>


/**
 * @brief Data block passed between the caller thread and the menu-tracking thread.
 *
 * Bundles together all parameters that _Go() needs to communicate with the
 * background tracking thread spawned by _thread_entry().  The semaphore
 * @c lock is used for synchronisation: the tracking thread deletes it when
 * it finishes, which unblocks _WaitMenu() on the calling side.
 */
struct popup_menu_data {
	BPopUpMenu* object;      /**< @brief The menu being tracked. */
	BWindow*    window;      /**< @brief Window from which Go() was called, may be NULL. */
	BMenuItem*  selected;    /**< @brief Item chosen by the user; set by the tracking thread. */

	BPoint where;            /**< @brief Screen location at which the menu should appear. */
	BRect  rect;             /**< @brief Optional click-to-open rectangle. */

	bool async;              /**< @brief True when operating in asynchronous mode. */
	bool autoInvoke;         /**< @brief True when the selected item should be invoked automatically. */
	bool startOpened;        /**< @brief True when the menu should open with its first item selected. */
	bool useRect;            /**< @brief True when @c rect should be passed to Track(). */

	sem_id lock;             /**< @brief Synchronisation semaphore; deleted by the tracking thread on exit. */
};


/**
 * @brief Constructs a BPopUpMenu with a name and optional radio/label behaviour.
 *
 * @param name            The internal name of the menu.
 * @param radioMode       If true, the menu operates in radio mode (at most one
 *                        marked item at a time).
 * @param labelFromMarked If true, the parent BMenuField label tracks the marked item.
 * @param layout          The menu layout; typically B_ITEMS_IN_COLUMN.
 *
 * @see BMenu::SetRadioMode(), BMenu::SetLabelFromMarked()
 */
BPopUpMenu::BPopUpMenu(const char* name, bool radioMode, bool labelFromMarked,
	menu_layout layout)
	:
	BMenu(name, layout),
	fUseWhere(false),
	fAutoDestruct(false),
	fTrackThread(-1)
{
	if (radioMode)
		SetRadioMode(true);

	if (labelFromMarked)
		SetLabelFromMarked(true);
}


/**
 * @brief Constructs a BPopUpMenu from a previously archived BMessage.
 *
 * Used by the BArchivable instantiation mechanism; state is restored from
 * the fields written by Archive().
 *
 * @param archive The BMessage containing the archived menu state.
 *
 * @see BPopUpMenu::Instantiate(), BPopUpMenu::Archive()
 */
BPopUpMenu::BPopUpMenu(BMessage* archive)
	:
	BMenu(archive),
	fUseWhere(false),
	fAutoDestruct(false),
	fTrackThread(-1)
{
}


/**
 * @brief Destroys the BPopUpMenu, waiting for any active tracking thread to finish.
 *
 * If an asynchronous tracking thread is still running (fTrackThread >= 0)
 * the destructor blocks until it terminates before releasing resources.
 */
BPopUpMenu::~BPopUpMenu()
{
	if (fTrackThread >= 0) {
		status_t status;
		while (wait_for_thread(fTrackThread, &status) == B_INTERRUPTED)
			;
	}
}


/**
 * @brief Archives the BPopUpMenu into a BMessage.
 *
 * Delegates entirely to BMenu::Archive(); no additional fields are written.
 *
 * @param data  The BMessage to archive into.
 * @param deep  If true, child items are also archived.
 *
 * @return A status code forwarded from BMenu::Archive().
 * @retval B_OK On success.
 *
 * @see BPopUpMenu::Instantiate()
 */
status_t
BPopUpMenu::Archive(BMessage* data, bool deep) const
{
	return BMenu::Archive(data, deep);
}


/**
 * @brief Instantiates a BPopUpMenu from an archived BMessage.
 *
 * Factory method required by the BArchivable protocol.  Validates that
 * @a data was produced by BPopUpMenu before constructing the object.
 *
 * @param data The BMessage previously produced by Archive().
 *
 * @return A newly allocated BPopUpMenu, or NULL if validation fails.
 *
 * @see BPopUpMenu::Archive()
 */
BArchivable*
BPopUpMenu::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "BPopUpMenu"))
		return new BPopUpMenu(data);

	return NULL;
}


/**
 * @brief Displays the menu at a screen location and waits for the user to choose an item.
 *
 * This is the primary entry point for showing a pop-up menu.  When @a async
 * is false (the default) the call blocks until the menu is dismissed and
 * returns the selected BMenuItem.  When @a async is true the menu is
 * tracked on a background thread and the method returns immediately with NULL.
 *
 * @param where            The screen coordinate at which the top-left corner of
 *                         the menu should appear.
 * @param deliversMessage  If true, invoking an item sends its message to its
 *                         target in addition to returning the item pointer.
 * @param openAnyway       If true, the menu opens even when there are no items.
 * @param async            If true, run the menu on a background thread and return
 *                         immediately.
 *
 * @return The BMenuItem chosen by the user, or NULL if the menu was dismissed
 *         without a selection (or when running asynchronously).
 *
 * @see Go(BPoint, bool, bool, BRect, bool), SetAsyncAutoDestruct()
 */
BMenuItem*
BPopUpMenu::Go(BPoint where, bool deliversMessage, bool openAnyway, bool async)
{
	return _Go(where, deliversMessage, openAnyway, NULL, async);
}


/**
 * @brief Displays the menu at a screen location with a click-to-open rectangle.
 *
 * Identical to the four-argument overload but additionally passes a rectangle
 * to the underlying tracking code.  Clicks within @a clickToOpen are treated
 * as initiating a click-open sequence rather than an immediate selection.
 *
 * @param where            The screen coordinate for the menu's top-left corner.
 * @param deliversMessage  If true, the selected item's message is dispatched.
 * @param openAnyway       If true, the menu opens even when there are no items.
 * @param clickToOpen      A screen rectangle; clicks inside it begin a delayed-open
 *                         sequence so the user can drag to select without committing.
 * @param async            If true, run the menu on a background thread.
 *
 * @return The chosen BMenuItem, or NULL on dismissal or in async mode.
 *
 * @see Go(BPoint, bool, bool, bool)
 */
BMenuItem*
BPopUpMenu::Go(BPoint where, bool deliversMessage, bool openAnyway,
	BRect clickToOpen, bool async)
{
	return _Go(where, deliversMessage, openAnyway, &clickToOpen, async);
}


void
BPopUpMenu::MessageReceived(BMessage* message)
{
	BMenu::MessageReceived(message);
}


void
BPopUpMenu::MouseDown(BPoint point)
{
	BView::MouseDown(point);
}


void
BPopUpMenu::MouseUp(BPoint point)
{
	BView::MouseUp(point);
}


void
BPopUpMenu::MouseMoved(BPoint point, uint32 code, const BMessage* message)
{
	BView::MouseMoved(point, code, message);
}


void
BPopUpMenu::AttachedToWindow()
{
	BMenu::AttachedToWindow();
}


void
BPopUpMenu::DetachedFromWindow()
{
	BMenu::DetachedFromWindow();
}


void
BPopUpMenu::FrameMoved(BPoint newPosition)
{
	BMenu::FrameMoved(newPosition);
}


void
BPopUpMenu::FrameResized(float newWidth, float newHeight)
{
	BMenu::FrameResized(newWidth, newHeight);
}


BHandler*
BPopUpMenu::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 form, const char* property)
{
	return BMenu::ResolveSpecifier(message, index, specifier, form, property);
}


status_t
BPopUpMenu::GetSupportedSuites(BMessage* data)
{
	return BMenu::GetSupportedSuites(data);
}


status_t
BPopUpMenu::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_MIN_SIZE:
			((perform_data_min_size*)_data)->return_value
				= BPopUpMenu::MinSize();
			return B_OK;
		case PERFORM_CODE_MAX_SIZE:
			((perform_data_max_size*)_data)->return_value
				= BPopUpMenu::MaxSize();
			return B_OK;
		case PERFORM_CODE_PREFERRED_SIZE:
			((perform_data_preferred_size*)_data)->return_value
				= BPopUpMenu::PreferredSize();
			return B_OK;
		case PERFORM_CODE_LAYOUT_ALIGNMENT:
			((perform_data_layout_alignment*)_data)->return_value
				= BPopUpMenu::LayoutAlignment();
			return B_OK;
		case PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH:
			((perform_data_has_height_for_width*)_data)->return_value
				= BPopUpMenu::HasHeightForWidth();
			return B_OK;
		case PERFORM_CODE_GET_HEIGHT_FOR_WIDTH:
		{
			perform_data_get_height_for_width* data
				= (perform_data_get_height_for_width*)_data;
			BPopUpMenu::GetHeightForWidth(data->width, &data->min, &data->max,
				&data->preferred);
			return B_OK;
		}
		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BPopUpMenu::SetLayout(data->layout);
			return B_OK;
		}
		case PERFORM_CODE_LAYOUT_INVALIDATED:
		{
			perform_data_layout_invalidated* data
				= (perform_data_layout_invalidated*)_data;
			BPopUpMenu::LayoutInvalidated(data->descendants);
			return B_OK;
		}
		case PERFORM_CODE_DO_LAYOUT:
		{
			BPopUpMenu::DoLayout();
			return B_OK;
		}
	}

	return BMenu::Perform(code, _data);
}


void
BPopUpMenu::ResizeToPreferred()
{
	BMenu::ResizeToPreferred();
}


void
BPopUpMenu::GetPreferredSize(float* _width, float* _height)
{
	BMenu::GetPreferredSize(_width, _height);
}


void
BPopUpMenu::MakeFocus(bool state)
{
	BMenu::MakeFocus(state);
}


void
BPopUpMenu::AllAttached()
{
	BMenu::AllAttached();
}


void
BPopUpMenu::AllDetached()
{
	BMenu::AllDetached();
}


/**
 * @brief Controls whether the menu deletes itself after an asynchronous Go() call.
 *
 * When @a on is true and the menu was launched with the @c async flag set,
 * the tracking thread will call @c delete @c this once tracking finishes,
 * relieving the caller of ownership responsibility.
 *
 * @param on True to enable automatic self-deletion after async tracking.
 *
 * @see AsyncAutoDestruct(), Go()
 */
void
BPopUpMenu::SetAsyncAutoDestruct(bool on)
{
	fAutoDestruct = on;
}


/**
 * @brief Returns whether the menu will delete itself after asynchronous tracking ends.
 *
 * @return True if auto-destruct is enabled, false otherwise.
 *
 * @see SetAsyncAutoDestruct()
 */
bool
BPopUpMenu::AsyncAutoDestruct() const
{
	return fAutoDestruct;
}


/**
 * @brief Returns the screen location at which the menu should open.
 *
 * When the menu is standalone (launched via Go()) it returns the position
 * stored by _StartTrack().  When the menu is embedded inside a BMenuField
 * it computes the correct location based on the super-item's frame and
 * the currently marked item so that the marked entry aligns under the field.
 *
 * @return The screen point for the menu's top-left corner.
 *
 * @see BMenu::Track(), Go()
 */
BPoint
BPopUpMenu::ScreenLocation()
{
	// This case is when the BPopUpMenu is standalone
	if (fUseWhere)
		return fWhere;

	// This case is when the BPopUpMenu is inside a BMenuField
	BMenuItem* superItem = Superitem();
	BMenu* superMenu = Supermenu();
	BMenuItem* selectedItem = FindItem(superItem->Label());
	BPoint point = superItem->Frame().LeftTop();

	superMenu->ConvertToScreen(&point);

	if (selectedItem != NULL) {
		while (selectedItem->Menu() != this
			&& selectedItem->Menu()->Superitem() != NULL) {
			selectedItem = selectedItem->Menu()->Superitem();
		}
		point.y -= selectedItem->Frame().top;
	}

	return point;
}


//	#pragma mark - private methods


void BPopUpMenu::_ReservedPopUpMenu1() {}
void BPopUpMenu::_ReservedPopUpMenu2() {}
void BPopUpMenu::_ReservedPopUpMenu3() {}


BPopUpMenu&
BPopUpMenu::operator=(const BPopUpMenu& other)
{
	return *this;
}


/**
 * @brief Internal implementation that backs all public Go() overloads.
 *
 * Creates the popup_menu_data block, spawns the tracking thread via
 * _thread_entry(), and either blocks with _WaitMenu() (synchronous) or
 * returns immediately (asynchronous).  The semaphore stored in the data
 * block serves as the synchronisation primitive between the two sides.
 *
 * @param where         Screen coordinate for the menu.
 * @param autoInvoke    If true, the chosen item is invoked inside the tracking thread.
 * @param startOpened   If true, tracking begins with the first item pre-selected.
 * @param _specialRect  Optional click-to-open rectangle; NULL if not used.
 * @param async         If true, return immediately after spawning the thread.
 *
 * @return The selected BMenuItem in synchronous mode, or NULL in async mode
 *         (or on any allocation or thread-spawn failure).
 *
 * @see _StartTrack(), _WaitMenu(), _thread_entry()
 */
BMenuItem*
BPopUpMenu::_Go(BPoint where, bool autoInvoke, bool startOpened,
	BRect* _specialRect, bool async)
{
	if (fTrackThread >= B_OK) {
		// we already have an active menu, wait for it to go away before
		// spawning another
		status_t unused;
		while (wait_for_thread(fTrackThread, &unused) == B_INTERRUPTED)
			;
	}

	popup_menu_data* data = new (std::nothrow) popup_menu_data;
	if (data == NULL)
		return NULL;

	sem_id sem = create_sem(0, "window close lock");
	if (sem < B_OK) {
		delete data;
		return NULL;
	}

	// Get a pointer to the window from which Go() was called
	BWindow* window = dynamic_cast<BWindow*>(BLooper::LooperForThread(find_thread(NULL)));
	data->window = window;

	// Asynchronous menu: we set the BWindow menu's semaphore
	// and let BWindow block when needed
	if (async && window != NULL)
		_set_menu_sem_(window, sem);

	data->object = this;
	data->autoInvoke = autoInvoke;
	data->useRect = _specialRect != NULL;
	if (_specialRect != NULL)
		data->rect = *_specialRect;
	data->async = async;
	data->where = where;
	data->startOpened = startOpened;
	data->selected = NULL;
	data->lock = sem;

	// Spawn the tracking thread
	fTrackThread = spawn_thread(_thread_entry, "popup", B_DISPLAY_PRIORITY, data);
	if (fTrackThread < B_OK) {
		// Something went wrong. Cleanup and return NULL
		delete_sem(sem);
		if (async && window != NULL)
			_set_menu_sem_(window, B_BAD_SEM_ID);
		delete data;
		return NULL;
	}

	resume_thread(fTrackThread);

	if (!async)
		return _WaitMenu(data);

	return 0;
}


/**
 * @brief Thread entry point for the background menu-tracking thread.
 *
 * Casts @a menuData back to popup_menu_data, calls _StartTrack() on the
 * target BPopUpMenu, then cleans up the semaphore.  In async mode it also
 * resets the window's menu semaphore and, if auto-destruct is enabled,
 * deletes the menu object.
 *
 * @param menuData Pointer to the popup_menu_data block allocated by _Go().
 *
 * @return Always 0.
 *
 * @see _Go(), _StartTrack()
 */
/* static */
int32
BPopUpMenu::_thread_entry(void* menuData)
{
	popup_menu_data* data = static_cast<popup_menu_data*>(menuData);
	BPopUpMenu* menu = data->object;
	BRect* rect = NULL;

	if (data->useRect)
		rect = &data->rect;

	data->selected = menu->_StartTrack(data->where, data->autoInvoke,
		data->startOpened, rect);

	// Reset the window menu semaphore
	if (data->async && data->window)
		_set_menu_sem_(data->window, B_BAD_SEM_ID);

	delete_sem(data->lock);

	// Commit suicide if needed
	if (data->async && menu->fAutoDestruct) {
		menu->fTrackThread = -1;
		delete menu;
	}

	if (data->async)
		delete data;

	return 0;
}


/**
 * @brief Shows the menu window, runs the tracking loop, and hides the menu.
 *
 * Sets fUseWhere and fWhere so that ScreenLocation() returns the correct
 * position while BMenu::Track() is executing.  The method also handles the
 * click-vs-drag distinction: if the user releases the mouse button within
 * the system click speed it opens the menu for a second tracking pass with
 * @c startOpened set to true.
 *
 * @param where         Screen location for the menu (stored in fWhere).
 * @param autoInvoke    If true, Invoke() is called on the returned item.
 * @param startOpened   If true, the first tracking pass begins with the menu open.
 * @param _specialRect  Optional click-to-open rectangle forwarded to BMenu::Track().
 *
 * @return The BMenuItem chosen by the user, or NULL if none.
 *
 * @see BMenu::Track(), ScreenLocation()
 */
BMenuItem*
BPopUpMenu::_StartTrack(BPoint where, bool autoInvoke, bool startOpened,
	BRect* _specialRect)
{
	// I know, this doesn't look senseful, but don't be fooled,
	// fUseWhere is used in ScreenLocation(), which is a virtual
	// called by BMenu::Track()
	fWhere = where;
	fUseWhere = true;

	// Determine when mouse-down-up will be taken as a 'press',
	// rather than a 'click'
	bigtime_t clickMaxTime = 0;
	get_click_speed(&clickMaxTime);
	clickMaxTime += system_time();

	// Show the menu's window
	Show();
	snooze(50000);
	BMenuItem* result = Track(startOpened, _specialRect);

	// If it was a click, keep the menu open and tracking
	if (system_time() <= clickMaxTime)
		result = Track(true, _specialRect);
	if (result != NULL && autoInvoke)
		result->Invoke();

	fUseWhere = false;

	Hide();
	be_app->ShowCursor();

	return result;
}


/**
 * @brief Blocks the calling thread until the background tracking thread finishes.
 *
 * Repeatedly attempts to acquire the semaphore with a short timeout, calling
 * BWindow::UpdateIfNeeded() in between to keep the window responsive.  Once
 * the semaphore is gone (deleted by the tracking thread), waits for the thread
 * itself to exit and then returns the selected item.
 *
 * @param _data Pointer to the popup_menu_data allocated by _Go(); freed here.
 *
 * @return The BMenuItem selected by the user, or NULL if none.
 *
 * @see _Go(), _thread_entry()
 */
BMenuItem*
BPopUpMenu::_WaitMenu(void* _data)
{
	popup_menu_data* data = static_cast<popup_menu_data*>(_data);
	BWindow* window = data->window;
	sem_id sem = data->lock;
	if (window != NULL) {
		while (acquire_sem_etc(sem, 1, B_RELATIVE_TIMEOUT, 50000) != B_BAD_SEM_ID)
			window->UpdateIfNeeded();
	}

 	status_t unused;
	while (wait_for_thread(fTrackThread, &unused) == B_INTERRUPTED)
		;

	fTrackThread = -1;

	BMenuItem* selected = data->selected;
		// data->selected is filled by the tracking thread

	delete data;

	return selected;
}
