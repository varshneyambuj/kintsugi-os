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
 * @file PackageDataV1.cpp
 * @brief Descriptor for a data region inside a v1 HPKG package.
 *
 * BPackageData (v1) records whether file content is stored inline in the
 * attribute tree or as a heap-allocated compressed block, together with the
 * associated size, offset, chunk size, and compression algorithm. The class
 * is used by PackageDataReaderV1 to obtain the information needed to
 * decompress and read a file's data.
 *
 * @see PackageDataReaderV1, BPackageEntry (V1)
 */


#include <package/hpkg/v1/PackageData.h>

#include <string.h>

#include <package/hpkg/v1/HPKGDefsPrivate.h>


namespace BPackageKit {

namespace BHPKG {

namespace V1 {


using namespace BPrivate;


/**
 * @brief Constructs a BPackageData with no data (zero sizes, no compression).
 *
 * The initial state has no data and the encoding is marked as inline.
 */
BPackageData::BPackageData()
	:
	fCompressedSize(0),
	fUncompressedSize(0),
	fChunkSize(0),
	fCompression(B_HPKG_COMPRESSION_NONE),
	fEncodedInline(true)
{
}


/**
 * @brief Sets the data descriptor to reference a heap-allocated block.
 *
 * The compressed and uncompressed sizes are both set to @a size initially;
 * callers that later learn the uncompressed size should call
 * SetUncompressedSize(). The encoding is switched to non-inline (heap) mode.
 *
 * @param size   Size in bytes of the data as stored in the heap.
 * @param offset Byte offset of the data block within the HPKG heap.
 */
void
BPackageData::SetData(uint64 size, uint64 offset)
{
	fUncompressedSize = fCompressedSize = size;
	fOffset = offset;
	fEncodedInline = false;
}


/**
 * @brief Sets the data descriptor to reference an inline data buffer.
 *
 * Copies up to @a size bytes from @a data into the internal inline buffer.
 * Both compressed and uncompressed sizes are set to @a size, and the encoding
 * is switched to inline mode.
 *
 * @param size Number of bytes of inline data; must fit in the inline buffer.
 * @param data Pointer to the inline data to copy.
 */
void
BPackageData::SetData(uint8 size, const void* data)
{
	fUncompressedSize = fCompressedSize = size;
	if (size > 0)
		memcpy(fInlineData, data, size);
	fEncodedInline = true;
}


}	// namespace V1

}	// namespace BHPKG

}	// namespace BPackageKit
