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
 *   Copyright 2011-2014 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 *       Hamish Morrison, hamish@lavabit.com
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file NetworkTimeView.cpp
 * @brief Implementation of NetworkTimeView and the Settings persistence
 *        helper.
 *
 * Builds the server list / add / remove / reset / synchronize UI, runs NTP
 * synchronization in a background thread, and persists settings through a
 * flattened BMessage stored in the user settings directory.
 */


#include "NetworkTimeView.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <File.h>
#include <FindDirectory.h>
#include <Invoker.h>
#include <ListItem.h>
#include <ListView.h>
#include <Path.h>
#include <ScrollView.h>
#include <Size.h>
#include <TextControl.h>

#include "ntp.h"
#include "TimeMessages.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Time"


//	#pragma mark - Settings


/**
 * @brief Constructs Settings, applies factory defaults, and loads any
 *        existing on-disk values.
 */
Settings::Settings()
	:
	fMessage(kMsgNetworkTimeSettings)
{
	ResetToDefaults();
	Load();
}


/**
 * @brief Destructor; persists the current settings to disk.
 */
Settings::~Settings()
{
	Save();
}


/**
 * @brief Adds a server hostname if it is not already present.
 *
 * @param server Hostname or IP address to add.
 */
void
Settings::AddServer(const char* server)
{
	if (_GetStringByValue("server", server) == B_ERROR)
		fMessage.AddString("server", server);
}


/**
 * @brief Returns the Nth server hostname.
 *
 * @param index Zero-based server index.
 * @return Pointer to the hostname, or NULL when @a index is out of range.
 */
const char*
Settings::GetServer(int32 index) const
{
	const char* server;
	fMessage.FindString("server", index, &server);
	return server;
}


/**
 * @brief Removes a server hostname and adjusts the default-server index.
 *
 * @param server Hostname to remove. No-op when not present.
 */
void
Settings::RemoveServer(const char* server)
{
	int32 index = _GetStringByValue("server", server);
	if (index != B_ERROR) {
		fMessage.RemoveData("server", index);

		int32 count;
		fMessage.GetInfo("server", NULL, &count);
		if (GetDefaultServer() >= count)
			SetDefaultServer(count - 1);
	}
}


/**
 * @brief Records which server should be tried first.
 *
 * @param index Zero-based index into the server list.
 */
void
Settings::SetDefaultServer(int32 index)
{
	if (fMessage.ReplaceInt32("default server", index) != B_OK)
		fMessage.AddInt32("default server", index);
}


/**
 * @brief Returns the index of the configured default server.
 */
int32
Settings::GetDefaultServer() const
{
	int32 index;
	fMessage.FindInt32("default server", &index);
	return index;
}


/**
 * @brief Sets the "try every server on failure" toggle.
 *
 * @param boolean True to attempt every server when the default fails.
 */
void
Settings::SetTryAllServers(bool boolean)
{
	fMessage.ReplaceBool("try all servers", boolean);
}


/**
 * @brief Returns the current "try all servers" toggle.
 */
bool
Settings::GetTryAllServers() const
{
	bool boolean;
	fMessage.FindBool("try all servers", &boolean);
	return boolean;
}


/**
 * @brief Sets the "synchronize at boot" toggle.
 *
 * @param boolean True to run a synchronization at every boot.
 */
void
Settings::SetSynchronizeAtBoot(bool boolean)
{
	fMessage.ReplaceBool("synchronize at boot", boolean);
}


/**
 * @brief Returns the current "synchronize at boot" toggle.
 */
bool
Settings::GetSynchronizeAtBoot() const
{
	bool boolean;
	fMessage.FindBool("synchronize at boot", &boolean);
	return boolean;
}


/**
 * @brief Replaces the server list with the canonical built-in set.
 *
 * Resets the default-server index to zero so the first entry is preferred.
 */
void
Settings::ResetServersToDefaults()
{
	fMessage.RemoveName("server");

	fMessage.AddString("server", "pool.ntp.org");
	fMessage.AddString("server", "de.pool.ntp.org");
	fMessage.AddString("server", "time.nist.gov");

	if (fMessage.ReplaceInt32("default server", 0) != B_OK)
		fMessage.AddInt32("default server", 0);
}


/**
 * @brief Reverts every setting to its built-in default value.
 */
