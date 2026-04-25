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
 *   Copyright 2001-2019 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 *       Ingo Weinhold, bonefish@@users.sf.net
 *       Jacob Secunda
 */


/**
 * @file AppMisc.cpp
 * @brief Miscellaneous application utility functions in the BPrivate namespace.
 *
 * Provides helper functions for retrieving application paths, entry_refs,
 * team information, port lookups, and desktop server connections. These
 * utilities are used internally by the application kit and are not part of
 * the public API.
 */

#include <AppMisc.h>

#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <Entry.h>
#include <image.h>
#include <Messenger.h>
#include <OS.h>
#include <Window.h>

#include <AutoDeleter.h>
#include <ServerLink.h>
#include <ServerProtocol.h>
#include <WindowInfo.h>


namespace BPrivate {


static team_id sCurrentTeam = -1;


/**
 * @brief Returns the path to an application's executable.
 *
 * Walks the team's image list and returns the path of the image typed
 * @c B_APP_IMAGE.
 *
 * @param team The application's team ID.
 * @param buffer A pointer to a pre-allocated character array of at least
 *        @c B_PATH_NAME_LENGTH bytes; receives the executable path on success.
 * @retval B_OK                 The buffer was populated successfully.
 * @retval B_BAD_VALUE          @a buffer is @c NULL.
 * @retval B_ENTRY_NOT_FOUND    No B_APP_IMAGE was found for the team.
 * @return Another error code propagated from get_next_image_info().
 */
status_t
get_app_path(team_id team, char *buffer)
{
	// The only way to get the path to the application's executable seems to
	// be to get an image_info of its image, which also contains a path.
	// Several images may belong to the team (libraries, add-ons), but only
	// the one in question should be typed B_APP_IMAGE.
	if (!buffer)
		return B_BAD_VALUE;

	image_info info;
	int32 cookie = 0;

	while (get_next_image_info(team, &cookie, &info) == B_OK) {
		if (info.type == B_APP_IMAGE) {
			strlcpy(buffer, info.name, B_PATH_NAME_LENGTH - 1);
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Returns the path to the current application's executable.
 *
 * Convenience overload that calls get_app_path(B_CURRENT_TEAM, buffer).
 *
 * @param buffer A pointer to a pre-allocated character array of at least
 *        @c B_PATH_NAME_LENGTH bytes; receives the executable path on success.
 * @retval B_OK         The buffer was populated successfully.
 * @retval B_BAD_VALUE  @a buffer is @c NULL.
 * @return Another error code if the path cannot be retrieved.
 */
status_t
get_app_path(char *buffer)
{
	return get_app_path(B_CURRENT_TEAM, buffer);
}


/**
 * @brief Returns an entry_ref referring to an application's executable.
 *
 * @param team The application's team ID.
 * @param ref A pointer to a pre-allocated entry_ref that receives the result.
 * @param traverse If @c true, the function traverses symbolic links when
 *        resolving the path.
 * @retval B_OK         The entry_ref was populated successfully.
 * @retval B_BAD_VALUE  @a ref is @c NULL.
 * @return Another error code propagated from get_app_path() or BEntry.
 */
status_t
get_app_ref(team_id team, entry_ref *ref, bool traverse)
{
	status_t error = (ref ? B_OK : B_BAD_VALUE);
	char appFilePath[B_PATH_NAME_LENGTH];

	if (error == B_OK)
		error = get_app_path(team, appFilePath);

	if (error == B_OK) {
		BEntry entry(appFilePath, traverse);
		error = entry.GetRef(ref);
	}

	return error;
}


/**
 * @brief Returns an entry_ref referring to the current application's executable.
 *
 * Convenience overload that calls get_app_ref(B_CURRENT_TEAM, ref, traverse).
 *
 * @param ref A pointer to a pre-allocated entry_ref that receives the result.
 * @param traverse If @c true, the function traverses symbolic links when
 *        resolving the path.
 * @retval B_OK         The entry_ref was populated successfully.
 * @retval B_BAD_VALUE  @a ref is @c NULL.
 * @return Another error code if the entry_ref cannot be retrieved.
 */
status_t
get_app_ref(entry_ref *ref, bool traverse)
{
	return get_app_ref(B_CURRENT_TEAM, ref, traverse);
}


/**
 * @brief Returns the ID of the current team.
 *
 * The team ID is cached in @c sCurrentTeam after the first lookup so that
 * subsequent calls do not re-enter the kernel. The cache is invalidated by
 * init_team_after_fork().
 *
 * @return The ID of the current team.
 */
team_id
current_team()
{
	if (sCurrentTeam < 0) {
		thread_info info;
		if (get_thread_info(find_thread(NULL), &info) == B_OK)
			sCurrentTeam = info.team;
	}
	return sCurrentTeam;
}


/** @brief Resets the cached team ID after a fork.
 *
 *  Must be called after fork() so that the child process re-queries its
 *  own team ID on the next call to current_team().
 */
void
init_team_after_fork()
{
	sCurrentTeam = -1;
}


/**
 * @brief Returns the ID of the supplied team's main thread.
 *
 * On Haiku the team ID is identical to the team's main thread ID; this
 * function fetches a team_info to verify the team exists and then returns
 * the team ID itself.
 *
 * @param team The team to query.
 * @return The thread ID of the team's main thread on success.
 * @retval B_BAD_TEAM_ID The supplied team ID does not identify a running team.
 */
thread_id
main_thread_for(team_id team)
{
	// Under Haiku the team ID is equal to it's main thread ID. We just get
	// a team info to verify the existence of the team.
	team_info info;
	status_t error = get_team_info(team, &info);
	return error == B_OK ? team : error;
}


/**
 * @brief Returns whether the application identified by @a team is currently
 *        showing a modal window.
 *
 * Iterates over the team's window tokens and checks each window's feel for
 * one of the modal feels (subset, application, or global modal).
 *
 * @param team The ID of the application in question.
 * @return @c true if the application is showing a modal window, @c false
 *         otherwise (including the case where the team has no windows).
 */
bool
is_app_showing_modal_window(team_id team)
{
	int32 tokenCount;
	int32* tokens = get_token_list(team, &tokenCount);

	if (tokens != NULL) {
		MemoryDeleter tokenDeleter(tokens);

		for (int32 index = 0; index < tokenCount; index++) {
			client_window_info* matchWindowInfo = get_window_info(tokens[index]);
			if (matchWindowInfo == NULL) {
				// That window probably closed. Just go to the next one.
				continue;
			}

			window_feel theFeel = (window_feel)matchWindowInfo->feel;
			free(matchWindowInfo);

			if (theFeel == B_MODAL_SUBSET_WINDOW_FEEL
				|| theFeel == B_MODAL_APP_WINDOW_FEEL
				|| theFeel == B_MODAL_ALL_WINDOW_FEEL)
				return true;
		}
	}

	return false;
}


#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST


/**
 * @brief Creates a connection with the desktop's app_server.
 *
 * Allocates a client-side reply port, then sends an @c AS_GET_DESKTOP message
 * via BMessenger to the app_server to retrieve the per-desktop send port.
 * The resulting send/receive port pair is installed into @a link.
 *
 * @param link     The ServerLink to populate with the resolved send port and
 *                 a freshly created receive port.
 * @param name     Name to assign to the receive port.
 * @param capacity Capacity (number of messages) for the receive port.
 * @return B_OK on success, or a negative error code if the port cannot be
 *         created or the app_server fails to respond with a valid desktop port.
 */
status_t
create_desktop_connection(ServerLink* link, const char* name, int32 capacity)
{
	// Create the port so that the app_server knows where to send messages
	port_id clientPort = create_port(capacity, name);
	if (clientPort < 0)
		return clientPort;

	link->SetReceiverPort(clientPort);

	BMessage request(AS_GET_DESKTOP);
	request.AddInt32("user", getuid());
	request.AddInt32("version", AS_PROTOCOL_VERSION);
	request.AddString("target", getenv("TARGET_SCREEN"));

	BMessenger server("application/x-vnd.Haiku-app_server");
	BMessage reply;
	status_t status = server.SendMessage(&request, &reply);
	if (status != B_OK)
		return status;

	port_id desktopPort = reply.GetInt32("port", B_ERROR);
	if (desktopPort < 0)
		return desktopPort;

	link->SetSenderPort(desktopPort);
	return B_OK;
}


#else // HAIKU_TARGET_PLATFORM_LIBBE_TEST


static port_id sServerPort = -1;


/** @brief Returns the port ID of the app_server.
 *
 *  Lazily looks up and caches the app_server port by name. Used only in
 *  the test-mode build (HAIKU_TARGET_PLATFORM_LIBBE_TEST).
 *
 *  @return The app_server port ID, or a negative error code if not found.
 */
port_id
get_app_server_port()
{
	if (sServerPort < 0) {
		// No need for synchronization - in the worst case, we'll call
		// find_port() twice.
		sServerPort = find_port(SERVER_PORT_NAME);
	}

	return sServerPort;
}


/**
 * @brief Creates a connection with the desktop (libbe test build).
 *
 * Test-mode variant that uses the already-known app_server port returned by
 * get_app_server_port() and exchanges desktop port handles directly through
 * the ServerLink protocol rather than via BMessenger.
 *
 * @param link     The ServerLink to populate with the resolved send port and
 *                 a freshly created receive port.
 * @param name     Name to assign to the receive port.
 * @param capacity Capacity (number of messages) for the receive port.
 * @return B_OK on success, B_ERROR if the desktop reply is not B_OK, or a
 *         negative port-creation error.
 */
status_t
create_desktop_connection(ServerLink* link, const char* name, int32 capacity)
{
	port_id serverPort = get_app_server_port();
	if (serverPort < 0)
		return serverPort;

	// Create the port so that the app_server knows where to send messages
	port_id clientPort = create_port(capacity, name);
	if (clientPort < 0)
		return clientPort;

	link->SetTo(serverPort, clientPort);

	link->StartMessage(AS_GET_DESKTOP);
	link->Attach<port_id>(clientPort);
	link->Attach<int32>(getuid());
	link->AttachString(getenv("TARGET_SCREEN"));
	link->Attach<int32>(AS_PROTOCOL_VERSION);

	int32 code;
	if (link->FlushWithReply(code) != B_OK || code != B_OK) {
		link->SetSenderPort(-1);
		return B_ERROR;
	}

	link->Read<port_id>(&serverPort);
	link->SetSenderPort(serverPort);

	return B_OK;
}


#endif // HAIKU_TARGET_PLATFORM_LIBBE_TEST


} // namespace BPrivate
