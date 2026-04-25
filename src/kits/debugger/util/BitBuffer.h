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
 * MIT License. Copyright 2009, Haiku.
 * Original authors: Ingo Weinhold.
 */

/** @file BitBuffer.h
    @brief Append-only bit-aligned buffer used by the DWARF reader. */

#ifndef BIT_BUFFER_H
#define BIT_BUFFER_H


#include <SupportDefs.h>

#include <Array.h>


/** @brief Bit-aligned buffer that accumulates arbitrary bit chunks into bytes. */
class BitBuffer {
public:
								BitBuffer();
								~BitBuffer();

			bool				AddBytes(const void* data, size_t size);
			bool				AddBits(const void* data, uint64 bitSize,
									uint32 bitOffset = 0);
			bool				AddZeroBits(uint64 bitSize);

			/** @brief Pointer to the start of the accumulated byte buffer. */
			uint8*				Bytes() const	{ return fBytes.Elements(); }
			/** @brief Number of bytes currently in the buffer (including any partial trailing byte). */
			size_t				Size() const	{ return fBytes.Size(); }
			/** @brief Total number of bits stored, accounting for the trailing partial byte. */
			size_t				BitSize() const
									{ return Size() * 8 - fMissingBits; }

private:
			struct BitReader;

private:
			Array<uint8>		fBytes;
			uint8				fMissingBits;
};


#endif	// BIT_BUFFER_H
