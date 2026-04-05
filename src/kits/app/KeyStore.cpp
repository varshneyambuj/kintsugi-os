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
 *   Copyright 2011, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */


/** @file KeyStore.cpp
 *  @brief Implementation of BKeyStore, the secure key management interface.
 *
 *  BKeyStore provides an API for storing, retrieving, and managing
 *  cryptographic keys and credentials. It communicates with the
 *  keystore server to persist keys in organized keyrings, manage
 *  application access permissions, and support master keyring locking.
 */


#include <KeyStore.h>

#include <KeyStoreDefs.h>

#include <Messenger.h>
#include <Roster.h>


using namespace BPrivate;


/** @brief Default constructor.
 */
BKeyStore::BKeyStore()
{
}


/** @brief Destructor.
 */
BKeyStore::~BKeyStore()
{
}


// #pragma mark - Key handling


/** @brief Retrieves a key by type and identifier from the default keyring.
 *  @param type       The key type to search for.
 *  @param identifier The primary identifier of the key.
 *  @param key        The BKey to populate with the retrieved key data.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BKeyStore::GetKey(BKeyType type, const char* identifier, BKey& key)
{
	return GetKey(NULL, type, identifier, NULL, true, key);
}


/** @brief Retrieves a key by type and both identifiers from the default keyring.
 *  @param type                 The key type to search for.
 *  @param identifier           The primary identifier of the key.
 *  @param secondaryIdentifier  The secondary identifier to match.
 *  @param key                  The BKey to populate with the retrieved key data.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BKeyStore::GetKey(BKeyType type, const char* identifier,
	const char* secondaryIdentifier, BKey& key)
{
	return GetKey(NULL, type, identifier, secondaryIdentifier, false, key);
}


/** @brief Retrieves a key with optional secondary identifier matching from the default keyring.
 *  @param type                          The key type to search for.
 *  @param identifier                    The primary identifier of the key.
 *  @param secondaryIdentifier           The secondary identifier to match.
 *  @param secondaryIdentifierOptional   If true, match even without a secondary identifier.
 *  @param key                           The BKey to populate with the retrieved key data.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BKeyStore::GetKey(BKeyType type, const char* identifier,
	const char* secondaryIdentifier, bool secondaryIdentifierOptional,
	BKey& key)
{
	return GetKey(NULL, type, identifier, secondaryIdentifier,
		secondaryIdentifierOptional, key);
}


/** @brief Retrieves a key by type and identifier from a named keyring.
 *  @param keyring    The keyring name, or NULL for the default keyring.
 *  @param type       The key type to search for.
 *  @param identifier The primary identifier of the key.
 *  @param key        The BKey to populate with the retrieved key data.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BKeyStore::GetKey(const char* keyring, BKeyType type, const char* identifier,
	BKey& key)
{
	return GetKey(keyring, type, identifier, NULL, true, key);
}


/** @brief Retrieves a key by type and both identifiers from a named keyring.
 *  @param keyring              The keyring name, or NULL for the default keyring.
 *  @param type                 The key type to search for.
 *  @param identifier           The primary identifier of the key.
 *  @param secondaryIdentifier  The secondary identifier to match.
 *  @param key                  The BKey to populate with the retrieved key data.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BKeyStore::GetKey(const char* keyring, BKeyType type, const char* identifier,
	const char* secondaryIdentifier, BKey& key)
{
	return GetKey(keyring, type, identifier, secondaryIdentifier, false, key);
}


/** @brief Retrieves a key from a named keyring with full search criteria.
 *
 *  This is the most complete GetKey overload. It sends a request to the
 *  keystore server to look up a key matching the given type, identifier,
 *  and secondary identifier within the specified keyring.
 *
 *  @param keyring                       The keyring name, or NULL for the default keyring.
 *  @param type                          The key type to search for.
 *  @param identifier                    The primary identifier of the key.
 *  @param secondaryIdentifier           The secondary identifier to match.
 *  @param secondaryIdentifierOptional   If true, match even without a secondary identifier.
 *  @param key                           The BKey to populate with the retrieved key data.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BKeyStore::GetKey(const char* keyring, BKeyType type, const char* identifier,
	const char* secondaryIdentifier, bool secondaryIdentifierOptional,
	BKey& key)
{
	BMessage message(KEY_STORE_GET_KEY);
	message.AddString("keyring", keyring);
	message.AddUInt32("type", type);
	message.AddString("identifier", identifier);
	message.AddString("secondaryIdentifier", secondaryIdentifier);
	message.AddBool("secondaryIdentifierOptional", secondaryIdentifierOptional);

	BMessage reply;
	status_t result = _SendKeyMessage(message, &reply);
	if (result != B_OK)
		return result;

	BMessage keyMessage;
	if (reply.FindMessage("key", &keyMessage) != B_OK)
		return B_ERROR;

	return key.Unflatten(keyMessage);
}


/** @brief Adds a key to the default keyring.
 *  @param key The key to add.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BKeyStore::AddKey(const BKey& key)
{
	return AddKey(NULL, key);
}


/** @brief Adds a key to the specified keyring.
 *  @param keyring The keyring name, or NULL for the default keyring.
 *  @param key     The key to add.
 *  @return B_OK on success, B_BAD_VALUE if the key cannot be flattened, or an error code.
 */
