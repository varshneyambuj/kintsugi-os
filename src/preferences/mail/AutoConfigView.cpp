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
 *   Copyright 2007-2015, Haiku, Inc. All rights reserved.
 *   Copyright 2011, Clemens Zeidler <haiku@clemens-zeidler.de>
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file AutoConfigView.cpp
 * @brief Implements the two wizard pages used by the new-account flow:
 *        AutoConfigView for the identity form and ServerSettingsView for
 *        per-protocol server review.
 */


#include "AutoConfigView.h"

#include <pwd.h>

#include <Catalog.h>
#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <String.h>
#include <Window.h>

#include <MailSettings.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "E-Mail"


/**
 * @brief Constructs the identity-collection page and lays out its text
 *        controls in a two-column grid.
 *
 * Pre-fills the real-name field from the local @c passwd entry so that
 * users with a populated GECOS field do not have to retype it.
 *
 * @param config  Reference to the wizard-owned AutoConfig instance, kept
 *                so the view can re-use its cached database lookups.
 */
AutoConfigView::AutoConfigView(AutoConfig &config)
	:
	BBox("auto config"),
	fAutoConfig(config)
{
	// Search for SMTP entry_ref
	_GetSMTPAddOnRef(&fSMTPAddOnRef);

	fInProtocolsField = new BMenuField(NULL, NULL, _SetupProtocolMenu());

	fEmailView = new BTextControl("email", B_TRANSLATE("E-mail address:"),
		"", new BMessage(kEMailChangedMsg));

	fLoginNameView = new BTextControl("login", B_TRANSLATE("Login name:"),
		"", NULL);

	fPasswordView = new BTextControl("password", B_TRANSLATE("Password:"),
		"", NULL);
	fPasswordView->TextView()->HideTyping(true);

	fAccountNameView = new BTextControl("account", B_TRANSLATE("Account name:"),
		"", NULL);

	fNameView = new BTextControl("name", B_TRANSLATE("Real name:"), "", NULL);

	struct passwd* passwd = getpwent();
	if (passwd != NULL)
		fNameView->SetText(passwd->pw_gecos);

	AddChild(BLayoutBuilder::Grid<>()
		.SetInsets(B_USE_DEFAULT_SPACING)
		.SetSpacing(B_USE_HALF_ITEM_SPACING, B_USE_HALF_ITEM_SPACING)

		.Add(fInProtocolsField->CreateLabelLayoutItem(), 0, 0)
		.Add(fInProtocolsField->CreateMenuBarLayoutItem(), 1, 0)

		.Add(fEmailView->CreateLabelLayoutItem(), 0, 1)
		.Add(fEmailView->CreateTextViewLayoutItem(), 1, 1)

		.Add(fLoginNameView->CreateLabelLayoutItem(), 0, 2)
		.Add(fLoginNameView->CreateTextViewLayoutItem(), 1, 2)

		.Add(fPasswordView->CreateLabelLayoutItem(), 0, 3)
		.Add(fPasswordView->CreateTextViewLayoutItem(), 1, 3)

		.Add(fAccountNameView->CreateLabelLayoutItem(), 0, 4)
		.Add(fAccountNameView->CreateTextViewLayoutItem(), 1, 4)

		.Add(fNameView->CreateLabelLayoutItem(), 0, 5)
		.Add(fNameView->CreateTextViewLayoutItem(), 1, 5)
		.View());
}


/**
 * @brief Hooks the view into its window: tightens its height to fit, adopts
 *        parent colors, retargets the e-mail field to this view, and gives
 *        it focus.
 */
void
AutoConfigView::AttachedToWindow()
{
	// Resize the view to fit the contents properly
	BRect rect = Bounds();
	float newHeight = fNameView->Frame().bottom + 20 + 2;
	newHeight += InnerFrame().top;
	ResizeTo(rect.Width(), newHeight);

	AdoptParentColors();
	fEmailView->SetTarget(this);
	fEmailView->MakeFocus(true);
}


/**
 * @brief Handles control-change notifications from the e-mail field.
 *
 * On every edit it proposes a login name (if the user has not entered one)
 * and copies the e-mail into the account-name slot when that field is
 * still empty.
 *
 * @param msg  Incoming BMessage; the only one handled is
 *             @c kEMailChangedMsg.
 */
