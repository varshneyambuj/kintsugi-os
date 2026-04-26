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
 *   Copyright 2007-2016 Haiku, Inc. All rights reserved.
 *   Copyright 2001-2003 Dr. Zoidberg Enterprises. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ConfigWindow.cpp
 * @brief Implements ConfigWindow, the Mail preferences app's main window.
 *
 * Manages the editable in-memory copy of the user's BMailAccountSettings,
 * dispatches selection changes to AccountConfigView/ProtocolSettingsView/
 * FiltersConfigView, and writes settings out to disk and to the
 * BMailDaemon when the user applies them.
 */


//! Main E-Mail config window


#include "ConfigWindow.h"

#include <new>
#include <stdio.h>
#include <string.h>

#include <Alert.h>
#include <AppFileInfo.h>
#include <Application.h>
#include <Bitmap.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Locale.h>
#include <MailDaemon.h>
#include <MailSettings.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Region.h>
#include <Resources.h>
#include <Roster.h>
#include <Screen.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <StringView.h>
#include <TabView.h>
#include <TextControl.h>
#include <TextView.h>

#include <MailPrivate.h>

#include "AutoConfigWindow.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Config Window"


using std::nothrow;

// define if you want to have an apply button
//#define HAVE_APPLY_BUTTON


/** @brief Posted when the user right-clicks a row in the accounts
           listview; carries point and index for the popup menu. */
const uint32 kMsgAccountsRightClicked = 'arcl';
/** @brief Posted when the listview selection changes. */
const uint32 kMsgAccountSelected = 'acsl';
/** @brief Posted when the Add button is clicked; opens AutoConfigWindow. */
const uint32 kMsgAddAccount = 'adac';
/** @brief Posted when the Remove button is clicked or DEL is pressed in
           the listview. */
const uint32 kMsgRemoveAccount = 'rmac';

/** @brief Reserved for future use to switch the auto-check interval
           between minutes/hours/days. */
const uint32 kMsgIntervalUnitChanged = 'iuch';

/** @brief Posted by the "Show notifications" pop-up to live-update the
           status window mode in the running daemon. */
const uint32 kMsgShowStatusWindowChanged = 'shst';
/** @brief Reserved for future status-window appearance change. */
const uint32 kMsgStatusLookChanged = 'lkch';
/** @brief Reserved for future status-window workspace change. */
const uint32 kMsgStatusWorkspaceChanged = 'wsch';

/** @brief Posted when the Apply button is clicked. */
const uint32 kMsgSaveSettings = 'svst';
/** @brief Posted when the Revert button is clicked. */
const uint32 kMsgRevertSettings = 'rvst';
/** @brief Reserved for future cancel-without-saving flow. */
const uint32 kMsgCancelSettings = 'cnst';



/**
 * @brief Constructs a listview row for one of the four item types tied to
 *        @a account.
 *
 * @param label    Visible text for the row.
 * @param account  Backing settings; not owned, shared with siblings.
 * @param type     Discriminator used by the parent window when this row
 *                 is selected.
 */
AccountItem::AccountItem(const char* label, BMailAccountSettings* account,
	item_types type)
	:
	BStringItem(label),
	fAccount(account),
	fType(type)
{
}


/**
 * @brief Forces the bold system font for account-header rows so they read
 *        as section titles.
 *
 * @param owner  Listview that owns this item.
 * @param font   Font selected by the listview; replaced with
 *               @c be_bold_font for ACCOUNT_ITEM rows.
 */
void
AccountItem::Update(BView* owner, const BFont* font)
{
	if (fType == ACCOUNT_ITEM)
		font = be_bold_font;

	BStringItem::Update(owner, font);
}


/**
 * @brief Draws the row, swapping in @c be_bold_font for account-header
 *        rows around the inherited BStringItem draw.
 *
 * @param owner     Listview that triggered the draw.
 * @param rect      Frame to paint into.
 * @param complete  Whether the listview wants the entire row redrawn.
 */
void
AccountItem::DrawItem(BView* owner, BRect rect, bool complete)
{
	owner->PushState();
	if (fType == ACCOUNT_ITEM)
		owner->SetFont(be_bold_font);

	BStringItem::DrawItem(owner, rect, complete);
	owner->PopState();
}


