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
 * @file ArrayIndexPath.cpp
 * @brief Sequence of integer indices identifying a position inside a multi-dimensional array.
 *
 * Stores a list of int64 indices and provides string conversion using ';'
 * as the separator (e.g. "0;3;7" for a[0][3][7]).
 */


#include "ArrayIndexPath.h"

#include <stdlib.h>

#include <String.h>


/** @brief Separator character used in textual array-index paths. */
static const char kIndexSeparator = ';';


/** @brief Construct an empty index path. */
ArrayIndexPath::ArrayIndexPath()
{
}


/**
 * @brief Copy-construct from another path.
 *
 * @param other  Source path.
 */
ArrayIndexPath::ArrayIndexPath(const ArrayIndexPath& other)
	:
	fIndices(other.fIndices)
{
}


/** @brief Destructor; the index array frees itself. */
ArrayIndexPath::~ArrayIndexPath()
{
}


/**
 * @brief Replace the path by parsing a ';'-separated list of integers.
 *
 * @param path  Textual path; NULL or empty leaves the result empty.
 * @retval B_OK         Parsed.
 * @retval B_BAD_VALUE  Token was not a valid integer or separator was missing.
 * @retval B_NO_MEMORY  Allocation failed.
 */
status_t
ArrayIndexPath::SetTo(const char* path)
{
	fIndices.Clear();

	if (path == NULL)
		return B_OK;

	while (*path != '\0') {
		char* numberEnd;
		int64 index = strtoll(path, &numberEnd, 0);
		if (numberEnd == path)
			return B_BAD_VALUE;
		path = numberEnd;

		if (!fIndices.Add(index))
			return B_NO_MEMORY;

		if (*path == '\0')
			break;

		if (*path != kIndexSeparator)
			return B_BAD_VALUE;
		path++;
	}

	return B_OK;
}


/** @brief Drop all indices. */
void
ArrayIndexPath::Clear()
{
	fIndices.Clear();
}


/**
 * @brief Render the path back into its textual form (e.g. "0;3;7").
 *
 * @param path  Output BString. Truncated and rewritten.
 * @return true on success, false on allocation failure.
 */
bool
ArrayIndexPath::GetPathString(BString& path) const
{
	path.Truncate(0);

	int32 count = CountIndices();
	for (int32 i = 0; i < count; i++) {
		// append separator for all but the first index
		if (i > 0) {
			int32 oldLength = path.Length();
			if (path.Append(kIndexSeparator, 1).Length() != oldLength + 1)
				return false;
		}

		// append index
		int32 oldLength = path.Length();
		if ((path << IndexAt(i)).Length() == oldLength)
			return false;
	}

	return true;
}


/**
 * @brief Copy-assign from another path.
 *
 * @param other  Source path.
 * @return Reference to *this.
 */
ArrayIndexPath&
ArrayIndexPath::operator=(const ArrayIndexPath& other)
{
	fIndices = other.fIndices;
	return *this;
}
