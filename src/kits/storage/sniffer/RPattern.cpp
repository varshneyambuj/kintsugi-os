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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file RPattern.cpp
 * @brief MIME sniffer ranged-pattern implementation.
 *
 * Implements the RPattern class, which pairs a Pattern with its own specific
 * Range, allowing different patterns within the same RPatternList to each specify
 * distinct byte-offset intervals for their search. RPattern is the building block
 * of RPatternList, the ranged-disjunction form of MIME sniffer rules.
 *
 * @see BPrivate::Storage::Sniffer::RPatternList
 * @see BPrivate::Storage::Sniffer::Pattern
 */

#include <sniffer/Err.h>
#include <sniffer/Pattern.h>
#include <sniffer/Range.h>
#include <sniffer/RPattern.h>
#include <DataIO.h>

using namespace BPrivate::Storage::Sniffer;

/**
 * @brief Constructs an RPattern associating a range with a pattern.
 *
 * Takes ownership of the supplied Pattern pointer.
 *
 * @param range   The byte range over which the pattern is searched.
 * @param pattern Heap-allocated Pattern to match; ownership is transferred.
 */
RPattern::RPattern(Range range, Pattern *pattern)
	: fRange(range)
	, fPattern(pattern)
{
}

/**
 * @brief Returns the initialization status of this RPattern.
 *
 * Checks in order: the validity of the range, the presence of the pattern pointer,
 * and finally the pattern's own initialization status.
 *
 * @return B_OK if both the range and pattern are valid, or an appropriate error code.
 */
status_t
RPattern::InitCheck() const {
	status_t err = fRange.InitCheck();
	if (!err)
		err = fPattern ? B_OK : B_BAD_VALUE;
	if (!err)
		err = fPattern->InitCheck();
	return err;
}

/**
 * @brief Returns a heap-allocated error describing the first problem found, or NULL if valid.
 *
 * Checks the range first, then the pattern pointer, then the pattern's own error.
 *
 * @return A new Err object, or NULL if this RPattern is fully initialized.
 */
Err*
RPattern::GetErr() const {
	if (fRange.InitCheck() != B_OK)
		return fRange.GetErr();
	else if (fPattern) {
		if (fPattern->InitCheck() != B_OK)
			return fPattern->GetErr();
		else
			return NULL;
	} else
		return new Err("Sniffer parser error: RPattern::RPattern() -- NULL pattern parameter", -1);
}

/**
 * @brief Destroys the RPattern and the Pattern it owns.
 */
RPattern::~RPattern() {
	delete fPattern;
}

/**
 * @brief Sniffs the given data stream over the object's range for the object's pattern.
 *
 * @param data            The data stream to sniff.
 * @param caseInsensitive If true, alphabetic bytes are compared case-insensitively.
 * @return true if the pattern matches somewhere within the range, false otherwise.
 */
bool
RPattern::Sniff(BPositionIO *data, bool caseInsensitive) const {
	if (!data || InitCheck() != B_OK)
		return false;
	else
		return fPattern->Sniff(fRange, data, caseInsensitive);
}

/**
 * @brief Returns the number of bytes needed to perform a complete sniff.
 *
 * Adds the pattern's own BytesNeeded() result to the range end offset to account
 * for the furthest allowed starting position.
 *
 * @return The total bytes needed, or a negative error code if initialization failed.
 */
ssize_t
RPattern::BytesNeeded() const
{
	ssize_t result = InitCheck();
	if (result == B_OK)
		result = fPattern->BytesNeeded();
	if (result >= 0)
		result += fRange.End();
	return result;
}
