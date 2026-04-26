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
 *   Copyright 2006-2010, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file FileTypeWindow.cpp
 * @brief Implementation of the per-file MIME-type editor window. Edits the
 *        BEOS:TYPE and BEOS:PREF_APP attributes of one or several entries
 *        and lets the user pick a type from the database, drop another
 *        file as a "same as" reference, or drop an executable to set the
 *        preferred application.
 */


#include "FileTypes.h"
#include "FileTypeWindow.h"
#include "IconView.h"
#include "PreferredAppMenu.h"
#include "TypeListWindow.h"

#include <Application.h>
#include <Bitmap.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <File.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Mime.h>
#include <NodeInfo.h>
#include <PopUpMenu.h>
#include <SpaceLayoutItem.h>
#include <TextControl.h>

#include <stdio.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "FileType Window"


/** @brief Type text control was edited; commit the new MIME type. */
const uint32 kMsgTypeEntered = 'type';
/** @brief Open the modal type chooser. */
const uint32 kMsgSelectType = 'sltp';
/** @brief A type was picked in the chooser. */
const uint32 kMsgTypeSelected = 'tpsd';
/** @brief Open the file panel to copy the type from another file. */
const uint32 kMsgSameTypeAs = 'stpa';
/** @brief The file panel returned a "same type as" target. */
const uint32 kMsgSameTypeAsOpened = 'stpO';

/** @brief A preferred-application menu entry was chosen. */
const uint32 kMsgPreferredAppChosen = 'papc';
/** @brief Open the file panel to pick a preferred application. */
const uint32 kMsgSelectPreferredApp = 'slpa';
/** @brief Open the file panel to copy the preferred app from another file. */
const uint32 kMsgSamePreferredAppAs = 'spaa';
/** @brief The file panel returned a preferred-app executable. */
const uint32 kMsgPreferredAppOpened = 'paOp';
/** @brief The file panel returned a "same preferred app as" target. */
const uint32 kMsgSamePreferredAppAsOpened = 'spaO';


/**
 * @brief Builds the editor window and populates it from the entry refs
 *        carried by @a refs.
 *
 * Creates the File Type, Icon, and Preferred Application boxes, registers
 * for MIME database notifications via BMimeType::StartWatching, and
 * pipes the initial state through _SetTo().
 *
 * @param position  Top-left corner where the window should appear.
 * @param refs      Message containing one or more "refs" entries to edit.
 */
