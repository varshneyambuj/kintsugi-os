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
 *   Copyright 2016, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file core_dump.cpp
 * @brief Kernel-side ELF core-dump writer for killed or crashing teams.
 *
 * Produces a standards-conforming ET_CORE ELF file at a caller-specified
 * filesystem path. The dumper walks the crashing team's address space (user
 * memory) to enumerate areas, collects thread state (registers, priority,
 * stack bounds) with the appropriate locks held, and resolves mapped files
 * and loaded images. It then emits an ELF header, a PT_NOTE segment
 * containing team/area/image/thread notes (plus symbol tables for the
 * commpage), and PT_LOAD segments with the contents of every area copied
 * page-by-page from user memory.
 */


#include <core_dump.h>

#include <errno.h>
#include <string.h>

#include <algorithm>
#include <new>

#include <BeBuild.h>
#include <ByteOrder.h>

#include <AutoDeleter.h>

#include <commpage.h>
#include <condition_variable.h>
#include <elf.h>
#include <kimage.h>
#include <ksignal.h>
#include <team.h>
#include <thread.h>
#include <user_debugger.h>
#include <util/AutoLock.h>
#include <util/ThreadAutoLock.h>
#include <util/DoublyLinkedList.h>
#include <vm/vm.h>
#include <vm/VMArea.h>
#include <vm/VMCache.h>

#include "../cache/vnode_store.h"
#include "../vm/VMAddressSpaceLocking.h"


//#define TRACE_CORE_DUMP
#ifdef TRACE_CORE_DUMP
#	define TRACE(...) dprintf(__VA_ARGS__)
#else
#	define TRACE(...) do {} while (false)
#endif


namespace {


static const size_t kBufferSize = 1024 * 1024;
static const char* const kCoreNote = ELF_NOTE_CORE;
static const char* const kHaikuNote = ELF_NOTE_HAIKU;


struct Allocator {
	/**
	 * @brief Construct an empty two-arena allocator with no backing storage.
	 */
	Allocator()
		:
		fAligned(NULL),
		fStrings(NULL),
		fAlignedCapacity(0),
		fStringCapacity(0),
		fAlignedSize(0),
		fStringSize(0)
	{
	}

	/**
	 * @brief Free the single backing block that holds both arenas.
	 */
	~Allocator()
	{
		free(fAligned);
	}

	/**
	 * @brief Report whether any allocation during the previous pass exceeded
	 *        the currently reserved capacity.
	 *
	 * Used by two-pass consumers that grow the backing block until every
	 * allocation fits.
	 *
	 * @return @c true if the arenas overflowed and Reallocate() is required.
	 */
	bool HasMissingAllocations() const
	{
		return fAlignedSize > fAlignedCapacity || fStringSize > fStringCapacity;
	}

	/**
	 * @brief Resize the backing block to match the sizes observed in the last
	 *        pass, discarding any content and resetting the arenas.
	 *
	 * @return @c true on success; @c false if allocation failed.
	 */
	bool Reallocate()
	{
		free(fAligned);

		fAlignedCapacity = fAlignedSize;
		fStringCapacity = fStringSize;
		fAlignedSize = 0;
		fStringSize = 0;

		fAligned = (uint8*)malloc(fAlignedCapacity + fStringCapacity);
		if (fAligned == NULL)
			return false;
		fStrings = (char*)(fAligned + fAlignedCapacity);

		return true;
	}

	/**
	 * @brief Bump-allocate a block in the 8-byte-aligned arena.
	 *
	 * Always advances fAlignedSize so the first pass can be run against zero
	 * capacity to compute the required size.
	 *
	 * @param size  Requested size in bytes.
	 * @return Pointer into the arena on success, @c NULL if the capacity was
	 *         exceeded (the caller should Reallocate() and retry).
	 */
	void* AllocateAligned(size_t size)
	{
		size_t offset = fAlignedSize;
		fAlignedSize += (size + 7) / 8 * 8;
		if (fAlignedSize <= fAlignedCapacity)
			return fAligned + offset;
		return NULL;
	}

	/**
	 * @brief Bump-allocate a NUL-terminated string slot of @p length + 1 bytes.
	 *
	 * @param length  Character length of the string (excluding terminator).
	 * @return Pointer into the string arena or @c NULL when capacity is
	 *         exceeded.
	 */
	char* AllocateString(size_t length)
	{
		size_t offset = fStringSize;
		fStringSize += length + 1;
		if (fStringSize <= fStringCapacity)
			return fStrings + offset;
		return NULL;
	}

	/**
	 * @brief Construct a new @c Type instance using placement new on top of an
	 *        AllocateAligned() slot.
	 *
	 * @return Newly constructed object or @c NULL on capacity exhaustion.
	 */
	template <typename Type>
	Type* New()
	{
		void* buffer = AllocateAligned(sizeof(Type));
		if (buffer == NULL)
			return NULL;
		return new(buffer) Type;
	}

	/**
	 * @brief Copy a NUL-terminated string into the arena's string pool.
	 *
	 * @param string  Source string; may be @c NULL in which case @c NULL is
	 *                returned.
	 * @return Pointer to the copied string, or @c NULL on exhaustion or when
	 *         @p string is @c NULL.
	 */
	char* DuplicateString(const char* string)
	{
		if (string == NULL)
			return NULL;
		char* newString = AllocateString(strlen(string));
		if (newString != NULL)
			strcpy(newString, string);
		return newString;
	}

private:
	uint8*	fAligned;
	char*	fStrings;
	size_t	fAlignedCapacity;
	size_t	fStringCapacity;
	size_t	fAlignedSize;
	size_t	fStringSize;
};


struct TeamInfo : team_info {
};


struct ThreadState : DoublyLinkedListLinkImpl<ThreadState> {
	/**
	 * @brief Construct an empty per-thread snapshot slot.
	 */
	ThreadState()
		:
		fThread(NULL),
		fComplete(false)
	{
	}

	/**
	 * @brief Release the held Thread reference (if any).
	 */
	~ThreadState()
	{
		SetThread(NULL);
	}

	/**
	 * @brief Factory creating a new ThreadState on the heap.
	 *
	 * @return New ThreadState or @c NULL on allocation failure.
	 */
	static ThreadState* Create()
	{
		ThreadState* state = new(std::nothrow) ThreadState;
		if (state == NULL)
			return NULL;
		return state;
	}

	/**
	 * @brief Accessor for the bound Thread, held as a refcounted pointer.
	 *
	 * @return The bound Thread, or @c NULL if none is set.
	 */
	Thread* GetThread() const
	{
		return fThread;
	}

	/**
	 * @brief Replace the bound Thread, managing reference counts.
	 *
	 * Releases the reference to any currently held thread and acquires one
	 * for @p thread. Passing @c NULL just releases.
	 *
	 * @param thread  New thread to track; may be @c NULL.
	 */
	void SetThread(Thread* thread)
	{
		if (fThread != NULL)
			fThread->ReleaseReference();

		fThread = thread;

		if (fThread != NULL)
			fThread->AcquireReference();
	}

	/**
	 * @brief Capture the thread's state, priority, stack bounds, name, and
	 *        CPU register set.
	 *
	 * Must be invoked with both the thread lock and the scheduler lock held.
	 * Reads the Thread struct (kernel memory) and calls
	 * arch_get_thread_debug_cpu_state() to fetch the saved user-mode register
	 * set; on failure the CPU-state buffer is zeroed.
	 */
	void GetState()
	{
		fState = fThread->state;
		fPriority = fThread->priority;
		fStackBase = fThread->user_stack_base;
		fStackEnd = fStackBase + fThread->user_stack_size;
		strlcpy(fName, fThread->name, sizeof(fName));
		if (arch_get_thread_debug_cpu_state(fThread, &fCpuState) != B_OK)
			memset(&fCpuState, 0, sizeof(fCpuState));
	}

	/**
	 * @brief Whether GetState() has successfully captured this thread.
	 */
	bool IsComplete() const
	{
		return fComplete;
	}

	/**
	 * @brief Mark or unmark the snapshot as complete.
	 *
	 * @param complete  New completion flag value.
	 */
	void SetComplete(bool complete)
	{
		fComplete = complete;
	}

