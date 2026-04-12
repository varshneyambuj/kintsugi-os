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
 *   Copyright 2009-2013, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file PackageReader.cpp
 * @brief Public API for reading HPKG package files.
 *
 * BPackageReader is the primary entry point for consumers of the HPKG Kit.
 * It wraps the internal PackageReaderImpl, providing three Init() overloads
 * (file path, file descriptor, BPositionIO) and two ParseContent() overloads
 * for high-level and low-level content handlers.  All actual parsing logic
 * lives in PackageReaderImpl.
 *
 * @see PackageReaderImpl, BPackageContentHandler, BLowLevelPackageContentHandler
 */


#include <package/hpkg/PackageReader.h>

#include <new>

#include <package/hpkg/ErrorOutput.h>

#include <package/hpkg/PackageFileHeapReader.h>
#include <package/hpkg/PackageReaderImpl.h>


namespace BPackageKit {

namespace BHPKG {


/**
 * @brief Construct a package reader with the given error output channel.
 *
 * Allocates the internal PackageReaderImpl.  If allocation fails, all
 * subsequent operations will return B_NO_INIT.
 *
 * @param errorOutput Diagnostic output channel used during parsing;
 *                    must remain valid for the lifetime of this reader.
 */
BPackageReader::BPackageReader(BErrorOutput* errorOutput)
	:
	fImpl(new (std::nothrow) PackageReaderImpl(errorOutput))
{
}


/**
 * @brief Destroy the package reader and release the internal implementation.
 */
BPackageReader::~BPackageReader()
{
	delete fImpl;
}


/**
 * @brief Open and initialise the reader from a package file path.
 *
 * @param fileName Path to the HPKG file to open.
 * @param flags    Reader flags (reserved, pass 0).
 * @return B_OK on success, B_NO_INIT if the implementation was not allocated,
 *         or any error from PackageReaderImpl::Init().
 */
status_t
BPackageReader::Init(const char* fileName, uint32 flags)
{
	if (fImpl == NULL)
		return B_NO_INIT;

	return fImpl->Init(fileName, flags);
}


/**
 * @brief Initialise the reader from an open file descriptor.
 *
 * @param fd      Open file descriptor for the HPKG file.
 * @param keepFD  If true the reader takes ownership of \a fd and will close
 *                it on destruction; if false the caller retains ownership.
 * @param flags   Reader flags (reserved, pass 0).
 * @return B_OK on success, B_NO_INIT if the implementation was not allocated,
 *         or any error from PackageReaderImpl::Init().
 */
status_t
BPackageReader::Init(int fd, bool keepFD, uint32 flags)
{
	if (fImpl == NULL)
		return B_NO_INIT;

	return fImpl->Init(fd, keepFD, flags);
}


/**
 * @brief Initialise the reader from an existing BPositionIO object.
 *
 * @param file      Positioned I/O object representing the package file.
 * @param keepFile  If true the reader takes ownership of \a file; if false
 *                  the caller retains ownership.
 * @param flags     Reader flags (reserved, pass 0).
 * @return B_OK on success, B_NO_INIT if the implementation was not allocated,
 *         or any error from PackageReaderImpl::Init().
 */
status_t
BPackageReader::Init(BPositionIO* file, bool keepFile, uint32 flags)
{
	if (fImpl == NULL)
		return B_NO_INIT;

	return fImpl->Init(file, keepFile, flags);
}


/**
 * @brief Parse the package content and deliver it to the high-level handler.
 *
 * Processes both the package attributes section and the TOC, invoking the
 * BPackageContentHandler callbacks for each entry and attribute encountered.
 *
 * @param contentHandler High-level handler to receive parsed content.
 * @return B_OK on success, B_NO_INIT if the implementation is absent,
 *         or any parse error from PackageReaderImpl::ParseContent().
 */
status_t
BPackageReader::ParseContent(BPackageContentHandler* contentHandler)
{
	if (fImpl == NULL)
		return B_NO_INIT;

	return fImpl->ParseContent(contentHandler);
}


/**
 * @brief Parse the package content and deliver raw attributes to the low-level handler.
 *
 * Processes both the package attributes section and the TOC at the raw
 * attribute level, invoking BLowLevelPackageContentHandler callbacks for
 * every attribute value encountered.
 *
 * @param contentHandler Low-level handler to receive raw attribute data.
 * @return B_OK on success, B_NO_INIT if the implementation is absent,
 *         or any parse error from PackageReaderImpl::ParseContent().
 */
status_t
BPackageReader::ParseContent(BLowLevelPackageContentHandler* contentHandler)
{
	if (fImpl == NULL)
		return B_NO_INIT;

	return fImpl->ParseContent(contentHandler);
}


/**
 * @brief Return the BPositionIO object used to access the package file.
 *
 * @return Pointer to the package file I/O object, or NULL if the
 *         implementation was not allocated or the reader has not been
 *         initialised.
 */
BPositionIO*
BPackageReader::PackageFile() const
{
	if (fImpl == NULL)
		return NULL;

	return fImpl->PackageFile();
}


/**
 * @brief Return the heap reader for direct access to the package's data heap.
 *
 * @return Pointer to the BAbstractBufferedDataReader for the package heap, or
 *         NULL if the implementation is absent or not yet initialised.
 */
BAbstractBufferedDataReader*
BPackageReader::HeapReader() const
{
	return fImpl != NULL ? fImpl->HeapReader() : NULL;
}


}	// namespace BHPKG

}	// namespace BPackageKit
