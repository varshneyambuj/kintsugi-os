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
 *   Copyright 2013-2022, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Paweł Dziepak, pdziepak@quarnos.org
 *       Augustin Cavalier <waddlesplash>
 */

/**
 * @file Bitmap.cpp
 * @brief Dynamically sized bitmap primitive used by kernel subsystems.
 *
 * Implements BKernel::Bitmap — a heap-backed array of machine words with
 * set/clear/shift operations, a clear-bit search, and a highest-set-bit query.
 * Resize() reallocates the underlying storage and zero-fills newly added
 * elements so freshly grown regions read as clear.
 */

#include <util/Bitmap.h>

#include <stdlib.h>
#include <string.h>

#include <util/BitUtils.h>


namespace BKernel {


/**
 * @brief Construct an empty bitmap and size it to @a bitCount bits.
 * @param bitCount Initial bit capacity; 0 is allowed.
 */
Bitmap::Bitmap(size_t bitCount)
	:
	fElementsCount(0),
	fSize(0),
	fBits(NULL)
{
	Resize(bitCount);
}


/**
 * @brief Release the backing word array.
 */
Bitmap::~Bitmap()
{
	free(fBits);
}


/**
 * @brief Report whether the initial allocation succeeded.
 * @return B_OK if storage is available, B_NO_MEMORY otherwise.
 */
status_t
Bitmap::InitCheck()
{
	return (fBits != NULL) ? B_OK : B_NO_MEMORY;
}


/**
 * @brief Grow or shrink the bitmap to @a bitCount bits.
 *
 * If the new element count matches the current one only fSize is updated
 * (no reallocation). When growing, newly added words are zero-filled.
 *
 * @param bitCount New bit capacity.
 * @return B_OK on success, B_NO_MEMORY on allocation failure.
 */
status_t
Bitmap::Resize(size_t bitCount)
{
	const size_t count = (bitCount + kBitsPerElement - 1) / kBitsPerElement;
	if (count == fElementsCount) {
		fSize = bitCount;
		return B_OK;
	}

	void* bits = realloc(fBits, sizeof(addr_t) * count);
	if (bits == NULL)
		return B_NO_MEMORY;
	fBits = (addr_t*)bits;

	if (fElementsCount < count)
		memset(&fBits[fElementsCount], 0, sizeof(addr_t) * (count - fElementsCount));

	fSize = bitCount;
	fElementsCount = count;
	return B_OK;
}


/**
 * @brief Shift all bits in place by @a bitCount positions.
 * @param bitCount Positive shifts left (toward higher indices); negative shifts right.
 */
void
Bitmap::Shift(ssize_t bitCount)
{
	return bitmap_shift<addr_t>(fBits, fSize, bitCount);
}


/**
 * @brief Set @a count consecutive bits starting at @a index.
 * @param index First bit to set.
 * @param count Number of bits.
 */
void
Bitmap::SetRange(size_t index, size_t count)
{
	// TODO: optimize
	for (; count > 0; count--)
		Set(index++);
}


/**
 * @brief Clear @a count consecutive bits starting at @a index.
 * @param index First bit to clear.
 * @param count Number of bits.
 */
void
Bitmap::ClearRange(size_t index, size_t count)
{
	// TODO: optimize
	for (; count > 0; count--)
		Clear(index++);
}


/**
 * @brief Find the lowest clear bit at or above @a fromIndex.
 * @param fromIndex Starting bit position for the scan.
 * @return Index of the first clear bit, or -1 if none exists.
 */
ssize_t
Bitmap::GetLowestClear(size_t fromIndex) const
{
	// TODO: optimize

	for (size_t i = fromIndex; i < fSize; i++) {
		if (!Get(i))
			return i;
	}
	return -1;
}


/**
 * @brief Find the lowest run of @a count consecutive clear bits.
 * @param count     Minimum run length required.
 * @param fromIndex Starting bit position for the scan.
 * @return Index of the first bit of the matching run, or -1 if none exists.
 */
ssize_t
Bitmap::GetLowestContiguousClear(size_t count, size_t fromIndex) const
{
	// TODO: optimize

	// nothing to find
	if (count == 0)
		return fromIndex;

	for (;;) {
		ssize_t index = GetLowestClear(fromIndex);
		if (index < 0)
			return index;

		// overflow check
		if ((size_t)index + count - 1 < (size_t)index)
			return -1;

		size_t curCount = 1;
		while (curCount < count && Get(index + curCount))
			curCount++;

		if (curCount == count)
			return index;

		fromIndex = index + curCount;
	}
}


/**
 * @brief Return the index of the highest set bit.
 *
 * Uses log2() on the top non-zero word, handling 32-bit and 64-bit addr_t
 * without portability hacks on the rest of the arithmetic.
 *
 * @return Index of the highest set bit, or -1 if the bitmap is empty.
 */
ssize_t
Bitmap::GetHighestSet() const
{
	size_t i = fElementsCount - 1;
	while (i >= 0 && fBits[i] == 0)
		i--;

	if (i < 0)
		return -1;

	STATIC_ASSERT(sizeof(addr_t) == sizeof(uint64)
		|| sizeof(addr_t) == sizeof(uint32));
	if (sizeof(addr_t) == sizeof(uint32))
		return log2(fBits[i]) + i * kBitsPerElement;

	uint32 v = (uint64)fBits[i] >> 32;
	if (v != 0)
		return log2(v) + sizeof(uint32) * 8 + i * kBitsPerElement;
	return log2(fBits[i]) + i * kBitsPerElement;
}


} // namespace BKernel
