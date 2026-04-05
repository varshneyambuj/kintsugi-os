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
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 *       Ingo Weinhold, ingo_weinhold@gmx.de
 */


/** @file Roster.cpp
 *  @brief Implementation of BRoster -- the application roster manager.
 *
 *  BRoster provides the interface for querying, launching, and managing
 *  running applications.  It communicates with the registrar server to
 *  maintain the system-wide roster of applications and supports features
 *  such as MIME-type-based application lookup, broadcasting messages to
 *  all running applications, application watching, activation, and
 *  management of recently used documents, folders, and applications.
 *
 *  The global @c be_roster pointer is the primary access point for
 *  applications to interact with the roster.
 */


#include <Roster.h>

#include <ctype.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include <AppFileInfo.h>
#include <Application.h>
#include <Bitmap.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <fs_index.h>
#include <fs_info.h>
#include <image.h>
#include <List.h>
#include <Mime.h>
#include <Node.h>
#include <NodeInfo.h>
#include <OS.h>
#include <Path.h>
#include <Query.h>
#include <RegistrarDefs.h>
#include <String.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include <locks.h>

#include <AppMisc.h>
#include <DesktopLink.h>
#include <LaunchRoster.h>
#include <MessengerPrivate.h>
#include <PortLink.h>
#include <RosterPrivate.h>
#include <ServerProtocol.h>


using namespace std;
using namespace BPrivate;


// debugging
//#define DBG(x) x
#define DBG(x)
#ifdef DEBUG_PRINTF
#	define OUT DEBUG_PRINTF
#else
#	define OUT printf
#endif


const BRoster* be_roster;


//	#pragma mark - Helper functions


/*!	Extracts an app_info from a BMessage.

	The function searchs for a field "app_info" typed B_REG_APP_INFO_TYPE
	and initializes \a info with the found data.

	\param message The message
	\param info A pointer to a pre-allocated app_info to be filled in with the
	       info found in the message.

	\return A status code.
	\retval B_OK Everything went fine.
	\retval B_BAD_VALUE \c NULL \a message or \a info.
*/
static status_t
find_message_app_info(BMessage* message, app_info* info)
{
	status_t error = (message && info ? B_OK : B_BAD_VALUE);
	const flat_app_info* flatInfo = NULL;
	ssize_t size = 0;
	// find the flat app info in the message
	if (error == B_OK) {
		error = message->FindData("app_info", B_REG_APP_INFO_TYPE,
			(const void**)&flatInfo, &size);
	}
	// unflatten the flat info
	if (error == B_OK) {
		if (size == sizeof(flat_app_info)) {
			info->thread = flatInfo->thread;
			info->team = flatInfo->team;
			info->port = flatInfo->port;
			info->flags = flatInfo->flags;
			info->ref.device = flatInfo->ref_device;
			info->ref.directory = flatInfo->ref_directory;
			info->ref.name = NULL;
			memcpy(info->signature, flatInfo->signature, B_MIME_TYPE_LENGTH);
			if (strlen(flatInfo->ref_name) > 0)
				info->ref.set_name(flatInfo->ref_name);
		} else
			error = B_ERROR;
	}

	return error;
}


/*!	Checks whether or not an application can be used.

	Currently it is only checked whether the application is in the trash.

	\param ref An entry_ref referring to the application executable.

	\return A status code, \c B_OK on success oir other error codes specifying
	        why the application cannot be used.
	\retval B_OK The application can be used.
	\retval B_ENTRY_NOT_FOUND \a ref doesn't refer to and existing entry.
	\retval B_IS_A_DIRECTORY \a ref refers to a directory.
	\retval B_LAUNCH_FAILED_APP_IN_TRASH The application executable is in the
	        trash.
*/
static status_t
can_app_be_used(const entry_ref* ref)
{
	status_t error = (ref ? B_OK : B_BAD_VALUE);
	// check whether the file exists and is a file.
	BEntry entry;
	if (error == B_OK)
		error = entry.SetTo(ref, true);

	if (error == B_OK && !entry.Exists())
		error = B_ENTRY_NOT_FOUND;

	if (error == B_OK && !entry.IsFile())
		error = B_IS_A_DIRECTORY;

	// check whether the file is in trash
	BPath trashPath;
	BDirectory directory;
	BVolume volume;
	if (error == B_OK
		&& volume.SetTo(ref->device) == B_OK
		&& find_directory(B_TRASH_DIRECTORY, &trashPath, false, &volume)
			== B_OK
		&& directory.SetTo(trashPath.Path()) == B_OK
		&& directory.Contains(&entry)) {
		error = B_LAUNCH_FAILED_APP_IN_TRASH;
	}

	return error;
}


/*!	Compares the supplied version infos.

	\param info1 The first info.
	\param info2 The second info.

	\return \c -1, if the first info is less than the second one, \c 1, if
	        the first one is greater than the second one, and \c 0, if both
	        are equal.
*/
static int32
compare_version_infos(const version_info& info1, const version_info& info2)
{
	int32 result = 0;
	if (info1.major < info2.major)
		result = -1;
	else if (info1.major > info2.major)
		result = 1;
	else if (info1.middle < info2.middle)
		result = -1;
	else if (info1.middle > info2.middle)
		result = 1;
	else if (info1.minor < info2.minor)
		result = -1;
	else if (info1.minor > info2.minor)
		result = 1;
	else if (info1.variety < info2.variety)
		result = -1;
	else if (info1.variety > info2.variety)
		result = 1;
	else if (info1.internal < info2.internal)
		result = -1;
	else if (info1.internal > info2.internal)
		result = 1;

	return result;
}


/*!	Compares two applications to decide which one should be rather
	returned as a query result.

	First, it checks if both apps are in the path, and prefers the app that
	appears earlier.

	If both files have a version info, then those are compared.
	If one file has a version info, it is said to be greater. If both
	files have no version info, their modification times are compared.

	\param app1 An entry_ref referring to the first application.
	\param app2 An entry_ref referring to the second application.
	\return \c -1, if the first application version is less than the second
	        one, \c 1, if the first one is greater than the second one, and
	        \c 0, if both are equal.
*/
static int32
compare_queried_apps(const entry_ref* app1, const entry_ref* app2)
{
	BPath path1(app1);
	BPath path2(app2);

	// Check search path

	const char* searchPathes = getenv("PATH");
	if (searchPathes != NULL) {
		char* searchBuffer = strdup(searchPathes);
		if (searchBuffer != NULL) {
			char* last;
			const char* path = strtok_r(searchBuffer, ":", &last);
			while (path != NULL) {
				// Check if any app path matches
				size_t length = strlen(path);
				bool found1 = !strncmp(path, path1.Path(), length)
					&& path1.Path()[length] == '/';
				bool found2 = !strncmp(path, path2.Path(), length)
					&& path2.Path()[length] == '/';;

				if (found1 != found2) {
					free(searchBuffer);
					return found1 ? 1 : -1;
				}

				path = strtok_r(NULL, ":", &last);
			}

			free(searchBuffer);
		}
	}

	// Check system servers folder
	BPath path;
	find_directory(B_SYSTEM_SERVERS_DIRECTORY, &path);
	BString serverPath(path.Path());
	serverPath << '/';
	size_t length = serverPath.Length();

	bool inSystem1 = !strncmp(serverPath.String(), path1.Path(), length);
	bool inSystem2 = !strncmp(serverPath.String(), path2.Path(), length);
	if (inSystem1 != inSystem2)
		return inSystem1 ? 1 : -1;

	// Check version info

	BFile file1;
	file1.SetTo(app1, B_READ_ONLY);
	BFile file2;
	file2.SetTo(app2, B_READ_ONLY);

	BAppFileInfo appFileInfo1;
	appFileInfo1.SetTo(&file1);
	BAppFileInfo appFileInfo2;
	appFileInfo2.SetTo(&file2);

	time_t modificationTime1 = 0;
	time_t modificationTime2 = 0;

	file1.GetModificationTime(&modificationTime1);
	file2.GetModificationTime(&modificationTime2);

	int32 result = 0;

	version_info versionInfo1;
	version_info versionInfo2;
	bool hasVersionInfo1 = (appFileInfo1.GetVersionInfo(
		&versionInfo1, B_APP_VERSION_KIND) == B_OK);
	bool hasVersionInfo2 = (appFileInfo2.GetVersionInfo(
		&versionInfo2, B_APP_VERSION_KIND) == B_OK);

	if (hasVersionInfo1) {
		if (hasVersionInfo2)
			result = compare_version_infos(versionInfo1, versionInfo2);
		else
			result = 1;
	} else {
		if (hasVersionInfo2)
			result = -1;
		else if (modificationTime1 < modificationTime2)
			result = -1;
		else if (modificationTime1 > modificationTime2)
			result = 1;
	}

	return result;
}


/*!	Finds an app by signature on any mounted volume.

	\param signature The app's signature.
	\param appRef A pointer to a pre-allocated entry_ref to be filled with
	       a reference to the found application's executable.

	\return A status code.
	\retval B_OK Everything went fine.
	\retval B_BAD_VALUE: \c NULL \a signature or \a appRef.
	\retval B_LAUNCH_FAILED_APP_NOT_FOUND: An application with this signature
	        could not be found.
*/
static status_t
query_for_app(const char* signature, entry_ref* appRef)
{
	if (signature == NULL || appRef == NULL)
		return B_BAD_VALUE;

	status_t error = B_LAUNCH_FAILED_APP_NOT_FOUND;
	bool caseInsensitive = false;

	while (true) {
		// search on all volumes
		BVolumeRoster volumeRoster;
		BVolume volume;
		while (volumeRoster.GetNextVolume(&volume) == B_OK) {
			if (!volume.KnowsQuery())
				continue;

			index_info info;
			if (fs_stat_index(volume.Device(), "BEOS:APP_SIG", &info) != 0) {
				// This volume doesn't seem to have the index we're looking for;
				// querying it might need a long time, and we don't care *that*
				// much...
				continue;
			}

			BQuery query;
			query.SetVolume(&volume);
			query.PushAttr("BEOS:APP_SIG");
			if (!caseInsensitive)
				query.PushString(signature);
			else {
				// second pass, create a case insensitive query string
				char string[B_MIME_TYPE_LENGTH * 4];
				strlcpy(string, "application/", sizeof(string));

				int32 length = strlen(string);
				const char* from = signature + length;
				char* to = string + length;

				for (; from[0]; from++) {
					if (isalpha(from[0])) {
						*to++ = '[';
						*to++ = tolower(from[0]);
						*to++ = toupper(from[0]);
						*to++ = ']';
					} else
						*to++ = from[0];
				}

				to[0] = '\0';
				query.PushString(string);
			}
			query.PushOp(B_EQ);

			query.Fetch();

			// walk through the query
			bool appFound = false;
			status_t foundAppError = B_OK;
			entry_ref ref;
			while (query.GetNextRef(&ref) == B_OK) {
				if ((!appFound || compare_queried_apps(appRef, &ref) < 0)
					&& (foundAppError = can_app_be_used(&ref)) == B_OK) {
					*appRef = ref;
					appFound = true;
				}
			}
			if (!appFound) {
				// If the query didn't return any hits, the error is
				// B_LAUNCH_FAILED_APP_NOT_FOUND, otherwise we return the
				// result of the last can_app_be_used().
				error = foundAppError != B_OK
					? foundAppError : B_LAUNCH_FAILED_APP_NOT_FOUND;
			} else
				return B_OK;
		}

		if (!caseInsensitive)
			caseInsensitive = true;
		else
			break;
	}

	return error;
}


//	#pragma mark - app_info


app_info::app_info()
	:
	thread(-1),
	team(-1),
	port(-1),
	flags(B_REG_DEFAULT_APP_FLAGS),
	ref()
{
	signature[0] = '\0';
}


app_info::~app_info()
{
}


//	#pragma mark - BRoster::ArgVector


