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
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file BreakpointManager.cpp
 * @brief Drives breakpoint installation and lifecycle for a debugged Team.
 *
 * Tracks the relationships between user-facing UserBreakpoint objects, the
 * per-address Breakpoint records they expand into, and the underlying
 * DebuggerInterface installation. Also reconciles breakpoints when images are
 * loaded (creating instances at newly-resolved addresses) or unloaded
 * (removing the now-stale instances and uninstalling).
 */


#include "BreakpointManager.h"

#include <stdio.h>

#include <new>

#include <AutoLocker.h>

#include "DebuggerInterface.h"
#include "Function.h"
#include "SpecificImageDebugInfo.h"
#include "Statement.h"
#include "Team.h"
#include "Tracing.h"


/**
 * @brief Constructs the manager and acquires a reference on the debugger interface.
 *
 * @param team              The Team whose breakpoint set is being managed.
 * @param debuggerInterface Back-end used to install/uninstall breakpoints; the
 *                          manager holds a reference for its lifetime.
 */
BreakpointManager::BreakpointManager(Team* team,
	DebuggerInterface* debuggerInterface)
	:
	fLock("breakpoint manager"),
	fTeam(team),
	fDebuggerInterface(debuggerInterface)
{
	fDebuggerInterface->AcquireReference();
}


/**
 * @brief Releases the debugger-interface reference held by the manager.
 */
BreakpointManager::~BreakpointManager()
{
	fDebuggerInterface->ReleaseReference();
}


/**
 * @brief Performs late initialization of the manager's lock.
 *
 * @return Result of the lock's InitCheck (B_OK on success).
 */
status_t
BreakpointManager::Init()
{
	return fLock.InitCheck();
}


/**
 * @brief Installs (or updates the enabled state of) a user breakpoint.
 *
 * Walks the user breakpoint's instances, ensures each one has a Breakpoint
 * record bound to it (creating a new record if needed), then reconciles the
 * actual installation state by calling _UpdateBreakpointInstallation(). On
 * any error, fully reverts to the prior state so the team's view stays
 * consistent.
 *
 * @param userBreakpoint  User-visible breakpoint to install or update.
 * @param enabled         Desired enabled state.
 * @return B_OK on success, B_BAD_ADDRESS if any instance lies outside any
 *         loaded image, B_NO_MEMORY on allocation failure, or any error
 *         returned by the debugger interface.
 */
