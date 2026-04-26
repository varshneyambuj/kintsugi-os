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
 *   Copyright 2007-2012, Haiku, Inc. All rights reserved.
 *   Copyright 2001 Dr. Zoidberg Enterprises. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ConfigViews.cpp
 * @brief Implements AccountConfigView and ProtocolSettingsView, the
 *        right-hand detail panes used by ConfigWindow.
 */


//!	Config views for the account, protocols, and filters.


#include "ConfigViews.h"

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <ListView.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <Looper.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <TextControl.h>

#include <string.h>

#include <MailSettings.h>

#include "FilterConfigView.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Config Views"


// AccountConfigView
/** @brief Posted on every keystroke in the account-name text control so
           the listview entry stays in sync. */
const uint32 kMsgAccountNameChanged = 'anmc';

// ProtocolsConfigView
/** @brief Reserved for future use when the protocol add-on selection
           becomes user-editable in this pane. */
const uint32 kMsgProtocolChanged = 'prch';


// #pragma mark -


/**
 * @brief Builds the account detail pane and pre-fills it with @a account's
 *        current values via UpdateViews().
 *
 * @param account  Backing settings; not owned by the view.
 */
AccountConfigView::AccountConfigView(BMailAccountSettings* account)
	:
	BBox("account"),
	fAccount(account)
{
	SetLabel(B_TRANSLATE("Account settings"));

	fNameControl = new BTextControl(NULL, B_TRANSLATE("Account name:"), NULL,
		new BMessage(kMsgAccountNameChanged));
	fRealNameControl = new BTextControl(NULL, B_TRANSLATE("Real name:"), NULL,
		NULL);
	fReturnAddressControl = new BTextControl(NULL,
		B_TRANSLATE("Return address:"), NULL, NULL);

	BView* contents = new BView(NULL, 0);
	AddChild(contents);

	BLayoutBuilder::Grid<>(contents, 0.f)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fNameControl->CreateLabelLayoutItem(), 0, 0)
		.Add(fNameControl->CreateTextViewLayoutItem(), 1, 0)
		.Add(fRealNameControl->CreateLabelLayoutItem(), 0, 1)
		.Add(fRealNameControl->CreateTextViewLayoutItem(), 1, 1)
		.Add(fReturnAddressControl->CreateLabelLayoutItem(), 0, 2)
		.Add(fReturnAddressControl->CreateTextViewLayoutItem(), 1, 2)
		.AddGlue(0, 3);
}


/**
 * @brief Persists the current text-control values back into the backing
 *        BMailAccountSettings when this pane is unmounted.
 */
void
AccountConfigView::DetachedFromWindow()
{
	fAccount->SetName(fNameControl->Text());
	fAccount->SetRealName(fRealNameControl->Text());
	fAccount->SetReturnAddress(fReturnAddressControl->Text());
}


/**
 * @brief Pulls the latest values out of the BMailAccountSettings on
 *        mount and retargets the modification message at this view.
 */
void
AccountConfigView::AttachedToWindow()
{
	UpdateViews();
	fNameControl->SetTarget(this);
}


/**
 * @brief Handles control updates posted by the account-name text control.
 *
 * @param msg  Message routed by the BWindow's looper. Only
 *             @c kMsgAccountNameChanged is acted on; other messages
 *             defer to BView.
 */
void
AccountConfigView::MessageReceived(BMessage *msg)
{
	switch (msg->what) {
		case kMsgAccountNameChanged:
			fAccount->SetName(fNameControl->Text());
			break;

		default:
			BView::MessageReceived(msg);
	}
}


/**
 * @brief Refreshes the three text controls from the account's stored
 *        name, real name, and return address.
 */
void
AccountConfigView::UpdateViews()
{
	fNameControl->SetText(fAccount->Name());
	fRealNameControl->SetText(fAccount->RealName());
	fReturnAddressControl->SetText(fAccount->ReturnAddress());
}


// #pragma mark -


/**
 * @brief Loads the protocol add-on identified by @a ref and embeds its
 *        BMailSettingsView in this pane.
 *
 * On any failure path a localised error string is shown in place of the
 * settings view so the rest of the window stays usable.
 *
 * @param ref               Add-on file ref (e.g. POP3 or IMAP).
 * @param accountSettings   Read-only context for the add-on view.
 * @param settings          Mutable settings the embedded view writes into
 *                          when it is detached.
 */
ProtocolSettingsView::ProtocolSettingsView(const entry_ref& ref,
	const BMailAccountSettings& accountSettings,
	BMailProtocolSettings& settings)
	:
	BBox("protocol"),
	fSettings(settings),
	fSettingsView(NULL)
{
	status_t status = _CreateSettingsView(ref, accountSettings, settings);
	BView* view = fSettingsView;

	if (status == B_OK) {
		SetLabel(ref.name);
	} else {
		BString text(B_TRANSLATE("An error occurred while creating the "
			"config view: %error."));
		text.ReplaceAll("%error", strerror(status));
		view = new BStringView("error", text.String());

		SetLabel(B_TRANSLATE("Error!"));
	}

	BView* contents = new BView(NULL, 0);
	AddChild(contents);

	BLayoutBuilder::Group<>(contents, B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(view)
		.AddGlue();
}


/**
 * @brief Persists the embedded view's state into @c fSettings, removes the
 *        view from the hierarchy, and unloads its add-on image.
 *
 * @note The view is removed before @c unload_add_on() to avoid running
 *       destructors in code that has already been mapped out.
 */
void
ProtocolSettingsView::DetachedFromWindow()
{
	if (fSettingsView == NULL)
		return;

	if (fSettingsView->SaveInto(fSettings) != B_OK)
		return;

	// We need to remove the settings view before unloading its add-on
	fSettingsView->RemoveSelf();
	delete fSettingsView;
	fSettingsView = NULL;
	unload_add_on(fImage);
}


/**
 * @brief Loads the add-on at @a ref and looks up its
 *        @c instantiate_protocol_settings_view export.
 *
 * @param ref              Path to the protocol add-on.
 * @param accountSettings  Forwarded to the add-on's instantiator.
 * @param settings         Forwarded to the add-on's instantiator.
 * @retval B_OK              View created and stored in @c fSettingsView.
 * @retval B_MISSING_SYMBOL  Add-on lacked the required entry point.
 * @retval (image error)     Negative image_id from @c load_add_on().
 */
status_t
ProtocolSettingsView::_CreateSettingsView(const entry_ref& ref,
	const BMailAccountSettings& accountSettings,
	BMailProtocolSettings& settings)
{
	BMailSettingsView* (*instantiateConfig)(
		const BMailAccountSettings& accountSettings,
		BMailProtocolSettings& settings);
	BPath path(&ref);
	image_id image = load_add_on(path.Path());
	if (image < 0)
		return image;

	if (get_image_symbol(image, "instantiate_protocol_settings_view",
			B_SYMBOL_TYPE_TEXT, (void**)&instantiateConfig) != B_OK) {
		unload_add_on(image);
		return B_MISSING_SYMBOL;
	}

	fImage = image;
	fSettingsView = instantiateConfig(accountSettings, settings);
	return B_OK;
}
