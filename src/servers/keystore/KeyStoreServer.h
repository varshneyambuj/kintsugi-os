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
/** @file KeyStoreServer.h
 *  @brief BApplication-based server for the system key store. */
#ifndef _KEY_STORE_SERVER_H
#define _KEY_STORE_SERVER_H


#include <Application.h>
#include <File.h>
#include <Key.h>
#include <ObjectList.h>


struct app_info;
class Keyring;

typedef BObjectList<Keyring, true> KeyringList;


/** @brief Application server managing keyrings and access policies. */
class KeyStoreServer : public BApplication {
public:
									/** @brief Construct the key store server application. */
									KeyStoreServer();
/** @brief Destructor. */
virtual								~KeyStoreServer();

/** @brief Dispatch key store operation messages. */
virtual	void						MessageReceived(BMessage* message);

private:
		status_t					_ReadKeyStoreDatabase();
		status_t					_WriteKeyStoreDatabase();

		uint32						_AccessFlagsFor(uint32 command) const;
		const char*					_AccessStringFor(uint32 accessFlag) const;
		status_t					_ResolveCallingApp(const BMessage& message,
										app_info& callingAppInfo) const;

		status_t					_ValidateAppAccess(Keyring& keyring,
										const app_info& appInfo,
										uint32 accessFlags);
		status_t					_RequestAppAccess(
										const BString& keyringName,
										const char* signature,
										const char* path,
										const char* accessString, bool appIsNew,
										bool appWasUpdated, uint32 accessFlags,
										bool& allowAlways);

		Keyring*					_FindKeyring(const BString& name);

		status_t					_AddKeyring(const BString& name);
		status_t					_RemoveKeyring(const BString& name);

		status_t					_UnlockKeyring(Keyring& keyring);

		status_t					_RequestKey(const BString& keyringName,
										BMessage& keyMessage);

		Keyring*					fMasterKeyring; /**< The master keyring instance */
		KeyringList					fKeyrings; /**< Sorted list of all keyrings */
		BFile						fKeyStoreFile; /**< Persistent database file */
};


#endif // _KEY_STORE_SERVER_H
