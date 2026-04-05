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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */
#ifndef _ROSTER_H
#define _ROSTER_H

/**
 * @file Roster.h
 * @brief Provides the BRoster class for managing the roster of running
 *        applications, and the app_info struct for querying application
 *        metadata.
 */


#include <Entry.h>
#include <Messenger.h>
#include <OS.h>

class BFile;
class BMimeType;
class BNodeInfo;


/**
 * @brief Holds metadata about a running application.
 *
 * An app_info structure is filled in by the various BRoster query methods
 * to describe a running (or pre-registered) application.
 */
struct app_info {
	app_info();
	~app_info();

	thread_id	thread;   /**< Main thread ID of the application. */
	team_id		team;     /**< Team ID of the application. */
	port_id		port;     /**< Application's main message port. */
	uint32		flags;    /**< Launch flags (e.g. B_SINGLE_LAUNCH). */
	entry_ref	ref;      /**< Entry ref to the application's executable. */
	char		signature[B_MIME_TYPE_LENGTH]; /**< MIME signature string. */
};

/** @name Application Launch Flags
 *  Flags that control how an application is launched.
 *  @{
 */
#define B_SINGLE_LAUNCH			(0x0)   /**< Only one instance allowed; re-activates existing. */
#define B_MULTIPLE_LAUNCH		(0x1)   /**< Multiple instances may run simultaneously. */
#define B_EXCLUSIVE_LAUNCH		(0x2)   /**< Only one instance system-wide; launch fails if running. */
#define B_LAUNCH_MASK			(0x3)   /**< Mask for the launch mode bits. */
#define B_BACKGROUND_APP		(0x4)   /**< Application runs without a window or Deskbar entry. */
#define B_ARGV_ONLY				(0x8)   /**< Application receives arguments via argv only, not messages. */
#define _B_APP_INFO_RESERVED1_	(0x10000000) /**< Reserved for internal use. */
/** @} */

/**
 * @enum watching_request_flags
 * @brief Flags passed to BRoster::StartWatching() to specify which
 *        application events to monitor.
 */
enum watching_request_flags {
	B_REQUEST_LAUNCHED	= 0x00000001, /**< Notify when an application is launched. */
	B_REQUEST_QUIT		= 0x00000002, /**< Notify when an application quits. */
	B_REQUEST_ACTIVATED	= 0x00000004, /**< Notify when an application is activated. */
};

/**
 * @brief Notification message codes delivered to watchers registered via
 *        BRoster::StartWatching().
 */
enum {
	B_SOME_APP_LAUNCHED		= 'BRAS', /**< An application has been launched. */
	B_SOME_APP_QUIT			= 'BRAQ', /**< An application has quit. */
	B_SOME_APP_ACTIVATED	= 'BRAW', /**< An application has been activated. */
};

class BList;


/**
 * @brief Manages the system-wide roster of running applications.
 *
 * BRoster provides an interface for querying, launching, and monitoring
 * applications. You can determine whether an application is running, obtain
 * its team ID, retrieve its app_info, find the preferred handler for a given
 * MIME type, broadcast messages to all applications, and watch for
 * application launch/quit/activation events.
 *
 * A global instance is available as @c be_roster.
 *
 * @see be_roster
 * @see app_info
 */
class BRoster {
public:
	/**
	 * @brief Constructs a new BRoster object.
	 */
								BRoster();

	/**
	 * @brief Destroys the BRoster object.
	 */
								~BRoster();

	// running apps

	/**
	 * @brief Checks whether an application with the given signature is running.
	 * @param signature The MIME application signature to look for.
	 * @return @c true if at least one instance is running, @c false otherwise.
	 */
			bool				IsRunning(const char* signature) const;

	/**
	 * @brief Checks whether the application identified by entry_ref is running.
	 * @param ref Pointer to the entry_ref of the application executable.
	 * @return @c true if the application is running, @c false otherwise.
	 */
			bool				IsRunning(entry_ref* ref) const;

	/**
	 * @brief Returns the team ID of a running application by signature.
	 * @param signature The MIME application signature.
	 * @return The team_id if running, or a negative error code if not found.
	 */
			team_id				TeamFor(const char* signature) const;

