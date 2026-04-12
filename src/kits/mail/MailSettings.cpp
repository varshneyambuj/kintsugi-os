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
 *   Copyright 2004-2015, Haiku Inc. All rights reserved.
 *   Copyright 2001-2003 Dr. Zoidberg Enterprises. All rights reserved.
 *
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file MailSettings.cpp
 * @brief Mail daemon and account configuration classes.
 *
 * Implements BMailSettings (global daemon settings such as auto-check interval
 * and status window preferences), BMailAccounts (an enumeration of all
 * configured accounts sorted by creation time), BMailAddOnSettings (per-filter
 * persistent settings backed by a BMessage), BMailProtocolSettings (protocol
 * settings including a list of filter settings), and BMailAccountSettings
 * (a complete account including inbound/outbound protocol and filter settings,
 * real name, and return address). Settings are stored as flattened BMessages
 * under the user settings directory.
 *
 * @see BMailDaemon, BMailProtocol, BMailFilter
 */


#include <MailSettings.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <MailDaemon.h>
#include <Message.h>
#include <Messenger.h>
#include <Path.h>
#include <PathFinder.h>
#include <String.h>
#include <Window.h>

#include <MailPrivate.h>


//	#pragma mark - BMailSettings


/**
 * @brief Constructs a BMailSettings and loads the current settings from disk.
 */
BMailSettings::BMailSettings()
{
	Reload();
}


/**
 * @brief Destroys the BMailSettings.
 */
BMailSettings::~BMailSettings()
{
}


/**
 * @brief Returns the initialisation status (always B_OK).
 *
 * @return B_OK always.
 */
status_t
BMailSettings::InitCheck() const
{
	return B_OK;
}


/**
 * @brief Saves the current settings to the user settings directory and notifies the daemon.
 *
 * Writes the flattened settings BMessage to Mail/new_mail_daemon, then sends
 * kMsgSettingsUpdated to the mail daemon so it reloads its configuration.
 *
 * @return B_OK on success, or a file-system error code on failure.
 */
status_t
BMailSettings::Save()
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK) {
		fprintf(stderr, "Couldn't find user settings directory: %s\n",
			strerror(status));
		return status;
	}

	path.Append("Mail");

	status = BPrivate::WriteMessageFile(fData, path, "new_mail_daemon");
	if (status != B_OK)
		return status;

	BMessenger(B_MAIL_DAEMON_SIGNATURE).SendMessage(
		BPrivate::kMsgSettingsUpdated);
	return B_OK;
}


/**
 * @brief Reloads settings from the most-specific available settings directory.
 *
 * Searches B_USER_SETTINGS_DIRECTORY then B_SYSTEM_SETTINGS_DIRECTORY in
 * order, reading the first Mail/new_mail_daemon file found.
 *
 * @return B_OK on success, or B_ENTRY_NOT_FOUND if no settings file exists.
 */
status_t
BMailSettings::Reload()
{
	// Try directories from most specific to least
	directory_which which[] = {
		B_USER_SETTINGS_DIRECTORY,
		B_SYSTEM_SETTINGS_DIRECTORY};
	status_t status = B_ENTRY_NOT_FOUND;

	for (size_t i = 0; i < sizeof(which) / sizeof(which[0]); i++) {
		BPath path;
		status = find_directory(which[i], &path);
		if (status != B_OK)
			continue;

		path.Append("Mail/new_mail_daemon");
		BFile file;
		status = file.SetTo(path.Path(), B_READ_ONLY);
		if (status != B_OK)
			continue;

		// read settings
		BMessage settings;
		status = settings.Unflatten(&file);
		if (status != B_OK) {
			fprintf(stderr, "Couldn't read settings from '%s': %s\n",
				path.Path(), strerror(status));
			continue;
		}

		// clobber old settings
		fData = settings;
		return B_OK;
	}

	return status;
}


//	# pragma mark - Global settings


/**
 * @brief Returns which screen corner the status window should follow.
 *
 * @return Corner constant stored in the settings BMessage.
 */
int32
BMailSettings::WindowFollowsCorner()
{
	return fData.FindInt32("WindowFollowsCorner");
}


/**
 * @brief Sets which screen corner the status window should follow.
 *
 * @param whichCorner  One of the B_FOLLOW_* constants.
 */
void
BMailSettings::SetWindowFollowsCorner(int32 whichCorner)
{
	if (fData.ReplaceInt32("WindowFollowsCorner", whichCorner) != B_OK)
		fData.AddInt32("WindowFollowsCorner", whichCorner);
}


