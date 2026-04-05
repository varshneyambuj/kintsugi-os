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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2004-2012, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file file_map.cpp
 * @brief File-to-physical-block mapping cache for the file cache.
 *
 * FileMap maintains a cache of file-offset → block-number translations,
 * avoiding repeated calls into the file system for each I/O. Used by
 * file_cache.cpp to resolve logical file positions to device block addresses.
 *
 * @see file_cache.cpp, block_cache.cpp
 */


#include <new>
#include <stdlib.h>
#include <string.h>

#ifdef FS_SHELL
#	include "vfs.h"
#	include "fssh_api_wrapper.h"

using namespace FSShell;
#else
#	include <unistd.h>

#	include <KernelExport.h>
#	include <fs_cache.h>

#	include <condition_variable.h>
#	include <file_cache.h>
#	include <generic_syscall.h>
#	include <util/AutoLock.h>
#	include <util/DoublyLinkedList.h>
#	include <vfs.h>
#	include <vm/vm.h>
#	include <vm/vm_page.h>
#	include <vm/VMCache.h>

#	include "kernel_debug_config.h"
#endif


//#define TRACE_FILE_MAP
#ifdef TRACE_FILE_MAP
#	define TRACE(x...) dprintf_no_syslog(x)
#else
#	define TRACE(x...) ;
#endif

// TODO: use a sparse array - eventually, the unused BlockMap would be something
//	to reuse for this. We could also have an upperbound of memory consumption
//	for the whole map.
// TODO: it would be nice if we could free a file map in low memory situations.


#define CACHED_FILE_EXTENTS	2
	// must be smaller than MAX_FILE_IO_VECS
	// TODO: find out how much of these are typically used

struct file_extent {
	off_t			offset;
	file_io_vec		disk;
};

struct file_extent_array {
	file_extent*	array;
	size_t			max_count;
};

class FileMap
#if DEBUG_FILE_MAP
	: public DoublyLinkedListLinkImpl<FileMap>
#endif
{
public:
							FileMap(struct vnode* vnode, off_t size);
							~FileMap();

			void			Invalidate(off_t offset, off_t size);
			void			SetSize(off_t size);

			status_t		Translate(off_t offset, size_t size,
								file_io_vec* vecs, size_t* _count,
								size_t align);

			file_extent*	ExtentAt(uint32 index);

			size_t			Count() const { return fCount; }
			struct vnode*	Vnode() const { return fVnode; }
			off_t			Size() const { return fSize; }

			status_t		SetMode(uint32 mode);

private:
			file_extent*	_FindExtent(off_t offset, uint32* _index);
			status_t		_MakeSpace(size_t count);
			status_t		_Add(file_io_vec* vecs, size_t vecCount,
								off_t& lastOffset);
			status_t		_Cache(off_t offset, off_t size);
			void			_InvalidateAfter(off_t offset);
			void			_Free();

	union {
		file_extent	fDirect[CACHED_FILE_EXTENTS];
		file_extent_array fIndirect;
	};
	mutex			fLock;
	size_t			fCount;
	struct vnode*	fVnode;
	off_t			fSize;
	bool			fCacheAll;
};

#if DEBUG_FILE_MAP
typedef DoublyLinkedList<FileMap> FileMapList;

static FileMapList sList;
static mutex sLock;
#endif


/**
 * @brief Constructs a FileMap for the given vnode and initial file size.
 *
 * Initialises the internal mutex and, when DEBUG_FILE_MAP is enabled,
 * registers this instance in the global list for debugger inspection.
 * The extent cache starts empty; entries are populated lazily on the
 * first Translate() call.
 *
 * @param vnode Pointer to the VFS vnode that owns this map.
 * @param size  Current logical size of the file in bytes.
 */
FileMap::FileMap(struct vnode* vnode, off_t size)
	:
	fCount(0),
	fVnode(vnode),
	fSize(size),
	fCacheAll(false)
{
	mutex_init(&fLock, "file map");

#if DEBUG_FILE_MAP
	MutexLocker _(sLock);
	sList.Add(this);
#endif
}


