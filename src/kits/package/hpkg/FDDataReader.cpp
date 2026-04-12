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
 * @file FDDataReader.cpp
 * @brief BDataReader implementation that reads from a POSIX file descriptor.
 *
 * BFDDataReader wraps a raw file descriptor and implements ReadData() via
 * pread(), enabling random-access reads without disturbing the file's current
 * seek position.  It is used by the HPKG layer to access package files that
 * have been opened externally and passed in as an fd.
 *
 * @see BAttributeDataReader, BBufferDataReader
 */


#include <package/hpkg/DataReader.h>

#include <errno.h>
#include <unistd.h>


namespace BPackageKit {

namespace BHPKG {


/**
 * @brief Construct a reader wrapping the given file descriptor.
 *
 * @param fd Open, readable file descriptor to read from.  Ownership is
 *           not transferred; the caller is responsible for closing it.
 */
BFDDataReader::BFDDataReader(int fd)
	:
	fFD(fd)
{
}


/**
 * @brief Replace the file descriptor used for subsequent reads.
 *
 * @param fd New file descriptor to use.  The previously held descriptor
 *           is not closed.
 */
void
BFDDataReader::SetFD(int fd)
{
	fFD = fd;
}


/**
 * @brief Read a range of bytes from the file descriptor using pread().
 *
 * Uses pread() for position-independent I/O so that the file's current
 * offset is not affected.
 *
 * @param offset Byte position within the file to begin reading.
 * @param buffer Destination buffer for the read bytes.
 * @param size   Number of bytes to read.
 * @return B_OK on success, an errno value on I/O error, or B_ERROR if
 *         fewer bytes than requested were read.
 */
status_t
BFDDataReader::ReadData(off_t offset, void* buffer, size_t size)
{
	ssize_t bytesRead = pread(fFD, buffer, size, offset);
	if (bytesRead < 0)
		return errno;
	return (size_t)bytesRead == size ? B_OK : B_ERROR;
}


}	// namespace BHPKG

}	// namespace BPackageKit
