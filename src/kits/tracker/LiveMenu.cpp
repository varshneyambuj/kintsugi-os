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
 *   Copyright 2020-2024 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *   Authors:
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file LiveMenu.cpp
 * @brief Menu classes that update their items dynamically when modifier keys change.
 *
 * TLiveMixin provides helper methods for updating File and Window menu items
 * in response to modifier-key state.  TLiveMenu and TLivePopUpMenu subclass
 * BMenu and BPopUpMenu respectively and forward B_MODIFIERS_CHANGED messages
 * to a virtual Update() hook.
 *
 * @see BContainerWindow, TShortcuts
 */


#include "LiveMenu.h"

#include <string.h>

#include <Application.h>
#include <Locale.h>
#include <MenuItem.h>
#include <Window.h>

#include "Commands.h"
#include "TrackerSettings.h"


//	#pragma mark - TLiveMixin


/**
 * @brief Construct a TLiveMixin associated with the given container window.
 *
 * @param window  The BContainerWindow whose shortcut state drives item updates.
 */
TLiveMixin::TLiveMixin(const BContainerWindow* window)
{
	fWindow = window;
}


/**
 * @brief Refresh all File-menu items whose label/enabled state depends on modifier keys.
 *
 * Iterates every item in @p menu and calls the appropriate TShortcuts update
 * method for commands such as Move to Trash, Create Link, Cut, Copy, Paste,
 * and Identify.  Runs with the window looper locked.
 *
 * @param menu  The File BMenu to update; does nothing if NULL.
 */
void
TLiveMixin::UpdateFileMenu(BMenu* menu)
{
	if (menu == NULL)
		return;

	if (menu->Window()->LockLooper()) {
		int32 itemCount = menu->CountItems();
		for (int32 index = 0; index < itemCount; index++) {
			BMenuItem* item = menu->ItemAt(index);
			if (item == NULL || item->Message() == NULL)
				continue;

			switch (item->Message()->what) {
				// Move to Trash/Delete
				case kMoveSelectionToTrash:
				case kDeleteSelection:
					if (!fWindow->Shortcuts()->IsTrash())
						fWindow->Shortcuts()->UpdateMoveToTrashItem(item);
					break;

				// Create link/Create relative link
				case kCreateLink:
				case kCreateRelativeLink:
					fWindow->Shortcuts()->UpdateCreateLinkItem(item);
					break;

				// Cut/Cut more
				case B_CUT:
				case kCutMoreSelectionToClipboard:
					fWindow->Shortcuts()->UpdateCutItem(item);
					break;

				// Copy/Copy more
				case B_COPY:
				case kCopyMoreSelectionToClipboard:
					fWindow->Shortcuts()->UpdateCopyItem(item);
					break;

				// Paste/Paste links
				case B_PASTE:
				case kPasteLinksFromClipboard:
					fWindow->Shortcuts()->UpdatePasteItem(item);
					break;

				// Identify/Force identify
				case kIdentifyEntry:
					fWindow->Shortcuts()->UpdateIdentifyItem(item);
					break;
			}
		}

		menu->Window()->UnlockLooper();
	}
}


/**
 * @brief Refresh all Window-menu items whose state depends on modifier keys.
 *
 * Handles Clean up, Open parent, and Close commands.
 * Runs with the window looper locked.
 *
 * @param menu  The Window BMenu to update; does nothing if NULL.
 */
void
TLiveMixin::UpdateWindowMenu(BMenu* menu)
{
	if (menu == NULL)
		return;

	// update using the window version of TShortcuts class

	if (menu->Window()->LockLooper()) {
		int32 itemCount = menu->CountItems();
		for (int32 index = 0; index < itemCount; index++) {
			BMenuItem* item = menu->ItemAt(index);
			if (item == NULL || item->Message() == NULL)
				continue;

			switch (item->Message()->what) {
				// Clean up/Clean up all
				case kCleanup:
				case kCleanupAll:
					fWindow->Shortcuts()->UpdateCleanupItem(item);
					break;

				// Open parent
				case kOpenParentDir:
					fWindow->Shortcuts()->UpdateOpenParentItem(item);
					break;

				// Close/Close all
				case B_QUIT_REQUESTED:
				case kCloseAllWindows:
					fWindow->Shortcuts()->UpdateCloseItem(item);
					break;
			}
		}

		menu->Window()->UnlockLooper();
	}
}


//	#pragma mark - TLiveMenu


/**
 * @brief Construct a TLiveMenu with the given label.
 *
 * @param label  The menu label string.
 */
TLiveMenu::TLiveMenu(const char* label)
	:
	BMenu(label)
{
}


/**
 * @brief Destroy the TLiveMenu.
 */
TLiveMenu::~TLiveMenu()
{
}


/**
 * @brief Handle B_MODIFIERS_CHANGED by calling Update(), forwarding all others.
 *
 * @param message  The incoming BMessage.
 */
void
TLiveMenu::MessageReceived(BMessage* message)
{
	if (message != NULL && message->what == B_MODIFIERS_CHANGED)
		Update();
	else
		BMenu::MessageReceived(message);
}


/**
 * @brief Hook method called when modifier keys change; subclasses override to refresh items.
 */
void
TLiveMenu::Update()
{
	// hook method
}


//	#pragma mark - TLivePopUpMenu


