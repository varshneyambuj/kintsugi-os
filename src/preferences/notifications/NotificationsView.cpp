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
 *   Copyright 2010-2017, Haiku, Inc. All Rights Reserved.
 *   Copyright 2009, Pier Luigi Fiorini.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Pier Luigi Fiorini, pierluigi.fiorini@gmail.com
 *       Brian Hill, supernova@tycho.email
 */


/**
 * @file NotificationsView.cpp
 * @brief Implementation of NotificationsView and AppRow, the Applications
 *        tab of the Notifications preflet.
 *
 * Maintains a BColumnListView of registered applications keyed by MIME
 * signature. Adding an entry uses a filtered BFilePanel; muting or
 * removing entries posts an Apply so notification_server picks up the
 * change immediately.
 */


#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Notification.h>
#include <Path.h>
#include <TextControl.h>
#include <Window.h>

#include <notification/Notifications.h>
#include <notification/NotificationReceived.h>

#include "NotificationsConstants.h"
#include "NotificationsView.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "NotificationView"

// Applications column indexes
/** @brief Column index for the application name. */
const int32 kAppNameIndex = 0;
/** @brief Column index for the allowed/muted status text. */
const int32 kAppEnabledIndex = 1;


/**
 * @brief Constructs the row with a name, MIME signature, and initial
 *        allowed/muted state.
 *
 * @param name       Display name of the application.
 * @param signature  Application MIME signature.
 * @param allowed    true when notifications from this app are allowed.
 */
AppRow::AppRow(const char* name, const char* signature, bool allowed)
	:
	BRow(),
	fName(name),
	fSignature(signature),
	fAllowed(allowed)
{
	SetField(new BStringField(fName.String()), kAppNameIndex);
	BString text = fAllowed ? B_TRANSLATE("Allowed") : B_TRANSLATE("Muted");
	SetField(new BStringField(text.String()), kAppEnabledIndex);
}


/**
 * @brief Updates the allowed flag and refreshes the displayed status.
 *
 * @param allowed  New allowed/muted state.
 */
void
AppRow::SetAllowed(bool allowed)
{
	fAllowed = allowed;
	RefreshEnabledField();
}


/**
 * @brief Rewrites the status column field to match the current allowed
 *        flag and invalidates the row so the change is repainted.
 */
void
AppRow::RefreshEnabledField()
{
	BStringField* field = (BStringField*)GetField(kAppEnabledIndex);
	BString text = fAllowed ? B_TRANSLATE("Allowed") : B_TRANSLATE("Muted");
	field->SetString(text.String());
	Invalidate();
}


/**
 * @brief Builds the column list view, the Add/Remove buttons, and the
 *        Mute checkbox, and prepares the file panel used to add new apps.
 *
 * @param host  Settings host receiving change notifications.
 */
NotificationsView::NotificationsView(SettingsHost* host)
	:
	SettingsPane("apps", host),
	fSelectedRow(NULL)
{
	// Applications list
	fApplications = new BColumnListView(B_TRANSLATE("Applications"),
		0, B_FANCY_BORDER, false);
	fApplications->SetSelectionMode(B_SINGLE_SELECTION_LIST);
	fApplications->SetSelectionMessage(new BMessage(kApplicationSelected));

	float colWidth = be_plain_font->StringWidth(B_TRANSLATE("Application"))
		+ (kCLVTitlePadding * 2);
	fAppCol = new BStringColumn(B_TRANSLATE("Application"), colWidth * 2,
		colWidth, colWidth * 4, B_TRUNCATE_END, B_ALIGN_LEFT);
	fApplications->AddColumn(fAppCol, kAppNameIndex);

	colWidth = be_plain_font->StringWidth(B_TRANSLATE("Status"))
		+ (kCLVTitlePadding * 2);
	fAppEnabledCol = new BStringColumn(B_TRANSLATE("Status"), colWidth * 1.5,
		colWidth, colWidth * 3, B_TRUNCATE_END, B_ALIGN_LEFT);
	fApplications->AddColumn(fAppEnabledCol, kAppEnabledIndex);
	fApplications->SetSortColumn(fAppCol, true, true);

	fAddButton = new BButton("add_app", B_TRANSLATE("Add" B_UTF8_ELLIPSIS),
		new BMessage(kAddApplication));
	fRemoveButton = new BButton("add_app", B_TRANSLATE("Remove"),
		new BMessage(kRemoveApplication));
	fRemoveButton->SetEnabled(false);

	fMuteAll = new BCheckBox("block", B_TRANSLATE("Mute notifications from "
		"this application"),
		new BMessage(kMuteChanged));

	// Add views
	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.AddGroup(B_HORIZONTAL)
			.Add(fApplications)
			.AddGroup(B_VERTICAL)
				.Add(fAddButton)
				.Add(fRemoveButton)
				.AddGlue()
			.End()
		.End()
		.Add(fMuteAll)
		.SetInsets(B_USE_WINDOW_SPACING, B_USE_WINDOW_SPACING,
			B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING);

	// Set button sizes
	float maxButtonWidth = std::max(fAddButton->PreferredSize().Width(),
		fRemoveButton->PreferredSize().Width());
	fAddButton->SetExplicitMaxSize(BSize(maxButtonWidth, B_SIZE_UNSET));
	fRemoveButton->SetExplicitMaxSize(BSize(maxButtonWidth, B_SIZE_UNSET));

	// File Panel
	fPanelFilter = new AppRefFilter();
	fAddAppPanel = new BFilePanel(B_OPEN_PANEL, NULL, NULL, B_FILE_NODE, false,
		NULL, fPanelFilter);
}


