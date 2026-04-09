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
 *   Copyright 2007 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 *   Copyright 2008 Mika Lindqvist, monni1995_at_gmail.com
 *   All rights reserved. Distributed under the terms of the MIT License.
 */

/** @file bdaddrUtils.h
 *  @brief Utility class for creating, comparing, copying, and formatting Bluetooth device addresses. */

#ifndef _BDADDR_UTILS_H
#define _BDADDR_UTILS_H

#include <stdio.h>
#include <string.h>

#include <String.h>

#include <bluetooth/bluetooth.h>

namespace Bluetooth {

/** @brief Static utility methods for manipulating Bluetooth device addresses (bdaddr_t). */
class bdaddrUtils {

public:
	/** @brief Returns the null (all-zero) Bluetooth device address.
	 *  @return A bdaddr_t with all bytes set to zero. */
	static inline bdaddr_t NullAddress()
	{
		return ((bdaddr_t) {{0, 0, 0, 0, 0, 0}});
	}


	/** @brief Returns the local loopback Bluetooth device address.
	 *  @return A bdaddr_t representing the local device. */
	static inline bdaddr_t LocalAddress()
	{
		return ((bdaddr_t) {{0, 0, 0, 0xff, 0xff, 0xff}});
	}


	/** @brief Returns the broadcast Bluetooth device address (all bits set).
	 *  @return A bdaddr_t with all bytes set to 0xFF. */
	static inline bdaddr_t BroadcastAddress()
	{
		return ((bdaddr_t) {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}});
	}


	/** @brief Compares two Bluetooth device addresses for equality.
	 *  @param ba1 First address to compare.
	 *  @param ba2 Second address to compare.
	 *  @return true if both addresses are identical, false otherwise. */
	static bool Compare(const bdaddr_t& ba1, const bdaddr_t& ba2)
	{
		return (memcmp(&ba1, &ba2, sizeof(bdaddr_t)) == 0);
	}


	/** @brief Copies a Bluetooth device address from src into dst.
	 *  @param dst Destination address to be overwritten.
	 *  @param src Source address to copy from. */
	static void Copy(bdaddr_t& dst, const bdaddr_t& src)
	{
		memcpy(&dst, &src, sizeof(bdaddr_t));
	}

	/** @brief Formats a Bluetooth device address as a human-readable string (XX:XX:XX:XX:XX:XX).
	 *  @param bdaddr The address to format.
	 *  @return A BString containing the colon-separated hex representation. */
	static BString ToString(const bdaddr_t bdaddr)
	{
		BString str;

		str.SetToFormat("%2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X",bdaddr.b[5],
				bdaddr.b[4], bdaddr.b[3], bdaddr.b[2], bdaddr.b[1],
				bdaddr.b[0]);
		return str;
	}


	/** @brief Parses a Bluetooth device address from a colon-separated hex string.
	 *  @param addr Null-terminated string in "XX:XX:XX:XX:XX:XX" format.
	 *  @return The parsed bdaddr_t, or NullAddress() if parsing fails or addr is NULL. */
	static bdaddr_t FromString(const char * addr)
	{
		uint8 b0, b1, b2, b3, b4, b5;

		if (addr != NULL) {
			size_t count = sscanf(addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
						&b5, &b4, &b3, &b2, &b1, &b0);

			if (count == 6)
				return ((bdaddr_t) {{b0, b1, b2, b3, b4, b5}});
		}

		return NullAddress();
	}

};

}


#ifndef _BT_USE_EXPLICIT_NAMESPACE
using Bluetooth::bdaddrUtils;
#endif


#endif // _BDADDR_UTILS_H
