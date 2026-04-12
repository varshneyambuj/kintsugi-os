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
 * @file PackageFileHeapWriter.cpp
 * @brief Write and rewrite the compressed heap section of an HPKG package file.
 *
 * PackageFileHeapWriter extends PackageFileHeapAccessorBase with buffered
 * sequential writes and an in-place range-removal algorithm.  Incoming data is
 * accumulated in a pending-data buffer; when a full kChunkSize chunk is ready
 * it is optionally compressed and flushed to disk.  RemoveDataRanges() can
 * surgically excise arbitrary byte ranges from the heap while preserving and
 * recompressing the surrounding data.  Finish() writes the per-chunk size
 * table required by the HPKG format when compression is active.
 *
 * @see PackageFileHeapAccessorBase, PackageFileHeapReader
 */


#include <package/hpkg/PackageFileHeapWriter.h>

#include <algorithm>
#include <new>

#include <ByteOrder.h>
#include <List.h>
#include <package/hpkg/ErrorOutput.h>
#include <package/hpkg/HPKGDefs.h>

#include <AutoDeleter.h>
#include <package/hpkg/DataReader.h>
#include <package/hpkg/PackageFileHeapReader.h>
#include <RangeArray.h>
#include <CompressionAlgorithm.h>


// minimum length of data we require before trying to compress them
static const size_t kCompressionSizeThreshold = 64;


namespace BPackageKit {

namespace BHPKG {

namespace BPrivate {


/** @brief Metadata for a single compressed chunk tracked by ChunkBuffer. */
struct PackageFileHeapWriter::Chunk {
	uint64	offset;
	uint32	compressedSize;
	uint32	uncompressedSize;
	void*	buffer;
};


/** @brief Identifies the portion of a chunk that should be preserved during range removal. */
struct PackageFileHeapWriter::ChunkSegment {
	ssize_t	chunkIndex;
	uint32	toKeepOffset;
	uint32	toKeepSize;
};


/**
 * @brief Internal buffer manager that reads and caches compressed chunks during range removal.
 *
 * ChunkBuffer builds ordered lists of Chunk and ChunkSegment descriptors from
 * the data ranges that must be preserved.  It lazily reads each chunk's
 * compressed bytes from the heap file and recycles I/O buffers to bound peak
 * memory usage.
 */
struct PackageFileHeapWriter::ChunkBuffer {
	/**
	 * @brief Construct the chunk buffer for use during range removal.
	 *
	 * @param writer     The heap writer that owns the compressed data.
	 * @param bufferSize Size of each per-chunk I/O buffer in bytes.
	 */
	ChunkBuffer(PackageFileHeapWriter* writer, size_t bufferSize)
		:
		fWriter(writer),
		fChunks(),
		fCurrentChunkIndex(0),
		fNextReadIndex(0),
		fSegments(),
		fCurrentSegmentIndex(0),
		fBuffers(),
		fUnusedBuffers(),
		fBufferSize(bufferSize)
	{
	}

	/**
	 * @brief Destroy the chunk buffer and free all I/O buffers.
	 */
	~ChunkBuffer()
	{
		for (int32 i = 0; void* buffer = fBuffers.ItemAt(i); i++)
			free(buffer);
	}

