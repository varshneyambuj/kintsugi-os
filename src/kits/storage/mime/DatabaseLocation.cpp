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
 *   Copyright 2002-2014 Haiku, Inc. All Rights Reserved.
 *   Authors: Tyler Dauwalder, Rene Gollent, Ingo Weinhold
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file DatabaseLocation.cpp
 * @brief Provides path resolution and node I/O for the MIME database.
 *
 * DatabaseLocation manages a list of directories that collectively make up
 * the MIME database. It provides methods to open, create, read, write, and
 * delete MIME type nodes and their attributes. The first directory in the
 * list is the writable (user-settings) directory; additional directories
 * are read-only system locations consulted as fallbacks.
 *
 * @see Database
 */

#include <mime/DatabaseLocation.h>

#include <stdlib.h>
#include <syslog.h>

#include <new>

#include <Bitmap.h>
#include <DataIO.h>
#include <Directory.h>
#include <File.h>
#include <fs_attr.h>
#include <IconUtils.h>
#include <Message.h>
#include <Node.h>

#include <AutoDeleter.h>
#include <mime/database_support.h>


namespace BPrivate {
namespace Storage {
namespace Mime {


/**
 * @brief Constructs a DatabaseLocation with an empty directory list.
 */
DatabaseLocation::DatabaseLocation()
	:
	fDirectories()
{
}


/**
 * @brief Destroys the DatabaseLocation object.
 */
DatabaseLocation::~DatabaseLocation()
{
}


/**
 * @brief Appends a directory path to the list of MIME database directories.
 *
 * @param directory The directory path to add.
 * @return true if the directory was added successfully, false if the string
 *         was empty or the addition failed.
 */
bool
DatabaseLocation::AddDirectory(const BString& directory)
{
	return !directory.IsEmpty() && fDirectories.Add(directory);
}


/**
 * @brief Opens a read-only BNode for the given MIME type.
 *
 * Searches all registered directories for a node corresponding to the given
 * type. Fails with B_ENTRY_NOT_FOUND if the type has no matching file.
 *
 * @param type The MIME type string to open.
 * @param _node Reference to a BNode that will be set on success.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if not found, or another error.
 */
status_t
DatabaseLocation::OpenType(const char* type, BNode& _node) const
{
	if (type == NULL)
		return B_BAD_VALUE;

	int32 index;
	return _OpenType(type, _node, index);
}


/**
 * @brief Opens or creates a writable BNode for the given MIME type.
 *
 * If the type already exists in the writable directory (index 0), it is
 * opened directly. If it exists only in a read-only directory, a copy is
 * made in the writable directory (and *_didCreate is set to true). If the
 * type does not exist and @a create is true, a new node is created.
 *
 * @param type     The MIME type string.
 * @param _node    Reference to a BNode set on success.
 * @param create   If true, create the node when it does not yet exist.
 * @param _didCreate Optional pointer; set to true when a new node is created.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if the type does not exist and
 *         @a create is false, or another error code.
 */
status_t
DatabaseLocation::OpenWritableType(const char* type, BNode& _node, bool create,
	bool* _didCreate) const
{
	if (_didCreate)
		*_didCreate = false;

	// See, if the type already exists.
	int32 index;
	status_t result = _OpenType(type, _node, index);
	if (result == B_OK) {
		if (index == 0)
			return B_OK;
		else if (!create)
			return B_ENTRY_NOT_FOUND;

		// The caller wants a editable node, but the node found is not in the
		// user's settings directory. Copy the node.
		BNode nodeToClone(_node);
		if (nodeToClone.InitCheck() != B_OK)
			return nodeToClone.InitCheck();

		result = _CopyTypeNode(nodeToClone, type, _node);
		if (result != B_OK) {
			_node.Unset();
			return result;
		}

		if (_didCreate != NULL)
			*_didCreate = true;

		return result;
	} else if (!create)
		return B_ENTRY_NOT_FOUND;

	// type doesn't exist yet -- create the respective node
	result = _CreateTypeNode(type, _node);
	if (result != B_OK)
		return result;

	// write the type attribute
	size_t toWrite = strlen(type) + 1;
	ssize_t bytesWritten = _node.WriteAttr(kTypeAttr, B_STRING_TYPE, 0, type,
		toWrite);
	if (bytesWritten < 0)
		result = bytesWritten;
	else if ((size_t)bytesWritten != toWrite)
		result = B_FILE_ERROR;

	if (result != B_OK) {
		_node.Unset();
		return result;
	}

	if (_didCreate != NULL)
		*_didCreate = true;
	return B_OK;
}


/**
 * @brief Reads raw attribute data for the given MIME type.
 *
 * @param type      The MIME type string.
 * @param attribute The attribute name.
 * @param data      Buffer into which data is read.
 * @param length    Maximum number of bytes to read.
 * @param datatype  Expected attribute type code.
 * @return Number of bytes read on success, or a negative error code.
 */
ssize_t
DatabaseLocation::ReadAttribute(const char* type, const char* attribute,
	void* data, size_t length, type_code datatype) const
{
	if (type == NULL || attribute == NULL || data == NULL)
		return B_BAD_VALUE;

	BNode node;
	status_t result = OpenType(type, node);
	if (result != B_OK)
		return result;

	return node.ReadAttr(attribute, datatype, 0, data, length);
}


/**
 * @brief Reads a flattened BMessage from an attribute of the given MIME type.
 *
 * @param type      The MIME type string.
 * @param attribute The attribute name.
 * @param _message  Reference to a pre-allocated BMessage filled on success.
 * @return B_OK on success, or an error code on failure.
 */
status_t
DatabaseLocation::ReadMessageAttribute(const char* type, const char* attribute,
	BMessage& _message) const
{
	if (type == NULL || attribute == NULL)
		return B_BAD_VALUE;

	BNode node;
	attr_info info;

	status_t result = OpenType(type, node);
	if (result != B_OK)
		return result;

	result = node.GetAttrInfo(attribute, &info);
	if (result != B_OK)
		return result;

	if (info.type != B_MESSAGE_TYPE)
		return B_BAD_VALUE;

	void* buffer = malloc(info.size);
	if (buffer == NULL)
		return B_NO_MEMORY;
	MemoryDeleter bufferDeleter(buffer);

	ssize_t bytesRead = node.ReadAttr(attribute, B_MESSAGE_TYPE, 0, buffer,
		info.size);
	if (bytesRead != info.size)
		return bytesRead < 0 ? (status_t)bytesRead : (status_t)B_FILE_ERROR;

	return _message.Unflatten((const char*)buffer);
}


/**
 * @brief Reads a string attribute for the given MIME type into a BString.
 *
 * @param type      The MIME type string.
 * @param attribute The attribute name.
 * @param _string   Reference to a BString that receives the value on success.
 * @return B_OK on success, or an error code on failure.
 */
status_t
DatabaseLocation::ReadStringAttribute(const char* type, const char* attribute,
	BString& _string) const
{
	if (type == NULL || attribute == NULL)
		return B_BAD_VALUE;

	BNode node;
	status_t result = OpenType(type, node);
	if (result != B_OK)
		return result;

	return node.ReadAttrString(attribute, &_string);
}


/**
 * @brief Writes raw attribute data for the given MIME type.
 *
 * Creates the type node if it does not already exist.
 *
 * @param type       The MIME type string.
 * @param attribute  The attribute name.
 * @param data       Pointer to the data to write.
 * @param length     Number of bytes to write.
 * @param datatype   Type code for the attribute.
 * @param _didCreate Optional pointer; set to true if the type node was created.
 * @return B_OK on success, or an error code on failure.
 */
status_t
DatabaseLocation::WriteAttribute(const char* type, const char* attribute,
	const void* data, size_t length, type_code datatype, bool* _didCreate) const
{
	if (type == NULL || attribute == NULL || data == NULL)
		return B_BAD_VALUE;

	BNode node;
	status_t result = OpenWritableType(type, node, true, _didCreate);
	if (result != B_OK)
		return result;

	ssize_t bytesWritten = node.WriteAttr(attribute, datatype, 0, data, length);
	if (bytesWritten < 0)
		return bytesWritten;
	return bytesWritten == (ssize_t)length
		? (status_t)B_OK : (status_t)B_FILE_ERROR;
}


/**
 * @brief Flattens a BMessage and writes it as an attribute of the given MIME type.
 *
 * Creates the type node if it does not already exist.
 *
 * @param type       The MIME type string.
 * @param attribute  The attribute name.
 * @param message    The BMessage to flatten and store.
 * @param _didCreate Optional pointer; set to true if the type node was created.
 * @return B_OK on success, or an error code on failure.
 */
status_t
DatabaseLocation::WriteMessageAttribute(const char* type, const char* attribute,
	const BMessage& message, bool* _didCreate) const
{
	BMallocIO data;
	status_t result = data.SetSize(message.FlattenedSize());
	if (result != B_OK)
		return result;

	ssize_t bytes;
	result = message.Flatten(&data, &bytes);
	if (result != B_OK)
		return result;

	return WriteAttribute(type, attribute, data.Buffer(), data.BufferLength(),
		B_MESSAGE_TYPE, _didCreate);
}


/**
 * @brief Removes an attribute from the given MIME type's node.
 *
 * @param type      The MIME type string.
 * @param attribute The attribute name to remove.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if the type or attribute does
 *         not exist, or another error code.
 */
status_t
DatabaseLocation::DeleteAttribute(const char* type, const char* attribute) const
{
	if (type == NULL || attribute == NULL)
		return B_BAD_VALUE;

	BNode node;
	status_t result = OpenWritableType(type, node, false);
	if (result != B_OK)
		return result;

	return node.RemoveAttr(attribute);
}


/**
 * @brief Fetches the application hint (preferred application path) for the type.
 *
 * @param type The MIME type of interest.
 * @param _ref Reference to a pre-allocated entry_ref filled with the hint path.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no hint exists, or an error code.
 */
status_t
DatabaseLocation::GetAppHint(const char* type, entry_ref& _ref)
{
	if (type == NULL)
		return B_BAD_VALUE;

	char path[B_PATH_NAME_LENGTH];
	BEntry entry;
	ssize_t status = ReadAttribute(type, kAppHintAttr, path, B_PATH_NAME_LENGTH,
		kAppHintType);

	if (status >= B_OK)
		status = entry.SetTo(path);
	if (status == B_OK)
		status = entry.GetRef(&_ref);

	return status;
}


/**
 * @brief Retrieves the attribute-info message describing file attributes for the type.
 *
 * The returned message follows the format described by BMimeType::SetAttrInfo().
 * An empty message is returned when no attribute info has been stored yet.
 *
 * @param type  The MIME type of interest.
 * @param _info Reference to a pre-allocated BMessage filled on success.
 * @return B_OK on success, or an error code on failure.
 */
status_t
DatabaseLocation::GetAttributesInfo(const char* type, BMessage& _info)
{
	status_t result = ReadMessageAttribute(type, kAttrInfoAttr, _info);

	if (result == B_ENTRY_NOT_FOUND) {
		// return an empty message
		_info.MakeEmpty();
		result = B_OK;
	}

	if (result == B_OK) {
		_info.what = 233;
			// Don't know why, but that's what R5 does.
		result = _info.AddString("type", type);
	}

	return result;
}


/**
 * @brief Retrieves the short description string for the given MIME type.
 *
 * @param type        The MIME type of interest.
 * @param description Pre-allocated buffer of at least B_MIME_TYPE_LENGTH bytes.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no short description exists,
 *         or another error code.
 */
status_t
DatabaseLocation::GetShortDescription(const char* type, char* description)
{
	ssize_t result = ReadAttribute(type, kShortDescriptionAttr, description,
		B_MIME_TYPE_LENGTH, kShortDescriptionType);

	return result >= 0 ? B_OK : result;
}


/**
 * @brief Retrieves the long description string for the given MIME type.
 *
 * @param type        The MIME type of interest.
 * @param description Pre-allocated buffer of at least B_MIME_TYPE_LENGTH bytes.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no long description exists,
 *         or another error code.
 */
status_t
DatabaseLocation::GetLongDescription(const char* type, char* description)
{
	ssize_t result = ReadAttribute(type, kLongDescriptionAttr, description,
		B_MIME_TYPE_LENGTH, kLongDescriptionType);

	return result >= 0 ? B_OK : result;
}


/**
 * @brief Retrieves the file-extension list message for the given MIME type.
 *
 * The message format is described by BMimeType::GetFileExtensions(). An empty
 * message is returned when no extensions have been stored.
 *
 * @param type        The MIME type of interest.
 * @param _extensions Reference to a pre-allocated BMessage filled on success.
 * @return B_OK on success, or an error code on failure.
 */
status_t
DatabaseLocation::GetFileExtensions(const char* type, BMessage& _extensions)
{
	status_t result = ReadMessageAttribute(type, kFileExtensionsAttr, _extensions);
	if (result == B_ENTRY_NOT_FOUND) {
		// return an empty message
		_extensions.MakeEmpty();
		result = B_OK;
	}

	if (result == B_OK) {
		_extensions.what = 234;	// Don't know why, but that's what R5 does.
		result = _extensions.AddString("type", type);
	}

	return result;
}


/**
 * @brief Retrieves the bitmap icon of the requested size for the given MIME type.
 *
 * @param type  The MIME type of interest.
 * @param _icon Reference to a pre-allocated BBitmap of correct size and depth.
 * @param size  Desired icon size (B_LARGE_ICON or B_MINI_ICON).
 * @return B_OK on success, or an error code on failure.
 */
status_t
DatabaseLocation::GetIcon(const char* type, BBitmap& _icon, icon_size size)
{
	return GetIconForType(type, NULL, _icon, size);
}


/**
 * @brief Retrieves the raw vector icon data for the given MIME type.
 *
 * The caller is responsible for freeing the returned buffer with delete[].
 *
 * @param type  The MIME type of interest.
 * @param _data Reference via which the allocated icon buffer is returned.
 * @param _size Reference via which the buffer size in bytes is returned.
 * @return B_OK on success, or an error code on failure.
 */
status_t
DatabaseLocation::GetIcon(const char* type, uint8*& _data, size_t& _size)
{
	return GetIconForType(type, NULL, _data, _size);
}


/**
 * @brief Retrieves the bitmap icon an application uses for files of a given type.
 *
 * @param type     The application MIME type.
 * @param fileType The file MIME type whose custom icon is requested. Pass NULL
 *                 to retrieve the application's own icon.
 * @param _icon    Reference to a pre-allocated BBitmap of correct size/depth.
 * @param which    B_LARGE_ICON or B_MINI_ICON.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no matching icon exists,
 *         or another error code.
 */
status_t
DatabaseLocation::GetIconForType(const char* type, const char* fileType,
	BBitmap& _icon, icon_size which)
{
	if (type == NULL)
		return B_BAD_VALUE;

	// open the node for the given type
	BNode node;
	status_t result = OpenType(type, node);
	if (result != B_OK)
		return result;

	// construct our attribute name
	BString vectorIconAttrName;
	BString smallIconAttrName;
	BString largeIconAttrName;

	if (fileType != NULL) {
		BString lowerCaseFileType(fileType);
		lowerCaseFileType.ToLower();

		vectorIconAttrName << kIconAttrPrefix << lowerCaseFileType;
		smallIconAttrName << kMiniIconAttrPrefix << lowerCaseFileType;
		largeIconAttrName << kLargeIconAttrPrefix << lowerCaseFileType;
	} else {
		vectorIconAttrName = kIconAttr;
		smallIconAttrName = kMiniIconAttr;
		largeIconAttrName = kLargeIconAttr;
	}

	return BIconUtils::GetIcon(&node, vectorIconAttrName, smallIconAttrName,
		largeIconAttrName, which, &_icon);
}


/**
 * @brief Retrieves the raw vector icon an application uses for files of a given type.
 *
 * The caller is responsible for freeing the returned buffer with delete[].
 *
 * @param type     The application MIME type.
 * @param fileType The file MIME type whose custom icon is requested. Pass NULL
 *                 to retrieve the application's own vector icon.
 * @param _data    Reference via which the allocated icon buffer is returned.
 * @param _size    Reference via which the buffer size in bytes is returned.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no vector icon exists,
 *         or another error code.
 */
status_t
DatabaseLocation::GetIconForType(const char* type, const char* fileType,
	uint8*& _data, size_t& _size)
{
	if (type == NULL)
		return B_BAD_VALUE;

	// open the node for the given type
	BNode node;
	status_t result = OpenType(type, node);
	if (result != B_OK)
		return result;

	// construct our attribute name
	BString iconAttrName;

	if (fileType != NULL)
		iconAttrName << kIconAttrPrefix << BString(fileType).ToLower();
	else
		iconAttrName = kIconAttr;

	// get info about attribute for that name
	attr_info info;
	if (result == B_OK)
		result = node.GetAttrInfo(iconAttrName, &info);

	// validate attribute type
	if (result == B_OK)
		result = (info.type == B_VECTOR_ICON_TYPE) ? B_OK : B_BAD_VALUE;

	// allocate a buffer and read the attribute data into it
	if (result == B_OK) {
		uint8* buffer = new(std::nothrow) uint8[info.size];
		if (buffer == NULL)
			result = B_NO_MEMORY;

		ssize_t bytesRead = -1;
		if (result == B_OK) {
			bytesRead = node.ReadAttr(iconAttrName, B_VECTOR_ICON_TYPE, 0, buffer,
				info.size);
		}

		if (bytesRead >= 0)
			result = bytesRead == info.size ? B_OK : B_FILE_ERROR;

		if (result == B_OK) {
			// success, set data pointer and size
			_data = buffer;
			_size = info.size;
		} else
			delete[] buffer;
	}

	return result;
}


/**
 * @brief Retrieves the preferred application signature for the given action.
 *
 * Currently only B_OPEN is a supported app_verb.
 *
 * @param type      The MIME type of interest.
 * @param signature Pre-allocated buffer of at least B_MIME_TYPE_LENGTH bytes.
 * @param verb      The application action (currently only B_OPEN is supported).
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no preferred app is set,
 *         or another error code.
 */
status_t
DatabaseLocation::GetPreferredApp(const char* type, char* signature,
	app_verb verb)
{
	// Since B_OPEN is the currently the only app_verb, it is essentially
	// ignored
	ssize_t result = ReadAttribute(type, kPreferredAppAttr, signature,
		B_MIME_TYPE_LENGTH, kPreferredAppType);

	return result >= 0 ? B_OK : result;
}


/**
 * @brief Retrieves the sniffer rule string for the given MIME type.
 *
 * @param type    The MIME type of interest.
 * @param _result Reference to a BString that receives the rule on success.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no rule has been set,
 *         or another error code.
 */
status_t
DatabaseLocation::GetSnifferRule(const char* type, BString& _result)
{
	return ReadStringAttribute(type, kSnifferRuleAttr, _result);
}


/**
 * @brief Retrieves the supported-types message for the given MIME type.
 *
 * An empty message is returned when no supported types have been stored.
 *
 * @param type   The MIME type of interest.
 * @param _types Reference to a pre-allocated BMessage filled on success.
 * @return B_OK on success, or an error code on failure.
 */
status_t
DatabaseLocation::GetSupportedTypes(const char* type, BMessage& _types)
{
	status_t result = ReadMessageAttribute(type, kSupportedTypesAttr, _types);
	if (result == B_ENTRY_NOT_FOUND) {
		// return an empty message
		_types.MakeEmpty();
		result = B_OK;
	}
	if (result == B_OK) {
		_types.what = 0;
		result = _types.AddString("type", type);
	}

	return result;
}


/**
 * @brief Checks whether the given MIME type is present in the database.
 *
 * @param type The MIME type string to check.
 * @return true if the type exists in any registered directory, false otherwise.
 */
bool
DatabaseLocation::IsInstalled(const char* type)
{
	BNode node;
	return OpenType(type, node) == B_OK;
}


/**
 * @brief Builds the filesystem path for a MIME type in a specific directory slot.
 *
 * @param type  The MIME type string (will be lowercased).
 * @param index Index into the registered directory list.
 * @return The full path as a BString.
 */
BString
DatabaseLocation::_TypeToFilename(const char* type, int32 index) const
{
	BString path = fDirectories.StringAt(index);
	return path << '/' << BString(type).ToLower();
}


/**
 * @brief Searches all directories for a node matching the given MIME type.
 *
 * @param type   The MIME type string.
 * @param _node  Reference to a BNode set on success.
 * @param _index Reference set to the index of the directory where found.
 * @return B_OK if found, B_ENTRY_NOT_FOUND otherwise.
 */
status_t
DatabaseLocation::_OpenType(const char* type, BNode& _node, int32& _index) const
{
	int32 count = fDirectories.CountStrings();
	for (int32 i = 0; i < count; i++) {
		status_t result = _node.SetTo(_TypeToFilename(type, i));
		attr_info attrInfo;
		if (result == B_OK && _node.GetAttrInfo(kTypeAttr, &attrInfo) == B_OK) {
			_index = i;
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Creates a new type node (file or directory) in the writable directory.
 *
 * For subtypes ("supertype/subtype"), this creates the supertype directory if
 * needed and then creates the subtype file inside it. For supertypes, only the
 * directory is created.
 *
 * @param type  The MIME type string.
 * @param _node Reference to a BNode set to the newly created node on success.
 * @return B_OK on success, or an error code on failure.
 */
status_t
DatabaseLocation::_CreateTypeNode(const char* type, BNode& _node) const
{
	const char* slash = strchr(type, '/');
	BString superTypeName;
	if (slash != NULL)
		superTypeName.SetTo(type, slash - type);
	else
		superTypeName = type;
	superTypeName.ToLower();

	// open/create the directory for the supertype
	BDirectory parent(WritableDirectory());
	status_t result = parent.InitCheck();
	if (result != B_OK)
		return result;

	BDirectory superTypeDirectory;
	if (BEntry(&parent, superTypeName).Exists())
		result = superTypeDirectory.SetTo(&parent, superTypeName);
	else
		result = parent.CreateDirectory(superTypeName, &superTypeDirectory);

	if (result != B_OK)
		return result;

	// create the subtype
	BFile subTypeFile;
	if (slash != NULL) {
		result = superTypeDirectory.CreateFile(BString(slash + 1).ToLower(),
			&subTypeFile);
		if (result != B_OK)
			return result;
	}

	// assign the result
	if (slash != NULL)
		_node = subTypeFile;
	else
		_node = superTypeDirectory;
	return _node.InitCheck();
}


/**
 * @brief Copies all attributes from a source node into a newly created type node.
 *
 * Used to promote a read-only type entry to a writable copy in the user's
 * settings directory. Errors during individual attribute copies are logged but
 * do not abort the overall operation.
 *
 * @param source  The source BNode to copy attributes from.
 * @param type    The MIME type string (used to create the target node).
 * @param _target Reference to a BNode set to the newly created target.
 * @return B_OK on success, or an error code if node creation fails.
 */
status_t
DatabaseLocation::_CopyTypeNode(BNode& source, const char* type, BNode& _target)
	const
{
	status_t result = _CreateTypeNode(type, _target);
	if (result != B_OK)
		return result;

	// copy the attributes
	MemoryDeleter bufferDeleter;
	size_t bufferSize = 0;

	source.RewindAttrs();
	char attribute[B_ATTR_NAME_LENGTH];
	while (source.GetNextAttrName(attribute) == B_OK) {
		attr_info info;
		result = source.GetAttrInfo(attribute, &info);
		if (result != B_OK) {
			syslog(LOG_ERR, "Failed to get info for attribute \"%s\" of MIME "
				"type \"%s\": %s", attribute, type, strerror(result));
			continue;
		}

		// resize our buffer, if necessary
		if (info.size > (off_t)bufferSize) {
			bufferDeleter.SetTo(malloc(info.size));
			if (!bufferDeleter.IsSet())
				return B_NO_MEMORY;
			bufferSize = info.size;
		}

		ssize_t bytesRead = source.ReadAttr(attribute, info.type, 0,
			bufferDeleter.Get(), info.size);
		if (bytesRead != info.size) {
			syslog(LOG_ERR, "Failed to read attribute \"%s\" of MIME "
				"type \"%s\": %s", attribute, type,
		  		bytesRead < 0 ? strerror(bytesRead) : "short read");
			continue;
		}

		ssize_t bytesWritten = _target.WriteAttr(attribute, info.type, 0,
			bufferDeleter.Get(), info.size);
		if (bytesWritten < 0) {
			syslog(LOG_ERR, "Failed to write attribute \"%s\" of MIME "
				"type \"%s\": %s", attribute, type,
		  		bytesWritten < 0 ? strerror(bytesWritten) : "short write");
			continue;
		}
	}

	return B_OK;
}


} // namespace Mime
} // namespace Storage
} // namespace BPrivate
