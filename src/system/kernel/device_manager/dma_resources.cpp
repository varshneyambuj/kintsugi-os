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
 *   Copyright 2008, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file dma_resources.cpp
 * @brief DMA resource management — splitting I/O requests to satisfy DMA constraints.
 *
 * DMAResource takes a generic IORequest and splits it into DMABuffer objects
 * that comply with the hardware DMA constraints (address range, alignment,
 * boundary, scatter-gather limits). Handles bounce-buffer allocation for
 * transfers that cannot be done directly.
 *
 * @see IORequest.cpp, IOSchedulerSimple.cpp
 */


#include "dma_resources.h"

#include <device_manager.h>

#include <kernel.h>
#include <util/AutoLock.h>
#include <vm/vm.h>

#include "IORequest.h"


//#define TRACE_DMA_RESOURCE
#ifdef TRACE_DMA_RESOURCE
#	define TRACE(x...) dprintf(x)
#else
#	define TRACE(x...) ;
#endif


extern device_manager_info gDeviceManagerModule;

const phys_size_t kMaxBounceBufferSize = 4 * B_PAGE_SIZE;


/**
 * @brief Allocates a new DMABuffer with room for @a count scatter-gather vectors.
 *
 * @param count Number of generic_io_vec slots to pre-allocate.
 * @return Pointer to the newly allocated DMABuffer, or @c NULL on allocation failure.
 * @note The returned buffer is not yet associated with a bounce buffer; call
 *       SetBounceBuffer() before use if bounce memory is required.
 */
DMABuffer*
DMABuffer::Create(size_t count)
{
	DMABuffer* buffer = (DMABuffer*)malloc(
		sizeof(DMABuffer) + sizeof(generic_io_vec) * (count - 1));
	if (buffer == NULL)
		return NULL;

	buffer->fVecCount = count;

	return buffer;
}


/**
 * @brief Overrides the number of valid scatter-gather vectors in this buffer.
 *
 * @param count New vector count; must not exceed the capacity set at creation.
 */
void
DMABuffer::SetVecCount(uint32 count)
{
	fVecCount = count;
}


/**
 * @brief Appends a new scatter-gather vector to the buffer.
 *
 * @param base Physical base address of the memory region.
 * @param size Byte length of the memory region.
 * @note The caller must ensure that fVecCount has not already reached the
 *       buffer's capacity before calling this function.
 */
void
DMABuffer::AddVec(generic_addr_t base, generic_size_t size)
{
	generic_io_vec& vec = fVecs[fVecCount++];
	vec.base = base;
	vec.length = size;
}


/**
 * @brief Tests whether the scatter-gather entry at @a index resides inside the
 *        associated bounce buffer.
 *
 * @param index Zero-based index into the vector array.
 * @return @c true if the entry at @a index falls within the bounce buffer's
 *         physical address range; @c false if the index is out of range or no
 *         bounce buffer is attached.
 */
bool
DMABuffer::UsesBounceBufferAt(uint32 index)
{
	if (index >= fVecCount || fBounceBuffer == NULL)
		return false;

	return fVecs[index].base >= fBounceBuffer->physical_address
		&& fVecs[index].base
				< fBounceBuffer->physical_address + fBounceBuffer->size;
}


/**
 * @brief Prints a human-readable description of this DMABuffer to the kernel
 *        debugger console.
 *
 * @note Must only be called from kernel debugger context (kprintf is used).
 */
void
DMABuffer::Dump() const
{
	kprintf("DMABuffer at %p\n", this);

	kprintf("  bounce buffer:      %p (physical %#" B_PRIxPHYSADDR ")\n",
		fBounceBuffer->address, fBounceBuffer->physical_address);
	kprintf("  bounce buffer size: %" B_PRIxPHYSADDR "\n", fBounceBuffer->size);
	kprintf("  vecs:               %" B_PRIu32 "\n", fVecCount);

	for (uint32 i = 0; i < fVecCount; i++) {
		kprintf("    [%" B_PRIu32 "] %#" B_PRIxGENADDR ", %" B_PRIuGENADDR "\n",
			i, fVecs[i].base, fVecs[i].length);
	}
}


//	#pragma mark -


/**
 * @brief Constructs a DMAResource with zeroed state and initialises the
 *        internal mutex.
 *
 * @note Call Init() before using any other method on the object.
 */
DMAResource::DMAResource()
	:
	fBlockSize(0),
	fScratchVecs(NULL)
{
	mutex_init(&fLock, "dma resource");
}


