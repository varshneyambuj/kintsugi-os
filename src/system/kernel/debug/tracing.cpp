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
 *   Copyright 2008-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2008-2009, Axel Dörfler, axeld@pinc-software.de.
 *   Copyright 2012, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file tracing.cpp
 * @brief Kernel tracing ring buffer and the @c traced KDL command.
 *
 * Implements the lightweight in-kernel tracing infrastructure used by
 * SCHEDULER_TRACING, WAIT_FOR_OBJECTS_TRACING, VM_TRACING and friends. A single
 * contiguous ring buffer of variable-length TraceEntry records is bump-allocated
 * forward and reclaimed only by wrap-around; writers serialize via a spinlock so
 * that readers in the kernel debugger observe a consistent snapshot. Also hosts
 * the TraceEntryIterator used to walk the buffer, the AbstractTraceEntry base
 * class with its auxiliary data helpers, and the filter mini-language parser
 * backing the "traced" debugger command.
 */


#include <tracing.h>

#include <stdlib.h>

#include <algorithm>

#include <arch/debug.h>
#include <debug.h>
#include <debug_heap.h>
#include <elf.h>
#include <interrupts.h>
#include <kernel.h>
#include <team.h>
#include <thread.h>
#include <util/AutoLock.h>
#include <vm/vm.h>


#if ENABLE_TRACING

//#define TRACE_TRACING
#ifdef TRACE_TRACING
#	define TRACE(x) dprintf_no_syslog x
#else
#	define TRACE(x) ;
#endif


enum {
	WRAP_ENTRY			= 0x01,
	ENTRY_INITIALIZED	= 0x02,
	BUFFER_ENTRY		= 0x04,
	FILTER_MATCH		= 0x08,
	INVALID_ENTRY		= 0x10,
	CHECK_ENTRY			= 0x20,
};


static const size_t kTraceOutputBufferSize = 10240;
static const size_t kBufferSize = MAX_TRACE_SIZE / sizeof(trace_entry);

static const uint32 kMaxRecoveringErrorCount	= 100;
static const addr_t kMetaDataBaseAddress		= 32 * 1024 * 1024;
static const addr_t kMetaDataBaseEndAddress		= 128 * 1024 * 1024;
static const addr_t kMetaDataAddressIncrement	= 8 * 1024 * 1024;
static const uint32 kMetaDataMagic1 = 'Vali';
static const uint32 kMetaDataMagic2 = 'dTra';
static const uint32 kMetaDataMagic3 = 'cing';

// the maximum we can address with the trace_entry::[previous_]size fields
static const size_t kMaxTracingEntryByteSize
	= ((1 << 13) - 1) * sizeof(trace_entry);


struct TraceOutputPrint {
	TraceOutputPrint(TraceOutput& output)
		:
		fOutput(output)
	{
	}

	void operator()(const char* format,...) const
	{
		va_list args;
		va_start(args, format);
		fOutput.PrintArgs(format, args);
		va_end(args);
	}

private:
	TraceOutput&	fOutput;
};


class TracingMetaData {
public:
	static	status_t			Create(TracingMetaData*& _metaData);

	inline	bool				Lock();
	inline	void				Unlock();

	inline	trace_entry*		FirstEntry() const;
	inline	trace_entry*		AfterLastEntry() const;

	inline	uint32				Entries() const;
	inline	uint32				EntriesEver() const;

	inline	void				IncrementEntriesEver();

	inline	char*				TraceOutputBuffer() const;

			trace_entry*		NextEntry(trace_entry* entry);
			trace_entry*		PreviousEntry(trace_entry* entry);

			trace_entry*		AllocateEntry(size_t size, uint16 flags);

			bool				IsInBuffer(void* address, size_t size);

private:
			bool				_FreeFirstEntry();
			bool				_MakeSpace(size_t needed);

	static	status_t			_CreateMetaDataArea(bool findPrevious,
									area_id& _area,
									TracingMetaData*& _metaData);
			bool				_InitPreviousTracingData();

private:
			uint32				fMagic1;
			trace_entry*		fBuffer;
			trace_entry*		fFirstEntry;
			trace_entry*		fAfterLastEntry;
			uint32				fEntries;
			uint32				fMagic2;
			uint32				fEntriesEver;
			spinlock			fLock;
			char*				fTraceOutputBuffer;
			phys_addr_t			fPhysicalAddress;
			uint32				fMagic3;
};

static TracingMetaData sFallbackTracingMetaData;
static TracingMetaData* sTracingMetaData = &sFallbackTracingMetaData;
static bool sTracingDataRecovered = false;


// #pragma mark -


/**
 * @brief Pretty-print a captured tracing stack trace via a caller-supplied printer.
 *
 * Walks each return address, resolves it to a symbol/image via the ELF debug
 * lookup, demangles the C++ symbol name when possible, and emits one line per
 * frame using @p print. Intended to be called from KDL (panic/debugger)
 * context, hence the use of @c debug_malloc rather than the regular heap.
 *
 * @tparam Print Callable matching @c printf signature used to emit each line.
 * @param stackTrace  Captured stack trace or @c NULL (no-op if null/empty).
 * @param print       Sink receiving one formatted line per frame.
 */
template<typename Print>
static void
print_stack_trace(struct tracing_stack_trace* stackTrace,
	const Print& print)
{
	if (stackTrace == NULL || stackTrace->depth <= 0)
		return;

	static const size_t kBufferSize = 256;
	char* buffer = (char*)debug_malloc(kBufferSize);

	for (int32 i = 0; i < stackTrace->depth; i++) {
		addr_t address = stackTrace->return_addresses[i];

		const char* symbol;
		const char* demangledName = NULL;
		const char* imageName;
		bool exactMatch;
		addr_t baseAddress;

		if (elf_debug_lookup_symbol_address(address, &baseAddress, &symbol,
				&imageName, &exactMatch) == B_OK) {

			if (buffer != NULL) {
				bool isObjectMethod;
				demangledName = debug_demangle_symbol(symbol, buffer,
					kBufferSize, &isObjectMethod);
			}

			print("  %p  %s + 0x%lx (%s)%s\n", (void*)address,
				demangledName != NULL ? demangledName : symbol,
				address - baseAddress, imageName,
				exactMatch ? "" : " (nearest)");
		} else
			print("  %p\n", (void*)address);
	}

	if (buffer != NULL)
		debug_free(buffer);
}


// #pragma mark - TracingMetaData


/**
 * @brief Acquire the meta-data spinlock, serializing with tracing writers.
 *
 * The caller becomes the sole mutator of the ring-buffer state and blocks
 * every @c AllocateEntry() call on all CPUs for the duration. Callers should
 * interleave with @c disable_interrupts() when contention with ISR-level
 * tracers is possible; this helper itself does not touch interrupt state.
 *
 * @return Always @c true (kept for API parity with AutoLocker templates).
 */
bool
TracingMetaData::Lock()
{
	acquire_spinlock(&fLock);
	return true;
}


/**
 * @brief Release the meta-data spinlock previously taken by @c Lock().
 *
 * Pairs one-for-one with @c Lock(); must be called on the same CPU with
 * interrupts in the same state as at acquisition.
 */
void
TracingMetaData::Unlock()
{
	release_spinlock(&fLock);
}


/**
 * @brief Return the oldest entry currently resident in the ring buffer.
 *
 * Lock-free and safe to call from KDL/panic context. The returned pointer
 * aliases bump-allocated storage which is only reclaimed on wrap-around.
 *
 * @return Pointer to the first entry, or an empty slot if the buffer is empty.
 */
trace_entry*
TracingMetaData::FirstEntry() const
{
	return fFirstEntry;
}


/**
 * @brief Return the sentinel one-past-the-last allocated entry.
 *
 * Useful as a termination cursor when iterating forwards. Lock-free.
 *
 * @return Pointer to the slot that the next @c AllocateEntry() will populate.
 */
trace_entry*
TracingMetaData::AfterLastEntry() const
{
	return fAfterLastEntry;
}


/**
 * @brief Return the count of live (non-buffer, non-wrap) entries.
 *
 * Lock-free read; may be stale by the time the caller uses it on SMP.
 *
 * @return Number of TraceEntry instances currently resident.
 */
uint32
TracingMetaData::Entries() const
{
	return fEntries;
}


/**
 * @brief Return the monotonic count of entries ever written.
 *
 * Used by the @c traced command to detect buffer churn between continuations.
 * Lock-free; may under-count on SMP (see @c IncrementEntriesEver).
 *
 * @return Total number of entries since buffer creation.
 */
uint32
TracingMetaData::EntriesEver() const
{
	return fEntriesEver;
}


/**
 * @brief Bump the lifetime-entries counter by one.
 *
 * Deliberately non-atomic: the counter is advisory only and losing occasional
 * increments on SMP is cheaper than an atomic operation on the tracing hot
 * path. Called exactly once per fully-initialised entry.
 */
void
TracingMetaData::IncrementEntriesEver()
{
	fEntriesEver++;
		// NOTE: Race condition on SMP machines! We should use atomic_add(),
		// though that costs some performance and the information is for
		// informational purpose anyway.
}


/**
 * @brief Accessor for the scratch buffer used by @c TraceOutput formatting.
 *
 * @return Pointer to the trace output scratch buffer (size
 *         @c kTraceOutputBufferSize).
 */
char*
TracingMetaData::TraceOutputBuffer() const
{
	return fTraceOutputBuffer;
}


/**
 * @brief Step forward to the entry immediately following @p entry.
 *
 * Skips a @c WRAP_ENTRY marker by jumping back to the buffer start, so callers
 * observe a linear sequence. Lock-free; safe under @c Lock() held or not.
 *
 * @param entry  Current entry.
 * @return Next entry, or @c NULL if @p entry was the last one.
 */
trace_entry*
TracingMetaData::NextEntry(trace_entry* entry)
{
	entry += entry->size;
	if ((entry->flags & WRAP_ENTRY) != 0)
		entry = fBuffer;

	if (entry == fAfterLastEntry)
		return NULL;

	return entry;
}


