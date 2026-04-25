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
 *   Copyright 2009-2016, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2011-2016, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DebuggerGlobals.cpp
 * @brief Process-wide setup and teardown for the debugger kit's singleton rosters.
 *
 * Owns construction and destruction of the type-handler, image-debug
 * loading-state-handler, and target-host-interface rosters that the rest of
 * the debugger kit relies on. Called once during process startup and
 * shutdown by hosts that embed the debugger kit.
 */


#include "DebuggerGlobals.h"

#include "ImageDebugLoadingStateHandlerRoster.h"
#include "TargetHostInterface.h"
#include "TypeHandlerRoster.h"


/**
 * @brief Initialize the debugger kit's global rosters and default host interface.
 *
 * Creates the default TypeHandlerRoster, ImageDebugLoadingStateHandlerRoster,
 * and TargetHostInterfaceRoster (passing @a listener for change notifications),
 * then instantiates the first available host interface (typically the local
 * one). Must be paired with debugger_global_uninit().
 *
 * @param listener  Listener forwarded to TargetHostInterfaceRoster::CreateDefault();
 *                  may be NULL when no host-interface change notifications are required.
 * @return Status code of the first failing step.
 * @retval B_OK  All rosters were created and an interface instance is ready.
 */
status_t
debugger_global_init(TargetHostInterfaceRoster::Listener* listener)
{
	status_t error = TypeHandlerRoster::CreateDefault();
	if (error != B_OK)
		return error;

	error = ImageDebugLoadingStateHandlerRoster::CreateDefault();
	if (error != B_OK)
		return error;

	error = TargetHostInterfaceRoster::CreateDefault(listener);
	if (error != B_OK)
		return error;

	// for now, always create an instance of the local interface
	// by default
	TargetHostInterface* hostInterface;
	TargetHostInterfaceRoster* roster = TargetHostInterfaceRoster::Default();
	error = roster->CreateInterface(roster->InterfaceInfoAt(0), NULL,
		hostInterface);
	if (error != B_OK)
		return error;

	return B_OK;
}


/**
 * @brief Tear down the debugger kit's global rosters in reverse creation order.
 *
 * Releases the default TargetHostInterfaceRoster, ImageDebugLoadingStateHandlerRoster,
 * and TypeHandlerRoster. Safe to call only after a successful debugger_global_init().
 */
void
debugger_global_uninit()
{
	TargetHostInterfaceRoster::DeleteDefault();
	ImageDebugLoadingStateHandlerRoster::DeleteDefault();
	TypeHandlerRoster::DeleteDefault();
}
