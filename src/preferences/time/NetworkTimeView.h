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
 * MIT License. Copyright 2011-2014, Haiku, Inc.
 * Original authors: Axel Dörfler, Hamish Morrison, John Scipione.
 */

/** @file NetworkTimeView.h
    @brief NTP server list management and synchronization preference page. */

#ifndef NETWORK_TIME_VIEW_H
#define NETWORK_TIME_VIEW_H


#include <LayoutBuilder.h>


class BButton;
class BCheckBox;
class BListView;
class BMessage;
class BMessenger;
class BPath;
class BTextControl;


/** @brief Magic 'what' for the flattened NTP settings BMessage. */
static const uint32 kMsgNetworkTimeSettings = 'ntst';
/** @brief Selects a new default NTP server from the list. */
static const uint32 kMsgSetDefaultServer = 'setd';
/** @brief Posted while the user types in the server text field. */
static const uint32 kMsgServerEdited = 'sved';
/** @brief Adds the entry in the text field to the server list. */
static const uint32 kMsgAddServer = 'asrv';
/** @brief Removes the currently selected server from the list. */
static const uint32 kMsgRemoveServer = 'rsrv';
/** @brief Restores the built-in default server list. */
static const uint32 kMsgResetServerList = 'rstl';
/** @brief Toggles the "try every server on failure" behavior. */
static const uint32 kMsgTryAllServers = 'tras';
/** @brief Toggles the "synchronize at boot" behavior. */
static const uint32 kMsgSynchronizeAtBoot = 'synb';
/** @brief Starts an NTP synchronization run. */
static const uint32 kMsgSynchronize = 'sync';
/** @brief Cancels an in-progress synchronization run. */
static const uint32 kMsgStopSynchronization = 'stps';
/** @brief Reports the result of a finished synchronization run. */
static const uint32 kMsgSynchronizationResult = 'syrs';
/** @brief Notice broadcast when network time settings change. */
static const uint32 kMsgNetworkTimeChange = 'ntch';


/**
 * @brief Persistent NTP configuration backing the network-time preference page.
 *
 * Owns a BMessage of "server" strings, a default-server index, and the two
 * boolean toggles "try all servers" and "synchronize at boot". Snapshots the
 * load-time state so Revert can restore it. Saves automatically on
 * destruction.
 */
class Settings {
public:
							Settings();
							~Settings();

			void			AddServer(const char* server);
			const char*		GetServer(int32 index) const;
			void			RemoveServer(const char* server);
			void			SetDefaultServer(int32 index);
			int32			GetDefaultServer() const;
			void			SetTryAllServers(bool boolean);
			bool			GetTryAllServers() const;
			void			SetSynchronizeAtBoot(bool boolean);
			bool			GetSynchronizeAtBoot() const;

			void			ResetServersToDefaults();
			void			ResetToDefaults();
			void			Revert();
			bool			SettingsChanged();

			status_t		Load();
			status_t		Save();

private:
			int32			_GetStringByValue(const char* name,
								const char* value);
			status_t		_GetPath(BPath& path);

			BMessage		fMessage;
			BMessage		fOldMessage;
			bool			fWasUpdated;
};


/**
 * @brief Preference page for NTP server configuration and synchronization.
 *
 * Hosts a server list, an add/remove/reset row, the two boolean options,
 * and a Synchronize button that runs an NTP query in a background thread.
 */
class NetworkTimeView : public BGroupView {
public:
							NetworkTimeView(const char* name);
	virtual					~NetworkTimeView();

	virtual	void			MessageReceived(BMessage* message);
	virtual	void			AttachedToWindow();

			bool			CheckCanRevert();
private:
			void			_InitView();
			void			_UpdateServerList();
			void			_DoneSynchronizing();
			bool			_IsValidServerName(const char* serverName);

			Settings		fSettings;

			BTextControl*	fServerTextControl;
			BButton*		fAddButton;
			BButton*		fRemoveButton;
			BButton*		fResetButton;

			BListView*		fServerListView;
			BCheckBox*		fTryAllServersCheckBox;
			BCheckBox*		fSynchronizeAtBootCheckBox;
			BButton*		fSynchronizeButton;

			rgb_color		fTextColor;
			rgb_color		fInvalidColor;

			thread_id		fUpdateThread;
};


/**
 * @brief Background thread function that runs an NTP synchronization.
 *
 * @param params A BList containing pointers to a Settings and a BMessenger.
 * @return B_OK once the synchronization result message has been sent.
 */
int32
update_thread(void* params);

/**
 * @brief Spawns and resumes a background thread to perform synchronization.
 *
 * @param settings  Settings the thread should consult for servers and modes.
 * @param messenger Messenger the thread should reply to with the result.
 * @param thread    Output: spawned thread id.
 * @return Result of resume_thread() on the new thread.
 */
status_t
update_time(const Settings& settings, BMessenger* messenger,
	thread_id* thread);

/**
 * @brief Synchronously runs an NTP request against the configured servers.
 *
 * Tries the default server first; if that fails and "try all servers" is
 * set, falls back to each remaining server in turn.
 *
 * @param settings    Settings to consult.
 * @param errorString Output: human-readable error description on failure.
 * @param errorCode   Output: errno-style code on failure.
 * @return B_OK on success or the last underlying error code.
 */
status_t
update_time(const Settings& settings, const char** errorString,
	int32* errorCode);


#endif	// NETWORK_TIME_VIEW_H
