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
 *   Copyright 2011, Oliver Tappe <zooey@hirschkaefer.de>
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file PackageReaderImplV1.cpp
 * @brief Internal implementation of the v1 HPKG package reader.
 *
 * PackageReaderImpl (v1) parses the TOC and package attributes sections of a
 * v1 HPKG package file. It drives a chain of inner AttributeHandler objects
 * (DataAttributeHandler, FileAttributeHandler, etc.) that reconstruct the
 * virtual file system tree and deliver entries, extended attributes, and
 * package metadata to the caller-supplied content handler.
 *
 * @see BPackageReader (V1), ReaderImplBaseV1, PackageDataReaderV1
 */


#include <package/hpkg/v1/PackageReaderImpl.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <new>

#include <ByteOrder.h>

#include <package/hpkg/v1/HPKGDefsPrivate.h>

#include <package/hpkg/ErrorOutput.h>
#include <package/hpkg/v1/PackageData.h>
#include <package/hpkg/v1/PackageEntry.h>
#include <package/hpkg/v1/PackageEntryAttribute.h>


namespace BPackageKit {

namespace BHPKG {

namespace V1 {

namespace BPrivate {


//#define TRACE(format...)	printf(format)
#define TRACE(format...)	do {} while (false)


/** @brief Maximum TOC size in bytes the v1 reader will accept. */
static const size_t kMaxTOCSize					= 64 * 1024 * 1024;

/** @brief Maximum package attributes section size the v1 reader will
 *         accept. */
static const size_t kMaxPackageAttributesSize	= 1 * 1024 * 1024;


// #pragma mark - DataAttributeHandler


/**
 * @brief Attribute handler that fills out a BPackageData (v1) from
 *        @c B_HPKG_ATTRIBUTE_ID_DATA_* sub-attributes.
 *
 * Captures size, compression method, and chunk size of an entry's data
 * stream while the package's TOC is being parsed.
 */
struct PackageReaderImpl::DataAttributeHandler : AttributeHandler {
	/**
	 * @brief Construct the handler bound to the data record being filled.
	 * @param data Package data record updated as attributes are decoded.
	 */
	DataAttributeHandler(BPackageData* data)
		:
		fData(data)
	{
	}

	/**
	 * @brief Initialise the basic data fields from the leading data
	 *        attribute value.
	 *
	 * Distinguishes between inline and heap-resident data and records the
	 * uncompressed size as a starting point.
	 *
	 * @param context Reader context (unused here, accepted for symmetry).
	 * @param data    Package data record to initialise.
	 * @param value   Decoded attribute value carrying the data reference.
	 * @return Always B_OK.
	 */
	static status_t InitData(AttributeHandlerContext* context,
		BPackageData* data, const AttributeValue& value)
	{
		if (value.encoding == B_HPKG_ATTRIBUTE_ENCODING_RAW_INLINE)
			data->SetData(value.data.size, value.data.raw);
		else
			data->SetData(value.data.size, value.data.offset);

		data->SetUncompressedSize(value.data.size);

		return B_OK;
	}

	/**
	 * @brief Allocate a DataAttributeHandler and run InitData() on its
	 *        backing record.
	 *
	 * @param context  Reader context.
	 * @param data     Package data record to populate.
	 * @param value    Decoded data attribute value.
	 * @param _handler Output: heap-allocated handler on success.
	 * @retval B_OK         Handler created.
	 * @retval B_NO_MEMORY  Allocation failed.
	 */
	static status_t Create(AttributeHandlerContext* context,
		BPackageData* data, const AttributeValue& value,
		AttributeHandler*& _handler)
	{
		DataAttributeHandler* handler = new(std::nothrow) DataAttributeHandler(
			data);
		if (handler == NULL)
			return B_NO_MEMORY;

		InitData(context, data, value);

		_handler = handler;
		return B_OK;
	}

