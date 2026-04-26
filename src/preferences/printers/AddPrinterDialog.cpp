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
 *   Copyright 2002-2009, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Pfeiffer
 *       Philippe Houdoin
 */


/**
 * @file AddPrinterDialog.cpp
 * @brief Modal dialog that creates a new printer entry via print_server.
 *
 * Lets the user pick a name, a driver discovered under add-on directories,
 * and a transport (with optional sub-port menu). On OK the dialog sends a
 * PSRV_MAKE_PRINTER scripting message to print_server.
 *
 * @see TransportMenu, PrinterListView
 */


#include "AddPrinterDialog.h"

#include <stdio.h>

#include <Button.h>
#include <Catalog.h>
#include <FindDirectory.h>
#include <GroupLayoutBuilder.h>
#include <Layout.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <MimeType.h>
#include <NodeInfo.h>
#include <Path.h>

#include "pr_server.h"
#include "Globals.h"
#include "Messages.h"
#include "PrinterListView.h"
#include "TransportMenu.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AddPrinterDialog"


/**
 * @brief Constructs the modal Add Printer dialog and shows it.
 *
 * @param parent BWindow used as the response target for kMsgAddPrinterClosed
 *               when the dialog goes away.
 */
AddPrinterDialog::AddPrinterDialog(BWindow *parent)
	:
	Inherited(BRect(78, 71, 400, 300), B_TRANSLATE("Add printer"),
		B_TITLED_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS
		| B_CLOSE_ON_ESCAPE),
	fPrintersPrefletMessenger(parent)
{
	_BuildGUI(0);

	Show();
}


/**
 * @brief Notifies the parent window before allowing the dialog to close.
 *
 * @return The BWindow base class result.
 */
bool
AddPrinterDialog::QuitRequested()
{
	fPrintersPrefletMessenger.SendMessage(kMsgAddPrinterClosed);
	return Inherited::QuitRequested();
}


/**
 * @brief Dispatches messages from the dialog's controls.
 *
 * Handles the OK and Cancel buttons, name edits, and printer/transport menu
 * selection.
 *
 * @param msg Incoming BMessage.
 */
void
AddPrinterDialog::MessageReceived(BMessage* msg)
{
	switch(msg->what) {
		case B_OK:
			_AddPrinter(msg);
			PostMessage(B_QUIT_REQUESTED);
			break;

		case B_CANCEL:
			PostMessage(B_QUIT_REQUESTED);
			break;

		case kNameChangedMsg:
			fNameText = fName->Text();
			_Update();
			break;

		case kPrinterSelectedMsg:
			_StorePrinter(msg);
			break;

		case kTransportSelectedMsg:
			_HandleChangedTransport(msg);
			break;

		default:
			Inherited::MessageReceived(msg);
			break;
	}
}


/**
 * @brief Sends a PSRV_MAKE_PRINTER scripting request to print_server.
 *
 * Composes the request from the dialog's current state. The Preview driver
 * skips the transport fields because it does not use a transport add-on.
 *
 * @param msg Source message that triggered the action (unused).
 */
void
AddPrinterDialog::_AddPrinter(BMessage *msg)
{
	BMessage m(PSRV_MAKE_PRINTER);
	BMessenger msgr;
	if (GetPrinterServerMessenger(msgr) != B_OK)
		return;

	BString transport;
	BString transportPath;
	if (fPrinterText != "Preview") {
		// Preview printer does not use transport add-on
		transport = fTransportText;
		transportPath = fTransportPathText;
	}

	m.AddString("driver", fPrinterText.String());
	m.AddString("transport", transport.String());
	m.AddString("transport path", transportPath.String());
	m.AddString("printer name", fNameText.String());
	m.AddString("connection", "Local");
	msgr.SendMessage(&m);
		// request print_server to create printer
}


/**
 * @brief Records the user's printer-driver selection.
 *
 * Reads the @c name string from @a msg, stores it in fPrinterText, and
 * triggers a UI refresh via _Update().
 *
 * @param msg Selection message carrying the driver name.
 */
void
AddPrinterDialog::_StorePrinter(BMessage *msg)
{
	BString name;
	if (msg->FindString("name", &name) != B_OK)
		name = "";

	fPrinterText = name;
	_Update();
}


/**
 * @brief Updates internal state after the user picks a transport or port.
 *
 * If @a msg includes a @c path field the user selected a port within a
 * transport submenu; otherwise the user picked a top-level transport. The
 * code keeps menu marks consistent and may auto-fill the printer name with
 * the chosen port label.
 *
 * @param msg Incoming selection message from the transport menu.
 */
