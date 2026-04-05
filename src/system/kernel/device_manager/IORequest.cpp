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
 *   Copyright 2008-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2008-2017, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file IORequest.cpp
 * @brief Kernel I/O request — the unit of work passed through the I/O stack.
 *
 * IORequest represents a single read or write operation submitted by the VFS
 * or block cache. It carries the target address, buffer (user or kernel),
 * offset, and length. As it passes through the I/O scheduler, DMA resource
 * manager, and device driver, sub-requests (IOOperation) are created and
 * completed until the whole request is satisfied.
 *
 * @see IOSchedulerSimple.cpp, dma_resources.cpp, IOCache.cpp
 */

#include "IORequest.h"

#include <string.h>

#include <arch/debug.h>
#include <debug.h>
#include <heap.h>
#include <kernel.h>
#include <team.h>
#include <thread.h>
#include <util/AutoLock.h>
#include <vm/vm.h>
#include <vm/VMAddressSpace.h>

#include "dma_resources.h"


//#define TRACE_IO_REQUEST
#ifdef TRACE_IO_REQUEST
#	define TRACE(x...) dprintf(x)
#else
#	define TRACE(x...) ;
#endif


// partial I/O operation phases
enum {
	PHASE_READ_BEGIN	= 0,
	PHASE_READ_END		= 1,
	PHASE_DO_ALL		= 2
};


struct virtual_vec_cookie {
	uint32			vec_index;
	generic_size_t	vec_offset;
	area_id			mapped_area;
	void*			physical_page_handle;
	addr_t			virtual_address;

	virtual_vec_cookie()
		:
		vec_index(0),
		vec_offset(0),
		mapped_area(-1),
		physical_page_handle(NULL),
		virtual_address((addr_t)-1)
	{
	}

	void PutPhysicalPageIfNeeded()
	{
		if (virtual_address != (addr_t)-1) {
			vm_put_physical_page(virtual_address, physical_page_handle);
			virtual_address = (addr_t)-1;
		}
	}
};


// #pragma mark -


/**
 * @brief Default constructor — initialises parent pointer and pending status.
 */
IORequestChunk::IORequestChunk()
	:
	fParent(NULL),
	fStatus(1)
{
}


/**
 * @brief Destructor — no owned resources at this base level.
 */
IORequestChunk::~IORequestChunk()
{
}


//	#pragma mark -


/**
 * @brief Allocates a new IOBuffer capable of holding @p count scatter/gather
 *        vectors.
 *
 * @param count  Number of generic_io_vec slots to reserve.
 * @param vip    When true, allocates from the VIP heap so the allocation
 *               succeeds even under memory pressure.
 * @return Pointer to the new IOBuffer, or NULL on allocation failure.
 */
IOBuffer*
IOBuffer::Create(uint32 count, bool vip)
{
	size_t size = sizeof(IOBuffer) + sizeof(generic_io_vec) * (count - 1);
	IOBuffer* buffer
		= (IOBuffer*)(malloc_etc(size, vip ? HEAP_PRIORITY_VIP : 0));
	if (buffer == NULL)
		return NULL;

	buffer->fCapacity = count;
	buffer->fVecCount = 0;
	buffer->fUser = false;
	buffer->fPhysical = false;
	buffer->fVIP = vip;
	buffer->fMemoryLocked = false;

	return buffer;
}


/**
 * @brief Frees this IOBuffer, selecting the correct heap tier based on the
 *        VIP flag set at creation time.
 */
void
IOBuffer::Delete()
{
	free_etc(this, fVIP ? HEAP_PRIORITY_VIP : 0);
}


/**
 * @brief Copies a caller-supplied vector array into the buffer and trims the
 *        first and last vecs to honour sub-block offsets.
 *
 * @param firstVecOffset  Bytes to skip at the start of the first vec.
 * @param lastVecSize     Byte count to retain in the last vec (0 = keep all).
 * @param vecs            Source scatter/gather vector array.
 * @param count           Number of elements in @p vecs.
 * @param length          Total logical byte length represented by the vecs.
 * @param flags           I/O request flags (e.g. B_PHYSICAL_IO_REQUEST).
 */
void
IOBuffer::SetVecs(generic_size_t firstVecOffset, generic_size_t lastVecSize,
	const generic_io_vec* vecs, uint32 count, generic_size_t length, uint32 flags)
{
	memcpy(fVecs, vecs, sizeof(generic_io_vec) * count);

	if (count > 0 && firstVecOffset > 0) {
		fVecs[0].base += firstVecOffset;
		fVecs[0].length -= firstVecOffset;
	}
	if (lastVecSize > 0)
		fVecs[count - 1].length = lastVecSize;

	fVecCount = count;
	fLength = length;
	fPhysical = (flags & B_PHYSICAL_IO_REQUEST) != 0;
	fUser = !fPhysical && IS_USER_ADDRESS(vecs[0].base);

#if KDEBUG
	generic_size_t actualLength = 0;
	for (size_t i = 0; i < fVecCount; i++)
		actualLength += fVecs[i].length;

	ASSERT(actualLength == fLength);
#endif
}


/**
 * @brief Returns the next virtual iovec for iterating over the buffer's
 *        contents, mapping physical pages on demand when necessary.
 *
 * @param _cookie  Opaque iteration state; pass a NULL pointer on the first
 *                 call; the function allocates and updates it automatically.
 * @param vector   Filled with the base address and length of the next chunk.
 * @retval B_OK           A valid iovec was written to @p vector.
 * @retval B_BAD_INDEX    All vecs have been exhausted.
 * @retval B_NO_MEMORY    Could not allocate the iteration cookie.
 */
status_t
IOBuffer::GetNextVirtualVec(void*& _cookie, iovec& vector)
{
	virtual_vec_cookie* cookie = (virtual_vec_cookie*)_cookie;
	if (cookie == NULL) {
		cookie = new(malloc_flags(fVIP ? HEAP_PRIORITY_VIP : 0))
			virtual_vec_cookie;
		if (cookie == NULL)
			return B_NO_MEMORY;

		_cookie = cookie;
	}

	// recycle a potential previously mapped page
	cookie->PutPhysicalPageIfNeeded();

	if (cookie->vec_index >= fVecCount)
		return B_BAD_INDEX;

	if (!fPhysical) {
		vector.iov_base = (void*)(addr_t)fVecs[cookie->vec_index].base;
		vector.iov_len = fVecs[cookie->vec_index++].length;
		return B_OK;
	}

	if (cookie->vec_index == 0
			&& (fVecCount > 1 || fVecs[0].length > B_PAGE_SIZE)) {
		void* mappedAddress;
		addr_t mappedSize;
		ASSERT(cookie->mapped_area < 0);

// TODO: This is a potential violation of the VIP requirement, since
// vm_map_physical_memory_vecs() allocates memory without special flags!
		cookie->mapped_area = vm_map_physical_memory_vecs(
			VMAddressSpace::KernelID(), "io buffer mapped physical vecs",
			&mappedAddress, B_ANY_KERNEL_ADDRESS, &mappedSize,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, fVecs, fVecCount);

		if (cookie->mapped_area >= 0) {
			vector.iov_base = mappedAddress;
			vector.iov_len = mappedSize;
			return B_OK;
		} else
			ktrace_printf("failed to map area: %s\n", strerror(cookie->mapped_area));
	}

	// fallback to page wise mapping
	const generic_io_vec& currentVec = fVecs[cookie->vec_index];
	const generic_addr_t address = currentVec.base + cookie->vec_offset;
	const size_t pageOffset = address % B_PAGE_SIZE;

// TODO: This is a potential violation of the VIP requirement, since
// vm_get_physical_page() may allocate memory without special flags!
	status_t result = vm_get_physical_page(address - pageOffset,
		&cookie->virtual_address, &cookie->physical_page_handle);
	if (result != B_OK)
		return result;

	generic_size_t length = min_c(currentVec.length - cookie->vec_offset,
		B_PAGE_SIZE - pageOffset);

	vector.iov_base = (void*)(cookie->virtual_address + pageOffset);
	vector.iov_len = length;

	cookie->vec_offset += length;
	if (cookie->vec_offset >= currentVec.length) {
		cookie->vec_index++;
		cookie->vec_offset = 0;
	}

	return B_OK;
}


