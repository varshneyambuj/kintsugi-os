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
 * @file AutoConfigWindow.cpp
 * @brief Implements AutoConfigWindow, the modal wizard that drives the
 *        new-account creation flow.
 *
 * The window swaps between AutoConfigView (identity page) and
 * ServerSettingsView (server review page), uses AutoConfig to look up
 * provider data, and on completion calls back into the parent
 * ConfigWindow to register the new BMailAccountSettings.
 */


#include "AutoConfigWindow.h"

#include "AutoConfig.h"
#include "AutoConfigView.h"

#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <MailSettings.h>
#include <Message.h>
#include <Path.h>

#include <crypt.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AutoConfigWindow"


/** @brief Default visual spacing inside the wizard window, in pixels. */
const float kSpacing = 10;


/**
 * @brief Constructs the modal wizard window with the identity page
 *        showing.
 *
 * Registers a Command-W shortcut for closing and configures the Next
 * button as the default. The window is application-modal and avoids the
 * front so it does not steal focus from other apps.
 *
 * @param rect    Initial frame.
 * @param parent  ConfigWindow that owns the new account once created.
 */
AutoConfigWindow::AutoConfigWindow(BRect rect, ConfigWindow *parent)
	:
	BWindow(rect, B_TRANSLATE("Create new account"), B_TITLED_WINDOW_LOOK,
		B_MODAL_APP_WINDOW_FEEL, B_NOT_ZOOMABLE | B_AVOID_FRONT
			| B_AUTO_UPDATE_SIZE_LIMITS, B_ALL_WORKSPACES),
	fParentWindow(parent),
	fAccount(NULL),
	fMainConfigState(true),
	fServerConfigState(false),
	fAutoConfigServer(true)
{
	fContainerView = new BGroupView("config container");

	fBackButton = new BButton("back", B_TRANSLATE("Back"),
		new BMessage(kBackMsg));
	fBackButton->SetEnabled(false);

	fNextButton = new BButton("next", B_TRANSLATE("Next"),
		new BMessage(kOkMsg));
	fNextButton->MakeDefault(true);

	fMainView = new AutoConfigView(fAutoConfig);
	fMainView->SetLabel(B_TRANSLATE("Account settings"));
	fContainerView->AddChild(fMainView);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fContainerView)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fBackButton)
			.Add(fNextButton);

	// Add a shortcut to close the window using Command-W
	AddShortcut('W', B_COMMAND_KEY, new BMessage(B_QUIT_REQUESTED));
}


/**
 * @brief Trivial destructor; child views are owned by the BWindow.
 */
AutoConfigWindow::~AutoConfigWindow()
{
}


/**
 * @brief Drives the wizard state machine in response to button clicks.
 *
 * Handles four messages: @c kOkMsg advances or finishes the wizard;
 * @c kBackMsg returns to the identity page; @c kServerChangedMsg disables
 * the auto-detection shortcut so user edits survive going forward; the
 * default branch falls through to BWindow.
 *
 * @param msg  Incoming BMessage.
 */
void
AutoConfigWindow::MessageReceived(BMessage* msg)
{
	status_t status = B_ERROR;
	BAlert* invalidMailAlert = NULL;

	switch (msg->what) {
		case kOkMsg:
			if (fMainConfigState) {
				fMainView->GetBasicAccountInfo(fAccountInfo);
				if (!fMainView->IsValidMailAddress(fAccountInfo.email)) {
					invalidMailAlert = new BAlert("invalidMailAlert",
						B_TRANSLATE("Enter a valid e-mail address."),
						B_TRANSLATE("OK"));
					invalidMailAlert->SetFlags(invalidMailAlert->Flags()
						| B_CLOSE_ON_ESCAPE);
					invalidMailAlert->Go();
					return;
				}
				if (fAutoConfigServer) {
					status = fAutoConfig.GetInfoFromMailAddress(
						fAccountInfo.email.String(),
						&fAccountInfo.providerInfo);
				}
				if (status == B_OK) {
					fParentWindow->Lock();
					GenerateBasicAccount();
					fParentWindow->Unlock();
					Quit();
				}
				fMainConfigState = false;
				fServerConfigState = true;
				fMainView->Hide();

				fServerView = new ServerSettingsView(fAccountInfo);
				fContainerView->AddChild(fServerView);

				fBackButton->SetEnabled(true);
				fNextButton->SetLabel(B_TRANSLATE("Finish"));
			} else {
				fServerView->GetServerInfo(fAccountInfo);
				fParentWindow->Lock();
				GenerateBasicAccount();
				fParentWindow->Unlock();
				Quit();
			}
			break;

		case kBackMsg:
			if (fServerConfigState) {
				fServerView->GetServerInfo(fAccountInfo);

				fMainConfigState = true;
				fServerConfigState = false;

				fContainerView->RemoveChild(fServerView);
				delete fServerView;

				fMainView->Show();
				fBackButton->SetEnabled(false);
			}
			break;

		case kServerChangedMsg:
			fAutoConfigServer = false;
			break;

		default:
			BWindow::MessageReceived(msg);
			break;
	}
}


