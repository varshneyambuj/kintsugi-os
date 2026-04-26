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
 *   Copyright 1999-2009 Jeremy Friesner
 *   Copyright 2009-2010 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Jeremy Friesner
 *       Fredrik Modéen
 */


/**
 * @file ShortcutsWindow.cpp
 * @brief Implementation of the Shortcuts preference window.
 *
 * Builds the menu bar, BColumnListView, and action buttons; loads and saves
 * KeySet files; and routes keyboard, mouse, drag, and clipboard activity
 * into the underlying ShortcutsSpec rows. Window position and column state
 * are persisted to a separate side-band settings file.
 */


#include "ShortcutsWindow.h"

#include <math.h>
#include <stdio.h>

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <Clipboard.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <ControlLook.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <Input.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <Message.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MessageFilter.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Screen.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <String.h>
#include <SupportDefs.h>
#include <usb/USB_hid.h>
#include <usb/USB_hid_page_consumer.h>

#include "EditWindow.h"
#include "KeyInfos.h"
#include "MetaKeyStateMap.h"
#include "ParseCommandLine.h"
#include "PopUpColumn.h"
#include "ShortcutsFilterConstants.h"
#include "ShortcutsSpec.h"


// Window sizing constraints
#define MAX_WIDTH 10000
#define MAX_HEIGHT 10000
	// SetSizeLimits does not provide a mechanism for specifying an
	// unrestricted maximum. 10,000 seems to be the most common value used
	// in other Haiku system applications.

#define WINDOW_SETTINGS_FILE_NAME "Shortcuts_window_settings"
	// Because the "shortcuts_settings" file (SHORTCUTS_SETTING_FILE_NAME) is
	// already used as a communications method between this configurator and
	// the "shortcut_catcher" input_server filter, it should not be overloaded
	// with window position information, instead, a separate file is used.

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ShortcutsWindow"

#define ERROR "Shortcuts error"
#define WARNING "Shortcuts warning"


/**
 * @brief Builds a pop-up menu reflecting the chording states of one
 *        meta-key column.
 *
 * @param column Column index in the range [0, NUM_META_COLUMNS).
 * @return Newly allocated BPopUpMenu owned by the caller.
 */
// Creates a pop-up-menu that reflects the possible states of the specified
// meta-key.
static BPopUpMenu*
CreateMetaPopUp(int column)
{
	MetaKeyStateMap& map = GetNthKeyMap(column);
	BPopUpMenu* popup = new BPopUpMenu(NULL, false);
	int stateCount = map.GetNumStates();

	for (int i = 0; i < stateCount; i++)
		popup->AddItem(new BMenuItem(map.GetNthStateDesc(i), NULL));

	return popup;
}


/**
 * @brief Builds a pop-up menu listing every supported keycap name.
 *
 * @return Newly allocated BPopUpMenu owned by the caller.
 */
// Creates a pop-up that allows the user to choose a key-cap visually
static BPopUpMenu*
CreateKeysPopUp()
{
	BPopUpMenu* popup = new BPopUpMenu(NULL, false);
	int numKeys = GetNumKeyIndices();
	for (int i = 0; i < numKeys; i++) {
		const char* next = GetKeyName(i);
		if (next != NULL)
			popup->AddItem(new BMenuItem(next, NULL));
	}

	return popup;
}


/**
 * @brief Constructs and lays out the main Shortcuts preference window.
 *
 * Creates the File menu, builds the column list (modifier states, key,
 * application command), wires the Add/Remove/Save buttons, then either
 * loads the user's KeySet from disk or seeds the list with default
 * volume-control bindings.
 */