	// The accessors below are trivial getters for the fields captured by
	// GetState(); no per-method Doxygen because each one simply returns the
	// identically-named member.
	int32 State() const
	{
		return fState;
	}

	int32 Priority() const
	{
		return fPriority;
	}

	addr_t StackBase() const
	{
		return fStackBase;
	}

	addr_t StackEnd() const
	{
		return fStackEnd;
	}

	const char* Name() const
	{
		return fName;
	}

	const debug_cpu_state* CpuState() const
	{
		return &fCpuState;
	}

private:
	Thread*			fThread;
	int32			fState;
	int32			fPriority;
	addr_t			fStackBase;
	addr_t			fStackEnd;
	char			fName[B_OS_NAME_LENGTH];
	debug_cpu_state	fCpuState;
	bool			fComplete;
};


typedef DoublyLinkedList<ThreadState> ThreadStateList;


struct ImageInfo : DoublyLinkedListLinkImpl<ImageInfo> {
	/**
	 * @brief Snapshot all relevant fields from a kernel image struct.
	 *
	 * Reads kernel memory only (the @c struct @c image lives in kernel space)
	 * and duplicates the image name. When the image is the commpage, also
	 * pulls its symbol/string tables via elf_read_kernel_image_symbols() so
	 * userland debuggers can resolve commpage addresses.
	 *
	 * @param image  Kernel image descriptor to snapshot.
	 */
	ImageInfo(struct image* image)
		:
		fId(image->info.basic_info.id),
		fType(image->info.basic_info.type),
		fDeviceId(image->info.basic_info.device),
		fNodeId(image->info.basic_info.node),
		fName(strdup(image->info.basic_info.name)),
		fInitRoutine((addr_t)image->info.basic_info.init_routine),
		fTermRoutine((addr_t)image->info.basic_info.term_routine),
		fText((addr_t)image->info.basic_info.text),
		fData((addr_t)image->info.basic_info.data),
		fTextSize(image->info.basic_info.text_size),
		fDataSize(image->info.basic_info.data_size),
		fTextDelta(image->info.text_delta),
		fSymbolTable((addr_t)image->info.symbol_table),
		fSymbolHash((addr_t)image->info.symbol_hash),
		fStringTable((addr_t)image->info.string_table),
		fSymbolTableData(NULL),
		fStringTableData(NULL),
		fSymbolCount(0),
		fStringTableSize(0)
	{
		if (fName != NULL && strcmp(fName, "commpage") == 0)
			_GetCommpageSymbols();
	}

	/**
	 * @brief Release the duplicated name and any commpage symbol buffers.
	 */
	~ImageInfo()
	{
		free(fName);
		_FreeSymbolData();
	}

	/**
	 * @brief Heap-allocate an ImageInfo snapshot, failing cleanly if the name
	 *        could not be duplicated.
	 *
	 * @param image  Kernel image to snapshot.
	 * @return New ImageInfo or @c NULL on allocation failure.
	 */
	static ImageInfo* Create(struct image* image)
	{
		ImageInfo* imageInfo = new(std::nothrow) ImageInfo(image);
		if (imageInfo == NULL || imageInfo->fName == NULL) {
			delete imageInfo;
			return NULL;
		}

		return imageInfo;
	}

	// The accessors below are trivial snapshot getters for the fields
	// populated in the constructor; no per-method Doxygen because each one
	// simply returns the identically-named member.
	image_id Id() const
	{
		return fId;
	}

	image_type Type() const
	{
		return fType;
	}

	const char* Name() const
	{
		return fName;
	}

	dev_t DeviceId() const
	{
		return fDeviceId;
	}

	ino_t NodeId() const
	{
		return fNodeId;
	}

	addr_t InitRoutine() const
	{
		return fInitRoutine;
	}

	addr_t TermRoutine() const
	{
		return fTermRoutine;
	}

	addr_t TextBase() const
	{
		return fText;
	}

	size_t TextSize() const
	{
		return fTextSize;
	}

	ssize_t TextDelta() const
	{
		return fTextDelta;
	}

	addr_t DataBase() const
	{
		return fData;
	}

	size_t DataSize() const
	{
		return fDataSize;
	}

	addr_t SymbolTable() const
	{
		return fSymbolTable;
	}

	addr_t SymbolHash() const
	{
		return fSymbolHash;
	}

	addr_t StringTable() const
	{
		return fStringTable;
	}

	elf_sym* SymbolTableData() const
	{
		return fSymbolTableData;
	}

	char* StringTableData() const
	{
		return fStringTableData;
	}

	uint32 SymbolCount() const
	{
		return fSymbolCount;
	}

	size_t StringTableSize() const
	{
		return fStringTableSize;
	}

private:
	/**
	 * @brief Fetch symbol and string tables for the commpage image.
	 *
	 * Two-pass call: first sizing, then filling heap buffers. The commpage
	 * is a kernel-managed image, so the symbols are read out of kernel
	 * memory via elf_read_kernel_image_symbols(). On any failure the buffers
	 * are released and the image continues without symbols.
	 */
	void _GetCommpageSymbols()
	{
		image_id commpageId = get_commpage_image();

		// get the size of the tables
		int32 symbolCount = 0;
		size_t stringTableSize = 0;
		status_t error = elf_read_kernel_image_symbols(commpageId, NULL,
			&symbolCount, NULL, &stringTableSize,
			NULL, true);
		if (error != B_OK)
			return;
		if (symbolCount == 0 || stringTableSize == 0)
			return;

		// allocate the tables
		fSymbolTableData = (elf_sym*)malloc(sizeof(elf_sym) * symbolCount);
		fStringTableData = (char*)malloc(stringTableSize);
		if (fSymbolTableData == NULL || fStringTableData == NULL) {
			_FreeSymbolData();
			return;
		}

		fSymbolCount = symbolCount;
		fStringTableSize = stringTableSize;

		// get the data
		error = elf_read_kernel_image_symbols(commpageId,
			fSymbolTableData, &symbolCount, fStringTableData, &stringTableSize,
			NULL, true);
		if (error != B_OK)
			_FreeSymbolData();
	}

	/**
	 * @brief Release and null out the kernel-owned symbol/string buffers.
	 */
	void _FreeSymbolData()
	{
		free(fSymbolTableData);
		free(fStringTableData);

		fSymbolTableData = NULL;
		fStringTableData = NULL;
		fSymbolCount = 0;
		fStringTableSize = 0;
	}

private:
	image_id	fId;
	image_type	fType;
	dev_t		fDeviceId;
	ino_t		fNodeId;
	char*		fName;
	addr_t		fInitRoutine;
	addr_t		fTermRoutine;
	addr_t		fText;
	addr_t		fData;
	size_t		fTextSize;
	size_t		fDataSize;
	ssize_t		fTextDelta;
	addr_t		fSymbolTable;
	addr_t		fSymbolHash;
	addr_t		fStringTable;
	// for commpage image
	elf_sym*	fSymbolTableData;
	char*		fStringTableData;
	uint32		fSymbolCount;
	size_t		fStringTableSize;
};


typedef DoublyLinkedList<ImageInfo> ImageInfoList;


struct AreaInfo : DoublyLinkedListLinkImpl<AreaInfo> {
	/**
	 * @brief Allocate and populate a per-area snapshot out of the supplied
	 *        bump allocator.
	 *
	 * Reads the VMArea struct (kernel memory) to snapshot id, base, size,
	 * protection, and cache offset. The name is copied into the allocator's
	 * string arena; returning a partially-initialized struct with a @c NULL
	 * name is expected when the allocator is undersized, prompting the
	 * caller to grow capacity and retry.
	 *
	 * @param allocator  Bump allocator for the struct and its name copy.
	 * @param area       VMArea being snapshotted.
	 * @param ramSize    Resident page count * B_PAGE_SIZE.
	 * @param deviceId   Device id of the mapped file (-1 if anonymous).
	 * @param nodeId     Inode id of the mapped file (-1 if anonymous).
	 * @return Pointer into the arena or @c NULL if the aligned arena was
	 *         exhausted.
	 */
	static AreaInfo* Create(Allocator& allocator, VMArea* area, size_t ramSize,
		dev_t deviceId, ino_t nodeId)
	{
		AreaInfo* areaInfo = allocator.New<AreaInfo>();
		const char* name = allocator.DuplicateString(area->name);

		if (areaInfo != NULL) {
			areaInfo->fId = area->id;
			areaInfo->fName = name;
			areaInfo->fBase = area->Base();
			areaInfo->fSize = area->Size();
			areaInfo->fLock = B_FULL_LOCK;
			areaInfo->fProtection = area->protection;
			areaInfo->fRamSize = ramSize;
			areaInfo->fDeviceId = deviceId;
			areaInfo->fNodeId = nodeId;
			areaInfo->fCacheOffset = area->cache_offset;
			areaInfo->fImageInfo = NULL;
		}

		return areaInfo;
	}