/**
 * @brief Destroys the DMAResource, releasing its mutex and scratch vector array.
 *
 * @note DMABuffer and DMABounceBuffer objects are currently not freed here
 *       (see in-source TODO). Callers must ensure that no DMA operations are
 *       in flight when the destructor runs.
 */
DMAResource::~DMAResource()
{
	mutex_lock(&fLock);
	mutex_destroy(&fLock);
	free(fScratchVecs);

// TODO: Delete DMABuffers and BounceBuffers!
}


/**
 * @brief Initialises the DMAResource by reading DMA constraint attributes from
 *        a device node and forwarding them to the restrictions-based overload.
 *
 * @param node         Device node whose attributes describe the hardware DMA
 *                     constraints (alignment, boundary, address range, segment
 *                     count, transfer size).
 * @param blockSize    Physical block size of the device; used to derive
 *                     segment-size restrictions from block-count attributes.
 * @param bufferCount  Number of DMABuffer objects to pre-allocate.
 * @param bounceBufferCount Number of DMABounceBuffer objects to pre-allocate.
 * @retval B_OK        Initialisation succeeded.
 * @retval B_NO_MEMORY Scratch vector or buffer allocation failed.
 * @note This overload queries optional node attributes; missing attributes are
 *       treated as "no restriction" and filled in by the restrictions-based
 *       overload.
 */
status_t
DMAResource::Init(device_node* node, generic_size_t blockSize,
	uint32 bufferCount, uint32 bounceBufferCount)
{
	dma_restrictions restrictions;
	memset(&restrictions, 0, sizeof(dma_restrictions));

	// TODO: add DMA attributes instead of reusing block_io's

	uint32 value;
	if (gDeviceManagerModule.get_attr_uint32(node,
			B_DMA_ALIGNMENT, &value, true) == B_OK)
		restrictions.alignment = (generic_size_t)value + 1;

	if (gDeviceManagerModule.get_attr_uint32(node,
			B_DMA_BOUNDARY, &value, true) == B_OK)
		restrictions.boundary = (generic_size_t)value + 1;

	if (gDeviceManagerModule.get_attr_uint32(node,
			B_DMA_MAX_SEGMENT_BLOCKS, &value, true) == B_OK)
		restrictions.max_segment_size = (generic_size_t)value * blockSize;

	if (gDeviceManagerModule.get_attr_uint32(node,
			B_DMA_MAX_TRANSFER_BLOCKS, &value, true) == B_OK)
		restrictions.max_transfer_size = (generic_size_t)value * blockSize;

	if (gDeviceManagerModule.get_attr_uint32(node,
			B_DMA_MAX_SEGMENT_COUNT, &value, true) == B_OK)
		restrictions.max_segment_count = value;

	uint64 value64;
	if (gDeviceManagerModule.get_attr_uint64(node,
			B_DMA_LOW_ADDRESS, &value64, true) == B_OK) {
		restrictions.low_address = value64;
	}

	if (gDeviceManagerModule.get_attr_uint64(node,
			B_DMA_HIGH_ADDRESS, &value64, true) == B_OK) {
		restrictions.high_address = value64;
	}

	return Init(restrictions, blockSize, bufferCount, bounceBufferCount);
}


/**
 * @brief Initialises the DMAResource from an explicit @c dma_restrictions
 *        structure, pre-allocating DMA and bounce buffers.
 *
 * @param restrictions Hardware DMA constraints; zero fields are filled in with
 *                     safe defaults (e.g. max_segment_count defaults to 16).
 * @param blockSize    Physical block size; set to 1 if the device is not
 *                     block-oriented.
 * @param bufferCount  Number of DMABuffer objects to pre-allocate.
 * @param bounceBufferCount Number of DMABounceBuffer objects to pre-allocate.
 * @retval B_OK        Initialisation succeeded.
 * @retval B_NO_MEMORY A buffer or scratch-vector allocation failed.
 * @note The @c fLock mutex must not be held when calling this function.
 *       @c blockSize must be a power of two and must be >= @c restrictions.alignment.
 */
