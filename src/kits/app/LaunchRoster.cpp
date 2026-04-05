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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2015-2017 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 */


/**
 * @file LaunchRoster.cpp
 * @brief Implementation of BLaunchRoster for communicating with the launch_daemon.
 *
 * Provides the client-side API for interacting with the system launch daemon.
 * Supports querying launch data and ports, starting and stopping jobs and
 * targets, managing sessions, and registering/notifying launch events.
 */

#include <LaunchRoster.h>

#include <Application.h>
#include <String.h>
#include <StringList.h>

#include <launch.h>
#include <LaunchDaemonDefs.h>
#include <LaunchRosterPrivate.h>
#include <MessengerPrivate.h>


using namespace BPrivate;


/** @brief Constructs a Private accessor from a BLaunchRoster pointer.
 *  @param roster Pointer to the BLaunchRoster instance to access.
 */
BLaunchRoster::Private::Private(BLaunchRoster* roster)
	:
	fRoster(roster)
{
}


/** @brief Constructs a Private accessor from a BLaunchRoster reference.
 *  @param roster Reference to the BLaunchRoster instance to access.
 */
BLaunchRoster::Private::Private(BLaunchRoster& roster)
	:
	fRoster(&roster)
{
}


/** @brief Registers a session daemon with the launch_daemon.
 *
 *  Sends a B_REGISTER_SESSION_DAEMON request to the launch_daemon,
 *  associating the given messenger as the session daemon for the
 *  current user.
 *
 *  @param daemon The BMessenger identifying the session daemon to register.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BLaunchRoster::Private::RegisterSessionDaemon(const BMessenger& daemon)
{
	BMessage request(B_REGISTER_SESSION_DAEMON);
	status_t status = request.AddInt32("user", getuid());
	if (status == B_OK)
		status = request.AddMessenger("daemon", daemon);
	if (status != B_OK)
		return status;

	// send the request
	BMessage result;
	status = fRoster->fMessenger.SendMessage(&request, &result);

	// evaluate the reply
	if (status == B_OK)
		status = result.what;

	return status;
}


// #pragma mark -


/** @brief Constructs a BLaunchRoster and connects to the launch_daemon.
 *
 *  Initializes the internal messenger by locating and connecting to the
 *  system launch_daemon port.
 */
BLaunchRoster::BLaunchRoster()
{
	_InitMessenger();
}


/** @brief Destroys the BLaunchRoster. */
BLaunchRoster::~BLaunchRoster()
{
}


/** @brief Checks whether the connection to the launch_daemon is valid.
 *  @return B_OK if connected, B_ERROR if the messenger is not initialized.
 */
status_t
BLaunchRoster::InitCheck() const
{
	return fMessenger.Team() >= 0 ? B_OK : B_ERROR;
}


/** @brief Retrieves launch data for the current application.
 *
 *  Convenience overload that uses the current application's signature.
 *  Requires a valid be_app global.
 *
 *  @param data Output BMessage to receive the launch data.
 *  @return B_OK on success, B_BAD_VALUE if be_app is NULL, or an error
 *          from the launch_daemon.
 */
status_t
BLaunchRoster::GetData(BMessage& data)
{
	if (be_app == NULL)
		return B_BAD_VALUE;

	return GetData(be_app->Signature(), data);
}


/** @brief Retrieves launch data for the application with the given signature.
 *
 *  Sends a B_GET_LAUNCH_DATA request to the launch_daemon and returns
 *  the response data, which typically contains port IDs and other
 *  configuration provided by the daemon.
 *
 *  @param signature The MIME application signature (e.g., "application/x-vnd.MyApp").
 *  @param data Output BMessage to receive the launch data.
 *  @return B_OK on success, B_BAD_VALUE if signature is NULL or empty,
 *          or an error from the launch_daemon.
 */