/**
 * @brief Step backward to the entry immediately preceding @p entry.
 *
 * Handles buffer-start wrap by consulting the previous-size of the wrap
 * sentinel. Lock-free.
 *
 * @param entry  Current entry.
 * @return Previous entry, or @c NULL if @p entry is the first one.
 */
trace_entry*
TracingMetaData::PreviousEntry(trace_entry* entry)
{
	if (entry == fFirstEntry)
		return NULL;

	if (entry == fBuffer) {
		// beginning of buffer -- previous entry is a wrap entry
		entry = fBuffer + kBufferSize - entry->previous_size;
	}

	return entry - entry->previous_size;
}


/**
 * @brief Bump-allocate storage for one trace entry with the given flags.
 *
 * Takes @c fLock with interrupts disabled, so it is safe to call from any
 * non-panic kernel context including ISRs. The entry is reclaimed only when
 * the write head wraps and overwrites it, therefore TraceEntry instances must
 * never be freed explicitly; their destructors are expected to be empty.
 *
 * @param size   Total byte size needed, including the @c trace_entry header.
 * @param flags  Initial flags word (e.g. @c BUFFER_ENTRY for auxiliary data).
 * @return Pointer to the allocated @c trace_entry header, or @c NULL on
 *         failure (buffer not yet created, zero-sized, or oversize request).
 */
trace_entry*
TracingMetaData::AllocateEntry(size_t size, uint16 flags)
{
	if (fAfterLastEntry == NULL || size == 0
		|| size >= kMaxTracingEntryByteSize) {
		return NULL;
	}

	InterruptsSpinLocker _(fLock);

	size = (size + 3) >> 2;
		// 4 byte aligned, don't store the lower 2 bits

	TRACE(("AllocateEntry(%lu), start %p, end %p, buffer %p\n", size * 4,
		fFirstEntry, fAfterLastEntry, fBuffer));

	if (!_MakeSpace(size))
		return NULL;

	trace_entry* entry = fAfterLastEntry;
	entry->size = size;
	entry->flags = flags;
	fAfterLastEntry += size;
	fAfterLastEntry->previous_size = size;

	if (!(flags & BUFFER_ENTRY))
		fEntries++;

	TRACE(("  entry: %p, end %p, start %p, entries %ld\n", entry,
		fAfterLastEntry, fFirstEntry, fEntries));

	return entry;
}


/**
 * @brief Test whether @p address..@p address+@p size lies inside the live region.
 *
 * Used by validity checks (e.g. verifying a candidate AbstractTraceEntry
 * pointer) to avoid dereferencing stale or out-of-range memory. Lock-free.
 *
 * @param address  Base pointer to test.
 * @param size     Byte extent of the region starting at @p address.
 * @return @c true if the whole range is in the currently occupied portion.
 */
bool
TracingMetaData::IsInBuffer(void* address, size_t size)
{
	if (fEntries == 0)
		return false;

	addr_t start = (addr_t)address;
	addr_t end = start + size;

	if (start < (addr_t)fBuffer || end > (addr_t)(fBuffer + kBufferSize))
		return false;

	if (fFirstEntry > fAfterLastEntry)
		return start >= (addr_t)fFirstEntry || end <= (addr_t)fAfterLastEntry;

	return start >= (addr_t)fFirstEntry && end <= (addr_t)fAfterLastEntry;
}


/**
 * @brief Reclaim the oldest entry by advancing @c fFirstEntry past it.
 *
 * Invoked under @c fLock. A not-yet-initialised TraceEntry cannot be freed
 * safely because its constructor could still be writing into the slot; the
 * caller must retry later in that case.
 *
 * @return @c true on success, @c false if the oldest entry is still being
 *         constructed.
 */
bool
TracingMetaData::_FreeFirstEntry()
{
	TRACE(("  skip start %p, %lu*4 bytes\n", fFirstEntry, fFirstEntry->size));

	trace_entry* newFirst = NextEntry(fFirstEntry);

	if (fFirstEntry->flags & BUFFER_ENTRY) {
		// a buffer entry -- just skip it
	} else if (fFirstEntry->flags & ENTRY_INITIALIZED) {
		// Fully initialized TraceEntry: We could destroy it, but don't do so
		// for sake of robustness. The destructors of tracing entry classes
		// should be empty anyway.
		fEntries--;
	} else {
		// Not fully initialized TraceEntry. We can't free it, since
		// then it's constructor might still write into the memory and
		// overwrite data of the entry we're going to allocate.
		// We can't do anything until this entry can be discarded.
		return false;
	}

	if (newFirst == NULL) {
		// everything is freed -- practically this can't happen, if
		// the buffer is large enough to hold three max-sized entries
		fFirstEntry = fAfterLastEntry = fBuffer;
		TRACE(("_FreeFirstEntry(): all entries freed!\n"));
	} else
		fFirstEntry = newFirst;

	return true;
}


/**
 * @brief Ensure @p needed trace_entry slots are free after @c fAfterLastEntry.
 *
 * Called under @c fLock from @c AllocateEntry. If the contiguous tail is
 * insufficient, wrap-around is performed (planting a @c WRAP_ENTRY sentinel)
 * and as many leading entries as necessary are reclaimed via
 * @c _FreeFirstEntry. Fails if any such entry is still uninitialised.
 *
 * @param needed  Number of @c trace_entry-sized slots required.
 * @return @c true on success, @c false if the space cannot currently be made.
 */
bool
TracingMetaData::_MakeSpace(size_t needed)
{
	// we need space for fAfterLastEntry, too (in case we need to wrap around
	// later)
	needed++;

	// If there's not enough space (free or occupied) after fAfterLastEntry,
	// we free all entries in that region and wrap around.
	if (fAfterLastEntry + needed > fBuffer + kBufferSize) {
		TRACE(("_MakeSpace(%lu), wrapping around: after last: %p\n", needed,
			fAfterLastEntry));

		// Free all entries after fAfterLastEntry and one more at the beginning
		// of the buffer.
		while (fFirstEntry > fAfterLastEntry) {
			if (!_FreeFirstEntry())
				return false;
		}
		if (fAfterLastEntry != fBuffer && !_FreeFirstEntry())
			return false;

		// just in case _FreeFirstEntry() freed the very last existing entry
		if (fAfterLastEntry == fBuffer)
			return true;

		// mark as wrap entry and actually wrap around
		trace_entry* wrapEntry = fAfterLastEntry;
		wrapEntry->size = 0;
		wrapEntry->flags = WRAP_ENTRY;
		fAfterLastEntry = fBuffer;
		fAfterLastEntry->previous_size = fBuffer + kBufferSize - wrapEntry;
	}

	if (fFirstEntry <= fAfterLastEntry) {
		// buffer is empty or the space after fAfterLastEntry is unoccupied
		return true;
	}

	// free the first entries, until there's enough space
	size_t space = fFirstEntry - fAfterLastEntry;

	if (space < needed) {
		TRACE(("_MakeSpace(%lu), left %ld\n", needed, space));
	}

	while (space < needed) {
		space += fFirstEntry->size;

		if (!_FreeFirstEntry())
			return false;
	}

	TRACE(("  out: start %p, entries %ld\n", fFirstEntry, fEntries));

	return true;
}


/**
 * @brief Locate existing tracing metadata from a prior session or create fresh.
 *
 * First probes well-known physical addresses for a previously-valid metadata
 * block and tries to re-attach its buffer; on any failure falls back to
 * allocating new metadata plus a contiguous tracing log area. Called once from
 * @c tracing_init during kernel boot.
 *
 * @param[out] _metaData  Receives the instance to drive subsequent allocations.
 * @return @c B_OK on success, or an error from the area allocator.
 */
/*static*/ status_t
TracingMetaData::Create(TracingMetaData*& _metaData)
{
	// search meta data in memory (from previous session)
	area_id area;
	TracingMetaData* metaData;
	status_t error = _CreateMetaDataArea(true, area, metaData);
	if (error == B_OK) {
		if (metaData->_InitPreviousTracingData()) {
			_metaData = metaData;
			return B_OK;
		}

		dprintf("Found previous tracing meta data, but failed to init.\n");

		// invalidate the meta data
		metaData->fMagic1 = 0;
		metaData->fMagic2 = 0;
		metaData->fMagic3 = 0;
		delete_area(area);
	} else
		dprintf("No previous tracing meta data found.\n");

	// no previous tracing data found -- create new one
	error = _CreateMetaDataArea(false, area, metaData);
	if (error != B_OK)
		return error;

	virtual_address_restrictions virtualRestrictions = {};
	virtualRestrictions.address_specification = B_ANY_KERNEL_ADDRESS;
	physical_address_restrictions physicalRestrictions = {};
	area = create_area_etc(B_SYSTEM_TEAM, "tracing log",
		kTraceOutputBufferSize + MAX_TRACE_SIZE, B_CONTIGUOUS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, CREATE_AREA_DONT_WAIT, 0,
		&virtualRestrictions, &physicalRestrictions,
		(void**)&metaData->fTraceOutputBuffer);
	if (area < 0)
		return area;

	// get the physical address
	physical_entry physicalEntry;
	if (get_memory_map(metaData->fTraceOutputBuffer, B_PAGE_SIZE,
			&physicalEntry, 1) == B_OK) {
		metaData->fPhysicalAddress = physicalEntry.address;
	} else {
		dprintf("TracingMetaData::Create(): failed to get physical address "
			"of tracing buffer\n");
		metaData->fPhysicalAddress = 0;
	}

	metaData->fBuffer = (trace_entry*)(metaData->fTraceOutputBuffer
		+ kTraceOutputBufferSize);
	metaData->fFirstEntry = metaData->fBuffer;
	metaData->fAfterLastEntry = metaData->fBuffer;

	metaData->fEntries = 0;
	metaData->fEntriesEver = 0;
	B_INITIALIZE_SPINLOCK(&metaData->fLock);

	metaData->fMagic1 = kMetaDataMagic1;
	metaData->fMagic2 = kMetaDataMagic2;
	metaData->fMagic3 = kMetaDataMagic3;

	_metaData = metaData;
	return B_OK;
}


