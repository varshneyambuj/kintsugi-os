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
 * @file PackageReaderImpl.cpp
 * @brief Internal HPKG package parser implementation.
 *
 * PackageReaderImpl drives the two-phase parse of an HPKG file: the package
 * attributes section and the TOC.  It provides the attribute-handler stack
 * machinery (AttributeAttributeHandler, EntryAttributeHandler,
 * RootAttributeHandler) that translate the raw binary attribute tree into
 * BPackageEntry / BPackageEntryAttribute callbacks delivered to the content
 * handler supplied by the caller.
 *
 * @see BPackageReader, BPackageContentHandler, BLowLevelPackageContentHandler
 */


#include <package/hpkg/PackageReaderImpl.h>

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

#include <FdIO.h>

#include <package/hpkg/HPKGDefsPrivate.h>

#include <package/hpkg/PackageData.h>
#include <package/hpkg/PackageEntry.h>
#include <package/hpkg/PackageEntryAttribute.h>


namespace BPackageKit {

namespace BHPKG {

namespace BPrivate {


//#define TRACE(format...)	printf(format)
#define TRACE(format...)	do {} while (false)


// maximum TOC size we support reading
static const size_t kMaxTOCSize					= 64 * 1024 * 1024;

// maximum package attributes size we support reading
static const size_t kMaxPackageAttributesSize	= 1 * 1024 * 1024;


/**
 * @brief Populate a BPackageData from a raw attribute value encoding.
 *
 * Selects either inline or heap encoding based on the value's encoding field
 * and calls the appropriate BPackageData::SetData() overload.
 *
 * @param value The raw attribute value containing the encoding and data fields.
 * @param data  Output BPackageData to populate.
 * @return B_OK unconditionally.
 */
static status_t
set_package_data_from_attribute_value(const BPackageAttributeValue& value,
	BPackageData& data)
{
	if (value.encoding == B_HPKG_ATTRIBUTE_ENCODING_RAW_INLINE)
		data.SetData(value.data.size, value.data.raw);
	else
		data.SetData(value.data.size, value.data.offset);
	return B_OK;
}


// #pragma mark - AttributeAttributeHandler


/**
 * @brief Attribute handler that processes sub-attributes of a file attribute.
 *
 * Handles B_HPKG_ATTRIBUTE_ID_DATA (to set the attribute's data payload) and
 * B_HPKG_ATTRIBUTE_ID_FILE_ATTRIBUTE_TYPE (to record the BeOS attribute type).
 * On deletion it delivers the completed BPackageEntryAttribute to the content
 * handler via HandleEntryAttribute().
 */
struct PackageReaderImpl::AttributeAttributeHandler : AttributeHandler {
	/**
	 * @brief Construct the handler for a named extended attribute on \a entry.
	 *
	 * @param entry The package entry that owns this attribute.
	 * @param name  The name of the extended attribute.
	 */
	AttributeAttributeHandler(BPackageEntry* entry, const char* name)
		:
		fEntry(entry),
		fAttribute(name)
	{
	}

	/**
	 * @brief Handle a sub-attribute of the current file attribute.
	 *
	 * Recognises DATA and FILE_ATTRIBUTE_TYPE sub-attributes and delegates
	 * everything else to the base class.
	 *
	 * @param context  Current parse context.
	 * @param id       Attribute ID.
	 * @param value    Decoded attribute value.
	 * @param _handler Optional output for a child handler to push.
	 * @return B_OK on success, or any error from the base class.
	 */
	virtual status_t HandleAttribute(AttributeHandlerContext* context,
		uint8 id, const AttributeValue& value, AttributeHandler** _handler)
	{
		switch (id) {
			case B_HPKG_ATTRIBUTE_ID_DATA:
				return set_package_data_from_attribute_value(value,
					fAttribute.Data());

			case B_HPKG_ATTRIBUTE_ID_FILE_ATTRIBUTE_TYPE:
				fAttribute.SetType(value.unsignedInt);
				return B_OK;
		}

		return AttributeHandler::HandleAttribute(context, id, value, _handler);
	}