ShortcutsWindow::ShortcutsWindow()
	:
	BWindow(BRect(0, 0, 0, 0), B_TRANSLATE_SYSTEM_NAME("Shortcuts"),
		B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS),
	fSavePanel(NULL),
	fOpenPanel(NULL),
	fSelectPanel(NULL),
	fKeySetModified(false),
	fLastOpenWasAppend(false)
{
	ShortcutsSpec::InitializeMetaMaps();

	BMenuBar* menuBar = new BMenuBar("Menu Bar");

	BMenu* fileMenu = new BMenu(B_TRANSLATE("File"));
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Open KeySet" B_UTF8_ELLIPSIS),
		new BMessage(OPEN_KEYSET), 'O'));
	fileMenu->AddItem(new BMenuItem(
		B_TRANSLATE("Append KeySet" B_UTF8_ELLIPSIS),
		new BMessage(APPEND_KEYSET), 'A'));
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Revert to saved"),
		new BMessage(REVERT_KEYSET), 'R'));
	fileMenu->AddItem(new BSeparatorItem);
	fileMenu->AddItem(new BMenuItem(
		B_TRANSLATE("Save KeySet as" B_UTF8_ELLIPSIS),
		new BMessage(SAVE_KEYSET_AS), 'S'));
	fileMenu->AddItem(new BSeparatorItem);
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Quit"),
		new BMessage(B_QUIT_REQUESTED), 'Q'));
	menuBar->AddItem(fileMenu);

	fColumnListView = new BColumnListView(NULL,
		B_WILL_DRAW | B_FRAME_EVENTS, B_FANCY_BORDER, false);

	float cellWidth = be_plain_font->StringWidth("Either") + 20;
		// ShortcutsSpec does not seem to translate the string "Either".

	for (int i = 0; i < ShortcutsSpec::NUM_META_COLUMNS; i++) {
		const char* name = ShortcutsSpec::GetColumnName(i);
		float headerWidth = be_plain_font->StringWidth(name) + 20;
		float width = max_c(headerWidth, cellWidth);

		fColumnListView->AddColumn(new PopUpColumn(CreateMetaPopUp(i), name,
				width, width - 1, width * 1.5, B_TRUNCATE_END, false, true, 1),
			fColumnListView->CountColumns());
	}

	float keyCellWidth = be_plain_font->StringWidth("Caps Lock") + 20;
	fColumnListView->AddColumn(new PopUpColumn(CreateKeysPopUp(),
			B_TRANSLATE("Key"), keyCellWidth, keyCellWidth - 10,
			keyCellWidth + 30, B_TRUNCATE_END),
		fColumnListView->CountColumns());
	BPopUpMenu* popup = new BPopUpMenu(NULL, false);
	popup->AddItem(new BMenuItem(
		B_TRANSLATE("(Choose application with file requester)"), NULL));
	popup->AddItem(new BMenuItem(
		B_TRANSLATE("*InsertString \"Your Text Here\""), NULL));
	popup->AddItem(new BMenuItem(
		B_TRANSLATE("*MoveMouse +20 +0"), NULL));
	popup->AddItem(new BMenuItem(B_TRANSLATE("*MoveMouseTo 50% 50%"), NULL));
	popup->AddItem(new BMenuItem(B_TRANSLATE("*MouseButton 1"), NULL));
	popup->AddItem(new BMenuItem(
		B_TRANSLATE("*LaunchHandler text/html"), NULL));
	popup->AddItem(new BMenuItem(
		B_TRANSLATE("*Multi \"*MoveMouseTo 100% 0\" \"*MouseButton 1\""),
		NULL));
	popup->AddItem(new BMenuItem(B_TRANSLATE("*MouseDown"), NULL));
	popup->AddItem(new BMenuItem(B_TRANSLATE("*MouseUp"), NULL));
	popup->AddItem(new BMenuItem(
		B_TRANSLATE("*SendMessage application/x-vnd.Be-TRAK 'Tfnd'"), NULL));
	popup->AddItem(new BMenuItem(B_TRANSLATE("*Beep"), NULL));
	fColumnListView->AddColumn(new PopUpColumn(popup, B_TRANSLATE("Application"),
			300.0, 223.0, 324.0, B_TRUNCATE_END, true),
		fColumnListView->CountColumns());

	fColumnListView->SetSelectionMessage(new BMessage(HOTKEY_ITEM_SELECTED));
	fColumnListView->SetSelectionMode(B_SINGLE_SELECTION_LIST);
	fColumnListView->SetTarget(this);

	fAddButton = new BButton("add", B_TRANSLATE("Add new shortcut"),
		new BMessage(ADD_HOTKEY_ITEM));

	fRemoveButton = new BButton("remove",
		B_TRANSLATE("Remove selected shortcut"),
		new BMessage(REMOVE_HOTKEY_ITEM));
	fRemoveButton->SetEnabled(false);

	fSaveButton = new BButton("save", B_TRANSLATE("Save & apply"),
		new BMessage(SAVE_KEYSET));
	fSaveButton->SetEnabled(false);

	CenterOnScreen();

	fColumnListView->ResizeAllColumnsToPreferred();

	entry_ref windowSettingsRef;
	if (_GetWindowSettingsFile(&windowSettingsRef)) {
		// The window settings file is not accepted via B_REFS_RECEIVED; this
		// is a behind-the-scenes file that the user will never see or
		// interact with.
		BFile windowSettingsFile(&windowSettingsRef, B_READ_ONLY);
		BMessage loadMessage;
		if (loadMessage.Unflatten(&windowSettingsFile) == B_OK)
			_LoadWindowSettings(loadMessage);
	}

	entry_ref keySetRef;
	if (_GetSettingsFile(&keySetRef)) {
		BMessage message(B_REFS_RECEIVED);
		message.AddRef("refs", &keySetRef);
		message.AddString("startupRef", "please");
		PostMessage(&message);
			// tell ourselves to load this file if it exists
	} else {
		_AddNewSpec("/bin/setvolume -t", (B_HID_USAGE_PAGE_CONSUMER << 16) | B_HID_UID_CON_MUTE);
		_AddNewSpec("/bin/setvolume -i", (B_HID_USAGE_PAGE_CONSUMER << 16) | B_HID_UID_CON_VOLUME_INCREMENT);
		_AddNewSpec("/bin/setvolume -d", (B_HID_USAGE_PAGE_CONSUMER << 16) | B_HID_UID_CON_VOLUME_DECREMENT);
		fLastSaved = BEntry(&keySetRef);
		PostMessage(SAVE_KEYSET);
	}

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL)
			.SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET))
			.SetInsets(B_USE_WINDOW_SPACING)
			.Add(fColumnListView)
			.AddGroup(B_HORIZONTAL)
				.AddGroup(B_HORIZONTAL)
				.SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP))
				.Add(fAddButton)
				.Add(fRemoveButton)
				.End()
				.AddGroup(B_HORIZONTAL)
					.SetExplicitAlignment(BAlignment(B_ALIGN_RIGHT, B_ALIGN_TOP))
					.Add(fSaveButton)
				.End()
			.End()
		.End();

	Show();
}


