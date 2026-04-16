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
 *   Copyright 2008, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file debug_paranoia.cpp
 * @brief Sanity-check framework enabled under ENABLE_PARANOIA_CHECKS.
 *
 * Maintains a set of named "check sets" keyed by an object pointer; each set
 * owns a list of ParanoiaCheck records that remember the CRC-32 of a memory
 * region at the time it was last considered valid. Kernel code marks regions
 * it expects to be stable (e.g. free-list heads, object magic fields, ref
 * counts) via set_paranoia_check(); run_paranoia_checks() later recomputes
 * and compares CRCs, panicking on mismatch. Memory for both check sets and
 * checks is drawn from a fixed-size static slot pool to avoid recursive
 * allocator interactions. Optional trace entries record create/update/remove
 * operations when PARANOIA_TRACING is on.
 */

#include <debug_paranoia.h>

#include <sys/param.h>

#include <new>

#include <OS.h>

#include <tracing.h>
#include <util/AutoLock.h>


#if ENABLE_PARANOIA_CHECKS


// #pragma mark - CRC-32


static const uint32 kCRC32Polynom = 0x04c11db7;
static uint32 sCRC32Table[256];


/**
 * @brief Bit-reverse the low @a bits of @a value.
 *
 * Helper used while pre-computing the CRC-32 lookup table. CRC-32 as used in
 * zlib/PKZIP processes bits LSB-first, but our polynomial multiplication is
 * MSB-first, so inputs and outputs get reflected to match the conventional
 * wire-format result.
 *
 * @param value Source value to reflect.
 * @param bits  Number of low-order bits to consider.
 * @return Bit-reversed value.
 */
static uint32
crc32_reflect(uint32 value, int32 bits)
{
	uint32 result = 0;
	for (int32 i = 1; i <= bits; i++) {
		if (value & 1)
			result |= 1 << (bits - i);
		value >>= 1;
	}

	return result;
}


/**
 * @brief Build the 256-entry CRC-32 lookup table.
 *
 * Populates sCRC32Table using the standard reflected IEEE 802.3 polynomial
 * (kCRC32Polynom). Called once from debug_paranoia_init().
 */
static void
init_crc32_table()
{
	for (int32 i = 0; i < 256; i++) {
		sCRC32Table[i] = crc32_reflect(i, 8) << 24;
		for (int32 k = 0; k < 8; k++) {
			sCRC32Table[i] = (sCRC32Table[i] << 1)
				^ (sCRC32Table[i] & (1 << 31) ? kCRC32Polynom : 0);
		}
		sCRC32Table[i] = crc32_reflect(sCRC32Table[i], 32);
	}
}


/**
 * @brief Compute the CRC-32 checksum of a byte range.
 *
 * Uses the precomputed sCRC32Table. Initial value is 0xFFFFFFFF; no final
 * XOR/inversion is applied because the return value is compared byte-for-byte
 * with a previously captured checksum rather than published externally.
 *
 * @param _data Pointer to the bytes to checksum.
 * @param size  Number of bytes to hash.
 * @return 32-bit CRC.
 */
static uint32
crc32(const void* _data, size_t size)
{
	uint8* data = (uint8*)_data;
	uint32 crc = 0xffffffff;

	while (size-- > 0) {
		crc = (crc >> 8) ^ sCRC32Table[(crc & 0xff) ^ *data];
		data++;
	}

	return crc;
}


// #pragma mark - ParanoiaCheck[Set]


class ParanoiaCheckSet;

class ParanoiaCheck {
public:
	ParanoiaCheck(const void* address, size_t size)
		:
		fAddress(address),
		fSize(size)
	{
		Update();
	}

	const void*		Address() const	{ return fAddress; }
	size_t			Size() const	{ return fSize; }

	/**
	 * @brief Snapshot the current CRC-32 of the guarded memory region.
	 *
	 * Called when the caller has legitimately mutated the region and wants the
	 * new state to become the new "expected" value for later Check() calls.
	 */
	void Update()
	{
		fCheckSum = crc32(fAddress, fSize);
	}