class BRoster::ArgVector {
public:
								ArgVector();
								~ArgVector();

			status_t			Init(int argc, const char* const* args,
									const entry_ref* appRef,
									const entry_ref* docRef);
			void				Unset();
	inline	int					Count() const { return fArgc; }
	inline	const char* const*	Args() const { return fArgs; }

private:
			int					fArgc;
			const char**		fArgs;
			BPath				fAppPath;
			BPath				fDocPath;
};


//!	Creates an uninitialized ArgVector.
BRoster::ArgVector::ArgVector()
	:
	fArgc(0),
	fArgs(NULL),
	fAppPath(),
	fDocPath()
{
}


//!	Frees all resources associated with the ArgVector.
BRoster::ArgVector::~ArgVector()
{
	Unset();
}


/*!	Initilizes the object according to the supplied parameters.

	If the initialization succeeds, the methods Count() and Args() grant
	access to the argument count and vector created by this methods.
	\note The returned vector is valid only as long as the elements of the
	supplied \a args (if any) are valid and this object is not destroyed.
	This object retains ownership of the vector returned by Args().
	In case of error, the value returned by Args() is invalid (or \c NULL).

	The argument vector is created as follows: First element is the path
	of the entry \a appRef refers to, then follow all elements of \a args
	and then, if \a args has at least one element and \a docRef can be
	resolved to a path, the path of the entry \a docRef refers to. That is,
	if no or an empty \a args vector is supplied, the resulting argument
	vector contains only one element, the path associated with \a appRef.

	\param argc Specifies the number of elements \a args contains.
	\param args Argument vector. May be \c NULL.
	\param appRef entry_ref referring to the entry whose path shall be the
	       first element of the resulting argument vector.
	\param docRef entry_ref referring to the entry whose path shall be the
	       last element of the resulting argument vector. May be \c NULL.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \c NULL \a appRef.
	- \c B_ENTRY_NOT_FOUND or other file system error codes: \a appRef could
	  not be resolved to a path.
	- \c B_NO_MEMORY: Not enough memory to allocate for this operation.
*/
status_t
BRoster::ArgVector::Init(int argc, const char* const* args,
	const entry_ref* appRef, const entry_ref* docRef)
{
	// unset old values
	Unset();
	status_t error = appRef ? B_OK : B_BAD_VALUE;
	// get app path
	if (error == B_OK)
		error = fAppPath.SetTo(appRef);
	// determine number of arguments
	bool hasDocArg = false;
	if (error == B_OK) {
		fArgc = 1;
		if (argc > 0 && args) {
			fArgc += argc;
			if (docRef != NULL && fDocPath.SetTo(docRef) == B_OK) {
				fArgc++;
				hasDocArg = true;
			}
		}
		fArgs = new(nothrow) const char*[fArgc + 1];
			// + 1 for the terminating NULL
		if (!fArgs)
			error = B_NO_MEMORY;
	}
	// init vector
	if (error == B_OK) {
		fArgs[0] = fAppPath.Path();
		if (argc > 0 && args != NULL) {
			for (int i = 0; i < argc; i++)
				fArgs[i + 1] = args[i];
			if (hasDocArg)
				fArgs[fArgc - 1] = fDocPath.Path();
		}
		// NULL terminate (e.g. required by load_image())
		fArgs[fArgc] = NULL;
	}
	return error;
}


//!	Uninitializes the object.
void
BRoster::ArgVector::Unset()
{
	fArgc = 0;
	delete[] fArgs;
	fArgs = NULL;
	fAppPath.Unset();
	fDocPath.Unset();
}


//	#pragma mark - BRoster


/** @brief Constructs a BRoster and connects to the registrar.
 *
 *  Initializes the internal messenger that communicates with the
 *  registrar server.  In normal operation, applications use the
 *  global @c be_roster pointer rather than constructing their own
 *  BRoster instances.
 */
BRoster::BRoster()
	:
	fMessenger(),
	fMimeMessenger(),
	fMimeMessengerInitOnce(INIT_ONCE_UNINITIALIZED),
	fNoRegistrar(false)
{
	_InitMessenger();
}


/** @brief Destructor.
 *
 *  BRoster holds no resources that require explicit cleanup beyond
 *  the BMessenger members which are destroyed automatically.
 */
BRoster::~BRoster()
{
}


//	#pragma mark - Querying for apps


/** @brief Checks whether an application with the given signature is running.
 *  @param signature The MIME application signature to look up.
 *  @return @c true if a matching application is running, @c false otherwise.
 *  @see TeamFor(const char*)
 */
bool
BRoster::IsRunning(const char* signature) const
{
	return (TeamFor(signature) >= 0);
}


/** @brief Checks whether an application launched from the given entry is running.
 *  @param ref An entry_ref identifying the application executable.
 *  @return @c true if a matching application is running, @c false otherwise.
 *  @see TeamFor(entry_ref*)
 */
bool
BRoster::IsRunning(entry_ref* ref) const
{
	return (TeamFor(ref) >= 0);
}


/** @brief Returns the team ID of a running application identified by signature.
 *  @param signature The MIME application signature to look up.
 *  @return The team ID on success, or a negative error code if no matching
 *          application is running.
 *  @see GetAppInfo(const char*, app_info*)
 */
team_id
BRoster::TeamFor(const char* signature) const
{
	team_id team;
	app_info info;
	status_t error = GetAppInfo(signature, &info);
	if (error == B_OK)
		team = info.team;
	else
		team = error;

	return team;
}


/** @brief Returns the team ID of a running application identified by entry_ref.
 *  @param ref An entry_ref identifying the application executable.
 *  @return The team ID on success, or a negative error code if no matching
 *          application is running.
 *  @see GetAppInfo(entry_ref*, app_info*)
 */
team_id
BRoster::TeamFor(entry_ref* ref) const
{
	team_id team;
	app_info info;
	status_t error = GetAppInfo(ref, &info);
	if (error == B_OK)
		team = info.team;
	else
		team = error;
	return team;
}


/** @brief Retrieves the team IDs of all running applications.
 *
 *  Sends a request to the registrar and populates @a teamIDList with
 *  team_id values cast to @c void*.
 *
 *  @param teamIDList Pointer to a BList to be populated with team IDs.
 *                    Must not be @c NULL.
 *  @see GetAppList(const char*, BList*)
 */
void
BRoster::GetAppList(BList* teamIDList) const
{
	status_t error = (teamIDList ? B_OK : B_BAD_VALUE);
	// compose the request message
	BMessage request(B_REG_GET_APP_LIST);

	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);

	// evaluate the reply
	if (error == B_OK) {
		if (reply.what == B_REG_SUCCESS) {
			team_id team;
			for (int32 i = 0; reply.FindInt32("teams", i, &team) == B_OK; i++)
				teamIDList->AddItem((void*)(addr_t)team);
		} else {
			if (reply.FindInt32("error", &error) != B_OK)
				error = B_ERROR;
			DBG(OUT("Roster request unsuccessful: %s\n", strerror(error)));
			DBG(reply.PrintToStream());
		}
	} else {
		DBG(OUT("Sending message to roster failed: %s\n", strerror(error)));
	}
}


/** @brief Retrieves the team IDs of all running instances of a given app.
 *
 *  Only applications whose signature matches @a signature are included.
 *
 *  @param signature The MIME application signature to filter by.
 *                   Must not be @c NULL.
 *  @param teamIDList Pointer to a BList to be populated with team IDs.
 *                    Must not be @c NULL.
 *  @see GetAppList(BList*)
 */
void
BRoster::GetAppList(const char* signature, BList* teamIDList) const
{
	status_t error = B_OK;
	if (signature == NULL || teamIDList == NULL)
		error = B_BAD_VALUE;

	// compose the request message
	BMessage request(B_REG_GET_APP_LIST);
	if (error == B_OK)
		error = request.AddString("signature", signature);

	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);

	// evaluate the reply
	if (error == B_OK) {
		if (reply.what == B_REG_SUCCESS) {
			team_id team;
			for (int32 i = 0; reply.FindInt32("teams", i, &team) == B_OK; i++)
				teamIDList->AddItem((void*)(addr_t)team);
		} else if (reply.FindInt32("error", &error) != B_OK)
			error = B_ERROR;
	}
}


/** @brief Retrieves information about a running app identified by signature.
 *
 *  Queries the registrar for the app_info of the application whose
 *  signature matches @a signature.
 *
 *  @param signature The MIME application signature. Must not be @c NULL.
 *  @param info      Pointer to an app_info structure to fill in.
 *                   Must not be @c NULL.
 *  @return @c B_OK on success, @c B_BAD_VALUE if parameters are @c NULL,
 *          or @c B_ERROR if no matching application is found.
 *  @see GetRunningAppInfo(), GetActiveAppInfo()
 */
status_t
BRoster::GetAppInfo(const char* signature, app_info* info) const
{
	status_t error = B_OK;
	if (signature == NULL || info == NULL)
		error = B_BAD_VALUE;

	// compose the request message
	BMessage request(B_REG_GET_APP_INFO);
	if (error == B_OK)
		error = request.AddString("signature", signature);

	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);

	// evaluate the reply
	if (error == B_OK) {
		if (reply.what == B_REG_SUCCESS)
			error = find_message_app_info(&reply, info);
		else if (reply.FindInt32("error", &error) != B_OK)
			error = B_ERROR;
	}

	return error;
}


/** @brief Retrieves information about a running app identified by entry_ref.
 *
 *  Queries the registrar for the app_info of the application whose
 *  executable matches @a ref.
 *
 *  @param ref  An entry_ref identifying the application executable.
 *              Must not be @c NULL.
 *  @param info Pointer to an app_info structure to fill in.
 *              Must not be @c NULL.
 *  @return @c B_OK on success, @c B_BAD_VALUE if parameters are @c NULL,
 *          or @c B_ERROR if no matching application is found.
 *  @see GetAppInfo(const char*, app_info*)
 */
status_t
BRoster::GetAppInfo(entry_ref* ref, app_info* info) const
{
	status_t error = (ref && info ? B_OK : B_BAD_VALUE);
	// compose the request message
	BMessage request(B_REG_GET_APP_INFO);
	if (error == B_OK)
		error = request.AddRef("ref", ref);

	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);

	// evaluate the reply
	if (error == B_OK) {
		if (reply.what == B_REG_SUCCESS)
			error = find_message_app_info(&reply, info);
		else if (reply.FindInt32("error", &error) != B_OK)
			error = B_ERROR;
	}
	return error;
}


/** @brief Retrieves information about a running application by team ID.
 *
 *  @param team The team ID of the target application. Must be >= 0.
 *  @param info Pointer to an app_info structure to fill in.
 *              Must not be @c NULL.
 *  @return @c B_OK on success, @c B_BAD_VALUE if @a info is @c NULL,
 *          @c B_BAD_TEAM_ID if @a team is negative, or @c B_ERROR if the
 *          team is not registered.
 *  @see GetAppInfo(), GetActiveAppInfo()
 */
status_t
BRoster::GetRunningAppInfo(team_id team, app_info* info) const
{
	status_t error = (info ? B_OK : B_BAD_VALUE);
	if (error == B_OK && team < 0)
		error = B_BAD_TEAM_ID;
	// compose the request message
	BMessage request(B_REG_GET_APP_INFO);
	if (error == B_OK)
		error = request.AddInt32("team", team);
	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);

	// evaluate the reply
	if (error == B_OK) {
		if (reply.what == B_REG_SUCCESS)
			error = find_message_app_info(&reply, info);
		else if (reply.FindInt32("error", &error) != B_OK)
			error = B_ERROR;
	}
	return error;
}


/** @brief Retrieves information about the currently active application.
 *
 *  The active application is the one that currently has focus on the
 *  desktop.
 *
 *  @param info Pointer to an app_info structure to fill in.
 *              Must not be @c NULL.
 *  @return @c B_OK on success, @c B_BAD_VALUE if @a info is @c NULL,
 *          or @c B_ERROR if the active app cannot be determined.
 *  @see GetRunningAppInfo(), ActivateApp()
 */
