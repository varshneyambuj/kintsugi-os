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
 * @file RepositoryReaderImpl.cpp
 * @brief Internal implementation of the HPKG repository index reader.
 *
 * RepositoryReaderImpl parses the repository info and packages attribute
 * sections of an HPKG repository index file, delivering package names and
 * attributes to a BRepositoryContentHandler through a chain of inner
 * AttributeHandler objects. It inherits heap and attribute-tree parsing
 * machinery from ReaderImplBase.
 *
 * @see BRepositoryReader, ReaderImplBase, RepositoryWriterImpl
 */


#include <package/hpkg/RepositoryReaderImpl.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <algorithm>
#include <new>

#include <ByteOrder.h>
#include <Message.h>

#include <FdIO.h>

#include <package/hpkg/HPKGDefsPrivate.h>
#include <package/hpkg/RepositoryContentHandler.h>


namespace BPackageKit {

namespace BHPKG {

namespace BPrivate {


//#define TRACE(format...)	printf(format)
#define TRACE(format...)	do {} while (false)


/** @brief Maximum size in bytes of the repository info section we will read. */
static const size_t kMaxRepositoryInfoSize		= 1 * 1024 * 1024;

/** @brief Maximum size in bytes of the package attributes section we will read. */
static const size_t kMaxPackageAttributesSize	= 64 * 1024 * 1024;


// #pragma mark - PackagesAttributeHandler


/**
 * @brief Top-level attribute handler that walks the repository's per-package
 *        attribute records.
 *
 * Forwards each encountered package and its attributes to the supplied
 * BRepositoryContentHandler, signalling start and end of every package.
 */
class RepositoryReaderImpl::PackagesAttributeHandler
	: public AttributeHandler {
private:
	typedef AttributeHandler super;
public:
	/**
	 * @brief Construct a packages attribute handler bound to a content
	 *        handler.
	 * @param contentHandler Caller-supplied repository content handler that
	 *                       receives package notifications. May be NULL.
	 */
	PackagesAttributeHandler(BRepositoryContentHandler* contentHandler)
		:
		fContentHandler(contentHandler),
		fPackageName(NULL)
	{
	}

	/**
	 * @brief Dispatch a single attribute appearing under the packages section.
	 *
	 * Recognises @c B_HPKG_ATTRIBUTE_ID_PACKAGE entries and notifies the
	 * content handler of every new package. A nested
	 * PackageAttributeHandler is allocated to consume the package's
	 * sub-attributes.
	 *
	 * @param context Reader context (memory pool, error output, options).
	 * @param id      The attribute ID being handled.
	 * @param value   Decoded attribute value.
	 * @param _handler Output for a child handler when the attribute opens a
	 *                 nested attribute scope.
	 * @return B_OK on success, B_BAD_DATA on unexpected attributes, or an
	 *         error from the content handler.
	 */
	virtual status_t HandleAttribute(AttributeHandlerContext* context, uint8 id,
		const AttributeValue& value, AttributeHandler** _handler)
	{
		switch (id) {
			case B_HPKG_ATTRIBUTE_ID_PACKAGE:
			{
				status_t error = _NotifyPackageDone();
				if (error != B_OK)
					return error;

				if (_handler != NULL) {
					if (fContentHandler != NULL) {
						error = fContentHandler->HandlePackage(value.string);
						if (error != B_OK)
							return error;
					}

					*_handler = new(context) PackageAttributeHandler;
					if (*_handler == NULL)
						return B_NO_MEMORY;

					fPackageName = value.string;
				}
				break;
			}

			default:
				if (context->ignoreUnknownAttributes)
					break;

				context->errorOutput->PrintError(
					"Error: Invalid package attribute section: unexpected "
					"top level attribute id %d encountered\n", id);
				return B_BAD_DATA;
		}

		return B_OK;
	}

	/**
	 * @brief Finalise the current package then defer to the parent handler.
	 *
	 * Called when the packages attribute scope closes. Ensures the in-flight
	 * package is reported as done before the base handler runs its own
	 * shutdown logic.
	 *
	 * @param context Reader context.
	 * @return B_OK on success, otherwise the first error encountered.
	 */
	virtual status_t NotifyDone(AttributeHandlerContext* context)
	{
		status_t result = _NotifyPackageDone();
		if (result == B_OK)
			result = super::NotifyDone(context);
		return result;
	}

private:
	/**
	 * @brief Emit a HandlePackageDone() callback for the current package.
	 *
	 * No-op when no package is in flight or when no content handler was
	 * supplied.
	 *
	 * @return B_OK on success, otherwise the content handler's error code.
	 */
	status_t _NotifyPackageDone()
	{
		if (fPackageName == NULL || fContentHandler == NULL)
			return B_OK;

		status_t error = fContentHandler->HandlePackageDone(fPackageName);
		fPackageName = NULL;
		return error;
	}

private:
	/** @brief Content handler that receives every package callback. */
	BRepositoryContentHandler*	fContentHandler;
	/** @brief Name of the package currently being decoded, or NULL. */
	const char*					fPackageName;
};


// #pragma mark - PackageContentHandlerAdapter


/**
 * @brief Bridges a BPackageContentHandler interface onto a
 *        BRepositoryContentHandler.
 *
 * Repository attribute parsing is implemented in terms of the package
 * content handler API; this adapter forwards only the package-level
 * attribute callbacks that a repository handler actually exposes and
 * silently swallows the entry/attribute callbacks that do not apply to
 * repository indexes.
 */
class RepositoryReaderImpl::PackageContentHandlerAdapter
	: public BPackageContentHandler {
public:
	/**
	 * @brief Construct the adapter wrapping a repository content handler.
	 * @param contentHandler Repository content handler to forward to.
	 */
	PackageContentHandlerAdapter(BRepositoryContentHandler* contentHandler)
		:
		fContentHandler(contentHandler)
	{
	}

	/** @brief No-op; entries are not part of repository indexes. */
	virtual status_t HandleEntry(BPackageEntry* entry)
	{
		return B_OK;
	}

	/** @brief No-op; entry attributes are not part of repository indexes. */
	virtual status_t HandleEntryAttribute(BPackageEntry* entry,
		BPackageEntryAttribute* attribute)
	{
		return B_OK;
	}

	/** @brief No-op; entries are not part of repository indexes. */
	virtual status_t HandleEntryDone(BPackageEntry* entry)
	{
		return B_OK;
	}

	/**
	 * @brief Forward a package attribute value to the repository handler.
	 * @param value Decoded package info attribute value.
	 * @return The underlying repository handler's status.
	 */
	virtual status_t HandlePackageAttribute(
		const BPackageInfoAttributeValue& value)
	{
		return fContentHandler->HandlePackageAttribute(value);
	}

	/** @brief Forward an asynchronous error notification to the repository
	 *         handler. */
	virtual void HandleErrorOccurred()
	{
		return fContentHandler->HandleErrorOccurred();
	}

private:
	/** @brief Underlying repository handler that receives forwarded callbacks. */
	BRepositoryContentHandler*	fContentHandler;
};


// #pragma mark - RepositoryReaderImpl


/**
 * @brief Construct an empty repository reader bound to an error output.
 * @param errorOutput Sink for diagnostic messages emitted while reading.
 */
RepositoryReaderImpl::RepositoryReaderImpl(BErrorOutput* errorOutput)
	:
	inherited("repository", errorOutput)
{
}


/**
 * @brief Destroy the reader and release any sections still held open.
 */
RepositoryReaderImpl::~RepositoryReaderImpl()
{
}


/**
 * @brief Open a repository index file by path and prepare it for parsing.
 *
 * Opens the named file read-only, transferring ownership of the resulting
 * file descriptor to the reader.
 *
 * @param fileName Path to the .hpkr repository index file.
 * @return B_OK on success, errno-mapped error on open failure, or an init
 *         error from the descriptor-based overload.
 */
status_t
RepositoryReaderImpl::Init(const char* fileName)
{
	// open file
	int fd = open(fileName, O_RDONLY);
	if (fd < 0) {
		ErrorOutput()->PrintError(
			"Error: Failed to open repository file \"%s\": %s\n", fileName,
			strerror(errno));
		return errno;
	}

	return Init(fd, true);
}


/**
 * @brief Initialise the reader from an open file descriptor.
 *
 * Wraps @a fd in a BFdIO so that the standard ReaderImplBase machinery can
 * issue positioned reads.
 *
 * @param fd     A file descriptor opened for reading on a repository file.
 * @param keepFD If @c true, the reader takes ownership and will close the
 *               descriptor when destroyed.
 * @return B_OK on success, B_NO_MEMORY if the wrapper cannot be allocated,
 *         or an init error from the BPositionIO overload.
 */
status_t
RepositoryReaderImpl::Init(int fd, bool keepFD)
{
	BFdIO* file = new(std::nothrow) BFdIO(fd, keepFD);
	if (file == NULL) {
		if (keepFD && fd >= 0)
			close(fd);
		return B_NO_MEMORY;
	}

	return Init(file, true);
}


/**
 * @brief Initialise the reader from a positioned I/O object.
 *
 * Reads and validates the repository header, then prepares the repository
 * info and package attributes sections so they can be parsed on demand.
 * Unflattens the embedded BMessage carrying the repository info and stores
 * it for later retrieval.
 *
 * @param file     The repository file as a positioned I/O object.
 * @param keepFile If @c true, the reader owns @a file and will delete it
 *                 when destroyed.
 * @return B_OK on success, otherwise an error describing why the file could
 *         not be opened or interpreted.
 */
status_t
RepositoryReaderImpl::Init(BPositionIO* file, bool keepFile)
{
	hpkg_repo_header header;
	status_t error = inherited::Init<hpkg_repo_header, B_HPKG_REPO_MAGIC,
		B_HPKG_REPO_VERSION, B_HPKG_REPO_MINOR_VERSION>(file, keepFile, header,
		0);
	if (error != B_OK)
		return error;

	// init package attributes section
	error = InitSection(fPackageAttributesSection,
		UncompressedHeapSize(),
		B_BENDIAN_TO_HOST_INT64(header.packages_length),
		kMaxPackageAttributesSize,
		B_BENDIAN_TO_HOST_INT64(header.packages_strings_length),
		B_BENDIAN_TO_HOST_INT64(header.packages_strings_count));
	if (error != B_OK)
		return error;

	// init repository info section
	PackageFileSection repositoryInfoSection("repository info");
	error = InitSection(repositoryInfoSection,
		fPackageAttributesSection.offset,
		B_BENDIAN_TO_HOST_INT32(header.info_length), kMaxRepositoryInfoSize, 0,
		0);
	if (error != B_OK)
		return error;

	// prepare the sections for use
	error = PrepareSection(repositoryInfoSection);
	if (error != B_OK)
		return error;

	error = PrepareSection(fPackageAttributesSection);
	if (error != B_OK)
		return error;

	// unarchive repository info
	BMessage repositoryInfoArchive;
	error = repositoryInfoArchive.Unflatten((char*)repositoryInfoSection.data);
	if (error != B_OK) {
		ErrorOutput()->PrintError(
			"Error: Unable to unflatten repository info archive!\n");
		return error;
	}
	error = fRepositoryInfo.SetTo(&repositoryInfoArchive);
	if (error != B_OK) {
		ErrorOutput()->PrintError(
			"Error: Unable to unarchive repository info!\n");
		return error;
	}

	return B_OK;
}


/**
 * @brief Retrieve the repository's metadata as parsed during Init().
 *
 * @param _repositoryInfo Output target that receives a copy of the repository
 *                        info; must not be NULL.
 * @retval B_OK         The info was copied successfully.
 * @retval B_BAD_VALUE  @a _repositoryInfo is NULL.
 */
status_t
RepositoryReaderImpl::GetRepositoryInfo(BRepositoryInfo* _repositoryInfo) const
{
	if (_repositoryInfo == NULL)
		return B_BAD_VALUE;

	*_repositoryInfo = fRepositoryInfo;
	return B_OK;
}


/**
 * @brief Walk the package attributes section, dispatching callbacks to the
 *        supplied content handler.
 *
 * Notifies @a contentHandler of the repository info first, then iterates
 * over every package record using a PackagesAttributeHandler chain. The
 * minor-version mismatch flag is forwarded so that newer repositories can
 * be tolerated by older clients when they only use known attributes.
 *
 * @param contentHandler Caller-supplied repository content handler.
 * @return B_OK on success, otherwise the first error encountered while
 *         parsing or while a callback was running.
 */
status_t
RepositoryReaderImpl::ParseContent(BRepositoryContentHandler* contentHandler)
{
	status_t result = contentHandler->HandleRepositoryInfo(fRepositoryInfo);
	if (result == B_OK) {
		PackageContentHandlerAdapter contentHandlerAdapter(contentHandler);
		AttributeHandlerContext context(ErrorOutput(),
			contentHandler != NULL ? &contentHandlerAdapter : NULL,
			B_HPKG_SECTION_PACKAGE_ATTRIBUTES,
			MinorFormatVersion() > B_HPKG_REPO_MINOR_VERSION);
		PackagesAttributeHandler rootAttributeHandler(contentHandler);
		result = ParsePackageAttributesSection(&context, &rootAttributeHandler);
	}
	return result;
}


}	// namespace BPrivate

}	// namespace BHPKG

}	// namespace BPackageKit
