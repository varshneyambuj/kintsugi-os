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
 *   Copyright 2008 Mika Lindqvist, monni1995_at_gmail.com
 *   All rights reserved. Distributed under the terms of the MIT License.
 */

/** @file LinkKeyUtils.h
 *  @brief Utility class for comparing, formatting, and parsing Bluetooth link keys. */

#ifndef _LINKKEY_UTILS_H
#define _LINKKEY_UTILS_H

#include <stdio.h>

#include <bluetooth/bluetooth.h>


namespace Bluetooth {

/** @brief Static utility methods for manipulating 128-bit Bluetooth link keys (linkkey_t). */
class LinkKeyUtils {
public:
	/** @brief Compares two link keys for equality.
	 *  @param lk1 Pointer to the first link key.
	 *  @param lk2 Pointer to the second link key.
	 *  @return true if the keys are identical, false otherwise. */
	static bool Compare(linkkey_t* lk1, linkkey_t* lk2)
	{
		return memcmp(lk1, lk2, sizeof(linkkey_t)) == 0;
	}

	/** @brief Returns an all-zero (null) link key.
	 *  @return A linkkey_t with all 16 bytes set to zero. */
	static linkkey_t NullKey()
	{
		return (linkkey_t){{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
	}

	/** @brief Formats a link key as a human-readable colon-separated hex string.
	 *  @param lk The link key to format.
	 *  @return A BString in "XX:XX:...:XX" (16-byte) format. */
	static BString ToString(const linkkey_t lk)
	{
		BString str;

		str.SetToFormat("%2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X:"
				"%2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X",
				lk.l[0], lk.l[1], lk.l[2], lk.l[3], lk.l[4], lk.l[5],
				lk.l[6], lk.l[7], lk.l[8], lk.l[9], lk.l[10], lk.l[11],
				lk.l[12], lk.l[13], lk.l[14], lk.l[15]);

		return str;
	}

	/** @brief Parses a link key from a colon-separated hex string.
	 *  @param lkstr Null-terminated string in "XX:XX:...:XX" (16-byte) format.
	 *  @return The parsed linkkey_t, or NullKey() if parsing fails or lkstr is NULL. */
	static linkkey_t FromString(const char *lkstr)
	{
		if (lkstr != NULL) {
			uint8 l0, l1, l2, l3, l4, l5, l6, l7, l8, l9, l10, l11, l12, l13, l14, l15;
			size_t count = sscanf(lkstr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx:%hhx:%hhx:"
							"%hhx:%hhx:%hhx:%hhx:%hhx:%hhx:%hhx:%hhxs", &l0, &l1, &l2, &l3,
							&l4, &l5, &l6, &l7, &l8, &l9, &l10, &l11, &l12, &l13,
							&l14, &l15);

			if (count == 16) {
				return (linkkey_t){{l0, l1, l2, l3, l4, l5, l6, l7, l8,
					l9, l10, l11, l12, l13, l14, l15}};
			}
		}

		return NullKey();
	}
};

}

#ifndef _BT_USE_EXPLICIT_NAMESPACE
using Bluetooth::LinkKeyUtils;
#endif

#endif // _LINKKEY_UTILS_H