FileTypeWindow::FileTypeWindow(BPoint position, const BMessage& refs)
	:
	BWindow(BRect(0.0f, 0.0f, 300.0f, 200.0f).OffsetBySelf(position),
		B_TRANSLATE("File type"), B_TITLED_WINDOW,
		B_NOT_V_RESIZABLE | B_NOT_ZOOMABLE
			| B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS)
{
	float padding = be_control_look->DefaultItemSpacing();

	// "File Type" group
	BBox* fileTypeBox = new BBox("file type BBox");
	fileTypeBox->SetLabel(B_TRANSLATE("File type"));

	fTypeControl = new BTextControl("type", NULL, "Type Control",
		new BMessage(kMsgTypeEntered));

	// filter out invalid characters that can't be part of a MIME type name
	BTextView* textView = fTypeControl->TextView();
	const char* disallowedCharacters = "<>@,;:\"()[]?=";
	for (int32 i = 0; disallowedCharacters[i]; i++) {
		textView->DisallowChar(disallowedCharacters[i]);
	}

	fSelectTypeButton = new BButton("select type",
		B_TRANSLATE("Select" B_UTF8_ELLIPSIS), new BMessage(kMsgSelectType));

	fSameTypeAsButton = new BButton("same type as",
		B_TRANSLATE_COMMENT("Same as" B_UTF8_ELLIPSIS,
			"The same TYPE as ..."), new BMessage(kMsgSameTypeAs));

	BLayoutBuilder::Grid<>(fileTypeBox, padding, padding / 2)
		.SetInsets(padding, padding * 2, padding, padding)
		.Add(fTypeControl, 0, 0, 3, 1)
		.Add(fSelectTypeButton, 0, 1)
		.Add(fSameTypeAsButton, 1, 1);

	// "Icon" group

	BBox* iconBox = new BBox("icon BBox");
	iconBox->SetLabel(B_TRANSLATE("Icon"));
	fIconView = new IconView("icon");
	BLayoutBuilder::Group<>(iconBox, B_HORIZONTAL)
		.SetInsets(padding, padding * 2, padding, padding)
		.Add(fIconView);

	// "Preferred Application" group

	BBox* preferredBox = new BBox("preferred BBox");
	preferredBox->SetLabel(B_TRANSLATE("Preferred application"));

	BMenu* menu = new BPopUpMenu("preferred");
	BMenuItem* item;
	menu->AddItem(item = new BMenuItem(B_TRANSLATE("Default application"),
		new BMessage(kMsgPreferredAppChosen)));
	item->SetMarked(true);

	fPreferredField = new BMenuField("preferred", NULL, menu);

	fSelectAppButton = new BButton("select app",
		B_TRANSLATE("Select" B_UTF8_ELLIPSIS),
		new BMessage(kMsgSelectPreferredApp));

	fSameAppAsButton = new BButton("same app as",
		B_TRANSLATE_COMMENT("Same as" B_UTF8_ELLIPSIS,
			"The same APPLICATION as ..."),
			new BMessage(kMsgSamePreferredAppAs));

	BLayoutBuilder::Grid<>(preferredBox, padding, padding / 2)
		.SetInsets(padding, padding * 2, padding, padding)
		.Add(fPreferredField, 0, 0, 2, 1)
		.Add(fSelectAppButton, 0, 1)
		.Add(fSameAppAsButton, 1, 1);

	BLayoutBuilder::Grid<>(this)
		.SetInsets(padding)
		.Add(fileTypeBox, 0, 0, 2, 1)
		.Add(preferredBox, 0, 1, 1, 1)
		.Add(iconBox, 1, 1, 1, 1);

	fTypeControl->MakeFocus(true);
	BMimeType::StartWatching(this);
	_SetTo(refs);
}


/**
 * @brief Stops MIME database watching and tears the window down.
 */
FileTypeWindow::~FileTypeWindow()
{
	BMimeType::StopWatching(this);
}


/**
 * @brief Computes the window title for @a refs.
 *
 * For a single ref the file name is used. For multiple refs the title
 * mentions the common parent directory when all entries share one,
 * otherwise the generic "[Multiple files]" form.
 *
 * @param refs  Message holding the refs being edited.
 * @return      Localized window title.
 */
BString
FileTypeWindow::_Title(const BMessage& refs)
{
	BString title;

	entry_ref ref;
	if (refs.FindRef("refs", 1, &ref) == B_OK) {
		bool same = false;
		BEntry entry, parent;
		if (entry.SetTo(&ref) == B_OK
			&& entry.GetParent(&parent) == B_OK) {
			same = true;

			// Check if all entries have the same parent directory
			for (int32 i = 0; refs.FindRef("refs", i, &ref) == B_OK; i++) {
				BEntry directory;
				if (entry.SetTo(&ref) == B_OK
					&& entry.GetParent(&directory) == B_OK) {
					if (directory != parent) {
						same = false;
						break;
					}
				}
			}
		}

		char name[B_FILE_NAME_LENGTH];
		if (same && parent.GetName(name) == B_OK) {
			char buffer[512];
			snprintf(buffer, sizeof(buffer),
				B_TRANSLATE("Multiple files from \"%s\" file type"), name);
			title = buffer;
		} else
			title = B_TRANSLATE("[Multiple files] file types");
	} else if (refs.FindRef("refs", 0, &ref) == B_OK) {
		char buffer[512];
		snprintf(buffer, sizeof(buffer), B_TRANSLATE("%s file type"), ref.name);
		title = buffer;
	}

	return title;
}


/**
 * @brief Reads each ref in @a refs and updates the controls to show the
 *        common type, common preferred app, and (when single) the icon.
 *
 * Refs whose node info is unreadable are skipped. The internal entry
 * list is populated for later writebacks; the icon view is switched to
 * "icon heap" mode when more than one entry is shown.
 *
 * @param refs  Message containing one or more "refs" entries.
 */