status_t
BRoster::GetActiveAppInfo(app_info* info) const
{
	if (info == NULL)
		return B_BAD_VALUE;

	// compose the request message
	BMessage request(B_REG_GET_APP_INFO);
	// send the request
	BMessage reply;
	status_t error = fMessenger.SendMessage(&request, &reply);
	// evaluate the reply
	if (error == B_OK) {
		if (reply.what == B_REG_SUCCESS)
			error = find_message_app_info(&reply, info);
		else if (reply.FindInt32("error", &error) != B_OK)
			error = B_ERROR;
	}
	return error;
}


/** @brief Finds the application associated with a given MIME type.
 *
 *  Searches for the preferred application for @a mimeType and returns
 *  a reference to its executable.
 *
 *  @param mimeType The MIME type string. Must not be @c NULL.
 *  @param app      Pointer to an entry_ref to receive the found application.
 *                  Must not be @c NULL.
 *  @return @c B_OK on success, @c B_BAD_VALUE if parameters are @c NULL,
 *          or an error code if no application could be found.
 *  @see FindApp(entry_ref*, entry_ref*), _ResolveApp()
 */
status_t
BRoster::FindApp(const char* mimeType, entry_ref* app) const
{
	if (mimeType == NULL || app == NULL)
		return B_BAD_VALUE;

	return _ResolveApp(mimeType, NULL, app, NULL, NULL, NULL);
}


/** @brief Finds the application associated with a given file.
 *
 *  Determines which application should handle the file referred to by
 *  @a ref and returns a reference to its executable.
 *
 *  @param ref Pointer to an entry_ref identifying the file.
 *             Must not be @c NULL.
 *  @param app Pointer to an entry_ref to receive the found application.
 *             Must not be @c NULL.
 *  @return @c B_OK on success, @c B_BAD_VALUE if parameters are @c NULL,
 *          or an error code if no application could be found.
 *  @see FindApp(const char*, entry_ref*), _ResolveApp()
 */
status_t
BRoster::FindApp(entry_ref* ref, entry_ref* app) const
{
	if (ref == NULL || app == NULL)
		return B_BAD_VALUE;

	entry_ref _ref(*ref);
	return _ResolveApp(NULL, &_ref, app, NULL, NULL, NULL);
}


//	#pragma mark - Launching, activating, and broadcasting to apps


/** @brief Broadcasts a message to all running applications.
 *
 *  Uses the default application messenger as the reply target.
 *
 *  @param message The message to broadcast. Must not be @c NULL.
 *  @return @c B_OK on success, or an error code on failure.
 *  @see Broadcast(BMessage*, BMessenger)
 */
status_t
BRoster::Broadcast(BMessage* message) const
{
	return Broadcast(message, be_app_messenger);
}


/** @brief Broadcasts a message to all running applications with a
 *         custom reply target.
 *
 *  Sends a copy of @a message to every registered application except the
 *  caller.  Replies are directed to @a replyTo.
 *
 *  @param message The message to broadcast. Must not be @c NULL.
 *  @param replyTo The BMessenger that should receive replies.
 *  @return @c B_OK on success, @c B_BAD_VALUE if @a message is @c NULL,
 *          or an error code on failure.
 */
status_t
BRoster::Broadcast(BMessage* message, BMessenger replyTo) const
{
	status_t error = (message ? B_OK : B_BAD_VALUE);
	// compose the request message
	BMessage request(B_REG_BROADCAST);
	if (error == B_OK)
		error = request.AddInt32("team", BPrivate::current_team());
	if (error == B_OK)
		error = request.AddMessage("message", message);
	if (error == B_OK)
		error = request.AddMessenger("reply_target", replyTo);

	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);

	// evaluate the reply
	if (error == B_OK && reply.what != B_REG_SUCCESS
		&& reply.FindInt32("error", &error) != B_OK)
		error = B_ERROR;

	return error;
}


/** @brief Starts watching for application roster events.
 *
 *  Registers @a target to receive notifications when applications are
 *  launched, quit, or activated, according to the bits set in
 *  @a eventMask (e.g. @c B_REQUEST_LAUNCHED, @c B_REQUEST_QUIT,
 *  @c B_REQUEST_ACTIVATED).
 *
 *  @param target    The BMessenger to receive watch notifications.
 *  @param eventMask Bitmask indicating which events to watch for.
 *  @return @c B_OK on success, or an error code on failure.
 *  @see StopWatching()
 */
status_t
BRoster::StartWatching(BMessenger target, uint32 eventMask) const
{
	status_t error = B_OK;
	// compose the request message
	BMessage request(B_REG_START_WATCHING);
	if (error == B_OK)
		error = request.AddMessenger("target", target);
	if (error == B_OK)
		error = request.AddInt32("events", (int32)eventMask);

	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);

	// evaluate the reply
	if (error == B_OK && reply.what != B_REG_SUCCESS
		&& reply.FindInt32("error", &error) != B_OK)
		error = B_ERROR;

	return error;
}


/** @brief Stops watching for application roster events.
 *
 *  Unregisters @a target so it no longer receives roster notifications
 *  previously requested via StartWatching().
 *
 *  @param target The BMessenger that was previously registered.
 *  @return @c B_OK on success, or an error code on failure.
 *  @see StartWatching()
 */
status_t
BRoster::StopWatching(BMessenger target) const
{
	status_t error = B_OK;
	// compose the request message
	BMessage request(B_REG_STOP_WATCHING);
	if (error == B_OK)
		error = request.AddMessenger("target", target);

	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);

	// evaluate the reply
	if (error == B_OK && reply.what != B_REG_SUCCESS
		&& reply.FindInt32("error", &error) != B_OK)
		error = B_ERROR;

	return error;
}


/** @brief Activates (brings to front) the application with the given team ID.
 *
 *  Sends a request to the app_server to make the specified team's windows
 *  the active windows on the desktop.
 *
 *  @param team The team ID of the application to activate.
 *  @return @c B_OK on success, or an error code on failure.
 *  @see GetActiveAppInfo()
 */
status_t
BRoster::ActivateApp(team_id team) const
{
	BPrivate::DesktopLink link;

	status_t status = link.InitCheck();
	if (status < B_OK)
		return status;

	// prepare the message
	status_t error = link.StartMessage(AS_ACTIVATE_APP);
	if (error != B_OK)
		return error;

	error = link.Attach(link.ReceiverPort());
	if (error != B_OK)
		return error;

	error = link.Attach(team);
	if (error != B_OK)
		return error;

	// send it
	status_t code;
	error = link.FlushWithReply(code);
	if (error != B_OK)
		return error;

	return code;
}


/** @brief Launches an application by MIME type with an initial message.
 *
 *  Finds and launches the preferred application for @a mimeType.  If
 *  @a initialMessage is non-NULL it is sent to the application before
 *  ReadyToRun().
 *
 *  @param mimeType       The MIME type for which to launch an app.
 *                        Must not be @c NULL.
 *  @param initialMessage Optional message sent to the app on launch.
 *                        May be @c NULL.
 *  @param _appTeam       Optional pointer to receive the launched team ID.
 *  @return @c B_OK on success, @c B_ALREADY_RUNNING if the app was already
 *          running (single-launch), or another error code on failure.
 *  @see _LaunchApp()
 */
status_t
BRoster::Launch(const char* mimeType, BMessage* initialMessage,
	team_id* _appTeam) const
{
	if (mimeType == NULL)
		return B_BAD_VALUE;

	BList messageList;
	if (initialMessage != NULL)
		messageList.AddItem(initialMessage);

	return _LaunchApp(mimeType, NULL, &messageList, 0, NULL,
		(const char**)environ, _appTeam, NULL, NULL, NULL, false);
}


/** @brief Launches an application by MIME type with a list of messages.
 *
 *  @param mimeType    The MIME type for which to launch an app.
 *                     Must not be @c NULL.
 *  @param messageList Optional list of BMessages sent on launch.
 *                     May be @c NULL.
 *  @param _appTeam    Optional pointer to receive the launched team ID.
 *  @return @c B_OK on success, @c B_ALREADY_RUNNING if already running,
 *          or another error code on failure.
 *  @see _LaunchApp()
 */
status_t
BRoster::Launch(const char* mimeType, BList* messageList,
	team_id* _appTeam) const
{
	if (mimeType == NULL)
		return B_BAD_VALUE;

	return _LaunchApp(mimeType, NULL, messageList, 0, NULL,
		(const char**)environ, _appTeam, NULL, NULL, NULL, false);
}


/** @brief Launches an application by MIME type with command-line arguments.
 *
 *  The arguments are delivered via a @c B_ARGV_RECEIVED message.
 *
 *  @param mimeType The MIME type for which to launch an app.
 *                  Must not be @c NULL.
 *  @param argc     Number of elements in @a args.
 *  @param args     The argument vector.
 *  @param _appTeam Optional pointer to receive the launched team ID.
 *  @return @c B_OK on success, or an error code on failure.
 *  @see _LaunchApp()
 */
status_t
BRoster::Launch(const char* mimeType, int argc, const char* const* args,
	team_id* _appTeam) const
{
	if (mimeType == NULL)
		return B_BAD_VALUE;

	return _LaunchApp(mimeType, NULL, NULL, argc, args, (const char**)environ,
		_appTeam, NULL, NULL, NULL, false);
}


/** @brief Launches an application for a file with an initial message.
 *
 *  Finds and launches the preferred application for the file identified
 *  by @a ref.  The file reference is delivered via @c B_REFS_RECEIVED.
 *
 *  @param ref            An entry_ref identifying the file/application.
 *                        Must not be @c NULL.
 *  @param initialMessage Optional message sent to the app on launch.
 *                        May be @c NULL.
 *  @param _appTeam       Optional pointer to receive the launched team ID.
 *  @return @c B_OK on success, or an error code on failure.
 *  @see _LaunchApp()
 */
status_t
BRoster::Launch(const entry_ref* ref, const BMessage* initialMessage,
	team_id* _appTeam) const
{
	if (ref == NULL)
		return B_BAD_VALUE;

	BList messageList;
	if (initialMessage != NULL)
		messageList.AddItem(const_cast<BMessage*>(initialMessage));

	return _LaunchApp(NULL, ref, &messageList, 0, NULL, (const char**)environ,
		_appTeam, NULL, NULL, NULL, false);
}


/** @brief Launches an application for a file with a list of messages.
 *
 *  @param ref         An entry_ref identifying the file/application.
 *                     Must not be @c NULL.
 *  @param messageList Optional list of BMessages sent on launch.
 *                     May be @c NULL.
 *  @param appTeam     Optional pointer to receive the launched team ID.
 *  @return @c B_OK on success, or an error code on failure.
 *  @see _LaunchApp()
 */
status_t
BRoster::Launch(const entry_ref* ref, const BList* messageList,
	team_id* appTeam) const
{
	if (ref == NULL)
		return B_BAD_VALUE;

	return _LaunchApp(NULL, ref, messageList, 0, NULL, (const char**)environ,
		appTeam, NULL, NULL, NULL, false);
}


/** @brief Launches an application for a file with command-line arguments.
 *
 *  @param ref     An entry_ref identifying the file/application.
 *                 Must not be @c NULL.
 *  @param argc    Number of elements in @a args.
 *  @param args    The argument vector.
 *  @param appTeam Optional pointer to receive the launched team ID.
 *  @return @c B_OK on success, or an error code on failure.
 *  @see _LaunchApp()
 */
status_t
BRoster::Launch(const entry_ref* ref, int argc, const char* const* args,
	team_id* appTeam) const
{
	if (ref == NULL)
		return B_BAD_VALUE;

	return _LaunchApp(NULL, ref, NULL, argc, args, (const char**)environ,
		appTeam, NULL, NULL, NULL, false);
}


