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
 *   Copyright 2011-2012, Haiku Inc. All Rights Reserved.
 *   Copyright 2001 Dr. Zoidberg Enterprises. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ProtocolConfigView.cpp
 * @brief Standard configuration views for inbound and outbound mail protocol add-ons.
 *
 * Provides BodyDownloadConfigView (a checkbox + size field for partial-download
 * limits) and MailProtocolConfigView (the full protocol settings panel with
 * server hostname, username, password, connection type, authentication method,
 * leave-on-server, and partial-download controls). Protocol add-ons use these
 * views in their instantiate_config_panel() entry point.
 *
 * @see BMailSettingsView, BMailAddOnSettings, BMailProtocolSettings
 */


#include "ProtocolConfigView.h"

#include <stdio.h>
#include <stdlib.h>

#include <Catalog.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <GridLayout.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <PopUpMenu.h>
#include <String.h>
#include <TextControl.h>

#include <crypt.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ProtocolConfigView"


/** @brief Settings key for the partial-download size limit in bytes. */
static const char* kPartialDownloadLimit = "partial_download_limit";

/** @brief Message sent when the "Leave on server" checkbox changes state. */
static const uint32 kMsgLeaveOnServer = 'lmos';

/** @brief Message sent by an authentication menu item that requires no password. */
static const uint32 kMsgNoPassword = 'none';

/** @brief Message sent by an authentication menu item that requires a password. */
static const uint32 kMsgNeedPassword = 'some';


namespace BPrivate {


/**
 * @brief Constructs the body download configuration sub-view.
 *
 * Creates a horizontal layout containing a "Partially download messages
 * larger than" checkbox and a KB text-entry field.
 */
BodyDownloadConfigView::BodyDownloadConfigView()
	:
	BView("body_config", 0)
{
	fPartialBox = new BCheckBox("size_if", B_TRANSLATE(
		"Partially download messages larger than"), new BMessage('SIZF'));

	fSizeControl = new BTextControl("size",
		B_TRANSLATE_COMMENT("KB", "kilo byte"), "", NULL);
	fSizeControl->SetExplicitMinSize(BSize(be_plain_font->StringWidth("0000"),
		B_SIZE_UNSET));

	BLayoutBuilder::Group<>(this, B_HORIZONTAL,
			be_control_look->DefaultLabelSpacing())
		.Add(fPartialBox)
		.Add(fSizeControl->CreateTextViewLayoutItem())
		.Add(fSizeControl->CreateLabelLayoutItem());
}


/**
 * @brief Populates the partial-download controls from the given protocol settings.
 *
 * If the limit is negative (not set), the checkbox is unchecked and the size
 * field is disabled. Otherwise the current limit is converted to KB and displayed.
 *
 * @param settings  Protocol settings to read the partial_download_limit from.
 */
void
BodyDownloadConfigView::SetTo(const BMailProtocolSettings& settings)
{
	int32 limit = settings.GetInt32(kPartialDownloadLimit, -1);
	if (limit < 0) {
		fPartialBox->SetValue(B_CONTROL_OFF);
		fSizeControl->SetText("0");
		fSizeControl->SetEnabled(false);
	} else {
		BString kb;
		kb << int32(limit / 1024);
		fSizeControl->SetText(kb);
		fPartialBox->SetValue(B_CONTROL_ON);
		fSizeControl->SetEnabled(true);
	}
}


/**
 * @brief Saves the partial-download limit into the add-on settings.
 *
 * If the checkbox is checked, stores the KB value multiplied by 1024.
 * Otherwise removes the key entirely so the default (no limit) applies.
 *
 * @param settings  BMailAddOnSettings to update.
 * @return B_OK always.
 */
status_t
BodyDownloadConfigView::SaveInto(BMailAddOnSettings& settings) const
{
	if (fPartialBox->Value() == B_CONTROL_ON) {
		settings.SetInt32(kPartialDownloadLimit,
			atoi(fSizeControl->Text()) * 1024);
	} else
		settings.RemoveName(kPartialDownloadLimit);

	return B_OK;
}


/**
 * @brief Enables or disables the size field when the partial-download checkbox changes.
 *
 * @param msg  Incoming BMessage (only 'SIZF' is handled; others are forwarded).
 */
void
BodyDownloadConfigView::MessageReceived(BMessage *msg)
{
	if (msg->what != 'SIZF')
		return BView::MessageReceived(msg);
	fSizeControl->SetEnabled(fPartialBox->Value());
}


/**
 * @brief Sets the checkbox target to this view after attachment to a window.
 */
void
BodyDownloadConfigView::AttachedToWindow()
{
	fPartialBox->SetTarget(this);
	fPartialBox->ResizeToPreferred();
}


// #pragma mark -


/**
 * @brief Constructs the full protocol configuration view.
 *
 * Builds a BGridLayout containing any combination of hostname, username,
 * password, connection-type, authentication-method, leave-on-server, and
 * partial-download controls as selected by \a optionsMask.
 *
 * @param optionsMask  Bitmask of B_MAIL_PROTOCOL_HAS_* constants controlling
 *                     which controls are created.
 */
MailProtocolConfigView::MailProtocolConfigView(uint32 optionsMask)
	:
	BMailSettingsView("protocol_config_view"),
	fHostControl(NULL),
	fUserControl(NULL),
	fPasswordControl(NULL),
	fFlavorField(NULL),
	fAuthenticationField(NULL),
	fLeaveOnServerCheckBox(NULL),
	fRemoveFromServerCheckBox(NULL),
	fBodyDownloadConfig(NULL)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	BGridLayout* layout = new BGridLayout(0.f);
	SetLayout(layout);