/**
 * @brief Create or locate the physical-memory-backed metadata area.
 *
 * Scans a fixed sweep of physical addresses, either searching for a magic-
 * matching previous session (@p findPrevious == @c true) or claiming the first
 * usable slot. Falls back to @c sFallbackTracingMetaData if every location is
 * unavailable.
 *
 * @param findPrevious  If @c true, require magic match against a prior session.
 * @param[out] _area     Receives the created/attached area id on success.
 * @param[out] _metaData Receives the metadata pointer on success.
 * @return @c B_OK or @c B_ENTRY_NOT_FOUND when no previous session is found.
 */
/*static*/ status_t
TracingMetaData::_CreateMetaDataArea(bool findPrevious, area_id& _area,
	TracingMetaData*& _metaData)
{
	// search meta data in memory (from previous session)
	TracingMetaData* metaData;
	phys_addr_t metaDataAddress = kMetaDataBaseAddress;
	for (; metaDataAddress <= kMetaDataBaseEndAddress;
			metaDataAddress += kMetaDataAddressIncrement) {
		virtual_address_restrictions virtualRestrictions = {};
		virtualRestrictions.address_specification = B_ANY_KERNEL_ADDRESS;
		physical_address_restrictions physicalRestrictions = {};
		physicalRestrictions.low_address = metaDataAddress;
		physicalRestrictions.high_address = metaDataAddress + B_PAGE_SIZE;
		area_id area = create_area_etc(B_SYSTEM_TEAM, "tracing metadata",
			B_PAGE_SIZE, B_FULL_LOCK, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
			CREATE_AREA_DONT_CLEAR, 0, &virtualRestrictions,
			&physicalRestrictions, (void**)&metaData);
		if (area < 0)
			continue;

		if (!findPrevious) {
			_area = area;
			_metaData = metaData;
			return B_OK;
		}

		if (metaData->fMagic1 == kMetaDataMagic1
			&& metaData->fMagic2 == kMetaDataMagic2
			&& metaData->fMagic3 == kMetaDataMagic3) {
			_area = area;
			_metaData = metaData;
			return B_OK;
		}

		delete_area(area);
	}

	if (findPrevious)
		return B_ENTRY_NOT_FOUND;

	// We could allocate any of the standard locations. Instead of failing
	// entirely, we use the static meta data. The tracing buffer won't be
	// reattachable in the next session, but at least we can use it in this
	// session.
	_metaData = &sFallbackTracingMetaData;
	return B_OK;
}


/**
 * @brief Attempt to validate and re-attach a tracing buffer from the last boot.
 *
 * Re-maps the old contiguous log area at the recorded physical address, then
 * sweeps the entry list to repair previous-size/size/wrap-flag invariants up
 * to @c kMaxRecoveringErrorCount problems. Currently short-circuits to
 * @c false at the top because the vtable pointers in recovered entries are
 * not yet validated safely.
 *
 * @return @c true on successful recovery, @c false otherwise.
 */
bool
TracingMetaData::_InitPreviousTracingData()
{
	// TODO: ATM re-attaching the previous tracing buffer doesn't work very
	// well. The entries should be checked more thoroughly for validity -- e.g.
	// the pointers to the entries' vtable pointers could be invalid, which can
	// make the "traced" command quite unusable. The validity of the entries
	// could be checked in a safe environment (i.e. with a fault handler) with
	// typeid() and call of a virtual function.
	return false;

	addr_t bufferStart
		= (addr_t)fTraceOutputBuffer + kTraceOutputBufferSize;
	addr_t bufferEnd = bufferStart + MAX_TRACE_SIZE;

	if (bufferStart > bufferEnd || (addr_t)fBuffer != bufferStart
		|| (addr_t)fFirstEntry % sizeof(trace_entry) != 0
		|| (addr_t)fFirstEntry < bufferStart
		|| (addr_t)fFirstEntry + sizeof(trace_entry) >= bufferEnd
		|| (addr_t)fAfterLastEntry % sizeof(trace_entry) != 0
		|| (addr_t)fAfterLastEntry < bufferStart
		|| (addr_t)fAfterLastEntry > bufferEnd
		|| fPhysicalAddress == 0) {
		dprintf("Failed to init tracing meta data: Sanity checks "
			"failed.\n");
		return false;
	}

	// re-map the previous tracing buffer
	virtual_address_restrictions virtualRestrictions = {};
	virtualRestrictions.address = fTraceOutputBuffer;
	virtualRestrictions.address_specification = B_EXACT_ADDRESS;
	physical_address_restrictions physicalRestrictions = {};
	physicalRestrictions.low_address = fPhysicalAddress;
	physicalRestrictions.high_address = fPhysicalAddress
		+ ROUNDUP(kTraceOutputBufferSize + MAX_TRACE_SIZE, B_PAGE_SIZE);
	area_id area = create_area_etc(B_SYSTEM_TEAM, "tracing log",
		kTraceOutputBufferSize + MAX_TRACE_SIZE, B_CONTIGUOUS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, CREATE_AREA_DONT_CLEAR, 0,
		&virtualRestrictions, &physicalRestrictions, NULL);
	if (area < 0) {
		dprintf("Failed to init tracing meta data: Mapping tracing log "
			"buffer failed: %s\n", strerror(area));
		return false;
	}

	dprintf("ktrace: Remapped tracing buffer at %p, size: %" B_PRIuSIZE "\n",
		fTraceOutputBuffer, kTraceOutputBufferSize + MAX_TRACE_SIZE);

	// verify/repair the tracing entry list
	uint32 errorCount = 0;
	uint32 entryCount = 0;
	uint32 nonBufferEntryCount = 0;
	uint32 previousEntrySize = 0;
	trace_entry* entry = fFirstEntry;
	while (errorCount <= kMaxRecoveringErrorCount) {
		// check previous entry size
		if (entry->previous_size != previousEntrySize) {
			if (entry != fFirstEntry) {
				dprintf("ktrace recovering: entry %p: fixing previous_size "
					"size: %" B_PRIu32 " (should be %" B_PRIu32 ")\n", entry,
					entry->previous_size, previousEntrySize);
				errorCount++;
			}
			entry->previous_size = previousEntrySize;
		}

		if (entry == fAfterLastEntry)
			break;

		// check size field
		if ((entry->flags & WRAP_ENTRY) == 0 && entry->size == 0) {
			dprintf("ktrace recovering: entry %p: non-wrap entry size is 0\n",
				entry);
			errorCount++;
			fAfterLastEntry = entry;
			break;
		}

		if (entry->size > uint32(fBuffer + kBufferSize - entry)) {
			dprintf("ktrace recovering: entry %p: size too big: %" B_PRIu32 "\n",
				entry, entry->size);
			errorCount++;
			fAfterLastEntry = entry;
			break;
		}

		if (entry < fAfterLastEntry && entry + entry->size > fAfterLastEntry) {
			dprintf("ktrace recovering: entry %p: entry crosses "
				"fAfterLastEntry (%p)\n", entry, fAfterLastEntry);
			errorCount++;
			fAfterLastEntry = entry;
			break;
		}

		// check for wrap entry
		if ((entry->flags & WRAP_ENTRY) != 0) {
			if ((uint32)(fBuffer + kBufferSize - entry)
					> kMaxTracingEntryByteSize / sizeof(trace_entry)) {
				dprintf("ktrace recovering: entry %p: wrap entry at invalid "
					"buffer location\n", entry);
				errorCount++;
			}

			if (entry->size != 0) {
				dprintf("ktrace recovering: entry %p: invalid wrap entry "
					"size: %" B_PRIu32 "\n", entry, entry->size);
				errorCount++;
				entry->size = 0;
			}

			previousEntrySize = fBuffer + kBufferSize - entry;
			entry = fBuffer;
			continue;
		}

		if ((entry->flags & BUFFER_ENTRY) == 0) {
			entry->flags |= CHECK_ENTRY;
			nonBufferEntryCount++;
		}

		entryCount++;
		previousEntrySize = entry->size;

		entry += entry->size;
	}

	if (errorCount > kMaxRecoveringErrorCount) {
		dprintf("ktrace recovering: Too many errors.\n");
		fAfterLastEntry = entry;
		fAfterLastEntry->previous_size = previousEntrySize;
	}

	dprintf("ktrace recovering: Recovered %" B_PRIu32 " entries + %" B_PRIu32
		" buffer entries from previous session. Expected %" B_PRIu32
		" entries.\n", nonBufferEntryCount, entryCount - nonBufferEntryCount,
		fEntries);
	fEntries = nonBufferEntryCount;

	B_INITIALIZE_SPINLOCK(&fLock);

	// TODO: Actually check the entries! Do that when first accessing the
	// tracing buffer from the kernel debugger (when sTracingDataRecovered is
	// true).
	sTracingDataRecovered = true;
	return true;
}


#endif	// ENABLE_TRACING


// #pragma mark -


/**
 * @brief Construct a TraceOutput over a caller-provided scratch buffer.
 *
 * No allocations are performed; the buffer is zeroed by @c Clear() so that
 * partially-formatted output is always C-string-safe.
 *
 * @param buffer      Storage for formatted text (owned by caller).
 * @param bufferSize  Capacity of @p buffer in bytes.
 * @param flags       Bitmask of @c TRACE_OUTPUT_* format options.
 */
TraceOutput::TraceOutput(char* buffer, size_t bufferSize, uint32 flags)
	: fBuffer(buffer),
	  fCapacity(bufferSize),
	  fFlags(flags)
{
	Clear();
}


/**
 * @brief Reset the buffer to an empty C-string.
 *
 * Drops any previously-formatted contents; subsequent PrintArgs() calls start
 * writing at offset 0.
 */