/**
 * @brief Destructor; tears down file panels and asks the application to quit.
 */
ShortcutsWindow::~ShortcutsWindow()
{
	delete fSavePanel;
	delete fOpenPanel;
	delete fSelectPanel;
	be_app->PostMessage(B_QUIT_REQUESTED);
}


/**
 * @brief Confirms with the user before quitting if there are unsaved changes.
 *
 * Pops a Save / Don't save / Cancel alert when the keyset is dirty, attempts
 * an automatic save if a path is already known, and otherwise routes the
 * user through the save-as panel. On a confirmed quit, persists the window
 * frame and column state.
 *
 * @return True when quitting is allowed; false to abort the quit.
 */
bool
ShortcutsWindow::QuitRequested()
{
	bool result = true;

	if (fKeySetModified) {
		BAlert* alert = new BAlert(WARNING,
			B_TRANSLATE("Save changes before closing?"),
			B_TRANSLATE("Cancel"), B_TRANSLATE("Don't save"),
			B_TRANSLATE("Save"));
		alert->SetShortcut(0, B_ESCAPE);
		alert->SetShortcut(1, 'd');
		alert->SetShortcut(2, 's');
		switch (alert->Go()) {
			case 0:
				result = false;
				break;

			case 1:
				result = true;
				break;

			case 2:
				// Save: automatically if possible, otherwise go back and open
				// up the file requester
				if (fLastSaved.InitCheck() == B_OK) {
					if (_SaveKeySet(fLastSaved) == false) {
						BString text(B_TRANSLATE("%prefname% was unable to save your "
							"KeySet file!"));
						text.ReplaceFirst("%prefname%", B_TRANSLATE_SYSTEM_NAME("Shortcuts"));
						BAlert* alert = new BAlert(ERROR, text,B_TRANSLATE("Oh no"));
						alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
						alert->Go();
						result = true; // quit anyway
					}
				} else {
					PostMessage(SAVE_KEYSET);
					result = false;
				}
				break;
		}
	}

	if (result) {
		fColumnListView->DeselectAll();

		// Save the window position.
		entry_ref ref;
		if (_GetWindowSettingsFile(&ref)) {
			BEntry entry(&ref);
			_SaveWindowSettings(entry);
		}
	}

	return result;
}


