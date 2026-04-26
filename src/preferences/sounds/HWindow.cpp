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
 *   Copyright 2003-2008, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Jérôme Duval
 *       Oliver Ruiz Dorantes
 *       Atsushi Takamatsu
 */


/**
 * @file HWindow.cpp
 * @brief Main window for the Sounds preferences app.
 *
 * Hosts the event list, a sound-file picker menu populated from the system
 * sound directories, the embedded SoundFilePanel for browsing custom files,
 * and Play/Stop buttons backed by BFileGameSound. Persists the window frame
 * and last-used directory across runs.
 *
 * @see HEventList, SoundFilePanel, BFileGameSound, BMediaFiles
 */


#include "HWindow.h"
#include "HEventList.h"

#include <stdio.h>

#include <Alert.h>
#include <Application.h>
#include <Beep.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MediaFiles.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <PathFinder.h>
#include <Roster.h>
#include <ScrollView.h>
#include <Sound.h>
#include <StringView.h>

#include <fs_attr.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "HWindow"

/** @brief Leaf name of the settings file under the user settings directory. */
static const char kSettingsFile[] = "Sounds_Settings";
/** @brief UTF-8 play glyph defined in SoundFilePanel.cpp; reused by buttons. */
extern const char* kPlayLabel;
/** @brief UTF-8 stop glyph defined in SoundFilePanel.cpp. */
extern const char* kStopLabel;


/**
 * @brief Builds the window, restores its persisted frame, and chooses the
 *        initial directory shown by the SoundFilePanel.
 *
 * Probes BPathFinder for the system sounds directory, then overlays the
 * persisted frame and last-used directory if a settings file is present.
 *
 * @param rect  Initial window frame (used when no persisted frame exists).
 * @param name  Window title.
 */
HWindow::HWindow(BRect rect, const char* name)
	:
	BWindow(rect, name, B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS),
	fFilePanel(NULL),
	fPlayButton(NULL),
	fPlayer(NULL)
{
	_InitGUI();

	// set default path
	BPathFinder pathFinder;
	BStringList paths;
	pathFinder.FindPaths(B_FIND_PATH_SOUNDS_DIRECTORY, paths);
	for (int i = 0; i < paths.CountStrings(); ++i) {
		BEntry entry(paths.StringAt(i));
		if (entry.Exists()) {
			entry.GetRef(&fPathRef);
			break;
		}
	}

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		path.Append(kSettingsFile);
		BFile file(path.Path(), B_READ_ONLY);

		BMessage msg;
		if (file.InitCheck() == B_OK && msg.Unflatten(&file) == B_OK) {
			if (msg.FindRect("frame", &fFrame) == B_OK) {
				MoveTo(fFrame.LeftTop());
				ResizeTo(fFrame.Width(), fFrame.Height());
			}

			entry_ref ref;
			if (msg.FindRef("last_path", &ref) == B_OK) {
				BNode node(&ref);
				if (node.InitCheck() == B_OK && node.IsDirectory())
					fPathRef = ref;
			}
		}
	}

	fFilePanel = new SoundFilePanel(this);
	fFilePanel->SetTarget(this);
	BEntry entry(&fPathRef);
	if (entry.Exists())
		fFilePanel->SetPanelDirectory(&fPathRef);

	MoveOnScreen();
}


/**
 * @brief Persists the current frame and last-used directory and tears down
 *        the file panel and active sound player.
 */
HWindow::~HWindow()
{
	delete fFilePanel;
	delete fPlayer;

	BPath path;
	BMessage msg;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		path.Append(kSettingsFile);
		BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE);

		if (file.InitCheck() == B_OK) {
			msg.AddRect("frame", fFrame);
			msg.AddRef("last_path", &fPathRef);
			msg.Flatten(&file);
		}
	}
}


