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
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file TargetHostInterfaceRoster.cpp
 * @brief Process-global registry of available debugger transports and the
 *        active TargetHostInterface instances spawned from them.
 *
 * Owns one TargetHostInterfaceInfo per supported transport (Local, Network,
 * ...) and a list of running TargetHostInterface BLoopers. Listens to each
 * interface so it can keep a global running-team-debugger count and report
 * changes back to the application's UI listener.
 */


#include "TargetHostInterfaceRoster.h"

#include <new>

#include <AutoDeleter.h>
#include <AutoLocker.h>

#include "LocalTargetHostInterfaceInfo.h"
#include "NetworkTargetHostInterfaceInfo.h"
#include "TargetHostInterfaceInfo.h"


/** @brief Process-wide singleton used by the rest of the debugger to find transports. */
/*static*/ TargetHostInterfaceRoster*
	TargetHostInterfaceRoster::sDefaultInstance = NULL;


/**
 * @brief Constructs an empty roster; Init() must be called before use.
 */
TargetHostInterfaceRoster::TargetHostInterfaceRoster()
	:
	TargetHostInterface::Listener(),
	fLock(),
	fRunningTeamDebuggers(0),
	fInterfaceInfos(20),
	fActiveInterfaces(20),
	fListener(NULL)
{
}


/**
 * @brief Releases all interface descriptors and quits every active interface.
 */
TargetHostInterfaceRoster::~TargetHostInterfaceRoster()
{
	for (int32 i = 0; TargetHostInterfaceInfo* info
			= fInterfaceInfos.ItemAt(i); i++) {
		info->ReleaseReference();
	}

	for (int32 i = 0; TargetHostInterface* interface
			= fActiveInterfaces.ItemAt(i); i++) {
		if (interface->Lock())
			interface->Quit();
	}
}


/**
 * @brief Returns the process-wide roster instance (may be NULL before init).
 *
 * @return Pointer to the default roster, or NULL if CreateDefault() has not run.
 */
/*static*/ TargetHostInterfaceRoster*
TargetHostInterfaceRoster::Default()
{
	return sDefaultInstance;
}


/**
 * @brief Constructs and registers the default roster on first call.
 *
 * @param listener  Listener notified of running-debugger-count changes.
 * @return B_OK if a default already exists or the new one initialized cleanly,
 *         B_NO_MEMORY on allocation failure, or any error from Init() /
 *         RegisterInterfaceInfos().
 */
/*static*/ status_t
TargetHostInterfaceRoster::CreateDefault(Listener* listener)
{
	if (sDefaultInstance != NULL)
		return B_OK;

	TargetHostInterfaceRoster* roster
		= new(std::nothrow) TargetHostInterfaceRoster;
	if (roster == NULL)
		return B_NO_MEMORY;
	ObjectDeleter<TargetHostInterfaceRoster> rosterDeleter(roster);

	status_t error = roster->Init(listener);
	if (error != B_OK)
		return error;

	error = roster->RegisterInterfaceInfos();
	if (error != B_OK)
		return error;

	sDefaultInstance = rosterDeleter.Detach();
	return B_OK;
}


/**
 * @brief Tears down the default roster created by CreateDefault().
 */
/*static*/ void
TargetHostInterfaceRoster::DeleteDefault()
{
	TargetHostInterfaceRoster* roster = sDefaultInstance;
	sDefaultInstance = NULL;
	delete roster;
}


/**
 * @brief Performs first-stage initialization of the lock and listener.
 *
 * @param listener  External listener for global team-debugger count changes.
 * @return Result of the lock's InitCheck (B_OK on success).
 */
status_t
TargetHostInterfaceRoster::Init(Listener* listener)
{
	fListener = listener;
	return fLock.InitCheck();
}


/**
 * @brief Instantiates and registers descriptors for every supported transport.
 *
 * Currently registers the local and network transport descriptors. Adding a
 * new transport is a matter of extending REGISTER_INTERFACE_INFO calls.
 *
 * @return B_OK on success, B_NO_MEMORY if any descriptor allocation or
 *         insertion fails.
 */
status_t
TargetHostInterfaceRoster::RegisterInterfaceInfos()
{
	TargetHostInterfaceInfo* info = NULL;
	BReference<TargetHostInterfaceInfo> interfaceReference;

	#undef REGISTER_INTERFACE_INFO
	#define REGISTER_INTERFACE_INFO(type) \
		info = new(std::nothrow) type##TargetHostInterfaceInfo; \
		if (info == NULL) \
			return B_NO_MEMORY; \
		interfaceReference.SetTo(info, true); \
		if (info->Init() != B_OK) \
			return B_NO_MEMORY; \
		if (!fInterfaceInfos.AddItem(info)) \
			return B_NO_MEMORY; \
		interfaceReference.Detach();

	REGISTER_INTERFACE_INFO(Local)
	REGISTER_INTERFACE_INFO(Network)

	return B_OK;
}


