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
 *   Copyright 2002-2006 Haiku, Inc. All rights reserved.
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 *       Tyler Dauwalder
 *       Ingo Weinhold, bonefish@users.sf.net
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file MimeType.cpp
 * @brief Implementation of the BMimeType class for MIME type handling.
 *
 * This file provides the full implementation of BMimeType, which represents
 * a single MIME type string and provides methods to query and modify the
 * system MIME database. It supports installing/deleting types, reading and
 * writing icons, descriptions, preferred applications, sniffer rules, and
 * more. Static helpers for MIME type validation and database-wide queries
 * are also included.
 *
 * @see BMimeType
 */


#include "MimeType.h"

#include <Bitmap.h>
#include <mime/database_support.h>
#include <mime/DatabaseLocation.h>
#include <sniffer/Rule.h>
#include <sniffer/Parser.h>

#include <RegistrarDefs.h>
#include <RosterPrivate.h>

#include <ctype.h>
#include <new>
#include <stdio.h>
#include <strings.h>


using namespace BPrivate;

// Private helper functions
static bool isValidMimeChar(const char ch);

using namespace BPrivate::Storage::Mime;
using namespace std;

const char* B_PEF_APP_MIME_TYPE		= "application/x-be-executable";
const char* B_PE_APP_MIME_TYPE		= "application/x-vnd.Be-peexecutable";
const char* B_ELF_APP_MIME_TYPE		= "application/x-vnd.Be-elfexecutable";
const char* B_RESOURCE_MIME_TYPE	= "application/x-be-resource";
const char* B_FILE_MIME_TYPE		= "application/octet-stream";
// Might be defined platform depended, but ELF will certainly be the common
// format for all platforms anyway.
const char* B_APP_MIME_TYPE			= B_ELF_APP_MIME_TYPE;


/**
 * @brief Returns whether the given character is a valid MIME type character.
 *
 * @param ch The character to test.
 * @return true if the character is allowed in a MIME type string, false otherwise.
 */
static bool
isValidMimeChar(const char ch)
{
	// Handles white space and most CTLs
	return ch > 32
		&& ch != '/'
		&& ch != '<'
		&& ch != '>'
		&& ch != '@'
		&& ch != ','
		&& ch != ';'
		&& ch != ':'
		&& ch != '"'
		&& ch != '('
		&& ch != ')'
		&& ch != '['
		&& ch != ']'
		&& ch != '?'
		&& ch != '='
		&& ch != '\\'
		&& ch != 127;	// DEL
}


//	#pragma mark -


/**
 * @brief Creates an uninitialized BMimeType object.
 */
BMimeType::BMimeType()
	:
	fType(NULL),
	fCStatus(B_NO_INIT)
{
}


/**
 * @brief Creates a BMimeType object and initializes it to the supplied MIME type.
 *
 * @param mimeType A MIME type string (e.g., "text/plain").
 */
BMimeType::BMimeType(const char* mimeType)
	:
	fType(NULL),
	fCStatus(B_NO_INIT)
{
	SetTo(mimeType);
}


/**
 * @brief Frees all resources associated with this object.
 */
BMimeType::~BMimeType()
{
	Unset();
}


/**
 * @brief Initializes this object to the supplied MIME type.
 *
 * @param mimeType A valid MIME type string to set.
 * @return B_OK on success, B_BAD_VALUE if the string is invalid, or B_NO_MEMORY.
 */
status_t
BMimeType::SetTo(const char* mimeType)
{
	if (mimeType == NULL) {
		Unset();
	} else if (!BMimeType::IsValid(mimeType)) {
		fCStatus = B_BAD_VALUE;
	} else {
		Unset();
		fType = new(std::nothrow) char[strlen(mimeType) + 1];
		if (fType) {
			strlcpy(fType, mimeType, B_MIME_TYPE_LENGTH);
			fCStatus = B_OK;
		} else {
			fCStatus = B_NO_MEMORY;
		}
	}
	return fCStatus;
}


/**
 * @brief Returns the object to an uninitialized state.
 */
void
BMimeType::Unset()
{
	delete [] fType;
	fType = NULL;
	fCStatus = B_NO_INIT;
}


/**
 * @brief Returns the result of the most recent constructor or SetTo() call.
 *
 * @return B_OK if the object is properly initialized, or an error code otherwise.
 */
status_t
BMimeType::InitCheck() const
{
	return fCStatus;
}


/**
 * @brief Returns the MIME string represented by this object.
 *
 * @return A pointer to the internal MIME type C-string, or NULL if uninitialized.
 */
const char*
BMimeType::Type() const
{
	return fType;
}


/**
 * @brief Returns whether the object represents a valid MIME type.
 *
 * @return true if the object is initialized and its MIME string is valid.
 */
bool
BMimeType::IsValid() const
{
	return InitCheck() == B_OK && BMimeType::IsValid(Type());
}


/**
 * @brief Returns whether this object represents a supertype (has no subtype).
 *
 * @return true if the MIME string contains no '/' separator.
 */
bool
BMimeType::IsSupertypeOnly() const
{
	if (fCStatus == B_OK) {
		// We assume here fCStatus will be B_OK *only* if
		// the MIME string is valid
		size_t len = strlen(fType);
		for (size_t i = 0; i < len; i++) {
			if (fType[i] == '/')
				return false;
		}
		return true;
	} else
		return false;
}


/**
 * @brief Returns whether or not this type is currently installed in the MIME database.
 *
 * @return true if the type exists in the default MIME database location.
 */