	/**
	 * @brief Verify that the guarded region still matches the saved CRC.
	 *
	 * @return true if unchanged since the last Update(); false if corrupted.
	 */
	bool Check() const
	{
		return crc32(fAddress, fSize) == fCheckSum;
	}

private:
	const void*		fAddress;
	size_t			fSize;
	uint32			fCheckSum;
	ParanoiaCheck*	fNext;

	friend class ParanoiaCheckSet;
};


class ParanoiaCheckSet {
public:
	ParanoiaCheckSet(const void* object, const char* description)
		:
		fObject(object),
		fDescription(description),
		fChecks(NULL)
	{
	}

	const void* Object() const		{ return fObject; }
	const char* Description() const	{ return fDescription; }

	ParanoiaCheck* FirstCheck() const
	{
		return fChecks;
	}

	ParanoiaCheck* NextCheck(ParanoiaCheck* check) const
	{
		return check->fNext;
	}

	/**
	 * @brief Linear search for a check covering a specific address.
	 *
	 * @param address Guarded region start address to find.
	 * @return Matching ParanoiaCheck or NULL if absent.
	 */
	ParanoiaCheck* FindCheck(const void* address) const
	{
		ParanoiaCheck* check = fChecks;
		while (check != NULL && check->Address() != address)
			check = check->fNext;
		return check;
	}

	/**
	 * @brief Prepend a ParanoiaCheck to the set's singly-linked list.
	 *
	 * @param check Check to insert; must not already be on the list.
	 */
	void AddCheck(ParanoiaCheck* check)
	{
		check->fNext = fChecks;
		fChecks = check;
	}

	/**
	 * @brief Unlink a ParanoiaCheck from the set's list.
	 *
	 * If @a check is not actually on the list the helper intentionally
	 * dereferences NULL to produce a crash with a clear backtrace, since that
	 * indicates a serious accounting bug in the caller.
	 *
	 * @param check Check to remove.
	 */
	void RemoveCheck(ParanoiaCheck* check)
	{
		if (check == fChecks) {
			fChecks = check->fNext;
			return;
		}

		ParanoiaCheck* previous = fChecks;
		while (previous != NULL && previous->fNext != check)
			previous = previous->fNext;

		// if previous is NULL (which it shouldn't be), just crash here
		previous->fNext = check->fNext;
	}

	/**
	 * @brief Pop and return the head of the check list.
	 *
	 * Used when tearing down an entire check set.
	 *
	 * @return The detached check or NULL if the set was empty.
	 */
	ParanoiaCheck* RemoveFirstCheck()
	{
		ParanoiaCheck* check = fChecks;
		if (check == NULL)
			return NULL;

		fChecks = check->fNext;
		return check;
	}

	void SetHashNext(ParanoiaCheckSet* next)
	{
		fHashNext = next;
	}

	ParanoiaCheckSet* HashNext() const
	{
		return fHashNext;
	}

private:
	const void*			fObject;
	const char*			fDescription;
	ParanoiaCheck*		fChecks;
	ParanoiaCheckSet*	fHashNext;
};


union paranoia_slot {
	uint8			check[sizeof(ParanoiaCheck)];
	uint8			checkSet[sizeof(ParanoiaCheckSet)];
	paranoia_slot*	nextFree;
};


// #pragma mark - Tracing


#if PARANOIA_TRACING


namespace ParanoiaTracing {

class ParanoiaTraceEntry : public AbstractTraceEntry {
	public:
		ParanoiaTraceEntry(const void* object)
			:
			fObject(object)
		{
#if PARANOIA_TRACING_STACK_TRACE
		fStackTrace = capture_tracing_stack_trace(PARANOIA_TRACING_STACK_TRACE,
			1, false);
#endif
		}

#if PARANOIA_TRACING_STACK_TRACE
		virtual void DumpStackTrace(TraceOutput& out)
		{
			out.PrintStackTrace(fStackTrace);
		}
#endif

	protected:
		const void*	fObject;
#if PARANOIA_TRACING_STACK_TRACE
		tracing_stack_trace* fStackTrace;
#endif
};


class CreateCheckSet : public ParanoiaTraceEntry {
	public:
		CreateCheckSet(const void* object, const char* description)
			:
			ParanoiaTraceEntry(object)
		{
			fDescription = alloc_tracing_buffer_strcpy(description, 64, false);
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("paranoia create check set: object: %p, "
				"description: \"%s\"", fObject, fDescription);
		}