	/**
	 * @brief Apply a single data sub-attribute to the bound BPackageData
	 *        record.
	 *
	 * Recognises @c B_HPKG_ATTRIBUTE_ID_DATA_SIZE,
	 * @c B_HPKG_ATTRIBUTE_ID_DATA_COMPRESSION (validating the method), and
	 * @c B_HPKG_ATTRIBUTE_ID_DATA_CHUNK_SIZE. Unknown IDs are forwarded to
	 * the base class.
	 *
	 * @param context  Reader context.
	 * @param id       Attribute ID being handled.
	 * @param value    Decoded attribute value.
	 * @param _handler Optional output for a child handler.
	 * @retval B_OK         The attribute was applied.
	 * @retval B_BAD_DATA   Unsupported compression method.
	 * @return Otherwise the result of the base class handler.
	 */
	virtual status_t HandleAttribute(AttributeHandlerContext* context,
		uint8 id, const AttributeValue& value, AttributeHandler** _handler)
	{
		switch (id) {
			case B_HPKG_ATTRIBUTE_ID_DATA_SIZE:
				fData->SetUncompressedSize(value.unsignedInt);
				return B_OK;

			case B_HPKG_ATTRIBUTE_ID_DATA_COMPRESSION:
			{
				switch (value.unsignedInt) {
					case B_HPKG_COMPRESSION_NONE:
					case B_HPKG_COMPRESSION_ZLIB:
						break;
					default:
						context->errorOutput->PrintError("Error: Invalid "
							"compression type for data (%llu)\n",
							value.unsignedInt);
						return B_BAD_DATA;
				}

				fData->SetCompression(value.unsignedInt);
				return B_OK;
			}

			case B_HPKG_ATTRIBUTE_ID_DATA_CHUNK_SIZE:
				fData->SetChunkSize(value.unsignedInt);
				return B_OK;
		}

		return AttributeHandler::HandleAttribute(context, id, value, _handler);
	}

private:
	/** @brief Package data record being populated by this handler. */
	BPackageData*	fData;
};


// #pragma mark - AttributeAttributeHandler


/**
 * @brief Handler for a single extended-attribute record attached to a
 *        package entry.
 *
 * Builds a BPackageEntryAttribute from @c FILE_ATTRIBUTE_TYPE and @c DATA
 * sub-attributes and notifies the package content handler when the
 * attribute is complete.
 */
struct PackageReaderImpl::AttributeAttributeHandler : AttributeHandler {
	/**
	 * @brief Construct the handler with the parent entry and attribute name.
	 * @param entry Entry the extended attribute belongs to.
	 * @param name  Name of the extended attribute.
	 */
	AttributeAttributeHandler(BPackageEntry* entry, const char* name)
		:
		fEntry(entry),
		fAttribute(name)
	{
	}

	/**
	 * @brief Apply a single sub-attribute to the in-flight extended
	 *        attribute record.
	 *
	 * Recognises @c B_HPKG_ATTRIBUTE_ID_DATA (delegating to a
	 * DataAttributeHandler) and @c B_HPKG_ATTRIBUTE_ID_FILE_ATTRIBUTE_TYPE.
	 *
	 * @param context  Reader context.
	 * @param id       Attribute ID being handled.
	 * @param value    Decoded attribute value.
	 * @param _handler Optional output for a child handler.
	 * @return B_OK on success, otherwise the base class result.
	 */
	virtual status_t HandleAttribute(AttributeHandlerContext* context,
		uint8 id, const AttributeValue& value, AttributeHandler** _handler)
	{
		switch (id) {
			case B_HPKG_ATTRIBUTE_ID_DATA:
				if (_handler != NULL) {
					return DataAttributeHandler::Create(context,
						&fAttribute.Data(), value, *_handler);
				}
				return DataAttributeHandler::InitData(context,
					&fAttribute.Data(), value);

			case B_HPKG_ATTRIBUTE_ID_FILE_ATTRIBUTE_TYPE:
				fAttribute.SetType(value.unsignedInt);
				return B_OK;
		}

		return AttributeHandler::HandleAttribute(context, id, value, _handler);
	}

	/**
	 * @brief Notify the content handler that the extended attribute is
	 *        complete and self-destruct.
	 *
	 * @param context Reader context.
	 * @return B_OK on success, otherwise the content handler's error.
	 */
	virtual status_t Delete(AttributeHandlerContext* context)
	{
		status_t error = context->packageContentHandler->HandleEntryAttribute(
			fEntry, &fAttribute);

		delete this;
		return error;
	}

private:
	/** @brief Parent entry that owns the extended attribute. */
	BPackageEntry*			fEntry;
	/** @brief Extended attribute being assembled. */
	BPackageEntryAttribute	fAttribute;
};


// #pragma mark - EntryAttributeHandler


/**
 * @brief Handler for one entry (file/directory/symlink) inside the package
 *        TOC tree.
 *
 * Maintains a BPackageEntry across nested attribute calls, recording file
 * type, permissions, timestamps, symlink target, and data range. Spawns
 * child handlers for nested directory entries, file attributes, and data
 * sub-records, and emits @c HandleEntry / @c HandleEntryDone notifications
 * to the content handler.
 */
struct PackageReaderImpl::EntryAttributeHandler : AttributeHandler {
	/**
	 * @brief Construct the handler with a parent entry and a child name.
	 *
	 * Initialises the entry's file type to the default and records the
	 * default permissions; subsequent attributes may override these.
	 *
	 * @param context     Reader context.
	 * @param parentEntry Parent directory entry, or NULL for top-level.
	 * @param name        Name of the new entry.
	 */
	EntryAttributeHandler(AttributeHandlerContext* context,
		BPackageEntry* parentEntry, const char* name)
		:
		fEntry(parentEntry, name),
		fNotified(false)
	{
		_SetFileType(context, B_HPKG_DEFAULT_FILE_TYPE);
	}