bool
BMimeType::IsInstalled() const
{
	return InitCheck() == B_OK
		&& default_database_location()->IsInstalled(Type());
}


/**
 * @brief Gets the supertype of the MIME type represented by this object.
 *
 * @param supertype Pointer to a BMimeType to be filled with the supertype.
 * @return B_OK on success, B_BAD_VALUE if this type is already a supertype or supertype is NULL.
 */
status_t
BMimeType::GetSupertype(BMimeType* supertype) const
{
	if (supertype == NULL)
		return B_BAD_VALUE;

	supertype->Unset();
	status_t status = fCStatus == B_OK ? B_OK : B_BAD_VALUE;
	if (status == B_OK) {
		size_t len = strlen(fType);
		size_t i = 0;
		for (; i < len; i++) {
			if (fType[i] == '/')
				break;
		}
		if (i == len) {
			// object is a supertype only
			status = B_BAD_VALUE;
		} else {
			char superMime[B_MIME_TYPE_LENGTH];
			strncpy(superMime, fType, i);
			superMime[i] = 0;
			status = supertype->SetTo(superMime) == B_OK ? B_OK : B_BAD_VALUE;
		}
	}

	return status;
}


/**
 * @brief Returns whether this and the supplied BMimeType object are equal.
 *
 * @param type The BMimeType to compare against.
 * @return true if both objects represent the same MIME type (case-insensitive).
 */
bool
BMimeType::operator==(const BMimeType &type) const
{
	if (InitCheck() == B_NO_INIT && type.InitCheck() == B_NO_INIT)
		return true;
	else if (InitCheck() == B_OK && type.InitCheck() == B_OK)
		return strcasecmp(Type(), type.Type()) == 0;

	return false;
}


/**
 * @brief Returns whether this and the supplied MIME type C-string are equal.
 *
 * @param type A MIME type C-string to compare against.
 * @return true if both represent the same MIME type (case-insensitive).
 */
bool
BMimeType::operator==(const char* type) const
{
	BMimeType mime;
	if (type)
		mime.SetTo(type);

	return (*this) == mime;
}


/**
 * @brief Returns whether this MIME type is a supertype of or equals the supplied one.
 *
 * @param type The BMimeType to test containment against.
 * @return true if this type equals or is the supertype of the given type.
 */
bool
BMimeType::Contains(const BMimeType* type) const
{
	if (type == NULL)
		return false;

	if (*this == *type)
		return true;

	BMimeType super;
	if (type->GetSupertype(&super) == B_OK && *this == super)
		return true;
	return false;
}


/**
 * @brief Adds the MIME type to the MIME database.
 *
 * @return B_OK on success, or an error code if the registrar could not be reached
 *         or the type is already installed.
 */
status_t
BMimeType::Install()
{
	status_t err = InitCheck();

	BMessage message(B_REG_MIME_INSTALL);
	BMessage reply;
	status_t result;

	// Build and send the message, read the reply
	if (err == B_OK)
		err = message.AddString("type", Type());

	if (err == B_OK)
		err = BRoster::Private().SendTo(&message, &reply, true);

	if (err == B_OK)
		err = (status_t)(reply.what == B_REG_RESULT ? B_OK : B_BAD_REPLY);

	if (err == B_OK)
		err = reply.FindInt32("result", &result);

	if (err == B_OK)
		err = result;

	return err;
}


/**
 * @brief Removes the MIME type from the MIME database.
 *
 * @return B_OK on success, or an error code on failure.
 */
status_t
BMimeType::Delete()
{
	status_t err = InitCheck();

	BMessage message(B_REG_MIME_DELETE);
	BMessage reply;
	status_t result;

	// Build and send the message, read the reply
	if (err == B_OK)
		err = message.AddString("type", Type());

	if (err == B_OK)
		err = BRoster::Private().SendTo(&message, &reply, true);

	if (err == B_OK)
		err = (status_t)(reply.what == B_REG_RESULT ? B_OK : B_BAD_REPLY);

	if (err == B_OK)
		err = reply.FindInt32("result", &result);

	if (err == B_OK)
		err = result;

	return err;
}


/**
 * @brief Fetches the large or mini icon associated with the MIME type.
 *
 * @param icon Pointer to a BBitmap to be filled with the icon data.
 * @param size The desired icon size (B_LARGE_ICON or B_MINI_ICON).
 * @return B_OK on success, B_BAD_VALUE if icon is NULL, or another error code.
 */
status_t
BMimeType::GetIcon(BBitmap* icon, icon_size size) const
{
	if (icon == NULL)
		return B_BAD_VALUE;

	status_t err = InitCheck();
	if (err == B_OK)
		err = default_database_location()->GetIcon(Type(), *icon, size);

	return err;
}


/**
 * @brief Fetches the vector icon associated with the MIME type.
 *
 * @param data Output pointer to be set to an allocated buffer containing the icon data.
 * @param size Output pointer to be filled with the size of the icon data buffer.
 * @return B_OK on success, B_BAD_VALUE if data or size is NULL, or another error code.
 */
status_t
BMimeType::GetIcon(uint8** data, size_t* size) const
{
	if (data == NULL || size == NULL)
		return B_BAD_VALUE;

	status_t err = InitCheck();
	if (err == B_OK)
		err = default_database_location()->GetIcon(Type(), *data, *size);

	return err;
}


/**
 * @brief Fetches the signature of the MIME type's preferred application from the MIME database.
 *
 * @param signature Buffer to be filled with the preferred application's MIME signature.
 * @param verb The application verb (default B_OPEN).
 * @return B_OK on success, or an error code on failure.
 */
