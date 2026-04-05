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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2002-2006, Haiku.
 *   Authors: Tyler Dauwalder, Ingo Weinhold, Axel Dörfler
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file InstalledTypes.cpp
 * @brief Maintains the complete list of installed MIME types in the database.
 *
 * InstalledTypes builds and caches an in-memory representation of every MIME
 * type (both supertypes and subtypes) present in the MIME database. The list
 * is built lazily on the first query and is updated incrementally as types are
 * added or removed via AddType() and RemoveType().
 *
 * @see Database
 */

#include <mime/InstalledTypes.h>

#include <stdio.h>

#include <new>

#include <Directory.h>
#include <Entry.h>
#include <Message.h>
#include <MimeType.h>
#include <String.h>

#include <mime/database_support.h>
#include <mime/DatabaseDirectory.h>
#include <storage_support.h>


#define DBG(x) x
//#define DBG(x)
#define OUT printf

namespace BPrivate {
namespace Storage {
namespace Mime {

/*!
	\class InstalledTypes
	\brief Installed types information for the entire database
*/

/**
 * @brief Constructs an InstalledTypes object.
 *
 * @param databaseLocation Pointer to the DatabaseLocation used to scan the
 *                         MIME database directories.
 */
InstalledTypes::InstalledTypes(DatabaseLocation* databaseLocation)
	:
	fDatabaseLocation(databaseLocation),
	fCachedMessage(NULL),
	fCachedSupertypesMessage(NULL),
	fHaveDoneFullBuild(false)
{
}


/**
 * @brief Destroys the InstalledTypes object and frees cached messages.
 */
InstalledTypes::~InstalledTypes()
{
	delete fCachedSupertypesMessage;
	delete fCachedMessage;
}


/**
 * @brief Returns a BMessage listing all currently installed MIME types.
 *
 * The result is written into the pre-allocated BMessage pointed to by @a types.
 * Results are cached; the cache is used on subsequent calls unless invalidated.
 * Triggers a full database scan on the first call.
 *
 * @param types Pointer to a pre-allocated BMessage that receives all types.
 * @return B_OK on success, or an error code on failure.
 */
status_t
InstalledTypes::GetInstalledTypes(BMessage *types)
{
	status_t err = types ? B_OK : B_BAD_VALUE;
	// See if we need to do our initial build still
	if (!err && !fHaveDoneFullBuild)
		err = _BuildInstalledTypesList();

	// See if we need to fill up a new message
	if (!err && !fCachedMessage)
		err = _CreateMessageWithTypes(&fCachedMessage);

	// If we get this far, there a cached message waiting
	if (!err)
		*types = *fCachedMessage;

	return err;
}


/**
 * @brief Returns a BMessage listing all installed subtypes of the given supertype.
 *
 * @param supertype The supertype string (e.g. "text", "image").
 * @param types     Pointer to a pre-allocated BMessage that receives subtypes.
 * @return B_OK on success, B_NAME_NOT_FOUND if the supertype is not installed,
 *         B_BAD_VALUE if @a supertype is not a pure supertype, or another error.
 */
status_t
InstalledTypes::GetInstalledTypes(const char *supertype, BMessage *types)
{
	if (supertype == NULL || types == NULL)
		return B_BAD_VALUE;

	// Verify the supertype is valid *and* is a supertype

	BMimeType mime;
	BMimeType super;
	// Make sure the supertype is valid
	status_t err = mime.SetTo(supertype);
	// Make sure it's really a supertype
	if (!err && !mime.IsSupertypeOnly())
		err = B_BAD_VALUE;
	// See if we need to do our initial build still
	if (!err && !fHaveDoneFullBuild)
		err = _BuildInstalledTypesList();

	// Ask the appropriate supertype for its list
	if (!err) {
		std::map<std::string, Supertype>::iterator i = fSupertypes.find(supertype);
		if (i != fSupertypes.end())
			err = i->second.GetInstalledSubtypes(types);
		else
			err = B_NAME_NOT_FOUND;
	}
	return err;
}


/**
 * @brief Returns a BMessage listing all currently installed MIME supertypes.
 *
 * @param types Pointer to a pre-allocated BMessage that receives supertypes.
 * @return B_OK on success, or an error code on failure.
 */
status_t
InstalledTypes::GetInstalledSupertypes(BMessage *types)
{
	if (types == NULL)
		return B_BAD_VALUE;

	status_t err = B_OK;

	// See if we need to do our initial build still
	if (!fHaveDoneFullBuild)
		err = _BuildInstalledTypesList();

	// See if we need to fill up a new message
	if (!err && !fCachedSupertypesMessage)
		err = _CreateMessageWithSupertypes(&fCachedSupertypesMessage);

	// If we get this far, there's a cached message waiting
	if (!err)
		*types = *fCachedSupertypesMessage;

	return err;
}


/**
 * @brief Adds the given MIME type to the installed-types lists.
 *
 * If the full database scan has not yet been performed this call is a no-op.
 * Otherwise the type is appended to all relevant cached structures.
 *
 * @param type The MIME type string to add (e.g. "text/plain").
 * @return B_OK on success, or an error code on failure.
 */
status_t
InstalledTypes::AddType(const char *type)
{
	if (!fHaveDoneFullBuild)
		return B_OK;

	BMimeType mime(type);
	if (type == NULL || mime.InitCheck() != B_OK)
		return B_BAD_VALUE;

	// Find the / in the string, if one exists
	uint i;
	size_t len = strlen(type);
	for (i = 0; i < len; i++) {
		if (type[i] == '/')
			break;
	}
	if (i == len) {
		// Supertype only
		return _AddSupertype(type);
	}

	// Copy the supertype
	char super[B_PATH_NAME_LENGTH];
	strncpy(super, type, i);
	super[i] = 0;

	// Get a pointer to the subtype
	const char *sub = &(type[i+1]);

	// Add the subtype (which will add the supertype if necessary)
	return _AddSubtype(super, sub);
}


/**
 * @brief Removes the given MIME type from the installed-types lists.
 *
 * If the full database scan has not yet been performed this call is a no-op.
 * Otherwise any cached messages that contain the type are invalidated.
 *
 * @param type The MIME type string to remove.
 * @return B_OK on success, or an error code on failure.
 */
status_t
InstalledTypes::RemoveType(const char *type)
{
	if (!fHaveDoneFullBuild)
		return B_OK;

	BMimeType mime(type);
	if (type == NULL || mime.InitCheck() != B_OK)
		return B_BAD_VALUE;

	// Find the / in the string, if one exists
	uint i;
	size_t len = strlen(type);
	for (i = 0; i < len; i++) {
		if (type[i] == '/')
			break;
	}
	if (i == len) {
		// Supertype only
		return _RemoveSupertype(type);
	}

	// Copy the supertype
	char super[B_PATH_NAME_LENGTH];
	strncpy(super, type, i);
	super[i] = 0;

	// Get a pointer to the subtype
	const char *sub = &(type[i+1]);

	// Remove the subtype
	return _RemoveSubtype(super, sub);
}


/**
 * @brief Adds a supertype entry to the internal map and any cached messages.
 *
 * @param super The supertype string (e.g. "text").
 * @return B_OK on success (even if it already existed), or an error code.
 */
status_t
InstalledTypes::_AddSupertype(const char* super)
{
	if (super == NULL)
		return B_BAD_VALUE;

	status_t err = B_OK;

	if (fSupertypes.find(super) == fSupertypes.end()) {
		Supertype &supertype = fSupertypes[super];
		supertype.SetName(super);
		if (fCachedMessage)
			err = fCachedMessage->AddString(kTypesField, super);
		if (!err && fCachedSupertypesMessage)
			err = fCachedSupertypesMessage->AddString(kSupertypesField, super);
	}

	return err;
}


/**
 * @brief Adds a subtype to the given supertype, creating the supertype if needed.
 *
 * @param super The supertype string.
 * @param sub   The subtype string (without the "supertype/" prefix).
 * @return B_OK on success, B_NAME_IN_USE if the subtype already exists,
 *         or another error code.
 */
status_t
InstalledTypes::_AddSubtype(const char *super, const char *sub)
{
	if (super == NULL || sub == NULL)
		return B_BAD_VALUE;

	status_t err = _AddSupertype(super);
	if (!err)
		err = _AddSubtype(fSupertypes[super], sub);

	return err;
}


/**
 * @brief Adds a subtype string to the given Supertype object and cached message.
 *
 * @param super The Supertype object to which the subtype is added.
 * @param sub   The subtype string (without the "supertype/" prefix).
 * @return B_OK on success, B_NAME_IN_USE if the subtype already exists,
 *         or another error code.
 */
status_t
InstalledTypes::_AddSubtype(Supertype &super, const char *sub)
{
	if (sub == NULL)
		return B_BAD_VALUE;

	status_t err = super.AddSubtype(sub);
	if (!err && fCachedMessage) {
		char type[B_PATH_NAME_LENGTH];
		sprintf(type, "%s/%s", super.GetName(), sub);
		err = fCachedMessage->AddString("types", type);
	}
	return err;
}


/**
 * @brief Removes a supertype and all its subtypes from the internal map.
 *
 * Cached messages are invalidated upon removal.
 *
 * @param super The supertype string to remove.
 * @return B_OK on success, B_NAME_NOT_FOUND if the supertype is not installed,
 *         or another error code.
 */
status_t
InstalledTypes::_RemoveSupertype(const char *super)
{
	if (super == NULL)
		return B_BAD_VALUE;

	status_t err = fSupertypes.erase(super) == 1 ? B_OK : B_NAME_NOT_FOUND;
	if (!err)
		_ClearCachedMessages();
	return err;
}


/**
 * @brief Removes a subtype from the given supertype's list.
 *
 * Cached messages are invalidated upon removal.
 *
 * @param super The supertype string.
 * @param sub   The subtype string (without the "supertype/" prefix).
 * @return B_OK on success, B_NAME_NOT_FOUND if not found, or another error code.
 */
status_t
InstalledTypes::_RemoveSubtype(const char *super, const char *sub)
{
	if (super == NULL || sub == NULL)
		return B_BAD_VALUE;

	status_t err = B_NAME_NOT_FOUND;

	std::map<std::string, Supertype>::iterator i = fSupertypes.find(super);
	if (i != fSupertypes.end()) {
		err = i->second.RemoveSubtype(sub);
		if (!err)
			_ClearCachedMessages();
	}

	return err;

}


/**
 * @brief Clears cached messages and empties the supertype map.
 */
void
InstalledTypes::_Unset()
{
	_ClearCachedMessages();
	fSupertypes.clear();
}


/**
 * @brief Frees and NULLs both cached BMessage pointers.
 */
void
InstalledTypes::_ClearCachedMessages()
{
	delete fCachedSupertypesMessage;
	delete fCachedMessage;
	fCachedSupertypesMessage = NULL;
	fCachedMessage = NULL;
}


/**
 * @brief Scans the database and builds the complete installed-types lists.
 *
 * Iterates over all supertype directories and their subtype files, reading the
 * META:TYPE attribute from each to build the in-memory maps. Also creates
 * initial cached messages. Sets fHaveDoneFullBuild to true on completion.
 *
 * @return B_OK on success, or an error code on failure.
 */
status_t
InstalledTypes::_BuildInstalledTypesList()
{
	status_t err = B_OK;
	_Unset();

	// Create empty "cached messages" so proper messages
	// will be built up as we add new types
	try {
		fCachedMessage = new BMessage();
		fCachedSupertypesMessage = new BMessage();
	} catch (std::bad_alloc&) {
		err = B_NO_MEMORY;
	}

	DatabaseDirectory root;
	if (!err)
		err = root.Init(fDatabaseLocation);
	if (!err) {
		root.Rewind();
		while (true) {
			BEntry entry;
			err = root.GetNextEntry(&entry);
			if (err) {
				// If we've come to the end of list, it's not an error
				if (err == B_ENTRY_NOT_FOUND)
					err = B_OK;
				break;
			} else {
				// Check that this entry is both a directory and a valid MIME string
				char supertype[B_PATH_NAME_LENGTH];
				if (entry.IsDirectory()
				      && entry.GetName(supertype) == B_OK
				         && BMimeType::IsValid(supertype))
				{
					// Make sure our string is all lowercase
					BPrivate::Storage::to_lower(supertype);

					// Add this supertype
					if (_AddSupertype(supertype) != B_OK)
						DBG(OUT("Mime::InstalledTypes::BuildInstalledTypesList()"
							" -- Error adding supertype '%s': 0x%" B_PRIx32 "\n",
							supertype, err));
					Supertype &supertypeRef = fSupertypes[supertype];

					// Now iterate through this supertype directory and add
					// all of its subtypes
					DatabaseDirectory dir;
					if (dir.Init(fDatabaseLocation, supertype) == B_OK) {
						dir.Rewind();
						while (true) {
							BEntry subEntry;
							err = dir.GetNextEntry(&subEntry);
							if (err) {
								// If we've come to the end of list, it's not an error
								if (err == B_ENTRY_NOT_FOUND)
									err = B_OK;
								break;
							} else {
								// We need to preserve the case of the type name for
								// queries, so we can't use the file name directly
								BString type;
								int32 subStart;
								BNode node(&subEntry);
								if (node.InitCheck() == B_OK
									&& node.ReadAttrString(kTypeAttr, &type) >= B_OK
									&& (subStart = type.FindFirst('/')) > 0) {
									// Add the subtype
									if (_AddSubtype(supertypeRef, type.String()
											+ subStart + 1) != B_OK) {
										DBG(OUT("Mime::InstalledTypes::BuildInstalledTypesList()"
											" -- Error adding subtype '%s/%s': 0x%" B_PRIx32 "\n",
											supertype, type.String() + subStart + 1, err));
									}
								}
							}
						}
					} else {
						DBG(OUT("Mime::InstalledTypes::BuildInstalledTypesList(): "
						          "Failed opening supertype directory '%s'\n",
						            supertype));
					}
				}
			}
		}
	} else {
		DBG(OUT("Mime::InstalledTypes::BuildInstalledTypesList(): "
		          "Failed opening mime database directory.\n"));
	}
	fHaveDoneFullBuild = true;
	return err;

}


/**
 * @brief Allocates a new BMessage and fills it with all installed MIME types.
 *
 * Both supertype and supertype/subtype strings are added to the message.
 *
 * @param _result Pointer to a BMessage pointer that is allocated and filled.
 * @return B_OK on success, or an error code on failure.
 */
status_t
InstalledTypes::_CreateMessageWithTypes(BMessage **_result) const
{
	if (_result == NULL)
		return B_BAD_VALUE;

	status_t err = B_OK;

	// Alloc the message
	try {
		*_result = new BMessage();
	} catch (std::bad_alloc&) {
		err = B_NO_MEMORY;
	}

	// Fill with types
	if (!err) {
		BMessage &msg = **_result;
		std::map<std::string, Supertype>::const_iterator i;
		for (i = fSupertypes.begin(); i != fSupertypes.end() && !err; i++) {
			err = msg.AddString(kTypesField, i->first.c_str());
			if (!err)
				err = i->second.FillMessageWithTypes(msg);
		}
	}
	return err;
}


/**
 * @brief Allocates a new BMessage and fills it with all installed supertypes.
 *
 * @param _result Pointer to a BMessage pointer that is allocated and filled.
 * @return B_OK on success, or an error code on failure.
 */
status_t
InstalledTypes::_CreateMessageWithSupertypes(BMessage **_result) const
{
	if (_result == NULL)
		return B_BAD_VALUE;

	status_t err = B_OK;

	// Alloc the message
	try {
		*_result = new BMessage();
	} catch (std::bad_alloc&) {
		err = B_NO_MEMORY;
	}

	// Fill with types
	if (!err) {
		BMessage &msg = **_result;
		std::map<std::string, Supertype>::const_iterator i;
		for (i = fSupertypes.begin(); i != fSupertypes.end() && !err; i++) {
			err = msg.AddString(kSupertypesField, i->first.c_str());
		}
	}
	return err;
}

} // namespace Mime
} // namespace Storage
} // namespace BPrivate
