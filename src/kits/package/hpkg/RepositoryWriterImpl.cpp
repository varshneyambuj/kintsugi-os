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
 * @file RepositoryWriterImpl.cpp
 * @brief Internal implementation of the HPKG repository index writer.
 *
 * RepositoryWriterImpl accumulates package metadata from multiple HPKG files
 * or BPackageInfo objects and serialises them into a single repository index
 * file. It extracts licence text from packages that require approval, builds
 * the repository-level attribute tree (including checksums), and delegates
 * compression and attribute serialisation to WriterImplBase.
 *
 * @see BRepositoryWriter, WriterImplBase, RepositoryReaderImpl
 */


#include <package/hpkg/RepositoryWriterImpl.h>

#include <algorithm>
#include <new>

#include <ByteOrder.h>
#include <Message.h>
#include <Path.h>

#include <AutoDeleter.h>
#include <HashSet.h>

#include <package/hpkg/BlockBufferPoolNoLock.h>
#include <package/hpkg/HPKGDefsPrivate.h>
#include <package/hpkg/PackageDataReader.h>
#include <package/hpkg/PackageEntry.h>
#include <package/hpkg/PackageFileHeapWriter.h>
#include <package/hpkg/PackageInfoAttributeValue.h>
#include <package/hpkg/PackageReader.h>
#include <package/ChecksumAccessors.h>
#include <package/PackageInfoContentHandler.h>
#include <package/RepositoryInfo.h>

#include "HashableString.h"


namespace BPackageKit {

namespace BHPKG {

namespace BPrivate {


using BPackageKit::BPrivate::GeneralFileChecksumAccessor;
using BPackageKit::BPrivate::HashableString;


namespace {


// #pragma mark - PackageEntryDataFetcher


/**
 * @brief Helper that pulls the raw bytes of a single package entry's data
 *        stream into a BString.
 *
 * Used by the repository writer to extract licence text from packages that
 * require licence approval, so the text can be embedded in the repository
 * info.
 */
struct PackageEntryDataFetcher {
	/**
	 * @brief Bind the fetcher to an error sink and a specific package data
	 *        descriptor.
	 *
	 * @param errorOutput Sink that receives diagnostic messages on failure.
	 * @param packageData The package entry data record to read.
	 */
	PackageEntryDataFetcher(BErrorOutput* errorOutput,
		BPackageData& packageData)
		:
		fErrorOutput(errorOutput),
		fPackageData(packageData)
	{
	}

	/**
	 * @brief Read the entire data stream described by @c fPackageData into a
	 *        BString buffer.
	 *
	 * Allocates a PackageDataReader on top of @a heapReader, locks a buffer
	 * of the data's full size in @a _contents, and reads everything in.
	 *
	 * @param heapReader Reader providing access to the package's compressed
	 *                   heap.
	 * @param _contents  Output BString that receives the data; contents are
	 *                   undefined on error.
	 * @return B_OK on success, B_NO_MEMORY if the buffer cannot be locked,
	 *         or an error from the reader factory or the read itself.
	 */
	status_t ReadIntoString(BAbstractBufferedDataReader* heapReader,
		BString& _contents)
	{
		// create a PackageDataReader
		BAbstractBufferedDataReader* reader;
		status_t result = BPackageDataReaderFactory()
			.CreatePackageDataReader(heapReader, fPackageData, reader);
		if (result != B_OK)
			return result;
		ObjectDeleter<BAbstractBufferedDataReader> readerDeleter(reader);

		// copy data into the given string
		int32 bufferSize = fPackageData.Size();
		char* buffer = _contents.LockBuffer(bufferSize);
		if (buffer == NULL)
			return B_NO_MEMORY;

		result = reader->ReadData(0, buffer, bufferSize);
		if (result != B_OK) {
			fErrorOutput->PrintError("Error: Failed to read data: %s\n",
				strerror(result));
			_contents.UnlockBuffer(0);
		} else
			_contents.UnlockBuffer(bufferSize);

		return result;
	}

private:
	/** @brief Sink for error reporting. */
	BErrorOutput*			fErrorOutput;
	/** @brief Package data descriptor whose bytes will be fetched. */
	BPackageData&			fPackageData;
};


// #pragma mark - PackageContentHandler


/**
 * @brief Content handler used while reading an existing HPKG to populate a
 *        BPackageInfo and harvest licence files into the repository info.
 *
 * Inherits the package-info parsing from BPackageInfoContentHandler and
 * overrides HandleEntry() so that any file inside a package's
 * "data/licenses" directory is read out and registered with the repository
 * info when @c B_PACKAGE_FLAG_APPROVE_LICENSE is set on the package.
 */
struct PackageContentHandler : public BPackageInfoContentHandler {
	/**
	 * @brief Construct the handler for one specific package and repository.
	 *
	 * @param errorOutput    Sink for diagnostic messages.
	 * @param packageInfo    Output info object that will be populated by
	 *                       the parent class as attributes are decoded.
	 * @param heapReader     Reader providing the package's compressed heap.
	 * @param repositoryInfo Repository info that gathers harvested licences.
	 */
	PackageContentHandler(BErrorOutput* errorOutput, BPackageInfo* packageInfo,
		BAbstractBufferedDataReader* heapReader,
		BRepositoryInfo* repositoryInfo)
		:
		BPackageInfoContentHandler(*packageInfo, errorOutput),
		fHeapReader(heapReader),
		fRepositoryInfo(repositoryInfo)
	{
	}