	/**
	 * @brief Returns the team ID of a running application by entry_ref.
	 * @param ref Pointer to the entry_ref of the application executable.
	 * @return The team_id if running, or a negative error code if not found.
	 */
			team_id				TeamFor(entry_ref* ref) const;

	/**
	 * @brief Fills a BList with team IDs of all running applications.
	 * @param teamIDList Pointer to a BList to receive the team_id values.
	 */
			void				GetAppList(BList* teamIDList) const;

	/**
	 * @brief Fills a BList with team IDs of running applications matching a
	 *        specific signature.
	 * @param signature The MIME application signature to filter by.
	 * @param teamIDList Pointer to a BList to receive the team_id values.
	 */
			void				GetAppList(const char* signature,
									BList* teamIDList) const;

	// app infos

	/**
	 * @brief Retrieves the app_info for a running application by signature.
	 * @param signature The MIME application signature.
	 * @param info Pointer to an app_info struct to be filled in.
	 * @return @c B_OK on success, or an error code on failure.
	 */
			status_t			GetAppInfo(const char* signature,
									app_info* info) const;

	/**
	 * @brief Retrieves the app_info for a running application by entry_ref.
	 * @param ref Pointer to the entry_ref of the application executable.
	 * @param info Pointer to an app_info struct to be filled in.
	 * @return @c B_OK on success, or an error code on failure.
	 */
			status_t			GetAppInfo(entry_ref* ref,
									app_info* info) const;

	/**
	 * @brief Retrieves the app_info for a running application by team ID.
	 * @param team The team_id of the target application.
	 * @param info Pointer to an app_info struct to be filled in.
	 * @return @c B_OK on success, or an error code on failure.
	 */
			status_t			GetRunningAppInfo(team_id team,
									app_info* info) const;

	/**
	 * @brief Retrieves the app_info of the currently active application.
	 * @param info Pointer to an app_info struct to be filled in.
	 * @return @c B_OK on success, or an error code on failure.
	 */
			status_t			GetActiveAppInfo(app_info* info) const;

	// find app

	/**
	 * @brief Finds the preferred application for a given MIME type.
	 * @param mimeType The MIME type string to resolve.
	 * @param app Pointer to an entry_ref to receive the application location.
	 * @return @c B_OK on success, or an error code on failure.
	 * @see Launch
	 */
			status_t			FindApp(const char* mimeType,
									entry_ref* app) const;

	/**
	 * @brief Finds the preferred application for a file identified by entry_ref.
	 * @param ref Pointer to the entry_ref of the file.
	 * @param app Pointer to an entry_ref to receive the application location.
	 * @return @c B_OK on success, or an error code on failure.
	 * @see Launch
	 */
			status_t			FindApp(entry_ref* ref, entry_ref* app) const;

	// broadcast

	/**
	 * @brief Broadcasts a message to all running applications.
	 * @param message Pointer to the BMessage to broadcast.
	 * @return @c B_OK on success, or an error code on failure.
	 */
			status_t			Broadcast(BMessage* message) const;

	/**
	 * @brief Broadcasts a message to all running applications with a reply
	 *        target.
	 * @param message Pointer to the BMessage to broadcast.
	 * @param replyTo A BMessenger to which replies should be directed.
	 * @return @c B_OK on success, or an error code on failure.
	 */
			status_t			Broadcast(BMessage* message,
									BMessenger replyTo) const;

	// watching

	/**
	 * @brief Registers a target to receive notifications about application
	 *        roster events.
	 * @param target A BMessenger identifying the handler for notifications.
	 * @param eventMask A bitmask of watching_request_flags specifying which
	 *        events to watch (defaults to launch and quit).
	 * @return @c B_OK on success, or an error code on failure.
	 * @see StopWatching
	 * @see watching_request_flags
	 */
			status_t			StartWatching(BMessenger target,
									uint32 eventMask
										= B_REQUEST_LAUNCHED
											| B_REQUEST_QUIT) const;

	/**
	 * @brief Stops sending application roster notifications to the given
	 *        target.
	 * @param target The BMessenger that was previously registered.
	 * @return @c B_OK on success, or an error code on failure.
	 * @see StartWatching
	 */
			status_t			StopWatching(BMessenger target) const;

	/**
	 * @brief Brings the application identified by team ID to the foreground.
	 * @param team The team_id of the application to activate.
	 * @return @c B_OK on success, or an error code on failure.
	 */
			status_t			ActivateApp(team_id team) const;