/**
 * @brief Releases all resources associated with a virtual-vec iteration
 *        cookie obtained from GetNextVirtualVec().
 *
 * @param _cookie  Cookie to free; may be NULL (no-op in that case).
 */
void
IOBuffer::FreeVirtualVecCookie(void* _cookie)
{
	virtual_vec_cookie* cookie = (virtual_vec_cookie*)_cookie;

	if (cookie->mapped_area >= 0)
		delete_area(cookie->mapped_area);
	cookie->PutPhysicalPageIfNeeded();

	free_etc(cookie, fVIP ? HEAP_PRIORITY_VIP : 0);
}


/**
 * @brief Locks all virtual memory regions described by the buffer's vecs so
 *        that the pages cannot be swapped out during the I/O transfer.
 *
 * @param team     Team whose address space the vecs belong to.
 * @param isWrite  True if the I/O is a write (pages must be writable).
 * @retval B_OK        All pages successfully locked.
 * @retval B_BAD_VALUE Memory was already locked.
 */
status_t
IOBuffer::LockMemory(team_id team, bool isWrite)
{
	if (fMemoryLocked) {
		panic("memory already locked!");
		return B_BAD_VALUE;
	}

	for (uint32 i = 0; i < fVecCount; i++) {
		status_t status = lock_memory_etc(team, (void*)(addr_t)fVecs[i].base,
			fVecs[i].length, isWrite ? 0 : B_READ_DEVICE);
		if (status != B_OK) {
			_UnlockMemory(team, i, isWrite);
			return status;
		}
	}

	fMemoryLocked = true;
	return B_OK;
}


/**
 * @brief Unlocks the first @p count vecs; used internally to roll back a
 *        partially completed LockMemory() on error.
 *
 * @param team    Team whose address space the vecs belong to.
 * @param count   Number of leading vecs to unlock.
 * @param isWrite Whether the original lock was for a write operation.
 */
void
IOBuffer::_UnlockMemory(team_id team, size_t count, bool isWrite)
{
	for (uint32 i = 0; i < count; i++) {
		unlock_memory_etc(team, (void*)(addr_t)fVecs[i].base, fVecs[i].length,
			isWrite ? 0 : B_READ_DEVICE);
	}
}


/**
 * @brief Unlocks all previously locked memory regions and clears the locked
 *        flag.
 *
 * @param team    Team whose address space the vecs belong to.
 * @param isWrite Whether the original lock was for a write operation.
 */
void
IOBuffer::UnlockMemory(team_id team, bool isWrite)
{
	if (!fMemoryLocked) {
		panic("memory not locked");
		return;
	}

	_UnlockMemory(team, fVecCount, isWrite);
	fMemoryLocked = false;
}


/**
 * @brief Prints a human-readable summary of the IOBuffer to the kernel
 *        debugger console.
 */
void
IOBuffer::Dump() const
{
	kprintf("IOBuffer at %p\n", this);

	kprintf("  origin:     %s\n", fUser ? "user" : "kernel");
	kprintf("  kind:       %s\n", fPhysical ? "physical" : "virtual");
	kprintf("  length:     %" B_PRIuGENADDR "\n", fLength);
	kprintf("  capacity:   %" B_PRIuSIZE "\n", fCapacity);
	kprintf("  vecs:       %" B_PRIuSIZE "\n", fVecCount);

	for (uint32 i = 0; i < fVecCount; i++) {
		kprintf("    [%" B_PRIu32 "] %#" B_PRIxGENADDR ", %" B_PRIuGENADDR "\n",
			i, fVecs[i].base, fVecs[i].length);
	}
}


// #pragma mark -


/**
 * @brief Records the final status of the operation and accumulates the number
 *        of bytes that were actually transferred relative to the original
 *        (untranslated) range.
 *
 * @param status           Completion status code.
 * @param completedLength  Bytes completed in the (possibly translated) range.
 */
void
IOOperation::SetStatus(status_t status, generic_size_t completedLength)
{
	IORequestChunk::SetStatus(status);
	if (IsWrite() == fParent->IsWrite()) {
		// Determine how many bytes we actually read or wrote,
		// relative to the original range, not the translated range.
		const generic_size_t partialBegin = (fOriginalOffset - fOffset);
		generic_size_t originalTransferredBytes = completedLength;
		if (originalTransferredBytes < partialBegin)
			originalTransferredBytes = 0;
		else
			originalTransferredBytes -= partialBegin;

		if (originalTransferredBytes > fOriginalLength)
			originalTransferredBytes = fOriginalLength;

		fTransferredBytes += originalTransferredBytes;
	}
}


/**
 * @brief Advances the operation through its multi-phase execution state
 *        machine and, when the final phase completes, copies any bounce-buffer
 *        data back to the caller's buffer.
 *
 * For partial writes the sequence is:
 *   1. PHASE_READ_BEGIN — read the first partial block so its unmodified
 *      prefix is preserved in the bounce buffer.
 *   2. PHASE_READ_END   — read the last partial block for the same reason.
 *   3. PHASE_DO_ALL     — perform the actual write with complete blocks.
 *
 * @return true if the operation is fully finished and should be retired;
 *         false if another phase is pending and the driver should be
 *         re-invoked.
 */
bool
IOOperation::Finish()
{
	TRACE("IOOperation::Finish()\n");

	if (fStatus == B_OK) {
		if (fParent->IsWrite()) {
			TRACE("  is write\n");
			if (fPhase == PHASE_READ_BEGIN) {
				TRACE("  phase read begin\n");
				// repair phase adjusted vec
				fDMABuffer->VecAt(fSavedVecIndex).length = fSavedVecLength;

				// partial write: copy partial begin to bounce buffer
				bool skipReadEndPhase;
				status_t error = _CopyPartialBegin(true, skipReadEndPhase);
				if (error == B_OK) {
					// We're done with the first phase only (read in begin).
					// Get ready for next phase...
					fPhase = HasPartialEnd() && !skipReadEndPhase
						? PHASE_READ_END : PHASE_DO_ALL;
					_PrepareVecs();
					ResetStatus();
						// TODO: Is there a race condition, if the request is
						// aborted at the same time?
					return false;
				}

				IORequestChunk::SetStatus(error);
			} else if (fPhase == PHASE_READ_END) {
				TRACE("  phase read end\n");
				// repair phase adjusted vec
				generic_io_vec& vec = fDMABuffer->VecAt(fSavedVecIndex);
				vec.base += vec.length - fSavedVecLength;
				vec.length = fSavedVecLength;

				// partial write: copy partial end to bounce buffer
				status_t error = _CopyPartialEnd(true);
				if (error == B_OK) {
					// We're done with the second phase only (read in end).
					// Get ready for next phase...
					fPhase = PHASE_DO_ALL;
					ResetStatus();
						// TODO: Is there a race condition, if the request is
						// aborted at the same time?
					return false;
				}

				IORequestChunk::SetStatus(error);
			}
		}
	}

	if (fParent->IsRead() && UsesBounceBuffer()) {
		TRACE("  read with bounce buffer\n");
		// copy the bounce buffer segments to the final location
		uint8* bounceBuffer = (uint8*)fDMABuffer->BounceBufferAddress();
		phys_addr_t bounceBufferStart
			= fDMABuffer->PhysicalBounceBufferAddress();
		phys_addr_t bounceBufferEnd = bounceBufferStart
			+ fDMABuffer->BounceBufferSize();

		const generic_io_vec* vecs = fDMABuffer->Vecs();
		uint32 vecCount = fDMABuffer->VecCount();

		status_t error = B_OK;

		// We iterate through the vecs we have read, moving offset (the device
		// offset) as we go. If [offset, offset + vec.length) intersects with
		// [startOffset, endOffset) we copy to the final location.
		off_t offset = fOffset;
		const off_t startOffset = fOriginalOffset;
		const off_t endOffset = fOriginalOffset + fOriginalLength;

		for (uint32 i = 0; error == B_OK && i < vecCount; i++) {
			const generic_io_vec& vec = vecs[i];
			generic_addr_t base = vec.base;
			generic_size_t length = vec.length;

			if (offset < startOffset) {
				// If the complete vector is before the start offset, skip it.
				if (offset + (off_t)length <= startOffset) {
					offset += length;
					continue;
				}

				// The vector starts before the start offset, but intersects
				// with it. Skip the part we aren't interested in.
				generic_size_t diff = startOffset - offset;
				offset += diff;
				base += diff;
				length -= diff;
			}

			if (offset + (off_t)length > endOffset) {
				// If we're already beyond the end offset, we're done.
				if (offset >= endOffset)
					break;

				// The vector extends beyond the end offset -- cut it.
				length = endOffset - offset;
			}

			if (base >= bounceBufferStart && base < bounceBufferEnd) {
				error = fParent->CopyData(
					bounceBuffer + (base - bounceBufferStart), offset, length);
			}

			offset += length;
		}

		if (error != B_OK)
			IORequestChunk::SetStatus(error);
	}

	return true;
}