void
TraceOutput::Clear()
{
	if (fCapacity > 0)
		fBuffer[0] = '\0';
	fSize = 0;
}


/**
 * @brief Append a @c printf-style formatted fragment to the output buffer.
 *
 * Safe to call from KDL; silently drops additional text once the buffer is
 * full. The buffer is always kept NUL-terminated.
 *
 * @param format  @c printf format string.
 * @param args    Arguments matching @p format.
 */
void
TraceOutput::PrintArgs(const char* format, va_list args)
{
#if ENABLE_TRACING
	if (IsFull())
		return;

	size_t length = vsnprintf(fBuffer + fSize, fCapacity - fSize, format, args);
	fSize += std::min(length, fCapacity - fSize - 1);
#endif
}


/**
 * @brief Append a symbolised rendering of @p stackTrace to the output buffer.
 *
 * Delegates to @c print_stack_trace with this object as the sink. No-op when
 * tracing is compile-time disabled.
 *
 * @param stackTrace  Captured stack trace to render, or @c NULL.
 */
void
TraceOutput::PrintStackTrace(tracing_stack_trace* stackTrace)
{
#if ENABLE_TRACING
	print_stack_trace(stackTrace, TraceOutputPrint(*this));
#endif
}


/**
 * @brief Remember the timestamp of the last printed entry for diff-time mode.
 *
 * Used by @c AbstractTraceEntry::Dump to render inter-entry deltas when the
 * @c TRACE_OUTPUT_DIFF_TIME flag is set.
 *
 * @param time  System time (microseconds) of the most recent entry rendered.
 */
void
TraceOutput::SetLastEntryTime(bigtime_t time)
{
	fLastEntryTime = time;
}


/**
 * @brief Retrieve the timestamp stored by @c SetLastEntryTime.
 *
 * @return Last recorded entry time, or 0 if never set.
 */
bigtime_t
TraceOutput::LastEntryTime() const
{
	return fLastEntryTime;
}


//	#pragma mark -


/**
 * @brief Construct a base trace entry.
 *
 * Storage is already bump-allocated by the custom @c operator @c new; this
 * constructor performs no additional work.
 */
TraceEntry::TraceEntry()
{
}


/**
 * @brief Virtual destructor kept trivial on purpose.
 *
 * Entry storage is reclaimed only by ring-buffer wrap-around, so destructors
 * must be effectively no-ops to avoid touching recycled memory.
 */
TraceEntry::~TraceEntry()
{
}


/**
 * @brief Default implementation; prints a generic "ENTRY <ptr>" line.
 *
 * Subclasses typically override this or @c AddDump to produce meaningful text.
 *
 * @param out  Sink for formatted output.
 */
void
TraceEntry::Dump(TraceOutput& out)
{
#if ENABLE_TRACING
	// to be overridden by subclasses
	out.Print("ENTRY %p", this);
#endif
}


/**
 * @brief Default no-op stack-trace renderer.
 *
 * Overridden by @c AbstractTraceEntryWithStackTrace (and similar classes) to
 * emit the captured stack frames.
 *
 * @param out  Sink for the rendered trace.
 */
void
TraceEntry::DumpStackTrace(TraceOutput& out)
{
}


/**
 * @brief Mark this entry as fully constructed and visible to iterators.
 *
 * Sets @c ENTRY_INITIALIZED on the trace_entry header and bumps the lifetime
 * counter. Must be called from the subclass constructor once all fields are
 * populated; until this runs the allocator will not reclaim the entry but
 * iterators in the debugger treat it as "uninitialized".
 */
void
TraceEntry::Initialized()
{
#if ENABLE_TRACING
	ToTraceEntry()->flags |= ENTRY_INITIALIZED;
	sTracingMetaData->IncrementEntriesEver();
#endif
}


/**
 * @brief Placement-style @c new drawing from the tracing ring buffer.
 *
 * Allocates @p size + @c sizeof(trace_entry) bytes via
 * @c TracingMetaData::AllocateEntry and returns a pointer to the storage
 * immediately after the header. The returned memory is lifetime-bound to the
 * ring buffer and must never be released with @c delete.
 *
 * @param size        Size of the subclass object in bytes.
 * @return Pointer to bump-allocated storage, or @c NULL on failure.
 */
void*
TraceEntry::operator new(size_t size, const std::nothrow_t&) throw()
{
#if ENABLE_TRACING
	trace_entry* entry = sTracingMetaData->AllocateEntry(
		size + sizeof(trace_entry), 0);
	return entry != NULL ? entry + 1 : NULL;
#endif
	return NULL;
}


//	#pragma mark -


/**
 * @brief Virtual destructor kept trivial; see @c TraceEntry::~TraceEntry.
 */
AbstractTraceEntry::~AbstractTraceEntry()
{
}


/**
 * @brief Render the common "[tid] timestamp:" prefix then delegate to AddDump.
 *
 * Honours @c TRACE_OUTPUT_DIFF_TIME (prints delta from the previous entry)
 * and @c TRACE_OUTPUT_TEAM_ID (prints the team id too). After the subclass
 * AddDump runs, updates the output's last-entry-time stamp.
 *
 * @param out  Sink for formatted output.
 */
void
AbstractTraceEntry::Dump(TraceOutput& out)
{
	bigtime_t time = (out.Flags() & TRACE_OUTPUT_DIFF_TIME)
		? fTime - out.LastEntryTime()
		: fTime;

	if (out.Flags() & TRACE_OUTPUT_TEAM_ID) {
		out.Print("[%6" B_PRId32 ":%6" B_PRId32 "] %10" B_PRId64 ": ", fThread,
			fTeam, time);
	} else
		out.Print("[%6" B_PRId32 "] %10" B_PRId64 ": ", fThread, time);

	AddDump(out);

	out.SetLastEntryTime(fTime);
}


/**
 * @brief Default AddDump does nothing; subclasses produce the body text.
 *
 * @param out  Sink for formatted output.
 */
void
AbstractTraceEntry::AddDump(TraceOutput& out)
{
}


/**
 * @brief Populate the thread id, team id and timestamp fields from the caller.
 *
 * Invoked from subclass constructors; captures @c system_time() and the
 * running thread/team to enable thread- and team-filtered dumps.
 */
void
AbstractTraceEntry::_Init()
{
	Thread* thread = thread_get_current_thread();
	if (thread != NULL) {
		fThread = thread->id;
		if (thread->team)
			fTeam = thread->team->id;
	}
	fTime = system_time();
}


//	#pragma mark - AbstractTraceEntryWithStackTrace



/**
 * @brief Capture a stack trace into the ring buffer as auxiliary data.
 *
 * The storage for the trace is itself allocated with @c BUFFER_ENTRY so that
 * it follows the same wrap-reclaim lifetime as the owning entry.
 *
 * @param stackTraceDepth  Maximum number of frames to record.
 * @param skipFrames       Frames to skip before recording (constructor frame
 *                         is implicitly added).
 * @param kernelOnly       If @c true, omit user-space frames.
 */
AbstractTraceEntryWithStackTrace::AbstractTraceEntryWithStackTrace(
	size_t stackTraceDepth, size_t skipFrames, bool kernelOnly)
{
	fStackTrace = capture_tracing_stack_trace(stackTraceDepth, skipFrames + 1,
		kernelOnly);
}


/**
 * @brief Emit the captured stack trace via the @c TraceOutput sink.
 *
 * @param out  Sink for the rendered trace.
 */
void
AbstractTraceEntryWithStackTrace::DumpStackTrace(TraceOutput& out)
{
	out.PrintStackTrace(fStackTrace);
}


//	#pragma mark -


#if ENABLE_TRACING

/**
 * @brief Trace entry for kernel-originated @c ktrace_printf messages.
 */
class KernelTraceEntry : public AbstractTraceEntry {
	public:
		/**
		 * @brief Snapshot the kernel message string into buffer storage.
		 *
		 * @param message  NUL-terminated kernel-space string (max 256 bytes).
		 */
		KernelTraceEntry(const char* message)
		{
			fMessage = alloc_tracing_buffer_strcpy(message, 256, false);

#if KTRACE_PRINTF_STACK_TRACE
			fStackTrace = capture_tracing_stack_trace(
				KTRACE_PRINTF_STACK_TRACE, 1, false);
#endif
			Initialized();
		}

		/**
		 * @brief Render the entry body prefixed with "kern:".
		 * @param out  Formatting sink.
		 */
		virtual void AddDump(TraceOutput& out)
		{
			out.Print("kern: %s", fMessage);
		}

#if KTRACE_PRINTF_STACK_TRACE
		/**
		 * @brief Emit the optional captured stack trace.
		 * @param out  Formatting sink.
		 */
		virtual void DumpStackTrace(TraceOutput& out)
		{
			out.PrintStackTrace(fStackTrace);
		}
#endif

	private:
		char*	fMessage;
#if KTRACE_PRINTF_STACK_TRACE
		tracing_stack_trace* fStackTrace;
#endif
};


/**
 * @brief Trace entry for user-originated @c _user_ktrace_output messages.
 */
class UserTraceEntry : public AbstractTraceEntry {
	public:
		/**
		 * @brief Copy a user-space message safely into buffer storage.
		 *
		 * @param message  User-space pointer to a NUL-terminated string.
		 */
		UserTraceEntry(const char* message)
		{
			fMessage = alloc_tracing_buffer_strcpy(message, 256, true);

#if KTRACE_PRINTF_STACK_TRACE
			fStackTrace = capture_tracing_stack_trace(
				KTRACE_PRINTF_STACK_TRACE, 1, false);
#endif
			Initialized();
		}

		/**
		 * @brief Render the entry body prefixed with "user:".
		 * @param out  Formatting sink.
		 */
		virtual void AddDump(TraceOutput& out)
		{
			out.Print("user: %s", fMessage);
		}

#if KTRACE_PRINTF_STACK_TRACE
		/**
		 * @brief Emit the optional captured stack trace.
		 * @param out  Formatting sink.
		 */
		virtual void DumpStackTrace(TraceOutput& out)
		{
			out.PrintStackTrace(fStackTrace);
		}
#endif

