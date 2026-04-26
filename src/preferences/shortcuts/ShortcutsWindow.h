/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 1999-2010, Jeremy Friesner and Haiku, Inc.
 * Original author: Jeremy Friesner.
 */

/** @file ShortcutsWindow.h
    @brief Main preference window for the Shortcuts application. */

#ifndef SHORTCUTS_WINDOW_H
#define SHORTCUTS_WINDOW_H


#include <ColumnListView.h>
#include <Entry.h>
#include <Window.h>


class BButton;
class BColumnListView;
class BFilePanel;
class BMessage;
class ShortcutsSpec;

/**
 * @brief Top-level configuration window for global keyboard shortcuts.
 *
 * Hosts a BColumnListView of ShortcutsSpec rows (one per hotkey), file/
 * append/save controls, and the bookkeeping needed to load and persist
 * KeySet files consumed by the shortcut_catcher input_server filter.
 */
// This class defines our preferences/configuration window.
class ShortcutsWindow : public BWindow {
public:
							ShortcutsWindow();
							~ShortcutsWindow();

	virtual	void 			DispatchMessage(BMessage* message,
								BHandler* handler);
	virtual	void 			Quit();
	virtual	void 			MessageReceived(BMessage* message);
	virtual	bool 			QuitRequested();

	// BMessage 'what' codes, representing commands understood by this Window.
	enum {
		ADD_HOTKEY_ITEM = 'SpKy',	// Add a new hotkey entry to the GUI list.
		REMOVE_HOTKEY_ITEM,			// Remove a hotkey entry from the GUI list.
		HOTKEY_ITEM_SELECTED,		// Give the "focus bar" to the specified
									// entry.
		HOTKEY_ITEM_MODIFIED,		// Update the state of an entry to reflect
									// user's changes.
		OPEN_KEYSET,				// Bring up a File requester to load new
									// settings.
		APPEND_KEYSET,				// Bring up a File requester to append
									// settings.
		REVERT_KEYSET,				// Dump the current state and re-read
									// settings from disk.
		SAVE_KEYSET,				// Save the current settings to disk
		SAVE_KEYSET_AS,				// Bring up a File requester to save
									// current settings.
		SELECT_APPLICATION,			// Set the current entry to point to the
									// given file.
	};

private:
			BMenuItem* 			_CreateActuatorPresetMenuItem(const char* label)
									const;
			void 				_AddNewSpec(const char* defaultCommand, uint32 keyCode = 0);
			void 				_MarkKeySetModified();
			bool 				_LoadKeySet(const BMessage& loadMessage);
			bool 				_SaveKeySet(BEntry& saveEntry);
			bool				_GetSettingsFile(entry_ref* ref);
			void 				_LoadWindowSettings(
									const BMessage& loadMessage);
			void 				_SaveWindowSettings(BEntry& saveEntry);
			bool				_GetWindowSettingsFile(entry_ref* ref);

			BButton*			fAddButton;
			BButton*			fRemoveButton;
			BButton*			fSaveButton;
			BColumnListView*	fColumnListView;
			BFilePanel*			fSavePanel;
				// for saving settings
			BFilePanel*			fOpenPanel;
				// for loading settings
			BFilePanel*			fSelectPanel;
				// for selecting apps to launch

			// Points to the settings file to save to
			BEntry				fLastSaved;

			// true iff changes were made since last load or save
			bool				fKeySetModified;

			// true iff the file-requester's ref should be appended to current
			bool				fLastOpenWasAppend;

			BRow*				fSelectedRow;
};


#endif	// SHORTCUTS_WINDOW_H
