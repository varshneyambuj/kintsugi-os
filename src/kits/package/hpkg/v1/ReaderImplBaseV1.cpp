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
 * @file ReaderImplBaseV1.cpp
 * @brief Shared base class for v1 HPKG package reader implementations.
 *
 * ReaderImplBase (v1) implements the attribute-tree parsing engine used by
 * PackageReaderImpl (v1). It manages zlib-based heap decompression, per-section
 * string tables, and the AttributeHandler stack. This v1 variant differs from
 * the current ReaderImplBase in that it does not support Zstd compression and
 * uses a simpler section layout without the ignoreUnknownAttributes mechanism.
 *
 * @see PackageReaderImplV1, ReaderImplBase (current format)
 */


#include <package/hpkg/v1/ReaderImplBase.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <new>

#include <ByteOrder.h>
#include <DataIO.h>

#include <package/hpkg/ErrorOutput.h>

#include <AutoDeleter.h>
#include <package/hpkg/v1/HPKGDefsPrivate.h>
#include <ZlibCompressionAlgorithm.h>


namespace BPackageKit {

namespace BHPKG {

namespace V1 {

namespace BPrivate {


static const size_t kScratchBufferSize = 64 * 1024;


// #pragma mark - AttributeHandlerContext


/**
 * @brief Constructs an AttributeHandlerContext for a high-level content handler.
 *
 * @param errorOutput           Destination for diagnostic messages.
 * @param packageContentHandler High-level callback object to receive parsed data.
 * @param section               Which HPKG section is being parsed.
 */
ReaderImplBase::AttributeHandlerContext::AttributeHandlerContext(
	BErrorOutput* errorOutput, BPackageContentHandler* packageContentHandler,
	BHPKGPackageSectionID section)
	:
	errorOutput(errorOutput),
	packageContentHandler(packageContentHandler),
	hasLowLevelHandler(false),
	section(section)
{
}


/**
 * @brief Constructs an AttributeHandlerContext for a low-level content handler.
 *
 * @param errorOutput     Destination for diagnostic messages.
 * @param lowLevelHandler Low-level callback object to receive raw attribute data.
 * @param section         Which HPKG section is being parsed.
 */
ReaderImplBase::AttributeHandlerContext::AttributeHandlerContext(
	BErrorOutput* errorOutput, BLowLevelPackageContentHandler* lowLevelHandler,
	BHPKGPackageSectionID section)
	:
	errorOutput(errorOutput),
	lowLevelHandler(lowLevelHandler),
	hasLowLevelHandler(true),
	section(section)
{
}


/**
 * @brief Forwards an error-occurred notification to whichever handler is active.
 */
void
ReaderImplBase::AttributeHandlerContext::ErrorOccurred()
{
	if (hasLowLevelHandler)
		lowLevelHandler->HandleErrorOccurred();
	else
		packageContentHandler->HandleErrorOccurred();
}


// #pragma mark - AttributeHandler


/**
 * @brief Destroys the AttributeHandler.
 */
ReaderImplBase::AttributeHandler::~AttributeHandler()
{
}


/**
 * @brief Sets the nesting level of this handler in the attribute tree.
 *
 * @param level Zero-based depth in the attribute tree.
 */
void
ReaderImplBase::AttributeHandler::SetLevel(int level)
{
	fLevel = level;
}


/**
 * @brief Default attribute handler; ignores the attribute and returns B_OK.
 *
 * @param context  Parsing context.
 * @param id       Attribute ID.
 * @param value    Decoded attribute value.
 * @param _handler Output for an optional child handler; NULL if no children.
 * @return B_OK.
 */
status_t
ReaderImplBase::AttributeHandler::HandleAttribute(
	AttributeHandlerContext* context, uint8 id, const AttributeValue& value,
	AttributeHandler** _handler)
{
	return B_OK;
}


/**
 * @brief Destroys this handler and returns B_OK.
 *
 * @param context Parsing context; unused in this base implementation.
 * @return B_OK.
 */
status_t
ReaderImplBase::AttributeHandler::Delete(AttributeHandlerContext* context)
{
	delete this;
	return B_OK;
}


// #pragma mark - PackageVersionAttributeHandler


/**
 * @brief Constructs a handler for a v1 package version sub-attribute.
 *
 * @param packageInfoValue Reference to the enclosing attribute value.
 * @param versionData      Reference to the BPackageVersionData to populate.
 * @param notify           If true, Delete() will forward the completed value.
 */
ReaderImplBase::PackageVersionAttributeHandler::PackageVersionAttributeHandler(
	BPackageInfoAttributeValue& packageInfoValue,
	BPackageVersionData& versionData, bool notify)
	:
	fPackageInfoValue(packageInfoValue),
	fPackageVersionData(versionData),
	fNotify(notify)
{
}


/**
 * @brief Handles version component sub-attributes for the v1 format.
 *
 * @param context  Parsing context.
 * @param id       Attribute ID.
 * @param value    Decoded attribute value.
 * @param _handler Unused; versions have no further nesting in v1.
 * @return B_OK on success, or B_BAD_DATA for an unknown attribute ID.
 */
status_t
ReaderImplBase::PackageVersionAttributeHandler::HandleAttribute(
	AttributeHandlerContext* context, uint8 id, const AttributeValue& value,
	AttributeHandler** _handler)
{
	switch (id) {
		case B_HPKG_ATTRIBUTE_ID_PACKAGE_VERSION_MINOR:
			fPackageVersionData.minor = value.string;
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_VERSION_MICRO:
			fPackageVersionData.micro = value.string;
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_VERSION_PRE_RELEASE:
			fPackageVersionData.preRelease = value.string;
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_VERSION_REVISION:
			fPackageVersionData.revision = value.unsignedInt;
			break;

		default:
			context->errorOutput->PrintError("Error: Invalid package "
				"attribute section: unexpected package attribute id %d "
				"encountered when parsing package version\n", id);
			return B_BAD_DATA;
	}

	return B_OK;
}


/**
 * @brief Notifies the content handler (if @a fNotify) and deletes this handler.
 *
 * @param context Parsing context.
 * @return B_OK on success, or an error code from the content handler.
 */
status_t
ReaderImplBase::PackageVersionAttributeHandler::Delete(
	AttributeHandlerContext* context)
{
	status_t error = B_OK;
	if (fNotify) {
		fPackageInfoValue.attributeID = B_PACKAGE_INFO_VERSION;
		error = context->packageContentHandler->HandlePackageAttribute(
			fPackageInfoValue);
		fPackageInfoValue.Clear();
	}

	delete this;
	return error;
}


// #pragma mark - PackageResolvableAttributeHandler


/**
 * @brief Constructs a handler for a v1 package provides-resolvable sub-attribute.
 *
 * @param packageInfoValue Reference to the enclosing attribute value buffer.
 */
ReaderImplBase::PackageResolvableAttributeHandler
	::PackageResolvableAttributeHandler(
		BPackageInfoAttributeValue& packageInfoValue)
	:
	fPackageInfoValue(packageInfoValue)
{
}


/**
 * @brief Handles version and compatible-version sub-attributes of a v1 resolvable.
 *
 * @param context  Parsing context.
 * @param id       Attribute ID.
 * @param value    Decoded attribute value.
 * @param _handler Output for a child PackageVersionAttributeHandler if needed.
 * @return B_OK on success, or B_BAD_DATA / B_NO_MEMORY on error.
 */
status_t
ReaderImplBase::PackageResolvableAttributeHandler::HandleAttribute(
	AttributeHandlerContext* context, uint8 id, const AttributeValue& value,
	AttributeHandler** _handler)
{
	switch (id) {
		case B_HPKG_ATTRIBUTE_ID_PACKAGE_PROVIDES_TYPE:
			// obsolete
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_VERSION_MAJOR:
			fPackageInfoValue.resolvable.haveVersion = true;
			fPackageInfoValue.resolvable.version.major = value.string;
			if (_handler != NULL) {
				*_handler
					= new(std::nothrow) PackageVersionAttributeHandler(
						fPackageInfoValue,
						fPackageInfoValue.resolvable.version, false);
				if (*_handler == NULL)
					return B_NO_MEMORY;
			}
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_PROVIDES_COMPATIBLE:
			fPackageInfoValue.resolvable.haveCompatibleVersion = true;
			fPackageInfoValue.resolvable.compatibleVersion.major = value.string;
			if (_handler != NULL) {
				*_handler
					= new(std::nothrow) PackageVersionAttributeHandler(
						fPackageInfoValue,
						fPackageInfoValue.resolvable.compatibleVersion, false);
				if (*_handler == NULL)
					return B_NO_MEMORY;
			}
			break;

		default:
			context->errorOutput->PrintError("Error: Invalid package "
				"attribute section: unexpected package attribute id %d "
				"encountered when parsing package resolvable\n", id);
			return B_BAD_DATA;
	}

	return B_OK;
}


/**
 * @brief Forwards the completed resolvable value and deletes this handler.
 *
 * @param context Parsing context.
 * @return B_OK on success, or an error from the content handler.
 */
status_t
ReaderImplBase::PackageResolvableAttributeHandler::Delete(
	AttributeHandlerContext* context)
{
	status_t error = context->packageContentHandler->HandlePackageAttribute(
		fPackageInfoValue);
	fPackageInfoValue.Clear();

	delete this;
	return error;
}


// #pragma mark - PackageResolvableExpressionAttributeHandler


/**
 * @brief Constructs a handler for a v1 resolvable-expression sub-attribute.
 *
 * @param packageInfoValue Reference to the enclosing attribute value buffer.
 */
ReaderImplBase::PackageResolvableExpressionAttributeHandler
	::PackageResolvableExpressionAttributeHandler(
		BPackageInfoAttributeValue& packageInfoValue)
	:
	fPackageInfoValue(packageInfoValue)
{
}


/**
 * @brief Handles operator and version sub-attributes of a v1 resolvable expression.
 *
 * @param context  Parsing context.
 * @param id       Attribute ID.
 * @param value    Decoded attribute value.
 * @param _handler Output for a child PackageVersionAttributeHandler if needed.
 * @return B_OK on success, or B_BAD_DATA / B_NO_MEMORY on error.
 */
status_t
ReaderImplBase::PackageResolvableExpressionAttributeHandler::HandleAttribute(
	AttributeHandlerContext* context, uint8 id, const AttributeValue& value,
	AttributeHandler** _handler)
{
	switch (id) {
		case B_HPKG_ATTRIBUTE_ID_PACKAGE_RESOLVABLE_OPERATOR:
			if (value.unsignedInt >= B_PACKAGE_RESOLVABLE_OP_ENUM_COUNT) {
				context->errorOutput->PrintError(
					"Error: Invalid package attribute section: invalid "
					"package resolvable operator %lld encountered\n",
					value.unsignedInt);
				return B_BAD_DATA;
			}
			fPackageInfoValue.resolvableExpression.op
				= (BPackageResolvableOperator)value.unsignedInt;
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_VERSION_MAJOR:
			fPackageInfoValue.resolvableExpression.haveOpAndVersion = true;
			fPackageInfoValue.resolvableExpression.version.major
				= value.string;
			if (_handler != NULL) {
				*_handler
					= new(std::nothrow) PackageVersionAttributeHandler(
						fPackageInfoValue,
						fPackageInfoValue.resolvableExpression.version,
						false);
				if (*_handler == NULL)
					return B_NO_MEMORY;
			}
			return B_OK;

		default:
			context->errorOutput->PrintError("Error: Invalid package "
				"attribute section: unexpected package attribute id %d "
				"encountered when parsing package resolvable-expression\n",
				id);
			return B_BAD_DATA;
	}

	return B_OK;
}


/**
 * @brief Forwards the completed resolvable expression value and deletes this handler.
 *
 * @param context Parsing context.
 * @return B_OK on success, or an error from the content handler.
 */
status_t
ReaderImplBase::PackageResolvableExpressionAttributeHandler::Delete(
	AttributeHandlerContext* context)
{
	status_t error = context->packageContentHandler->HandlePackageAttribute(
		fPackageInfoValue);
	fPackageInfoValue.Clear();

	delete this;
	return error;
}


// #pragma mark - PackageAttributeHandler


/**
 * @brief Dispatches top-level package attribute IDs to typed value handlers (v1 format).
 *
 * Routes each known attribute ID to the appropriate BPackageInfo field and
 * optionally creates child handlers for compound attributes.
 *
 * @param context  Parsing context.
 * @param id       Attribute ID.
 * @param value    Decoded attribute value.
 * @param _handler Output for a child handler when the attribute has children.
 * @return B_OK on success, or B_BAD_DATA / B_NO_MEMORY on error.
 */
status_t
ReaderImplBase::PackageAttributeHandler::HandleAttribute(
	AttributeHandlerContext* context, uint8 id, const AttributeValue& value,
	AttributeHandler** _handler)
{
	switch (id) {
		case B_HPKG_ATTRIBUTE_ID_PACKAGE_NAME:
			fPackageInfoValue.SetTo(B_PACKAGE_INFO_NAME, value.string);
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_SUMMARY:
			fPackageInfoValue.SetTo(B_PACKAGE_INFO_SUMMARY, value.string);
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_DESCRIPTION:
			fPackageInfoValue.SetTo(B_PACKAGE_INFO_DESCRIPTION,
				value.string);
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_VENDOR:
			fPackageInfoValue.SetTo(B_PACKAGE_INFO_VENDOR, value.string);
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_PACKAGER:
			fPackageInfoValue.SetTo(B_PACKAGE_INFO_PACKAGER, value.string);
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_FLAGS:
			fPackageInfoValue.SetTo(B_PACKAGE_INFO_FLAGS,
				(uint32)value.unsignedInt);
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_ARCHITECTURE:
			if (value.unsignedInt
					>= B_PACKAGE_ARCHITECTURE_ENUM_COUNT) {
				context->errorOutput->PrintError(
					"Error: Invalid package attribute section: "
					"Invalid package architecture %lld encountered\n",
					value.unsignedInt);
				return B_BAD_DATA;
			}
			fPackageInfoValue.SetTo(B_PACKAGE_INFO_ARCHITECTURE,
				(uint8)value.unsignedInt);
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_VERSION_MAJOR:
			fPackageInfoValue.attributeID = B_PACKAGE_INFO_VERSION;
			fPackageInfoValue.version.major = value.string;
			if (_handler != NULL) {
				*_handler
					= new(std::nothrow) PackageVersionAttributeHandler(
						fPackageInfoValue, fPackageInfoValue.version, true);
				if (*_handler == NULL)
					return B_NO_MEMORY;
			}
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_COPYRIGHT:
			fPackageInfoValue.SetTo(B_PACKAGE_INFO_COPYRIGHTS,
				value.string);
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_LICENSE:
			fPackageInfoValue.SetTo(B_PACKAGE_INFO_LICENSES,
				value.string);
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_URL:
			fPackageInfoValue.SetTo(B_PACKAGE_INFO_URLS, value.string);
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_SOURCE_URL:
			fPackageInfoValue.SetTo(B_PACKAGE_INFO_SOURCE_URLS, value.string);
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_PROVIDES:
			fPackageInfoValue.resolvable.name = value.string;
			fPackageInfoValue.attributeID = B_PACKAGE_INFO_PROVIDES;
			if (_handler != NULL) {
				*_handler
					= new(std::nothrow) PackageResolvableAttributeHandler(
						fPackageInfoValue);
				if (*_handler == NULL)
					return B_NO_MEMORY;
			}
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_REQUIRES:
		case B_HPKG_ATTRIBUTE_ID_PACKAGE_SUPPLEMENTS:
		case B_HPKG_ATTRIBUTE_ID_PACKAGE_CONFLICTS:
		case B_HPKG_ATTRIBUTE_ID_PACKAGE_FRESHENS:
			fPackageInfoValue.resolvableExpression.name = value.string;
			switch (id) {
				case B_HPKG_ATTRIBUTE_ID_PACKAGE_REQUIRES:
					fPackageInfoValue.attributeID = B_PACKAGE_INFO_REQUIRES;
					break;

				case B_HPKG_ATTRIBUTE_ID_PACKAGE_SUPPLEMENTS:
					fPackageInfoValue.attributeID
						= B_PACKAGE_INFO_SUPPLEMENTS;
					break;

				case B_HPKG_ATTRIBUTE_ID_PACKAGE_CONFLICTS:
					fPackageInfoValue.attributeID
						= B_PACKAGE_INFO_CONFLICTS;
					break;

				case B_HPKG_ATTRIBUTE_ID_PACKAGE_FRESHENS:
					fPackageInfoValue.attributeID = B_PACKAGE_INFO_FRESHENS;
					break;
			}
			if (_handler != NULL) {
				*_handler = new(std::nothrow)
					PackageResolvableExpressionAttributeHandler(
						fPackageInfoValue);
				if (*_handler == NULL)
					return B_NO_MEMORY;
			}
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_REPLACES:
			fPackageInfoValue.SetTo(B_PACKAGE_INFO_REPLACES, value.string);
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_CHECKSUM:
			fPackageInfoValue.SetTo(B_PACKAGE_INFO_CHECKSUM, value.string);
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_INSTALL_PATH:
			fPackageInfoValue.SetTo(B_PACKAGE_INFO_INSTALL_PATH, value.string);
			break;

		default:
			context->errorOutput->PrintError(
				"Error: Invalid package attribute section: unexpected "
				"package attribute id %d encountered\n", id);
			return B_BAD_DATA;
	}

	// notify unless the current attribute has children, in which case
	// the child-handler will notify when it's done
	if (_handler == NULL) {
		status_t error = context->packageContentHandler
			->HandlePackageAttribute(fPackageInfoValue);
		fPackageInfoValue.Clear();
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


// #pragma mark - LowLevelAttributeHandler


/**
 * @brief Default-constructs a root LowLevelAttributeHandler for the v1 format.
 */
ReaderImplBase::LowLevelAttributeHandler::LowLevelAttributeHandler()
	:
	fParentToken(NULL),
	fToken(NULL),
	fID(B_HPKG_ATTRIBUTE_ID_ENUM_COUNT)
{
}


/**
 * @brief Constructs a LowLevelAttributeHandler for a specific v1 attribute node.
 *
 * @param id          Attribute ID of the triggering node.
 * @param value       Decoded value of the triggering attribute.
 * @param parentToken Opaque token of the parent attribute node.
 * @param token       Opaque token assigned by the content handler.
 */
ReaderImplBase::LowLevelAttributeHandler::LowLevelAttributeHandler(uint8 id,
	const BPackageAttributeValue& value, void* parentToken, void* token)
	:
	fParentToken(NULL),
	fToken(token),
	fID(id),
	fValue(value)
{
}


/**
 * @brief Forwards the attribute to the low-level content handler and creates a child.
 *
 * @param context  Parsing context.
 * @param id       Attribute ID.
 * @param value    Decoded attribute value.
 * @param _handler Output for a child LowLevelAttributeHandler if children follow.
 * @return B_OK on success, B_NO_MEMORY if a child handler cannot be allocated.
 */
status_t
ReaderImplBase::LowLevelAttributeHandler::HandleAttribute(
	AttributeHandlerContext* context, uint8 id, const AttributeValue& value,
	AttributeHandler** _handler)
{
	// notify the content handler
	void* token;
	status_t error = context->lowLevelHandler->HandleAttribute(
		(BHPKGAttributeID)id, value, fToken, token);
	if (error != B_OK)
		return error;

	// create a subhandler for the attribute, if it has children
	if (_handler != NULL) {
		*_handler = new(std::nothrow) LowLevelAttributeHandler(id, value,
			fToken, token);
		if (*_handler == NULL) {
			context->lowLevelHandler->HandleAttributeDone((BHPKGAttributeID)id,
				value, fToken, token);
			return B_NO_MEMORY;
		}
		return B_OK;
	}

	// no children -- just call the done hook
	return context->lowLevelHandler->HandleAttributeDone((BHPKGAttributeID)id,
		value, fToken, token);
}


/**
 * @brief Calls HandleAttributeDone() on the low-level handler and deletes this object.
 *
 * @param context Parsing context.
 * @return B_OK on success, or an error code from the low-level handler.
 */
status_t
ReaderImplBase::LowLevelAttributeHandler::Delete(
	AttributeHandlerContext* context)
{
	status_t error = B_OK;
	if (fID != B_HPKG_ATTRIBUTE_ID_ENUM_COUNT) {
		error = context->lowLevelHandler->HandleAttributeDone(
			(BHPKGAttributeID)fID, fValue, fParentToken, fToken);
	}

	delete this;
	return error;
}


// #pragma mark - ReaderImplBase


/**
 * @brief Constructs a v1 ReaderImplBase with no file open yet.
 *
 * @param errorOutput Destination for diagnostic messages.
 */
ReaderImplBase::ReaderImplBase(BErrorOutput* errorOutput)
	:
	fPackageAttributesSection("package attributes"),
	fErrorOutput(errorOutput),
	fFD(-1),
	fOwnsFD(false),
	fCurrentSection(NULL),
	fScratchBuffer(NULL),
	fScratchBufferSize(0)
{
}


/**
 * @brief Destroys the v1 ReaderImplBase and releases the scratch buffer and file descriptor.
 */
ReaderImplBase::~ReaderImplBase()
{
	if (fOwnsFD && fFD >= 0)
		close(fFD);

	delete[] fScratchBuffer;
}


/**
 * @brief Stores the file descriptor and allocates the scratch decompression buffer.
 *
 * @param fd     File descriptor referencing the open v1 HPKG file.
 * @param keepFD If true, the reader takes ownership and will close @a fd.
 * @return B_OK on success, or B_NO_MEMORY if the scratch buffer cannot be allocated.
 */
status_t
ReaderImplBase::Init(int fd, bool keepFD)
{
	fFD = fd;
	fOwnsFD = keepFD;

	// allocate a scratch buffer
	fScratchBuffer = new(std::nothrow) uint8[kScratchBufferSize];
	if (fScratchBuffer == NULL) {
		fErrorOutput->PrintError("Error: Out of memory!\n");
		return B_NO_MEMORY;
	}
	fScratchBufferSize = kScratchBufferSize;

	return B_OK;
}


/**
 * @brief Validates the compression parameters of a section descriptor.
 *
 * Checks that compressed and uncompressed lengths are consistent with the
 * compression type, and that the compression algorithm is known.
 *
 * @param section Section whose compression fields are to be validated.
 * @return NULL if valid, or a human-readable error string on failure.
 */
const char*
ReaderImplBase::CheckCompression(const SectionInfo& section) const
{
	switch (section.compression) {
		case B_HPKG_COMPRESSION_NONE:
			if (section.compressedLength != section.uncompressedLength) {
				return "Uncompressed, but compressed and uncompressed length "
					"don't match";
			}
			return NULL;

		case B_HPKG_COMPRESSION_ZLIB:
			if (section.compressedLength >= section.uncompressedLength) {
				return "Compressed, but compressed length is not less than "
					"uncompressed length";
			}
			return NULL;

		default:
			return "Invalid compression algorithm ID";
	}
}


/**
 * @brief Parses the string table at the beginning of the current v1 section.
 *
 * Iterates null-terminated strings and builds the pointer array for the
 * current section's string table.
 *
 * @return B_OK on success, or B_BAD_DATA / B_NO_MEMORY if malformed.
 */
status_t
ReaderImplBase::ParseStrings()
{
	// allocate table, if there are any strings
	if (fCurrentSection->stringsCount == 0) {
		fCurrentSection->currentOffset += fCurrentSection->stringsLength;
		return B_OK;
	}

	fCurrentSection->strings
		= new(std::nothrow) char*[fCurrentSection->stringsCount];
	if (fCurrentSection->strings == NULL) {
		fErrorOutput->PrintError("Error: Out of memory!\n");
		return B_NO_MEMORY;
	}

	// parse the section and fill the table
	char* position
		= (char*)fCurrentSection->data + fCurrentSection->currentOffset;
	char* sectionEnd = position + fCurrentSection->stringsLength;
	uint32 index = 0;
	while (true) {
		if (position >= sectionEnd) {
			fErrorOutput->PrintError("Error: Malformed %s strings section\n",
				fCurrentSection->name);
			return B_BAD_DATA;
		}

		size_t stringLength = strnlen(position, (char*)sectionEnd - position);

		if (stringLength == 0) {
			if (position + 1 != sectionEnd) {
				fErrorOutput->PrintError(
					"Error: %ld excess bytes in %s strings section\n",
					sectionEnd - (position + 1), fCurrentSection->name);
				return B_BAD_DATA;
			}

			if (index != fCurrentSection->stringsCount) {
				fErrorOutput->PrintError("Error: Invalid %s strings section: "
					"Less strings (%lld) than specified in the header (%lld)\n",
					fCurrentSection->name, index,
					fCurrentSection->stringsCount);
				return B_BAD_DATA;
			}

			fCurrentSection->currentOffset += fCurrentSection->stringsLength;

			return B_OK;
		}

		if (index >= fCurrentSection->stringsCount) {
			fErrorOutput->PrintError("Error: Invalid %s strings section: "
				"More strings (%lld) than specified in the header (%lld)\n",
				fCurrentSection->name, index, fCurrentSection->stringsCount);
			return B_BAD_DATA;
		}

		fCurrentSection->strings[index++] = position;
		position += stringLength + 1;
	}
}


/**
 * @brief Parses the v1 package attributes section using the supplied handler stack.
 *
 * @param context               Parsing context.
 * @param rootAttributeHandler  Root handler pushed before parsing begins.
 * @return B_OK on success, or an error code.
 */
status_t
ReaderImplBase::ParsePackageAttributesSection(
	AttributeHandlerContext* context, AttributeHandler* rootAttributeHandler)
{
	// parse package attributes
	SetCurrentSection(&fPackageAttributesSection);

	// init the attribute handler stack
	rootAttributeHandler->SetLevel(0);
	ClearAttributeHandlerStack();
	PushAttributeHandler(rootAttributeHandler);

	bool sectionHandled;
	status_t error = ParseAttributeTree(context, sectionHandled);
	if (error == B_OK && sectionHandled) {
		if (fPackageAttributesSection.currentOffset
				< fPackageAttributesSection.uncompressedLength) {
			fErrorOutput->PrintError("Error: %llu excess byte(s) in package "
				"attributes section\n",
				fPackageAttributesSection.uncompressedLength
					- fPackageAttributesSection.currentOffset);
			error = B_BAD_DATA;
		}
	}

	SetCurrentSection(NULL);

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
 * @brief Wraps the v1 _ParseAttributeTree with optional section start/end calls.
 *
 * @param context          Parsing context.
 * @param _sectionHandled  Set to true if the section was parsed, false if skipped.
 * @return B_OK on success, or an error code.
 */
status_t
ReaderImplBase::ParseAttributeTree(AttributeHandlerContext* context,
	bool& _sectionHandled)
{
	if (context->hasLowLevelHandler) {
		bool handleSection = false;
		status_t error = context->lowLevelHandler->HandleSectionStart(
			context->section, handleSection);
		if (error != B_OK)
			return error;

		if (!handleSection) {
			_sectionHandled = false;
			return B_OK;
		}
	}

	status_t error = _ParseAttributeTree(context);

	if (context->hasLowLevelHandler) {
		status_t endError = context->lowLevelHandler->HandleSectionEnd(
			context->section);
		if (error == B_OK)
			error = endError;
	}

	_sectionHandled = true;
	return error;
}


/**
 * @brief Iterates the v1 attribute tree using the handler stack.
 *
 * @param context Parsing context.
 * @return B_OK on success, or an error code.
 */
status_t
ReaderImplBase::_ParseAttributeTree(AttributeHandlerContext* context)
{
	int level = 0;

	while (true) {
		uint8 id;
		AttributeValue value;
		bool hasChildren;
		uint64 tag;

		status_t error = _ReadAttribute(id, value, &hasChildren, &tag);
		if (error != B_OK)
			return error;

		if (tag == 0) {
			AttributeHandler* handler = PopAttributeHandler();
			if (level-- == 0)
				return B_OK;

			error = handler->Delete(context);
			if (error != B_OK)
				return error;

			continue;
		}

		AttributeHandler* childHandler = NULL;
		error = CurrentAttributeHandler()->HandleAttribute(context, id, value,
			hasChildren ? &childHandler : NULL);
		if (error != B_OK)
			return error;

		// parse children
		if (hasChildren) {
			// create an ignore handler, if necessary
			if (childHandler == NULL) {
				childHandler = new(std::nothrow) IgnoreAttributeHandler;
				if (childHandler == NULL) {
					fErrorOutput->PrintError("Error: Out of memory!\n");
					return B_NO_MEMORY;
				}
			}

			childHandler->SetLevel(++level);
			PushAttributeHandler(childHandler);
		}
	}
}


/**
 * @brief Reads and decodes a single v1 attribute tag and its value.
 *
 * @param _id          Output for the attribute ID byte.
 * @param _value       Output for the decoded attribute value.
 * @param _hasChildren Output for the has-children flag; may be NULL.
 * @param _tag         Output for the raw tag value; may be NULL.
 * @return B_OK on success, or B_BAD_DATA if malformed.
 */
status_t
ReaderImplBase::_ReadAttribute(uint8& _id, AttributeValue& _value,
	bool* _hasChildren, uint64* _tag)
{
	uint64 tag;
	status_t error = ReadUnsignedLEB128(tag);
	if (error != B_OK)
		return error;

	if (tag != 0) {
		// get the type
		uint16 type = attribute_tag_type(tag);
		if (type >= B_HPKG_ATTRIBUTE_TYPE_ENUM_COUNT) {
			fErrorOutput->PrintError("Error: Invalid %s section: attribute "
				"type %d not supported!\n", fCurrentSection->name, type);
			return B_BAD_DATA;
		}

		// get the value
		error = ReadAttributeValue(type, attribute_tag_encoding(tag),
			_value);
		if (error != B_OK)
			return error;

		_id = attribute_tag_id(tag);
		if (_id >= B_HPKG_ATTRIBUTE_ID_ENUM_COUNT) {
			fErrorOutput->PrintError("Error: Invalid %s section: "
				"attribute id %d not supported!\n", fCurrentSection->name, _id);
			return B_BAD_DATA;
		}
	}

	if (_hasChildren != NULL)
		*_hasChildren = attribute_tag_has_children(tag);
	if (_tag != NULL)
		*_tag = tag;

	return B_OK;
}


/**
 * @brief Decodes the payload of a v1 attribute given its type and encoding.
 *
 * @param type      B_HPKG_ATTRIBUTE_TYPE_* constant.
 * @param encoding  B_HPKG_ATTRIBUTE_ENCODING_* constant.
 * @param _value    Output for the decoded value.
 * @return B_OK on success, or B_BAD_DATA / B_BAD_VALUE on malformed input.
 */
status_t
ReaderImplBase::ReadAttributeValue(uint8 type, uint8 encoding,
	AttributeValue& _value)
{
	switch (type) {
		case B_HPKG_ATTRIBUTE_TYPE_INT:
		case B_HPKG_ATTRIBUTE_TYPE_UINT:
		{
			uint64 intValue;
			status_t error;

			switch (encoding) {
				case B_HPKG_ATTRIBUTE_ENCODING_INT_8_BIT:
				{
					uint8 value;
					error = _Read(value);
					intValue = value;
					break;
				}
				case B_HPKG_ATTRIBUTE_ENCODING_INT_16_BIT:
				{
					uint16 value;
					error = _Read(value);
					intValue = B_BENDIAN_TO_HOST_INT16(value);
					break;
				}
				case B_HPKG_ATTRIBUTE_ENCODING_INT_32_BIT:
				{
					uint32 value;
					error = _Read(value);
					intValue = B_BENDIAN_TO_HOST_INT32(value);
					break;
				}
				case B_HPKG_ATTRIBUTE_ENCODING_INT_64_BIT:
				{
					uint64 value;
					error = _Read(value);
					intValue = B_BENDIAN_TO_HOST_INT64(value);
					break;
				}
				default:
				{
					fErrorOutput->PrintError("Error: Invalid %s section: "
						"invalid encoding %d for int value type %d\n",
						fCurrentSection->name, encoding, type);
					return B_BAD_VALUE;
				}
			}

			if (error != B_OK)
				return error;

			if (type == B_HPKG_ATTRIBUTE_TYPE_INT)
				_value.SetTo((int64)intValue);
			else
				_value.SetTo(intValue);

			return B_OK;
		}

		case B_HPKG_ATTRIBUTE_TYPE_STRING:
		{
			if (encoding == B_HPKG_ATTRIBUTE_ENCODING_STRING_TABLE) {
				uint64 index;
				status_t error = ReadUnsignedLEB128(index);
				if (error != B_OK)
					return error;

				if (index > fCurrentSection->stringsCount) {
					fErrorOutput->PrintError("Error: Invalid %s section: "
						"string reference (%lld) out of bounds (%lld)\n",
						fCurrentSection->name, index,
						fCurrentSection->stringsCount);
					return B_BAD_DATA;
				}

				_value.SetTo(fCurrentSection->strings[index]);
			} else if (encoding == B_HPKG_ATTRIBUTE_ENCODING_STRING_INLINE) {
				const char* string;
				status_t error = _ReadString(string);
				if (error != B_OK)
					return error;

				_value.SetTo(string);
			} else {
				fErrorOutput->PrintError("Error: Invalid %s section: invalid "
					"string encoding (%u)\n", fCurrentSection->name, encoding);
				return B_BAD_DATA;
			}

			return B_OK;
		}

		default:
			fErrorOutput->PrintError("Error: Invalid %s section: invalid "
				"value type: %d\n", fCurrentSection->name, type);
			return B_BAD_DATA;
	}
}


/**
 * @brief Reads an unsigned LEB128-encoded integer from the current v1 section.
 *
 * @param _value Output for the decoded 64-bit unsigned value.
 * @return B_OK on success, or an I/O error code.
 */
status_t
ReaderImplBase::ReadUnsignedLEB128(uint64& _value)
{
	uint64 result = 0;
	int shift = 0;
	while (true) {
		uint8 byte;
		status_t error = _Read(byte);
		if (error != B_OK)
			return error;

		result |= uint64(byte & 0x7f) << shift;
		if ((byte & 0x80) == 0)
			break;
		shift += 7;
	}

	_value = result;
	return B_OK;
}


/**
 * @brief Reads a null-terminated string from the current v1 section buffer.
 *
 * @param _string       Output pointer into the section buffer (no copy).
 * @param _stringLength Optional output for the string length.
 * @return B_OK on success, or B_BAD_DATA if the string extends past section end.
 */
status_t
ReaderImplBase::_ReadString(const char*& _string, size_t* _stringLength)
{
	const char* string
		= (const char*)fCurrentSection->data + fCurrentSection->currentOffset;
	size_t stringLength = strnlen(string,
		fCurrentSection->uncompressedLength - fCurrentSection->currentOffset);

	if (stringLength
		== fCurrentSection->uncompressedLength
			- fCurrentSection->currentOffset) {
		fErrorOutput->PrintError(
			"_ReadString(): string extends beyond %s end\n",
			fCurrentSection->name);
		return B_BAD_DATA;
	}

	_string = string;
	if (_stringLength != NULL)
		*_stringLength = stringLength;

	fCurrentSection->currentOffset += stringLength + 1;
	return B_OK;
}


/**
 * @brief Reads @a size bytes from the current v1 section into @a buffer.
 *
 * @param buffer Destination buffer.
 * @param size   Number of bytes to copy.
 * @return B_OK on success, or B_BAD_DATA if the read would exceed section end.
 */
status_t
ReaderImplBase::_ReadSectionBuffer(void* buffer, size_t size)
{
	if (size > fCurrentSection->uncompressedLength
			- fCurrentSection->currentOffset) {
		fErrorOutput->PrintError("_ReadBuffer(%lu): read beyond %s end\n",
			size, fCurrentSection->name);
		return B_BAD_DATA;
	}

	memcpy(buffer, fCurrentSection->data + fCurrentSection->currentOffset,
		size);
	fCurrentSection->currentOffset += size;
	return B_OK;
}


/**
 * @brief Reads @a size bytes from the v1 package file via pread() at the given offset.
 *
 * @param offset Absolute byte offset into the file.
 * @param buffer Destination buffer.
 * @param size   Number of bytes to read.
 * @return B_OK on success, or an error code if the read fails or is short.
 */
status_t
ReaderImplBase::ReadBuffer(off_t offset, void* buffer, size_t size)
{
	ssize_t bytesRead = pread(fFD, buffer, size, offset);
	if (bytesRead < 0) {
		fErrorOutput->PrintError("_ReadBuffer(%p, %lu) failed to read data: "
			"%s\n", buffer, size, strerror(errno));
		return errno;
	}
	if ((size_t)bytesRead != size) {
		fErrorOutput->PrintError("_ReadBuffer(%p, %lu) failed to read all "
			"data\n", buffer, size);
		return B_ERROR;
	}

	return B_OK;
}


/**
 * @brief Reads and decompresses a complete v1 section into its preallocated buffer.
 *
 * For uncompressed sections, performs a single pread(). For zlib-compressed
 * sections, streams the compressed data through a BZlibCompressionAlgorithm
 * decompressor in scratch-buffer-sized chunks.
 *
 * @param section Section descriptor with compression info, offset, and data pointer.
 * @return B_OK on success, or an error code on I/O or decompression failure.
 */
status_t
ReaderImplBase::ReadCompressedBuffer(const SectionInfo& section)
{
	uint32 compressedSize = section.compressedLength;
	uint64 offset = section.offset;

	switch (section.compression) {
		case B_HPKG_COMPRESSION_NONE:
			return ReadBuffer(offset, section.data, compressedSize);

		case B_HPKG_COMPRESSION_ZLIB:
		{
			// create the decompression stream
			BMemoryIO bufferOutput(section.data, section.uncompressedLength);
			BZlibCompressionAlgorithm algorithm;
			BDataIO* zlibOutput;
			status_t error = algorithm.CreateDecompressingOutputStream(
				&bufferOutput, NULL, zlibOutput);
			if (error != B_OK)
				return error;

			ObjectDeleter<BDataIO> zlibOutputDeleter(zlibOutput);

			while (compressedSize > 0) {
				// read compressed buffer
				size_t toRead = std::min((size_t)compressedSize,
					fScratchBufferSize);
				error = ReadBuffer(offset, fScratchBuffer, toRead);
				if (error != B_OK)
					return error;

				// uncompress
				error = zlibOutput->WriteExactly(fScratchBuffer, toRead);
				if (error != B_OK)
					return error;

				compressedSize -= toRead;
				offset += toRead;
			}

			error = zlibOutput->Flush();
			if (error != B_OK)
				return error;

			// verify that all data have been read
			if ((uint64)bufferOutput.Position() != section.uncompressedLength) {
				fErrorOutput->PrintError("Error: Missing bytes in uncompressed "
					"buffer!\n");
				return B_BAD_DATA;
			}

			return B_OK;
		}

		default:
		{
			fErrorOutput->PrintError("Error: Invalid compression type: %u\n",
				section.compression);
			return B_BAD_DATA;
		}
	}
}


}	// namespace BPrivate

}	// namespace V1

}	// namespace BHPKG

}	// namespace BPackageKit