status_t
BKeyStore::AddKey(const char* keyring, const BKey& key)
{
	BMessage keyMessage;
	if (key.Flatten(keyMessage) != B_OK)
		return B_BAD_VALUE;

	BMessage message(KEY_STORE_ADD_KEY);
	message.AddString("keyring", keyring);
	message.AddMessage("key", &keyMessage);

	return _SendKeyMessage(message, NULL);
}


/** @brief Removes a key from the default keyring.
 *  @param key The key to remove.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BKeyStore::RemoveKey(const BKey& key)
{
	return RemoveKey(NULL, key);
}


/** @brief Removes a key from the specified keyring.
 *  @param keyring The keyring name, or NULL for the default keyring.
 *  @param key     The key to remove.
 *  @return B_OK on success, B_BAD_VALUE if the key cannot be flattened, or an error code.
 */
status_t
BKeyStore::RemoveKey(const char* keyring, const BKey& key)
{
	BMessage keyMessage;
	if (key.Flatten(keyMessage) != B_OK)
		return B_BAD_VALUE;

	BMessage message(KEY_STORE_REMOVE_KEY);
	message.AddString("keyring", keyring);
	message.AddMessage("key", &keyMessage);

	return _SendKeyMessage(message, NULL);
}


/** @brief Iterates over all keys in the default keyring.
 *  @param cookie An iteration cookie; initialize to 0 before the first call.
 *  @param key    The BKey to populate with the next key.
 *  @return B_OK on success, or B_ENTRY_NOT_FOUND when no more keys remain.
 */
status_t
BKeyStore::GetNextKey(uint32& cookie, BKey& key)
{
	return GetNextKey(NULL, cookie, key);
}


/** @brief Iterates over keys of a specific type and purpose in the default keyring.
 *  @param type    The key type to filter by, or B_KEY_TYPE_ANY.
 *  @param purpose The key purpose to filter by, or B_KEY_PURPOSE_ANY.
 *  @param cookie  An iteration cookie; initialize to 0 before the first call.
 *  @param key     The BKey to populate with the next key.
 *  @return B_OK on success, or B_ENTRY_NOT_FOUND when no more keys remain.
 */
status_t
BKeyStore::GetNextKey(BKeyType type, BKeyPurpose purpose, uint32& cookie,
	BKey& key)
{
	return GetNextKey(NULL, type, purpose, cookie, key);
}


/** @brief Iterates over all keys in the specified keyring.
 *  @param keyring The keyring name, or NULL for the default keyring.
 *  @param cookie  An iteration cookie; initialize to 0 before the first call.
 *  @param key     The BKey to populate with the next key.
 *  @return B_OK on success, or B_ENTRY_NOT_FOUND when no more keys remain.
 */
