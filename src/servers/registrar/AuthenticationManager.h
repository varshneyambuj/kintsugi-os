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
   Copyright 2008, Ingo Weinhold, ingo_weinhold@gmx.de. All Rights Reserved.
   Distributed under the terms of the MIT License.
 */

/** @file AuthenticationManager.h
 *  @brief Handles user and group authentication by maintaining passwd, group, and shadow databases. */
#ifndef AUTHENTICATION_MANAGER_H
#define AUTHENTICATION_MANAGER_H


#include <OS.h>


namespace BPrivate {
	class KMessage;
}


/** @brief Provides user and group database lookups and handles authentication requests. */
class AuthenticationManager {
public:
								AuthenticationManager();
								~AuthenticationManager();

			/** @brief Initializes the authentication databases and starts the request handler thread. */
			status_t			Init();

private:
	class FlatStore;
	class User;
	class Group;
	class UserDB;
	class GroupDB;

	static	status_t			_RequestThreadEntry(void* data);
			status_t			_RequestThread();

			status_t			_InitPasswdDB();
			status_t			_InitGroupDB();
			status_t			_InitShadowPwdDB();

			void				_InvalidatePasswdDBReply();
			void				_InvalidateGroupDBReply();
			void				_InvalidateShadowPwdDBReply();

private:
			port_id				fRequestPort;
			thread_id			fRequestThread;
			UserDB*				fUserDB;
			GroupDB*			fGroupDB;
			BPrivate::KMessage*	fPasswdDBReply;
			BPrivate::KMessage*	fGroupDBReply;
			BPrivate::KMessage*	fShadowPwdDBReply;
};


#endif	// AUTHENTICATION_MANAGER_H