/**
 * @brief Destructor. Releases the file panel and its filter.
 */
NotificationsView::~NotificationsView()
{
	delete fAddAppPanel;
	delete fPanelFilter;
}


/**
 * @brief Retargets controls to this view and primes the dependent state.
 */
void
NotificationsView::AttachedToWindow()
{
	fApplications->SetTarget(this);
	fApplications->SetInvocationMessage(new BMessage(kApplicationSelected));
	fAddButton->SetTarget(this);
	fRemoveButton->SetTarget(this);
	fMuteAll->SetTarget(this);
	fAddAppPanel->SetTarget(this);
	_RecallItemSettings();
}


/**
 * @brief Routes selection, mute, add, and remove messages.
 *
 * Adding a new entry validates that the dropped binary has a MIME
 * signature; missing signatures or duplicates raise an info BAlert and
 * are rejected. Each successful change posts kApply so the host saves.
 *
 * @param msg  Incoming BMessage.
 */
void
NotificationsView::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kApplicationSelected:
		{
			Window()->Lock();
			_ClearItemSettings();
			_UpdateSelectedItem();
			_RecallItemSettings();
			Window()->Unlock();
			break;
		}
		case kMuteChanged:
		{
			bool allowed = fMuteAll->Value() == B_CONTROL_OFF;
			fSelectedRow->SetAllowed(allowed);
			appusage_t::iterator it = fAppFilters.find(fSelectedRow->Signature());
			if (it != fAppFilters.end())
				it->second->SetAllowed(allowed);
			Window()->PostMessage(kApply);
			break;
		}
		case kAddApplication:
		{
			BMessage addmsg(kAddApplicationRef);
			fAddAppPanel->SetMessage(&addmsg);
			fAddAppPanel->Show();
			break;
		}
		case kAddApplicationRef:
		{
			entry_ref srcRef;
			msg->FindRef("refs", &srcRef);
			BEntry srcEntry(&srcRef, true);
			BPath path(&srcEntry);
			BNode node(&srcEntry);
			char *buf = new char[B_ATTR_NAME_LENGTH];
			ssize_t size;
			if ( (size = node.ReadAttr("BEOS:APP_SIG", 0, 0, buf,
				B_ATTR_NAME_LENGTH)) > 0 )
			{
				// Search for already existing app
				appusage_t::iterator it = fAppFilters.find(buf);
				if (it != fAppFilters.end()) {
					BString text(path.Leaf());
					text.Append(B_TRANSLATE_COMMENT(" is already listed",
							"Alert message"));
					BAlert* alert = new BAlert("", text.String(),
						B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
						B_WARNING_ALERT);
					alert->Go(NULL);
				} else {
					AppUsage* appUsage = new AppUsage(path.Leaf(), buf, true);
					fAppFilters[appUsage->Signature()] = appUsage;
					AppRow* row = new AppRow(appUsage->AppName(),
						appUsage->Signature(), appUsage->Allowed());
					fApplications->AddRow(row);
					fApplications->DeselectAll();
					fApplications->AddToSelection(row);
					fApplications->ScrollTo(row);
					_UpdateSelectedItem();
					_RecallItemSettings();
					//row->Invalidate();
					//fApplications->InvalidateRow(row);
					// TODO redraw row properly
					Window()->PostMessage(kApply);
				}
			} else {
				BAlert* alert = new BAlert("",
					B_TRANSLATE_COMMENT("Application does not have "
						"a valid signature", "Alert message"),
					B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
					B_WARNING_ALERT);
				alert->Go(NULL);
			}
			delete[] buf;
			break;
		}
		case kRemoveApplication:
		{
			if (fSelectedRow) {
				appusage_t::iterator it = fAppFilters.find(fSelectedRow->Signature());
				if (it != fAppFilters.end()) {
					delete it->second;
					fAppFilters.erase(it);
				}
				fApplications->RemoveRow(fSelectedRow);
				delete fSelectedRow;
				fSelectedRow = NULL;
				_ClearItemSettings();
				_UpdateSelectedItem();
				_RecallItemSettings();
				Window()->PostMessage(kApply);
			}
			break;
		}
		default:
			BView::MessageReceived(msg);
			break;
	}
}


