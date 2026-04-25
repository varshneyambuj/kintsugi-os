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
 * MIT License. Copyright 2009-2012, Ingo Weinhold.
 */

/** @file DataReader.h
    @brief Forward-only reader of DWARF byte streams (LEB128, addresses, strings, ...). */

#ifndef DATA_READER_H
#define DATA_READER_H


#include <string.h>

#include "Types.h"


/**
 * @brief Cursor over a byte buffer with DWARF-specific decoding helpers.
 *
 * Tracks address size, endianness, and an overflow flag so that
 * truncated input becomes an explicit failure rather than undefined
 * behaviour.  All readers return a caller-supplied default value when
 * the buffer is exhausted.
 */
class DataReader {
public:
	/** @brief Constructs an empty reader; SetTo must be called before use. */
	DataReader()
		:
		fData(NULL),
		fSize(0),
		fInitialSize(0),
		fAddressSize(4),
		fIsBigEndian(false),
		fOverflow(false)
	{
	}

	/** @brief Constructs a reader bound to @a data of @a size bytes. */
	DataReader(const void* data, off_t size, uint8 addressSize, bool isBigEndian)
	{
		SetTo(data, size, addressSize, isBigEndian);
	}

	/** @brief Rebinds the reader to a new buffer and resets overflow state. */
	void SetTo(const void* data, off_t size, uint8 addressSize, bool isBigEndian)
	{
		fData = (const uint8*)data;
		fInitialSize = fSize = size;
		fAddressSize = addressSize;
		fIsBigEndian = isBigEndian;
		fOverflow = false;
	}

	/** @brief Returns a copy of this reader (positioned identically). */
	DataReader RestrictedReader()
	{
		return *this;
	}

	/** @brief Returns a sub-reader limited to @a maxLength bytes from the cursor. */
	DataReader RestrictedReader(off_t maxLength)
	{
		return DataReader(fData, maxLength, fAddressSize, fIsBigEndian);
	}

	/** @brief Returns a sub-reader at a relative offset within the buffer. */
	DataReader RestrictedReader(off_t relativeOffset, off_t maxLength)
	{
		return DataReader(fData + relativeOffset, maxLength, fAddressSize, fIsBigEndian);
	}

	/** @brief Returns @c true while at least one byte remains. */
	bool HasData() const
	{
		return fSize > 0;
	}

	/** @brief Returns the configured target address width in bytes. */
	uint32 AddressSize() const
	{
		return fAddressSize;
	}

	/** @brief Returns @c true if the buffer is encoded big-endian. */
	bool IsBigEndian() const
	{
		return fIsBigEndian;
	}

	/** @brief Updates the target address width on the fly. */
	void SetAddressSize(uint8 addressSize)
	{
		fAddressSize = addressSize;
	}

	/** @brief Returns @c true if any read attempted to consume past the buffer end. */
	bool HasOverflow() const
	{
		return fOverflow;
	}

	/** @brief Returns the current cursor position as a raw pointer. */
	const void* Data() const
	{
		return fData;
	}

	/** @brief Returns the number of unread bytes remaining. */
	off_t BytesRemaining() const
	{
		return fSize;
	}

	/** @brief Returns the cursor offset relative to the original buffer start. */
	off_t Offset() const
	{
		return fInitialSize - fSize;
	}

	/** @brief Seeks the cursor to an absolute offset, clamping to [0, size]. */
	void SeekAbsolute(off_t offset)
	{
		if (offset < 0)
			offset = 0;
		else if (offset > fInitialSize)
			offset = fInitialSize;

		fData += offset - Offset();
		fSize = fInitialSize - offset;
	}

	/**
	 * @brief Reads a fixed-size native-endian value from the buffer.
	 *
	 * @param defaultValue Value returned on overflow.
	 * @return The decoded value, or @a defaultValue on overflow.
	 */
	//TODO: take care of host vs target endianness
	template<typename Type>
	Type Read(const Type& defaultValue)
	{
		if (fSize < (off_t)sizeof(Type)) {
			fOverflow = true;
			fSize = 0;
			return defaultValue;
		}

		Type data;
		memcpy(&data, fData, sizeof(Type));

		fData += sizeof(Type);
		fSize -= sizeof(Type);

		return data;
	}