/**
 * @brief Binds the operation to @p request, pre-fills bounce-buffer segments
 *        for write operations, sets the initial execution phase, and registers
 *        the operation with the parent request.
 *
 * SetPartial() must be called before Prepare() so that the partial-begin and
 * partial-end flags are already set.
 *
 * @param request  The owning IORequest; must not be NULL.
 * @retval B_OK        Preparation succeeded.
 * @retval B_NO_MEMORY A bounce-buffer copy failed due to an address fault.
 */
status_t
IOOperation::Prepare(IORequest* request)
{
	if (fParent != NULL)
		fParent->RemoveOperation(this);

	fParent = request;

	fTransferredBytes = 0;

	// set initial phase
	fPhase = PHASE_DO_ALL;
	if (fParent->IsWrite()) {
		// Copy data to bounce buffer segments, save the partial begin/end vec,
		// which will be copied after their respective read phase.
		if (UsesBounceBuffer()) {
			TRACE("  write with bounce buffer\n");
			uint8* bounceBuffer = (uint8*)fDMABuffer->BounceBufferAddress();
			phys_addr_t bounceBufferStart
				= fDMABuffer->PhysicalBounceBufferAddress();
			phys_addr_t bounceBufferEnd = bounceBufferStart
				+ fDMABuffer->BounceBufferSize();

			const generic_io_vec* vecs = fDMABuffer->Vecs();
			uint32 vecCount = fDMABuffer->VecCount();
			generic_size_t vecOffset = 0;
			uint32 i = 0;

			off_t offset = fOffset;
			off_t endOffset = fOffset + fLength;

			if (HasPartialBegin()) {
				// skip first block
				generic_size_t toSkip = fBlockSize;
				while (toSkip > 0) {
					if (vecs[i].length <= toSkip) {
						toSkip -= vecs[i].length;
						i++;
					} else {
						vecOffset = toSkip;
						break;
					}
				}

				offset += fBlockSize;
			}

			if (HasPartialEnd()) {
				// skip last block
				generic_size_t toSkip = fBlockSize;
				while (toSkip > 0) {
					if (vecs[vecCount - 1].length <= toSkip) {
						toSkip -= vecs[vecCount - 1].length;
						vecCount--;
					} else
						break;
				}

				endOffset -= fBlockSize;
			}

			for (; i < vecCount; i++) {
				const generic_io_vec& vec = vecs[i];
				generic_addr_t base = vec.base + vecOffset;
				generic_size_t length = vec.length - vecOffset;
				vecOffset = 0;

				if (base >= bounceBufferStart && base < bounceBufferEnd) {
					if (offset + (off_t)length > endOffset)
						length = endOffset - offset;
					status_t error = fParent->CopyData(offset,
						bounceBuffer + (base - bounceBufferStart), length);
					if (error != B_OK)
						return error;
				}

				offset += length;
			}
		}

		if (HasPartialBegin())
			fPhase = PHASE_READ_BEGIN;
		else if (HasPartialEnd())
			fPhase = PHASE_READ_END;

		_PrepareVecs();
	}

	ResetStatus();

	if (fParent != NULL)
		fParent->AddOperation(this);

	return B_OK;
}


/**
 * @brief Sets both the translated and original offset/length to the same
 *        values, establishing the initial range for a new operation.
 *
 * @param offset  Device byte offset where the operation starts.
 * @param length  Number of bytes covered by the operation.
 */
void
IOOperation::SetOriginalRange(off_t offset, generic_size_t length)
{
	fOriginalOffset = fOffset = offset;
	fOriginalLength = fLength = length;
}


/**
 * @brief Updates the translated (DMA-aligned) offset and length without
 *        touching the original range.
 *
 * @param offset  New translated device byte offset.
 * @param length  New translated byte count.
 */
void
IOOperation::SetRange(off_t offset, generic_size_t length)
{
	fOffset = offset;
	fLength = length;
}


/**
 * @brief Returns the device byte offset appropriate for the current execution
 *        phase.
 *
 * During PHASE_READ_END the offset is adjusted to point at the last partial
 * block rather than the start of the operation.
 *
 * @return Device offset in bytes.
 */
off_t
IOOperation::Offset() const
{
	return fPhase == PHASE_READ_END ? fOffset + fLength - fBlockSize : fOffset;
}


/**
 * @brief Returns the transfer length appropriate for the current execution
 *        phase.
 *
 * Single-block phases (READ_BEGIN / READ_END) return fBlockSize; the full
 * phase returns fLength.
 *
 * @return Number of bytes to transfer in the current phase.
 */
generic_size_t
IOOperation::Length() const
{
	return fPhase == PHASE_DO_ALL ? fLength : fBlockSize;
}


/**
 * @brief Returns a pointer to the first DMA scatter/gather vec that applies
 *        to the current execution phase.
 *
 * @return Pointer into the DMA buffer's vec array.
 */
generic_io_vec*
IOOperation::Vecs() const
{
	switch (fPhase) {
		case PHASE_READ_END:
			return fDMABuffer->Vecs() + fSavedVecIndex;
		case PHASE_READ_BEGIN:
		case PHASE_DO_ALL:
		default:
			return fDMABuffer->Vecs();
	}
}


/**
 * @brief Returns the number of scatter/gather vecs active in the current
 *        execution phase.
 *
 * @return Vec count for the current phase.
 */
uint32
IOOperation::VecCount() const
{
	switch (fPhase) {
		case PHASE_READ_BEGIN:
			return fSavedVecIndex + 1;
		case PHASE_READ_END:
			return fDMABuffer->VecCount() - fSavedVecIndex;
		case PHASE_DO_ALL:
		default:
			return fDMABuffer->VecCount();
	}
}


/**
 * @brief Records whether this operation covers a partial first block, a
 *        partial last block, or both.
 *
 * Must be called before Prepare().
 *
 * @param partialBegin  True if the first block is only partially written.
 * @param partialEnd    True if the last block is only partially written.
 */
void
IOOperation::SetPartial(bool partialBegin, bool partialEnd)
{
	TRACE("partial begin %d, end %d\n", partialBegin, partialEnd);
	fPartialBegin = partialBegin;
	fPartialEnd = partialEnd;
}