#if __GNUC__ == 2
// #pragma mark - Binary compatibility


extern "C" status_t
Launch__C7BRosterP9entry_refP8BMessagePl(BRoster* roster, entry_ref* ref,
	BMessage* initialMessage)
{
	return roster->BRoster::Launch(ref, initialMessage, NULL);
}


extern "C" status_t
Launch__C7BRosterPCciPPcPl(BRoster* roster, const char* mimeType,
	int argc, char** args, team_id* _appTeam)
{
	return roster->BRoster::Launch(mimeType, argc, args, _appTeam);
}


extern "C" status_t
Launch__C7BRosterP9entry_refiPPcPl(BRoster* roster, entry_ref* ref,
	int argc, char* const* args, team_id* _appTeam)
{
	return roster->BRoster::Launch(ref, argc, args, _appTeam);
}
#endif	// __GNUC__ == 2


//	#pragma mark - Recent document and app support


/** @brief Retrieves recently used documents, optionally filtered by type
 *         and/or application signature.
 *
 *  The returned @a refList message contains entry_ref values for the
 *  most recently used documents.
 *
 *  @param refList   Pointer to a BMessage to receive the results.
 *                   Must not be @c NULL.
 *  @param maxCount  Maximum number of entries to return. Must be > 0.
 *  @param fileType  Optional MIME type to filter by. May be @c NULL.
 *  @param signature Optional application signature to filter by.
 *                   May be @c NULL.
 *  @see GetRecentDocuments(BMessage*, int32, const char*[], int32, const char*)
 */
void
BRoster::GetRecentDocuments(BMessage* refList, int32 maxCount,
	const char* fileType, const char* signature) const
{
	if (refList == NULL)
		return;

	status_t error = maxCount > 0 ? B_OK : B_BAD_VALUE;

	// Use the message we've been given for both request and reply
	BMessage& message = *refList;
	BMessage& reply = *refList;
	status_t result;

	// Build and send the message, read the reply
	if (error == B_OK) {
		message.what = B_REG_GET_RECENT_DOCUMENTS;
		error = message.AddInt32("max count", maxCount);
	}
	if (error == B_OK && fileType)
		error = message.AddString("file type", fileType);

	if (error == B_OK && signature)
		error = message.AddString("app sig", signature);

	fMessenger.SendMessage(&message, &reply);
	if (error == B_OK) {
		error = reply.what == B_REG_RESULT
			? (status_t)B_OK : (status_t)B_BAD_REPLY;
	}

	if (error == B_OK)
		error = reply.FindInt32("result", &result);

	if (error == B_OK)
		error = result;

	// Clear the result if an error occured
	if (error != B_OK && refList != NULL)
		refList->MakeEmpty();

	// No return value, how sad :-(
	//return error;
}


/** @brief Retrieves recently used documents filtered by multiple file types.
 *
 *  @param refList        Pointer to a BMessage to receive the results.
 *                        Must not be @c NULL.
 *  @param maxCount       Maximum number of entries to return. Must be > 0.
 *  @param fileTypes      Array of MIME type strings to filter by.
 *                        May be @c NULL.
 *  @param fileTypesCount Number of elements in @a fileTypes.
 *  @param signature      Optional application signature to filter by.
 *                        May be @c NULL.
 *  @see GetRecentDocuments(BMessage*, int32, const char*, const char*)
 */
void
BRoster::GetRecentDocuments(BMessage* refList, int32 maxCount,
	const char* fileTypes[], int32 fileTypesCount,
	const char* signature) const
{
	if (refList == NULL)
		return;

	status_t error = maxCount > 0 ? B_OK : B_BAD_VALUE;

	// Use the message we've been given for both request and reply
	BMessage& message = *refList;
	BMessage& reply = *refList;
	status_t result;

	// Build and send the message, read the reply
	if (error == B_OK) {
		message.what = B_REG_GET_RECENT_DOCUMENTS;
		error = message.AddInt32("max count", maxCount);
	}
	if (error == B_OK && fileTypes) {
		for (int i = 0; i < fileTypesCount && error == B_OK; i++)
			error = message.AddString("file type", fileTypes[i]);
	}
	if (error == B_OK && signature)
		error = message.AddString("app sig", signature);

	fMessenger.SendMessage(&message, &reply);
	if (error == B_OK) {
		error = reply.what == B_REG_RESULT
			? (status_t)B_OK : (status_t)B_BAD_REPLY;
	}
	if (error == B_OK)
		error = reply.FindInt32("result", &result);

	if (error == B_OK)
		error = result;

	// Clear the result if an error occured
	if (error != B_OK && refList != NULL)
		refList->MakeEmpty();

	// No return value, how sad :-(
	//return error;
}


/** @brief Retrieves recently used folders.
 *
 *  @param refList   Pointer to a BMessage to receive the results.
 *                   Must not be @c NULL.
 *  @param maxCount  Maximum number of entries to return. Must be > 0.
 *  @param signature Optional application signature to filter by.
 *                   May be @c NULL.
 */
void
BRoster::GetRecentFolders(BMessage* refList, int32 maxCount,
	const char* signature) const
{
	if (refList == NULL)
		return;

	status_t error = maxCount > 0 ? B_OK : B_BAD_VALUE;

	// Use the message we've been given for both request and reply
	BMessage& message = *refList;
	BMessage& reply = *refList;
	status_t result;

	// Build and send the message, read the reply
	if (error == B_OK) {
		message.what = B_REG_GET_RECENT_FOLDERS;
		error = message.AddInt32("max count", maxCount);
	}
	if (error == B_OK && signature)
		error = message.AddString("app sig", signature);

	fMessenger.SendMessage(&message, &reply);
	if (error == B_OK) {
		error = reply.what == B_REG_RESULT
			? (status_t)B_OK : (status_t)B_BAD_REPLY;
	}

	if (error == B_OK)
		error = reply.FindInt32("result", &result);

	if (error == B_OK)
		error = result;

	// Clear the result if an error occured
	if (error != B_OK && refList != NULL)
		refList->MakeEmpty();

	// No return value, how sad :-(
	//return error;
}


/** @brief Retrieves recently launched applications.
 *
 *  @param refList  Pointer to a BMessage to receive the results.
 *                  Must not be @c NULL.
 *  @param maxCount Maximum number of entries to return. Must be > 0.
 */
void
BRoster::GetRecentApps(BMessage* refList, int32 maxCount) const
{
	if (refList == NULL)
		return;

	status_t err = maxCount > 0 ? B_OK : B_BAD_VALUE;

	// Use the message we've been given for both request and reply
	BMessage& message = *refList;
	BMessage& reply = *refList;
	status_t result;

	// Build and send the message, read the reply
	if (!err) {
		message.what = B_REG_GET_RECENT_APPS;
		err = message.AddInt32("max count", maxCount);
	}
	fMessenger.SendMessage(&message, &reply);
	if (!err) {
		err = reply.what == B_REG_RESULT
			? (status_t)B_OK : (status_t)B_BAD_REPLY;
	}
	if (!err)
		err = reply.FindInt32("result", &result);

	if (!err)
		err = result;

	// Clear the result if an error occured
	if (err && refList)
		refList->MakeEmpty();

	// No return value, how sad :-(
	//return err;
}


/** @brief Adds a document to the recent documents list.
 *
 *  If @a signature is @c NULL the calling application's signature is
 *  used instead.
 *
 *  @param document  An entry_ref identifying the document file.
 *                   Must not be @c NULL.
 *  @param signature Optional application signature to associate.
 *                   May be @c NULL.
 *  @see GetRecentDocuments()
 */
void
BRoster::AddToRecentDocuments(const entry_ref* document,
	const char* signature) const
{
	status_t error = document ? B_OK : B_BAD_VALUE;

	// Use the message we've been given for both request and reply
	BMessage message(B_REG_ADD_TO_RECENT_DOCUMENTS);
	BMessage reply;
	status_t result;
	char* callingApplicationSignature = NULL;

	// If no signature is supplied, look up the signature of
	// the calling app
	if (error == B_OK && signature == NULL) {
		app_info info;
		error = GetRunningAppInfo(be_app->Team(), &info);
		if (error == B_OK)
			callingApplicationSignature = info.signature;
	}

	// Build and send the message, read the reply
	if (error == B_OK)
		error = message.AddRef("ref", document);

	if (error == B_OK) {
		error = message.AddString("app sig", signature != NULL
			? signature : callingApplicationSignature);
	}
	fMessenger.SendMessage(&message, &reply);
	if (error == B_OK) {
		error = reply.what == B_REG_RESULT
			? (status_t)B_OK : (status_t)B_BAD_REPLY;
	}
	if (error == B_OK)
		error = reply.FindInt32("result", &result);

	if (error == B_OK)
		error = result;

	if (error != B_OK) {
		DBG(OUT("WARNING: BRoster::AddToRecentDocuments() failed with error "
			"0x%" B_PRIx32 "\n", error));
	}
}


/** @brief Adds a folder to the recent folders list.
 *
 *  If @a signature is @c NULL the calling application's signature is
 *  used instead.
 *
 *  @param folder    An entry_ref identifying the folder.
 *                   Must not be @c NULL.
 *  @param signature Optional application signature to associate.
 *                   May be @c NULL.
 *  @see GetRecentFolders()
 */
void
BRoster::AddToRecentFolders(const entry_ref* folder,
	const char* signature) const
{
	status_t error = folder ? B_OK : B_BAD_VALUE;

	// Use the message we've been given for both request and reply
	BMessage message(B_REG_ADD_TO_RECENT_FOLDERS);
	BMessage reply;
	status_t result;
	char* callingApplicationSignature = NULL;

	// If no signature is supplied, look up the signature of
	// the calling app
	if (error == B_OK && signature == NULL) {
		app_info info;
		error = GetRunningAppInfo(be_app->Team(), &info);
		if (error == B_OK)
			callingApplicationSignature = info.signature;
	}

	// Build and send the message, read the reply
	if (error == B_OK)
		error = message.AddRef("ref", folder);

	if (error == B_OK) {
		error = message.AddString("app sig",
			signature != NULL ? signature : callingApplicationSignature);
	}
	fMessenger.SendMessage(&message, &reply);
	if (error == B_OK) {
		error = reply.what == B_REG_RESULT
			? (status_t)B_OK : (status_t)B_BAD_REPLY;
	}
	if (error == B_OK)
		error = reply.FindInt32("result", &result);

	if (error == B_OK)
		error = result;

	if (error != B_OK) {
		DBG(OUT("WARNING: BRoster::AddToRecentDocuments() failed with error "
			"0x%" B_PRIx32 "\n", error));
	}
}

//	#pragma mark - Private or reserved


/** @brief Shuts down the system.
 *
 *  When @a synchronous is @c true and the method succeeds, it does not
 *  return.
 *
 *  @param reboot      If @c true, the system will be rebooted instead of
 *                     being powered off.
 *  @param confirm     If @c true, the user will be asked to confirm the
 *                     shutdown.
 *  @param synchronous If @c false, the method returns as soon as the
 *                     shutdown process has been initiated.  Otherwise it
 *                     does not return on success.
 *  @return @c B_OK on success, @c B_SHUTTING_DOWN if already in progress,
 *          @c B_SHUTDOWN_CANCELLED if the user cancelled, or another
 *          error code.
 */
status_t
BRoster::_ShutDown(bool reboot, bool confirm, bool synchronous)
{
	status_t error = B_OK;

	// compose the request message
	BMessage request(B_REG_SHUT_DOWN);
	if (error == B_OK)
		error = request.AddBool("reboot", reboot);

	if (error == B_OK)
		error = request.AddBool("confirm", confirm);

	if (error == B_OK)
		error = request.AddBool("synchronous", synchronous);

	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);

	// evaluate the reply
	if (error == B_OK && reply.what != B_REG_SUCCESS
		&& reply.FindInt32("error", &error) != B_OK) {
		error = B_ERROR;
	}

	return error;
}