void
AutoConfigView::MessageReceived(BMessage *msg)
{
	switch (msg->what) {
		case kEMailChangedMsg:
		{
			BString text = fLoginNameView->Text();
			if (text == "")
				_ProposeUsername();
			fLoginNameView->MakeFocus();
			fLoginNameView->TextView()->SelectAll();

			text = fAccountNameView->Text();
			if (text == "")
				fAccountNameView->SetText(fEmailView->Text());
			break;
		}
		default:
			BView::MessageReceived(msg);
			break;
	}
}


/**
 * @brief Gathers the user-entered fields into @a info for the next wizard
 *        step.
 *
 * Determines whether the user picked POP or IMAP based on the protocol
 * menu label and copies the SMTP add-on ref located at construction time.
 *
 * @param info  Destination account_info; filled even when no protocol is
 *              selected.
 * @return @c true if a marked inbound protocol was found, @c false if the
 *         menu was empty (caller should report an error).
 */
bool
AutoConfigView::GetBasicAccountInfo(account_info &info)
{
	status_t status = B_OK;

	BString inboundProtocolName = "";
	BMenuItem* item = fInProtocolsField->Menu()->FindMarked();
	if (item) {
		inboundProtocolName = item->Label();
		item->Message()->FindRef("protocol", &(info.inboundProtocol));
	}
	else
		status = B_ERROR;

	if (inboundProtocolName.FindFirst("IMAP") >= 0)
		info.inboundType = IMAP;
	else
		info.inboundType = POP;

	info.outboundProtocol = fSMTPAddOnRef;
	info.name = fNameView->Text();
	info.accountName = fAccountNameView->Text();
	info.email = fEmailView->Text();
	info.loginName = fLoginNameView->Text();
	info.password = fPasswordView->Text();

	return status;
}


/**
 * @brief Builds the inbound-protocol pop-up menu by scanning the user and
 *        system inbound add-on directories.
 *
 * If an "IMAP" entry is found it is marked as the default; otherwise the
 * last add-on encountered remains marked.
 *
 * @return Newly allocated BPopUpMenu owned by the caller (typically
 *         attached to a BMenuField).
 * @todo Switch to BPathFinder instead of hand-rolled directory traversal.
 */
BPopUpMenu*
AutoConfigView::_SetupProtocolMenu()
{
	BPopUpMenu* menu = new BPopUpMenu(B_TRANSLATE("Choose Protocol"));

	// TODO: use path finder!
	for (int i = 0; i < 2; i++) {
		BPath path;
		status_t status = find_directory((i == 0) ? B_USER_ADDONS_DIRECTORY :
			B_BEOS_ADDONS_DIRECTORY, &path);
		if (status != B_OK)
			return menu;

		path.Append("mail_daemon");
		path.Append("inbound_protocols");

		BDirectory dir(path.Path());
		entry_ref protocolRef;
		while (dir.GetNextRef(&protocolRef) == B_OK)
		{
			BEntry entry(&protocolRef);

			BMessage* msg = new BMessage(kProtokollChangedMsg);
			BMenuItem* item = new BMenuItem(entry.Name(), msg);
			menu->AddItem(item);
			msg->AddRef("protocol", &protocolRef);

			item->SetMarked(true);
		}
	}

	// make imap default protocol if existing
	BMenuItem* imapItem =  menu->FindItem("IMAP");
	if (imapItem)
		imapItem->SetMarked(true);

	return menu;
}


/**
 * @brief Locates the SMTP outbound-protocol add-on and stores a ref to it
 *        in @a ref.
 *
 * Prefers the user-specific add-ons directory; falls back to the system
 * add-ons directory.
 *
 * @param ref  Output entry_ref; only meaningful on @c B_OK.
 * @retval B_OK              SMTP add-on found in one of the searched
 *                           directories.
 * @retval B_ENTRY_NOT_FOUND No SMTP add-on installed.
 * @retval B_ERROR           Could not resolve the requested directory.
 */