	/**
	 * @brief Validate @a name and allocate an EntryAttributeHandler.
	 *
	 * Rejects empty names, "." / ".." , and names containing '/'.
	 *
	 * @param context     Reader context.
	 * @param parentEntry Parent directory entry, or NULL.
	 * @param name        Proposed name of the entry.
	 * @param _handler    Output: heap-allocated handler on success.
	 * @retval B_OK         Handler created.
	 * @retval B_BAD_DATA   Invalid entry name.
	 * @retval B_NO_MEMORY  Allocation failure.
	 */
	static status_t Create(AttributeHandlerContext* context,
		BPackageEntry* parentEntry, const char* name,
		AttributeHandler*& _handler)
	{
		// check name
		if (name[0] == '\0' || strcmp(name, ".") == 0
			|| strcmp(name, "..") == 0 || strchr(name, '/') != NULL) {
			context->errorOutput->PrintError("Error: Invalid package: Invalid "
				"entry name: \"%s\"\n", name);
			return B_BAD_DATA;
		}

		// create handler
		EntryAttributeHandler* handler = new(std::nothrow)
			EntryAttributeHandler(context, parentEntry, name);
		if (handler == NULL)
			return B_NO_MEMORY;

		_handler = handler;
		return B_OK;
	}

	/**
	 * @brief Apply a sub-attribute to the in-flight entry.
	 *
	 * Dispatches over the entry-relevant attribute IDs (file type,
	 * permissions, timestamps, symlink path, nested directory entry, file
	 * attribute, and data). For new directory entries and file attributes
	 * the entry's @c HandleEntry callback is fired before delegating to
	 * the appropriate child handler.
	 *
	 * @param context  Reader context.
	 * @param id       Attribute ID being handled.
	 * @param value    Decoded attribute value.
	 * @param _handler Optional output for a child handler.
	 * @return B_OK on success, otherwise the first error from a callback or
	 *         child handler.
	 */
	virtual status_t HandleAttribute(AttributeHandlerContext* context,
		uint8 id, const AttributeValue& value, AttributeHandler** _handler)
	{
		switch (id) {
			case B_HPKG_ATTRIBUTE_ID_DIRECTORY_ENTRY:
			{
				status_t error = _Notify(context);
				if (error != B_OK)
					return error;

//TRACE("%*sentry \"%s\"\n", fLevel * 2, "", value.string);
				if (_handler != NULL) {
					return EntryAttributeHandler::Create(context, &fEntry,
						value.string, *_handler);
				}
				return B_OK;
			}

			case B_HPKG_ATTRIBUTE_ID_FILE_TYPE:
				return _SetFileType(context, value.unsignedInt);

			case B_HPKG_ATTRIBUTE_ID_FILE_PERMISSIONS:
				fEntry.SetPermissions(value.unsignedInt);
				return B_OK;

			case B_HPKG_ATTRIBUTE_ID_FILE_USER:
			case B_HPKG_ATTRIBUTE_ID_FILE_GROUP:
				// TODO:...
				break;

			case B_HPKG_ATTRIBUTE_ID_FILE_ATIME:
				fEntry.SetAccessTime(value.unsignedInt);
				return B_OK;

			case B_HPKG_ATTRIBUTE_ID_FILE_MTIME:
				fEntry.SetModifiedTime(value.unsignedInt);
				return B_OK;

			case B_HPKG_ATTRIBUTE_ID_FILE_CRTIME:
				fEntry.SetCreationTime(value.unsignedInt);
				return B_OK;

			case B_HPKG_ATTRIBUTE_ID_FILE_ATIME_NANOS:
				fEntry.SetAccessTimeNanos(value.unsignedInt);
				return B_OK;

			case B_HPKG_ATTRIBUTE_ID_FILE_MTIME_NANOS:
				fEntry.SetModifiedTimeNanos(value.unsignedInt);
				return B_OK;

			case B_HPKG_ATTRIBUTE_ID_FILE_CRTIM_NANOS:
				fEntry.SetCreationTimeNanos(value.unsignedInt);
				return B_OK;

			case B_HPKG_ATTRIBUTE_ID_FILE_ATTRIBUTE:
			{
				status_t error = _Notify(context);
				if (error != B_OK)
					return error;

				if (_handler != NULL) {
					*_handler = new(std::nothrow) AttributeAttributeHandler(
						&fEntry, value.string);
					if (*_handler == NULL)
						return B_NO_MEMORY;
					return B_OK;
				} else {
					BPackageEntryAttribute attribute(value.string);
					return context->packageContentHandler->HandleEntryAttribute(
						&fEntry, &attribute);
				}
			}

			case B_HPKG_ATTRIBUTE_ID_DATA:
				if (_handler != NULL) {
					return DataAttributeHandler::Create(context, &fEntry.Data(),
						value, *_handler);
				}
				return DataAttributeHandler::InitData(context, &fEntry.Data(),
					value);

			case B_HPKG_ATTRIBUTE_ID_SYMLINK_PATH:
				fEntry.SetSymlinkPath(value.string);
				return B_OK;
		}

		return AttributeHandler::HandleAttribute(context, id, value, _handler);
	}

