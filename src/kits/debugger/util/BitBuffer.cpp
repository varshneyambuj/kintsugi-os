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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file BitBuffer.cpp
 * @brief Append-only bitstream buffer used by the DWARF reader and friends.
 *
 * Allows callers to accumulate arbitrary bit-aligned chunks (or runs of
 * zero bits) into a contiguous byte buffer, packing partial bytes across
 * subsequent calls. Used for assembling DWARF location-expression results
 * and similar bit-packed payloads.
 */


#include "BitBuffer.h"


// #pragma mark - BitReader


/**
 * @brief Internal helper that reads a sequence of bits from a source buffer.
 *
 * Tracks the current data pointer, remaining bit count, and intra-byte bit
 * offset. Used by BitBuffer to consume input piece by piece.
 */
struct BitBuffer::BitReader {
	const uint8*	data;
	uint64			bitSize;
	uint32			bitOffset;

	BitReader(const uint8* data, uint64 bitSize, uint32 bitOffset)
		:
		data(data),
		bitSize(bitSize),
		bitOffset(bitOffset)
	{
	}

	uint8 ReadByte()
	{
		uint8 byte = *data;
		data++;
		bitSize -= 8;

		if (bitOffset == 0)
			return byte;

		return (byte << bitOffset) | (*data >> (8 - bitOffset));
	}

	uint8 ReadBits(uint32 count)
	{
		uint8 byte = *data;
		bitSize -= count;
		bitOffset += count;

		if (bitOffset <= 8) {
			if (bitOffset == 8) {
				bitOffset = 0;
				data++;
				return byte & ((1 << count) - 1);
			}

			return (byte >> (8 - bitOffset)) & ((1 << count) - 1);
		}

		data++;
		bitOffset -= 8;
		return ((byte << bitOffset) | (*data >> (8 - bitOffset)))
			& ((1 << count) - 1);
	}
};


// #pragma mark - BitBuffer


/** @brief Construct an empty buffer with no missing bits in the last byte. */
BitBuffer::BitBuffer()
	:
	fMissingBits(0)
{
}


/** @brief Destructor; the underlying byte array frees itself. */
BitBuffer::~BitBuffer()
{
}


/**
 * @brief Append @a size bytes of byte-aligned data to the buffer.
 *
 * If the buffer is currently byte-aligned (no partial trailing byte),
 * the bytes are copied directly. Otherwise this delegates to AddBits()
 * to handle the bit-level packing.
 *
 * @param data  Source bytes to append.
 * @param size  Number of bytes to append.
 * @return true on success, false on allocation failure.
 */
bool
BitBuffer::AddBytes(const void* data, size_t size)
{
	if (size == 0)
		return true;

	if (fMissingBits == 0) {
		size_t oldSize = fBytes.Size();
		if (!fBytes.AddUninitialized(size))
			return false;

		memcpy(fBytes.Elements() + oldSize, data, size);
		return true;
	}

	return AddBits(data, (uint64)size * 8, 0);
}


/**
 * @brief Append @a bitSize bits from @a _data, packing across the current trailing byte.
 *
 * Honors @a bitOffset (which may exceed 8) so callers can append a slice
 * starting partway into a source byte. Resizes the underlying array as
 * needed and tracks any partial byte produced at the tail.
 *
 * @param _data      Source buffer.
 * @param bitSize    Number of bits to append.
 * @param bitOffset  Starting bit offset within @a _data.
 * @return true on success, false on allocation failure.
 */
bool
BitBuffer::AddBits(const void* _data, uint64 bitSize, uint32 bitOffset)
{
	if (bitSize == 0)
		return true;

	const uint8* data = (const uint8*)_data + bitOffset / 8;
	bitOffset %= 8;

	BitReader reader(data, bitSize, bitOffset);

	// handle special case first: no more bits than missing
	size_t oldSize = fBytes.Size();
	if (fMissingBits > 0 && bitSize <= fMissingBits) {
		fMissingBits -= bitSize;
		uint8 bits = reader.ReadBits(bitSize) << fMissingBits;
		fBytes[oldSize - 1] |= bits;
		return true;
	}

	// resize the buffer
	if (!fBytes.AddUninitialized((reader.bitSize - fMissingBits + 7) / 8))
		return false;

	// fill in missing bits
	if (fMissingBits > 0) {
		fBytes[oldSize - 1] |= reader.ReadBits(fMissingBits);
		fMissingBits = 0;
	}

	// read full bytes as long as we can
	uint8* buffer = fBytes.Elements() + oldSize;
	while (reader.bitSize >= 8) {
		*buffer = reader.ReadByte();
		buffer++;
	}

	// If we have left-over bits, write a partial byte.
	if (reader.bitSize > 0) {
		fMissingBits = 8 - reader.bitSize;
		*buffer = reader.ReadBits(reader.bitSize) << fMissingBits;
	}

	return true;
}


/**
 * @brief Append @a bitSize zero bits to the buffer.
 *
 * Equivalent to AddBits() with an all-zero source but cheaper because no
 * input has to be read. Updates the trailing-byte bookkeeping the same way.
 *
 * @param bitSize  Number of zero bits to append.
 * @return true on success, false on allocation failure.
 */
bool
BitBuffer::AddZeroBits(uint64 bitSize)
{
	if (bitSize == 0)
		return true;

	// handle special case first: no more bits than missing
	size_t oldSize = fBytes.Size();
	if (fMissingBits > 0 && bitSize <= fMissingBits) {
		fMissingBits -= bitSize;
		return true;
	}

	// resize the buffer
	if (!fBytes.AddUninitialized((bitSize - fMissingBits + 7) / 8))
		return false;

	// fill in missing bits
	if (fMissingBits > 0) {
		bitSize -= fMissingBits;
		fMissingBits = 0;
	}

	// zero the remaining bytes, including a potentially partial last byte
	uint8* buffer = fBytes.Elements() + oldSize;
	memset(buffer, 0, (bitSize + 7) / 8);
	bitSize %= 8;

	if (bitSize > 0)
		fMissingBits = 8 - bitSize;

	return true;
}