void
Settings::ResetToDefaults()
{
	fMessage.MakeEmpty();
	ResetServersToDefaults();

	fMessage.AddBool("synchronize at boot", true);
	fMessage.AddBool("try all servers", true);
}


/**
 * @brief Restores the snapshot taken when settings were last loaded.
 */
void
Settings::Revert()
{
	fMessage = fOldMessage;
}


/**
 * @brief Returns true when the in-memory settings differ from the snapshot.
 *
 * Compares the flattened representations byte-for-byte. Allocation
 * failures conservatively report "changed".
 */
bool
Settings::SettingsChanged()
{
	ssize_t oldSize = fOldMessage.FlattenedSize();
	ssize_t newSize = fMessage.FlattenedSize();

	if (oldSize != newSize || oldSize < 0 || newSize < 0)
		return true;

	char* oldBytes = new (std::nothrow) char[oldSize];
	if (oldBytes == NULL)
		return true;

	fOldMessage.Flatten(oldBytes, oldSize);
	char* newBytes = new (std::nothrow) char[newSize];
	if (newBytes == NULL) {
		delete[] oldBytes;
		return true;
	}
	fMessage.Flatten(newBytes, newSize);

	int result = memcmp(oldBytes, newBytes, oldSize);

	delete[] oldBytes;
	delete[] newBytes;

	return result != 0;
}


/**
 * @brief Loads the flattened settings file and updates the snapshot.
 *
 * @return B_OK on success or the underlying status code on failure.
 */
status_t
Settings::Load()
{
	status_t status;

	BPath path;
	if ((status = _GetPath(path)) != B_OK)
		return status;

	BFile file(path.Path(), B_READ_ONLY);
	if ((status = file.InitCheck()) != B_OK)
		return status;

	BMessage load;
	if ((status = load.Unflatten(&file)) != B_OK)
		return status;

	if (load.what != kMsgNetworkTimeSettings)
		return B_BAD_TYPE;

	fMessage = load;
	fOldMessage = fMessage;
	return B_OK;
}


/**
 * @brief Writes the current settings BMessage out to disk.
 *
 * @return B_OK on success or the underlying status code on failure.
 */
status_t
Settings::Save()
{
	status_t status;

	BPath path;
	if ((status = _GetPath(path)) != B_OK)
		return status;

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if ((status = file.InitCheck()) != B_OK)
		return status;

	file.SetSize(0);

	return fMessage.Flatten(&file);
}


/**
 * @brief Returns the index of the first @a name field whose value matches @a value.
 *
 * @param name  Name of the BMessage field to scan.
 * @param value Value to compare against.
 * @return Matching index, or B_ERROR when no match is found.
 */
int32
Settings::_GetStringByValue(const char* name, const char* value)
{
	const char* string;
	for (int32 index = 0; fMessage.FindString(name, index, &string) == B_OK;
			index++) {
		if (strcmp(string, value) == 0)
			return index;
	}

	return B_ERROR;
}


/**
 * @brief Resolves the path of the settings file in the user settings dir.
 *
 * @param path Output BPath populated with the absolute file location.
 * @return B_OK on success or the find_directory() failure code.
 */
status_t
Settings::_GetPath(BPath& path)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;

	path.Append("networktime settings");

	return B_OK;
}


//	#pragma mark - NetworkTimeView


/**
 * @brief Constructs the network-time preference page.
 *
 * Loads the NTP settings, then builds the controls. The constructor
 * intentionally calls Load() a second time even though the Settings
 * constructor already does so; the duplicate call is harmless and
 * preserves upstream behavior.
 *
 * @param name View name passed to BGroupView.
 */
NetworkTimeView::NetworkTimeView(const char* name)
	:
	BGroupView(name, B_VERTICAL, B_USE_DEFAULT_SPACING),
	fSettings(),
	fServerTextControl(NULL),
	fAddButton(NULL),
	fRemoveButton(NULL),
	fResetButton(NULL),
	fServerListView(NULL),
	fTryAllServersCheckBox(NULL),
	fSynchronizeAtBootCheckBox(NULL),
	fSynchronizeButton(NULL),
	fTextColor(ui_color(B_CONTROL_TEXT_COLOR)),
	fInvalidColor(ui_color(B_FAILURE_COLOR)),
	fUpdateThread(-1)
{
	fSettings.Load();
	_InitView();
}


