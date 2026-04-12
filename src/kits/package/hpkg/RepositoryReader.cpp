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
 *   Copyright 2011, Oliver Tappe <zooey@hirschkaefer.de>
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file RepositoryReader.cpp
 * @brief Public facade for reading HPKG repository index files.
 *
 * BRepositoryReader wraps the internal RepositoryReaderImpl and provides the
 * public API for opening a repository index file and streaming its package
 * attribute tree to a caller-supplied BRepositoryContentHandler. The impl
 * is heap-allocated at construction time; every public method guards against
 * a NULL impl before delegating.
 *
 * @see BRepositoryWriter, BRepositoryContentHandler
 */


#include <package/hpkg/RepositoryReader.h>

#include <new>

#include <package/hpkg/ErrorOutput.h>
#include <package/hpkg/RepositoryContentHandler.h>
#include <package/hpkg/RepositoryReaderImpl.h>


namespace BPackageKit {

namespace BHPKG {


/**
 * @brief Constructs a BRepositoryReader and allocates its implementation.
 *
 * @param errorOutput Callback object used for error message output.
 */
BRepositoryReader::BRepositoryReader(BErrorOutput* errorOutput)
	:
	fImpl(new (std::nothrow) RepositoryReaderImpl(errorOutput))
{
}


/**
 * @brief Destroys the BRepositoryReader and frees the implementation object.
 */
BRepositoryReader::~BRepositoryReader()
{
	delete fImpl;
}


/**
 * @brief Opens and validates the repository index file at the given path.
 *
 * Reads the HPKG header and verifies the magic and format version before
 * any content parsing begins.
 *
 * @param fileName Path to the repository index HPKG file to open.
 * @return B_OK on success, B_NO_INIT if the impl was not allocated, or
 *         another error code on failure.
 */
status_t
BRepositoryReader::Init(const char* fileName)
{
	if (fImpl == NULL)
		return B_NO_INIT;

	return fImpl->Init(fileName);
}


/**
 * @brief Parses the repository content and delivers it to a content handler.
 *
 * Walks the repository attribute section of the previously opened file and
 * invokes @a contentHandler callbacks for each package entry encountered.
 *
 * @param contentHandler Receives the parsed repository package entries.
 * @return B_OK on success, B_NO_INIT if the reader was not initialised, or
 *         another error code on failure.
 */
status_t
BRepositoryReader::ParseContent(BRepositoryContentHandler* contentHandler)
{
	if (fImpl == NULL)
		return B_NO_INIT;

	return fImpl->ParseContent(contentHandler);
}


}	// namespace BHPKG

}	// namespace BPackageKit
