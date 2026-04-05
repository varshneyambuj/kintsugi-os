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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2011, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */


/** @file Key.cpp
 *  @brief Implementation of BKey and BPasswordKey for credential storage.
 *
 *  BKey represents a generic key with an identifier, purpose, and raw data
 *  payload. BPasswordKey extends BKey to provide a specialized interface
 *  for storing and retrieving password credentials. Both classes support
 *  serialization to and from BMessage for use with the keystore system.
 */


#include <Key.h>

#include <stdio.h>


#if 0
// TODO: move this to the KeyStore or the registrar backend if needed
static bool
CompareLists(BObjectList<BString> a, BObjectList<BString> b)
{
	if (a.CountItems() != b.CountItems())
		return false;

	for (int32 i = 0; i < a.CountItems(); i++) {
		if (*a.ItemAt(i) != *b.ItemAt(i))
			return false;
	}

	return true;
}
#endif


// #pragma mark - Generic BKey


/** @brief Default constructor. Creates an empty key with generic purpose.
 */
BKey::BKey()
{
	Unset();
}


/** @brief Constructs a key with the specified properties.
 *  @param purpose              The key purpose (e.g., B_KEY_PURPOSE_GENERIC, B_KEY_PURPOSE_WEB).
 *  @param identifier           The primary identifier string for the key.
 *  @param secondaryIdentifier  An optional secondary identifier string.
 *  @param data                 Pointer to the raw key data, or NULL.
 *  @param length               Length of the raw key data in bytes.
 */
BKey::BKey(BKeyPurpose purpose, const char* identifier,
	const char* secondaryIdentifier, const uint8* data, size_t length)
{
	SetTo(purpose, identifier, secondaryIdentifier, data, length);
}


/** @brief Copy constructor. Creates a key as a copy of another.
 *  @param other The key to copy.
 */
BKey::BKey(BKey& other)
{
	*this = other;
}


/** @brief Destructor.
 */
BKey::~BKey()
{
}


/** @brief Resets the key to default empty state with generic purpose.
 */
void
BKey::Unset()
{
	SetTo(B_KEY_PURPOSE_GENERIC, "", "", NULL, 0);
}


/** @brief Initializes the key with the specified properties.
 *
 *  Resets the creation time to 0 and sets all fields to the provided values.
 *
 *  @param purpose              The key purpose.
 *  @param identifier           The primary identifier string.
 *  @param secondaryIdentifier  An optional secondary identifier string.
 *  @param data                 Pointer to the raw key data, or NULL.
 *  @param length               Length of the raw key data in bytes.
 *  @return B_OK on success, or an error code if setting data fails.
 */
status_t
BKey::SetTo(BKeyPurpose purpose, const char* identifier,
	const char* secondaryIdentifier, const uint8* data, size_t length)
{
	fCreationTime = 0;
	SetPurpose(purpose);
	SetIdentifier(identifier);
	SetSecondaryIdentifier(secondaryIdentifier);
	return SetData(data, length);
}


/** @brief Sets the purpose of this key.
 *  @param purpose The key purpose to set.
 */
void
BKey::SetPurpose(BKeyPurpose purpose)
{
	fPurpose = purpose;
}


/** @brief Returns the purpose of this key.
 *  @return The BKeyPurpose value.
 */
BKeyPurpose
BKey::Purpose() const
{
	return fPurpose;
}


/** @brief Sets the primary identifier of this key.
 *  @param identifier The identifier string to set.
 */
void
BKey::SetIdentifier(const char* identifier)
{
	fIdentifier = identifier;
}


/** @brief Returns the primary identifier of this key.
 *  @return The identifier as a C string.
 */
const char*
BKey::Identifier() const
{
	return fIdentifier.String();
}


/** @brief Sets the secondary identifier of this key.
 *  @param identifier The secondary identifier string to set.
 */
void
BKey::SetSecondaryIdentifier(const char* identifier)
{
	fSecondaryIdentifier = identifier;
}


/** @brief Returns the secondary identifier of this key.
 *  @return The secondary identifier as a C string.
 */
const char*
BKey::SecondaryIdentifier() const
{
	return fSecondaryIdentifier.String();
}


/** @brief Sets the raw data payload of this key.
 *  @param data   Pointer to the data buffer.
 *  @param length Length of the data in bytes.
 *  @return B_OK on success, or B_NO_MEMORY if the data could not be fully written.
 */