	// launch app

	/**
	 * @brief Launches an application by MIME type with an optional initial
	 *        message.
	 * @param mimeType The MIME type of the application to launch.
	 * @param initialMessage Optional BMessage to deliver on launch.
	 * @param _appTeam Optional pointer to receive the team_id of the launched
	 *        application.
	 * @return @c B_OK on success, or an error code on failure.
	 * @see FindApp
	 */
			status_t			Launch(const char* mimeType,
									BMessage* initialMessage = NULL,
									team_id* _appTeam = NULL) const;

	/**
	 * @brief Launches an application by MIME type with a list of messages.
	 * @param mimeType The MIME type of the application to launch.
	 * @param messageList Pointer to a BList of BMessage pointers.
	 * @param _appTeam Optional pointer to receive the team_id.
	 * @return @c B_OK on success, or an error code on failure.
	 */
			status_t			Launch(const char* mimeType, BList* messageList,
									team_id* _appTeam = NULL) const;

	/**
	 * @brief Launches an application by MIME type with command-line arguments.
	 * @param mimeType The MIME type of the application to launch.
	 * @param argc Number of arguments in the args array.
	 * @param args Array of C-string arguments.
	 * @param _appTeam Optional pointer to receive the team_id.
	 * @return @c B_OK on success, or an error code on failure.
	 */
			status_t			Launch(const char* mimeType, int argc,
									const char* const* args,
									team_id* _appTeam = NULL) const;

	/**
	 * @brief Launches an application by entry_ref with an optional initial
	 *        message.
	 * @param ref Pointer to the entry_ref of the application executable.
	 * @param initialMessage Optional BMessage to deliver on launch.
	 * @param _appTeam Optional pointer to receive the team_id.
	 * @return @c B_OK on success, or an error code on failure.
	 */
			status_t			Launch(const entry_ref* ref,
									const BMessage* initialMessage = NULL,
									team_id* _appTeam = NULL) const;

	/**
	 * @brief Launches an application by entry_ref with a list of messages.
	 * @param ref Pointer to the entry_ref of the application executable.
	 * @param messageList Pointer to a BList of BMessage pointers.
	 * @param _appTeam Optional pointer to receive the team_id.
	 * @return @c B_OK on success, or an error code on failure.
	 */
			status_t			Launch(const entry_ref* ref,
									const BList* messageList,
									team_id* _appTeam = NULL) const;

	/**
	 * @brief Launches an application by entry_ref with command-line arguments.
	 * @param ref Pointer to the entry_ref of the application executable.
	 * @param argc Number of arguments in the args array.
	 * @param args Array of C-string arguments.
	 * @param _appTeam Optional pointer to receive the team_id.
	 * @return @c B_OK on success, or an error code on failure.
	 */
			status_t			Launch(const entry_ref* ref, int argc,
									const char* const* args,
									team_id* _appTeam = NULL) const;

	// recent documents, folders, apps

	/**
	 * @brief Retrieves a list of recently opened documents.
	 * @param refList Pointer to a BMessage to receive the entry_ref list.
	 * @param maxCount Maximum number of entries to return.
	 * @param fileType Optional MIME type filter.
	 * @param signature Optional application signature filter.
	 */
			void				GetRecentDocuments(BMessage* refList,
									int32 maxCount, const char* fileType = NULL,
									const char* signature = NULL) const;

	/**
	 * @brief Retrieves a list of recently opened documents filtered by
	 *        multiple MIME types.
	 * @param refList Pointer to a BMessage to receive the entry_ref list.
	 * @param maxCount Maximum number of entries to return.
	 * @param fileTypes Array of MIME type strings to filter by.
	 * @param fileTypesCount Number of entries in the fileTypes array.
	 * @param signature Optional application signature filter.
	 */
			void				GetRecentDocuments(BMessage* refList,
									int32 maxCount, const char* fileTypes[],
									int32 fileTypesCount,
									const char* signature = NULL) const;

	/**
	 * @brief Retrieves a list of recently opened folders.
	 * @param refList Pointer to a BMessage to receive the entry_ref list.
	 * @param maxCount Maximum number of entries to return.
	 * @param signature Optional application signature filter.
	 */
			void				GetRecentFolders(BMessage* refList,
									int32 maxCount,
									const char* signature = NULL) const;

