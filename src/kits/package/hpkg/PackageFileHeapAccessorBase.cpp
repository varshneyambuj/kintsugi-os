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
 *   Copyright 2013-2014, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file PackageFileHeapAccessorBase.cpp
 * @brief Shared base class for reading and writing the HPKG compressed heap.
 *
 * PackageFileHeapAccessorBase encapsulates the chunk-offset index (OffsetArray),
 * the decompression algorithm, and the core I/O primitives used by both the
 * reader and writer.  The heap is split into fixed-size chunks; each chunk
 * may be independently compressed.  The OffsetArray maps chunk indices to
 * their byte offsets within the compressed heap, using a compact mixed
 * 32/64-bit encoding for large heaps.
 *
 * @see PackageFileHeapReader, PackageFileHeapWriter
 */


#include <package/hpkg/PackageFileHeapAccessorBase.h>

#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <new>
#if defined(_KERNEL_MODE) && !defined(_BOOT_MODE)
#include <util/AutoLock.h>
#include <slab/Slab.h>
#endif

#include <ByteOrder.h>
#include <DataIO.h>
#include <package/hpkg/ErrorOutput.h>

#include <AutoDeleter.h>
#include <CompressionAlgorithm.h>


namespace BPackageKit {

namespace BHPKG {

namespace BPrivate {


#if defined(_KERNEL_MODE) && !defined(_BOOT_MODE)
void* PackageFileHeapAccessorBase::sQuadChunkCache = NULL;
void* PackageFileHeapAccessorBase::sQuadChunkFallbackBuffer = NULL;
static mutex sFallbackBufferLock = MUTEX_INITIALIZER("PackageFileHeapAccessorBase fallback buffer");
#endif


// #pragma mark - OffsetArray


/**
 * @brief Construct an empty OffsetArray with no allocated storage.
 */
PackageFileHeapAccessorBase::OffsetArray::OffsetArray()
	:
	fOffsets(NULL)
{
}


/**
 * @brief Destroy the OffsetArray and release the underlying offset storage.
 */
PackageFileHeapAccessorBase::OffsetArray::~OffsetArray()
{
	delete[] fOffsets;
}


/**
 * @brief Initialise offsets for a heap where every chunk has the nominal size.
 *
 * When no compression is used all chunk boundaries are at multiples of
 * kChunkSize, so offsets can be computed arithmetically without storing each
 * one individually.  For single-chunk heaps no storage is allocated at all.
 *
 * @param totalChunkCount Total number of chunks in the uncompressed heap.
 * @return true on success, false if memory allocation fails.
 */
bool
PackageFileHeapAccessorBase::OffsetArray::InitUncompressedChunksOffsets(
	size_t totalChunkCount)
{
	if (totalChunkCount <= 1)
		return true;

	const size_t max32BitChunks = (uint64(1) << 32) / kChunkSize;
	size_t actual32BitChunks = totalChunkCount;
	if (totalChunkCount - 1 > max32BitChunks) {
		actual32BitChunks = max32BitChunks;
		fOffsets = _AllocateOffsetArray(totalChunkCount, max32BitChunks);
	} else
		fOffsets = _AllocateOffsetArray(totalChunkCount, 0);

	if (fOffsets == NULL)
		return false;

	{
		uint32 offset = kChunkSize;
		for (size_t i = 1; i < actual32BitChunks; i++, offset += kChunkSize)
			fOffsets[i] = offset;

	}

	if (actual32BitChunks < totalChunkCount) {
		uint64 offset = actual32BitChunks * kChunkSize;
		uint32* offsets = fOffsets + actual32BitChunks;
		for (size_t i = actual32BitChunks; i < totalChunkCount;
				i++, offset += kChunkSize) {
			*offsets++ = (uint32)offset;
			*offsets++ = uint32(offset >> 32);
		}
	}

	return true;
}


/**
 * @brief Accumulate compressed chunk sizes to populate the offset table.
 *
 * Reads \a chunkCount entries from \a chunkSizes (each stored as a big-endian
 * uint16 representing chunkSize - 1) starting at \a baseIndex, computes
 * the resulting byte offsets, and stores them in the array.  Automatically
 * upgrades the internal layout from 32-bit to mixed 64-bit when an offset
 * exceeds 4 GiB.
 *
 * @param totalChunkCount Total number of chunks in the heap (used to size
 *                        the initial allocation if not already done).
 * @param baseIndex       Index of the first chunk whose offset is already
 *                        known; sizes begin at the following chunk.
 * @param chunkSizes      Array of big-endian uint16 values, each encoding
 *                        (compressedChunkSize - 1).
 * @param chunkCount      Number of entries to read from \a chunkSizes.
 * @return true on success, false if memory allocation fails.
 */
bool
PackageFileHeapAccessorBase::OffsetArray::InitChunksOffsets(
	size_t totalChunkCount, size_t baseIndex, const uint16* chunkSizes,
	size_t chunkCount)
{
	if (totalChunkCount <= 1)
		return true;

	if (fOffsets == NULL) {
		fOffsets = _AllocateOffsetArray(totalChunkCount, totalChunkCount);
		if (fOffsets == NULL)
			return false;
	}

	uint64 offset = (*this)[baseIndex];
	for (size_t i = 0; i < chunkCount; i++) {
		offset += (uint64)B_BENDIAN_TO_HOST_INT16(chunkSizes[i]) + 1;
			// the stored value is chunkSize - 1
		size_t index = baseIndex + i + 1;
			// (baseIndex + i) is the index of the chunk whose size is stored in
			// chunkSizes[i]. We compute the offset of the following element
			// which we store at index (baseIndex + i + 1).

		if (offset <= ~(uint32)0) {
			fOffsets[index] = (uint32)offset;
		} else {
			if (fOffsets[0] == 0) {
				// Not scaled to allow for 64 bit offsets yet. Do that.
				uint32* newOffsets = _AllocateOffsetArray(totalChunkCount,
					index);
				if (newOffsets == NULL)
					return false;

				fOffsets[0] = index;
				memcpy(newOffsets, fOffsets, sizeof(newOffsets[0]) * index);

				delete[] fOffsets;
				fOffsets = newOffsets;
			}

			index += index - fOffsets[0];
			fOffsets[index] = (uint32)offset;
			fOffsets[index + 1] = uint32(offset >> 32);
		}
	}

	return true;
}


/**
 * @brief Copy the offset table from another OffsetArray.
 *
 * Allocates a new buffer and copies all entries from \a other.  If \a other
 * has no allocated storage (single-chunk or empty heap) this is a no-op.
 *
 * @param totalChunkCount Total number of chunks described by \a other.
 * @param other           Source OffsetArray to copy from.
 * @return true on success, false if memory allocation fails.
 */
bool
PackageFileHeapAccessorBase::OffsetArray::Init(size_t totalChunkCount,
	const OffsetArray& other)
{
	if (other.fOffsets == NULL)
		return true;

	size_t elementCount = other.fOffsets[0] == 0
		? totalChunkCount
		: 2 * totalChunkCount - other.fOffsets[0];

	fOffsets = new(std::nothrow) uint32[elementCount];
	if (fOffsets == NULL)
		return false;

	memcpy(fOffsets, other.fOffsets, elementCount * sizeof(fOffsets[0]));
	return true;
}


/**
 * @brief Allocate the raw uint32 array for the offset table.
 *
 * The first element encodes the layout: 0 means all offsets fit in 32 bits;
 * a non-zero value is the index of the first chunk that needs a 64-bit offset,
 * at which point each such entry occupies two consecutive uint32 slots.
 *
 * @param totalChunkCount       Total number of chunks in the heap.
 * @param offset32BitChunkCount Number of chunks whose offsets fit in 32 bits.
 * @return Pointer to the allocated array, or NULL on failure.
 */
/*static*/ uint32*
PackageFileHeapAccessorBase::OffsetArray::_AllocateOffsetArray(
	size_t totalChunkCount, size_t offset32BitChunkCount)
{
	uint32* offsets = new(std::nothrow) uint32[
		2 * totalChunkCount - offset32BitChunkCount];

	if (offsets != NULL) {
		offsets[0] = offset32BitChunkCount == totalChunkCount
			? 0 : offset32BitChunkCount;
			// 0 means that all offsets are 32 bit. Otherwise it's the index of
			// the first 64 bit offset.
	}

	return offsets;
}


// #pragma mark - PackageFileHeapAccessorBase


/**
 * @brief Construct the heap accessor base.
 *
 * Acquires a reference to \a decompressionAlgorithm if it is non-NULL so the
 * algorithm object outlives this accessor.
 *
 * @param errorOutput            Channel for diagnostic messages; must remain
 *                               valid for the lifetime of this object.
 * @param file                   Positioned I/O object representing the package
 *                               file; must remain valid for the lifetime of
 *                               this object.
 * @param heapOffset             Byte offset of the heap within \a file.
 * @param decompressionAlgorithm Decompression algorithm and parameters to use,
 *                               or NULL for an uncompressed heap.
 */
PackageFileHeapAccessorBase::PackageFileHeapAccessorBase(
	BErrorOutput* errorOutput, BPositionIO* file, off_t heapOffset,
	DecompressionAlgorithmOwner* decompressionAlgorithm)
	:
	fErrorOutput(errorOutput),
	fFile(file),
	fHeapOffset(heapOffset),
	fCompressedHeapSize(0),
	fUncompressedHeapSize(0),
	fDecompressionAlgorithm(decompressionAlgorithm)
{
	if (fDecompressionAlgorithm != NULL)
		fDecompressionAlgorithm->AcquireReference();
}


/**
 * @brief Destroy the accessor and release the decompression algorithm reference.
 */
PackageFileHeapAccessorBase::~PackageFileHeapAccessorBase()
{
	if (fDecompressionAlgorithm != NULL)
		fDecompressionAlgorithm->ReleaseReference();
}


/**
 * @brief Read and decompress a contiguous range from the heap into a BDataIO.
 *
 * Iterates over the chunks that overlap the requested range, decompressing
 * each chunk in turn and writing the relevant portion to \a output.  Both
 * compressed and uncompressed chunk buffers are allocated on demand.
 *
 * @param offset Byte offset within the uncompressed heap to start reading.
 * @param size   Number of uncompressed bytes to read.
 * @param output Destination BDataIO that receives the decompressed data.
 * @return B_OK on success, B_BAD_VALUE for an out-of-range request,
 *         B_NO_MEMORY if buffer allocation fails, or any error returned by
 *         ReadAndDecompressChunk() or BDataIO::WriteExactly().
 */
status_t
PackageFileHeapAccessorBase::ReadDataToOutput(off_t offset, size_t size,
	BDataIO* output)
{
	if (size == 0)
		return B_OK;

	if (offset < 0 || (uint64)offset > fUncompressedHeapSize
		|| size > fUncompressedHeapSize - offset) {
		return B_BAD_VALUE;
	}

	// allocate buffers for compressed and uncompressed data
	uint16* compressedDataBuffer, *uncompressedDataBuffer;
	iovec* scratch = NULL;

#if defined(_KERNEL_MODE) && !defined(_BOOT_MODE)
	struct ObjectCacheDeleter {
		object_cache* cache;
		void* object;

		ObjectCacheDeleter(object_cache* c)
			: cache(c)
			, object(NULL)
		{
		}

		~ObjectCacheDeleter()
		{
			if (cache != NULL && object != NULL)
				object_cache_free(cache, object, 0);
		}
	};

	ObjectCacheDeleter chunkBufferDeleter((object_cache*)sQuadChunkCache);
	uint8* quadChunkBuffer = (uint8*)object_cache_alloc((object_cache*)sQuadChunkCache,
		CACHE_DONT_WAIT_FOR_MEMORY);
	chunkBufferDeleter.object = quadChunkBuffer;

	MutexLocker fallbackBufferLocker(sFallbackBufferLock, false, false);
	if (quadChunkBuffer == NULL) {
		fallbackBufferLocker.Lock();
		quadChunkBuffer = (uint8*)sQuadChunkFallbackBuffer;
	}

	// segment data buffer
	iovec localScratch;
	compressedDataBuffer = (uint16*)(quadChunkBuffer + 0);
	uncompressedDataBuffer = (uint16*)(quadChunkBuffer + kChunkSize);
	localScratch.iov_base = (quadChunkBuffer + (kChunkSize * 2));
	localScratch.iov_len = kChunkSize * 2;
	scratch = &localScratch;
#else
	MemoryDeleter compressedMemoryDeleter, uncompressedMemoryDeleter;
	compressedDataBuffer = (uint16*)malloc(kChunkSize);
	uncompressedDataBuffer = (uint16*)malloc(kChunkSize);
	compressedMemoryDeleter.SetTo(compressedDataBuffer);
	uncompressedMemoryDeleter.SetTo(uncompressedDataBuffer);
#endif

	if (compressedDataBuffer == NULL || uncompressedDataBuffer == NULL)
		return B_NO_MEMORY;

	// read the data
	size_t chunkIndex = size_t(offset / kChunkSize);
	size_t inChunkOffset = (uint64)offset - (uint64)chunkIndex * kChunkSize;
	size_t remainingBytes = size;

	while (remainingBytes > 0) {
		status_t error = ReadAndDecompressChunk(chunkIndex,
			compressedDataBuffer, uncompressedDataBuffer, scratch);
		if (error != B_OK)
			return error;

		size_t toWrite = std::min((size_t)kChunkSize - inChunkOffset,
			remainingBytes);
			// The last chunk may be shorter than kChunkSize, but since
			// size (and thus remainingSize) had been clamped, that doesn't
			// harm.
		error = output->WriteExactly(
			(char*)uncompressedDataBuffer + inChunkOffset, toWrite);
		if (error != B_OK)
			return error;

		remainingBytes -= toWrite;
		chunkIndex++;
		inChunkOffset = 0;
	}

	return B_OK;
}


/**
 * @brief Read and optionally decompress a single raw chunk from the file.
 *
 * When \a compressedSize equals \a uncompressedSize the data is read directly
 * into \a uncompressedDataBuffer.  Otherwise the compressed bytes are read
 * into \a compressedDataBuffer and then decompressed.
 *
 * @param offset               Byte offset within the compressed heap of the
 *                             chunk to read.
 * @param compressedSize       Number of compressed bytes to read.
 * @param uncompressedSize     Expected size of the decompressed data.
 * @param compressedDataBuffer Scratch buffer for the compressed bytes.
 * @param uncompressedDataBuffer Destination buffer for the decompressed data.
 * @param scratchBuffer        Optional algorithm-specific scratch space.
 * @return B_OK on success, or an error code on I/O or decompression failure.
 */
status_t
PackageFileHeapAccessorBase::ReadAndDecompressChunkData(uint64 offset,
	size_t compressedSize, size_t uncompressedSize,
	void* compressedDataBuffer, void* uncompressedDataBuffer,
	iovec* scratchBuffer)
{
	// if uncompressed, read directly into the uncompressed data buffer
	if (compressedSize == uncompressedSize)
		return ReadFileData(offset, uncompressedDataBuffer, compressedSize);

	// otherwise read into the other buffer and decompress
	status_t error = ReadFileData(offset, compressedDataBuffer, compressedSize);
	if (error != B_OK)
		return error;

	iovec compressed = { compressedDataBuffer, compressedSize },
		uncompressed = { uncompressedDataBuffer, uncompressedSize };
	return DecompressChunkData(compressed, uncompressed, scratchBuffer);
}


/**
 * @brief Decompress a chunk of data using the configured algorithm.
 *
 * Invokes the decompression algorithm and validates that the output size
 * matches the expected uncompressed size.  Emits an error message through
 * fErrorOutput on failure.
 *
 * @param compressed   iovec describing the compressed input buffer.
 * @param uncompressed iovec describing the output buffer; iov_len must be
 *                     set to the expected uncompressed size on entry.
 * @param scratchBuffer Optional algorithm-specific scratch space; may be NULL.
 * @return B_OK on success, B_ERROR if the decompressed size mismatches,
 *         or any error returned by the decompression algorithm.
 */
status_t
PackageFileHeapAccessorBase::DecompressChunkData(const iovec& compressed,
	iovec& uncompressed, iovec* scratchBuffer)
{
	const size_t uncompressedSize = uncompressed.iov_len;
	status_t error = fDecompressionAlgorithm->algorithm->DecompressBuffer(
		compressed, uncompressed, fDecompressionAlgorithm->parameters, scratchBuffer);
	if (error != B_OK) {
		fErrorOutput->PrintError("Failed to decompress chunk data: %s\n",
			strerror(error));
		return error;
	}

	if (uncompressed.iov_len != uncompressedSize) {
		fErrorOutput->PrintError("Failed to decompress chunk data: size "
			"mismatch\n");
		return B_ERROR;
	}

	return B_OK;
}


/**
 * @brief Read raw bytes from the package file at a heap-relative offset.
 *
 * Translates \a offset by adding fHeapOffset before reading via
 * BPositionIO::ReadAtExactly().  Emits a diagnostic message through
 * fErrorOutput on failure.
 *
 * @param offset Byte offset within the compressed heap to read from.
 * @param buffer Destination buffer for the read bytes.
 * @param size   Number of bytes to read.
 * @return B_OK on success, or any error returned by ReadAtExactly().
 */
status_t
PackageFileHeapAccessorBase::ReadFileData(uint64 offset, void* buffer,
	size_t size)
{
	status_t error = fFile->ReadAtExactly(fHeapOffset + (off_t)offset, buffer,
		size);
	if (error != B_OK) {
		fErrorOutput->PrintError("ReadFileData(%" B_PRIu64 ", %p, %zu) failed "
			"to read data: %s\n", offset, buffer, size, strerror(error));
		return error;
	}

	return B_OK;
}


}	// namespace BPrivate

}	// namespace BHPKG

}	// namespace BPackageKit