status_t
DMAResource::Init(const dma_restrictions& restrictions,
	generic_size_t blockSize, uint32 bufferCount, uint32 bounceBufferCount)
{
	ASSERT(restrictions.alignment <= blockSize);
	ASSERT(fBlockSize == 0);

	fRestrictions = restrictions;
	fBlockSize = blockSize == 0 ? 1 : blockSize;
	fBufferCount = bufferCount;
	fBounceBufferCount = bounceBufferCount;
	fBounceBufferSize = 0;

	if (fRestrictions.high_address == 0)
		fRestrictions.high_address = ~(generic_addr_t)0;
	if (fRestrictions.max_segment_count == 0)
		fRestrictions.max_segment_count = 16;
	if (fRestrictions.alignment == 0)
		fRestrictions.alignment = 1;
	if (fRestrictions.max_transfer_size == 0)
		fRestrictions.max_transfer_size = ~(generic_size_t)0;
	if (fRestrictions.max_segment_size == 0)
		fRestrictions.max_segment_size = ~(generic_size_t)0;

	if (_NeedsBoundsBuffers()) {
		fBounceBufferSize = fRestrictions.max_segment_size
			* min_c(fRestrictions.max_segment_count, 4);
		if (fBounceBufferSize > kMaxBounceBufferSize)
			fBounceBufferSize = kMaxBounceBufferSize;
		TRACE("DMAResource::Init(): chose bounce buffer size %lu\n",
			fBounceBufferSize);
	}

	dprintf("DMAResource@%p: low/high %" B_PRIxGENADDR "/%" B_PRIxGENADDR
		", max segment count %" B_PRIu32 ", align %" B_PRIuGENADDR ", "
		"boundary %" B_PRIuGENADDR ", max transfer %" B_PRIuGENADDR
		", max segment size %" B_PRIuGENADDR "\n", this,
		fRestrictions.low_address, fRestrictions.high_address,
		fRestrictions.max_segment_count, fRestrictions.alignment,
		fRestrictions.boundary, fRestrictions.max_transfer_size,
		fRestrictions.max_segment_size);

	fScratchVecs = (generic_io_vec*)malloc(
		sizeof(generic_io_vec) * fRestrictions.max_segment_count);
	if (fScratchVecs == NULL)
		return B_NO_MEMORY;

	for (size_t i = 0; i < fBufferCount; i++) {
		DMABuffer* buffer;
		status_t error = CreateBuffer(&buffer);
		if (error != B_OK)
			return error;

		fDMABuffers.Add(buffer);
	}

	// TODO: create bounce buffers in as few areas as feasible
	for (size_t i = 0; i < fBounceBufferCount; i++) {
		DMABounceBuffer* buffer;
		status_t error = CreateBounceBuffer(&buffer);
		if (error != B_OK)
			return error;

		fBounceBuffers.Add(buffer);
	}

	return B_OK;
}


/**
 * @brief Allocates a single DMABuffer sized for the maximum scatter-gather
 *        segment count configured for this resource.
 *
 * @param[out] _buffer Receives the pointer to the newly allocated DMABuffer.
 * @retval B_OK        Allocation succeeded.
 * @retval B_NO_MEMORY Heap allocation failed.
 */
status_t
DMAResource::CreateBuffer(DMABuffer** _buffer)
{
	DMABuffer* buffer = DMABuffer::Create(fRestrictions.max_segment_count);
	if (buffer == NULL)
		return B_NO_MEMORY;

	*_buffer = buffer;
	return B_OK;
}


/**
 * @brief Allocates a physically contiguous bounce buffer area that satisfies
 *        the DMA address and alignment restrictions of this resource.
 *
 * @param[out] _buffer Receives the pointer to the newly created DMABounceBuffer.
 * @retval B_OK        The bounce buffer area was created successfully.
 * @retval B_NO_MEMORY Object allocation failed.
 * @retval other       @c create_area_etc() or @c get_memory_map() error code.
 * @note The area is created in the B_SYSTEM_TEAM address space with contiguous
 *       physical backing and is mapped with kernel read/write permissions.
 */
status_t
DMAResource::CreateBounceBuffer(DMABounceBuffer** _buffer)
{
	void* bounceBuffer = NULL;
	phys_addr_t physicalBase = 0;
	area_id area = -1;
	phys_size_t size = ROUNDUP(fBounceBufferSize, B_PAGE_SIZE);

	virtual_address_restrictions virtualRestrictions = {};
	virtualRestrictions.address_specification = B_ANY_KERNEL_ADDRESS;
	physical_address_restrictions physicalRestrictions = {};
	physicalRestrictions.low_address = fRestrictions.low_address;
	physicalRestrictions.high_address = fRestrictions.high_address;
	physicalRestrictions.alignment = fRestrictions.alignment;
	physicalRestrictions.boundary = fRestrictions.boundary;
	area = create_area_etc(B_SYSTEM_TEAM, "dma buffer", size, B_CONTIGUOUS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, 0, 0, &virtualRestrictions,
		&physicalRestrictions, &bounceBuffer);
	if (area < B_OK)
		return area;

	physical_entry entry;
	if (get_memory_map(bounceBuffer, size, &entry, 1) != B_OK) {
		panic("get_memory_map() failed.");
		delete_area(area);
		return B_ERROR;
	}

	physicalBase = entry.address;

	ASSERT(fRestrictions.high_address >= physicalBase + size);

	DMABounceBuffer* buffer = new(std::nothrow) DMABounceBuffer;
	if (buffer == NULL) {
		delete_area(area);
		return B_NO_MEMORY;
	}

	buffer->address = bounceBuffer;
	buffer->physical_address = physicalBase;
	buffer->size = size;

	*_buffer = buffer;
	return B_OK;
}