/**
 * @brief Routes B_PULSE through _Pulse() before normal dispatch.
 *
 * Used to keep the Stop button's enabled state in sync with the player's
 * current playing state without needing a separate pulse target.
 *
 * @param message  Incoming message; only B_PULSE is special-cased here.
 * @param handler  Target handler chosen by the looper.
 */
void
HWindow::DispatchMessage(BMessage* message, BHandler* handler)
{
	if (message->what == B_PULSE)
		_Pulse();
	BWindow::DispatchMessage(message, handler);
}


/**
 * @brief Handles UI messages from the menu, file panel, and event list.
 *
 * Recognises the "Other..." menu choice (opens the SoundFilePanel), file
 * panel cancel and ref-received messages, the Play / Stop buttons, the
 * M_EVENT_CHANGED notification from HEventList, and the menu items for the
 * built-in "<none>" entry as well as wav files discovered in the system
 * sound directories.
 *
 * @param message  Incoming BMessage.
 */
void
HWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case M_OTHER_MESSAGE:
		{
			BMenuField* menufield
				= dynamic_cast<BMenuField*>(FindView("filemenu"));
			if (menufield == NULL)
				return;
			BMenu* menu = menufield->Menu();

			HEventRow* row = (HEventRow*)fEventList->CurrentSelection();
			if (row != NULL) {
				BPath path(row->Path());
				if (path.InitCheck() != B_OK) {
					BMenuItem* item = menu->FindItem(B_TRANSLATE("<none>"));
					if (item != NULL)
						item->SetMarked(true);
				} else {
					BMenuItem* item = menu->FindItem(path.Leaf());
					if (item != NULL)
						item->SetMarked(true);
				}
			}
			fFilePanel->Show();
			break;
		}

		case B_CANCEL:
		{
			// reset file panel location
			fFilePanel->SetPanelDirectory(&fPathRef);
			break;
		}
		case B_SIMPLE_DATA:
		case B_REFS_RECEIVED:
		{
			entry_ref ref;
			HEventRow* row = (HEventRow*)fEventList->CurrentSelection();
			if (message->FindRef("refs", &ref) == B_OK && row != NULL) {
				BMenuField* menufield
					= dynamic_cast<BMenuField*>(FindView("filemenu"));
				if (menufield == NULL)
					return;
				BMenu* menu = menufield->Menu();

				// add file item
				BMessage* msg = new BMessage(M_ITEM_MESSAGE);
				BPath path(&ref);
				msg->AddRef("refs", &ref);
				BMenuItem* menuitem = menu->FindItem(path.Leaf());
				if (menuitem == NULL)
					menu->AddItem(menuitem = new BMenuItem(path.Leaf(), msg), 0);
				// refresh item
				fEventList->SetPath(BPath(&ref).Path());
				// check file menu
				if (menuitem != NULL)
					menuitem->SetMarked(true);

				// save as last used path and set as file panel location
				path.GetParent(&path);
				get_ref_for_path(path.Path(), &fPathRef);
				fFilePanel->SetPanelDirectory(&fPathRef);
				fPlayButton->SetEnabled(true);
			}
			break;
		}

		case M_PLAY_MESSAGE:
		{
			HEventRow* row = (HEventRow*)fEventList->CurrentSelection();
			if (row != NULL) {
				const char* path = row->Path();
				if (path != NULL) {
					entry_ref ref;
					::get_ref_for_path(path, &ref);
					delete fPlayer;
					fPlayer = new BFileGameSound(&ref, false);
					fPlayer->StartPlaying();
				}
			}
			break;
		}

		case M_STOP_MESSAGE:
		{
			if (fPlayer == NULL)
				break;
			if (fPlayer->IsPlaying()) {
				fPlayer->StopPlaying();
				delete fPlayer;
				fPlayer = NULL;
			}
			break;
		}

		case M_EVENT_CHANGED:
		{
			BMenuField* menufield
				= dynamic_cast<BMenuField*>(FindView("filemenu"));
			if (menufield == NULL)
				return;

			menufield->SetEnabled(true);

			const char* filePath;

			if (message->FindString("path", &filePath) == B_OK) {

				BMenu* menu = menufield->Menu();
				BPath path(filePath);

				if (path.InitCheck() != B_OK) {
					BMenuItem* item = menu->FindItem(B_TRANSLATE("<none>"));
					if (item != NULL)
						item->SetMarked(true);
				} else {
					BMenuItem* item = menu->FindItem(path.Leaf());
					if (item != NULL)
						item->SetMarked(true);
				}

				HEventRow* row = (HEventRow*)fEventList->CurrentSelection();
				if (row != NULL) {
					menufield->SetEnabled(true);

					const char* path = row->Path();
					fPlayButton->SetEnabled(path != NULL && strcmp(path, "") != 0);
				} else {
					menufield->SetEnabled(false);
					fPlayButton->SetEnabled(false);
				}
			}
			break;
		}

		case M_ITEM_MESSAGE:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK) {
				fEventList->SetPath(BPath(&ref).Path());
				_UpdateZoomLimits();

				HEventRow* row = (HEventRow*)fEventList->CurrentSelection();
				fPlayButton->SetEnabled(row != NULL && row->Path() != NULL);
			}
			break;
		}

		case M_NONE_MESSAGE:
		{
			fPlayButton->SetEnabled(false);
			fEventList->SetPath(NULL);
			break;
		}

		default:
			BWindow::MessageReceived(message);
	}
}