	/**
	 * @brief Inspect a package entry and harvest licence text when
	 *        applicable.
	 *
	 * Returns immediately for packages that do not require licence
	 * approval. For licence-bearing packages, reads any file located at
	 * @c ./data/licenses/<name> and registers @a entry's name and contents
	 * with the bound repository info. Duplicates already registered with
	 * the repository are skipped.
	 *
	 * @param entry The entry being visited; @c NULL terminates the walk.
	 * @return B_OK on success or skipped entries; otherwise an error from
	 *         the data reader or the repository info.
	 */
	virtual status_t HandleEntry(BPackageEntry* entry)
	{
		// if license must be approved, read any license files from package such
		// that those can be stored in the repository later
		if ((fPackageInfo.Flags() & B_PACKAGE_FLAG_APPROVE_LICENSE) == 0
			|| entry == NULL)
			return B_OK;

		// return if not in ./data/licenses folder
		const BPackageEntry* parent = entry->Parent();
		BString licenseFolderName("licenses");
		if (parent == NULL || licenseFolderName != parent->Name())
			return B_OK;

		parent = parent->Parent();
		BString dataFolderName("data");
		if (parent == NULL || dataFolderName != parent->Name())
			return B_OK;

		if (parent->Parent() != NULL)
			return B_OK;

		// check if license already is in repository
		const BStringList& licenseNames = fRepositoryInfo->LicenseNames();
		for (int i = 0; i < licenseNames.CountStrings(); ++i) {
			if (licenseNames.StringAt(i).ICompare(entry->Name()) == 0) {
				// license already exists
				return B_OK;
			}
		}

		// fetch contents of license file
		BPackageData& packageData = entry->Data();
		PackageEntryDataFetcher dataFetcher(fErrorOutput, packageData);

		BString licenseText;
		status_t result = dataFetcher.ReadIntoString(fHeapReader, licenseText);
		if (result != B_OK)
			return result;

		// add license to repository
		return fRepositoryInfo->AddLicense(entry->Name(), licenseText);
	}

	/** @brief No-op; entry attributes are not relevant for repository
	 *         indexing. */
	virtual status_t HandleEntryAttribute(BPackageEntry* entry,
		BPackageEntryAttribute* attribute)
	{
		return B_OK;
	}

	/** @brief No-op; the repository writer does not track per-entry
	 *         completion. */
	virtual status_t HandleEntryDone(BPackageEntry* entry)
	{
		return B_OK;
	}

