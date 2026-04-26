/*
 * Copyright 2026, Kintsugi OS Contributors. All rights reserved.
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
 * MIT License. Copyright 2004-2014 Haiku, Inc.
 * Original authors: Alexandre Deckner, Axel Dörfler, Jérôme Duval,
 *                   John Scipione, Sandor Vroemisse.
 */

/** @file KeymapWindow.h
    @brief Top-level window for the Keymap preferences applet. */

#ifndef KEYMAP_WINDOW_H
#define KEYMAP_WINDOW_H


#include <FilePanel.h>
#include <ListView.h>
#include <String.h>
#include <Window.h>

#include "Keymap.h"


class BMenu;
class BMenuBar;
class BMenuField;
class BTextControl;
class KeyboardLayoutView;
class KeymapListItem;


/**
 * @brief Main window of the Keymap preferences applet.
 *
 * Hosts the system and user keymap lists, the editable
 * KeyboardLayoutView, the dead-key menu, the layout/font menus, and
 * the Defaults/Revert buttons. Owns the active, applied, and
 * previous keymap snapshots so the user can preview, apply, revert,
 * or save changes via Open/Save-As panels.
 */
class KeymapWindow : public BWindow {
public:
								KeymapWindow();
	virtual						~KeymapWindow();

	virtual	bool				QuitRequested();
	virtual void				MessageReceived(BMessage* message);

protected:
			BMenuBar*			_CreateMenu();
			BView*				_CreateMapLists();
			void				_AddKeyboardLayouts(BMenu* menu);
			void				_AddKeyboardLayoutMenu(BMenu* menu,
									BDirectory directory);
			status_t			_SetKeyboardLayout(const char* path);
			void				_MarkKeyboardLayoutItem(const char* path,
									BMenu* menu);

			void				_UpdateSwitchShortcutButton();
			void				_UpdateButtons();
			void				_SwitchShortcutKeys();

			void				_UseKeymap();
			void				_DefaultKeymap();
			void				_RevertKeymap();

			BMenuField*			_CreateDeadKeyMenuField();
			void				_UpdateDeadKeyMenu();

			void 				_FillSystemMaps();
			void				_FillUserMaps();
			void				_SetListViewSize(BListView* listView);

			status_t			_GetCurrentKeymap(entry_ref& ref);
			BString				_GetActiveKeymapName();
			bool				_SelectCurrentMap(BListView* list);
			void				_SelectCurrentMap();

			status_t			_GetSettings(BFile& file, int mode) const;
			status_t			_LoadSettings(BRect& frame);
			status_t			_SaveSettings();
			BPath				_GetMarkedKeyboardLayoutPath(BMenu* menu);

private:
			BListView*			fSystemListView;
			BListView*			fUserListView;
			BButton*			fDefaultsButton;
			BButton*			fRevertButton;
			BMenu*				fLayoutMenu;
			BMenu*				fFontMenu;
			KeyboardLayoutView*	fKeyboardLayoutView;
			BTextControl*		fTextControl;
			BButton*			fSwitchShortcutsButton;
			BMenu*				fAcuteMenu;
			BMenu*				fCircumflexMenu;
			BMenu*				fDiaeresisMenu;
			BMenu*				fGraveMenu;
			BMenu*				fTildeMenu;

			Keymap				fCurrentMap;
			Keymap				fPreviousMap;
			Keymap				fAppliedMap;
			BString				fCurrentMapName;

			BFilePanel*			fOpenPanel;
			BFilePanel*			fSavePanel;
};

#endif	// KEYMAP_WINDOW_H
