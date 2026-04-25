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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Breakpoint.cpp
 * @brief Implementation of Breakpoint and BreakpointClient: low-level
 *        breakpoint instances managed by the debugger core.
 *
 * A Breakpoint represents a single instruction-address trap inside an
 * Image. It tracks both internal subscribers (BreakpointClient instances)
 * and the user-visible UserBreakpoint instances that map onto it; the
 * controller installs the trap whenever any subscriber requires it.
 */

#include "Breakpoint.h"


// #pragma mark - BreakpointClient


/**
 * @brief Virtual destructor anchor for the BreakpointClient interface.
 */
BreakpointClient::~BreakpointClient()
{
}


// #pragma mark - Breakpoint


/**
 * @brief Constructs an uninstalled Breakpoint at @a address inside @a image.
 *
 * @param image   Image owning the address space; not reference-counted here.
 * @param address Target-space address of the trap instruction.
 */
Breakpoint::Breakpoint(Image* image, target_addr_t address)
	:
	fAddress(address),
	fImage(image),
	fInstalled(false)
{
}


/**
 * @brief Destroys the Breakpoint.
 */
Breakpoint::~Breakpoint()
{
}


/**
 * @brief Records whether the trap instruction is currently in place.
 *
 * @param installed True after the controller successfully installs the trap.
 */
void
Breakpoint::SetInstalled(bool installed)
{
	fInstalled = installed;
}


/**
 * @brief Determines whether the trap should currently be installed.
 *
 * Returns true if any internal client has subscribed or if at least one
 * mapped UserBreakpoint is enabled.
 *
 * @return True if installation is required to satisfy a subscriber.
 */
bool
Breakpoint::ShouldBeInstalled() const
{
	if (!fClients.IsEmpty())
		return true;

	return !fClients.IsEmpty() || HasEnabledUserBreakpoint();
}


/**
 * @brief Reports whether the breakpoint has neither clients nor user maps.
 *
 * @return True if the Breakpoint can be reaped by the controller.
 */
bool
Breakpoint::IsUnused() const
{
	return fClients.IsEmpty() && fUserBreakpoints.IsEmpty();
}


/**
 * @brief Tests whether any mapped UserBreakpoint is enabled.
 *
 * @return True if at least one UserBreakpointInstance has its parent
 *         UserBreakpoint enabled.
 */
bool
Breakpoint::HasEnabledUserBreakpoint() const
{
	for (UserBreakpointInstanceList::ConstIterator it
				= fUserBreakpoints.GetIterator();
			UserBreakpointInstance* instance = it.Next();) {
		if (instance->GetUserBreakpoint()->IsEnabled())
			return true;
	}

	return false;
}


/**
 * @brief Attaches a UserBreakpointInstance to this Breakpoint.
 *
 * @param instance User-breakpoint instance to register.
 */
void
Breakpoint::AddUserBreakpoint(UserBreakpointInstance* instance)
{
	fUserBreakpoints.Add(instance);
}


/**
 * @brief Detaches a previously added UserBreakpointInstance.
 *
 * @param instance User-breakpoint instance to remove.
 */
void
Breakpoint::RemoveUserBreakpoint(UserBreakpointInstance* instance)
{
	fUserBreakpoints.Remove(instance);
}


/**
 * @brief Registers an internal subscriber for this Breakpoint.
 *
 * @param client Client to add.
 * @return      True on success, false on allocation failure.
 */
bool
Breakpoint::AddClient(BreakpointClient* client)
{
	return fClients.AddItem(client);
}


/**
 * @brief Unregisters a previously added internal subscriber.
 *
 * @param client Client to remove.
 */
void
Breakpoint::RemoveClient(BreakpointClient* client)
{
	fClients.RemoveItem(client);
}


/**
 * @brief Comparator ordering two breakpoints by address.
 *
 * @param a First breakpoint.
 * @param b Second breakpoint.
 * @return -1 if @a a precedes @a b, 0 if equal, 1 otherwise.
 */
/*static*/ int
Breakpoint::CompareBreakpoints(const Breakpoint* a, const Breakpoint* b)
{
	if (a->Address() < b->Address())
		return -1;
	return a->Address() == b->Address() ? 0 : 1;
}


/**
 * @brief Comparator locating a breakpoint by address.
 *
 * @param address    Address being searched for.
 * @param breakpoint Candidate breakpoint to compare against.
 * @return -1, 0, or 1 with the standard search-key ordering.
 */
/*static*/ int
Breakpoint::CompareAddressBreakpoint(const target_addr_t* address,
	const Breakpoint* breakpoint)
{
	if (*address < breakpoint->Address())
		return -1;
	return *address == breakpoint->Address() ? 0 : 1;
}