status_t
BMimeType::GetPreferredApp(char* signature, app_verb verb) const
{
	status_t err = InitCheck();
	if (err == B_OK) {
		err = default_database_location()->GetPreferredApp(Type(), signature,
			verb);
	}

	return err;
}


/**
 * @brief Fetches from the MIME database a BMessage describing the attributes
 *        typically associated with files of the given MIME type.
 *
 * @param info Pointer to a BMessage to be filled with attribute information.
 * @return B_OK on success, B_BAD_VALUE if info is NULL, or another error code.
 */
status_t
BMimeType::GetAttrInfo(BMessage* info) const
{
	if (info == NULL)
		return B_BAD_VALUE;

	status_t err = InitCheck();
	if (err == B_OK)
		err = default_database_location()->GetAttributesInfo(Type(), *info);

	return err;
}


/**
 * @brief Fetches the MIME type's associated filename extensions from the MIME database.
 *
 * @param extensions Pointer to a BMessage to be filled with the list of extensions.
 * @return B_OK on success, B_BAD_VALUE if extensions is NULL, or another error code.
 */
status_t
BMimeType::GetFileExtensions(BMessage* extensions) const
{
	if (extensions == NULL)
		return B_BAD_VALUE;

	status_t err = InitCheck();
	if (err == B_OK) {
		err = default_database_location()->GetFileExtensions(Type(),
			*extensions);
	}

	return err;
}


/**
 * @brief Fetches the MIME type's short description from the MIME database.
 *
 * @param description Buffer to be filled with the short description string.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BMimeType::GetShortDescription(char* description) const
{
	status_t err = InitCheck();
	if (err == B_OK) {
		err = default_database_location()->GetShortDescription(Type(),
			description);
	}

	return err;
}


/**
 * @brief Fetches the MIME type's long description from the MIME database.
 *
 * @param description Buffer to be filled with the long description string.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BMimeType::GetLongDescription(char* description) const
{
	status_t err = InitCheck();
	if (err == B_OK) {
		err = default_database_location()->GetLongDescription(Type(),
			description);
	}

	return err;
}


/**
 * @brief Fetches a BMessage containing a list of MIME signatures of applications
 *        that are able to handle files of this MIME type.
 *
 * @param signatures Pointer to a BMessage to be filled with supporting application signatures.
 * @return B_OK on success, B_BAD_VALUE if signatures is NULL, or another error code.
 */
status_t
BMimeType::GetSupportingApps(BMessage* signatures) const
{
	if (signatures == NULL)
		return B_BAD_VALUE;

	BMessage message(B_REG_MIME_GET_SUPPORTING_APPS);
	status_t result;

	status_t err = InitCheck();
	if (err == B_OK)
		err = message.AddString("type", Type());
	if (err == B_OK)
		err = BRoster::Private().SendTo(&message, signatures, true);
	if (err == B_OK) {
		err = (status_t)(signatures->what == B_REG_RESULT ? B_OK
			: B_BAD_REPLY);
	}
	if (err == B_OK)
		err = signatures->FindInt32("result", &result);
	if (err == B_OK)
		err = result;

	return err;
}


/**
 * @brief Sets the large or mini icon for the MIME type.
 *
 * @param icon Pointer to a BBitmap containing the icon data, or NULL to remove.
 * @param which The icon size (B_LARGE_ICON or B_MINI_ICON).
 * @return B_OK on success, or an error code on failure.
 */
status_t
BMimeType::SetIcon(const BBitmap* icon, icon_size which)
{
	return SetIconForType(NULL, icon, which);
}


/**
 * @brief Sets the vector icon for the MIME type.
 *
 * @param data Pointer to the raw vector icon data, or NULL to remove.
 * @param size The size of the data buffer in bytes.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BMimeType::SetIcon(const uint8* data, size_t size)
{
	return SetIconForType(NULL, data, size);
}


/**
 * @brief Sets the preferred application for the MIME type.
 *
 * @param signature The MIME signature of the preferred application, or NULL/empty to remove.
 * @param verb The application verb (default B_OPEN).
 * @return B_OK on success, or an error code on failure.
 */
status_t
BMimeType::SetPreferredApp(const char* signature, app_verb verb)
{
	status_t err = InitCheck();

	BMessage message(signature && signature[0]
		? B_REG_MIME_SET_PARAM : B_REG_MIME_DELETE_PARAM);
	BMessage reply;
	status_t result;

	// Build and send the message, read the reply
	if (err == B_OK)
		err = message.AddString("type", Type());

	if (err == B_OK)
		err = message.AddInt32("which", B_REG_MIME_PREFERRED_APP);

	if (err == B_OK && signature != NULL)
		err = message.AddString("signature", signature);

	if (err == B_OK)
		err = message.AddInt32("app verb", verb);

	if (err == B_OK)
		err = BRoster::Private().SendTo(&message, &reply, true);

	if (err == B_OK)
		err = (status_t)(reply.what == B_REG_RESULT ? B_OK : B_BAD_REPLY);

	if (err == B_OK)
		err = reply.FindInt32("result", &result);

	if (err == B_OK)
		err = result;

	return err;
}


