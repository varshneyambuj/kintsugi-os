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
 *   Copyright Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file Pattern.cpp
 * @brief MIME sniffer byte-pattern matching implementation.
 *
 * Implements the Pattern class, which stores a byte string and an optional bitmask
 * and searches for masked matches within a BPositionIO data stream over a given
 * Range. Supports both case-sensitive and case-insensitive comparisons. Patterns
 * are the leaf-level matching primitives used by PatternList and RPattern.
 *
 * @see BPrivate::Storage::Sniffer::PatternList
 * @see BPrivate::Storage::Sniffer::RPattern
 */

#include <sniffer/Err.h>
#include <sniffer/Pattern.h>
#include <DataIO.h>
#include <stdio.h>	// for SEEK_* defines
#include <new>

#include <AutoDeleter.h>

using namespace BPrivate::Storage::Sniffer;

/**
 * @brief Constructs a Pattern with an explicit byte string and bitmask.
 *
 * @param string The byte string to match against the data stream.
 * @param mask   A bitmask of the same length applied before comparison.
 */
Pattern::Pattern(const std::string &string, const std::string &mask)
	: fCStatus(B_NO_INIT)
	, fErrorMessage(NULL)
{
	SetTo(string, mask);
}

/**
 * @brief Constructs a Pattern from a byte string, using an all-bits-set mask.
 *
 * Automatically builds a mask of 0xFF bytes with the same length as the string.
 *
 * @param string The byte string to match against the data stream.
 */
Pattern::Pattern(const std::string &string)
	: fCStatus(B_NO_INIT)
	, fErrorMessage(NULL)
{
	// Build a mask with all bits turned on of the
	// appropriate length
	std::string mask = "";
	for (uint i = 0; i < string.length(); i++)
		mask += (char)0xFF;
	SetTo(string, mask);
}

/**
 * @brief Destroys the Pattern and releases the error message if present.
 */
Pattern::~Pattern() {
	delete fErrorMessage;
}

/**
 * @brief Returns the initialization status of this Pattern.
 *
 * @return B_OK if the pattern is valid, or an error code if initialization failed.
 */
status_t
Pattern::InitCheck() const {
	return fCStatus;
}

/**
 * @brief Returns a heap-allocated copy of the stored error, or NULL if the pattern is valid.
 *
 * @return A new Err object describing the initialization failure, or NULL if B_OK.
 */
Err*
Pattern::GetErr() const {
	if (fCStatus == B_OK)
		return NULL;
	else
		return new(std::nothrow) Err(*fErrorMessage);
}

/**
 * @brief Debug helper that prints the hex values of a string to stdout.
 *
 * @param string The string whose bytes are to be printed.
 * @param label  An optional label prefix printed before the hex dump.
 */
void dumpStr(const std::string &string, const char *label = NULL) {
	if (label)
		printf("%s: ", label);
	for (uint i = 0; i < string.length(); i++)
		printf("%x ", string[i]);
	printf("\n");
}

/**
 * @brief Initializes the pattern with the given byte string and bitmask.
 *
 * Validates that neither the string is empty nor the string and mask lengths
 * differ. Sets the internal status and error message accordingly.
 *
 * @param string The byte string to match.
 * @param mask   The bitmask to apply; must be the same length as string.
 * @return B_OK on success, B_BAD_VALUE if the string is empty or lengths differ.
 */
status_t
Pattern::SetTo(const std::string &string, const std::string &mask) {
	fString = string;
	if (fString.length() == 0) {
		SetStatus(B_BAD_VALUE, "Sniffer pattern error: illegal empty pattern");
	} else {
		fMask = mask;
//		dumpStr(string, "data");
//		dumpStr(mask, "mask");
		if (fString.length() != fMask.length()) {
			SetStatus(B_BAD_VALUE, "Sniffer pattern error: pattern and mask lengths do not match");
		} else {
			SetStatus(B_OK);
		}
	}
	return fCStatus;
}

/**
 * @brief Searches for a pattern match in the data stream over the given range.
 *
 * Iterates over each offset from range.Start() to range.End() (clamped to the
 * stream size) and returns true as soon as a match is found.
 *
 * @param range           The byte range within the stream to search.
 * @param data            The data stream to search in.
 * @param caseInsensitive If true, alphabetic characters are compared case-insensitively.
 * @return true if a match is found at any offset within the range, false otherwise.
 */
bool
Pattern::Sniff(Range range, BPositionIO *data, bool caseInsensitive) const {
	int32 start = range.Start();
	int32 end = range.End();
	off_t size = data->Seek(0, SEEK_END);
	if (end >= size)
		end = size-1;	// Don't bother searching beyond the end of the stream
	for (int i = start; i <= end; i++) {
		if (Sniff(i, size, data, caseInsensitive))
			return true;
	}
	return false;
}

/**
 * @brief Returns the number of bytes needed to perform a complete sniff.
 *
 * @return The length of the pattern string on success, or a negative error code.
 */
ssize_t
Pattern::BytesNeeded() const
{
	ssize_t result = InitCheck();
	if (result == B_OK)
		result = fString.length();
	return result;
}

