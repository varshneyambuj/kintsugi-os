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
 * @file BreakpointManager.cpp
 * @brief User-space software/hardware breakpoint bookkeeping for the user_debugger.
 *
 * Maintains a per-team table of installed breakpoints plus the pool of underlying
 * hardware breakpoint slots. Each InstalledBreakpoint is keyed by user virtual
 * address in a tree; the associated Breakpoint object tracks whether it is backed
 * by a hardware slot or by a software trap (in which case the original instruction
 * bytes are saved in softwareData for later restoration). The manager also owns
 * the list of installed watchpoints.
 *
 * Locking: fLock is a read/write lock scoped to a single team's BreakpointManager
 * instance. The team's debug_info lock is held higher up in user_debugger.cpp
 * when mutating breakpoint state, so callers must respect that ordering
 * (debug_info first, then BreakpointManager::fLock). ReadMemory() takes fLock
 * shared; all mutators take it exclusively via WriteLocker.
 */

#include "BreakpointManager.h"

#include <algorithm>

#include <AutoDeleter.h>

#include <kernel.h>
#include <util/AutoLock.h>
#include <vm/vm.h>


//#define TRACE_BREAKPOINT_MANAGER
#ifdef TRACE_BREAKPOINT_MANAGER
#	define TRACE(x...) dprintf(x)
#else
#	define TRACE(x...) do {} while (false)
#endif


// soft limit for the number of breakpoints
const int32 kMaxBreakpointCount = 10240;


/**
 * @brief Construct an InstalledBreakpoint record for a user address.
 *
 * The underlying Breakpoint pointer is left null until the manager decides
 * whether the breakpoint will be backed by a hardware slot or a software trap.
 *
 * @param address User virtual address at which the breakpoint is requested.
 */
BreakpointManager::InstalledBreakpoint::InstalledBreakpoint(addr_t address)
	:
	breakpoint(NULL),
	address(address)
{
}


// #pragma mark -


/**
 * @brief Construct an empty BreakpointManager for a team.
 *
 * Initializes the internal read/write lock. Hardware breakpoint slot objects
 * are created separately in Init(); do not use the manager until Init() has
 * returned B_OK.
 */
BreakpointManager::BreakpointManager()
	:
	fBreakpointCount(0),
	fWatchpointCount(0)
{
	rw_lock_init(&fLock, "breakpoint manager");
}


/**
 * @brief Destroy the manager, removing all breakpoints and watchpoints.
 *
 * Walks the installed breakpoint tree and the watchpoint list, deleting each
 * record and its underlying software breakpoint (if any). Hardware breakpoint
 * slot objects are also freed. Called when a team exits; the team's debug_info
 * lock must already be held by the caller so no new installs race with us.
 */
BreakpointManager::~BreakpointManager()
{
	WriteLocker locker(fLock);

	// delete the installed breakpoint objects
	BreakpointTree::Iterator it = fBreakpoints.GetIterator();
	while (InstalledBreakpoint* installedBreakpoint = it.Next()) {
		it.Remove();

		// delete underlying software breakpoint
		if (installedBreakpoint->breakpoint->software)
			delete installedBreakpoint->breakpoint;

		delete installedBreakpoint;
	}

	// delete the watchpoints
	while (InstalledWatchpoint* watchpoint = fWatchpoints.RemoveHead())
		delete watchpoint;

	// delete the hardware breakpoint objects
	while (Breakpoint* breakpoint = fHardwareBreakpoints.RemoveHead())
		delete breakpoint;

	rw_lock_destroy(&fLock);
}


/**
 * @brief Allocate the pool of hardware breakpoint slot objects.
 *
 * Creates DEBUG_MAX_BREAKPOINTS Breakpoint records, each representing an
 * architectural debug register slot. All start unused and are chained on
 * fHardwareBreakpoints in LRU order (least recently installed at the head).
 *
 * @return B_OK on success; B_NO_MEMORY if allocation of any slot fails.
 */
status_t
BreakpointManager::Init()
{
	// create objects for the hardware breakpoints
	for (int32 i = 0; i < DEBUG_MAX_BREAKPOINTS; i++) {
		Breakpoint* breakpoint = new(std::nothrow) Breakpoint;
		if (breakpoint == NULL)
			return B_NO_MEMORY;

		breakpoint->address = 0;
		breakpoint->installedBreakpoint = NULL;
		breakpoint->used = false;
		breakpoint->software = false;

		fHardwareBreakpoints.Add(breakpoint);
	}

	return B_OK;
}