	/** @brief Reads a target-sized address (4 or 8 bytes). */
	target_addr_t ReadAddress(target_addr_t defaultValue)
	{
		return fAddressSize == 4
			? (target_addr_t)Read<uint32>(defaultValue)
			: (target_addr_t)Read<uint64>(defaultValue);
	}

	/** @brief Reads an unsigned LEB128-encoded integer. */
	uint64 ReadUnsignedLEB128(uint64 defaultValue)
	{
		uint64 result = 0;
		int shift = 0;
		while (true) {
			uint8 byte = Read<uint8>(0);
			result |= uint64(byte & 0x7f) << shift;
			if ((byte & 0x80) == 0)
				break;
			shift += 7;
		}

		return fOverflow ? defaultValue : result;
	}

	/** @brief Reads a signed LEB128-encoded integer (sign-extended). */
	int64 ReadSignedLEB128(int64 defaultValue)
	{
		int64 result = 0;
		int shift = 0;
		while (true) {
			uint8 byte = Read<uint8>(0);
			result |= uint64(byte & 0x7f) << shift;
			shift += 7;

			if ((byte & 0x80) == 0) {
				// sign extend
				if ((byte & 0x40) != 0 && shift < 64)
					result |= -((uint64)1 << shift);
				break;
			}
		}

		return fOverflow ? defaultValue : result;
	}

	/** @brief Reads an integer of @a numBytes width honouring the buffer's endianness. */
	uint64 ReadUInt(size_t numBytes, uint64 defaultValue)
	{
		uint64 result = 0;
		if (fIsBigEndian) {
			for (size_t i = 0; i < numBytes; i++) {
				uint8 byte = Read<uint8>(0);
				result <<= 8;
				result |= (uint64)byte;
			}
		} else {
			int shift = 0;
			for (size_t i = 0; i < numBytes; i++) {
				uint8 byte = Read<uint8>(0);
				result |= (uint64)byte << shift;
				shift += 8;
			}
		}

		return fOverflow ? defaultValue : result;
	}

	/** @brief Reads a 24-bit unsigned integer (used in some DWARF forms). */
	uint32 ReadU24(uint32 defaultValue)
	{
		return ReadUInt(3, defaultValue);
	}

	/** @brief Reads a NUL-terminated string and advances past the terminator. */
	const char* ReadString()
	{
		const char* string = (const char*)fData;
		while (fSize > 0) {
			fData++;
			fSize--;

			if (fData[-1] == 0)
				return string;
		}

		fOverflow = true;
		return NULL;
	}

	/**
	 * @brief Reads the DWARF "initial length" field, detecting DWARF-64 form.
	 *
	 * @param _dwarf64 Output set to @c true when the 0xffffffff escape
	 *                 marker indicated a 64-bit length follows.
	 * @return The parsed length value.
	 */
	uint64 ReadInitialLength(bool& _dwarf64)
	{
		uint64 length = Read<uint32>(0);
		_dwarf64 = (length == 0xffffffff);
		if (_dwarf64)
			length = Read<uint64>(0);
		return length;
	}

	/** @brief Advances the cursor by @a bytes; sets overflow if it would underflow. */
	bool Skip(off_t bytes)
	{
		if (bytes < 0)
			return false;

		if (bytes > fSize) {
			fSize = 0;
			fOverflow = true;
			return false;
		}

		fData += bytes;
		fSize -= bytes;

		return true;
	}

private:
	const uint8*	fData;
	off_t			fSize;
	off_t			fInitialSize;
	uint8			fAddressSize;
	bool			fIsBigEndian;
	bool			fOverflow;
};


#endif	// DATA_READER_H