void
FileTypeWindow::_SetTo(const BMessage& refs)
{
	SetTitle(_Title(refs).String());

	// get common info and icons

	fCommonPreferredApp = "";
	fCommonType = "";
	entry_ref ref;
	for (int32 i = 0; refs.FindRef("refs", i, &ref) == B_OK; i++) {
		BNode node(&ref);
		if (node.InitCheck() != B_OK)
			continue;

		BNodeInfo info(&node);
		if (info.InitCheck() != B_OK)
			continue;

		// TODO: watch entries?

		entry_ref* copiedRef = new entry_ref(ref);
		fEntries.AddItem(copiedRef);

		char type[B_MIME_TYPE_LENGTH];
		if (info.GetType(type) != B_OK)
			type[0] = '\0';

		if (i > 0) {
			if (fCommonType != type)
				fCommonType = "";
		} else
			fCommonType = type;

		char preferredApp[B_MIME_TYPE_LENGTH];
		if (info.GetPreferredApp(preferredApp) != B_OK)
			preferredApp[0] = '\0';

		if (i > 0) {
			if (fCommonPreferredApp != preferredApp)
				fCommonPreferredApp = "";
		} else
			fCommonPreferredApp = preferredApp;

		if (i == 0)
			fIconView->SetTo(ref);
	}

	fTypeControl->SetText(fCommonType.String());
	_UpdatePreferredApps();

	fIconView->ShowIconHeap(fEntries.CountItems() != 1);
}


/**
 * @brief Reads the MIME type of the file referenced in @a message and
 *        applies it to every entry currently being edited.
 *
 * Used as the handler for "Same type as..." drops and file-panel
 * results. Errors are reported via error_alert().
 *
 * @param message  Message containing exactly one "refs" entry.
 */
void
FileTypeWindow::_AdoptType(BMessage* message)
{
	entry_ref ref;
	if (message == NULL || message->FindRef("refs", &ref) != B_OK)
		return;

	BNode node(&ref);
	status_t status = node.InitCheck();

	char type[B_MIME_TYPE_LENGTH];

	if (status == B_OK) {
			// get type from file
		BNodeInfo nodeInfo(&node);
		status = nodeInfo.InitCheck();
		if (status == B_OK) {
			if (nodeInfo.GetType(type) != B_OK)
				type[0] = '\0';
		}
	}

	if (status != B_OK) {
		error_alert(B_TRANSLATE("Could not open file"), status);
		return;
	}

	fCommonType = type;
	fTypeControl->SetText(type);
	_AdoptType();
}


/**
 * @brief Writes @a fCommonType into the BEOS:TYPE attribute of every
 *        edited entry.
 *
 * Entries whose node or node info cannot be opened are silently skipped.
 */
void
FileTypeWindow::_AdoptType()
{
	for (int32 i = 0; i < fEntries.CountItems(); i++) {
		BNode node(fEntries.ItemAt(i));
		BNodeInfo info(&node);
		if (node.InitCheck() != B_OK
			|| info.InitCheck() != B_OK)
			continue;

		info.SetType(fCommonType.String());
	}
}


/**
 * @brief Resolves a preferred-application choice from @a message and
 *        applies it to every edited entry.
 *
 * @param message  Refs message from a file panel or drop.
 * @param sameAs   True when the dropped entry should be treated as a
 *                 reference whose preferred app is to be reused; false
 *                 when the dropped entry is itself the application.
 */
void
FileTypeWindow::_AdoptPreferredApp(BMessage* message, bool sameAs)
{
	if (retrieve_preferred_app(message, sameAs, fCommonType.String(),
			fCommonPreferredApp) == B_OK) {
		_AdoptPreferredApp();
		_UpdatePreferredApps();
	}
}


/**
 * @brief Writes @a fCommonPreferredApp into the BEOS:PREF_APP attribute
 *        of every edited entry, or removes the attribute when empty.
 */