void
AddPrinterDialog::_HandleChangedTransport(BMessage *msg)
{
	BString name;
	if (msg->FindString("name", &name) != B_OK) {
		name = "";
	}
	fTransportText = name;

	BString path;
	if (msg->FindString("path", &path) == B_OK) {
		// transport path selected
		fTransportPathText = path;

		// mark sub menu
		void* pointer;
		if (msg->FindPointer("source", &pointer) == B_OK) {
			BMenuItem* item = (BMenuItem*)pointer;

			// Update printer name with Transport Path if not filled in
			if (strlen(fName->Text()) == 0)
				fName->SetText(item->Label());

			BMenu* menu = item->Menu();
			int32 index = fTransport->IndexOf(menu);
			item = fTransport->ItemAt(index);
			if (item != NULL)
				item->SetMarked(true);
		}
	} else {
		// transport selected
		fTransportPathText = "";

		// remove mark from item in sub menu of transport sub menu
		for (int32 i = fTransport->CountItems() - 1; i >= 0; i --) {
			BMenu* menu = fTransport->SubmenuAt(i);
			if (menu != NULL) {
				BMenuItem* item = menu->FindMarked();
				if (item != NULL)
					item->SetMarked(false);
			}
		}
	}
	_Update();
}


/**
 * @brief Constructs the dialog's controls and lays them out.
 *
 * Builds the printer name field, the driver and transport pop-up menus,
 * and the Add/Cancel buttons. The accompanying ASCII storyboards describe
 * the planned multi-stage flow.
 *
 * @param stage Reserved for the staged "local vs network printer" flow
 *              described in the comments below; only stage 0 is currently
 *              implemented.
 */