//	#pragma mark -


/**
 * @brief Single-selection BListView specialisation that turns DEL/BS into
 *        @c kMsgRemoveAccount and right-clicks into
 *        @c kMsgAccountsRightClicked targeted at the parent window.
 */
class AccountsListView : public BListView {
public:
	/**
	 * @brief Constructs an accounts listview that posts custom messages to
	 *        @a target.
	 *
	 * @param target  Handler that receives the right-click and key-driven
	 *                remove notifications.
	 */
	AccountsListView(BHandler* target)
		:
		BListView(NULL, B_SINGLE_SELECTION_LIST),
		fTarget(target)
	{
	}

	/**
	 * @brief Forwards Delete/Backspace as @c kMsgRemoveAccount before
	 *        delegating to BListView for normal navigation handling.
	 *
	 * @param bytes     UTF-8 bytes for the keystroke.
	 * @param numBytes  Length of @a bytes.
	 */
	void
	KeyDown(const char *bytes, int32 numBytes)
	{
		if (numBytes != 1)
			return;

		if ((*bytes == B_DELETE) || (*bytes == B_BACKSPACE))
			Window()->PostMessage(kMsgRemoveAccount);

		BListView::KeyDown(bytes,numBytes);
	}

	/**
	 * @brief Detects a right-click on a row and posts
	 *        @c kMsgAccountsRightClicked carrying the screen point and row
	 *        index.
	 *
	 * @param point  Mouse-down location in view coordinates.
	 */
	void
	MouseDown(BPoint point)
	{
		BListView::MouseDown(point);

		BPoint dummy;
		uint32 buttons;
		GetMouse(&dummy, &buttons);
		if (buttons != B_SECONDARY_MOUSE_BUTTON)
			return;

		int32 index = IndexOf(point);
		if (index < 0)
			return;

		BMessage message(kMsgAccountsRightClicked);
		ConvertToScreen(&point);
		message.AddPoint("point", point);
		message.AddInt32("index", index);
		BMessenger messenger(fTarget);
		messenger.SendMessage(&message);
	}

private:
			BHandler*			fTarget;
};


/**
 * @brief Tiny view that paints a single BBitmap with alpha-overlay
 *        compositing; used to render the app icon on the empty howto pane.
 */
class BitmapView : public BView {
	public:
		/**
		 * @brief Adopts @a bitmap and sizes the view to its bounds.
		 *
		 * @param bitmap  Bitmap to display; ownership is transferred to
		 *                this view.
		 */
		BitmapView(BBitmap *bitmap)
			:
			BView(NULL, B_WILL_DRAW)
		{
			fBitmap = bitmap;

			SetDrawingMode(B_OP_ALPHA);
			SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
			SetExplicitSize(bitmap->Bounds().Size());
		}

		/** @brief Releases the owned bitmap. */
		~BitmapView()
		{
			delete fBitmap;
		}

		/** @brief Adopts the parent panel colors so the view blends with
		           the surrounding pane. */
		virtual void AttachedToWindow()
		{
			AdoptParentColors();
		}

		/**
		 * @brief Draws the owned bitmap clipped to @a updateRect.
		 *
		 * @param updateRect  Region the system asks us to repaint.
		 */
		virtual void Draw(BRect updateRect)
		{
			DrawBitmap(fBitmap, updateRect, updateRect);
		}

	private:
		BBitmap *fBitmap;
};


//	#pragma mark -


/**
 * @brief Builds the two-tab layout (Accounts / Settings), loads the
 *        on-disk settings, and centers the window on screen.
 */