/**
 * @brief Allows the wizard to close without prompting; any unsaved data is
 *        intentionally discarded.
 *
 * @return Always @c true.
 */
bool
AutoConfigWindow::QuitRequested()
{
	return true;
}


/**
 * @brief Materialises the collected fAccountInfo into a fresh
 *        BMailAccountSettings owned by the parent ConfigWindow.
 *
 * Selects the appropriate inbound add-on (POP3 or IMAP) and writes the
 * SMTP outbound add-on. The username/password and SSL/auth indices are
 * stored verbatim into the inbound and outbound BMessages. Re-callable:
 * if the account was already created on the first wizard step, the same
 * one is reused on the second step.
 *
 * @return The owned BMailAccountSettings (caller must not delete).
 * @note Briefly takes the parent ConfigWindow lock to add and update the
 *       account.
 */
BMailAccountSettings*
AutoConfigWindow::GenerateBasicAccount()
{
	if (!fAccount) {
		fParentWindow->Lock();
		fAccount = fParentWindow->AddAccount();
		fParentWindow->Unlock();
	}

	fAccount->SetName(fAccountInfo.accountName.String());
	fAccount->SetRealName(fAccountInfo.name.String());
	fAccount->SetReturnAddress(fAccountInfo.email.String());

	BMessage& inboundArchive = fAccount->InboundSettings();
	inboundArchive.MakeEmpty();
	BString inServerName;
	int32 authType = 0;
	int32 ssl = 0;
	if (fAccountInfo.inboundType == IMAP) {
		inServerName = fAccountInfo.providerInfo.imap_server;
		ssl = fAccountInfo.providerInfo.ssl_imap;
		fAccount->SetInboundAddOn("IMAP");
	} else {
		inServerName = fAccountInfo.providerInfo.pop_server;
		authType = fAccountInfo.providerInfo.authentification_pop;
		ssl = fAccountInfo.providerInfo.ssl_pop;
		fAccount->SetInboundAddOn("POP3");
	}
	inboundArchive.AddString("server", inServerName);
	inboundArchive.AddInt32("auth_method", authType);
	inboundArchive.AddInt32("flavor", ssl);
	inboundArchive.AddString("username", fAccountInfo.loginName);
	set_passwd(&inboundArchive, "cpasswd", fAccountInfo.password);
	inboundArchive.AddBool("leave_mail_on_server", true);
	inboundArchive.AddBool("delete_remote_when_local", true);

	BMessage& outboundArchive = fAccount->OutboundSettings();
	outboundArchive.MakeEmpty();
	fAccount->SetOutboundAddOn("SMTP");
	outboundArchive.AddString("server",
		fAccountInfo.providerInfo.smtp_server);
	outboundArchive.AddString("username", fAccountInfo.loginName);
	set_passwd(&outboundArchive, "cpasswd", fAccountInfo.password);
	outboundArchive.AddInt32("auth_method",
		fAccountInfo.providerInfo.authentification_smtp);
	outboundArchive.AddInt32("flavor",
		fAccountInfo.providerInfo.ssl_smtp);

	fParentWindow->Lock();
	fParentWindow->AccountUpdated(fAccount);
	fParentWindow->Unlock();

	return fAccount;
}
