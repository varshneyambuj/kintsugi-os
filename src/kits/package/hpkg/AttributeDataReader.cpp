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
 *   Copyright 2009-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file AttributeDataReader.cpp
 * @brief Data reader that reads package data from a file system attribute.
 *
 * BAttributeDataReader implements the BDataReader interface by reading
 * raw bytes from a named extended attribute on an open file descriptor.
 * It is used by the HPKG layer when package data is stored as an FS
 * attribute rather than inside the heap section of the package file.
 *
 * @see BDataReader, BFDDataReader
 */


#include <package/hpkg/DataReader.h>

#include <errno.h>

#include <fs_attr.h>


namespace BPackageKit {

namespace BHPKG {


/**
 * @brief Construct a reader for a named extended attribute.
 *
 * @param fd        Open file descriptor whose extended attribute will be read.
 * @param attribute Name of the extended attribute to read from.
 * @param type      BeOS attribute type code (e.g. B_RAW_TYPE) associated with
 *                  the attribute.
 */
BAttributeDataReader::BAttributeDataReader(int fd, const char* attribute,
	uint32 type)
	:
	fFD(fd),
	fType(type),
	fAttribute(attribute)
{
}


/**
 * @brief Read a range of bytes from the named attribute.
 *
 * Calls fs_read_attr() to copy exactly \a size bytes starting at \a offset
 * from the stored attribute into \a buffer.
 *
 * @param offset  Byte offset within the attribute data to begin reading.
 * @param buffer  Destination buffer that receives the attribute bytes.
 * @param size    Number of bytes to read.
 * @return B_OK on success, errno value on an I/O error, or B_ERROR if
 *         fewer bytes than requested were available.
 */
status_t
BAttributeDataReader::ReadData(off_t offset, void* buffer, size_t size)
{
	ssize_t bytesRead = fs_read_attr(fFD, fAttribute, fType, offset, buffer,
		size);
	if (bytesRead < 0)
		return errno;
	return (size_t)bytesRead == size ? B_OK : B_ERROR;
}


}	// namespace BHPKG

}	// namespace BPackageKit
