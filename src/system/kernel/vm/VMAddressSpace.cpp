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
 *   Copyright 2009-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2002-2009, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file VMAddressSpace.cpp
 * @brief Virtual memory address space management for kernel and user processes.
 *
 * VMAddressSpace represents the address space of a team (process). It maintains
 * the collection of VMArea objects mapped into the space, handles creation and
 * deletion of kernel/user address spaces, and provides the global address space
 * registry.
 *
 * @see VMKernelAddressSpace, VMUserAddressSpace, VMArea
 */

#include <vm/VMAddressSpace.h>

#include <stdlib.h>

#include <new>

#include <KernelExport.h>

#include <util/OpenHashTable.h>

#include <heap.h>
#include <thread.h>
#include <vm/vm.h>
#include <vm/VMArea.h>
#include <vm/VMCache.h>

#include "VMKernelAddressSpace.h"
#include "VMUserAddressSpace.h"


//#define TRACE_VM
#ifdef TRACE_VM
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


#define ASPACE_HASH_TABLE_SIZE 1024


// #pragma mark - AddressSpaceHashDefinition


namespace {

struct AddressSpaceHashDefinition {
	typedef team_id			KeyType;
	typedef VMAddressSpace	ValueType;

	size_t HashKey(team_id key) const
	{
		return key;
	}

	size_t Hash(const VMAddressSpace* value) const
	{
		return HashKey(value->ID());
	}

	bool Compare(team_id key, const VMAddressSpace* value) const
	{
		return value->ID() == key;
	}

	VMAddressSpace*& GetLink(VMAddressSpace* value) const
	{
		return value->HashTableLink();
	}
};

typedef BOpenHashTable<AddressSpaceHashDefinition> AddressSpaceTable;

} // namespace


static AddressSpaceTable	sAddressSpaceTable;
static rw_lock				sAddressSpaceTableLock;

VMAddressSpace* VMAddressSpace::sKernelAddressSpace;


// #pragma mark - VMAddressSpace


/**
 * @brief Constructs an address space with the given identity and virtual range.
 *
 * Initialises the base address, end address, free-space counter, team ID,
 * reference count (1), fault and change counters, translation map pointer,
 * randomisation flag, and deletion flag. Also initialises the reader/writer
 * lock named @p name.
 *
 * @param id    Team ID that owns this address space.
 * @param base  Lowest valid virtual address in the space.
 * @param size  Total size in bytes of the virtual address range.
 * @param name  Human-readable name used for the rw_lock.
 */
VMAddressSpace::VMAddressSpace(team_id id, addr_t base, size_t size,
	const char* name)
	:
	fBase(base),
	fEndAddress(base + (size - 1)),
	fFreeSpace(size),
	fID(id),
	fRefCount(1),
	fFaultCount(0),
	fChangeCount(0),
	fTranslationMap(NULL),
	fRandomizingEnabled(true),
	fDeleting(false)
{
	rw_lock_init(&fLock, name);
}


/**
 * @brief Destructor; acquires the write lock, deletes the translation map,
 *        and destroys the rw_lock.
 *
 * The write lock is acquired to synchronise with any last readers, then
 * released by rw_lock_destroy().
 */
VMAddressSpace::~VMAddressSpace()
{
	TRACE(("VMAddressSpace::~VMAddressSpace: called on aspace %" B_PRId32 "\n",
		ID()));

	WriteLock();

	delete fTranslationMap;

	rw_lock_destroy(&fLock);
}


/**
 * @brief Initialises the global address-space subsystem.
 *
 * Creates the hash table for all address spaces, allocates the initial kernel
 * address space via Create(), and registers the @c aspaces and @c aspace
 * kernel debugger commands.
 *
 * @return B_OK on success; panics and does not return on failure.
 * @retval B_OK  Subsystem initialised successfully.
 */
/*static*/ status_t
VMAddressSpace::Init()
{
	rw_lock_init(&sAddressSpaceTableLock, "address spaces table");

	// create the area and address space hash tables
	{
		new(&sAddressSpaceTable) AddressSpaceTable;
		status_t error = sAddressSpaceTable.Init(ASPACE_HASH_TABLE_SIZE);
		if (error != B_OK)
			panic("vm_init: error creating aspace hash table\n");
	}

	// create the initial kernel address space
	if (Create(B_SYSTEM_TEAM, KERNEL_BASE, KERNEL_SIZE, true,
			&sKernelAddressSpace) != B_OK) {
		panic("vm_init: error creating kernel address space!\n");
	}

	add_debugger_command("aspaces", &_DumpListCommand,
		"Dump a list of all address spaces");
	add_debugger_command("aspace", &_DumpCommand,
		"Dump info about a particular address space");

	return B_OK;
}


/**
 * @brief Marks the address space as deleting, removes all areas, and releases
 *        the last reference.
 *
 * Sets fDeleting under the write lock so that new area creation is rejected,
 * then calls vm_delete_areas() to tear down every VMArea, and finally Put()
 * to drop the caller's reference (which may trigger destruction).
 */