status_t
BLaunchRoster::GetData(const char* signature, BMessage& data)
{
	if (signature == NULL || signature[0] == '\0')
		return B_BAD_VALUE;

	BMessage request(B_GET_LAUNCH_DATA);
	status_t status = request.AddString("name", signature);
	if (status == B_OK)
		status = request.AddInt32("user", getuid());
	if (status != B_OK)
		return status;

	return _SendRequest(request, data);
}


/** @brief Retrieves a named port for the current application from the launch_daemon.
 *
 *  Convenience overload that uses the current application's signature.
 *
 *  @param name The port name suffix, or NULL for the default port.
 *  @return The port ID on success, or a negative error code.
 */
port_id
BLaunchRoster::GetPort(const char* name)
{
	if (be_app == NULL)
		return B_BAD_VALUE;

	return GetPort(be_app->Signature(), name);
}


/** @brief Retrieves a named port for the given application signature.
 *
 *  Queries launch data for the specified signature and extracts the port
 *  ID stored under the field name "<name>_port" (or just "port" if name
 *  is NULL).
 *
 *  @param signature The MIME application signature to query.
 *  @param name The port name suffix, or NULL for the default port.
 *  @return The port ID on success, or -1 if not found.
 */
port_id
BLaunchRoster::GetPort(const char* signature, const char* name)
{
	BMessage data;
	status_t status = GetData(signature, data);
	if (status == B_OK) {
		BString fieldName;
		if (name != NULL)
			fieldName << name << "_";
		fieldName << "port";

		port_id port = data.GetInt32(fieldName.String(), B_NAME_NOT_FOUND);
		if (port >= 0)
			return port;
	}

	return -1;
}


/** @brief Launches a target by name with optional data and base target.
 *
 *  Convenience overload accepting a BMessage reference.
 *
 *  @param name The name of the launch target.
 *  @param data Additional data to pass to the target.
 *  @param baseName Optional base target name for inheritance, or NULL.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BLaunchRoster::Target(const char* name, const BMessage& data,
	const char* baseName)
{
	return Target(name, &data, baseName);
}


/** @brief Launches a target by name with optional data and base target.
 *
 *  Sends a B_LAUNCH_TARGET request to the launch_daemon to activate the
 *  named target. An optional data message and base target name can be
 *  provided.
 *
 *  @param name The name of the launch target.
 *  @param data Optional additional data to pass (may be NULL).
 *  @param baseName Optional base target name for inheritance, or NULL.
 *  @return B_OK on success, B_BAD_VALUE if name is NULL, or an error
 *          from the launch_daemon.
 */
status_t
BLaunchRoster::Target(const char* name, const BMessage* data,
	const char* baseName)
{
	if (name == NULL)
		return B_BAD_VALUE;

	BMessage request(B_LAUNCH_TARGET);
	status_t status = request.AddInt32("user", getuid());
	if (status == B_OK)
		status = request.AddString("target", name);
	if (status == B_OK && data != NULL && !data->IsEmpty())
		status = request.AddMessage("data", data);
	if (status == B_OK && baseName != NULL)
		status = request.AddString("base target", baseName);
	if (status != B_OK)
		return status;

	return _SendRequest(request);
}


/** @brief Stops a launch target by name.
 *
 *  Sends a B_STOP_LAUNCH_TARGET request to the launch_daemon.
 *
 *  @param name The name of the target to stop.
 *  @param force If true, forces the target to stop immediately.
 *  @return B_OK on success, B_BAD_VALUE if name is NULL, or an error
 *          from the launch_daemon.
 */
status_t
BLaunchRoster::StopTarget(const char* name, bool force)
{
	if (name == NULL)
		return B_BAD_VALUE;

	BMessage request(B_STOP_LAUNCH_TARGET);
	status_t status = request.AddInt32("user", getuid());
	if (status == B_OK)
		status = request.AddString("target", name);
	if (status == B_OK)
		status = request.AddBool("force", force);
	if (status != B_OK)
		return status;

	return _SendRequest(request);
}