status_t
BKeyStore::GetNextKey(const char* keyring, uint32& cookie, BKey& key)
{
	return GetNextKey(keyring, B_KEY_TYPE_ANY, B_KEY_PURPOSE_ANY, cookie, key);
}


/** @brief Iterates over keys of a specific type and purpose in a named keyring.
 *
 *  This is the most complete GetNextKey overload. The cookie is updated
 *  on each call to track the iteration position.
 *
 *  @param keyring The keyring name, or NULL for the default keyring.
 *  @param type    The key type to filter by, or B_KEY_TYPE_ANY.
 *  @param purpose The key purpose to filter by, or B_KEY_PURPOSE_ANY.
 *  @param cookie  An iteration cookie; initialize to 0 before the first call.
 *  @param key     The BKey to populate with the next key.
 *  @return B_OK on success, or B_ENTRY_NOT_FOUND when no more keys remain.
 */
status_t
BKeyStore::GetNextKey(const char* keyring, BKeyType type, BKeyPurpose purpose,
	uint32& cookie, BKey& key)
{
	BMessage message(KEY_STORE_GET_NEXT_KEY);
	message.AddString("keyring", keyring);
	message.AddUInt32("type", type);
	message.AddUInt32("purpose", purpose);
	message.AddUInt32("cookie", cookie);

	BMessage reply;
	status_t result = _SendKeyMessage(message, &reply);
	if (result != B_OK)
		return result;

	BMessage keyMessage;
	if (reply.FindMessage("key", &keyMessage) != B_OK)
		return B_ERROR;

	reply.FindUInt32("cookie", &cookie);
	return key.Unflatten(keyMessage);
}


// #pragma mark - Keyrings


/** @brief Creates a new keyring with the specified name.
 *  @param keyring The name of the keyring to create.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BKeyStore::AddKeyring(const char* keyring)
{
	BMessage message(KEY_STORE_ADD_KEYRING);
	message.AddString("keyring", keyring);
	return _SendKeyMessage(message, NULL);
}


/** @brief Removes a keyring and all keys it contains.
 *  @param keyring The name of the keyring to remove.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BKeyStore::RemoveKeyring(const char* keyring)
{
	BMessage message(KEY_STORE_REMOVE_KEYRING);
	message.AddString("keyring", keyring);
	return _SendKeyMessage(message, NULL);
}


/** @brief Iterates over all available keyrings.
 *  @param cookie  An iteration cookie; initialize to 0 before the first call.
 *  @param keyring A BString to receive the name of the next keyring.
 *  @return B_OK on success, or B_ENTRY_NOT_FOUND when no more keyrings remain.
 */
status_t
BKeyStore::GetNextKeyring(uint32& cookie, BString& keyring)
{
	BMessage message(KEY_STORE_GET_NEXT_KEYRING);
	message.AddUInt32("cookie", cookie);

	BMessage reply;
	status_t result = _SendKeyMessage(message, &reply);
	if (result != B_OK)
		return result;

	if (reply.FindString("keyring", &keyring) != B_OK)
		return B_ERROR;

	reply.FindUInt32("cookie", &cookie);
	return B_OK;
}


/** @brief Sets the unlock key for a keyring.
 *
 *  The unlock key is required to access the keyring's contents when
 *  the keyring is locked.
 *
 *  @param keyring The keyring name.
 *  @param key     The key to use for unlocking.
 *  @return B_OK on success, B_BAD_VALUE if the key cannot be flattened, or an error code.
 */
status_t
BKeyStore::SetUnlockKey(const char* keyring, const BKey& key)
{
	BMessage keyMessage;
	if (key.Flatten(keyMessage) != B_OK)
		return B_BAD_VALUE;

	BMessage message(KEY_STORE_SET_UNLOCK_KEY);
	message.AddString("keyring", keyring);
	message.AddMessage("key", &keyMessage);

	return _SendKeyMessage(message, NULL);
}


