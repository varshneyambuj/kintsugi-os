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
   Copyright 2012, Michael Lotz, mmlr@mlotz.ch. All Rights Reserved.
   Distributed under the terms of the MIT License.
 */
/** @file Keyring.cpp
 *  @brief Keyring implementation for storing and managing credentials. */
#include "Keyring.h"


/** @brief Constructs an unnamed, locked keyring with no unlock key. */
Keyring::Keyring()
	:
	fHasUnlockKey(false),
	fUnlocked(false),
	fModified(false)
{
}


/**
 * @brief Constructs a named, locked keyring with no unlock key.
 *
 * @param name The name of the keyring.
 */
Keyring::Keyring(const char* name)
	:
	fName(name),
	fHasUnlockKey(false),
	fUnlocked(false),
	fModified(false)
{
}


/** @brief Destroys the keyring. */
Keyring::~Keyring()
{
}


/**
 * @brief Deserialises the keyring state from a BMessage.
 *
 * Reads the keyring name, unlock key presence flag, and encrypted data
 * blob from the message into internal fields.
 *
 * @param message The source BMessage containing the serialised keyring.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Keyring::ReadFromMessage(const BMessage& message)
{
	status_t result = message.FindString("name", &fName);
	if (result != B_OK)
		return result;

	result = message.FindBool("hasUnlockKey", &fHasUnlockKey);
	if (result != B_OK)
		return result;

	if (message.GetBool("noData", false)) {
		fFlatBuffer.SetSize(0);
		return B_OK;
	}

	ssize_t size;
	const void* data;
	result = message.FindData("data", B_RAW_TYPE, &data, &size);
	if (result != B_OK)
		return result;

	if (size < 0)
		return B_ERROR;

	fFlatBuffer.SetSize(0);
	ssize_t written = fFlatBuffer.WriteAt(0, data, size);
	if (written != size) {
		fFlatBuffer.SetSize(0);
		return written < 0 ? written : B_ERROR;
	}

	return B_OK;
}


/**
 * @brief Serialises the keyring state into a BMessage for persistent storage.
 *
 * Encrypts the data to the flat buffer first, then writes the buffer,
 * unlock key flag, and name into the message.
 *
 * @param message The destination BMessage.
 * @return B_OK on success, or an error code on failure.
 */
status_t
Keyring::WriteToMessage(BMessage& message)
{
	status_t result = _EncryptToFlatBuffer();
	if (result != B_OK)
		return result;

	if (fFlatBuffer.BufferLength() == 0)
		result = message.AddBool("noData", true);
	else {
		result = message.AddData("data", B_RAW_TYPE, fFlatBuffer.Buffer(),
			fFlatBuffer.BufferLength());
	}
	if (result != B_OK)
		return result;

	result = message.AddBool("hasUnlockKey", fHasUnlockKey);
	if (result != B_OK)
		return result;

	return message.AddString("name", fName);
}


/**
 * @brief Unlocks the keyring using the provided passphrase key.
 *
 * If the keyring has an unlock key, a matching keyMessage must be
 * provided. Decrypts the flat buffer into the in-memory data and
 * application maps.
 *
 * @param keyMessage The key to decrypt with, or NULL for unprotected keyrings.
 * @return B_OK on success, B_BAD_VALUE if key presence is mismatched,
 *         or an error from decryption.
 */
status_t
Keyring::Unlock(const BMessage* keyMessage)
{
	if (fUnlocked)
		return B_OK;

	if (fHasUnlockKey == (keyMessage == NULL))
		return B_BAD_VALUE;

	if (keyMessage != NULL)
		fUnlockKey = *keyMessage;

	status_t result = _DecryptFromFlatBuffer();
	if (result != B_OK) {
		fUnlockKey.MakeEmpty();
		return result;
	}

	fUnlocked = true;
	return B_OK;
}


/**
 * @brief Locks the keyring, encrypting data back to the flat buffer.
 *
 * Clears the unlock key and in-memory data/application maps. Does
 * nothing if the keyring is already locked.
 */
void
Keyring::Lock()
{
	if (!fUnlocked)
		return;

	_EncryptToFlatBuffer();

	fUnlockKey.MakeEmpty();
	fData.MakeEmpty();
	fApplications.MakeEmpty();
	fUnlocked = false;
}


/** @brief Returns whether the keyring is currently unlocked. */
bool
Keyring::IsUnlocked() const
{
	return fUnlocked;
}


/** @brief Returns whether the keyring is protected by an unlock key. */
bool
Keyring::HasUnlockKey() const
{
	return fHasUnlockKey;
}