	if ((optionsMask & B_MAIL_PROTOCOL_HAS_HOSTNAME) != 0) {
		fHostControl = _AddTextControl(layout, "host",
			B_TRANSLATE("Mail server:"));
	}
	if ((optionsMask & B_MAIL_PROTOCOL_HAS_USERNAME) != 0) {
		fUserControl = _AddTextControl(layout, "user",
			B_TRANSLATE("Username:"));
	}

	if ((optionsMask & B_MAIL_PROTOCOL_HAS_PASSWORD) != 0) {
		fPasswordControl = _AddTextControl(layout, "pass",
			B_TRANSLATE("Password:"));
		fPasswordControl->TextView()->HideTyping(true);
	}

	if ((optionsMask & B_MAIL_PROTOCOL_HAS_FLAVORS) != 0) {
		fFlavorField = _AddMenuField(layout, "flavor",
			B_TRANSLATE("Connection type:"));
	}

	if ((optionsMask & B_MAIL_PROTOCOL_HAS_AUTH_METHODS) != 0) {
		fAuthenticationField = _AddMenuField(layout, "auth_method",
			B_TRANSLATE("Login type:"));
	}

	if ((optionsMask & B_MAIL_PROTOCOL_CAN_LEAVE_MAIL_ON_SERVER) != 0) {
		fLeaveOnServerCheckBox = new BCheckBox("leave_mail_on_server",
			B_TRANSLATE("Leave mail on server"),
			new BMessage(kMsgLeaveOnServer));
		layout->AddView(fLeaveOnServerCheckBox, 0, layout->CountRows(), 2);

		fRemoveFromServerCheckBox = new BCheckBox("delete_remote_when_local",
			B_TRANSLATE("Remove mail from server when deleted"), NULL);
		fRemoveFromServerCheckBox->SetEnabled(false);
		layout->AddView(fRemoveFromServerCheckBox, 0, layout->CountRows(), 2);
	}

	if ((optionsMask & B_MAIL_PROTOCOL_PARTIAL_DOWNLOAD) != 0) {
		fBodyDownloadConfig = new BodyDownloadConfigView();
		layout->AddView(fBodyDownloadConfig, 0, layout->CountRows(), 2);
	}
}


/**
 * @brief Destroys the MailProtocolConfigView.
 */
MailProtocolConfigView::~MailProtocolConfigView()
{
}


/**
 * @brief Populates all controls from the given protocol settings.
 *
 * Fills the server, username, password (decrypting the stored cpasswd),
 * flavor, auth-method, and leave-on-server controls from the BMessage fields
 * in \a settings.
 *
 * @param settings  Protocol settings to read from.
 */