/**
 * @brief Destroys the FileMap and releases all resources.
 *
 * Frees any dynamically allocated extent array via _Free(), destroys the
 * mutex, and, when DEBUG_FILE_MAP is enabled, removes this instance from
 * the global tracking list.
 */
FileMap::~FileMap()
{
	_Free();
	mutex_destroy(&fLock);

#if DEBUG_FILE_MAP
	MutexLocker _(sLock);
	sList.Remove(this);
#endif
}


/**
 * @brief Returns a pointer to the file_extent at the given cache index.
 *
 * Selects either the inline fDirect array (for small maps) or the heap-
 * allocated fIndirect array (for larger maps) based on fCount.
 *
 * @param index Zero-based index into the cached extent array.
 * @return Pointer to the corresponding file_extent, or @c NULL if
 *         @p index is out of range.
 */
file_extent*
FileMap::ExtentAt(uint32 index)
{
	if (index >= fCount)
		return NULL;

	if (fCount > CACHED_FILE_EXTENTS)
		return &fIndirect.array[index];

	return &fDirect[index];
}


/**
 * @brief Searches the cached extent array for the extent covering @p offset.
 *
 * Uses a binary search over the sorted extent list. Each extent covers the
 * half-open interval [extent->offset, extent->offset + extent->disk.length).
 *
 * @param offset  Logical file byte offset to locate.
 * @param _index  If non-NULL and a match is found, receives the index of
 *                the matching extent.
 * @return Pointer to the matching file_extent, or @c NULL if no cached
 *         extent covers @p offset.
 */
file_extent*
FileMap::_FindExtent(off_t offset, uint32 *_index)
{
	int32 left = 0;
	int32 right = fCount - 1;

	while (left <= right) {
		int32 index = (left + right) / 2;
		file_extent* extent = ExtentAt(index);

		if (extent->offset > offset) {
			// search in left part
			right = index - 1;
		} else if (extent->offset + extent->disk.length <= offset) {
			// search in right part
			left = index + 1;
		} else {
			// found extent
			if (_index)
				*_index = index;

			return extent;
		}
	}

	return NULL;
}


/**
 * @brief Resizes the internal extent array to hold exactly @p count entries.
 *
 * Manages the transition between the inline fDirect storage (up to
 * CACHED_FILE_EXTENTS entries) and a heap-allocated fIndirect array.
 * The heap array grows in powers of two up to 32768 entries, then in
 * increments of 32768 to amortise reallocation cost.
 *
 * @param count Desired number of extent slots after the operation.
 * @return B_OK on success, B_NO_MEMORY if heap allocation fails.
 *
 * @note Shrinking from indirect to direct storage copies existing entries
 *       into the inline array and frees the heap buffer.
 */
status_t
FileMap::_MakeSpace(size_t count)
{
	if (count <= CACHED_FILE_EXTENTS) {
		// just use the reserved area in the file_cache_ref structure
		if (fCount > CACHED_FILE_EXTENTS) {
			// the new size is smaller than the minimal array size
			file_extent *array = fIndirect.array;
			memcpy(fDirect, array, sizeof(file_extent) * count);
			free(array);
		}
	} else {
		// resize array if needed
		file_extent* oldArray = NULL;
		size_t maxCount = CACHED_FILE_EXTENTS;
		if (fCount > CACHED_FILE_EXTENTS) {
			oldArray = fIndirect.array;
			maxCount = fIndirect.max_count;
		}

		if (count > maxCount) {
			// allocate new array
			while (maxCount < count) {
				if (maxCount < 32768)
					maxCount <<= 1;
				else
					maxCount += 32768;
			}

			file_extent* newArray = (file_extent *)realloc(oldArray,
				maxCount * sizeof(file_extent));
			if (newArray == NULL)
				return B_NO_MEMORY;

			if (fCount > 0 && fCount <= CACHED_FILE_EXTENTS)
				memcpy(newArray, fDirect, sizeof(file_extent) * fCount);

			fIndirect.array = newArray;
			fIndirect.max_count = maxCount;
		}
	}

	fCount = count;
	return B_OK;
}


