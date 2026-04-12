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
 * @file DataReader.cpp
 * @brief Concrete BDataReader implementations for in-memory and file-backed sources.
 *
 * Provides the virtual destructors that anchor vtables for the abstract reader
 * hierarchy, as well as BBufferDataReader — a reader that serves data directly
 * from a fixed in-memory buffer with bounds checking.  On kernel builds with
 * user-space safety required, user_memcpy() is used when the destination
 * address lives in user space.
 *
 * @see BFDDataReader, BAttributeDataReader, BAbstractBufferedDataReader
 */


#include <package/hpkg/DataReader.h>

#include <DataIO.h>

#include <string.h>

#if defined(_KERNEL_MODE) && !defined(_BOOT_MODE)
// for user_memcpy() and IS_USER_ADDRESS()
#include <KernelExport.h>

#include <kernel.h>
#endif


namespace BPackageKit {

namespace BHPKG {


// #pragma mark - BDataReader


/**
 * @brief Virtual destructor for BDataReader.
 *
 * Anchors the vtable so that derived readers are properly destroyed
 * when deleted through a base-class pointer.
 */
BDataReader::~BDataReader()
{
}


// #pragma mark - BAbstractBufferedDataReader


/**
 * @brief Virtual destructor for BAbstractBufferedDataReader.
 *
 * Ensures correct polymorphic destruction of buffered reader subclasses.
 */
BAbstractBufferedDataReader::~BAbstractBufferedDataReader()
{
}


/**
 * @brief Read data into a flat buffer by delegating to ReadDataToOutput().
 *
 * Wraps \a buffer in a BMemoryIO and calls the virtual ReadDataToOutput()
 * method, allowing subclasses to provide a single output-based implementation
 * while still satisfying the simpler ReadData() interface.
 *
 * @param offset Byte offset within the data source to begin reading.
 * @param buffer Destination buffer for the read bytes.
 * @param size   Number of bytes to read.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BAbstractBufferedDataReader::ReadData(off_t offset, void* buffer, size_t size)
{
	BMemoryIO output(buffer, size);
	return ReadDataToOutput(offset, size, &output);
}


// #pragma mark - BBufferDataReader


/**
 * @brief Construct a reader backed by an existing in-memory buffer.
 *
 * @param data Pointer to the start of the data buffer; the caller retains
 *             ownership and must ensure the buffer remains valid for the
 *             lifetime of this reader.
 * @param size Total number of bytes available in \a data.
 */
BBufferDataReader::BBufferDataReader(const void* data, size_t size)
	:
	fData(data),
	fSize(size)
{
}


/**
 * @brief Copy a range of bytes from the in-memory buffer.
 *
 * Performs bounds checking and, on kernel builds, uses user_memcpy() when
 * the destination is a user-space address.
 *
 * @param offset Byte offset within the buffer to begin reading.
 * @param buffer Destination for the copied bytes.
 * @param size   Number of bytes to copy.
 * @return B_OK on success, B_BAD_VALUE for a negative offset, B_ERROR if
 *         the requested range exceeds the buffer, or B_BAD_ADDRESS if a
 *         kernel-mode user_memcpy() fails.
 */
status_t
BBufferDataReader::ReadData(off_t offset, void* buffer, size_t size)
{
	if (size == 0)
		return B_OK;

	if (offset < 0)
		return B_BAD_VALUE;

	if (size > fSize || offset > (off_t)fSize - (off_t)size)
		return B_ERROR;

#if defined(_KERNEL_MODE) && !defined(_BOOT_MODE)
	if (IS_USER_ADDRESS(buffer)) {
		if (user_memcpy(buffer, (const uint8*)fData + offset, size) != B_OK)
			return B_BAD_ADDRESS;
	} else
#endif
	memcpy(buffer, (const uint8*)fData + offset, size);
	return B_OK;
}


/**
 * @brief Write a range of bytes from the in-memory buffer to a BDataIO.
 *
 * Validates the requested range and writes the data via
 * BDataIO::WriteExactly().
 *
 * @param offset Byte offset within the buffer to begin reading.
 * @param size   Number of bytes to write to \a output.
 * @param output Destination BDataIO that receives the data.
 * @return B_OK on success, B_BAD_VALUE for a negative offset, B_ERROR if
 *         the range exceeds the buffer, or any error returned by WriteExactly().
 */
status_t
BBufferDataReader::ReadDataToOutput(off_t offset, size_t size, BDataIO* output)
{
	if (size == 0)
		return B_OK;

	if (offset < 0)
		return B_BAD_VALUE;

	if (size > fSize || offset > (off_t)fSize - (off_t)size)
		return B_ERROR;

	return output->WriteExactly((const uint8*)fData + offset, size);
}


}	// namespace BHPKG

}	// namespace BPackageKit