/** @brief Checks whether a system shutdown is currently in progress.
 *
 *  @param inProgress Pointer to a bool to be filled in with @c true if
 *                    a shutdown is in progress, @c false otherwise.
 *                    May be @c NULL.
 *  @return @c B_OK on success, or an error code on failure.
 *  @see _ShutDown()
 */
status_t
BRoster::_IsShutDownInProgress(bool* inProgress)
{
	status_t error = B_OK;

	// compose the request message
	BMessage request(B_REG_IS_SHUT_DOWN_IN_PROGRESS);

	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);

	// evaluate the reply
	if (error == B_OK) {
		if (reply.what == B_REG_SUCCESS) {
			if (inProgress != NULL
				&& reply.FindBool("in-progress", inProgress) != B_OK) {
				error = B_ERROR;
			}
		} else if (reply.FindInt32("error", &error) != B_OK)
			error = B_ERROR;
	}

	return error;
}



/** @brief (Pre-)Registers an application with the registrar.
 *
 *  Full registration requires all parameters to be valid.  For
 *  pre-registration, @a team and @a thread may be -1; a unique token
 *  is returned via @a pToken.  Registration may fail with
 *  @c B_ALREADY_RUNNING if single/exclusive launch is requested and an
 *  instance is already running.
 *
 *  @param signature        The application's MIME signature.
 *  @param ref              An entry_ref to the application executable.
 *  @param flags            The application's launch flags.
 *  @param team             The team ID, or -1 for pre-registration.
 *  @param thread           The main thread ID, or -1.
 *  @param port             The looper port ID, or -1.
 *  @param fullRegistration @c true for full registration, @c false for
 *                          pre-registration.
 *  @param pToken           Receives the registration token on success.
 *                          May be @c NULL.
 *  @param otherTeam        Receives the team ID of an already-running
 *                          instance on @c B_ALREADY_RUNNING. May be @c NULL.
 *  @return @c B_OK on success, @c B_ALREADY_RUNNING if a single-launch
 *          app is already running, @c B_REG_ALREADY_REGISTERED if the
 *          team is already registered, or another error code.
 *  @see _RemovePreRegApp(), _RemoveApp()
 */
status_t
BRoster::_AddApplication(const char* signature, const entry_ref* ref,
	uint32 flags, team_id team, thread_id thread, port_id port,
	bool fullRegistration, uint32* pToken, team_id* otherTeam) const
{
	status_t error = B_OK;

	// compose the request message
	BMessage request(B_REG_ADD_APP);
	if (error == B_OK && signature != NULL)
		error = request.AddString("signature", signature);

	if (error == B_OK && ref != NULL)
		error = request.AddRef("ref", ref);

	if (error == B_OK)
		error = request.AddInt32("flags", (int32)flags);

	if (error == B_OK && team >= 0)
		error = request.AddInt32("team", team);

	if (error == B_OK && thread >= 0)
		error = request.AddInt32("thread", thread);

	if (error == B_OK && port >= 0)
		error = request.AddInt32("port", port);

	if (error == B_OK)
		error = request.AddBool("full_registration", fullRegistration);

	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);

	// evaluate the reply
	if (error == B_OK) {
		if (reply.what == B_REG_SUCCESS) {
			if (!fullRegistration && team < 0) {
				uint32 token;
				if (reply.FindInt32("token", (int32*)&token) == B_OK) {
					if (pToken != NULL)
						*pToken = token;
				} else
					error = B_ERROR;
			}
		} else {
			if (reply.FindInt32("error", &error) != B_OK)
				error = B_ERROR;

			// get team and token from the reply
			if (otherTeam != NULL
				&& reply.FindInt32("other_team", otherTeam) != B_OK) {
				*otherTeam = -1;
			}
			if (pToken != NULL
				&& reply.FindInt32("token", (int32*)pToken) != B_OK) {
				*pToken = 0;
			}
		}
	}

	return error;
}


/** @brief Sets a registered application's signature.
 *
 *  The application must already be registered or pre-registered with a
 *  valid team ID.
 *
 *  @param team      The application's team ID.
 *  @param signature The new MIME signature string.
 *  @return @c B_OK on success, @c B_REG_APP_NOT_REGISTERED if the team
 *          is not registered, or another error code.
 */
status_t
BRoster::_SetSignature(team_id team, const char* signature) const
{
	status_t error = B_OK;

	// compose the request message
	BMessage request(B_REG_SET_SIGNATURE);
	if (team >= 0)
		error = request.AddInt32("team", team);

	if (error == B_OK && signature)
		error = request.AddString("signature", signature);

	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);

	// evaluate the reply
	if (error == B_OK && reply.what != B_REG_SUCCESS
		&& reply.FindInt32("error", &error) != B_OK) {
		error = B_ERROR;
	}

	return error;
}


/** @brief Sets the thread ID for a registered application (stub).
 *
 *  @param team   The application's team ID.
 *  @param thread The application's main thread ID.
 *  @todo Determine if this method is still needed.
 */
void
BRoster::_SetThread(team_id team, thread_id thread) const
{
}


/** @brief Sets the team and thread IDs for a pre-registered application.
 *
 *  After pre-registration via _AddApplication() without a team ID, this
 *  method must be called to assign the actual team and thread IDs.
 *
 *  @param entryToken The pre-registration token returned by _AddApplication().
 *  @param thread     The application's main thread ID.
 *  @param team       The application's team ID.
 *  @param _port      Optional pointer to receive the assigned looper port.
 *  @return @c B_OK on success, @c B_REG_APP_NOT_PRE_REGISTERED if the
 *          token is invalid, or another error code.
 *  @see _AddApplication()
 */
status_t
BRoster::_SetThreadAndTeam(uint32 entryToken, thread_id thread,
	team_id team, port_id* _port) const
{
	status_t error = B_OK;

	// compose the request message
	BMessage request(B_REG_SET_THREAD_AND_TEAM);
	if (error == B_OK)
		error = request.AddInt32("token", (int32)entryToken);

	if (error == B_OK && team >= 0)
		error = request.AddInt32("team", team);

	if (error == B_OK && thread >= 0)
		error = request.AddInt32("thread", thread);

	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);

	// evaluate the reply
	if (error == B_OK && reply.what != B_REG_SUCCESS
		&& reply.FindInt32("error", &error) != B_OK)
		error = B_ERROR;

	if (error == B_OK && _port != NULL)
		*_port = reply.GetInt32("port", -1);

	return error;
}


/** @brief Completes registration for a pre-registered application.
 *
 *  After pre-registration and assignment of thread/team IDs, this
 *  method finalizes the registration so the application is fully
 *  registered in the roster.
 *
 *  @param team   The application's team ID.
 *  @param thread The application's main thread ID.
 *  @param port   The application's looper port ID.
 *  @return @c B_OK on success, @c B_REG_APP_NOT_PRE_REGISTERED if the
 *          team is not pre-registered or is already fully registered,
 *          or another error code.
 *  @see _AddApplication(), _SetThreadAndTeam()
 */
status_t
BRoster::_CompleteRegistration(team_id team, thread_id thread,
	port_id port) const
{
	status_t error = B_OK;

	// compose the request message
	BMessage request(B_REG_COMPLETE_REGISTRATION);
	if (team >= 0)
		error = request.AddInt32("team", team);

	if (error == B_OK && thread >= 0)
		error = request.AddInt32("thread", thread);

	if (error == B_OK && port >= 0)
		error = request.AddInt32("port", port);

	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);

	// evaluate the reply
	if (error == B_OK && reply.what != B_REG_SUCCESS
		&& reply.FindInt32("error", &error) != B_OK) {
		error = B_ERROR;
	}

	return error;
}


/** @brief Checks whether an application is registered with the registrar.
 *
 *  If the application is registered, @a preRegistered indicates whether
 *  it is only pre-registered, and @a info (if non-NULL) is filled with
 *  the application's info.
 *
 *  @param ref           An entry_ref for the application executable.
 *                       May be @c NULL.
 *  @param team          The team ID, or -1 if @a token is used instead.
 *  @param token         The pre-registration token, or 0 if @a team is used.
 *  @param preRegistered Pointer to a bool set to @c true if the app is
 *                       pre-registered, @c false if fully registered.
 *                       May be @c NULL.
 *  @param info          Pointer to an app_info to fill in. May be @c NULL.
 *  @return @c B_OK if registered, or an error code if not registered or
 *          an error occurred.
 */
status_t
BRoster::_IsAppRegistered(const entry_ref* ref, team_id team,
	uint32 token, bool* preRegistered, app_info* info) const
{
	status_t error = B_OK;

	// compose the request message
	BMessage request(B_REG_IS_APP_REGISTERED);
	if (ref)
		error = request.AddRef("ref", ref);
	if (error == B_OK && team >= 0)
		error = request.AddInt32("team", team);
	if (error == B_OK && token > 0)
		error = request.AddInt32("token", (int32)token);

	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);

	// evaluate the reply
	bool isRegistered = false;
	bool isPreRegistered = false;
	if (error == B_OK) {
		if (reply.what == B_REG_SUCCESS) {
			if (reply.FindBool("registered", &isRegistered) != B_OK
				|| !isRegistered
				|| reply.FindBool("pre-registered", &isPreRegistered) != B_OK) {
				error = B_ERROR;
			}

			if (error == B_OK && preRegistered)
				*preRegistered = isPreRegistered;
			if (error == B_OK && info)
				error = find_message_app_info(&reply, info);
		} else if (reply.FindInt32("error", &error) != B_OK)
			error = B_ERROR;
	}

	return error;
}


/** @brief Unregisters a pre-registered application that has no team ID.
 *
 *  This method only works for applications that have not yet been
 *  assigned a team ID.  Applications with a team ID must be unregistered
 *  via _RemoveApp().
 *
 *  @param entryToken The pre-registration token returned by _AddApplication().
 *  @return @c B_OK on success, @c B_REG_APP_NOT_PRE_REGISTERED if the
 *          token does not identify a pre-registered application.
 *  @see _RemoveApp()
 */
status_t
BRoster::_RemovePreRegApp(uint32 entryToken) const
{
	status_t error = B_OK;

	// compose the request message
	BMessage request(B_REG_REMOVE_PRE_REGISTERED_APP);
	if (error == B_OK)
		error = request.AddInt32("token", (int32)entryToken);

	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);

	// evaluate the reply
	if (error == B_OK && reply.what != B_REG_SUCCESS
		&& reply.FindInt32("error", &error) != B_OK) {
		error = B_ERROR;
	}

	return error;
}


/** @brief Unregisters a (pre-)registered application by team ID.
 *
 *  Use this for applications that have a team ID assigned (including
 *  pre-registered applications after _SetThreadAndTeam()).
 *
 *  @param team The application's team ID.
 *  @return @c B_OK on success, @c B_REG_APP_NOT_REGISTERED if the team
 *          is not registered, or another error code.
 *  @see _RemovePreRegApp()
 */
status_t
BRoster::_RemoveApp(team_id team) const
{
	status_t error = B_OK;

	// compose the request message
	BMessage request(B_REG_REMOVE_APP);
	if (team >= 0)
		error = request.AddInt32("team", team);

	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);

	// evaluate the reply
	if (error == B_OK && reply.what != B_REG_SUCCESS
		&& reply.FindInt32("error", &error) != B_OK) {
		error = B_ERROR;
	}

	return error;
}


/** @brief Notifies the app_server that an application has crashed.
 *
 *  Sends an @c AS_APP_CRASHED message so the desktop can clean up
 *  resources associated with the crashed application.
 *
 *  @param team The team ID of the crashed application.
 */