//#define OPTIMIZATION_IS_FOR_CHUMPS
#if OPTIMIZATION_IS_FOR_CHUMPS
/**
 * @brief Checks for a pattern match at a specific offset in the data stream.
 *
 * Reads exactly fString.length() bytes starting at @p start and compares them
 * against the stored pattern and mask. This path uses nothrow (without std::).
 *
 * @param start           The byte offset in the stream to compare against.
 * @param size            Total size of the data stream (unused in this path).
 * @param data            The data stream to read from.
 * @param caseInsensitive If true, alphabetic bytes are compared case-insensitively.
 * @return true if the masked bytes at @p start match the pattern, false otherwise.
 */
bool
Pattern::Sniff(off_t start, off_t size, BPositionIO *data, bool caseInsensitive) const {
	off_t len = fString.length();
	char *buffer = new(nothrow) char[len+1];
	if (buffer) {
		ArrayDeleter<char> _(buffer);
		ssize_t bytesRead = data->ReadAt(start, buffer, len);
		// \todo If there are fewer bytes left in the data stream
		// from the given position than the length of our data
		// string, should we just return false (which is what we're
		// doing now), or should we compare as many bytes as we
		// can and return true if those match?
		if (bytesRead < len)
			return false;
		else {
			bool result = true;
			if (caseInsensitive) {
				for (int i = 0; i < len; i++) {
					char secondChar;
					if ('A' <= fString[i] && fString[i] <= 'Z')
						secondChar = 'a' + (fString[i] - 'A');	// Also check lowercase
					else if ('a' <= fString[i] && fString[i] <= 'z')
						secondChar = 'A' + (fString[i] - 'a');	// Also check uppercase
					else
						secondChar = fString[i]; // Check the same char twice as punishment for doing a case insensitive search ;-)
					if (((fString[i] & fMask[i]) != (buffer[i] & fMask[i]))
					     && ((secondChar & fMask[i]) != (buffer[i] & fMask[i])))
					{
						result = false;
						break;
					}
				}
			} else {
				for (int i = 0; i < len; i++) {
					if ((fString[i] & fMask[i]) != (buffer[i] & fMask[i])) {
						result = false;
						break;
					}
				}
			}
			return result;
		}
	} else
		return false;
}
#else
/**
 * @brief Checks for a pattern match at a specific offset in the data stream.
 *
 * Reads exactly fString.length() bytes starting at @p start and compares them
 * against the stored pattern and mask, applying case folding when requested.
 *
 * @param start           The byte offset in the stream to compare against.
 * @param size            Total size of the data stream (unused directly).
 * @param data            The data stream to read from.
 * @param caseInsensitive If true, alphabetic bytes are compared case-insensitively.
 * @return true if the masked bytes at @p start match the pattern, false otherwise.
 */
bool
Pattern::Sniff(off_t start, off_t size, BPositionIO *data, bool caseInsensitive) const {
	off_t len = fString.length();
	char *buffer = new(std::nothrow) char[len+1];
	if (buffer) {
		ArrayDeleter<char> _(buffer);
		ssize_t bytesRead = data->ReadAt(start, buffer, len);
		// \todo If there are fewer bytes left in the data stream
		// from the given position than the length of our data
		// string, should we just return false (which is what we're
		// doing now), or should we compare as many bytes as we
		// can and return true if those match?
		if (bytesRead < len)
			return false;
		else {
			bool result = true;
			if (caseInsensitive) {
				for (int i = 0; i < len; i++) {
					char secondChar;
					if ('A' <= fString[i] && fString[i] <= 'Z')
						secondChar = 'a' + (fString[i] - 'A');	// Also check lowercase
					else if ('a' <= fString[i] && fString[i] <= 'z')
						secondChar = 'A' + (fString[i] - 'a');	// Also check uppercase
					else
						secondChar = fString[i]; // Check the same char twice as punishment for doing a case insensitive search ;-)
					if (((fString[i] & fMask[i]) != (buffer[i] & fMask[i]))
					     && ((secondChar & fMask[i]) != (buffer[i] & fMask[i])))
					{
						result = false;
						break;
					}
				}
			} else {
				for (int i = 0; i < len; i++) {
					if ((fString[i] & fMask[i]) != (buffer[i] & fMask[i])) {
						result = false;
						break;
					}
				}
			}
			return result;
		}
	} else
		return false;
}
#endif

/**
 * @brief Updates the initialization status and optionally stores an error message.
 *
 * When status is B_OK the stored error message is cleared; otherwise the supplied
 * message (or a fallback) is recorded for later retrieval via GetErr().
 *
 * @param status The new status code to store.
 * @param msg    A descriptive error message; ignored when status is B_OK.
 */
void
Pattern::SetStatus(status_t status, const char *msg) {
	fCStatus = status;
	if (status == B_OK)
		SetErrorMessage(NULL);
	else {
		if (msg)
			SetErrorMessage(msg);
		else {
			SetErrorMessage("Sniffer parser error: Pattern::SetStatus() -- NULL msg with non-B_OK status.\n"
				"(This is officially the most helpful error message you will ever receive ;-)");
		}
	}
}

/**
 * @brief Stores an Err object constructed from the given message, or clears it.
 *
 * @param msg A null-terminated error message string, or NULL to clear the stored error.
 */
void
Pattern::SetErrorMessage(const char *msg) {
	delete fErrorMessage;
	fErrorMessage = (msg) ? (new(std::nothrow) Err(msg, -1)) : (NULL);
}