	// The accessors below are trivial snapshot getters for the fields
	// populated by Create(); no per-method Doxygen because each one simply
	// returns the identically-named member.
	area_id Id() const
	{
		return fId;
	}

	const char* Name() const
	{
		return fName;
	}

	addr_t Base() const
	{
		return fBase;
	}

	size_t Size() const
	{
		return fSize;
	}

	uint32 Lock() const
	{
		return fLock;
	}

	uint32 Protection() const
	{
		return fProtection;
	}

	size_t RamSize() const
	{
		return fRamSize;
	}

	off_t CacheOffset() const
	{
		return fCacheOffset;
	}

	dev_t DeviceId() const
	{
		return fDeviceId;
	}

	ino_t NodeId() const
	{
		return fNodeId;
	}

	ImageInfo* GetImageInfo() const
	{
		return fImageInfo;
	}

	/**
	 * @brief Link an ImageInfo whose device/inode matches this area.
	 *
	 * @param imageInfo  ImageInfo to associate, or @c NULL to clear.
	 */
	void SetImageInfo(ImageInfo* imageInfo)
	{
		fImageInfo = imageInfo;
	}

private:
	area_id		fId;
	const char*	fName;
	addr_t		fBase;
	size_t		fSize;
	uint32		fLock;
	uint32		fProtection;
	size_t		fRamSize;
	dev_t		fDeviceId;
	ino_t		fNodeId;
	off_t		fCacheOffset;
	ImageInfo*	fImageInfo;
};


typedef DoublyLinkedList<AreaInfo> AreaInfoList;


struct BufferedFile {
	/**
	 * @brief Construct an uninitialized BufferedFile.
	 *
	 * The initial Status() is @c B_NO_INIT until Init() is called.
	 */
	BufferedFile()
		:
		fFd(-1),
		fBuffer(NULL),
		fCapacity(0),
		fOffset(0),
		fBuffered(0),
		fStatus(B_NO_INIT)
	{
	}

	/**
	 * @brief Close the underlying fd and free the write buffer.
	 */
	~BufferedFile()
	{
		if (fFd >= 0)
			close(fFd);

		free(fBuffer);
	}

	/**
	 * @brief Allocate the write buffer and create the destination file.
	 *
	 * Opens @p path with O_WRONLY|O_CREAT|O_EXCL so an existing dump cannot
	 * be silently overwritten, with mode S_IRUSR.
	 *
	 * @param path  Absolute filesystem path of the core file to create.
	 * @return B_OK on success; B_NO_MEMORY on allocation failure; @c errno
	 *         mapped value on open() failure.
	 */
	status_t Init(const char* path)
	{
		fCapacity = kBufferSize;
		fBuffer = (uint8*)malloc(fCapacity);
		if (fBuffer == NULL)
			return B_NO_MEMORY;

		fFd = open(path, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR);
		if (fFd < 0)
			return errno;

		fStatus = B_OK;
		return B_OK;
	}

	/**
	 * @brief Sticky error status captured on the first failure.
	 */
	status_t Status() const
	{
		return fStatus;
	}

	/**
	 * @brief Logical offset of the first byte beyond the buffered region.
	 *
	 * @return Sum of the flushed file offset and the in-memory buffered size.
	 */
	off_t EndOffset() const
	{
		return fOffset + (off_t)fBuffered;
	}

	/**
	 * @brief Write any pending buffered data to disk via pwrite().
	 *
	 * Short writes are treated as B_IO_ERROR and set the sticky status.
	 *
	 * @return B_OK on success; the captured error on failure.
	 */
	status_t Flush()
	{
		if (fStatus != B_OK)
			return fStatus;

		if (fBuffered == 0)
			return B_OK;

		ssize_t written = pwrite(fFd, fBuffer, fBuffered, fOffset);
		if (written < 0)
			return fStatus = errno;
		if ((size_t)written != fBuffered)
			return fStatus = B_IO_ERROR;

		fOffset += (off_t)fBuffered;
		fBuffered = 0;
		return B_OK;
	}

	/**
	 * @brief Reposition the logical write cursor to @p offset.
	 *
	 * If the destination differs from the current buffered tail, the buffer
	 * is flushed first.
	 *
	 * @param offset  Absolute file offset for subsequent writes.
	 * @return B_OK on success; a sticky error otherwise.
	 */
	status_t Seek(off_t offset)
	{
		if (fStatus != B_OK)
			return fStatus;

		if (fBuffered == 0) {
			fOffset = offset;
		} else if (offset != fOffset + (off_t)fBuffered) {
			status_t error = Flush();
			if (error != B_OK)
				return fStatus = error;
			fOffset = offset;
		}

		return B_OK;
	}

	/**
	 * @brief Append @p size bytes of @p data to the buffer, flushing when it
	 *        fills up.
	 *
	 * Reads from kernel memory (the caller-supplied @p data pointer); user
	 * memory is handled separately via WriteUserArea().
	 *
	 * @param data  Kernel-side pointer to the bytes to append.
	 * @param size  Number of bytes to append.
	 * @return B_OK on success; a sticky error otherwise.
	 */
	status_t Write(const void* data, size_t size)
	{
		if (fStatus != B_OK)
			return fStatus;

		if (size == 0)
			return B_OK;

		while (size > 0) {
			size_t toWrite = std::min(size, fCapacity - fBuffered);
			if (toWrite == 0) {
				status_t error = Flush();
				if (error != B_OK)
					return fStatus = error;
				continue;
			}

			memcpy(fBuffer + fBuffered, data, toWrite);
			fBuffered += toWrite;
			size -= toWrite;
		}

		return B_OK;
	}

	/**
	 * @brief Convenience overload that writes a POD value.
	 *
	 * @param data  Value whose bytes are streamed into the buffer.
	 * @return B_OK on success; a sticky error otherwise.
	 */
	template<typename Data>
	status_t Write(const Data& data)
	{
		return Write(&data, sizeof(data));
	}

	/**
	 * @brief Seek to @p offset and write @p size bytes from @p data.
	 *
	 * @param offset  Absolute file offset.
	 * @param data    Kernel-side source pointer.
	 * @param size    Number of bytes to write.
	 * @return B_OK on success; a sticky error otherwise.
	 */
	status_t WriteAt(off_t offset, const void* data, size_t size)
	{
		if (Seek(offset) != B_OK)
			return fStatus;

		return Write(data, size);
	}

	/**
	 * @brief Stream a range of user memory into the file page by page.
	 *
	 * Walks user memory at @p base using user_memcpy(), zero-filling any
	 * unreadable pages (e.g. swapped-out or unmapped) so the resulting
	 * PT_LOAD segment has a consistent size. Flushes the buffer whenever a
	 * new page would not fit.
	 *
	 * @param base  User-space start address; rounded down by caller.
	 * @param size  Length in bytes, truncated to a page multiple.
	 * @return B_OK on success; a sticky error otherwise.
	 */
	status_t WriteUserArea(addr_t base, size_t size)
	{
		uint8* data = (uint8*)base;
		size = size / B_PAGE_SIZE * B_PAGE_SIZE;

		// copy the area page-wise into the buffer, flushing when necessary
		while (size > 0) {
			if (fBuffered + B_PAGE_SIZE > fCapacity) {
				status_t error = Flush();
				if (error != B_OK)
					return error;
			}

			if (user_memcpy(fBuffer + fBuffered, data, B_PAGE_SIZE) != B_OK)
				memset(fBuffer + fBuffered, 0, B_PAGE_SIZE);

			fBuffered += B_PAGE_SIZE;
			data += B_PAGE_SIZE;
			size -= B_PAGE_SIZE;
		}

		return B_OK;
	}

private:
	int			fFd;
	uint8*		fBuffer;
	size_t		fCapacity;
	off_t		fOffset;
	size_t		fBuffered;
	status_t	fStatus;
};


struct DummyWriter {
	/**
	 * @brief Construct a zero-length dummy writer used for size-computation
	 *        passes.
	 */
	DummyWriter()
		:
		fWritten(0)
	{
	}

