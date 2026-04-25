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
 *   Copyright 2009-2014, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file PackageDataReaderV1.cpp
 * @brief Factory and reader implementations for v1 HPKG package data blocks.
 *
 * Provides UncompressedPackageDataReader and ZlibPackageDataReader, concrete
 * BAbstractBufferedDataReader subclasses that decompress or pass through a
 * BPackageData region from a v1 HPKG heap. BPackageDataReaderFactory selects
 * the appropriate reader based on the BPackageData compression field.
 *
 * @see BPackageData (V1), BAbstractBufferedDataReader, BBufferPool
 */


#include <package/hpkg/v1/PackageDataReader.h>

#include <string.h>

#include <algorithm>
#include <new>

#include <DataIO.h>
#include <package/hpkg/BufferPool.h>
#include <package/hpkg/PoolBuffer.h>
#include <package/hpkg/v1/HPKGDefsPrivate.h>
#include <package/hpkg/v1/PackageData.h>
#include <ZlibCompressionAlgorithm.h>


namespace BPackageKit {

namespace BHPKG {

namespace V1 {


using BHPKG::BPrivate::PoolBufferPutter;


/** @brief Minimum zlib chunk size accepted as plausible by the reader. */
static const size_t kMinSaneZlibChunkSize = 1024;
/** @brief Maximum zlib chunk size accepted as plausible by the reader. */
static const size_t kMaxSaneZlibChunkSize = 10 * 1024 * 1024;

/** @brief Maximum number of entries cached in the zlib offset-table window. */
static const uint32 kMaxZlibOffsetTableBufferSize = 512;

/** @brief Working buffer size used by the uncompressed reader when streaming
 *         to a BDataIO output. */
static const size_t kUncompressedReaderBufferSize
	= B_HPKG_DEFAULT_DATA_CHUNK_SIZE_ZLIB;


// #pragma mark - PackageDataReader


/**
 * @brief Abstract base for v1 HPKG package data readers.
 *
 * Holds the underlying BDataReader (the heap reader) and exposes a pure
 * virtual Init() that subclasses use to capture range information from a
 * BPackageData record.
 */
class PackageDataReader : public BAbstractBufferedDataReader {
public:
	/**
	 * @brief Construct the reader bound to a heap data reader.
	 * @param dataReader Reader providing access to the v1 HPKG heap.
	 */
	PackageDataReader(BDataReader* dataReader)
		:
		fDataReader(dataReader)
	{
	}

	/** @brief Destroy the reader; subclasses release their own buffers. */
	virtual ~PackageDataReader()
	{
	}

	/**
	 * @brief Capture the package data record's offset and size info.
	 * @param data Package data record being read.
	 * @return B_OK on success, or an error from validation.
	 */
	virtual status_t Init(const BPackageData& data) = 0;

protected:
			BDataReader*			fDataReader;
};


// #pragma mark - UncompressedPackageDataReader


/**
 * @brief Pass-through reader for v1 HPKG package data stored without
 *        compression.
 *
 * Delegates reads to the underlying BDataReader, applying the package
 * data's offset and size as bounds.
 */
class UncompressedPackageDataReader : public PackageDataReader {
public:
	/**
	 * @brief Construct the uncompressed reader.
	 * @param dataReader Reader providing access to the v1 HPKG heap.
	 * @param bufferPool Buffer pool used to obtain temporary buffers when
	 *                   streaming to a BDataIO sink.
	 */
	UncompressedPackageDataReader(BDataReader* dataReader,
		BBufferPool* bufferPool)
		:
		PackageDataReader(dataReader),
		fBufferPool(bufferPool)
	{
	}

	/**
	 * @brief Capture the package data record's offset and uncompressed size.
	 * @param data Package data record describing the uncompressed range.
	 * @return Always B_OK.
	 */
	status_t Init(const BPackageData& data)
	{
		fOffset = data.Offset();
		fSize = data.UncompressedSize();
		return B_OK;
	}

	/**
	 * @brief Read @a size bytes at @a offset within the uncompressed range
	 *        into @a buffer.
	 *
	 * @param offset Offset within the package data, in bytes.
	 * @param buffer Caller-supplied buffer of at least @a size bytes.
	 * @param size   Number of bytes to read.
	 * @retval B_OK         The read completed.
	 * @retval B_BAD_VALUE  @a offset or @a offset + @a size lies outside the
	 *                      record bounds.
	 * @return Otherwise an error from the underlying data reader.
	 */
	virtual status_t ReadData(off_t offset, void* buffer, size_t size)
	{
		if (size == 0)
			return B_OK;

		if (offset < 0)
			return B_BAD_VALUE;

		if ((uint64)offset > fSize || size > fSize - offset)
			return B_BAD_VALUE;

		return fDataReader->ReadData(fOffset + offset, buffer, size);
	}