void
FileTypeWindow::_AdoptPreferredApp()
{
	for (int32 i = 0; i < fEntries.CountItems(); i++) {
		BNode node(fEntries.ItemAt(i));
		if (fCommonPreferredApp.Length() == 0) {
			node.RemoveAttr("BEOS:PREF_APP");
			continue;
		}

		BNodeInfo info(&node);
		if (node.InitCheck() != B_OK
			|| info.InitCheck() != B_OK)
			continue;

		info.SetPreferredApp(fCommonPreferredApp.String());
	}
}


/**
 * @brief Rebuilds the preferred-application pop-up to reflect the
 *        current MIME type and selected signature.
 */
void
FileTypeWindow::_UpdatePreferredApps()
{
	BMimeType type(fCommonType.String());
	update_preferred_app_menu(fPreferredField->Menu(), &type,
		kMsgPreferredAppChosen, fCommonPreferredApp.String());
}


/**
 * @brief Routes UI events: type editing, type chooser, file-panel
 *        results, preferred-app menu and panel handling, drag-and-drop
 *        of refs, and MIME database change notifications.
 *
 * @param message  Incoming BMessage.
 */
void
FileTypeWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		// File Type group

		case kMsgTypeEntered:
			fCommonType = fTypeControl->Text();
			_AdoptType();
			break;

		case kMsgSelectType:
		{
			BWindow* window = new TypeListWindow(fCommonType.String(),
				kMsgTypeSelected, this);
			window->Show();
			break;
		}
		case kMsgTypeSelected:
		{
			const char* type;
			if (message->FindString("type", &type) == B_OK) {
				fCommonType = type;
				fTypeControl->SetText(type);
				_AdoptType();
			}
			break;
		}

		case kMsgSameTypeAs:
		{
			BMessage panel(kMsgOpenFilePanel);
			panel.AddString("title", B_TRANSLATE("Select same type as"));
			panel.AddInt32("message", kMsgSameTypeAsOpened);
			panel.AddMessenger("target", this);
			panel.AddBool("allowDirs", true);

			be_app_messenger.SendMessage(&panel);
			break;
		}
		case kMsgSameTypeAsOpened:
			_AdoptType(message);
			break;

		// Preferred Application group

		case kMsgPreferredAppChosen:
		{
			const char* signature;
			if (message->FindString("signature", &signature) == B_OK)
				fCommonPreferredApp = signature;
			else
				fCommonPreferredApp = "";

			_AdoptPreferredApp();
			break;
		}

		case kMsgSelectPreferredApp:
		{
			BMessage panel(kMsgOpenFilePanel);
			panel.AddString("title",
				B_TRANSLATE("Select preferred application"));
			panel.AddInt32("message", kMsgPreferredAppOpened);
			panel.AddMessenger("target", this);

			be_app_messenger.SendMessage(&panel);
			break;
		}
		case kMsgPreferredAppOpened:
			_AdoptPreferredApp(message, false);
			break;

		case kMsgSamePreferredAppAs:
		{
			BMessage panel(kMsgOpenFilePanel);
			panel.AddString("title",
				B_TRANSLATE("Select same preferred application as"));
			panel.AddInt32("message", kMsgSamePreferredAppAsOpened);
			panel.AddMessenger("target", this);
			panel.AddBool("allowDirs", true);

			be_app_messenger.SendMessage(&panel);
			break;
		}
		case kMsgSamePreferredAppAsOpened:
			_AdoptPreferredApp(message, true);
			break;

		// Other

		case B_SIMPLE_DATA:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) != B_OK)
				break;

			BFile file(&ref, B_READ_ONLY);
			if (is_application(file))
				_AdoptPreferredApp(message, false);
			else
				_AdoptType(message);
			break;
		}

		case B_META_MIME_CHANGED:
			const char* type;
			int32 which;
			if (message->FindString("be:type", &type) != B_OK
				|| message->FindInt32("be:which", &which) != B_OK)
				break;

			if (which == B_MIME_TYPE_DELETED
				|| which == B_SUPPORTED_TYPES_CHANGED) {
				_UpdatePreferredApps();
			}
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


/**
 * @brief Notifies the BApplication that this window is closing so the
 *        live-window counter can be decremented.
 *
 * @return Always true.
 */
bool
FileTypeWindow::QuitRequested()
{
	be_app->PostMessage(kMsgTypeWindowClosed);
	return true;
}