void
VMAddressSpace::RemoveAndPut()
{
	WriteLock();
	fDeleting = true;
	WriteUnlock();

	vm_delete_areas(this, true);
	Put();
}


/**
 * @brief Post-construction initialisation hook for subclasses.
 *
 * The base implementation is a no-op. Subclasses override this to perform
 * allocations that may fail, returning an error code rather than throwing.
 *
 * @return Always B_OK in the base class.
 * @retval B_OK  No additional initialisation required.
 */
status_t
VMAddressSpace::InitObject()
{
	return B_OK;
}


/**
 * @brief Prints a summary of this address space to the kernel debugger console.
 *
 * Outputs the pointer, team ID, reference count, fault count, translation map
 * pointer, base address, end address, and change count.
 */
void
VMAddressSpace::Dump() const
{
	kprintf("dump of address space at %p:\n", this);
	kprintf("id: %" B_PRId32 "\n", fID);
	kprintf("ref_count: %" B_PRId32 "\n", fRefCount);
	kprintf("fault_count: %" B_PRId32 "\n", fFaultCount);
	kprintf("translation_map: %p\n", fTranslationMap);
	kprintf("base: %#" B_PRIxADDR "\n", fBase);
	kprintf("end: %#" B_PRIxADDR "\n", fEndAddress);
	kprintf("change_count: %" B_PRId32 "\n", fChangeCount);
}


/**
 * @brief Allocates and fully initialises a new address space.
 *
 * Chooses VMKernelAddressSpace or VMUserAddressSpace depending on @p kernel,
 * calls InitObject(), creates the architecture translation map, and inserts
 * the new space into the global hash table.
 *
 * @param teamID         Team ID to assign to the new address space.
 * @param base           Lowest virtual address in the range.
 * @param size           Size in bytes of the virtual address range.
 * @param kernel         If @c true, create a kernel address space;
 *                       otherwise create a user address space.
 * @param _addressSpace  Output: set to the newly created VMAddressSpace on
 *                       success; unchanged on failure.
 * @return Status code.
 * @retval B_OK        Address space created and registered successfully.
 * @retval B_NO_MEMORY Allocation of the address space object failed.
 */
/*static*/ status_t
VMAddressSpace::Create(team_id teamID, addr_t base, size_t size, bool kernel,
	VMAddressSpace** _addressSpace)
{
	VMAddressSpace* addressSpace = kernel
		? (VMAddressSpace*)new(std::nothrow) VMKernelAddressSpace(teamID, base,
			size)
		: (VMAddressSpace*)new(std::nothrow) VMUserAddressSpace(teamID, base,
			size);
	if (addressSpace == NULL)
		return B_NO_MEMORY;

	status_t status = addressSpace->InitObject();
	if (status != B_OK) {
		delete addressSpace;
		return status;
	}

	TRACE(("VMAddressSpace::Create(): team %" B_PRId32 " (%skernel): %#lx "
		"bytes starting at %#lx => %p\n", teamID, kernel ? "" : "!", size,
		base, addressSpace));

	// create the corresponding translation map
	status = arch_vm_translation_map_create_map(kernel,
		&addressSpace->fTranslationMap);
	if (status != B_OK) {
		delete addressSpace;
		return status;
	}

	// add the aspace to the global hash table
	rw_lock_write_lock(&sAddressSpaceTableLock);
	sAddressSpaceTable.InsertUnchecked(addressSpace);
	rw_lock_write_unlock(&sAddressSpaceTableLock);

	*_addressSpace = addressSpace;
	return B_OK;
}


/**
 * @brief Returns the kernel address space with an incremented reference count.
 *
 * The kernel address space is never deleted, so no hash lookup is required.
 *
 * @return Pointer to the kernel VMAddressSpace (never NULL).
 */
/*static*/ VMAddressSpace*
VMAddressSpace::GetKernel()
{
	// we can treat this one a little differently since it can't be deleted
	sKernelAddressSpace->Get();
	return sKernelAddressSpace;
}


/**
 * @brief Returns the team ID of the currently executing thread's address space.
 *
 * @return The team ID of the current thread, or B_ERROR if there is no current
 *         thread or it has no address space.
 * @retval B_ERROR  No current thread or no address space is associated.
 */
/*static*/ team_id
VMAddressSpace::CurrentID()
{
	Thread* thread = thread_get_current_thread();

	if (thread != NULL && thread->team->address_space != NULL)
		return thread->team->id;

	return B_ERROR;
}


/**
 * @brief Returns the address space of the currently executing thread with an
 *        incremented reference count.
 *
 * @return Pointer to the current VMAddressSpace, or NULL if the current thread
 *         has no address space.
 */
/*static*/ VMAddressSpace*
VMAddressSpace::GetCurrent()
{
	Thread* thread = thread_get_current_thread();

	if (thread != NULL) {
		VMAddressSpace* addressSpace = thread->team->address_space;
		if (addressSpace != NULL) {
			addressSpace->Get();
			return addressSpace;
		}
	}

	return NULL;
}