/**
 * @brief Install a breakpoint at the given user-space address.
 *
 * Prefers a hardware breakpoint while slots are available, falling back to a
 * software breakpoint (overwritten instruction bytes saved in softwareData).
 * Enforces kMaxBreakpointCount as a soft cap and rejects duplicate addresses.
 * Takes fLock exclusively; the team's debug_info lock is expected to be held
 * by the caller in user_debugger.cpp.
 *
 * @param _address User virtual address at which to set the breakpoint.
 * @return B_OK on success; B_BUSY if the per-team cap is reached;
 *         B_BAD_VALUE if a breakpoint already exists there; B_NO_MEMORY on
 *         allocation failure; or an architectural error from
 *         arch_set_breakpoint().
 */
status_t
BreakpointManager::InstallBreakpoint(void* _address)
{
	const addr_t address = (addr_t)_address;

	WriteLocker locker(fLock);

	if (fBreakpointCount >= kMaxBreakpointCount)
		return B_BUSY;

	// check whether there's already a breakpoint at the address
	InstalledBreakpoint* installed = fBreakpoints.Lookup(address);
	if (installed != NULL)
		return B_BAD_VALUE;

	// create the breakpoint object
	installed = new(std::nothrow) InstalledBreakpoint(address);
	if (installed == NULL)
		return B_NO_MEMORY;
	ObjectDeleter<InstalledBreakpoint> installedDeleter(installed);

	// If we still have enough hardware breakpoints left, install a hardware
	// breakpoint.
	Breakpoint* breakpoint = _GetUnusedHardwareBreakpoint(false);
	if (breakpoint != NULL) {
		status_t error = _InstallHardwareBreakpoint(breakpoint, address);
		if (error != B_OK)
			return error;

		breakpoint->installedBreakpoint = installed;
		installed->breakpoint = breakpoint;
	} else {
		// install a software breakpoint
		status_t error = _InstallSoftwareBreakpoint(installed, address);
		if (error != B_OK)
			return error;
	}

	fBreakpoints.Insert(installed);
	installedDeleter.Detach();
	fBreakpointCount++;

	return B_OK;
}


/**
 * @brief Remove a previously installed breakpoint.
 *
 * Looks up the breakpoint by address, restores the original instruction bytes
 * (software case) or clears the architectural slot (hardware case), and frees
 * the record. Takes fLock exclusively.
 *
 * @param _address User virtual address of the breakpoint to remove.
 * @return B_OK on success; B_BAD_VALUE if no breakpoint exists at the address.
 */
status_t
BreakpointManager::UninstallBreakpoint(void* _address)
{
	const addr_t address = (addr_t)_address;

	WriteLocker locker(fLock);

	InstalledBreakpoint* installed = fBreakpoints.Lookup(address);
	if (installed == NULL)
		return B_BAD_VALUE;

	if (installed->breakpoint->software)
		_UninstallSoftwareBreakpoint(installed->breakpoint);
	else
		_UninstallHardwareBreakpoint(installed->breakpoint);

	fBreakpoints.Remove(installed);
	delete installed;
	fBreakpointCount--;

	return B_OK;
}


/**
 * @brief Install a hardware watchpoint at the given user-space address.
 *
 * Allocates a slot (possibly stealing a hardware breakpoint slot when
 * DEBUG_SHARED_BREAK_AND_WATCHPOINTS is set) and programs the architectural
 * watchpoint registers. Takes fLock exclusively.
 *
 * @param _address Address to watch.
 * @param type     Watchpoint type (read/write/access) as defined by the arch layer.
 * @param length   Watch region length in bytes.
 * @return B_OK on success; B_BAD_VALUE if a watchpoint already exists at the
 *         address; B_BUSY if no slot is available; B_NO_MEMORY on allocation
 *         failure; or an architectural error from arch_set_watchpoint().
 */
