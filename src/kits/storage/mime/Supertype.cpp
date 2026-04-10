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
 *   Copyright Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file Supertype.cpp
 * @brief Tracks installed subtypes for a single MIME supertype.
 *
 * The Supertype class maintains an in-memory set of installed subtype strings
 * for a given supertype (e.g. "image") and caches the corresponding BMessage
 * used to answer GetInstalledSubtypes() queries.  The cache is invalidated
 * whenever a subtype is removed, and is built lazily on the next query.
 *
 * @see Supertype
 */

#include <mime/Supertype.h>

#include <Message.h>
#include <mime/database_support.h>

#include <new>
#include <stdio.h>

#define DBG(x) x
//#define DBG(x)
#define OUT printf

namespace BPrivate {
namespace Storage {
namespace Mime {

/*!
	\class Supertype
	\brief Installed types information for a single supertype
*/

/**
 * @brief Constructs a Supertype with the given supertype name.
 *
 * @param super The supertype name string (e.g. "image"); may be NULL, which
 *              is treated as an empty string.
 */
Supertype::Supertype(const char *super)
	: fCachedMessage(NULL)
	, fName(super ? super : "")
{
}

/**
 * @brief Destroys the Supertype and releases any cached BMessage.
 */
Supertype::~Supertype()
{
	delete fCachedMessage;
}

/**
 * @brief Returns the list of installed subtypes for this supertype.
 *
 * Builds and caches a BMessage containing all installed subtype strings the
 * first time it is called (or after the cache has been invalidated by a
 * RemoveSubtype() call).  Copies the cached message into \a types.
 *
 * @param types Pointer to a pre-allocated BMessage that receives the subtype list.
 * @return B_OK on success, B_BAD_VALUE if \a types is NULL, or an error code
 *         if CreateMessageWithTypes() fails.
 */
status_t
Supertype::GetInstalledSubtypes(BMessage *types)
{
	status_t err = types ? B_OK : B_BAD_VALUE;
	// See if we need to fill up a new message
	if (!err && !fCachedMessage) {
		err = CreateMessageWithTypes(&fCachedMessage);
	}
	// If we get this far, there's a cached message waiting
	if (!err) {
		*types = *fCachedMessage;
	}
	return err;
}

/**
 * @brief Adds a subtype to this supertype's set and updates the cached message.
 *
 * If a cached BMessage exists, the full "supertype/subtype" string is appended
 * to it so the cache remains valid.
 *
 * @param sub The subtype to add (without the supertype prefix, e.g. "jpeg").
 * @return B_OK on success, B_NAME_IN_USE if the subtype is already registered,
 *         B_BAD_VALUE if \a sub is NULL, or an error code if updating the
 *         cached message fails.
 */
status_t
Supertype::AddSubtype(const char *sub)
{
	status_t err = sub ? B_OK : B_BAD_VALUE;
	if (!err)
		err = fSubtypes.insert(sub).second ? B_OK : B_NAME_IN_USE;
	if (!err && fCachedMessage) {
		char type[B_PATH_NAME_LENGTH];
		sprintf(type, "%s/%s", fName.c_str(), sub);
		err = fCachedMessage->AddString("types", type);
	}
	return err;
}

/**
 * @brief Removes a subtype from this supertype's set and invalidates the cached message.
 *
 * The cached BMessage is deleted so that the next GetInstalledSubtypes() call
 * rebuilds it from scratch without the removed subtype.
 *
 * @param sub The subtype to remove (without the supertype prefix).
 * @return B_OK on success, B_NAME_NOT_FOUND if the subtype is not registered,
 *         or B_BAD_VALUE if \a sub is NULL.
 */
status_t
Supertype::RemoveSubtype(const char *sub)
{
	status_t err = sub ? B_OK : B_BAD_VALUE;
	if (!err)
		err = fSubtypes.erase(sub) == 1 ? B_OK : B_NAME_NOT_FOUND;
	if (!err && fCachedMessage) {
		delete fCachedMessage;
		fCachedMessage = NULL;
	}
	return err;
}

/**
 * @brief Sets the supertype's name string.
 *
 * @param super The new supertype name; ignored if NULL.
 */
void
Supertype::SetName(const char *super)
{
	if (super)
		fName = super;
}

/**
 * @brief Returns the supertype's name string.
 *
 * @return A C string containing the supertype name (e.g. "image").
 */
const char*
Supertype::GetName()
{
	return fName.c_str();
}

/**
 * @brief Appends all installed subtypes to an existing BMessage.
 *
 * Each subtype is added as a "supertype/subtype" string in the
 * Mime::kTypesField field of \a msg.  The supertype itself is not added.
 *
 * @param msg The BMessage to fill; existing contents are preserved.
 * @return B_OK on success, or an error code if AddString() fails.
 */
status_t
Supertype::FillMessageWithTypes(BMessage &msg) const
{
	status_t err = B_OK;
	std::set<std::string>::const_iterator i;
	for (i = fSubtypes.begin(); i != fSubtypes.end() && !err; i++) {
		char type[B_PATH_NAME_LENGTH];
		sprintf(type, "%s/%s", fName.c_str(), (*i).c_str());
		err = msg.AddString(kTypesField, type);
	}
	return err;
}

/**
 * @brief Allocates a new BMessage and fills it with installed subtype strings.
 *
 * Allocates a BMessage via new, calls FillMessageWithTypes() to populate it,
 * and stores the pointer in *result.  The caller takes ownership of the
 * allocated BMessage.
 *
 * @param result Output parameter that receives the pointer to the newly allocated
 *               BMessage; must not be NULL.
 * @return B_OK on success, B_BAD_VALUE if \a result is NULL, B_NO_MEMORY if
 *         allocation fails, or an error code from FillMessageWithTypes().
 */
status_t
Supertype::CreateMessageWithTypes(BMessage **result) const
{
	status_t err = result ? B_OK : B_BAD_VALUE;
	// Alloc the message
	if (!err) {
		try {
			*result = new BMessage();
		} catch (std::bad_alloc&) {
			err = B_NO_MEMORY;
		}
	}
	// Fill with types
	if (!err)
		err = FillMessageWithTypes(**result);
	return err;
}

} // namespace Mime
} // namespace Storage
} // namespace BPrivate
