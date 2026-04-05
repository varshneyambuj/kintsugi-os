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
#ifndef _KEY_H
#define _KEY_H

/**
 * @file Key.h
 * @brief Defines the BKey and BPasswordKey classes for credential and key storage.
 */

#include <DataIO.h>
#include <Message.h>
#include <ObjectList.h>
#include <String.h>


/**
 * @enum BKeyPurpose
 * @brief Describes the intended purpose of a key.
 *
 * Used to categorize keys by their usage context within the key store.
 */
enum BKeyPurpose {
	B_KEY_PURPOSE_ANY,		/**< Matches any key purpose (wildcard). */
	B_KEY_PURPOSE_GENERIC,	/**< General-purpose key. */
	B_KEY_PURPOSE_KEYRING,	/**< Key used to unlock a keyring. */
	B_KEY_PURPOSE_WEB,		/**< Key for web/HTTP authentication. */
	B_KEY_PURPOSE_NETWORK,	/**< Key for network service authentication. */
	B_KEY_PURPOSE_VOLUME	/**< Key for encrypted volume access. */
};


/**
 * @enum BKeyType
 * @brief Describes the type of data a key holds.
 *
 * Used to distinguish between different key storage formats.
 */
enum BKeyType {
	B_KEY_TYPE_ANY,			/**< Matches any key type (wildcard). */
	B_KEY_TYPE_GENERIC,		/**< Generic binary key data. */
	B_KEY_TYPE_PASSWORD,	/**< Password stored as a string. */
	B_KEY_TYPE_CERTIFICATE	/**< Certificate key data. */
};


/**
 * @brief Represents a generic key for credential storage.
 *
 * BKey holds an identifier, an optional secondary identifier, a purpose,
 * and arbitrary binary data. It can be stored and retrieved from a BKeyStore.
 * Keys can be flattened to and unflattened from BMessage objects for
 * serialization.
 *
 * @see BKeyStore
 * @see BPasswordKey
 * @see BKeyPurpose
 * @see BKeyType
 */
class BKey {
public:
	/**
	 * @brief Default constructor.
	 *
	 * Creates an empty key with no purpose, identifier, or data.
	 */
								BKey();

	/**
	 * @brief Constructs a key with the specified attributes.
	 *
	 * @param purpose              The intended purpose of the key.
	 * @param identifier           The primary identifier (e.g., a URL or service name).
	 * @param secondaryIdentifier  An optional secondary identifier (e.g., a username).
	 * @param data                 Optional raw key data.
	 * @param length               The length of the raw key data in bytes.
	 */
								BKey(BKeyPurpose purpose,
									const char* identifier,
									const char* secondaryIdentifier = NULL,
									const uint8* data = NULL,
									size_t length = 0);

	/**
	 * @brief Copy constructor.
	 *
	 * @param other  The BKey to copy.
	 */
								BKey(BKey& other);

	/**
	 * @brief Destructor.
	 */
	virtual						~BKey();

	/**
	 * @brief Returns the type of this key.
	 *
	 * @return B_KEY_TYPE_GENERIC for BKey.
	 */
	virtual	BKeyType			Type() const { return B_KEY_TYPE_GENERIC; };

	/**
	 * @brief Resets the key to its default uninitialized state.
	 *
	 * Clears the purpose, identifiers, and data.
	 */
			void				Unset();

	/**
	 * @brief Reinitializes the key with the specified attributes.
	 *
	 * @param purpose              The intended purpose of the key.
	 * @param identifier           The primary identifier.
	 * @param secondaryIdentifier  An optional secondary identifier.
	 * @param data                 Optional raw key data.
	 * @param length               The length of the raw key data in bytes.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t			SetTo(BKeyPurpose purpose,
									const char* identifier,
									const char* secondaryIdentifier = NULL,
									const uint8* data = NULL,
									size_t length = 0);

	/**
	 * @brief Sets the purpose of the key.
	 *
	 * @param purpose  The new key purpose.
	 */
			void				SetPurpose(BKeyPurpose purpose);

	/**
	 * @brief Returns the purpose of the key.
	 *
	 * @return The BKeyPurpose value.
	 */
			BKeyPurpose			Purpose() const;

	/**
	 * @brief Sets the primary identifier for the key.
	 *
	 * @param identifier  The primary identifier string.
	 */
			void				SetIdentifier(const char* identifier);

	/**
	 * @brief Returns the primary identifier of the key.
	 *
	 * @return The identifier string, or NULL if not set.
	 */
			const char*			Identifier() const;

	/**
	 * @brief Sets the secondary identifier for the key.
	 *
	 * @param identifier  The secondary identifier string (e.g., username).
	 */
			void				SetSecondaryIdentifier(const char* identifier);

	/**
	 * @brief Returns the secondary identifier of the key.
	 *
	 * @return The secondary identifier string, or NULL if not set.
	 */
			const char*			SecondaryIdentifier() const;