status_t
BreakpointManager::InstallUserBreakpoint(UserBreakpoint* userBreakpoint,
	bool enabled)
{
	TRACE_CONTROL("BreakpointManager::InstallUserBreakpoint(%p, %d)\n",
		userBreakpoint, enabled);

	AutoLocker<BLocker> installLocker(fLock);
	AutoLocker<Team> teamLocker(fTeam);

	bool oldEnabled = userBreakpoint->IsEnabled();
	if (userBreakpoint->IsValid() && enabled == oldEnabled) {
		TRACE_CONTROL("  user breakpoint already valid and with same enabled "
			"state\n");
		return B_OK;
	}

	// get/create the breakpoints for all instances
	TRACE_CONTROL("  creating breakpoints for breakpoint instances\n");

	status_t error = B_OK;
	for (int32 i = 0;
		UserBreakpointInstance* instance = userBreakpoint->InstanceAt(i); i++) {

		TRACE_CONTROL("    breakpoint instance %p\n", instance);

		if (instance->GetBreakpoint() != NULL) {
			TRACE_CONTROL("    -> already has breakpoint\n");
			continue;
		}

		target_addr_t address = instance->Address();
		Breakpoint* breakpoint = fTeam->BreakpointAtAddress(address);
		if (breakpoint == NULL) {
			TRACE_CONTROL("    -> no breakpoint at that address yet\n");

			Image* image = fTeam->ImageByAddress(address);
			if (image == NULL) {
				TRACE_CONTROL("    -> no image at that address\n");
				error = B_BAD_ADDRESS;
				break;
			}

			breakpoint = new(std::nothrow) Breakpoint(image, address);
			if (breakpoint == NULL || !fTeam->AddBreakpoint(breakpoint)) {
				delete breakpoint;
				error = B_NO_MEMORY;
				break;
			}
		}

		TRACE_CONTROL("    -> adding instance to breakpoint %p\n", breakpoint);

		breakpoint->AddUserBreakpoint(instance);
		instance->SetBreakpoint(breakpoint);
	}

	// If everything looks good so far mark the user breakpoint according to
	// its new state.
	if (error == B_OK)
		userBreakpoint->SetEnabled(enabled);

	// notify user breakpoint listeners
	if (error == B_OK)
		fTeam->NotifyUserBreakpointChanged(userBreakpoint);

	teamLocker.Unlock();

	// install/uninstall the breakpoints as needed
	TRACE_CONTROL("  updating breakpoints\n");

	if (error == B_OK) {
		for (int32 i = 0;
			UserBreakpointInstance* instance = userBreakpoint->InstanceAt(i);
			i++) {
			TRACE_CONTROL("    breakpoint instance %p\n", instance);

			error = _UpdateBreakpointInstallation(instance->GetBreakpoint());
			if (error != B_OK)
				break;
		}
	}

	if (error == B_OK) {
		TRACE_CONTROL("  success, marking user breakpoint valid\n");

		// everything went fine -- mark the user breakpoint valid
		if (!userBreakpoint->IsValid()) {
			teamLocker.Lock();
			userBreakpoint->SetValid(true);
			userBreakpoint->AcquireReference();
			fTeam->AddUserBreakpoint(userBreakpoint);
			fTeam->NotifyUserBreakpointChanged(userBreakpoint);
				// notify again -- the breakpoint hadn't been added before
			teamLocker.Unlock();
		}
	} else {
		// something went wrong -- revert the situation
		TRACE_CONTROL("  error, reverting\n");

		teamLocker.Lock();
		userBreakpoint->SetEnabled(oldEnabled);
		teamLocker.Unlock();

		if (!oldEnabled || !userBreakpoint->IsValid()) {
			for (int32 i = 0;  UserBreakpointInstance* instance
					= userBreakpoint->InstanceAt(i);
				i++) {
				Breakpoint* breakpoint = instance->GetBreakpoint();
				if (breakpoint == NULL)
					continue;

				if (!userBreakpoint->IsValid()) {
					instance->SetBreakpoint(NULL);
					breakpoint->RemoveUserBreakpoint(instance);
				}

				_UpdateBreakpointInstallation(breakpoint);

				teamLocker.Lock();

				if (breakpoint->IsUnused())
					fTeam->RemoveBreakpoint(breakpoint);
				teamLocker.Unlock();
			}

			teamLocker.Lock();
			fTeam->NotifyUserBreakpointChanged(userBreakpoint);
			teamLocker.Unlock();
		}
	}

	installLocker.Unlock();

	return error;
}


/**
 * @brief Uninstalls a previously-installed user breakpoint and its instances.
 *
 * Marks the user breakpoint invalid, detaches each instance from its
 * Breakpoint record, uninstalls the underlying breakpoints whose last user
 * just went away, and releases the reference taken at install time.
 *
 * @param userBreakpoint  User-visible breakpoint to remove. No-op if invalid.
 */
void
BreakpointManager::UninstallUserBreakpoint(UserBreakpoint* userBreakpoint)
{
	AutoLocker<BLocker> installLocker(fLock);
	AutoLocker<Team> teamLocker(fTeam);

	if (!userBreakpoint->IsValid())
		return;

	fTeam->RemoveUserBreakpoint(userBreakpoint);

	userBreakpoint->SetValid(false);
	userBreakpoint->SetEnabled(false);

	teamLocker.Unlock();

	// uninstall the breakpoints as needed
	for (int32 i = 0;
		UserBreakpointInstance* instance = userBreakpoint->InstanceAt(i); i++) {
		if (Breakpoint* breakpoint = instance->GetBreakpoint())
			_UpdateBreakpointInstallation(breakpoint);
	}

	teamLocker.Lock();

	// detach the breakpoints from the user breakpoint instances
	for (int32 i = 0;
		UserBreakpointInstance* instance = userBreakpoint->InstanceAt(i); i++) {
		if (Breakpoint* breakpoint = instance->GetBreakpoint()) {
			instance->SetBreakpoint(NULL);
			breakpoint->RemoveUserBreakpoint(instance);

			if (breakpoint->IsUnused())
				fTeam->RemoveBreakpoint(breakpoint);
		}
	}

	fTeam->NotifyUserBreakpointChanged(userBreakpoint);

	teamLocker.Unlock();
	installLocker.Unlock();

	// release the reference from InstallUserBreakpoint()
	userBreakpoint->ReleaseReference();
}