/**
 * @brief Returns when the status window should be shown.
 *
 * @return 0 = never, 1 = always, 2 = during send and receive (default).
 */
uint32
BMailSettings::ShowStatusWindow()
{
	int32 showStatusWindow;
	if (fData.FindInt32("ShowStatusWindow", &showStatusWindow) != B_OK) {
		// show during send and receive
		return 2;
	}

	return showStatusWindow;
}


/**
 * @brief Sets when the status window should be shown.
 *
 * @param mode  0 = never, 1 = always, 2 = during send and receive.
 */
void
BMailSettings::SetShowStatusWindow(uint32 mode)
{
	if (fData.ReplaceInt32("ShowStatusWindow", mode) != B_OK)
		fData.AddInt32("ShowStatusWindow", mode);
}


/**
 * @brief Returns true if the mail daemon should start automatically at login.
 *
 * @return true if auto-start is enabled.
 */
bool
BMailSettings::DaemonAutoStarts()
{
	return fData.FindBool("DaemonAutoStarts");
}


/**
 * @brief Enables or disables automatic daemon start at login.
 *
 * @param startIt  true to enable auto-start.
 */
void
BMailSettings::SetDaemonAutoStarts(bool startIt)
{
	if (fData.ReplaceBool("DaemonAutoStarts", startIt) != B_OK)
		fData.AddBool("DaemonAutoStarts", startIt);
}


/**
 * @brief Returns the saved frame rectangle for the mail configuration window.
 *
 * @return Saved BRect; zero-sized if not previously stored.
 */
BRect
BMailSettings::ConfigWindowFrame()
{
	return fData.FindRect("ConfigWindowFrame");
}


/**
 * @brief Saves the frame rectangle for the mail configuration window.
 *
 * @param frame  BRect to store.
 */
void
BMailSettings::SetConfigWindowFrame(BRect frame)
{
	if (fData.ReplaceRect("ConfigWindowFrame", frame) != B_OK)
		fData.AddRect("ConfigWindowFrame", frame);
}


/**
 * @brief Returns the saved frame rectangle for the mail status window.
 *
 * @return Saved BRect, or BRect(100,100,200,120) if not previously stored.
 */
BRect
BMailSettings::StatusWindowFrame()
{
	BRect frame;
	if (fData.FindRect("StatusWindowFrame", &frame) != B_OK)
		return BRect(100, 100, 200, 120);

	return frame;
}


/**
 * @brief Saves the frame rectangle for the mail status window.
 *
 * @param frame  BRect to store.
 */
void
BMailSettings::SetStatusWindowFrame(BRect frame)
{
	if (fData.ReplaceRect("StatusWindowFrame", frame) != B_OK)
		fData.AddRect("StatusWindowFrame", frame);
}


/**
 * @brief Returns the workspace mask for the status window.
 *
 * @return Workspace mask; defaults to B_ALL_WORKSPACES if not stored.
 */
int32
BMailSettings::StatusWindowWorkspaces()
{
	uint32 workspaces;
	if (fData.FindInt32("StatusWindowWorkSpace", (int32*)&workspaces) != B_OK)
		return B_ALL_WORKSPACES;

	return workspaces;
}


/**
 * @brief Sets the workspace mask for the status window and notifies the daemon.
 *
 * @param workspace  Workspace mask to store and send to the running daemon.
 */
void
BMailSettings::SetStatusWindowWorkspaces(int32 workspace)
{
	if (fData.ReplaceInt32("StatusWindowWorkSpace", workspace) != B_OK)
		fData.AddInt32("StatusWindowWorkSpace", workspace);

	BMessage msg('wsch');
	msg.AddInt32("StatusWindowWorkSpace",workspace);
	BMessenger(B_MAIL_DAEMON_SIGNATURE).SendMessage(&msg);
}


/**
 * @brief Returns the window-look constant for the status window.
 *
 * @return One of the window look constants stored in the settings.
 */
int32
BMailSettings::StatusWindowLook()
{
	return fData.FindInt32("StatusWindowLook");
}


/**
 * @brief Sets the window-look for the status window and notifies the daemon.
 *
 * @param look  Window look constant to apply.
 */
void
BMailSettings::SetStatusWindowLook(int32 look)
{
	if (fData.ReplaceInt32("StatusWindowLook", look) != B_OK)
		fData.AddInt32("StatusWindowLook", look);

	BMessage msg('lkch');
	msg.AddInt32("StatusWindowLook", look);
	BMessenger(B_MAIL_DAEMON_SIGNATURE).SendMessage(&msg);
}