/** @brief Starts a launch job by name.
 *
 *  Sends a B_LAUNCH_JOB request to the launch_daemon.
 *
 *  @param name The name of the job to start.
 *  @return B_OK on success, B_BAD_VALUE if name is NULL, or an error
 *          from the launch_daemon.
 */
status_t
BLaunchRoster::Start(const char* name)
{
	if (name == NULL)
		return B_BAD_VALUE;

	BMessage request(B_LAUNCH_JOB);
	status_t status = request.AddInt32("user", getuid());
	if (status == B_OK)
		status = request.AddString("name", name);
	if (status != B_OK)
		return status;

	return _SendRequest(request);
}


/** @brief Stops a launch job by name.
 *
 *  Sends a B_STOP_LAUNCH_JOB request to the launch_daemon.
 *
 *  @param name The name of the job to stop.
 *  @param force If true, forces the job to stop immediately.
 *  @return B_OK on success, B_BAD_VALUE if name is NULL, or an error
 *          from the launch_daemon.
 */
status_t
BLaunchRoster::Stop(const char* name, bool force)
{
	if (name == NULL)
		return B_BAD_VALUE;

	BMessage request(B_STOP_LAUNCH_JOB);
	status_t status = request.AddInt32("user", getuid());
	if (status == B_OK)
		status = request.AddString("name", name);
	if (status == B_OK)
		status = request.AddBool("force", force);
	if (status != B_OK)
		return status;

	return _SendRequest(request);
}


/** @brief Enables or disables a launch job.
 *
 *  Sends a B_ENABLE_LAUNCH_JOB request to the launch_daemon.
 *
 *  @param name The name of the job to enable or disable.
 *  @param enable true to enable, false to disable.
 *  @return B_OK on success, B_BAD_VALUE if name is NULL, or an error
 *          from the launch_daemon.
 */
status_t
BLaunchRoster::SetEnabled(const char* name, bool enable)
{
	if (name == NULL)
		return B_BAD_VALUE;

	BMessage request(B_ENABLE_LAUNCH_JOB);
	status_t status = request.AddInt32("user", getuid());
	if (status == B_OK)
		status = request.AddString("name", name);
	if (status == B_OK)
		status = request.AddBool("enable", enable);
	if (status != B_OK)
		return status;

	return _SendRequest(request);
}


/** @brief Starts a new user session for the given login name.
 *
 *  Sends a B_LAUNCH_SESSION request to the launch_daemon.
 *
 *  @param login The user login name for the session.
 *  @return B_OK on success, B_BAD_VALUE if login is NULL, or an error
 *          from the launch_daemon.
 */
status_t
BLaunchRoster::StartSession(const char* login)
{
	if (login == NULL)
		return B_BAD_VALUE;

	BMessage request(B_LAUNCH_SESSION);
	status_t status = request.AddInt32("user", getuid());
	if (status == B_OK)
		status = request.AddString("login", login);
	if (status != B_OK)
		return status;

	return _SendRequest(request);
}


