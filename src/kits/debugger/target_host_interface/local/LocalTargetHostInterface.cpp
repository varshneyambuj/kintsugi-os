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
 *   Copyright 2016, Rene Gollent, rene@gollent.com.
 *   Copyright 2016, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file LocalTargetHostInterface.cpp
 * @brief Local-host transport for debugging teams running on the same machine.
 *
 * Owns a TargetHost populated from get_next_team_info() and kept current via
 * a watcher thread that consumes B_WATCH_SYSTEM_TEAM_CREATION /
 * B_WATCH_SYSTEM_TEAM_DELETION notifications. Attach, spawn-from-args, and
 * core-file load each produce a fresh DebuggerInterface for the rest of the
 * debugger to use.
 */


#include "LocalTargetHostInterface.h"

#include <set>

#include <stdio.h>
#include <unistd.h>

#include <image.h>

#include <AutoDeleter.h>
#include <AutoLocker.h>
#include <system_info.h>
#include <kernel/util/KMessage.h>

#include "debug_utils.h"

#include "CoreFile.h"
#include "CoreFileDebuggerInterface.h"
#include "LocalDebuggerInterface.h"
#include "TargetHost.h"

using std::set;

/**
 * @brief Constructs the interface with the display name "Local"; Init() must follow.
 */
LocalTargetHostInterface::LocalTargetHostInterface()
	:
	TargetHostInterface(),
	fTargetHost(NULL),
	fDataPort(-1)
{
	SetName("Local");
}


/**
 * @brief Stops the watcher thread, deletes the data port, and releases the host.
 */
LocalTargetHostInterface::~LocalTargetHostInterface()
{
	Close();

	if (fTargetHost != NULL)
		fTargetHost->ReleaseReference();
}


/**
 * @brief Initializes the local transport: discovers the host name, builds the
 *        TargetHost, populates it with current teams, and starts the
 *        team-watching thread.
 *
 * @param settings  Ignored; the local transport requires no configuration.
 * @return B_OK on success, B_NO_MEMORY on TargetHost allocation failure, or
 *         the first failing status from get_team_info(), create_port(),
 *         spawn_thread(), or __start_watching_system().
 */
status_t
LocalTargetHostInterface::Init(Settings* settings)
{
	char hostname[HOST_NAME_MAX + 1];
	status_t error = gethostname(hostname, sizeof(hostname));
	if (error != B_OK) {
		fprintf(stderr, "gethostname() failed, defaults to localhost\n");
		strlcpy(hostname, "localhost", sizeof(hostname));
	}

	fTargetHost = new(std::nothrow) TargetHost(hostname);
	if (fTargetHost == NULL)
		return B_NO_MEMORY;

	team_info info;
	error = get_team_info(B_CURRENT_TEAM, &info);
	if (error != B_OK)
		return error;

	char buffer[128];
	snprintf(buffer, sizeof(buffer), "LocalTargetHostInterface %" B_PRId32,
		info.team);

	fDataPort = create_port(100, buffer);
	if (fDataPort < 0)
		return fDataPort;

	fPortWorker = spawn_thread(_PortLoop, "Local Target Host Loop",
		B_NORMAL_PRIORITY, this);
	if (fPortWorker < 0)
		return fPortWorker;

	resume_thread(fPortWorker);

	AutoLocker<TargetHost> hostLocker(fTargetHost);

	error = __start_watching_system(-1,
		B_WATCH_SYSTEM_TEAM_CREATION | B_WATCH_SYSTEM_TEAM_DELETION,
		fDataPort, 0);
	if (error != B_OK)
		return error;

	int32 cookie = 0;
	while (get_next_team_info(&cookie, &info) == B_OK) {
		error = fTargetHost->AddTeam(info);
		if (error != B_OK)
			return error;
	}

	snprintf(buffer, sizeof(buffer), "Local (%s)", hostname);
	SetName(buffer);

	return B_OK;
}


/**
 * @brief Stops watching for team events and joins the watcher thread.
 *
 * Stopping the system watch causes the next read on the data port to fail,
 * which lets the loop exit so wait_for_thread() returns promptly.
 */
void
LocalTargetHostInterface::Close()
{
	if (fDataPort > 0) {
		__stop_watching_system(-1,
			B_WATCH_SYSTEM_TEAM_CREATION | B_WATCH_SYSTEM_TEAM_DELETION,
			fDataPort, 0);

		delete_port(fDataPort);
		fDataPort = -1;
	}

	if (fPortWorker > 0) {
		wait_for_thread(fPortWorker, NULL);
		fPortWorker = -1;
	}
}