/**
 * @brief Installs a short-lived breakpoint owned by a runtime client.
 *
 * Used for things like step-over and step-out, where the debugger needs to
 * stop at a known address but no user-visible breakpoint should appear. If a
 * Breakpoint record already exists at @a address it is reused; otherwise a
 * new one is created and added to the team.
 *
 * @param address  Target-side instruction address to break on.
 * @param client   Client object that will own the breakpoint reference.
 * @return B_OK on success, B_BAD_ADDRESS if no image covers the address,
 *         B_NO_MEMORY on allocation failure, or any error returned by the
 *         debugger interface during installation.
 */
status_t
BreakpointManager::InstallTemporaryBreakpoint(target_addr_t address,
	BreakpointClient* client)
{
	AutoLocker<BLocker> installLocker(fLock);
	AutoLocker<Team> teamLocker(fTeam);

	// create a breakpoint, if it doesn't exist yet
	Breakpoint* breakpoint = fTeam->BreakpointAtAddress(address);
	if (breakpoint == NULL) {
		Image* image = fTeam->ImageByAddress(address);
		if (image == NULL)
			return B_BAD_ADDRESS;

		breakpoint = new(std::nothrow) Breakpoint(image, address);
		if (breakpoint == NULL)
			return B_NO_MEMORY;

		if (!fTeam->AddBreakpoint(breakpoint))
			return B_NO_MEMORY;
	}

	BReference<Breakpoint> breakpointReference(breakpoint);

	// add the client
	status_t error;
	if (breakpoint->AddClient(client)) {
		if (breakpoint->IsInstalled())
			return B_OK;

		// install
		teamLocker.Unlock();

		error = fDebuggerInterface->InstallBreakpoint(address);
		if (error == B_OK) {
			breakpoint->SetInstalled(true);
			return B_OK;
		}

		teamLocker.Lock();

		breakpoint->RemoveClient(client);
	} else
		error = B_NO_MEMORY;

	// clean up on error
	if (breakpoint->IsUnused())
		fTeam->RemoveBreakpoint(breakpoint);

	return error;
}


/**
 * @brief Removes a temporary breakpoint previously installed for @a client.
 *
 * Drops @a client from the breakpoint's owner set; if no users remain and the
 * underlying breakpoint is no longer needed, uninstalls the hardware site and
 * removes the team-level record.
 *
 * @param address  Address of the temporary breakpoint.
 * @param client   Client originally passed to InstallTemporaryBreakpoint().
 */
void
BreakpointManager::UninstallTemporaryBreakpoint(target_addr_t address,
	BreakpointClient* client)
{
	AutoLocker<BLocker> installLocker(fLock);
	AutoLocker<Team> teamLocker(fTeam);

	Breakpoint* breakpoint = fTeam->BreakpointAtAddress(address);
	if (breakpoint == NULL)
		return;

	// remove the client
	breakpoint->RemoveClient(client);

	// check whether the breakpoint needs to be uninstalled
	bool uninstall = !breakpoint->ShouldBeInstalled()
		&& breakpoint->IsInstalled();

	// if unused remove it
	BReference<Breakpoint> breakpointReference(breakpoint);
	if (breakpoint->IsUnused())
		fTeam->RemoveBreakpoint(breakpoint);

	teamLocker.Unlock();

	if (uninstall) {
		fDebuggerInterface->UninstallBreakpoint(address);
		breakpoint->SetInstalled(false);
	}
}


/**
 * @brief Reconciles breakpoints with the contents of a freshly-loaded image.
 *
 * Removes stale instances that pointed at unloaded code and creates new
 * instances for any user breakpoint that names a function present in the
 * incoming image.
 *
 * @param image  Image whose load just completed.
 */
void
BreakpointManager::UpdateImageBreakpoints(Image* image)
{
	_UpdateImageBreakpoints(image, false);
}


