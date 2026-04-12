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
 *   Copyright 2001-2006, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marc Flerackers (mflerackers@androme.be)
 *       Stefano Ceccherini (stefano.ceccherini@gmail.com)
 */


/**
 * @file TextGapBuffer.cpp
 * @brief Gap-buffer implementation for the raw text storage used by BTextView.
 *
 * TextGapBuffer stores a mutable UTF-8 string using the gap-buffer technique:
 * a contiguous allocation is split into a "before" region, a gap, and an
 * "after" region.  Insertions at the gap position are O(1); all other
 * operations move the gap first.  A scratch buffer is maintained to assemble
 * contiguous substrings that straddle the gap, and optionally to replace all
 * characters with the bullet character in password mode.
 *
 * @see BTextView, LineBuffer, StyleBuffer
 */


#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <utf8_functions.h>

#include <File.h>
#include <InterfaceDefs.h> // for B_UTF8_BULLET

#include "TextGapBuffer.h"


namespace BPrivate {


/** @brief Minimum allocation block size used when enlarging or creating the gap. */
static const int32 kTextGapBufferBlockSize = 2048;


/**
 * @brief Constructs an empty TextGapBuffer with an initial gap allocation.
 */
TextGapBuffer::TextGapBuffer()
	:
	fItemCount(0),
	fBuffer(NULL),
	fBufferCount(kTextGapBufferBlockSize + fItemCount),
	fGapIndex(fItemCount),
	fGapCount(fBufferCount - fGapIndex),
	fScratchBuffer(NULL),
	fScratchSize(0),
	fPasswordMode(false)
{
	fBuffer = (char*)malloc(kTextGapBufferBlockSize + fItemCount);
	fScratchBuffer = NULL;
}


/**
 * @brief Destroys the TextGapBuffer and frees all allocated memory.
 */
TextGapBuffer::~TextGapBuffer()
{
	free(fBuffer);
	free(fScratchBuffer);
}


/**
 * @brief Inserts a byte string into the buffer at the given position.
 *
 * Moves the gap to @p inAtIndex if necessary, enlarges the gap if needed,
 * then copies the bytes into the gap and advances past them.
 *
 * @param inText      Pointer to the bytes to insert.
 * @param inNumItems  Number of bytes to insert.
 * @param inAtIndex   Byte position at which to insert; clamped to [0, fItemCount].
 */
void
TextGapBuffer::InsertText(const char* inText, int32 inNumItems, int32 inAtIndex)
{
	if (inNumItems < 1)
		return;

	inAtIndex = (inAtIndex > fItemCount) ? fItemCount : inAtIndex;
	inAtIndex = (inAtIndex < 0) ? 0 : inAtIndex;

	if (inAtIndex != fGapIndex)
		_MoveGapTo(inAtIndex);

	if (fGapCount < inNumItems)
		_EnlargeGapTo(inNumItems + kTextGapBufferBlockSize);

	memcpy(fBuffer + fGapIndex, inText, inNumItems);

	fGapCount -= inNumItems;
	fGapIndex += inNumItems;
	fItemCount += inNumItems;
}


/**
 * @brief Inserts bytes read from a BFile into the buffer.
 *
 * Reads up to @p inNumItems bytes starting at @p fileOffset from @p file,
 * clamped to the actual file size.
 *
 * @param file        Readable BFile to read from.
 * @param fileOffset  Byte position within the file to start reading.
 * @param inNumItems  Number of bytes to insert.
 * @param inAtIndex   Byte position in the buffer at which to insert.
 * @return true on success, false if the file is unreadable or empty.
 */
bool
TextGapBuffer::InsertText(BFile* file, int32 fileOffset, int32 inNumItems,
	int32 inAtIndex)
{
	off_t fileSize;

	if (file->GetSize(&fileSize) != B_OK
		|| !file->IsReadable())
		return false;

	// Clamp the text length to the file size
	fileSize -= fileOffset;

	if (fileSize < inNumItems)
		inNumItems = fileSize;

	if (inNumItems < 1)
		return false;

	inAtIndex = (inAtIndex > fItemCount) ? fItemCount : inAtIndex;
	inAtIndex = (inAtIndex < 0) ? 0 : inAtIndex;

	if (inAtIndex != fGapIndex)
		_MoveGapTo(inAtIndex);

	if (fGapCount < inNumItems)
		_EnlargeGapTo(inNumItems + kTextGapBufferBlockSize);

	// Finally, read the data and put it into the buffer
	if (file->ReadAt(fileOffset, fBuffer + fGapIndex, inNumItems) > 0) {
		fGapCount -= inNumItems;
		fGapIndex += inNumItems;
		fItemCount += inNumItems;
	}

	return true;
}


/**
 * @brief Removes the bytes in [start, end) from the buffer.
 *
 * Moves the gap to @p start, then widens it by the deleted count.  If the
 * resulting gap exceeds the block size, the gap is shrunk to save memory.
 *
 * @param start First byte index of the range to delete.
 * @param end   One-past-last byte index of the range to delete.
 */
void
TextGapBuffer::RemoveRange(int32 start, int32 end)
{
	int32 inAtIndex = start;
	int32 inNumItems = end - start;

	if (inNumItems < 1)
		return;

	inAtIndex = (inAtIndex > fItemCount - 1) ? (fItemCount - 1) : inAtIndex;
	inAtIndex = (inAtIndex < 0) ? 0 : inAtIndex;

	_MoveGapTo(inAtIndex);

	fGapCount += inNumItems;
	fItemCount -= inNumItems;

	if (fGapCount > kTextGapBufferBlockSize)
		_ShrinkGapTo(kTextGapBufferBlockSize / 2);
}


/**
 * @brief Returns a pointer to the substring starting at @p fromOffset.
 *
 * If the requested range lies entirely on one side of the gap, returns a
 * direct pointer into fBuffer.  Otherwise copies the bytes into the scratch
 * buffer and returns that.  In password mode the returned bytes are replaced
 * with the UTF-8 bullet character and @p *_numBytes is updated.
 *
 * @param fromOffset   First byte of the substring.
 * @param _numBytes    In: desired byte count. Out: actual byte count (may change
 *                     in password mode).
 * @return Pointer to a null-terminated substring; valid until the next
 *         mutating call.
 */
const char*
TextGapBuffer::GetString(int32 fromOffset, int32* _numBytes)
{
	const char* result = "";
	if (_numBytes == NULL)
		return result;

	int32 numBytes = *_numBytes;
	if (numBytes < 1)
		return result;

	bool isStartBeforeGap = fromOffset < fGapIndex;
	bool isEndBeforeGap = (fromOffset + numBytes - 1) < fGapIndex;

	if (isStartBeforeGap == isEndBeforeGap) {
		result = fBuffer + fromOffset;
		if (!isStartBeforeGap)
			result += fGapCount;
	} else {
		if (fScratchSize < numBytes) {
			fScratchBuffer = (char*)realloc(fScratchBuffer, numBytes);
			fScratchSize = numBytes;
		}

		for (int32 i = 0; i < numBytes; i++)
			fScratchBuffer[i] = RealCharAt(fromOffset + i);

		result = fScratchBuffer;
	}

	// TODO: this could be improved. We are overwriting what we did some lines
	// ago, we could just avoid to do that.
	if (fPasswordMode) {
		uint32 numChars = UTF8CountChars(result, numBytes);
		uint32 charLen = UTF8CountBytes(B_UTF8_BULLET, 1);
		uint32 newSize = numChars * charLen;

		if ((uint32)fScratchSize < newSize) {
			fScratchBuffer = (char*)realloc(fScratchBuffer, newSize);
			fScratchSize = newSize;
		}
		result = fScratchBuffer;

		char* scratchPtr = fScratchBuffer;
		for (uint32 i = 0; i < numChars; i++) {
			memcpy(scratchPtr, B_UTF8_BULLET, charLen);
			scratchPtr += charLen;
		}

		*_numBytes = newSize;
	}

	return result;
}


/**
 * @brief Searches for the first occurrence of @p inChar in a forward range.
 *
 * Skips UTF-8 continuation bytes.
 *
 * @param inChar     The ASCII byte value to search for.
 * @param fromIndex  Starting byte index for the search.
 * @param ioDelta    In: maximum number of bytes to search. Out: byte offset of
 *                   the match relative to @p fromIndex.
 * @return true if the character was found, false otherwise.
 */
bool
TextGapBuffer::FindChar(char inChar, int32 fromIndex, int32* ioDelta)
{
	int32 numChars = *ioDelta;
	for (int32 i = 0; i < numChars; i++) {
		char realChar = RealCharAt(fromIndex + i);
		if ((realChar & 0xc0) == 0x80)
			continue;
		if (realChar == inChar) {
			*ioDelta = i;
			return true;
		}
	}

	return false;
}


/**
 * @brief Returns a null-terminated pointer to the entire text content.
 *
 * In password mode each UTF-8 character is replaced with the bullet character.
 * The gap is moved to the end before the pointer is constructed.
 *
 * @return Pointer to the full text; valid until the next mutating call.
 */
const char*
TextGapBuffer::Text()
{
	const char* realText = RealText();

	if (fPasswordMode) {
		const uint32 numChars = UTF8CountChars(realText, Length());
		const uint32 bulletCharLen = UTF8CountBytes(B_UTF8_BULLET, 1);
		uint32 newSize = numChars * bulletCharLen + 1;

		if ((uint32)fScratchSize < newSize) {
			fScratchBuffer = (char*)realloc(fScratchBuffer, newSize);
			fScratchSize = newSize;
		}

		char* scratchPtr = fScratchBuffer;
		for (uint32 i = 0; i < numChars; i++) {
			memcpy(scratchPtr, B_UTF8_BULLET, bulletCharLen);
			scratchPtr += bulletCharLen;
		}
		*scratchPtr = '\0';

		return fScratchBuffer;
	}

	return realText;
}


/**
 * @brief Returns the raw (non-password-masked) null-terminated text pointer.
 *
 * Moves the gap to the end so that the pre-gap region is one contiguous span.
 *
 * @return Pointer to the raw text; valid until the next mutating call.
 */
const char*
TextGapBuffer::RealText()
{
	_MoveGapTo(fItemCount);

	if (fGapCount == 0)
		_EnlargeGapTo(kTextGapBufferBlockSize);

	fBuffer[fItemCount] = '\0';
	return fBuffer;
}


/**
 * @brief Copies a substring into a caller-supplied buffer and null-terminates it.
 *
 * If the range is clamped or empty, @p buffer[0] is set to '\\0'.
 *
 * @param offset  First byte of the range.
 * @param length  Number of bytes to copy.
 * @param buffer  Destination buffer; must be at least @p length + 1 bytes.
 */
void
TextGapBuffer::GetString(int32 offset, int32 length, char* buffer)
{
	if (buffer == NULL)
		return;

	int32 textLen = Length();

	if (offset < 0 || offset > (textLen - 1) || length < 1) {
		buffer[0] = '\0';
		return;
	}

	length = ((offset + length) > textLen) ? textLen - offset : length;

	bool isStartBeforeGap = (offset < fGapIndex);
	bool isEndBeforeGap = ((offset + length - 1) < fGapIndex);

	if (isStartBeforeGap == isEndBeforeGap) {
		char* source = fBuffer + offset;
		if (!isStartBeforeGap)
			source += fGapCount;

		memcpy(buffer, source, length);

	} else {
		// if we are here, it can only be that start is before gap,
		// and the end is after gap.

		int32 beforeLen = fGapIndex - offset;
		int32 afterLen = length - beforeLen;

		memcpy(buffer, fBuffer + offset, beforeLen);
		memcpy(buffer + beforeLen, fBuffer + fGapIndex + fGapCount, afterLen);

	}

	buffer[length] = '\0';
}


/**
 * @brief Returns whether password masking is active.
 *
 * @return true if all characters are displayed as bullet symbols.
 */
bool
TextGapBuffer::PasswordMode() const
{
	return fPasswordMode;
}


/**
 * @brief Enables or disables password masking.
 *
 * @param state true to enable masking, false to show real text.
 */
void
TextGapBuffer::SetPasswordMode(bool state)
{
	fPasswordMode = state;
}


/**
 * @brief Moves the gap so that it starts at @p toIndex.
 *
 * Copies the bytes between the current gap position and @p toIndex to close
 * the gap on one side and re-open it on the other.
 *
 * @param toIndex Target byte index for the start of the gap; must be in
 *                [0, fItemCount].
 */
void
TextGapBuffer::_MoveGapTo(int32 toIndex)
{
	if (toIndex == fGapIndex)
		return;
	if (toIndex > fItemCount) {
		debugger("MoveGapTo: invalid toIndex supplied");
		return;
	}

	int32 srcIndex = 0;
	int32 dstIndex = 0;
	int32 count = 0;
	if (toIndex > fGapIndex) {
		srcIndex = fGapIndex + fGapCount;
		dstIndex = fGapIndex;
		count = toIndex - fGapIndex;
	} else {
		srcIndex = toIndex;
		dstIndex = toIndex + fGapCount;
		count = fGapIndex- toIndex;
	}

	if (count > 0)
		memmove(fBuffer + dstIndex, fBuffer + srcIndex, count);

	fGapIndex = toIndex;
}


/**
 * @brief Enlarges the gap to at least @p inCount bytes.
 *
 * Reallocates fBuffer and moves the after-gap region to maintain buffer
 * consistency.
 *
 * @param inCount New gap size in bytes; must be > fGapCount.
 */
void
TextGapBuffer::_EnlargeGapTo(int32 inCount)
{
	if (inCount == fGapCount)
		return;

	fBuffer = (char*)realloc(fBuffer, fItemCount + inCount);
	memmove(fBuffer + fGapIndex + inCount, fBuffer + fGapIndex + fGapCount,
		fBufferCount - (fGapIndex + fGapCount));

	fGapCount = inCount;
	fBufferCount = fItemCount + fGapCount;
}


/**
 * @brief Shrinks the gap to @p inCount bytes to reclaim memory.
 *
 * Moves the after-gap region closer to the end of the "before" region, then
 * reallocates fBuffer to the smaller size.
 *
 * @param inCount New gap size in bytes; must be < fGapCount.
 */
void
TextGapBuffer::_ShrinkGapTo(int32 inCount)
{
	if (inCount == fGapCount)
		return;

	memmove(fBuffer + fGapIndex + inCount, fBuffer + fGapIndex + fGapCount,
		fBufferCount - (fGapIndex + fGapCount));
	fBuffer = (char*)realloc(fBuffer, fItemCount + inCount);

	fGapCount = inCount;
	fBufferCount = fItemCount + fGapCount;
}


} // namespace BPrivate