status_t
BKey::SetData(const uint8* data, size_t length)
{
	fData.SetSize(0);
	ssize_t bytesWritten = fData.WriteAt(0, data, length);
	if (bytesWritten < 0)
		return (status_t)bytesWritten;

	return (size_t)bytesWritten == length ? B_OK : B_NO_MEMORY;
}


/** @brief Returns the length of the raw key data.
 *  @return The data length in bytes.
 */
size_t
BKey::DataLength() const
{
	return fData.BufferLength();
}


/** @brief Returns a pointer to the raw key data.
 *  @return A pointer to the data buffer, or NULL if empty.
 */
const uint8*
BKey::Data() const
{
	return (const uint8*)fData.Buffer();
}


/** @brief Copies the raw key data into the provided buffer.
 *  @param buffer     The destination buffer.
 *  @param bufferSize The size of the destination buffer in bytes.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BKey::GetData(uint8* buffer, size_t bufferSize) const
{
	ssize_t bytesRead = fData.ReadAt(0, buffer, bufferSize);
	if (bytesRead < 0)
		return (status_t)bytesRead;

	return B_OK;
}



/** @brief Returns the owner of this key.
 *  @return The owner string, typically the application signature.
 */
const char*
BKey::Owner() const
{
	return fOwner.String();
}


/** @brief Returns the creation time of this key.
 *  @return The creation time as a bigtime_t value in microseconds.
 */
bigtime_t
BKey::CreationTime() const
{
	return fCreationTime;
}


/** @brief Serializes the key into a BMessage.
 *
 *  Stores the key type, purpose, identifiers, owner, creation time,
 *  and raw data into the message for transmission or persistence.
 *
 *  @param message The message to flatten into.
 *  @return B_OK on success, or B_ERROR if any field fails to be added.
 */
status_t
BKey::Flatten(BMessage& message) const
{
	if (message.MakeEmpty() != B_OK
		|| message.AddUInt32("type", Type()) != B_OK
		|| message.AddUInt32("purpose", fPurpose) != B_OK
		|| message.AddString("identifier", fIdentifier) != B_OK
		|| message.AddString("secondaryIdentifier", fSecondaryIdentifier)
			!= B_OK
		|| message.AddString("owner", fOwner) != B_OK
		|| message.AddInt64("creationTime", fCreationTime) != B_OK
		|| message.AddData("data", B_RAW_TYPE, fData.Buffer(),
			fData.BufferLength()) != B_OK) {
		return B_ERROR;
	}

	return B_OK;
}


/** @brief Restores the key from a serialized BMessage.
 *
 *  Validates that the message contains a key of the matching type,
 *  then restores all fields from the message.
 *
 *  @param message The message to unflatten from.
 *  @return B_OK on success, B_BAD_VALUE if the type does not match, or B_ERROR on failure.
 */
status_t
BKey::Unflatten(const BMessage& message)
{
	BKeyType type;
	if (message.FindUInt32("type", (uint32*)&type) != B_OK || type != Type())
		return B_BAD_VALUE;

	const void* data = NULL;
	ssize_t dataLength = 0;
	if (message.FindUInt32("purpose", (uint32*)&fPurpose) != B_OK
		|| message.FindString("identifier", &fIdentifier) != B_OK
		|| message.FindString("secondaryIdentifier", &fSecondaryIdentifier)
			!= B_OK
		|| message.FindString("owner", &fOwner) != B_OK
		|| message.FindInt64("creationTime", &fCreationTime) != B_OK
		|| message.FindData("data", B_RAW_TYPE, &data, &dataLength) != B_OK
		|| dataLength < 0) {
		return B_ERROR;
	}

	return SetData((const uint8*)data, (size_t)dataLength);
}


/** @brief Assignment operator. Copies all properties from another key.
 *  @param other The key to copy from.
 *  @return A reference to this key.
 */
BKey&
BKey::operator=(const BKey& other)
{
	SetPurpose(other.Purpose());
	SetData((const uint8*)other.Data(), other.DataLength());

	fIdentifier = other.fIdentifier;
	fSecondaryIdentifier = other.fSecondaryIdentifier;
	fOwner = other.fOwner;
	fCreationTime = other.fCreationTime;

	return *this;
}