	/**
	 * @brief Record a segment of a compressed chunk that should be preserved.
	 *
	 * If the chunk at \a chunkOffset is already the last entry in the chunk
	 * list it is reused; otherwise a new Chunk entry is appended.
	 *
	 * @param chunkOffset      Compressed offset of the source chunk.
	 * @param compressedSize   Compressed size of the source chunk.
	 * @param uncompressedSize Uncompressed size of the source chunk.
	 * @param toKeepOffset     Byte offset within the uncompressed chunk to keep.
	 * @param toKeepSize       Number of uncompressed bytes to keep.
	 * @return true on success, false if memory allocation fails.
	 */
	bool PushChunkSegment(uint64 chunkOffset, uint32 compressedSize,
		uint32 uncompressedSize, uint32 toKeepOffset, uint32 toKeepSize)
	{
		ChunkSegment segment;
		segment.toKeepOffset = toKeepOffset;
		segment.toKeepSize = toKeepSize;

		// might refer to the last chunk
		segment.chunkIndex = fChunks.Count() - 1;

		if (segment.chunkIndex < 0
			|| fChunks.ElementAt(segment.chunkIndex).offset != chunkOffset) {
			// no, need to push a new chunk
			segment.chunkIndex++;

			Chunk chunk;
			chunk.offset = chunkOffset;
			chunk.compressedSize = compressedSize;
			chunk.uncompressedSize = uncompressedSize;
			chunk.buffer = NULL;
			if (!fChunks.Add(chunk))
				return false;
		}

		return fSegments.Add(segment);
	}

	/** @return true if no segment has been pushed yet. */
	bool IsEmpty() const
	{
		return fSegments.IsEmpty();
	}

	/** @return true if there are unprocessed segments remaining. */
	bool HasMoreSegments() const
	{
		return fCurrentSegmentIndex < fSegments.Count();
	}

	/** @return A reference to the current (not yet processed) segment. */
	const ChunkSegment& CurrentSegment() const
	{
		return fSegments[fCurrentSegmentIndex];
	}

	/**
	 * @brief Return the Chunk descriptor at the given index.
	 *
	 * @param index Zero-based chunk index.
	 * @return A const reference to the Chunk descriptor.
	 */
	const Chunk& ChunkAt(ssize_t index) const
	{
		return fChunks[index];
	}

	/** @return true if there are chunks that have not been read from disk yet. */
	bool HasMoreChunksToRead() const
	{
		return fNextReadIndex < fChunks.Count();
	}

	/** @return true if the current chunk has already been read into a buffer. */
	bool HasBufferedChunk() const
	{
		return fCurrentChunkIndex < fNextReadIndex;
	}

	/** @return The heap offset of the next chunk to be read from disk. */
	uint64 NextReadOffset() const
	{
		return fChunks[fNextReadIndex].offset;
	}

	/**
	 * @brief Read the next unread chunk's compressed bytes from disk.
	 *
	 * Obtains an I/O buffer and reads the chunk's compressed data into it.
	 * Throws status_t on I/O error.
	 */
	void ReadNextChunk()
	{
		if (!HasMoreChunksToRead())
			throw status_t(B_BAD_VALUE);

		Chunk& chunk = fChunks[fNextReadIndex++];
		chunk.buffer = _GetBuffer();

		status_t error = fWriter->ReadFileData(chunk.offset, chunk.buffer,
			chunk.compressedSize);
		if (error != B_OK)
			throw error;
	}

	/**
	 * @brief Advance past the current segment and release its chunk buffer if done.
	 *
	 * If the following segment refers to a different chunk the current chunk's
	 * I/O buffer is returned to the pool.
	 */
	void CurrentSegmentDone()
	{
		// Unless the next segment refers to the same chunk, advance to the next
		// chunk.
		const ChunkSegment& segment = fSegments[fCurrentSegmentIndex++];
		if (!HasMoreSegments()
			|| segment.chunkIndex != CurrentSegment().chunkIndex) {
			_PutBuffer(fChunks[fCurrentChunkIndex++].buffer);
		}
	}

private:
	/**
	 * @brief Obtain a free I/O buffer, allocating one if necessary.
	 *
	 * @return Pointer to a free buffer of fBufferSize bytes.
	 * @throws std::bad_alloc if allocation fails.
	 */
	void* _GetBuffer()
	{
		if (!fUnusedBuffers.IsEmpty())
			return fUnusedBuffers.RemoveItem(fUnusedBuffers.CountItems() - 1);

		void* buffer = malloc(fBufferSize);
		if (buffer == NULL || !fBuffers.AddItem(buffer)) {
			free(buffer);
			throw std::bad_alloc();
		}

		return buffer;
	}

