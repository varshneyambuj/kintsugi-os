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
 * MIT License. Copyright 2007-2015, Haiku, Inc. and Copyright 2011,
 * Clemens Zeidler <haiku@clemens-zeidler.de>.
 */

/** @file AutoConfigView.h
    @brief First and second pages of the new-account auto-configuration
           wizard: gather basic identity from the user, then let them
           review and edit the discovered server settings. */

#ifndef AUTO_CONFIG_VIEW_H
#define AUTO_CONFIG_VIEW_H


#include "AutoConfig.h"
#include "ConfigViews.h"

#include <Box.h>
#include <Entry.h>
#include <GroupView.h>
#include <MenuField.h>
#include <String.h>
#include <TextControl.h>

class BPopUpMenu;


/** @brief Posted when the user types in the real-name field. */
const int32	kNameChangedMsg			=	'?nch';
/** @brief Posted when the e-mail address field is edited; triggers a
           username proposal and account-name auto-fill. */
const int32	kEMailChangedMsg		=	'?ech';
/** @brief Posted when the inbound protocol selection changes (POP vs
           IMAP). */
const int32 kProtokollChangedMsg	=	'?pch';
/** @brief Posted when any server hostname or auth/encryption menu changes
           on page two; disables the auto-discovery shortcut. */
const int32 kServerChangedMsg		=	'?sch';


/**
 * @brief Identifies which mail protocol a chosen add-on speaks.
 */
enum protocol_type {
	POP,
	IMAP,
	SMTP
};


/**
 * @brief Mutable record assembled by the wizard pages and consumed by
 *        AutoConfigWindow::GenerateBasicAccount.
 *
 * Bundles user-visible identity fields (name, e-mail, login, password)
 * with the chosen inbound/outbound add-on refs and the provider_info
 * inferred by AutoConfig.
 */
struct account_info {
	protocol_type	inboundType;
	entry_ref		inboundProtocol;
	entry_ref		outboundProtocol;
	BString			name;
	BString			accountName;
	BString			email;
	BString			loginName;
	BString			password;
	provider_info	providerInfo;
};


/**
 * @brief First wizard page: collects the user's name, e-mail, login,
 *        password, and inbound protocol selection.
 */
class AutoConfigView : public BBox {
public:
								AutoConfigView(AutoConfig& config);

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* msg);

			bool				GetBasicAccountInfo(account_info& info);
			bool				IsValidMailAddress(BString email);

private:
			BPopUpMenu*			_SetupProtocolMenu();
			status_t			_GetSMTPAddOnRef(entry_ref* ref);

			BString				_ExtractLocalPart(const char* email);
			void				_ProposeUsername();

private:
			entry_ref			fSMTPAddOnRef;
			BMenuField*			fInProtocolsField;
			BTextControl*		fNameView;
			BTextControl*		fAccountNameView;
			BTextControl*		fEmailView;
			BTextControl*		fLoginNameView;
			BTextControl*		fPasswordView;

			// ref to the parent autoconfig so you only ones read the database
			AutoConfig&			fAutoConfig;
};


/**
 * @brief Second wizard page: shows discovered server hostnames plus
 *        authentication and SSL menus, letting the user override any
 *        auto-detected value before the account is created.
 */
class ServerSettingsView : public BGroupView {
public:
								ServerSettingsView(const account_info& info);
								~ServerSettingsView();
			void				GetServerInfo(account_info& info);

private:
			void				_DetectMenuChanges();
			bool				_HasMarkedChanged(BMenuField* field,
									BMenuItem* originalItem);
			void				_GetAuthEncrMenu(entry_ref protocol,
									BMenuField*& authField,
									BMenuField*& sslField);

private:
			bool				fInboundAccount;
			bool				fOutboundAccount;
			BTextControl*		fInboundNameView;
			BMenuField*			fInboundAuthMenu;
			BMenuField*			fInboundEncryptionMenu;
			BTextControl*		fOutboundNameView;
			BMenuField*			fOutboundAuthMenu;
			BMenuField*			fOutboundEncryptionMenu;

			BMenuItem*			fInboundAuthItemStart;
			BMenuItem*			fInboundEncrItemStart;
			BMenuItem*			fOutboundAuthItemStart;
			BMenuItem*			fOutboundEncrItemStart;

			image_id			fImageID;
};


#endif	// AUTO_CONFIG_VIEW_H