/** @brief Registers a named event with the launch_daemon.
 *
 *  The event can later be notified via NotifyEvent() to trigger
 *  dependent jobs or targets.
 *
 *  @param source The BMessenger identifying the event source.
 *  @param name The name of the event to register.
 *  @param flags Optional event flags (e.g., sticky).
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BLaunchRoster::RegisterEvent(const BMessenger& source, const char* name,
	uint32 flags)
{
	return _UpdateEvent(B_REGISTER_LAUNCH_EVENT, source, name, flags);
}


/** @brief Unregisters a previously registered event from the launch_daemon.
 *
 *  @param source The BMessenger identifying the event source.
 *  @param name The name of the event to unregister.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BLaunchRoster::UnregisterEvent(const BMessenger& source, const char* name)
{
	return _UpdateEvent(B_UNREGISTER_LAUNCH_EVENT, source, name);
}


/** @brief Notifies the launch_daemon that a registered event has occurred.
 *
 *  This triggers any jobs or targets that depend on the named event.
 *
 *  @param source The BMessenger identifying the event source.
 *  @param name The name of the event that occurred.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BLaunchRoster::NotifyEvent(const BMessenger& source, const char* name)
{
	return _UpdateEvent(B_NOTIFY_LAUNCH_EVENT, source, name);
}


/** @brief Resets a sticky event in the launch_daemon.
 *
 *  Clears the triggered state of a sticky event so it can be re-triggered.
 *
 *  @param source The BMessenger identifying the event source.
 *  @param name The name of the sticky event to reset.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BLaunchRoster::ResetStickyEvent(const BMessenger& source, const char* name)
{
	return _UpdateEvent(B_RESET_STICKY_LAUNCH_EVENT, source, name);
}


/** @brief Retrieves the list of all registered launch targets.
 *
 *  @param targets Output BStringList to receive the target names.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BLaunchRoster::GetTargets(BStringList& targets)
{
	BMessage request(B_GET_LAUNCH_TARGETS);
	status_t status = request.AddInt32("user", getuid());
	if (status != B_OK)
		return status;

	// send the request
	BMessage result;
	status = _SendRequest(request, result);
	if (status == B_OK)
		status = result.FindStrings("target", &targets);

	return status;
}


/** @brief Retrieves detailed information about a launch target.
 *
 *  @param name The name of the target to query.
 *  @param info Output BMessage to receive the target information.
 *  @return B_OK on success, B_BAD_VALUE if name is NULL or empty, or
 *          an error from the launch_daemon.
 */
status_t
BLaunchRoster::GetTargetInfo(const char* name, BMessage& info)
{
	return _GetInfo(B_GET_LAUNCH_TARGET_INFO, name, info);
}


/** @brief Retrieves the list of jobs, optionally filtered by target.
 *
 *  @param target The target name to filter by, or NULL for all jobs.
 *  @param jobs Output BStringList to receive the job names.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BLaunchRoster::GetJobs(const char* target, BStringList& jobs)
{
	BMessage request(B_GET_LAUNCH_JOBS);
	status_t status = request.AddInt32("user", getuid());
	if (status == B_OK && target != NULL)
		status = request.AddString("target", target);
	if (status != B_OK)
		return status;

	// send the request
	BMessage result;
	status = _SendRequest(request, result);
	if (status == B_OK)
		status = result.FindStrings("job", &jobs);

	return status;
}


/** @brief Retrieves detailed information about a launch job.
 *
 *  @param name The name of the job to query.
 *  @param info Output BMessage to receive the job information.
 *  @return B_OK on success, B_BAD_VALUE if name is NULL or empty, or
 *          an error from the launch_daemon.
 */
status_t
BLaunchRoster::GetJobInfo(const char* name, BMessage& info)
{
	return _GetInfo(B_GET_LAUNCH_JOB_INFO, name, info);
}


/** @brief Retrieves the full launch_daemon log.
 *
 *  @param info Output BMessage to receive the log entries.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BLaunchRoster::GetLog(BMessage& info)
{
	return _GetLog(NULL, info);
}


/** @brief Retrieves filtered launch_daemon log entries.
 *
 *  @param filter A BMessage containing filter criteria for the log query.
 *  @param info Output BMessage to receive the matching log entries.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BLaunchRoster::GetLog(const BMessage& filter, BMessage& info)
{
	return _GetLog(&filter, info);
}


/** @brief Initializes the internal messenger to communicate with the launch_daemon.
 *
 *  Locates the launch_daemon port and sets up the BMessenger for sending
 *  requests. In test mode, uses find_port(); in production, uses the
 *  BPrivate::get_launch_daemon_port() helper.
 */
void
BLaunchRoster::_InitMessenger()
{
#ifdef TEST_MODE
	port_id daemonPort = find_port(B_LAUNCH_DAEMON_PORT_NAME);
#else
	// find the launch_daemon port
	port_id daemonPort = BPrivate::get_launch_daemon_port();
#endif
	port_info info;
	if (daemonPort >= 0 && get_port_info(daemonPort, &info) == B_OK) {
		BMessenger::Private(fMessenger).SetTo(info.team, daemonPort,
			B_PREFERRED_TOKEN);
	}
}


