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
 * MIT License. Copyright 2004-2015, Haiku Inc.;
 * Copyright 2001, Dr. Zoidberg Enterprises;
 * Copyright 2011, Clemens Zeidler <haiku@clemens-zeidler.de>.
 */

/** @file ConfigWindow.h
    @brief Top-level Mail preferences window: maintains the account list
           and the right-hand details pane that shows account, protocol,
           or filter settings depending on which row is selected. */

#ifndef CONFIG_WINDOW_H
#define CONFIG_WINDOW_H


#include <Window.h>
#include <ObjectList.h>
#include <ListItem.h>

#include "MailSettings.h"


class BPopup;
class BTextControl;
class BCheckBox;
class BListView;
class BButton;
class BMenuField;
class BMailSettings;
class BTextView;
class CenterContainer;


/**
 * @brief Discriminator for AccountItem's place in the listview hierarchy:
 *        an account header, its inbound/outbound rows, or its filters
 *        row.
 */
enum item_types {
	ACCOUNT_ITEM = 0,
	INBOUND_ITEM,
	OUTBOUND_ITEM,
	FILTER_ITEM
};


/**
 * @brief List item representing one row in the accounts listview.
 *
 * Account-header rows render in bold; the four item types share a single
 * BMailAccountSettings pointer which the parent window uses to select the
 * appropriate detail pane.
 */
class AccountItem : public BStringItem {
public:
								AccountItem(const char* label,
									BMailAccountSettings* account,
									item_types type);

			void				Update(BView* owner, const BFont* font);
			void				DrawItem(BView* owner, BRect rect,
									bool complete);
			/** @brief Returns the BMailAccountSettings this row belongs
			           to; shared with the row's siblings. */
			BMailAccountSettings* Account() { return fAccount; }
			/** @brief Returns the item kind so the parent window can pick
			           the right detail pane. */
			item_types			Type() { return fType; }

private:
			BMailAccountSettings* fAccount;
			item_types			fType;
};


/**
 * @brief Main Mail preferences window: hosts the account listview, the
 *        general settings tab, the apply/revert buttons, and the dynamic
 *        detail pane.
 */
class ConfigWindow : public BWindow {
public:
								ConfigWindow();
								~ConfigWindow();

			bool				QuitRequested();
			void				MessageReceived(BMessage* msg);

			BMailAccountSettings*	AddAccount();
			void				AccountUpdated(BMailAccountSettings* account);

private:
			BView*				_BuildHowToView();

			void				_LoadSettings();
			void				_LoadAccounts();
			void				_SaveSettings();

			status_t			_SetToGeneralSettings(BMailSettings *general);
			void				_RevertToLastSettings();

			void				_AddAccountToView(
									BMailAccountSettings* account);
			void				_RemoveAccount(BMailAccountSettings* account);
			void				_RemoveAccountFromListView(
									BMailAccountSettings* account);
			void				_AccountSelected(AccountItem* item);
			void				_ReplaceConfigView(BView* view);

private:
			BListView*			fAccountsListView;
			BMailAccountSettings* fLastSelectedAccount;
			BView*				fConfigView;
			BButton*			fRemoveButton;

			BCheckBox*			fCheckMailCheckBox;
			BTextControl*		fIntervalControl;
			BMenuField*			fStatusModeField;
			BTextView*			fHowToTextView;

			bool				fSaveSettings;
			BObjectList<BMailAccountSettings>	fAccounts;
			BObjectList<BMailAccountSettings>	fToDeleteAccounts;
};

#endif	/* CONFIG_WINDOW_H */