	/** @brief Asynchronous error notification; unused by this handler. */
	virtual void HandleErrorOccurred()
	{
	}

private:
	/** @brief Reserved for an outer package reader (unused in this path). */
	BPackageReader*					fPackageReader;
	/** @brief Reader providing access to the package's compressed heap. */
	BAbstractBufferedDataReader*	fHeapReader;
	/** @brief Repository info that gathers harvested licence text. */
	BRepositoryInfo*				fRepositoryInfo;
};


}	// anonymous namespace


// #pragma mark - PackageNameSet


/**
 * @brief Hash set of package names already added to the repository, keyed by
 *        case-sensitive hashable strings.
 */
struct RepositoryWriterImpl::PackageNameSet
	: public ::BPrivate::HashSet<HashableString> {
};


// #pragma mark - RepositoryWriterImpl


/**
 * @brief Construct a repository writer bound to a listener and repository
 *        metadata object.
 *
 * The listener receives progress callbacks and error messages while the
 * writer runs. The repository info supplies the vendor, architecture, and
 * licence collection that constraints individual packages must satisfy.
 *
 * @param listener       Caller-supplied listener for progress and errors.
 * @param repositoryInfo Repository-wide metadata (not owned by the writer).
 */
RepositoryWriterImpl::RepositoryWriterImpl(BRepositoryWriterListener* listener,
	BRepositoryInfo* repositoryInfo)
	:
	inherited("repository", listener),
	fListener(listener),
	fRepositoryInfo(repositoryInfo),
	fPackageCount(0),
	fPackageNames(NULL)
{
}


/**
 * @brief Destroy the writer and release the duplicate-name guard set.
 */
RepositoryWriterImpl::~RepositoryWriterImpl()
{
	delete fPackageNames;
}


/**
 * @brief Open the repository file for writing and prepare internal state.
 *
 * Creates the duplicate-name guard set and then forwards to @c _Init() to
 * open the underlying file and heap writer. Catches and translates
 * std::bad_alloc and status_t-typed exceptions thrown by the lower layers.
 *
 * @param fileName Path of the repository file to create or truncate.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or an error
 *         from the base writer's Init().
 */
status_t
RepositoryWriterImpl::Init(const char* fileName)
{
	try {
		fPackageNames = new PackageNameSet();
		status_t result = fPackageNames->InitCheck();
		if (result != B_OK)
			return result;
		return _Init(fileName);
	} catch (status_t error) {
		return error;
	} catch (std::bad_alloc&) {
		fListener->PrintError("Out of memory!\n");
		return B_NO_MEMORY;
	}
}


/**
 * @brief Read a built HPKG file from disk and add its metadata to the
 *        repository being written.
 *
 * Catches and translates std::bad_alloc and status_t-typed exceptions
 * thrown by the lower layers.
 *
 * @param packageEntry BEntry referring to an existing .hpkg file on disk.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or an error
 *         describing why the package could not be parsed or registered.
 */
status_t
RepositoryWriterImpl::AddPackage(const BEntry& packageEntry)
{
	try {
		return _AddPackage(packageEntry);
	} catch (status_t error) {
		return error;
	} catch (std::bad_alloc&) {
		fListener->PrintError("Out of memory!\n");
		return B_NO_MEMORY;
	}
}


/**
 * @brief Add a package to the repository directly from a BPackageInfo
 *        object.
 *
 * Used when the caller has already produced or unpacked the package info
 * and does not need a checksum to be computed from the on-disk file.
 *
 * @param packageInfo The package metadata to register.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or an error
 *         from validation (mismatched vendor, duplicate name, etc.).
 */
status_t
RepositoryWriterImpl::AddPackageInfo(const BPackageInfo& packageInfo)
{
	try {
		return _AddPackageInfo(packageInfo);
	} catch (status_t error) {
		return error;
	} catch (std::bad_alloc&) {
		fListener->PrintError("Out of memory!\n");
		return B_NO_MEMORY;
	}
}


/**
 * @brief Flush all repository data to disk and close out the file.
 *
 * Writes the repository info section, the package attributes section,
 * finalises the heap writer, and updates the file header. Catches and
 * translates std::bad_alloc and status_t-typed exceptions.
 *
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or an error
 *         from the underlying heap or attribute writers.
 */
status_t
RepositoryWriterImpl::Finish()
{
	try {
		return _Finish();
	} catch (status_t error) {
		return error;
	} catch (std::bad_alloc&) {
		fListener->PrintError("Out of memory!\n");
		return B_NO_MEMORY;
	}
}


/**
 * @brief Open the underlying writer base and the heap writer.
 *
 * Internal helper called from Init() with exception handling already in
 * place.
 *
 * @param fileName Path of the repository file to create or truncate.
 * @return B_OK on success, otherwise an error from the base writer or
 *         heap writer initialisation.
 */
status_t
RepositoryWriterImpl::_Init(const char* fileName)
{
	status_t error = inherited::Init(NULL, false, fileName,
		BPackageWriterParameters());
	if (error != B_OK)
		return error;

	return InitHeapReader(sizeof(hpkg_repo_header));
}


/**
 * @brief Serialise repository metadata, finalise the heap, and write the
 *        file header.
 *
 * Builds the repository info section, then the package attributes section,
 * flushes the heap writer (gathering compressed/uncompressed sizes), and
 * patches the @c hpkg_repo_header at offset 0 with the section sizes,
 * compression method, and final total file size.
 *
 * @return B_OK on success, otherwise an error from one of the section
 *         writers or the heap writer.
 */
status_t
RepositoryWriterImpl::_Finish()
{
	hpkg_repo_header header;

	// write repository info
	uint64 infoLength;
	status_t result = _WriteRepositoryInfo(header, infoLength);
	if (result != B_OK)
		return result;

	// write package attributes
	uint64 packagesLength;
	_WritePackageAttributes(header, packagesLength);

	// flush the heap writer
	result = fHeapWriter->Finish();
	if (result != B_OK)
		return result;
	uint64 compressedHeapSize = fHeapWriter->CompressedHeapSize();
	uint64 totalSize = fHeapWriter->HeapOffset() + compressedHeapSize;

	header.heap_compression = B_HOST_TO_BENDIAN_INT16(
		Parameters().Compression());
	header.heap_chunk_size = B_HOST_TO_BENDIAN_INT32(fHeapWriter->ChunkSize());
	header.heap_size_compressed = B_HOST_TO_BENDIAN_INT64(compressedHeapSize);
	header.heap_size_uncompressed = B_HOST_TO_BENDIAN_INT64(
		fHeapWriter->UncompressedHeapSize());

	fListener->OnRepositoryDone(sizeof(header), infoLength,
		fRepositoryInfo->LicenseNames().CountStrings(), fPackageCount,
		packagesLength, totalSize);

	// update the general header info and write the header
	header.magic = B_HOST_TO_BENDIAN_INT32(B_HPKG_REPO_MAGIC);
	header.header_size = B_HOST_TO_BENDIAN_INT16((uint16)sizeof(header));
	header.version = B_HOST_TO_BENDIAN_INT16(B_HPKG_REPO_VERSION);
	header.total_size = B_HOST_TO_BENDIAN_INT64(totalSize);
	header.minor_version = B_HOST_TO_BENDIAN_INT16(B_HPKG_REPO_MINOR_VERSION);

	RawWriteBuffer(&header, sizeof(header), 0);

	SetFinished(true);
	return B_OK;
}


/**
 * @brief Parse an HPKG file and register its metadata with the repository.
 *
 * Resolves @a packageEntry to a path, opens it with a BPackageReader,
 * parses package attributes through PackageContentHandler (also harvesting
 * any approval-required licence text), computes the file's checksum, and
 * delegates to _RegisterCurrentPackageInfo() to validate and store it.
 *
 * @param packageEntry BEntry referring to an existing .hpkg file on disk.
 * @return B_OK on success, otherwise an error describing why the package
 *         could not be opened, parsed, checksummed, or registered.
 */
status_t
RepositoryWriterImpl::_AddPackage(const BEntry& packageEntry)
{
	status_t result = packageEntry.InitCheck();
	if (result != B_OK) {
		fListener->PrintError("entry not initialized!\n");
		return result;
	}

	BPath packagePath;
	if ((result = packageEntry.GetPath(&packagePath)) != B_OK) {
		fListener->PrintError("can't get path for entry '%s'!\n",
			packageEntry.Name());
		return result;
	}

	BPackageReader packageReader(fListener);
	if ((result = packageReader.Init(packagePath.Path())) != B_OK) {
		fListener->PrintError("can't create package reader for '%s'!\n",
			packagePath.Path());
		return result;
	}

	fPackageInfo.Clear();

	// parse package
	PackageContentHandler contentHandler(fListener, &fPackageInfo,
		packageReader.HeapReader(), fRepositoryInfo);
	if ((result = packageReader.ParseContent(&contentHandler)) != B_OK)
		return result;

	// determine package's checksum
	GeneralFileChecksumAccessor checksumAccessor(packageEntry);
	BString checksum;
	if ((result = checksumAccessor.GetChecksum(checksum)) != B_OK) {
		fListener->PrintError("can't compute checksum of file '%s'!\n",
			packagePath.Path());
		return result;
	}
	fPackageInfo.SetChecksum(checksum);

	// register package's attributes
	if ((result = _RegisterCurrentPackageInfo()) != B_OK)
		return result;

	return B_OK;
}


/**
 * @brief Copy a caller-supplied BPackageInfo into the writer and register
 *        it.
 *
 * Skips file I/O entirely; useful when the caller has already gathered the
 * metadata from another source.
 *
 * @param packageInfo Package metadata to register.
 * @return B_OK on success, otherwise an error from validation.
 */
status_t
RepositoryWriterImpl::_AddPackageInfo(const BPackageInfo& packageInfo)
{
	fPackageInfo = packageInfo;

	// register package's attributes
	status_t result = _RegisterCurrentPackageInfo();
	if (result != B_OK)
		return result;

	return B_OK;
}


/**
 * @brief Validate the in-flight @c fPackageInfo and append it to the
 *        repository.
 *
 * Enforces the duplicate-name guard, vendor match against the repository,
 * and architecture compatibility (the package must match the repository
 * architecture or be one of @c B_PACKAGE_ARCHITECTURE_ANY /
 * @c B_PACKAGE_ARCHITECTURE_SOURCE). On success the package's
 * info attributes are written into the package attribute tree and the
 * listener is notified.
 *
 * @retval B_OK            The package was accepted.
 * @retval B_NAME_IN_USE   A package with the same name was already added.
 * @retval B_BAD_DATA      The package's vendor or architecture does not
 *                         match the repository.
 * @return Otherwise an error from BPackageInfo::InitCheck() or the hash
 *         set.
 */
status_t
RepositoryWriterImpl::_RegisterCurrentPackageInfo()
{
	status_t result = fPackageInfo.InitCheck();
	if (result != B_OK) {
		fListener->PrintError("package %s has incomplete package-info!\n",
			fPackageInfo.Name().String());
		return result;
	}

	// reject package with a name that we've seen already
	if (fPackageNames->Contains(fPackageInfo.Name())) {
		fListener->PrintError("package %s has already been added!\n",
			fPackageInfo.Name().String());
		return B_NAME_IN_USE;
	}

	// all packages must have the same vendor as the repository
	const BString& expectedVendor = fRepositoryInfo->Vendor();
	if (fPackageInfo.Vendor().ICompare(expectedVendor) != 0) {
		fListener->PrintError("package '%s' has unexpected vendor '%s' "
			"(expected '%s')!\n", fPackageInfo.Name().String(),
			fPackageInfo.Vendor().String(), expectedVendor.String());
		return B_BAD_DATA;
	}

	// all packages must have an architecture that's compatible with the one
	// used by the repository
	BPackageArchitecture expectedArchitecture = fRepositoryInfo->Architecture();
	if (fPackageInfo.Architecture() != expectedArchitecture
		&& fPackageInfo.Architecture() != B_PACKAGE_ARCHITECTURE_ANY
		&& fPackageInfo.Architecture() != B_PACKAGE_ARCHITECTURE_SOURCE) {
		fListener->PrintError(
			"package '%s' has non-matching architecture '%s' "
			"(expected '%s', '%s', or '%s')!\n", fPackageInfo.Name().String(),
			BPackageInfo::kArchitectureNames[fPackageInfo.Architecture()],
			BPackageInfo::kArchitectureNames[expectedArchitecture],
			BPackageInfo::kArchitectureNames[B_PACKAGE_ARCHITECTURE_ANY],
			BPackageInfo::kArchitectureNames[B_PACKAGE_ARCHITECTURE_SOURCE]);
		return B_BAD_DATA;
	}

	if ((result = fPackageNames->Add(fPackageInfo.Name())) != B_OK)
		return result;

	PackageAttribute* packageAttribute = AddStringAttribute(
		B_HPKG_ATTRIBUTE_ID_PACKAGE, fPackageInfo.Name(), PackageAttributes());
	RegisterPackageInfo(packageAttribute->children, fPackageInfo);
	fPackageCount++;
	fListener->OnPackageAdded(fPackageInfo);

	return B_OK;
}


/**
 * @brief Archive the repository info into a flattened BMessage and write it
 *        to the file.
 *
 * Updates @a header.info_length with the section's flattened size and
 * notifies the listener that the section is complete.
 *
 * @param header  The repository file header being assembled (output).
 * @param _length Output variable receiving the section's flattened size.
 * @return B_OK on success, otherwise an error from BMessage::Archive() or
 *         BMessage::Flatten().
 */
status_t
RepositoryWriterImpl::_WriteRepositoryInfo(hpkg_repo_header& header,
	uint64& _length)
{
	// archive and flatten the repository info and write it
	BMessage archive;
	status_t result = fRepositoryInfo->Archive(&archive);
	if (result != B_OK) {
		fListener->PrintError("can't archive repository header!\n");
		return result;
	}

	ssize_t	flattenedSize = archive.FlattenedSize();
	char buffer[flattenedSize];
	if ((result = archive.Flatten(buffer, flattenedSize)) != B_OK) {
		fListener->PrintError("can't flatten repository header!\n");
		return result;
	}

	WriteBuffer(buffer, flattenedSize);

	// notify listener
	fListener->OnRepositoryInfoSectionDone(flattenedSize);

	// update the header
	header.info_length = B_HOST_TO_BENDIAN_INT32(flattenedSize);

	_length = flattenedSize;
	return B_OK;
}


/**
 * @brief Serialise the accumulated package attribute tree into the heap.
 *
 * Records section size, string count, and string-table length in the
 * header for later retrieval by the reader, and notifies the listener.
 *
 * @param header  The repository file header being assembled (output).
 * @param _length Output variable receiving the section's uncompressed size.
 */
void
RepositoryWriterImpl::_WritePackageAttributes(hpkg_repo_header& header,
	uint64& _length)
{
	// write the package attributes (zlib writer on top of a file writer)
	uint64 startOffset = fHeapWriter->UncompressedHeapSize();

	uint32 stringsLength;
	uint32 stringsCount = WritePackageAttributes(PackageAttributes(),
		stringsLength);

	uint64 sectionSize = fHeapWriter->UncompressedHeapSize() - startOffset;

	fListener->OnPackageAttributesSectionDone(stringsCount, sectionSize);

	// update the header
	header.packages_length = B_HOST_TO_BENDIAN_INT64(sectionSize);
	header.packages_strings_count = B_HOST_TO_BENDIAN_INT64(stringsCount);
	header.packages_strings_length = B_HOST_TO_BENDIAN_INT64(stringsLength);

	_length = sectionSize;
}


}	// namespace BPrivate

}	// namespace BHPKG

}	// namespace BPackageKit