/**
 * @brief Removes breakpoint instances that referenced an image being unloaded.
 *
 * @param image  Image about to be unloaded.
 */
void
BreakpointManager::RemoveImageBreakpoints(Image* image)
{
	_UpdateImageBreakpoints(image, true);
}


/**
 * @brief Internal implementation shared by UpdateImageBreakpoints() and
 *        RemoveImageBreakpoints().
 *
 * Walks all known user breakpoints, drops instances that referenced
 * @a image, and (when @a removeOnly is false) re-creates instances for
 * functions in @a image's debug info — picking the address from the source
 * statement when available, otherwise from the same relative offset within
 * the function.
 *
 * @param image       Image to add or remove breakpoints for.
 * @param removeOnly  If true, only stale removal is performed; if false the
 *                    function also adds new instances using @a image 's debug info.
 */
void
BreakpointManager::_UpdateImageBreakpoints(Image* image, bool removeOnly)
{
	AutoLocker<BLocker> installLocker(fLock);
	AutoLocker<Team> teamLocker(fTeam);

	// remove obsolete user breakpoint instances
	BObjectList<Breakpoint> breakpointsToUpdate;
	for (UserBreakpointList::ConstIterator it
			= fTeam->UserBreakpoints().GetIterator();
		UserBreakpoint* userBreakpoint = it.Next();) {
		int32 instanceCount = userBreakpoint->CountInstances();
		for (int32 i = instanceCount - 1; i >= 0; i--) {
			UserBreakpointInstance* instance = userBreakpoint->InstanceAt(i);
			Breakpoint* breakpoint = instance->GetBreakpoint();
			if (breakpoint == NULL || breakpoint->GetImage() != image)
				continue;

			userBreakpoint->RemoveInstanceAt(i);
			breakpoint->RemoveUserBreakpoint(instance);

			if (!breakpointsToUpdate.AddItem(breakpoint)) {
				_UpdateBreakpointInstallation(breakpoint);
				if (breakpoint->IsUnused())
					fTeam->RemoveBreakpoint(breakpoint);
			}

			delete instance;
		}
	}

	// update breakpoints
	teamLocker.Unlock();
	for (int32 i = 0; Breakpoint* breakpoint = breakpointsToUpdate.ItemAt(i);
			i++) {
		_UpdateBreakpointInstallation(breakpoint);
	}

	teamLocker.Lock();
	for (int32 i = 0; Breakpoint* breakpoint = breakpointsToUpdate.ItemAt(i);
			i++) {
		if (breakpoint->IsUnused())
			fTeam->RemoveBreakpoint(breakpoint);
	}

	// add breakpoint instances for function instances in the image (if we have
	// an image debug info)
	BObjectList<UserBreakpointInstance> newInstances;
	ImageDebugInfo* imageDebugInfo = image->GetImageDebugInfo();
	if (imageDebugInfo == NULL)
		return;

	for (UserBreakpointList::ConstIterator it
			= fTeam->UserBreakpoints().GetIterator();
		UserBreakpoint* userBreakpoint = it.Next();) {
		// get the function
		Function* function = fTeam->FunctionByID(
			userBreakpoint->Location().GetFunctionID());
		if (function == NULL)
			continue;

		const SourceLocation& sourceLocation
			= userBreakpoint->Location().GetSourceLocation();
		target_addr_t relativeAddress
			= userBreakpoint->Location().RelativeAddress();

		// iterate through the function instances
		for (FunctionInstanceList::ConstIterator it
				= function->Instances().GetIterator();
			FunctionInstance* functionInstance = it.Next();) {
			if (functionInstance->GetImageDebugInfo() != imageDebugInfo)
				continue;

			// get the breakpoint address for the instance
			target_addr_t instanceAddress = 0;
			if (functionInstance->SourceFile() != NULL) {
				// We have a source file, so get the address for the source
				// location.
				Statement* statement = NULL;
				FunctionDebugInfo* functionDebugInfo
					= functionInstance->GetFunctionDebugInfo();
				functionDebugInfo->GetSpecificImageDebugInfo()
					->GetStatementAtSourceLocation(functionDebugInfo,
						sourceLocation, statement);
				if (statement != NULL) {
					instanceAddress = statement->CoveringAddressRange().Start();
						// TODO: What about BreakpointAllowed()?
					statement->ReleaseReference();
					// TODO: Make sure we do hit the function in question!
				}
			}

			if (instanceAddress == 0) {
				// No source file (or we failed getting the statement), so try
				// to use the same relative address.
				if (relativeAddress > functionInstance->Size())
					continue;
				instanceAddress = functionInstance->Address() + relativeAddress;
					// TODO: Make sure it does at least hit an instruction!
			}

			// create the user breakpoint instance
			UserBreakpointInstance* instance = new(std::nothrow)
				UserBreakpointInstance(userBreakpoint, instanceAddress);
			if (instance == NULL || !newInstances.AddItem(instance)) {
				delete instance;
				continue;
			}

			if (!userBreakpoint->AddInstance(instance)) {
				newInstances.RemoveItemAt(newInstances.CountItems() - 1);
				delete instance;
			}

			// get/create the breakpoint for the address
			target_addr_t address = instance->Address();
			Breakpoint* breakpoint = fTeam->BreakpointAtAddress(address);
			if (breakpoint == NULL) {
				breakpoint = new(std::nothrow) Breakpoint(image, address);
				if (breakpoint == NULL || !fTeam->AddBreakpoint(breakpoint)) {
					delete breakpoint;
					break;
				}
			}

			breakpoint->AddUserBreakpoint(instance);
			instance->SetBreakpoint(breakpoint);
		}
	}

	// install the breakpoints for the new user breakpoint instances
	teamLocker.Unlock();
	for (int32 i = 0; UserBreakpointInstance* instance = newInstances.ItemAt(i);
			i++) {
		Breakpoint* breakpoint = instance->GetBreakpoint();
		if (breakpoint == NULL
			|| _UpdateBreakpointInstallation(breakpoint) != B_OK) {
			// something went wrong -- remove the instance
			teamLocker.Lock();

			instance->GetUserBreakpoint()->RemoveInstance(instance);
			if (breakpoint != NULL) {
				breakpoint->AddUserBreakpoint(instance);
				if (breakpoint->IsUnused())
					fTeam->RemoveBreakpoint(breakpoint);
			}

			teamLocker.Unlock();
		}
	}
}