	private:
		const char*	fDescription;
};


class DeleteCheckSet : public ParanoiaTraceEntry {
	public:
		DeleteCheckSet(const void* object)
			:
			ParanoiaTraceEntry(object)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("paranoia delete check set: object: %p", fObject);
		}
};


class SetCheck : public ParanoiaTraceEntry {
	public:
		SetCheck(const void* object, const void* address, size_t size,
				paranoia_set_check_mode mode)
			:
			ParanoiaTraceEntry(object),
			fAddress(address),
			fSize(size),
			fMode(mode)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			const char* mode = "??? op:";
			switch (fMode) {
				case PARANOIA_DONT_FAIL:
					mode = "set:   ";
					break;
				case PARANOIA_FAIL_IF_EXISTS:
					mode = "add:   ";
					break;
				case PARANOIA_FAIL_IF_MISSING:
					mode = "update:";
					break;
			}
			out.Print("paranoia check %s object: %p, address: %p, size: %lu",
				mode, fObject, fAddress, fSize);
		}

	private:
		const void*				fAddress;
		size_t					fSize;
		paranoia_set_check_mode	fMode;
};


class RemoveCheck : public ParanoiaTraceEntry {
	public:
		RemoveCheck(const void* object, const void* address, size_t size)
			:
			ParanoiaTraceEntry(object),
			fAddress(address),
			fSize(size)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("paranoia check remove: object: %p, address: %p, size: "
				"%lu", fObject, fAddress, fSize);
		}

	private:
		const void*				fAddress;
		size_t					fSize;
		paranoia_set_check_mode	fMode;
};


}	// namespace ParanoiaTracing

#	define T(x)	new(std::nothrow) ParanoiaTracing::x

#else
#	define T(x)
#endif	// PARANOIA_TRACING


// #pragma mark -


#define PARANOIA_HASH_SIZE	PARANOIA_SLOT_COUNT

static paranoia_slot		sSlots[PARANOIA_SLOT_COUNT];
static paranoia_slot*		sSlotFreeList;
static ParanoiaCheckSet*	sCheckSetHash[PARANOIA_HASH_SIZE];
static spinlock				sParanoiaLock;


/**
 * @brief Pop a free slot off the global freelist.
 *
 * Slots are fixed-size unions big enough to hold either a ParanoiaCheck or a
 * ParanoiaCheckSet via placement new; this avoids the kernel heap and makes
 * the subsystem usable from allocator code itself.
 *
 * @return Pointer to a slot, or NULL if the pool is exhausted.
 */
static paranoia_slot*
allocate_slot()
{
	if (sSlotFreeList == NULL)
		return NULL;

	paranoia_slot* slot = sSlotFreeList;
	sSlotFreeList = slot->nextFree;
	return slot;
}


/**
 * @brief Return a slot to the global freelist.
 *
 * @param slot Slot previously returned by allocate_slot().
 */
static void
free_slot(paranoia_slot* slot)
{
	slot->nextFree = sSlotFreeList;
	sSlotFreeList = slot;
}


/**
 * @brief Insert a check set into the object-pointer hash table.
 *
 * The hash bucket is chosen by taking the object address modulo
 * PARANOIA_HASH_SIZE.
 *
 * @param set Fully populated check set to register.
 */
static void
add_check_set(ParanoiaCheckSet* set)
{
	int slot = (addr_t)set->Object() % PARANOIA_HASH_SIZE;
	set->SetHashNext(sCheckSetHash[slot]);
	sCheckSetHash[slot] = set;
}


/**
 * @brief Remove a check set from the hash table.
 *
 * If @a set is absent from its bucket the helper intentionally faults rather
 * than silently succeeding, since that indicates a tracking bug.
 *
 * @param set Check set to unhash.
 */
static void
remove_check_set(ParanoiaCheckSet* set)
{
	int slot = (addr_t)set->Object() % PARANOIA_HASH_SIZE;
	if (set == sCheckSetHash[slot]) {
		sCheckSetHash[slot] = set->HashNext();
		return;
	}

	ParanoiaCheckSet* previousSet = sCheckSetHash[slot];
	while (previousSet != NULL && previousSet->HashNext() != set)
		previousSet = previousSet->HashNext();

	// if previousSet is NULL (which it shouldn't be), just crash here
	previousSet->SetHashNext(set->HashNext());
}