	/**
	 * @brief Return a buffer to the unused pool.
	 *
	 * If the buffer cannot be tracked it is freed and removed from the
	 * master list.
	 *
	 * @param buffer Pointer to the buffer to recycle; NULL is silently ignored.
	 */
	void _PutBuffer(void* buffer)
	{
		if (buffer != NULL && !fUnusedBuffers.AddItem(buffer)) {
			fBuffers.RemoveItem(buffer);
			free(buffer);
		}
	}

private:
	PackageFileHeapWriter*	fWriter;

	Array<Chunk>			fChunks;
	ssize_t					fCurrentChunkIndex;
	ssize_t					fNextReadIndex;

	Array<ChunkSegment>		fSegments;
	ssize_t					fCurrentSegmentIndex;

	BList					fBuffers;
	BList					fUnusedBuffers;
	size_t					fBufferSize;
};


/**
 * @brief Construct the heap writer.
 *
 * Acquires a reference to \a compressionAlgorithm so it stays valid while
 * the writer is alive.
 *
 * @param errorOutput            Diagnostic output channel.
 * @param file                   Positioned I/O object representing the package file.
 * @param heapOffset             Byte offset of the heap within \a file.
 * @param compressionAlgorithm   Compression algorithm to use, or NULL to
 *                               write uncompressed chunks.
 * @param decompressionAlgorithm Decompression algorithm used when re-reading
 *                               written chunks, or NULL for uncompressed heaps.
 */
PackageFileHeapWriter::PackageFileHeapWriter(BErrorOutput* errorOutput,
	BPositionIO* file, off_t heapOffset,
	CompressionAlgorithmOwner* compressionAlgorithm,
	DecompressionAlgorithmOwner* decompressionAlgorithm)
	:
	PackageFileHeapAccessorBase(errorOutput, file, heapOffset,
		decompressionAlgorithm),
	fPendingDataBuffer(NULL),
	fCompressedDataBuffer(NULL),
	fPendingDataSize(0),
	fOffsets(),
	fCompressionAlgorithm(compressionAlgorithm)
{
	if (fCompressionAlgorithm != NULL)
		fCompressionAlgorithm->AcquireReference();
}


/**
 * @brief Destroy the heap writer and release all resources.
 */
PackageFileHeapWriter::~PackageFileHeapWriter()
{
	_Uninit();

	if (fCompressionAlgorithm != NULL)
		fCompressionAlgorithm->ReleaseReference();
}


/**
 * @brief Allocate the pending-data and compressed-data scratch buffers.
 *
 * Must be called before AddData().  Throws std::bad_alloc on failure.
 */
void
PackageFileHeapWriter::Init()
{
	// allocate data buffers
	fPendingDataBuffer = malloc(kChunkSize);
	fCompressedDataBuffer = malloc(kChunkSize);
	if (fPendingDataBuffer == NULL || fCompressedDataBuffer == NULL)
		throw std::bad_alloc();
}


/**
 * @brief Reinitialise the writer to continue from an existing heap reader's state.
 *
 * Copies the heap parameters and chunk offset table from \a heapReader, then
 * reads the last partial chunk (if any) back into the pending-data buffer so
 * that subsequent AddData() calls append correctly.
 *
 * @param heapReader Fully initialised reader for the heap to continue writing.
 */
void
PackageFileHeapWriter::Reinit(PackageFileHeapReader* heapReader)
{
	fHeapOffset = heapReader->HeapOffset();
	fCompressedHeapSize = heapReader->CompressedHeapSize();
	fUncompressedHeapSize = heapReader->UncompressedHeapSize();
	fPendingDataSize = 0;

	// copy the offsets array
	size_t chunkCount = (fUncompressedHeapSize + kChunkSize - 1) / kChunkSize;
	if (chunkCount > 0) {
		if (!fOffsets.AddUninitialized(chunkCount))
			throw std::bad_alloc();

		for (size_t i = 0; i < chunkCount; i++)
			fOffsets[i] = heapReader->Offsets()[i];
	}

	_UnwriteLastPartialChunk();
}


/**
 * @brief Append data from a BDataReader to the heap.
 *
 * Reads \a size bytes from \a dataReader in chunks, accumulating them in the
 * pending-data buffer and flushing full chunks to disk as they fill up.
 * Returns the uncompressed heap offset at which the data begins in \a _offset.
 *
 * @param dataReader Source of the data to append.
 * @param size       Number of bytes to append.
 * @param _offset    Output parameter receiving the start offset of the
 *                   appended data within the uncompressed heap.
 * @return B_OK on success, or any error from dataReader or _FlushPendingData().
 */
status_t
PackageFileHeapWriter::AddData(BDataReader& dataReader, off_t size,
	uint64& _offset)
{
	_offset = fUncompressedHeapSize;

	// copy the data to the heap
	off_t readOffset = 0;
	off_t remainingSize = size;
	while (remainingSize > 0) {
		// read data into pending data buffer
		size_t toCopy = std::min(remainingSize,
			off_t(kChunkSize - fPendingDataSize));
		status_t error = dataReader.ReadData(readOffset,
			(uint8*)fPendingDataBuffer + fPendingDataSize, toCopy);
		if (error != B_OK) {
			fErrorOutput->PrintError("Failed to read data: %s\n",
				strerror(error));
			return error;
		}

		fPendingDataSize += toCopy;
		fUncompressedHeapSize += toCopy;
		remainingSize -= toCopy;
		readOffset += toCopy;

		if (fPendingDataSize == kChunkSize) {
			error = _FlushPendingData();
			if (error != B_OK)
				return error;
		}
	}

	return B_OK;
}


/**
 * @brief Append a raw memory buffer to the heap, throwing on error.
 *
 * Convenience wrapper around AddData() that throws status_t instead of
 * returning an error code.
 *
 * @param buffer Pointer to the data to append.
 * @param size   Number of bytes to append.
 * @throws status_t if AddData() fails.
 */
void
PackageFileHeapWriter::AddDataThrows(const void* buffer, size_t size)
{
	BBufferDataReader reader(buffer, size);
	uint64 dummyOffset;
	status_t error = AddData(reader, size, dummyOffset);
	if (error != B_OK)
		throw status_t(error);
}


/**
 * @brief Remove the specified byte ranges from the heap in place.
 *
 * Flushes pending data, builds a list of chunk segments to retain,
 * then recompresses and rewrites all affected data starting from the
 * first touched chunk.  Complete aligned chunks that need no recompression
 * are copied directly in their compressed form to avoid decompression
 * overhead.  Throws status_t or std::bad_alloc on failure.
 *
 * @param ranges Array of non-overlapping byte ranges to remove, sorted by
 *               offset in ascending order.
 */
void
PackageFileHeapWriter::RemoveDataRanges(
	const ::BPrivate::RangeArray<uint64>& ranges)
{
	ssize_t rangeCount = ranges.CountRanges();
	if (rangeCount == 0)
		return;

	if (fUncompressedHeapSize == 0) {
		fErrorOutput->PrintError("Can't remove ranges from empty heap\n");
		throw status_t(B_BAD_VALUE);
	}

	// Before we begin flush any pending data, so we don't need any special
	// handling and also can use the pending data buffer.
	status_t status = _FlushPendingData();
	if (status != B_OK)
		throw status_t(status);

	// We potentially have to recompress all data from the first affected chunk
	// to the end (minus the removed ranges, of course). As a basic algorithm we
	// can use our usual data writing strategy, i.e. read a chunk, decompress it
	// to a temporary buffer, and write the data to keep via AddData(). There
	// are a few complications/optimizations, though:
	// * As data moves to other chunks, it may actually compress worse than
	//   before. While unlikely, we still have to take care of this case by
	//   making sure our reading end is at least a complete uncompressed chunk
	//   ahead of the writing end.
	// * When we run into the situation that we have to move complete aligned
	//   chunks, we want to avoid uncompressing and recompressing them
	//   needlessly.

	// Build a list of (possibly partial) chunks we want to keep.

	// the first partial chunk (if any) and all chunks between ranges
	ChunkBuffer chunkBuffer(this, kChunkSize);
	uint64 writeOffset = ranges[0].offset - ranges[0].offset % kChunkSize;
	uint64 readOffset = writeOffset;
	for (ssize_t i = 0; i < rangeCount; i++) {
		const Range<uint64>& range = ranges[i];
		if (range.size > 0) {
			_PushChunks(chunkBuffer, readOffset, range.offset);
			readOffset = range.offset + range.size;
		}
	}

	if (readOffset == writeOffset) {
		fErrorOutput->PrintError("Only empty ranges to remove from heap\n");
		throw status_t(B_BAD_VALUE);
	}

	// all chunks after the last range
	_PushChunks(chunkBuffer, readOffset, fUncompressedHeapSize);

	// Reset our state to look like all chunks from the first affected one have
	// been removed and re-add all data we want to keep.

	// truncate the offsets array and reset the heap sizes
	ssize_t firstChunkIndex = ssize_t(writeOffset / kChunkSize);
	fCompressedHeapSize = fOffsets[firstChunkIndex];
	fUncompressedHeapSize = (uint64)firstChunkIndex * kChunkSize;
	fOffsets.Remove(firstChunkIndex, fOffsets.Count() - firstChunkIndex);

	// we need a decompression buffer
	void* decompressionBuffer = malloc(kChunkSize);
	if (decompressionBuffer == NULL)
		throw std::bad_alloc();
	MemoryDeleter decompressionBufferDeleter(decompressionBuffer);

	const Chunk* decompressedChunk = NULL;

	while (chunkBuffer.HasMoreSegments()) {
		const ChunkSegment& segment = chunkBuffer.CurrentSegment();

		// If we have an aligned, complete chunk, copy its compressed data.
		bool copyCompressed = fPendingDataSize == 0 && segment.toKeepOffset == 0
			&& segment.toKeepSize == kChunkSize;

		// Read more chunks. We need at least one buffered one to do anything
		// and we want to buffer as many as necessary to ensure we don't
		// overwrite one we haven't buffered yet.
		while (chunkBuffer.HasMoreChunksToRead()
			&& (!chunkBuffer.HasBufferedChunk()
				|| (!copyCompressed
					&& chunkBuffer.NextReadOffset()
						< fCompressedHeapSize + kChunkSize))) {
			// read chunk
			chunkBuffer.ReadNextChunk();
		}

		// copy compressed chunk data, if possible
		const Chunk& chunk = chunkBuffer.ChunkAt(segment.chunkIndex);
		if (copyCompressed) {
			status_t error = _WriteChunk(chunk.buffer, chunk.compressedSize,
				false);
			if (error != B_OK)
				throw error;
			continue;
		}

		// decompress chunk, if compressed
		void* uncompressedData;
		if (chunk.uncompressedSize == chunk.compressedSize) {
			uncompressedData = chunk.buffer;
		} else if (decompressedChunk == &chunk) {
			uncompressedData = decompressionBuffer;
		} else {
			iovec compressed = { chunk.buffer, chunk.compressedSize },
				uncompressed = { decompressionBuffer, chunk.uncompressedSize };
			status_t error = DecompressChunkData(compressed, uncompressed);
			if (error != B_OK)
				throw error;

			decompressedChunk = &chunk;
			uncompressedData = decompressionBuffer;
		}

		// add chunk data
		AddDataThrows((uint8*)uncompressedData + segment.toKeepOffset,
			segment.toKeepSize);

		chunkBuffer.CurrentSegmentDone();
	}

	// Make sure a last partial chunk ends up in the pending data buffer. This
	// is only necessary when we didn't have to move any chunk segments, since
	// the loop would otherwise have read it in and left it in the pending data
	// buffer.
	if (chunkBuffer.IsEmpty())
		_UnwriteLastPartialChunk();
}


/**
 * @brief Flush any remaining pending data and write the chunk-size table.
 *
 * Flushes the pending-data buffer, then (when compression is active and more
 * than one chunk was written) serialises the per-chunk size table to the end
 * of the compressed heap using the pending-data buffer as scratch space.
 *
 * @return B_OK on success, or any error from _FlushPendingData() or
 *         _WriteDataUncompressed().
 */
status_t
PackageFileHeapWriter::Finish()
{
	// flush pending data, if any
	status_t error = _FlushPendingData();
	if (error != B_OK)
		return error;

	// write chunk sizes table

	// We don't need to do that, if we don't use any compression.
	if (fCompressionAlgorithm == NULL)
		return B_OK;

	// We don't need to write the last chunk size, since it is implied by the
	// total size minus the sum of all other chunk sizes.
	ssize_t offsetCount = fOffsets.Count();
	if (offsetCount < 2)
		return B_OK;

	// Convert the offsets to 16 bit sizes and write them. We use the (no longer
	// used) pending data buffer for the conversion.
	uint16* buffer = (uint16*)fPendingDataBuffer;
	for (ssize_t offsetIndex = 1; offsetIndex < offsetCount;) {
		ssize_t toWrite = std::min(offsetCount - offsetIndex,
			ssize_t(kChunkSize / 2));

		for (ssize_t i = 0; i < toWrite; i++, offsetIndex++) {
			// store chunkSize - 1, so it fits 16 bit (chunks cannot be empty)
			buffer[i] = B_HOST_TO_BENDIAN_INT16(
				uint16(fOffsets[offsetIndex] - fOffsets[offsetIndex - 1] - 1));
		}

		error = _WriteDataUncompressed(buffer, toWrite * 2);
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


/**
 * @brief Decompress a single heap chunk for use during range removal.
 *
 * If the chunk has not yet been flushed to disk its data is still in the
 * pending-data buffer; it is copied directly.  Otherwise the chunk is
 * located via fOffsets and decompressed from the file.
 *
 * @param chunkIndex             Zero-based index of the chunk to read.
 * @param compressedDataBuffer   Scratch buffer for compressed bytes.
 * @param uncompressedDataBuffer Destination buffer for decompressed data.
 * @param scratchBuffer          Optional algorithm-specific scratch buffer.
 * @return B_OK on success, or any error from ReadAndDecompressChunkData().
 */
status_t
PackageFileHeapWriter::ReadAndDecompressChunk(size_t chunkIndex,
	void* compressedDataBuffer, void* uncompressedDataBuffer,
	iovec* scratchBuffer)
{
	if (uint64(chunkIndex + 1) * kChunkSize > fUncompressedHeapSize) {
		// The chunk has not been written to disk yet. Its data are still in the
		// pending data buffer.
		memcpy(uncompressedDataBuffer, fPendingDataBuffer, fPendingDataSize);
		// TODO: This can be optimized. Since we write to a BDataIO anyway,
		// there's no need to copy the data.
		return B_OK;
	}

	uint64 offset = fOffsets[chunkIndex];
	size_t compressedSize = chunkIndex + 1 == (size_t)fOffsets.Count()
		? fCompressedHeapSize - offset
		: fOffsets[chunkIndex + 1] - offset;

	return ReadAndDecompressChunkData(offset, compressedSize, kChunkSize,
		compressedDataBuffer, uncompressedDataBuffer, scratchBuffer);
}


/**
 * @brief Free the pending-data and compressed-data buffers.
 *
 * Sets both pointers to NULL so that subsequent calls are safe.
 */
void
PackageFileHeapWriter::_Uninit()
{
	free(fPendingDataBuffer);
	free(fCompressedDataBuffer);
	fPendingDataBuffer = NULL;
	fCompressedDataBuffer = NULL;
}


/**
 * @brief Write the pending-data buffer to disk as a new chunk if non-empty.
 *
 * Delegates to _WriteChunk() with compression enabled, then resets
 * fPendingDataSize to zero on success.
 *
 * @return B_OK on success, or any error from _WriteChunk().
 */
status_t
PackageFileHeapWriter::_FlushPendingData()
{
	if (fPendingDataSize == 0)
		return B_OK;

	status_t error = _WriteChunk(fPendingDataBuffer, fPendingDataSize, true);
	if (error == B_OK)
		fPendingDataSize = 0;

	return error;
}


/**
 * @brief Record the current heap position and write one chunk to the file.
 *
 * Appends the current fCompressedHeapSize to fOffsets, attempts compression
 * when \a mayCompress is true and the data is large enough, and falls back to
 * uncompressed writing if compression fails or produces no saving.
 *
 * @param data         Pointer to the chunk data to write.
 * @param size         Number of bytes in the chunk.
 * @param mayCompress  Whether compression should be attempted.
 * @return B_OK on success, B_NO_MEMORY if the offset array allocation fails,
 *         or any error from _WriteDataCompressed() / _WriteDataUncompressed().
 */
status_t
PackageFileHeapWriter::_WriteChunk(const void* data, size_t size,
	bool mayCompress)
{
	// add offset
	if (!fOffsets.Add(fCompressedHeapSize)) {
		fErrorOutput->PrintError("Out of memory!\n");
		return B_NO_MEMORY;
	}

	// Try to use compression only for data large enough.
	bool compress = mayCompress && size >= (off_t)kCompressionSizeThreshold;
	if (compress) {
		status_t error = _WriteDataCompressed(data, size);
		if (error != B_OK) {
			if (error != B_BUFFER_OVERFLOW)
				return error;
			compress = false;
		}
	}

	// Write uncompressed, if necessary.
	if (!compress) {
		status_t error = _WriteDataUncompressed(data, size);
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


/**
 * @brief Compress \a data and write it to the file if smaller than the original.
 *
 * Uses fCompressionAlgorithm to compress the data into fCompressedDataBuffer.
 * Returns B_BUFFER_OVERFLOW (without printing an error) when the compressed
 * result is not smaller than the uncompressed input, signalling the caller to
 * fall back to uncompressed writing.
 *
 * @param data Pointer to the uncompressed input data.
 * @param size Size of the uncompressed input in bytes.
 * @return B_OK on success, B_BUFFER_OVERFLOW if compression is ineffective,
 *         or any error from the compression algorithm.
 */
status_t
PackageFileHeapWriter::_WriteDataCompressed(const void* data, size_t size)
{
	if (fCompressionAlgorithm == NULL)
		return B_BUFFER_OVERFLOW;

	const iovec uncompressed = { (void*)data, size };
	iovec compressed = { fCompressedDataBuffer, size };
	status_t error = fCompressionAlgorithm->algorithm->CompressBuffer(
		uncompressed, compressed,
		fCompressionAlgorithm->parameters);
	if (error != B_OK) {
		if (error != B_BUFFER_OVERFLOW) {
			fErrorOutput->PrintError("Failed to compress chunk data: %s\n",
				strerror(error));
		}
		return error;
	}

	// only use compressed data when we've actually saved space
	if (compressed.iov_len == size)
		return B_BUFFER_OVERFLOW;

	return _WriteDataUncompressed(fCompressedDataBuffer, compressed.iov_len);
}


/**
 * @brief Write raw bytes to the package file at the current heap end.
 *
 * Uses BPositionIO::WriteAtExactly() to place \a size bytes at
 * fHeapOffset + fCompressedHeapSize, then advances fCompressedHeapSize.
 *
 * @param data Pointer to the bytes to write.
 * @param size Number of bytes to write.
 * @return B_OK on success, or any error from WriteAtExactly().
 */
status_t
PackageFileHeapWriter::_WriteDataUncompressed(const void* data, size_t size)
{
	status_t error = fFile->WriteAtExactly(
		fHeapOffset + (off_t)fCompressedHeapSize, data, size);
	if (error != B_OK) {
		fErrorOutput->PrintError("Failed to write data: %s\n", strerror(error));
		return error;
	}

	fCompressedHeapSize += size;

	return B_OK;
}


/**
 * @brief Push all chunk segments covering [startOffset, endOffset) into the buffer.
 *
 * Iterates over the chunks that overlap the given uncompressed range and calls
 * chunkBuffer.PushChunkSegment() for each portion that should be kept.
 * Throws status_t(B_BAD_VALUE) if \a endOffset exceeds the heap or
 * std::bad_alloc if PushChunkSegment() runs out of memory.
 *
 * @param chunkBuffer Destination ChunkBuffer to receive the segments.
 * @param startOffset Start of the uncompressed range to push (inclusive).
 * @param endOffset   End of the uncompressed range to push (exclusive).
 */
void
PackageFileHeapWriter::_PushChunks(ChunkBuffer& chunkBuffer, uint64 startOffset,
	uint64 endOffset)
{
	if (endOffset > fUncompressedHeapSize) {
		fErrorOutput->PrintError("Invalid range to remove from heap\n");
		throw status_t(B_BAD_VALUE);
	}

	ssize_t chunkIndex = startOffset / kChunkSize;
	uint64 uncompressedChunkOffset = (uint64)chunkIndex * kChunkSize;

	while (startOffset < endOffset) {
		bool isLastChunk = fUncompressedHeapSize - uncompressedChunkOffset
			<= kChunkSize;
		uint32 inChunkOffset = uint32(startOffset - uncompressedChunkOffset);
		uint32 uncompressedChunkSize = isLastChunk
			? fUncompressedHeapSize - uncompressedChunkOffset
			: kChunkSize;
		uint64 compressedChunkOffset = fOffsets[chunkIndex];
		uint32 compressedChunkSize = isLastChunk
			? fCompressedHeapSize - compressedChunkOffset
			: fOffsets[chunkIndex + 1] - compressedChunkOffset;
		uint32 toKeepSize = uint32(std::min(
			(uint64)uncompressedChunkSize - inChunkOffset,
			endOffset - startOffset));

		if (!chunkBuffer.PushChunkSegment(compressedChunkOffset,
				compressedChunkSize, uncompressedChunkSize, inChunkOffset,
				toKeepSize)) {
			throw std::bad_alloc();
		}

		startOffset += toKeepSize;
		chunkIndex++;
		uncompressedChunkOffset += uncompressedChunkSize;
	}
}


/**
 * @brief Re-read the last partial chunk from disk into the pending-data buffer.
 *
 * When the uncompressed heap size is not a multiple of kChunkSize the last
 * chunk on disk is partial.  This method decompresses it back into
 * fPendingDataBuffer, removes it from the offset table, and resets the
 * compressed heap size so the partial chunk will be rewritten (possibly
 * differently) by the next Finish() call.  Throws status_t on error.
 */
void
PackageFileHeapWriter::_UnwriteLastPartialChunk()
{
	// If the last chunk is partial, read it in and remove it from the offsets.
	size_t lastChunkSize = fUncompressedHeapSize % kChunkSize;
	if (lastChunkSize != 0) {
		uint64 lastChunkOffset = fOffsets[fOffsets.Count() - 1];
		size_t compressedSize = fCompressedHeapSize - lastChunkOffset;

		status_t error = ReadAndDecompressChunkData(lastChunkOffset,
			compressedSize, lastChunkSize, fCompressedDataBuffer,
			fPendingDataBuffer);
		if (error != B_OK)
			throw error;

		fPendingDataSize = lastChunkSize;
		fCompressedHeapSize = lastChunkOffset;
		fOffsets.Remove(fOffsets.Count() - 1);
	}
}


}	// namespace BPrivate

}	// namespace BHPKG

}	// namespace BPackageKit
