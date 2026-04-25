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
 * MIT License. Copyright 2009, Ingo Weinhold; Copyright 2012, Rene Gollent.
 */

/** @file WatchpointManager.h
    @brief Manages installation and removal of hardware watchpoints in a debugged team. */

#ifndef WATCHPOINT_MANAGER_H
#define WATCHPOINT_MANAGER_H

#include <Locker.h>

#include "Watchpoint.h"


class DebuggerInterface;
class Team;


/** @brief Mediates between Team-level watchpoint requests and the underlying
           DebuggerInterface, serializing install/uninstall operations. */
class WatchpointManager {
public:
								WatchpointManager(Team* team,
									DebuggerInterface* debuggerInterface);
								~WatchpointManager();

			status_t			Init();

			status_t			InstallWatchpoint(Watchpoint* watchpoint,
									bool enabled);
			void				UninstallWatchpoint(Watchpoint* watchpoint);

private:
			BLocker				fLock;	// used to synchronize un-/installing
			Team*				fTeam;
			DebuggerInterface*	fDebuggerInterface;
};


#endif	// WATCHPOINT_MANAGER_H
