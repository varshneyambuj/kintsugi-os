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
 *   Copyright 2009-2012, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2012, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file WatchpointManager.cpp
 * @brief Coordinates installation, removal, and team-state synchronization for
 *        hardware watchpoints attached to a debugged Team.
 *
 * Methods serialize via the per-manager lock and the Team lock so that
 * concurrent UI requests and asynchronous debugger events cannot leave the
 * watchpoint set inconsistent.
 */


#include "WatchpointManager.h"

#include <stdio.h>

#include <new>

#include <AutoLocker.h>

#include "DebuggerInterface.h"
#include "Team.h"
#include "Tracing.h"


/**
 * @brief Constructs the manager and acquires a reference on the debugger interface.
 *
 * @param team              The Team whose watchpoint set is being managed.
 * @param debuggerInterface Back-end used to actually install watchpoints; the
 *                          manager holds an additional reference for its lifetime.
 */
WatchpointManager::WatchpointManager(Team* team,
	DebuggerInterface* debuggerInterface)
	:
	fLock("watchpoint manager"),
	fTeam(team),
	fDebuggerInterface(debuggerInterface)
{
	fDebuggerInterface->AcquireReference();
}


/**
 * @brief Releases the debugger-interface reference held by the manager.
 */
WatchpointManager::~WatchpointManager()
{
	fDebuggerInterface->ReleaseReference();
}


/**
 * @brief Performs late initialization of the manager's mutex.
 *
 * @return Result of the lock's InitCheck (B_OK on success).
 */
status_t
WatchpointManager::Init()
{
	return fLock.InitCheck();
}


/**
 * @brief Installs (or updates the enabled state of) a watchpoint in the team.
 *
 * Updates @a watchpoint 's stored enabled flag, then either installs the
 * hardware watchpoint via the debugger interface or removes it depending on
 * whether the watchpoint should currently be active. The team is notified of
 * the resulting change.
 *
 * @param watchpoint Watchpoint to install or update; must be non-NULL.
 * @param enabled    Desired enabled state.
 * @return B_OK if the watchpoint matches @a enabled at exit, otherwise an
 *         error from the underlying DebuggerInterface call.
 */
status_t
WatchpointManager::InstallWatchpoint(Watchpoint* watchpoint,
	bool enabled)
{
	status_t error = B_OK;
	TRACE_CONTROL("WatchpointManager::InstallUserWatchpoint(%p, %d)\n",
		watchpoint, enabled);

	AutoLocker<BLocker> installLocker(fLock);
	AutoLocker<Team> teamLocker(fTeam);

	bool oldEnabled = watchpoint->IsEnabled();
	if (enabled == oldEnabled) {
		TRACE_CONTROL("  watchpoint already valid and with same enabled "
			"state\n");
		return B_OK;
	}

	watchpoint->SetEnabled(enabled);

	if (watchpoint->ShouldBeInstalled()) {
		error = fDebuggerInterface->InstallWatchpoint(watchpoint->Address(),
			watchpoint->Type(), watchpoint->Length());

		if (error == B_OK)
			watchpoint->SetInstalled(true);
	} else {
		error = fDebuggerInterface->UninstallWatchpoint(watchpoint->Address());

		if (error == B_OK)
			watchpoint->SetInstalled(false);
	}

	if (error == B_OK) {
		if (fTeam->WatchpointAtAddress(watchpoint->Address()) == NULL)
			fTeam->AddWatchpoint(watchpoint);
		fTeam->NotifyWatchpointChanged(watchpoint);
	}

	return error;
}


/**
 * @brief Removes a watchpoint from the team and uninstalls hardware tracking.
 *
 * Removes the watchpoint from the team's bookkeeping unconditionally; only
 * issues a hardware uninstall if the watchpoint is currently installed. On
 * success, observers are notified through the team.
 *
 * @param watchpoint Watchpoint to remove; must be non-NULL.
 */
void
WatchpointManager::UninstallWatchpoint(Watchpoint* watchpoint)
{
	AutoLocker<BLocker> installLocker(fLock);
	AutoLocker<Team> teamLocker(fTeam);

	fTeam->RemoveWatchpoint(watchpoint);

	if (!watchpoint->IsInstalled())
		return;

	status_t error = fDebuggerInterface->UninstallWatchpoint(
		watchpoint->Address());

	if (error == B_OK) {
		watchpoint->SetInstalled(false);
		fTeam->NotifyWatchpointChanged(watchpoint);
	}
}