	private:
		char*	fMessage;
#if KTRACE_PRINTF_STACK_TRACE
		tracing_stack_trace* fStackTrace;
#endif
};


/**
 * @brief Marker entry emitted once at boot so iterators have a visible origin.
 */
class TracingLogStartEntry : public AbstractTraceEntry {
	public:
		/**
		 * @brief Construct and mark the entry as initialised.
		 */
		TracingLogStartEntry()
		{
			Initialized();
		}

		/**
		 * @brief Emit the literal string "ktrace start".
		 * @param out  Formatting sink.
		 */
		virtual void AddDump(TraceOutput& out)
		{
			out.Print("ktrace start");
		}
};

#endif	// ENABLE_TRACING


//	#pragma mark - trace filters


/**
 * @brief Virtual destructor for the polymorphic filter base class.
 */
TraceFilter::~TraceFilter()
{
}


/**
 * @brief Default filter: rejects every entry.
 *
 * Concrete filter subclasses override this with their predicate.
 *
 * @param entry  Entry under consideration (unused in the base class).
 * @param out    Lazy formatter that concrete filters can consult.
 * @return Always @c false.
 */
bool
TraceFilter::Filter(const TraceEntry* entry, LazyTraceOutput& out)
{
	return false;
}



/**
 * @brief Filter accepting entries whose thread id matches @c fThread.
 */
class ThreadTraceFilter : public TraceFilter {
public:
	/**
	 * @brief Predicate matching on @c AbstractTraceEntry::ThreadID.
	 * @param _entry  Candidate entry.
	 * @param out     Lazy formatter (unused).
	 * @return @c true if the entry is an AbstractTraceEntry for @c fThread.
	 */
	virtual bool Filter(const TraceEntry* _entry, LazyTraceOutput& out)
	{
		const AbstractTraceEntry* entry
			= dynamic_cast<const AbstractTraceEntry*>(_entry);
		return (entry != NULL && entry->ThreadID() == fThread);
	}
};


/**
 * @brief Filter accepting entries whose team id matches @c fTeam.
 */
class TeamTraceFilter : public TraceFilter {
public:
	/**
	 * @brief Predicate matching on @c AbstractTraceEntry::TeamID.
	 * @param _entry  Candidate entry.
	 * @param out     Lazy formatter (unused).
	 * @return @c true if the entry is an AbstractTraceEntry for @c fTeam.
	 */
	virtual bool Filter(const TraceEntry* _entry, LazyTraceOutput& out)
	{
		const AbstractTraceEntry* entry
			= dynamic_cast<const AbstractTraceEntry*>(_entry);
		return (entry != NULL && entry->TeamID() == fTeam);
	}
};


/**
 * @brief Filter matching entries whose rendered form contains @c fString.
 */
class PatternTraceFilter : public TraceFilter {
public:
	/**
	 * @brief Substring predicate against the lazily-dumped entry text.
	 * @param entry  Candidate entry.
	 * @param out    Lazy formatter used to cache entry rendering.
	 * @return @c true on substring match.
	 */
	virtual bool Filter(const TraceEntry* entry, LazyTraceOutput& out)
	{
		return strstr(out.DumpEntry(entry), fString) != NULL;
	}
};


/**
 * @brief Filter matching entries whose rendered form contains @c fValue as
 *        a decimal literal.
 */
class DecimalPatternTraceFilter : public TraceFilter {
public:
	/**
	 * @brief Format @c fValue in base 10 and test substring.
	 * @param entry  Candidate entry.
	 * @param out    Lazy formatter.
	 * @return @c true on substring match.
	 */
	virtual bool Filter(const TraceEntry* entry, LazyTraceOutput& out)
	{
		// TODO: this is *very* slow
		char buffer[64];
		snprintf(buffer, sizeof(buffer), "%" B_PRId64, fValue);
		return strstr(out.DumpEntry(entry), buffer) != NULL;
	}
};

/**
 * @brief Filter matching entries whose rendered form contains @c fValue as
 *        a hex literal.
 */
class HexPatternTraceFilter : public TraceFilter {
public:
	/**
	 * @brief Format @c fValue in base 16 and test substring.
	 * @param entry  Candidate entry.
	 * @param out    Lazy formatter.
	 * @return @c true on substring match.
	 */
	virtual bool Filter(const TraceEntry* entry, LazyTraceOutput& out)
	{
		// TODO: this is *very* slow
		char buffer[64];
		snprintf(buffer, sizeof(buffer), "%" B_PRIx64, fValue);
		return strstr(out.DumpEntry(entry), buffer) != NULL;
	}
};

/**
 * @brief Filter matching entries whose rendered form contains the string at
 *        address @c fValue.
 */
class StringPatternTraceFilter : public TraceFilter {
public:
	/**
	 * @brief Dereference @c fValue (kernel or user address) and test substring.
	 * @param entry  Candidate entry.
	 * @param out    Lazy formatter.
	 * @return @c true on substring match.
	 */
	virtual bool Filter(const TraceEntry* entry, LazyTraceOutput& out)
	{
		if (IS_KERNEL_ADDRESS(fValue))
			return strstr(out.DumpEntry(entry), (const char*)fValue) != NULL;

		// TODO: this is *very* slow
		char buffer[64];
		user_strlcpy(buffer, (const char*)fValue, sizeof(buffer));
		return strstr(out.DumpEntry(entry), buffer) != NULL;
	}
};

/**
 * @brief Logical-NOT combinator over the first sub-filter.
 */
class NotTraceFilter : public TraceFilter {
public:
	/**
	 * @brief Invert the outcome of the wrapped sub-filter.
	 * @param entry  Candidate entry.
	 * @param out    Lazy formatter.
	 * @return Negation of the sub-filter's result.
	 */
	virtual bool Filter(const TraceEntry* entry, LazyTraceOutput& out)
	{
		return !fSubFilters.first->Filter(entry, out);
	}
};


/**
 * @brief Short-circuiting logical-AND combinator.
 */
class AndTraceFilter : public TraceFilter {
public:
	/**
	 * @brief Evaluate the left, then right, sub-filter.
	 * @param entry  Candidate entry.
	 * @param out    Lazy formatter.
	 * @return @c true only if both sub-filters accept.
	 */
	virtual bool Filter(const TraceEntry* entry, LazyTraceOutput& out)
	{
		return fSubFilters.first->Filter(entry, out)
			&& fSubFilters.second->Filter(entry, out);
	}
};


/**
 * @brief Short-circuiting logical-OR combinator.
 */
class OrTraceFilter : public TraceFilter {
public:
	/**
	 * @brief Evaluate the left, then right, sub-filter.
	 * @param entry  Candidate entry.
	 * @param out    Lazy formatter.
	 * @return @c true if either sub-filter accepts.
	 */
	virtual bool Filter(const TraceEntry* entry, LazyTraceOutput& out)
	{
		return fSubFilters.first->Filter(entry, out)
			|| fSubFilters.second->Filter(entry, out);
	}
};


/**
 * @brief Prefix-notation parser for the tracing filter mini-language.
 *
 * The grammar accepts the prefix combinators "not", "and", "or", token
 * filters "thread <id>", "team <id>", and substring filters prefixed by
 * '#', 'd#', 'x#', 's#'. Parsed filters are allocated in a fixed-size
 * in-line arena to avoid touching the heap from the debugger.
 */
class TraceFilterParser {
public:
	/**
	 * @brief Obtain the shared singleton parser.
	 * @return Pointer to the static default instance.
	 */
	static TraceFilterParser* Default()
	{
		return &sParser;
	}

	/**
	 * @brief Parse the argv-style token stream into a filter expression tree.
	 *
	 * Resets the parser state on every call so the arena is reused.
	 *
	 * @param argc  Number of tokens.
	 * @param argv  Token array.
	 * @return @c true if the stream parses to a single complete expression.
	 */
	bool Parse(int argc, const char* const* argv)
	{
		fTokens = argv;
		fTokenCount = argc;
		fTokenIndex = 0;
		fFilterCount = 0;

		TraceFilter* filter = _ParseExpression();
		return fTokenIndex == fTokenCount && filter != NULL;
	}

	/**
	 * @brief Return the root filter from the most recent successful parse.
	 * @return Pointer to the root filter (first arena slot).
	 */
	TraceFilter* Filter()
	{
		return &fFilters[0];
	}

private:
	/**
	 * @brief Recursive-descent parse of one expression starting at the cursor.
	 *
	 * Tokens are consumed in prefix order. On syntax error or arena exhaustion
	 * returns @c NULL; partial filters remain in the arena but are ignored
	 * because @c fFilterCount is not rolled back beyond what was written.
	 *
	 * @return Pointer to the parsed subtree root, or @c NULL on failure.
	 */
	TraceFilter* _ParseExpression()
	{
		const char* token = _NextToken();
		if (!token) {
			// unexpected end of expression
			return NULL;
		}

		if (fFilterCount == MAX_FILTERS) {
			// too many filters
			return NULL;
		}

		if (token[0] == '#') {
			TraceFilter* filter = new(&fFilters[fFilterCount++])
				PatternTraceFilter;
			filter->fString = token + 1;
			return filter;
		} else if (token[0] == 'd' && token[1] == '#') {
			TraceFilter* filter = new(&fFilters[fFilterCount++])
				DecimalPatternTraceFilter;
			filter->fValue = parse_expression(token + 2);
			return filter;
		} else if (token[0] == 'x' && token[1] == '#') {
			TraceFilter* filter = new(&fFilters[fFilterCount++])
				HexPatternTraceFilter;
			filter->fValue = parse_expression(token + 2);
			return filter;
		} else if (token[0] == 's' && token[1] == '#') {
			TraceFilter* filter = new(&fFilters[fFilterCount++])
				StringPatternTraceFilter;
			filter->fValue = parse_expression(token + 2);
			return filter;
		} else if (strcmp(token, "not") == 0) {
			TraceFilter* filter = new(&fFilters[fFilterCount++]) NotTraceFilter;
			if ((filter->fSubFilters.first = _ParseExpression()) != NULL)
				return filter;
			delete(filter);
			return NULL;
		} else if (strcmp(token, "and") == 0) {
			TraceFilter* filter = new(&fFilters[fFilterCount++]) AndTraceFilter;
			if ((filter->fSubFilters.first = _ParseExpression()) != NULL
				&& (filter->fSubFilters.second = _ParseExpression()) != NULL) {
				return filter;
			}
			delete(filter);
			return NULL;
		} else if (strcmp(token, "or") == 0) {
			TraceFilter* filter = new(&fFilters[fFilterCount++]) OrTraceFilter;
			if ((filter->fSubFilters.first = _ParseExpression()) != NULL
				&& (filter->fSubFilters.second = _ParseExpression()) != NULL) {
				return filter;
			}
			delete(filter);
			return NULL;
		} else if (strcmp(token, "thread") == 0) {
			const char* arg = _NextToken();
			if (arg == NULL) {
				// unexpected end of expression
				return NULL;
			}

			TraceFilter* filter = new(&fFilters[fFilterCount++])
				ThreadTraceFilter;
			filter->fThread = strtol(arg, NULL, 0);
			return filter;
		} else if (strcmp(token, "team") == 0) {
			const char* arg = _NextToken();
			if (arg == NULL) {
				// unexpected end of expression
				return NULL;
			}

			TraceFilter* filter = new(&fFilters[fFilterCount++])
				TeamTraceFilter;
			filter->fTeam = strtol(arg, NULL, 0);
			return filter;
		} else {
			// invalid token
			return NULL;
		}
	}