/**
 * @brief Appends a set of VFS file_io_vec extents to the cached map.
 *
 * Allocates space for up to @p vecCount new entries and merges each
 * incoming vector with the previous extent when the device offsets are
 * contiguous (or both represent holes, i.e., offset == -1). Non-contiguous
 * vectors are stored as separate extents.
 *
 * @param vecs       Array of file_io_vec structures returned by the VFS.
 * @param vecCount   Number of entries in @p vecs.
 * @param lastOffset Output: updated logical file offset after all added extents.
 * @return B_OK on success, B_NO_MEMORY if _MakeSpace() fails.
 *
 * @note After a merge that shrinks the array back to CACHED_FILE_EXTENTS,
 *       lastExtent is re-fetched because the indirect-to-direct copy
 *       invalidates the previous pointer.
 */
status_t
FileMap::_Add(file_io_vec* vecs, size_t vecCount, off_t& lastOffset)
{
	TRACE("FileMap@%p::Add(vecCount = %ld)\n", this, vecCount);

	uint32 start = fCount;
	off_t offset = 0;

	status_t status = _MakeSpace(fCount + vecCount);
	if (status != B_OK)
		return status;

	file_extent* lastExtent = NULL;
	if (start != 0) {
		lastExtent = ExtentAt(start - 1);
		offset = lastExtent->offset + lastExtent->disk.length;
	}

	for (uint32 i = 0; i < vecCount; i++) {
		if (lastExtent != NULL) {
			if (lastExtent->disk.offset + lastExtent->disk.length
					== vecs[i].offset
				|| (lastExtent->disk.offset == -1 && vecs[i].offset == -1)) {

				lastExtent->disk.length += vecs[i].length;
				offset += vecs[i].length;

				_MakeSpace(fCount - 1);
				if (fCount == CACHED_FILE_EXTENTS) {
					// We moved the indirect array into the direct one, making
					// lastExtent a stale pointer, re-get it.
					lastExtent = ExtentAt(start - 1);
				}

				continue;
			}
		}

		file_extent* extent = ExtentAt(start++);
		extent->offset = offset;
		extent->disk = vecs[i];

		offset += extent->disk.length;
		lastExtent = extent;
	}

#ifdef TRACE_FILE_MAP
	for (uint32 i = 0; i < fCount; i++) {
		file_extent* extent = ExtentAt(i);
		TRACE("[%ld] extent offset %lld, disk offset %lld, length %lld\n",
			i, extent->offset, extent->disk.offset, extent->disk.length);
	}
#endif

	lastOffset = offset;
	return B_OK;
}


/**
 * @brief Truncates the cached extent list at the given logical file offset.
 *
 * Finds the extent that contains @p offset and shortens its disk length so
 * that the map covers bytes only up to (but not including) @p offset. Any
 * extents that lie entirely beyond @p offset are removed by shrinking fCount.
 * If the trimmed extent would have zero length it is removed as well.
 *
 * @param offset Logical byte offset at which the map should be truncated.
 */
void
FileMap::_InvalidateAfter(off_t offset)
{
	uint32 index;
	file_extent* extent = _FindExtent(offset, &index);
	if (extent != NULL) {
		uint32 resizeTo = index + 1;

		if (extent->offset + extent->disk.length > offset) {
			extent->disk.length = offset - extent->offset;
			if (extent->disk.length == 0)
				resizeTo = index;
		}

		_MakeSpace(resizeTo);
	}
}


/*!	Invalidates or removes the specified part of the file map.
*/
/**
 * @brief Invalidates part or all of the cached file map.
 *
 * When @p offset is zero the entire map is discarded via _Free(). Otherwise,
 * all extents at or beyond @p offset are removed by _InvalidateAfter().
 *
 * @param offset Starting logical byte offset of the region to invalidate.
 * @param size   Length of the region to invalidate (currently unused;
 *               everything from @p offset onwards is always discarded).
 *
 * @note The @p size parameter is not yet honoured; it is reserved for a
 *       future implementation that invalidates only a precise sub-range.
 */
void
FileMap::Invalidate(off_t offset, off_t size)
{
	MutexLocker _(fLock);

	// TODO: honour size, we currently always remove everything after "offset"
	if (offset == 0) {
		_Free();
		return;
	}

	_InvalidateAfter(offset);
}