status_t
BreakpointManager::InstallWatchpoint(void* _address, uint32 type, int32 length)
{
	const addr_t address = (addr_t)_address;

	WriteLocker locker(fLock);

	InstalledWatchpoint* watchpoint = _FindWatchpoint(address);
	if (watchpoint != NULL)
		return B_BAD_VALUE;

#if DEBUG_SHARED_BREAK_AND_WATCHPOINTS
	// We need at least one hardware breakpoint for our breakpoint management.
	if (fWatchpointCount + 1 >= DEBUG_MAX_WATCHPOINTS)
		return B_BUSY;
#else
	if (fWatchpointCount >= DEBUG_MAX_WATCHPOINTS)
		return B_BUSY;
#endif

	watchpoint = new(std::nothrow) InstalledWatchpoint;
	if (watchpoint == NULL)
		return B_NO_MEMORY;
	ObjectDeleter<InstalledWatchpoint> watchpointDeleter(watchpoint);

	status_t error = _InstallWatchpoint(watchpoint, address, type, length);
	if (error != B_OK)
		return error;

	fWatchpoints.Add(watchpointDeleter.Detach());
	fWatchpointCount++;
	return B_OK;
}


/**
 * @brief Remove a previously installed watchpoint.
 *
 * @param address Address of the watchpoint to remove.
 * @return B_OK on success; B_BAD_VALUE if no watchpoint exists at the address;
 *         or an architectural error from arch_clear_watchpoint().
 */
status_t
BreakpointManager::UninstallWatchpoint(void* address)
{
	WriteLocker locker(fLock);

	InstalledWatchpoint* watchpoint = _FindWatchpoint((addr_t)address);
	if (watchpoint == NULL)
		return B_BAD_VALUE;

	ObjectDeleter<InstalledWatchpoint> deleter(watchpoint);
	fWatchpoints.Remove(watchpoint);
	fWatchpointCount--;

	return _UninstallWatchpoint(watchpoint);
}


/**
 * @brief Remove all breakpoints and watchpoints in bulk.
 *
 * Used when the team is being detached from the debugger or when debug state
 * is being reset. Takes fLock exclusively; the team's debug_info lock is
 * expected to be held by the caller.
 */
void
BreakpointManager::RemoveAllBreakpoints()
{
	WriteLocker locker(fLock);

	// remove the breakpoints
	BreakpointTree::Iterator it = fBreakpoints.GetIterator();
	while (InstalledBreakpoint* installedBreakpoint = it.Next()) {
		it.Remove();

		// uninstall underlying hard/software breakpoint
		if (installedBreakpoint->breakpoint->software)
			_UninstallSoftwareBreakpoint(installedBreakpoint->breakpoint);
		else
			_UninstallHardwareBreakpoint(installedBreakpoint->breakpoint);

		delete installedBreakpoint;
	}

	// remove the watchpoints
	while (InstalledWatchpoint* watchpoint = fWatchpoints.RemoveHead()) {
		_UninstallWatchpoint(watchpoint);
		delete watchpoint;
	}
}


/*!	\brief Returns whether the given address can be accessed in principle.
	No check whether there's an actually accessible area is performed, though.
*/
/**
 * @brief Check whether an address is in principle accessible for breakpoint I/O.
 *
 * Does not probe whether a mapped area actually exists, only that the address
 * falls within the portion of the address space the manager is permitted to
 * touch (user space).
 *
 * @param _address Address to test.
 * @param write    True for write-access checks, false for read-access.
 *                 Currently unused but preserved for future protection checks.
 * @return true if the address is in user space; false otherwise.
 */
/*static*/ bool
BreakpointManager::CanAccessAddress(const void* _address, bool write)
{
	const addr_t address = (addr_t)_address;

	// user addresses are always fine
	if (IS_USER_ADDRESS(address))
		return true;

	return false;
}


/*!	\brief Reads data from user memory.

	Tries to read \a size bytes of data from user memory address \a address
	into the supplied buffer \a buffer. If only a part could be read the
	function won't fail. The number of bytes actually read is return through
	\a bytesRead.

	\param address The user memory address from which to read.
	\param buffer The buffer into which to write.
	\param size The number of bytes to read.
	\param bytesRead Will be set to the number of bytes actually read.
	\return \c B_OK, if reading went fine. Then \a bytesRead will be set to
			the amount of data actually read. An error indicates that nothing
			has been read.
*/
/**
 * @brief Read user memory, splicing in bytes hidden by software breakpoints.
 *
 * Performs the raw read via _ReadMemory(), then walks the breakpoint tree for
 * any software breakpoints intersecting [address, address + size) and patches
 * the buffer with the saved original instruction bytes, so the debugger sees
 * the program text rather than the trap instruction. Takes fLock shared.
 *
 * @param _address   User memory address to read from.
 * @param buffer     Destination buffer.
 * @param size       Number of bytes to attempt.
 * @param bytesRead  [out] Bytes actually read (may be less than size).
 * @return B_OK if any bytes were read, otherwise the underlying error.
 */