/**
 * @brief Replaces the in-memory app-usage map with entries deserialized
 *        from @a settings, then repopulates the list view.
 *
 * @param settings  Source BMessage; must contain "app_usage" entries.
 * @return B_OK on success, B_ERROR when no app_usage entries are found.
 */
status_t
NotificationsView::Load(BMessage& settings)
{
	type_code type;
	int32 count = 0;

	if (settings.GetInfo("app_usage", &type, &count) != B_OK)
		return B_ERROR;

	// Clean filters
	appusage_t::iterator auIt;
	for (auIt = fAppFilters.begin(); auIt != fAppFilters.end(); auIt++)
		delete auIt->second;
	fAppFilters.clear();

	// Add new filters
	for (int32 i = 0; i < count; i++) {
		AppUsage* app = new AppUsage();
		settings.FindFlat("app_usage", i, app);
		fAppFilters[app->Signature()] = app;
	}

	// Load the applications list
	_PopulateApplications();

	return B_OK;
}


/**
 * @brief Flattens every AppUsage entry into @a storage as repeated
 *        "app_usage" fields.
 *
 * @param storage  Destination BMessage.
 * @return Always B_OK.
 */
status_t
NotificationsView::Save(BMessage& storage)
{
	appusage_t::iterator fIt;
	for (fIt = fAppFilters.begin(); fIt != fAppFilters.end(); fIt++)
		storage.AddFlat("app_usage", fIt->second);

	return B_OK;
}


/**
 * @brief Resets the per-app controls (mute checkbox) to a neutral state.
 */
void
NotificationsView::_ClearItemSettings()
{
	fMuteAll->SetValue(B_CONTROL_OFF);
}


/**
 * @brief Caches the currently selected list row in fSelectedRow.
 */
void
NotificationsView::_UpdateSelectedItem()
{
	fSelectedRow = dynamic_cast<AppRow*>(fApplications->CurrentSelection());

}


/**
 * @brief Synchronizes the per-app controls with the cached selection.
 *
 * Disables the mute checkbox and Remove button when no row is selected;
 * otherwise reflects the selected app's allowed flag.
 */
void
NotificationsView::_RecallItemSettings()
{
	// No selected item
	if(fSelectedRow == NULL)
	{
		fMuteAll->SetValue(B_CONTROL_OFF);
		fMuteAll->SetEnabled(false);
		fRemoveButton->SetEnabled(false);
	} else {
		fMuteAll->SetEnabled(true);
		fRemoveButton->SetEnabled(true);
		appusage_t::iterator it = fAppFilters.find(fSelectedRow->Signature());
		if (it != fAppFilters.end())
			fMuteAll->SetValue(!(it->second->Allowed()));
	}
}


/**
 * @brief Revert is a no-op: edits are persisted immediately on Apply.
 *
 * @return Always B_OK.
 */
status_t
NotificationsView::Revert()
{
	return B_OK;
}


/**
 * @brief Reports that this pane has nothing to revert (it auto-saves).
 *
 * @return Always false.
 */
bool
NotificationsView::RevertPossible()
{
	return false;
}


/**
 * @brief Defaults is a no-op for the per-app list.
 *
 * @return Always B_OK.
 */
status_t
NotificationsView::Defaults()
{
	return B_OK;
}


/**
 * @brief Reports that this pane has no notion of factory defaults.
 *
 * @return Always false.
 */
bool
NotificationsView::DefaultsPossible()
{
	return false;
}


/**
 * @brief Opts this pane out of the global Defaults / Revert button strip.
 *
 * @return Always false.
 */
bool
NotificationsView::UseDefaultRevertButtons()
{
	return false;
}


/**
 * @brief Rebuilds the BColumnListView from the current fAppFilters map.
 */
void
NotificationsView::_PopulateApplications()
{
	fApplications->Clear();

	appusage_t::iterator it;
	for (it = fAppFilters.begin(); it != fAppFilters.end(); ++it) {
		AppUsage* appUsage = it->second;
		AppRow* row = new AppRow(appUsage->AppName(),
			appUsage->Signature(), appUsage->Allowed());
		fApplications->AddRow(row);
	}
}
