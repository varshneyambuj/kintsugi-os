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
 * @file RepositoryWriter.cpp
 * @brief Public facade for creating HPKG repository index files.
 *
 * BRepositoryWriter aggregates package metadata from multiple HPKG packages
 * into a single repository index file. The class delegates all file I/O and
 * attribute encoding to the internal RepositoryWriterImpl; this wrapper owns
 * the impl object and guards every public call against a NULL impl.
 *
 * @see BRepositoryReader, BPackageWriter
 */


#include <package/hpkg/RepositoryWriter.h>

#include <new>

#include <package/hpkg/RepositoryWriterImpl.h>
#include <package/RepositoryInfo.h>


namespace BPackageKit {

namespace BHPKG {


/**
 * @brief Constructs a BRepositoryWriter and allocates its implementation.
 *
 * @param listener        Callback object for progress and error notifications.
 * @param repositoryInfo  Metadata describing the repository (name, URL, etc.).
 */
BRepositoryWriter::BRepositoryWriter(BRepositoryWriterListener* listener,
	BRepositoryInfo* repositoryInfo)
	:
	fImpl(new (std::nothrow) RepositoryWriterImpl(listener, repositoryInfo))
{
}


/**
 * @brief Destroys the BRepositoryWriter and frees the implementation object.
 */
BRepositoryWriter::~BRepositoryWriter()
{
	delete fImpl;
}


/**
 * @brief Initialises the writer to write to a file at the given path.
 *
 * Creates or truncates the file at @a fileName and writes the HPKG header.
 *
 * @param fileName Destination path for the repository index file.
 * @return B_OK on success, B_NO_MEMORY if the impl was not allocated, or
 *         another error code on failure.
 */
status_t
BRepositoryWriter::Init(const char* fileName)
{
	if (fImpl == NULL)
		return B_NO_MEMORY;

	return fImpl->Init(fileName);
}


/**
 * @brief Adds a package from an on-disk HPKG file to the repository index.
 *
 * Reads the package metadata from the file referenced by @a packageEntry and
 * accumulates it in the in-memory attribute tree.
 *
 * @param packageEntry BEntry referring to the HPKG file to include.
 * @return B_OK on success, B_NO_INIT if not initialised, or an error code.
 */
status_t
BRepositoryWriter::AddPackage(const BEntry& packageEntry)
{
	if (fImpl == NULL)
		return B_NO_INIT;

	return fImpl->AddPackage(packageEntry);
}


/**
 * @brief Adds a package described by an in-memory BPackageInfo to the index.
 *
 * This variant is useful when the package metadata is already parsed and no
 * HPKG file needs to be opened.
 *
 * @param packageInfo Fully populated package information structure.
 * @return B_OK on success, B_NO_INIT if not initialised, or an error code.
 */
status_t
BRepositoryWriter::AddPackageInfo(const BPackageInfo& packageInfo)
{
	if (fImpl == NULL)
		return B_NO_INIT;

	return fImpl->AddPackageInfo(packageInfo);
}


/**
 * @brief Finalises the repository index, writing all attributes and the heap.
 *
 * Must be called after all packages have been added. The output file is not
 * a valid repository index until this method returns B_OK.
 *
 * @return B_OK on success, B_NO_INIT if not initialised, or an error code.
 */
status_t
BRepositoryWriter::Finish()
{
	if (fImpl == NULL)
		return B_NO_INIT;

	return fImpl->Finish();
}


}	// namespace BHPKG

}	// namespace BPackageKit