status_t
BreakpointManager::ReadMemory(const void* _address, void* buffer, size_t size,
	size_t& bytesRead)
{
	const addr_t address = (addr_t)_address;

	ReadLocker locker(fLock);

	status_t error = _ReadMemory(address, buffer, size, bytesRead);
	if (error != B_OK)
		return error;

	// If we have software breakpoints installed, fix the buffer not to contain
	// any of them.

	// address of the first possibly intersecting software breakpoint
	const addr_t startAddress
		= std::max(address, (addr_t)DEBUG_SOFTWARE_BREAKPOINT_SIZE - 1)
			- (DEBUG_SOFTWARE_BREAKPOINT_SIZE - 1);

	BreakpointTree::Iterator it = fBreakpoints.GetIterator(startAddress, true,
		true);
	while (InstalledBreakpoint* installed = it.Next()) {
		Breakpoint* breakpoint = installed->breakpoint;
		if (breakpoint->address >= address + size)
			break;

		if (breakpoint->software) {
			// Software breakpoint intersects -- replace the read data with
			// the data saved in the breakpoint object.
			addr_t minAddress = std::max(breakpoint->address, address);
			size_t toCopy = std::min(address + size,
					breakpoint->address + DEBUG_SOFTWARE_BREAKPOINT_SIZE)
				- minAddress;
			memcpy((uint8*)buffer + (minAddress - address),
				breakpoint->softwareData + (minAddress - breakpoint->address),
				toCopy);
		}
	}

	return B_OK;
}


/**
 * @brief Write user memory while preserving installed software breakpoints.
 *
 * Iterates intersecting software breakpoints, writing normal memory between
 * them via _WriteMemory() and redirecting writes that fall within a breakpoint
 * into the breakpoint's softwareData shadow buffer. The trap instruction
 * itself is left untouched so execution still traps. Takes fLock exclusively.
 *
 * @param _address     Destination user address.
 * @param _buffer      Source buffer.
 * @param size         Number of bytes to attempt.
 * @param bytesWritten [out] Bytes actually written (may be less than size).
 * @return B_OK if any bytes were written, otherwise the underlying error.
 */
status_t
BreakpointManager::WriteMemory(void* _address, const void* _buffer, size_t size,
	size_t& bytesWritten)
{
	bytesWritten = 0;

	if (size == 0)
		return B_OK;

	addr_t address = (addr_t)_address;
	const uint8* buffer = (uint8*)_buffer;

	WriteLocker locker(fLock);

	// We don't want to overwrite software breakpoints, so things are a bit more
	// complicated. We iterate through the intersecting software breakpoints,
	// writing the memory between them normally, but skipping the breakpoints
	// itself. We write into their softwareData instead.

	// Get the first breakpoint -- if it starts before the address, we'll
	// handle it separately to make things in the main loop simpler.
	const addr_t startAddress
		= std::max(address, (addr_t)DEBUG_SOFTWARE_BREAKPOINT_SIZE - 1)
			- (DEBUG_SOFTWARE_BREAKPOINT_SIZE - 1);

	BreakpointTree::Iterator it = fBreakpoints.GetIterator(startAddress, true,
		true);
	InstalledBreakpoint* installed = it.Next();
	while (installed != NULL) {
		Breakpoint* breakpoint = installed->breakpoint;
		if (breakpoint->address >= address)
			break;

		if (breakpoint->software) {
			// We've got a breakpoint that is partially intersecting with the
			// beginning of the address range to write.
			size_t toCopy = std::min(address + size,
					breakpoint->address + DEBUG_SOFTWARE_BREAKPOINT_SIZE)
				- address;
			memcpy(breakpoint->softwareData + (address - breakpoint->address),
				buffer, toCopy);

			address += toCopy;
			size -= toCopy;
			bytesWritten += toCopy;
			buffer += toCopy;
		}

		installed = it.Next();
	}

	// loop through the breakpoints intersecting with the range
	while (installed != NULL) {
		Breakpoint* breakpoint = installed->breakpoint;
		if (breakpoint->address >= address + size)
			break;

		if (breakpoint->software) {
			// write the data up to the breakpoint (if any)
			size_t toCopy = breakpoint->address - address;
			if (toCopy > 0) {
				size_t chunkWritten;
				status_t error = _WriteMemory(address, buffer, toCopy,
					chunkWritten);
				if (error != B_OK)
					return bytesWritten > 0 ? B_OK : error;

				address += chunkWritten;
				size -= chunkWritten;
				bytesWritten += chunkWritten;
				buffer += chunkWritten;

				if (chunkWritten < toCopy)
					return B_OK;
			}

			// write to the breakpoint data
			toCopy = std::min(size, (size_t)DEBUG_SOFTWARE_BREAKPOINT_SIZE);
			memcpy(breakpoint->softwareData, buffer, toCopy);

			address += toCopy;
			size -= toCopy;
			bytesWritten += toCopy;
			buffer += toCopy;
		}

		installed = it.Next();
	}

	// write remaining data
	if (size > 0) {
		size_t chunkWritten;
		status_t error = _WriteMemory(address, buffer, size, chunkWritten);
		if (error != B_OK)
			return bytesWritten > 0 ? B_OK : error;

		bytesWritten += chunkWritten;
	}

	return B_OK;
}