/**
 * @brief Sets the description of the attributes typically associated with files
 *        of the given MIME type.
 *
 * @param info Pointer to a BMessage describing the attributes, or NULL to remove.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BMimeType::SetAttrInfo(const BMessage* info)
{
	status_t err = InitCheck();

	BMessage message(info ? B_REG_MIME_SET_PARAM : B_REG_MIME_DELETE_PARAM);
	BMessage reply;
	status_t result;

	// Build and send the message, read the reply
	if (err == B_OK)
		err = message.AddString("type", Type());
	if (err == B_OK)
		err = message.AddInt32("which", B_REG_MIME_ATTR_INFO);
	if (err == B_OK && info != NULL)
		err = message.AddMessage("attr info", info);
	if (err == B_OK)
		err = BRoster::Private().SendTo(&message, &reply, true);
	if (err == B_OK)
		err = (status_t)(reply.what == B_REG_RESULT ? B_OK : B_BAD_REPLY);
	if (err == B_OK)
		err = reply.FindInt32("result", &result);
	if (err == B_OK)
		err = result;

	return err;
}


/**
 * @brief Sets the list of filename extensions associated with the MIME type.
 *
 * @param extensions Pointer to a BMessage containing the extensions list, or NULL to remove.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BMimeType::SetFileExtensions(const BMessage* extensions)
{
	status_t err = InitCheck();

	BMessage message(extensions ? B_REG_MIME_SET_PARAM : B_REG_MIME_DELETE_PARAM);
	BMessage reply;
	status_t result;

	// Build and send the message, read the reply
	if (err == B_OK)
		err = message.AddString("type", Type());

	if (err == B_OK)
		err = message.AddInt32("which", B_REG_MIME_FILE_EXTENSIONS);

	if (err == B_OK && extensions != NULL)
		err = message.AddMessage("extensions", extensions);

	if (err == B_OK)
		err = BRoster::Private().SendTo(&message, &reply, true);

	if (err == B_OK)
		err = (status_t)(reply.what == B_REG_RESULT ? B_OK : B_BAD_REPLY);

	if (err == B_OK)
		err = reply.FindInt32("result", &result);

	if (err == B_OK)
		err = result;

	return err;
}


/**
 * @brief Sets the short description field for the MIME type.
 *
 * @param description The short description string, or NULL/empty to remove.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BMimeType::SetShortDescription(const char* description)
{
	status_t err = InitCheck();

	BMessage message(description && description [0]
		? B_REG_MIME_SET_PARAM : B_REG_MIME_DELETE_PARAM);
	BMessage reply;
	status_t result;

	// Build and send the message, read the reply
	if (err == B_OK)
		err = message.AddString("type", Type());

	if (err == B_OK)
		err = message.AddInt32("which", B_REG_MIME_DESCRIPTION);

	if (err == B_OK && description)
		err = message.AddString("description", description);

	if (err == B_OK)
		err = message.AddBool("long", false);

	if (err == B_OK)
		err = BRoster::Private().SendTo(&message, &reply, true);

	if (err == B_OK)
		err = (status_t)(reply.what == B_REG_RESULT ? B_OK : B_BAD_REPLY);

	if (err == B_OK)
		err = reply.FindInt32("result", &result);

	if (err == B_OK)
		err = result;

	return err;
}


/**
 * @brief Sets the long description field for the MIME type.
 *
 * @param description The long description string, or NULL/empty to remove.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BMimeType::SetLongDescription(const char* description)
{
	status_t err = InitCheck();

	BMessage message(description && description[0]
		? B_REG_MIME_SET_PARAM : B_REG_MIME_DELETE_PARAM);
	BMessage reply;
	status_t result;

	// Build and send the message, read the reply
	if (err == B_OK)
		err = message.AddString("type", Type());

	if (err == B_OK)
		err = message.AddInt32("which", B_REG_MIME_DESCRIPTION);

	if (err == B_OK && description)
		err = message.AddString("description", description);

	if (err == B_OK)
		err = message.AddBool("long", true);

	if (err == B_OK)
		err = BRoster::Private().SendTo(&message, &reply, true);

	if (err == B_OK)
		err = (status_t)(reply.what == B_REG_RESULT ? B_OK : B_BAD_REPLY);

	if (err == B_OK)
		err = reply.FindInt32("result", &result);

	if (err == B_OK)
		err = result;

	return err;
}


/**
 * @brief Fetches a BMessage listing all the MIME supertypes currently installed
 *        in the MIME database.
 *
 * @param supertypes Pointer to a BMessage to be filled with the list of supertypes.
 * @return B_OK on success, B_BAD_VALUE if supertypes is NULL, or another error code.
 */
/*static*/ status_t
BMimeType::GetInstalledSupertypes(BMessage* supertypes)
{
	if (supertypes == NULL)
		return B_BAD_VALUE;

	BMessage message(B_REG_MIME_GET_INSTALLED_SUPERTYPES);
	status_t result;

	status_t err = BRoster::Private().SendTo(&message, supertypes, true);
	if (err == B_OK) {
		err = (status_t)(supertypes->what == B_REG_RESULT ? B_OK
			: B_BAD_REPLY);
	}
	if (err == B_OK)
		err = supertypes->FindInt32("result", &result);
	if (err == B_OK)
		err = result;

	return err;
}


/**
 * @brief Fetches a BMessage listing all the MIME types currently installed
 *        in the MIME database.
 *
 * @param types Pointer to a BMessage to be filled with all installed types.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BMimeType::GetInstalledTypes(BMessage* types)
{
	return GetInstalledTypes(NULL, types);
}


/**
 * @brief Fetches a BMessage listing all the MIME subtypes of the given supertype
 *        currently installed in the MIME database.
 *
 * @param supertype The supertype to filter by, or NULL to get all types.
 * @param types Pointer to a BMessage to be filled with the matching types.
 * @return B_OK on success, B_BAD_VALUE if types is NULL, or another error code.
 */