	/**
	 * @brief Deliver the completed attribute to the content handler and free this object.
	 *
	 * @param context Current parse context.
	 * @return The status_t returned by HandleEntryAttribute().
	 */
	virtual status_t Delete(AttributeHandlerContext* context)
	{
		status_t error = context->packageContentHandler->HandleEntryAttribute(
			fEntry, &fAttribute);

		AttributeHandler::Delete(context);
		return error;
	}

private:
	BPackageEntry*			fEntry;
	BPackageEntryAttribute	fAttribute;
};


// #pragma mark - EntryAttributeHandler


/**
 * @brief Attribute handler that accumulates metadata for a single package entry.
 *
 * Processes all known entry-level attribute IDs (file type, permissions,
 * timestamps, symlink path, data, and nested directory entries or extended
 * attributes).  Sends HandleEntry() to the content handler on first child
 * access and HandleEntryDone() on deletion.
 */
struct PackageReaderImpl::EntryAttributeHandler : AttributeHandler {
	/**
	 * @brief Construct an entry handler inside the given context's allocator.
	 *
	 * @param context     Current parse context (used for the allocator).
	 * @param parentEntry The parent directory entry, or NULL for the root.
	 * @param name        Entry name string (borrowed from TOC buffer).
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
	 * @brief Validate the entry name and allocate an EntryAttributeHandler.
	 *
	 * Rejects empty names, ".", "..", and names containing '/'.
	 *
	 * @param context      Current parse context.
	 * @param parentEntry  Parent directory entry.
	 * @param name         Candidate entry name.
	 * @param _handler     Output parameter for the new handler.
	 * @return B_OK on success, B_BAD_DATA for an invalid name, B_NO_MEMORY
	 *         if allocation fails.
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
		EntryAttributeHandler* handler = new(context)
			EntryAttributeHandler(context, parentEntry, name);
		if (handler == NULL)
			return B_NO_MEMORY;

		_handler = handler;
		return B_OK;
	}

	/**
	 * @brief Dispatch a single attribute for the current entry.
	 *
	 * Handles all known B_HPKG_ATTRIBUTE_ID_* values for files, directories,
	 * and symlinks.  Pushes child handlers for nested directory entries and
	 * extended attributes.
	 *
	 * @param context  Current parse context.
	 * @param id       Attribute ID.
	 * @param value    Decoded attribute value.
	 * @param _handler Optional output for a child handler.
	 * @return B_OK on success, or a relevant error code.
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
					*_handler = new(context) AttributeAttributeHandler(
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
				return set_package_data_from_attribute_value(value,
					fEntry.Data());

			case B_HPKG_ATTRIBUTE_ID_SYMLINK_PATH:
				fEntry.SetSymlinkPath(value.string);
				return B_OK;
		}

		return AttributeHandler::HandleAttribute(context, id, value, _handler);
	}

	/**
	 * @brief Notify the content handler that the entry is complete, then delete this handler.
	 *
	 * Sends HandleEntry() if not already sent, then always sends HandleEntryDone().
	 *
	 * @param context Current parse context.
	 * @return B_OK on success, or any error from HandleEntry() / HandleEntryDone().
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

		AttributeHandler::Delete(context);
		return error;
	}

private:
	/**
	 * @brief Send HandleEntry() to the content handler the first time it is needed.
	 *
	 * @param context Current parse context.
	 * @return B_OK if already notified or on success; any error from HandleEntry().
	 */
	status_t _Notify(AttributeHandlerContext* context)
	{
		if (fNotified)
			return B_OK;

		fNotified = true;
		return context->packageContentHandler->HandleEntry(&fEntry);
	}

	/**
	 * @brief Apply a file-type constant to the entry, setting mode and default permissions.
	 *
	 * @param context  Current parse context (used to print errors).
	 * @param fileType One of B_HPKG_FILE_TYPE_* constants.
	 * @return B_OK on success, B_BAD_DATA for an unrecognised file type.
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
	BPackageEntry	fEntry;
	bool			fNotified;
};


// #pragma mark - RootAttributeHandler


/**
 * @brief Top-level attribute handler for the TOC section.
 *
 * Handles B_HPKG_ATTRIBUTE_ID_DIRECTORY_ENTRY at the root level (i.e. the
 * top-level entries of the package archive) and delegates all other attributes
 * to PackageAttributeHandler.
 */
struct PackageReaderImpl::RootAttributeHandler : PackageAttributeHandler {
	typedef PackageAttributeHandler inherited;