/**
 * @brief Destructor; releases the controls held in member pointers.
 *
 * @note The parent layout typically owns these views as well, so the
 *       deletes here are belt-and-braces.
 */
NetworkTimeView::~NetworkTimeView()
{
	delete fServerTextControl;
	delete fAddButton;
	delete fRemoveButton;
	delete fResetButton;
	delete fServerListView;
	delete fTryAllServersCheckBox;
	delete fSynchronizeAtBootCheckBox;
	delete fSynchronizeButton;
}


/**
 * @brief Routes user actions and synchronization replies into Settings.
 *
 * Edit / add / remove / reset of servers, the two checkboxes, the start
 * and stop of synchronization, the asynchronous result, and Revert all
 * dispatch from here. Posts kMsgChange to the parent window so the Revert
 * button reflects pending changes.
 *
 * @param message Incoming message.
 */
void
NetworkTimeView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSetDefaultServer:
		{
			int32 currentSelection = fServerListView->CurrentSelection();
			if (currentSelection < 0)
				fServerListView->Select(fSettings.GetDefaultServer());
			else {
				fSettings.SetDefaultServer(currentSelection);
				Looper()->PostMessage(new BMessage(kMsgChange));
			}
			break;
		}

		case kMsgServerEdited:
		{
			bool isValid = _IsValidServerName(fServerTextControl->Text());
			fServerTextControl->TextView()->SetFontAndColor(0,
				fServerTextControl->TextView()->TextLength(), NULL, 0,
				isValid ? &fTextColor : &fInvalidColor);
			fAddButton->SetEnabled(isValid);
			break;
		}

		case kMsgAddServer:
			if (!_IsValidServerName(fServerTextControl->Text()))
				break;

			fSettings.AddServer(fServerTextControl->Text());
			_UpdateServerList();
			fServerTextControl->SetText("");
			Looper()->PostMessage(new BMessage(kMsgChange));
			break;

		case kMsgRemoveServer:
		{
			int32 currentSelection = fServerListView->CurrentSelection();
			if (currentSelection < 0)
				break;

			fSettings.RemoveServer(((BStringItem*)
				fServerListView->ItemAt(currentSelection))->Text());
			_UpdateServerList();
			Looper()->PostMessage(new BMessage(kMsgChange));
			break;
		}

		case kMsgResetServerList:
			fSettings.ResetServersToDefaults();
			_UpdateServerList();
			Looper()->PostMessage(new BMessage(kMsgChange));
			break;

		case kMsgTryAllServers:
			fSettings.SetTryAllServers(
				fTryAllServersCheckBox->Value());
			Looper()->PostMessage(new BMessage(kMsgChange));
			break;

		case kMsgSynchronizeAtBoot:
			fSettings.SetSynchronizeAtBoot(fSynchronizeAtBootCheckBox->Value());
			Looper()->PostMessage(new BMessage(kMsgChange));
			break;

		case kMsgStopSynchronization:
			if (fUpdateThread >= B_OK)
				kill_thread(fUpdateThread);

			_DoneSynchronizing();
			break;

		case kMsgSynchronize:
		{
			if (fUpdateThread >= B_OK)
				break;

			BMessenger* messenger = new BMessenger(this);
			update_time(fSettings, messenger, &fUpdateThread);
			fSynchronizeButton->SetLabel(B_TRANSLATE("Stop"));
			fSynchronizeButton->Message()->what = kMsgStopSynchronization;
			break;
		}

		case kMsgSynchronizationResult:
		{
			_DoneSynchronizing();

			status_t status;
			if (message->FindInt32("status", (int32 *)&status) == B_OK) {
				if (status == B_OK)
					return;

				const char* errorString;
				message->FindString("error string", &errorString);
				char buffer[256];

				int32 errorCode;
				if (message->FindInt32("error code", &errorCode) == B_OK) {
					snprintf(buffer, sizeof(buffer),
						B_TRANSLATE("The following error occured "
							"while synchronizing:\n%s: %s"),
						errorString, strerror(errorCode));
				} else {
					snprintf(buffer, sizeof(buffer),
						B_TRANSLATE("The following error occured "
							"while synchronizing:\n%s"),
						errorString);
				}

				BAlert* alert = new BAlert(B_TRANSLATE("Time"), buffer,
					B_TRANSLATE("OK"));
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				alert->Go();
			}
			break;
		}

		case kMsgRevert:
			fSettings.Revert();
			fTryAllServersCheckBox->SetValue(fSettings.GetTryAllServers());
			fSynchronizeAtBootCheckBox->SetValue(
				fSettings.GetSynchronizeAtBoot());
			_UpdateServerList();
			break;

		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