/**
 * @brief Returns the auto-check interval in microseconds.
 *
 * @return Interval in microseconds; defaults to 5 minutes if not stored.
 */
bigtime_t
BMailSettings::AutoCheckInterval()
{
	bigtime_t value;
	if (fData.FindInt64("AutoCheckInterval", &value) != B_OK) {
		// every 5 min
		return 5 * 60 * 1000 * 1000;
	}
	return value;
}


/**
 * @brief Sets the auto-check interval.
 *
 * @param interval  Check interval in microseconds.
 */
void
BMailSettings::SetAutoCheckInterval(bigtime_t interval)
{
	if (fData.ReplaceInt64("AutoCheckInterval", interval) != B_OK)
		fData.AddInt64("AutoCheckInterval", interval);
}


/**
 * @brief Returns true if mail should only be checked when a PPP connection is up.
 *
 * @return true if PPP-only checking is enabled.
 */
bool
BMailSettings::CheckOnlyIfPPPUp()
{
	return fData.FindBool("CheckOnlyIfPPPUp");
}


/**
 * @brief Enables or disables PPP-gated mail checking.
 *
 * @param yes  true to check only when PPP is up.
 */
void
BMailSettings::SetCheckOnlyIfPPPUp(bool yes)
{
	if (fData.ReplaceBool("CheckOnlyIfPPPUp", yes))
		fData.AddBool("CheckOnlyIfPPPUp", yes);
}


/**
 * @brief Returns true if mail should only be sent when a PPP connection is up.
 *
 * @return true if PPP-gated sending is enabled.
 */
bool
BMailSettings::SendOnlyIfPPPUp()
{
	return fData.FindBool("SendOnlyIfPPPUp");
}


/**
 * @brief Enables or disables PPP-gated mail sending.
 *
 * @param yes  true to send only when PPP is up.
 */
void
BMailSettings::SetSendOnlyIfPPPUp(bool yes)
{
	if (fData.ReplaceBool("SendOnlyIfPPPUp", yes))
		fData.AddBool("SendOnlyIfPPPUp", yes);
}


/**
 * @brief Returns the account ID of the default outbound account.
 *
 * @return Numeric account ID.
 */
int32
BMailSettings::DefaultOutboundAccount()
{
	return fData.FindInt32("DefaultOutboundAccount");
}


/**
 * @brief Sets the default outbound account by ID.
 *
 * @param to  Numeric account ID to use as the default outbound account.
 */
void
BMailSettings::SetDefaultOutboundAccount(int32 to)
{
	if (fData.ReplaceInt32("DefaultOutboundAccount", to) != B_OK)
		fData.AddInt32("DefaultOutboundAccount", to);
}


// #pragma mark -


/**
 * @brief Constructs a BMailAccounts and loads all account settings sorted by creation time.
 *
 * Reads every entry from the Mail/accounts settings directory and inserts
 * valid accounts into fAccounts in ascending creation-time order.
 */
BMailAccounts::BMailAccounts()
{
	BPath path;
	status_t status = AccountsPath(path);
	if (status != B_OK)
		return;

	BDirectory dir(path.Path());
	if (dir.InitCheck() != B_OK)
		return;

	std::vector<time_t> creationTimeList;
	BEntry entry;
	while (dir.GetNextEntry(&entry) != B_ENTRY_NOT_FOUND) {
		BNode node(&entry);
		time_t creationTime;
		if (node.GetCreationTime(&creationTime) != B_OK)
			continue;

		BMailAccountSettings* account = new BMailAccountSettings(entry);
		if (account->InitCheck() != B_OK) {
			delete account;
			continue;
		}

		// sort by creation time
		int insertIndex = -1;
		for (unsigned int i = 0; i < creationTimeList.size(); i++) {
			if (creationTimeList[i] > creationTime) {
				insertIndex = i;
				break;
			}
		}
		if (insertIndex < 0) {
			fAccounts.AddItem(account);
			creationTimeList.push_back(creationTime);
		} else {
			fAccounts.AddItem(account, insertIndex);
			creationTimeList.insert(creationTimeList.begin() + insertIndex,
				creationTime);
		}
	}
}


/**
 * @brief Resolves the path to the Mail/accounts settings directory.
 *
 * @param path  Output BPath set to the accounts directory.
 * @return B_OK on success, or an error from find_directory() or path.Append().
 */
status_t
BMailAccounts::AccountsPath(BPath& path)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;
	return path.Append("Mail/accounts");
}


/**
 * @brief Destroys the BMailAccounts, releasing all loaded account settings.
 */