	/**
	 * @brief Sets the raw data for the key.
	 *
	 * @param data    Pointer to the data to store.
	 * @param length  The number of bytes to store.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t			SetData(const uint8* data, size_t length);

	/**
	 * @brief Returns the length of the raw key data.
	 *
	 * @return The data length in bytes.
	 */
			size_t				DataLength() const;

	/**
	 * @brief Returns a pointer to the raw key data.
	 *
	 * @return A pointer to the key data, or NULL if no data is set.
	 */
			const uint8*		Data() const;

	/**
	 * @brief Copies the raw key data into a caller-provided buffer.
	 *
	 * @param buffer      The buffer to copy data into.
	 * @param bufferSize  The size of the buffer in bytes.
	 * @return B_OK on success, or B_BUFFER_OVERFLOW if the buffer is too small.
	 */
			status_t			GetData(uint8* buffer, size_t bufferSize) const;

	/**
	 * @brief Returns the owner of the key.
	 *
	 * The owner is typically the application signature that created the key.
	 *
	 * @return The owner string, or NULL if not set.
	 */
			const char*			Owner() const;

	/**
	 * @brief Returns the creation time of the key.
	 *
	 * @return The creation time as a bigtime_t value (microseconds since boot).
	 */
			bigtime_t			CreationTime() const;

	/**
	 * @brief Serializes the key into a BMessage.
	 *
	 * @param message  The BMessage to flatten the key into.
	 * @return B_OK on success, or an error code on failure.
	 */
	virtual	status_t			Flatten(BMessage& message) const;

	/**
	 * @brief Restores the key from a BMessage.
	 *
	 * @param message  The BMessage to unflatten the key from.
	 * @return B_OK on success, or an error code on failure.
	 */
	virtual	status_t			Unflatten(const BMessage& message);

	/**
	 * @brief Assignment operator.
	 *
	 * @param other  The BKey to assign from.
	 * @return A reference to this BKey.
	 */
			BKey&				operator=(const BKey& other);

	/**
	 * @brief Equality operator.
	 *
	 * @param other  The BKey to compare with.
	 * @return true if the keys are equal, false otherwise.
	 */
			bool				operator==(const BKey& other) const;

	/**
	 * @brief Inequality operator.
	 *
	 * @param other  The BKey to compare with.
	 * @return true if the keys are not equal, false otherwise.
	 */
			bool				operator!=(const BKey& other) const;

	/**
	 * @brief Prints the key's information to standard output.
	 *
	 * Useful for debugging. Outputs purpose, identifiers, owner, and data length.
	 */
	virtual	void				PrintToStream();

private:
			friend class BKeyStore;

			BKeyPurpose			fPurpose;
			BString				fIdentifier;
			BString				fSecondaryIdentifier;
			BString				fOwner;
			bigtime_t			fCreationTime;
	mutable	BMallocIO			fData;
};


/**
 * @brief A specialized key that stores a password string.
 *
 * BPasswordKey extends BKey to provide convenience methods for storing and
 * retrieving password strings. The password is stored as the key's raw data.
 *
 * @see BKey
 * @see BKeyStore
 */
class BPasswordKey : public BKey {
public:
	/**
	 * @brief Default constructor.
	 *
	 * Creates an empty password key.
	 */
								BPasswordKey();

	/**
	 * @brief Constructs a password key with the specified attributes.
	 *
	 * @param password             The password string to store.
	 * @param purpose              The intended purpose of the key.
	 * @param identifier           The primary identifier (e.g., a URL or service name).
	 * @param secondaryIdentifier  An optional secondary identifier (e.g., username).
	 */
								BPasswordKey(const char* password,
									BKeyPurpose purpose, const char* identifier,
									const char* secondaryIdentifier = NULL);

	/**
	 * @brief Copy constructor.
	 *
	 * @param other  The BPasswordKey to copy.
	 */
								BPasswordKey(BPasswordKey& other);

	/**
	 * @brief Destructor.
	 */
	virtual						~BPasswordKey();

	/**
	 * @brief Returns the type of this key.
	 *
	 * @return B_KEY_TYPE_PASSWORD.
	 */
	virtual	BKeyType			Type() const { return B_KEY_TYPE_PASSWORD; };

	/**
	 * @brief Reinitializes the password key with the specified attributes.
	 *
	 * @param password             The password string to store.
	 * @param purpose              The intended purpose of the key.
	 * @param identifier           The primary identifier.
	 * @param secondaryIdentifier  An optional secondary identifier.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t			SetTo(const char* password,
									BKeyPurpose purpose,
									const char* identifier,
									const char* secondaryIdentifier = NULL);

	/**
	 * @brief Sets the password string.
	 *
	 * @param password  The new password string.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t			SetPassword(const char* password);

	/**
	 * @brief Returns the stored password string.
	 *
	 * @return The password as a null-terminated string, or NULL if not set.
	 */
			const char*			Password() const;

	/**
	 * @brief Prints the password key's information to standard output.
	 *
	 * Useful for debugging. The password value itself is not printed.
	 */
	virtual	void				PrintToStream();
};

#endif	// _KEY_H