void
BRoster::_ApplicationCrashed(team_id team)
{
	BPrivate::DesktopLink link;
	if (link.InitCheck() != B_OK)
		return;

	if (link.StartMessage(AS_APP_CRASHED) == B_OK
		&& link.Attach(team) == B_OK) {
		link.Flush();
	}
}


/** @brief Notifies the registrar of the currently active application.
 *
 *  Called by the app_server when the active application changes.
 *  Because this runs in the event loop, the message is sent
 *  asynchronously without waiting for a reply.
 *
 *  @param team The team ID of the newly active application.
 *  @return @c B_OK on success, @c B_BAD_TEAM_ID if @a team is negative,
 *          or another error code.
 */
status_t
BRoster::_UpdateActiveApp(team_id team) const
{
	if (team < B_OK)
		return B_BAD_TEAM_ID;

	// compose the request message
	BMessage request(B_REG_UPDATE_ACTIVE_APP);
	status_t status = request.AddInt32("team", team);
	if (status < B_OK)
		return status;

	// send the request
	return fMessenger.SendMessage(&request);
}


/** @brief Core implementation that launches an application by MIME type
 *         or entry_ref.
 *
 *  At least one of @a mimeType or @a ref must be non-NULL.  The
 *  application is resolved the same way FindApp() does.  Messages in
 *  @a messageList are sent before ReadyToRun(); @a argc/@a args are
 *  delivered as @c B_ARGV_RECEIVED.  If @a launchSuspended is @c true
 *  the main thread is not resumed.
 *
 *  @param mimeType        MIME type to resolve. May be @c NULL.
 *  @param ref             Entry_ref for the file/application. May be @c NULL.
 *  @param messageList     Optional list of BMessages to send on launch.
 *  @param argc            Number of elements in @a args.
 *  @param args            Argument vector for @c B_ARGV_RECEIVED.
 *  @param environment     Environment variable array for load_image().
 *  @param _appTeam        Receives the launched team ID. May be @c NULL.
 *  @param _appThread      Receives the main thread ID. May be @c NULL.
 *  @param _appPort        Receives the looper port ID. May be @c NULL.
 *  @param _appToken       Receives the registration token. May be @c NULL.
 *  @param launchSuspended If @c true, the main thread is kept suspended.
 *  @return @c B_OK on success, @c B_ALREADY_RUNNING if a single-launch
 *          app was already running, @c B_LAUNCH_FAILED_NO_PREFERRED_APP,
 *          @c B_LAUNCH_FAILED_APP_NOT_FOUND,
 *          @c B_LAUNCH_FAILED_APP_IN_TRASH,
 *          @c B_LAUNCH_FAILED_EXECUTABLE, or another error code.
 *  @see FindApp(), _ResolveApp(), _SendToRunning()
 */
status_t
BRoster::_LaunchApp(const char* mimeType, const entry_ref* ref,
	const BList* messageList, int argc, const char* const* args,
	const char** environment, team_id* _appTeam, thread_id* _appThread,
	port_id* _appPort, uint32* _appToken, bool launchSuspended) const
{
	DBG(OUT("BRoster::_LaunchApp()"));

	if (_appTeam != NULL) {
		// we're supposed to set _appTeam to -1 on error; we'll
		// reset it later if everything goes well
		*_appTeam = -1;
	}

	if (mimeType == NULL && ref == NULL)
		return B_BAD_VALUE;

	// use a mutable copy of the document entry_ref
	entry_ref _docRef;
	entry_ref* docRef = NULL;
	if (ref != NULL) {
		_docRef = *ref;
		docRef = &_docRef;
	}

	uint32 otherAppFlags = B_REG_DEFAULT_APP_FLAGS;
	uint32 appFlags = B_REG_DEFAULT_APP_FLAGS;
	bool alreadyRunning = false;
	bool wasDocument = true;
	status_t error = B_OK;
	ArgVector argVector;
	team_id team = -1;
	thread_id appThread = -1;
	port_id appPort = -1;
	uint32 appToken = 0;
	entry_ref hintRef;

	while (true) {
		// find the app
		entry_ref appRef;
		char signature[B_MIME_TYPE_LENGTH];
		error = _ResolveApp(mimeType, docRef, &appRef, signature,
			&appFlags, &wasDocument);
		DBG(OUT("  find app: %s (%" B_PRIx32 ") %s \n", strerror(error), error,
			signature));

		if (error != B_OK)
			return error;

		// build an argument vector
		error = argVector.Init(argc, args, &appRef,
			wasDocument ? docRef : NULL);
		DBG(OUT("  build argv: %s (%" B_PRIx32 ")\n", strerror(error), error));
		if (error != B_OK)
			return error;

		// pre-register the app (but ignore scipts)
		app_info appInfo;
		bool isScript = wasDocument && docRef != NULL && *docRef == appRef;
		if (!isScript && !fNoRegistrar) {
			error = _AddApplication(signature, &appRef, appFlags, -1, -1, -1,
				false, &appToken, &team);
			if (error == B_ALREADY_RUNNING) {
				DBG(OUT("  already running\n"));
				alreadyRunning = true;

				// get the app flags for the running application
				error = _IsAppRegistered(&appRef, team, appToken, NULL,
					&appInfo);
				if (error == B_OK) {
					otherAppFlags = appInfo.flags;
					appPort = appInfo.port;
					team = appInfo.team;
				}
			}
			DBG(OUT("  pre-register: %s (%" B_PRIx32 ")\n", strerror(error),
				error));
		}

		// launch the app
		if (error == B_OK && !alreadyRunning) {
			DBG(OUT("  token: %" B_PRIu32 "\n", appToken));
			// load the app image
			appThread = load_image(argVector.Count(),
				const_cast<const char**>(argVector.Args()), environment);

			// get the app team
			if (appThread >= 0) {
				thread_info threadInfo;
				error = get_thread_info(appThread, &threadInfo);
				if (error == B_OK)
					team = threadInfo.team;
			} else if (wasDocument && appThread == B_NOT_AN_EXECUTABLE)
				error = B_LAUNCH_FAILED_EXECUTABLE;
			else
				error = appThread;

			DBG(OUT("  load image: %s (%" B_PRIx32 ")\n", strerror(error),
				error));
			// finish the registration
			if (error == B_OK && !isScript && !fNoRegistrar)
				error = _SetThreadAndTeam(appToken, appThread, team, &appPort);

			DBG(OUT("  set thread and team: %s (%" B_PRIx32 ")\n",
				strerror(error), error));
			// resume the launched team
			if (error == B_OK && !launchSuspended)
				error = resume_thread(appThread);

			DBG(OUT("  resume thread: %s (%" B_PRIx32 ")\n", strerror(error),
				error));
			// on error: kill the launched team and unregister the app
			if (error != B_OK) {
				if (appThread >= 0)
					kill_thread(appThread);

				if (!isScript) {
					if (!fNoRegistrar)
						_RemovePreRegApp(appToken);

					if (!wasDocument) {
						// Did we already try this?
						if (appRef == hintRef)
							break;

						// Remove app hint if it's this one
						BMimeType appType(signature);

						if (appType.InitCheck() == B_OK
							&& appType.GetAppHint(&hintRef) == B_OK
							&& appRef == hintRef) {
							appType.SetAppHint(NULL);
							// try again with the app hint removed
							continue;
						}
					}
				}
			}
		}
		// Don't try again
		break;
	}

	if (alreadyRunning && current_team() == team) {
		// The target team is calling us, so we don't send it the message
		// to prevent an endless loop
		error = B_BAD_VALUE;
	}

	// send "on launch" messages
	if (error == B_OK && !fNoRegistrar) {
		// If the target app is B_ARGV_ONLY, only if it is newly launched
		// messages are sent to it (namely B_ARGV_RECEIVED and B_READY_TO_RUN).
		// An already running B_ARGV_ONLY app won't get any messages.
		bool argvOnly = (appFlags & B_ARGV_ONLY) != 0
			|| (alreadyRunning && (otherAppFlags & B_ARGV_ONLY) != 0);
		const BList* _messageList = (argvOnly ? NULL : messageList);
		// don't send ref, if it refers to the app or is included in the
		// argument vector
		const entry_ref* _ref = argvOnly || !wasDocument
			|| argVector.Count() > 1 ? NULL : docRef;
		if (!(argvOnly && alreadyRunning)) {
			_SendToRunning(team, argVector.Count(), argVector.Args(),
				_messageList, _ref, alreadyRunning);
		}
	}

	// set return values
	if (error == B_OK) {
		if (alreadyRunning)
			error = B_ALREADY_RUNNING;
		else if (_appTeam)
			*_appTeam = team;

		if (_appThread != NULL)
			*_appThread = appThread;
		if (_appPort != NULL)
			*_appPort = appPort;
		if (_appToken != NULL)
			*_appToken = appToken;
	}

	DBG(OUT("BRoster::_LaunchApp() done: %s (%" B_PRIx32 ")\n",
		strerror(error), error));

	return error;
}


/** @brief Sets the application flags for a registered app (stub).
 *
 *  @param team  The application's team ID.
 *  @param flags The new application flags.
 */
void
BRoster::_SetAppFlags(team_id team, uint32 flags) const
{
}


/** @brief Dumps the roster state for debugging purposes (stub).
 */
void
BRoster::_DumpRoster() const
{
}


/** @brief Resolves the preferred application for a MIME type or file.
 *
 *  At least one of @a inType or @a ref must be non-NULL.  If @a inType
 *  is supplied, @a ref is ignored.  Symbolic links in @a ref are
 *  resolved.  Also returns the application signature, flags, and whether
 *  the input identified a document rather than an app.
 *
 *  @param inType       The MIME type. May be @c NULL.
 *  @param ref          The file entry_ref. May be @c NULL.
 *  @param _appRef      Receives the resolved application entry_ref.
 *                      May be @c NULL.
 *  @param _signature   Buffer of at least @c B_MIME_TYPE_LENGTH to receive
 *                      the application's signature. May be @c NULL.
 *  @param _appFlags    Receives the application's flags. May be @c NULL.
 *  @param _wasDocument Set to @c true if the input was a document, @c false
 *                      if it was already an application. May be @c NULL.
 *  @return @c B_OK on success, @c B_BAD_VALUE if both @a inType and @a ref
 *          are @c NULL, or another error code.
 *  @see FindApp(), _TranslateRef(), _TranslateType()
 */
status_t
BRoster::_ResolveApp(const char* inType, entry_ref* ref,
	entry_ref* _appRef, char* _signature, uint32* _appFlags,
	bool* _wasDocument) const
{
	if ((inType == NULL && ref == NULL)
		|| (inType != NULL && strlen(inType) >= B_MIME_TYPE_LENGTH))
		return B_BAD_VALUE;

	// find the app
	BMimeType appMeta;
	BFile appFile;
	entry_ref appRef;
	status_t error;

	if (inType != NULL) {
		error = _TranslateType(inType, &appMeta, &appRef, &appFile);
		if (_wasDocument != NULL)
			*_wasDocument = !(appMeta == inType);
	} else {
		error = _TranslateRef(ref, &appMeta, &appRef, &appFile,
			_wasDocument);
	}

	// create meta mime
	if (!fNoRegistrar && error == B_OK) {
		BPath path;
		if (path.SetTo(&appRef) == B_OK)
			create_app_meta_mime(path.Path(), false, true, false);
	}

	// set the app hint on the type -- but only if the file has the
	// respective signature, otherwise unset the app hint
	BAppFileInfo appFileInfo;
	if (!fNoRegistrar && error == B_OK) {
		char signature[B_MIME_TYPE_LENGTH];
		if (appFileInfo.SetTo(&appFile) == B_OK
			&& appFileInfo.GetSignature(signature) == B_OK) {
			if (!strcasecmp(appMeta.Type(), signature)) {
				// Only set the app hint if there is none yet
				entry_ref dummyRef;
				if (appMeta.GetAppHint(&dummyRef) != B_OK)
					appMeta.SetAppHint(&appRef);
			} else {
				appMeta.SetAppHint(NULL);
				appMeta.SetTo(signature);
			}
		} else
			appMeta.SetAppHint(NULL);
	}

	// set the return values
	if (error == B_OK) {
		if (_appRef)
			*_appRef = appRef;

		if (_signature != NULL) {
			// there's no warranty, that appMeta is valid
			if (appMeta.IsValid()) {
				strlcpy(_signature, appMeta.Type(),
					B_MIME_TYPE_LENGTH);
			} else
				_signature[0] = '\0';
		}

		if (_appFlags != NULL) {
			// if an error occurs here, we don't care and just set a default
			// value
			if (appFileInfo.InitCheck() != B_OK
				|| appFileInfo.GetAppFlags(_appFlags) != B_OK) {
				*_appFlags = B_REG_DEFAULT_APP_FLAGS;
			}
		}
	} else {
		// unset the ref on error
		if (_appRef != NULL)
			*_appRef = appRef;
	}

	return error;
}


