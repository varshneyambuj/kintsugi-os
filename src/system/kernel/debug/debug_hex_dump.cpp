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
 *   Copyright 2013, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file debug_hex_dump.cpp
 * @brief Classic 16-byte-per-line hex/ASCII dumper for the kernel debugger.
 *
 * Provides a small helper that prints a buffer (or any abstract byte stream
 * supplied via a HexDumpDataProvider) to the kernel debugger console using
 * the familiar "address: hex bytes  printable" format. Used by many KDL
 * commands that need to inspect raw memory.
 */


#include <debug_hex_dump.h>

#include <ctype.h>
#include <stdio.h>


namespace BKernel {


// #pragma mark - HexDumpDataProvider


/**
 * @brief Virtual destructor for the abstract data-provider base class.
 *
 * Present so that derived providers with their own cleanup are destroyed
 * correctly via a base pointer; this base implementation has no state.
 */
HexDumpDataProvider::~HexDumpDataProvider()
{
}


/**
 * @brief Default address-string hook: providers without a notion of address.
 *
 * Returning false causes print_hex_dump() to suppress the leading address
 * column for the current line.
 *
 * @param buffer Output buffer (unused in the base implementation).
 * @param bufferSize Capacity of buffer in bytes (unused).
 * @return Always false.
 */
bool
HexDumpDataProvider::GetAddressString(char* buffer, size_t bufferSize) const
{
	return false;
}


// #pragma mark - HexDumpBufferDataProvider


/**
 * @brief Construct a provider backed by an in-memory byte buffer.
 *
 * Stores a pointer-and-length pair; the buffer is not copied and must remain
 * valid for the lifetime of this provider.
 *
 * @param data Start of the byte buffer to be dumped.
 * @param dataSize Number of bytes in `data`.
 */
HexDumpBufferDataProvider::HexDumpBufferDataProvider(const void* data,
	size_t dataSize)
	:
	fData((const uint8*)data),
	fDataSize(dataSize)
{
}


/**
 * @brief Report whether any bytes remain in the backing buffer.
 *
 * @return true while the internal cursor is before the end of the buffer.
 */
bool
HexDumpBufferDataProvider::HasMoreData() const
{
	return fDataSize > 0;
}


/**
 * @brief Return and consume the next byte from the backing buffer.
 *
 * Returns '\0' once the buffer is exhausted so that callers which forget to
 * check HasMoreData() still observe a well-defined value.
 *
 * @return The next byte, or 0 if the buffer is empty.
 */
uint8
HexDumpBufferDataProvider::NextByte()
{
	if (fDataSize == 0)
		return '\0';

	fDataSize--;
	return *fData++;
}


/**
 * @brief Format the current cursor address as a printable pointer string.
 *
 * Used by print_hex_dump() to label each line with the address of its first
 * byte. Because the cursor advances as bytes are consumed, this naturally
 * reflects the in-buffer offset of the line being printed.
 *
 * @param buffer Output buffer to receive the NUL-terminated address string.
 * @param bufferSize Capacity of `buffer` in bytes.
 * @return Always true (this provider always has an address to report).
 */
bool
HexDumpBufferDataProvider::GetAddressString(char* buffer,
	size_t bufferSize) const
{
	snprintf(buffer, bufferSize, "%p", fData);
	return true;
}


// #pragma mark -


/**
 * @brief Print a hex/ASCII dump of bytes supplied by a data provider.
 *
 * Emits up to `maxBytes` bytes, sixteen per line, grouped in blocks of four
 * with a space separator. Each line optionally begins with an address label
 * obtained from the provider, followed by the hex bytes, padding on a short
 * final line, and the printable-character sidebar. Non-printable bytes in
 * the sidebar are rendered as '.'.
 *
 * @param data Data provider to consume bytes from; stops early once
 *             data.HasMoreData() returns false.
 * @param maxBytes Upper bound on the total number of bytes to print.
 * @param flags Bit set of HEX_DUMP_FLAG_* values; currently only
 *              HEX_DUMP_FLAG_OMIT_ADDRESS is honoured.
 * @return void
 */
void
print_hex_dump(HexDumpDataProvider& data, size_t maxBytes, uint32 flags)
{
	static const size_t kBytesPerBlock = 4;
	static const size_t kBytesPerLine = 16;

	size_t i = 0;
	for (; i < maxBytes && data.HasMoreData();) {
		if (i > 0)
			kputs("\n");

		// print address
		uint8 buffer[kBytesPerLine];
		if ((flags & HEX_DUMP_FLAG_OMIT_ADDRESS) == 0
			&& data.GetAddressString((char*)buffer, sizeof(buffer))) {
			kputs((char*)buffer);
			kputs(": ");
		}

		// get the line data
		size_t bytesInLine = 0;
		for (; i < maxBytes && bytesInLine < kBytesPerLine
				&& data.HasMoreData();
			i++) {
			buffer[bytesInLine++] = data.NextByte();
		}

		// print hex representation
		for (size_t k = 0; k < bytesInLine; k++) {
			if (k > 0 && k % kBytesPerBlock == 0)
				kputs(" ");
			kprintf("%02x", buffer[k]);
		}

		// pad to align the text representation, if line is incomplete
		if (bytesInLine < kBytesPerLine) {
			int missingBytes = int(kBytesPerLine - bytesInLine);
			kprintf("%*s",
				2 * missingBytes + int(missingBytes / kBytesPerBlock), "");
		}

		// print character representation
		kputs("  ");
		for (size_t k = 0; k < bytesInLine; k++)
			kprintf("%c", isprint(buffer[k]) ? buffer[k] : '.');
	}

	if (i > 0)
		kputs("\n");
}


/**
 * @brief Convenience overload: hex-dump a raw memory buffer.
 *
 * Wraps the buffer in a HexDumpBufferDataProvider and forwards to the
 * provider-based overload.
 *
 * @param data Start of the buffer to dump.
 * @param maxBytes Number of bytes to print.
 * @param flags Bit set of HEX_DUMP_FLAG_* values.
 * @return void
 */
void
print_hex_dump(const void* data, size_t maxBytes, uint32 flags)
{
	HexDumpBufferDataProvider dataProvider(data, maxBytes);
	print_hex_dump(dataProvider, maxBytes, flags);
}


}	// namespace BKernel
