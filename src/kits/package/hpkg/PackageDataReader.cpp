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
 * @file PackageDataReader.cpp
 * @brief Factory and reader implementations for BPackageData payloads.
 *
 * Provides two internal reader classes — PackageDataHeapReader for data
 * stored in the HPKG heap, and PackageDataInlineReader for small payloads
 * encoded directly in the TOC — and the public BPackageDataReaderFactory
 * that selects the correct implementation based on a BPackageData descriptor.
 *
 * @see BPackageData, BAbstractBufferedDataReader, BPackageDataReaderFactory
 */


#include <package/hpkg/PackageDataReader.h>

#include <string.h>

#include <algorithm>
#include <new>

#include <package/hpkg/HPKGDefsPrivate.h>
#include <package/hpkg/PackageData.h>


namespace BPackageKit {

namespace BHPKG {


using namespace BPrivate;


// #pragma mark - PackageDataHeapReader


/**
 * @brief Internal reader that serves data from a region of the HPKG heap.
 *
 * Translates local (entry-relative) offsets into absolute heap offsets and
 * delegates reads to the underlying BAbstractBufferedDataReader.
 */
class PackageDataHeapReader : public BAbstractBufferedDataReader {
public:
	/**
	 * @brief Construct a heap reader for the given BPackageData descriptor.
	 *
	 * @param dataReader Reader for the HPKG heap that backs this entry's data.
	 * @param data       Descriptor providing the heap offset and uncompressed size.
	 */
	PackageDataHeapReader(BAbstractBufferedDataReader* dataReader,
		const BPackageData& data)
		:
		fDataReader(dataReader),
		fOffset(data.Offset()),
		fSize(data.Size())
	{
	}

	/**
	 * @brief Read a range of bytes from the heap region into a flat buffer.
	 *
	 * @param offset Byte offset within this entry's data region.
	 * @param buffer Destination buffer.
	 * @param size   Number of bytes to read.
	 * @return B_OK on success, B_BAD_VALUE if the range is out of bounds.
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
	 * @brief Write a range of bytes from the heap region to a BDataIO.
	 *
	 * @param offset Byte offset within this entry's data region.
	 * @param size   Number of bytes to write.
	 * @param output Destination BDataIO.
	 * @return B_OK on success, B_BAD_VALUE if the range is out of bounds.
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

		return fDataReader->ReadDataToOutput(fOffset + offset, size, output);
	}

private:
	BAbstractBufferedDataReader*	fDataReader;
	uint64							fOffset;
	uint64							fSize;
};


// #pragma mark - PackageDataInlineReader


/**
 * @brief Internal reader that serves data stored inline in the TOC.
 *
 * Wraps BBufferDataReader over the inline byte array embedded in the
 * BPackageData descriptor.  A copy of the descriptor is kept to ensure the
 * inline buffer remains valid for the lifetime of this reader.
 */
class PackageDataInlineReader : public BBufferDataReader {
public:
	/**
	 * @brief Construct an inline reader for the given BPackageData descriptor.
	 *
	 * @param data Descriptor containing the inline bytes and their size.
	 */
	PackageDataInlineReader(const BPackageData& data)
		:
		BBufferDataReader(data.InlineData(), data.Size()),
		fData(data)
	{
	}

private:
	BPackageData	fData;
};


// #pragma mark - BPackageDataReaderFactory


/**
 * @brief Construct the factory; no resources are allocated at this point.
 */
BPackageDataReaderFactory::BPackageDataReaderFactory()
{
}


/**
 * @brief Create the appropriate data reader for a BPackageData descriptor.
 *
 * Selects PackageDataInlineReader when the data is encoded inline in the TOC,
 * or PackageDataHeapReader when it refers to a region in the package heap.
 *
 * @param dataReader  Heap reader used for non-inline payloads; may be NULL
 *                    when \a data is inline.
 * @param data        Descriptor that specifies the data location and size.
 * @param _reader     Output parameter that receives the newly created reader
 *                    on success; the caller takes ownership.
 * @return B_OK on success, B_NO_MEMORY if allocation fails.
 */
status_t
BPackageDataReaderFactory::CreatePackageDataReader(
	BAbstractBufferedDataReader* dataReader, const BPackageData& data,
	BAbstractBufferedDataReader*& _reader)
{
	BAbstractBufferedDataReader* reader;
	if (data.IsEncodedInline())
		reader = new(std::nothrow) PackageDataInlineReader(data);
	else
		reader = new(std::nothrow) PackageDataHeapReader(dataReader, data);

	if (reader == NULL)
		return B_NO_MEMORY;

	_reader = reader;
	return B_OK;
}


}	// namespace BHPKG

}	// namespace BPackageKit