/** @brief Finds the application associated with a file reference.
 *
 *  Resolves symbolic links, checks executability, and determines the
 *  application that should handle the file.  @a appMeta is left
 *  unmodified if the file is executable but has no signature.
 *
 *  @param ref          The file entry_ref.  Updated if it was a symlink.
 *                      Must not be @c NULL.
 *  @param appMeta      Receives the found application's MIME type.
 *                      Must not be @c NULL.
 *  @param appRef       Receives the found application's entry_ref.
 *                      Must not be @c NULL.
 *  @param appFile      Receives the found application's BFile.
 *                      Must not be @c NULL.
 *  @param _wasDocument Set to @c true if @a ref was a document rather
 *                      than an application. May be @c NULL.
 *  @return @c B_OK on success, @c B_BAD_VALUE if required parameters are
 *          @c NULL, or another error code.
 *  @see _TranslateType(), _ResolveApp()
 */
status_t
BRoster::_TranslateRef(entry_ref* ref, BMimeType* appMeta,
	entry_ref* appRef, BFile* appFile, bool* _wasDocument) const
{
	if (ref == NULL || appMeta == NULL || appRef == NULL || appFile == NULL)
		return B_BAD_VALUE;

	entry_ref originalRef = *ref;

	// resolve ref, if necessary
	BEntry entry;
	status_t error = entry.SetTo(ref, false);
	if (error != B_OK)
		return error;

	if (entry.IsSymLink()) {
		// ref refers to a link
		if (entry.SetTo(ref, true) != B_OK || entry.GetRef(ref) != B_OK)
			return B_LAUNCH_FAILED_NO_RESOLVE_LINK;
	}

	// init node
	BNode node;
	error = node.SetTo(ref);
	if (error != B_OK)
		return error;

	// get permissions
	mode_t permissions;
	error = node.GetPermissions(&permissions);
	if (error != B_OK)
		return error;

	if ((permissions & S_IXUSR) != 0 && node.IsFile()) {
		// node is executable and a file
		error = appFile->SetTo(ref, B_READ_ONLY);
		if (error != B_OK)
			return error;

		// get the app's signature via a BAppFileInfo
		BAppFileInfo appFileInfo;
		error = appFileInfo.SetTo(appFile);
		if (error != B_OK)
			return error;

		// don't worry, if the file doesn't have a signature, just
		// unset the supplied object
		char type[B_MIME_TYPE_LENGTH];
		if (appFileInfo.GetSignature(type) == B_OK) {
			error = appMeta->SetTo(type);
			if (error != B_OK)
				return error;
		} else
			appMeta->Unset();

		// If the file type indicates that the file is an application, we've
		// definitely got what we're looking for.
		bool isDocument = true;
		if (_GetFileType(ref, &appFileInfo, type) == B_OK
			&& strcasecmp(type, B_APP_MIME_TYPE) == 0) {
			isDocument = false;
		}

		// If our file is not an application executable, we probably have a
		// script. Check whether the file has a preferred application set. If
		// so, we fall through and use the preferred app instead. Otherwise
		// we're done.
		char preferredApp[B_MIME_TYPE_LENGTH];
		if (!isDocument || appFileInfo.GetPreferredApp(preferredApp) != B_OK) {
			// If we were given a symlink, point appRef to it in case its name
			// or attributes are relevant.
			*appRef = originalRef;
			if (_wasDocument != NULL)
				*_wasDocument = isDocument;

			return B_OK;
		}

		// Executable file, but not an application, and it has a preferred
		// application set. Fall through...
	}

	// the node is not exectuable or not a file
	// init a node info
	BNodeInfo nodeInfo;
	error = nodeInfo.SetTo(&node);
	if (error != B_OK)
		return error;

	// if the file has a preferred app, let _TranslateType() find
	// it for us
	char preferredApp[B_MIME_TYPE_LENGTH];
	if (nodeInfo.GetPreferredApp(preferredApp) == B_OK
		&& _TranslateType(preferredApp, appMeta, appRef, appFile) == B_OK) {
		if (_wasDocument != NULL)
			*_wasDocument = true;

		return B_OK;
	}

	// no preferred app or existing one was not found -- we
	// need to get the file's type

	// get the type from the file
	char fileType[B_MIME_TYPE_LENGTH];
	error = _GetFileType(ref, &nodeInfo, fileType);
	if (error != B_OK)
		return error;

	// now let _TranslateType() do the actual work
	error = _TranslateType(fileType, appMeta, appRef, appFile);
	if (error != B_OK)
		return error;

	if (_wasDocument != NULL)
		*_wasDocument = true;

	return B_OK;
}


/** @brief Finds the preferred application for a MIME type.
 *
 *  Iterates over preferred and supporting applications in priority
 *  order, attempting to resolve each via app hints and filesystem
 *  queries until a usable application is found.
 *
 *  @param mimeType The MIME type string. Must not be @c NULL.
 *  @param appMeta  Receives the found application's BMimeType.
 *                  Must not be @c NULL.
 *  @param appRef   Receives the found application's entry_ref.
 *                  Must not be @c NULL.
 *  @param appFile  Receives the found application's BFile.
 *                  Must not be @c NULL.
 *  @return @c B_OK on success, @c B_BAD_VALUE if parameters are @c NULL,
 *          @c B_LAUNCH_FAILED_NO_PREFERRED_APP if no suitable application
 *          was found, or another error code.
 *  @see _TranslateRef(), _ResolveApp()
 */
status_t
BRoster::_TranslateType(const char* mimeType, BMimeType* appMeta,
	entry_ref* appRef, BFile* appFile) const
{
	if (mimeType == NULL || appMeta == NULL || appRef == NULL
		|| appFile == NULL || strlen(mimeType) >= B_MIME_TYPE_LENGTH) {
		return B_BAD_VALUE;
	}

	// Create a BMimeType and check, if the type is installed.
	BMimeType type;
	status_t error = type.SetTo(mimeType);

	// Get the preferred apps from the sub and super type.
	char primarySignature[B_MIME_TYPE_LENGTH];
	char secondarySignature[B_MIME_TYPE_LENGTH];
	primarySignature[0] = '\0';
	secondarySignature[0] = '\0';

	if (error == B_OK) {
		BMimeType superType;
		if (type.GetSupertype(&superType) == B_OK)
			superType.GetPreferredApp(secondarySignature);
		if (type.IsInstalled()) {
			if (type.GetPreferredApp(primarySignature) != B_OK) {
				// The type is installed, but has no preferred app.
				primarySignature[0] = '\0';
			} else if (!strcmp(primarySignature, secondarySignature)) {
				// Both types have the same preferred app, there is
				// no point in testing it twice.
				secondarySignature[0] = '\0';
			}
		} else {
			// The type is not installed. We assume it is an app signature.
			strlcpy(primarySignature, mimeType, sizeof(primarySignature));
		}
	}

	// We will use this BMessage "signatures" to hold all supporting apps
	// so we can iterator over them in the preferred order. We include
	// the supporting apps in such a way that the configured preferred
	// applications for the MIME type are in front of other supporting
	// applications for the sub and the super type respectively.
	const char* kSigField = "applications";
	BMessage signatures;
	bool addedSecondarySignature = false;
	if (error == B_OK) {
		if (primarySignature[0] != '\0')
			error = signatures.AddString(kSigField, primarySignature);
		else {
			// If there is a preferred app configured for the super type,
			// but no preferred type for the sub-type, add the preferred
			// super type handler in front of any other handlers. This way
			// we fall-back to non-preferred but supporting apps only in the
			// case when there is a preferred handler for the sub-type but
			// it cannot be resolved (misconfiguration).
			if (secondarySignature[0] != '\0') {
				error = signatures.AddString(kSigField, secondarySignature);
				addedSecondarySignature = true;
			}
		}
	}

	BMessage supportingSignatures;
	if (error == B_OK
		&& type.GetSupportingApps(&supportingSignatures) == B_OK) {
		int32 subCount;
		if (supportingSignatures.FindInt32("be:sub", &subCount) != B_OK)
			subCount = 0;
		// Add all signatures with direct support for the sub-type.
		const char* supportingType;
		if (!addedSecondarySignature) {
			// Try to add the secondarySignature in front of all other
			// supporting apps, if we find it among those.
			for (int32 i = 0; error == B_OK && i < subCount
					&& supportingSignatures.FindString(kSigField, i,
						&supportingType) == B_OK; i++) {
				if (strcmp(primarySignature, supportingType) != 0
					&& strcmp(secondarySignature, supportingType) == 0) {
					error = signatures.AddString(kSigField, supportingType);
					addedSecondarySignature = true;
					break;
				}
			}
		}

		for (int32 i = 0; error == B_OK && i < subCount
				&& supportingSignatures.FindString(kSigField, i,
					&supportingType) == B_OK; i++) {
			if (strcmp(primarySignature, supportingType) != 0
				&& strcmp(secondarySignature, supportingType) != 0) {
				error = signatures.AddString(kSigField, supportingType);
			}
		}

		// Add the preferred type of the super type here before adding
		// the other types supporting the super type, but only if we have
		// not already added it in case there was no preferred app for the
		// sub-type configured.
		if (error == B_OK && !addedSecondarySignature
			&& secondarySignature[0] != '\0') {
			error = signatures.AddString(kSigField, secondarySignature);
		}

		// Add all signatures with support for the super-type.
		for (int32 i = subCount; error == B_OK
				&& supportingSignatures.FindString(kSigField, i,
					&supportingType) == B_OK; i++) {
			// Don't add the signature if it's one of the preferred apps
			// already.
			if (strcmp(primarySignature, supportingType) != 0
				&& strcmp(secondarySignature, supportingType) != 0) {
				error = signatures.AddString(kSigField, supportingType);
			}
		}
	} else {
		// Failed to get supporting apps, just add the preferred apps.
		if (error == B_OK && secondarySignature[0] != '\0')
			error = signatures.AddString(kSigField, secondarySignature);
	}

	if (error != B_OK)
		return error;

	// Set an error in case we can't resolve a single supporting app.
	error = B_LAUNCH_FAILED_NO_PREFERRED_APP;

	// See if we can find a good application that is valid from the messege.
	const char* signature;
	for (int32 i = 0;
		signatures.FindString(kSigField, i, &signature) == B_OK; i++) {
		if (signature[0] == '\0')
			continue;

		error = appMeta->SetTo(signature);

		// Check, whether the signature is installed and has an app hint
		bool appFound = false;
		if (error == B_OK && appMeta->GetAppHint(appRef) == B_OK) {
			// Resolve symbolic links, if necessary
			BEntry entry;
			if (entry.SetTo(appRef, true) == B_OK && entry.IsFile()
				&& entry.GetRef(appRef) == B_OK) {
				appFound = true;
			} else {
				// Bad app hint -- remove it
				appMeta->SetAppHint(NULL);
			}
		}

		// In case there is no app hint or it is invalid, we need to query for
		// the app.
		if (error == B_OK && !appFound)
			error = query_for_app(appMeta->Type(), appRef);

		if (error == B_OK)
			error = appFile->SetTo(appRef, B_READ_ONLY);

		// check, whether the app can be used
		if (error == B_OK)
			error = can_app_be_used(appRef);

		if (error == B_OK)
			break;
	}

	return error;
}


