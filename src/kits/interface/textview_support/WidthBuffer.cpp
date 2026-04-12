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
 *   Copyright 2003-2008, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stefano Ceccherini (stefano.ceccherini@gmail.com)
 */

//!	Caches string widths in a hash table, to avoid a trip to the app server.


/**
 * @file WidthBuffer.cpp
 * @brief Per-font character-escapement cache used by BTextView.
 *
 * WidthBuffer maintains one open-addressing hash table per font.  Each entry
 * maps a UTF-8 character code (packed into a uint32) to its escapement value
 * (advance width expressed as a fraction of the font size).  Lookups avoid
 * round-trips to the app_server; cache misses are batched and filled via
 * BFont::GetEscapements().  The table doubles in size when 2/3 full.
 *
 * @see BTextView, TextGapBuffer
 */


#include "utf8_functions.h"
#include "TextGapBuffer.h"
#include "WidthBuffer.h"

#include <Autolock.h>
#include <Debug.h>
#include <Font.h>
#include <Locker.h>

#include <stdio.h>


//! NetPositive binary compatibility support
class _BWidthBuffer_;


namespace BPrivate {


/** @brief Number of initial hash-table slots allocated per font. */
const static uint32 kTableCount = 128;

/** @brief Sentinel code value used to mark empty hash-table slots. */
const static uint32 kInvalidCode = 0xFFFFFFFF;

/** @brief Global WidthBuffer instance; initialised in InterfaceDefs.cpp. */
WidthBuffer* gWidthBuffer = NULL;


/** @brief A single slot in the per-font hash table. */
struct hashed_escapement {
	uint32 code;
	float escapement;