/**
 * @brief Returns true when this operation is performing the actual write
 *        phase (PHASE_DO_ALL) of a write request.
 *
 * @return true if this is an active write phase, false otherwise.
 */
bool
IOOperation::IsWrite() const
{
	return fParent->IsWrite() && fPhase == PHASE_DO_ALL;
}


/**
 * @brief Returns true when the parent request is a read operation.
 *
 * @return true if the parent IORequest is a read.
 */
bool
IOOperation::IsRead() const
{
	return fParent->IsRead();
}


/**
 * @brief Trims the DMA buffer's vec array to cover only the block(s) needed
 *        for the current phase (READ_BEGIN or READ_END), saving the original
 *        vec length so it can be restored later.
 */
void
IOOperation::_PrepareVecs()
{
	// we need to prepare the vecs for consumption by the drivers
	if (fPhase == PHASE_READ_BEGIN) {
		generic_io_vec* vecs = fDMABuffer->Vecs();
		uint32 vecCount = fDMABuffer->VecCount();
		generic_size_t vecLength = fBlockSize;
		for (uint32 i = 0; i < vecCount; i++) {
			generic_io_vec& vec = vecs[i];
			if (vec.length >= vecLength) {
				fSavedVecIndex = i;
				fSavedVecLength = vec.length;
				vec.length = vecLength;
				break;
			}
			vecLength -= vec.length;
		}
	} else if (fPhase == PHASE_READ_END) {
		generic_io_vec* vecs = fDMABuffer->Vecs();
		uint32 vecCount = fDMABuffer->VecCount();
		generic_size_t vecLength = fBlockSize;
		for (int32 i = vecCount - 1; i >= 0; i--) {
			generic_io_vec& vec = vecs[i];
			if (vec.length >= vecLength) {
				fSavedVecIndex = i;
				fSavedVecLength = vec.length;
				vec.base += vec.length - vecLength;
				vec.length = vecLength;
				break;
			}
			vecLength -= vec.length;
		}
	}
}


/**
 * @brief Copies the partial first-block data between the caller's buffer and
 *        the bounce buffer.
 *
 * @param isWrite          Direction: true copies from caller to bounce buffer,
 *                         false copies from bounce buffer to caller.
 * @param singleBlockOnly  Set to true on return if the entire original range
 *                         fits within a single block.
 * @retval B_OK  Copy succeeded.
 */
status_t
IOOperation::_CopyPartialBegin(bool isWrite, bool& singleBlockOnly)
{
	generic_size_t relativeOffset = OriginalOffset() - fOffset;
	generic_size_t length = fBlockSize - relativeOffset;

	singleBlockOnly = length >= OriginalLength();
	if (singleBlockOnly)
		length = OriginalLength();

	TRACE("_CopyPartialBegin(%s, single only %d)\n",
		isWrite ? "write" : "read", singleBlockOnly);

	if (isWrite) {
		return fParent->CopyData(OriginalOffset(),
			(uint8*)fDMABuffer->BounceBufferAddress() + relativeOffset, length);
	} else {
		return fParent->CopyData(
			(uint8*)fDMABuffer->BounceBufferAddress() + relativeOffset,
			OriginalOffset(), length);
	}
}


/**
 * @brief Copies the partial last-block data between the caller's buffer and
 *        the bounce buffer.
 *
 * @param isWrite  Direction: true copies from caller to bounce buffer,
 *                 false copies from bounce buffer to caller.
 * @retval B_OK  Copy succeeded.
 */
status_t
IOOperation::_CopyPartialEnd(bool isWrite)
{
	TRACE("_CopyPartialEnd(%s)\n", isWrite ? "write" : "read");

	const generic_io_vec& lastVec
		= fDMABuffer->VecAt(fDMABuffer->VecCount() - 1);
	off_t lastVecPos = fOffset + fLength - fBlockSize;
	uint8* base = (uint8*)fDMABuffer->BounceBufferAddress()
		+ (lastVec.base + lastVec.length - fBlockSize
		- fDMABuffer->PhysicalBounceBufferAddress());
		// NOTE: this won't work if we don't use the bounce buffer contiguously
		// (because of boundary alignments).
	generic_size_t length = OriginalOffset() + OriginalLength() - lastVecPos;

	if (isWrite)
		return fParent->CopyData(lastVecPos, base, length);

	return fParent->CopyData(base, lastVecPos, length);
}


/**
 * @brief Prints a detailed description of the IOOperation to the kernel
 *        debugger console.
 */
void
IOOperation::Dump() const
{
	kprintf("io_operation at %p\n", this);

	kprintf("  parent:           %p\n", fParent);
	kprintf("  status:           %s\n", strerror(fStatus));
	kprintf("  dma buffer:       %p\n", fDMABuffer);
	kprintf("  offset:           %-8" B_PRIdOFF " (original: %" B_PRIdOFF ")\n",
		fOffset, fOriginalOffset);
	kprintf("  length:           %-8" B_PRIuGENADDR " (original: %"
		B_PRIuGENADDR ")\n", fLength, fOriginalLength);
	kprintf("  transferred:      %" B_PRIuGENADDR "\n", fTransferredBytes);
	kprintf("  block size:       %" B_PRIuGENADDR "\n", fBlockSize);
	kprintf("  saved vec index:  %u\n", fSavedVecIndex);
	kprintf("  saved vec length: %u\n", fSavedVecLength);
	kprintf("  r/w:              %s\n", IsWrite() ? "write" : "read");
	kprintf("  phase:            %s\n", fPhase == PHASE_READ_BEGIN
		? "read begin" : fPhase == PHASE_READ_END ? "read end"
		: fPhase == PHASE_DO_ALL ? "do all" : "unknown");
	kprintf("  partial begin:    %s\n", fPartialBegin ? "yes" : "no");
	kprintf("  partial end:      %s\n", fPartialEnd ? "yes" : "no");
	kprintf("  bounce buffer:    %s\n", fUsesBounceBuffer ? "yes" : "no");

	set_debug_variable("_parent", (addr_t)fParent);
	set_debug_variable("_buffer", (addr_t)fDMABuffer);
}


// #pragma mark -


/**
 * @brief Default constructor — initialises the request lock and finished
 *        condition variable.
 */
IORequest::IORequest()
	:
	fIsNotified(false),
	fFinishedCallback(NULL),
	fFinishedCookie(NULL),
	fIterationCallback(NULL),
	fIterationCookie(NULL)
{
	mutex_init(&fLock, "I/O request lock");
	fFinishedCondition.Init(this, "I/O request finished");
}


/**
 * @brief Destructor — deletes all child sub-requests and frees the IOBuffer.
 */
IORequest::~IORequest()
{
	mutex_lock(&fLock);
	DeleteSubRequests();
	if (fBuffer != NULL)
		fBuffer->Delete();
	mutex_destroy(&fLock);
}


/**
 * @brief Allocates a new IORequest from the appropriate heap tier.
 *
 * @param vip  When true, allocates from the VIP heap so the allocation
 *             succeeds even under severe memory pressure.
 * @return Pointer to the new IORequest, or NULL on failure.
 */
/* static */ IORequest*
IORequest::Create(bool vip)
{
	return vip
		? new(malloc_flags(HEAP_PRIORITY_VIP)) IORequest
		: new(std::nothrow) IORequest;
}


/**
 * @brief Convenience initialiser for single-vector requests.
 *
 * Wraps the provided flat buffer address in a single generic_io_vec and
 * delegates to the multi-vector Init() overload.
 *
 * @param offset  Byte offset within the target file or device.
 * @param buffer  Base address of the data buffer.
 * @param length  Number of bytes to transfer.
 * @param write   True for write, false for read.
 * @param flags   Request flags (e.g. B_VIP_IO_REQUEST, B_PHYSICAL_IO_REQUEST).
 * @retval B_OK        Initialisation succeeded.
 * @retval B_NO_MEMORY Could not allocate the internal IOBuffer.
 */