	/**
	 * @brief Always B_OK: the dummy writer cannot fail.
	 */
	status_t Status() const
	{
		return B_OK;
	}

	/**
	 * @brief Cumulative byte count accumulated by Write() calls.
	 */
	size_t BytesWritten() const
	{
		return fWritten;
	}

	/**
	 * @brief Accumulate @p size bytes without actually writing anything.
	 *
	 * @param data  Ignored.
	 * @param size  Bytes to add to the running counter.
	 * @return Always B_OK.
	 */
	status_t Write(const void* data, size_t size)
	{
		fWritten += size;
		return B_OK;
	}

	/**
	 * @brief POD convenience overload mirroring BufferedFile::Write.
	 *
	 * @param data  Value whose size contributes to the counter.
	 * @return Always B_OK.
	 */
	template<typename Data>
	status_t Write(const Data& data)
	{
		return Write(&data, sizeof(data));
	}

private:
	size_t	fWritten;
};


struct CoreDumper {
	/**
	 * @brief Initialize a dumper bound to the calling thread's team.
	 *
	 * Captures the current Thread and Team pointers, initializes the
	 * condition variable that threads trapped in core_dump_trap_thread()
	 * wait on, and leaves the rest of the state empty for Dump().
	 */
	CoreDumper()
		:
		fCurrentThread(thread_get_current_thread()),
		fTeam(fCurrentThread->team),
		fFile(),
		fThreadCount(0),
		fThreadStates(),
		fPreAllocatedThreadStates(),
		fAreaInfoAllocator(),
		fAreaInfos(),
		fImageInfos(),
		fThreadBlockCondition()
	{
		fThreadBlockCondition.Init(this, "core dump");
	}

	/**
	 * @brief Release every ThreadState and ImageInfo owned by the dumper.
	 *
	 * AreaInfo instances live inside the bump allocator and are freed with
	 * it. ThreadStates release their Thread references in their destructors.
	 */
	~CoreDumper()
	{
		while (ThreadState* state = fThreadStates.RemoveHead())
			delete state;
		while (ThreadState* state = fPreAllocatedThreadStates.RemoveHead())
			delete state;
		while (ImageInfo* info = fImageInfos.RemoveHead())
			delete info;
	}

	/**
	 * @brief Write the team's core dump to @p path and optionally kill it.
	 *
	 * Marks the team as dumping core (TEAM_FLAG_DUMP_CORE), parks peer
	 * threads on the core-dump condition variable so their user-mode state
	 * is quiescent, delegates to _Dump() to produce the ELF file, then
	 * tears the flags down and wakes any trapped threads. When @p killTeam
	 * is true, kill_team() is invoked once the dump has been produced.
	 *
	 * @param path      Absolute filesystem path; must start with '/'.
	 * @param killTeam  Whether to signal-kill the team after dumping.
	 * @return B_OK on success, B_BAD_VALUE for a relative path, B_BUSY if
	 *         another core dump is in flight for the same team, or the
	 *         error produced by _Dump().
	 */
	status_t Dump(const char* path, bool killTeam)
	{
		// gcc thinks fTeam may be null in atomic_or
        // and warn which causes error on some configs
		if (fTeam == NULL)
			return B_ERROR;

		// the path must be absolute
		if (path[0] != '/')
			return B_BAD_VALUE;

		AutoLocker<Team> teamLocker(fTeam);

		// indicate that we're dumping core
		if ((atomic_or(&fTeam->flags, TEAM_FLAG_DUMP_CORE)
				& TEAM_FLAG_DUMP_CORE) != 0) {
			return B_BUSY;
		}

		fTeam->SetCoreDumpCondition(&fThreadBlockCondition);

		int32 threadCount = _SetThreadsCoreDumpFlag(true);

		teamLocker.Unlock();

		// write the core file
		status_t error = _Dump(path, threadCount);

		// send kill signal, if requested
		if (killTeam)
			kill_team(fTeam->id);

		// clean up the team state and wake up waiting threads
		teamLocker.Lock();

		fTeam->SetCoreDumpCondition(NULL);

		atomic_and(&fTeam->flags, ~(int32)TEAM_FLAG_DUMP_CORE);

		_SetThreadsCoreDumpFlag(false);

		fThreadBlockCondition.NotifyAll();

		return error;
	}

private:
	/**
	 * @brief Produce the ELF core file, assuming Dump() has already parked
	 *        the team's threads.
	 *
	 * Fetches team info, pre-allocates ThreadState slots, snapshots thread
	 * states (user register state) and team areas + images, opens the
	 * destination file, and writes the ELF header, PT_NOTE segment, program
	 * headers, and PT_LOAD area data. The ELF header is rewritten at the end
	 * so any offsets derived from note-segment placement are finalized.
	 *
	 * @param path         Destination path (already validated by Dump()).
	 * @param threadCount  Approximate initial thread count hint.
	 * @return B_OK on success; first error propagated from the steps above.
	 */
	status_t _Dump(const char* path, int32 threadCount)
	{
		status_t error = _GetTeamInfo();
		if (error != B_OK)
			return error;

		// pre-allocate a list of thread states
		if (!_AllocateThreadStates(threadCount))
			return B_NO_MEMORY;

		// collect the threads states
		_GetThreadStates();

		// collect the other team information
		if (!_GetAreaInfos() || !_GetImageInfos())
			return B_NO_MEMORY;

		// open the file
		error = fFile.Init(path);
		if (error != B_OK)
			return error;

		_PrepareCoreFileInfo();

		// write ELF header
		error = _WriteElfHeader();
		if (error != B_OK)
			return error;

		// write note segment
		error = _WriteNotes();
		if (error != B_OK)
			return error;

		size_t notesEndOffset = (size_t)fFile.EndOffset();
		fNoteSegmentSize = notesEndOffset - fNoteSegmentOffset;
		fFirstAreaSegmentOffset = (notesEndOffset + B_PAGE_SIZE - 1)
			/ B_PAGE_SIZE * B_PAGE_SIZE;

		error = _WriteProgramHeaders();
		if (error != B_OK)
			return error;

		// write area segments
		error = _WriteAreaSegments();
		if (error != B_OK)
			return error;

		return _WriteElfHeader();
	}

	/**
	 * @brief Set or clear THREAD_FLAGS_TRAP_FOR_CORE_DUMP on every thread of
	 *        the team and return the count.
	 *
	 * Runs with the team lock held and walks the thread list (kernel
	 * structures). Threads that observe the flag on their way back to user
	 * space end up parked in core_dump_trap_thread().
	 *
	 * @param setFlag  @c true to arm the trap; @c false to disarm.
	 * @return Number of threads traversed.
	 */
	int32 _SetThreadsCoreDumpFlag(bool setFlag)
	{
		int32 count = 0;

		for (Thread* thread = fTeam->thread_list.First(); thread != NULL;
				thread = fTeam->thread_list.GetNext(thread)) {
			count++;
			if (setFlag) {
				atomic_or(&thread->flags, THREAD_FLAGS_TRAP_FOR_CORE_DUMP);
			} else {
				atomic_and(&thread->flags,
					~(int32)THREAD_FLAGS_TRAP_FOR_CORE_DUMP);
			}
		}

		return count;
	}

	/**
	 * @brief Populate @c fTeamInfo with the team's team_info metadata.
	 *
	 * @return B_OK on success; error from get_team_info() otherwise.
	 */
	status_t _GetTeamInfo()
	{
		return get_team_info(fTeam->id, &fTeamInfo);
	}