void
MailProtocolConfigView::SetTo(const BMailProtocolSettings& settings)
{
	BString host = settings.FindString("server");
	if (settings.HasInt32("port"))
		host << ':' << settings.FindInt32("port");

	if (fHostControl != NULL)
		fHostControl->SetText(host.String());
	if (fUserControl != NULL)
		fUserControl->SetText(settings.FindString("username"));

	if (fPasswordControl != NULL) {
		char* password = get_passwd(&settings, "cpasswd");
		if (password != NULL) {
			fPasswordControl->SetText(password);
			delete[] password;
		} else
			fPasswordControl->SetText(settings.FindString("password"));
	}

	if (settings.HasInt32("flavor") && fFlavorField != NULL) {
		if (BMenuItem* item = fFlavorField->Menu()->ItemAt(
				settings.FindInt32("flavor")))
			item->SetMarked(true);
	}

	if (settings.HasInt32("auth_method") && fAuthenticationField != NULL) {
		if (BMenuItem* item = fAuthenticationField->Menu()->ItemAt(
				settings.FindInt32("auth_method"))) {
			item->SetMarked(true);
			_SetCredentialsEnabled(item->Command() != kMsgNoPassword);
		}
	}

	if (fLeaveOnServerCheckBox != NULL) {
		fLeaveOnServerCheckBox->SetValue(settings.FindBool(
			"leave_mail_on_server") ? B_CONTROL_ON : B_CONTROL_OFF);
	}

	if (fRemoveFromServerCheckBox != NULL) {
		fRemoveFromServerCheckBox->SetValue(settings.FindBool(
			"delete_remote_when_local") ? B_CONTROL_ON : B_CONTROL_OFF);
		fRemoveFromServerCheckBox->SetEnabled(
			settings.FindBool("leave_mail_on_server"));
	}

	if (fBodyDownloadConfig != NULL)
		fBodyDownloadConfig->SetTo(settings);
}


/**
 * @brief Adds a labeled connection-type menu item.
 *
 * Call once per supported connection flavor (e.g. "Plain", "SSL"). The first
 * added item is automatically selected.
 *
 * @param label  Human-readable connection type string.
 */
void
MailProtocolConfigView::AddFlavor(const char* label)
{
	if (fFlavorField != NULL) {
		fFlavorField->Menu()->AddItem(new BMenuItem(label, NULL));

		if (fFlavorField->Menu()->FindMarked() == NULL)
			fFlavorField->Menu()->ItemAt(0)->SetMarked(true);
	}
}


/**
 * @brief Adds a labeled authentication-method menu item.
 *
 * @param label              Human-readable auth method name.
 * @param needUserPassword   If true, the item enables the username/password
 *                           fields; if false, it disables them.
 */
void
MailProtocolConfigView::AddAuthMethod(const char* label, bool needUserPassword)
{
	if (fAuthenticationField != NULL) {
		fAuthenticationField->Menu()->AddItem(new BMenuItem(label,
			new BMessage(needUserPassword
				? kMsgNeedPassword : kMsgNoPassword)));

		if (fAuthenticationField->Menu()->FindMarked() == NULL) {
			BMenuItem* item = fAuthenticationField->Menu()->ItemAt(0);
			item->SetMarked(true);
			MessageReceived(item->Message());
		}
	}
}


/**
 * @brief Returns the underlying BGridLayout for adding custom controls.
 *
 * @return Pointer to the BGridLayout managing this view's children.
 */
BGridLayout*
MailProtocolConfigView::Layout() const
{
	return (BGridLayout*)BView::GetLayout();
}


/**
 * @brief Saves all control values into the given add-on settings.
 *
 * Splits the hostname:port string, stores username and encrypted password,
 * records menu selection indices, saves checkbox states, and delegates to
 * fBodyDownloadConfig if present.
 *
 * @param settings  BMailAddOnSettings to receive the updated values.
 * @return B_OK on success.
 */
status_t
MailProtocolConfigView::SaveInto(BMailAddOnSettings& settings) const
{
	if (fHostControl != NULL) {
		int32 port = -1;
		BString hostName = fHostControl->Text();
		if (hostName.FindFirst(':') > -1) {
			port = atol(hostName.String() + hostName.FindFirst(':') + 1);
			hostName.Truncate(hostName.FindFirst(':'));
		}

		settings.SetString("server", hostName);

		// since there is no need for the port option, remove it here
		if (port != -1)
			settings.SetInt32("port", port);
		else
			settings.RemoveName("port");
	} else {
		settings.RemoveName("server");
		settings.RemoveName("port");
	}

	if (fUserControl != NULL)
		settings.SetString("username", fUserControl->Text());
	else
		settings.RemoveName("username");

	// remove old unencrypted passwords
	settings.RemoveName("password");

	if (fPasswordControl != NULL)
		set_passwd(&settings, "cpasswd", fPasswordControl->Text());
	else
		settings.RemoveName("cpasswd");

	_StoreIndexOfMarked(settings, "flavor", fFlavorField);
	_StoreIndexOfMarked(settings, "auth_method", fAuthenticationField);

	_StoreCheckBox(settings, "leave_mail_on_server", fLeaveOnServerCheckBox);
	_StoreCheckBox(settings, "delete_remote_when_local",
		fRemoveFromServerCheckBox);

	if (fBodyDownloadConfig != NULL)
		return fBodyDownloadConfig->SaveInto(settings);

	return B_OK;
}