	/**
	 * @brief Peek at the token most recently consumed.
	 * @return The current token or @c NULL before the first advance.
	 */
	const char* _CurrentToken() const
	{
		if (fTokenIndex >= 1 && fTokenIndex <= fTokenCount)
			return fTokens[fTokenIndex - 1];
		return NULL;
	}

	/**
	 * @brief Advance the cursor and return the next token.
	 * @return Next token or @c NULL at end-of-stream.
	 */
	const char* _NextToken()
	{
		if (fTokenIndex >= fTokenCount)
			return NULL;
		return fTokens[fTokenIndex++];
	}

private:
	enum { MAX_FILTERS = 32 };

	const char* const*			fTokens;
	int							fTokenCount;
	int							fTokenIndex;
	TraceFilter					fFilters[MAX_FILTERS];
	int							fFilterCount;

	static TraceFilterParser	sParser;
};


TraceFilterParser TraceFilterParser::sParser;


//	#pragma mark -


#if ENABLE_TRACING


/**
 * @brief Advance the iterator to the next non-buffer entry.
 *
 * Hides @c BUFFER_ENTRY auxiliary allocations so callers only see user-
 * meaningful TraceEntry instances. Not thread-safe; callers serialize via
 * @c lock_tracing_buffer / the KDL single-threaded context.
 *
 * @return Pointer to the new current entry, or @c NULL past the end.
 */
TraceEntry*
TraceEntryIterator::Next()
{
	if (fIndex == 0) {
		fEntry = _NextNonBufferEntry(sTracingMetaData->FirstEntry());
		fIndex = 1;
	} else if (fEntry != NULL) {
		fEntry = _NextNonBufferEntry(sTracingMetaData->NextEntry(fEntry));
		fIndex++;
	}

	return Current();
}


/**
 * @brief Step the iterator back to the previous non-buffer entry.
 *
 * When positioned one-past-the-end, wraps to @c AfterLastEntry first.
 *
 * @return Pointer to the new current entry, or @c NULL before the start.
 */
TraceEntry*
TraceEntryIterator::Previous()
{
	if (fIndex == (int32)sTracingMetaData->Entries() + 1)
		fEntry = sTracingMetaData->AfterLastEntry();

	if (fEntry != NULL) {
		fEntry = _PreviousNonBufferEntry(
			sTracingMetaData->PreviousEntry(fEntry));
		fIndex--;
	}

	return Current();
}


/**
 * @brief Reposition the iterator to the given 1-based Pascal-style index.
 *
 * Selects the shorter of forward/backward traversal from the current position
 * to amortise cost. An out-of-range index parks the iterator at the nearest
 * end sentinel (0 or Entries()+1).
 *
 * @param index  Target 1-based entry index.
 * @return Pointer to the entry at @p index, or @c NULL if out of range.
 */
TraceEntry*
TraceEntryIterator::MoveTo(int32 index)
{
	if (index == fIndex)
		return Current();

	if (index <= 0 || index > (int32)sTracingMetaData->Entries()) {
		fIndex = (index <= 0 ? 0 : sTracingMetaData->Entries() + 1);
		fEntry = NULL;
		return NULL;
	}

	// get the shortest iteration path
	int32 distance = index - fIndex;
	int32 direction = distance < 0 ? -1 : 1;
	distance *= direction;

	if (index < distance) {
		distance = index;
		direction = 1;
		fEntry = NULL;
		fIndex = 0;
	}
	if ((int32)sTracingMetaData->Entries() + 1 - fIndex < distance) {
		distance = sTracingMetaData->Entries() + 1 - fIndex;
		direction = -1;
		fEntry = NULL;
		fIndex = sTracingMetaData->Entries() + 1;
	}

	// iterate to the index
	if (direction < 0) {
		while (fIndex != index)
			Previous();
	} else {
		while (fIndex != index)
			Next();
	}

	return Current();
}


/**
 * @brief Walk forwards until a non-auxiliary entry is found.
 *
 * Internal helper used by @c Next; used also after wrap-around.
 *
 * @param entry  Starting raw entry pointer (may be @c NULL).
 * @return First non-BUFFER_ENTRY at or after @p entry, or @c NULL.
 */
trace_entry*
TraceEntryIterator::_NextNonBufferEntry(trace_entry* entry)
{
	while (entry != NULL && (entry->flags & BUFFER_ENTRY) != 0)
		entry = sTracingMetaData->NextEntry(entry);

	return entry;
}


/**
 * @brief Walk backwards until a non-auxiliary entry is found.
 *
 * @param entry  Starting raw entry pointer (may be @c NULL).
 * @return First non-BUFFER_ENTRY at or before @p entry, or @c NULL.
 */
trace_entry*
TraceEntryIterator::_PreviousNonBufferEntry(trace_entry* entry)
{
	while (entry != NULL && (entry->flags & BUFFER_ENTRY) != 0)
		entry = sTracingMetaData->PreviousEntry(entry);

	return entry;
}


/**
 * @brief Implementation backing the @c traced KDL command.
 *
 * Parses options, the index window, and the optional filter expression; drives
 * a @c TraceEntryIterator across the selected range and prints matching
 * entries. State is retained between invocations to support the "forward" and
 * "backward" continuation verbs. Runs only from KDL, so no explicit locking
 * of the tracing buffer is needed here.
 *
 * @param argc            Command argument count.
 * @param argv            Command argument array.
 * @param wrapperFilter   Optional outer filter layered over the parsed one.
 * @return @c B_KDEBUG_CONT when the command should be re-invokable via empty
 *         line, or 0 otherwise.
 */
