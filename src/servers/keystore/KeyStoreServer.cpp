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
/** @file KeyStoreServer.cpp
 *  @brief Key store server application managing keyrings and access control. */
#include "KeyStoreServer.h"

#include "AppAccessRequestWindow.h"
#include "KeyRequestWindow.h"
#include "Keyring.h"

#include <KeyStoreDefs.h>

#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>

#include <new>

#include <stdio.h>


using namespace BPrivate;


/** @brief Name of the master keyring that stores keys for other keyrings. */
static const char* kMasterKeyringName = "Master";
/** @brief Identifier used when storing keyring unlock keys in the master keyring. */
static const char* kKeyringKeysIdentifier = "Keyrings";

/** @brief Version number of the on-disk key store database format. */
static const uint32 kKeyStoreFormatVersion = 1;

/** @brief Access flag: permission to retrieve keys from a keyring. */
static const uint32 kFlagGetKey						= 0x0001;
static const uint32 kFlagEnumerateKeys				= 0x0002;
static const uint32 kFlagAddKey						= 0x0004;
static const uint32 kFlagRemoveKey					= 0x0008;
static const uint32 kFlagAddKeyring					= 0x0010;
static const uint32 kFlagRemoveKeyring				= 0x0020;
static const uint32 kFlagEnumerateKeyrings			= 0x0040;
static const uint32 kFlagSetUnlockKey				= 0x0080;
static const uint32 kFlagRemoveUnlockKey			= 0x0100;
static const uint32 kFlagAddKeyringsToMaster		= 0x0200;
static const uint32 kFlagRemoveKeyringsFromMaster	= 0x0400;
static const uint32 kFlagEnumerateMasterKeyrings	= 0x0800;
static const uint32 kFlagQueryLockState				= 0x1000;
static const uint32 kFlagLockKeyring				= 0x2000;
static const uint32 kFlagEnumerateApplications		= 0x4000;
static const uint32 kFlagRemoveApplications			= 0x8000;

/** @brief Default set of access flags granted to newly authorised applications. */
static const uint32 kDefaultAppFlags = kFlagGetKey | kFlagEnumerateKeys
	| kFlagAddKey | kFlagRemoveKey | kFlagAddKeyring | kFlagRemoveKeyring
	| kFlagEnumerateKeyrings | kFlagSetUnlockKey | kFlagRemoveUnlockKey
	| kFlagAddKeyringsToMaster | kFlagRemoveKeyringsFromMaster
	| kFlagEnumerateMasterKeyrings | kFlagQueryLockState | kFlagLockKeyring
	| kFlagEnumerateApplications | kFlagRemoveApplications;


/**
 * @brief Constructs the key store server application.
 *
 * Ensures the settings directory hierarchy exists, opens or creates the
 * database file, reads existing keyrings, and creates the master keyring
 * if it does not already exist.
 */
KeyStoreServer::KeyStoreServer()
	:
	BApplication(kKeyStoreServerSignature),
	fMasterKeyring(NULL),
	fKeyrings(20)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;

	BDirectory settingsDir(path.Path());
	path.Append("system");
	if (!settingsDir.Contains(path.Path()))
		settingsDir.CreateDirectory(path.Path(), NULL);

	settingsDir.SetTo(path.Path());
	path.Append("keystore");
	if (!settingsDir.Contains(path.Path()))
		settingsDir.CreateDirectory(path.Path(), NULL);

	settingsDir.SetTo(path.Path());
	path.Append("keystore_database");

	fKeyStoreFile.SetTo(path.Path(), B_READ_WRITE
		| (settingsDir.Contains(path.Path()) ? 0 : B_CREATE_FILE));

	_ReadKeyStoreDatabase();

	if (fMasterKeyring == NULL) {
		fMasterKeyring = new(std::nothrow) Keyring(kMasterKeyringName);
		fKeyrings.BinaryInsert(fMasterKeyring, &Keyring::Compare);
	}
}


/** @brief Destroys the key store server. */
KeyStoreServer::~KeyStoreServer()
{
}