/**
 * @brief Clamps @a length so that the resulting physical range does not cross
 *        a DMA boundary and does not exceed the maximum segment size.
 *
 * @param base   Physical start address of the segment being evaluated.
 * @param length In/out: desired segment length; reduced in place if necessary.
 * @note This is an inline hot-path helper called once per scatter-gather
 *       vector during TranslateNext().
 */
inline void
DMAResource::_RestrictBoundaryAndSegmentSize(generic_addr_t base,
	generic_addr_t& length)
{
	if (length > fRestrictions.max_segment_size)
		length = fRestrictions.max_segment_size;
	if (fRestrictions.boundary > 0) {
		generic_addr_t baseBoundary = base / fRestrictions.boundary;
		if (baseBoundary
				!= (base + (length - 1)) / fRestrictions.boundary) {
			length = (baseBoundary + 1) * fRestrictions.boundary - base;
		}
	}
}


/**
 * @brief Removes @a toCut bytes from the tail of @a buffer, returning any
 *        reclaimed bounce-buffer space to the available pool.
 *
 * @param buffer                The DMABuffer whose trailing vectors are trimmed.
 * @param physicalBounceBuffer  In/out: next free physical address in the bounce
 *                              buffer; decremented by the reclaimed amount.
 * @param bounceLeft            In/out: remaining free bytes in the bounce buffer;
 *                              incremented by the reclaimed amount.
 * @param toCut                 Number of bytes to remove from the tail.
 * @note Vectors are removed or shortened from last to first until the full
 *       @a toCut quota has been satisfied.
 */
void
DMAResource::_CutBuffer(DMABuffer& buffer, phys_addr_t& physicalBounceBuffer,
	phys_size_t& bounceLeft, generic_size_t toCut)
{
	int32 vecCount = buffer.VecCount();
	for (int32 i = vecCount - 1; toCut > 0 && i >= 0; i--) {
		generic_io_vec& vec = buffer.VecAt(i);
		generic_size_t length = vec.length;
		bool inBounceBuffer = buffer.UsesBounceBufferAt(i);

		if (length <= toCut) {
			vecCount--;
			toCut -= length;

			if (inBounceBuffer) {
				bounceLeft += length;
				physicalBounceBuffer -= length;
			}
		} else {
			vec.length -= toCut;

			if (inBounceBuffer) {
				bounceLeft += toCut;
				physicalBounceBuffer -= toCut;
			}
			break;
		}
	}

	buffer.SetVecCount(vecCount);
}


/*!	Adds \a length bytes from the bounce buffer to the DMABuffer \a buffer.
	Takes care of boundary, and segment restrictions. \a length must be aligned.
	If \a fixedLength is requested, this function will fail if it cannot
	satisfy the request.

	\return 0 if the request cannot be satisfied. There could have been some
		additions to the DMA buffer, and you will need to cut them back.
	TODO: is that what we want here?
	\return >0 the number of bytes added to the buffer.
*/
/**
 * @brief Appends up to @a length bytes of bounce-buffer space as one or more
 *        scatter-gather vectors in @a buffer.
 *
 * @param buffer                DMABuffer that receives the new bounce-buffer vec(s).
 * @param physicalBounceBuffer  In/out: next free physical address in the bounce
 *                              buffer; advanced by the number of bytes consumed.
 * @param bounceLeft            In/out: remaining free bytes in the bounce buffer;
 *                              decremented by the number of bytes consumed.
 * @param length                Desired number of bytes to add from the bounce buffer.
 * @param fixedLength           If @c true, the function returns 0 (failure) rather
 *                              than a partial result when the full @a length cannot
 *                              be satisfied.
 * @return Number of bytes actually appended to @a buffer (>0 on success, 0 on
 *         failure). Partial additions may have occurred even on a 0 return; use
 *         _CutBuffer() to roll them back.
 * @note Boundary and segment-size restrictions are enforced on each new vec.
 *       The caller must hold @c fLock when this function is called as part of
 *       TranslateNext().
 */