/**
 * @brief Returns the number of registered transport descriptors.
 *
 * @return Count of TargetHostInterfaceInfo objects in the registry.
 */
int32
TargetHostInterfaceRoster::CountInterfaceInfos() const
{
	return fInterfaceInfos.CountItems();
}


/**
 * @brief Returns the descriptor at @a index in the registry.
 *
 * @param index  Zero-based index into the descriptor list.
 * @return Pointer to the descriptor, or NULL if @a index is out of range.
 */
TargetHostInterfaceInfo*
TargetHostInterfaceRoster::InterfaceInfoAt(int32 index) const
{
	return fInterfaceInfos.ItemAt(index);
}


/**
 * @brief Builds and starts a new TargetHostInterface from @a info and @a settings.
 *
 * Creates the interface via the descriptor, kicks off its BLooper via Run(),
 * inserts it into the active list, and registers the roster as a listener.
 *
 * @param info        Transport descriptor selected by the user.
 * @param settings    User-supplied transport settings (may be NULL for local).
 * @param _interface  On success, set to the newly-running interface; ownership
 *                    remains with the roster but the caller may use it freely.
 * @return B_OK on success, an error from descriptor creation, or B_NO_MEMORY
 *         if the active list cannot accept the new interface.
 * @todo Verify that an active interface with matching settings/type doesn't
 *       already exist and reuse it (notably for the local transport).
 */
status_t
TargetHostInterfaceRoster::CreateInterface(TargetHostInterfaceInfo* info,
	Settings* settings, TargetHostInterface*& _interface)
{
	// TODO: this should eventually verify that an active interface with
	// matching settings/type doesn't already exist, and if so, return that
	// directly rather than instantiating a new one, since i.e. the interface
	// for the local host only requires one instance.
	AutoLocker<TargetHostInterfaceRoster> locker(this);
	TargetHostInterface* interface;
	status_t error = info->CreateInterface(settings, interface);
	if (error != B_OK)
		return error;

	error = interface->Run();
	if (error < B_OK || !fActiveInterfaces.AddItem(interface)) {
		delete interface;
		return B_NO_MEMORY;
	}

	interface->AddListener(this);
	_interface = interface;
	return B_OK;
}


/**
 * @brief Returns the number of currently-running interfaces.
 *
 * @return Count of active TargetHostInterface objects.
 */
int32
TargetHostInterfaceRoster::CountActiveInterfaces() const
{
	return fActiveInterfaces.CountItems();
}


/**
 * @brief Returns the active interface at @a index.
 *
 * @param index  Zero-based index into the active interface list.
 * @return Pointer to the interface, or NULL if @a index is out of range.
 */
TargetHostInterface*
TargetHostInterfaceRoster::ActiveInterfaceAt(int32 index) const
{
	return fActiveInterfaces.ItemAt(index);
}


/**
 * @brief Listener hook: increments the running-debugger count and notifies up.
 *
 * @param debugger  Newly-running TeamDebugger; not used directly here.
 */
void
TargetHostInterfaceRoster::TeamDebuggerStarted(TeamDebugger* debugger)
{
	fRunningTeamDebuggers++;
	fListener->TeamDebuggerCountChanged(fRunningTeamDebuggers);
}


/**
 * @brief Listener hook: decrements the running-debugger count and notifies up.
 *
 * @param debugger  TeamDebugger that has finished; not used directly here.
 */
void
TargetHostInterfaceRoster::TeamDebuggerQuit(TeamDebugger* debugger)
{
	fRunningTeamDebuggers--;
	fListener->TeamDebuggerCountChanged(fRunningTeamDebuggers);
}


/**
 * @brief Listener hook: removes an interface from the active list when it quits.
 *
 * @param interface  Interface that has quit.
 */
void
TargetHostInterfaceRoster::TargetHostInterfaceQuit(
	TargetHostInterface* interface)
{
	AutoLocker<TargetHostInterfaceRoster> locker(this);
	fActiveInterfaces.RemoveItem(interface);

}


// #pragma mark - TargetHostInterfaceRoster::Listener


/**
 * @brief Virtual destructor for the roster's listener interface.
 */
TargetHostInterfaceRoster::Listener::~Listener()
{
}


/**
 * @brief Default no-op hook called when the running team-debugger count changes.
 *
 * @param count  New number of running team debuggers; ignored.
 */
void
TargetHostInterfaceRoster::Listener::TeamDebuggerCountChanged(int32 count)
{
}