ConfigWindow::ConfigWindow()
	:
	BWindow(BRect(100, 100, 600, 540), B_TRANSLATE_SYSTEM_NAME("E-mail"),
		B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fLastSelectedAccount(NULL),
	fSaveSettings(false)
{
	BTabView* tabView = new BTabView("tab", B_WIDTH_FROM_WIDEST);
	tabView->SetBorder(B_NO_BORDER);

	// accounts listview

	BView* view = new BView("accounts", 0);
	tabView->AddTab(view);
	tabView->TabAt(0)->SetLabel(B_TRANSLATE("Accounts"));

	fAccountsListView = new AccountsListView(this);
	fAccountsListView->SetExplicitPreferredSize(BSize(
		fAccountsListView->StringWidth("W") * 22, B_SIZE_UNSET));

	BButton* addButton = new BButton(NULL, B_TRANSLATE("Add"),
		new BMessage(kMsgAddAccount));
	fRemoveButton = new BButton(NULL, B_TRANSLATE("Remove"),
		new BMessage(kMsgRemoveAccount));

	fConfigView = new BView(NULL, 0);
	fConfigView->SetLayout(new BGroupLayout(B_VERTICAL));
	fConfigView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	fConfigView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	BScrollView* scroller = new BScrollView(NULL, fAccountsListView, 0,
		false, true);

	BLayoutBuilder::Group<>(view, B_HORIZONTAL)
		.SetInsets(B_USE_WINDOW_SPACING, B_USE_WINDOW_SPACING,
			B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING)
		.AddGroup(B_VERTICAL)
			.Add(scroller)
			.AddGroup(B_HORIZONTAL)
				.Add(addButton)
				.Add(fRemoveButton)
			.End()
		.End()
		.Add(fConfigView, 2.0f);

	_ReplaceConfigView(_BuildHowToView());

	// general settings

	view = new BView("general", 0);
	view->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	tabView->AddTab(view);
	tabView->TabAt(1)->SetLabel(B_TRANSLATE("Settings"));

	fCheckMailCheckBox = new BCheckBox("check", B_TRANSLATE("Check every"),
		NULL);
	fIntervalControl = new BTextControl("time", B_TRANSLATE("minutes"), NULL,
		NULL);

	BPopUpMenu* statusPopUp = new BPopUpMenu(B_EMPTY_STRING);
	const char* statusModes[] = {
		B_TRANSLATE_COMMENT("Never", "show status window"),
		B_TRANSLATE("While sending"),
		B_TRANSLATE("While sending and receiving")};
	for (size_t i = 0; i < sizeof(statusModes) / sizeof(statusModes[0]); i++) {
		BMessage* msg = new BMessage(kMsgShowStatusWindowChanged);
		BMenuItem* item = new BMenuItem(statusModes[i], msg);
		statusPopUp->AddItem(item);
		msg->AddInt32("ShowStatusWindow", i);
	}

	fStatusModeField = new BMenuField("show status",
		B_TRANSLATE("Show notifications:"), statusPopUp);

	BMessage* msg = new BMessage(B_REFS_RECEIVED);
	BButton* editMenuButton = new BButton(B_EMPTY_STRING,
		B_TRANSLATE("Edit mailbox menu…"), msg);
	editMenuButton->SetTarget(BMessenger("application/x-vnd.Be-TRAK"));

	BPath path;
	find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	path.Append("Mail/Menu Links");
	BEntry entry(path.Path());
	if (entry.InitCheck() == B_OK && entry.Exists()) {
		entry_ref ref;
		entry.GetRef(&ref);
		msg->AddRef("refs", &ref);
	} else
		editMenuButton->SetEnabled(false);

	BLayoutBuilder::Group<>(view, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING, B_USE_WINDOW_SPACING,
			B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING)
//		.AddGlue()
		.AddGroup(B_HORIZONTAL, 0.f)
			.AddGlue()
			.Add(fCheckMailCheckBox)
			.AddStrut(be_control_look->DefaultLabelSpacing())
			.Add(fIntervalControl->CreateTextViewLayoutItem())
			.AddStrut(be_control_look->DefaultLabelSpacing())
			.Add(fIntervalControl->CreateLabelLayoutItem())
			.AddGlue()
		.End()
		.AddGroup(B_HORIZONTAL, 0.f)
			.AddGlue()
			.Add(fStatusModeField->CreateLabelLayoutItem())
			.Add(fStatusModeField->CreateMenuBarLayoutItem())
			.AddGlue()
		.End()
		.Add(editMenuButton)
		.AddGlue();

	// save/revert buttons

	BButton* applyButton = new BButton("apply", B_TRANSLATE("Apply"),
		new BMessage(kMsgSaveSettings));
	BButton* revertButton = new BButton("revert", B_TRANSLATE("Revert"),
		new BMessage(kMsgRevertSettings));

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0, B_USE_DEFAULT_SPACING, 0, B_USE_WINDOW_SPACING)
		.Add(tabView)
		.Add(new BSeparatorView(B_HORIZONTAL))
		.AddGroup(B_HORIZONTAL, 0)
			.Add(revertButton)
			.AddGlue()
			.Add(applyButton)
			.SetInsets(B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING,
				B_USE_WINDOW_SPACING, 0);

	_LoadSettings();

	fAccountsListView->SetSelectionMessage(new BMessage(kMsgAccountSelected));
	fAccountsListView->MakeFocus(true);

	ResizeToPreferred();
	CenterOnScreen();
}