/**
 * @brief Updates the logical file size tracked by the map.
 *
 * If the new @p size is smaller than the current size, cached extents that
 * extend beyond the new end-of-file are trimmed via _InvalidateAfter().
 * Growing the file does not populate new extents; they are fetched lazily on
 * the next Translate() call.
 *
 * @param size New logical file size in bytes.
 */
void
FileMap::SetSize(off_t size)
{
	MutexLocker _(fLock);

	if (size < fSize)
		_InvalidateAfter(size);

	fSize = size;
}


/**
 * @brief Releases any heap-allocated extent storage and resets the count.
 *
 * Frees the fIndirect.array buffer when fCount exceeds CACHED_FILE_EXTENTS,
 * then sets fCount to zero. The inline fDirect array requires no explicit
 * deallocation.
 */
void
FileMap::_Free()
{
	if (fCount > CACHED_FILE_EXTENTS)
		free(fIndirect.array);

	fCount = 0;
}


/**
 * @brief Populates the extent cache to cover the range [@p offset, @p offset + @p size).
 *
 * Queries vfs_get_file_map() in a loop, adding batches of up to 8 extents at
 * a time, until the cached map reaches the end of the requested range. In
 * FILE_MAP_CACHE_ALL mode the map must already cover the entire file; if it
 * does not, B_ERROR is returned immediately.
 *
 * @param offset Logical byte offset at the start of the range to cache.
 * @param size   Number of bytes that must be covered after this call.
 * @return B_OK on success, B_ERROR if fCacheAll is set but the map is
 *         incomplete, or a VFS error code from vfs_get_file_map() / _Add().
 */
status_t
FileMap::_Cache(off_t offset, off_t size)
{
	file_extent* lastExtent = NULL;
	if (fCount > 0)
		lastExtent = ExtentAt(fCount - 1);

	off_t mapEnd = 0;
	if (lastExtent != NULL)
		mapEnd = lastExtent->offset + lastExtent->disk.length;

	off_t end = offset + size;

	if (fCacheAll && mapEnd < end)
		return B_ERROR;

	status_t status = B_OK;
	file_io_vec vecs[8];
	const size_t kMaxVecs = 8;

	while (status == B_OK && mapEnd < end) {
		// We don't have the requested extents yet, retrieve them
		size_t vecCount = kMaxVecs;
		status = vfs_get_file_map(Vnode(), mapEnd, ~0UL, vecs, &vecCount);
		if (status == B_OK || status == B_BUFFER_OVERFLOW)
			status = _Add(vecs, vecCount, mapEnd);
	}

	return status;
}


/**
 * @brief Controls the caching strategy for this file map.
 *
 * Switches between FILE_MAP_CACHE_ALL (the entire file extent map is cached
 * up-front) and FILE_MAP_CACHE_ON_DEMAND (extents are fetched lazily). When
 * switching to FILE_MAP_CACHE_ALL the full map is populated immediately by
 * calling _Cache(0, fSize).
 *
 * @param mode Either FILE_MAP_CACHE_ALL or FILE_MAP_CACHE_ON_DEMAND.
 * @return B_OK on success, B_BAD_VALUE if @p mode is unrecognised, or a
 *         _Cache() error if pre-population fails.
 */
status_t
FileMap::SetMode(uint32 mode)
{
	if (mode != FILE_MAP_CACHE_ALL && mode != FILE_MAP_CACHE_ON_DEMAND)
		return B_BAD_VALUE;

	MutexLocker _(fLock);

	if ((mode == FILE_MAP_CACHE_ALL && fCacheAll)
		|| (mode == FILE_MAP_CACHE_ON_DEMAND && !fCacheAll))
		return B_OK;

	if (mode == FILE_MAP_CACHE_ALL) {
		status_t status = _Cache(0, fSize);
		if (status != B_OK)
			return status;

		fCacheAll = true;
	} else
		fCacheAll = false;

	return B_OK;
}


