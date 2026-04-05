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
 *   Copyright 2012, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Lotz, mmlr@mlotz.ch
 */
#ifndef _KEY_STORE_DEFS_H
#define _KEY_STORE_DEFS_H


namespace BPrivate {


const char* kKeyStoreServerSignature
	= "application/x-vnd.Haiku-keystore_server";


enum {
	// Replies
	KEY_STORE_SUCCESS						= 'KRok',
	KEY_STORE_ERROR							= 'KRer',
	KEY_STORE_RESULT						= 'KRrs',

	// KeyStore requests
	KEY_STORE_GET_KEY						= 'KgtK',
	KEY_STORE_GET_NEXT_KEY					= 'KgnK',
	KEY_STORE_ADD_KEY						= 'KadK',
	KEY_STORE_REMOVE_KEY					= 'KrmK',
	KEY_STORE_ADD_KEYRING					= 'KaKR',
	KEY_STORE_REMOVE_KEYRING				= 'KrKR',
	KEY_STORE_GET_NEXT_KEYRING				= 'KnKR',
	KEY_STORE_SET_UNLOCK_KEY				= 'KsuK',
	KEY_STORE_REMOVE_UNLOCK_KEY				= 'KruK',
	KEY_STORE_ADD_KEYRING_TO_MASTER			= 'KarM',
	KEY_STORE_REMOVE_KEYRING_FROM_MASTER	= 'KrrM',
	KEY_STORE_GET_NEXT_MASTER_KEYRING		= 'KnrM',
	KEY_STORE_IS_KEYRING_UNLOCKED			= 'KuKR',
	KEY_STORE_LOCK_KEYRING					= 'KlKR',
	KEY_STORE_GET_NEXT_APPLICATION			= 'KnKA',
	KEY_STORE_REMOVE_APPLICATION			= 'KrKA',
};


}	// namespace BPrivate


#endif	// _KEY_STORE_DEFS_H
