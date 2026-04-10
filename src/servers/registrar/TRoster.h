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
   Copyright 2001-2007, Ingo Weinhold, bonefish@users.sf.net.
   Distributed under the terms of the MIT License.
 */

/** @file TRoster.h
 *  @brief Core application roster managing registration, activation, watching, and recent lists. */
#ifndef T_ROSTER_H
#define T_ROSTER_H


#include "AppInfoList.h"
#include "RecentApps.h"
#include "RecentEntries.h"
#include "WatchingService.h"

#include <HashSet.h>
#include <HashMap.h>
#include <Locker.h>
#include <MessageQueue.h>
#include <Path.h>
#include <Roster.h>
#include <SupportDefs.h>

#include <map>


using std::map;

class BMessage;
class WatchingService;

typedef map<int32, BMessageQueue*>	IARRequestMap;

/** @brief The central roster that tracks all running applications and their lifecycle events. */
class TRoster {
public:
							TRoster();
	virtual					~TRoster();

			/** @brief Processes a request to register a new application. */
			void			HandleAddApplication(BMessage* request);
			/** @brief Completes a previously pre-registered application's registration. */
			void			HandleCompleteRegistration(BMessage* request);
			/** @brief Checks whether a given application is registered. */
			void			HandleIsAppRegistered(BMessage* request);
			/** @brief Removes a pre-registered application entry. */
			void			HandleRemovePreRegApp(BMessage* request);
			/** @brief Removes a fully registered application entry. */
			void			HandleRemoveApp(BMessage* request);
			/** @brief Updates the thread and team of a registered application. */
			void			HandleSetThreadAndTeam(BMessage* request);
			/** @brief Updates the signature of a registered application. */
			void			HandleSetSignature(BMessage* request);
			/** @brief Retrieves information about a registered application. */
			void			HandleGetAppInfo(BMessage* request);
			/** @brief Returns the list of currently registered applications. */
			void			HandleGetAppList(BMessage* request);
			/** @brief Updates which application is currently active. */
			void			HandleUpdateActiveApp(BMessage* request);
			/** @brief Broadcasts a message to all registered applications. */
			void			HandleBroadcast(BMessage* request);
			/** @brief Registers a watcher for roster change events. */
			void			HandleStartWatching(BMessage* request);
			/** @brief Unregisters a roster change watcher. */
			void			HandleStopWatching(BMessage* request);
			void			HandleGetRecentDocuments(BMessage* request);
			void			HandleGetRecentFolders(BMessage* request);
			void			HandleGetRecentApps(BMessage* request);
			void			HandleAddToRecentDocuments(BMessage* request);
			void			HandleAddToRecentFolders(BMessage* request);
			void			HandleAddToRecentApps(BMessage* request);
			void			HandleLoadRecentLists(BMessage* request);
			void			HandleSaveRecentLists(BMessage* request);

			void			HandleAppServerStarted(BMessage* request);

			void			ClearRecentDocuments();
			void			ClearRecentFolders();
			void			ClearRecentApps();

			/** @brief Initializes the roster and loads saved settings. */
			status_t		Init();

			/** @brief Adds an application info entry to the roster. */
			status_t		AddApp(RosterAppInfo* info);
			/** @brief Removes an application info entry from the roster. */
			void			RemoveApp(RosterAppInfo* info);
			void			UpdateActiveApp(RosterAppInfo* info);

			void			CheckSanity();

			/** @brief Marks the roster as entering the shutdown sequence. */
			void			SetShuttingDown(bool shuttingDown);
			status_t		GetShutdownApps(AppInfoList& userApps,
								AppInfoList& systemApps,
								AppInfoList& backgroundApps,
								HashSet<HashKey32<team_id> >& vitalSystemApps);
			status_t		AddAppInfo(AppInfoList& apps, team_id team);

			status_t		AddWatcher(Watcher* watcher);
			void			RemoveWatcher(Watcher* watcher);

private:
	// hook functions
			void			_AppAdded(RosterAppInfo* info);
			void			_AppRemoved(RosterAppInfo* info);
			void			_AppActivated(RosterAppInfo* info);
			void			_AppDeactivated(RosterAppInfo* info);

	// helper functions
	static	status_t		_AddMessageAppInfo(BMessage* message,
								const app_info* info);
	static	status_t		_AddMessageWatchingInfo(BMessage* message,
								const app_info* info);
			uint32			_NextToken();

			void			_AddIARRequest(IARRequestMap& map, int32 key,
								BMessage* request);
			void			_ReplyToIARRequests(BMessageQueue* requests,
								const RosterAppInfo* info);
			void			_ReplyToIARRequest(BMessage* request,
								const RosterAppInfo* info);

			void			_HandleGetRecentEntries(BMessage* request);

			void			_ValidateRunning(const entry_ref& ref,
								const char* signature);
			bool			_IsSystemApp(RosterAppInfo* info) const;

			status_t		_LoadRosterSettings(const char* path = NULL);
			status_t		_SaveRosterSettings(const char* path = NULL);
	static	const char*		kDefaultRosterSettingsFile;

private:
			BLocker			fLock;
			AppInfoList		fRegisteredApps;
			AppInfoList		fEarlyPreRegisteredApps;
			IARRequestMap	fIARRequestsByID;
			IARRequestMap	fIARRequestsByToken;
			RosterAppInfo*	fActiveApp;
			WatchingService	fWatchingService;
			RecentApps		fRecentApps;
			RecentEntries	fRecentDocuments;
			RecentEntries	fRecentFolders;
			uint32			fLastToken;
			bool			fShuttingDown;
			BPath			fSystemAppPath;
			BPath			fSystemServerPath;
};

#endif	// T_ROSTER_H