/**
 * @brief Wires every control's target to this view and disables Add until
 *        a valid server name has been typed.
 */
void
NetworkTimeView::AttachedToWindow()
{
	fServerTextControl->SetTarget(this);
	fServerListView->SetTarget(this);
	fAddButton->SetTarget(this);
	fAddButton->SetEnabled(false);
	fRemoveButton->SetTarget(this);
	fResetButton->SetTarget(this);
	fTryAllServersCheckBox->SetTarget(this);
	fSynchronizeAtBootCheckBox->SetTarget(this);
	fSynchronizeButton->SetTarget(this);
}


/**
 * @brief Returns true when settings have been edited since load.
 */
bool
NetworkTimeView::CheckCanRevert()
{
	return fSettings.SettingsChanged();
}


/**
 * @brief Constructs and lays out the controls of the network-time page.
 *
 * Sizes the +/- mini buttons to match the text control's height for visual
 * alignment with the rest of the form.
 */
void
NetworkTimeView::_InitView()
{
	fServerTextControl = new BTextControl(NULL, NULL,
		new BMessage(kMsgAddServer));
	fServerTextControl->SetModificationMessage(new BMessage(kMsgServerEdited));

	const float kButtonWidth = fServerTextControl->Frame().Height();

	fAddButton = new BButton("add", "+", new BMessage(kMsgAddServer));
	fAddButton->SetToolTip(B_TRANSLATE("Add"));
	fAddButton->SetExplicitSize(BSize(kButtonWidth, kButtonWidth));

	fRemoveButton = new BButton("remove", "−", new BMessage(kMsgRemoveServer));
	fRemoveButton->SetToolTip(B_TRANSLATE("Remove"));
	fRemoveButton->SetExplicitSize(BSize(kButtonWidth, kButtonWidth));

	fServerListView = new BListView("serverList");
	fServerListView->SetExplicitMinSize(BSize(B_SIZE_UNSET, kButtonWidth * 4));
	fServerListView->SetSelectionMessage(new BMessage(kMsgSetDefaultServer));
	BScrollView* scrollView = new BScrollView("serverScrollView",
		fServerListView, B_FRAME_EVENTS | B_WILL_DRAW, false, true);
	_UpdateServerList();

	fTryAllServersCheckBox = new BCheckBox("tryAllServers",
		B_TRANSLATE("Try all servers"), new BMessage(kMsgTryAllServers));
	fTryAllServersCheckBox->SetValue(fSettings.GetTryAllServers());

	fSynchronizeAtBootCheckBox = new BCheckBox("autoUpdate",
		B_TRANSLATE("Synchronize at boot"),
		new BMessage(kMsgSynchronizeAtBoot));
	fSynchronizeAtBootCheckBox->SetValue(fSettings.GetSynchronizeAtBoot());

	fResetButton = new BButton("reset",
		B_TRANSLATE("Reset to default server list"),
		new BMessage(kMsgResetServerList));

	fSynchronizeButton = new BButton("update", B_TRANSLATE("Synchronize"),
		new BMessage(kMsgSynchronize));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
			.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
				.Add(fServerTextControl)
				.Add(fAddButton)
			.End()
			.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
				.Add(scrollView)
				.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
					.Add(fRemoveButton)
					.AddGlue()
				.End()
			.End()
		.End()
		.AddGroup(B_HORIZONTAL)
			.AddGroup(B_VERTICAL, 0)
				.Add(fTryAllServersCheckBox)
				.Add(fSynchronizeAtBootCheckBox)
			.End()
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(fResetButton)
			.AddGlue()
			.Add(fSynchronizeButton)
		.End()
		.SetInsets(B_USE_WINDOW_SPACING, B_USE_WINDOW_SPACING,
			B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING);
}


/**
 * @brief Rebuilds the server list view from the current Settings contents.
 *
 * Restores the selection to the default-server index and disables the
 * Remove button when the list is empty.
 */