/**
 * @brief Identifies this interface as a local-host transport.
 *
 * @return Always true.
 */
bool
LocalTargetHostInterface::IsLocal() const
{
	return true;
}


/**
 * @brief Reports whether the interface can currently be used.
 *
 * @return Always true; the local transport is always considered connected.
 */
bool
LocalTargetHostInterface::Connected() const
{
	return true;
}


/**
 * @brief Returns the TargetHost owned by this interface.
 *
 * @return Borrowed pointer to the TargetHost; valid for the interface lifetime.
 */
TargetHost*
LocalTargetHostInterface::GetTargetHost()
{
	return fTargetHost;
}


/**
 * @brief Attaches to a local team (resolved from @a teamID or @a threadID) and
 *        constructs a LocalDebuggerInterface for it.
 *
 * @param teamID      Team to attach to, or -1 to resolve from @a threadID.
 * @param threadID    Optional thread id to seed the resolution from.
 * @param _interface  On success, set to a fresh DebuggerInterface owned by the caller.
 * @return B_OK on success, B_BAD_VALUE if neither id is valid, B_NO_MEMORY
 *         on allocation failure, or any error from get_thread_info() or the
 *         interface's Init().
 */
status_t
LocalTargetHostInterface::Attach(team_id teamID, thread_id threadID,
	DebuggerInterface*& _interface) const
{
	if (teamID < 0 && threadID < 0)
		return B_BAD_VALUE;

	status_t error;
	if (teamID < 0) {
		thread_info threadInfo;
		error = get_thread_info(threadID, &threadInfo);
		if (error != B_OK)
			return error;

		teamID = threadInfo.team;
	}

	LocalDebuggerInterface* interface
		= new(std::nothrow) LocalDebuggerInterface(teamID);
	if (interface == NULL)
		return B_NO_MEMORY;

	BReference<DebuggerInterface> interfaceReference(interface, true);
	error = interface->Init();
	if (error != B_OK)
		return error;

	_interface = interface;
	interfaceReference.Detach();
	return B_OK;
}


/**
 * @brief Spawns a new team via load_program() and returns its id.
 *
 * @param commandLineArgc  Number of arguments in @a arguments.
 * @param arguments        Argv-style argument vector.
 * @param _teamID          On success, set to the team id (which equals the
 *                         main thread id).
 * @return B_OK on success, or any negative status returned by load_program().
 */
status_t
LocalTargetHostInterface::CreateTeam(int commandLineArgc,
	const char* const* arguments, team_id& _teamID) const
{
	thread_id thread = load_program(arguments, commandLineArgc, false);
	if (thread < 0)
		return thread;

	// main thread ID == team ID.
	_teamID = thread;
	return B_OK;
}


/**
 * @brief Loads a core file and wraps it in a CoreFileDebuggerInterface.
 *
 * @param coreFilePath  Filesystem path to the core file.
 * @param _interface    On success, set to a fresh DebuggerInterface owned by the caller.
 * @param _thread       On success, set to the recorded team/thread id from the dump.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or any error
 *         from CoreFile::Init() or the interface's Init().
 */
status_t
LocalTargetHostInterface::LoadCore(const char* coreFilePath,
	DebuggerInterface*& _interface, thread_id& _thread) const
{
	// load the core file
	CoreFile* coreFile = new(std::nothrow) CoreFile;
	if (coreFile == NULL)
		return B_NO_MEMORY;
	ObjectDeleter<CoreFile> coreFileDeleter(coreFile);

	status_t error = coreFile->Init(coreFilePath);
	if (error != B_OK)
		return error;

	// create the debugger interface
	CoreFileDebuggerInterface* interface
		= new(std::nothrow) CoreFileDebuggerInterface(coreFile);
	if (interface == NULL)
		return B_NO_MEMORY;
	coreFileDeleter.Detach();

	BReference<DebuggerInterface> interfaceReference(interface, true);
	error = interface->Init();
	if (error != B_OK)
		return error;

	const CoreFileTeamInfo& teamInfo = coreFile->GetTeamInfo();
	_thread = teamInfo.Id();
	_interface = interface;
	interfaceReference.Detach();

	return B_OK;
}


/**
 * @brief Maps a thread id to its owning team via get_thread_info().
 *
 * @param thread   Thread id to resolve.
 * @param _teamID  On success, set to the owning team id.
 * @return B_OK on success or any error from get_thread_info().
 */
status_t
LocalTargetHostInterface::FindTeamByThread(thread_id thread,
	team_id& _teamID) const
{
	thread_info info;
	status_t error = get_thread_info(thread, &info);
	if (error != B_OK)
		return error;

	_teamID = info.team;
	return B_OK;
}