/**
 * @brief Drains the in-memory account lists, releasing all owned
 *        BMailAccountSettings objects.
 *
 * @note Anything still in @c fToDeleteAccounts is freed here even though
 *       its on-disk file was already removed during the most recent save.
 */
ConfigWindow::~ConfigWindow()
{
	while (fAccounts.CountItems() > 0)
		_RemoveAccount(fAccounts.ItemAt(0));
	for (int32 i = 0; i < fToDeleteAccounts.CountItems(); i++)
		delete fToDeleteAccounts.ItemAt(i);
}


/**
 * @brief Builds the placeholder pane shown on the right when no row is
 *        selected: app icon plus translated how-to text.
 *
 * @return New BView with a BTextView and an optional BitmapView; caller
 *         takes ownership when adding it to the layout.
 */
BView*
ConfigWindow::_BuildHowToView()
{
	BView* groupView = new BView("howTo", 0);

	BitmapView* bitmapView = NULL;
	app_info info;
	if (be_app->GetAppInfo(&info) == B_OK) {
		BFile appFile(&info.ref, B_READ_ONLY);
		BAppFileInfo appFileInfo(&appFile);
		if (appFileInfo.InitCheck() == B_OK) {
			BBitmap* bitmap = new (std::nothrow) BBitmap(BRect(0, 0, 63, 63),
				B_RGBA32);
			if (appFileInfo.GetIcon(bitmap, B_LARGE_ICON) == B_OK)
				bitmapView = new BitmapView(bitmap);
			else
				delete bitmap;
		}
	}

	fHowToTextView = new BTextView(NULL, B_WILL_DRAW);
	fHowToTextView->SetAlignment(B_ALIGN_CENTER);
	fHowToTextView->SetText(B_TRANSLATE(
		"Create a new account with the Add button.\n\n"
		"Remove an account with the Remove button on the selected item.\n\n"
		"Select an item in the list to change its settings."));

	fHowToTextView->MakeEditable(false);
	fHowToTextView->MakeSelectable(false);

	BFont font(be_plain_font);
	float fontFactor = font.Size() / 12.0f;

	fHowToTextView->SetExplicitPreferredSize(
		BSize(300 * fontFactor,400 * fontFactor));

	rgb_color textColor = ui_color(B_PANEL_TEXT_COLOR);
	fHowToTextView->SetFontAndColor(&font, B_FONT_ALL, &textColor);

	BLayoutBuilder::Group<>(groupView, B_VERTICAL)
		.AddGlue()
		.Add(fHowToTextView)
		.AddGlue();

	if (bitmapView != NULL)
		groupView->GetLayout()->AddView(1, bitmapView);

	fHowToTextView->AdoptSystemColors();

	return groupView;
}


/**
 * @brief Reloads accounts and general settings from disk, replacing any
 *        unsaved in-memory state.
 *
 * @note Errors retrieving general settings are reported on stderr but do
 *       not abort the load.
 */
void
ConfigWindow::_LoadSettings()
{
	// load accounts
	for (int i = 0; i < fAccounts.CountItems(); i++)
		delete fAccounts.ItemAt(i);
	fAccounts.MakeEmpty();

	_LoadAccounts();

	// load in general settings
	BMailSettings settings;
	status_t status = _SetToGeneralSettings(&settings);
	if (status != B_OK) {
		fprintf(stderr, B_TRANSLATE("Error retrieving general settings: %s\n"),
			strerror(status));
	}
}


