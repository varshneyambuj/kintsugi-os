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
 *   Copyright 2011, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */
#ifndef _KEY_STORE_H
#define _KEY_STORE_H

/**
 * @file KeyStore.h
 * @brief Defines the BKeyStore class for secure credential and key management.
 */

#include <Key.h>


/**
 * @brief Provides access to the system's secure key and credential storage.
 *
 * BKeyStore is the primary interface for storing, retrieving, and managing
 * keys and credentials. Keys are organized into named keyrings, with a
 * default keyring used when no keyring name is specified.
 *
 * The key store supports:
 * - Storing and retrieving keys by type, identifier, and optional secondary identifier
 * - Organizing keys into named keyrings
 * - Locking and unlocking keyrings with unlock keys
 * - A master keyring that can aggregate other keyrings
 * - Per-application access control
 * - Password generation and strength evaluation
 *
 * @see BKey
 * @see BPasswordKey
 * @see BKeyType
 * @see BKeyPurpose
 */
class BKeyStore {
public:
	/**
	 * @brief Default constructor.
	 *
	 * Creates a BKeyStore instance connected to the system key store service.
	 */
								BKeyStore();

	/**
	 * @brief Destructor.
	 */
	virtual						~BKeyStore();

	/**
	 * @brief Retrieves a key by type and identifier from the default keyring.
	 *
	 * @param type        The type of key to find.
	 * @param identifier  The primary identifier of the key.
	 * @param key         Filled with the matching key on success.
	 * @return B_OK on success, B_ENTRY_NOT_FOUND if no match, or another error code.
	 */
			status_t			GetKey(BKeyType type, const char* identifier,
									BKey& key);

	/**
	 * @brief Retrieves a key by type, primary, and secondary identifier from the default keyring.
	 *
	 * @param type                 The type of key to find.
	 * @param identifier           The primary identifier of the key.
	 * @param secondaryIdentifier  The secondary identifier of the key.
	 * @param key                  Filled with the matching key on success.
	 * @return B_OK on success, B_ENTRY_NOT_FOUND if no match, or another error code.
	 */
			status_t			GetKey(BKeyType type, const char* identifier,
									const char* secondaryIdentifier, BKey& key);

	/**
	 * @brief Retrieves a key with optional secondary identifier matching from the default keyring.
	 *
	 * @param type                          The type of key to find.
	 * @param identifier                    The primary identifier of the key.
	 * @param secondaryIdentifier           The secondary identifier of the key.
	 * @param secondaryIdentifierOptional   If true, a match without the secondary
	 *                                      identifier is also accepted.
	 * @param key                           Filled with the matching key on success.
	 * @return B_OK on success, B_ENTRY_NOT_FOUND if no match, or another error code.
	 */
			status_t			GetKey(BKeyType type, const char* identifier,
									const char* secondaryIdentifier,
									bool secondaryIdentifierOptional,
									BKey& key);

	/**
	 * @brief Retrieves a key by type and identifier from a named keyring.
	 *
	 * @param keyring     The name of the keyring to search.
	 * @param type        The type of key to find.
	 * @param identifier  The primary identifier of the key.
	 * @param key         Filled with the matching key on success.
	 * @return B_OK on success, B_ENTRY_NOT_FOUND if no match, or another error code.
	 */
			status_t			GetKey(const char* keyring,
									BKeyType type, const char* identifier,
									BKey& key);

	/**
	 * @brief Retrieves a key by type, primary, and secondary identifier from a named keyring.
	 *
	 * @param keyring              The name of the keyring to search.
	 * @param type                 The type of key to find.
	 * @param identifier           The primary identifier of the key.
	 * @param secondaryIdentifier  The secondary identifier of the key.
	 * @param key                  Filled with the matching key on success.
	 * @return B_OK on success, B_ENTRY_NOT_FOUND if no match, or another error code.
	 */
			status_t			GetKey(const char* keyring,
									BKeyType type, const char* identifier,
									const char* secondaryIdentifier, BKey& key);

	/**
	 * @brief Retrieves a key with optional secondary identifier matching from a named keyring.
	 *
	 * @param keyring                       The name of the keyring to search.
	 * @param type                          The type of key to find.
	 * @param identifier                    The primary identifier of the key.
	 * @param secondaryIdentifier           The secondary identifier of the key.
	 * @param secondaryIdentifierOptional   If true, a match without the secondary
	 *                                      identifier is also accepted.
	 * @param key                           Filled with the matching key on success.
	 * @return B_OK on success, B_ENTRY_NOT_FOUND if no match, or another error code.
	 */
			status_t			GetKey(const char* keyring,
									BKeyType type, const char* identifier,
									const char* secondaryIdentifier,
									bool secondaryIdentifierOptional,
									BKey& key);

