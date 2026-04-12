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
 * @file PackageData.cpp
 * @brief Descriptor for a single data payload within an HPKG package entry.
 *
 * BPackageData holds either an inline byte array (for small payloads encoded
 * directly in the TOC) or a heap reference (offset + size) pointing into the
 * package file's compressed heap section.  The IsEncodedInline() predicate
 * determines which representation is active.
 *
 * @see BPackageEntry, BPackageDataReaderFactory
 */


#include <package/hpkg/PackageData.h>

#include <string.h>

#include <package/hpkg/HPKGDefsPrivate.h>


namespace BPackageKit {

namespace BHPKG {


using namespace BPrivate;


/**
 * @brief Construct a zero-size inline-encoded BPackageData.
 *
 * Initialises the descriptor to represent an empty inline payload so that
 * it is safe to call before SetData() has been called.
 */
BPackageData::BPackageData()
	:
	fSize(0),
	fEncodedInline(true)
{
}


/**
 * @brief Configure this descriptor to reference a heap region.
 *
 * Stores a (size, offset) pair that locates the data within the package
 * file's heap section.  After this call IsEncodedInline() returns false.
 *
 * @param size   Uncompressed size of the data in bytes.
 * @param offset Byte offset of the data within the uncompressed heap.
 */
void
BPackageData::SetData(uint64 size, uint64 offset)
{
	fSize = size;
	fOffset = offset;
	fEncodedInline = false;
}


/**
 * @brief Configure this descriptor to hold an inline byte payload.
 *
 * Copies up to \a size bytes from \a data into the internal inline buffer.
 * After this call IsEncodedInline() returns true.
 *
 * @param size Number of bytes to copy from \a data; must not exceed
 *             B_HPKG_MAX_INLINE_DATA_SIZE.
 * @param data Pointer to the source bytes to copy.
 */
void
BPackageData::SetData(uint8 size, const void* data)
{
	fSize = size;
	if (size > 0)
		memcpy(fInlineData, data, size);
	fEncodedInline = true;
}


}	// namespace BHPKG

}	// namespace BPackageKit