BMailAccounts::~BMailAccounts()
{
	for (int i = 0; i < fAccounts.CountItems(); i++)
		delete fAccounts.ItemAt(i);
}


/**
 * @brief Returns the total number of loaded accounts.
 *
 * @return Account count.
 */
int32
BMailAccounts::CountAccounts()
{
	return fAccounts.CountItems();
}


/**
 * @brief Returns the account settings at the given index.
 *
 * @param index  Zero-based account index.
 * @return Pointer to BMailAccountSettings, or NULL if out of range.
 */
BMailAccountSettings*
BMailAccounts::AccountAt(int32 index)
{
	return fAccounts.ItemAt(index);
}


/**
 * @brief Finds an account by its numeric ID.
 *
 * @param id  Account ID to search for.
 * @return Pointer to the matching BMailAccountSettings, or NULL.
 */
BMailAccountSettings*
BMailAccounts::AccountByID(int32 id)
{
	for (int i = 0; i < fAccounts.CountItems(); i++) {
		BMailAccountSettings* account = fAccounts.ItemAt(i);
		if (account->AccountID() == id)
			return account;
	}
	return NULL;
}


/**
 * @brief Finds an account by its name string.
 *
 * @param name  Account name string to search for (case-sensitive).
 * @return Pointer to the matching BMailAccountSettings, or NULL.
 */
BMailAccountSettings*
BMailAccounts::AccountByName(const char* name)
{
	for (int i = 0; i < fAccounts.CountItems(); i++) {
		BMailAccountSettings* account = fAccounts.ItemAt(i);
		if (strcmp(account->Name(), name) == 0)
			return account;
	}
	return NULL;
}


// #pragma mark -


/**
 * @brief Default constructor — creates an empty BMailAddOnSettings.
 */
BMailAddOnSettings::BMailAddOnSettings()
{
}


/**
 * @brief Destroys the BMailAddOnSettings.
 */
BMailAddOnSettings::~BMailAddOnSettings()
{
}


/**
 * @brief Loads the add-on reference and settings from a BMessage.
 *
 * Resolves relative add-on paths against all mail_daemon add-on directories.
 * Populates this BMessage (which IS the settings) from the "settings"
 * sub-message in \a message.
 *
 * @param message  BMessage containing "add-on path" and optionally "settings".
 * @return B_OK on success, B_BAD_VALUE if "add-on path" is missing,
 *         B_ENTRY_NOT_FOUND if the add-on cannot be located.
 */
status_t
BMailAddOnSettings::Load(const BMessage& message)
{
	const char* pathString = NULL;
	if (message.FindString("add-on path", &pathString) != B_OK)
		return B_BAD_VALUE;

	BPath path(pathString);

	if (!path.IsAbsolute()) {
		BStringList paths;
		BPathFinder().FindPaths(B_FIND_PATH_ADD_ONS_DIRECTORY, "mail_daemon",
			paths);

		status_t status = B_ENTRY_NOT_FOUND;

		for (int32 i = 0; i < paths.CountStrings(); i++) {
			path.SetTo(paths.StringAt(i), pathString);
			BEntry entry(path.Path());
			if (entry.Exists()) {
				status = B_OK;
				break;
			}
		}
		if (status != B_OK)
			return status;
	}

	status_t status = get_ref_for_path(path.Path(), &fRef);
	if (status != B_OK)
		return status;

	BMessage settings;
	message.FindMessage("settings", &settings);

	MakeEmpty();
	Append(settings);

	fOriginalSettings = *this;
	fOriginalRef = fRef;
	return B_OK;
}


/**
 * @brief Saves the add-on reference and settings into a BMessage.
 *
 * Stores the path relative to the mail_daemon add-ons directory under
 * "add-on path" and this BMessage's contents under "settings".
 *
 * @param message  Output BMessage to receive the serialised settings.
 * @return B_OK on success, or a BMessage error code on failure.
 */
status_t
BMailAddOnSettings::Save(BMessage& message)
{
	BPath path(&fRef);
	status_t status = message.AddString("add-on path", _RelativizePath(path));
	if (status == B_OK)
		status = message.AddMessage("settings", this);
	if (status != B_OK)
		return status;

	fOriginalSettings = *this;
	fOriginalRef = fRef;
	return B_OK;
}


/**
 * @brief Sets the entry_ref of the add-on file associated with these settings.
 *
 * @param ref  entry_ref of the add-on shared library.
 */
void
BMailAddOnSettings::SetAddOnRef(const entry_ref& ref)
{
	fRef = ref;
}