void
AddPrinterDialog::_BuildGUI(int stage)
{
	// add a "printer name" input field
	fName = new BTextControl("printer_name", B_TRANSLATE("Printer name:"),
		B_EMPTY_STRING, NULL);
	fName->SetFont(be_bold_font);
	fName->SetAlignment(B_ALIGN_RIGHT, B_ALIGN_LEFT);
	fName->SetModificationMessage(new BMessage(kNameChangedMsg));

	// add a "driver" popup menu field
	fPrinter = new BPopUpMenu(B_TRANSLATE("<pick one>"));
	BMenuField *printerMenuField = new BMenuField("drivers_list",
		B_TRANSLATE("Printer type:"), fPrinter);
	printerMenuField->SetAlignment(B_ALIGN_RIGHT);
	_FillMenu(fPrinter, "Print", kPrinterSelectedMsg);

	// add a "connected to" (aka transports list) menu field
	fTransport = new BPopUpMenu(B_TRANSLATE("<pick one>"));
	BMenuField *transportMenuField = new BMenuField("transports_list",
		B_TRANSLATE("Connected to:"), fTransport);
	transportMenuField->SetAlignment(B_ALIGN_RIGHT);
	_FillTransportMenu(fTransport);

	// add a "OK" button
	fOk = new BButton(NULL, B_TRANSLATE("Add"), new BMessage((uint32)B_OK),
		B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM);

	// add a "Cancel button
	BButton *cancel = new BButton(NULL, B_TRANSLATE("Cancel"),
		new BMessage(B_CANCEL));

	BLayoutBuilder::Grid<>(this, B_USE_ITEM_SPACING, B_USE_ITEM_SPACING)
		.Add(fName->CreateLabelLayoutItem(), 0, 0)
		.Add(fName->CreateTextViewLayoutItem(), 1, 0)
		.Add(printerMenuField->CreateLabelLayoutItem(), 0, 1)
		.Add(printerMenuField->CreateMenuBarLayoutItem(), 1, 1)
		.Add(transportMenuField->CreateLabelLayoutItem(), 0, 2)
		.Add(transportMenuField->CreateMenuBarLayoutItem(), 1, 2)
		.Add(BGroupLayoutBuilder(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(fOk)
			, 0, 3, 2)
		.SetInsets(B_USE_WINDOW_SPACING, B_USE_WINDOW_SPACING,
			B_USE_WINDOW_SPACING, B_USE_WINDOW_SPACING);

	AddShortcut('W', B_COMMAND_KEY, new BMessage(B_QUIT_REQUESTED));

	SetDefaultButton(fOk);
	fOk->MakeDefault(true);

	fName->MakeFocus(true);

	_Update();
// Stage == 0
// init_icon 64x114  Add a Local or Network Printer
//                   ------------------------------
//                   I would like to add a...
//                              Local Printer
//                              Network Printer
// ------------------------------------------------
//                                Cancel   Continue

// Add local printer:

// Stage == 1
// local_icon        Add a Local Printer
//                   ------------------------------
//                   Printer Name: ________________
//                   Printer Type: pick one
//                   Connected to: pick one
// ------------------------------------------------
//                                Cancel        Add

// This seems to be hard coded into the preferences dialog:
// Don't show Network transport add-on in Printer Type menu.
// If Printer Type == Preview disable Connect to popup menu.
// If Printer Type == Serial Port add a submenu to menu item
//    with names in /dev/ports (if empty remove item from menu)
// If Printer Type == Parallel Port add a submenu to menu item
//    with names in /dev/parallel (if empty remove item from menu)

// Printer Driver Setup

// Dialog Info
// Would you like to make X the default printer?
//                                        No Yes

// Add network printer:

// Dialog Info
// Apple Talk networking isn't currenty enabled. If you
// wish to install a network printer you should enable
// AppleTalk in the Network preferences.
//                               Cancel   Open Network

// Stage == 2

// network_icon      Add a Network Printer
//                   ------------------------------
//                   Printer Name: ________________
//                   Printer Type: pick one
//              AppleTalk Printer: pick one
// ------------------------------------------------
//                                Cancel        Add
}


/** @brief Search order for printer driver and transport add-on
    directories: user-private first, then system. */
static directory_which gAddonDirs[] = {
	B_USER_NONPACKAGED_ADDONS_DIRECTORY,
	B_USER_ADDONS_DIRECTORY,
	B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY,
	B_SYSTEM_ADDONS_DIRECTORY,
};


/**
 * @brief Populates @a menu with executable add-ons found under @a path.
 *
 * Walks every directory in gAddonDirs, descending into the @a path
 * subdirectory of each, and adds one BMenuItem per executable file.
 * Each item carries a BMessage of @a what with a @c name string.
 *
 * @param menu Target menu to populate.
 * @param path Subdirectory under each add-on directory (e.g. "Print").
 * @param what BMessage @c what code to attach to each generated item.
 */
void
AddPrinterDialog::_FillMenu(BMenu* menu, const char* path, uint32 what)
{
	for (uint32 i = 0; i < sizeof(gAddonDirs) / sizeof(directory_which); i++) {
		BPath addonPath;
		if (find_directory(gAddonDirs[i], &addonPath) != B_OK)
			continue;

		if (addonPath.Append(path) != B_OK)
			continue;

		BDirectory dir(addonPath.Path());
		if (dir.InitCheck() != B_OK)
			continue;

		BEntry entry;
		while (dir.GetNextEntry(&entry, true) == B_OK) {
			if (!entry.IsFile())
				continue;

			BNode node(&entry);
			if (node.InitCheck() != B_OK)
				continue;

			BNodeInfo info(&node);
			if (info.InitCheck() != B_OK)
				continue;

			char type[B_MIME_TYPE_LENGTH + 1];
			info.GetType(type);
			BMimeType entryType(type);
			// filter non executable entries (like "transport" subfolder...)
			if (entryType == B_APP_MIME_TYPE) {
				BPath transportPath;
				if (entry.GetPath(&transportPath) != B_OK)
					continue;

				BMessage* msg = new BMessage(what);
				msg->AddString("name", transportPath.Leaf());
				menu->AddItem(new BMenuItem(transportPath.Leaf(), msg));
			}
		}
	}
}


/**
 * @brief Populates @a menu with transports queried from print_server.
 *
 * Iterates every Transport via scripting calls. For transports that expose
 * a list of ports a TransportMenu submenu is created; for transports that
 * do not (or return no ports) a plain BMenuItem is added.
 *
 * @param menu Target menu to populate.
 */
void
AddPrinterDialog::_FillTransportMenu(BMenu* menu)
{
	BMessenger msgr;
	if (GetPrinterServerMessenger(msgr) != B_OK)
		return;

	for (long idx = 0; ; idx++) {
		BMessage reply, msg(B_GET_PROPERTY);
		msg.AddSpecifier("Transport", idx);
		if (msgr.SendMessage(&msg, &reply) != B_OK)
			break;

		BMessenger transport;
		if (reply.FindMessenger("result", &transport) != B_OK)
			break;

		// Got messenger to transport now
		msg.MakeEmpty();
		msg.what = B_GET_PROPERTY;
		msg.AddSpecifier("Name");
		if (transport.SendMessage(&msg, &reply) != B_OK)
			continue;

		BString transportName;
		if (reply.FindString("result", &transportName) != B_OK)
			continue;

		// Now get ports...
		BString portId, portName;
		int32 error;
		msg.MakeEmpty();
		msg.what = B_GET_PROPERTY;
		msg.AddSpecifier("Ports");
		if (transport.SendMessage(&msg, &reply) != B_OK
				|| reply.FindInt32("error", &error) != B_OK
				|| error != B_OK
				|| (transportName == "IPP"
						&& reply.FindString("port_id", &portId) != B_OK)) {
			// Transport does not provide list of ports
			BMessage* menuMsg = new BMessage(kTransportSelectedMsg);
			menuMsg->AddString("name", transportName);
			menu->AddItem(new BMenuItem(transportName.String(), menuMsg));
			continue;
		}

		// Create submenu
		BMenu* transportMenu = new TransportMenu(transportName.String(),
			kTransportSelectedMsg, transport, transportName);
		menu->AddItem(transportMenu);
		transportMenu->SetRadioMode(true);
		menu->ItemAt(menu->IndexOf(transportMenu))->
			SetMessage(new BMessage(kTransportSelectedMsg));
	}
}


/**
 * @brief Refreshes button and menu enable states from current selections.
 *
 * The OK button activates only when a name, driver, and transport are all
 * present (the Preview driver does not need a transport). The transport
 * menu is disabled while Preview is selected.
 */
void
AddPrinterDialog::_Update()
{
	fOk->SetEnabled(fNameText != "" && fPrinterText != ""
		&& (fTransportText != "" || fPrinterText == "Preview"));

	fTransport->SetEnabled(fPrinterText != "Preview");
}
