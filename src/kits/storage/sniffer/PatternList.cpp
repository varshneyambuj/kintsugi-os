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
 * @file PatternList.cpp
 * @brief MIME sniffer pattern list implementation.
 *
 * Implements the PatternList class, which holds a collection of Pattern objects
 * all searched over the same fixed byte Range within a data stream. Sniffing
 * succeeds if any one of the contained patterns matches. PatternList is one of
 * the two concrete DisjList subclasses produced by the parser.
 *
 * @see BPrivate::Storage::Sniffer::Pattern
 * @see BPrivate::Storage::Sniffer::DisjList
 */

#include <sniffer/Err.h>
#include <sniffer/Pattern.h>
#include <sniffer/PatternList.h>
#include <DataIO.h>
#include <stdio.h>

using namespace BPrivate::Storage::Sniffer;

/**
 * @brief Constructs a PatternList that searches over the given byte range.
 *
 * @param range The byte range within the data stream that all patterns are checked against.
 */
PatternList::PatternList(Range range)
	: DisjList()
	, fRange(range)
{
}

/**
 * @brief Destroys the PatternList and all Pattern objects it owns.
 */
PatternList::~PatternList() {
	// Clean up
	std::vector<Pattern*>::iterator i;
	for (i = fList.begin(); i != fList.end(); i++)
		delete *i;
}

/**
 * @brief Returns the initialization status, which reflects the validity of the range.
 *
 * @return B_OK if the stored range is valid, or an error code otherwise.
 */
status_t
PatternList::InitCheck() const {
	return fRange.InitCheck();
}

/**
 * @brief Returns a heap-allocated error describing the range problem, or NULL if valid.
 *
 * @return A new Err object if the range is invalid, or NULL if B_OK.
 */
Err*
PatternList::GetErr() const {
	return fRange.GetErr();
}

/**
 * @brief Sniffs the given data stream, searching for a match with any of the list's patterns.
 *
 * Each pattern in the list is tested over the stored range. Returns true as soon
 * as one pattern matches; returns false if none match or if the list is invalid.
 *
 * @param data The data stream to sniff.
 * @return true if at least one pattern matches, false otherwise.
 */
bool
PatternList::Sniff(BPositionIO *data) const {
	if (InitCheck() != B_OK)
		return false;
	else {
		bool result = false;
		std::vector<Pattern*>::const_iterator i;
		for (i = fList.begin(); i != fList.end(); i++) {
			if (*i)
				result |= (*i)->Sniff(fRange, data, fCaseInsensitive);
		}
		return result;
	}
}

/**
 * @brief Returns the number of bytes needed to perform a complete sniff.
 *
 * Computes the maximum of all contained patterns' BytesNeeded() values and adds
 * the range end offset to account for the furthest allowed starting position.
 *
 * @return The total bytes needed, or a negative error code if initialization failed.
 */
ssize_t
PatternList::BytesNeeded() const
{
	ssize_t result = InitCheck();

	// Find the number of bytes needed to sniff any of our
	// patterns from a single location in a data stream
	if (result == B_OK) {
		result = 0;	// I realize it already *is* zero if it == B_OK, but just in case that changes...
		std::vector<Pattern*>::const_iterator i;
		for (i = fList.begin(); i != fList.end(); i++) {
			if (*i) {
				ssize_t bytes = (*i)->BytesNeeded();
				if (bytes >= 0) {
					if (bytes > result)
						result = bytes;
				} else {
					result = bytes;
					break;
				}
			}
		}
	}

	// Now add on the number of bytes needed to get to the
	// furthest allowed starting point
	if (result >= 0)
		result += fRange.End();

	return result;
}

/**
 * @brief Appends a Pattern to the list.
 *
 * The PatternList takes ownership of the supplied Pattern pointer.
 *
 * @param pattern The Pattern to add; ignored if NULL.
 */
void
PatternList::Add(Pattern *pattern) {
	if (pattern)
		fList.push_back(pattern);
}