/*static*/ status_t
BMimeType::GetInstalledTypes(const char* supertype, BMessage* types)
{
	if (types == NULL)
		return B_BAD_VALUE;

	status_t result;

	// Build and send the message, read the reply
	BMessage message(B_REG_MIME_GET_INSTALLED_TYPES);
	status_t err = B_OK;

	if (supertype != NULL)
		err = message.AddString("supertype", supertype);
	if (err == B_OK)
		err = BRoster::Private().SendTo(&message, types, true);
	if (err == B_OK)
		err = (status_t)(types->what == B_REG_RESULT ? B_OK : B_BAD_REPLY);
	if (err == B_OK)
		err = types->FindInt32("result", &result);
	if (err == B_OK)
		err = result;

	return err;
}


/**
 * @brief Fetches a BMessage containing a list of MIME signatures of applications
 *        that are able to handle files of any type (wildcard apps).
 *
 * @param wild_ones Pointer to a BMessage to be filled with wildcard application signatures.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BMimeType::GetWildcardApps(BMessage* wild_ones)
{
	BMimeType mime;
	status_t err = mime.SetTo("application/octet-stream");
	if (err == B_OK)
		err = mime.GetSupportingApps(wild_ones);
	return err;
}


/**
 * @brief Returns whether the given string represents a valid MIME type.
 *
 * @param string The MIME type C-string to validate.
 * @return true if the string is a valid MIME type, false otherwise.
 */
bool
BMimeType::IsValid(const char* string)
{
	if (string == NULL)
		return false;

	bool foundSlash = false;
	size_t len = strlen(string);
	if (len >= B_MIME_TYPE_LENGTH || len == 0)
		return false;

	for (size_t i = 0; i < len; i++) {
		char ch = string[i];
		if (ch == '/') {
			if (foundSlash || i == 0 || i == len - 1)
				return false;
			else
				foundSlash = true;
		} else if (!isValidMimeChar(ch)) {
			return false;
		}
	}
	return true;
}


/**
 * @brief Fetches an entry_ref that serves as a hint as to where the MIME type's
 *        preferred application might live.
 *
 * @param ref Pointer to an entry_ref to be filled with the app hint.
 * @return B_OK on success, B_BAD_VALUE if ref is NULL, or another error code.
 */
status_t
BMimeType::GetAppHint(entry_ref* ref) const
{
	if (ref == NULL)
		return B_BAD_VALUE;

	status_t err = InitCheck();
	if (err == B_OK)
		err = default_database_location()->GetAppHint(Type(), *ref);
	return err;
}


/**
 * @brief Sets the app hint field for the MIME type.
 *
 * @param ref Pointer to an entry_ref indicating the application's location, or NULL to remove.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BMimeType::SetAppHint(const entry_ref* ref)
{
	status_t err = InitCheck();

	BMessage message(ref ? B_REG_MIME_SET_PARAM : B_REG_MIME_DELETE_PARAM);
	BMessage reply;
	status_t result;

	// Build and send the message, read the reply
	if (err == B_OK)
		err = message.AddString("type", Type());

	if (err == B_OK)
		err = message.AddInt32("which", B_REG_MIME_APP_HINT);

	if (err == B_OK && ref != NULL)
		err = message.AddRef("app hint", ref);

	if (err == B_OK)
		err = BRoster::Private().SendTo(&message, &reply, true);

	if (err == B_OK)
		err = (status_t)(reply.what == B_REG_RESULT ? B_OK : B_BAD_REPLY);

	if (err == B_OK)
		err = reply.FindInt32("result", &result);

	if (err == B_OK)
		err = result;

	return err;
}


/**
 * @brief Fetches the large or mini icon used by an application of this type for
 *        files of the given type.
 *
 * @param type The file MIME type whose icon to retrieve, or NULL to get the type's own icon.
 * @param icon Pointer to a BBitmap to be filled with the icon data.
 * @param which The desired icon size.
 * @return B_OK on success, B_BAD_VALUE if icon is NULL or type is invalid, or another error code.
 */
status_t
BMimeType::GetIconForType(const char* type, BBitmap* icon, icon_size which) const
{
	if (icon == NULL)
		return B_BAD_VALUE;

	// If type is NULL, this function works just like GetIcon(), othewise,
	// we need to make sure the give type is valid.
	status_t err;
	if (type) {
		err = BMimeType::IsValid(type) ? B_OK : B_BAD_VALUE;
		if (err == B_OK) {
			err = default_database_location()->GetIconForType(Type(), type,
				*icon, which);
		}
	} else
		err = GetIcon(icon, which);

	return err;
}


/**
 * @brief Fetches the vector icon used by an application of this type for files of
 *        the given type.
 *
 * @param type The file MIME type whose icon to retrieve, or NULL to get the type's own icon.
 * @param _data Output pointer to be set to the allocated icon data buffer.
 * @param _size Output pointer to be filled with the size of the data buffer.
 * @return B_OK on success, B_BAD_VALUE if pointers are NULL or type is invalid, or another error code.
 */
status_t
BMimeType::GetIconForType(const char* type, uint8** _data, size_t* _size) const
{
	if (_data == NULL || _size == NULL)
		return B_BAD_VALUE;

	// If type is NULL, this function works just like GetIcon(), otherwise,
	// we need to make sure the give type is valid.
	if (type == NULL)
		return GetIcon(_data, _size);

	if (!BMimeType::IsValid(type))
		return B_BAD_VALUE;

	return default_database_location()->GetIconForType(Type(), type, *_data,
		*_size);
}


