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
 * @file DisjList.cpp
 * @brief MIME sniffer disjunction list base class implementation.
 *
 * Implements the DisjList abstract base class, which represents a disjunction
 * (logical OR) of patterns in a MIME sniffer rule. Subclasses such as
 * PatternList and RPatternList hold the actual pattern collections and provide
 * concrete Sniff() and BytesNeeded() implementations. DisjList itself manages
 * the shared case-insensitivity flag.
 *
 * @see BPrivate::Storage::Sniffer::PatternList
 * @see BPrivate::Storage::Sniffer::RPatternList
 */

#include <sniffer/DisjList.h>

using namespace BPrivate::Storage::Sniffer;

/**
 * @brief Constructs a DisjList with case-sensitive matching enabled by default.
 */
DisjList::DisjList()
	: fCaseInsensitive(false)
{
}

/**
 * @brief Destroys the DisjList.
 */
DisjList::~DisjList() {
}

/**
 * @brief Sets whether pattern matching should be case-insensitive.
 *
 * @param how true to enable case-insensitive matching, false for case-sensitive.
 */
void
DisjList::SetCaseInsensitive(bool how) {
	fCaseInsensitive = how;
}

/**
 * @brief Returns whether this list performs case-insensitive matching.
 *
 * @return true if case-insensitive matching is active, false otherwise.
 */
bool
DisjList::IsCaseInsensitive() {
	return fCaseInsensitive;
}