/** @brief Removes the unlock key from a keyring.
 *  @param keyring The keyring name.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BKeyStore::RemoveUnlockKey(const char* keyring)
{
	BMessage message(KEY_STORE_REMOVE_UNLOCK_KEY);
	message.AddString("keyring", keyring);
	return _SendKeyMessage(message, NULL);
}


// #pragma mark - Master key


/** @brief Sets the unlock key for the master keyring.
 *  @param key The key to use for unlocking the master keyring.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BKeyStore::SetMasterUnlockKey(const BKey& key)
{
	return SetUnlockKey(NULL, key);
}


/** @brief Removes the unlock key from the master keyring.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BKeyStore::RemoveMasterUnlockKey()
{
	return RemoveUnlockKey(NULL);
}


/** @brief Adds a keyring to the master keyring's management.
 *
 *  Keyrings added to the master are automatically unlocked when
 *  the master keyring is unlocked.
 *
 *  @param keyring The name of the keyring to add.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BKeyStore::AddKeyringToMaster(const char* keyring)
{
	BMessage message(KEY_STORE_ADD_KEYRING_TO_MASTER);
	message.AddString("keyring", keyring);
	return _SendKeyMessage(message, NULL);
}


/** @brief Removes a keyring from the master keyring's management.
 *  @param keyring The name of the keyring to remove.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BKeyStore::RemoveKeyringFromMaster(const char* keyring)
{
	BMessage message(KEY_STORE_REMOVE_KEYRING_FROM_MASTER);
	message.AddString("keyring", keyring);
	return _SendKeyMessage(message, NULL);
}


/** @brief Iterates over keyrings managed by the master keyring.
 *  @param cookie  An iteration cookie; initialize to 0 before the first call.
 *  @param keyring A BString to receive the name of the next master-managed keyring.
 *  @return B_OK on success, or B_ENTRY_NOT_FOUND when no more keyrings remain.
 */
status_t
BKeyStore::GetNextMasterKeyring(uint32& cookie, BString& keyring)
{
	BMessage message(KEY_STORE_GET_NEXT_MASTER_KEYRING);
	message.AddUInt32("cookie", cookie);

	BMessage reply;
	status_t result = _SendKeyMessage(message, &reply);
	if (result != B_OK)
		return result;

	if (reply.FindString("keyring", &keyring) != B_OK)
		return B_ERROR;

	reply.FindUInt32("cookie", &cookie);
	return B_OK;
}


// #pragma mark - Locking


/** @brief Checks whether a keyring is currently unlocked.
 *  @param keyring The keyring name to check.
 *  @return true if the keyring is unlocked, false otherwise or on error.
 */
bool
BKeyStore::IsKeyringUnlocked(const char* keyring)
{
	BMessage message(KEY_STORE_IS_KEYRING_UNLOCKED);
	message.AddString("keyring", keyring);

	BMessage reply;
	if (_SendKeyMessage(message, &reply) != B_OK)
		return false;

	bool unlocked;
	if (reply.FindBool("unlocked", &unlocked) != B_OK)
		return false;

	return unlocked;
}


/** @brief Locks a keyring, preventing access until it is unlocked.
 *  @param keyring The keyring name to lock.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BKeyStore::LockKeyring(const char* keyring)
{
	BMessage message(KEY_STORE_LOCK_KEYRING);
	message.AddString("keyring", keyring);
	return _SendKeyMessage(message, NULL);
}


/** @brief Locks the master keyring.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BKeyStore::LockMasterKeyring()
{
	return LockKeyring(NULL);
}



// #pragma mark - Applications


/** @brief Iterates over applications with access to the default keyring.
 *  @param cookie   An iteration cookie; initialize to 0 before the first call.
 *  @param signature A BString to receive the next application's MIME signature.
 *  @return B_OK on success, or B_ENTRY_NOT_FOUND when no more applications remain.
 */