	/**
	 * @brief Handle a root-level TOC attribute.
	 *
	 * Creates an EntryAttributeHandler for directory-entry attributes;
	 * delegates everything else to PackageAttributeHandler.
	 *
	 * @param context  Current parse context.
	 * @param id       Attribute ID.
	 * @param value    Decoded attribute value.
	 * @param _handler Optional output for a child handler.
	 * @return B_OK on success, or any error from EntryAttributeHandler::Create()
	 *         or the inherited handler.
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
 * @brief Construct the package reader implementation.
 *
 * @param errorOutput Diagnostic output channel; must remain valid for the
 *                    lifetime of this object.
 */
PackageReaderImpl::PackageReaderImpl(BErrorOutput* errorOutput)
	:
	inherited("package", errorOutput),
	fTOCSection("TOC")
{
}


/**
 * @brief Destroy the package reader implementation.
 */
PackageReaderImpl::~PackageReaderImpl()
{
}


/**
 * @brief Open and initialise the reader from a package file path.
 *
 * @param fileName Path to the HPKG file to open.
 * @param flags    Reader flags (reserved, pass 0).
 * @return B_OK on success, or any error from open() or the fd-based Init().
 */
status_t
PackageReaderImpl::Init(const char* fileName, uint32 flags)
{
	// open file
	int fd = open(fileName, O_RDONLY);
	if (fd < 0) {
		ErrorOutput()->PrintError("Error: Failed to open package file \"%s\": "
			"%s\n", fileName, strerror(errno));
		return errno;
	}

	return Init(fd, true, flags);
}


/**
 * @brief Initialise the reader from an open file descriptor.
 *
 * Wraps \a fd in a BFdIO and delegates to the BPositionIO-based Init().
 *
 * @param fd      Open file descriptor for the HPKG file.
 * @param keepFD  If true, \a fd is owned by this reader and closed on
 *                destruction; if false, the caller retains ownership.
 * @param flags   Reader flags (reserved, pass 0).
 * @return B_OK on success, B_NO_MEMORY if BFdIO allocation fails, or any
 *         error from the BPositionIO-based Init().
 */
status_t
PackageReaderImpl::Init(int fd, bool keepFD, uint32 flags)
{
	BFdIO* file = new(std::nothrow) BFdIO(fd, keepFD);
	if (file == NULL) {
		if (keepFD && fd >= 0)
			close(fd);
		return B_NO_MEMORY;
	}

	return Init(file, true, flags);
}


/**
 * @brief Initialise the reader from a BPositionIO and parse the HPKG header.
 *
 * Validates the magic number and version, then reads and validates the
 * package attributes section and TOC section headers from the HPKG header
 * structure.
 *
 * @param file      Positioned I/O object for the package file.
 * @param keepFile  If true the reader takes ownership of \a file.
 * @param flags     Reader flags (reserved, pass 0).
 * @param _header   Optional output pointer to receive a copy of the raw
 *                  hpkg_header; may be NULL.
 * @return B_OK on success, or any error from inherited::Init() or InitSection().
 */
status_t
PackageReaderImpl::Init(BPositionIO* file, bool keepFile, uint32 flags,
	hpkg_header* _header)
{
	hpkg_header header;
	status_t error = inherited::Init<hpkg_header, B_HPKG_MAGIC, B_HPKG_VERSION,
		B_HPKG_MINOR_VERSION>(file, keepFile, header, flags);
	if (error != B_OK)
		return error;
	fHeapSize = UncompressedHeapSize();

	// init package attributes section
	error = InitSection(fPackageAttributesSection, fHeapSize,
		B_BENDIAN_TO_HOST_INT32(header.attributes_length),
		kMaxPackageAttributesSize,
		B_BENDIAN_TO_HOST_INT32(header.attributes_strings_length),
		B_BENDIAN_TO_HOST_INT32(header.attributes_strings_count));
	if (error != B_OK)
		return error;

	// init TOC section
	error = InitSection(fTOCSection, fPackageAttributesSection.offset,
		B_BENDIAN_TO_HOST_INT64(header.toc_length), kMaxTOCSize,
		B_BENDIAN_TO_HOST_INT64(header.toc_strings_length),
		B_BENDIAN_TO_HOST_INT64(header.toc_strings_count));
	if (error != B_OK)
		return error;

	if (_header != NULL)
		*_header = header;

	return B_OK;
}


/**
 * @brief Parse the package and deliver high-level content to the handler.
 *
 * Prepares both sections, then parses the package attributes section followed
 * by the TOC, delivering entry and attribute callbacks to \a contentHandler.
 *
 * @param contentHandler High-level handler to receive parsed content.
 * @return B_OK on success, or any parse error.
 */
status_t
PackageReaderImpl::ParseContent(BPackageContentHandler* contentHandler)
{
	status_t error = _PrepareSections();
	if (error != B_OK)
		return error;

	AttributeHandlerContext context(ErrorOutput(), contentHandler,
		B_HPKG_SECTION_PACKAGE_ATTRIBUTES,
		MinorFormatVersion() > B_HPKG_MINOR_VERSION);
	RootAttributeHandler rootAttributeHandler;

	error = ParsePackageAttributesSection(&context, &rootAttributeHandler);

	if (error == B_OK) {
		context.section = B_HPKG_SECTION_PACKAGE_TOC;
		error = _ParseTOC(&context, &rootAttributeHandler);
	}

	return error;
}


/**
 * @brief Parse the package and deliver raw attributes to the low-level handler.
 *
 * Prepares both sections, then parses the package attributes section followed
 * by the TOC, delivering raw attribute callbacks to \a contentHandler.
 *
 * @param contentHandler Low-level handler to receive raw attribute data.
 * @return B_OK on success, or any parse error.
 */
status_t
PackageReaderImpl::ParseContent(BLowLevelPackageContentHandler* contentHandler)
{
	status_t error = _PrepareSections();
	if (error != B_OK)
		return error;

	AttributeHandlerContext context(ErrorOutput(), contentHandler,
		B_HPKG_SECTION_PACKAGE_ATTRIBUTES,
		MinorFormatVersion() > B_HPKG_MINOR_VERSION);
	LowLevelAttributeHandler rootAttributeHandler;

	error = ParsePackageAttributesSection(&context, &rootAttributeHandler);

	if (error == B_OK) {
		context.section = B_HPKG_SECTION_PACKAGE_TOC;
		error = _ParseTOC(&context, &rootAttributeHandler);
	}

	return error;
}


/**
 * @brief Load both the TOC and package-attributes sections into memory.
 *
 * @return B_OK on success, or any error from PrepareSection().
 */
status_t
PackageReaderImpl::_PrepareSections()
{
	status_t error = PrepareSection(fTOCSection);
	if (error != B_OK)
		return error;

	error = PrepareSection(fPackageAttributesSection);
	if (error != B_OK)
		return error;

	return B_OK;
}


/**
 * @brief Parse the TOC attribute tree and dispatch it to the handler stack.
 *
 * Resets the TOC section's current offset to skip past the strings table,
 * pushes \a rootAttributeHandler onto the stack, and calls ParseAttributeTree().
 * On error all outstanding handlers are cleaned up.
 *
 * @param context              Current parse context.
 * @param rootAttributeHandler Root handler to push before parsing.
 * @return B_OK on success, B_BAD_DATA if there are excess bytes after the
 *         tree, or any parse error.
 */
status_t
PackageReaderImpl::_ParseTOC(AttributeHandlerContext* context,
	AttributeHandler* rootAttributeHandler)
{
	// parse the TOC
	fTOCSection.currentOffset = fTOCSection.stringsLength;
	SetCurrentSection(&fTOCSection);

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
 * @brief Decode a raw attribute value of the given type and encoding.
 *
 * Handles B_HPKG_ATTRIBUTE_TYPE_RAW by reading either a heap reference or
 * inline bytes from the TOC buffer.  All other types are delegated to the
 * inherited implementation.
 *
 * @param type     Attribute type constant (B_HPKG_ATTRIBUTE_TYPE_*).
 * @param encoding Encoding constant (B_HPKG_ATTRIBUTE_ENCODING_*).
 * @param _value   Output AttributeValue to populate.
 * @return B_OK on success, B_BAD_DATA for an invalid encoding or out-of-range
 *         reference, or any error from the inherited implementation.
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

				_value.SetToData(size, offset);
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
 * @brief Advance the TOC read pointer and return a pointer into the TOC buffer.
 *
 * Validates that \a size bytes remain in the TOC section before the current
 * position, then returns a pointer to the current position and advances it.
 *
 * @param size    Number of bytes to consume from the TOC buffer.
 * @param _buffer Output parameter that receives the pointer into the TOC buffer.
 * @return B_OK on success, B_BAD_DATA if the read would go past the end.
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

}	// namespace BHPKG

}	// namespace BPackageKit