/**
 * @brief Locates the user's shortcuts settings file in B_USER_SETTINGS.
 *
 * @param eref Output entry_ref filled with the settings file path.
 * @return True when the file exists and the ref was populated.
 */
bool
ShortcutsWindow::_GetSettingsFile(entry_ref* eref)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return false;
	else
		path.Append(SHORTCUTS_SETTING_FILE_NAME);
	BEntry entry(path.Path(), true);
	entry.GetRef(eref);
	return entry.Exists();
}


/**
 * @brief Persists the current shortcut list to disk.
 *
 * Archives every ShortcutsSpec into a single BMessage and flattens it to
 * the supplied BEntry. Clears the dirty flag and disables the Save button
 * on success.
 *
 * @param saveEntry Destination entry; opened for write/create/erase.
 * @return True on success, false on file or archive failure.
 */
// Saves a settings file to (saveEntry). Returns true iff successful.
bool
ShortcutsWindow::_SaveKeySet(BEntry& saveEntry)
{
	BFile saveTo(&saveEntry, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (saveTo.InitCheck() != B_OK)
		return false;

	BMessage saveMessage;
	for (int i = 0; i < fColumnListView->CountRows(); i++) {
		BMessage next;
		if (((ShortcutsSpec*)fColumnListView->RowAt(i))->Archive(&next)
				== B_OK) {
			saveMessage.AddMessage("spec", &next);
		} else
			printf("Error archiving ShortcutsSpec #%i!\n", i);
	}

	bool result = (saveMessage.Flatten(&saveTo) == B_OK);

	if (result) {
		fKeySetModified = false;
		fSaveButton->SetEnabled(false);
	}

	return result;
}


/**
 * @brief Adds rows from a flattened keyset message into the list view.
 *
 * Iterates every "spec" sub-message, instantiates the matching
 * ShortcutsSpec, and appends it to the column list.
 *
 * @param loadMessage Previously flattened keyset BMessage.
 * @return Always true; per-spec failures are logged but tolerated.
 */
// Appends new entries from the file specified in the "spec" entry of
// (loadMessage). Returns true iff successful.
bool
ShortcutsWindow::_LoadKeySet(const BMessage& loadMessage)
{
	int i = 0;
	BMessage message;
	while (loadMessage.FindMessage("spec", i++, &message) == B_OK) {
		ShortcutsSpec* spec
			= (ShortcutsSpec*)ShortcutsSpec::Instantiate(&message);
		if (spec != NULL)
			fColumnListView->AddRow(spec);
		else
			printf("_LoadKeySet: Error parsing spec!\n");
	}

	return true;
}


/**
 * @brief Resolves the path of the side-band window settings file.
 *
 * @param eref Output entry_ref populated when the settings directory exists.
 * @return True when the ref was populated.
 */
// Gets the filesystem location of the "Shortcuts_window_settings" file.
bool
ShortcutsWindow::_GetWindowSettingsFile(entry_ref* eref)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return false;
	else
		path.Append(WINDOW_SETTINGS_FILE_NAME);

	return BEntry(path.Path(), true).GetRef(eref) == B_OK;
}


/**
 * @brief Persists window frame and column state to a side-band file.
 *
 * Errors are silently ignored because window placement is non-essential.
 *
 * @param saveEntry Destination entry for the flattened settings.
 */