status_t
AutoConfigView::_GetSMTPAddOnRef(entry_ref *ref)
{
	directory_which which[] = {
		B_USER_ADDONS_DIRECTORY,
		B_BEOS_ADDONS_DIRECTORY
	};

	for (size_t i = 0; i < sizeof(which) / sizeof(which[0]); i++) {
		BPath path;
		status_t status = find_directory(which[i], &path);
		if (status != B_OK)
			return B_ERROR;

		path.Append("mail_daemon");
		path.Append("outbound_protocols");
		path.Append("SMTP");

		BEntry entry(path.Path());
		if (entry.Exists() && entry.GetRef(ref) == B_OK)
			return B_OK;
	}

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Returns the local-part of an e-mail address (everything before the
 *        last @c '@').
 *
 * @param email  Full e-mail address; must contain at least one @c '@'.
 * @return BString holding the local-part. Behavior is undefined if @a email
 *         has no @c '@'.
 */
BString
AutoConfigView::_ExtractLocalPart(const char* email)
{
	const char* at = strrchr(email, '@');
	return BString(email, at - email);
}


/**
 * @brief Pre-fills the login-name field based on the provider's preferred
 *        username pattern.
 *
 * Falls back to using the full e-mail address when AutoConfig has no entry
 * for the domain.
 */
void
AutoConfigView::_ProposeUsername()
{
	const char* email = fEmailView->Text();
	provider_info info;
	status_t status = fAutoConfig.GetInfoFromMailAddress(email, &info);
	if (status == B_OK) {
		BString localPart = _ExtractLocalPart(email);
		switch (info.username_pattern) {
			case 0:
				// username is the mail address
				fLoginNameView->SetText(email);
				break;
			case 1:
				// username is the local-part
				fLoginNameView->SetText(localPart.String());
				break;
			case 2:
				// do nothing
				break;
		}
	}
	else {
		fLoginNameView->SetText(email);
	}
}


/**
 * @brief Performs a coarse syntactic check that @a email looks like an
 *        e-mail address.
 *
 * Requires both an @c '@' and at least one dot in the domain portion. This
 * is intentionally permissive; the daemon performs the real validation.
 *
 * @param email  Address to validate.
 * @return @c true if both required separators are present, @c false
 *         otherwise.
 */
bool
AutoConfigView::IsValidMailAddress(BString email)
{
	int32 atPos = email.FindFirst("@");
	if (atPos < 0)
		return false;
	BString provider;
	email.CopyInto(provider, atPos + 1, email.Length() - atPos);
	if (provider.FindLast(".") < 0)
		return false;
	return true;
}


// #pragma mark -


/**
 * @brief Builds the second wizard page from the populated provider_info
 *        in @a info.
 *
 * Lays out two BBoxes (incoming and outgoing) each containing a server
 * hostname text control plus the protocol-specific authentication and
 * encryption menus pulled in from the chosen protocol add-ons.
 *
 * @param info  Populated account_info from the first wizard page.
 */
ServerSettingsView::ServerSettingsView(const account_info &info)
	:
	BGroupView("server", B_VERTICAL),
	fInboundAccount(true),
	fOutboundAccount(true),
	fInboundAuthMenu(NULL),
	fOutboundAuthMenu(NULL),
	fInboundEncrItemStart(NULL),
	fOutboundEncrItemStart(NULL),
	fImageID(-1)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fInboundAccount = true;
	fOutboundAccount = true;

	// inbound
	BBox* box = new BBox("inbound");
	box->SetLabel(B_TRANSLATE("Incoming"));
	AddChild(box);

	BString serverName;
	if (info.inboundType == IMAP)
		serverName = info.providerInfo.imap_server;
	else
		serverName = info.providerInfo.pop_server;

	BGridView* grid = new BGridView("inner");
	grid->GridLayout()->SetInsets(B_USE_DEFAULT_SPACING);
	box->AddChild(grid);

	fInboundNameView = new BTextControl("inbound", B_TRANSLATE("Server Name:"),
		serverName, new BMessage(kServerChangedMsg));
	grid->GridLayout()->AddItem(fInboundNameView->CreateLabelLayoutItem(),
		0, 0);
	grid->GridLayout()->AddItem(fInboundNameView->CreateTextViewLayoutItem(),
		1, 0);

	int32 row = 1;

	_GetAuthEncrMenu(info.inboundProtocol, fInboundAuthMenu,
		fInboundEncryptionMenu);
	if (fInboundAuthMenu != NULL) {
		int authID = info.providerInfo.authentification_pop;
		if (info.inboundType == POP)
			fInboundAuthMenu->Menu()->ItemAt(authID)->SetMarked(true);
		fInboundAuthItemStart = fInboundAuthMenu->Menu()->FindMarked();

		grid->GridLayout()->AddItem(fInboundAuthMenu->CreateLabelLayoutItem(),
			0, row);
		grid->GridLayout()->AddItem(fInboundAuthMenu->CreateMenuBarLayoutItem(),
			1, row++);
	}
	if (fInboundEncryptionMenu != NULL) {
		BMenuItem *item = NULL;
		if (info.inboundType == POP) {
			item = fInboundEncryptionMenu->Menu()->ItemAt(
				info.providerInfo.ssl_pop);
			if (item != NULL)
				item->SetMarked(true);
		}
		if (info.inboundType == IMAP) {
			item = fInboundEncryptionMenu->Menu()->ItemAt(
				info.providerInfo.ssl_imap);
			if (item != NULL)
				item->SetMarked(true);
		}
		fInboundEncrItemStart = fInboundEncryptionMenu->Menu()->FindMarked();

		grid->GridLayout()->AddItem(
			fInboundEncryptionMenu->CreateLabelLayoutItem(), 0, row);
		grid->GridLayout()->AddItem(
			fInboundEncryptionMenu->CreateMenuBarLayoutItem(), 1, row++);
	}
	grid->GridLayout()->AddItem(BSpaceLayoutItem::CreateGlue(), 0, row);

	if (!fInboundAccount)
		box->Hide();

	// outbound
	box = new BBox("outbound");
	box->SetLabel(B_TRANSLATE("Outgoing"));
	AddChild(box);

	grid = new BGridView("inner");
	grid->GridLayout()->SetInsets(B_USE_DEFAULT_SPACING);
	box->AddChild(grid);

	serverName = info.providerInfo.smtp_server;
	fOutboundNameView = new BTextControl("outbound",
		B_TRANSLATE("Server name:"), serverName.String(),
		new BMessage(kServerChangedMsg));
	grid->GridLayout()->AddItem(fOutboundNameView->CreateLabelLayoutItem(),
		0, 0);
	grid->GridLayout()->AddItem(fOutboundNameView->CreateTextViewLayoutItem(),
		1, 0);

	row = 1;

	_GetAuthEncrMenu(info.outboundProtocol, fOutboundAuthMenu,
		fOutboundEncryptionMenu);
	if (fOutboundAuthMenu != NULL) {
		BMenuItem* item = fOutboundAuthMenu->Menu()->ItemAt(
			info.providerInfo.authentification_smtp);
		if (item != NULL)
			item->SetMarked(true);
		fOutboundAuthItemStart = item;

		grid->GridLayout()->AddItem(fOutboundAuthMenu->CreateLabelLayoutItem(),
			0, row);
		grid->GridLayout()->AddItem(
			fOutboundAuthMenu->CreateMenuBarLayoutItem(), 1, row++);
	}
	if (fOutboundEncryptionMenu != NULL) {
		BMenuItem* item = fOutboundEncryptionMenu->Menu()->ItemAt(
			info.providerInfo.ssl_smtp);
		if (item != NULL)
			item->SetMarked(true);
		fOutboundEncrItemStart = item;

		grid->GridLayout()->AddItem(
			fOutboundEncryptionMenu->CreateLabelLayoutItem(), 0, row);
		grid->GridLayout()->AddItem(
			fOutboundEncryptionMenu->CreateMenuBarLayoutItem(), 1, row++);
	}
	grid->GridLayout()->AddItem(BSpaceLayoutItem::CreateGlue(), 0, row);

	if (!fOutboundAccount)
		box->Hide();
}