/**
 * @brief Prepare a thread to resume execution past a software breakpoint.
 *
 * If the breakpoint at @p _address is a software breakpoint, the original
 * instruction bytes have been overwritten with a trap, so the thread cannot
 * simply step. This swaps in a hardware breakpoint (forcing one free if
 * necessary), restores the original bytes, and leaves the installed record
 * bound to the new hardware slot so the breakpoint still fires next time.
 * Takes fLock exclusively.
 *
 * @param _address Continuation (PC) address to examine.
 */
void
BreakpointManager::PrepareToContinue(void* _address)
{
	const addr_t address = (addr_t)_address;

	WriteLocker locker(fLock);

	// Check whether there's a software breakpoint at the continuation address.
	InstalledBreakpoint* installed = fBreakpoints.Lookup(address);
	if (installed == NULL || !installed->breakpoint->software)
		return;

	// We need to replace the software breakpoint by a hardware one, or
	// we can't continue the thread.
	Breakpoint* breakpoint = _GetUnusedHardwareBreakpoint(true);
	if (breakpoint == NULL) {
		dprintf("Failed to allocate a hardware breakpoint.\n");
		return;
	}

	status_t error = _InstallHardwareBreakpoint(breakpoint, address);
	if (error != B_OK)
		return;

	_UninstallSoftwareBreakpoint(installed->breakpoint);

	breakpoint->installedBreakpoint = installed;
	installed->breakpoint = breakpoint;
}


/**
 * @brief Pick a free hardware breakpoint slot, optionally forcing one free.
 *
 * First tries the list for a slot whose 'used' flag is clear. When @p force
 * is true and no slot is free, evicts an in-use slot by converting its
 * InstalledBreakpoint over to a software breakpoint and reclaiming the slot.
 *
 * @param force If true, steal a slot from an existing hardware breakpoint
 *              when none are free.
 * @return A usable Breakpoint slot, or NULL if none could be obtained.
 *         Caller must hold fLock exclusively.
 */
BreakpointManager::Breakpoint*
BreakpointManager::_GetUnusedHardwareBreakpoint(bool force)
{
	// try to find a free one first
	for (BreakpointList::Iterator it = fHardwareBreakpoints.GetIterator();
			Breakpoint* breakpoint = it.Next();) {
		if (!breakpoint->used)
			return breakpoint;
	}

	if (!force)
		return NULL;

	// replace one by a software breakpoint
	for (BreakpointList::Iterator it = fHardwareBreakpoints.GetIterator();
			Breakpoint* breakpoint = it.Next();) {
		if (breakpoint->installedBreakpoint == NULL)
			continue;

		status_t error = _InstallSoftwareBreakpoint(
			breakpoint->installedBreakpoint, breakpoint->address);
		if (error != B_OK)
			continue;

		if (_UninstallHardwareBreakpoint(breakpoint) == B_OK)
			return breakpoint;
	}

	return NULL;
}