void
NetworkTimeView::_UpdateServerList()
{
	BListItem* item;
	while ((item = fServerListView->RemoveItem((int32)0)) != NULL)
		delete item;

	const char* server;
	int32 index = 0;
	while ((server = fSettings.GetServer(index++)) != NULL)
		fServerListView->AddItem(new BStringItem(server));

	fServerListView->Select(fSettings.GetDefaultServer());
	fServerListView->ScrollToSelection();

	fRemoveButton->SetEnabled(fServerListView->CountItems() > 0);
}


/**
 * @brief Resets the synchronize button's label and state after a run.
 */
void
NetworkTimeView::_DoneSynchronizing()
{
	fUpdateThread = -1;
	fSynchronizeButton->SetLabel(B_TRANSLATE("Synchronize again"));
	fSynchronizeButton->Message()->what = kMsgSynchronize;
}


/**
 * @brief Validates that a server name is plausibly a hostname.
 *
 * Accepts only alphanumeric characters plus '.', '-', and '_'. No URL
 * scheme is allowed.
 *
 * @param serverName Candidate hostname.
 * @return True when @a serverName looks like a valid host.
 */
bool
NetworkTimeView::_IsValidServerName(const char* serverName)
{
	if (serverName == NULL || *serverName == '\0')
		return false;

	for (int32 i = 0; serverName[i] != '\0'; i++) {
		char c = serverName[i];
		// Simple URL validation, no scheme should be present
		if (!(isalnum(c) || c == '.' || c == '-' || c == '_'))
			return false;
	}

	return true;
}


//	#pragma mark - update functions


/**
 * @brief Background-thread entry point that performs the NTP request.
 *
 * Unpacks the BList of (Settings*, BMessenger*), runs update_time(), and
 * sends a kMsgSynchronizationResult message back through the messenger.
 *
 * @param params BList containing exactly two pointers in order:
 *               Settings*, BMessenger*.
 * @return B_OK once the result has been delivered.
 */
int32
update_thread(void* params)
{
	BList* list = (BList*)params;
	BMessenger* messenger = (BMessenger*)list->ItemAt(1);

	const char* errorString = NULL;
	int32 errorCode = 0;
	status_t status = update_time(*(Settings*)list->ItemAt(0),
		&errorString, &errorCode);

	BMessage result(kMsgSynchronizationResult);
	result.AddInt32("status", status);
	result.AddString("error string", errorString);
	if (errorCode != 0)
		result.AddInt32("error code", errorCode);

	messenger->SendMessage(&result);
	delete messenger;

	return B_OK;
}


/**
 * @brief Spawns and resumes a worker thread to perform synchronization.
 *
 * @param settings  Settings the worker should consult.
 * @param messenger Messenger the worker should reply to.
 * @param thread    Output: thread id of the spawned worker.
 * @return Result of resume_thread() on the new thread.
 */
status_t
update_time(const Settings& settings, BMessenger* messenger,
	thread_id* thread)
{
	BList* params = new BList(2);
	params->AddItem((void*)&settings);
	params->AddItem((void*)messenger);
	*thread = spawn_thread(update_thread, "ntpUpdate", 64, params);

	return resume_thread(*thread);
}


/**
 * @brief Performs an NTP request, falling back through servers as needed.
 *
 * Tries the default server first; if that fails and "try all servers" is
 * enabled, walks the remaining servers in order until one succeeds.
 *
 * @param settings    Settings to consult.
 * @param errorString Output: human-readable error string from the last
 *                    attempt.
 * @param errorCode   Output: errno-style error code from the last attempt.
 * @return B_OK on success, or the last underlying error code.
 */
status_t
update_time(const Settings& settings, const char** errorString,
	int32* errorCode)
{
	int32 defaultServer = settings.GetDefaultServer();

	status_t status = B_ENTRY_NOT_FOUND;
	const char* server = settings.GetServer(defaultServer);

	if (server != NULL)
		status = ntp_update_time(server, errorString, errorCode);

	if (status != B_OK && settings.GetTryAllServers()) {
		for (int32 index = 0; ; index++) {
			if (index == defaultServer)
				index++;

			server = settings.GetServer(index);
			if (server == NULL)
				break;

			status = ntp_update_time(server, errorString, errorCode);
			if (status == B_OK)
				break;
		}
	}

	return status;
}
