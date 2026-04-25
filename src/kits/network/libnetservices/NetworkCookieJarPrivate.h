/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2010, Haiku Inc.
 */

/** @file NetworkCookieJarPrivate.h
    @brief Private definitions backing BNetworkCookieJar's hash-map storage and
           opaque iterator handle. */

#ifndef _B_NETWORK_COOKIE_JAR_PRIVATE_H_
#define _B_NETWORK_COOKIE_JAR_PRIVATE_H_


#include <HashMap.h>

using BPrivate::Network::BNetworkCookie;
using BPrivate::Network::BNetworkCookieJar;
using BPrivate::Network::BNetworkCookieList;


/** @brief Hash map keyed by domain string mapping to per-domain cookie lists. */
typedef BPrivate::SynchronizedHashMap<HashString, BNetworkCookieList*>
	BNetworkCookieHashMap;

/** @brief Opaque storage handle holding the cookie jar's hash map. */
struct BNetworkCookieJar::PrivateHashMap : public BNetworkCookieHashMap {
};

/** @brief Opaque iterator state retained by BNetworkCookieJar::Iterator
           between calls to Next(). */
struct BNetworkCookieJar::PrivateIterator {
	/** @brief Wrap an underlying hash-map iterator @a it. */
								PrivateIterator(
									BNetworkCookieHashMap::Iterator it)
									:
									fCookieMapIterator(it)
								{
								}

	HashString					fKey;
	BNetworkCookieHashMap::Iterator
								fCookieMapIterator;
};

#endif // _B_NETWORK_COOKIE_JAR_PRIVATE_H_