/**
 * @brief Snapshots the current on-disk BMailAccounts into @c fAccounts as
 *        editable copies and inserts the corresponding rows into the
 *        listview.
 */
void
ConfigWindow::_LoadAccounts()
{
	BMailAccounts accounts;
	for (int32 i = 0; i < accounts.CountAccounts(); i++)
		fAccounts.AddItem(new BMailAccountSettings(*accounts.AccountAt(i)));

	for (int i = 0; i < fAccounts.CountItems(); i++) {
		BMailAccountSettings* account = fAccounts.ItemAt(i);
		_AddAccountToView(account);
	}
}


/**
 * @brief Writes account and general settings to disk, deletes any
 *        accounts the user removed, and notifies the running mail_daemon.
 *
 * Honours @c fSaveSettings: when @c false the function still cleans up
 * deleted accounts and notifies the daemon, but it skips the actual
 * writes. Starts or stops the daemon to match the new auto-start flag.
 */
void
ConfigWindow::_SaveSettings()
{
	// collect changed accounts
	BMessage changedAccounts(BPrivate::kMsgAccountsChanged);
	for (int32 i = 0; i < fAccounts.CountItems(); i++) {
		BMailAccountSettings* account = fAccounts.ItemAt(i);
		if (account && account->HasBeenModified())
			changedAccounts.AddInt32("account", account->AccountID());
	}
	for (int32 i = 0; i < fToDeleteAccounts.CountItems(); i++) {
		BMailAccountSettings* account = fToDeleteAccounts.ItemAt(i);
		changedAccounts.AddInt32("account", account->AccountID());
	}

	// cleanup account directory
	for (int32 i = 0; i < fToDeleteAccounts.CountItems(); i++) {
		BMailAccountSettings* account = fToDeleteAccounts.ItemAt(i);
		BEntry entry(account->AccountFile());
		entry.Remove();
		delete account;
	}
	fToDeleteAccounts.MakeEmpty();

	// Apply and save general settings

	BMailSettings settings;
	if (fSaveSettings) {
		bigtime_t interval = 0;
		if (fCheckMailCheckBox->Value() == B_CONTROL_ON) {
			// figure out time interval
			float floatInterval;
			sscanf(fIntervalControl->Text(), "%f", &floatInterval);
			interval = bigtime_t(60000000L * floatInterval);
		}

		settings.SetAutoCheckInterval(interval);
		settings.SetDaemonAutoStarts(!fAccounts.IsEmpty());

		// status mode (alway, fetching/retrieving, ...)
		int32 index = fStatusModeField->Menu()->IndexOf(
			fStatusModeField->Menu()->FindMarked());
		settings.SetShowStatusWindow(index);

		settings.Save();
	}

	// Save accounts

	if (fSaveSettings) {
		for (int i = 0; i < fAccounts.CountItems(); i++)
			fAccounts.ItemAt(i)->Save();
	}

	BMessenger messenger(B_MAIL_DAEMON_SIGNATURE);
	if (messenger.IsValid()) {
		// server should reload general settings
		messenger.SendMessage(BPrivate::kMsgSettingsUpdated);
		// notify server about changed accounts
		messenger.SendMessage(&changedAccounts);
	}

	// Start/stop the mail_daemon depending on the settings
	BMailDaemon daemon;
	if (fSaveSettings) {
		if (settings.DaemonAutoStarts() && !daemon.IsRunning())
			daemon.Launch();
		else if (!settings.DaemonAutoStarts() && daemon.IsRunning())
			daemon.Quit();
	}
}


/**
 * @brief Saves any pending changes and quits the BApplication.
 *
 * @return Always @c true; the close is unconditional.
 */
