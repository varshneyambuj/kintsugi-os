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
 *   Copyright 2009-2012, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2013, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ValueLocation.cpp
 * @brief Composite description of where a value lives (registers, memory, etc.).
 *
 * A ValueLocation is an ordered list of ValuePieceLocation entries. Each
 * piece nominates a storage class (memory, register, implicit literal, or
 * unknown) plus a bit range within that storage. The interface supports
 * slicing the same value across multiple pieces (as DWARF expressions
 * sometimes require) and extracting sub-locations by bit offset and size.
 */

#include <stdio.h>

#include "ValueLocation.h"


// #pragma mark - ValuePieceLocation


/**
 * @brief Reduce excess bytes around the active bit range to canonical form.
 *
 * Trims off complete bytes outside the [bitOffset, bitOffset + bitSize)
 * window, adjusting the address (for memory pieces) accordingly so the
 * piece still refers to the same bits but uses the smallest possible byte
 * range.
 *
 * @param bigEndian  Endianness of the surrounding value, used when adjusting
 *                   the memory address.
 * @return Reference to *this for chaining.
 */
ValuePieceLocation&
ValuePieceLocation::Normalize(bool bigEndian)
{
	uint64 excessMSBs = bitOffset / 8;
	uint64 excessLSBs = size - (bitOffset + bitSize + 7) / 8;

	if (excessMSBs > 0 || excessLSBs > 0) {
		switch (type) {
			case VALUE_PIECE_LOCATION_MEMORY:
				if (bigEndian)
					address += excessMSBs;
				else
					address += excessLSBs;
				bitOffset -= excessMSBs * 8;
				size -= excessMSBs + excessLSBs;
				break;
			case VALUE_PIECE_LOCATION_UNKNOWN:
				bitOffset -= excessMSBs * 8;
				size -= excessMSBs + excessLSBs;
				break;
			case VALUE_PIECE_LOCATION_REGISTER:
			default:
				break;
		}
	}

	return *this;
}


// #pragma mark - ValueLocation


/** @brief Construct an empty location defaulting to little endian. */
ValueLocation::ValueLocation()
	:
	fBigEndian(false),
	fWritable(false)
{
}


/**
 * @brief Construct an empty location with explicit endianness.
 *
 * @param bigEndian  true for big-endian targets.
 */
ValueLocation::ValueLocation(bool bigEndian)
	:
	fBigEndian(bigEndian),
	fWritable(false)
{
}


/**
 * @brief Construct a single-piece location.
 *
 * @param bigEndian  Endianness flag.
 * @param piece      Piece to add.
 */
ValueLocation::ValueLocation(bool bigEndian, const ValuePieceLocation& piece)
	:
	fBigEndian(bigEndian)
{
	AddPiece(piece);
}


/**
 * @brief Copy-construct from another ValueLocation.
 *
 * @param other  Source location.
 */
ValueLocation::ValueLocation(const ValueLocation& other)
	:
	fPieces(other.fPieces),
	fBigEndian(other.fBigEndian)
{
}


/**
 * @brief Reset @c *this to a sub-range of the first piece of @a other.
 *
 * Useful when descending into a struct field at a known byte offset.
 *
 * @param other       Source location to slice.
 * @param byteOffset  Byte offset into the first piece.
 * @param byteSize    Size of the slice.
 * @return true on success, false on allocation failure.
 */
bool
ValueLocation::SetToByteOffset(const ValueLocation& other, uint64 byteOffset,
	uint64 byteSize)
{
	Clear();

	fBigEndian = other.fBigEndian;
	ValuePieceLocation piece = other.PieceAt(0);
	piece.SetToMemory(piece.address + byteOffset);
	piece.SetSize(byteSize);

	return AddPiece(piece);
}


/**
 * @brief Reset @c *this to a sub-bit-range of @a other.
 *
 * Walks the source pieces in order, skipping pieces outside the requested
 * range and trimming the first/last piece as needed. Endianness controls
 * whether the slice is taken from the most-significant or least-significant
 * end of the value.
 *
 * @param other      Source location.
 * @param bitOffset  Starting bit offset within the source.
 * @param bitSize    Number of bits in the slice. Clamped against the source.
 * @return true on success, false on allocation failure.
 */