/**
 * @brief Install a software breakpoint backing for an InstalledBreakpoint.
 *
 * Allocates a software Breakpoint record, reads DEBUG_SOFTWARE_BREAKPOINT_SIZE
 * bytes of the original instruction stream into softwareData, then writes the
 * architectural trap sequence in their place. Rolls back the partial write if
 * only some of the bytes could be deposited. Caller must hold fLock exclusively.
 *
 * @param installed InstalledBreakpoint the new software breakpoint will be
 *                  bound to on success.
 * @param address   User address at which to patch in the trap.
 * @return B_OK on success; B_NO_MEMORY on allocation failure; B_BAD_ADDRESS
 *         if the target memory could not be fully read or written.
 */
status_t
BreakpointManager::_InstallSoftwareBreakpoint(InstalledBreakpoint* installed,
	addr_t address)
{
	Breakpoint* breakpoint = new(std::nothrow) Breakpoint;
	if (breakpoint == NULL)
		return B_NO_MEMORY;
	ObjectDeleter<Breakpoint> breakpointDeleter(breakpoint);

	breakpoint->address = address;
	breakpoint->installedBreakpoint = installed;
	breakpoint->used = true;
	breakpoint->software = true;

	// save the memory where the software breakpoint shall be installed
	size_t bytesTransferred;
	status_t error = _ReadMemory(address, breakpoint->softwareData,
		DEBUG_SOFTWARE_BREAKPOINT_SIZE, bytesTransferred);
	if (error != B_OK)
		return error;
	if (bytesTransferred != DEBUG_SOFTWARE_BREAKPOINT_SIZE)
		return B_BAD_ADDRESS;

	// write the breakpoint code
	error = _WriteMemory(address, DEBUG_SOFTWARE_BREAKPOINT,
		DEBUG_SOFTWARE_BREAKPOINT_SIZE, bytesTransferred);
	if (error != B_OK)
		return error;

	if (bytesTransferred < DEBUG_SOFTWARE_BREAKPOINT_SIZE) {
		// breakpoint written partially only -- undo the written part
		if (bytesTransferred > 0) {
			size_t dummy;
			_WriteMemory(address, breakpoint->softwareData, bytesTransferred,
				dummy);
		}
		return B_BAD_ADDRESS;
	}

	installed->breakpoint = breakpoint;
	breakpointDeleter.Detach();

	TRACE("installed software breakpoint at %#lx\n", address);

	return B_OK;
}


/**
 * @brief Restore the original instruction bytes over a software breakpoint.
 *
 * Writes softwareData back to the target address (best-effort; failure is
 * ignored, since the team may already be tearing down) and frees the record.
 * Caller must hold fLock exclusively.
 *
 * @param breakpoint Software breakpoint to tear down and delete.
 * @return Always B_OK.
 */
status_t
BreakpointManager::_UninstallSoftwareBreakpoint(Breakpoint* breakpoint)
{
	size_t bytesWritten;
	_WriteMemory(breakpoint->address, breakpoint->softwareData,
		DEBUG_SOFTWARE_BREAKPOINT_SIZE, bytesWritten);

	TRACE("uninstalled software breakpoint at %#lx\n", breakpoint->address);

	delete breakpoint;
	return B_OK;
}


/**
 * @brief Program an architectural debug slot and bind a Breakpoint to it.
 *
 * On success the slot is moved to the tail of fHardwareBreakpoints so that
 * future allocations prefer colder slots (LRU ordering). Caller must hold
 * fLock exclusively.
 *
 * @param breakpoint Slot record to mark as in-use at @p address.
 * @param address    User address to break on.
 * @return B_OK on success; otherwise the error from arch_set_breakpoint().
 */
status_t
BreakpointManager::_InstallHardwareBreakpoint(Breakpoint* breakpoint,
	addr_t address)
{
	status_t error = arch_set_breakpoint((void*)address);
	if (error != B_OK)
		return error;

	// move to the tail of the list
	fHardwareBreakpoints.Remove(breakpoint);
	fHardwareBreakpoints.Add(breakpoint);

	TRACE("installed hardware breakpoint at %#lx\n", address);

	breakpoint->address = address;
	breakpoint->used = true;
	return B_OK;
}