phys_size_t
DMAResource::_AddBounceBuffer(DMABuffer& buffer,
	phys_addr_t& physicalBounceBuffer, phys_size_t& bounceLeft,
	generic_size_t length, bool fixedLength)
{
	if (bounceLeft < length) {
		if (fixedLength)
			return 0;

		length = bounceLeft;
	}

	phys_size_t bounceUsed = 0;

	uint32 vecCount = buffer.VecCount();
	if (vecCount > 0) {
		// see if we can join the bounce buffer with the previously last vec
		generic_io_vec& vec = buffer.VecAt(vecCount - 1);
		generic_addr_t vecBase = vec.base;
		generic_size_t vecLength = vec.length;

		if (vecBase + vecLength == physicalBounceBuffer) {
			vecLength += length;
			_RestrictBoundaryAndSegmentSize(vecBase, vecLength);

			generic_size_t lengthDiff = vecLength - vec.length;
			length -= lengthDiff;

			physicalBounceBuffer += lengthDiff;
			bounceLeft -= lengthDiff;
			bounceUsed += lengthDiff;

			vec.length = vecLength;
		}
	}

	while (length > 0) {
		// We need to add another bounce vec

		if (vecCount == fRestrictions.max_segment_count)
			return fixedLength ? 0 : bounceUsed;

		generic_addr_t vecLength = length;
		_RestrictBoundaryAndSegmentSize(physicalBounceBuffer, vecLength);

		buffer.AddVec(physicalBounceBuffer, vecLength);
		vecCount++;

		physicalBounceBuffer += vecLength;
		bounceLeft -= vecLength;
		bounceUsed += vecLength;
		length -= vecLength;
	}

	return bounceUsed;
}


/**
 * @brief Translates the next portion of @a request into a DMA-ready IOOperation,
 *        respecting all hardware DMA constraints and inserting bounce-buffer
 *        segments as needed.
 *
 * @param request              The in-flight IORequest to translate; its internal
 *                             vec/offset state is advanced on success.
 * @param operation            Output IOOperation populated with the DMABuffer and
 *                             range information for the translated segment.
 * @param maxOperationLength   Maximum number of bytes this operation may cover;
 *                             pass 0 for no additional limit beyond the hardware
 *                             restrictions.
 * @retval B_OK                Translation succeeded; @a operation is ready to submit.
 * @retval B_BUSY              No DMABuffer or bounce buffer currently available in
 *                             the pool; the caller should retry later.
 * @retval B_BAD_VALUE         The request geometry cannot be satisfied (e.g. a
 *                             partial-begin that exhausts the bounce buffer).
 * @note Acquires @c fLock for the duration of the translation. Virtual-address
 *       buffers are walked through get_memory_map_etc() to build a physical
 *       scatter-gather list before DMA checks are applied.
 */