/**
 * @brief Sets the large or mini icon used by an application of this type for
 *        files of the given type.
 *
 * @param type The file MIME type to associate the icon with, or NULL for the type's own icon.
 * @param icon Pointer to a BBitmap containing the icon, or NULL to remove.
 * @param which The icon size (B_LARGE_ICON or B_MINI_ICON).
 * @return B_OK on success, or an error code on failure.
 */
status_t
BMimeType::SetIconForType(const char* type, const BBitmap* icon, icon_size which)
{
	status_t err = InitCheck();

	BMessage message(icon ? B_REG_MIME_SET_PARAM : B_REG_MIME_DELETE_PARAM);
	BMessage reply;
	status_t result;

	void* data = NULL;
	int32 dataSize;

	// Build and send the message, read the reply
	if (err == B_OK)
		err = message.AddString("type", Type());

	if (err == B_OK) {
		err = message.AddInt32("which",
			type ? B_REG_MIME_ICON_FOR_TYPE : B_REG_MIME_ICON);
	}

	if (icon != NULL) {
		if (err == B_OK)
			err = get_icon_data(icon, which, &data, &dataSize);

		if (err == B_OK)
			err = message.AddData("icon data", B_RAW_TYPE, data, dataSize);
	}

	if (err == B_OK)
		err = message.AddInt32("icon size", which);

	if (type != NULL) {
		if (err == B_OK)
			err = BMimeType::IsValid(type) ? B_OK : B_BAD_VALUE;

		if (err == B_OK)
			err = message.AddString("file type", type);
	}

	if (err == B_OK)
		err = BRoster::Private().SendTo(&message, &reply, true);

	if (err == B_OK)
		err = (status_t)(reply.what == B_REG_RESULT ? B_OK : B_BAD_REPLY);

	if (err == B_OK)
		err = reply.FindInt32("result", &result);

	if (err == B_OK)
		err = result;

	delete[] (int8*)data;

	return err;
}


/**
 * @brief Sets the vector icon used by an application of this type for files of
 *        the given type.
 *
 * @param type The file MIME type to associate the icon with, or NULL for the type's own icon.
 * @param data Pointer to the raw vector icon data, or NULL to remove.
 * @param dataSize The size of the data buffer in bytes.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BMimeType::SetIconForType(const char* type, const uint8* data, size_t dataSize)
{
	status_t err = InitCheck();

	BMessage message(data ? B_REG_MIME_SET_PARAM : B_REG_MIME_DELETE_PARAM);
	BMessage reply;
	status_t result;

	// Build and send the message, read the reply
	if (err == B_OK)
		err = message.AddString("type", Type());
	if (err == B_OK)
		err = message.AddInt32("which", (type ? B_REG_MIME_ICON_FOR_TYPE : B_REG_MIME_ICON));
	if (data) {
		if (err == B_OK)
			err = message.AddData("icon data", B_RAW_TYPE, data, dataSize);
	}
	if (err == B_OK)
		err = message.AddInt32("icon size", -1);
		// -1 indicates size should be ignored (vector icon data)
	if (type) {
		if (err == B_OK)
			err = BMimeType::IsValid(type) ? B_OK : B_BAD_VALUE;
		if (err == B_OK)
			err = message.AddString("file type", type);
	}
	if (err == B_OK)
		err = BRoster::Private().SendTo(&message, &reply, true);
	if (err == B_OK)
		err = (status_t)(reply.what == B_REG_RESULT ? B_OK : B_BAD_REPLY);
	if (err == B_OK)
		err = reply.FindInt32("result", &result);
	if (err == B_OK)
		err = result;

	return err;
}


/**
 * @brief Retrieves the MIME type's sniffer rule from the database.
 *
 * @param result Pointer to a BString to be filled with the sniffer rule.
 * @return B_OK on success, B_BAD_VALUE if result is NULL, or another error code.
 */
status_t
BMimeType::GetSnifferRule(BString* result) const
{
	if (result == NULL)
		return B_BAD_VALUE;

	status_t err = InitCheck();
	if (err == B_OK)
		err = default_database_location()->GetSnifferRule(Type(), *result);

	return err;
}


/**
 * @brief Sets the MIME type's sniffer rule in the database.
 *
 * @param rule The sniffer rule string to set, or NULL/empty to remove.
 * @return B_OK on success, or an error code if the rule is syntactically invalid.
 */
status_t
BMimeType::SetSnifferRule(const char* rule)
{
	status_t err = InitCheck();
	if (err == B_OK && rule != NULL && rule[0] != '\0')
		err = CheckSnifferRule(rule, NULL);

	if (err != B_OK)
		return err;

	BMessage message(rule && rule[0] ? B_REG_MIME_SET_PARAM
		: B_REG_MIME_DELETE_PARAM);
	BMessage reply;
	status_t result;

	// Build and send the message, read the reply
	err = message.AddString("type", Type());
	if (err == B_OK)
		err = message.AddInt32("which", B_REG_MIME_SNIFFER_RULE);

	if (err == B_OK && rule)
		err = message.AddString("sniffer rule", rule);

	if (err == B_OK)
		err = BRoster::Private().SendTo(&message, &reply, true);

	if (err == B_OK)
		err = (status_t)(reply.what == B_REG_RESULT ? B_OK : B_BAD_REPLY);

	if (err == B_OK)
		err = reply.FindInt32("result", &result);

	if (err == B_OK)
		err = result;

	return err;
}