/** @brief Returns a const reference to the current unlock key message. */
const BMessage&
Keyring::UnlockKey() const
{
	return fUnlockKey;
}


/**
 * @brief Sets the unlock key for this keyring.
 *
 * The keyring must be unlocked before calling this method.
 *
 * @param keyMessage The new unlock key.
 * @return B_OK on success, B_NOT_ALLOWED if the keyring is locked.
 */
status_t
Keyring::SetUnlockKey(const BMessage& keyMessage)
{
	if (!fUnlocked)
		return B_NOT_ALLOWED;

	fHasUnlockKey = true;
	fUnlockKey = keyMessage;
	fModified = true;
	return B_OK;
}


/**
 * @brief Removes the unlock key, making the keyring unprotected.
 *
 * @return B_OK on success, B_NOT_ALLOWED if the keyring is locked.
 */
status_t
Keyring::RemoveUnlockKey()
{
	if (!fUnlocked)
		return B_NOT_ALLOWED;

	fUnlockKey.MakeEmpty();
	fHasUnlockKey = false;
	fModified = true;
	return B_OK;
}


/**
 * @brief Iterates through registered applications that have access to this keyring.
 *
 * @param cookie   Iteration index; incremented on each successful call.
 * @param signature Output: the application's MIME signature.
 * @param path      Output: the application's executable path.
 * @return B_OK on success, B_NOT_ALLOWED if locked, B_ENTRY_NOT_FOUND at end.
 */
status_t
Keyring::GetNextApplication(uint32& cookie, BString& signature,
	BString& path)
{
	if (!fUnlocked)
		return B_NOT_ALLOWED;

	char* nameFound = NULL;
	status_t result = fApplications.GetInfo(B_MESSAGE_TYPE, cookie++,
		&nameFound, NULL);
	if (result != B_OK)
		return B_ENTRY_NOT_FOUND;

	BMessage appMessage;
	result = fApplications.FindMessage(nameFound, &appMessage);
	if (result != B_OK)
		return B_ENTRY_NOT_FOUND;

	result = appMessage.FindString("path", &path);
	if (result != B_OK)
		return B_ERROR;

	signature = nameFound;
	return B_OK;
}


/**
 * @brief Finds a registered application by its signature and executable path.
 *
 * @param signature The application's MIME signature.
 * @param path      The application's executable path.
 * @param appMessage Output: the application's access record.
 * @return B_OK if found, B_NOT_ALLOWED if locked, B_ENTRY_NOT_FOUND if absent.
 */