/**
 * @brief Brings the hardware install state of @a breakpoint in line with its
 *        ShouldBeInstalled() flag.
 *
 * If the desired state already matches reality the call is a no-op. When
 * installation is needed but the back-end is not yet connected, the request
 * is silently accepted so settings can be saved for later use.
 *
 * @param breakpoint  Breakpoint to reconcile; must be non-NULL.
 * @return B_OK on success or any error returned by the debugger interface.
 * @note Caller must hold fLock.
 */
status_t
BreakpointManager::_UpdateBreakpointInstallation(Breakpoint* breakpoint)
{
	bool shouldBeInstalled = breakpoint->ShouldBeInstalled();

	TRACE_CONTROL("BreakpointManager::_UpdateBreakpointInstallation(%p): "
		"should be installed: %d, is installed: %d\n", breakpoint,
		shouldBeInstalled, breakpoint->IsInstalled());

	if (shouldBeInstalled == breakpoint->IsInstalled())
		return B_OK;

	if (shouldBeInstalled) {
		// install
		status_t error = B_OK;
		// if we're not actually connected to a team, silently
		// allow setting the breakpoint so it's saved to settings
		// for when we do connect/have the team in the debugger.
		if (fDebuggerInterface->Connected())
			fDebuggerInterface->InstallBreakpoint(breakpoint->Address());

		if (error != B_OK)
			return error;

		TRACE_CONTROL("BREAKPOINT at %#" B_PRIx64 " installed: %s\n",
			breakpoint->Address(), strerror(error));

		breakpoint->SetInstalled(true);
	} else {
		// uninstall
		fDebuggerInterface->UninstallBreakpoint(breakpoint->Address());

		TRACE_CONTROL("BREAKPOINT at %#" B_PRIx64 " uninstalled\n",
			breakpoint->Address());

		breakpoint->SetInstalled(false);
	}

	return B_OK;
}
