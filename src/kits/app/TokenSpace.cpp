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
 *   Copyright 2001-2011 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Erik Jaesler (erik@cgsoftware.com)
 *       Axel Dörfler, axeld@pinc-software.de
 */

/** @file TokenSpace.cpp
 *  @brief BPrivate::BTokenSpace implementation for token allocation and management.
 *
 *  Provides a thread-safe mapping from integer tokens to typed objects (typically
 *  BHandler instances). Tokens are allocated sequentially with wraparound, and
 *  each token can optionally have an associated BDirectMessageTarget for direct
 *  message delivery.
 */

#include <DirectMessageTarget.h>
#include <TokenSpace.h>

#include <PthreadMutexLocker.h>


namespace BPrivate {

/** @brief The default global token space used by all BHandler instances. */
BTokenSpace gDefaultTokens;
	// the default token space - all handlers will go into that one


/** @brief Computes the next token value with wraparound from INT32_MAX to 1.
 *  @param token The current token value.
 *  @return The next sequential token, wrapping around to 1 after INT32_MAX.
 */
static int32
get_next_token(int32 token)
{
	if (token == INT32_MAX)
		return 1;
	else
		return token + 1;
}


/** @brief Constructs a BTokenSpace with an initial token value of 1. */
BTokenSpace::BTokenSpace()
	:
	fNextToken(1)
{
	fLock = (pthread_mutex_t)PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
}


/** @brief Destroys the token space and its mutex. */
BTokenSpace::~BTokenSpace()
{
	pthread_mutex_destroy(&fLock);
}


/** @brief Allocates a new unique token for the given object.
 *
 *  Finds the next available token ID and inserts the object into the map.
 *  Token IDs start at 1 and wrap around at INT32_MAX. Returns -1 if the
 *  token space is exhausted or memory allocation fails.
 *
 *  @param type The type identifier for the token (e.g., B_HANDLER_TOKEN).
 *  @param object The object pointer to associate with the token.
 *  @return The newly allocated token ID, or -1 on failure.
 */
int32
BTokenSpace::NewToken(int16 type, void* object)
{
	PthreadMutexLocker locker(&fLock);

	token_info tokenInfo = { type, object, NULL };

	int32 wraparoundToken = fNextToken;

	try {
		for (;;) {
			int32 token = fNextToken;
			bool done = fTokenMap.insert(std::make_pair(token, tokenInfo)).second;
			fNextToken = get_next_token(token);

			if (done)
				return token;

			if (fNextToken == wraparoundToken)
				return -1;
		}
	} catch (std::bad_alloc& exception) {
		return -1;
	}
}


/*!
	Inserts the specified token into the token space. If that token
	already exists, it will be overwritten.
	Don't mix NewToken() and this method unless you know what you're
	doing.
*/
bool
BTokenSpace::SetToken(int32 token, int16 type, void* object)
{
	PthreadMutexLocker locker(&fLock);

	token_info tokenInfo = { type, object, NULL };

	try {
		fTokenMap[token] = tokenInfo;
	} catch (std::bad_alloc& exception) {
		return false;
	}

	// this makes sure SetToken() plays more or less nice with NewToken()
	if (token >= fNextToken)
		fNextToken = get_next_token(token);

	return true;
}


/** @brief Removes a token from the token space.
 *  @param token The token ID to remove.
 *  @return true if the token was found and removed, false otherwise.
 */
bool
BTokenSpace::RemoveToken(int32 token)
{
	PthreadMutexLocker locker(&fLock);

	TokenMap::iterator iterator = fTokenMap.find(token);
	if (iterator == fTokenMap.end())
		return false;

	fTokenMap.erase(iterator);
	return true;
}


/*!	Checks whether or not the \a token exists with the specified
	\a type in the token space or not.
*/
bool
BTokenSpace::CheckToken(int32 token, int16 type) const
{
	PthreadMutexLocker locker(&fLock);

	TokenMap::const_iterator iterator = fTokenMap.find(token);
	if (iterator != fTokenMap.end() && iterator->second.type == type)
		return true;

	return false;
}


/** @brief Retrieves the object associated with a token.
 *  @param token The token ID to look up.
 *  @param type The expected type of the token.
 *  @param _object Output: receives the object pointer associated with the token.
 *  @return B_OK if found and type matches, B_ENTRY_NOT_FOUND otherwise.
 */
status_t
BTokenSpace::GetToken(int32 token, int16 type, void** _object) const
{
	if (token < 1)
		return B_ENTRY_NOT_FOUND;

	PthreadMutexLocker locker(&fLock);

	TokenMap::const_iterator iterator = fTokenMap.find(token);
	if (iterator == fTokenMap.end() || iterator->second.type != type)
		return B_ENTRY_NOT_FOUND;

	*_object = iterator->second.object;
	return B_OK;
}


/** @brief Sets the direct message target for a handler token.
 *
 *  Replaces the existing target (releasing its reference) with the new one
 *  (acquiring a reference). Only valid for tokens of type B_HANDLER_TOKEN.
 *
 *  @param token The handler token ID.
 *  @param target The new BDirectMessageTarget, or NULL to clear.
 *  @return B_OK on success, B_ENTRY_NOT_FOUND if the token is invalid.
 */
status_t
BTokenSpace::SetHandlerTarget(int32 token, BDirectMessageTarget* target)
{
	if (token < 1)
		return B_ENTRY_NOT_FOUND;

	PthreadMutexLocker locker(&fLock);

	TokenMap::iterator iterator = fTokenMap.find(token);
	if (iterator == fTokenMap.end() || iterator->second.type != B_HANDLER_TOKEN)
		return B_ENTRY_NOT_FOUND;

	if (iterator->second.target != NULL)
		iterator->second.target->Release();

	iterator->second.target = target;
	if (target != NULL)
		target->Acquire();

	return B_OK;
}


/** @brief Acquires a reference to the direct message target for a handler token.
 *
 *  Looks up the token, acquires an additional reference on its target (if set),
 *  and returns the target pointer. Only valid for B_HANDLER_TOKEN tokens.
 *
 *  @param token The handler token ID.
 *  @param _target Output: receives the BDirectMessageTarget pointer (may be NULL).
 *  @return B_OK on success, B_ENTRY_NOT_FOUND if the token is invalid.
 */
status_t
BTokenSpace::AcquireHandlerTarget(int32 token, BDirectMessageTarget** _target)
{
	if (token < 1)
		return B_ENTRY_NOT_FOUND;

	PthreadMutexLocker locker(&fLock);

	TokenMap::const_iterator iterator = fTokenMap.find(token);
	if (iterator == fTokenMap.end() || iterator->second.type != B_HANDLER_TOKEN)
		return B_ENTRY_NOT_FOUND;

	if (iterator->second.target != NULL)
		iterator->second.target->Acquire();

	*_target = iterator->second.target;
	return B_OK;
}


}	// namespace BPrivate