/**
 * @brief Returns the entry_ref of the add-on file.
 *
 * @return Const reference to the add-on entry_ref.
 */
const entry_ref&
BMailAddOnSettings::AddOnRef() const
{
	return fRef;
}


/**
 * @brief Returns true if the settings have changed since the last Load() or Save().
 *
 * @return true if the ref or any settings field has been modified.
 */
bool
BMailAddOnSettings::HasBeenModified() const
{
	return fRef != fOriginalRef
		|| !fOriginalSettings.HasSameData(*this, true, true);
}


/**
 * @brief Strips the ".../add-ons/mail_daemon/" prefix from a path if present.
 *
 * Returns the substring after the last "mail_daemon/" component so that
 * add-on paths are stored as relative names, making them relocatable.
 *
 * @param path  Full path to potentially relativise.
 * @return Pointer into \a path.Path() after the prefix, or the full path string.
 */
const char*
BMailAddOnSettings::_RelativizePath(const BPath& path) const
{
	const char* string = path.Path();
	const char* parentDirectory = "/mail_daemon/";
	const char* at = strstr(string, parentDirectory);
	if (at == NULL)
		return string;

	return at + strlen(parentDirectory);
}


// #pragma mark -


/**
 * @brief Default constructor — creates an empty BMailProtocolSettings.
 */
BMailProtocolSettings::BMailProtocolSettings()
	:
	fFiltersSettings(5)
{
}


/**
 * @brief Destroys the BMailProtocolSettings.
 */
BMailProtocolSettings::~BMailProtocolSettings()
{
}


/**
 * @brief Loads protocol settings and all embedded filter settings from a BMessage.
 *
 * Calls the base class Load(), then iterates over "filters" sub-messages to
 * populate the filter settings list.
 *
 * @param message  BMessage containing the protocol and filter settings.
 * @return B_OK on success, B_BAD_VALUE if the filters field type is wrong.
 */
status_t
BMailProtocolSettings::Load(const BMessage& message)
{
	status_t status = BMailAddOnSettings::Load(message);
	if (status != B_OK)
		return status;

	type_code typeFound;
	int32 countFound;
	message.GetInfo("filters", &typeFound, &countFound);
	if (typeFound != B_MESSAGE_TYPE)
		return B_BAD_VALUE;

	for (int i = 0; i < countFound; i++) {
		int32 index = AddFilterSettings();
		if (index < 0)
			return B_NO_MEMORY;

		BMailAddOnSettings* filterSettings = fFiltersSettings.ItemAt(index);

		BMessage filterMessage;
		message.FindMessage("filters", i, &filterMessage);
		if (filterSettings->Load(filterMessage) != B_OK)
			RemoveFilterSettings(index);
	}
	return B_OK;
}


/**
 * @brief Saves protocol settings and all filter settings into a BMessage.
 *
 * Calls the base class Save(), then appends each filter's settings as
 * "filters" sub-messages.
 *
 * @param message  Output BMessage to receive the serialised settings.
 * @return B_OK on success, or an error code from the base class or BMessage.
 */
status_t
BMailProtocolSettings::Save(BMessage& message)
{
	status_t status = BMailAddOnSettings::Save(message);
	if (status != B_OK)
		return status;

	for (int i = 0; i < CountFilterSettings(); i++) {
		BMessage filter;
		BMailAddOnSettings* filterSettings = fFiltersSettings.ItemAt(i);
		filterSettings->Save(filter);
		message.AddMessage("filters", &filter);
	}
	return B_OK;
}


/**
 * @brief Returns the number of filter settings in this protocol's list.
 *
 * @return Filter settings count.
 */
int32
BMailProtocolSettings::CountFilterSettings() const
{
	return fFiltersSettings.CountItems();
}


/**
 * @brief Adds a new (empty) filter settings entry, optionally pre-seeded with a ref.
 *
 * @param ref  Optional entry_ref of the filter add-on, or NULL.
 * @return Zero-based index of the newly added entry, or -1 on allocation failure.
 */
int32
BMailProtocolSettings::AddFilterSettings(const entry_ref* ref)
{
	BMailAddOnSettings* filterSettings = new BMailAddOnSettings();
	if (ref != NULL)
		filterSettings->SetAddOnRef(*ref);

	if (fFiltersSettings.AddItem(filterSettings))
		return fFiltersSettings.CountItems() - 1;

	delete filterSettings;
	return -1;
}


/**
 * @brief Removes the filter settings at the given index.
 *
 * @param index  Zero-based index to remove.
 */
void
BMailProtocolSettings::RemoveFilterSettings(int32 index)
{
	fFiltersSettings.RemoveItemAt(index);
}