	hashed_escapement()
	{
		code = kInvalidCode;
		escapement = 0;
	}
};


/**
 * @brief Converts a UTF-8 character to a unique 32-bit code for hashing.
 *
 * Packs up to 4 bytes of the character into a uint32, most-significant byte
 * first, so that different characters always produce different codes.
 *
 * @param text    Pointer to the first byte of the character.
 * @param charLen Byte length of the UTF-8 character.
 * @return A uint32 that uniquely identifies the character.
 */
static inline uint32
CharToCode(const char* text, const int32 charLen)
{
	uint32 value = 0;
	int32 shiftVal = 24;
	for (int32 c = 0; c < charLen; c++) {
		value |= ((unsigned char)text[c] << shiftVal);
		shiftVal -= 8;
	}
	return value;
}


/**
 * @brief Constructs the global WidthBuffer with an initial capacity of one
 *        font table.
 */
WidthBuffer::WidthBuffer()
	:
	_BTextViewSupportBuffer_<_width_table_>(1, 0),
	fLock("width buffer")
{
}


/**
 * @brief Destroys the WidthBuffer, freeing all per-font hash tables.
 */
WidthBuffer::~WidthBuffer()
{
	for (int32 x = 0; x < fItemCount; x++)
		delete[] (hashed_escapement*)fBuffer[x].widths;
}


/**
 * @brief Returns the pixel width of a substring within a raw character array.
 *
 * Looks up each UTF-8 character in the hash table for @p inStyle; any
 * characters not yet cached are queued and passed to HashEscapements() in a
 * single batch.
 *
 * @param inText      Pointer to the source string.
 * @param fromOffset  Byte offset within @p inText at which to start.
 * @param length      Number of bytes to measure.
 * @param inStyle     Font to use for width calculation.
 * @return Pixel width of the substring, or 0 on error.
 */
float
WidthBuffer::StringWidth(const char* inText, int32 fromOffset, int32 length,
	const BFont* inStyle)
{
	if (inText == NULL || length <= 0)
		return 0;

	BAutolock _(fLock);

	int32 index = 0;
	if (!FindTable(inStyle, &index))
		index = InsertTable(inStyle);

	char* text = NULL;
	int32 numChars = 0;
	int32 textLen = 0;

	const char* sourceText = inText + fromOffset;
	const float fontSize = inStyle->Size();
	float stringWidth = 0;

	for (int32 charLen = 0; length > 0;
			sourceText += charLen, length -= charLen) {
		charLen = UTF8NextCharLen(sourceText, length);

		// End of string, bail out
		if (charLen <= 0)
			break;

		// Some magic, to uniquely identify this character
		const uint32 value = CharToCode(sourceText, charLen);

		float escapement;
		if (GetEscapement(value, index, &escapement)) {
			// Well, we've got a match for this character
			stringWidth += escapement;
		} else {
			// Store this character into an array, which we'll
			// pass to HashEscapements() later
			int32 offset = textLen;
			textLen += charLen;
			numChars++;
			char* newText = (char*)realloc(text, textLen + 1);
			if (newText == NULL) {
				free(text);
				return 0;
			}

			text = newText;
			memcpy(&text[offset], sourceText, charLen);
		}
	}

	if (text != NULL) {
		// We've found some characters which aren't yet in the hash table.
		// Get their width via HashEscapements()
		text[textLen] = 0;
		stringWidth += HashEscapements(text, numChars, textLen, index, inStyle);
		free(text);
	}

	return stringWidth * fontSize;
}


/**
 * @brief Returns the pixel width of a substring within a TextGapBuffer.
 *
 * Assembles the substring from the gap buffer and delegates to the
 * char-pointer overload.
 *
 * @param inBuffer    TextGapBuffer containing the source text.
 * @param fromOffset  Byte offset within the buffer at which to start.
 * @param length      Number of bytes to measure.
 * @param inStyle     Font to use for width calculation.
 * @return Pixel width of the substring.
 */
float
WidthBuffer::StringWidth(TextGapBuffer &inBuffer, int32 fromOffset,
	int32 length, const BFont* inStyle)
{
	const char* text = inBuffer.GetString(fromOffset, &length);
	return StringWidth(text, 0, length, inStyle);
}


/**
 * @brief Searches for an existing per-font hash table.
 *
 * @param inStyle  Font to look up.
 * @param outIndex Output: index of the table if found, or -1 if not found;
 *                 may be NULL.
 * @return true if a table for @p inStyle was found, false otherwise.
 */
bool
WidthBuffer::FindTable(const BFont* inStyle, int32* outIndex)
{
	if (inStyle == NULL)
		return false;

	int32 tableIndex = -1;

	for (int32 i = 0; i < fItemCount; i++) {
		if (*inStyle == fBuffer[i].font) {
			tableIndex = i;
			break;
		}
	}
	if (outIndex != NULL)
		*outIndex = tableIndex;

	return tableIndex != -1;
}


/**
 * @brief Creates and inserts an empty hash table for the given font.
 *
 * @param font Font for which to create the table.
 * @return Index of the newly created table.
 */
int32
WidthBuffer::InsertTable(const BFont* font)
{
	_width_table_ table;

	table.font = *font;
	table.hashCount = 0;
	table.tableCount = kTableCount;
	table.widths = new hashed_escapement[kTableCount];

	uint32 position = fItemCount;
	InsertItemsAt(1, position, &table);

	return position;
}


/**
 * @brief Looks up the cached escapement for a character code.
 *
 * Uses open-addressing with linear probing.
 *
 * @param value      Character code returned by CharToCode().
 * @param index      Index of the per-font table to search.
 * @param escapement Output: escapement value if found; may be NULL.
 * @return true if the code was found in the table, false otherwise.
 */
bool
WidthBuffer::GetEscapement(uint32 value, int32 index, float* escapement)
{
	const _width_table_ &table = fBuffer[index];
	const hashed_escapement* widths
		= static_cast<hashed_escapement*>(table.widths);
	uint32 hashed = Hash(value) & (table.tableCount - 1);

	uint32 found;
	while ((found = widths[hashed].code) != kInvalidCode) {
		if (found == value)
			break;

		if (++hashed >= (uint32)table.tableCount)
			hashed = 0;
	}

	if (found == kInvalidCode)
		return false;

	if (escapement != NULL)
		*escapement = widths[hashed].escapement;

	return true;
}


/**
 * @brief Computes a hash value for a character code.
 *
 * Uses a hand-crafted mix to spread the 32-bit code across the table index
 * range.
 *
 * @param val Character code to hash.
 * @return Hash value; caller masks it to the table size.
 */
uint32
WidthBuffer::Hash(uint32 val)
{
	uint32 shifted = val >> 24;
	uint32 result = (val >> 15) + (shifted * 3);

	result ^= (val >> 6) - (shifted * 22);
	result ^= (val << 3);

	return result;
}


/**
 * @brief Fetches escapements for a batch of uncached characters and inserts
 *        them into the hash table.
 *
 * Calls BFont::GetEscapements() once for the entire batch, then inserts each
 * result into the hash table using open-addressing.  The table is doubled if
 * its load factor exceeds 2/3.
 *
 * @param inText      Null-terminated UTF-8 string containing the characters
 *                    to cache.
 * @param numChars    Number of characters in @p inText.
 * @param textLen     Byte length of @p inText (excluding the null terminator).
 * @param tableIndex  Index of the per-font table to update.
 * @param inStyle     Font used to fetch escapements.
 * @return Sum of the escapement values for all characters in @p inText.
 */
float
WidthBuffer::HashEscapements(const char* inText, int32 numChars, int32 textLen,
	int32 tableIndex, const BFont* inStyle)
{
	ASSERT(inText != NULL);
	ASSERT(numChars > 0);
	ASSERT(textLen > 0);

	float* escapements = new float[numChars];
	inStyle->GetEscapements(inText, numChars, escapements);

	_width_table_ &table = fBuffer[tableIndex];
	hashed_escapement* widths = static_cast<hashed_escapement*>(table.widths);

	int32 charCount = 0;
	char* text = (char*)inText;
	const char* textEnd = inText + textLen;
	// Insert the escapements into the hash table
	do {
		// Using this variant is safe as the handed in string is guaranteed to
		// be 0 terminated.
		const int32 charLen = UTF8NextCharLen(text);
		if (charLen == 0)
			break;

		const uint32 value = CharToCode(text, charLen);

		uint32 hashed = Hash(value) & (table.tableCount - 1);
		uint32 found;
		while ((found = widths[hashed].code) != kInvalidCode) {
			if (found == value)
				break;
			if (++hashed >= (uint32)table.tableCount)
				hashed = 0;
		}

		if (found == kInvalidCode) {
			// The value is not in the table. Add it.
			widths[hashed].code = value;
			widths[hashed].escapement = escapements[charCount];
			table.hashCount++;

			// We always keep some free space in the hash table:
			// we double the current size when hashCount is 2/3 of
			// the total size.
			if (table.tableCount * 2 / 3 <= table.hashCount) {
				const int32 newSize = table.tableCount * 2;

				// Create and initialize a new hash table
				hashed_escapement* newWidths = new hashed_escapement[newSize];

				// Rehash the values, and put them into the new table
				for (uint32 oldPos = 0; oldPos < (uint32)table.tableCount;
						oldPos++) {
					if (widths[oldPos].code != kInvalidCode) {
						uint32 newPos
							= Hash(widths[oldPos].code) & (newSize - 1);
						while (newWidths[newPos].code != kInvalidCode) {
							if (++newPos >= (uint32)newSize)
								newPos = 0;
						}
						newWidths[newPos] = widths[oldPos];
					}
				}

				// Delete the old table, and put the new pointer into the
				// _width_table_
				delete[] widths;
				table.tableCount = newSize;
				table.widths = widths = newWidths;
			}
		}
		charCount++;
		text += charLen;
	} while (text < textEnd);

	// Calculate the width of the string
	float width = 0;
	for (int32 x = 0; x < numChars; x++)
		width += escapements[x];

	delete[] escapements;

	return width;
}

} // namespace BPrivate


#if __GNUC__ < 3
//! NetPositive binary compatibility support

_BWidthBuffer_::_BWidthBuffer_()
{
}

_BWidthBuffer_::~_BWidthBuffer_()
{
}

_BWidthBuffer_* gCompatibilityWidthBuffer = NULL;

extern "C"
float
StringWidth__14_BWidthBuffer_PCcllPC5BFont(_BWidthBuffer_* widthBuffer,
	const char* inText, int32 fromOffset, int32 length, const BFont* inStyle)
{
	return BPrivate::gWidthBuffer->StringWidth(inText, fromOffset, length,
		inStyle);
}

#endif // __GNUC__ < 3