/**
 * @brief Find the check set associated with a given object pointer.
 *
 * @param object Object pointer originally passed to create_paranoia_check_set().
 * @return Pointer to the matching set, or NULL if not registered.
 */
static ParanoiaCheckSet*
lookup_check_set(const void* object)
{
	int slot = (addr_t)object % PARANOIA_HASH_SIZE;
	ParanoiaCheckSet* set = sCheckSetHash[slot];
	while (set != NULL && set->Object() != object)
		set = set->HashNext();

	return set;
}

// #pragma mark - public interface


/**
 * @brief Register a new paranoia check set for an object.
 *
 * Allocates a slot from the static pool, constructs an empty ParanoiaCheckSet
 * in it, and stores it in the hash table. Panics if @a object is NULL or
 * already has an associated set, or if the slot pool is exhausted.
 *
 * @param object      Object pointer used as the hash key.
 * @param description Human-readable label printed in panic messages. The
 *                    string must outlive the check set (not copied).
 * @return B_OK on success, B_BAD_VALUE/B_NO_MEMORY on failure.
 */
status_t
create_paranoia_check_set(const void* object, const char* description)
{
	T(CreateCheckSet(object, description));

	if (object == NULL) {
		panic("create_paranoia_check_set(): NULL object");
		return B_BAD_VALUE;
	}

	InterruptsSpinLocker _(sParanoiaLock);

	// check, if object is already registered
	ParanoiaCheckSet* set = lookup_check_set(object);
	if (set != NULL) {
		panic("create_paranoia_check_set(): object %p already has a check set",
			object);
		return B_BAD_VALUE;
	}

	// allocate slot
	paranoia_slot* slot = allocate_slot();
	if (slot == NULL) {
		panic("create_paranoia_check_set(): out of free slots");
		return B_NO_MEMORY;
	}

	set = new(slot) ParanoiaCheckSet(object, description);
	add_check_set(set);

	return B_OK;
}


/**
 * @brief Tear down the check set associated with an object.
 *
 * Frees every ParanoiaCheck owned by the set back to the slot pool, then
 * removes the set itself. Panics if no set is registered for @a object.
 *
 * @param object Object pointer originally passed to create_paranoia_check_set().
 * @return B_OK on success, B_BAD_VALUE if no set was registered.
 */
status_t
delete_paranoia_check_set(const void* object)
{
	T(DeleteCheckSet(object));

	InterruptsSpinLocker _(sParanoiaLock);

	// get check set
	ParanoiaCheckSet* set = lookup_check_set(object);
	if (set == NULL) {
		panic("delete_paranoia_check_set(): object %p doesn't have a check set",
			object);
		return B_BAD_VALUE;
	}

	// free all checks
	while (ParanoiaCheck* check = set->RemoveFirstCheck())
		free_slot((paranoia_slot*)check);

	// free check set
	remove_check_set(set);
	free_slot((paranoia_slot*)set);

	return B_OK;
}


/**
 * @brief Validate every check belonging to an object's check set.
 *
 * Iterates the set's checks and recomputes each CRC; a mismatch triggers a
 * panic including the object description, corrupted address, and region
 * size, then the function records B_BAD_DATA and continues so that all
 * failures are surfaced before returning.
 *
 * @param object Object pointer originally passed to create_paranoia_check_set().
 * @return B_OK if every region still matches, B_BAD_DATA if any check
 *         mismatched, or B_BAD_VALUE if no set is registered.
 */
status_t
run_paranoia_checks(const void* object)
{
	InterruptsSpinLocker _(sParanoiaLock);

	// get check set
	ParanoiaCheckSet* set = lookup_check_set(object);
	if (set == NULL) {
		panic("run_paranoia_checks(): object %p doesn't have a check set",
			object);
		return B_BAD_VALUE;
	}
	
	status_t error = B_OK;

	ParanoiaCheck* check = set->FirstCheck();
	while (check != NULL) {
		if (!check->Check()) {
			panic("paranoia check failed for object %p (%s), address: %p, "
				"size: %lu", set->Object(), set->Description(),
				check->Address(), check->Size());
			error = B_BAD_DATA;
		}

		check = set->NextCheck(check);
	}

	return error;
}


