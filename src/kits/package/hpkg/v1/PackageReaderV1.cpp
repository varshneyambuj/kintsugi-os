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
 * @file PackageReaderV1.cpp
 * @brief Public facade for reading HPKG v1 package files.
 *
 * BPackageReader (v1) wraps the internal PackageReaderImpl and exposes the
 * public API for opening a v1 HPKG package and streaming its file-system tree
 * and package metadata to either a high-level BPackageContentHandler or a
 * low-level BLowLevelPackageContentHandler. All operations guard against a
 * NULL impl (allocation failure at construction time).
 *
 * @see BPackageWriter, BRepositoryReader, PackageReaderImplV1
 */


#include <package/hpkg/v1/PackageReader.h>

#include <new>

#include <package/hpkg/ErrorOutput.h>
#include <package/hpkg/v1/PackageReaderImpl.h>


namespace BPackageKit {

namespace BHPKG {

namespace V1 {


/**
 * @brief Constructs a BPackageReader and allocates its implementation.
 *
 * @param errorOutput Callback object for error message output.
 */
BPackageReader::BPackageReader(BErrorOutput* errorOutput)
	:
	fImpl(new (std::nothrow) PackageReaderImpl(errorOutput))
{
}


/**
 * @brief Destroys the BPackageReader and frees the implementation object.
 */
BPackageReader::~BPackageReader()
{
	delete fImpl;
}


/**
 * @brief Opens and validates the v1 package file at the given path.
 *
 * Reads the HPKG header, verifies magic and version, and prepares the heap
 * reader for subsequent ParseContent() calls.
 *
 * @param fileName Path to the v1 HPKG file.
 * @return B_OK on success, B_NO_INIT if the impl was not allocated, or
 *         another error code on failure.
 */
status_t
BPackageReader::Init(const char* fileName)
{
	if (fImpl == NULL)
		return B_NO_INIT;

	return fImpl->Init(fileName);
}


/**
 * @brief Opens and validates the v1 package file via an already-open file descriptor.
 *
 * @param fd     Open file descriptor referencing the v1 HPKG file.
 * @param keepFD If true the reader takes ownership and will close the descriptor.
 * @return B_OK on success, B_NO_INIT if the impl was not allocated, or
 *         another error code on failure.
 */
status_t
BPackageReader::Init(int fd, bool keepFD)
{
	if (fImpl == NULL)
		return B_NO_INIT;

	return fImpl->Init(fd, keepFD);
}


/**
 * @brief Parses the package content and delivers it to a high-level handler.
 *
 * Walks the TOC and package attributes sections and invokes the appropriate
 * methods on @a contentHandler for each entry and package attribute encountered.
 *
 * @param contentHandler Receives parsed entries and package attributes.
 * @return B_OK on success, B_NO_INIT if not initialised, or an error code.
 */
status_t
BPackageReader::ParseContent(BPackageContentHandler* contentHandler)
{
	if (fImpl == NULL)
		return B_NO_INIT;

	return fImpl->ParseContent(contentHandler);
}


/**
 * @brief Parses the package content and delivers it to a low-level handler.
 *
 * Provides raw attribute-level access; useful for tools that need to inspect
 * all attributes without the typed abstraction layer.
 *
 * @param contentHandler Receives raw attribute IDs, values, and tokens.
 * @return B_OK on success, B_NO_INIT if not initialised, or an error code.
 */
status_t
BPackageReader::ParseContent(BLowLevelPackageContentHandler* contentHandler)
{
	if (fImpl == NULL)
		return B_NO_INIT;

	return fImpl->ParseContent(contentHandler);
}


/**
 * @brief Returns the file descriptor of the open package file.
 *
 * @return The file descriptor, or -1 if the impl is NULL.
 */
int
BPackageReader::PackageFileFD()
{
	if (fImpl == NULL)
		return -1;

	return fImpl->PackageFileFD();
}


}	// namespace V1

}	// namespace BHPKG

}	// namespace BPackageKit