int
dump_tracing_internal(int argc, char** argv, WrapperTraceFilter* wrapperFilter)
{
	int argi = 1;

	// variables in which we store our state to be continuable
	static int32 _previousCount = 0;
	static bool _previousHasFilter = false;
	static bool _previousPrintStackTrace = false;
	static int32 _previousMaxToCheck = 0;
	static int32 _previousFirstChecked = 1;
	static int32 _previousLastChecked = -1;
	static int32 _previousDirection = 1;
	static uint32 _previousEntriesEver = 0;
	static uint32 _previousEntries = 0;
	static uint32 _previousOutputFlags = 0;
	static TraceEntryIterator iterator;

	uint32 entriesEver = sTracingMetaData->EntriesEver();

	// Note: start and index are Pascal-like indices (i.e. in [1, Entries()]).
	int32 start = 0;	// special index: print the last count entries
	int32 count = 0;
	int32 maxToCheck = 0;
	int32 cont = 0;

	bool hasFilter = false;
	bool printStackTrace = false;

	uint32 outputFlags = 0;
	while (argi < argc) {
		if (strcmp(argv[argi], "--difftime") == 0) {
			outputFlags |= TRACE_OUTPUT_DIFF_TIME;
			argi++;
		} else if (strcmp(argv[argi], "--printteam") == 0) {
			outputFlags |= TRACE_OUTPUT_TEAM_ID;
			argi++;
		} else if (strcmp(argv[argi], "--stacktrace") == 0) {
			printStackTrace = true;
			argi++;
		} else
			break;
	}

	if (argi < argc) {
		if (strcmp(argv[argi], "forward") == 0) {
			cont = 1;
			argi++;
		} else if (strcmp(argv[argi], "backward") == 0) {
			cont = -1;
			argi++;
		}
	} else
		cont = _previousDirection;

	if (cont != 0) {
		if (argi < argc) {
			print_debugger_command_usage(argv[0]);
			return 0;
		}
		if (entriesEver == 0 || entriesEver != _previousEntriesEver
			|| sTracingMetaData->Entries() != _previousEntries) {
			kprintf("Can't continue iteration. \"%s\" has not been invoked "
				"before, or there were new entries written since the last "
				"invocation.\n", argv[0]);
			return 0;
		}
	}

	// get start, count, maxToCheck
	int32* params[3] = { &start, &count, &maxToCheck };
	for (int i = 0; i < 3 && !hasFilter && argi < argc; i++) {
		if (strcmp(argv[argi], "filter") == 0) {
			hasFilter = true;
			argi++;
		} else if (argv[argi][0] == '#') {
			hasFilter = true;
		} else {
			*params[i] = parse_expression(argv[argi]);
			argi++;
		}
	}

	// filter specification
	if (argi < argc) {
		hasFilter = true;
		if (strcmp(argv[argi], "filter") == 0)
			argi++;

		if (!TraceFilterParser::Default()->Parse(argc - argi, argv + argi)) {
			print_debugger_command_usage(argv[0]);
			return 0;
		}
	}

	int32 direction;
	int32 firstToCheck;
	int32 lastToCheck;

	if (cont != 0) {
		// get values from the previous iteration
		direction = cont;
		count = _previousCount;
		maxToCheck = _previousMaxToCheck;
		hasFilter = _previousHasFilter;
		outputFlags = _previousOutputFlags;
		printStackTrace = _previousPrintStackTrace;

		if (direction < 0)
			start = _previousFirstChecked - 1;
		else
			start = _previousLastChecked + 1;
	} else {
		// defaults for count and maxToCheck
		if (count == 0)
			count = 30;
		if (maxToCheck == 0 || !hasFilter)
			maxToCheck = count;
		else if (maxToCheck < 0)
			maxToCheck = sTracingMetaData->Entries();

		// determine iteration direction
		direction = (start <= 0 || count < 0 ? -1 : 1);

		// validate count and maxToCheck
		if (count < 0)
			count = -count;
		if (maxToCheck < 0)
			maxToCheck = -maxToCheck;
		if (maxToCheck > (int32)sTracingMetaData->Entries())
			maxToCheck = sTracingMetaData->Entries();
		if (count > maxToCheck)
			count = maxToCheck;

		// validate start
		if (start <= 0 || start > (int32)sTracingMetaData->Entries())
			start = max_c(1, sTracingMetaData->Entries());
	}

	if (direction < 0) {
		firstToCheck = max_c(1, start - maxToCheck + 1);
		lastToCheck = start;
	} else {
		firstToCheck = start;
		lastToCheck = min_c((int32)sTracingMetaData->Entries(),
			start + maxToCheck - 1);
	}

	// reset the iterator, if something changed in the meantime
	if (entriesEver == 0 || entriesEver != _previousEntriesEver
		|| sTracingMetaData->Entries() != _previousEntries) {
		iterator.Reset();
	}

	LazyTraceOutput out(sTracingMetaData->TraceOutputBuffer(),
		kTraceOutputBufferSize, outputFlags);

	bool markedMatching = false;
	int32 firstToDump = firstToCheck;
	int32 lastToDump = lastToCheck;

	TraceFilter* filter = NULL;
	if (hasFilter)
		filter = TraceFilterParser::Default()->Filter();

	if (wrapperFilter != NULL) {
		wrapperFilter->Init(filter, direction, cont != 0);
		filter = wrapperFilter;
	}

	if (direction < 0 && filter && lastToCheck - firstToCheck >= count) {
		// iteration direction is backwards
		markedMatching = true;

		// From the last entry to check iterate backwards to check filter
		// matches.
		int32 matching = 0;

		// move to the entry after the last entry to check
		iterator.MoveTo(lastToCheck + 1);

		// iterate backwards
		firstToDump = -1;
		lastToDump = -1;
		while (iterator.Index() > firstToCheck) {
			TraceEntry* entry = iterator.Previous();
			if ((entry->Flags() & ENTRY_INITIALIZED) != 0) {
				out.Clear();
				if (filter->Filter(entry, out)) {
					entry->ToTraceEntry()->flags |= FILTER_MATCH;
					if (lastToDump == -1)
						lastToDump = iterator.Index();
					firstToDump = iterator.Index();

					matching++;
					if (matching >= count)
						break;
				} else
					entry->ToTraceEntry()->flags &= ~FILTER_MATCH;
			}
		}

		firstToCheck = iterator.Index();

		// iterate to the previous entry, so that the next loop starts at the
		// right one
		iterator.Previous();
	}

	out.SetLastEntryTime(0);

	// set the iterator to the entry before the first one to dump
	iterator.MoveTo(firstToDump - 1);

	// dump the entries matching the filter in the range
	// [firstToDump, lastToDump]
	int32 dumped = 0;

	while (TraceEntry* entry = iterator.Next()) {
		int32 index = iterator.Index();
		if (index < firstToDump)
			continue;
		if (index > lastToDump || dumped >= count) {
			if (direction > 0)
				lastToCheck = index - 1;
			break;
		}

		if ((entry->Flags() & ENTRY_INITIALIZED) != 0) {
			out.Clear();
			if (filter &&  (markedMatching
					? (entry->Flags() & FILTER_MATCH) == 0
					: !filter->Filter(entry, out))) {
				continue;
			}

			// don't print trailing new line
			const char* dump = out.DumpEntry(entry);
			int len = strlen(dump);
			if (len > 0 && dump[len - 1] == '\n')
				len--;

			kprintf("%5" B_PRId32 ". %.*s\n", index, len, dump);

			if (printStackTrace) {
				out.Clear();
				entry->DumpStackTrace(out);
				if (out.Size() > 0)
					kputs(out.Buffer());
			}
		} else if (!filter)
			kprintf("%5" B_PRId32 ". ** uninitialized entry **\n", index);

		dumped++;
	}

	kprintf("printed %" B_PRId32 " entries within range %" B_PRId32 " to %"
		B_PRId32 " (%" B_PRId32 " of %" B_PRId32 " total, %" B_PRId32 " ever)\n",
		dumped, firstToCheck, lastToCheck, lastToCheck - firstToCheck + 1,
		sTracingMetaData->Entries(), entriesEver);

	// store iteration state
	_previousCount = count;
	_previousMaxToCheck = maxToCheck;
	_previousHasFilter = hasFilter;
	_previousPrintStackTrace = printStackTrace;
	_previousFirstChecked = firstToCheck;
	_previousLastChecked = lastToCheck;
	_previousDirection = direction;
	_previousEntriesEver = entriesEver;
	_previousEntries = sTracingMetaData->Entries();
	_previousOutputFlags = outputFlags;

	return cont != 0 ? B_KDEBUG_CONT : 0;
}


/**
 * @brief Thin KDL shim forwarding to @c dump_tracing_internal with no wrapper.
 *
 * Installed as the handler for the @c traced debugger command.
 *
 * @param argc  Command argument count.
 * @param argv  Command argument array.
 * @return Forwarded return value from @c dump_tracing_internal.
 */
static int
dump_tracing_command(int argc, char** argv)
{
	return dump_tracing_internal(argc, argv, NULL);
}


#endif	// ENABLE_TRACING


/**
 * @brief Bump-allocate a raw auxiliary data buffer in the tracing ring.
 *
 * The returned storage has the same wrap-reclaimed lifetime as the owning
 * trace entry. Callers use this to attach variable-length payloads such as
 * string copies, memcpy-based captures, or stack traces. Safe under
 * interrupts; tagged @c BUFFER_ENTRY so iterators skip over it.
 *
 * @param size  Number of payload bytes required.
 * @return Pointer to payload storage, or @c NULL on failure.
 */
extern "C" uint8*
alloc_tracing_buffer(size_t size)
{
#if	ENABLE_TRACING
	trace_entry* entry = sTracingMetaData->AllocateEntry(
		size + sizeof(trace_entry), BUFFER_ENTRY);
	if (entry == NULL)
		return NULL;

	return (uint8*)(entry + 1);
#else
	return NULL;
#endif
}


/**
 * @brief Copy @p size bytes from @p source into a freshly-allocated buffer.
 *
 * Used to snapshot ephemeral structures into the tracing ring. If @p user is
 * set, a @c user_memcpy is issued so page faults on bad user addresses are
 * handled gracefully. The allocation itself is still subject to ring-buffer
 * wrap-around reclamation.
 *
 * @param source  Source pointer (kernel or user per @p user).
 * @param size    Bytes to copy.
 * @param user    If @c true, treat @p source as a user-space address.
 * @return Pointer to the snapshot, or @c NULL on allocation/copy failure.
 */
uint8*
alloc_tracing_buffer_memcpy(const void* source, size_t size, bool user)
{
	if (user && !IS_USER_ADDRESS(source))
		return NULL;

	uint8* buffer = alloc_tracing_buffer(size);
	if (buffer == NULL)
		return NULL;

	if (user) {
		if (user_memcpy(buffer, source, size) != B_OK)
			return NULL;
	} else
		memcpy(buffer, source, size);

	return buffer;
}


/**
 * @brief Copy a NUL-terminated string into a tracing-ring buffer.
 *
 * Length is clamped to @p maxSize. For user-space sources, @c user_strlcpy is
 * used (after an IS_USER_ADDRESS check) to avoid faulting in kernel code.
 *
 * @param source   Source string (kernel or user per @p user).
 * @param maxSize  Maximum including terminator.
 * @param user     If @c true, treat @p source as a user-space address.
 * @return Pointer to the copied string, or @c NULL on failure.
 */
char*
alloc_tracing_buffer_strcpy(const char* source, size_t maxSize, bool user)
{
	if (source == NULL || maxSize == 0)
		return NULL;

	if (user && !IS_USER_ADDRESS(source))
		return NULL;

	// limit maxSize to the actual source string len
	if (user) {
		ssize_t size = user_strlcpy(NULL, source, 0);
			// there's no user_strnlen()
		if (size < 0)
			return 0;
		maxSize = min_c(maxSize, (size_t)size + 1);
	} else
		maxSize = strnlen(source, maxSize - 1) + 1;

	char* buffer = (char*)alloc_tracing_buffer(maxSize);
	if (buffer == NULL)
		return NULL;

	if (user) {
		if (user_strlcpy(buffer, source, maxSize) < B_OK)
			return NULL;
	} else
		strlcpy(buffer, source, maxSize);

	return buffer;
}


/**
 * @brief Capture a stack trace and store it in the tracing ring buffer.
 *
 * When interrupts are disabled (as in many ISR/panic paths) user-mode frames
 * are suppressed regardless of @p kernelOnly because the fault handler cannot
 * recover from a bad user address safely.
 *
 * @param maxCount    Maximum frames to record.
 * @param skipFrames  Top frames to skip (the helper itself is auto-added).
 * @param kernelOnly  Restrict capture to kernel-mode frames when @c true.
 * @return Pointer to the stored stack trace, or @c NULL on failure.
 */