/**
 * @brief Install or update a guarded memory region on an object's set.
 *
 * Behaviour depends on @a mode: PARANOIA_DONT_FAIL creates a new check or
 * refreshes the CRC of an existing one; PARANOIA_FAIL_IF_EXISTS panics if a
 * check already exists for @a address; PARANOIA_FAIL_IF_MISSING panics if no
 * check yet exists. Region size cannot be changed in place.
 *
 * @param object  Object whose check set is targeted.
 * @param address Start of the region to guard.
 * @param size    Length of the region in bytes.
 * @param mode    Semantics governing add vs. update behaviour.
 * @return B_OK on success, B_BAD_VALUE/B_NO_MEMORY on policy or resource
 *         failures.
 */
status_t
set_paranoia_check(const void* object, const void* address, size_t size,
	paranoia_set_check_mode mode)
{
	T(SetCheck(object, address, size, mode));

	InterruptsSpinLocker _(sParanoiaLock);

	// get check set
	ParanoiaCheckSet* set = lookup_check_set(object);
	if (set == NULL) {
		panic("set_paranoia_check(): object %p doesn't have a check set",
			object);
		return B_BAD_VALUE;
	}

	// update check, if already existing
	ParanoiaCheck* check = set->FindCheck(address);
	if (check != NULL) {
		if (mode == PARANOIA_FAIL_IF_EXISTS) {
			panic("set_paranoia_check(): object %p already has a check for "
				"address %p", object, address);
			return B_BAD_VALUE;
		}

		if (check->Size() != size) {
			panic("set_paranoia_check(): changing check sizes not supported");
			return B_BAD_VALUE;
		}

		check->Update();
		return B_OK;
	}

	if (mode == PARANOIA_FAIL_IF_MISSING) {
		panic("set_paranoia_check(): object %p doesn't have a check for "
			"address %p yet", object, address);
		return B_BAD_VALUE;
	}

	// allocate slot
	paranoia_slot* slot = allocate_slot();
	if (slot == NULL) {
		panic("set_paranoia_check(): out of free slots");
		return B_NO_MEMORY;
	}

	check = new(slot) ParanoiaCheck(address, size);
	set->AddCheck(check);

	return B_OK;
}


/**
 * @brief Uninstall a previously registered guarded region.
 *
 * Panics if the region isn't registered on @a object or if the recorded size
 * differs from @a size (size changes are unsupported).
 *
 * @param object  Object whose check set the region lives on.
 * @param address Start address originally passed to set_paranoia_check().
 * @param size    Length originally passed to set_paranoia_check().
 * @return B_OK on success, B_BAD_VALUE on any validation failure.
 */
status_t
remove_paranoia_check(const void* object, const void* address, size_t size)
{
	T(RemoveCheck(object, address, size));

	InterruptsSpinLocker _(sParanoiaLock);

	// get check set
	ParanoiaCheckSet* set = lookup_check_set(object);
	if (set == NULL) {
		panic("remove_paranoia_check(): object %p doesn't have a check set",
			object);
		return B_BAD_VALUE;
	}

	// get check
	ParanoiaCheck* check = set->FindCheck(address);
	if (check == NULL) {
		panic("remove_paranoia_check(): no check for address %p "
			"(object %p (%s))", address, object, set->Description());
		return B_BAD_VALUE;
	}

	if (check->Size() != size) {
		panic("remove_paranoia_check(): changing check sizes not "
			"supported");
		return B_BAD_VALUE;
	}

	set->RemoveCheck(check);
	return B_OK;
}


#endif	// ENABLE_PARANOIA_CHECKS


/**
 * @brief One-time initialization of the paranoia subsystem.
 *
 * When ENABLE_PARANOIA_CHECKS is on this populates the CRC-32 lookup table
 * and seeds the slot freelist with every entry of the static sSlots pool.
 * When disabled it compiles to an empty function.
 */
void
debug_paranoia_init()
{
#if ENABLE_PARANOIA_CHECKS
	// init CRC-32 table
	init_crc32_table();

	// init paranoia slot free list
	for (int32 i = 0; i < PARANOIA_SLOT_COUNT; i++)
		free_slot(&sSlots[i]);
#endif
}