/**
 * @brief Moves a filter settings entry from one position to another.
 *
 * @param from  Source index.
 * @param to    Destination index.
 * @return true on success, false if either index is out of range.
 */
bool
BMailProtocolSettings::MoveFilterSettings(int32 from, int32 to)
{
	if (from < 0 || from >= (int32)CountFilterSettings() || to < 0
		|| to >= (int32)CountFilterSettings())
		return false;
	if (from == to)
		return true;

	BMailAddOnSettings* settings = fFiltersSettings.RemoveItemAt(from);
	fFiltersSettings.AddItem(settings, to);
	return true;
}


/**
 * @brief Returns the filter settings at the given index.
 *
 * @param index  Zero-based index.
 * @return Pointer to BMailAddOnSettings, or NULL if out of range.
 */
BMailAddOnSettings*
BMailProtocolSettings::FilterSettingsAt(int32 index) const
{
	return fFiltersSettings.ItemAt(index);
}


/**
 * @brief Returns true if the protocol or any filter settings have been modified.
 *
 * @return true if anything has changed since the last Load() or Save().
 */
bool
BMailProtocolSettings::HasBeenModified() const
{
	if (BMailAddOnSettings::HasBeenModified())
		return true;
	for (int32 i = 0; i < CountFilterSettings(); i++) {
		if (FilterSettingsAt(i)->HasBeenModified())
			return true;
	}
	return false;
}


//	#pragma mark -


/**
 * @brief Constructs a new BMailAccountSettings with a default account ID.
 *
 * Initialises inbound and outbound as enabled; the account ID is seeded from
 * the real-time clock to ensure uniqueness.
 */
BMailAccountSettings::BMailAccountSettings()
	:
	fStatus(B_OK),
	fInboundEnabled(true),
	fOutboundEnabled(true),
	fModified(true)
{
	fAccountID = real_time_clock();
}


/**
 * @brief Constructs a BMailAccountSettings by loading from an existing BEntry.
 *
 * @param account  BEntry pointing to the account settings file.
 */
BMailAccountSettings::BMailAccountSettings(BEntry account)
	:
	fAccountFile(account),
	fModified(false)
{
	fStatus = Reload();
}


/**
 * @brief Destroys the BMailAccountSettings.
 */
BMailAccountSettings::~BMailAccountSettings()
{

}


/**
 * @brief Sets the numeric account ID.
 *
 * @param id  New account ID; marks the settings as modified.
 */
void
BMailAccountSettings::SetAccountID(int32 id)
{
	fModified = true;
	fAccountID = id;
}


/**
 * @brief Returns the numeric account ID.
 *
 * @return Account ID integer.
 */
int32
BMailAccountSettings::AccountID() const
{
	return fAccountID;
}


/**
 * @brief Sets the account name (display name).
 *
 * @param name  Null-terminated account name string.
 */
void
BMailAccountSettings::SetName(const char* name)
{
	fModified = true;
	fAccountName = name;
}


/**
 * @brief Returns the account name.
 *
 * @return Null-terminated account name string.
 */
const char*
BMailAccountSettings::Name() const
{
	return fAccountName;
}


/**
 * @brief Sets the user's real name for the From header.
 *
 * @param realName  Null-terminated real name string.
 */
void
BMailAccountSettings::SetRealName(const char* realName)
{
	fModified = true;
	fRealName = realName;
}


/**
 * @brief Returns the user's real name.
 *
 * @return Null-terminated real name string.
 */
const char*
BMailAccountSettings::RealName() const
{
	return fRealName;
}


/**
 * @brief Sets the return (reply-to) e-mail address.
 *
 * @param returnAddress  Null-terminated e-mail address string.
 */
void
BMailAccountSettings::SetReturnAddress(const char* returnAddress)
{
	fModified = true;
	fReturnAdress = returnAddress;
}


/**
 * @brief Returns the return (reply-to) e-mail address.
 *
 * @return Null-terminated e-mail address string.
 */
const char*
BMailAccountSettings::ReturnAddress() const
{
	return fReturnAdress;
}


/**
 * @brief Sets the inbound protocol add-on by name, resolving the entry_ref.
 *
 * @param name  Add-on filename (e.g. "POP3") under mail_daemon/inbound_protocols.
 * @return true if the add-on was found and the ref was set successfully.
 */
bool
BMailAccountSettings::SetInboundAddOn(const char* name)
{
	entry_ref ref;
	if (_GetAddOnRef("mail_daemon/inbound_protocols", name, ref) != B_OK)
		return false;

	fInboundSettings.SetAddOnRef(ref);
	return true;
}