status_t
BKeyStore::GetNextApplication(uint32& cookie, BString& signature) const
{
	return GetNextApplication(NULL, cookie, signature);
}


/** @brief Iterates over applications with access to the specified keyring.
 *  @param keyring   The keyring name, or NULL for the default keyring.
 *  @param cookie    An iteration cookie; initialize to 0 before the first call.
 *  @param signature A BString to receive the next application's MIME signature.
 *  @return B_OK on success, or B_ENTRY_NOT_FOUND when no more applications remain.
 */
status_t
BKeyStore::GetNextApplication(const char* keyring, uint32& cookie,
	BString& signature) const
{
	BMessage message(KEY_STORE_GET_NEXT_APPLICATION);
	message.AddString("keyring", keyring);
	message.AddUInt32("cookie", cookie);

	BMessage reply;
	status_t result = _SendKeyMessage(message, &reply);
	if (result != B_OK)
		return result;

	if (reply.FindString("signature", &signature) != B_OK)
		return B_ERROR;

	reply.FindUInt32("cookie", &cookie);
	return B_OK;
}


/** @brief Revokes an application's access to the default keyring.
 *  @param signature The MIME signature of the application to remove.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BKeyStore::RemoveApplication(const char* signature)
{
	return RemoveApplication(NULL, signature);
}


/** @brief Revokes an application's access to the specified keyring.
 *  @param keyring   The keyring name, or NULL for the default keyring.
 *  @param signature The MIME signature of the application to remove.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BKeyStore::RemoveApplication(const char* keyring, const char* signature)
{
	BMessage message(KEY_STORE_REMOVE_APPLICATION);
	message.AddString("keyring", keyring);
	message.AddString("signature", signature);

	return _SendKeyMessage(message, NULL);
}


// #pragma mark - Service functions


/** @brief Generates a random password and stores it in a BPasswordKey.
 *
 *  Not yet implemented. Currently always returns B_ERROR.
 *
 *  @param password The BPasswordKey to receive the generated password.
 *  @param length   The desired password length in characters.
 *  @param flags    Flags controlling password generation characteristics.
 *  @return B_ERROR (not yet implemented).
 */
status_t
BKeyStore::GeneratePassword(BPasswordKey& password, size_t length, uint32 flags)
{
	return B_ERROR;
}


/** @brief Evaluates the strength of a password.
 *
 *  Not yet implemented. Currently always returns 0.
 *
 *  @param password The password string to evaluate.
 *  @return A strength score (currently always 0).
 */
float
BKeyStore::PasswordStrength(const char* password)
{
	return 0;
}


// #pragma mark - Private functions


/** @brief Sends a message to the keystore server and retrieves the reply.
 *
 *  If the keystore server is not running, attempts to launch it via
 *  the roster before sending. Checks the reply for a success indicator
 *  and extracts a result error code on failure.
 *
 *  @param message The message to send to the keystore server.
 *  @param reply   A pointer to receive the reply, or NULL to use a local reply.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BKeyStore::_SendKeyMessage(BMessage& message, BMessage* reply) const
{
	BMessage localReply;
	if (reply == NULL)
		reply = &localReply;

	BMessenger messenger(kKeyStoreServerSignature);
	if (!messenger.IsValid()) {
		// Try to start the keystore server.
		status_t result = be_roster->Launch(kKeyStoreServerSignature);
		if (result != B_OK && result != B_ALREADY_RUNNING)
			return B_ERROR;

		// Then re-target the messenger and check again.
		messenger.SetTo(kKeyStoreServerSignature);
		if (!messenger.IsValid())
			return B_ERROR;
	}

	if (messenger.SendMessage(&message, reply) != B_OK)
		return B_ERROR;

	if (reply->what != KEY_STORE_SUCCESS) {
		status_t result = B_ERROR;
		if (reply->FindInt32("result", &result) != B_OK)
			return B_ERROR;

		return result;
	}

	return B_OK;
}