	/**
	 * @brief Allocate one ThreadState per thread, coping with races where
	 *        threads join mid-allocation.
	 *
	 * Pre-allocates @p count slots, then walks the team's thread list (with
	 * the team lock held) binding slots to threads; if more threads than
	 * expected are observed, grows the pre-allocation list and retries.
	 *
	 * @param count  Initial thread-count estimate.
	 * @return @c true on success, @c false on allocation failure.
	 */
	bool _AllocateThreadStates(int32 count)
	{
		if (!_PreAllocateThreadStates(count))
			return false;

		TeamLocker teamLocker(fTeam);

		for (;;) {
			fThreadCount = 0;
			int32 missing = 0;

			for (Thread* thread = fTeam->thread_list.First(); thread != NULL;
					thread = fTeam->thread_list.GetNext(thread)) {
				fThreadCount++;
				ThreadState* state = fPreAllocatedThreadStates.RemoveHead();
				if (state != NULL) {
					state->SetThread(thread);
					fThreadStates.Insert(state);
				} else
					missing++;
			}

			if (missing == 0)
				break;

			teamLocker.Unlock();

			fPreAllocatedThreadStates.TakeFrom(&fThreadStates);
			if (!_PreAllocateThreadStates(missing))
				return false;

			teamLocker.Lock();
		}

		return true;
	}

	/**
	 * @brief Append @p count heap-allocated ThreadState objects to the
	 *        pre-allocation free list.
	 *
	 * @param count  Number of fresh ThreadStates to create.
	 * @return @c true on success, @c false on allocation failure.
	 */
	bool _PreAllocateThreadStates(int32 count)
	{
		for (int32 i = 0; i < count; i++) {
			ThreadState* state = ThreadState::Create();
			if (state == NULL)
				return false;
			fPreAllocatedThreadStates.Insert(state);
		}

		return true;
	}

	/**
	 * @brief Snapshot each tracked thread's state by polling until every
	 *        thread is off-CPU.
	 *
	 * Reads kernel memory (Thread struct, scheduler state). For each
	 * ThreadState, takes the thread lock and scheduler lock, drops threads
	 * that migrated to the kernel team during death, and calls GetState()
	 * on the rest. If a thread other than the current one is still running,
	 * loops with a 10ms snooze until it becomes quiescent.
	 */
	void _GetThreadStates()
	{
		for (;;) {
			bool missing = false;
			for (ThreadStateList::Iterator it = fThreadStates.GetIterator();
					ThreadState* state = it.Next();) {
				if (state->IsComplete())
					continue;

				Thread* thread = state->GetThread();
				AutoLocker<Thread> threadLocker(thread);
				if (thread->team != fTeam) {
					// no longer in our team -- i.e. dying and transferred to
					// the kernel team
					threadLocker.Unlock();
					it.Remove();
					delete state;
					fThreadCount--;
					continue;
				}

				InterruptsSpinLocker schedulerLocker(&thread->scheduler_lock);
				if (thread != fCurrentThread
						&& thread->state == B_THREAD_RUNNING) {
					missing = true;
					continue;
				}

				state->GetState();
				state->SetComplete(true);
			}

			if (!missing)
				break;

			// We still haven't got a state for all threads. Wait a moment and
			// try again.
			snooze(10000);
		}
	}

	/**
	 * @brief Walk the team's address space (user memory mappings) and
	 *        snapshot every VMArea.
	 *
	 * Holds the address-space read lock and the per-area cache lock chain
	 * so it can reach the root VMCache and extract device/inode ids for
	 * mapped files. Produces AreaInfo records out of @c fAreaInfoAllocator
	 * and grows the allocator on overflow, re-running the scan.
	 *
	 * @return @c true on success, @c false on allocator exhaustion.
	 */
	bool _GetAreaInfos()
	{
		for (;;) {
			AddressSpaceReadLocker addressSpaceLocker(fTeam->address_space,
				true);

			for (VMAddressSpace::AreaIterator it
						= addressSpaceLocker.AddressSpace()->GetAreaIterator();
					VMArea* area = it.Next();) {

				VMCache* cache = vm_area_get_locked_cache(area);
				size_t ramSize = (size_t)cache->page_count * B_PAGE_SIZE;
					// simplified, but what the kernel uses as well ATM

				// iterate to the root cache and, if it is a mapped file, get
				// the file's node_ref
				while (VMCache* source = cache->source) {
					source->Lock();
					source->AcquireRefLocked();
					cache->ReleaseRefAndUnlock();
					cache = source;
				}

				dev_t deviceId = -1;
				ino_t nodeId = -1;
				if (cache->type == CACHE_TYPE_VNODE) {
					VMVnodeCache* vnodeCache = (VMVnodeCache*)cache;
					deviceId = vnodeCache->DeviceId();
					nodeId = vnodeCache->InodeId();
				}

				cache->ReleaseRefAndUnlock();

				AreaInfo* areaInfo = AreaInfo::Create(fAreaInfoAllocator, area,
					ramSize, deviceId, nodeId);

				if (areaInfo != NULL)
					fAreaInfos.Insert(areaInfo);
			}

			addressSpaceLocker.Unlock();

			if (!fAreaInfoAllocator.HasMissingAllocations())
				return true;

			if (!fAreaInfoAllocator.Reallocate())
				return false;
		}
	}

	/**
	 * @brief Snapshot every image loaded in the team via
	 *        image_iterate_through_team_images().
	 *
	 * Reads kernel image descriptors; the iterator itself handles locking.
	 *
	 * @return @c true if all images were captured, @c false on allocation
	 *         failure (detected via the non-@c NULL iterator return).
	 */
	bool _GetImageInfos()
	{
		return image_iterate_through_team_images(fTeam->id,
			&_GetImageInfoCallback, this) == NULL;
	}

	/**
	 * @brief Trampoline from the image-iterator C API to the member hook.
	 *
	 * @param image   Kernel image descriptor being visited.
	 * @param cookie  CoreDumper instance pointer.
	 * @return @c true to stop iteration (e.g. on allocation failure);
	 *         @c false to continue.
	 */
	static bool _GetImageInfoCallback(struct image* image, void* cookie)
	{
		return ((CoreDumper*)cookie)->_GetImageInfo(image);
	}

	/**
	 * @brief Snapshot a single image into @c fImageInfos.
	 *
	 * @param image  Kernel image descriptor to snapshot.
	 * @return @c true on allocation failure (stops iteration),
	 *         @c false on success.
	 */
	bool _GetImageInfo(struct image* image)
	{
		ImageInfo* info = ImageInfo::Create(image);
		if (info == NULL)
			return true;

		fImageInfos.Insert(info);
		return false;
	}

	/**
	 * @brief Cross-reference areas with images and compute static core-file
	 *        layout offsets.
	 *
	 * For each area backed by a file, looks up the matching ImageInfo by
	 * (device id, inode id) and records the link. Counts areas, images,
	 * mapped files, and computes the program-header table and notes-segment
	 * offsets.
	 */
	void _PrepareCoreFileInfo()
	{
		// assign image infos to area infos where possible
		fAreaCount = 0;
		fMappedFilesCount = 0;
		for (AreaInfoList::Iterator it = fAreaInfos.GetIterator();
				AreaInfo* areaInfo = it.Next();) {
			fAreaCount++;
			dev_t deviceId = areaInfo->DeviceId();
			if (deviceId < 0)
				continue;
			ImageInfo* imageInfo = _FindImageInfo(deviceId, areaInfo->NodeId());
			if (imageInfo != NULL) {
				areaInfo->SetImageInfo(imageInfo);
				fMappedFilesCount++;
			}
		}

		fImageCount = fImageInfos.Count();
		fSegmentCount = 1 + fAreaCount;
		fProgramHeadersOffset = sizeof(elf_ehdr);
		fNoteSegmentOffset = fProgramHeadersOffset
			+ sizeof(elf_phdr) * fSegmentCount;
	}