	/**
	 * @brief Stream @a size bytes from @a offset into a BDataIO sink.
	 *
	 * Loops over a pool buffer reading from the heap and writing to the
	 * output until the requested range has been transferred.
	 *
	 * @param offset Offset within the package data, in bytes.
	 * @param size   Number of bytes to stream.
	 * @param output Sink that receives the data.
	 * @retval B_OK         The transfer completed.
	 * @retval B_BAD_VALUE  Range out of bounds.
	 * @retval B_NO_MEMORY  No temporary buffer was available.
	 * @return Otherwise an error from the reader or the output.
	 */
	virtual status_t ReadDataToOutput(off_t offset, size_t size,
		BDataIO* output)
	{
		if (size == 0)
			return B_OK;

		if (offset < 0)
			return B_BAD_VALUE;

		if ((uint64)offset > fSize || size > fSize - offset)
			return B_BAD_VALUE;

		// get a temporary buffer
		PoolBuffer* buffer = fBufferPool->GetBuffer(
			kUncompressedReaderBufferSize);
		if (buffer == NULL)
			return B_NO_MEMORY;
		PoolBufferPutter bufferPutter(fBufferPool, &buffer);

		while (size > 0) {
			// read into the buffer
			size_t toRead = std::min(size, buffer->Size());
			status_t error = fDataReader->ReadData(fOffset + offset,
				buffer->Buffer(), toRead);
			if (error != B_OK)
				return error;

			// write to the output
			error = output->WriteExactly(buffer->Buffer(), toRead);
			if (error != B_OK)
				return error;

			offset += toRead;
			size -= toRead;
		}

		return B_OK;
	}

private:
	/** @brief Buffer pool used for temporary streaming buffers. */
	BBufferPool*	fBufferPool;
	/** @brief Heap-relative offset of the package data range. */
	uint64			fOffset;
	/** @brief Total size in bytes of the uncompressed package data. */
	uint64			fSize;
};


// #pragma mark - ZlibPackageDataReader


/**
 * @brief Zlib-compressed package data reader.
 *
 * Decodes a v1 HPKG zlib-compressed data range, which is split into
 * fixed-size chunks individually compressed and prefixed by an offset
 * table. The reader caches one uncompressed chunk and a window of the
 * offset table to accelerate sequential and random access.
 */
class ZlibPackageDataReader : public PackageDataReader {
public:
	/**
	 * @brief Construct the zlib reader with no buffers yet allocated.
	 * @param dataReader Reader providing the v1 HPKG heap.
	 * @param bufferPool Buffer pool used for the chunk buffers.
	 */
	ZlibPackageDataReader(BDataReader* dataReader, BBufferPool* bufferPool)
		:
		PackageDataReader(dataReader),
		fBufferPool(bufferPool),
		fUncompressBuffer(NULL),
		fOffsetTable(NULL)
	{
	}

	/**
	 * @brief Release the offset-table buffer and return the chunk buffer to
	 *        the pool.
	 */
	~ZlibPackageDataReader()
	{
		delete[] fOffsetTable;

		fBufferPool->PutBuffer(&fUncompressBuffer);
	}

	/**
	 * @brief Capture range, chunk size, and chunk count from the package
	 *        data record and allocate the offset-table window.
	 *
	 * Validates the chunk size against the sane bounds and allocates the
	 * offset-table cache when there is more than one chunk.
	 *
	 * @param data Package data record describing the compressed range.
	 * @retval B_OK         Initialisation succeeded.
	 * @retval B_BAD_DATA   Chunk size or table size is implausible.
	 * @retval B_NO_MEMORY  Offset-table allocation failed.
	 */
	status_t Init(const BPackageData& data)
	{
		fOffset = data.Offset();
		fCompressedSize = data.CompressedSize();
		fUncompressedSize = data.UncompressedSize();
		fChunkSize = data.ChunkSize();

		// validate chunk size
		if (fChunkSize == 0)
			fChunkSize = B_HPKG_DEFAULT_DATA_CHUNK_SIZE_ZLIB;
		if (fChunkSize < kMinSaneZlibChunkSize
			|| fChunkSize > kMaxSaneZlibChunkSize) {
			return B_BAD_DATA;
		}

		fChunkCount = (fUncompressedSize + (fChunkSize - 1)) / fChunkSize;
		fOffsetTableSize = (fChunkCount - 1) * sizeof(uint64);
		if (fOffsetTableSize >= fCompressedSize)
			return B_BAD_DATA;

		// allocate a buffer for the offset table
		if (fChunkCount > 1) {
			fOffsetTableBufferEntryCount = std::min(fChunkCount - 1,
				(uint64)kMaxZlibOffsetTableBufferSize);
			fOffsetTable = new(std::nothrow) uint64[
				fOffsetTableBufferEntryCount];
			if (fOffsetTable == NULL)
				return B_NO_MEMORY;

			fOffsetTableIndex = -1;
				// mark the table content invalid
		} else
			fChunkSize = fUncompressedSize;

		// mark uncompressed content invalid
		fUncompressedChunk = -1;

		return B_OK;
	}