/**
 * @brief Sets the outbound protocol add-on by name, resolving the entry_ref.
 *
 * @param name  Add-on filename (e.g. "SMTP") under mail_daemon/outbound_protocols.
 * @return true if the add-on was found and the ref was set successfully.
 */
bool
BMailAccountSettings::SetOutboundAddOn(const char* name)
{
	entry_ref ref;
	if (_GetAddOnRef("mail_daemon/outbound_protocols", name, ref) != B_OK)
		return false;

	fOutboundSettings.SetAddOnRef(ref);
	return true;
}


/**
 * @brief Returns the entry_ref of the inbound protocol add-on.
 *
 * @return Const reference to the inbound add-on entry_ref.
 */
const entry_ref&
BMailAccountSettings::InboundAddOnRef() const
{
	return fInboundSettings.AddOnRef();
}


/**
 * @brief Returns the entry_ref of the outbound protocol add-on.
 *
 * @return Const reference to the outbound add-on entry_ref.
 */
const entry_ref&
BMailAccountSettings::OutboundAddOnRef() const
{
	return fOutboundSettings.AddOnRef();
}


/**
 * @brief Returns the inbound protocol settings (non-const).
 *
 * @return Reference to the inbound BMailProtocolSettings.
 */
BMailProtocolSettings&
BMailAccountSettings::InboundSettings()
{
	return fInboundSettings;
}


/**
 * @brief Returns the inbound protocol settings (const).
 *
 * @return Const reference to the inbound BMailProtocolSettings.
 */
const BMailProtocolSettings&
BMailAccountSettings::InboundSettings() const
{
	return fInboundSettings;
}


/**
 * @brief Returns the outbound protocol settings (non-const).
 *
 * @return Reference to the outbound BMailProtocolSettings.
 */
BMailProtocolSettings&
BMailAccountSettings::OutboundSettings()
{
	return fOutboundSettings;
}


/**
 * @brief Returns the outbound protocol settings (const).
 *
 * @return Const reference to the outbound BMailProtocolSettings.
 */
const BMailProtocolSettings&
BMailAccountSettings::OutboundSettings() const
{
	return fOutboundSettings;
}


/**
 * @brief Returns true if the inbound add-on file exists on disk.
 *
 * @return true if the inbound add-on ref points to an existing entry.
 */
bool
BMailAccountSettings::HasInbound()
{
	return BEntry(&fInboundSettings.AddOnRef()).Exists();
}


/**
 * @brief Returns true if the outbound add-on file exists on disk.
 *
 * @return true if the outbound add-on ref points to an existing entry.
 */
bool
BMailAccountSettings::HasOutbound()
{
	return BEntry(&fOutboundSettings.AddOnRef()).Exists();
}


/**
 * @brief Enables or disables the inbound (receiving) side of this account.
 *
 * @param enabled  true to enable inbound mail fetching.
 */
void
BMailAccountSettings::SetInboundEnabled(bool enabled)
{
	fInboundEnabled = enabled;
	fModified = true;
}


/**
 * @brief Returns true if inbound mail fetching is enabled for this account.
 *
 * @return true if inbound is enabled.
 */
bool
BMailAccountSettings::IsInboundEnabled() const
{
	return fInboundEnabled;
}


/**
 * @brief Enables or disables the outbound (sending) side of this account.
 *
 * @param enabled  true to enable outbound mail sending.
 */
void
BMailAccountSettings::SetOutboundEnabled(bool enabled)
{
	fOutboundEnabled = enabled;
	fModified = true;
}


/**
 * @brief Returns true if outbound mail sending is enabled for this account.
 *
 * @return true if outbound is enabled.
 */
bool
BMailAccountSettings::IsOutboundEnabled() const
{
	return fOutboundEnabled;
}


/**
 * @brief Reloads all account settings from the account file on disk.
 *
 * @return B_OK on success, or a file error code if the file cannot be read.
 */
status_t
BMailAccountSettings::Reload()
{
	BFile file(&fAccountFile, B_READ_ONLY);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;
	BMessage settings;
	settings.Unflatten(&file);

	int32 id;
	if (settings.FindInt32("id", &id) == B_OK)
		fAccountID = id;
	settings.FindString("name", &fAccountName);
	settings.FindString("real_name", &fRealName);
	settings.FindString("return_address", &fReturnAdress);

	BMessage inboundSettings;
	settings.FindMessage("inbound", &inboundSettings);
	fInboundSettings.Load(inboundSettings);
	BMessage outboundSettings;
	settings.FindMessage("outbound", &outboundSettings);
	fOutboundSettings.Load(outboundSettings);

	if (settings.FindBool("inbound_enabled", &fInboundEnabled) != B_OK)
		fInboundEnabled = true;
	if (settings.FindBool("outbound_enabled", &fOutboundEnabled) != B_OK)
		fOutboundEnabled = true;

	fModified = false;
	return B_OK;
}