/**
 * @brief Dispatches incoming key store commands to the appropriate handler.
 *
 * Resolves the calling application, determines access permissions, looks
 * up the target keyring, unlocks it if needed, validates access, then
 * performs the requested operation (get/add/remove keys, manage keyrings,
 * query lock state, manage applications, etc.).
 *
 * @param message The incoming command message.
 */
void
KeyStoreServer::MessageReceived(BMessage* message)
{
	BMessage reply;
	status_t result = B_UNSUPPORTED;
	app_info callingAppInfo;

	uint32 accessFlags = _AccessFlagsFor(message->what);
	if (accessFlags == 0)
		message->what = 0;

	if (message->what != 0) {
		result = _ResolveCallingApp(*message, callingAppInfo);
		if (result != B_OK)
			message->what = 0;
	}

	// Resolve the keyring for the relevant messages.
	Keyring* keyring = NULL;
	switch (message->what) {
		case KEY_STORE_GET_KEY:
		case KEY_STORE_GET_NEXT_KEY:
		case KEY_STORE_ADD_KEY:
		case KEY_STORE_REMOVE_KEY:
		case KEY_STORE_IS_KEYRING_UNLOCKED:
		case KEY_STORE_LOCK_KEYRING:
		case KEY_STORE_SET_UNLOCK_KEY:
		case KEY_STORE_REMOVE_UNLOCK_KEY:
		case KEY_STORE_ADD_KEYRING_TO_MASTER:
		case KEY_STORE_REMOVE_KEYRING_FROM_MASTER:
		case KEY_STORE_GET_NEXT_APPLICATION:
		case KEY_STORE_REMOVE_APPLICATION:
		{
			BString keyringName;
			if (message->FindString("keyring", &keyringName) != B_OK)
				keyringName = "";

			keyring = _FindKeyring(keyringName);
			if (keyring == NULL) {
				result = B_BAD_VALUE;
				message->what = 0;
					// So that we don't do anything in the second switch.
				break;
			}

			switch (message->what) {
				case KEY_STORE_GET_KEY:
				case KEY_STORE_GET_NEXT_KEY:
				case KEY_STORE_ADD_KEY:
				case KEY_STORE_REMOVE_KEY:
				case KEY_STORE_SET_UNLOCK_KEY:
				case KEY_STORE_REMOVE_UNLOCK_KEY:
				case KEY_STORE_ADD_KEYRING_TO_MASTER:
				case KEY_STORE_GET_NEXT_APPLICATION:
				case KEY_STORE_REMOVE_APPLICATION:
				{
					// These need keyring access to do anything.
					while (!keyring->IsUnlocked()) {
						status_t unlockResult = _UnlockKeyring(*keyring);
						if (unlockResult != B_OK) {
							result = unlockResult;
							message->what = 0;
							break;
						}
					}

					status_t validateResult = _ValidateAppAccess(*keyring,
						callingAppInfo, accessFlags);
					if (validateResult != B_OK) {
						result = validateResult;
						message->what = 0;
						break;
					}

					break;
				}
			}

			break;
		}
	}

	switch (message->what) {
		case KEY_STORE_GET_KEY:
		{
			BString identifier;
			if (message->FindString("identifier", &identifier) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			bool secondaryIdentifierOptional;
			if (message->FindBool("secondaryIdentifierOptional",
					&secondaryIdentifierOptional) != B_OK) {
				secondaryIdentifierOptional = false;
			}

			BString secondaryIdentifier;
			if (message->FindString("secondaryIdentifier",
					&secondaryIdentifier) != B_OK) {
				secondaryIdentifier = "";
				secondaryIdentifierOptional = true;
			}

			BMessage keyMessage;
			result = keyring->FindKey(identifier, secondaryIdentifier,
				secondaryIdentifierOptional, &keyMessage);
			if (result == B_OK)
				reply.AddMessage("key", &keyMessage);

			break;
		}

		case KEY_STORE_GET_NEXT_KEY:
		{
			BKeyType type;
			BKeyPurpose purpose;
			uint32 cookie;
			if (message->FindUInt32("type", (uint32*)&type) != B_OK
				|| message->FindUInt32("purpose", (uint32*)&purpose) != B_OK
				|| message->FindUInt32("cookie", &cookie) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			BMessage keyMessage;
			result = keyring->FindKey(type, purpose, cookie, keyMessage);
			if (result == B_OK) {
				cookie++;
				reply.AddUInt32("cookie", cookie);
				reply.AddMessage("key", &keyMessage);
			}

			break;
		}

		case KEY_STORE_ADD_KEY:
		{
			BMessage keyMessage;
			BString identifier;
			if (message->FindMessage("key", &keyMessage) != B_OK
				|| keyMessage.FindString("identifier", &identifier) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			BString secondaryIdentifier;
			if (keyMessage.FindString("secondaryIdentifier",
					&secondaryIdentifier) != B_OK) {
				secondaryIdentifier = "";
			}

			result = keyring->AddKey(identifier, secondaryIdentifier, keyMessage);
			if (result == B_OK)
				_WriteKeyStoreDatabase();

			break;
		}

		case KEY_STORE_REMOVE_KEY:
		{
			BMessage keyMessage;
			BString identifier;
			if (message->FindMessage("key", &keyMessage) != B_OK
				|| keyMessage.FindString("identifier", &identifier) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			result = keyring->RemoveKey(identifier, keyMessage);
			if (result == B_OK)
				_WriteKeyStoreDatabase();

			break;
		}

		case KEY_STORE_ADD_KEYRING:
		{
			BMessage keyMessage;
			BString keyring;
			if (message->FindString("keyring", &keyring) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			result = _AddKeyring(keyring);
			if (result == B_OK)
				_WriteKeyStoreDatabase();

			break;
		}

		case KEY_STORE_REMOVE_KEYRING:
		{
			BString keyringName;
			if (message->FindString("keyring", &keyringName) != B_OK)
				keyringName = "";

			result = _RemoveKeyring(keyringName);
			if (result == B_OK)
				_WriteKeyStoreDatabase();

			break;
		}

		case KEY_STORE_GET_NEXT_KEYRING:
		{
			uint32 cookie;
			if (message->FindUInt32("cookie", &cookie) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			keyring = fKeyrings.ItemAt(cookie);
			if (keyring == NULL) {
				result = B_ENTRY_NOT_FOUND;
				break;
			}

			cookie++;
			reply.AddUInt32("cookie", cookie);
			reply.AddString("keyring", keyring->Name());
			result = B_OK;
			break;
		}

		case KEY_STORE_IS_KEYRING_UNLOCKED:
		{
			reply.AddBool("unlocked", keyring->IsUnlocked());
			result = B_OK;
			break;
		}

		case KEY_STORE_LOCK_KEYRING:
		{
			keyring->Lock();
			result = B_OK;
			break;
		}

		case KEY_STORE_SET_UNLOCK_KEY:
		{
			BMessage keyMessage;
			if (message->FindMessage("key", &keyMessage) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			result = keyring->SetUnlockKey(keyMessage);
			if (result == B_OK)
				_WriteKeyStoreDatabase();

			// TODO: Update the key in the master if this keyring was added.
			break;
		}

		case KEY_STORE_REMOVE_UNLOCK_KEY:
		{
			result = keyring->RemoveUnlockKey();
			if (result == B_OK)
				_WriteKeyStoreDatabase();

			break;
		}

		case KEY_STORE_ADD_KEYRING_TO_MASTER:
		case KEY_STORE_REMOVE_KEYRING_FROM_MASTER:
		{
			// We also need access to the master keyring.
			while (!fMasterKeyring->IsUnlocked()) {
				status_t unlockResult = _UnlockKeyring(*fMasterKeyring);
				if (unlockResult != B_OK) {
					result = unlockResult;
					message->what = 0;
					break;
				}
			}

			if (message->what == 0)
				break;

			BString secondaryIdentifier = keyring->Name();
			BMessage keyMessage = keyring->UnlockKey();
			keyMessage.RemoveName("identifier");
			keyMessage.AddString("identifier", kKeyringKeysIdentifier);
			keyMessage.RemoveName("secondaryIdentifier");
			keyMessage.AddString("secondaryIdentifier", secondaryIdentifier);

			switch (message->what) {
				case KEY_STORE_ADD_KEYRING_TO_MASTER:
					result = fMasterKeyring->AddKey(kKeyringKeysIdentifier,
						secondaryIdentifier, keyMessage);
					break;

				case KEY_STORE_REMOVE_KEYRING_FROM_MASTER:
					result = fMasterKeyring->RemoveKey(kKeyringKeysIdentifier,
						keyMessage);
					break;
			}

			if (result == B_OK)
				_WriteKeyStoreDatabase();

			break;
		}

		case KEY_STORE_GET_NEXT_APPLICATION:
		{
			uint32 cookie;
			if (message->FindUInt32("cookie", &cookie) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			BString signature;
			BString path;
			result = keyring->GetNextApplication(cookie, signature, path);
			if (result != B_OK)
				break;

			reply.AddUInt32("cookie", cookie);
			reply.AddString("signature", signature);
			reply.AddString("path", path);
			result = B_OK;
			break;
		}

		case KEY_STORE_REMOVE_APPLICATION:
		{
			const char* signature = NULL;
			const char* path = NULL;

			if (message->FindString("signature", &signature) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			if (message->FindString("path", &path) != B_OK)
				path = NULL;

			result = keyring->RemoveApplication(signature, path);
			if (result == B_OK)
				_WriteKeyStoreDatabase();

			break;
		}

		case 0:
		{
			// Just the error case from above.
			break;
		}

		default:
		{
			printf("unknown message received: %" B_PRIu32 " \"%.4s\"\n",
				message->what, (const char*)&message->what);
			break;
		}
	}

	if (message->IsSourceWaiting()) {
		if (result == B_OK)
			reply.what = KEY_STORE_SUCCESS;
		else {
			reply.what = KEY_STORE_RESULT;
			reply.AddInt32("result", result);
		}

		message->SendReply(&reply);
	}
}


/**
 * @brief Reads and deserialises the key store database from disk.
 *
 * Unflattens keyring data from the database file and populates the
 * sorted keyring list. If reading fails, reinitialises the database.
 *
 * @return B_OK on success, or an error code if the file was corrupt.
 */
status_t
KeyStoreServer::_ReadKeyStoreDatabase()
{
	BMessage keystore;
	status_t result = keystore.Unflatten(&fKeyStoreFile);
	if (result != B_OK) {
		printf("failed to read keystore database\n");
		_WriteKeyStoreDatabase();
			// Reinitializes the database.
		return result;
	}

	int32 index = 0;
	BMessage keyringData;
	while (keystore.FindMessage("keyrings", index++, &keyringData) == B_OK) {
		Keyring* keyring = new(std::nothrow) Keyring();
		if (keyring == NULL) {
			printf("no memory for allocating keyring\n");
			break;
		}

		status_t result = keyring->ReadFromMessage(keyringData);
		if (result != B_OK) {
			printf("failed to read keyring from data\n");
			delete keyring;
			continue;
		}

		if (strcmp(keyring->Name(), kMasterKeyringName) == 0)
			fMasterKeyring = keyring;

		fKeyrings.BinaryInsert(keyring, &Keyring::Compare);
	}

	return B_OK;
}


/**
 * @brief Serialises all keyrings and writes them to the database file.
 *
 * Flattens every keyring into a container message and overwrites the
 * existing database file contents.
 *
 * @return B_OK on success, or an error code on failure.
 */
status_t
KeyStoreServer::_WriteKeyStoreDatabase()
{
	BMessage keystore;
	keystore.AddUInt32("format", kKeyStoreFormatVersion);

	for (int32 i = 0; i < fKeyrings.CountItems(); i++) {
		Keyring* keyring = fKeyrings.ItemAt(i);
		if (keyring == NULL)
			continue;

		BMessage keyringData;
		status_t result = keyring->WriteToMessage(keyringData);
		if (result != B_OK)
			return result;

		keystore.AddMessage("keyrings", &keyringData);
	}

	fKeyStoreFile.SetSize(0);
	fKeyStoreFile.Seek(0, SEEK_SET);
	return keystore.Flatten(&fKeyStoreFile);
}


/**
 * @brief Maps a key store command code to its required access flag.
 *
 * @param command The key store command (e.g., KEY_STORE_GET_KEY).
 * @return The corresponding access flag, or 0 for unknown commands.
 */
uint32
KeyStoreServer::_AccessFlagsFor(uint32 command) const
{
	switch (command) {
		case KEY_STORE_GET_KEY:
			return kFlagGetKey;
		case KEY_STORE_GET_NEXT_KEY:
			return kFlagEnumerateKeys;
		case KEY_STORE_ADD_KEY:
			return kFlagAddKey;
		case KEY_STORE_REMOVE_KEY:
			return kFlagRemoveKey;
		case KEY_STORE_ADD_KEYRING:
			return kFlagAddKeyring;
		case KEY_STORE_REMOVE_KEYRING:
			return kFlagRemoveKeyring;
		case KEY_STORE_GET_NEXT_KEYRING:
			return kFlagEnumerateKeyrings;
		case KEY_STORE_SET_UNLOCK_KEY:
			return kFlagSetUnlockKey;
		case KEY_STORE_REMOVE_UNLOCK_KEY:
			return kFlagRemoveUnlockKey;
		case KEY_STORE_ADD_KEYRING_TO_MASTER:
			return kFlagAddKeyringsToMaster;
		case KEY_STORE_REMOVE_KEYRING_FROM_MASTER:
			return kFlagRemoveKeyringsFromMaster;
		case KEY_STORE_GET_NEXT_MASTER_KEYRING:
			return kFlagEnumerateMasterKeyrings;
		case KEY_STORE_IS_KEYRING_UNLOCKED:
			return kFlagQueryLockState;
		case KEY_STORE_LOCK_KEYRING:
			return kFlagLockKeyring;
		case KEY_STORE_GET_NEXT_APPLICATION:
			return kFlagEnumerateApplications;
		case KEY_STORE_REMOVE_APPLICATION:
			return kFlagRemoveApplications;
	}

	return 0;
}


/**
 * @brief Returns a human-readable description of an access flag.
 *
 * @param accessFlag The access flag to describe.
 * @return A descriptive string, or NULL for unknown flags.
 */
const char*
KeyStoreServer::_AccessStringFor(uint32 accessFlag) const
{
	switch (accessFlag) {
		case kFlagGetKey:
			return "Get keys from the keyring.";
		case kFlagEnumerateKeys:
			return "Enumerate and get keys from the keyring.";
		case kFlagAddKey:
			return "Add keys to the keyring.";
		case kFlagRemoveKey:
			return "Remove keys from the keyring.";
		case kFlagAddKeyring:
			return "Add new keyrings.";
		case kFlagRemoveKeyring:
			return "Remove keyrings.";
		case kFlagEnumerateKeyrings:
			return "Enumerate the available keyrings.";
		case kFlagSetUnlockKey:
			return "Set the unlock key of the keyring.";
		case kFlagRemoveUnlockKey:
			return "Remove the unlock key of the keyring.";
		case kFlagAddKeyringsToMaster:
			return "Add the keyring key to the master keyring.";
		case kFlagRemoveKeyringsFromMaster:
			return "Remove the keyring key from the master keyring.";
		case kFlagEnumerateMasterKeyrings:
			return "Enumerate keyrings added to the master keyring.";
		case kFlagQueryLockState:
			return "Query the lock state of the keyring.";
		case kFlagLockKeyring:
			return "Lock the keyring.";
		case kFlagEnumerateApplications:
			return "Enumerate the applications of the keyring.";
		case kFlagRemoveApplications:
			return "Remove applications from the keyring.";
	}

	return NULL;
}


/**
 * @brief Identifies the application that sent a message.
 *
 * Uses the message's return address to look up the calling team's
 * app_info from the roster.
 *
 * @param message        The incoming message.
 * @param callingAppInfo Output: the resolved app_info.
 * @return B_OK on success, or an error code if resolution failed.
 */
status_t
KeyStoreServer::_ResolveCallingApp(const BMessage& message,
	app_info& callingAppInfo) const
{
	team_id callingTeam = message.ReturnAddress().Team();
	status_t result = be_roster->GetRunningAppInfo(callingTeam,
		&callingAppInfo);
	if (result != B_OK)
		return result;

	// Do some sanity checks.
	if (callingAppInfo.team != callingTeam)
		return B_ERROR;

	return B_OK;
}


/**
 * @brief Validates whether an application has the required access to a keyring.
 *
 * Looks up the application's stored access record, verifies the checksum,
 * and checks the required flags. If access is not yet granted, prompts
 * the user via a dialog and optionally persists the grant.
 *
 * @param keyring     The target keyring.
 * @param appInfo     Info about the requesting application.
 * @param accessFlags The required access flags.
 * @return B_OK if access is granted, B_NOT_ALLOWED if denied.
 */
status_t
KeyStoreServer::_ValidateAppAccess(Keyring& keyring, const app_info& appInfo,
	uint32 accessFlags)
{
	BMessage appMessage;
	BPath path(&appInfo.ref);
	status_t result = keyring.FindApplication(appInfo.signature,
		path.Path(), appMessage);
	if (result != B_OK && result != B_ENTRY_NOT_FOUND)
		return result;

	// TODO: Implement running image checksum mechanism.
	BString checksum = path.Path();

	bool appIsNew = false;
	bool appWasUpdated = false;
	uint32 appFlags = 0;
	BString appSum = "";
	if (result == B_OK) {
		if (appMessage.FindUInt32("flags", &appFlags) != B_OK
			|| appMessage.FindString("checksum", &appSum) != B_OK) {
			appIsNew = true;
			appFlags = 0;
		} else if (appSum != checksum) {
			appWasUpdated = true;
			appFlags = 0;
		}
	} else
		appIsNew = true;

	if ((accessFlags & appFlags) == accessFlags)
		return B_OK;

	const char* accessString = _AccessStringFor(accessFlags);
	bool allowAlways = false;
	result = _RequestAppAccess(keyring.Name(), appInfo.signature, path.Path(),
		accessString, appIsNew, appWasUpdated, accessFlags, allowAlways);
	if (result != B_OK || !allowAlways)
		return result;

	appMessage.MakeEmpty();
	appMessage.AddString("path", path.Path());
	appMessage.AddUInt32("flags", appFlags | accessFlags);
	appMessage.AddString("checksum", checksum);

	keyring.RemoveApplication(appInfo.signature, path.Path());
	if (keyring.AddApplication(appInfo.signature, appMessage) == B_OK)
		_WriteKeyStoreDatabase();

	return B_OK;
}


/**
 * @brief Shows a dialog asking the user to grant application access to a keyring.
 *
 * @param keyringName   Name of the keyring.
 * @param signature     The application's MIME signature.
 * @param path          The application's filesystem path.
 * @param accessString  Description of the requested operation.
 * @param appIsNew      True if the app has never been seen before.
 * @param appWasUpdated True if the app binary has changed.
 * @param accessFlags   The requested access flags.
 * @param allowAlways   Output: true if the user chose "Allow always".
 * @return B_OK if allowed, B_NOT_ALLOWED if denied, B_NO_MEMORY on allocation failure.
 */
status_t
KeyStoreServer::_RequestAppAccess(const BString& keyringName,
	const char* signature, const char* path, const char* accessString,
	bool appIsNew, bool appWasUpdated, uint32 accessFlags, bool& allowAlways)
{
	AppAccessRequestWindow* requestWindow
		= new(std::nothrow) AppAccessRequestWindow(keyringName, signature, path,
			accessString, appIsNew, appWasUpdated);
	if (requestWindow == NULL)
		return B_NO_MEMORY;

	return requestWindow->RequestAppAccess(allowAlways);
}


/**
 * @brief Finds a keyring by name, returning the master keyring for empty names.
 *
 * @param name The keyring name to find, or empty/"Master" for the master keyring.
 * @return Pointer to the keyring, or NULL if not found.
 */
Keyring*
KeyStoreServer::_FindKeyring(const BString& name)
{
	if (name.IsEmpty() || name == kMasterKeyringName)
		return fMasterKeyring;

	return fKeyrings.BinarySearchByKey(name, &Keyring::Compare);
}


/**
 * @brief Creates a new keyring and inserts it into the sorted list.
 *
 * @param name The name for the new keyring.
 * @return B_OK on success, B_NAME_IN_USE if a keyring with that name exists.
 */
status_t
KeyStoreServer::_AddKeyring(const BString& name)
{
	if (_FindKeyring(name) != NULL)
		return B_NAME_IN_USE;

	Keyring* keyring = new(std::nothrow) Keyring(name);
	if (keyring == NULL)
		return B_NO_MEMORY;

	if (!fKeyrings.BinaryInsert(keyring, &Keyring::Compare)) {
		delete keyring;
		return B_ERROR;
	}

	return B_OK;
}


/**
 * @brief Removes a keyring by name; the master keyring cannot be removed.
 *
 * @param name The keyring name.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if absent, B_NOT_ALLOWED for master.
 */
status_t
KeyStoreServer::_RemoveKeyring(const BString& name)
{
	Keyring* keyring = _FindKeyring(name);
	if (keyring == NULL)
		return B_ENTRY_NOT_FOUND;

	if (keyring == fMasterKeyring) {
		// The master keyring can't be removed.
		return B_NOT_ALLOWED;
	}

	return fKeyrings.RemoveItem(keyring) ? B_OK : B_ERROR;
}


/**
 * @brief Attempts to unlock a keyring, using the master keyring or user input.
 *
 * First checks if the keyring has no unlock key (and can be unlocked
 * directly). Then tries to find the unlock key in the master keyring.
 * As a last resort, prompts the user for the passphrase.
 *
 * @param keyring The keyring to unlock.
 * @return B_OK on success, or an error code on failure.
 */
status_t
KeyStoreServer::_UnlockKeyring(Keyring& keyring)
{
	if (!keyring.HasUnlockKey())
		return keyring.Unlock(NULL);

	// If we are accessing a keyring that has been added to master access we
	// get the key from the master keyring and unlock with that.
	BMessage keyMessage;
	if (&keyring != fMasterKeyring && fMasterKeyring->IsUnlocked()) {
		if (fMasterKeyring->FindKey(kKeyringKeysIdentifier, keyring.Name(),
				false, &keyMessage) == B_OK) {
			// We found a key for this keyring, try to unlock with it.
			if (keyring.Unlock(&keyMessage) == B_OK)
				return B_OK;
		}
	}

	// No key, we need to request one from the user.
	status_t result = _RequestKey(keyring.Name(), keyMessage);
	if (result != B_OK)
		return result;

	return keyring.Unlock(&keyMessage);
}


/**
 * @brief Shows a dialog requesting the user to enter a keyring passphrase.
 *
 * @param keyringName The name of the keyring to display in the dialog.
 * @param keyMessage  Output: the flattened password key on success.
 * @return B_OK if a key was provided, B_CANCELED if dismissed, B_NO_MEMORY on failure.
 */
status_t
KeyStoreServer::_RequestKey(const BString& keyringName, BMessage& keyMessage)
{
	KeyRequestWindow* requestWindow = new(std::nothrow) KeyRequestWindow();
	if (requestWindow == NULL)
		return B_NO_MEMORY;

	return requestWindow->RequestKey(keyringName, keyMessage);
}


/**
 * @brief Application entry point for the key store server.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 on allocation failure.
 */
int
main(int argc, char* argv[])
{
	KeyStoreServer* app = new(std::nothrow) KeyStoreServer();
	if (app == NULL)
		return 1;

	app->Run();
	delete app;
	return 0;
}
