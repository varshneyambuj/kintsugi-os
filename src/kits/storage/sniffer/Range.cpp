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
 * @file Range.cpp
 * @brief MIME sniffer byte-range implementation.
 *
 * Implements the Range class, which represents a contiguous interval of byte
 * offsets [start, end] within a data stream. Ranges are used by PatternList and
 * RPattern to constrain where in the stream patterns are allowed to match.
 * A Range is considered invalid if start > end.
 *
 * @see BPrivate::Storage::Sniffer::PatternList
 * @see BPrivate::Storage::Sniffer::RPattern
 */

#include <sniffer/Err.h>
#include <sniffer/Range.h>
#include <sniffer/Parser.h>
#include <stdio.h>

using namespace BPrivate::Storage::Sniffer;

/**
 * @brief Constructs a Range with the given start and end byte offsets.
 *
 * @param start The first byte offset (inclusive) of the range.
 * @param end   The last byte offset (inclusive) of the range.
 */
Range::Range(int32 start, int32 end)
	: fStart(-1)
	, fEnd(-1)
	, fCStatus(B_NO_INIT)
{
	SetTo(start, end);
}

/**
 * @brief Returns the initialization status of this Range.
 *
 * @return B_OK if start <= end, or B_BAD_VALUE if the range is inverted.
 */
status_t
Range::InitCheck() const {
	return fCStatus;
}

/**
 * @brief Returns a heap-allocated error describing an invalid range, or NULL if valid.
 *
 * @return A new Err with a formatted message showing [start:end], or NULL if B_OK.
 */
Err*
Range::GetErr() const {
	if (fCStatus == B_OK)
		return NULL;
	else {
		char start_str[32];
		char end_str[32];
		sprintf(start_str, "%" B_PRId32, fStart);
		sprintf(end_str, "%" B_PRId32, fEnd);
		return new Err(std::string("Sniffer Parser Error -- Invalid range: [") + start_str + ":" + end_str + "]", -1);
	}
}

/**
 * @brief Returns the start offset of the range.
 *
 * @return The first byte offset (inclusive).
 */
int32
Range::Start() const {
	return fStart;
}

/**
 * @brief Returns the end offset of the range.
 *
 * @return The last byte offset (inclusive).
 */
int32
Range::End() const {
	return fEnd;
}

/**
 * @brief Sets the range to the given start and end offsets.
 *
 * Updates the internal status to B_OK if start <= end, or B_BAD_VALUE otherwise.
 *
 * @param start The first byte offset (inclusive) of the range.
 * @param end   The last byte offset (inclusive) of the range.
 */
void
Range::SetTo(int32 start, int32 end) {
		fStart = start;
		fEnd = end;
	if (start > end) {
		fCStatus = B_BAD_VALUE;
	} else {
		fCStatus = B_OK;
	}
}