/**
 * @brief Checks whether a MIME sniffer rule is valid or not.
 *
 * @param rule The sniffer rule string to validate.
 * @param parseError Optional BString to be filled with a human-readable parse error.
 * @return B_OK if the rule is valid, or an error code describing the parse failure.
 */
status_t
BMimeType::CheckSnifferRule(const char* rule, BString* parseError)
{
	BPrivate::Storage::Sniffer::Rule snifferRule;

	return BPrivate::Storage::Sniffer::parse(rule, &snifferRule, parseError);
}


/**
 * @brief Guesses a MIME type for the entry referred to by the given entry_ref.
 *
 * @param file Pointer to the entry_ref identifying the file to sniff.
 * @param type Pointer to a BMimeType to be filled with the guessed MIME type.
 * @return B_OK on success, B_BAD_VALUE if either pointer is NULL, or another error code.
 */
status_t
BMimeType::GuessMimeType(const entry_ref* file, BMimeType* type)
{
	status_t err = file && type ? B_OK : B_BAD_VALUE;

	BMessage message(B_REG_MIME_SNIFF);
	BMessage reply;
	status_t result;
	const char* str;

	// Build and send the message, read the reply
	if (err == B_OK)
		err = message.AddRef("file ref", file);

	if (err == B_OK)
		err = BRoster::Private().SendTo(&message, &reply, true);

	if (err == B_OK)
		err = (status_t)(reply.what == B_REG_RESULT ? B_OK : B_BAD_REPLY);

	if (err == B_OK)
		err = reply.FindInt32("result", &result);

	if (err == B_OK)
		err = result;

	if (err == B_OK)
		err = reply.FindString("mime type", &str);

	if (err == B_OK)
		err = type->SetTo(str);

	return err;
}


/**
 * @brief Guesses a MIME type for the supplied chunk of data.
 *
 * @param buffer Pointer to the data buffer to sniff.
 * @param length The size of the data buffer in bytes.
 * @param type Pointer to a BMimeType to be filled with the guessed MIME type.
 * @return B_OK on success, B_BAD_VALUE if buffer or type is NULL, or another error code.
 */
status_t
BMimeType::GuessMimeType(const void* buffer, int32 length, BMimeType* type)
{
	status_t err = buffer && type ? B_OK : B_BAD_VALUE;

	BMessage message(B_REG_MIME_SNIFF);
	BMessage reply;
	status_t result;
	const char* str;

	// Build and send the message, read the reply
	if (err == B_OK)
		err = message.AddData("data", B_RAW_TYPE, buffer, length);

	if (err == B_OK)
		err = BRoster::Private().SendTo(&message, &reply, true);

	if (err == B_OK)
		err = (status_t)(reply.what == B_REG_RESULT ? B_OK : B_BAD_REPLY);

	if (err == B_OK)
		err = reply.FindInt32("result", &result);

	if (err == B_OK)
		err = result;

	if (err == B_OK)
		err = reply.FindString("mime type", &str);

	if (err == B_OK)
		err = type->SetTo(str);

	return err;
}


/**
 * @brief Guesses a MIME type for the given filename.
 *
 * @param filename The filename (including extension) to use for MIME type guessing.
 * @param type Pointer to a BMimeType to be filled with the guessed MIME type.
 * @return B_OK on success, B_BAD_VALUE if filename or type is NULL, or another error code.
 */
status_t
BMimeType::GuessMimeType(const char* filename, BMimeType* type)
{
	status_t err = filename && type ? B_OK : B_BAD_VALUE;

	BMessage message(B_REG_MIME_SNIFF);
	BMessage reply;
	status_t result;
	const char* str;

	// Build and send the message, read the reply
	if (err == B_OK)
		err = message.AddString("filename", filename);

	if (err == B_OK)
		err = BRoster::Private().SendTo(&message, &reply, true);

	if (err == B_OK)
		err = (status_t)(reply.what == B_REG_RESULT ? B_OK : B_BAD_REPLY);

	if (err == B_OK)
		err = reply.FindInt32("result", &result);

	if (err == B_OK)
		err = result;

	if (err == B_OK)
		err = reply.FindString("mime type", &str);

	if (err == B_OK)
		err = type->SetTo(str);

	return err;
}


/**
 * @brief Starts monitoring the MIME database for a given target messenger.
 *
 * @param target The BMessenger that will receive MIME database change notifications.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BMimeType::StartWatching(BMessenger target)
{
	BMessage message(B_REG_MIME_START_WATCHING);
	BMessage reply;
	status_t result;
	status_t err;

	// Build and send the message, read the reply
	err = message.AddMessenger("target", target);
	if (err == B_OK)
		err = BRoster::Private().SendTo(&message, &reply, true);

	if (err == B_OK)
		err = (status_t)(reply.what == B_REG_RESULT ? B_OK : B_BAD_REPLY);

	if (err == B_OK)
		err = reply.FindInt32("result", &result);

	if (err == B_OK)
		err = result;

	return err;
}


/**
 * @brief Stops monitoring the MIME database for a given target messenger.
 *
 * @param target The BMessenger to stop receiving MIME database change notifications.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BMimeType::StopWatching(BMessenger target)
{
	BMessage message(B_REG_MIME_STOP_WATCHING);
	BMessage reply;
	status_t result;
	status_t err;

	// Build and send the message, read the reply
	err = message.AddMessenger("target", target);
	if (err == B_OK)
		err = BRoster::Private().SendTo(&message, &reply, true);

	if (err == B_OK)
		err = (status_t)(reply.what == B_REG_RESULT ? B_OK : B_BAD_REPLY);

	if (err == B_OK)
		err = reply.FindInt32("result", &result);

	if (err == B_OK)
		err = result;

	return err;
}


/**
 * @brief Initializes this object to the supplied MIME type (deprecated alias for SetTo()).
 *
 * @param mimeType A valid MIME type string to set.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BMimeType::SetType(const char* mimeType)
{
	return SetTo(mimeType);
}


/**
 * @brief Reserved virtual method slot 1 (FBC padding).
 */