	/**
	 * @brief Retrieves a list of recently launched applications.
	 * @param refList Pointer to a BMessage to receive the entry_ref list.
	 * @param maxCount Maximum number of entries to return.
	 */
			void				GetRecentApps(BMessage* refList,
									int32 maxCount) const;

	/**
	 * @brief Adds a document to the recent documents list.
	 * @param document Pointer to the entry_ref of the document.
	 * @param signature Optional application signature to associate.
	 */
			void				AddToRecentDocuments(const entry_ref* document,
									const char* signature = NULL) const;

	/**
	 * @brief Adds a folder to the recent folders list.
	 * @param folder Pointer to the entry_ref of the folder.
	 * @param signature Optional application signature to associate.
	 */
			void				AddToRecentFolders(const entry_ref* folder,
									const char* signature = NULL) const;

	// private/reserved stuff starts here
	class Private;

private:
	class ArgVector;
	friend class Private;

			status_t			_ShutDown(bool reboot, bool confirm,
									bool synchronous);
			status_t			_IsShutDownInProgress(bool* inProgress);

			status_t			_AddApplication(const char* signature,
									const entry_ref* ref, uint32 flags,
									team_id team, thread_id thread,
									port_id port, bool fullRegistration,
									uint32* pToken, team_id* otherTeam) const;

			status_t			_SetSignature(team_id team,
									const char* signature) const;

			void				_SetThread(team_id team,
									thread_id thread) const;

			status_t			_SetThreadAndTeam(uint32 entryToken,
									thread_id thread, team_id team,
									port_id* _port) const;

			status_t			_CompleteRegistration(team_id team,
									thread_id thread, port_id port) const;

			bool				_IsAppPreRegistered(const entry_ref* ref,
									team_id team, app_info* info) const;

			status_t			_IsAppRegistered(const entry_ref* ref,
									team_id team, uint32 token,
									bool* preRegistered, app_info* info) const;

			status_t			_RemovePreRegApp(uint32 entryToken) const;
			status_t			_RemoveApp(team_id team) const;

			void				_ApplicationCrashed(team_id team);

			status_t			_LaunchApp(const char* mimeType,
									const entry_ref* ref,
									const BList* messageList, int argc,
									const char* const* args,
									const char** environment,
									team_id* _appTeam, thread_id* _appThread,
									port_id* _appPort, uint32* _appToken,
									bool launchSuspended) const;

			status_t			_UpdateActiveApp(team_id team) const;

			void				_SetAppFlags(team_id team, uint32 flags) const;

			void				_DumpRoster() const;

			status_t			_ResolveApp(const char* inType, entry_ref* ref,
									entry_ref* appRef,
									char* signature,
									uint32* appFlags,
									bool* wasDocument) const;

			status_t			_TranslateRef(entry_ref* ref,
									BMimeType* appMeta, entry_ref* appRef,
									BFile* appFile, bool* wasDocument) const;

			status_t			_TranslateType(const char* mimeType,
									BMimeType* appMeta, entry_ref* appRef,
									BFile* appFile) const;

			status_t			_GetFileType(const entry_ref* file,
									BNodeInfo* nodeInfo, char* mimeType) const;
			status_t			_SendToRunning(team_id team, int argc,
									const char* const* args,
									const BList* messageList,
									const entry_ref* ref,
									bool readyToRun) const;

			void				_SetWithoutRegistrar(bool noRegistrar);

			void				_InitMessenger();

	static	status_t			_InitMimeMessenger(void* data);

			BMessenger&			_MimeMessenger();

			void				_AddToRecentApps(const char* signature) const;

			void				_ClearRecentDocuments() const;
			void				_ClearRecentFolders() const;
			void				_ClearRecentApps() const;
			void				_LoadRecentLists(const char* filename) const;
			void				_SaveRecentLists(const char* filename) const;

			BMessenger			fMessenger;
			BMessenger			fMimeMessenger;
			int32				fMimeMessengerInitOnce;
			bool				fNoRegistrar;
			uint32				_reserved[1];
};

/**
 * @brief Global BRoster instance providing access to the application roster.
 *
 * Use this pointer to query running applications, launch new ones, and
 * register for roster event notifications.
 */
extern const BRoster* be_roster;


#endif	// _ROSTER_H