	/**
	 * @brief Decompress a range of zlib-compressed package data and stream
	 *        it to a BDataIO sink.
	 *
	 * Iterates chunk-by-chunk: for each chunk that overlaps the requested
	 * range, _ReadChunk() is called to populate @c fUncompressBuffer, then
	 * the relevant slice is written to the output.
	 *
	 * @param offset Offset within the uncompressed data.
	 * @param size   Number of uncompressed bytes to write.
	 * @param output Sink that receives the decompressed bytes.
	 * @retval B_OK         The transfer completed.
	 * @retval B_BAD_VALUE  Range out of bounds.
	 * @retval B_NO_MEMORY  Could not obtain the chunk buffer.
	 * @return Otherwise an error from the heap reader, the decompressor, or
	 *         the output.
	 */
	virtual status_t ReadDataToOutput(off_t offset, size_t size,
		BDataIO* output)
	{
		// check offset and size
		if (size == 0)
			return B_OK;

		if (offset < 0)
			return B_BAD_VALUE;

		if ((uint64)offset > fUncompressedSize
			|| size > fUncompressedSize - offset) {
			return B_BAD_VALUE;
		}

		// get our uncompressed chunk buffer back, if possible
		bool newBuffer;
		if (fBufferPool->GetBuffer(fChunkSize, &fUncompressBuffer, &newBuffer)
				== NULL) {
			return B_NO_MEMORY;
		}
		PoolBufferPutter uncompressBufferPutter(fBufferPool,
			&fUncompressBuffer);

		if (newBuffer)
			fUncompressedChunk = -1;

		// uncompress
		int64 chunkIndex = offset / fChunkSize;
		off_t chunkOffset = chunkIndex * fChunkSize;
		size_t inChunkOffset = offset - chunkOffset;

		while (size > 0) {
			// read and uncompress the chunk
			status_t error = _ReadChunk(chunkIndex);
			if (error != B_OK)
				return error;

			// write data to output
			size_t toCopy = std::min(size, (size_t)fChunkSize - inChunkOffset);
			error = output->WriteExactly(
				(uint8*)fUncompressBuffer->Buffer() + inChunkOffset, toCopy);
			if (error != B_OK)
				return error;

			size -= toCopy;

			chunkIndex++;
			chunkOffset += fChunkSize;
			inChunkOffset = 0;
		}

		return B_OK;
	}

private:
	/**
	 * @brief Ensure that the uncompressed chunk @a chunkIndex is loaded into
	 *        the chunk buffer.
	 *
	 * No-op when the requested chunk is already cached. Reads the chunk
	 * from the heap, optionally bypassing decompression for chunks stored
	 * literally (compressed size equals uncompressed size).
	 *
	 * @param chunkIndex Index of the chunk to load (0-based).
	 * @retval B_OK         Chunk is now in the buffer.
	 * @retval B_BAD_DATA   Decompressed size does not match the expected
	 *                      chunk size.
	 * @retval B_NO_MEMORY  Could not obtain a temporary read buffer.
	 * @return Otherwise an error from the heap reader or the decompressor.
	 */
	status_t _ReadChunk(int64 chunkIndex)
	{
		if (chunkIndex == fUncompressedChunk)
			return B_OK;

		// get the chunk offset and size
		uint64 offset = 0;
		uint32 compressedSize = 0;
		status_t error = _GetCompressedChunkOffsetAndSize(chunkIndex, offset,
			compressedSize);
		if (error != B_OK)
			return error;

		uint32 uncompressedSize = (uint64)chunkIndex + 1 < fChunkCount
			? fChunkSize : fUncompressedSize - chunkIndex * fChunkSize;

		// read the chunk
		if (compressedSize == uncompressedSize) {
			// the chunk is not compressed -- read it directly into the
			// uncompressed buffer
			error = fDataReader->ReadData(offset, fUncompressBuffer->Buffer(),
				compressedSize);
		} else {
			// read to a read buffer and uncompress
			PoolBuffer* readBuffer = fBufferPool->GetBuffer(fChunkSize);
			if (readBuffer == NULL)
				return B_NO_MEMORY;
			PoolBufferPutter readBufferPutter(fBufferPool, readBuffer);

			error = fDataReader->ReadData(offset, readBuffer->Buffer(),
				compressedSize);
			if (error != B_OK)
				return error;

			iovec compressed = { readBuffer->Buffer(), compressedSize },
				uncompressed = { fUncompressBuffer->Buffer(), uncompressedSize };
			error = BZlibCompressionAlgorithm().DecompressBuffer(
				compressed, uncompressed);
			if (error == B_OK && uncompressed.iov_len != uncompressedSize)
				error = B_BAD_DATA;
		}

		if (error != B_OK) {
			// error reading/decompressing data -- mark the cached data invalid
			fUncompressedChunk = -1;
			return error;
		}

		fUncompressedChunk = chunkIndex;
		return B_OK;
	}