status_t
IORequest::Init(off_t offset, generic_addr_t buffer, generic_size_t length,
	bool write, uint32 flags)
{
	ASSERT(offset >= 0);

	generic_io_vec vec;
	vec.base = buffer;
	vec.length = length;
	return Init(offset, &vec, 1, length, write, flags);
}


/**
 * @brief Full initialiser for scatter/gather requests.
 *
 * Creates and populates the internal IOBuffer, records thread/team ownership,
 * and resets all iteration and accounting fields.
 *
 * @param offset          Byte offset within the target file or device.
 * @param firstVecOffset  Bytes to skip at the beginning of the first vec.
 * @param lastVecSize     Byte count to keep from the last vec (0 = keep all).
 * @param vecs            Scatter/gather vector array.
 * @param count           Number of elements in @p vecs.
 * @param length          Total logical byte length of the transfer.
 * @param write           True for write, false for read.
 * @param flags           Request flags.
 * @retval B_OK        Initialisation succeeded.
 * @retval B_NO_MEMORY Could not allocate the internal IOBuffer.
 */
status_t
IORequest::Init(off_t offset, generic_size_t firstVecOffset,
	generic_size_t lastVecSize, const generic_io_vec* vecs, size_t count,
	generic_size_t length, bool write, uint32 flags)
{
	ASSERT(offset >= 0);

	fBuffer = IOBuffer::Create(count, (flags & B_VIP_IO_REQUEST) != 0);
	if (fBuffer == NULL)
		return B_NO_MEMORY;

	fBuffer->SetVecs(firstVecOffset, lastVecSize, vecs, count, length, flags);

	fOwner = NULL;
	fOffset = offset;
	fLength = length;
	fRelativeParentOffset = 0;
	fTransferSize = 0;
	fFlags = flags;
	Thread* thread = thread_get_current_thread();
	fTeam = thread->team->id;
	fThread = thread->id;
	fIsWrite = write;
	fPartialTransfer = false;
	fSuppressChildNotifications = false;

	// these are for iteration
	fVecIndex = 0;
	fVecOffset = 0;
	fRemainingBytes = length;

	fPendingChildren = 0;

	fStatus = 1;

	return B_OK;
}


/**
 * @brief Creates a child IORequest that covers a contiguous sub-range of this
 *        request's buffer and registers it as a pending child.
 *
 * The sub-request shares the parent's scatter/gather vecs (no copy) and
 * inherits team/thread ownership.
 *
 * @param parentOffset  Byte offset within the parent's logical range at which
 *                      the sub-request begins.
 * @param offset        Device offset for the sub-request.
 * @param length        Byte length of the sub-request.
 * @param _subRequest   Receives the newly created sub-request on success.
 * @retval B_OK        Sub-request created and linked successfully.
 * @retval B_NO_MEMORY Allocation of the sub-request failed.
 */
status_t
IORequest::CreateSubRequest(off_t parentOffset, off_t offset,
	generic_size_t length, IORequest*& _subRequest)
{
	ASSERT(parentOffset >= fOffset && length <= fLength
		&& parentOffset - fOffset <= (off_t)(fLength - length));

	// find start vec
	generic_size_t vecOffset = parentOffset - fOffset;
	generic_io_vec* vecs = fBuffer->Vecs();
	int32 vecCount = fBuffer->VecCount();
	int32 startVec = 0;
	for (; startVec < vecCount; startVec++) {
		const generic_io_vec& vec = vecs[startVec];
		if (vecOffset < vec.length)
			break;

		vecOffset -= vec.length;
	}

	// count vecs
	generic_size_t currentVecOffset = vecOffset;
	int32 endVec = startVec;
	generic_size_t remainingLength = length;
	for (; endVec < vecCount; endVec++) {
		const generic_io_vec& vec = vecs[endVec];
		if (vec.length - currentVecOffset >= remainingLength)
			break;

		remainingLength -= vec.length - currentVecOffset;
		currentVecOffset = 0;
	}

	// create subrequest
	IORequest* subRequest = Create((fFlags & B_VIP_IO_REQUEST) != 0);
	if (subRequest == NULL)
		return B_NO_MEMORY;

	status_t error = subRequest->Init(offset, vecOffset, remainingLength,
		vecs + startVec, endVec - startVec + 1, length, fIsWrite,
		fFlags & ~B_DELETE_IO_REQUEST);
	if (error != B_OK) {
		delete subRequest;
		return error;
	}

	subRequest->fRelativeParentOffset = parentOffset - fOffset;
	subRequest->fTeam = fTeam;
	subRequest->fThread = fThread;

	_subRequest = subRequest;
	subRequest->SetParent(this);

	MutexLocker _(fLock);

	fChildren.Add(subRequest);
	fPendingChildren++;
	TRACE("IORequest::CreateSubRequest(): request: %p, subrequest: %p\n", this,
		subRequest);

	return B_OK;
}


/**
 * @brief Destroys all child sub-requests and resets the pending-children
 *        counter to zero.
 */
void
IORequest::DeleteSubRequests()
{
	while (IORequestChunk* chunk = fChildren.RemoveHead())
		delete chunk;
	fPendingChildren = 0;
}


/**
 * @brief Registers a callback to be invoked when the request finishes.
 *
 * @param callback  Function pointer called on completion.
 * @param cookie    Opaque value forwarded as the first argument to @p callback.
 */
void
IORequest::SetFinishedCallback(io_request_finished_callback callback,
	void* cookie)
{
	fFinishedCallback = callback;
	fFinishedCookie = cookie;
}


/**
 * @brief Registers a callback that is called when the request needs to be
 *        iterated (e.g. to restart a partial transfer).
 *
 * @param callback  Iteration callback function pointer.
 * @param cookie    Opaque value forwarded to @p callback.
 */
void
IORequest::SetIterationCallback(io_request_iterate_callback callback,
	void* cookie)
{
	fIterationCallback = callback;
	fIterationCookie = cookie;
}


/**
 * @brief Returns the currently registered finished callback and its cookie.
 *
 * @param _cookie  If non-NULL, receives the cookie associated with the
 *                 callback.
 * @return The registered io_request_finished_callback, or NULL if none.
 */
io_request_finished_callback
IORequest::FinishedCallback(void** _cookie) const
{
	if (_cookie != NULL)
		*_cookie = fFinishedCookie;
	return fFinishedCallback;
}


/**
 * @brief Blocks the calling thread until the request has been notified as
 *        finished.
 *
 * @param flags    Wait flags passed to ConditionVariableEntry::Wait().
 * @param timeout  Maximum time to wait in microseconds.
 * @retval B_OK         The request completed successfully.
 * @retval B_TIMED_OUT  The timeout expired before completion.
 */
status_t
IORequest::Wait(uint32 flags, bigtime_t timeout)
{
	MutexLocker locker(fLock);

	if (IsFinished() && fIsNotified)
		return Status();

	ConditionVariableEntry entry;
	fFinishedCondition.Add(&entry);

	locker.Unlock();

	status_t error = entry.Wait(flags, timeout);
	if (error != B_OK)
		return error;

	return Status();
}


/**
 * @brief Drives the request to completion: handles iteration, unlocks locked
 *        memory, fires the finished callback, and notifies the parent
 *        request.
 *
 * If an iteration callback is registered and the transfer is incomplete, it
 * is invoked to continue the request before the final notification is sent.
 * This method may delete the request if the B_DELETE_IO_REQUEST flag is set.
 */
