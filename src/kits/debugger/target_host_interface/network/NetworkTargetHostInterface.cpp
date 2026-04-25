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
 *   Copyright 2016-2017, Rene Gollent, rene@gollent.com.
 *   Copyright 2016, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file NetworkTargetHostInterface.cpp
 * @brief Network target-host stub providing the interface contract while the
 *        network protocol is still under development.
 *
 * Every operation currently returns @c B_NOT_SUPPORTED; the class exists so
 * the front-end can offer a network transport choice and the runtime can
 * reject debugging attempts cleanly until the wire protocol is implemented.
 */


#include "NetworkTargetHostInterface.h"

#include <AutoDeleter.h>
#include <AutoLocker.h>
#include <system_info.h>
#include <kernel/util/KMessage.h>

#include "debug_utils.h"

#include "TargetHost.h"


/**
 * @brief Constructs the interface with the display name "Network".
 */
NetworkTargetHostInterface::NetworkTargetHostInterface()
	:
	TargetHostInterface(),
	fTargetHost(NULL)
{
	SetName("Network");
}


/**
 * @brief Closes the interface and releases the associated TargetHost reference.
 */
NetworkTargetHostInterface::~NetworkTargetHostInterface()
{
	Close();

	if (fTargetHost != NULL)
		fTargetHost->ReleaseReference();
}


/**
 * @brief Establishes the network connection described by @a settings.
 *
 * @param settings  Connection settings (currently unused).
 * @return Always @c B_NOT_SUPPORTED until the network transport is implemented.
 * @todo Implement the wire protocol and host discovery.
 */
status_t
NetworkTargetHostInterface::Init(Settings* settings)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Tears down the network connection; currently a no-op.
 */
void
NetworkTargetHostInterface::Close()
{
}


/**
 * @brief Reports whether this interface represents a local-host transport.
 *
 * @return Always false.
 */
bool
NetworkTargetHostInterface::IsLocal() const
{
	return false;
}


/**
 * @brief Reports whether the network transport is currently connected.
 *
 * @return Always false (stub).
 */
bool
NetworkTargetHostInterface::Connected() const
{
	return false;
}


/**
 * @brief Returns the TargetHost paired with the connection.
 *
 * @return Pointer to the TargetHost (currently @c NULL).
 */
TargetHost*
NetworkTargetHostInterface::GetTargetHost()
{
	return fTargetHost;
}


/**
 * @brief Attaches to a remote team for debugging.
 *
 * @param teamID      Identifier of the team to attach to.
 * @param threadID    Optional initial thread to focus on.
 * @param _interface  On success, set to a fresh DebuggerInterface for the team.
 * @return Always @c B_NOT_SUPPORTED until the network transport is implemented.
 */
status_t
NetworkTargetHostInterface::Attach(team_id teamID, thread_id threadID,
	DebuggerInterface*& _interface) const
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Spawns a new team on the remote host under debugger control.
 *
 * @param commandLineArgc  Number of command-line arguments.
 * @param arguments        Argv-style argument vector.
 * @param _teamID          On success, set to the new team's id.
 * @return Always @c B_NOT_SUPPORTED until the network transport is implemented.
 */
status_t
NetworkTargetHostInterface::CreateTeam(int commandLineArgc,
	const char* const* arguments, team_id& _teamID) const
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Loads a core file from the remote host for post-mortem debugging.
 *
 * @param coreFilePath  Remote-side path to the core file.
 * @param _interface    On success, set to a DebuggerInterface backing the dump.
 * @param _thread       On success, set to the originating thread id.
 * @return Always @c B_NOT_SUPPORTED until the network transport is implemented.
 */
status_t
NetworkTargetHostInterface::LoadCore(const char* coreFilePath,
	DebuggerInterface*& _interface, thread_id& _thread) const
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Resolves a thread id to its owning team on the remote host.
 *
 * @param thread   Thread id to look up.
 * @param _teamID  On success, set to the owning team id.
 * @return Always @c B_NOT_SUPPORTED until the network transport is implemented.
 */
status_t
NetworkTargetHostInterface::FindTeamByThread(thread_id thread,
	team_id& _teamID) const
{
	return B_NOT_SUPPORTED;
}