/**
 * @brief Wires authentication menu items and the leave-on-server checkbox targets.
 */
void
MailProtocolConfigView::AttachedToWindow()
{
	if (fAuthenticationField != NULL)
		fAuthenticationField->Menu()->SetTargetForItems(this);

	if (fLeaveOnServerCheckBox != NULL)
		fLeaveOnServerCheckBox->SetTarget(this);
}


/**
 * @brief Handles authentication and leave-on-server control changes.
 *
 * Enables or disables credential fields based on the selected auth method,
 * and enables the "Remove from server" checkbox when "Leave on server" is
 * checked.
 *
 * @param message  Incoming BMessage with what code kMsgNeedPassword,
 *                 kMsgNoPassword, or kMsgLeaveOnServer.
 */
void
MailProtocolConfigView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgNeedPassword:
			_SetCredentialsEnabled(true);
			break;
		case kMsgNoPassword:
			_SetCredentialsEnabled(false);
			break;

		case kMsgLeaveOnServer:
			fRemoveFromServerCheckBox->SetEnabled(
				message->FindInt32("be:value") == B_CONTROL_ON);
			break;
	}
}


/**
 * @brief Creates and adds a labeled BTextControl to the grid layout.
 *
 * @param layout  Grid layout to append to.
 * @param name    BView name for the control.
 * @param label   Human-readable label displayed to the left.
 * @return Pointer to the created BTextControl.
 */
BTextControl*
MailProtocolConfigView::_AddTextControl(BGridLayout* layout, const char* name,
	const char* label)
{
	BTextControl* control = new BTextControl(name, label, "", NULL);
	control->SetAlignment(B_ALIGN_RIGHT, B_ALIGN_LEFT);

	int32 row = layout->CountRows();
	layout->AddItem(control->CreateLabelLayoutItem(), 0, row);
	layout->AddItem(control->CreateTextViewLayoutItem(), 1, row);
	return control;
}


/**
 * @brief Creates and adds a labeled BMenuField (pop-up menu) to the grid layout.
 *
 * @param layout  Grid layout to append to.
 * @param name    BView name for the menu field.
 * @param label   Human-readable label displayed to the left.
 * @return Pointer to the created BMenuField.
 */
BMenuField*
MailProtocolConfigView::_AddMenuField(BGridLayout* layout, const char* name,
	const char* label)
{
	BPopUpMenu* menu = new BPopUpMenu("");
	BMenuField* field = new BMenuField(name, label, menu);
	field->SetAlignment(B_ALIGN_RIGHT);

	int32 row = layout->CountRows();
	layout->AddItem(field->CreateLabelLayoutItem(), 0, row);
	layout->AddItem(field->CreateMenuBarLayoutItem(), 1, row);
	return field;
}


/**
 * @brief Stores the index of the marked menu item into a BMessage field.
 *
 * @param message  BMessage to update.
 * @param name     Field name to store the index under.
 * @param field    Menu field to query; may be NULL (stores -1).
 */
void
MailProtocolConfigView::_StoreIndexOfMarked(BMessage& message, const char* name,
	BMenuField* field) const
{
	int32 index = -1;
	if (field != NULL && field->Menu() != NULL) {
		BMenuItem* item = field->Menu()->FindMarked();
		if (item != NULL)
			index = field->Menu()->IndexOf(item);
	}
	message.SetInt32(name, index);
}


/**
 * @brief Stores a boolean from a BCheckBox into a BMessage field.
 *
 * Removes the field if the checkbox is unchecked; sets it to true if checked.
 *
 * @param message   BMessage to update.
 * @param name      Field name to store the value under.
 * @param checkBox  Source checkbox; may be NULL (treated as unchecked).
 */
void
MailProtocolConfigView::_StoreCheckBox(BMessage& message, const char* name,
	BCheckBox* checkBox) const
{
	bool value = checkBox != NULL && checkBox->Value() == B_CONTROL_ON;
	if (value)
		message.SetBool(name, value);
	else
		message.RemoveName(name);
}


/**
 * @brief Enables or disables the username and password controls together.
 *
 * @param enabled  true to enable both controls.
 */
void
MailProtocolConfigView::_SetCredentialsEnabled(bool enabled)
{
	if (fUserControl != NULL && fPasswordControl != NULL) {
		fUserControl->SetEnabled(enabled);
		fPasswordControl->SetEnabled(enabled);
	}
}


}	// namespace BPrivate
