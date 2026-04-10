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
 *   Copyright 2009-2021, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Weirauch, dev@m-phasis.de
 */

/** @file DeskbarReplicant.cpp
 *  @brief Implementation of the Bluetooth Deskbar tray replicant. */


#include "DeskbarReplicant.h"

#include <Alert.h>
#include <Application.h>
#include <Bitmap.h>
#include <Catalog.h>
#include <Deskbar.h>
#include <IconUtils.h>
#include <MenuItem.h>
#include <Message.h>
#include <PopUpMenu.h>
#include <Resources.h>
#include <Roster.h>
#include <String.h>

#include <bluetoothserver_p.h>


extern "C" _EXPORT BView *instantiate_deskbar_item(float maxWidth, float maxHeight);
status_t our_image(image_info& image);

/** @brief Message code sent when the user chooses "Settings..." from the popup menu. */
const uint32 kMsgOpenBluetoothPreferences = 'obtp';

/** @brief Message code sent when the user chooses "Quit" from the popup menu. */
const uint32 kMsgQuitBluetoothServer = 'qbts';

/** @brief The replicant's shelf name inside the Deskbar tray. */
const char* kDeskbarItemName = "BluetoothServerReplicant";

/** @brief Class name used for instantiation validation. */
const char* kClassName = "DeskbarReplicant";


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BluetoothReplicant"


//	#pragma mark -


/**
 * @brief Construct the Deskbar replicant view with an explicit frame.
 *
 * Used when the Deskbar instantiates the item via instantiate_deskbar_item().
 *
 * @param frame        The bounding rectangle for the replicant view.
 * @param resizingMode The BView resizing mode flags.
 */
DeskbarReplicant::DeskbarReplicant(BRect frame, int32 resizingMode)
	: BView(frame, kDeskbarItemName, resizingMode,
		B_WILL_DRAW | B_TRANSPARENT_BACKGROUND | B_FRAME_EVENTS)
{
	_Init();
}


/**
 * @brief Reconstruct the replicant from an archived BMessage.
 *
 * @param archive The BMessage produced by Archive().
 */
DeskbarReplicant::DeskbarReplicant(BMessage* archive)
	: BView(archive)
{
	_Init();
}


/** @brief Destroy the Deskbar replicant. */
DeskbarReplicant::~DeskbarReplicant()
{
}


/**
 * @brief Load the Bluetooth tray icon from the executable's resources.
 *
 * Attempts to locate the vector icon resource named "tray_icon" in the
 * running image, rasterize it at the view's current bounds, and store
 * the result in fIcon.  If any step fails, fIcon remains NULL and the
 * Draw() method falls back to a solid colour rectangle.
 */
void
DeskbarReplicant::_Init()
{
	fIcon = NULL;

	image_info info;
	if (our_image(info) != B_OK)
		return;

	BFile file(info.name, B_READ_ONLY);
	if (file.InitCheck() < B_OK)
		return;

	BResources resources(&file);
	if (resources.InitCheck() < B_OK)
		return;

	size_t size;
	const void* data = resources.LoadResource(B_VECTOR_ICON_TYPE,
		"tray_icon", &size);
	if (data != NULL) {
		BBitmap* icon = new BBitmap(Bounds(), B_RGBA32);
		if (icon->InitCheck() == B_OK
			&& BIconUtils::GetVectorIcon((const uint8 *)data,
				size, icon) == B_OK) {
			fIcon = icon;
		} else
			delete icon;
	}
}


/**
 * @brief BArchivable hook: instantiate a DeskbarReplicant from an archive.
 *
 * Validates the archive class name before constructing the object.
 *
 * @param archive The archive message to instantiate from.
 * @return A new DeskbarReplicant on success, or NULL if validation fails.
 */
DeskbarReplicant *
DeskbarReplicant::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, kClassName))
		return NULL;

	return new DeskbarReplicant(archive);
}


/**
 * @brief Archive the replicant into a BMessage for Deskbar shelf storage.
 *
 * Stores the Bluetooth server add-on signature and class name so that the
 * Deskbar can reconstruct the replicant after a restart.
 *
 * @param archive The message to archive into.
 * @param deep    If true, child views are also archived.
 * @return B_OK on success, or an error code on failure.
 */
status_t
DeskbarReplicant::Archive(BMessage* archive, bool deep) const
{
	status_t status = BView::Archive(archive, deep);
	if (status == B_OK)
		status = archive->AddString("add_on", BLUETOOTH_SIGNATURE);
	if (status == B_OK)
		status = archive->AddString("class", kClassName);

	return status;
}


/**
 * @brief Hook called when the replicant is attached to the Deskbar window.
 *
 * Adopts the parent's background colour so the icon blends with the tray.
 */
void
DeskbarReplicant::AttachedToWindow()
{
	BView::AttachedToWindow();
	AdoptParentColors();

	if (ViewUIColor() == B_NO_COLOR)
		SetLowColor(ViewColor());
	else
		SetLowUIColor(ViewUIColor());
}


/**
 * @brief Draw the Bluetooth tray icon, or a coloured fallback rectangle.
 *
 * If fIcon was successfully loaded, it is drawn with alpha blending.
 * Otherwise a solid blue rounded rectangle is rendered as a placeholder.
 *
 * @param updateRect The portion of the view that needs redrawing.
 */