status_t
DMAResource::TranslateNext(IORequest* request, IOOperation* operation,
	generic_size_t maxOperationLength)
{
	IOBuffer* buffer = request->Buffer();
	off_t originalOffset = request->Offset() + request->Length()
		- request->RemainingBytes();
	off_t offset = originalOffset;
	generic_size_t partialBegin = offset & (fBlockSize - 1);

	// current iteration state
	uint32 vecIndex = request->VecIndex();
	uint32 vecOffset = request->VecOffset();
	generic_size_t totalLength = min_c(request->RemainingBytes(),
		fRestrictions.max_transfer_size);

	if (maxOperationLength > 0
		&& maxOperationLength < totalLength + partialBegin) {
		totalLength = maxOperationLength - partialBegin;
	}

	MutexLocker locker(fLock);

	DMABuffer* dmaBuffer = fDMABuffers.RemoveHead();
	if (dmaBuffer == NULL)
		return B_BUSY;

	dmaBuffer->SetVecCount(0);

	generic_io_vec* vecs = NULL;
	uint32 segmentCount = 0;

	TRACE("  offset %" B_PRIdOFF ", remaining size: %lu, block size %lu -> partial: %lu\n",
		offset, request->RemainingBytes(), fBlockSize, partialBegin);

	if (buffer->IsVirtual()) {
		// Unless we need the bounce buffer anyway, we have to translate the
		// virtual addresses to physical addresses, so we can check the DMA
		// restrictions.
		TRACE("  buffer is virtual %s\n", buffer->IsUser() ? "user" : "kernel");
		// TODO: !partialOperation || totalLength >= fBlockSize
		// TODO: Maybe enforce fBounceBufferSize >= 2 * fBlockSize.
		if (true) {
			generic_size_t transferLeft = totalLength;
			vecs = fScratchVecs;

			TRACE("  create physical map (for %ld vecs)\n", buffer->VecCount());
			for (uint32 i = vecIndex; i < buffer->VecCount(); i++) {
				generic_io_vec& vec = buffer->VecAt(i);
				generic_addr_t base = vec.base + vecOffset;
				generic_size_t size = vec.length - vecOffset;
				vecOffset = 0;
				if (size > transferLeft)
					size = transferLeft;

				while (size > 0 && segmentCount
						< fRestrictions.max_segment_count) {
					physical_entry entry;
					uint32 count = 1;
					get_memory_map_etc(request->TeamID(), (void*)base, size,
						&entry, &count);

					vecs[segmentCount].base = entry.address;
					vecs[segmentCount].length = entry.size;

					transferLeft -= entry.size;
					base += entry.size;
					size -= entry.size;
					segmentCount++;
				}

				if (transferLeft == 0)
					break;
			}

			totalLength -= transferLeft;
		}

		vecIndex = 0;
		vecOffset = 0;
	} else {
		// We do already have physical addresses.
		vecs = buffer->Vecs();
		segmentCount = min_c(buffer->VecCount() - vecIndex,
			fRestrictions.max_segment_count);
	}

#ifdef TRACE_DMA_RESOURCE
	TRACE("  physical count %" B_PRIu32 "\n", segmentCount);
	for (uint32 i = 0; i < segmentCount; i++) {
		TRACE("    [%" B_PRIu32 "] %#" B_PRIxGENADDR ", %" B_PRIxGENADDR "\n",
			i, vecs[vecIndex + i].base, vecs[vecIndex + i].length);
	}
#endif

	// check alignment, boundaries, etc. and set vecs in DMA buffer

	// Fetch a bounce buffer we can use for the DMABuffer.
	// TODO: We should do that lazily when needed!
	DMABounceBuffer* bounceBuffer = NULL;
	if (_NeedsBoundsBuffers()) {
		bounceBuffer = fBounceBuffers.Head();
		if (bounceBuffer == NULL)
			return B_BUSY;
	}
	dmaBuffer->SetBounceBuffer(bounceBuffer);

	generic_size_t dmaLength = 0;
	phys_addr_t physicalBounceBuffer = dmaBuffer->PhysicalBounceBufferAddress();
	phys_size_t bounceLeft = fBounceBufferSize;
	generic_size_t transferLeft = totalLength;

	// If the offset isn't block-aligned, use the bounce buffer to bridge the
	// gap to the start of the vec.
	if (partialBegin > 0) {
		generic_size_t length;
		if (request->IsWrite()) {
			// we always need to read in a whole block for the partial write
			length = fBlockSize;
		} else {
			length = (partialBegin + fRestrictions.alignment - 1)
				& ~(fRestrictions.alignment - 1);
		}

		if (_AddBounceBuffer(*dmaBuffer, physicalBounceBuffer, bounceLeft,
				length, true) == 0) {
			TRACE("  adding partial begin failed, length %lu!\n", length);
			return B_BAD_VALUE;
		}

		dmaLength += length;

		generic_size_t transferred = length - partialBegin;
		vecOffset += transferred;
		offset -= partialBegin;

		if (transferLeft > transferred)
			transferLeft -= transferred;
		else
			transferLeft = 0;

		TRACE("  partial begin, using bounce buffer: offset: %" B_PRIdOFF ", length: "
			"%lu\n", offset, length);
	}

	for (uint32 i = vecIndex;
			i < vecIndex + segmentCount && transferLeft > 0;) {
		if (dmaBuffer->VecCount() >= fRestrictions.max_segment_count)
			break;

		const generic_io_vec& vec = vecs[i];
		if (vec.length <= vecOffset) {
			vecOffset -= vec.length;
			i++;
			continue;
		}

		generic_addr_t base = vec.base + vecOffset;
		generic_size_t maxLength = vec.length - vecOffset;
		if (maxLength > transferLeft)
			maxLength = transferLeft;
		generic_size_t length = maxLength;

		// Cut the vec according to transfer size, segment size, and boundary.

		if (dmaLength + length > fRestrictions.max_transfer_size) {
			length = fRestrictions.max_transfer_size - dmaLength;
			TRACE("  vec %" B_PRIu32 ": restricting length to %lu due to transfer size "
				"limit\n", i, length);
		}
		_RestrictBoundaryAndSegmentSize(base, length);

		phys_size_t useBounceBufferSize = 0;

		// Check low address: use bounce buffer for range to low address.
		// Check alignment: if not aligned, use bounce buffer for complete vec.
		if (base < fRestrictions.low_address) {
			useBounceBufferSize = fRestrictions.low_address - base;
			TRACE("  vec %" B_PRIu32 ": below low address, using bounce buffer: %lu\n", i,
				useBounceBufferSize);
		} else if (base & (fRestrictions.alignment - 1)) {
			useBounceBufferSize = length;
			TRACE("  vec %" B_PRIu32 ": misalignment, using bounce buffer: %lu\n", i,
				useBounceBufferSize);
		}

		// Enforce high address restriction
		if (base > fRestrictions.high_address)
			useBounceBufferSize = length;
		else if (base + length > fRestrictions.high_address)
			length = fRestrictions.high_address - base;

		// Align length as well
		if (useBounceBufferSize == 0)
			length &= ~(fRestrictions.alignment - 1);

		// If length is 0, use bounce buffer for complete vec.
		if (length == 0) {
			length = maxLength;
			useBounceBufferSize = length;
			TRACE("  vec %" B_PRIu32 ": 0 length, using bounce buffer: %lu\n", i,
				useBounceBufferSize);
		}

		if (useBounceBufferSize > 0) {
			// alignment could still be wrong (we round up here)
			useBounceBufferSize = (useBounceBufferSize
				+ fRestrictions.alignment - 1) & ~(fRestrictions.alignment - 1);

			length = _AddBounceBuffer(*dmaBuffer, physicalBounceBuffer,
				bounceLeft, useBounceBufferSize, false);
			if (length == 0) {
				TRACE("  vec %" B_PRIu32 ": out of bounce buffer space\n", i);
				// We don't have any bounce buffer space left, we need to move
				// this request to the next I/O operation.
				break;
			}
			TRACE("  vec %" B_PRIu32 ": final bounce length: %lu\n", i, length);
		} else {
			TRACE("  vec %" B_PRIu32 ": final length restriction: %lu\n", i, length);
			dmaBuffer->AddVec(base, length);
		}

		dmaLength += length;
		vecOffset += length;
		transferLeft -= min_c(length, transferLeft);
	}

	// If we're writing partially, we always need to have a block sized bounce
	// buffer (or else we would overwrite memory to be written on the read in
	// the first phase).
	off_t requestEnd = request->Offset() + request->Length();
	if (request->IsWrite()) {
		generic_size_t diff = dmaLength & (fBlockSize - 1);

		// If the transfer length is block aligned and we're writing past the
		// end of the given data, we still have to check the whether the last
		// vec is a bounce buffer segment shorter than the block size. If so, we
		// have to cut back the complete block and use a bounce buffer for it
		// entirely.
		if (diff == 0 && offset + (off_t)dmaLength > requestEnd) {
			const generic_io_vec& dmaVec
				= dmaBuffer->VecAt(dmaBuffer->VecCount() - 1);
			ASSERT(dmaVec.base >= dmaBuffer->PhysicalBounceBufferAddress()
				&& dmaVec.base
					< dmaBuffer->PhysicalBounceBufferAddress()
						+ fBounceBufferSize);
				// We can be certain that the last vec is a bounce buffer vec,
				// since otherwise the DMA buffer couldn't exceed the end of the
				// request data.
			if (dmaVec.length < fBlockSize)
				diff = fBlockSize;
		}

		if (diff != 0) {
			// Not yet block aligned -- cut back to the previous block and add
			// a block-sized bounce buffer segment.
			TRACE("  partial end write: %lu, diff %lu\n", dmaLength, diff);

			_CutBuffer(*dmaBuffer, physicalBounceBuffer, bounceLeft, diff);
			dmaLength -= diff;

			if (_AddBounceBuffer(*dmaBuffer, physicalBounceBuffer,
					bounceLeft, fBlockSize, true) == 0) {
				// If we cannot write anything, we can't process the request at
				// all.
				TRACE("  adding bounce buffer failed!!!\n");
				if (dmaLength == 0)
					return B_BAD_VALUE;
			} else
				dmaLength += fBlockSize;
		}
	}

	// If total length not block aligned, use bounce buffer for padding (read
	// case only).
	while ((dmaLength & (fBlockSize - 1)) != 0) {
		TRACE("  dmaLength not block aligned: %lu\n", dmaLength);
			generic_size_t length
				= (dmaLength + fBlockSize - 1) & ~(fBlockSize - 1);

		// If total length > max transfer size, segment count > max segment
		// count, truncate.
		// TODO: sometimes we can replace the last vec with the bounce buffer
		// to let it match the restrictions.
		if (length > fRestrictions.max_transfer_size
			|| dmaBuffer->VecCount() == fRestrictions.max_segment_count
			|| bounceLeft < length - dmaLength) {
			// cut the part of dma length
			TRACE("  can't align length due to max transfer size, segment "
				"count restrictions, or lacking bounce buffer space\n");
			generic_size_t toCut = dmaLength
				& (max_c(fBlockSize, fRestrictions.alignment) - 1);
			dmaLength -= toCut;
			if (dmaLength == 0) {
				// This can only happen, when we have too many small segments
				// and hit the max segment count. In this case we just use the
				// bounce buffer for as much as possible of the total length.
				dmaBuffer->SetVecCount(0);
				generic_addr_t base = dmaBuffer->PhysicalBounceBufferAddress();
				dmaLength = min_c(totalLength, fBounceBufferSize)
					& ~(max_c(fBlockSize, fRestrictions.alignment) - 1);
				_RestrictBoundaryAndSegmentSize(base, dmaLength);
				dmaBuffer->AddVec(base, dmaLength);

				physicalBounceBuffer = base + dmaLength;
				bounceLeft = fBounceBufferSize - dmaLength;
			} else {
				_CutBuffer(*dmaBuffer, physicalBounceBuffer, bounceLeft, toCut);
			}
		} else {
			TRACE("  adding %lu bytes final bounce buffer\n",
				length - dmaLength);
			length -= dmaLength;
			length = _AddBounceBuffer(*dmaBuffer, physicalBounceBuffer,
				bounceLeft, length, true);
			if (length == 0)
				panic("don't do this to me!");
			dmaLength += length;
		}
	}

	operation->SetBuffer(dmaBuffer);
	operation->SetBlockSize(fBlockSize);
	operation->SetOriginalRange(originalOffset,
		min_c(offset + (off_t)dmaLength, requestEnd) - originalOffset);
	operation->SetRange(offset, dmaLength);
	operation->SetPartial(partialBegin != 0,
		offset + (off_t)dmaLength > requestEnd);

	// If we don't need the bounce buffer, we put it back, otherwise
	operation->SetUsesBounceBuffer(bounceLeft < fBounceBufferSize);
	if (operation->UsesBounceBuffer())
		fBounceBuffers.RemoveHead();
	else
		dmaBuffer->SetBounceBuffer(NULL);


	status_t error = operation->Prepare(request);
	if (error != B_OK)
		return error;

	request->Advance(operation->OriginalLength());

	return B_OK;
}


