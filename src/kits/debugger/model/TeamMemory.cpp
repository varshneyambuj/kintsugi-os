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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file TeamMemory.cpp
 * @brief Implementation of TeamMemory, the abstract interface for reading
 *        from a debugged team's address space, plus a string-read helper.
 *
 * Concrete subclasses implement @c ReadMemory() to access either a live
 * team or a core-file image; this translation unit anchors the destructor
 * and provides @c ReadMemoryString(), a portable helper that reads
 * NUL-terminated C strings page-by-page without crossing page boundaries
 * in a single call.
 */


#include "TeamMemory.h"

#include <algorithm>

#include <OS.h>
#include <String.h>


/**
 * @brief Virtual destructor anchor for TeamMemory.
 */
TeamMemory::~TeamMemory()
{
}


/**
 * @brief Reads a NUL-terminated C string from target memory into @a _string.
 *
 * Reads at most @a maxLength bytes, never crossing a page boundary in a
 * single underlying @c ReadMemory() call so that an unmapped page only
 * truncates the result rather than failing the entire operation. Reading
 * stops at the first NUL byte.
 *
 * @param address    Target-space starting address of the string.
 * @param maxLength  Maximum number of bytes to read.
 * @param _string    Receives the bytes read; cleared before reading.
 * @return          @c B_OK on success or partial success after at least one
 *                   byte was read; the propagated I/O error if no bytes were
 *                   read; @c B_BAD_ADDRESS if the first read returned zero
 *                   bytes.
 */
status_t
TeamMemory::ReadMemoryString(target_addr_t address, size_t maxLength,
	BString& _string)
{
	char buffer[B_PAGE_SIZE];

	_string.Truncate(0);
	while (maxLength > 0) {
		// read at max maxLength bytes, but don't read across page bounds
		size_t toRead = std::min(maxLength,
			B_PAGE_SIZE - size_t(address % B_PAGE_SIZE));
		ssize_t bytesRead = ReadMemory(address, buffer, toRead);
		if (bytesRead < 0)
			return _string.Length() == 0 ? bytesRead : B_OK;

		if (bytesRead == 0)
			return _string.Length() == 0 ? B_BAD_ADDRESS : B_OK;

		// append the bytes read
		size_t length = strnlen(buffer, bytesRead);
		_string.Append(buffer, length);

		// stop at end of string
		if (length < (size_t)bytesRead)
			return B_OK;

		address += bytesRead;
		maxLength -= bytesRead;
	}

	return B_OK;
}