/**
 * @brief Looks up the address space for @p teamID and returns it with an
 *        incremented reference count.
 *
 * Acquires the table read lock, performs the lookup, increments the reference
 * count while the lock is still held, then releases the lock.
 *
 * @param teamID  Team ID to look up.
 * @return Pointer to the VMAddressSpace, or NULL if not found.
 */
/*static*/ VMAddressSpace*
VMAddressSpace::Get(team_id teamID)
{
	rw_lock_read_lock(&sAddressSpaceTableLock);
	VMAddressSpace* addressSpace = sAddressSpaceTable.Lookup(teamID);
	if (addressSpace)
		addressSpace->Get();
	rw_lock_read_unlock(&sAddressSpaceTableLock);

	return addressSpace;
}


/**
 * @brief Returns the first address space in the global table (debugger use only).
 *
 * @return Pointer to the first VMAddressSpace, or NULL if the table is empty.
 */
/*static*/ VMAddressSpace*
VMAddressSpace::DebugFirst()
{
	return sAddressSpaceTable.GetIterator().Next();
}


/**
 * @brief Returns the address space that follows @p addressSpace in the global
 *        table (debugger use only).
 *
 * @param addressSpace  Current position; passing NULL returns NULL immediately.
 * @return Pointer to the next VMAddressSpace, or NULL if there is none.
 */
/*static*/ VMAddressSpace*
VMAddressSpace::DebugNext(VMAddressSpace* addressSpace)
{
	if (addressSpace == NULL)
		return NULL;

	AddressSpaceTable::Iterator it
		= sAddressSpaceTable.GetIterator(addressSpace->ID());
	it.Next();
	return it.Next();
}


/**
 * @brief Looks up an address space by team ID without reference counting
 *        (debugger use only).
 *
 * @param teamID  Team ID to look up.
 * @return Pointer to the VMAddressSpace, or NULL if not found.
 */
/*static*/ VMAddressSpace*
VMAddressSpace::DebugGet(team_id teamID)
{
	return sAddressSpaceTable.Lookup(teamID);
}


/**
 * @brief Removes and deletes the address space for @p id if its reference count
 *        has dropped to zero.
 *
 * Acquires the table write lock to check and optionally remove the entry, then
 * deletes the object outside the lock if it was removed.
 *
 * @param id  Team ID of the address space to potentially delete.
 */
/*static*/ void
VMAddressSpace::_DeleteIfUnreferenced(team_id id)
{
	rw_lock_write_lock(&sAddressSpaceTableLock);

	bool remove = false;
	VMAddressSpace* addressSpace = sAddressSpaceTable.Lookup(id);
	if (addressSpace != NULL && addressSpace->fRefCount == 0) {
		sAddressSpaceTable.RemoveUnchecked(addressSpace);
		remove = true;
	}

	rw_lock_write_unlock(&sAddressSpaceTableLock);

	if (remove)
		delete addressSpace;
}


/**
 * @brief Kernel debugger command handler: dumps a single address space by ID.
 *
 * Parses argv[1] as an integer team ID, looks up the corresponding address
 * space, and calls Dump() on it.
 *
 * @param argc  Must be at least 2.
 * @param argv  argv[1] is the team ID (decimal or hex).
 * @return Always 0.
 */
/*static*/ int
VMAddressSpace::_DumpCommand(int argc, char** argv)
{
	VMAddressSpace* aspace;

	if (argc < 2) {
		kprintf("aspace: not enough arguments\n");
		return 0;
	}

	// if the argument looks like a number, treat it as such

	{
		team_id id = strtoul(argv[1], NULL, 0);

		aspace = sAddressSpaceTable.Lookup(id);
		if (aspace == NULL) {
			kprintf("invalid aspace id\n");
		} else {
			aspace->Dump();
		}
	}

	return 0;
}


/**
 * @brief Kernel debugger command handler: lists all registered address spaces.
 *
 * Iterates the global hash table and prints a one-line summary for each entry
 * including its pointer, team ID, base, end address, area count, and total
 * mapped area size.
 *
 * @param argc  Unused.
 * @param argv  Unused.
 * @return Always 0.
 */
/*static*/ int
VMAddressSpace::_DumpListCommand(int argc, char** argv)
{
	kprintf("  %*s      id     %*s     %*s   area count    area size\n",
		B_PRINTF_POINTER_WIDTH, "address", B_PRINTF_POINTER_WIDTH, "base",
		B_PRINTF_POINTER_WIDTH, "end");

	AddressSpaceTable::Iterator it = sAddressSpaceTable.GetIterator();
	while (VMAddressSpace* space = it.Next()) {
		int32 areaCount = 0;
		off_t areaSize = 0;
		for (VMAddressSpace::AreaIterator areaIt = space->GetAreaIterator();
				VMArea* area = areaIt.Next();) {
			areaCount++;
			areaSize += area->Size();
		}
		kprintf("%p  %6" B_PRId32 "   %#010" B_PRIxADDR "   %#10" B_PRIxADDR
			"   %10" B_PRId32 "   %10" B_PRIdOFF "\n", space, space->ID(),
			space->Base(), space->EndAddress(), areaCount, areaSize);
	}

	return 0;
}