/**
 * @brief Translates a logical file range into an array of physical I/O vectors.
 *
 * Ensures the required extents are cached via _Cache(), then walks the sorted
 * extent list starting at the extent that contains @p offset and fills
 * @p vecs with the corresponding device offsets and lengths. If the logical
 * range straddles a file-size boundary and @p align is greater than 1, the
 * final vector is padded to an @p align-byte boundary.
 *
 * @param offset   Logical byte offset within the file to start translating.
 * @param size     Number of bytes to translate.
 * @param vecs     Caller-supplied array to receive the translated file_io_vec
 *                 entries.
 * @param _count   In: capacity of @p vecs (maximum number of entries).
 *                 Out: actual number of entries written.
 * @param align    Alignment in bytes for padding the last vector when the
 *                 request extends beyond the end of file (pass 1 or 0 to
 *                 disable padding).
 * @return B_OK on success.
 * @retval B_BAD_VALUE      @p offset is negative.
 * @retval B_BUFFER_OVERFLOW The translation requires more vectors than
 *                           @p *_count permits; @p *_count is set to the
 *                           number of vectors filled so far.
 *
 * @note An @p offset at or beyond fSize produces @p *_count == 0 and B_OK.
 */
status_t
FileMap::Translate(off_t offset, size_t size, file_io_vec* vecs, size_t* _count,
	size_t align)
{
	if (offset < 0)
		return B_BAD_VALUE;

	MutexLocker _(fLock);

	size_t maxVecs = *_count;
	size_t padLastVec = 0;

	if (offset >= Size()) {
		*_count = 0;
		return B_OK;
	}
	if ((off_t)(offset + size) > fSize) {
		if (align > 1) {
			off_t alignedSize = (fSize + align - 1) & ~(off_t)(align - 1);
			if ((off_t)(offset + size) >= alignedSize)
				padLastVec = alignedSize - fSize;
		}
		size = fSize - offset;
	}

	// First, we need to make sure that we have already cached all file
	// extents needed for this request.

	status_t status = _Cache(offset, size);
	if (status != B_OK)
		return status;

	// We now have cached the map of this file as far as we need it, now
	// we need to translate it for the requested access.

	uint32 index;
	file_extent* fileExtent = _FindExtent(offset, &index);

	offset -= fileExtent->offset;
	if (fileExtent->disk.offset != -1)
		vecs[0].offset = fileExtent->disk.offset + offset;
	else
		vecs[0].offset = -1;
	vecs[0].length = fileExtent->disk.length - offset;

	if (vecs[0].length >= (off_t)size) {
		vecs[0].length = size + padLastVec;
		*_count = 1;
		return B_OK;
	}

	// copy the rest of the vecs

	size -= vecs[0].length;
	uint32 vecIndex = 1;

	while (true) {
		fileExtent++;

		vecs[vecIndex++] = fileExtent->disk;

		if ((off_t)size <= fileExtent->disk.length) {
			vecs[vecIndex - 1].length = size + padLastVec;
			break;
		}

		if (vecIndex >= maxVecs) {
			*_count = vecIndex;
			return B_BUFFER_OVERFLOW;
		}

		size -= fileExtent->disk.length;
	}

	*_count = vecIndex;
	return B_OK;
}


//	#pragma mark -


#if DEBUG_FILE_MAP

/**
 * @brief Kernel debugger command: dumps a single FileMap instance.
 *
 * Prints the map's address, logical file size, and cached extent count.
 * When invoked with the @c -p flag, also lists every individual extent with
 * its logical offset and corresponding device offset and length.
 *
 * @param argc Argument count (must be >= 2).
 * @param argv argv[0] = command name; optional argv[1] = "-p";
 *             last argument = pointer to FileMap as a hex expression.
 * @return 0 in all cases (required by the debugger command interface).
 */