	/**
	 * @brief Compute the absolute file offset and compressed size of a
	 *        specific chunk.
	 *
	 * @param chunkIndex Index of the chunk to locate (0-based).
	 * @param _offset    Output: absolute heap offset where the chunk begins.
	 * @param _size      Output: number of compressed bytes the chunk
	 *                   occupies.
	 * @retval B_OK        Offsets resolved successfully.
	 * @retval B_BAD_DATA  The end offset is less than the start offset.
	 * @return Otherwise an error from the offset-table reader.
	 */
	status_t _GetCompressedChunkOffsetAndSize(int64 chunkIndex, uint64& _offset,
		uint32& _size)
	{
		// get the offset
		uint64 offset;
		if (chunkIndex == 0) {
			// first chunk is at 0
			offset = 0;
		} else {
			status_t error = _GetCompressedChunkRelativeOffset(chunkIndex,
				offset);
			if (error != B_OK)
				return error;
		}

		// get the end offset
		uint64 endOffset;
		if ((uint64)chunkIndex + 1 == fChunkCount) {
			// last chunk end with the end of the data
			endOffset = fCompressedSize - fOffsetTableSize;
		} else {
			status_t error = _GetCompressedChunkRelativeOffset(chunkIndex + 1,
				endOffset);
			if (error != B_OK)
				return error;
		}

		// sanity check
		if (endOffset < offset)
			return B_BAD_DATA;

		_offset = fOffset + fOffsetTableSize + offset;
		_size = endOffset - offset;
		return B_OK;
	}

	/**
	 * @brief Read a chunk's relative start offset from the offset table,
	 *        repaging the cache window as needed.
	 *
	 * The offset table stores the start of every chunk after the first
	 * (chunk 0 starts at relative offset 0). When the requested chunk lies
	 * outside the currently cached window, a new window of up to
	 * @c kMaxZlibOffsetTableBufferSize entries is read, anchored either at
	 * @a chunkIndex or at index 1 if the entire table fits.
	 *
	 * @param chunkIndex Index of the chunk whose offset is requested.
	 * @param _offset    Output: relative chunk offset.
	 * @retval B_OK         Offset retrieved.
	 * @retval B_BAD_DATA   The offset exceeds the compressed-data range.
	 * @return Otherwise an error from the heap reader.
	 */
	status_t _GetCompressedChunkRelativeOffset(int64 chunkIndex,
		uint64& _offset)
	{
		if (fOffsetTableIndex < 0 || fOffsetTableIndex > chunkIndex
			|| fOffsetTableIndex + fOffsetTableBufferEntryCount <= chunkIndex) {
			// read the table at the given index, or, if we can, the whole table
			int64 readAtIndex = fChunkCount - 1 > fOffsetTableBufferEntryCount
				? chunkIndex : 1;
			uint32 entriesToRead = std::min(
				(uint64)fOffsetTableBufferEntryCount,
				fChunkCount - readAtIndex);

			status_t error = fDataReader->ReadData(
				fOffset + (readAtIndex - 1) * sizeof(uint64),
				fOffsetTable, entriesToRead * sizeof(uint64));
			if (error != B_OK) {
				fOffsetTableIndex = -1;
				return error;
			}

			fOffsetTableIndex = readAtIndex;
		}

		// get and check the offset
		_offset = fOffsetTable[chunkIndex - fOffsetTableIndex];
		if (_offset > fCompressedSize - fOffsetTableSize)
			return B_BAD_DATA;

		return B_OK;
	}

private:
	/** @brief Buffer pool used for chunk and decode buffers. */
	BBufferPool*	fBufferPool;
	/** @brief Buffer holding the most recently decompressed chunk. */
	PoolBuffer*		fUncompressBuffer;
	/** @brief Index of the chunk in @c fUncompressBuffer, or -1 if invalid. */
	int64			fUncompressedChunk;