/**
 * @brief Detaches the protocol-supplied auth/encryption menus before
 *        unloading their backing add-on.
 *
 * @note The menus are removed manually because their code lives in the
 *       loaded image and is unmapped on @c unload_add_on().
 */
ServerSettingsView::~ServerSettingsView()
{
	// Remove manually, as their code may be located in an add-on
	RemoveChild(fInboundAuthMenu);
	RemoveChild(fInboundEncryptionMenu);
	delete fInboundAuthMenu;
	delete fInboundEncryptionMenu;
	unload_add_on(fImageID);
}


/**
 * @brief Copies the user-edited server hostnames and menu selections back
 *        into @a info.
 *
 * Also calls _DetectMenuChanges() to synthesise a kServerChangedMsg if the
 * user toggled any auth/encryption value while on this page.
 *
 * @param info  account_info to update in place.
 */
void
ServerSettingsView::GetServerInfo(account_info& info)
{
	if (info.inboundType == IMAP) {
		info.providerInfo.imap_server = fInboundNameView->Text();
		if (fInboundEncryptionMenu != NULL) {
			BMenuItem* item = fInboundEncryptionMenu->Menu()->FindMarked();
			if (item != NULL) {
				info.providerInfo.ssl_imap
					= fInboundEncryptionMenu->Menu()->IndexOf(item);
			}
		}
	} else {
		info.providerInfo.pop_server = fInboundNameView->Text();
		BMenuItem* item = NULL;
		if (fInboundAuthMenu != NULL) {
			item = fInboundAuthMenu->Menu()->FindMarked();
			if (item != NULL) {
				info.providerInfo.authentification_pop
					= fInboundAuthMenu->Menu()->IndexOf(item);
			}
		}
		if (fInboundEncryptionMenu != NULL) {
			item = fInboundEncryptionMenu->Menu()->FindMarked();
			if (item != NULL) {
				info.providerInfo.ssl_pop
					= fInboundEncryptionMenu->Menu()->IndexOf(item);
			}
		}
	}
	info.providerInfo.smtp_server = fOutboundNameView->Text();
	BMenuItem* item = NULL;
	if (fOutboundAuthMenu != NULL) {
		item = fOutboundAuthMenu->Menu()->FindMarked();
		if (item != NULL) {
			info.providerInfo.authentification_smtp
				= fOutboundAuthMenu->Menu()->IndexOf(item);
		}
	}

	if (fOutboundEncryptionMenu != NULL) {
		item = fOutboundEncryptionMenu->Menu()->FindMarked();
		if (item != NULL) {
			info.providerInfo.ssl_smtp
				= fOutboundEncryptionMenu->Menu()->IndexOf(item);
		}
	}
	_DetectMenuChanges();
}