static int
dump_file_map(int argc, char** argv)
{
	if (argc < 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	bool printExtents = false;
	if (argc > 2 && !strcmp(argv[1], "-p"))
		printExtents = true;

	FileMap* map = (FileMap*)parse_expression(argv[argc - 1]);
	if (map == NULL) {
		kprintf("invalid file map!\n");
		return 0;
	}

	kprintf("FileMap %p\n", map);
	kprintf("  size    %" B_PRIdOFF "\n", map->Size());
	kprintf("  count   %lu\n", map->Count());

	if (!printExtents)
		return 0;

	for (uint32 i = 0; i < map->Count(); i++) {
		file_extent* extent = map->ExtentAt(i);

		kprintf("  [%" B_PRIu32 "] offset %" B_PRIdOFF ", disk offset %"
			B_PRIdOFF ", length %" B_PRIdOFF "\n", i, extent->offset,
			extent->disk.offset, extent->disk.length);
	}

	return 0;
}


/**
 * @brief Kernel debugger command: prints aggregate statistics for all FileMaps.
 *
 * Iterates the global sList and accumulates total file bytes, cached map
 * bytes, extent count, and map count. An optional size range [minSize,
 * maxSize] can be passed to restrict which maps are included in the summary.
 *
 * @param argc Argument count: 1 = all maps; 2 = maps up to maxSize;
 *             3 = maps in [minSize, maxSize].
 * @param argv argv[1] = minSize or maxSize; argv[2] = maxSize.
 * @return 0 in all cases (required by the debugger command interface).
 */
static int
dump_file_map_stats(int argc, char** argv)
{
	off_t minSize = 0;
	off_t maxSize = -1;

	if (argc == 2) {
		maxSize = parse_expression(argv[1]);
	} else if (argc > 2) {
		minSize = parse_expression(argv[1]);
		maxSize = parse_expression(argv[2]);
	}

	FileMapList::Iterator iterator = sList.GetIterator();
	off_t size = 0;
	off_t mapSize = 0;
	uint32 extents = 0;
	uint32 count = 0;
	uint32 emptyCount = 0;

	while (iterator.HasNext()) {
		FileMap* map = iterator.Next();

		if (minSize > map->Size() || (maxSize != -1 && maxSize < map->Size()))
			continue;

		if (map->Count() != 0) {
			file_extent* extent = map->ExtentAt(map->Count() - 1);
			if (extent != NULL)
				mapSize += extent->offset + extent->disk.length;

			extents += map->Count();
		} else
			emptyCount++;

		size += map->Size();
		count++;
	}

	kprintf("%" B_PRId32 " file maps (%" B_PRIu32 " empty), %" B_PRIdOFF " file"
		" bytes in total, %" B_PRIdOFF " bytes cached, %" B_PRIu32 " extents\n",
		count, emptyCount, size, mapSize, extents);
	kprintf("average %" B_PRIu32 " extents per map for %" B_PRIdOFF " bytes.\n",
		extents / (count - emptyCount), mapSize / (count - emptyCount));

	return 0;
}

#endif	// DEBUG_FILE_MAP


//	#pragma mark - private kernel API


/**
 * @brief Initialises the file map subsystem.
 *
 * When DEBUG_FILE_MAP is enabled, registers the "file_map" and
 * "file_map_stats" kernel debugger commands and initialises the global
 * list mutex. In release builds this is a no-op.
 *
 * @return Always B_OK.
 */
extern "C" status_t
file_map_init(void)
{
#if DEBUG_FILE_MAP
	add_debugger_command_etc("file_map", &dump_file_map,
		"Dumps the specified file map.",
		"[-p] <file-map>\n"
		"  -p          - causes the file extents to be printed as well.\n"
		"  <file-map>  - pointer to the file map.\n", 0);
	add_debugger_command("file_map_stats", &dump_file_map_stats,
		"Dumps some file map statistics.");

	mutex_init(&sLock, "file map list");
#endif
	return B_OK;
}


//	#pragma mark - public FS API


/**
 * @brief Creates a new FileMap for the vnode identified by @p mountID / @p vnodeID.
 *
 * Looks up the vnode without acquiring a reference (the file cache holds the
 * reference separately), then heap-allocates a FileMap with an initial size
 * of @p size bytes.
 *
 * @param mountID  Device ID of the volume that owns the file.
 * @param vnodeID  Inode number within the volume.
 * @param size     Initial logical file size in bytes.
 * @return Opaque pointer to the new FileMap, or @c NULL on failure
 *         (vnode not found or out of memory).
 */
extern "C" void*
file_map_create(dev_t mountID, ino_t vnodeID, off_t size)
{
	TRACE("file_map_create(mountID = %ld, vnodeID = %lld, size = %lld)\n",
		mountID, vnodeID, size);

	// Get the vnode for the object
	// (note, this does not grab a reference to the node)
	struct vnode* vnode;
	if (vfs_lookup_vnode(mountID, vnodeID, &vnode) != B_OK)
		return NULL;

	return new(std::nothrow) FileMap(vnode, size);
}


/**
 * @brief Destroys a FileMap previously created by file_map_create().
 *
 * Deletes the FileMap object, releasing all cached extent storage and the
 * internal mutex. Silently ignores a @c NULL @p _map pointer.
 *
 * @param _map Opaque pointer returned by file_map_create(), or @c NULL.
 */
extern "C" void
file_map_delete(void* _map)
{
	FileMap* map = (FileMap*)_map;
	if (map == NULL)
		return;

	TRACE("file_map_delete(map = %p)\n", map);
	delete map;
}


/**
 * @brief Updates the logical size of the file map.
 *
 * Forwards to FileMap::SetSize(), which trims any cached extents that extend
 * beyond the new end-of-file. Silently ignores a @c NULL @p _map pointer.
 *
 * @param _map Opaque pointer returned by file_map_create(), or @c NULL.
 * @param size New logical file size in bytes.
 */
extern "C" void
file_map_set_size(void* _map, off_t size)
{
	FileMap* map = (FileMap*)_map;
	if (map == NULL)
		return;

	map->SetSize(size);
}


/**
 * @brief Invalidates part or all of the cached extent map.
 *
 * Forwards to FileMap::Invalidate(). Cached extents at or after @p offset
 * are discarded so they will be re-fetched from the file system on the next
 * Translate() call. Silently ignores a @c NULL @p _map pointer.
 *
 * @param _map   Opaque pointer returned by file_map_create(), or @c NULL.
 * @param offset Starting logical byte offset of the region to invalidate.
 * @param size   Length of the region (currently the implementation discards
 *               everything from @p offset onwards regardless of @p size).
 */
extern "C" void
file_map_invalidate(void* _map, off_t offset, off_t size)
{
	FileMap* map = (FileMap*)_map;
	if (map == NULL)
		return;

	map->Invalidate(offset, size);
}


/**
 * @brief Sets the caching strategy for the file map.
 *
 * Forwards to FileMap::SetMode(). Use FILE_MAP_CACHE_ALL to pre-populate
 * the entire extent map, or FILE_MAP_CACHE_ON_DEMAND for lazy population.
 *
 * @param _map Opaque pointer returned by file_map_create(), or @c NULL.
 * @param mode FILE_MAP_CACHE_ALL or FILE_MAP_CACHE_ON_DEMAND.
 * @return B_OK on success, B_BAD_VALUE if @p _map is @c NULL or @p mode is
 *         unrecognised, or a _Cache() error when switching to
 *         FILE_MAP_CACHE_ALL fails.
 */
extern "C" status_t
file_map_set_mode(void* _map, uint32 mode)
{
	FileMap* map = (FileMap*)_map;
	if (map == NULL)
		return B_BAD_VALUE;

	return map->SetMode(mode);
}


/**
 * @brief Translates a logical file range to an array of physical I/O vectors.
 *
 * Thin C wrapper around FileMap::Translate(). Populates @p vecs with
 * device-offset / length pairs that together cover [@p offset,
 * @p offset + @p size) within the file.
 *
 * @param _map    Opaque pointer returned by file_map_create(), or @c NULL.
 * @param offset  Logical byte offset within the file.
 * @param size    Number of bytes to translate.
 * @param vecs    Caller-supplied array to receive the translated vectors.
 * @param _count  In: capacity of @p vecs. Out: number of entries written.
 * @param align   Alignment for the last vector's padding (1 = no padding).
 * @return B_OK on success, B_BAD_VALUE if @p _map is @c NULL, or any error
 *         returned by FileMap::Translate().
 */
extern "C" status_t
file_map_translate(void* _map, off_t offset, size_t size, file_io_vec* vecs,
	size_t* _count, size_t align)
{
	TRACE("file_map_translate(map %p, offset %lld, size %ld)\n",
		_map, offset, size);

	FileMap* map = (FileMap*)_map;
	if (map == NULL)
		return B_BAD_VALUE;

	return map->Translate(offset, size, vecs, _count, align);
}