	/**
	 * @brief Adds a key to the default keyring.
	 *
	 * @param key  The key to add.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t			AddKey(const BKey& key);

	/**
	 * @brief Adds a key to a named keyring.
	 *
	 * @param keyring  The name of the keyring to add the key to.
	 * @param key      The key to add.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t			AddKey(const char* keyring, const BKey& key);

	/**
	 * @brief Removes a key from the default keyring.
	 *
	 * @param key  The key to remove.
	 * @return B_OK on success, B_ENTRY_NOT_FOUND if not found, or another error code.
	 */
			status_t			RemoveKey(const BKey& key);

	/**
	 * @brief Removes a key from a named keyring.
	 *
	 * @param keyring  The name of the keyring to remove the key from.
	 * @param key      The key to remove.
	 * @return B_OK on success, B_ENTRY_NOT_FOUND if not found, or another error code.
	 */
			status_t			RemoveKey(const char* keyring, const BKey& key);

	/**
	 * @brief Iterates through keys in the default keyring.
	 *
	 * Initialize @a cookie to 0 before the first call. Each call fills
	 * @a key with the next key and updates the cookie.
	 *
	 * @param cookie  An iteration cookie. Set to 0 to start.
	 * @param key     Filled with the next key on success.
	 * @return B_OK on success, B_ENTRY_NOT_FOUND when iteration is complete.
	 */
			status_t			GetNextKey(uint32& cookie, BKey& key);

	/**
	 * @brief Iterates through keys of a given type and purpose in the default keyring.
	 *
	 * @param type     The key type to filter by (B_KEY_TYPE_ANY for all).
	 * @param purpose  The key purpose to filter by (B_KEY_PURPOSE_ANY for all).
	 * @param cookie   An iteration cookie. Set to 0 to start.
	 * @param key      Filled with the next matching key on success.
	 * @return B_OK on success, B_ENTRY_NOT_FOUND when iteration is complete.
	 */
			status_t			GetNextKey(BKeyType type, BKeyPurpose purpose,
									uint32& cookie, BKey& key);

	/**
	 * @brief Iterates through keys in a named keyring.
	 *
	 * @param keyring  The name of the keyring to iterate.
	 * @param cookie   An iteration cookie. Set to 0 to start.
	 * @param key      Filled with the next key on success.
	 * @return B_OK on success, B_ENTRY_NOT_FOUND when iteration is complete.
	 */
			status_t			GetNextKey(const char* keyring,
									uint32& cookie, BKey& key);

	/**
	 * @brief Iterates through keys of a given type and purpose in a named keyring.
	 *
	 * @param keyring  The name of the keyring to iterate.
	 * @param type     The key type to filter by (B_KEY_TYPE_ANY for all).
	 * @param purpose  The key purpose to filter by (B_KEY_PURPOSE_ANY for all).
	 * @param cookie   An iteration cookie. Set to 0 to start.
	 * @param key      Filled with the next matching key on success.
	 * @return B_OK on success, B_ENTRY_NOT_FOUND when iteration is complete.
	 */
			status_t			GetNextKey(const char* keyring,
									BKeyType type, BKeyPurpose purpose,
									uint32& cookie, BKey& key);

			// Keyrings

	/**
	 * @brief Creates a new named keyring.
	 *
	 * @param keyring  The name for the new keyring.
	 * @return B_OK on success, B_NAME_IN_USE if it already exists, or another error code.
	 */
			status_t			AddKeyring(const char* keyring);

	/**
	 * @brief Removes a named keyring and all its keys.
	 *
	 * @param keyring  The name of the keyring to remove.
	 * @return B_OK on success, B_ENTRY_NOT_FOUND if not found, or another error code.
	 */
			status_t			RemoveKeyring(const char* keyring);

	/**
	 * @brief Iterates through available keyring names.
	 *
	 * Initialize @a cookie to 0 before the first call.
	 *
	 * @param cookie   An iteration cookie. Set to 0 to start.
	 * @param keyring  Filled with the next keyring name on success.
	 * @return B_OK on success, B_ENTRY_NOT_FOUND when iteration is complete.
	 */
			status_t			GetNextKeyring(uint32& cookie,
									BString& keyring);

