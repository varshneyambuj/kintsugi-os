/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
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
 * MIT License. Copyright 2010-2017, Haiku, Inc.
 */

/** @file NotificationsView.h
    @brief Applications tab of the Notifications preflet: per-app allow /
           mute list backed by a BColumnListView. */

#ifndef _APPS_VIEW_H
#define _APPS_VIEW_H

#include <FilePanel.h>
#include <ColumnListView.h>
#include <View.h>

#include <notification/AppUsage.h>

#include "AppRefFilter.h"
#include "SettingsPane.h"

/** @brief Map keyed by application MIME signature, used to look up an
           AppUsage record for the currently selected row. */
typedef std::map<BString, AppUsage *> appusage_t;

class BButton;
class BCheckBox;
class BTextControl;
class BStringColumn;
class BDateColumn;


/**
 * @brief Single row in the applications list, carrying the visible name,
 *        the MIME signature, and the current allowed/muted state.
 */
class AppRow : public BRow {
public:
								AppRow(const char* name,
									const char* signature, bool allowed);

			/** @brief Returns the application's display name. */
			const char*			Name() const { return fName.String(); }
			/** @brief Returns the application's MIME signature. */
			const char*			Signature() { return fSignature.String(); };
			void				SetAllowed(bool allowed);
			/** @brief Returns true when notifications from this app are
			           allowed. */
			bool				Allowed() { return fAllowed; };
			void				RefreshEnabledField();

private:
			BString				fName;
			BString				fSignature;
			bool				fAllowed;
};


/**
 * @brief Settings pane that shows the registered notification-aware apps
 *        and lets the user mute or remove individual entries.
 *
 * Saves immediately on every edit and therefore opts out of the global
 * Defaults / Revert buttons via UseDefaultRevertButtons().
 */
class NotificationsView : public SettingsPane {
public:
								NotificationsView(SettingsHost* host);
								~NotificationsView();

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* msg);
			status_t			Revert();
			bool				RevertPossible();
			status_t			Defaults();
			bool				DefaultsPossible();
			bool				UseDefaultRevertButtons();

private:
			status_t			Load(BMessage&);
			status_t			Save(BMessage&);
			void				_ClearItemSettings();
			void				_UpdateSelectedItem();
			void				_RecallItemSettings();
			void				_PopulateApplications();

			appusage_t			fAppFilters;
			AppRefFilter*		fPanelFilter;
			BFilePanel*			fAddAppPanel;
			BButton*			fAddButton;
			BButton*			fRemoveButton;
			BCheckBox*			fMuteAll;
			BColumnListView*	fApplications;
			AppRow*				fSelectedRow;
			BStringColumn*		fAppCol;
			BStringColumn*		fAppEnabledCol;
};

#endif // _APPS_VIEW_H
