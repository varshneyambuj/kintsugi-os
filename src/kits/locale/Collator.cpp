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
 *   Copyright 2003, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 *   Copyright 2010, Adrien Destugues <pulkomandy@pulkomandy.ath.cx>
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Collator.cpp
 * @brief Implementation of BCollator, the locale-aware string comparison class.
 *
 * BCollator wraps an ICU RuleBasedCollator to provide locale-sensitive string
 * ordering and sort-key generation. Collation strength can be tuned from
 * B_COLLATE_PRIMARY (ignores accents and case) through B_COLLATE_IDENTICAL.
 * The collator supports archiving and unarchiving via BMessage so it can be
 * persisted and restored.
 *
 * @see BLanguage, BLocale
 */


#include <unicode/uversion.h>
#include <Collator.h>

#include <ctype.h>
#include <stdlib.h>

#include <new>
#include <typeinfo>

#include <UnicodeChar.h>
#include <String.h>
#include <Message.h>

#include <unicode/coll.h>
#include <unicode/tblcoll.h>


U_NAMESPACE_USE


/**
 * @brief Construct a BCollator using the default ICU locale and tertiary strength.
 *
 * Punctuation is ignored by default. The collation strength is set to
 * B_COLLATE_TERTIARY, which distinguishes base letters, accents, and case.
 */
BCollator::BCollator()
	:
	fIgnorePunctuation(true)
{
	// TODO: the collator construction will have to change; the default
	//	collator should be constructed by the Locale/LocaleRoster, so we
	//	only need a constructor where you specify all details

	UErrorCode error = U_ZERO_ERROR;
	fICUCollator = Collator::createInstance(error);
	SetStrength(B_COLLATE_TERTIARY);
}


/**
 * @brief Construct a BCollator for the specified ICU locale and strength.
 *
 * @param locale              ICU locale name string (e.g. "de_DE").
 * @param strength            Collation strength constant (B_COLLATE_*).
 * @param ignorePunctuation   If true, punctuation characters are skipped
 *                            during comparisons.
 */
BCollator::BCollator(const char* locale, int8 strength, bool ignorePunctuation)
	:
	fIgnorePunctuation(ignorePunctuation)
{
	UErrorCode error = U_ZERO_ERROR;
	fICUCollator = Collator::createInstance(locale, error);
	SetStrength(strength);
}


/**
 * @brief Reconstruct a BCollator from a BMessage archive.
 *
 * If the archive contains serialized collator binary data ("loc:collator"),
 * a RuleBasedCollator is rebuilt from it. Otherwise the default ICU collator
 * is used as a fallback.
 *
 * @param archive  BMessage produced by a previous Archive() call.
 */
BCollator::BCollator(BMessage* archive)
	:
	BArchivable(archive),
	fICUCollator(NULL),
	fIgnorePunctuation(true)
{
	archive->FindBool("loc:punctuation", &fIgnorePunctuation);

	UErrorCode error = U_ZERO_ERROR;
	RuleBasedCollator* fallbackICUCollator
		= static_cast<RuleBasedCollator*>(Collator::createInstance(error));

	ssize_t size;
	const void* buffer = NULL;
	if (archive->FindData("loc:collator", B_RAW_TYPE, &buffer, &size) == B_OK) {
		fICUCollator = new RuleBasedCollator((const uint8_t*)buffer, (int)size,
			fallbackICUCollator, error);
		if (fICUCollator == NULL) {
			fICUCollator = fallbackICUCollator;
				// Unarchiving failed, so we revert to the fallback collator
				// TODO: when can this happen, can it be avoided?
		}
	}
}


/**
 * @brief Copy-construct a BCollator by cloning the underlying ICU collator.
 *
 * @param other  The source BCollator to copy.
 */
BCollator::BCollator(const BCollator& other)
	:
	fICUCollator(NULL)
{
	*this = other;
}


/**
 * @brief Destroy the BCollator and free the ICU collator instance.
 */
BCollator::~BCollator()
{
	delete fICUCollator;
}


/**
 * @brief Copy-assign another BCollator, cloning its ICU collator.
 *
 * @param source  The source BCollator to copy from.
 * @return Reference to this BCollator.
 */
BCollator& BCollator::operator=(const BCollator& source)
{
	if (&source != this) {
		delete fICUCollator;

		fICUCollator = source.fICUCollator != NULL
			? source.fICUCollator->clone()
			: NULL;
		fIgnorePunctuation = source.fIgnorePunctuation;
	}

	return *this;
}


/**
 * @brief Set whether punctuation characters are ignored during comparison.
 *
 * @param ignore  true to skip punctuation, false to include it.
 */
void
BCollator::SetIgnorePunctuation(bool ignore)
{
	fIgnorePunctuation = ignore;
}


/**
 * @brief Return whether punctuation is currently ignored in comparisons.
 *
 * @return true if punctuation is ignored, false otherwise.
 */
bool
BCollator::IgnorePunctuation() const
{
	return fIgnorePunctuation;
}


/**
 * @brief Enable or disable numeric (natural) sort order for digit sequences.
 *
 * When enabled, "item2" sorts before "item10" rather than after it.
 *
 * @param enable  true to enable numeric collation, false to disable.
 * @return B_OK on success, B_NO_INIT if no ICU collator is set, B_ERROR
 *         if the ICU call fails.
 */