void BMimeType::_ReservedMimeType1() {}

/**
 * @brief Reserved virtual method slot 2 (FBC padding).
 */
void BMimeType::_ReservedMimeType2() {}

/**
 * @brief Reserved virtual method slot 3 (FBC padding).
 */
void BMimeType::_ReservedMimeType3() {}


#ifdef __HAIKU_BEOS_COMPATIBLE
/**
 * @brief Assignment operator (unimplemented, provided for ABI compatibility).
 *
 * @return Reference to this object (unchanged).
 */
BMimeType&
BMimeType::operator=(const BMimeType &)
{
	return *this;
		// not implemented
}


/**
 * @brief Copy constructor (unimplemented, provided for ABI compatibility).
 */
BMimeType::BMimeType(const BMimeType &)
{
}
#endif


/**
 * @brief Fetches a BMessage listing the MIME types supported by this application type.
 *
 * @param types Pointer to a BMessage to be filled with the supported types list.
 * @return B_OK on success, B_BAD_VALUE if types is NULL, or another error code.
 */
status_t
BMimeType::GetSupportedTypes(BMessage* types)
{
	if (types == NULL)
		return B_BAD_VALUE;

	status_t err = InitCheck();
	if (err == B_OK)
		err = default_database_location()->GetSupportedTypes(Type(), *types);

	return err;
}


/*!	Sets the list of MIME types supported by the MIME type (which is
	assumed to be an application signature).

	If \a types is \c NULL the application's supported types are unset.

	The supported MIME types must be stored in a field "types" of type
	\c B_STRING_TYPE in \a types.

	For each supported type the result of BMimeType::GetSupportingApps() will
	afterwards include the signature of this application.

	\a fullSync specifies whether or not any types that are no longer
	listed as supported types as of this call to SetSupportedTypes() shall be
	updated as well, i.e. whether this application shall be removed from their
	lists of supporting applications.

	If \a fullSync is \c false, this application will not be removed from the
	previously supported types' supporting apps lists until the next call
	to BMimeType::SetSupportedTypes() or BMimeType::DeleteSupportedTypes()
	with a \c true \a fullSync parameter, the next call to BMimeType::Delete(),
	or the next reboot.

	\param types The supported types to be assigned to the file.
	       May be \c NULL.
	\param fullSync \c true to also synchronize the previously supported
	       types, \c false otherwise.

	\returns \c B_OK on success or another error code on failure.
*/
/**
 * @brief Sets the list of MIME types supported by this application MIME type.
 *
 * @param types Pointer to a BMessage containing the supported types, or NULL to remove.
 * @param fullSync If true, also removes this app from previously-supported types' lists.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BMimeType::SetSupportedTypes(const BMessage* types, bool fullSync)
{
	status_t err = InitCheck();

	// Build and send the message, read the reply
	BMessage message(types ? B_REG_MIME_SET_PARAM : B_REG_MIME_DELETE_PARAM);
	BMessage reply;
	status_t result;

	if (err == B_OK)
		err = message.AddString("type", Type());

	if (err == B_OK)
		err = message.AddInt32("which", B_REG_MIME_SUPPORTED_TYPES);

	if (err != B_OK && types != NULL)
		err = message.AddMessage("types", types);

	if (err == B_OK)
		err = message.AddBool("full sync", fullSync);

	if (err == B_OK)
		err = BRoster::Private().SendTo(&message, &reply, true);

	if (err == B_OK)
		err = (status_t)(reply.what == B_REG_RESULT ? B_OK : B_BAD_REPLY);

	if (err == B_OK)
		err = reply.FindInt32("result", &result);

	if (err == B_OK)
		err = result;

	return err;
}


/*!	Returns a list of mime types associated with the given file extension

	The list of types is returned in the pre-allocated \c BMessage pointed to
	by \a types. The types are stored in the message's "types" field, which
	is an array of \c B_STRING_TYPE values.

	\param extension The file extension of interest
	\param types Pointer to a pre-allocated BMessage into which the result will
	       be stored.

	\returns \c B_OK on success or another error code on failure.
*/
/**
 * @brief Returns a list of MIME types associated with the given file extension.
 *
 * @param extension The file extension to look up (e.g., "txt").
 * @param types Pointer to a pre-allocated BMessage to be filled with matching MIME types.
 * @return B_OK on success, B_BAD_VALUE if either pointer is NULL, or another error code.
 */
status_t
BMimeType::GetAssociatedTypes(const char* extension, BMessage* types)
{
	status_t err = extension && types ? B_OK : B_BAD_VALUE;

	BMessage message(B_REG_MIME_GET_ASSOCIATED_TYPES);
	BMessage &reply = *types;
	status_t result;

	// Build and send the message, read the reply
	if (err == B_OK)
		err = message.AddString("extension", extension);

	if (err == B_OK)
		err = BRoster::Private().SendTo(&message, &reply, true);

	if (err == B_OK)
		err = (status_t)(reply.what == B_REG_RESULT ? B_OK : B_BAD_REPLY);

	if (err == B_OK)
		err = reply.FindInt32("result", &result);

	if (err == B_OK)
		err = result;

	return err;
}
