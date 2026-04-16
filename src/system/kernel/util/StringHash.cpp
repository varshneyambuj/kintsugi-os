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
 *   Copyright 2002-2013, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file StringHash.cpp
 * @brief djb2 string-hash helpers used across the kernel.
 *
 * Both functions implement Dan Bernstein's djb2 hash (h = h*33 + c, seed 5381).
 * The _part variant caps the number of bytes hashed to support non-terminated
 * strings.
 */


#include <util/StringHash.h>


/**
 * @brief Hash a NUL-terminated string using djb2.
 * @param _string NUL-terminated input string.
 * @return 32-bit hash value.
 */
uint32
hash_hash_string(const char* _string)
{
	const uint8* string = (const uint8*)_string;

	uint32 h = 5381;
	char c;
	while ((c = *string++) != 0)
		h = (h * 33) + c;
	return h;
}


/**
 * @brief Hash at most @a maxLength bytes of a string using djb2.
 *
 * Stops at the first NUL or after @a maxLength bytes, whichever comes first.
 *
 * @param _string   Input bytes (need not be NUL-terminated).
 * @param maxLength Upper bound on bytes to hash.
 * @return 32-bit hash value.
 */
uint32
hash_hash_string_part(const char* _string, size_t maxLength)
{
	const uint8* string = (const uint8*)_string;

	uint32 h = 5381;
	char c;
	while (maxLength-- > 0 && (c = *string++) != 0)
		h = (h * 33) + c;
	return h;
}