/** @brief Sends a request to the launch_daemon, discarding the reply.
 *
 *  @param request The request BMessage to send.
 *  @return B_OK on success, or an error code from the daemon or messenger.
 */
status_t
BLaunchRoster::_SendRequest(BMessage& request)
{
	BMessage result;
	return _SendRequest(request, result);
}


/** @brief Sends a request to the launch_daemon and returns the reply.
 *
 *  Sends the request via the internal messenger and interprets the
 *  reply's what field as the result status.
 *
 *  @param request The request BMessage to send.
 *  @param result Output BMessage to receive the daemon's reply.
 *  @return B_OK on success, or an error code from the daemon or messenger.
 */
status_t
BLaunchRoster::_SendRequest(BMessage& request, BMessage& result)
{
	// Send the request, and evaluate the reply
	status_t status = fMessenger.SendMessage(&request, &result);
	if (status == B_OK)
		status = result.what;

	return status;
}


/** @brief Sends an event-related request to the launch_daemon.
 *
 *  Used internally by RegisterEvent(), UnregisterEvent(), NotifyEvent(),
 *  and ResetStickyEvent() to build and send the appropriate message.
 *
 *  @param what The message code (e.g., B_REGISTER_LAUNCH_EVENT).
 *  @param source The BMessenger identifying the event source.
 *  @param name The name of the event.
 *  @param flags Optional event flags (only included if non-zero).
 *  @return B_OK on success, B_BAD_VALUE if be_app is NULL or name is
 *          empty, or an error from the launch_daemon.
 */
status_t
BLaunchRoster::_UpdateEvent(uint32 what, const BMessenger& source,
	const char* name, uint32 flags)
{
	if (be_app == NULL || name == NULL || name[0] == '\0')
		return B_BAD_VALUE;

	BMessage request(what);
	status_t status = request.AddInt32("user", getuid());
	if (status == B_OK)
		status = request.AddMessenger("source", source);
	if (status == B_OK)
		status = request.AddString("owner", be_app->Signature());
	if (status == B_OK)
		status = request.AddString("name", name);
	if (status == B_OK && flags != 0)
		status = request.AddUInt32("flags", flags);
	if (status != B_OK)
		return status;

	return _SendRequest(request);
}


/** @brief Sends an info retrieval request to the launch_daemon.
 *
 *  Used internally by GetTargetInfo() and GetJobInfo().
 *
 *  @param what The message code (e.g., B_GET_LAUNCH_TARGET_INFO).
 *  @param name The name of the target or job to query.
 *  @param info Output BMessage to receive the information.
 *  @return B_OK on success, B_BAD_VALUE if name is NULL or empty, or
 *          an error from the launch_daemon.
 */
status_t
BLaunchRoster::_GetInfo(uint32 what, const char* name, BMessage& info)
{
	if (name == NULL || name[0] == '\0')
		return B_BAD_VALUE;

	BMessage request(what);
	status_t status = request.AddInt32("user", getuid());
	if (status == B_OK)
		status = request.AddString("name", name);
	if (status != B_OK)
		return status;

	return _SendRequest(request, info);
}


/** @brief Retrieves launch_daemon log entries with an optional filter.
 *
 *  Sends a B_GET_LAUNCH_LOG request. If a filter message is provided,
 *  it is included in the request for server-side filtering.
 *
 *  @param filter Optional filter criteria message, or NULL for all entries.
 *  @param info Output BMessage to receive the log data.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BLaunchRoster::_GetLog(const BMessage* filter, BMessage& info)
{
	BMessage request(B_GET_LAUNCH_LOG);
	status_t status = request.AddInt32("user", getuid());
	if (status == B_OK && filter != NULL)
		status = request.AddMessage("filter", filter);
	if (status != B_OK)
		return status;

	return _SendRequest(request, info);
}