bool
ValueLocation::SetTo(const ValueLocation& other, uint64 bitOffset,
	uint64 bitSize)
{
	Clear();

	fBigEndian = other.fBigEndian;

	// compute the total bit size
	int32 count = other.CountPieces();
	uint64 totalBitSize = 0;
	for (int32 i = 0; i < count; i++) {
		const ValuePieceLocation &piece = other.PieceAt(i);
		totalBitSize += piece.bitSize;
	}

	// adjust requested bit offset/size to something reasonable, if necessary
	if (bitOffset + bitSize > totalBitSize) {
		if (bitOffset >= totalBitSize)
			return true;
		bitSize = totalBitSize - bitOffset;
	}

	if (fBigEndian) {
		// Big endian: Skip the superfluous most significant bits, copy the
		// pieces we need (cutting the first and the last one as needed) and
		// ignore the remaining pieces.

		// skip pieces for the most significant bits we don't need anymore
		uint64 bitsToSkip = bitOffset;
		int32 i;
		ValuePieceLocation piece;
		for (i = 0; i < count; i++) {
			const ValuePieceLocation& tempPiece = other.PieceAt(i);
			if (tempPiece.bitSize > bitsToSkip) {
				if (!piece.Copy(tempPiece))
					return false;
				break;
			}
			bitsToSkip -= tempPiece.bitSize;
		}

		// handle partial piece
		if (bitsToSkip > 0) {
			piece.bitOffset += bitsToSkip;
			piece.bitSize -= bitsToSkip;
			piece.Normalize(fBigEndian);
		}

		// handle remaining pieces
		while (bitSize > 0) {
			if (piece.bitSize > bitSize) {
				// the piece is bigger than the remaining size -- cut it
				piece.bitSize = bitSize;
				piece.Normalize(fBigEndian);
				bitSize = 0;
			} else
				bitSize -= piece.bitSize;

			if (!AddPiece(piece))
				return false;

			if (++i >= count)
				break;

			if (!piece.Copy(other.PieceAt(i)))
				return false;
		}
	} else {
		// Little endian: Skip the superfluous least significant bits, copy the
		// pieces we need (cutting the first and the last one as needed) and
		// ignore the remaining pieces.

		// skip pieces for the least significant bits we don't need anymore
		uint64 bitsToSkip = totalBitSize - bitOffset - bitSize;
		int32 i;
		ValuePieceLocation piece;
		for (i = 0; i < count; i++) {
			const ValuePieceLocation& tempPiece = other.PieceAt(i);
			if (tempPiece.bitSize > bitsToSkip) {
				if (!piece.Copy(tempPiece))
					return false;
				break;
			}
			bitsToSkip -= piece.bitSize;
		}

		// handle partial piece
		if (bitsToSkip > 0) {
			piece.bitSize -= bitsToSkip;
			piece.Normalize(fBigEndian);
		}

		// handle remaining pieces
		while (bitSize > 0) {
			if (piece.bitSize > bitSize) {
				// the piece is bigger than the remaining size -- cut it
				piece.bitOffset += piece.bitSize - bitSize;
				piece.bitSize = bitSize;
				piece.Normalize(fBigEndian);
				bitSize = 0;
			} else
				bitSize -= piece.bitSize;

			if (!AddPiece(piece))
				return false;

			if (++i >= count)
				break;

			if (!piece.Copy(other.PieceAt(i)))
				return false;
		}
	}

	return true;
}


/** @brief Drop all pieces and reset writability to false. */
void
ValueLocation::Clear()
{
	fWritable = false;
	fPieces.clear();
}


/**
 * @brief Append a piece to the location, updating the writability flag.
 *
 * The location is writable only when every piece is writable. The first
 * piece's writability is taken verbatim; subsequent pieces are AND-combined.
 *
 * @param piece  Piece to add.
 * @return true on success, false on allocation failure.
 */
bool
ValueLocation::AddPiece(const ValuePieceLocation& piece)
{
	// Just add, don't normalize. This allows for using the class with different
	// semantics (e.g. in the DWARF code).
	try {
		fPieces.push_back(piece);
	} catch (...) {
		return false;
	}

	if (fPieces.size() == 1)
		fWritable = piece.writable;
	else
		fWritable = fWritable && piece.writable;

	return true;
}


/** @brief Return the number of pieces currently stored. */
int32
ValueLocation::CountPieces() const
{
	return fPieces.size();
}


/**
 * @brief Return a copy of the piece at @a index.
 *
 * @param index  Zero-based index.
 * @return The piece, or a default-constructed (invalid) piece when out of range.
 */
ValuePieceLocation
ValueLocation::PieceAt(int32 index) const
{
	if (index < 0 || index >= (int32)fPieces.size())
		return ValuePieceLocation();

	return fPieces[index];
}


/**
 * @brief Replace the piece at @a index by copying from @a piece.
 *
 * @param index  Zero-based index.
 * @param piece  Replacement piece.
 * @return true on success, false if @a index is out of range or copy failed.
 */
bool
ValueLocation::SetPieceAt(int32 index, const ValuePieceLocation& piece)
{
	if (index < 0 || index >= (int32)fPieces.size())
		return false;

	return fPieces[index].Copy(piece);
}


/**
 * @brief Copy-assign from another ValueLocation.
 *
 * @param other  Source location.
 * @return Reference to *this.
 */
ValueLocation&
ValueLocation::operator=(const ValueLocation& other)
{
	fPieces = other.fPieces;
	fBigEndian = other.fBigEndian;
	return *this;
}


/**
 * @brief Print a multi-line description of the location to stdout.
 *
 * Intended for debugging; reports endianness, piece count, and per-piece
 * type, address/register, size, bit-size, and bit-offset.
 */
void
ValueLocation::Dump() const
{
	int32 count = fPieces.size();
	printf("ValueLocation: %s endian, %" B_PRId32 " pieces:\n",
		fBigEndian ? "big" : "little", count);

	for (int32 i = 0; i < count; i++) {
		const ValuePieceLocation& piece = fPieces[i];
		switch (piece.type) {
			case VALUE_PIECE_LOCATION_INVALID:
				printf("  invalid\n");
				continue;
			case VALUE_PIECE_LOCATION_UNKNOWN:
				printf("  unknown");
				break;
			case VALUE_PIECE_LOCATION_MEMORY:
				printf("  address %#" B_PRIx64, piece.address);
				break;
			case VALUE_PIECE_LOCATION_REGISTER:
				printf("  register %" B_PRIu32, piece.reg);
				break;
			case VALUE_PIECE_LOCATION_IMPLICIT:
				printf("  implicit value: ");
				for (uint32 j = 0; j < piece.size; j++)
					printf("%x ", ((char *)piece.value)[j]);
				break;
		}

		printf(" size: %" B_PRIu64 " (%" B_PRIu64 " bits), offset: %" B_PRIu64
			" bits\n", piece.size, piece.bitSize, piece.bitOffset);
	}
}