/**
 * @brief Saves all account settings to the account file on disk.
 *
 * Creates the account file if it does not yet exist (via _CreateAccountFilePath()),
 * then flattens the complete settings BMessage into it.
 *
 * @return B_OK on success, or a file error code on failure.
 */
status_t
BMailAccountSettings::Save()
{
	fModified = false;

	BMessage settings;
	settings.AddInt32("id", fAccountID);
	settings.AddString("name", fAccountName);
	settings.AddString("real_name", fRealName);
	settings.AddString("return_address", fReturnAdress);

	BMessage inboundSettings;
	fInboundSettings.Save(inboundSettings);
	settings.AddMessage("inbound", &inboundSettings);
	BMessage outboundSettings;
	fOutboundSettings.Save(outboundSettings);
	settings.AddMessage("outbound", &outboundSettings);

	settings.AddBool("inbound_enabled", fInboundEnabled);
	settings.AddBool("outbound_enabled", fOutboundEnabled);

	status_t status = _CreateAccountFilePath();
	if (status != B_OK)
		return status;

	BFile file(&fAccountFile, B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	status = file.InitCheck();
	if (status != B_OK)
		return status;
	return settings.Flatten(&file);
}


/**
 * @brief Removes the account settings file from disk.
 *
 * @return B_OK on success, or an error code from BEntry::Remove().
 */
status_t
BMailAccountSettings::Delete()
{
	return fAccountFile.Remove();
}


/**
 * @brief Returns true if any setting has changed since the last load or save.
 *
 * @return true if fModified is set or if either protocol settings report a change.
 */
bool
BMailAccountSettings::HasBeenModified() const
{
	return fModified
		|| fInboundSettings.HasBeenModified()
		|| fOutboundSettings.HasBeenModified();
}


/**
 * @brief Returns the BEntry for the account settings file.
 *
 * @return Const reference to fAccountFile.
 */
const BEntry&
BMailAccountSettings::AccountFile() const
{
	return fAccountFile;
}


/**
 * @brief Creates the account file path if it does not yet exist.
 *
 * Generates a unique filename under Mail/accounts based on the account name
 * or ID, appending a numeric suffix if the preferred name is already taken.
 *
 * @return B_OK on success, or an error code if the directory cannot be created.
 */
status_t
BMailAccountSettings::_CreateAccountFilePath()
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;
	path.Append("Mail/accounts");
	create_directory(path.Path(), 777);

	if (fAccountFile.InitCheck() == B_OK)
		return B_OK;

	BString fileName = fAccountName;
	if (fileName == "")
		fileName << fAccountID;
	for (int i = 0; ; i++) {
		BString testFileName = fileName;
		if (i != 0) {
			testFileName += "_";
			testFileName << i;
		}
		BPath testPath(path);
		testPath.Append(testFileName);
		BEntry testEntry(testPath.Path());
		if (!testEntry.Exists()) {
			fileName = testFileName;
			break;
		}
	}

	path.Append(fileName);
	return fAccountFile.SetTo(path.Path());
}


/**
 * @brief Searches for an add-on file by name under the given add-ons sub-path.
 *
 * Iterates all paths returned by BPathFinder for \a subPath and returns the
 * entry_ref of the first matching file.
 *
 * @param subPath  Sub-path under B_FIND_PATH_ADD_ONS_DIRECTORY (e.g. "mail_daemon/inbound_protocols").
 * @param name     Add-on filename to search for.
 * @param ref      Output entry_ref set to the found file.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no matching file exists.
 */
status_t
BMailAccountSettings::_GetAddOnRef(const char* subPath, const char* name,
	entry_ref& ref)
{
	BStringList paths;
	BPathFinder().FindPaths(B_FIND_PATH_ADD_ONS_DIRECTORY, subPath, paths);

	for (int32 i = 0; i < paths.CountStrings(); i++) {
		BPath path(paths.StringAt(i), name);
		BEntry entry(path.Path());
		if (entry.Exists()) {
			if (entry.GetRef(&ref) == B_OK)
				return B_OK;
		}
	}
	return B_ENTRY_NOT_FOUND;
}