/** @brief Tests equality by comparing type, purpose, identifiers, owner, and data.
 *  @param other The key to compare with.
 *  @return true if all fields are equal, false otherwise.
 */
bool
BKey::operator==(const BKey& other) const
{
	return Type() == other.Type()
		&& DataLength() == other.DataLength()
		&& Purpose() == other.Purpose()
		&& fOwner == other.fOwner
		&& fIdentifier == other.fIdentifier
		&& fSecondaryIdentifier == other.fSecondaryIdentifier
		&& memcmp(Data(), other.Data(), DataLength()) == 0;
}


/** @brief Tests inequality between two keys.
 *  @param other The key to compare with.
 *  @return true if the keys differ in any field, false otherwise.
 */
bool
BKey::operator!=(const BKey& other) const
{
	return !(*this == other);
}


/** @brief Prints a human-readable representation of the key to standard output.
 *
 *  Outputs the key type, purpose, identifiers, owner, creation time,
 *  and raw data length.
 */
void
BKey::PrintToStream()
{
	if (Type() == B_KEY_TYPE_GENERIC)
		printf("generic key:\n");

	const char* purposeString = "unknown";
	switch (fPurpose) {
		case B_KEY_PURPOSE_ANY:
			purposeString = "any";
			break;
		case B_KEY_PURPOSE_GENERIC:
			purposeString = "generic";
			break;
		case B_KEY_PURPOSE_KEYRING:
			purposeString = "keyring";
			break;
		case B_KEY_PURPOSE_WEB:
			purposeString = "web";
			break;
		case B_KEY_PURPOSE_NETWORK:
			purposeString = "network";
			break;
		case B_KEY_PURPOSE_VOLUME:
			purposeString = "volume";
			break;
	}

	printf("\tpurpose: %s\n", purposeString);
	printf("\tidentifier: \"%s\"\n", fIdentifier.String());
	printf("\tsecondary identifier: \"%s\"\n", fSecondaryIdentifier.String());
	printf("\towner: \"%s\"\n", fOwner.String());
	printf("\tcreation time: %" B_PRIu64 "\n", fCreationTime);
	printf("\traw data length: %" B_PRIuSIZE "\n", fData.BufferLength());
}


// #pragma mark - BPasswordKey


/** @brief Default constructor. Creates an empty password key.
 */
BPasswordKey::BPasswordKey()
{
}


/** @brief Constructs a password key with the given credentials.
 *  @param password             The password string (stored including null terminator).
 *  @param purpose              The key purpose (e.g., B_KEY_PURPOSE_WEB).
 *  @param identifier           The primary identifier (e.g., a URL or service name).
 *  @param secondaryIdentifier  An optional secondary identifier (e.g., a username).
 */
BPasswordKey::BPasswordKey(const char* password, BKeyPurpose purpose,
	const char* identifier, const char* secondaryIdentifier)
	:
	BKey(purpose, identifier, secondaryIdentifier, (const uint8*)password,
		strlen(password) + 1)
{
}


/** @brief Copy constructor. Creates a password key from another.
 *  @param other The password key to copy.
 */
BPasswordKey::BPasswordKey(BPasswordKey& other)
{
}


/** @brief Destructor.
 */
BPasswordKey::~BPasswordKey()
{
}


/** @brief Initializes the password key with the given credentials.
 *  @param password             The password string.
 *  @param purpose              The key purpose.
 *  @param identifier           The primary identifier.
 *  @param secondaryIdentifier  An optional secondary identifier.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BPasswordKey::SetTo(const char* password, BKeyPurpose purpose,
	const char* identifier, const char* secondaryIdentifier)
{
	return BKey::SetTo(purpose, identifier, secondaryIdentifier,
		(const uint8*)password, strlen(password) + 1);
}


/** @brief Sets the password for this key.
 *  @param password The password string to store (including null terminator).
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BPasswordKey::SetPassword(const char* password)
{
	return SetData((const uint8*)password, strlen(password) + 1);
}


/** @brief Returns the stored password.
 *  @return The password as a C string.
 */
const char*
BPasswordKey::Password() const
{
	return (const char*)Data();
}


/** @brief Prints a human-readable representation of the password key to standard output.
 *
 *  Outputs the base key information followed by the password value.
 */
void
BPasswordKey::PrintToStream()
{
	printf("password key:\n");
	BKey::PrintToStream();
	printf("\tpassword: \"%s\"\n", Password());
}