/**
 * @brief Construct a TLivePopUpMenu.
 *
 * @param label            The menu label string.
 * @param radioMode        Whether the menu operates in radio mode.
 * @param labelFromMarked  Whether the menu label tracks the marked item.
 * @param layout           The menu layout constant.
 */
TLivePopUpMenu::TLivePopUpMenu(const char* label, bool radioMode, bool labelFromMarked,
	menu_layout layout)
	:
	BPopUpMenu(label, radioMode, labelFromMarked, layout)
{
}


/**
 * @brief Destroy the TLivePopUpMenu.
 */
TLivePopUpMenu::~TLivePopUpMenu()
{
}


/**
 * @brief Handle B_MODIFIERS_CHANGED by calling Update(), forwarding all others.
 *
 * @param message  The incoming BMessage.
 */
void
TLivePopUpMenu::MessageReceived(BMessage* message)
{
	if (message != NULL && message->what == B_MODIFIERS_CHANGED)
		Update();
	else
		BMenu::MessageReceived(message);
}


/**
 * @brief Hook method called when modifier keys change; subclasses override to refresh items.
 */
void
TLivePopUpMenu::Update()
{
	// hook method
}


//	#pragma mark - TLiveArrangeByMenu


/**
 * @brief Construct a TLiveArrangeByMenu.
 *
 * @param label   The menu label string.
 * @param window  The owning BContainerWindow.
 */
TLiveArrangeByMenu::TLiveArrangeByMenu(const char* label, const BContainerWindow* window)
	:
	TLiveMenu(label),
	TLiveMixin(window)
{
}


/**
 * @brief Destroy the TLiveArrangeByMenu.
 */
TLiveArrangeByMenu::~TLiveArrangeByMenu()
{
}


/**
 * @brief Refresh the Clean-up item's label/state for current modifier keys.
 */
void
TLiveArrangeByMenu::Update()
{
	// Clean up/Clean up all
	TShortcuts().UpdateCleanupItem(TShortcuts().FindItem(this, kCleanup, kCleanupAll));
}


//	#pragma mark - TLiveFileMenu


/**
 * @brief Construct a TLiveFileMenu.
 *
 * @param label   The menu label string.
 * @param window  The owning BContainerWindow.
 */
TLiveFileMenu::TLiveFileMenu(const char* label, const BContainerWindow* window)
	:
	TLiveMenu(label),
	TLiveMixin(window)
{
}


/**
 * @brief Destroy the TLiveFileMenu.
 */
TLiveFileMenu::~TLiveFileMenu()
{
}


/**
 * @brief Update all file-menu items via UpdateFileMenu().
 */
void
TLiveFileMenu::Update()
{
	UpdateFileMenu(this);
}


//	#pragma mark - TLivePosePopUpMenu


/**
 * @brief Construct a TLivePosePopUpMenu.
 *
 * @param label            The menu label string.
 * @param window           The owning BContainerWindow.
 * @param radioMode        Whether the menu operates in radio mode.
 * @param labelFromMarked  Whether the label tracks the marked item.
 * @param layout           The menu layout constant.
 */
TLivePosePopUpMenu::TLivePosePopUpMenu(const char* label, const BContainerWindow* window,
	bool radioMode, bool labelFromMarked, menu_layout layout)
	:
	TLivePopUpMenu(label, radioMode, labelFromMarked, layout),
	TLiveMixin(window)
{
}


/**
 * @brief Destroy the TLivePosePopUpMenu.
 */
TLivePosePopUpMenu::~TLivePosePopUpMenu()
{
}


/**
 * @brief Update all file-menu items in this pose context pop-up menu.
 */
void
TLivePosePopUpMenu::Update()
{
	UpdateFileMenu(this);
}


//	#pragma mark - TLiveWindowMenu


/**
 * @brief Construct a TLiveWindowMenu.
 *
 * @param label   The menu label string.
 * @param window  The owning BContainerWindow.
 */
TLiveWindowMenu::TLiveWindowMenu(const char* label, const BContainerWindow* window)
	:
	TLiveMenu(label),
	TLiveMixin(window)
{
}


/**
 * @brief Destroy the TLiveWindowMenu.
 */
TLiveWindowMenu::~TLiveWindowMenu()
{
}


/**
 * @brief Update all window-menu items via UpdateWindowMenu().
 */
void
TLiveWindowMenu::Update()
{
	UpdateWindowMenu(this);
}


//	#pragma mark - TLiveWindowPopUpMenu


/**
 * @brief Construct a TLiveWindowPopUpMenu.
 *
 * @param label            The menu label string.
 * @param window           The owning BContainerWindow.
 * @param radioMode        Whether the menu operates in radio mode.
 * @param labelFromMarked  Whether the label tracks the marked item.
 * @param layout           The menu layout constant.
 */
TLiveWindowPopUpMenu::TLiveWindowPopUpMenu(const char* label, const BContainerWindow* window,
	bool radioMode, bool labelFromMarked, menu_layout layout)
	:
	TLivePopUpMenu(label, radioMode, labelFromMarked, layout),
	TLiveMixin(window)
{
}


/**
 * @brief Destroy the TLiveWindowPopUpMenu.
 */
TLiveWindowPopUpMenu::~TLiveWindowPopUpMenu()
{
}


/**
 * @brief Update all window-menu items in this pop-up menu.
 */
void
TLiveWindowPopUpMenu::Update()
{
	UpdateWindowMenu(this);
}
