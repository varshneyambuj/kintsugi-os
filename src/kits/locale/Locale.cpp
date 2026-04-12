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
 *   Copyright 2003, Axel Dörfler, axeld@pinc-software.de.
 *   Copyright 2010-2011, Oliver Tappe, zooey@hirschkaefer.de.
 *   Copyright 2012, John Scipione, jscipione@gmail.com
 *   All rights reserved. Distributed under the terms of the MIT License.
 */


/**
 * @file Locale.cpp
 * @brief Implementation of BLocale, the combined locale descriptor.
 *
 * BLocale aggregates a BLanguage, a BFormattingConventions, and a BCollator
 * into a single thread-safe object. It is the primary entry point for
 * applications that need locale-sensitive string lookup, collation, or
 * formatting convention queries. The static Default() accessor returns the
 * process-wide default locale maintained by BLocaleRoster.
 *
 * @see BLanguage, BFormattingConventions, BCollator, BLocaleRoster
 */


#include <Autolock.h>
#include <Catalog.h>
#include <Locale.h>
#include <LocaleRoster.h>


/**
 * @brief Construct a BLocale with optional explicit language and conventions.
 *
 * If \a conventions or \a language is NULL the corresponding value is copied
 * from the process-wide default locale returned by BLocale::Default().
 *
 * @param language     Language to use, or NULL for the system default.
 * @param conventions  Formatting conventions to use, or NULL for the default.
 */
BLocale::BLocale(const BLanguage* language,
		const BFormattingConventions* conventions)
	:
	fLock("BLocale")
{
	if (conventions != NULL)
		fConventions = *conventions;
	else
		BLocale::Default()->GetFormattingConventions(&fConventions);

	if (language != NULL)
		fLanguage = *language;
	else
		BLocale::Default()->GetLanguage(&fLanguage);
}


/**
 * @brief Copy-construct a BLocale from another instance.
 *
 * @param other  Source BLocale to copy.
 */
BLocale::BLocale(const BLocale& other)
	:
	fConventions(other.fConventions),
	fLanguage(other.fLanguage)
{
}


/**
 * @brief Return a pointer to the process-wide default BLocale.
 *
 * @return Pointer to the default locale; never NULL.
 */
/*static*/ const BLocale*
BLocale::Default()
{
	return BLocaleRoster::Default()->GetDefaultLocale();
}


/**
 * @brief Copy-assign from another BLocale under mutual locks.
 *
 * @param other  Source BLocale to assign from.
 * @return Reference to this BLocale.
 */
BLocale&
BLocale::operator=(const BLocale& other)
{
	if (this == &other)
		return *this;

	BAutolock lock(fLock);
	BAutolock otherLock(other.fLock);
	if (!lock.IsLocked() || !otherLock.IsLocked())
		return *this;

	fConventions = other.fConventions;
	fLanguage = other.fLanguage;

	return *this;
}


/**
 * @brief Destroy the BLocale object.
 */
BLocale::~BLocale()
{
}


/**
 * @brief Copy the locale's BCollator into the caller's object.
 *
 * @param collator  Output BCollator to populate.
 * @return B_OK on success, B_BAD_VALUE if collator is NULL, B_ERROR on lock
 *         failure.
 */
status_t
BLocale::GetCollator(BCollator* collator) const
{
	if (!collator)
		return B_BAD_VALUE;

	BAutolock lock(fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	*collator = fCollator;

	return B_OK;
}


/**
 * @brief Copy the locale's BLanguage into the caller's object.
 *
 * @param language  Output BLanguage to populate.
 * @return B_OK on success, B_BAD_VALUE if language is NULL, B_ERROR on lock
 *         failure.
 */
status_t
BLocale::GetLanguage(BLanguage* language) const
{
	if (!language)
		return B_BAD_VALUE;

	BAutolock lock(fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	*language = fLanguage;

	return B_OK;
}


/**
 * @brief Copy the locale's BFormattingConventions into the caller's object.
 *
 * @param conventions  Output BFormattingConventions to populate.
 * @return B_OK on success, B_BAD_VALUE if conventions is NULL, B_ERROR on
 *         lock failure.
 */
status_t
BLocale::GetFormattingConventions(BFormattingConventions* conventions) const
{
	if (!conventions)
		return B_BAD_VALUE;

	BAutolock lock(fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	*conventions = fConventions;

	return B_OK;
}


/**
 * @brief Look up a locale-kit string constant by numeric identifier.
 *
 * Handles B_CODESET specially (returns "UTF-8") and delegates other IDs to
 * the language object.
 *
 * @param id  Numeric string identifier.
 * @return The string, or "" if the ID is unrecognized.
 */
const char *
BLocale::GetString(uint32 id) const
{
	// Note: this code assumes a certain order of the string bases

	BAutolock lock(fLock);
	if (!lock.IsLocked())
		return "";

	if (id >= B_OTHER_STRINGS_BASE) {
		if (id == B_CODESET)
			return "UTF-8";

		return "";
	}
	return fLanguage.GetString(id);
}


/**
 * @brief Replace the formatting conventions held by this locale.
 *
 * @param conventions  New BFormattingConventions to use.
 */
void
BLocale::SetFormattingConventions(const BFormattingConventions& conventions)
{
	BAutolock lock(fLock);
	if (!lock.IsLocked())
		return;

	fConventions = conventions;
}


/**
 * @brief Replace the collator held by this locale.
 *
 * @param newCollator  New BCollator to use.
 */
void
BLocale::SetCollator(const BCollator& newCollator)
{
	BAutolock lock(fLock);
	if (!lock.IsLocked())
		return;

	fCollator = newCollator;
}


/**
 * @brief Replace the language held by this locale.
 *
 * @param newLanguage  New BLanguage to use.
 */
void
BLocale::SetLanguage(const BLanguage& newLanguage)
{
	BAutolock lock(fLock);
	if (!lock.IsLocked())
		return;

	fLanguage = newLanguage;
}
