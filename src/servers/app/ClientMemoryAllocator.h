/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2006-2013, Haiku, Inc. All Rights Reserved.
 * Original author: Axel Dörfler, axeld@pinc-software.de.
 */

/** @file ClientMemoryAllocator.h
    @brief Sub-allocator for memory areas shared between the app server and clients. */

#ifndef CLIENT_MEMORY_ALLOCATOR_H
#define CLIENT_MEMORY_ALLOCATOR_H


#include <Locker.h>
#include <Referenceable.h>

#include <util/DoublyLinkedList.h>


class ServerApp;
struct chunk;
struct block;

/** @brief Represents a contiguous region of a shared memory area used as a heap chunk. */
struct chunk : DoublyLinkedListLinkImpl<struct chunk> {
	area_id	area;
	uint8*	base;
	size_t	size;
};

/** @brief Represents a single allocation within a chunk, with its base address and size. */
struct block : DoublyLinkedListLinkImpl<struct block> {
	struct chunk* chunk;
	uint8*	base;
	size_t	size;
};

/** @brief Doubly-linked list of allocated blocks within shared memory areas. */
typedef DoublyLinkedList<block> block_list;

/** @brief Doubly-linked list of memory area chunks managed by the allocator. */
typedef DoublyLinkedList<chunk> chunk_list;


/** @brief Reference-counted sub-allocator that carves blocks from shared areas
           visible to both the app server and a connected client application. */
class ClientMemoryAllocator : public BReferenceable {
public:
								ClientMemoryAllocator(ServerApp* application);
								~ClientMemoryAllocator();

			/** @brief Allocates a block of at least size bytes from shared memory.
			    @param size Number of bytes to allocate.
			    @param _address Output pointer to the block descriptor.
			    @return Pointer to the usable memory, or NULL on failure. */
			void*				Allocate(size_t size, block** _address);

			/** @brief Frees a previously allocated block.
			    @param cookie The block descriptor returned by Allocate(). */
			void				Free(block* cookie);

			/** @brief Detaches the allocator from its owning ServerApp, preventing
			           further client-visible allocations. */
			void				Detach();

			/** @brief Dumps allocation state to stdout for debugging purposes. */
			void				Dump();

private:
			/** @brief Allocates a new memory area chunk large enough for size bytes.
			    @param size Minimum usable size required.
			    @return Pointer to the first free block in the new chunk, or NULL. */
			struct block*		_AllocateChunk(size_t size);

private:
			ServerApp*			fApplication;
			BLocker				fLock;
			chunk_list			fChunks;
			block_list			fFreeBlocks;
};


/** @brief Abstract interface to a memory region identified by an area_id and offset. */
class AreaMemory {
public:
	virtual						~AreaMemory() {}

	/** @brief Returns the area_id of the underlying memory area.
	    @return area_id. */
	virtual area_id				Area() = 0;

	/** @brief Returns a pointer to the base of the usable memory.
	    @return Base address. */
	virtual uint8*				Address() = 0;

	/** @brief Returns the byte offset of this allocation within its area.
	    @return Offset in bytes. */
	virtual uint32				AreaOffset() = 0;
};


/** @brief AreaMemory implementation that sub-allocates from a ClientMemoryAllocator. */
class ClientMemory : public AreaMemory {
public:
								ClientMemory();

	virtual						~ClientMemory();

			/** @brief Allocates memory of the given size from the supplied allocator.
			    @param allocator The ClientMemoryAllocator to allocate from.
			    @param size Number of bytes to allocate.
			    @return Pointer to the allocated memory, or NULL on failure. */
			void*				Allocate(ClientMemoryAllocator* allocator,
									size_t size);

	/** @brief Returns the area_id of the underlying shared area.
	    @return area_id. */
	virtual area_id				Area();

	/** @brief Returns a pointer to the allocated memory.
	    @return Base address of the allocation. */
	virtual uint8*				Address();

	/** @brief Returns the byte offset of this allocation within its shared area.
	    @return Offset in bytes. */
	virtual uint32				AreaOffset();

private:
			BReference<ClientMemoryAllocator>
								fAllocator;
			block*				fBlock;
};


/** @brief AreaMemory implementation that clones an existing area and wraps a sub-region of it. */
/*! Just clones an existing area. */
class ClonedAreaMemory : public AreaMemory {
public:
								ClonedAreaMemory();
	virtual						~ClonedAreaMemory();

			/** @brief Clones the given area and sets up access at the specified offset.
			    @param area The area_id of the area to clone.
			    @param offset Byte offset into the cloned area.
			    @return Pointer to the memory at the offset, or NULL on failure. */
			void*				Clone(area_id area, uint32 offset);

	/** @brief Returns the cloned area_id.
	    @return area_id of the clone. */
	virtual area_id				Area();

	/** @brief Returns a pointer to the memory at the configured offset.
	    @return Base address. */
	virtual uint8*				Address();

	/** @brief Returns the byte offset used when Clone() was called.
	    @return Offset in bytes. */
	virtual uint32				AreaOffset();

private:
			area_id		fArea;
			area_id		fClonedArea;
			uint32		fOffset;
			uint8*		fBase;
};


#endif	/* CLIENT_MEMORY_ALLOCATOR_H */