	/**
	 * @brief Sets the unlock key for a named keyring.
	 *
	 * When set, the keyring requires this key to be provided to unlock it.
	 *
	 * @param keyring  The name of the keyring.
	 * @param key      The key required to unlock the keyring.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t			SetUnlockKey(const char* keyring,
									const BKey& key);

	/**
	 * @brief Removes the unlock key from a named keyring.
	 *
	 * After removal, the keyring no longer requires a key to unlock.
	 *
	 * @param keyring  The name of the keyring.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t			RemoveUnlockKey(const char* keyring);

			// Master keyring

	/**
	 * @brief Sets the unlock key for the master keyring.
	 *
	 * The master keyring controls access to all keyrings added to it.
	 *
	 * @param key  The key required to unlock the master keyring.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t			SetMasterUnlockKey(const BKey& key);

	/**
	 * @brief Removes the unlock key from the master keyring.
	 *
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t			RemoveMasterUnlockKey();

	/**
	 * @brief Adds a keyring to the master keyring.
	 *
	 * Keyrings added to the master keyring are unlocked when the master is unlocked.
	 *
	 * @param keyring  The name of the keyring to add to the master.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t			AddKeyringToMaster(const char* keyring);

	/**
	 * @brief Removes a keyring from the master keyring.
	 *
	 * @param keyring  The name of the keyring to remove from the master.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t			RemoveKeyringFromMaster(const char* keyring);

	/**
	 * @brief Iterates through keyrings in the master keyring.
	 *
	 * Initialize @a cookie to 0 before the first call.
	 *
	 * @param cookie   An iteration cookie. Set to 0 to start.
	 * @param keyring  Filled with the next keyring name on success.
	 * @return B_OK on success, B_ENTRY_NOT_FOUND when iteration is complete.
	 */
			status_t			GetNextMasterKeyring(uint32& cookie,
									BString& keyring);

			// Locking

	/**
	 * @brief Checks whether a named keyring is currently unlocked.
	 *
	 * @param keyring  The name of the keyring to check.
	 * @return true if the keyring is unlocked, false otherwise.
	 */
			bool				IsKeyringUnlocked(const char* keyring);

	/**
	 * @brief Locks a named keyring.
	 *
	 * Once locked, the keyring's keys cannot be accessed until it is unlocked.
	 *
	 * @param keyring  The name of the keyring to lock.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t			LockKeyring(const char* keyring);

	/**
	 * @brief Locks the master keyring.
	 *
	 * This also effectively locks all keyrings managed by the master keyring.
	 *
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t			LockMasterKeyring();

			// Applications

	/**
	 * @brief Iterates through applications with access to the default keyring.
	 *
	 * @param cookie     An iteration cookie. Set to 0 to start.
	 * @param signature  Filled with the next application signature on success.
	 * @return B_OK on success, B_ENTRY_NOT_FOUND when iteration is complete.
	 */
			status_t			GetNextApplication(uint32& cookie,
									BString& signature) const;

	/**
	 * @brief Iterates through applications with access to a named keyring.
	 *
	 * @param keyring    The name of the keyring.
	 * @param cookie     An iteration cookie. Set to 0 to start.
	 * @param signature  Filled with the next application signature on success.
	 * @return B_OK on success, B_ENTRY_NOT_FOUND when iteration is complete.
	 */
			status_t			GetNextApplication(const char* keyring,
									uint32& cookie, BString& signature) const;

	/**
	 * @brief Removes an application's access to the default keyring.
	 *
	 * @param signature  The signature of the application to remove.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t			RemoveApplication(const char* signature);

	/**
	 * @brief Removes an application's access to a named keyring.
	 *
	 * @param keyring    The name of the keyring.
	 * @param signature  The signature of the application to remove.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t			RemoveApplication(const char* keyring,
									const char* signature);

			// Service functions

	/**
	 * @brief Generates a random password.
	 *
	 * @param password  Filled with the generated password key on success.
	 * @param length    The desired length of the password in characters.
	 * @param flags     Flags controlling password generation characteristics.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t			GeneratePassword(BPasswordKey& password,
									size_t length, uint32 flags);

	/**
	 * @brief Evaluates the strength of a password.
	 *
	 * @param password  The password string to evaluate.
	 * @return A value between 0.0 (very weak) and 1.0 (very strong).
	 */
			float				PasswordStrength(const char* password);

private:
			status_t			_SendKeyMessage(BMessage& message,
									BMessage* reply) const;
};


#endif	// _KEY_STORE_H
