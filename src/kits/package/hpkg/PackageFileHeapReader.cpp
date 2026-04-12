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
 * @file PackageFileHeapReader.cpp
 * @brief Read-only access to the compressed heap section of an HPKG file.
 *
 * PackageFileHeapReader extends PackageFileHeapAccessorBase with the ability
 * to parse the chunk-size table stored at the end of the compressed heap and
 * build the OffsetArray index used for random-access decompression.  It also
 * supports cloning itself so that multiple independent read cursors can share
 * the same heap without re-reading the chunk table.
 *
 * @see PackageFileHeapAccessorBase, PackageFileHeapWriter
 */


#include <package/hpkg/PackageFileHeapReader.h>

#include <algorithm>
#include <new>

#include <package/hpkg/ErrorOutput.h>
#include <package/hpkg/HPKGDefs.h>

#include <AutoDeleter.h>
#include <package/hpkg/PoolBuffer.h>


namespace BPackageKit {

namespace BHPKG {

namespace BPrivate {


/**
 * @brief Construct a heap reader for an already-located heap region.
 *
 * @param errorOutput            Channel for diagnostic messages.
 * @param file                   Positioned I/O object for the package file.
 * @param heapOffset             Byte offset of the heap within \a file.
 * @param compressedHeapSize     Size of the compressed heap data in bytes
 *                               (including the chunk-size table).
 * @param uncompressedHeapSize   Total uncompressed size of all heap data.
 * @param decompressionAlgorithm Decompression algorithm to use, or NULL for
 *                               an uncompressed heap.
 */
PackageFileHeapReader::PackageFileHeapReader(BErrorOutput* errorOutput,
	BPositionIO* file, off_t heapOffset, off_t compressedHeapSize,
	uint64 uncompressedHeapSize,
	DecompressionAlgorithmOwner* decompressionAlgorithm)
	:
	PackageFileHeapAccessorBase(errorOutput, file, heapOffset,
		decompressionAlgorithm),
	fOffsets()
{
	fCompressedHeapSize = compressedHeapSize;
	fUncompressedHeapSize = uncompressedHeapSize;
}


/**
 * @brief Destroy the heap reader.
 */
PackageFileHeapReader::~PackageFileHeapReader()
{
}


/**
 * @brief Parse the chunk-size table and build the offset index.
 *
 * Validates the compressed and uncompressed heap sizes, reads the per-chunk
 * size array from the tail of the compressed heap, and populates the
 * OffsetArray used for random-access chunk lookup.  For uncompressed heaps
 * the chunk-size table is absent and offsets are computed arithmetically.
 *
 * @return B_OK on success, B_BAD_DATA if the heap metadata is inconsistent,
 *         B_NO_MEMORY if buffer allocation fails, or any I/O error.
 */
status_t
PackageFileHeapReader::Init()
{
	if (fUncompressedHeapSize == 0) {
		if (fCompressedHeapSize != 0) {
			fErrorOutput->PrintError(
				"Invalid total compressed heap size (!= 0, empty heap)\n");
			return B_BAD_DATA;
		}
		return B_OK;
	}

	// Determine number of chunks and adjust the compressed heap size (subtract
	// the size of the chunk size array at the end). Note that the size of the
	// last chunk has not been saved, since its size is implied.
	ssize_t chunkCount = (fUncompressedHeapSize + kChunkSize - 1) / kChunkSize;
	if (chunkCount == 0)
		return B_OK;

	// If no compression is used at all, the chunk size table is omitted. Handle
	// this case.
	if (fDecompressionAlgorithm == NULL) {
		if (fUncompressedHeapSize != fCompressedHeapSize) {
			fErrorOutput->PrintError(
				"Compressed and uncompressed heap sizes (%" B_PRIu64 " vs. "
				"%" B_PRIu64 ") don't match for uncompressed heap.\n",
				fCompressedHeapSize, fUncompressedHeapSize);
			return B_BAD_DATA;
		}

		if (!fOffsets.InitUncompressedChunksOffsets(chunkCount))
			return B_NO_MEMORY;

		return B_OK;
	}

	size_t chunkSizeTableSize = (chunkCount - 1) * 2;
	if (fCompressedHeapSize <= chunkSizeTableSize) {
		fErrorOutput->PrintError(
			"Invalid total compressed heap size (%" B_PRIu64 ", "
			"uncompressed %" B_PRIu64 ")\n", fCompressedHeapSize,
			fUncompressedHeapSize);
		return B_BAD_DATA;
	}

	fCompressedHeapSize -= chunkSizeTableSize;

	// allocate a buffer
	uint16* buffer = (uint16*)malloc(kChunkSize);
	if (buffer == NULL)
		return B_NO_MEMORY;
	MemoryDeleter bufferDeleter(buffer);

	// read the chunk size array
	size_t remainingChunks = chunkCount - 1;
	size_t index = 0;
	uint64 offset = fCompressedHeapSize;
	while (remainingChunks > 0) {
		size_t toRead = std::min(remainingChunks, kChunkSize / 2);
		status_t error = ReadFileData(offset, buffer, toRead * 2);
		if (error != B_OK)
			return error;

		if (!fOffsets.InitChunksOffsets(chunkCount, index, buffer, toRead))
			return B_NO_MEMORY;

		remainingChunks -= toRead;
		index += toRead;
		offset += toRead * 2;
	}

	// Sanity check: The sum of the chunk sizes must match the compressed heap
	// size. The information aren't stored redundantly, so we check, if things
	// look at least plausible.
	uint64 lastChunkOffset = fOffsets[chunkCount - 1];
	if (lastChunkOffset >= fCompressedHeapSize
			|| fCompressedHeapSize - lastChunkOffset > kChunkSize
			|| fCompressedHeapSize - lastChunkOffset
				> fUncompressedHeapSize - (chunkCount - 1) * kChunkSize) {
		fErrorOutput->PrintError(
			"Invalid total compressed heap size (%" B_PRIu64 ", uncompressed: "
			"%" B_PRIu64 ", last chunk offset: %" B_PRIu64 ")\n",
			fCompressedHeapSize, fUncompressedHeapSize, lastChunkOffset);
		return B_BAD_DATA;
	}

	return B_OK;
}


/**
 * @brief Create an independent copy of this heap reader sharing the same file.
 *
 * Clones the OffsetArray so that the copy can perform independent reads
 * against the same underlying package file without re-reading the chunk table.
 *
 * @return A pointer to the cloned PackageFileHeapReader on success, or NULL
 *         if memory allocation fails.
 */
PackageFileHeapReader*
PackageFileHeapReader::Clone() const
{
	PackageFileHeapReader* clone = new(std::nothrow) PackageFileHeapReader(
		fErrorOutput, fFile, fHeapOffset, fCompressedHeapSize,
		fUncompressedHeapSize, fDecompressionAlgorithm);
	if (clone == NULL)
		return NULL;

	ssize_t chunkCount = (fUncompressedHeapSize + kChunkSize - 1) / kChunkSize;
	if (!clone->fOffsets.Init(chunkCount, fOffsets)) {
		delete clone;
		return NULL;
	}

	return clone;
}


/**
 * @brief Decompress a single heap chunk into the caller-supplied buffers.
 *
 * Looks up the compressed offset and sizes for \a chunkIndex using the
 * OffsetArray and delegates to ReadAndDecompressChunkData().
 *
 * @param chunkIndex            Zero-based index of the chunk to read.
 * @param compressedDataBuffer  Scratch buffer for the compressed bytes;
 *                              must be at least kChunkSize bytes.
 * @param uncompressedDataBuffer Destination buffer for the decompressed data;
 *                              must be at least kChunkSize bytes.
 * @param scratchBuffer         Optional algorithm-specific scratch buffer.
 * @return B_OK on success, or any error from ReadAndDecompressChunkData().
 */
status_t
PackageFileHeapReader::ReadAndDecompressChunk(size_t chunkIndex,
	void* compressedDataBuffer, void* uncompressedDataBuffer,
	iovec* scratchBuffer)
{
	uint64 offset = fOffsets[chunkIndex];
	bool isLastChunk
		= ((uint64)chunkIndex + 1) * kChunkSize >= fUncompressedHeapSize;
	size_t compressedSize = isLastChunk
		? fCompressedHeapSize - offset
		: fOffsets[chunkIndex + 1] - offset;
	size_t uncompressedSize = isLastChunk
		? fUncompressedHeapSize - (uint64)chunkIndex * kChunkSize
		: kChunkSize;

	return ReadAndDecompressChunkData(offset, compressedSize, uncompressedSize,
		compressedDataBuffer, uncompressedDataBuffer, scratchBuffer);
}


}	// namespace BPrivate

}	// namespace BHPKG

}	// namespace BPackageKit