/**
 * @brief Captures the current frame for persistence and asks the app to quit.
 *
 * @return Always true; the close is allowed to proceed.
 */
bool
HWindow::QuitRequested()
{
	fFrame = Frame();

	fEventList->RemoveAll();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


/**
 * @brief Builds the view hierarchy and primes initial UI state.
 *
 * Creates the HEventList bound to BMediaFiles::B_SOUNDS, the sound-file
 * BMenuField with its "<none>" and "Other..." items, the Play and Stop
 * buttons (sized from the plain font), and assembles them in a
 * BLayoutBuilder group.
 */
void
HWindow::_InitGUI()
{
	fEventList = new HEventList();
	fEventList->SetType(BMediaFiles::B_SOUNDS);
	fEventList->SetSelectionMode(B_SINGLE_SELECTION_LIST);

	BMenu* menu = new BMenu("file");
	menu->SetRadioMode(true);
	menu->SetLabelFromMarked(true);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(B_TRANSLATE("<none>"),
		new BMessage(M_NONE_MESSAGE)));
	menu->AddItem(new BMenuItem(B_TRANSLATE("Other" B_UTF8_ELLIPSIS),
		new BMessage(M_OTHER_MESSAGE)));

	BString label(B_TRANSLATE("Sound file:"));
	BMenuField* menuField = new BMenuField("filemenu", label, menu);
	menuField->SetDivider(menuField->StringWidth(label) + 10);

	BSize buttonsSize(be_plain_font->Size() * 2.5, be_plain_font->Size() * 2.5);

	BButton* stopbutton = new BButton("stop", kStopLabel, new BMessage(M_STOP_MESSAGE));
	stopbutton->SetEnabled(false);
	stopbutton->SetExplicitSize(buttonsSize);

	// We need at least one view to trigger B_PULSE_NEEDED events which we will
	// intercept in DispatchMessage to trigger the buttons enabling or disabling.
	stopbutton->SetFlags(stopbutton->Flags() | B_PULSE_NEEDED);

	fPlayButton = new BButton("play", kPlayLabel, new BMessage(M_PLAY_MESSAGE));
	fPlayButton->SetEnabled(false);
	fPlayButton->SetExplicitSize(buttonsSize);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fEventList)
		.AddGroup(B_HORIZONTAL)
			.Add(menuField)
			.AddGroup(B_HORIZONTAL, 0)
				.Add(fPlayButton)
				.Add(stopbutton)
			.End()
		.End();

	// setup file menu
	_SetupMenuField();
	BMenuItem* noneItem = menu->FindItem(B_TRANSLATE("<none>"));
	if (noneItem != NULL)
		noneItem->SetMarked(true);

	menuField->SetEnabled(false);

	_UpdateZoomLimits();
}