/**
 * @brief Clear an architectural debug slot and mark it unused.
 *
 * Does not delete the Breakpoint record itself (the slot pool is preallocated
 * in Init()); only marks it as free and detaches it from any InstalledBreakpoint.
 * Caller must hold fLock exclusively.
 *
 * @param breakpoint Hardware slot record to release.
 * @return B_OK on success; otherwise the error from arch_clear_breakpoint().
 */
status_t
BreakpointManager::_UninstallHardwareBreakpoint(Breakpoint* breakpoint)
{
	status_t error = arch_clear_breakpoint((void*)breakpoint->address);
	if (error != B_OK)
		return error;

	TRACE("uninstalled hardware breakpoint at %#lx\n", breakpoint->address);

	breakpoint->used = false;
	breakpoint->installedBreakpoint = NULL;
	return B_OK;
}


/**
 * @brief Linear search for a watchpoint matching an exact address.
 *
 * @param address User address to look up.
 * @return The matching InstalledWatchpoint, or NULL if not present.
 *         Caller must hold fLock.
 */
BreakpointManager::InstalledWatchpoint*
BreakpointManager::_FindWatchpoint(addr_t address) const
{
	for (InstalledWatchpointList::ConstIterator it = fWatchpoints.GetIterator();
		InstalledWatchpoint* watchpoint = it.Next();) {
		if (address == watchpoint->address)
			return watchpoint;
	}

	return NULL;
}


/**
 * @brief Program the architectural watchpoint registers.
 *
 * When DEBUG_SHARED_BREAK_AND_WATCHPOINTS is set, a hardware breakpoint slot
 * is also consumed (and marked used) to represent the watchpoint. Caller must
 * hold fLock exclusively.
 *
 * @param watchpoint Watchpoint record to populate on success.
 * @param address    Address to watch.
 * @param type       Watchpoint type (read/write/access).
 * @param length     Watched region length in bytes.
 * @return B_OK on success; B_BUSY if no hardware slot can be borrowed;
 *         otherwise the error from arch_set_watchpoint().
 */
status_t
BreakpointManager::_InstallWatchpoint(InstalledWatchpoint* watchpoint,
	addr_t address, uint32 type, int32 length)
{
#if DEBUG_SHARED_BREAK_AND_WATCHPOINTS
	// We need a hardware breakpoint.
	watchpoint->breakpoint = _GetUnusedHardwareBreakpoint(true);
	if (watchpoint->breakpoint == NULL) {
		dprintf("Failed to allocate a hardware breakpoint for watchpoint.\n");
		return B_BUSY;
	}
#endif

	status_t error = arch_set_watchpoint((void*)address, type, length);
	if (error != B_OK)
		return error;

	watchpoint->address = address;

#if DEBUG_SHARED_BREAK_AND_WATCHPOINTS
	watchpoint->breakpoint->used = true;
#endif

	return B_OK;
}


/**
 * @brief Clear architectural watchpoint registers and release any shared slot.
 *
 * @param watchpoint Watchpoint to tear down.
 * @return The result of arch_clear_watchpoint().
 *         Caller must hold fLock exclusively.
 */
status_t
BreakpointManager::_UninstallWatchpoint(InstalledWatchpoint* watchpoint)
{
#if DEBUG_SHARED_BREAK_AND_WATCHPOINTS
	watchpoint->breakpoint->used = false;
#endif

	return arch_clear_watchpoint((void*)watchpoint->address);
}


/**
 * @brief Raw user-memory read, broken into page-sized chunks.
 *
 * Validates the address range, then copies via user_memcpy() one page at a
 * time so a fault aborts only the current chunk. Partial reads are reported
 * as success with a smaller @p bytesRead.
 *
 * @param _address  Source user address.
 * @param _buffer   Destination kernel buffer.
 * @param size      Bytes requested.
 * @param bytesRead [out] Bytes actually copied.
 * @return B_OK if any bytes were read; B_BAD_ADDRESS or B_BAD_VALUE otherwise.
 */