/**
 * @brief Worker thread main loop that consumes team-watching messages.
 *
 * Drains the data port, parses each message, and forwards
 * created/exec'd/deleted teams into _HandleTeamEvent(). Newly-created teams
 * whose application image has not yet shown up are kept on a "waiting" set
 * and retried with a 20 ms timeout until their app image is present.
 *
 * @param arg  Pointer to the LocalTargetHostInterface instance owning the port.
 * @return Status from a fatal port read; B_OK is unreachable in practice.
 */
status_t
LocalTargetHostInterface::_PortLoop(void* arg)
{
	LocalTargetHostInterface* interface = (LocalTargetHostInterface*)arg;
	set<team_id> waitingTeams;

	for (;;) {
		status_t error;
		bool addToWaiters;
		char buffer[2048];
		int32 messageCode;
		team_id team;

		ssize_t size = read_port_etc(interface->fDataPort, &messageCode,
			buffer, sizeof(buffer), B_TIMEOUT, waitingTeams.empty()
				? B_INFINITE_TIMEOUT : 20000);
		if (size == B_INTERRUPTED)
			continue;
		else if (size == B_TIMED_OUT && !waitingTeams.empty()) {
			for (set<team_id>::iterator it = waitingTeams.begin();
				it != waitingTeams.end(); ++it) {
				team = *it;
				error = interface->_HandleTeamEvent(team,
					B_TEAM_CREATED, addToWaiters);
				if (error != B_OK)
					continue;
				else if (!addToWaiters) {
					waitingTeams.erase(it);
					if (waitingTeams.empty())
						break;
					it = waitingTeams.begin();
				}
			}
			continue;
		} else if (size < 0)
			return size;

		KMessage message;
		size = message.SetTo(buffer);
		if (size != B_OK)
			continue;

		if (message.What() != B_SYSTEM_OBJECT_UPDATE)
			continue;

		int32 opcode = 0;
		if (message.FindInt32("opcode", &opcode) != B_OK)
			continue;

		team = -1;
		if (message.FindInt32("team", &team) != B_OK)
			continue;

		error = interface->_HandleTeamEvent(team, opcode,
			addToWaiters);
		if (error != B_OK)
			continue;
		if (opcode == B_TEAM_CREATED && addToWaiters) {
			try {
				waitingTeams.insert(team);
			} catch (...) {
				continue;
			}
		}
	}

	return B_OK;
}


/**
 * @brief Applies a single team-creation/deletion/exec event to the TargetHost.
 *
 * For B_TEAM_CREATED and B_TEAM_EXEC, defers the update until at least one
 * B_APP_IMAGE has been loaded so the team's name is meaningful when the UI
 * sees it. Sets @a addToWaiters when a defer is required.
 *
 * @param team          Team id described by the event.
 * @param opcode        One of B_TEAM_CREATED, B_TEAM_EXEC, B_TEAM_DELETED.
 * @param addToWaiters  Output flag set true when the caller should re-queue
 *                      this team for a retry once its image table has populated.
 * @return B_OK on success, B_OK with @a addToWaiters set when deferring, or
 *         any error from get_team_info() (other than B_BAD_TEAM_ID, which is
 *         silently swallowed for already-gone teams).
 */
status_t
LocalTargetHostInterface::_HandleTeamEvent(team_id team, int32 opcode,
	bool& addToWaiters)
{
	addToWaiters = false;
	AutoLocker<TargetHost> locker(fTargetHost);
	switch (opcode) {
		case B_TEAM_CREATED:
		case B_TEAM_EXEC:
		{
			team_info info;
			status_t error = get_team_info(team, &info);
			// this team is already gone, no point in sending a notification
			if (error == B_BAD_TEAM_ID)
				return B_OK;
			else if (error != B_OK)
				return error;
			else {
				int32 cookie = 0;
				image_info imageInfo;
				addToWaiters = true;
				while (get_next_image_info(team, &cookie, &imageInfo)
					== B_OK) {
					if (imageInfo.type == B_APP_IMAGE) {
						addToWaiters = false;
						break;
					}
				}
				if (addToWaiters)
					return B_OK;
			}

			if (opcode == B_TEAM_CREATED)
				fTargetHost->AddTeam(info);
			else
				fTargetHost->UpdateTeam(info);
			break;
		}

		case B_TEAM_DELETED:
		{
			fTargetHost->RemoveTeam(team);
			break;
		}

		default:
		{
			break;
		}
	}

	return B_OK;
}