	/** @brief Heap-relative offset where the package data begins. */
	uint64			fOffset;
	/** @brief Total uncompressed size in bytes. */
	uint64			fUncompressedSize;
	/** @brief Total compressed size in bytes (including offset table). */
	uint64			fCompressedSize;
	/** @brief Size in bytes of the chunk offset table. */
	uint64			fOffsetTableSize;
	/** @brief Total number of chunks. */
	uint64			fChunkCount;
	/** @brief Size in bytes of each uncompressed chunk. */
	uint32			fChunkSize;
	/** @brief Number of entries the offset-table cache can hold. */
	uint32			fOffsetTableBufferEntryCount;
	/** @brief Cached window of the chunk offset table. */
	uint64*			fOffsetTable;
	/** @brief First chunk index represented in @c fOffsetTable, or -1. */
	int32			fOffsetTableIndex;
};


// #pragma mark - PackageDataHeapReader


/**
 * @brief Reader for v1 package data inlined directly inside the attribute
 *        record.
 *
 * Inline data does not live in the heap; it is carried in the package
 * attribute payload itself. This class exposes that buffer through the
 * BBufferDataReader interface and keeps a copy of the parent BPackageData
 * to anchor pointer ownership.
 */
class PackageDataInlineReader : public BBufferDataReader {
public:
	/**
	 * @brief Construct the inline reader over a package data record's inline
	 *        bytes.
	 * @param data Package data record carrying inline bytes.
	 */
	PackageDataInlineReader(const BPackageData& data)
		:
		BBufferDataReader(data.InlineData(), data.UncompressedSize()),
		fData(data)
	{
	}

private:
	/** @brief Stored copy of the package data record (anchors inline ptr). */
	BPackageData	fData;
};


// #pragma mark - BPackageDataReaderFactory


/**
 * @brief Construct the factory bound to a buffer pool.
 *
 * The buffer pool is shared among all readers the factory produces so that
 * pool buffers can be recycled across reads.
 *
 * @param bufferPool Buffer pool used by manufactured readers.
 */
BPackageDataReaderFactory::BPackageDataReaderFactory(BBufferPool* bufferPool)
	:
	fBufferPool(bufferPool)
{
}


/**
 * @brief Manufacture a reader appropriate to the package data's storage
 *        kind.
 *
 * Returns a PackageDataInlineReader for inline-encoded data, an
 * UncompressedPackageDataReader when compression is @c B_HPKG_COMPRESSION_NONE,
 * or a ZlibPackageDataReader for zlib-compressed data. Other compression
 * methods are rejected with B_BAD_VALUE.
 *
 * @param dataReader Reader providing the v1 HPKG heap.
 * @param data       Package data record describing the range.
 * @param _reader    Output: heap-allocated reader on success.
 * @retval B_OK         Reader created.
 * @retval B_BAD_VALUE  Unrecognised compression method.
 * @retval B_NO_MEMORY  Allocation failure.
 * @return Otherwise an error from the reader's Init().
 * @note The caller owns @a _reader and must @c delete it when finished.
 */
status_t
BPackageDataReaderFactory::CreatePackageDataReader(BDataReader* dataReader,
	const BPackageData& data, BAbstractBufferedDataReader*& _reader)
{
	if (data.IsEncodedInline()) {
		BAbstractBufferedDataReader* reader
			= new(std::nothrow) PackageDataInlineReader(data);
		if (reader == NULL)
			return B_NO_MEMORY;

		_reader = reader;
		return B_OK;
	}

	PackageDataReader* reader;

	switch (data.Compression()) {
		case B_HPKG_COMPRESSION_NONE:
			reader = new(std::nothrow) UncompressedPackageDataReader(
				dataReader, fBufferPool);
			break;
		case B_HPKG_COMPRESSION_ZLIB:
			reader = new(std::nothrow) ZlibPackageDataReader(dataReader,
				fBufferPool);
			break;
		default:
			return B_BAD_VALUE;
	}

	if (reader == NULL)
		return B_NO_MEMORY;

	status_t error = reader->Init(data);
	if (error != B_OK) {
		delete reader;
		return error;
	}

	_reader = reader;
	return B_OK;
}


}	// namespace V1

}	// namespace BHPKG

}	// namespace BPackageKit