status_t
BreakpointManager::_ReadMemory(const addr_t _address, void* _buffer,
	size_t size, size_t& bytesRead)
{
	const uint8* address = (const uint8*)_address;
	uint8* buffer = (uint8*)_buffer;

	// check the parameters
	if (!CanAccessAddress(address, false))
		return B_BAD_ADDRESS;
	if (size <= 0)
		return B_BAD_VALUE;

	// If the region to be read crosses page boundaries, we split it up into
	// smaller chunks.
	status_t error = B_OK;
	bytesRead = 0;
	while (size > 0) {
		// check whether we're still in user address space
		if (!CanAccessAddress(address, false)) {
			error = B_BAD_ADDRESS;
			break;
		}

		// don't cross page boundaries in a single read
		int32 toRead = size;
		int32 maxRead = B_PAGE_SIZE - (addr_t)address % B_PAGE_SIZE;
		if (toRead > maxRead)
			toRead = maxRead;

		error = user_memcpy(buffer, address, toRead);
		if (error != B_OK)
			break;

		bytesRead += toRead;
		address += toRead;
		buffer += toRead;
		size -= toRead;
	}

	// If reading fails, we only fail, if we haven't read anything yet.
	if (error != B_OK) {
		if (bytesRead > 0)
			return B_OK;
		return error;
	}

	return B_OK;
}


/**
 * @brief Raw user-memory write, temporarily unlocking read-only areas.
 *
 * For each target area, if the area is not already writable, its protection
 * is elevated to B_WRITE_AREA for the duration of the copy and then restored.
 * This allows debuggers to patch instruction text in otherwise read-only
 * regions. Partial writes are reported as success.
 *
 * @param _address     Destination user address.
 * @param _buffer      Source buffer.
 * @param size         Bytes requested.
 * @param bytesWritten [out] Bytes actually copied.
 * @return B_OK if any bytes were written; B_BAD_ADDRESS, B_BAD_VALUE, or an
 *         area-management error otherwise.
 */
status_t
BreakpointManager::_WriteMemory(addr_t _address, const void* _buffer,
	size_t size, size_t& bytesWritten)
{
	uint8* address = (uint8*)_address;
	const uint8* buffer = (const uint8*)_buffer;

	// check the parameters
	if (!CanAccessAddress(address, true))
		return B_BAD_ADDRESS;
	if (size <= 0)
		return B_BAD_VALUE;

	// If the region to be written crosses area boundaries, we split it up into
	// smaller chunks.
	status_t error = B_OK;
	bytesWritten = 0;
	while (size > 0) {
		// check whether we're still in user address space
		if (!CanAccessAddress(address, true)) {
			error = B_BAD_ADDRESS;
			break;
		}

		// get the area for the address (we need to use _user_area_for(), since
		// we're looking for a user area)
		area_id area = _user_area_for(address);
		if (area < 0) {
			TRACE("BreakpointManager::_WriteMemory(): area not found for "
				"address: %p: %lx\n", address, area);
			error = area;
			break;
		}

		area_info areaInfo;
		status_t error = get_area_info(area, &areaInfo);
		if (error != B_OK) {
			TRACE("BreakpointManager::_WriteMemory(): failed to get info for "
				"area %ld: %lx\n", area, error);
			error = B_BAD_ADDRESS;
			break;
		}

		// restrict this round of writing to the found area
		int32 toWrite = size;
		int32 maxWrite = (uint8*)areaInfo.address + areaInfo.size - address;
		if (toWrite > maxWrite)
			toWrite = maxWrite;

		// if the area is read-only, we temporarily need to make it writable
		bool protectionChanged = false;
		if (!(areaInfo.protection & (B_WRITE_AREA | B_KERNEL_WRITE_AREA))) {
			error = set_area_protection(area,
				areaInfo.protection | B_WRITE_AREA);
			if (error != B_OK) {
				TRACE("BreakpointManager::_WriteMemory(): failed to set new "
					"protection for area %ld: %lx\n", area, error);
				break;
			}
			protectionChanged = true;
		}

		// copy the memory
		error = user_memcpy(address, buffer, toWrite);

		// reset the area protection
		if (protectionChanged)
			set_area_protection(area, areaInfo.protection);

		if (error != B_OK) {
			TRACE("BreakpointManager::_WriteMemory(): user_memcpy() failed: "
				"%lx\n", error);
			break;
		}

		bytesWritten += toWrite;
		address += toWrite;
		buffer += toWrite;
		size -= toWrite;
	}

	// If writing fails, we only fail, if we haven't written anything yet.
	if (error != B_OK) {
		if (bytesWritten > 0)
			return B_OK;
		return error;
	}

	return B_OK;
}