/**
 * @brief Keeps the Stop button enabled only while the player is active.
 *
 * Called from DispatchMessage() on every B_PULSE so the button reflects the
 * BFileGameSound state without requiring per-state callbacks.
 */
void
HWindow::_Pulse()
{
	BButton* stop = dynamic_cast<BButton*>(FindView("stop"));

	if (stop == NULL)
		return;

	if (fPlayer != NULL) {
		if (fPlayer->IsPlaying())
			stop->SetEnabled(true);
		else
			stop->SetEnabled(false);
	} else
		stop->SetEnabled(false);
}


/**
 * @brief Populates the sound-file menu with currently bound and well-known
 *        wav files.
 *
 * First adds entries for paths already bound to events, then walks the four
 * canonical sound directories (system, system non-packaged, user, user
 * non-packaged) and inserts a menu item per file found there. Duplicate
 * leaf names are skipped so the menu stays compact.
 */
void
HWindow::_SetupMenuField()
{
	BMenuField* menufield = dynamic_cast<BMenuField*>(FindView("filemenu"));
	if (menufield == NULL)
		return;
	BMenu* menu = menufield->Menu();
	int32 count = fEventList->CountRows();
	for (int32 i = 0; i < count; i++) {
		HEventRow* row = (HEventRow*)fEventList->RowAt(i);
		if (row == NULL)
			continue;

		BPath path(row->Path());
		if (path.InitCheck() != B_OK)
			continue;
		if (menu->FindItem(path.Leaf()))
			continue;

		BMessage* msg = new BMessage(M_ITEM_MESSAGE);
		entry_ref ref;
		::get_ref_for_path(path.Path(), &ref);
		msg->AddRef("refs", &ref);
		menu->AddItem(new BMenuItem(path.Leaf(), msg), 0);
	}

	directory_which whichDirectories[] = {
		B_SYSTEM_SOUNDS_DIRECTORY,
		B_SYSTEM_NONPACKAGED_SOUNDS_DIRECTORY,
		B_USER_SOUNDS_DIRECTORY,
		B_USER_NONPACKAGED_SOUNDS_DIRECTORY,
	};

	for (size_t i = 0;
		i < sizeof(whichDirectories) / sizeof(whichDirectories[0]); i++) {
		BPath path;
		BDirectory dir;
		BEntry entry;
		BPath item_path;

		status_t err = find_directory(whichDirectories[i], &path);
		if (err == B_OK)
			err = dir.SetTo(path.Path());
		while (err == B_OK) {
			err = dir.GetNextEntry(&entry, true);
			if (entry.InitCheck() != B_NO_ERROR)
				break;

			if (entry.IsDirectory())
				continue;

			entry.GetPath(&item_path);

			if (menu->FindItem(item_path.Leaf()))
				continue;

			BMessage* msg = new BMessage(M_ITEM_MESSAGE);
			entry_ref ref;
			::get_ref_for_path(item_path.Path(), &ref);
			msg->AddRef("refs", &ref);
			menu->AddItem(new BMenuItem(item_path.Leaf(), msg), 0);
		}
	}
}


/**
 * @brief Recomputes the window's zoom limits to fit the event list contents.
 *
 * The width is the event list's preferred width plus the inset and a
 * vertical scroll bar; the height adds a few inset rows and the button row
 * height (driven by the plain font size).
 */
void
HWindow::_UpdateZoomLimits()
{
	const float kInset = be_control_look->DefaultItemSpacing();

	BSize size = fEventList->PreferredSize();
	SetZoomLimits(size.width + 2 * kInset + B_V_SCROLL_BAR_WIDTH,
		size.height + 5 * kInset + 2 * B_H_SCROLL_BAR_HEIGHT
			+ 2 * be_plain_font->Size() * 2.5);
}