	/**
	 * @brief Linear search for an ImageInfo matching a (deviceId, nodeId)
	 *        pair.
	 *
	 * @param deviceId  Device id to match.
	 * @param nodeId    Inode id to match.
	 * @return Matching ImageInfo or @c NULL when none is found.
	 */
	ImageInfo* _FindImageInfo(dev_t deviceId, ino_t nodeId) const
	{
		for (ImageInfoList::ConstIterator it = fImageInfos.GetIterator();
				ImageInfo* info = it.Next();) {
			if (info->DeviceId() == deviceId && info->NodeId() == nodeId)
				return info;
		}

		return NULL;
	}

	/**
	 * @brief Write the ELF header at file offset zero.
	 *
	 * Selects ELFCLASS32/64, endianness, and e_machine based on build-time
	 * macros, and sets @c e_type to ET_CORE with the correct program-header
	 * count. Called twice by _Dump(): once as a placeholder, once at the
	 * end to finalize any fields that depend on the notes segment.
	 *
	 * @return B_OK on success; sticky file error otherwise.
	 */
	status_t _WriteElfHeader()
	{
		elf_ehdr header;
		memset(&header, 0, sizeof(header));

		// e_ident
		header.e_ident[EI_MAG0] = ELFMAG[0];
		header.e_ident[EI_MAG1] = ELFMAG[1];
		header.e_ident[EI_MAG2] = ELFMAG[2];
		header.e_ident[EI_MAG3] = ELFMAG[3];
#ifdef B_HAIKU_64_BIT
		header.e_ident[EI_CLASS] = ELFCLASS64;
#else
		header.e_ident[EI_CLASS] = ELFCLASS32;
#endif
#if B_HOST_IS_LENDIAN
		header.e_ident[EI_DATA] = ELFDATA2LSB;
#else
		header.e_ident[EI_DATA] = ELFDATA2MSB;
#endif
		header.e_ident[EI_VERSION] = EV_CURRENT;

		// e_type
		header.e_type = ET_CORE;

		// e_machine
#if defined(__HAIKU_ARCH_X86)
		header.e_machine = EM_386;
#elif defined(__HAIKU_ARCH_X86_64)
		header.e_machine = EM_X86_64;
#elif defined(__HAIKU_ARCH_PPC)
		header.e_machine = EM_PPC64;
#elif defined(__HAIKU_ARCH_M68K)
		header.e_machine = EM_68K;
#elif defined(__HAIKU_ARCH_MIPSEL)
		header.e_machine = EM_MIPS;
#elif defined(__HAIKU_ARCH_ARM)
		header.e_machine = EM_ARM;
#elif defined(__HAIKU_ARCH_ARM64)
		header.e_machine = EM_AARCH64;
#elif defined(__HAIKU_ARCH_SPARC)
		header.e_machine = EM_SPARCV9;
#elif defined(__HAIKU_ARCH_RISCV64)
		header.e_machine = EM_RISCV;
#else
#	error Unsupported architecture!
#endif

		header.e_version = EV_CURRENT;
		header.e_entry = 0;
		header.e_phoff = sizeof(header);
		header.e_shoff = 0;
		header.e_flags = 0;
		header.e_ehsize = sizeof(header);
		header.e_phentsize = sizeof(elf_phdr);
		header.e_phnum = fSegmentCount;
		header.e_shentsize = sizeof(elf_shdr);
		header.e_shnum = 0;
		header.e_shstrndx = SHN_UNDEF;

		return fFile.WriteAt(0, &header, sizeof(header));
	}

	/**
	 * @brief Emit the program-header table: one PT_NOTE plus one PT_LOAD
	 *        per area.
	 *
	 * Maps each area's B_{READ,WRITE,EXECUTE}_AREA flags to the matching
	 * PF_ flags and records file offsets for later payload writes.
	 *
	 * @return B_OK on success; sticky file error otherwise.
	 */
	status_t _WriteProgramHeaders()
	{
		fFile.Seek(fProgramHeadersOffset);

		// write the header for the notes segment
		elf_phdr header;
		memset(&header, 0, sizeof(header));
		header.p_type = PT_NOTE;
		header.p_flags = 0;
		header.p_offset = fNoteSegmentOffset;
		header.p_vaddr = 0;
		header.p_paddr = 0;
		header.p_filesz = fNoteSegmentSize;
		header.p_memsz = 0;
		header.p_align = 0;
		fFile.Write(header);

		// write the headers for the area segments
		size_t segmentOffset = fFirstAreaSegmentOffset;
		for (AreaInfoList::Iterator it = fAreaInfos.GetIterator();
				AreaInfo* areaInfo = it.Next();) {
			memset(&header, 0, sizeof(header));
			header.p_type = PT_LOAD;
			header.p_flags = 0;
			uint32 protection = areaInfo->Protection();
			if ((protection & B_READ_AREA) != 0)
				header.p_flags |= PF_READ;
			if ((protection & B_WRITE_AREA) != 0)
				header.p_flags |= PF_WRITE;
			if ((protection & B_EXECUTE_AREA) != 0)
				header.p_flags |= PF_EXECUTE;
			header.p_offset = segmentOffset;
			header.p_vaddr = areaInfo->Base();
			header.p_paddr = 0;
			header.p_filesz = areaInfo->Size();
			header.p_memsz = areaInfo->Size();
			header.p_align = 0;
			fFile.Write(header);

			segmentOffset += areaInfo->Size();
		}

		return fFile.Status();
	}

	/**
	 * @brief Stream each area's contents from user memory into the file as
	 *        its PT_LOAD payload.
	 *
	 * Walks user memory page-by-page via BufferedFile::WriteUserArea, which
	 * zero-fills any unreadable pages.
	 *
	 * @return B_OK on success; sticky file error otherwise.
	 */
	status_t _WriteAreaSegments()
	{
		fFile.Seek(fFirstAreaSegmentOffset);

		for (AreaInfoList::Iterator it = fAreaInfos.GetIterator();
				AreaInfo* areaInfo = it.Next();) {
			status_t error = fFile.WriteUserArea(areaInfo->Base(),
				areaInfo->Size());
			if (error != B_OK)
				return error;
		}

		return fFile.Status();
	}

	/**
	 * @brief Emit the PT_NOTE segment: files, team, areas, images, symbols,
	 *        threads.
	 *
	 * Writes purely kernel-side data accumulated during the snapshot phase;
	 * does not touch user memory.
	 *
	 * @return B_OK on success; first error from the note writers otherwise.
	 */
	status_t _WriteNotes()
	{
		status_t error = fFile.Seek((off_t)fNoteSegmentOffset);
		if (error != B_OK)
			return error;

		error = _WriteFilesNote();
		if (error != B_OK)
			return error;

		error = _WriteTeamNote();
		if (error != B_OK)
			return error;

		error = _WriteAreasNote();
		if (error != B_OK)
			return error;

		error = _WriteImagesNote();
		if (error != B_OK)
			return error;

		error = _WriteImageSymbolsNotes();
		if (error != B_OK)
			return error;

		error = _WriteThreadsNote();
		if (error != B_OK)
			return error;

		return B_OK;
	}

	/**
	 * @brief Stream the NT_TEAM note payload through @p writer.
	 *
	 * Writes an elf_note_team header followed by the team's argv string.
	 * Used with both DummyWriter (to size the note) and BufferedFile (to
	 * emit it). Reads kernel-side team_info fields only.
	 *
	 * @param writer  Writer sink (DummyWriter or BufferedFile).
	 */
	template<typename Writer>
	void _WriteTeamNote(Writer& writer)
	{
		elf_note_team note;
		memset(&note, 0, sizeof(note));
		note.nt_id = fTeamInfo.team;
		note.nt_uid = fTeamInfo.uid;
		note.nt_gid = fTeamInfo.gid;
		writer.Write((uint32)sizeof(note));
		writer.Write(note);

		// write args
		const char* args = fTeamInfo.args;
		writer.Write(args, strlen(args) + 1);
	}

	/**
	 * @brief Emit the full NT_TEAM note: header, payload, padding.
	 *
	 * Performs a size pass with DummyWriter, writes a note header, streams
	 * the payload to @c fFile, then pads to 4 bytes.
	 *
	 * @return B_OK on success; sticky file error otherwise.
	 */
	status_t _WriteTeamNote()
	{
		// determine needed size for the note's data
		DummyWriter dummyWriter;
		_WriteTeamNote(dummyWriter);
		size_t dataSize = dummyWriter.BytesWritten();

		// write the note header
		_WriteNoteHeader(kHaikuNote, NT_TEAM, dataSize);

		// write the note data
		_WriteTeamNote(fFile);

		// padding
		_WriteNotePadding(dataSize);

		return fFile.Status();
	}