/**
 * @brief Posts @c kServerChangedMsg to the parent window when any of the
 *        auth or encryption menus differ from the values they held when
 *        the page was first shown.
 */
void
ServerSettingsView::_DetectMenuChanges()
{
	bool changed = _HasMarkedChanged(fInboundAuthMenu, fInboundAuthItemStart)
		|| _HasMarkedChanged(fInboundEncryptionMenu, fInboundEncrItemStart)
		|| _HasMarkedChanged(fOutboundAuthMenu, fOutboundAuthItemStart)
		|| _HasMarkedChanged(fOutboundEncryptionMenu, fOutboundEncrItemStart);

	if (changed) {
		BMessage msg(kServerChangedMsg);
		BMessenger messenger(NULL, Window()->Looper());
		messenger.SendMessage(&msg);
	}
}


/**
 * @brief Helper that tests whether the currently marked item of @a field
 *        differs from @a originalItem.
 *
 * @param field         Menu field to inspect; may be @c NULL (returns
 *                      false).
 * @param originalItem  The item that was marked when the page opened.
 * @return @c true if the marked item changed; @c false if @a field is
 *         @c NULL or unchanged.
 */
bool
ServerSettingsView::_HasMarkedChanged(BMenuField* field,
	BMenuItem* originalItem)
{
	if (field != NULL) {
		BMenuItem *item = field->Menu()->FindMarked();
		if (item != originalItem)
			return true;
	}
	return false;
}


/**
 * @brief Extracts the @c "auth_method" and @c "flavor" BMenuField widgets
 *        from a protocol add-on's settings view.
 *
 * @param protocol   entry_ref of the protocol add-on (currently unused
 *                   pending the upstream rework noted inline).
 * @param authField  Output: pointer to the extracted auth-method menu
 *                   field; @c NULL if missing.
 * @param sslField   Output: pointer to the extracted SSL/TLS flavor menu
 *                   field; @c NULL if missing.
 * @todo Wire this back to a real CreateConfigView call once the protocol
 *       add-on API is finalised.
 */
void
ServerSettingsView::_GetAuthEncrMenu(entry_ref protocol,
	BMenuField*& authField, BMenuField*& sslField)
{
	BMailAccountSettings dummySettings;
	BView *view = new BStringView("", "Not here!");//CreateConfigView(protocol, dummySettings.InboundSettings(),
//		dummySettings, fImageId);

	authField = (BMenuField*)view->FindView("auth_method");
	sslField = (BMenuField*)view->FindView("flavor");

	view->RemoveChild(authField);
	view->RemoveChild(sslField);
	delete view;
}