/** @brief Determines a file's MIME type from node info or by sniffing.
 *
 *  First attempts to read the type from the file's attributes via
 *  @a nodeInfo.  If that fails, updates the file's MIME info or
 *  falls back to BMimeType::GuessMimeType().
 *
 *  @param file     An entry_ref for the file. Must not be @c NULL.
 *  @param nodeInfo A BNodeInfo initialized to the file's node.
 *                  Must not be @c NULL.
 *  @param mimeType Buffer of at least @c B_MIME_TYPE_LENGTH to receive
 *                  the determined MIME type. Must not be @c NULL.
 *  @return @c B_OK on success, @c B_BAD_VALUE if parameters are @c NULL
 *          or the sniffed type is invalid, or another error code.
 */
status_t
BRoster::_GetFileType(const entry_ref* file, BNodeInfo* nodeInfo,
	char* mimeType) const
{
	// first try the node info
	if (nodeInfo->GetType(mimeType) == B_OK)
		return B_OK;

	if (fNoRegistrar)
		return B_NO_INIT;

	// Try to update the file's MIME info and just read the updated type.
	// If that fails, sniff manually.
	BPath path;
	if (path.SetTo(file) != B_OK
		|| update_mime_info(path.Path(), false, true, false) != B_OK
		|| nodeInfo->GetType(mimeType) != B_OK) {
		BMimeType type;
		status_t error = BMimeType::GuessMimeType(file, &type);
		if (error != B_OK)
			return error;

		if (!type.IsValid())
			return B_BAD_VALUE;

		strlcpy(mimeType, type.Type(), B_MIME_TYPE_LENGTH);
	}

	return B_OK;
}


/** @brief Delivers launch-time messages to a running application.
 *
 *  Sends custom messages from @a messageList, then @c B_ARGV_RECEIVED
 *  (if @a args has > 1 element), @c B_REFS_RECEIVED (if @a ref is
 *  non-NULL), and @c B_READY_TO_RUN (for newly launched apps).
 *  Ownership of all supplied objects is retained by the caller.
 *
 *  @param team           The target application's team ID.
 *  @param argc           Number of elements in @a args.
 *  @param args           Argument vector. May be @c NULL.
 *  @param messageList    List of BMessages to send first. May be @c NULL.
 *  @param ref            entry_ref for @c B_REFS_RECEIVED. May be @c NULL.
 *  @param alreadyRunning @c true if the app was already running (no
 *                        @c B_READY_TO_RUN will be sent), @c false for
 *                        newly launched apps.
 *  @return @c B_OK on success, or an error code on failure.
 *  @see _LaunchApp()
 */
status_t
BRoster::_SendToRunning(team_id team, int argc, const char* const* args,
	const BList* messageList, const entry_ref* ref,
	bool alreadyRunning) const
{
	status_t error = B_OK;

	// Construct a messenger to the app: We can't use the public constructor,
	// since the target application may be B_ARGV_ONLY.
	app_info info;
	error = GetRunningAppInfo(team, &info);
	if (error == B_OK) {
		BMessenger messenger;
		BMessenger::Private(messenger).SetTo(team, info.port,
			B_PREFERRED_TOKEN);

		// send messages from the list
		if (messageList != NULL) {
			for (int32 i = 0;
					BMessage* message = (BMessage*)messageList->ItemAt(i);
					i++) {
				messenger.SendMessage(message);
			}
		}

		// send B_ARGV_RECEIVED or B_REFS_RECEIVED or B_SILENT_RELAUNCH
		// (if already running)
		if (args != NULL && argc > 1) {
			BMessage message(B_ARGV_RECEIVED);
			message.AddInt32("argc", argc);
			for (int32 i = 0; i < argc; i++)
				message.AddString("argv", args[i]);

			// also add current working directory
			char cwd[B_PATH_NAME_LENGTH];
			if (getcwd(cwd, B_PATH_NAME_LENGTH) != NULL)
				message.AddString("cwd", cwd);

			messenger.SendMessage(&message);
		} else if (ref != NULL) {
			DBG(OUT("_SendToRunning : B_REFS_RECEIVED\n"));
			BMessage message(B_REFS_RECEIVED);
			message.AddRef("refs", ref);
			messenger.SendMessage(&message);
		} else if (alreadyRunning && (!messageList || messageList->IsEmpty()))
			messenger.SendMessage(B_SILENT_RELAUNCH);

		if (!alreadyRunning) {
			// send B_READY_TO_RUN
			DBG(OUT("_SendToRunning : B_READY_TO_RUN\n"));
			messenger.SendMessage(B_READY_TO_RUN);
		}
	}

	return error;
}


/** @brief Enables or disables registrar-free operation.
 *
 *  When @a noRegistrar is @c true, methods that would normally
 *  communicate with the registrar skip those calls.  Used during
 *  early boot or in testing environments.
 *
 *  @param noRegistrar @c true to bypass the registrar, @c false for
 *                     normal operation.
 */
void
BRoster::_SetWithoutRegistrar(bool noRegistrar)
{
	fNoRegistrar = noRegistrar;
}


/** @brief Initializes the internal messenger to the registrar.
 *
 *  Looks up the registrar's port via the launch roster and configures
 *  the internal BMessenger to communicate with it.
 */
void
BRoster::_InitMessenger()
{
	DBG(OUT("BRoster::InitMessengers()\n"));

	// find the registrar port

#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
	BMessage data;
	if (BLaunchRoster().GetData(B_REGISTRAR_SIGNATURE, data) == B_OK) {
		port_id port = data.GetInt32("port", -1);
		team_id team = data.GetInt32("team", -1);

		if (port >= 0 && team != current_team()) {
			// Make sure we aren't the registrar ourselves.

			DBG(OUT("  found roster port\n"));

			BMessenger::Private(fMessenger).SetTo(team, port,
				B_PREFERRED_TOKEN);
		}
	}
#else
	port_id rosterPort = find_port(B_REGISTRAR_PORT_NAME);
	port_info info;
	if (rosterPort >= 0 && get_port_info(rosterPort, &info) == B_OK) {
		DBG(OUT("  found roster port\n"));

		BMessenger::Private(fMessenger).SetTo(info.team, rosterPort,
			B_PREFERRED_TOKEN);
	}
#endif

	DBG(OUT("BRoster::InitMessengers() done\n"));
}


/** @brief Lazily initializes the MIME messenger (called via __init_once).
 *
 *  Sends a @c B_REG_GET_MIME_MESSENGER request to the registrar and
 *  stores the returned messenger in @c fMimeMessenger.
 *
 *  @param data Pointer to the BRoster instance (cast from @c void*).
 *  @return @c B_OK on success, or an error code on failure.
 *  @see _MimeMessenger()
 */
/*static*/ status_t
BRoster::_InitMimeMessenger(void* data)
{
	BRoster* roster = (BRoster*)data;

	// ask for the MIME messenger
	// Generous 1s + 5s timeouts. It could actually be synchronous, but
	// timeouts allow us to debug the registrar main thread.
	BMessage request(B_REG_GET_MIME_MESSENGER);
	BMessage reply;
	status_t error = roster->fMessenger.SendMessage(&request, &reply,
		1000000LL, 5000000LL);
	if (error == B_OK && reply.what == B_REG_SUCCESS) {
		DBG(OUT("  got reply from roster\n"));
			reply.FindMessenger("messenger", &roster->fMimeMessenger);
	} else {
		DBG(OUT("  no (useful) reply from roster: error: %" B_PRIx32 ": %s\n",
			error, strerror(error)));
		if (error == B_OK)
			DBG(reply.PrintToStream());
	}

	return error;
}


/** @brief Returns the MIME messenger, initializing it on first call.
 *
 *  Uses __init_once to ensure the MIME messenger is initialized exactly
 *  once in a thread-safe manner.
 *
 *  @return A reference to the MIME messenger.
 *  @see _InitMimeMessenger()
 */
BMessenger&
BRoster::_MimeMessenger()
{
	__init_once(&fMimeMessengerInitOnce, &_InitMimeMessenger, this);
	return fMimeMessenger;
}


/** @brief Adds an application to the front of the recent apps list.
 *
 *  Sends a @c B_REG_ADD_TO_RECENT_APPS request to the registrar.
 *
 *  @param signature The MIME signature of the application to add.
 *  @see GetRecentApps()
 */
void
BRoster::_AddToRecentApps(const char* signature) const
{
	status_t error = B_OK;
	// compose the request message
	BMessage request(B_REG_ADD_TO_RECENT_APPS);
	if (error == B_OK)
		error = request.AddString("app sig", signature);

	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);

	// evaluate the reply
	status_t result;
	if (error == B_OK) {
		error = reply.what == B_REG_RESULT
			? (status_t)B_OK : (status_t)B_BAD_REPLY;
	}

	if (error == B_OK)
		error = reply.FindInt32("result", &result);

	if (error == B_OK)
		error = result;

	// Nothing to return... how sad :-(
	//return error;
}


/** @brief Clears the system's recent documents list.
 */
void
BRoster::_ClearRecentDocuments() const
{
	BMessage request(B_REG_CLEAR_RECENT_DOCUMENTS);
	BMessage reply;
	fMessenger.SendMessage(&request, &reply);
}


/** @brief Clears the system's recent folders list.
 */
void
BRoster::_ClearRecentFolders() const
{
	BMessage request(B_REG_CLEAR_RECENT_FOLDERS);
	BMessage reply;
	fMessenger.SendMessage(&request, &reply);
}


/** @brief Clears the system's recent applications list.
 */
void
BRoster::_ClearRecentApps() const
{
	BMessage request(B_REG_CLEAR_RECENT_APPS);
	BMessage reply;
	fMessenger.SendMessage(&request, &reply);
}


/** @brief Loads recently used lists from a file.
 *
 *  The current recent documents, folders, and applications lists are
 *  cleared before loading the new data from @a filename.
 *
 *  @param filename The path of the file to load from.
 *  @see _SaveRecentLists()
 */
void
BRoster::_LoadRecentLists(const char* filename) const
{
	status_t error = B_OK;

	// compose the request message
	BMessage request(B_REG_LOAD_RECENT_LISTS);
	if (error == B_OK)
		error = request.AddString("filename", filename);

	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);

	// evaluate the reply
	status_t result;
	if (error == B_OK) {
		error = reply.what == B_REG_RESULT
			? (status_t)B_OK : (status_t)B_BAD_REPLY;
	}
	if (error == B_OK)
		error = reply.FindInt32("result", &result);

	if (error == B_OK)
		error = result;

	// Nothing to return... how sad :-(
	//return error;
}


/** @brief Saves recently used lists to a file.
 *
 *  Persists the current recent documents, folders, and applications
 *  lists to @a filename.
 *
 *  @param filename The path of the file to save to.
 *  @see _LoadRecentLists()
 */
void
BRoster::_SaveRecentLists(const char* filename) const
{
	status_t error = B_OK;

	// compose the request message
	BMessage request(B_REG_SAVE_RECENT_LISTS);
	if (error == B_OK)
		error = request.AddString("filename", filename);

	// send the request
	BMessage reply;
	if (error == B_OK)
		error = fMessenger.SendMessage(&request, &reply);

	// evaluate the reply
	status_t result;
	if (error == B_OK) {
		error = reply.what == B_REG_RESULT
			? (status_t)B_OK : (status_t)B_BAD_REPLY;
	}
	if (error == B_OK)
		error = reply.FindInt32("result", &result);

	if (error == B_OK)
		error = result;

	// Nothing to return... how sad :-(
	//return error;
}