void
IORequest::NotifyFinished()
{
	TRACE("IORequest::NotifyFinished(): request: %p\n", this);

	MutexLocker locker(fLock);
	ASSERT(fStatus != 1);

	if (fStatus == B_OK && !fPartialTransfer && RemainingBytes() > 0) {
		// The request is not really done yet. If it has an iteration callback,
		// call it.
		if (fIterationCallback != NULL) {
			ResetStatus();
			locker.Unlock();
			bool partialTransfer = false;
			status_t error = fIterationCallback(fIterationCookie, this,
				&partialTransfer);
			if (error == B_OK && !partialTransfer)
				return;

			// Iteration failed, which means we're responsible for notifying the
			// requests finished.
			locker.Lock();
			fStatus = error;
			fPartialTransfer = true;
		}
	}

	ASSERT(!fIsNotified);
	ASSERT(fPendingChildren == 0);
	ASSERT(fChildren.IsEmpty()
		|| dynamic_cast<IOOperation*>(fChildren.Head()) == NULL);
	ASSERT(fTransferSize <= fLength);

	// unlock the memory
	if (fBuffer->IsMemoryLocked())
		fBuffer->UnlockMemory(fTeam, fIsWrite);

	// Cache the callbacks before we unblock waiters and unlock. Any of the
	// following could delete this request, so we don't want to touch it
	// once we have started telling others that it is done.
	IORequest* parent = fParent;
	io_request_finished_callback finishedCallback = fFinishedCallback;
	void* finishedCookie = fFinishedCookie;
	status_t status = fStatus;
	generic_size_t transferredBytes = fTransferSize;
	generic_size_t lastTransferredOffset
		= fRelativeParentOffset + transferredBytes;
	bool partialTransfer = status != B_OK || fPartialTransfer;
	bool deleteRequest = (fFlags & B_DELETE_IO_REQUEST) != 0;

	// unblock waiters
	fIsNotified = true;
	fFinishedCondition.NotifyAll();

	locker.Unlock();

	// notify callback
	if (finishedCallback != NULL) {
		finishedCallback(finishedCookie, this, status, partialTransfer,
			transferredBytes);
	}

	// notify parent
	if (parent != NULL) {
		parent->SubRequestFinished(this, status, partialTransfer,
			lastTransferredOffset);
	}

	if (deleteRequest)
		delete this;
}


/**
 * @brief Returns whether this request or any ancestor has a finished or
 *        iteration callback registered.
 *
 * Used to determine whether NotifyFinished() must be dispatched to a
 * separate thread rather than called synchronously.
 *
 * @return true if at least one callback is registered in the ancestor chain.
 */
bool
IORequest::HasCallbacks() const
{
	if (fFinishedCallback != NULL || fIterationCallback != NULL)
		return true;

	return fParent != NULL && fParent->HasCallbacks();
}


/**
 * @brief Atomically sets the request status and immediately triggers the
 *        finished notification chain.
 *
 * If the request status has already been set (i.e. it is no longer the
 * sentinel value 1) this call is a no-op.
 *
 * @param status  The completion status to record (e.g. B_OK or an error).
 */
void
IORequest::SetStatusAndNotify(status_t status)
{
	MutexLocker locker(fLock);

	if (fStatus != 1)
		return;

	fStatus = status;

	locker.Unlock();

	NotifyFinished();
}


/**
 * @brief Called by an IOOperation when it has finished executing; updates
 *        transfer accounting and triggers NotifyFinished() when the last
 *        child operation completes.
 *
 * @param operation  The operation that has just finished.
 */
void
IORequest::OperationFinished(IOOperation* operation)
{
	TRACE("IORequest::OperationFinished(%p, %#" B_PRIx32 "): request: %p\n",
		operation, operation->Status(), this);

	MutexLocker locker(fLock);

	fChildren.Remove(operation);
	operation->SetParent(NULL);

	const status_t status = operation->Status();
	const bool partialTransfer =
		(operation->TransferredBytes() < operation->OriginalLength());
	const generic_size_t transferEndOffset =
		(operation->OriginalOffset() - Offset()) + operation->TransferredBytes();

	if (status != B_OK || partialTransfer) {
		if (fTransferSize > transferEndOffset)
			fTransferSize = transferEndOffset;
		fPartialTransfer = true;
	}

	if (status != B_OK && fStatus == 1)
		fStatus = status;

	if (--fPendingChildren > 0)
		return;

	// last child finished

	// set status, if not done yet
	if (fStatus == 1)
		fStatus = B_OK;
}


/**
 * @brief Called by a child sub-request when it finishes; propagates status
 *        and triggers NotifyFinished() when the last pending child is done.
 *
 * @param request              The sub-request that finished.
 * @param status               Completion status of the sub-request.
 * @param partialTransfer      True if the sub-request transferred fewer bytes
 *                             than requested.
 * @param transferEndOffset    Byte offset relative to this request's start at
 *                             which the sub-request's transfer ended.
 */
void
IORequest::SubRequestFinished(IORequest* request, status_t status,
	bool partialTransfer, generic_size_t transferEndOffset)
{
	TRACE("IORequest::SubrequestFinished(%p, %#" B_PRIx32 ", %d, %" B_PRIuGENADDR
		"): request: %p\n", request, status, partialTransfer, transferEndOffset, this);

	MutexLocker locker(fLock);

	if (status != B_OK || partialTransfer) {
		if (fTransferSize > transferEndOffset)
			fTransferSize = transferEndOffset;
		fPartialTransfer = true;
	}

	if (status != B_OK && fStatus == 1)
		fStatus = status;

	if (--fPendingChildren > 0 || fSuppressChildNotifications)
		return;

	// last child finished

	// set status, if not done yet
	if (fStatus == 1)
		fStatus = B_OK;

	locker.Unlock();

	NotifyFinished();
}


/**
 * @brief Resets the request status back to the "in progress" sentinel (1) so
 *        that the request can be resubmitted.
 */
void
IORequest::SetUnfinished()
{
	MutexLocker _(fLock);
	ResetStatus();
}


/**
 * @brief Directly sets the transferred-bytes count and the partial-transfer
 *        flag, bypassing the normal child-notification accounting.
 *
 * @param partialTransfer   True if fewer bytes were transferred than requested.
 * @param transferredBytes  Number of bytes successfully transferred.
 */
void
IORequest::SetTransferredBytes(bool partialTransfer,
	generic_size_t transferredBytes)
{
	TRACE("%p->IORequest::SetTransferredBytes(%d, %" B_PRIuGENADDR ")\n", this,
		partialTransfer, transferredBytes);

	MutexLocker _(fLock);

	fPartialTransfer = partialTransfer;
	fTransferSize = transferredBytes;
}


/**
 * @brief Controls whether child sub-requests call NotifyFinished() on this
 *        request when they complete.
 *
 * Setting this to true is useful when the caller wants to batch child
 * completions and drive the parent notification manually.
 *
 * @param suppress  True to suppress automatic notifications from children.
 */
void
IORequest::SetSuppressChildNotifications(bool suppress)
{
	fSuppressChildNotifications = suppress;
}


/**
 * @brief Advances the iteration cursor by @p bySize bytes, updating the
 *        current vec index/offset and the remaining-bytes / transfer-size
 *        counters.
 *
 * @param bySize  Number of bytes to advance.
 */
void
IORequest::Advance(generic_size_t bySize)
{
	TRACE("IORequest::Advance(%" B_PRIuGENADDR "): remaining: %" B_PRIuGENADDR
		" -> %" B_PRIuGENADDR "\n", bySize, fRemainingBytes,
		fRemainingBytes - bySize);
	fRemainingBytes -= bySize;
	fTransferSize += bySize;

	generic_io_vec* vecs = fBuffer->Vecs();
	uint32 vecCount = fBuffer->VecCount();
	while (fVecIndex < vecCount
			&& vecs[fVecIndex].length - fVecOffset <= bySize) {
		bySize -= vecs[fVecIndex].length - fVecOffset;
		fVecOffset = 0;
		fVecIndex++;
	}

	fVecOffset += bySize;
}


