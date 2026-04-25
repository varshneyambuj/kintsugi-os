/*
 * Copyright 2026, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2011, Haiku.
 * Original author: Oliver Tappe <zooey@hirschkaefer.de>.
 */

/** @file HashableString.h
    @brief BString subclass that caches its hash code for fast lookups in hash tables. */

#ifndef _PACKAGE__PRIVATE__HASHABLE_STRING_H_
#define _PACKAGE__PRIVATE__HASHABLE_STRING_H_


#include <String.h>

#include <HashString.h>


namespace BPackageKit {

namespace BPrivate {


/**
 * @brief BString subclass that precomputes and caches a hash code so it can be
 *        used as a key in BPackageKit hash containers.
 */
class HashableString : public BString {
public:
	inline						HashableString();

	inline						HashableString(const BString& string);

	/** @brief Returns the cached hash code computed at construction. */
	inline	uint32				GetHashCode() const;

	/** @brief Inequality test that compares both string content and hash code. */
	inline	bool				operator!= (const HashableString& other) const;

private:
			uint32				fHashCode;
};


inline
HashableString::HashableString()
	:
	fHashCode(0)
{
}


inline
HashableString::HashableString(const BString& string)
	:
	BString(string),
	fHashCode(string_hash(String()))
{
}


inline uint32
HashableString::GetHashCode() const
{
	return fHashCode;
}


inline bool
HashableString::operator!= (const HashableString& other) const
{
	return Compare(other) != 0 || fHashCode != other.fHashCode;
}


}	// namespace BPrivate

}	// namespace BPackageKit


#endif // _PACKAGE__PRIVATE__HASHABLE_STRING_H_
