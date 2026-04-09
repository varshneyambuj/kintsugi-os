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
 *   Copyright 2003-2008, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *   Authors:
 *       Andrew Bachmann
 */


/**
 * @file CharacterSetRoster.cpp
 * @brief Implementation of BCharacterSetRoster, the registry and iterator
 *        for all supported character encodings.
 *
 * BCharacterSetRoster exposes the compile-time table of character sets
 * (built from the IANA character-sets registry) through two complementary
 * interfaces: an iterator API (GetNextCharacterSet() / RewindCharacterSets())
 * for sequential enumeration, and a set of static lookup functions that
 * find a character set by font ID, conversion ID, MIB enum, print name, or
 * any registered alias/MIME name.
 *
 * @see BCharacterSet, convert_to_utf8(), convert_from_utf8()
 */


#include <string.h>
#include <strings.h>
#include <CharacterSet.h>
#include <CharacterSetRoster.h>
#include "character_sets.h"

namespace BPrivate {


/**
 * @brief Constructs a roster iterator positioned before the first entry.
 *
 * After construction, the first call to GetNextCharacterSet() will return
 * the character set with the lowest font ID.
 */
BCharacterSetRoster::BCharacterSetRoster()
{
	index = 0;
}


/**
 * @brief Destructor.
 */
BCharacterSetRoster::~BCharacterSetRoster()
{
	// nothing to do
}


/**
 * @brief Copies the next character set into @p charset and advances the
 *        iterator.
 *
 * Iterates over all character sets in ascending font-ID order.  The caller
 * should call RewindCharacterSets() before the first call, or simply
 * construct a fresh BCharacterSetRoster.
 *
 * @param charset Output parameter; on success the pointed-to object is
 *                overwritten with the next character set's data.
 * @return \c B_NO_ERROR on success; \c B_BAD_VALUE if @p charset is NULL
 *         or the iterator has reached the end of the table.
 */
status_t
BCharacterSetRoster::GetNextCharacterSet(BCharacterSet * charset)
{
	if (charset == 0) {
		return B_BAD_VALUE;
	}
	if (index >= character_sets_by_id_count) {
		return B_BAD_VALUE;
	}
	*charset = *character_sets_by_id[index++];
	return B_NO_ERROR;
}


/**
 * @brief Resets the iterator to the first character set.
 *
 * After rewinding, the next call to GetNextCharacterSet() will return the
 * character set with the lowest font ID.
 *
 * @return \c B_NO_ERROR on success; \c B_BAD_VALUE if the character-set
 *         table is empty.
 */
status_t
BCharacterSetRoster::RewindCharacterSets()
{
	index = 0;
	if (index >= character_sets_by_id_count) {
		return B_BAD_VALUE;
	}
	return B_NO_ERROR;
}


/**
 * @brief Registers a BMessenger to receive character-set change notifications.
 *
 * @note Not yet implemented; always returns \c B_ERROR.
 *
 * @param target The messenger to register for notifications.
 * @return \c B_NO_ERROR on success; \c B_ERROR if not implemented or
 *         @p target is invalid.
 */
status_t
BCharacterSetRoster::StartWatching(BMessenger target)
{
	// TODO: implement it
	return B_ERROR;
}


/**
 * @brief Unregisters a BMessenger from receiving character-set change
 *        notifications.
 *
 * @note Not yet implemented; always returns \c B_ERROR.
 *
 * @param target The messenger to unregister.
 * @return \c B_NO_ERROR on success; \c B_ERROR if not implemented or
 *         @p target is invalid.
 */
status_t
BCharacterSetRoster::StopWatching(BMessenger target)
{
	// TODO: implement it
	return B_ERROR;
}


/**
 * @brief Returns the character set whose font ID equals @p id.
 *
 * Executes in O(1) time via a direct table lookup.
 *
 * @param id Font-encoding ID as returned by BCharacterSet::GetFontID().
 * @return A pointer to the matching BCharacterSet, or NULL if @p id is
 *         out of range.
 * @see BCharacterSet::GetFontID()
 */
const BCharacterSet *
BCharacterSetRoster::GetCharacterSetByFontID(uint32 id)
{
	if (id >= character_sets_by_id_count) {
		return NULL;
	}
	return character_sets_by_id[id];
}


/**
 * @brief Returns the character set whose conversion ID equals @p id.
 *
 * The conversion ID is one less than the corresponding font ID.
 * Executes in O(1) time via a direct table lookup.
 *
 * @param id Conversion ID as returned by BCharacterSet::GetConversionID().
 * @return A pointer to the matching BCharacterSet, or NULL if @p id is
 *         out of range.
 * @see BCharacterSet::GetConversionID(), convert_to_utf8(), convert_from_utf8()
 */
const BCharacterSet *
BCharacterSetRoster::GetCharacterSetByConversionID(uint32 id)
{
	if (id + 1 >= character_sets_by_id_count) {
		return NULL;
	}
	return character_sets_by_id[id+1];
}


/**
 * @brief Returns the character set registered under the given MIB enum.
 *
 * MIB enums are assigned by IANA and uniquely identify each registered
 * encoding.  Executes in O(1) time via a direct table lookup.
 *
 * @param MIBenum The IANA MIB enum value.
 * @return A pointer to the matching BCharacterSet, or NULL if no character
 *         set is registered for @p MIBenum.
 * @see http://www.iana.org/assignments/character-sets
 */
const BCharacterSet *
BCharacterSetRoster::GetCharacterSetByMIBenum(uint32 MIBenum)
{
	if (MIBenum > maximum_valid_MIBenum) {
		return NULL;
	}
	return character_sets_by_MIBenum[MIBenum];
}


/**
 * @brief Finds a character set by its human-readable display (print) name.
 *
 * Performs an exact, case-sensitive match against the print names in the
 * table.  Executes in O(n) time.
 *
 * @param name The display name to search for (e.g. "Western European
 *             (ISO-8859-1)").
 * @return A pointer to the first matching BCharacterSet, or NULL if none
 *         is found.
 * @see BCharacterSet::GetPrintName()
 */
const BCharacterSet *
BCharacterSetRoster::FindCharacterSetByPrintName(const char * name)
{
	for (uint id = 0 ; (id < character_sets_by_id_count) ; id++) {
		if (strcmp(character_sets_by_id[id]->GetPrintName(),name) == 0) {
			return character_sets_by_id[id];
		}
	}
	return 0;
}


/**
 * @brief Finds a character set by any of its registered names.
 *
 * Searches in three passes, preferring exact IANA/MIME names over aliases:
 * -# Case-insensitive match against each character set's IANA name.
 * -# Case-insensitive match against each character set's MIME name.
 * -# Case-insensitive match against every registered alias.
 *
 * Only if no match is found in passes 1–2 does the search proceed to
 * aliases, ensuring that standard names always take precedence.
 * Executes in O(n) time.
 *
 * @param name The name or alias to search for (e.g. "utf-8", "latin1",
 *             "windows-1252").
 * @return A pointer to the matching BCharacterSet, or NULL if none is
 *         found under any registered name.
 * @see BCharacterSet::GetName(), BCharacterSet::GetMIMEName(),
 *      BCharacterSet::AliasAt()
 */
const BCharacterSet *
BCharacterSetRoster::FindCharacterSetByName(const char * name)
{
	// first search standard names and mime names
	for (uint id = 0 ; (id < character_sets_by_id_count) ; id++) {
		if (strcasecmp(character_sets_by_id[id]->GetName(),name) == 0) {
			return character_sets_by_id[id];
		}
		const char * mime = character_sets_by_id[id]->GetMIMEName();
		if ((mime != NULL) && (strcasecmp(mime,name) == 0)) {
			return character_sets_by_id[id];
		}
	}
	// only after searching all the above names do we look at aliases
	for (uint id = 0 ; (id < character_sets_by_id_count) ; id++) {
		for (int alias = 0 ; (alias < character_sets_by_id[id]->CountAliases()) ; alias++) {
			if (strcasecmp(character_sets_by_id[id]->AliasAt(alias),name) == 0) {
				return character_sets_by_id[id];
			}
		}
	}
	return 0;
}

}