status_t
BCollator::SetNumericSorting(bool enable)
{
	if (fICUCollator == NULL)
		return B_NO_INIT;

	UErrorCode error = U_ZERO_ERROR;
	fICUCollator->setAttribute(UCOL_NUMERIC_COLLATION,
		enable ? UCOL_ON : UCOL_OFF, error);

	return error == U_ZERO_ERROR ? B_OK : B_ERROR;
}


/**
 * @brief Generate a binary sort key for the given UTF-8 string.
 *
 * Sort keys can be compared with memcmp() to determine collation order without
 * invoking the full collation algorithm on every comparison.
 *
 * @param string  Input UTF-8 string.
 * @param key     Output BString that receives the binary sort key bytes.
 * @return B_OK on success, B_NO_MEMORY if allocation fails, B_ERROR on ICU
 *         failure.
 */
status_t
BCollator::GetSortKey(const char* string, BString* key) const
{
	// TODO : handle fIgnorePunctuation

	int length = strlen(string);

	uint8_t* buffer = (uint8_t*)malloc(length * 2);
		// According to ICU documentation this should be enough in "most cases"
	if (buffer == NULL)
		return B_NO_MEMORY;

	UErrorCode error = U_ZERO_ERROR;
	int requiredSize = fICUCollator->getSortKey(UnicodeString(string, length,
		NULL, error), buffer, length * 2);
	if (requiredSize > length * 2) {
		uint8_t* tmpBuffer = (uint8_t*)realloc(buffer, requiredSize);
		if (tmpBuffer == NULL) {
			free(buffer);
			buffer = NULL;
			return B_NO_MEMORY;
		} else {
			buffer = tmpBuffer;
		}

		error = U_ZERO_ERROR;
		fICUCollator->getSortKey(UnicodeString(string, length, NULL, error),
			buffer,	requiredSize);
	}

	key->SetTo((char*)buffer);
	free(buffer);

	if (error == U_ZERO_ERROR)
		return B_OK;

	return B_ERROR;
}


/**
 * @brief Compare two UTF-8 strings using the current collation rules.
 *
 * Falls back to plain strcmp() when no ICU collator is available.
 *
 * @param s1  First UTF-8 string.
 * @param s2  Second UTF-8 string.
 * @return Negative if s1 < s2, zero if equal, positive if s1 > s2.
 */
int
BCollator::Compare(const char* s1, const char* s2) const
{
	if (fICUCollator == NULL)
		return strcmp(s1, s2);

	// TODO : handle fIgnorePunctuation
	UErrorCode error = U_ZERO_ERROR;
	return fICUCollator->compare(s1, s2, error);
}


/**
 * @brief Flatten this BCollator into a BMessage for persistent storage.
 *
 * Stores the punctuation flag and the raw binary representation of the
 * underlying RuleBasedCollator.
 *
 * @param archive  Output BMessage to write the archive data into.
 * @param deep     Passed to BArchivable::Archive(); unused here.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BCollator::Archive(BMessage* archive, bool deep) const
{
	status_t status = BArchivable::Archive(archive, deep);
	if (status < B_OK)
		return status;

	if (status == B_OK)
		status = archive->AddBool("loc:punctuation", fIgnorePunctuation);

	UErrorCode error = U_ZERO_ERROR;
	int size = static_cast<RuleBasedCollator*>(fICUCollator)->cloneBinary(NULL,
		0, error);
		// This WILL fail with U_BUFFER_OVERFLOW_ERROR. But we get the needed
		// size.
	error = U_ZERO_ERROR;
	uint8_t* buffer = (uint8_t*)malloc(size);
	static_cast<RuleBasedCollator*>(fICUCollator)->cloneBinary(buffer, size,
		error);

	if (status == B_OK && error == U_ZERO_ERROR)
		status = archive->AddData("loc:collator", B_RAW_TYPE, buffer, size);
	free(buffer);

	if (error == U_ZERO_ERROR)
		return status;
	return B_ERROR;
}


/**
 * @brief Instantiate a BCollator from a BMessage archive.
 *
 * @param archive  BMessage previously produced by Archive().
 * @return A newly allocated BCollator on success, or NULL on failure.
 */
BArchivable*
BCollator::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "BCollator"))
		return new(std::nothrow) BCollator(archive);

	return NULL;
}


/**
 * @brief Set the collation strength on the underlying ICU collator.
 *
 * Maps B_COLLATE_* constants to the corresponding ICU ECollationStrength
 * values. B_COLLATE_DEFAULT is treated as B_COLLATE_TERTIARY.
 *
 * @param strength  One of the B_COLLATE_* constants.
 * @return B_OK on success, B_NO_INIT if no ICU collator is available.
 */
status_t
BCollator::SetStrength(int8 strength) const
{
	if (fICUCollator == NULL)
		return B_NO_INIT;

	if (strength == B_COLLATE_DEFAULT)
		strength = B_COLLATE_TERTIARY;

	Collator::ECollationStrength icuStrength;
	switch (strength) {
		case B_COLLATE_PRIMARY:
			icuStrength = Collator::PRIMARY;
			break;
		case B_COLLATE_SECONDARY:
			icuStrength = Collator::SECONDARY;
			break;
		case B_COLLATE_TERTIARY:
		default:
			icuStrength = Collator::TERTIARY;
			break;
		case B_COLLATE_QUATERNARY:
			icuStrength = Collator::QUATERNARY;
			break;
		case B_COLLATE_IDENTICAL:
			icuStrength = Collator::IDENTICAL;
			break;
	}
	fICUCollator->setStrength(icuStrength);

	return B_OK;
}