	/**
	 * @brief Emit @c HandleEntryDone for the entry and self-destruct.
	 *
	 * Ensures @c HandleEntry has fired (in case the entry had no
	 * sub-attributes that triggered it earlier), then notifies the content
	 * handler that the entry is fully consumed.
	 *
	 * @param context Reader context.
	 * @return B_OK on success, otherwise the content handler's error.
	 */
	virtual status_t Delete(AttributeHandlerContext* context)
	{
		// notify if not done yet
		status_t error = _Notify(context);

		// notify done
		if (error == B_OK)
			error = context->packageContentHandler->HandleEntryDone(&fEntry);
		else
			context->packageContentHandler->HandleEntryDone(&fEntry);

		delete this;
		return error;
	}

private:
	/**
	 * @brief Fire @c HandleEntry exactly once for this entry.
	 *
	 * Subsequent calls are no-ops.
	 *
	 * @param context Reader context.
	 * @return B_OK on success or when already notified, otherwise the
	 *         content handler's error.
	 */
	status_t _Notify(AttributeHandlerContext* context)
	{
		if (fNotified)
			return B_OK;

		fNotified = true;
		return context->packageContentHandler->HandleEntry(&fEntry);
	}

	/**
	 * @brief Translate a v1 file-type code into POSIX type and default
	 *        permissions on the entry.
	 *
	 * Recognises @c B_HPKG_FILE_TYPE_FILE, @c B_HPKG_FILE_TYPE_DIRECTORY,
	 * and @c B_HPKG_FILE_TYPE_SYMLINK; reports invalid types as
	 * @c B_BAD_DATA.
	 *
	 * @param context  Reader context.
	 * @param fileType File-type code from the attribute payload.
	 * @retval B_OK         Type accepted.
	 * @retval B_BAD_DATA   Unrecognised file type.
	 */
	status_t _SetFileType(AttributeHandlerContext* context, uint64 fileType)
	{
		switch (fileType) {
			case B_HPKG_FILE_TYPE_FILE:
				fEntry.SetType(S_IFREG);
				fEntry.SetPermissions(B_HPKG_DEFAULT_FILE_PERMISSIONS);
				break;

			case B_HPKG_FILE_TYPE_DIRECTORY:
				fEntry.SetType(S_IFDIR);
				fEntry.SetPermissions(B_HPKG_DEFAULT_DIRECTORY_PERMISSIONS);
				break;

			case B_HPKG_FILE_TYPE_SYMLINK:
				fEntry.SetType(S_IFLNK);
				fEntry.SetPermissions(B_HPKG_DEFAULT_SYMLINK_PERMISSIONS);
				break;

			default:
				context->errorOutput->PrintError("Error: Invalid file type for "
					"package entry (%llu)\n", fileType);
				return B_BAD_DATA;
		}
		return B_OK;
	}

private:
	/** @brief Entry being assembled by this handler. */
	BPackageEntry	fEntry;
	/** @brief Whether @c HandleEntry has already been emitted. */
	bool			fNotified;
};


// #pragma mark - RootAttributeHandler


/**
 * @brief Top-level handler for a v1 package's TOC root.
 *
 * Specialises PackageAttributeHandler to recognise top-level directory
 * entries; non-entry attributes are delegated back to the package
 * attribute handler base class.
 */
struct PackageReaderImpl::RootAttributeHandler : PackageAttributeHandler {
	typedef PackageAttributeHandler inherited;