bool
ConfigWindow::QuitRequested()
{
	_SaveSettings();

	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


/**
 * @brief Routes BMessages from the listview, the toolbar buttons, and the
 *        general-settings tab into the appropriate slots.
 *
 * Notable behaviours: the right-click handler intentionally falls through
 * to the selection handler so popping the context menu also moves the
 * selection; @c kMsgSaveSettings flips @c fSaveSettings before delegating
 * to _SaveSettings(); B_COLORS_UPDATED retints the howto text view live.
 *
 * @param msg  Incoming BMessage.
 */
void
ConfigWindow::MessageReceived(BMessage *msg)
{
	float fontFactor = be_plain_font->Size() / 12.0f;
	BRect autoConfigRect(0, 0, 400 * fontFactor, 300 * fontFactor);
	BRect frame;

	AutoConfigWindow *autoConfigWindow = NULL;
	switch (msg->what) {
		case B_COLORS_UPDATED:
		{
			rgb_color textColor;
			if (msg->FindColor(ui_color_name(B_PANEL_TEXT_COLOR), &textColor)
					== B_OK) {
				BFont font;
				fHowToTextView->SetFontAndColor(&font, 0, &textColor);
			}
			break;
		}

		case kMsgAccountsRightClicked:
		{
			BPoint point;
			msg->FindPoint("point", &point);
			int32 index = msg->FindInt32("index");
			AccountItem* clickedItem = dynamic_cast<AccountItem*>(
				fAccountsListView->ItemAt(index));
			if (clickedItem == NULL || clickedItem->Type() != ACCOUNT_ITEM)
				break;

			BPopUpMenu rightClickMenu("accounts", false, false);

			BMenuItem* inMenuItem = new BMenuItem(B_TRANSLATE("Incoming"),
				NULL);
			BMenuItem* outMenuItem = new BMenuItem(B_TRANSLATE("Outgoing"),
				NULL);
			rightClickMenu.AddItem(inMenuItem);
			rightClickMenu.AddItem(outMenuItem);

			BMailAccountSettings* settings = clickedItem->Account();
			if (settings->IsInboundEnabled())
				inMenuItem->SetMarked(true);
			if (settings->IsOutboundEnabled())
				outMenuItem->SetMarked(true);

			BMenuItem* selectedItem = rightClickMenu.Go(point);
			if (selectedItem == NULL)
				break;
			if (selectedItem == inMenuItem) {
				AccountItem* item = dynamic_cast<AccountItem*>(
					fAccountsListView->ItemAt(index + 1));
				if (item == NULL)
					break;
				if (settings->IsInboundEnabled()) {
					settings->SetInboundEnabled(false);
					item->SetEnabled(false);
				} else {
					settings->SetInboundEnabled(true);
					item->SetEnabled(true);
				}
			} else {
				AccountItem* item = dynamic_cast<AccountItem*>(
					fAccountsListView->ItemAt(index + 2));
				if (item == NULL)
					break;
				if (settings->IsOutboundEnabled()) {
					settings->SetOutboundEnabled(false);
					item->SetEnabled(false);
				} else {
					settings->SetOutboundEnabled(true);
					item->SetEnabled(true);
				}
			}
		}

		case kMsgAccountSelected:
		{
			int32 index;
			if (msg->FindInt32("index", &index) != B_OK || index < 0) {
				// deselect current item
				_ReplaceConfigView(_BuildHowToView());
				break;
			}
			AccountItem* item = (AccountItem*)fAccountsListView->ItemAt(index);
			if (item != NULL)
				_AccountSelected(item);
			break;
		}

		case kMsgAddAccount:
		{
			frame = Frame();
			autoConfigRect.OffsetTo(
				frame.left + (frame.Width() - autoConfigRect.Width()) / 2,
				frame.top + (frame.Width() - autoConfigRect.Height()) / 2);
			autoConfigWindow = new AutoConfigWindow(autoConfigRect, this);
			autoConfigWindow->Show();
			break;
		}

		case kMsgRemoveAccount:
		{
			int32 index = fAccountsListView->CurrentSelection();
			if (index >= 0) {
				AccountItem *item = (AccountItem *)fAccountsListView->ItemAt(
					index);
				if (item != NULL) {
					_RemoveAccount(item->Account());
					_ReplaceConfigView(_BuildHowToView());
				}
			}
			break;
		}

		case kMsgIntervalUnitChanged:
		{
			int32 index;
			if (msg->FindInt32("index",&index) == B_OK)
				fIntervalControl->SetEnabled(index != 0);
			break;
		}

		case kMsgShowStatusWindowChanged:
		{
			// the status window stuff is the only "live" setting
			BMessenger messenger("application/x-vnd.Be-POST");
			if (messenger.IsValid())
				messenger.SendMessage(msg);
			break;
		}

		case kMsgRevertSettings:
			_RevertToLastSettings();
			break;

		case kMsgSaveSettings:
			fSaveSettings = true;
			_SaveSettings();
			AccountUpdated(fLastSelectedAccount);
			_ReplaceConfigView(_BuildHowToView());
			fAccountsListView->DeselectAll();
			break;

		default:
			BWindow::MessageReceived(msg);
			break;
	}
}


/**
 * @brief Creates a fresh BMailAccountSettings, registers it with this
 *        window, and inserts the listview rows for it.
 *
 * Used by the AutoConfigWindow wizard once the user clicks Finish.
 *
 * @return The newly created account, or @c NULL on allocation failure.
 *         Ownership stays with this window.
 */
BMailAccountSettings*
ConfigWindow::AddAccount()
{
	BMailAccountSettings* account = new BMailAccountSettings;
	if (!account)
		return NULL;
	fAccounts.AddItem(account);
	_AddAccountToView(account);
	return account;
}


/**
 * @brief Refreshes the listview header label for @a account so name edits
 *        on the detail pane are reflected immediately.
 *
 * @param account  Account whose label needs to be repainted; @c NULL is a
 *                 no-op.
 */
void
ConfigWindow::AccountUpdated(BMailAccountSettings* account)
{
	if (account == NULL)
		return;

	for (int i = 0; i < fAccountsListView->CountItems(); i++) {
		AccountItem* item = (AccountItem*)fAccountsListView->ItemAt(i);
		if (item->Account() == account) {
			if (item->Type() == ACCOUNT_ITEM) {
				item->SetText(account->Name());
				fAccountsListView->Invalidate();
			}
		}
	}
}


/**
 * @brief Pulls general settings (auto-check interval, status-window mode)
 *        out of @a settings and reflects them in the Settings tab.
 *
 * Sends a synthetic @c kMsgShowStatusWindowChanged so the running
 * mail_daemon picks up the live update.
 *
 * @param settings  Source settings; must not be @c NULL.
 * @retval B_OK         All values applied.
 * @retval B_BAD_VALUE  @a settings was @c NULL.
 * @retval (other)      Whatever BMailSettings::InitCheck() returned.
 */
status_t
ConfigWindow::_SetToGeneralSettings(BMailSettings* settings)
{
	if (settings == NULL)
		return B_BAD_VALUE;

	status_t status = settings->InitCheck();
	if (status != B_OK)
		return status;

	// retrieval frequency
	uint32 interval = uint32(settings->AutoCheckInterval() / 60000000L);
	fCheckMailCheckBox->SetValue(interval != 0 ? B_CONTROL_ON : B_CONTROL_OFF);

	if (interval == 0)
		interval = 5;

	BString intervalText;
	intervalText.SetToFormat("%" B_PRIu32, interval);
	fIntervalControl->SetText(intervalText.String());

	int32 showStatusIndex = settings->ShowStatusWindow();
	BMenuItem* item = fStatusModeField->Menu()->ItemAt(showStatusIndex);
	if (item != NULL) {
		item->SetMarked(true);
		// send live update to the server by simulating a menu click
		BMessage msg(kMsgShowStatusWindowChanged);
		msg.AddInt32("ShowStatusWindow", showStatusIndex);
		PostMessage(&msg);
	}

	return B_OK;
}


/**
 * @brief Discards in-memory edits and reloads everything from disk.
 *
 * Pops an alert if the general settings could not be re-read; account
 * lists are always rebuilt from scratch.
 */
void
ConfigWindow::_RevertToLastSettings()
{
	// revert general settings
	BMailSettings settings;

	status_t status = _SetToGeneralSettings(&settings);
	if (status != B_OK) {
		char text[256];
		sprintf(text, B_TRANSLATE(
				"\nThe general settings couldn't be reverted.\n\n"
				"Error retrieving general settings:\n%s\n"),
			strerror(status));
		BAlert* alert = new BAlert(B_TRANSLATE("Error"), text,
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go();
	}

	// revert account data

	if (fAccountsListView->CurrentSelection() != -1)
		_ReplaceConfigView(_BuildHowToView());

	for (int32 i = 0; i < fAccounts.CountItems(); i++) {
		BMailAccountSettings* account = fAccounts.ItemAt(i);
		_RemoveAccountFromListView(account);
		delete account;
	}

	fAccounts.MakeEmpty();
	_LoadAccounts();
}


/**
 * @brief Appends the four child rows for @a account (header, inbound,
 *        outbound, filters) to the listview.
 *
 * Inbound and outbound rows are disabled when their direction is turned
 * off in the account settings.
 *
 * @param account  Account to surface in the listview.
 */
void
ConfigWindow::_AddAccountToView(BMailAccountSettings* account)
{
	BString label;
	label << account->Name();

	AccountItem* item;
	item = new AccountItem(label, account, ACCOUNT_ITEM);
	fAccountsListView->AddItem(item);

	item = new AccountItem(B_TRANSLATE("\t\t· Incoming"), account, INBOUND_ITEM);
	fAccountsListView->AddItem(item);
	if (!account->IsInboundEnabled())
		item->SetEnabled(false);

	item = new AccountItem(B_TRANSLATE("\t\t· Outgoing"), account,
		OUTBOUND_ITEM);
	fAccountsListView->AddItem(item);
	if (!account->IsOutboundEnabled())
		item->SetEnabled(false);

	item = new AccountItem(B_TRANSLATE("\t\t· E-mail filters"), account,
		FILTER_ITEM);
	fAccountsListView->AddItem(item);
}


/**
 * @brief Removes @a account from the visible list and queues it for
 *        deletion at the next save.
 *
 * The account is moved to @c fToDeleteAccounts so its on-disk file can be
 * unlinked when the user applies changes.
 *
 * @param account  Account to remove; ownership transfers to
 *                 @c fToDeleteAccounts.
 */
void
ConfigWindow::_RemoveAccount(BMailAccountSettings* account)
{
	_RemoveAccountFromListView(account);
	fAccounts.RemoveItem(account);
	fToDeleteAccounts.AddItem(account);
}


/**
 * @brief Strips every listview row that points at @a account and clears
 *        the detail pane if the account was being shown.
 *
 * @param account  Account whose rows should be deleted.
 */
void
ConfigWindow::_RemoveAccountFromListView(BMailAccountSettings* account)
{
	if (fLastSelectedAccount == account) {
		_ReplaceConfigView(_BuildHowToView());
		fLastSelectedAccount = NULL;
	}

	for (int i = fAccountsListView->CountItems(); i-- > 0;) {
		AccountItem* item = (AccountItem*)fAccountsListView->ItemAt(i);
		if (item->Account() == account) {
			fAccountsListView->RemoveItem(i);
			delete item;
		}
	}
}


/**
 * @brief Switches the right-hand pane to match @a item's row type.
 *
 * Saves any pending changes on the previously selected account first by
 * calling AccountUpdated() before swapping in the new pane.
 *
 * @param item  Listview row that became selected.
 */
void
ConfigWindow::_AccountSelected(AccountItem* item)
{
	AccountUpdated(fLastSelectedAccount);

	BMailAccountSettings* account = item->Account();
	fLastSelectedAccount = account;

	BView* view = NULL;
	switch (item->Type()) {
		case ACCOUNT_ITEM:
			view = new AccountConfigView(account);
			break;

		case INBOUND_ITEM:
			view = new ProtocolSettingsView(account->InboundAddOnRef(),
				*account, account->InboundSettings());
			break;

		case OUTBOUND_ITEM:
			view = new ProtocolSettingsView(account->OutboundAddOnRef(),
				*account, account->OutboundSettings());
			break;

		case FILTER_ITEM:
			view = new FiltersConfigView(*account);
			break;
	}

	_ReplaceConfigView(view);
}


/**
 * @brief Tears down whatever pane currently fills the detail area and
 *        installs @a view in its place.
 *
 * @param view  New pane; @c NULL is permitted and leaves the area empty.
 *              Ownership is transferred to the layout.
 */
void
ConfigWindow::_ReplaceConfigView(BView* view)
{
	while (BView* child = fConfigView->ChildAt(0)) {
		fConfigView->RemoveChild(child);
		delete child;
	}

	if (view != NULL)
		fConfigView->AddChild(view);
}