	/**
	 * @brief Stream the NT_FILE note payload describing mapped files
	 *        through @p writer.
	 *
	 * For every area linked to an ImageInfo, emits [start, end, page-offset]
	 * triplets followed by a run of NUL-terminated file-name strings, in
	 * the ptrace NT_FILE-compatible format.
	 *
	 * @param writer  Writer sink (DummyWriter or BufferedFile).
	 */
	template<typename Writer>
	void _WriteFilesNote(Writer& writer)
	{
		// file count and table size
		writer.Write(fMappedFilesCount);
		writer.Write((size_t)B_PAGE_SIZE);

		// write table
		for (AreaInfoList::Iterator it = fAreaInfos.GetIterator();
				AreaInfo* areaInfo = it.Next();) {
			if (areaInfo->GetImageInfo() == NULL)
				continue;

			// start address, end address, and file offset in pages
			writer.Write(areaInfo->Base());
			writer.Write(areaInfo->Base() + areaInfo->Size());
			writer.Write(size_t(areaInfo->CacheOffset() / B_PAGE_SIZE));
		}

		// write strings
		for (AreaInfoList::Iterator it = fAreaInfos.GetIterator();
				AreaInfo* areaInfo = it.Next();) {
			ImageInfo* imageInfo = areaInfo->GetImageInfo();
			if (imageInfo == NULL)
				continue;

			const char* name = imageInfo->Name();
			writer.Write(name, strlen(name) + 1);
		}
	}

	/**
	 * @brief Emit the complete NT_FILE note with header and padding.
	 *
	 * @return B_OK on success; sticky file error otherwise.
	 */
	status_t _WriteFilesNote()
	{
		// determine needed size for the note's data
		DummyWriter dummyWriter;
		_WriteFilesNote(dummyWriter);
		size_t dataSize = dummyWriter.BytesWritten();

		// write the note header
		_WriteNoteHeader(kCoreNote, NT_FILE, dataSize);

		// write the note data
		_WriteFilesNote(fFile);

		// padding
		_WriteNotePadding(dataSize);

		return fFile.Status();
	}

	/**
	 * @brief Stream the Haiku NT_AREAS note payload through @p writer.
	 *
	 * Emits a count, the entry size, one elf_note_area_entry per area, and
	 * a string table of area names. Reads only the kernel-side AreaInfo
	 * snapshots.
	 *
	 * @param writer  Writer sink (DummyWriter or BufferedFile).
	 */
	template<typename Writer>
	void _WriteAreasNote(Writer& writer)
	{
		// area count
		writer.Write((uint32)fAreaCount);
		writer.Write((uint32)sizeof(elf_note_area_entry));

		// write table
		for (AreaInfoList::Iterator it = fAreaInfos.GetIterator();
				AreaInfo* areaInfo = it.Next();) {
			elf_note_area_entry entry;
			memset(&entry, 0, sizeof(entry));
			entry.na_id = areaInfo->Id();
			entry.na_lock = areaInfo->Lock();
			entry.na_protection = areaInfo->Protection();
			entry.na_base = areaInfo->Base();
			entry.na_size = areaInfo->Size();
			entry.na_ram_size = areaInfo->RamSize();
			writer.Write(entry);
		}

		// write strings
		for (AreaInfoList::Iterator it = fAreaInfos.GetIterator();
				AreaInfo* areaInfo = it.Next();) {
			const char* name = areaInfo->Name();
			writer.Write(name, strlen(name) + 1);
		}
	}

	/**
	 * @brief Emit the complete Haiku NT_AREAS note with header and padding.
	 *
	 * @return B_OK on success; sticky file error otherwise.
	 */
	status_t _WriteAreasNote()
	{
		// determine needed size for the note's data
		DummyWriter dummyWriter;
		_WriteAreasNote(dummyWriter);
		size_t dataSize = dummyWriter.BytesWritten();

		// write the note header
		_WriteNoteHeader(kHaikuNote, NT_AREAS, dataSize);

		// write the note data
		_WriteAreasNote(fFile);

		// padding
		_WriteNotePadding(dataSize);

		return fFile.Status();
	}

	/**
	 * @brief Stream the Haiku NT_IMAGES note payload through @p writer.
	 *
	 * Emits count + entry size, an elf_note_image_entry per image, and a
	 * run of NUL-terminated image names. Works on kernel-side ImageInfo
	 * snapshots.
	 *
	 * @param writer  Writer sink (DummyWriter or BufferedFile).
	 */
	template<typename Writer>
	void _WriteImagesNote(Writer& writer)
	{
		// image count
		writer.Write((uint32)fImageCount);
		writer.Write((uint32)sizeof(elf_note_image_entry));

		// write table
		for (ImageInfoList::Iterator it = fImageInfos.GetIterator();
				ImageInfo* imageInfo = it.Next();) {
			elf_note_image_entry entry;
			memset(&entry, 0, sizeof(entry));
			entry.ni_id = imageInfo->Id();
			entry.ni_type = imageInfo->Type();
			entry.ni_init_routine = imageInfo->InitRoutine();
			entry.ni_term_routine = imageInfo->TermRoutine();
			entry.ni_device = imageInfo->DeviceId();
			entry.ni_node = imageInfo->NodeId();
			entry.ni_text_base = imageInfo->TextBase();
			entry.ni_text_size = imageInfo->TextSize();
			entry.ni_data_base = imageInfo->DataBase();
			entry.ni_data_size = imageInfo->DataSize();
			entry.ni_text_delta = imageInfo->TextDelta();
			entry.ni_symbol_table = imageInfo->SymbolTable();
			entry.ni_symbol_hash = imageInfo->SymbolHash();
			entry.ni_string_table = imageInfo->StringTable();
			writer.Write(entry);
		}

		// write strings
		for (ImageInfoList::Iterator it = fImageInfos.GetIterator();
				ImageInfo* imageInfo = it.Next();) {
			const char* name = imageInfo->Name();
			writer.Write(name, strlen(name) + 1);
		}
	}

	/**
	 * @brief Emit the complete Haiku NT_IMAGES note with header and padding.
	 *
	 * @return B_OK on success; sticky file error otherwise.
	 */
	status_t _WriteImagesNote()
	{
		// determine needed size for the note's data
		DummyWriter dummyWriter;
		_WriteImagesNote(dummyWriter);
		size_t dataSize = dummyWriter.BytesWritten();

		// write the note header
		_WriteNoteHeader(kHaikuNote, NT_IMAGES, dataSize);

		// write the note data
		_WriteImagesNote(fFile);

		// padding
		_WriteNotePadding(dataSize);

		return fFile.Status();
	}

	/**
	 * @brief Write one NT_SYMBOLS note per image that carries a
	 *        kernel-owned symbol/string table copy.
	 *
	 * Only the commpage currently populates such tables; userland images
	 * expose their symbols via the in-image tables referenced by the
	 * NT_IMAGES note.
	 *
	 * @return B_OK on success; first error encountered otherwise.
	 */
	status_t _WriteImageSymbolsNotes()
	{
		// write table
		for (ImageInfoList::Iterator it = fImageInfos.GetIterator();
				ImageInfo* imageInfo = it.Next();) {
			if (imageInfo->SymbolTableData() == NULL
					|| imageInfo->StringTableData() == NULL) {
				continue;
			}

			status_t error = _WriteImageSymbolsNote(imageInfo);
			if (error != B_OK)
				return error;
		}

		return B_OK;
	}