void
DeskbarReplicant::Draw(BRect updateRect)
{
	if (!fIcon) {
		/* At least display something... */
		rgb_color lowColor = LowColor();
		SetLowColor(0, 113, 187, 255);
		FillRoundRect(Bounds().InsetBySelf(3.f, 0.f), 5.f, 7.f, B_SOLID_LOW);
		SetLowColor(lowColor);
	} else {
		SetDrawingMode(B_OP_ALPHA);
		DrawBitmap(fIcon);
		SetDrawingMode(B_OP_COPY);
	}
}


/**
 * @brief Handle messages from the popup menu and other sources.
 *
 * Responds to kMsgOpenBluetoothPreferences by launching the Bluetooth
 * preferences application, and to kMsgQuitBluetoothServer by requesting
 * a server shutdown.
 *
 * @param msg The incoming BMessage.
 */
void
DeskbarReplicant::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgOpenBluetoothPreferences:
			be_roster->Launch(BLUETOOTH_APP_SIGNATURE);
			break;

		case kMsgQuitBluetoothServer:
			_QuitBluetoothServer();
			break;

		default:
			BView::MessageReceived(msg);
	}
}


/**
 * @brief Show the context popup menu on a secondary (right) mouse click.
 *
 * Builds a BPopUpMenu with "Settings..." and "Quit" items, displays it
 * at the click location, and dispatches the chosen item as a message back
 * to this view.
 *
 * @param where The point in view coordinates where the click occurred.
 */
void
DeskbarReplicant::MouseDown(BPoint where)
{
	BPoint point;
	uint32 buttons;
	GetMouse(&point, &buttons);
	if (!(buttons & B_SECONDARY_MOUSE_BUTTON)) {
		return;
	}

	BPopUpMenu* menu = new BPopUpMenu(B_EMPTY_STRING, false, false);

	menu->AddItem(new BMenuItem(B_TRANSLATE("Settings" B_UTF8_ELLIPSIS),
		new BMessage(kMsgOpenBluetoothPreferences)));

	// TODO show list of known/paired devices

	menu->AddItem(new BMenuItem(B_TRANSLATE("Quit"),
		new BMessage(kMsgQuitBluetoothServer)));

	menu->SetTargetForItems(this);
	ConvertToScreen(&point);
	menu->Go(point, true, true, true);

	delete menu;
}


/**
 * @brief Request the Bluetooth server to shut down.
 *
 * If the server is not running, removes the Deskbar replicant directly.
 * Otherwise sends B_QUIT_REQUESTED to the server; on failure an error
 * alert is shown.
 */
void
DeskbarReplicant::_QuitBluetoothServer()
{
	if (!be_roster->IsRunning(BLUETOOTH_SIGNATURE)) {
		// The server isn't running, so remove ourself
		BDeskbar deskbar;
		deskbar.RemoveItem(kDeskbarItemName);

		return;
	}
	status_t status = BMessenger(BLUETOOTH_SIGNATURE).SendMessage(
		B_QUIT_REQUESTED);
	if (status < B_OK) {
		_ShowErrorAlert(B_TRANSLATE("Stopping the Bluetooth server failed."),
			status);
	}
}


/**
 * @brief Display a modal alert dialog with an error message.
 *
 * Appends a human-readable representation of \a status to \a msg and
 * shows it in a BAlert.
 *
 * @param msg    A user-facing description of what went wrong.
 * @param status The status_t error code to include in the alert body.
 */
void
DeskbarReplicant::_ShowErrorAlert(BString msg, status_t status)
{
	BString error = B_TRANSLATE("Error: %status%");
	error.ReplaceFirst("%status%", strerror(status));
	msg << "\n\n" << error;
	BAlert* alert = new BAlert(B_TRANSLATE("Bluetooth error"), msg.String(),
		B_TRANSLATE("OK"));
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go(NULL);
}


//	#pragma mark -


/**
 * @brief Deskbar shelf entry point: create a DeskbarReplicant that fits the tray.
 *
 * Called by the Deskbar when the replicant add-on is loaded.  Returns a
 * square view sized to \a maxHeight.
 *
 * @param maxWidth  Maximum width available in the tray (unused).
 * @param maxHeight Maximum height available in the tray; used to size the view.
 * @return A new DeskbarReplicant view.
 */
extern "C" _EXPORT BView *
instantiate_deskbar_item(float maxWidth, float maxHeight)
{
	return new DeskbarReplicant(BRect(0, 0, maxHeight - 1, maxHeight - 1),
		B_FOLLOW_NONE);
}


//	#pragma mark -


/**
 * @brief Locate the image_info for the shared object that contains this function.
 *
 * Iterates the loaded images of the current team to find the one whose
 * text segment includes the address of this function, providing access to
 * the executable's on-disk path and embedded resources.
 *
 * @param image Output parameter that receives the matching image_info.
 * @return B_OK if found, B_ERROR otherwise.
 */
status_t
our_image(image_info& image)
{
	int32 cookie = 0;
	while (get_next_image_info(B_CURRENT_TEAM, &cookie, &image) == B_OK) {
		if ((char *)our_image >= (char *)image.text
			&& (char *)our_image <= (char *)image.text + image.text_size)
			return B_OK;
	}

	return B_ERROR;
}
