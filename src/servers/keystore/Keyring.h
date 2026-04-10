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
/** @file Keyring.h
 *  @brief Credential keyring that stores keys with encryption support. */
#ifndef _KEYRING_H
#define _KEYRING_H


#include <Key.h>
#include <Message.h>


/** @brief Named credential store with optional passphrase encryption. */
class Keyring {
public:
									/** @brief Default constructor for an unnamed keyring. */
									Keyring();
									/** @brief Construct a keyring with the given name. */
									Keyring(const char* name);
									/** @brief Destructor. */
									~Keyring();

		/** @brief Return the keyring name. */
		const char*					Name() const { return fName; }
		/** @brief Deserialize the keyring from a BMessage. */
		status_t					ReadFromMessage(const BMessage& message);
		/** @brief Serialize the keyring into a BMessage. */
		status_t					WriteToMessage(BMessage& message);

		/** @brief Unlock the keyring with an optional key message. */
		status_t					Unlock(const BMessage* keyMessage);
		/** @brief Lock the keyring and clear in-memory data. */
		void						Lock();
		/** @brief Return true if the keyring is currently unlocked. */
		bool						IsUnlocked() const;

		/** @brief Return true if a passphrase is set. */
		bool						HasUnlockKey() const;
		/** @brief Return the current unlock key message. */
		const BMessage&				UnlockKey() const;

		/** @brief Set a new unlock passphrase for the keyring. */
		status_t					SetUnlockKey(const BMessage& keyMessage);
		/** @brief Remove the passphrase so the keyring is unprotected. */
		status_t					RemoveUnlockKey();

		/** @brief Enumerate registered applications by cookie. */
		status_t					GetNextApplication(uint32& cookie,
										BString& signature, BString& path);
		/** @brief Look up an application by signature and path. */
		status_t					FindApplication(const char* signature,
										const char* path, BMessage& appMessage);
		/** @brief Register an application for keyring access. */
		status_t					AddApplication(const char* signature,
										const BMessage& appMessage);
		/** @brief Revoke an application access entry. */
		status_t					RemoveApplication(const char* signature,
										const char* path);

		/** @brief Find a key by primary and secondary identifier. */
		status_t					FindKey(const BString& identifier,
										const BString& secondaryIdentifier,
										bool secondaryIdentifierOptional,
										BMessage* _foundKeyMessage) const;
		/** @brief Find a key by type, purpose, and index. */
		status_t					FindKey(BKeyType type, BKeyPurpose purpose,
										uint32 index,
										BMessage& _foundKeyMessage) const;

		/** @brief Store a new key in the keyring. */
		status_t					AddKey(const BString& identifier,
										const BString& secondaryIdentifier,
										const BMessage& keyMessage);
		/** @brief Remove a key from the keyring. */
		status_t					RemoveKey(const BString& identifier,
										const BMessage& keyMessage);

/** @brief Compare two keyrings by name for sorting. */
static	int							Compare(const Keyring* one,
										const Keyring* two);
/** @brief Compare a name against a keyring for binary search. */
static	int							Compare(const BString* name,
										const Keyring* keyring);

private:
		status_t					_EncryptToFlatBuffer();
		status_t					_DecryptFromFlatBuffer();

		BString						fName; /**< Keyring display name */
		BMallocIO					fFlatBuffer; /**< Encrypted flat data buffer */
		BMessage					fData; /**< Decrypted key data message */
		BMessage					fApplications; /**< Registered application access entries */
		BMessage					fUnlockKey; /**< Passphrase key message */
		bool						fHasUnlockKey; /**< True if a passphrase is required */
		bool						fUnlocked; /**< True if currently unlocked */
		bool						fModified; /**< True if data changed since last encrypt */
};


#endif // _KEYRING_H