tracing_stack_trace*
capture_tracing_stack_trace(int32 maxCount, int32 skipFrames, bool kernelOnly)
{
#if	ENABLE_TRACING
	// page_fault_exception() doesn't allow us to gracefully handle a bad
	// address in the stack trace, if interrupts are disabled, so we always
	// restrict the stack traces to the kernel only in this case. A bad address
	// in the kernel stack trace would still cause a panic(), but this is
	// probably even desired.
	if (!are_interrupts_enabled())
		kernelOnly = true;

	tracing_stack_trace* stackTrace
		= (tracing_stack_trace*)alloc_tracing_buffer(
			sizeof(tracing_stack_trace) + maxCount * sizeof(addr_t));

	if (stackTrace != NULL) {
		stackTrace->depth = arch_debug_get_stack_trace(
			stackTrace->return_addresses, maxCount, 0, skipFrames + 1,
			STACK_TRACE_KERNEL | (kernelOnly ? 0 : STACK_TRACE_USER));
	}

	return stackTrace;
#else
	return NULL;
#endif
}


/**
 * @brief Locate the first frame outside a set of address ranges.
 *
 * Used by higher-level tracers to skip boilerplate wrappers and report the
 * actual caller. Pure function; no side effects.
 *
 * @param stackTrace         Captured stack trace.
 * @param excludeRanges      Flat array of [start, end) pairs to exclude.
 * @param excludeRangeCount  Number of pairs in @p excludeRanges.
 * @return Return address of the first accepted frame, or 0 if none match.
 */
addr_t
tracing_find_caller_in_stack_trace(struct tracing_stack_trace* stackTrace,
	const addr_t excludeRanges[], uint32 excludeRangeCount)
{
	for (int32 i = 0; i < stackTrace->depth; i++) {
		addr_t returnAddress = stackTrace->return_addresses[i];

		bool inRange = false;
		for (uint32 j = 0; j < excludeRangeCount; j++) {
			if (returnAddress >= excludeRanges[j * 2 + 0]
				&& returnAddress < excludeRanges[j * 2 + 1]) {
				inRange = true;
				break;
			}
		}

		if (!inRange)
			return returnAddress;
	}

	return 0;
}


/**
 * @brief Print a captured stack trace to the kernel debugger output.
 *
 * Convenience wrapper around @c print_stack_trace that emits via @c kprintf,
 * suitable for direct use from panic or debugger paths.
 *
 * @param stackTrace  Captured stack trace, or @c NULL.
 */
void
tracing_print_stack_trace(struct tracing_stack_trace* stackTrace)
{
#if ENABLE_TRACING
	print_stack_trace(stackTrace, kprintf);
#endif
}


/**
 * @brief Public entry point for dumping the tracing buffer with a wrapper.
 *
 * Present so subsystems that define their own @c WrapperTraceFilter can reuse
 * the standard @c traced rendering pipeline. Returns 0 when tracing is
 * compile-time disabled.
 *
 * @param argc           Command argument count.
 * @param argv           Command argument array.
 * @param wrapperFilter  Outer filter layered over any parsed filter.
 * @return Forwarded return from @c dump_tracing_internal.
 */
int
dump_tracing(int argc, char** argv, WrapperTraceFilter* wrapperFilter)
{
#if	ENABLE_TRACING
	return dump_tracing_internal(argc, argv, wrapperFilter);
#else
	return 0;
#endif
}


/**
 * @brief Validate that @p candidate still refers to a live trace entry.
 *
 * Checks that the pointer lies within the live buffer region and, when
 * @p entryTime is non-negative, confirms the timestamp matches (protecting
 * against buffer reuse since the pointer was captured).
 *
 * @param candidate  Pointer to test.
 * @param entryTime  Expected timestamp, or a negative value to skip matching.
 * @return @c true if the pointer is still valid.
 */
bool
tracing_is_entry_valid(AbstractTraceEntry* candidate, bigtime_t entryTime)
{
#if ENABLE_TRACING
	if (!sTracingMetaData->IsInBuffer(candidate, sizeof(*candidate)))
		return false;

	if (entryTime < 0)
		return true;

	TraceEntryIterator iterator;
	while (TraceEntry* entry = iterator.Next()) {
		AbstractTraceEntry* abstract = dynamic_cast<AbstractTraceEntry*>(entry);
		if (abstract == NULL)
			continue;

		if (abstract != candidate && abstract->Time() > entryTime)
			return false;

		return candidate->Time() == entryTime;
	}
#endif

	return false;
}


/**
 * @brief Acquire the global tracing spinlock, serializing with writers.
 *
 * Callers use this around multi-step reads of the buffer (e.g. analysis tools
 * consuming the scheduler trace). While held, every invocation of
 * @c AllocateEntry on every CPU will block, so the hold time should be kept
 * short. Compiles to a no-op when tracing is disabled.
 */
void
lock_tracing_buffer()
{
#if ENABLE_TRACING
	sTracingMetaData->Lock();
#endif
}


/**
 * @brief Release the tracing spinlock previously taken by @c lock_tracing_buffer.
 *
 * Must be balanced with @c lock_tracing_buffer on the same CPU and with
 * interrupts in the same state. No-op when tracing is compile-time disabled.
 */
void
unlock_tracing_buffer()
{
#if ENABLE_TRACING
	sTracingMetaData->Unlock();
#endif
}


/**
 * @brief Initialise the tracing subsystem during kernel boot.
 *
 * Creates or recovers the metadata area, posts the start-of-log marker, and
 * registers the @c traced command with the kernel debugger. The command help
 * text documents the filter mini-language understood by the parser.
 *
 * @return @c B_OK on success, or a propagated error from area/metadata
 *         creation (the fallback metadata is still usable in that case).
 */
extern "C" status_t
tracing_init(void)
{
#if	ENABLE_TRACING
	status_t result = TracingMetaData::Create(sTracingMetaData);
	if (result != B_OK) {
		memset(&sFallbackTracingMetaData, 0, sizeof(sFallbackTracingMetaData));
		sTracingMetaData = &sFallbackTracingMetaData;
		return result;
	}

	new(nothrow) TracingLogStartEntry();

	add_debugger_command_etc("traced", &dump_tracing_command,
		"Dump recorded trace entries",
		"[ --printteam ] [ --difftime ] [ --stacktrace ] "
			"(\"forward\" | \"backward\") "
			"| ([ <start> [ <count> [ <range> ] ] ] "
			"[ #<pattern> | (\"filter\" <filter>) ])\n"
		"Prints recorded trace entries. If \"backward\" or \"forward\" is\n"
		"specified, the command continues where the previous invocation left\n"
		"off, i.e. printing the previous respectively next entries (as many\n"
		"as printed before). In this case the command is continuable, that is\n"
		"afterwards entering an empty line in the debugger will reinvoke it.\n"
		"If no arguments are given, the command continues in the direction\n"
		"of the last invocation.\n"
		"--printteam  - enables printing the entries' team IDs.\n"
		"--difftime   - print difference times for all but the first entry.\n"
		"--stacktrace - print stack traces for entries that captured one.\n"
		"  <start>    - The base index of the entries to print. Depending on\n"
		"               whether the iteration direction is forward or\n"
		"               backward this will be the first or last entry printed\n"
		"               (potentially, if a filter is specified). The index of\n"
		"               the first entry in the trace buffer is 1. If 0 is\n"
		"               specified, the last <count> recorded entries are\n"
		"               printed (iteration direction is backward). Defaults \n"
		"               to 0.\n"
		"  <count>    - The number of entries to be printed. Defaults to 30.\n"
		"               If negative, the -<count> entries before and\n"
		"               including <start> will be printed.\n"
		"  <range>    - Only relevant if a filter is specified. Specifies the\n"
		"               number of entries to be filtered -- depending on the\n"
		"               iteration direction the entries before or after\n"
		"               <start>. If more than <count> entries match the\n"
		"               filter, only the first (forward) or last (backward)\n"
		"               <count> matching entries will be printed. If 0 is\n"
		"               specified <range> will be set to <count>. If -1,\n"
		"               <range> will be set to the number of recorded\n"
		"               entries.\n"
		"  <pattern>  - If specified only entries containing this string are\n"
		"               printed.\n"
		"  <filter>   - If specified only entries matching this filter\n"
		"               expression are printed. The expression can consist of\n"
		"               prefix operators \"not\", \"and\", \"or\", and\n"
		"               filters \"'thread' <thread>\" (matching entries\n"
		"               with the given thread ID), \"'team' <team>\"\n"
						"(matching entries with the given team ID), and\n"
		"               \"#<pattern>\" (matching entries containing the given\n"
		"               string).\n", 0);
#endif	// ENABLE_TRACING
	return B_OK;
}


/**
 * @brief @c printf-style kernel-side tracing primitive.
 *
 * Formats the message onto a 256-byte stack buffer and allocates a
 * @c KernelTraceEntry for it. Safe from any kernel context except panic-time
 * (where heap/allocator state may already be inconsistent).
 *
 * @param format  @c printf format string.
 */
void
ktrace_printf(const char *format, ...)
{
#if	ENABLE_TRACING
	va_list list;
	va_start(list, format);

	char buffer[256];
	vsnprintf(buffer, sizeof(buffer), format, list);

	va_end(list);

	new(nothrow) KernelTraceEntry(buffer);
#endif	// ENABLE_TRACING
}


/**
 * @brief Syscall implementation for user-space @c ktrace_output.
 *
 * Creates a @c UserTraceEntry that safely copies the user-supplied string into
 * the tracing buffer.
 *
 * @param message  User-space NUL-terminated message pointer.
 */
void
_user_ktrace_output(const char *message)
{
#if	ENABLE_TRACING
	new(nothrow) UserTraceEntry(message);
#endif	// ENABLE_TRACING
}

