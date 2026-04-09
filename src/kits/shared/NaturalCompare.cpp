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
 *   Copyright 2009, Dana Burkart
 *   Copyright 2009, Stephan Aßmus <superstippi@gmx.de>
 *   Copyright 2010, Axel Dörfler, axeld@pinc-software.de
 *   Copyright 2010, Rene Gollent (anevilyak@gmail.com)
 *   Distributed under the terms of the MIT License.
 */

/** @file NaturalCompare.cpp
 *  @brief Implements \c BPrivate::NaturalCompare(), a locale-aware string
 *         comparison function that sorts embedded numbers numerically rather
 *         than lexicographically (e.g. "file9" < "file10").
 */

#include <NaturalCompare.h>

#include <ctype.h>
#include <string.h>
#include <strings.h>

#include <Collator.h>
#include <Locale.h>


namespace BPrivate {


// #pragma mark - Natural sorting


/**
 * @brief Compares two strings naturally, as opposed to lexicographically.
 *
 * Uses a lazily-initialised \c BCollator configured with
 * \c B_COLLATE_SECONDARY strength and numeric sorting enabled, so that
 * embedded digit sequences are compared by value. The collator is derived
 * from the default locale and is shared across calls.
 *
 * @param stringA First string to compare.
 * @param stringB Second string to compare.
 * @return A negative value if \a stringA sorts before \a stringB, zero if
 *         they are equal, or a positive value if \a stringA sorts after
 *         \a stringB.
 */
int
NaturalCompare(const char* stringA, const char* stringB)
{
	static BCollator* collator = NULL;

	if (collator == NULL)
	{
		collator = new BCollator();
		BLocale::Default()->GetCollator(collator);
		collator->SetStrength(B_COLLATE_SECONDARY);
		collator->SetNumericSorting(true);
	}

	return collator->Compare(stringA, stringB);
}


} // namespace BPrivate
