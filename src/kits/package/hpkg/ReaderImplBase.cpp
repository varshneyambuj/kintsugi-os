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
 * @file ReaderImplBase.cpp
 * @brief Shared base class for HPKG package and repository reader implementations.
 *
 * ReaderImplBase implements the attribute-tree parsing engine used by both
 * PackageReaderImpl and RepositoryReaderImpl. It manages the compressed heap
 * reader, the per-section string tables, and the handler stack that drives
 * event dispatch as the attribute tree is walked depth-first.
 *
 * The file also contains the full family of AttributeHandler subclasses
 * (PackageVersionAttributeHandler, PackageResolvableAttributeHandler, etc.)
 * that translate raw attribute IDs and values into typed BPackageInfo fields.
 *
 * @see PackageReaderImpl, RepositoryReaderImpl, WriterImplBase
 */


#include <package/hpkg/ReaderImplBase.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <new>

#include <ByteOrder.h>
#include <DataIO.h>
#include <OS.h>

#include <ZlibCompressionAlgorithm.h>
#include <ZstdCompressionAlgorithm.h>

#include <package/hpkg/HPKGDefsPrivate.h>
#include <package/hpkg/PackageFileHeapReader.h>


namespace BPackageKit {

namespace BHPKG {

namespace BPrivate {


/** @brief Per-attribute-ID expected type table, indexed by BHPKGAttributeID. */
static const uint16 kAttributeTypes[B_HPKG_ATTRIBUTE_ID_ENUM_COUNT] = {
	#define B_DEFINE_HPKG_ATTRIBUTE(id, type, name, constant)	\
		B_HPKG_ATTRIBUTE_TYPE_##type,
	#include <package/hpkg/PackageAttributes.h>
	#undef B_DEFINE_HPKG_ATTRIBUTE
};

// #pragma mark - AttributeHandlerContext


/**
 * @brief Constructs an AttributeHandlerContext for a high-level content handler.
 *
 * @param errorOutput           Destination for diagnostic messages.
 * @param packageContentHandler High-level callback object to receive parsed data.
 * @param section               Which HPKG section is being parsed.
 * @param ignoreUnknownAttributes If true, attributes with unrecognised IDs are
 *                              silently skipped rather than triggering an error.
 */
ReaderImplBase::AttributeHandlerContext::AttributeHandlerContext(
	BErrorOutput* errorOutput, BPackageContentHandler* packageContentHandler,
	BHPKGPackageSectionID section, bool ignoreUnknownAttributes)
	:
	errorOutput(errorOutput),
	packageContentHandler(packageContentHandler),
	hasLowLevelHandler(false),
	ignoreUnknownAttributes(ignoreUnknownAttributes),
	section(section)
{
}


/**
 * @brief Constructs an AttributeHandlerContext for a low-level content handler.
 *
 * @param errorOutput       Destination for diagnostic messages.
 * @param lowLevelHandler   Low-level callback object to receive raw attribute data.
 * @param section           Which HPKG section is being parsed.
 * @param ignoreUnknownAttributes If true, unrecognised attributes are skipped.
 */
ReaderImplBase::AttributeHandlerContext::AttributeHandlerContext(
	BErrorOutput* errorOutput, BLowLevelPackageContentHandler* lowLevelHandler,
	BHPKGPackageSectionID section, bool ignoreUnknownAttributes)
	:
	errorOutput(errorOutput),
	lowLevelHandler(lowLevelHandler),
	hasLowLevelHandler(true),
	ignoreUnknownAttributes(ignoreUnknownAttributes),
	section(section)
{
}


/**
 * @brief Destroys the AttributeHandlerContext.
 */
ReaderImplBase::AttributeHandlerContext::~AttributeHandlerContext()
{
}


/**
 * @brief Forwards an error-occurred notification to whichever handler is active.
 *
 * Calls HandleErrorOccurred() on either the low-level handler or the high-level
 * package content handler, depending on which was supplied at construction time.
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
 * @brief Called when an attribute node is encountered at this handler's level.
 *
 * The default implementation ignores the attribute and returns B_OK.
 *
 * @param context  Parsing context carrying error output and content handler.
 * @param id       Attribute ID (one of the BHPKGAttributeID constants).
 * @param value    Decoded attribute value.
 * @param _handler Output pointer for an optional child handler; NULL if the
 *                 attribute has no children.
 * @return B_OK on success, or an error code.
 */
status_t
ReaderImplBase::AttributeHandler::HandleAttribute(
	AttributeHandlerContext* context, uint8 id, const AttributeValue& value,
	AttributeHandler** _handler)
{
	return B_OK;
}


/**
 * @brief Called when all children of this handler's attribute node are done.
 *
 * The default implementation is a no-op that returns B_OK.
 *
 * @param context Parsing context.
 * @return B_OK on success, or an error code.
 */
status_t
ReaderImplBase::AttributeHandler::NotifyDone(
	AttributeHandlerContext* context)
{
	return B_OK;
}


// #pragma mark - AttributeHandler allocation


/**
 * @brief Allocates an AttributeHandler from the context's arena allocator.
 *
 * All AttributeHandler subclasses should be allocated with placement-new
 * passing the AttributeHandlerContext so that arena memory is used.
 *
 * @param size    Byte size of the subclass to allocate.
 * @param context Parsing context whose arena provides the memory.
 * @return Pointer to the allocated (but not yet constructed) memory, or NULL.
 */
void*
ReaderImplBase::AttributeHandler::operator new(size_t size, AttributeHandlerContext* context)
{
	AttributeHandler* handler = (AttributeHandler*)context->handlersAllocator.Allocate(size);
	if (handler != NULL)
		handler->fDeleting = false;
	return handler;
}


/**
 * @brief Operator delete stub that enforces the Delete() contract.
 *
 * Attribute handlers must be released via Delete() rather than plain
 * @c delete so that arena memory is properly reclaimed. This stub fires a
 * debugger assertion if a handler is deleted directly.
 *
 * @param pointer Pointer returned by the placement new above.
 */
void
ReaderImplBase::AttributeHandler::operator delete(void* pointer)
{
	AttributeHandler* handler = (AttributeHandler*)pointer;
	if (!handler->fDeleting)
		debugger("Package AttributeHandler: deleted without calling Delete()");

	// Nothing else to do; memory is released by Delete().
}


/**
 * @brief Destroys the handler and returns its arena memory to the allocator.
 *
 * Sets fDeleting so that the overridden operator delete does not fire the
 * debugger assertion, then deletes and frees the arena slot.
 *
 * @param context Parsing context whose arena originally supplied the memory.
 * @return B_OK.
 */
status_t
ReaderImplBase::AttributeHandler::Delete(AttributeHandlerContext* context)
{
	fDeleting = true;
	delete this;

	context->handlersAllocator.Free(this);
	return B_OK;
}


// #pragma mark - PackageInfoAttributeHandlerBase


/**
 * @brief Constructs a PackageInfoAttributeHandlerBase bound to a value buffer.
 *
 * @param packageInfoValue Reference to the BPackageInfoAttributeValue that this
 *                         handler fills in before forwarding to the content handler.
 */
ReaderImplBase::PackageInfoAttributeHandlerBase
	::PackageInfoAttributeHandlerBase(
		BPackageInfoAttributeValue& packageInfoValue)
	:
	fPackageInfoValue(packageInfoValue)
{
}


/**
 * @brief Forwards the completed package info value to the content handler.
 *
 * Called when all child attributes of the current package-info node have been
 * processed. Clears the value buffer after delivery.
 *
 * @param context Parsing context.
 * @return B_OK on success, or an error code from the content handler.
 */
status_t
ReaderImplBase::PackageInfoAttributeHandlerBase::NotifyDone(
	AttributeHandlerContext* context)
{
	status_t error = context->packageContentHandler->HandlePackageAttribute(
		fPackageInfoValue);
	if (context->ignoreUnknownAttributes && error == B_NOT_SUPPORTED)
		error = B_OK; // Safe to skip a future/unknown attribute.
	fPackageInfoValue.Clear();
	return error;
}


// #pragma mark - PackageVersionAttributeHandler


/**
 * @brief Constructs a handler for a package version sub-attribute.
 *
 * @param packageInfoValue Reference to the enclosing attribute value.
 * @param versionData      Reference to the BPackageVersionData to populate.
 * @param notify           If true, NotifyDone() will forward the value to the
 *                         content handler.
 */
ReaderImplBase::PackageVersionAttributeHandler::PackageVersionAttributeHandler(
	BPackageInfoAttributeValue& packageInfoValue,
	BPackageVersionData& versionData, bool notify)
	:
	super(packageInfoValue),
	fPackageVersionData(versionData),
	fNotify(notify)
{
}


/**
 * @brief Handles version component sub-attributes (minor, micro, pre-release, revision).
 *
 * @param context  Parsing context.
 * @param id       Attribute ID.
 * @param value    Decoded attribute value.
 * @param _handler Unused child handler output; versions have no further nesting.
 * @return B_OK on success, or B_BAD_DATA for an unrecognised attribute ID.
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
			if (context->ignoreUnknownAttributes)
				break;

			context->errorOutput->PrintError("Error: Invalid package "
				"attribute section: unexpected package attribute id %d "
				"encountered when parsing package version\n", id);
			return B_BAD_DATA;
	}

	return B_OK;
}


/**
 * @brief Notifies the content handler that all version fields have been collected.
 *
 * Only fires the notification when @a fNotify is true (i.e., this is the top-level
 * version handler rather than a provides/compatible sub-handler).
 *
 * @param context Parsing context.
 * @return B_OK on success, or an error code from the base class.
 */
status_t
ReaderImplBase::PackageVersionAttributeHandler::NotifyDone(
	AttributeHandlerContext* context)
{
	if (!fNotify)
		return B_OK;

	fPackageInfoValue.attributeID = B_PACKAGE_INFO_VERSION;
	return super::NotifyDone(context);
}


// #pragma mark - PackageResolvableAttributeHandler


/**
 * @brief Constructs a handler for a package provides-resolvable sub-attribute.
 *
 * @param packageInfoValue Reference to the enclosing attribute value buffer.
 */
ReaderImplBase::PackageResolvableAttributeHandler
	::PackageResolvableAttributeHandler(
		BPackageInfoAttributeValue& packageInfoValue)
	:
	super(packageInfoValue)
{
}


/**
 * @brief Handles resolvable version and compatible-version sub-attributes.
 *
 * @param context  Parsing context.
 * @param id       Attribute ID.
 * @param value    Decoded attribute value.
 * @param _handler Output for a child PackageVersionAttributeHandler if needed.
 * @return B_OK on success, or B_BAD_DATA for an unrecognised ID.
 */
status_t
ReaderImplBase::PackageResolvableAttributeHandler::HandleAttribute(
	AttributeHandlerContext* context, uint8 id, const AttributeValue& value,
	AttributeHandler** _handler)
{
	switch (id) {
		case B_HPKG_ATTRIBUTE_ID_PACKAGE_VERSION_MAJOR:
			fPackageInfoValue.resolvable.haveVersion = true;
			fPackageInfoValue.resolvable.version.major = value.string;
			if (_handler != NULL) {
				*_handler
					= new(context) PackageVersionAttributeHandler(
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
					= new(context) PackageVersionAttributeHandler(
						fPackageInfoValue,
						fPackageInfoValue.resolvable.compatibleVersion, false);
				if (*_handler == NULL)
					return B_NO_MEMORY;
			}
			break;

		default:
			if (context->ignoreUnknownAttributes)
				break;

			context->errorOutput->PrintError("Error: Invalid package "
				"attribute section: unexpected package attribute id %d "
				"encountered when parsing package resolvable\n", id);
			return B_BAD_DATA;
	}

	return B_OK;
}


// #pragma mark - PackageResolvableExpressionAttributeHandler


/**
 * @brief Constructs a handler for a resolvable-expression (requires/conflicts/etc.).
 *
 * @param packageInfoValue Reference to the enclosing attribute value buffer.
 */
ReaderImplBase::PackageResolvableExpressionAttributeHandler
	::PackageResolvableExpressionAttributeHandler(
		BPackageInfoAttributeValue& packageInfoValue)
	:
	super(packageInfoValue)
{
}


/**
 * @brief Handles operator and version sub-attributes of a resolvable expression.
 *
 * @param context  Parsing context.
 * @param id       Attribute ID.
 * @param value    Decoded attribute value.
 * @param _handler Output for a child PackageVersionAttributeHandler if needed.
 * @return B_OK on success, or B_BAD_DATA for an invalid operator or unknown ID.
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
					= new(context) PackageVersionAttributeHandler(
						fPackageInfoValue,
						fPackageInfoValue.resolvableExpression.version,
						false);
				if (*_handler == NULL)
					return B_NO_MEMORY;
			}
			return B_OK;

		default:
			if (context->ignoreUnknownAttributes)
				break;

			context->errorOutput->PrintError("Error: Invalid package "
				"attribute section: unexpected package attribute id %d "
				"encountered when parsing package resolvable-expression\n",
				id);
			return B_BAD_DATA;
	}

	return B_OK;
}


// #pragma mark - GlobalWritableFileInfoAttributeHandler


/**
 * @brief Constructs a handler for a global writable file info sub-attribute.
 *
 * @param packageInfoValue Reference to the enclosing attribute value buffer.
 */
ReaderImplBase::GlobalWritableFileInfoAttributeHandler
	::GlobalWritableFileInfoAttributeHandler(
		BPackageInfoAttributeValue& packageInfoValue)
	:
	super(packageInfoValue)
{
}


/**
 * @brief Handles update-type and is-directory sub-attributes of a writable file.
 *
 * @param context  Parsing context.
 * @param id       Attribute ID.
 * @param value    Decoded attribute value.
 * @param _handler Unused; writable file info has no further nesting.
 * @return B_OK on success, or B_BAD_DATA for an invalid update type or unknown ID.
 */
status_t
ReaderImplBase::GlobalWritableFileInfoAttributeHandler::HandleAttribute(
	AttributeHandlerContext* context, uint8 id, const AttributeValue& value,
	AttributeHandler** _handler)
{
	switch (id) {
		case B_HPKG_ATTRIBUTE_ID_PACKAGE_WRITABLE_FILE_UPDATE_TYPE:
			if (value.unsignedInt >= B_WRITABLE_FILE_UPDATE_TYPE_ENUM_COUNT) {
				context->errorOutput->PrintError(
					"Error: Invalid package attribute section: invalid "
					"global settings file update type %" B_PRIu64
					" encountered\n", value.unsignedInt);
				return B_BAD_DATA;
			}
			fPackageInfoValue.globalWritableFileInfo.updateType
				= (BWritableFileUpdateType)value.unsignedInt;
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_IS_WRITABLE_DIRECTORY:
			fPackageInfoValue.globalWritableFileInfo.isDirectory
				= value.unsignedInt != 0;
			break;

		default:
			if (context->ignoreUnknownAttributes)
				break;

			context->errorOutput->PrintError("Error: Invalid package "
				"attribute section: unexpected package attribute id %d "
				"encountered when parsing global settings file info\n",
				id);
			return B_BAD_DATA;
	}

	return B_OK;
}


// #pragma mark - UserSettingsFileInfoAttributeHandler


/**
 * @brief Constructs a handler for a user settings file info sub-attribute.
 *
 * @param packageInfoValue Reference to the enclosing attribute value buffer.
 */
ReaderImplBase::UserSettingsFileInfoAttributeHandler
	::UserSettingsFileInfoAttributeHandler(
		BPackageInfoAttributeValue& packageInfoValue)
	:
	super(packageInfoValue)
{
}


/**
 * @brief Handles template-path and is-directory sub-attributes of a user settings file.
 *
 * @param context  Parsing context.
 * @param id       Attribute ID.
 * @param value    Decoded attribute value.
 * @param _handler Unused; user settings file info has no further nesting.
 * @return B_OK on success, or B_BAD_DATA for an unknown attribute ID.
 */
status_t
ReaderImplBase::UserSettingsFileInfoAttributeHandler::HandleAttribute(
	AttributeHandlerContext* context, uint8 id, const AttributeValue& value,
	AttributeHandler** _handler)
{
	switch (id) {
		case B_HPKG_ATTRIBUTE_ID_PACKAGE_SETTINGS_FILE_TEMPLATE:
			fPackageInfoValue.userSettingsFileInfo.templatePath = value.string;
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_IS_WRITABLE_DIRECTORY:
			fPackageInfoValue.userSettingsFileInfo.isDirectory
				= value.unsignedInt != 0;
			break;

		default:
			if (context->ignoreUnknownAttributes)
				break;

			context->errorOutput->PrintError("Error: Invalid package "
				"attribute section: unexpected package attribute id %d "
				"encountered when parsing user settings file info\n",
				id);
			return B_BAD_DATA;
	}

	return B_OK;
}


// #pragma mark - UserAttributeHandler


/**
 * @brief Constructs a handler for a package user entry sub-attribute.
 *
 * @param packageInfoValue Reference to the enclosing attribute value buffer.
 */
ReaderImplBase::UserAttributeHandler::UserAttributeHandler(
		BPackageInfoAttributeValue& packageInfoValue)
	:
	super(packageInfoValue),
	fGroups()
{
}


/**
 * @brief Handles user sub-attributes such as real name, home, shell, and groups.
 *
 * @param context  Parsing context.
 * @param id       Attribute ID.
 * @param value    Decoded attribute value.
 * @param _handler Unused; user info has no further nesting.
 * @return B_OK on success, B_NO_MEMORY if a group string cannot be stored,
 *         or B_BAD_DATA for an unknown attribute ID.
 */
status_t
ReaderImplBase::UserAttributeHandler::HandleAttribute(
	AttributeHandlerContext* context, uint8 id, const AttributeValue& value,
	AttributeHandler** _handler)
{
	switch (id) {
		case B_HPKG_ATTRIBUTE_ID_PACKAGE_USER_REAL_NAME:
			fPackageInfoValue.user.realName = value.string;
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_USER_HOME:
			fPackageInfoValue.user.home = value.string;
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_USER_SHELL:
			fPackageInfoValue.user.shell = value.string;
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_USER_GROUP:
			if (!fGroups.Add(value.string))
				return B_NO_MEMORY;
			break;

		default:
			if (context->ignoreUnknownAttributes)
				break;

			context->errorOutput->PrintError("Error: Invalid package "
				"attribute section: unexpected package attribute id %d "
				"encountered when parsing user settings file info\n",
				id);
			return B_BAD_DATA;
	}

	return B_OK;
}


/**
 * @brief Transfers collected groups into the value buffer and notifies the handler.
 *
 * @param context Parsing context.
 * @return B_OK on success, or an error code from the base class notification.
 */
status_t
ReaderImplBase::UserAttributeHandler::NotifyDone(
	AttributeHandlerContext* context)
{
	if (!fGroups.IsEmpty()) {
		fPackageInfoValue.user.groups = fGroups.Elements();
		fPackageInfoValue.user.groupCount = fGroups.Count();
	}

	return super::NotifyDone(context);
}


// #pragma mark - PackageAttributeHandler


/**
 * @brief Dispatches top-level package attribute IDs to typed value handlers.
 *
 * Routes each recognised attribute ID to the appropriate field in the
 * fPackageInfoValue buffer and optionally creates a child handler for
 * compound attributes such as versions, resolvables, and file infos.
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

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_BASE_PACKAGE:
			fPackageInfoValue.SetTo(B_PACKAGE_INFO_BASE_PACKAGE, value.string);
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
					= new(context) PackageVersionAttributeHandler(
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
					= new(context) PackageResolvableAttributeHandler(
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
				*_handler = new(context)
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

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_GLOBAL_WRITABLE_FILE:
			fPackageInfoValue.globalWritableFileInfo.path = value.string;
			fPackageInfoValue.globalWritableFileInfo.updateType
				= B_WRITABLE_FILE_UPDATE_TYPE_ENUM_COUNT;
			fPackageInfoValue.attributeID
				= B_PACKAGE_INFO_GLOBAL_WRITABLE_FILES;
			if (_handler != NULL) {
				*_handler
					= new(context) GlobalWritableFileInfoAttributeHandler(
						fPackageInfoValue);
				if (*_handler == NULL)
					return B_NO_MEMORY;
			}
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_USER_SETTINGS_FILE:
			fPackageInfoValue.userSettingsFileInfo.path = value.string;
			fPackageInfoValue.attributeID
				= B_PACKAGE_INFO_USER_SETTINGS_FILES;
			if (_handler != NULL) {
				*_handler
					= new(context) UserSettingsFileInfoAttributeHandler(
						fPackageInfoValue);
				if (*_handler == NULL)
					return B_NO_MEMORY;
			}
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_USER:
			fPackageInfoValue.user.name = value.string;
			fPackageInfoValue.attributeID = B_PACKAGE_INFO_USERS;
			if (_handler != NULL) {
				*_handler = new(context) UserAttributeHandler(
					fPackageInfoValue);
				if (*_handler == NULL)
					return B_NO_MEMORY;
			}
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_GROUP:
			fPackageInfoValue.SetTo(B_PACKAGE_INFO_GROUPS, value.string);
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_POST_INSTALL_SCRIPT:
			fPackageInfoValue.SetTo(B_PACKAGE_INFO_POST_INSTALL_SCRIPTS,
				value.string);
			break;

		case B_HPKG_ATTRIBUTE_ID_PACKAGE_PRE_UNINSTALL_SCRIPT:
			fPackageInfoValue.SetTo(B_PACKAGE_INFO_PRE_UNINSTALL_SCRIPTS,
				value.string);
			break;

		default:
			if (context->ignoreUnknownAttributes)
				break;

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
		if (context->ignoreUnknownAttributes && error == B_NOT_SUPPORTED)
			error = B_OK; // Safe to skip a future/unknown attribute.
		fPackageInfoValue.Clear();
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


// #pragma mark - LowLevelAttributeHandler


/**
 * @brief Default-constructs a root LowLevelAttributeHandler.
 *
 * Used as the root handler pushed onto the stack before parsing begins.
 * The sentinel ID B_HPKG_ATTRIBUTE_ID_ENUM_COUNT marks it as the root so
 * NotifyDone() does not attempt to call HandleAttributeDone for it.
 */
ReaderImplBase::LowLevelAttributeHandler::LowLevelAttributeHandler()
	:
	fParentToken(NULL),
	fToken(NULL),
	fID(B_HPKG_ATTRIBUTE_ID_ENUM_COUNT)
{
}


/**
 * @brief Constructs a LowLevelAttributeHandler for a specific attribute node.
 *
 * @param id          Attribute ID of the node this handler was created for.
 * @param value       Decoded value of the triggering attribute.
 * @param parentToken Opaque token of the parent attribute node.
 * @param token       Opaque token assigned to this attribute node by the handler.
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
		*_handler = new(context) LowLevelAttributeHandler(id, value,
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
 * @brief Calls HandleAttributeDone() on the low-level handler when all children are done.
 *
 * @param context Parsing context.
 * @return B_OK on success, or an error code from the low-level handler.
 */
status_t
ReaderImplBase::LowLevelAttributeHandler::NotifyDone(
	AttributeHandlerContext* context)
{
	if (fID != B_HPKG_ATTRIBUTE_ID_ENUM_COUNT) {
		status_t error = context->lowLevelHandler->HandleAttributeDone(
			(BHPKGAttributeID)fID, fValue, fParentToken, fToken);
		if (error != B_OK)
			return error;
	}
	return super::NotifyDone(context);
}


// #pragma mark - ReaderImplBase


/**
 * @brief Constructs a ReaderImplBase with no file open yet.
 *
 * @param fileType    Human-readable label used in error messages ("package" etc.).
 * @param errorOutput Destination for diagnostic messages.
 */
ReaderImplBase::ReaderImplBase(const char* fileType, BErrorOutput* errorOutput)
	:
	fPackageAttributesSection("package attributes"),
	fFileType(fileType),
	fErrorOutput(errorOutput),
	fFile(NULL),
	fOwnsFile(false),
	fRawHeapReader(NULL),
	fHeapReader(NULL),
	fCurrentSection(NULL)
{
}


/**
 * @brief Destroys the ReaderImplBase and releases all heap reader resources.
 *
 * If fHeapReader and fRawHeapReader are different objects (a caching layer was
 * created on top of the raw reader), both are deleted independently.
 */
ReaderImplBase::~ReaderImplBase()
{
	delete fHeapReader;
	if (fRawHeapReader != fHeapReader)
		delete fRawHeapReader;

	if (fOwnsFile)
		delete fFile;
}


/**
 * @brief Returns the total uncompressed size of the heap region.
 *
 * @return Uncompressed heap size in bytes.
 */
uint64
ReaderImplBase::UncompressedHeapSize() const
{
	return fRawHeapReader->UncompressedHeapSize();
}


/**
 * @brief Transfers heap reader ownership to the caller, clearing internal pointers.
 *
 * Used by recompression logic that needs to take over the heap reader. After
 * this call both fHeapReader and fRawHeapReader are NULL.
 *
 * @param _rawHeapReader Output that receives the raw (non-caching) heap reader.
 * @return The (possibly caching) heap reader; the caller must delete both objects.
 */
BAbstractBufferedDataReader*
ReaderImplBase::DetachHeapReader(PackageFileHeapReader*& _rawHeapReader)
{
	BAbstractBufferedDataReader* heapReader = fHeapReader;
	_rawHeapReader = fRawHeapReader;
	fHeapReader = NULL;
	fRawHeapReader = NULL;

	return heapReader;
}


/**
 * @brief Creates and initialises the heap reader for the given compression parameters.
 *
 * Selects and instantiates the appropriate decompression algorithm (none, zlib,
 * or zstd), creates a PackageFileHeapReader, and optionally wraps it in a caching
 * layer via CreateCachedHeapReader().
 *
 * @param compression       B_HPKG_COMPRESSION_* constant identifying the algorithm.
 * @param chunkSize         Compressed chunk size used by the heap.
 * @param offset            Byte offset of the heap region in the file.
 * @param compressedSize    Compressed size of the heap in bytes.
 * @param uncompressedSize  Uncompressed size of the heap in bytes.
 * @return B_OK on success, or an error code on failure.
 */
status_t
ReaderImplBase::InitHeapReader(uint32 compression, uint32 chunkSize,
	off_t offset, uint64 compressedSize, uint64 uncompressedSize)
{
	DecompressionAlgorithmOwner* decompressionAlgorithm = NULL;
	BReference<DecompressionAlgorithmOwner> decompressionAlgorithmReference;

	switch (compression) {
		case B_HPKG_COMPRESSION_NONE:
			break;
		case B_HPKG_COMPRESSION_ZLIB:
			decompressionAlgorithm = DecompressionAlgorithmOwner::Create(
				new(std::nothrow) BZlibCompressionAlgorithm,
				new(std::nothrow) BZlibDecompressionParameters);
			decompressionAlgorithmReference.SetTo(decompressionAlgorithm, true);
			if (decompressionAlgorithm == NULL
				|| decompressionAlgorithm->algorithm == NULL
				|| decompressionAlgorithm->parameters == NULL) {
				return B_NO_MEMORY;
			}
			break;
		case B_HPKG_COMPRESSION_ZSTD:
			decompressionAlgorithm = DecompressionAlgorithmOwner::Create(
				new(std::nothrow) BZstdCompressionAlgorithm,
				new(std::nothrow) BZstdDecompressionParameters);
			decompressionAlgorithmReference.SetTo(decompressionAlgorithm, true);
			if (decompressionAlgorithm == NULL
				|| decompressionAlgorithm->algorithm == NULL
				|| decompressionAlgorithm->parameters == NULL) {
				return B_NO_MEMORY;
			}
			break;
		default:
			fErrorOutput->PrintError("Error: Invalid heap compression\n");
			return B_BAD_DATA;
	}

	fRawHeapReader = new(std::nothrow) PackageFileHeapReader(fErrorOutput,
		fFile, offset, compressedSize, uncompressedSize,
		decompressionAlgorithm);
	if (fRawHeapReader == NULL)
		return B_NO_MEMORY;

	status_t error = fRawHeapReader->Init();
	if (error != B_OK)
		return error;

	error = CreateCachedHeapReader(fRawHeapReader, fHeapReader);
	if (error != B_OK) {
		if (error != B_NOT_SUPPORTED)
			return error;

		fHeapReader = fRawHeapReader;
	}

	return B_OK;
}


/**
 * @brief Hook for subclasses to wrap the raw heap reader in a caching layer.
 *
 * The default implementation returns B_NOT_SUPPORTED, which causes InitHeapReader()
 * to use the raw reader directly. Subclasses that implement read-ahead or caching
 * should override this method.
 *
 * @param heapReader    The raw PackageFileHeapReader to wrap.
 * @param _cachedReader Output for the new caching reader.
 * @return B_NOT_SUPPORTED by default; an error code on failure in overrides.
 */
status_t
ReaderImplBase::CreateCachedHeapReader(PackageFileHeapReader* heapReader,
	BAbstractBufferedDataReader*& _cachedReader)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Validates and stores section bounds relative to the end of the file.
 *
 * Checks that @a length does not exceed @a endOffset, enforces a sanity limit,
 * and validates the strings subsection description.
 *
 * @param section         Section descriptor to populate.
 * @param endOffset       Byte offset just past the end of this section in the file.
 * @param length          Total uncompressed byte length of the section.
 * @param maxSaneLength   Maximum accepted section length (0 = no limit).
 * @param stringsLength   Byte length of the strings subsection.
 * @param stringsCount    Number of strings recorded in the header.
 * @return B_OK on success, B_BAD_DATA if the lengths are inconsistent.
 */
status_t
ReaderImplBase::InitSection(PackageFileSection& section, uint64 endOffset,
	uint64 length, uint64 maxSaneLength, uint64 stringsLength,
	uint64 stringsCount)
{
	// check length vs. endOffset
	if (length > endOffset) {
		ErrorOutput()->PrintError("Error: %s file %s section size is %"
			B_PRIu64 " bytes. This is greater than the available space\n",
			fFileType, section.name, length);
		return B_BAD_DATA;
	}

	// check sanity length
	if (maxSaneLength > 0 && length > maxSaneLength) {
		ErrorOutput()->PrintError("Error: %s file %s section size is %"
			B_PRIu64 " bytes. This is beyond the reader's sanity limit\n",
			fFileType, section.name, length);
		return B_NOT_SUPPORTED;
	}

	// check strings subsection size/count
	if ((stringsLength <= 1) != (stringsCount == 0) || stringsLength > length) {
		ErrorOutput()->PrintError("Error: strings subsection description of %s "
			"file %s section is invalid (%" B_PRIu64 " strings, length: %"
			B_PRIu64 ", section length: %" B_PRIu64 ")\n",
			fFileType, section.name, stringsCount, stringsLength, length);
		return B_BAD_DATA;
	}

	section.uncompressedLength = length;
	section.offset = endOffset - length;
	section.currentOffset = 0;
	section.stringsLength = stringsLength;
	section.stringsCount = stringsCount;

	return B_OK;
}


/**
 * @brief Reads the section data from the heap and parses its string table.
 *
 * Allocates a buffer for the section, decompresses the data from the heap,
 * then calls ParseStrings() to fill the per-section string pointer array.
 *
 * @param section Section descriptor previously initialised by InitSection().
 * @return B_OK on success, or an error code if I/O or parsing fails.
 */
status_t
ReaderImplBase::PrepareSection(PackageFileSection& section)
{
	// allocate memory for the section data and read it in
	section.data = new(std::nothrow) uint8[section.uncompressedLength];
	if (section.data == NULL) {
		ErrorOutput()->PrintError("Error: Out of memory!\n");
		return B_NO_MEMORY;
	}

	status_t error = ReadSection(section);
	if (error != B_OK)
		return error;

	// parse the section strings
	section.currentOffset = 0;
	SetCurrentSection(&section);

	error = ParseStrings();
	if (error != B_OK)
		return error;

	return B_OK;
}


/**
 * @brief Parses the string table at the beginning of the current section.
 *
 * Iterates the null-terminated strings in the string region of fCurrentSection,
 * builds the pointer array fCurrentSection->strings, and advances currentOffset
 * past the strings region.
 *
 * @return B_OK on success, or B_BAD_DATA / B_NO_MEMORY if the table is malformed.
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
 * @brief Parses the package attributes section using the supplied handler stack.
 *
 * Activates the package attributes section, seeds the handler stack with
 * @a rootAttributeHandler, and drives the attribute tree parser. Cleans up
 * the handler stack if an error occurs.
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
 * @brief Wraps _ParseAttributeTree with optional low-level section start/end calls.
 *
 * If a low-level handler is active, calls HandleSectionStart() first; if the
 * handler declines the section, returns early with @a _sectionHandled set to false.
 * On completion calls HandleSectionEnd().
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
 * @brief Stores the supplied file pointer and takes optional ownership.
 *
 * @param file      BPositionIO stream to use for reading.
 * @param keepFile  If true, the reader takes ownership and will delete the file.
 * @return B_OK on success, or B_BAD_VALUE if @a file is NULL.
 */
status_t
ReaderImplBase::_Init(BPositionIO* file, bool keepFile)
{
	fFile = file;
	fOwnsFile = keepFile;
	return fFile != NULL ? B_OK : B_BAD_VALUE;
}


/**
 * @brief Iterates the attribute tree using the handler stack until a terminal tag.
 *
 * Reads attribute tags and values in a loop; pops the handler stack and calls
 * NotifyDone() when a zero tag (end-of-children marker) is encountered.
 * Creates child handlers or IgnoreAttributeHandlers as the tree descends.
 *
 * @param context Parsing context.
 * @return B_OK on success, or an error code from any handler or I/O operation.
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
			error = handler->NotifyDone(context);
			if (error != B_OK)
				return error;
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
				childHandler = new(context) IgnoreAttributeHandler;
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
 * @brief Reads and decodes a single attribute tag and its associated value.
 *
 * Decodes the LEB128 tag to extract type, ID, encoding, and has-children flag,
 * then calls ReadAttributeValue() to decode the payload.
 *
 * @param _id          Output for the attribute ID byte.
 * @param _value       Output for the decoded attribute value.
 * @param _hasChildren Output for the has-children flag; may be NULL.
 * @param _tag         Output for the raw tag value; may be NULL.
 * @return B_OK on success, or B_BAD_DATA if the tag is malformed.
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

		// get the ID
		_id = attribute_tag_id(tag);
		if (_id < B_HPKG_ATTRIBUTE_ID_ENUM_COUNT) {
			if (type != kAttributeTypes[_id]) {
				fErrorOutput->PrintError("Error: Invalid %s section: "
					"unexpected type %d for attribute id %d (expected %d)!\n",
					fCurrentSection->name, type, _id, kAttributeTypes[_id]);
				return B_BAD_DATA;
			}
		} else if (fMinorFormatVersion <= fCurrentMinorFormatVersion) {
			fErrorOutput->PrintError("Error: Invalid %s section: "
				"attribute id %d not supported!\n", fCurrentSection->name, _id);
			return B_BAD_DATA;
		}

		// get the value
		error = ReadAttributeValue(type, attribute_tag_encoding(tag),
			_value);
		if (error != B_OK)
			return error;
	}

	if (_hasChildren != NULL)
		*_hasChildren = attribute_tag_has_children(tag);
	if (_tag != NULL)
		*_tag = tag;

	return B_OK;
}


/**
 * @brief Decodes the payload of an attribute given its type and encoding.
 *
 * Reads the appropriate number of bytes from the current section buffer and
 * stores the result in @a _value.
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
 * @brief Reads an unsigned LEB128-encoded integer from the current section.
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
 * @brief Reads a null-terminated string from the current section buffer.
 *
 * Returns a pointer into the section's in-memory buffer (no copy). Advances
 * the section cursor past the string and its terminating null byte.
 *
 * @param _string       Output pointer into the section buffer.
 * @param _stringLength Optional output for the string length excluding the null.
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
 * @brief Reads @a size bytes from the current section into @a buffer.
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
		fErrorOutput->PrintError(
			"_ReadSectionBuffer(%lu): read beyond %s end\n", size,
			fCurrentSection->name);
		return B_BAD_DATA;
	}

	memcpy(buffer, fCurrentSection->data + fCurrentSection->currentOffset,
		size);
	fCurrentSection->currentOffset += size;
	return B_OK;
}


/**
 * @brief Reads @a size bytes from the underlying file at the given absolute offset.
 *
 * @param offset Absolute byte offset into the file.
 * @param buffer Destination buffer.
 * @param size   Number of bytes to read.
 * @return B_OK on success, or an I/O error code.
 */
status_t
ReaderImplBase::ReadBuffer(off_t offset, void* buffer, size_t size)
{
	status_t error = fFile->ReadAtExactly(offset, buffer, size);
	if (error != B_OK) {
		fErrorOutput->PrintError("_ReadBuffer(%p, %lu) failed to read data: "
			"%s\n", buffer, size, strerror(error));
		return error;
	}

	return B_OK;
}


/**
 * @brief Reads a complete section from the heap into its preallocated buffer.
 *
 * @param section Section whose data pointer and offset describe what to read.
 * @return B_OK on success, or an error code from the heap reader.
 */
status_t
ReaderImplBase::ReadSection(const PackageFileSection& section)
{
	return fHeapReader->ReadData(section.offset,
		section.data, section.uncompressedLength);
}


}	// namespace BPrivate

}	// namespace BHPKG

}	// namespace BPackageKit