	/**
	 * @brief Stream one NT_SYMBOLS payload for @p imageInfo.
	 *
	 * Writes image id, symbol count, per-entry size, the symbol blob, and
	 * the string table blob from the kernel-owned buffers.
	 *
	 * @param imageInfo  ImageInfo whose symbols should be emitted.
	 * @param writer     Writer sink.
	 */
	template<typename Writer>
	void _WriteImageSymbolsNote(const ImageInfo* imageInfo, Writer& writer)
	{
		uint32 symbolCount = imageInfo->SymbolCount();
		uint32 symbolEntrySize = (uint32)sizeof(elf_sym);

		writer.Write((int32)imageInfo->Id());
		writer.Write(symbolCount);
		writer.Write(symbolEntrySize);
		writer.Write(imageInfo->SymbolTableData(),
			symbolCount * symbolEntrySize);
		writer.Write(imageInfo->StringTableData(),
			imageInfo->StringTableSize());
	}

	/**
	 * @brief Emit a full NT_SYMBOLS note (header + payload + padding) for
	 *        @p imageInfo.
	 *
	 * @param imageInfo  Image being exported.
	 * @return B_OK on success; sticky file error otherwise.
	 */
	status_t _WriteImageSymbolsNote(const ImageInfo* imageInfo)
	{
		// determine needed size for the note's data
		DummyWriter dummyWriter;
		_WriteImageSymbolsNote(imageInfo, dummyWriter);
		size_t dataSize = dummyWriter.BytesWritten();

		// write the note header
		_WriteNoteHeader(kHaikuNote, NT_SYMBOLS, dataSize);

		// write the note data
		_WriteImageSymbolsNote(imageInfo, fFile);

		// padding
		_WriteNotePadding(dataSize);

		return fFile.Status();
	}

	/**
	 * @brief Stream the Haiku NT_THREADS note payload through @p writer.
	 *
	 * Emits thread count, per-entry size, debug_cpu_state size, one
	 * elf_note_thread_entry + debug_cpu_state per thread (the user-mode
	 * register snapshot captured in _GetThreadStates()), followed by a
	 * string table of thread names.
	 *
	 * @param writer  Writer sink (DummyWriter or BufferedFile).
	 */
	template<typename Writer>
	void _WriteThreadsNote(Writer& writer)
	{
		// thread count and size of CPU state
		writer.Write((uint32)fThreadCount);
		writer.Write((uint32)sizeof(elf_note_thread_entry));
		writer.Write((uint32)sizeof(debug_cpu_state));

		// write table
		for (ThreadStateList::Iterator it = fThreadStates.GetIterator();
				ThreadState* state = it.Next();) {
			elf_note_thread_entry entry;
			memset(&entry, 0, sizeof(entry));
			entry.nth_id = state->GetThread()->id;
			entry.nth_state = state->State();
			entry.nth_priority = state->Priority();
			entry.nth_stack_base = state->StackBase();
			entry.nth_stack_end = state->StackEnd();
			writer.Write(&entry, sizeof(entry));
			writer.Write(state->CpuState(), sizeof(debug_cpu_state));
		}

		// write strings
		for (ThreadStateList::Iterator it = fThreadStates.GetIterator();
				ThreadState* state = it.Next();) {
			const char* name = state->Name();
			writer.Write(name, strlen(name) + 1);
		}
	}

	/**
	 * @brief Emit the complete Haiku NT_THREADS note with header and padding.
	 *
	 * @return B_OK on success; sticky file error otherwise.
	 */
	status_t _WriteThreadsNote()
	{
		// determine needed size for the note's data
		DummyWriter dummyWriter;
		_WriteThreadsNote(dummyWriter);
		size_t dataSize = dummyWriter.BytesWritten();

		// write the note header
		_WriteNoteHeader(kHaikuNote, NT_THREADS, dataSize);

		// write the note data
		_WriteThreadsNote(fFile);

		// padding
		_WriteNotePadding(dataSize);

		return fFile.Status();
	}

	/**
	 * @brief Emit an Elf32_Nhdr followed by the note name and padding.
	 *
	 * The 32-bit Nhdr shape is used on both 32- and 64-bit hosts because it
	 * matches the on-disk format the notes subsystem expects.
	 *
	 * @param name      Note-name string (ELF_NOTE_CORE or ELF_NOTE_HAIKU).
	 * @param type      Note type id (NT_TEAM, NT_FILE, NT_THREADS, ...).
	 * @param dataSize  Size of the payload that will follow.
	 * @return B_OK on success; sticky file error otherwise.
	 */
	status_t _WriteNoteHeader(const char* name, uint32 type, uint32 dataSize)
	{
		// prepare and write the header
		Elf32_Nhdr noteHeader;
		memset(&noteHeader, 0, sizeof(noteHeader));
		size_t nameSize = strlen(name) + 1;
		noteHeader.n_namesz = nameSize;
		noteHeader.n_descsz = dataSize;
		noteHeader.n_type = type;
		fFile.Write(noteHeader);

		// write the name
		fFile.Write(name, nameSize);
		// pad the name to 4 byte alignment
		_WriteNotePadding(nameSize);
		return fFile.Status();
	}

	/**
	 * @brief Write up to 3 zero bytes so @p sizeToPad is rounded up to the
	 *        ELF note 4-byte alignment.
	 *
	 * @param sizeToPad  Size of the blob just written.
	 * @return B_OK on success; sticky file error otherwise.
	 */
	status_t _WriteNotePadding(size_t sizeToPad)
	{
		if (sizeToPad % 4 != 0) {
			uint8 pad[3] = {};
			fFile.Write(&pad, 4 - sizeToPad % 4);
		}
		return fFile.Status();
	}

private:
	Thread*				fCurrentThread;
	Team*				fTeam;
	BufferedFile		fFile;
	TeamInfo			fTeamInfo;
	size_t				fThreadCount;
	ThreadStateList		fThreadStates;
	ThreadStateList		fPreAllocatedThreadStates;
	Allocator			fAreaInfoAllocator;
	AreaInfoList		fAreaInfos;
	ImageInfoList		fImageInfos;
	ConditionVariable	fThreadBlockCondition;
	size_t				fSegmentCount;
	size_t				fProgramHeadersOffset;
	size_t				fNoteSegmentOffset;
	size_t				fNoteSegmentSize;
	size_t				fFirstAreaSegmentOffset;
	size_t				fAreaCount;
	size_t				fImageCount;
	size_t				fMappedFilesCount;
};


} // unnamed namespace


/**
 * @brief Public entry point to produce a core file for the current team.
 *
 * Allocates a CoreDumper on the heap, tied to the calling thread's Team,
 * and invokes CoreDumper::Dump(). Not a @c _user_* syscall wrapper: it is
 * invoked from kernel contexts such as the signal handler for fatal
 * signals and the debugger.
 *
 * @param path      Absolute filesystem path for the core file.
 * @param killTeam  If true, kill the team after writing the dump.
 * @return B_OK on success; B_NO_MEMORY on allocation failure; any error
 *         returned by CoreDumper::Dump().
 */
status_t
core_dump_write_core_file(const char* path, bool killTeam)
{
	TRACE("core_dump_write_core_file(\"%s\", %d): team: %" B_PRId32 "\n", path,
		killTeam, team_get_current_team_id());

	CoreDumper* coreDumper = new(std::nothrow) CoreDumper();
	if (coreDumper == NULL)
		return B_NO_MEMORY;
	ObjectDeleter<CoreDumper> coreDumperDeleter(coreDumper);
	return coreDumper->Dump(path, killTeam);
}


/**
 * @brief Park the calling thread until the active core dump completes.
 *
 * Called from the thread-exit path of peer threads when the core dumper
 * sets THREAD_FLAGS_TRAP_FOR_CORE_DUMP via _SetThreadsCoreDumpFlag(). The
 * thread waits on the team's core-dump condition variable (kernel memory),
 * re-checking the flag each time it is woken so it loops while the flag
 * remains set.
 */
void
core_dump_trap_thread()
{
	Thread* thread = thread_get_current_thread();
	ConditionVariableEntry conditionVariableEntry;
	TeamLocker teamLocker(thread->team);

	while ((atomic_get(&thread->flags) & THREAD_FLAGS_TRAP_FOR_CORE_DUMP)
			!= 0) {
		thread->team->CoreDumpCondition()->Add(&conditionVariableEntry);
		teamLocker.Unlock();
		conditionVariableEntry.Wait();
		teamLocker.Lock();
	}
}