/**
 * @brief Returns the first child sub-request, or NULL if there are none.
 *
 * @return Pointer to the first IORequest child, or NULL.
 */
IORequest*
IORequest::FirstSubRequest()
{
	return dynamic_cast<IORequest*>(fChildren.Head());
}


/**
 * @brief Returns the child sub-request that follows @p previous in the
 *        children list.
 *
 * @param previous  A sub-request previously returned by FirstSubRequest() or
 *                  NextSubRequest().
 * @return The next sibling sub-request, or NULL if @p previous was the last.
 */
IORequest*
IORequest::NextSubRequest(IORequest* previous)
{
	if (previous == NULL)
		return NULL;
	return dynamic_cast<IORequest*>(fChildren.GetNext(previous));
}


/**
 * @brief Appends an IOOperation to the children list and increments the
 *        pending-children counter.
 *
 * @param operation  The operation to add.
 */
void
IORequest::AddOperation(IOOperation* operation)
{
	MutexLocker locker(fLock);
	TRACE("IORequest::AddOperation(%p): request: %p\n", operation, this);
	fChildren.Add(operation);
	fPendingChildren++;
}


/**
 * @brief Removes an IOOperation from the children list without decrementing
 *        the pending-children counter or triggering any notification.
 *
 * @param operation  The operation to remove.
 */
void
IORequest::RemoveOperation(IOOperation* operation)
{
	MutexLocker locker(fLock);
	fChildren.Remove(operation);
	operation->SetParent(NULL);
}


/**
 * @brief Copies data from the request's buffer into @p buffer (read-out
 *        direction).
 *
 * @param offset  Logical byte offset within the request's range.
 * @param buffer  Destination buffer in kernel memory.
 * @param size    Number of bytes to copy.
 * @retval B_OK        Copy succeeded.
 * @retval B_BAD_VALUE The requested range falls outside the request's bounds.
 */
status_t
IORequest::CopyData(off_t offset, void* buffer, size_t size)
{
	return _CopyData(buffer, offset, size, true);
}


/**
 * @brief Copies data from @p buffer into the request's buffer (write-in
 *        direction).
 *
 * @param buffer  Source buffer in kernel memory.
 * @param offset  Logical byte offset within the request's range.
 * @param size    Number of bytes to copy.
 * @retval B_OK        Copy succeeded.
 * @retval B_BAD_VALUE The requested range falls outside the request's bounds.
 */
status_t
IORequest::CopyData(const void* buffer, off_t offset, size_t size)
{
	return _CopyData((void*)buffer, offset, size, false);
}


/**
 * @brief Zeroes a sub-range of the request's buffer, selecting the
 *        appropriate clear function based on whether the buffer is physical,
 *        user-space virtual, or kernel virtual.
 *
 * @param offset  Logical byte offset within the request's range to start
 *                clearing.
 * @param size    Number of bytes to zero.
 * @retval B_OK        Clear succeeded.
 * @retval B_BAD_VALUE The range falls outside the request's bounds.
 */
status_t
IORequest::ClearData(off_t offset, generic_size_t size)
{
	if (size == 0)
		return B_OK;

	if (offset < fOffset || offset + (off_t)size > fOffset + (off_t)fLength) {
		panic("IORequest::ClearData(): invalid range: (%" B_PRIdOFF
			", %" B_PRIuGENADDR ")", offset, size);
		return B_BAD_VALUE;
	}

	// If we can, we directly copy from/to the virtual buffer. The memory is
	// locked in this case.
	status_t (*clearFunction)(generic_addr_t, generic_size_t, team_id);
	if (fBuffer->IsPhysical()) {
		clearFunction = &IORequest::_ClearDataPhysical;
	} else {
		clearFunction = fBuffer->IsUser() && fTeam != team_get_current_team_id()
			? &IORequest::_ClearDataUser : &IORequest::_ClearDataSimple;
	}

	// skip bytes if requested
	generic_io_vec* vecs = fBuffer->Vecs();
	generic_size_t skipBytes = offset - fOffset;
	generic_size_t vecOffset = 0;
	while (skipBytes > 0) {
		if (vecs[0].length > skipBytes) {
			vecOffset = skipBytes;
			break;
		}

		skipBytes -= vecs[0].length;
		vecs++;
	}

	// clear vector-wise
	while (size > 0) {
		generic_size_t toClear = min_c(size, vecs[0].length - vecOffset);
		status_t error = clearFunction(vecs[0].base + vecOffset, toClear,
			fTeam);
		if (error != B_OK)
			return error;

		size -= toClear;
		vecs++;
		vecOffset = 0;
	}

	return B_OK;

}


/**
 * @brief Internal workhorse that performs the actual vec-by-vec copy between
 *        a bounce buffer and the request's scatter/gather buffer.
 *
 * Selects between physical, user-space, and simple copy helpers based on the
 * buffer type and the owning team.
 *
 * @param _buffer  Bounce-buffer address in kernel memory.
 * @param offset   Logical byte offset within the request's range.
 * @param size     Number of bytes to transfer.
 * @param copyIn   True to copy from the request buffer into @p _buffer
 *                 (read direction); false for the opposite direction.
 * @retval B_OK        Transfer succeeded.
 * @retval B_BAD_VALUE The range falls outside the request's bounds.
 */
status_t
IORequest::_CopyData(void* _buffer, off_t offset, size_t size, bool copyIn)
{
	if (size == 0)
		return B_OK;

	uint8* buffer = (uint8*)_buffer;

	if (offset < fOffset || offset + (off_t)size > fOffset + (off_t)fLength) {
		panic("IORequest::_CopyData(): invalid range: (%" B_PRIdOFF ", %lu)",
			offset, size);
		return B_BAD_VALUE;
	}

	// If we can, we directly copy from/to the virtual buffer. The memory is
	// locked in this case.
	status_t (*copyFunction)(void*, generic_addr_t, size_t, team_id, bool);
	if (fBuffer->IsPhysical()) {
		copyFunction = &IORequest::_CopyPhysical;
	} else {
		copyFunction = fBuffer->IsUser() && fTeam != team_get_current_team_id()
			? &IORequest::_CopyUser : &IORequest::_CopySimple;
	}

	// skip bytes if requested
	generic_io_vec* vecs = fBuffer->Vecs();
	generic_size_t skipBytes = offset - fOffset;
	generic_size_t vecOffset = 0;
	while (skipBytes > 0) {
		if (vecs[0].length > skipBytes) {
			vecOffset = skipBytes;
			break;
		}

		skipBytes -= vecs[0].length;
		vecs++;
	}

	// copy vector-wise
	while (size > 0) {
		generic_size_t toCopy = min_c(size, vecs[0].length - vecOffset);
		status_t error = copyFunction(buffer, vecs[0].base + vecOffset, toCopy,
			fTeam, copyIn);
		if (error != B_OK)
			return error;

		buffer += toCopy;
		size -= toCopy;
		vecs++;
		vecOffset = 0;
	}

	return B_OK;
}


/**
 * @brief Copies between a bounce buffer and a virtual (kernel or user)
 *        address, using user_memcpy() when the destination is in user space.
 *
 * @param bounceBuffer  Kernel-space bounce buffer.
 * @param external      Virtual address to copy to/from.
 * @param size          Number of bytes.
 * @param team          Unused; present for function-pointer uniformity.
 * @param copyIn        True to copy from @p external into @p bounceBuffer.
 * @retval B_OK  Copy succeeded.
 */
/* static */ status_t
IORequest::_CopySimple(void* bounceBuffer, generic_addr_t external, size_t size,
	team_id team, bool copyIn)
{
	TRACE("  IORequest::_CopySimple(%p, %#" B_PRIxGENADDR ", %lu, %d)\n",
		bounceBuffer, external, size, copyIn);
	if (IS_USER_ADDRESS(external)) {
		status_t status = B_OK;
		if (copyIn)
			status = user_memcpy(bounceBuffer, (void*)(addr_t)external, size);
		else
			status = user_memcpy((void*)(addr_t)external, bounceBuffer, size);
		if (status < B_OK)
			return status;
		return B_OK;
	}
	if (copyIn)
		memcpy(bounceBuffer, (void*)(addr_t)external, size);
	else
		memcpy((void*)(addr_t)external, bounceBuffer, size);
	return B_OK;
}


