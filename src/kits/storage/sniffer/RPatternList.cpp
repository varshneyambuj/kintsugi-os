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
 * @file RPatternList.cpp
 * @brief MIME sniffer ranged-pattern list implementation.
 *
 * Implements the RPatternList class, which holds a collection of RPattern objects
 * where each RPattern carries its own individual Range. Sniffing succeeds if any
 * one of the contained RPatterns matches within its respective range. This is the
 * ranged-disjunction form of a DisjList, complementing the fixed-range PatternList.
 *
 * @see BPrivate::Storage::Sniffer::RPattern
 * @see BPrivate::Storage::Sniffer::DisjList
 */

#include <sniffer/Err.h>
#include <sniffer/RPattern.h>
#include <sniffer/RPatternList.h>
#include <DataIO.h>
#include <stdio.h>

using namespace BPrivate::Storage::Sniffer;

/**
 * @brief Constructs an empty RPatternList with case-sensitive matching.
 */
RPatternList::RPatternList()
	: DisjList()
{
}

/**
 * @brief Destroys the RPatternList and all RPattern objects it owns.
 */
RPatternList::~RPatternList() {
	// Clear our rpattern list
	std::vector<RPattern*>::iterator i;
	for (i = fList.begin(); i != fList.end(); i++)
		delete *i;
}

/**
 * @brief Returns the initialization status of this RPatternList.
 *
 * An RPatternList is always considered valid; individual RPattern validity is
 * checked during sniffing.
 *
 * @return Always returns B_OK.
 */
status_t
RPatternList::InitCheck() const {
	return B_OK;
}

/**
 * @brief Returns NULL because an RPatternList itself is always valid.
 *
 * @return Always returns NULL.
 */
Err*
RPatternList::GetErr() const {
	return NULL;
}

/**
 * @brief Sniffs the given data stream, searching for a match with any of the list's patterns.
 *
 * Each RPattern is tested over its own individual range. Returns true as soon as
 * one RPattern matches. The case-insensitivity flag is forwarded to each RPattern.
 *
 * @param data The data stream to sniff.
 * @return true if at least one RPattern matches, false otherwise.
 */
bool
RPatternList::Sniff(BPositionIO *data) const {
	if (InitCheck() != B_OK)
		return false;
	else {
		bool result = false;
		std::vector<RPattern*>::const_iterator i;
		for (i = fList.begin(); i != fList.end(); i++) {
			if (*i)
				result |= (*i)->Sniff(data, fCaseInsensitive);
		}
		return result;
	}
}

/**
 * @brief Returns the number of bytes needed to perform a complete sniff.
 *
 * Returns the largest BytesNeeded() value among all contained RPatterns, since
 * any one of them may match and each has a different range.
 *
 * @return The maximum bytes needed across all RPatterns, or a negative error code.
 */
ssize_t
RPatternList::BytesNeeded() const
{
	ssize_t result = InitCheck();

	// Tally up the BytesNeeded() values for all the RPatterns and return the largest.
	if (result == B_OK) {
		result = 0; // Just to be safe...
		std::vector<RPattern*>::const_iterator i;
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
	return result;
}

/**
 * @brief Appends an RPattern to the list.
 *
 * The RPatternList takes ownership of the supplied RPattern pointer.
 *
 * @param rpattern The RPattern to add; ignored if NULL.
 */
void
RPatternList::Add(RPattern *rpattern) {
	if (rpattern)
		fList.push_back(rpattern);
}
