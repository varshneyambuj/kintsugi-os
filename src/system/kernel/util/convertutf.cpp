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
 *   Copyright 2014 Jonathan Schleifer <js@webkeks.org>
 *   Copyright 2014 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Jonathan Schleifer, js@webkeks.org
 *       John Scipione, jscipione@gmail.com
 */

/**
 * @file convertutf.cpp
 * @brief UTF-16 (LE/BE) → UTF-8 conversion used by filesystems.
 *
 * Converts a UTF-16 code-unit sequence into a UTF-8 byte sequence, honouring
 * surrogate pairs for codepoints above U+FFFF. Callers that truncate the
 * output (target buffer full or the kernel's B_FILE_NAME_LENGTH reached) get
 * B_NAME_TOO_LONG plus a NUL-terminated prefix they can recover with strlen().
 */


#include <util/convertutf.h>


#include <ByteOrder.h>
#include <Errors.h>
#include <StorageDefs.h>


/**
 * @brief Return the UTF-8 byte length needed to encode @a glyph.
 * @param glyph Unicode codepoint.
 * @return 1, 2, 3 or 4 for valid codepoints; 0 for codepoints above U+10FFFF.
 */
static inline size_t
glyph_length(uint32 glyph)
{
	if (glyph < 0x80)
		return 1;
	else if (glyph < 0x800)
		return 2;
	else if (glyph < 0x10000)
		return 3;
	else if (glyph < 0x110000)
		return 4;

	return 0;
}


/**
 * @brief Emit the UTF-8 encoding of @a glyph into @a buffer.
 *
 * Caller must have allocated at least @a glyphLength bytes and obtained
 * @a glyphLength from glyph_length(). No output terminator is appended.
 *
 * @param glyph       Unicode codepoint.
 * @param glyphLength Byte length returned by glyph_length().
 * @param buffer      Output buffer.
 */
static void
encode_glyph(uint32 glyph, size_t glyphLength, char* buffer)
{
	if (glyphLength == 1) {
		*buffer = glyph;
	} else if (glyphLength == 2) {
		*buffer++ = 0xC0 | (glyph >> 6);
		*buffer = 0x80 | (glyph & 0x3F);
	} else if (glyphLength == 3) {
		*buffer++ = 0xE0 | (glyph >> 12);
		*buffer++ = 0x80 | (glyph >> 6 & 0x3F);
		*buffer = 0x80 | (glyph & 0x3F);
	} else if (glyphLength == 4) {
		*buffer++ = 0xF0 | (glyph >> 18);
		*buffer++ = 0x80 | (glyph >> 12 & 0x3F);
		*buffer++ = 0x80 | (glyph >> 6 & 0x3F);
		*buffer = 0x80 | (glyph & 0x3F);
	}
}


/**
 * @brief Core UTF-16 → UTF-8 conversion with selectable byte order.
 *
 * Scans @a source, decodes surrogate pairs, and writes UTF-8 bytes into
 * @a target. The output is always NUL-terminated (including on truncation).
 *
 * @param source              UTF-16 code units.
 * @param sourceCodeUnitCount Number of input code units.
 * @param target              Output buffer for UTF-8 bytes.
 * @param targetLength        Size of @a target in bytes (including NUL).
 * @param isLittleEndian      true if @a source is UTF-16LE; false for UTF-16BE.
 * @return Bytes written (excluding NUL) on success, B_BAD_VALUE for invalid
 *         input, B_NAME_TOO_LONG if truncation occurred.
 */
static ssize_t
utf16_to_utf8(const uint16* source, size_t sourceCodeUnitCount, char* target,
	size_t targetLength, bool isLittleEndian)
{
	if (source == NULL || sourceCodeUnitCount == 0
		|| target == NULL || targetLength == 0) {
		return B_BAD_VALUE;
	}

	ssize_t outLength = 0;

	for (size_t i = 0; i < sourceCodeUnitCount; i++) {
		uint32 glyph = isLittleEndian
			? B_LENDIAN_TO_HOST_INT32(source[i])
			: B_BENDIAN_TO_HOST_INT32(source[i]);

		if ((glyph & 0xFC00) == 0xDC00) {
			// missing high surrogate
			return B_BAD_VALUE;
		}

		if ((glyph & 0xFC00) == 0xD800) {
			if (sourceCodeUnitCount <= i + 1) {
				// high surrogate at end of string
				return B_BAD_VALUE;
			}

			uint32 low = isLittleEndian
				? B_LENDIAN_TO_HOST_INT32(source[i + 1])
				: B_BENDIAN_TO_HOST_INT32(source[i + 1]);
			if ((low & 0xFC00) != 0xDC00) {
				// missing low surrogate
				return B_BAD_VALUE;
			}

			glyph = (((glyph & 0x3FF) << 10) | (low & 0x3FF)) + 0x10000;
			i++;
		}

		size_t glyphLength = glyph_length(glyph);
		if (glyphLength == 0)
			return B_BAD_VALUE;
		else if (outLength + glyphLength >= targetLength
			|| outLength + glyphLength >= B_FILE_NAME_LENGTH) {
			// NUL terminate the string so the caller can use the
			// abbreviated version in this case. Since the length
			// isn't returned the caller will need to call strlen()
			// to get the length of the string.
			target[outLength] = '\0';
			return B_NAME_TOO_LONG;
		}

		encode_glyph(glyph, glyphLength, target + outLength);
		outLength += glyphLength;
	}

	target[outLength] = '\0';

	return outLength;
}


/**
 * @brief UTF-16LE → UTF-8 conversion.
 * @see utf16_to_utf8() for parameter semantics and return values.
 */
ssize_t
utf16le_to_utf8(const uint16* source, size_t sourceCodeUnitCount,
	char* target, size_t targetLength)
{
	return utf16_to_utf8(source, sourceCodeUnitCount, target, targetLength,
		true);
}


/**
 * @brief UTF-16BE → UTF-8 conversion.
 * @see utf16_to_utf8() for parameter semantics and return values.
 */
ssize_t
utf16be_to_utf8(const uint16* source, size_t sourceCodeUnitCount,
	char* target, size_t targetLength)
{
	return utf16_to_utf8(source, sourceCodeUnitCount, target, targetLength,
		false);
}