status_t
Keyring::FindApplication(const char* signature, const char* path,
	BMessage& appMessage)
{
	if (!fUnlocked)
		return B_NOT_ALLOWED;

	int32 count;
	type_code type;
	if (fApplications.GetInfo(signature, &type, &count) != B_OK)
		return B_ENTRY_NOT_FOUND;

	for (int32 i = 0; i < count; i++) {
		if (fApplications.FindMessage(signature, i, &appMessage) != B_OK)
			continue;

		BString appPath;
		if (appMessage.FindString("path", &appPath) != B_OK)
			continue;

		if (appPath == path)
			return B_OK;
	}

	appMessage.MakeEmpty();
	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Registers an application's access record in this keyring.
 *
 * @param signature  The application's MIME signature.
 * @param appMessage The access record to store.
 * @return B_OK on success, B_NOT_ALLOWED if locked.
 */
status_t
Keyring::AddApplication(const char* signature, const BMessage& appMessage)
{
	if (!fUnlocked)
		return B_NOT_ALLOWED;

	status_t result = fApplications.AddMessage(signature, &appMessage);
	if (result != B_OK)
		return result;

	fModified = true;
	return B_OK;
}


/**
 * @brief Removes an application's access from this keyring.
 *
 * If @a path is NULL, all entries for the given signature are removed.
 * Otherwise, only the entry matching both signature and path is removed.
 *
 * @param signature The application's MIME signature.
 * @param path      The executable path, or NULL to remove all entries.
 * @return B_OK on success, B_NOT_ALLOWED if locked, B_ENTRY_NOT_FOUND if absent.
 */
status_t
Keyring::RemoveApplication(const char* signature, const char* path)
{
	if (!fUnlocked)
		return B_NOT_ALLOWED;

	if (path == NULL) {
		// We want all of the entries for this signature removed.
		status_t result = fApplications.RemoveName(signature);
		if (result != B_OK)
			return B_ENTRY_NOT_FOUND;

		fModified = true;
		return B_OK;
	}

	int32 count;
	type_code type;
	if (fApplications.GetInfo(signature, &type, &count) != B_OK)
		return B_ENTRY_NOT_FOUND;

	for (int32 i = 0; i < count; i++) {
		BMessage appMessage;
		if (fApplications.FindMessage(signature, i, &appMessage) != B_OK)
			return B_ERROR;

		BString appPath;
		if (appMessage.FindString("path", &appPath) != B_OK)
			continue;

		if (appPath == path) {
			fApplications.RemoveData(signature, i);
			fModified = true;
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Searches for a key by primary and secondary identifier.
 *
 * If @a secondaryIdentifierOptional is true and no exact secondary match
 * is found, the first key with a matching primary identifier is returned.
 *
 * @param identifier                  The primary key identifier.
 * @param secondaryIdentifier         The secondary key identifier.
 * @param secondaryIdentifierOptional If true, allows partial matching.
 * @param _foundKeyMessage            Output: the matching key message, or NULL to test existence.
 * @return B_OK if found, B_NOT_ALLOWED if locked, B_ENTRY_NOT_FOUND otherwise.
 */
status_t
Keyring::FindKey(const BString& identifier, const BString& secondaryIdentifier,
	bool secondaryIdentifierOptional, BMessage* _foundKeyMessage) const
{
	if (!fUnlocked)
		return B_NOT_ALLOWED;

	int32 count;
	type_code type;
	if (fData.GetInfo(identifier, &type, &count) != B_OK)
		return B_ENTRY_NOT_FOUND;

	// We have a matching primary identifier, need to check for the secondary
	// identifier.
	for (int32 i = 0; i < count; i++) {
		BMessage candidate;
		if (fData.FindMessage(identifier, i, &candidate) != B_OK)
			return B_ERROR;

		BString candidateIdentifier;
		if (candidate.FindString("secondaryIdentifier",
				&candidateIdentifier) != B_OK) {
			candidateIdentifier = "";
		}

		if (candidateIdentifier == secondaryIdentifier) {
			if (_foundKeyMessage != NULL)
				*_foundKeyMessage = candidate;
			return B_OK;
		}
	}

	// We didn't find an exact match.
	if (secondaryIdentifierOptional) {
		if (_foundKeyMessage == NULL)
			return B_OK;

		// The secondary identifier is optional, so we just return the
		// first entry.
		return fData.FindMessage(identifier, 0, _foundKeyMessage);
	}

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Searches for a key by type, purpose, and positional index.
 *
 * Iterates through all stored keys, filtering by type and purpose.
 * The @a index parameter selects the Nth matching key (zero-based).
 *
 * @param type              Key type filter, or B_KEY_TYPE_ANY for all.
 * @param purpose           Key purpose filter, or B_KEY_PURPOSE_ANY for all.
 * @param index             Zero-based index among matching keys.
 * @param _foundKeyMessage  Output: the matching key message.
 * @return B_OK if found, B_NOT_ALLOWED if locked, B_ENTRY_NOT_FOUND otherwise.
 */
status_t
Keyring::FindKey(BKeyType type, BKeyPurpose purpose, uint32 index,
	BMessage& _foundKeyMessage) const
{
	if (!fUnlocked)
		return B_NOT_ALLOWED;

	for (int32 keyIndex = 0;; keyIndex++) {
		int32 count = 0;
		char* identifier = NULL;
		if (fData.GetInfo(B_MESSAGE_TYPE, keyIndex, &identifier, NULL,
				&count) != B_OK) {
			break;
		}

		if (type == B_KEY_TYPE_ANY && purpose == B_KEY_PURPOSE_ANY) {
			// No need to inspect the actual keys.
			if ((int32)index >= count) {
				index -= count;
				continue;
			}

			return fData.FindMessage(identifier, index, &_foundKeyMessage);
		}

		// Go through the keys to check their type and purpose.
		for (int32 subkeyIndex = 0; subkeyIndex < count; subkeyIndex++) {
			BMessage subkey;
			if (fData.FindMessage(identifier, subkeyIndex, &subkey) != B_OK)
				return B_ERROR;

			bool match = true;
			if (type != B_KEY_TYPE_ANY) {
				BKeyType subkeyType;
				if (subkey.FindUInt32("type", (uint32*)&subkeyType) != B_OK)
					return B_ERROR;

				match = subkeyType == type;
			}

			if (match && purpose != B_KEY_PURPOSE_ANY) {
				BKeyPurpose subkeyPurpose;
				if (subkey.FindUInt32("purpose", (uint32*)&subkeyPurpose)
						!= B_OK) {
					return B_ERROR;
				}

				match = subkeyPurpose == purpose;
			}

			if (match) {
				if (index == 0) {
					_foundKeyMessage = subkey;
					return B_OK;
				}

				index--;
			}
		}
	}

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Adds a key to the keyring, checking for identifier collisions.
 *
 * @param identifier          The primary key identifier.
 * @param secondaryIdentifier The secondary key identifier.
 * @param keyMessage          The key data to store.
 * @return B_OK on success, B_NAME_IN_USE if a key with the same identifiers
 *         exists, B_NOT_ALLOWED if locked.
 */
status_t
Keyring::AddKey(const BString& identifier, const BString& secondaryIdentifier,
	const BMessage& keyMessage)
{
	if (!fUnlocked)
		return B_NOT_ALLOWED;

	// Check for collisions.
	if (FindKey(identifier, secondaryIdentifier, false, NULL) == B_OK)
		return B_NAME_IN_USE;

	// We're fine, just add the new key.
	status_t result = fData.AddMessage(identifier, &keyMessage);
	if (result != B_OK)
		return result;

	fModified = true;
	return B_OK;
}


/**
 * @brief Removes a key that exactly matches the given identifier and data.
 *
 * @param identifier The primary key identifier.
 * @param keyMessage The key data to match (exact comparison).
 * @return B_OK on success, B_NOT_ALLOWED if locked, B_ENTRY_NOT_FOUND if absent.
 */
status_t
Keyring::RemoveKey(const BString& identifier,
	const BMessage& keyMessage)
{
	if (!fUnlocked)
		return B_NOT_ALLOWED;

	int32 count;
	type_code type;
	if (fData.GetInfo(identifier, &type, &count) != B_OK)
		return B_ENTRY_NOT_FOUND;

	for (int32 i = 0; i < count; i++) {
		BMessage candidate;
		if (fData.FindMessage(identifier, i, &candidate) != B_OK)
			return B_ERROR;

		// We require an exact match.
		if (!candidate.HasSameData(keyMessage))
			continue;

		status_t result = fData.RemoveData(identifier, i);
		if (result != B_OK)
			return result;

		fModified = true;
		return B_OK;
	}

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Compares two keyrings by name for sorted insertion.
 *
 * @param one First keyring.
 * @param two Second keyring.
 * @return Negative, zero, or positive as in strcmp().
 */
int
Keyring::Compare(const Keyring* one, const Keyring* two)
{
	return strcmp(one->Name(), two->Name());
}


/**
 * @brief Compares a name string against a keyring's name for binary search.
 *
 * @param name    The name to search for.
 * @param keyring The keyring to compare against.
 * @return Negative, zero, or positive as in strcmp().
 */
int
Keyring::Compare(const BString* name, const Keyring* keyring)
{
	return strcmp(name->String(), keyring->Name());
}


/**
 * @brief Encrypts the in-memory data and applications into the flat buffer.
 *
 * Flattens the data and applications messages into a container, then
 * writes them to fFlatBuffer. Encryption of the buffer is not yet
 * implemented. Only operates if the keyring has been modified.
 *
 * @return B_OK on success, B_NOT_ALLOWED if locked.
 */
status_t
Keyring::_EncryptToFlatBuffer()
{
	if (!fModified)
		return B_OK;

	if (!fUnlocked)
		return B_NOT_ALLOWED;

	BMessage container;
	status_t result = container.AddMessage("data", &fData);
	if (result != B_OK)
		return result;

	result = container.AddMessage("applications", &fApplications);
	if (result != B_OK)
		return result;

	fFlatBuffer.SetSize(0);
	fFlatBuffer.Seek(0, SEEK_SET);

	result = container.Flatten(&fFlatBuffer);
	if (result != B_OK)
		return result;

	if (fHasUnlockKey) {
		// TODO: Actually encrypt the flat buffer...
	}

	fModified = false;
	return B_OK;
}


/**
 * @brief Decrypts the flat buffer into in-memory data and applications messages.
 *
 * Reads the container from fFlatBuffer and extracts the data and
 * applications sub-messages. Decryption is not yet implemented.
 *
 * @return B_OK on success, or an error code if the buffer is corrupt.
 */
status_t
Keyring::_DecryptFromFlatBuffer()
{
	if (fFlatBuffer.BufferLength() == 0)
		return B_OK;

	if (fHasUnlockKey) {
		// TODO: Actually decrypt the flat buffer...
	}

	BMessage container;
	fFlatBuffer.Seek(0, SEEK_SET);
	status_t result = container.Unflatten(&fFlatBuffer);
	if (result != B_OK)
		return result;

	result = container.FindMessage("data", &fData);
	if (result != B_OK)
		return result;

	result = container.FindMessage("applications", &fApplications);
	if (result != B_OK) {
		fData.MakeEmpty();
		return result;
	}

	return B_OK;
}
