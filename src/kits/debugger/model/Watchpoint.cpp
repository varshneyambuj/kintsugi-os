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
 *   Copyright 2012, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Watchpoint.cpp
 * @brief Implementation of Watchpoint, a memory-range data observer used
 *        to halt execution when a target address is accessed.
 *
 * Watchpoint records the watched address, access mode (read/write), and
 * length, plus the installed/enabled flags that the architecture-specific
 * controller consults when programming hardware watch registers.
 */

#include "Watchpoint.h"


/**
 * @brief Constructs a disabled, uninstalled Watchpoint over a memory range.
 *
 * @param address Base target-space address being watched.
 * @param type    Access type to trap on (e.g. write, read-write).
 * @param length  Number of bytes the watch covers from @a address.
 */
Watchpoint::Watchpoint(target_addr_t address, uint32 type, int32 length)
	:
	fAddress(address),
	fType(type),
	fLength(length),
	fInstalled(false),
	fEnabled(false)
{
}


/**
 * @brief Destroys the Watchpoint.
 */
Watchpoint::~Watchpoint()
{
}


/**
 * @brief Records whether the watchpoint is currently programmed in hardware.
 *
 * @param installed True after a successful install, false after an uninstall.
 */
void
Watchpoint::SetInstalled(bool installed)
{
	fInstalled = installed;
}


/**
 * @brief Marks the watchpoint enabled or disabled by user intent.
 *
 * @param enabled True to enable, false to suspend without removing it.
 */
void
Watchpoint::SetEnabled(bool enabled)
{
	fEnabled = enabled;
}


/**
 * @brief Tests whether @a address falls within the watched range.
 *
 * @param address Target-space address to test.
 * @return       True if @a address is inside [base, base + length].
 */
bool
Watchpoint::Contains(target_addr_t address) const
{
	return address >= fAddress && address <= (fAddress + fLength);
}


/**
 * @brief Comparator ordering two watchpoints by base address.
 *
 * @param a First watchpoint.
 * @param b Second watchpoint.
 * @return -1 if @a a precedes @a b, 0 if equal, 1 if @a a follows @a b.
 */
int
Watchpoint::CompareWatchpoints(const Watchpoint* a, const Watchpoint* b)
{
	if (a->Address() < b->Address())
		return -1;
	return a->Address() == b->Address() ? 0 : 1;
}


/**
 * @brief Comparator locating a watchpoint by its base address.
 *
 * @param address    Address being searched for.
 * @param watchpoint Candidate watchpoint to compare against.
 * @return -1, 0, or 1 in the same convention as @c CompareWatchpoints().
 */
int
Watchpoint::CompareAddressWatchpoint(const target_addr_t* address,
	const Watchpoint* watchpoint)
{
	if (*address < watchpoint->Address())
		return -1;
	return *address == watchpoint->Address() ? 0 : 1;
}



