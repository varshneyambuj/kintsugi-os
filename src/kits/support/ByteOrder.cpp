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
 *   Copyright 2004-2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ByteOrder.cpp
 * @brief Runtime byte-order swapping utilities for typed data buffers.
 *
 * Provides swap_data() for in-place endianness conversion of typed data
 * arrays and is_type_swapped() for querying whether a given type_code is
 * stored in the host's native byte order.
 *
 * @see swap_data(), is_type_swapped(), swap_action
 */


#include <ByteOrder.h>
#include <Messenger.h>
#include <MessengerPrivate.h>


/**
 * @brief Swap the byte order of a typed data buffer in place.
 *
 * Iterates over the elements in \a _data according to the element size
 * implied by \a type and applies the requested byte-swap \a action. The
 * operation is a no-op when \a action would leave the data unchanged on
 * the current host (e.g. B_SWAP_HOST_TO_LENDIAN on a little-endian host).
 *
 * Supported types and their swap widths:
 * - 16-bit: B_INT16_TYPE, B_UINT16_TYPE
 * - 32-bit: B_FLOAT_TYPE, B_INT32_TYPE, B_UINT32_TYPE, B_TIME_TYPE,
 *           B_RECT_TYPE, B_POINT_TYPE; also B_SIZE_T_TYPE, B_SSIZE_T_TYPE,
 *           B_POINTER_TYPE on 32-bit builds
 * - 64-bit: B_DOUBLE_TYPE, B_INT64_TYPE, B_UINT64_TYPE, B_OFF_T_TYPE;
 *           also B_SIZE_T_TYPE, B_SSIZE_T_TYPE, B_POINTER_TYPE on 64-bit
 *           builds
 * - Special: B_MESSENGER_TYPE (swaps team, port, and token fields)
 *
 * @param type   type_code identifying the element type of the buffer.
 * @param _data  Pointer to the start of the data buffer. Must not be NULL
 *               unless \a length is 0.
 * @param length Total byte length of the buffer. Must be a multiple of the
 *               element size for the given \a type.
 * @param action One of the swap_action constants (e.g. B_SWAP_HOST_TO_BENDIAN).
 * @return B_OK on success; B_OK (no-op) if the swap direction matches the
 *         host endianness; B_OK if \a length is 0; B_BAD_VALUE if
 *         \a _data is NULL or \a type is not swappable/recognised.
 * @see is_type_swapped(), swap_action
 */
status_t
swap_data(type_code type, void *_data, size_t length, swap_action action)
{
	// is there anything to do?
#if B_HOST_IS_LENDIAN
	if (action == B_SWAP_HOST_TO_LENDIAN || action == B_SWAP_LENDIAN_TO_HOST)
		return B_OK;
#else
	if (action == B_SWAP_HOST_TO_BENDIAN || action == B_SWAP_BENDIAN_TO_HOST)
		return B_OK;
#endif

	if (length == 0)
		return B_OK;

	if (_data == NULL)
		return B_BAD_VALUE;

	// ToDo: these are not safe. If the length is smaller than the size of
	// the type to be converted, too much data may be read. R5 behaves in the
	// same way though.
	switch (type) {
		// 16 bit types
		case B_INT16_TYPE:
		case B_UINT16_TYPE:
		{
			uint16 *data = (uint16 *)_data;
			uint16 *end = (uint16 *)((addr_t)_data + length);

			while (data < end) {
				*data = __swap_int16(*data);
				data++;
			}
			break;
		}

		// 32 bit types
		case B_FLOAT_TYPE:
		case B_INT32_TYPE:
		case B_UINT32_TYPE:
		case B_TIME_TYPE:
		case B_RECT_TYPE:
		case B_POINT_TYPE:
#if B_HAIKU_32_BIT
		case B_SIZE_T_TYPE:
		case B_SSIZE_T_TYPE:
		case B_POINTER_TYPE:
#endif
		{
			uint32 *data = (uint32 *)_data;
			uint32 *end = (uint32 *)((addr_t)_data + length);

			while (data < end) {
				*data = __swap_int32(*data);
				data++;
			}
			break;
		}

		// 64 bit types
		case B_DOUBLE_TYPE:
		case B_INT64_TYPE:
		case B_UINT64_TYPE:
		case B_OFF_T_TYPE:
#if B_HAIKU_64_BIT
		case B_SIZE_T_TYPE:
		case B_SSIZE_T_TYPE:
		case B_POINTER_TYPE:
#endif
		{
			uint64 *data = (uint64 *)_data;
			uint64 *end = (uint64 *)((addr_t)_data + length);

			while (data < end) {
				*data = __swap_int64(*data);
				data++;
			}
			break;
		}

		// special types
		case B_MESSENGER_TYPE:
		{
			BMessenger *messenger = (BMessenger *)_data;
			BMessenger *end = (BMessenger *)((addr_t)_data + length);

			while (messenger < end) {
				BMessenger::Private messengerPrivate(messenger);
				// ToDo: if the additional fields change, this function has to be updated!
				messengerPrivate.SetTo(
					__swap_int32(messengerPrivate.Team()),
					__swap_int32(messengerPrivate.Port()),
					__swap_int32(messengerPrivate.Token()));
				messenger++;
			}
			break;
		}

		default:
			// not swappable or recognized type!
			return B_BAD_VALUE;
	}

	return B_OK;
}


/**
 * @brief Test whether a type_code value is stored in host-native byte order.
 *
 * Returns true for every well-known type_code that is always serialised in
 * the host's native byte order. This function does not indicate whether a
 * swap is currently needed; it simply identifies which type codes belong to
 * the "native format" set.
 *
 * @param type The type_code to test.
 * @return true if \a type is a recognised natively-ordered type;
 *         false for unknown or platform-dependent types.
 * @see swap_data()
 */
bool
is_type_swapped(type_code type)
{
	// Returns true when the type is in the host's native format
	// Looks like a pretty strange function to me :)

	switch (type) {
		case B_BOOL_TYPE:
		case B_CHAR_TYPE:
		case B_COLOR_8_BIT_TYPE:
		case B_DOUBLE_TYPE:
		case B_FLOAT_TYPE:
		case B_GRAYSCALE_8_BIT_TYPE:
		case B_INT64_TYPE:
		case B_INT32_TYPE:
		case B_INT16_TYPE:
		case B_INT8_TYPE:
		case B_MESSAGE_TYPE:
		case B_MESSENGER_TYPE:
		case B_MIME_TYPE:
		case B_MONOCHROME_1_BIT_TYPE:
		case B_OFF_T_TYPE:
		case B_PATTERN_TYPE:
		case B_POINTER_TYPE:
		case B_POINT_TYPE:
		case B_RECT_TYPE:
		case B_REF_TYPE:
		case B_NODE_REF_TYPE:
		case B_RGB_32_BIT_TYPE:
		case B_RGB_COLOR_TYPE:
		case B_SIZE_T_TYPE:
		case B_SSIZE_T_TYPE:
		case B_STRING_TYPE:
		case B_TIME_TYPE:
		case B_UINT64_TYPE:
		case B_UINT32_TYPE:
		case B_UINT16_TYPE:
		case B_UINT8_TYPE:
			return true;
	}

	return false;
}