	/**
	 * @brief Dispatch a top-level attribute to either an EntryAttributeHandler
	 *        or the package-attribute base class.
	 *
	 * @param context  Reader context.
	 * @param id       Attribute ID being handled.
	 * @param value    Decoded attribute value.
	 * @param _handler Optional output for a child handler.
	 * @return B_OK on success, otherwise an error from the child handler or
	 *         the base class.
	 */
	virtual status_t HandleAttribute(AttributeHandlerContext* context,
		uint8 id, const AttributeValue& value, AttributeHandler** _handler)
	{
		if (id == B_HPKG_ATTRIBUTE_ID_DIRECTORY_ENTRY) {
			if (_handler != NULL) {
				return EntryAttributeHandler::Create(context, NULL,
					value.string, *_handler);
			}
			return B_OK;
		}

		return inherited::HandleAttribute(context, id, value, _handler);
	}
};


// #pragma mark - PackageReaderImpl


/**
 * @brief Construct an empty v1 package reader bound to an error output.
 * @param errorOutput Sink for diagnostic messages emitted while reading.
 */
PackageReaderImpl::PackageReaderImpl(BErrorOutput* errorOutput)
	:
	inherited(errorOutput),
	fTOCSection("TOC")
{
}


/**
 * @brief Destroy the reader and release any sections still held open.
 */
PackageReaderImpl::~PackageReaderImpl()
{
}


/**
 * @brief Open a v1 package file by path and prepare it for parsing.
 *
 * Opens the named file read-only and forwards to the descriptor-based
 * overload, transferring ownership of the descriptor.
 *
 * @param fileName Path to the .hpkg file.
 * @return B_OK on success, an errno-mapped error on open failure, or an
 *         init error from the file-descriptor overload.
 */
status_t
PackageReaderImpl::Init(const char* fileName)
{
	// open file
	int fd = open(fileName, O_RDONLY);
	if (fd < 0) {
		ErrorOutput()->PrintError("Error: Failed to open package file \"%s\": "
			"%s\n", fileName, strerror(errno));
		return errno;
	}

	return Init(fd, true);
}


/**
 * @brief Initialise the reader from an already-open file descriptor.
 *
 * Reads and validates the v1 hpkg_header, including the magic, version,
 * total file size, and section descriptors for both the TOC and the
 * package attributes section. Loads both sections fully into memory
 * (subject to size sanity limits) and parses their string tables so that
 * subsequent parses can resolve string references quickly.
 *
 * @param fd     File descriptor opened for reading on the package file.
 * @param keepFD If @c true, the reader takes ownership and will close the
 *               descriptor when destroyed.
 * @retval B_OK             Reader is ready to parse content.
 * @retval B_BAD_DATA       Header values fail integrity checks.
 * @retval B_MISMATCHED_VALUES Package version is not supported.
 * @retval B_UNSUPPORTED    A section size exceeds the reader's limits.
 * @retval B_NO_MEMORY      Section buffers could not be allocated.
 * @return Otherwise an error from the underlying I/O.
 */
status_t
PackageReaderImpl::Init(int fd, bool keepFD)
{
	status_t error = inherited::Init(fd, keepFD);
	if (error != B_OK)
		return error;

	// stat it
	struct stat st;
	if (fstat(FD(), &st) < 0) {
		ErrorOutput()->PrintError("Error: Failed to access package file: %s\n",
			strerror(errno));
		return errno;
	}

	// read the header
	hpkg_header header;
	if ((error = ReadBuffer(0, &header, sizeof(header))) != B_OK)
		return error;

	// check the header

	// magic
	if (B_BENDIAN_TO_HOST_INT32(header.magic) != B_HPKG_MAGIC) {
		ErrorOutput()->PrintError("Error: Invalid package file: Invalid "
			"magic\n");
		return B_BAD_DATA;
	}

	// version
	if (B_BENDIAN_TO_HOST_INT16(header.version) != B_HPKG_VERSION) {
		ErrorOutput()->PrintError("Error: Invalid/unsupported package file "
			"version (%d)\n", B_BENDIAN_TO_HOST_INT16(header.version));
		return B_MISMATCHED_VALUES;
	}

	// header size
	fHeapOffset = B_BENDIAN_TO_HOST_INT16(header.header_size);
	if ((size_t)fHeapOffset < sizeof(hpkg_header)) {
		ErrorOutput()->PrintError("Error: Invalid package file: Invalid header "
			"size (%llu)\n", fHeapOffset);
		return B_BAD_DATA;
	}

	// total size
	fTotalSize = B_BENDIAN_TO_HOST_INT64(header.total_size);
	if (fTotalSize != (uint64)st.st_size) {
		ErrorOutput()->PrintError("Error: Invalid package file: Total size in "
			"header (%llu) doesn't agree with total file size (%lld)\n",
			fTotalSize, st.st_size);
		return B_BAD_DATA;
	}

	// package attributes length and compression
	fPackageAttributesSection.compression
		= B_BENDIAN_TO_HOST_INT32(header.attributes_compression);
	fPackageAttributesSection.compressedLength
		= B_BENDIAN_TO_HOST_INT32(header.attributes_length_compressed);
	fPackageAttributesSection.uncompressedLength
		= B_BENDIAN_TO_HOST_INT32(header.attributes_length_uncompressed);
	fPackageAttributesSection.stringsLength
		= B_BENDIAN_TO_HOST_INT32(header.attributes_strings_length);
	fPackageAttributesSection.stringsCount
		= B_BENDIAN_TO_HOST_INT32(header.attributes_strings_count);

	if (const char* errorString = CheckCompression(
		fPackageAttributesSection)) {
		ErrorOutput()->PrintError("Error: Invalid package file: package "
			"attributes section: %s\n", errorString);
		return B_BAD_DATA;
	}

	// TOC length and compression
	fTOCSection.compression = B_BENDIAN_TO_HOST_INT32(header.toc_compression);
	fTOCSection.compressedLength
		= B_BENDIAN_TO_HOST_INT64(header.toc_length_compressed);
	fTOCSection.uncompressedLength
		= B_BENDIAN_TO_HOST_INT64(header.toc_length_uncompressed);

	if (const char* errorString = CheckCompression(fTOCSection)) {
		ErrorOutput()->PrintError("Error: Invalid package file: TOC section: "
			"%s\n", errorString);
		return B_BAD_DATA;
	}

	// TOC subsections
	fTOCSection.stringsLength
		= B_BENDIAN_TO_HOST_INT64(header.toc_strings_length);
	fTOCSection.stringsCount
		= B_BENDIAN_TO_HOST_INT64(header.toc_strings_count);

	if (fTOCSection.stringsLength > fTOCSection.uncompressedLength
		|| fTOCSection.stringsCount > fTOCSection.stringsLength) {
		ErrorOutput()->PrintError("Error: Invalid package file: Invalid TOC "
			"subsections description\n");
		return B_BAD_DATA;
	}

	// check whether the sections fit together
	if (fPackageAttributesSection.compressedLength > fTotalSize
		|| fTOCSection.compressedLength
			> fTotalSize - fPackageAttributesSection.compressedLength
		|| fHeapOffset
			> fTotalSize - fPackageAttributesSection.compressedLength
				- fTOCSection.compressedLength) {
		ErrorOutput()->PrintError("Error: Invalid package file: The sum of the "
			"sections sizes is greater than the package size\n");
		return B_BAD_DATA;
	}

	fPackageAttributesSection.offset
		= fTotalSize - fPackageAttributesSection.compressedLength;
	fTOCSection.offset = fPackageAttributesSection.offset
		- fTOCSection.compressedLength;
	fHeapSize = fTOCSection.offset - fHeapOffset;

	// TOC size sanity check
	if (fTOCSection.uncompressedLength > kMaxTOCSize) {
		ErrorOutput()->PrintError("Error: Package file TOC section size "
			"is %llu bytes. This is beyond the reader's sanity limit\n",
			fTOCSection.uncompressedLength);
		return B_UNSUPPORTED;
	}

	// package attributes size sanity check
	if (fPackageAttributesSection.uncompressedLength
			> kMaxPackageAttributesSize) {
		ErrorOutput()->PrintError(
			"Error: Package file package attributes section size "
			"is %llu bytes. This is beyond the reader's sanity limit\n",
			fPackageAttributesSection.uncompressedLength);
		return B_UNSUPPORTED;
	}

	// read in the complete TOC
	fTOCSection.data
		= new(std::nothrow) uint8[fTOCSection.uncompressedLength];
	if (fTOCSection.data == NULL) {
		ErrorOutput()->PrintError("Error: Out of memory!\n");
		return B_NO_MEMORY;
	}
	error = ReadCompressedBuffer(fTOCSection);
	if (error != B_OK)
		return error;

	// read in the complete package attributes section
	fPackageAttributesSection.data
		= new(std::nothrow) uint8[fPackageAttributesSection.uncompressedLength];
	if (fPackageAttributesSection.data == NULL) {
		ErrorOutput()->PrintError("Error: Out of memory!\n");
		return B_NO_MEMORY;
	}
	error = ReadCompressedBuffer(fPackageAttributesSection);
	if (error != B_OK)
		return error;

	// start parsing the TOC
	fTOCSection.currentOffset = 0;
	SetCurrentSection(&fTOCSection);

	// strings
	error = ParseStrings();
	if (error != B_OK)
		return error;

	// parse strings from package attributes section
	fPackageAttributesSection.currentOffset = 0;
	SetCurrentSection(&fPackageAttributesSection);

	// strings
	error = ParseStrings();
	if (error != B_OK)
		return error;

	SetCurrentSection(NULL);

	return B_OK;
}


/**
 * @brief Parse the package's content with a high-level
 *        BPackageContentHandler.
 *
 * Walks the package attributes section first, then the TOC, dispatching
 * callbacks to the supplied handler for entries, attributes, and
 * package-info attributes.
 *
 * @param contentHandler Caller-supplied content handler.
 * @return B_OK on success, otherwise the first error encountered.
 */
status_t
PackageReaderImpl::ParseContent(BPackageContentHandler* contentHandler)
{
	AttributeHandlerContext context(ErrorOutput(), contentHandler,
		B_HPKG_SECTION_PACKAGE_ATTRIBUTES);
	RootAttributeHandler rootAttributeHandler;

	status_t error
		= ParsePackageAttributesSection(&context, &rootAttributeHandler);

	if (error == B_OK) {
		context.section = B_HPKG_SECTION_PACKAGE_TOC;
		error = _ParseTOC(&context, &rootAttributeHandler);
	}

	return error;
}


/**
 * @brief Parse the package's content with a low-level
 *        BLowLevelPackageContentHandler.
 *
 * Provides raw attribute callbacks without higher-level entry/data
 * reconstruction. Useful for tools that want to inspect the on-disk
 * attribute tree directly.
 *
 * @param contentHandler Caller-supplied low-level content handler.
 * @return B_OK on success, otherwise the first error encountered.
 */
status_t
PackageReaderImpl::ParseContent(BLowLevelPackageContentHandler* contentHandler)
{
	AttributeHandlerContext context(ErrorOutput(), contentHandler,
		B_HPKG_SECTION_PACKAGE_ATTRIBUTES);
	LowLevelAttributeHandler rootAttributeHandler;

	status_t error
		= ParsePackageAttributesSection(&context, &rootAttributeHandler);

	if (error == B_OK) {
		context.section = B_HPKG_SECTION_PACKAGE_TOC;
		error = _ParseTOC(&context, &rootAttributeHandler);
	}

	return error;
}


/**
 * @brief Walk the TOC attribute tree using the supplied root handler.
 *
 * Sets the current section to TOC, configures the heap range on @a context,
 * pushes @a rootAttributeHandler onto the handler stack, and runs
 * ParseAttributeTree(). On error the handler stack is unwound (calling
 * @c Delete on every handler except the caller-owned root) and the
 * context is notified.
 *
 * @param context              Reader context to drive.
 * @param rootAttributeHandler Caller-owned root handler.
 * @retval B_OK         The TOC was parsed completely.
 * @retval B_BAD_DATA   Bytes remained after parsing finished.
 * @return Otherwise an error from ParseAttributeTree().
 */
status_t
PackageReaderImpl::_ParseTOC(AttributeHandlerContext* context,
	AttributeHandler* rootAttributeHandler)
{
	// parse the TOC
	fTOCSection.currentOffset = fTOCSection.stringsLength;
	SetCurrentSection(&fTOCSection);

	// prepare attribute handler context
	context->heapOffset = fHeapOffset;
	context->heapSize = fHeapSize;

	// init the attribute handler stack
	rootAttributeHandler->SetLevel(0);
	ClearAttributeHandlerStack();
	PushAttributeHandler(rootAttributeHandler);

	bool sectionHandled;
	status_t error = ParseAttributeTree(context, sectionHandled);
	if (error == B_OK && sectionHandled) {
		if (fTOCSection.currentOffset < fTOCSection.uncompressedLength) {
			ErrorOutput()->PrintError("Error: %llu excess byte(s) in TOC "
				"section\n",
				fTOCSection.uncompressedLength - fTOCSection.currentOffset);
			error = B_BAD_DATA;
		}
	}

	// clean up on error
	if (error != B_OK) {
		context->ErrorOccurred();
		while (AttributeHandler* handler = PopAttributeHandler()) {
			if (handler != rootAttributeHandler)
				handler->Delete(context);
		}
		return error;
	}

	return B_OK;
}


/**
 * @brief Decode a v1 attribute value of the given type and encoding from
 *        the current section.
 *
 * Adds handling for raw payloads on top of the base class: heap-encoded
 * raw values store an offset/size pair into the heap, and inline raw
 * values are read directly from the TOC stream. Other types are forwarded
 * to the base class.
 *
 * @param type     Attribute value type.
 * @param encoding Encoding flag carried in the attribute header.
 * @param _value   Output: decoded attribute value.
 * @retval B_OK         Value decoded.
 * @retval B_BAD_DATA   Invalid raw encoding, oversized inline data, or
 *                      out-of-range heap reference.
 * @return Otherwise the base class result.
 */
status_t
PackageReaderImpl::ReadAttributeValue(uint8 type, uint8 encoding,
	AttributeValue& _value)
{
	switch (type) {
		case B_HPKG_ATTRIBUTE_TYPE_RAW:
		{
			uint64 size;
			status_t error = ReadUnsignedLEB128(size);
			if (error != B_OK)
				return error;

			if (encoding == B_HPKG_ATTRIBUTE_ENCODING_RAW_HEAP) {
				uint64 offset;
				error = ReadUnsignedLEB128(offset);
				if (error != B_OK)
					return error;

				if (offset > fHeapSize || size > fHeapSize - offset) {
					ErrorOutput()->PrintError("Error: Invalid %s section: "
						"invalid data reference\n", CurrentSection()->name);
					return B_BAD_DATA;
				}

				_value.SetToData(size, fHeapOffset + offset);
			} else if (encoding == B_HPKG_ATTRIBUTE_ENCODING_RAW_INLINE) {
				if (size > B_HPKG_MAX_INLINE_DATA_SIZE) {
					ErrorOutput()->PrintError("Error: Invalid %s section: "
						"inline data too long\n", CurrentSection()->name);
					return B_BAD_DATA;
				}

				const void* buffer;
				error = _GetTOCBuffer(size, buffer);
				if (error != B_OK)
					return error;
				_value.SetToData(size, buffer);
			} else {
				ErrorOutput()->PrintError("Error: Invalid %s section: invalid "
					"raw encoding (%u)\n", CurrentSection()->name, encoding);
				return B_BAD_DATA;
			}

			return B_OK;
		}

		default:
			return inherited::ReadAttributeValue(type, encoding, _value);
	}
}


/**
 * @brief Carve @a size bytes out of the in-memory TOC at the current
 *        cursor.
 *
 * Used by inline raw attribute values to obtain a pointer to their bytes
 * without copying. Advances the TOC cursor by @a size.
 *
 * @param size    Number of bytes the caller wants to consume.
 * @param _buffer Output: pointer into the TOC buffer (valid for the
 *                lifetime of the reader).
 * @retval B_OK         A buffer of the requested size was returned.
 * @retval B_BAD_DATA   The TOC does not have @a size bytes remaining.
 */
status_t
PackageReaderImpl::_GetTOCBuffer(size_t size, const void*& _buffer)
{
	if (size > fTOCSection.uncompressedLength - fTOCSection.currentOffset) {
		ErrorOutput()->PrintError("_GetTOCBuffer(%lu): read beyond TOC end\n",
			size);
		return B_BAD_DATA;
	}

	_buffer = fTOCSection.data + fTOCSection.currentOffset;
	fTOCSection.currentOffset += size;
	return B_OK;
}


}	// namespace BPrivate

}	// namespace V1

}	// namespace BHPKG

}	// namespace BPackageKit
