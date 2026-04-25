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
 * MIT License. Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 */

/** @file BreakpointManager.h
    @brief Manages user and temporary breakpoints in a debugged team. */

#ifndef BREAKPOINT_MANAGER_H
#define BREAKPOINT_MANAGER_H

#include <Locker.h>

#include "Breakpoint.h"


class DebuggerInterface;
class Team;


/** @brief Coordinates breakpoint installation across user-visible breakpoints
           and short-lived temporary breakpoints, reconciling installation state
           against image load/unload events. */
class BreakpointManager {
public:
								BreakpointManager(Team* team,
									DebuggerInterface* debuggerInterface);
								~BreakpointManager();

			status_t			Init();

			status_t			InstallUserBreakpoint(
									UserBreakpoint* userBreakpoint,
									bool enabled);
			void				UninstallUserBreakpoint(
									UserBreakpoint* userBreakpoint);

			status_t			InstallTemporaryBreakpoint(
									target_addr_t address,
									BreakpointClient* client);
			void				UninstallTemporaryBreakpoint(
									target_addr_t address,
									BreakpointClient* client);

			void				UpdateImageBreakpoints(Image* image);
			void				RemoveImageBreakpoints(Image* image);

private:
			void				_UpdateImageBreakpoints(Image* image,
									bool removeOnly);
			status_t			_UpdateBreakpointInstallation(
									Breakpoint* breakpoint);
										// fLock must be held

private:
			BLocker				fLock;	// used to synchronize un-/installing
			Team*				fTeam;
			DebuggerInterface*	fDebuggerInterface;
};


#endif	// BREAKPOINT_MANAGER_H