/**
 * @brief Returns a DMABuffer (and its optional bounce buffer) to the resource's
 *        free pool so that it can be reused by a subsequent TranslateNext() call.
 *
 * @param buffer The DMABuffer to recycle; a @c NULL argument is silently ignored.
 * @note Acquires @c fLock internally. The caller must not hold @c fLock when
 *       calling this function. After this call @a buffer must not be accessed
 *       again.
 */
void
DMAResource::RecycleBuffer(DMABuffer* buffer)
{
	if (buffer == NULL)
		return;

	MutexLocker _(fLock);
	fDMABuffers.Add(buffer);
	if (buffer->BounceBuffer() != NULL) {
		fBounceBuffers.Add(buffer->BounceBuffer());
		buffer->SetBounceBuffer(NULL);
	}
}


/**
 * @brief Returns @c true when the hardware DMA constraints require bounce
 *        buffers (i.e. not all of physical RAM satisfies alignment, address
 *        range, or block-size requirements).
 *
 * @return @c true if bounce buffers must be allocated; @c false otherwise.
 */
bool
DMAResource::_NeedsBoundsBuffers() const
{
	return fRestrictions.alignment > 1
		|| fRestrictions.low_address != 0
		|| fRestrictions.high_address != ~(generic_addr_t)0
		|| fBlockSize > 1;
}




#if 0


status_t
create_dma_resource(restrictions)
{
	// Restrictions are: transfer size, address space, alignment
	// segment min/max size, num segments
}


void
delete_dma_resource(resource)
{
}


dma_buffer_alloc(resource, size)
{
}


dma_buffer_free(buffer)
{
//	Allocates or frees memory in that DMA buffer.
}

#endif	// 0