// Saves the application settings file to (saveEntry).  Because this is a
// non-essential file, errors are ignored when writing the settings.
void
ShortcutsWindow::_SaveWindowSettings(BEntry& saveEntry)
{
	BFile saveTo(&saveEntry, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (saveTo.InitCheck() != B_OK)
		return;

	BMessage saveMsg;
	saveMsg.AddRect("window frame", Frame());

	BMessage columnsState;
	fColumnListView->SaveState(&columnsState);
	saveMsg.AddMessage ("columns state", &columnsState);

	saveMsg.Flatten(&saveTo);
}


/**
 * @brief Restores window frame and column state from a side-band message.
 *
 * Clamps the restored size to the computed minimum and the restored origin
 * to the current screen, so a relocated/resized monitor cannot leave the
 * window off-screen.
 *
 * @param loadMessage Previously flattened settings message.
 */
// Loads the application settings file from (loadMessage) and resizes
// the interface to match the previously saved settings. Because this
// is a non-essential file, errors are ignored when loading the settings.
void
ShortcutsWindow::_LoadWindowSettings(const BMessage& loadMessage)
{
	BRect frame;
	if (loadMessage.FindRect("window frame", &frame) == B_OK) {
		// ensure the frame does not resize below the computed minimum.
		float width = max_c(Bounds().right, frame.right - frame.left);
		float height = max_c(Bounds().bottom, frame.bottom - frame.top);
		ResizeTo(width, height);

		// ensure the frame is not placed outside of the screen.
		BScreen screen(this);
		float left = min_c(screen.Frame().right - width, frame.left);
		float top = min_c(screen.Frame().bottom - height, frame.top);
		MoveTo(left, top);
	}

	BMessage columnsStateMessage;
	if (loadMessage.FindMessage ("columns state", &columnsStateMessage) == B_OK)
		fColumnListView->LoadState(&columnsStateMessage);
}


/**
 * @brief Adds a new shortcut row, optionally pre-populated from the selection.
 *
 * Marks the keyset dirty, copies the current selection if any, otherwise
 * creates an empty row, and seeds it with the supplied default command and
 * key code.
 *
 * @param defaultCommand Initial command line, or NULL to leave blank.
 * @param keyCode        Optional initial key code; ignored when zero.
 */
// Creates a new entry and adds it to the GUI. (defaultCommand) will be the
// text in the entry, or NULL if no text is desired.
void
ShortcutsWindow::_AddNewSpec(const char* defaultCommand, uint32 keyCode)
{
	_MarkKeySetModified();

	ShortcutsSpec* spec;
	BRow* curSel = fColumnListView->CurrentSelection();
	if (curSel)
		spec = new ShortcutsSpec(*((ShortcutsSpec*)curSel));
	else {
		spec = new ShortcutsSpec("");
		for (int i = 0; i < fColumnListView->CountColumns(); i++)
			spec->SetField(new BStringField(""), i);
	}

	fColumnListView->AddRow(spec);
	fColumnListView->AddToSelection(spec);
	fColumnListView->ScrollTo(spec);
	if (defaultCommand)
		spec->SetCommand(defaultCommand);
	if (keyCode != 0) {
		spec->ProcessColumnTextString(ShortcutsSpec::KEY_COLUMN_INDEX,
			GetFallbackKeyName(keyCode).String());
	}
}


/**
 * @brief Routes incoming messages to the appropriate UI handler.
 *
 * Handles file-panel commands (open / append / revert / save / save as),
 * drag-and-drop additions, B_REFS_RECEIVED loads, application-picker
 * confirmations, list add/remove, selection updates, and per-cell
 * modifications produced by the column list view.
 *
 * @param message Incoming BMessage.
 */
void
ShortcutsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case OPEN_KEYSET:
		case APPEND_KEYSET:
			fLastOpenWasAppend = (message->what == APPEND_KEYSET);
			if (fOpenPanel)
				fOpenPanel->Show();
			else {
				BMessenger messenger(this);
				fOpenPanel = new BFilePanel(B_OPEN_PANEL, &messenger, NULL,
					0, false);
				fOpenPanel->Show();
			}
			fOpenPanel->SetButtonLabel(B_DEFAULT_BUTTON, fLastOpenWasAppend ?
				B_TRANSLATE("Append") : B_TRANSLATE("Open"));
			break;

		// send a message to myself, to get me to reload the settings file
		case REVERT_KEYSET:
		{
			fLastOpenWasAppend = false;
			BMessage reload(B_REFS_RECEIVED);
			entry_ref eref;
			_GetSettingsFile(&eref);
			reload.AddRef("refs", &eref);
			reload.AddString("startupRef", "yeah");
			PostMessage(&reload);
			break;
		}

		// respond to drag-and-drop messages here
		case B_SIMPLE_DATA:
		{
			int i = 0;

			entry_ref ref;
			while (message->FindRef("refs", i++, &ref) == B_OK) {
				BEntry entry(&ref);
				if (entry.InitCheck() == B_OK) {
					BPath path(&entry);

					if (path.InitCheck() == B_OK) {
						// Add a new item with the given path.
						BString str(path.Path());
						DoStandardEscapes(str);
						_AddNewSpec(str.String());
					}
				}
			}
			break;
		}

		// respond to FileRequester's messages here
		case B_REFS_RECEIVED:
		{
			// Find file ref
			entry_ref ref;
			bool isStartMsg = message->HasString("startupRef");
			if (message->FindRef("refs", &ref) == B_OK) {
				// load the file into (fileMsg)
				BMessage fileMsg;
				{
					BFile file(&ref, B_READ_ONLY);
					if ((file.InitCheck() != B_OK)
						|| (fileMsg.Unflatten(&file) != B_OK)) {
						if (isStartMsg) {
							// use this to save to anyway
							fLastSaved = BEntry(&ref);
							break;
						} else {
							BAlert* alert = new BAlert(ERROR,
								B_TRANSLATE("Shortcuts was couldn't open your "
								"KeySet file!"), B_TRANSLATE("OK"));
							alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
							alert->Go(NULL);
							break;
						}
					}
				}

				if (fLastOpenWasAppend == false) {
					// Clear the menu...
					while (fColumnListView->CountRows()) {
						ShortcutsSpec* row =
							static_cast<ShortcutsSpec*>(fColumnListView->RowAt(0));
						fColumnListView->RemoveRow(row);
						delete row;
					}
				}

				if (_LoadKeySet(fileMsg)) {
					if (isStartMsg) fLastSaved = BEntry(&ref);
					fSaveButton->SetEnabled(isStartMsg == false);

					// If we just loaded in the Shortcuts settings file, then
					// no need to tell the user to save on exit.
					entry_ref eref;
					_GetSettingsFile(&eref);
					if (ref == eref) fKeySetModified = false;
				} else {
						BString text(B_TRANSLATE("%prefname% was unable to parse your "
							"KeySet file!"));
						text.ReplaceFirst("%prefname%", B_TRANSLATE_SYSTEM_NAME("Shortcuts"));
						BAlert* alert = new BAlert(ERROR, text, B_TRANSLATE("OK"));
					alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
					alert->Go(NULL);
					break;
				}
			}
			break;
		}

		// these messages come from the pop-up menu of the Applications column
		case SELECT_APPLICATION:
		{
			ShortcutsSpec* row =
				static_cast<ShortcutsSpec*>(fColumnListView->CurrentSelection());
			if (row != NULL) {
				entry_ref aref;
				if (message->FindRef("refs", &aref) == B_OK) {
					BEntry ent(&aref);
					if (ent.InitCheck() == B_OK) {
						BPath path;
						if ((ent.GetPath(&path) == B_OK)
							&& (row->
								ProcessColumnTextString(ShortcutsSpec::STRING_COLUMN_INDEX,
									path.Path()))) {
							_MarkKeySetModified();
						}
					}
				}
			}
			break;
		}

		case SAVE_KEYSET:
		{
			bool showSaveError = false;

			const char* name;
			entry_ref entry;
			if ((message->FindString("name", &name) == B_OK)
				&& (message->FindRef("directory", &entry) == B_OK)) {
				BDirectory dir(&entry);
				BEntry saveTo(&dir, name, true);
				showSaveError = ((saveTo.InitCheck() != B_OK)
					|| (_SaveKeySet(saveTo) == false));
			} else if (fLastSaved.InitCheck() == B_OK) {
				// We've saved this before, save over previous file.
				showSaveError = (_SaveKeySet(fLastSaved) == false);
			} else
				PostMessage(SAVE_KEYSET_AS);
					// open the save requester...

			if (showSaveError) {
				BString text(B_TRANSLATE("%prefname% wasn't able to save your keyset."));
				text.ReplaceFirst("%prefname%", B_TRANSLATE_SYSTEM_NAME("Shortcuts"));
				BAlert* alert = new BAlert(ERROR, text, B_TRANSLATE("OK"));
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				alert->Go(NULL);
			}
			break;
		}

		case SAVE_KEYSET_AS:
		{
			if (fSavePanel)
				fSavePanel->Show();
			else {
				BMessage message(SAVE_KEYSET);
				BMessenger messenger(this);
				fSavePanel = new BFilePanel(B_SAVE_PANEL, &messenger, NULL, 0,
					false, &message);
				fSavePanel->Show();
			}
			break;
		}

		case ADD_HOTKEY_ITEM:
			_AddNewSpec(NULL);
			break;

		case REMOVE_HOTKEY_ITEM:
		{
			BRow* item = fColumnListView->CurrentSelection();
			if (item) {
				int index = fColumnListView->IndexOf(item);
				fColumnListView->RemoveRow(item);
				delete item;
				_MarkKeySetModified();

				// Rules for new selection: If there is an item at (index),
				// select it. Otherwise, if there is an item at (index-1),
				// select it. Otherwise, select nothing.
				int num = fColumnListView->CountRows();
				if (num > 0) {
					if (index < num)
						fColumnListView->AddToSelection(
							fColumnListView->RowAt(index));
					else {
						if (index > 0)
							index--;
						if (index < num)
							fColumnListView->AddToSelection(
								fColumnListView->RowAt(index));
					}
				}
			}
			break;
		}

		// Received when the user clicks on the ColumnListView
		case HOTKEY_ITEM_SELECTED:
		{
			if (fColumnListView->CountRows() > 0)
				fRemoveButton->SetEnabled(true);
			else
				fRemoveButton->SetEnabled(false);
			break;
		}

		// Received when an entry is to be modified in response to GUI activity
		case HOTKEY_ITEM_MODIFIED:
		{
			int32 row, column;

			if ((message->FindInt32("row", &row) == B_OK)
				&& (message->FindInt32("column", &column) == B_OK)) {
				int32 key;
				const char* bytes;

				if (row >= 0) {
					ShortcutsSpec* item = (ShortcutsSpec*)
						fColumnListView->RowAt(row);
					bool repaintNeeded = false; // default

					if (message->HasInt32("mouseClick")) {
						repaintNeeded = item->ProcessColumnMouseClick(column);
					} else if ((message->FindString("bytes", &bytes) == B_OK)
						&& (message->FindInt32("key", &key) == B_OK)) {
						repaintNeeded = item->ProcessColumnKeyStroke(column,
							bytes, key);
					} else if (message->FindInt32("unmappedkey", &key) ==
						B_OK) {
						repaintNeeded = ((column == item->KEY_COLUMN_INDEX)
							&& ((key > 0xFF) || (GetKeyName(key) != NULL))
							&& (item->ProcessColumnKeyStroke(column, NULL,
							key)));
					} else if (message->FindString("text", &bytes) == B_OK) {
						if ((bytes[0] == '(')&&(bytes[1] == 'C')) {
							if (fSelectPanel)
								fSelectPanel->Show();
							else {
								BMessage message(SELECT_APPLICATION);
								BMessenger m(this);
								fSelectPanel = new BFilePanel(B_OPEN_PANEL, &m,
									NULL, 0, false, &message);
								fSelectPanel->Show();
							}
							fSelectPanel->SetButtonLabel(B_DEFAULT_BUTTON,
								B_TRANSLATE("Select"));
						} else
							repaintNeeded = item->ProcessColumnTextString(
								column, bytes);
					}

					if (repaintNeeded) {
						fColumnListView->Invalidate(row);
						_MarkKeySetModified();
					}
				}
			}
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


/**
 * @brief Marks the keyset as modified and re-enables the Save button.
 */
void
ShortcutsWindow::_MarkKeySetModified()
{
	if (fKeySetModified == false) {
		fKeySetModified = true;
		fSaveButton->SetEnabled(true);
	}
}


/**
 * @brief Forwards the quit request to the BWindow base class.
 */
void
ShortcutsWindow::Quit()
{
	BWindow::Quit();
}


/**
 * @brief Pre-routes drag, clipboard, and key events for direct handling.
 *
 * Diverts B_SIMPLE_DATA to MessageReceived(), implements copy/cut/paste of
 * the selected row's command text via the system clipboard, and converts
 * raw key-down events targeted at the column list into key-name updates on
 * the selected ShortcutsSpec. Anything else falls through to BWindow.
 *
 * @param message Incoming message.
 * @param handler Handler the framework would otherwise dispatch to.
 */
void
ShortcutsWindow::DispatchMessage(BMessage* message, BHandler* handler)
{
	switch (message->what) {
		case B_SIMPLE_DATA:
			MessageReceived(message);
			break;

		case B_COPY:
		case B_CUT:
			if (be_clipboard->Lock()) {
				ShortcutsSpec* row =
					static_cast<ShortcutsSpec*>(fColumnListView->CurrentSelection());
				if (row) {
					BMessage* data = be_clipboard->Data();
					data->RemoveName("text/plain");
					data->AddData("text/plain", B_MIME_TYPE,
						row->GetCellText(ShortcutsSpec::STRING_COLUMN_INDEX),
						strlen(row->GetCellText(ShortcutsSpec::STRING_COLUMN_INDEX)));
					be_clipboard->Commit();

					if (message->what == B_CUT) {
						row->ProcessColumnTextString(
							ShortcutsSpec::STRING_COLUMN_INDEX, "");
						_MarkKeySetModified();
					}
				}
				be_clipboard->Unlock();
			}
			break;

		case B_PASTE:
			if (be_clipboard->Lock()) {
				BMessage* data = be_clipboard->Data();
				const char* text;
				ssize_t textLen;
				if (data->FindData("text/plain", B_MIME_TYPE, (const void**)
					&text, &textLen) == B_OK) {
					ShortcutsSpec* row =
					static_cast<ShortcutsSpec*>(fColumnListView->CurrentSelection());
					if (row) {
						for (ssize_t i = 0; i < textLen; i++) {
							char buf[2] = {text[i], 0x00};
							row->ProcessColumnKeyStroke(
								ShortcutsSpec::STRING_COLUMN_INDEX, buf, 0);
						}
					}
					_MarkKeySetModified();
				}
				be_clipboard->Unlock();
			}
			break;

		case B_KEY_DOWN:
		case B_UNMAPPED_KEY_DOWN:
		{
			ShortcutsSpec* selected;
			int32 modifiers = message->GetInt32("modifiers", 0);
			// These should not block key detection here:
			modifiers &= ~(B_CAPS_LOCK | B_SCROLL_LOCK | B_NUM_LOCK);
			if (modifiers != 0)
				BWindow::DispatchMessage(message, handler);
			else if (handler == fColumnListView
				&& (selected =
					static_cast<ShortcutsSpec*>(fColumnListView->CurrentSelection()))) {
				uint32 keyCode = message->GetInt32("key", 0);
				const char* keyName = GetKeyName(keyCode);
				selected->ProcessColumnTextString(
						ShortcutsSpec::KEY_COLUMN_INDEX,
						keyName != NULL ? keyName : GetFallbackKeyName(keyCode).String());
				_MarkKeySetModified();
			}
			break;
		}
		default:
			BWindow::DispatchMessage(message, handler);
			break;
	}
}