/**
 * @brief Copies between a bounce buffer and a physical address using
 *        vm_memcpy_from_physical() / vm_memcpy_to_physical().
 *
 * @param bounceBuffer  Kernel-virtual bounce buffer.
 * @param external      Physical address to copy to/from.
 * @param size          Number of bytes.
 * @param team          Unused; present for function-pointer uniformity.
 * @param copyIn        True to copy from physical memory into @p bounceBuffer.
 * @retval B_OK  Copy succeeded.
 */
/* static */ status_t
IORequest::_CopyPhysical(void* bounceBuffer, generic_addr_t external,
	size_t size, team_id team, bool copyIn)
{
	if (copyIn)
		return vm_memcpy_from_physical(bounceBuffer, external, size, false);

	return vm_memcpy_to_physical(external, bounceBuffer, size, false);
}


/**
 * @brief Copies between a bounce buffer and a user-space virtual address
 *        by resolving the user pages through get_memory_map_etc() and then
 *        calling _CopyPhysical() on each physical entry.
 *
 * @param _bounceBuffer  Kernel-space bounce buffer.
 * @param _external      User-space virtual base address.
 * @param size           Total number of bytes to copy.
 * @param team           Team whose address space @p _external belongs to.
 * @param copyIn         True to copy from user space into @p _bounceBuffer.
 * @retval B_OK           Copy succeeded.
 * @retval B_BAD_ADDRESS  Could not resolve the user-space address.
 */
/* static */ status_t
IORequest::_CopyUser(void* _bounceBuffer, generic_addr_t _external, size_t size,
	team_id team, bool copyIn)
{
	uint8* bounceBuffer = (uint8*)_bounceBuffer;
	uint8* external = (uint8*)(addr_t)_external;

	while (size > 0) {
		static const int32 kEntryCount = 8;
		physical_entry entries[kEntryCount];

		uint32 count = kEntryCount;
		status_t error = get_memory_map_etc(team, external, size, entries,
			&count);
		if (error != B_OK && error != B_BUFFER_OVERFLOW) {
			panic("IORequest::_CopyUser(): Failed to get physical memory for "
				"user memory %p\n", external);
			return B_BAD_ADDRESS;
		}

		for (uint32 i = 0; i < count; i++) {
			const physical_entry& entry = entries[i];
			error = _CopyPhysical(bounceBuffer, entry.address, entry.size, team,
				copyIn);
			if (error != B_OK)
				return error;

			size -= entry.size;
			bounceBuffer += entry.size;
			external += entry.size;
		}
	}

	return B_OK;
}


/**
 * @brief Zeroes a kernel-virtual memory region using memset().
 *
 * @param external  Kernel virtual address to zero.
 * @param size      Number of bytes to zero.
 * @param team      Unused; present for function-pointer uniformity.
 * @retval B_OK  Always succeeds.
 */
/*static*/ status_t
IORequest::_ClearDataSimple(generic_addr_t external, generic_size_t size,
	team_id team)
{
	memset((void*)(addr_t)external, 0, (size_t)size);
	return B_OK;
}


/**
 * @brief Zeroes a physical memory region via vm_memset_physical().
 *
 * @param external  Physical address to zero.
 * @param size      Number of bytes to zero.
 * @param team      Unused; present for function-pointer uniformity.
 * @retval B_OK  On success.
 */
/*static*/ status_t
IORequest::_ClearDataPhysical(generic_addr_t external, generic_size_t size,
	team_id team)
{
	return vm_memset_physical((phys_addr_t)external, 0, (phys_size_t)size);
}


/**
 * @brief Zeroes a user-space virtual memory region by resolving its physical
 *        pages through get_memory_map_etc() and calling _ClearDataPhysical()
 *        on each entry.
 *
 * @param _external  User-space virtual base address to zero.
 * @param size       Number of bytes to zero.
 * @param team       Team whose address space @p _external belongs to.
 * @retval B_OK           Succeeded.
 * @retval B_BAD_ADDRESS  Could not resolve the user-space address.
 */
/*static*/ status_t
IORequest::_ClearDataUser(generic_addr_t _external, generic_size_t size,
	team_id team)
{
	uint8* external = (uint8*)(addr_t)_external;

	while (size > 0) {
		static const int32 kEntryCount = 8;
		physical_entry entries[kEntryCount];

		uint32 count = kEntryCount;
		status_t error = get_memory_map_etc(team, external, size, entries,
			&count);
		if (error != B_OK && error != B_BUFFER_OVERFLOW) {
			panic("IORequest::_ClearDataUser(): Failed to get physical memory "
				"for user memory %p\n", external);
			return B_BAD_ADDRESS;
		}

		for (uint32 i = 0; i < count; i++) {
			const physical_entry& entry = entries[i];
			error = _ClearDataPhysical(entry.address, entry.size, team);
			if (error != B_OK)
				return error;

			size -= entry.size;
			external += entry.size;
		}
	}

	return B_OK;
}


/**
 * @brief Prints a comprehensive dump of the IORequest's state to the kernel
 *        debugger console, including buffer, offset, flags, children, and
 *        iteration state.
 */
void
IORequest::Dump() const
{
	kprintf("io_request at %p\n", this);

	kprintf("  owner:             %p\n", fOwner);
	kprintf("  parent:            %p\n", fParent);
	kprintf("  status:            %s\n", strerror(fStatus));
	kprintf("  mutex:             %p\n", &fLock);
	kprintf("  IOBuffer:          %p\n", fBuffer);
	kprintf("  offset:            %" B_PRIdOFF "\n", fOffset);
	kprintf("  length:            %" B_PRIuGENADDR "\n", fLength);
	kprintf("  transfer size:     %" B_PRIuGENADDR "\n", fTransferSize);
	kprintf("  relative offset:   %" B_PRIuGENADDR "\n", fRelativeParentOffset);
	kprintf("  pending children:  %" B_PRId32 "\n", fPendingChildren);
	kprintf("  flags:             %#" B_PRIx32 "\n", fFlags);
	kprintf("  team:              %" B_PRId32 "\n", fTeam);
	kprintf("  thread:            %" B_PRId32 "\n", fThread);
	kprintf("  r/w:               %s\n", fIsWrite ? "write" : "read");
	kprintf("  partial transfer:  %s\n", fPartialTransfer ? "yes" : "no");
	kprintf("  finished cvar:     %p\n", &fFinishedCondition);
	kprintf("  iteration:\n");
	kprintf("    vec index:       %" B_PRIu32 "\n", fVecIndex);
	kprintf("    vec offset:      %" B_PRIuGENADDR "\n", fVecOffset);
	kprintf("    remaining bytes: %" B_PRIuGENADDR "\n", fRemainingBytes);
	kprintf("  callbacks:\n");
	kprintf("    finished %p, cookie %p\n", fFinishedCallback, fFinishedCookie);
	kprintf("    iteration %p, cookie %p\n", fIterationCallback,
		fIterationCookie);
	kprintf("  children:\n");

	IORequestChunkList::ConstIterator iterator = fChildren.GetIterator();
	while (iterator.HasNext()) {
		kprintf("    %p\n", iterator.Next());
	}

	set_debug_variable("_parent", (addr_t)fParent);
	set_debug_variable("_mutex", (addr_t)&fLock);
	set_debug_variable("_buffer", (addr_t)fBuffer);
	set_debug_variable("_cvar", (addr_t)&fFinishedCondition);
}
