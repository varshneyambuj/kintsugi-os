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

#ifndef CHARACTER_SET_ROSTER_H
#define CHARACTER_SET_ROSTER_H

#include <SupportDefs.h>
#include <Messenger.h>

namespace BPrivate {

/**
 * @file CharacterSetRoster.h
 * @brief Declares BCharacterSetRoster, the registry and iterator for all
 *        supported character encodings.
 *
 * @see BCharacterSet
 */

class BCharacterSet;

/**
 * @class BCharacterSetRoster
 * @brief Registry and iterator for all compiled-in character encodings.
 *
 * Provides two complementary interfaces for accessing the system character-
 * set table:
 *  - An iterator API (GetNextCharacterSet() / RewindCharacterSets()) for
 *    enumerating all available encodings in ascending font-ID order.
 *  - Static lookup functions that locate a specific character set by font
 *    ID, conversion ID, MIB enum, print name, or any registered alias.
 *
 * @see BCharacterSet, convert_to_utf8(), convert_from_utf8()
 */
class BCharacterSetRoster {
public:
	/**
	 * @brief Constructs a roster iterator positioned before the first entry.
	 */
	BCharacterSetRoster();

	virtual ~BCharacterSetRoster();

	/**
	 * @brief Copies the next character set into @p charset and advances the
	 *        iterator.
	 *
	 * @param charset Output parameter filled with the next character set's
	 *                data on success.
	 * @return \c B_NO_ERROR on success; \c B_BAD_VALUE if @p charset is NULL
	 *         or the iterator has reached the end of the table.
	 */
	status_t GetNextCharacterSet(BCharacterSet * charset);

	/**
	 * @brief Resets the iterator to the first character set.
	 *
	 * @return \c B_NO_ERROR on success; \c B_BAD_VALUE if the table is empty.
	 */
	status_t RewindCharacterSets();

	/**
	 * @brief Registers a BMessenger to receive character-set change
	 *        notifications.
	 *
	 * @note Not yet implemented; always returns \c B_ERROR.
	 *
	 * @param target The messenger to register.
	 * @return \c B_NO_ERROR on success; \c B_ERROR if unimplemented or
	 *         @p target is invalid.
	 */
	static status_t StartWatching(BMessenger target);

	/**
	 * @brief Unregisters a BMessenger from character-set change notifications.
	 *
	 * @note Not yet implemented; always returns \c B_ERROR.
	 *
	 * @param target The messenger to unregister.
	 * @return \c B_NO_ERROR on success; \c B_ERROR if unimplemented or
	 *         @p target is invalid.
	 */
	static status_t StopWatching(BMessenger target);

	/**
	 * @brief Returns the character set with the given font ID.
	 *
	 * Executes in O(1) time.
	 *
	 * @param id Font-encoding ID as returned by BCharacterSet::GetFontID().
	 * @return A pointer to the matching BCharacterSet, or NULL if out of
	 *         range.
	 */
	static const BCharacterSet * GetCharacterSetByFontID(uint32 id);

	/**
	 * @brief Returns the character set with the given conversion ID.
	 *
	 * The conversion ID is one less than the corresponding font ID.
	 * Executes in O(1) time.
	 *
	 * @param id Conversion ID as returned by BCharacterSet::GetConversionID().
	 * @return A pointer to the matching BCharacterSet, or NULL if out of
	 *         range.
	 */
	static const BCharacterSet * GetCharacterSetByConversionID(uint32 id);

	/**
	 * @brief Returns the character set registered under the given MIB enum.
	 *
	 * Executes in O(1) time.
	 *
	 * @param MIBenum The IANA MIB enum value.
	 * @return A pointer to the matching BCharacterSet, or NULL if none is
	 *         registered for @p MIBenum.
	 */
	static const BCharacterSet * GetCharacterSetByMIBenum(uint32 MIBenum);

	/**
	 * @brief Finds a character set by its human-readable display name.
	 *
	 * Performs an exact, case-sensitive match. Executes in O(n) time.
	 *
	 * @param name The display name to search for.
	 * @return A pointer to the matching BCharacterSet, or NULL if not found.
	 * @see BCharacterSet::GetPrintName()
	 */
	static const BCharacterSet * FindCharacterSetByPrintName(const char * name);

	/**
	 * @brief Finds a character set by any of its registered names.
	 *
	 * Searches IANA names and MIME names first (case-insensitive), then
	 * aliases.  Executes in O(n) time.
	 *
	 * @param name The name or alias to search for (e.g. "utf-8", "latin1").
	 * @return A pointer to the matching BCharacterSet, or NULL if not found.
	 * @see BCharacterSet::GetName(), BCharacterSet::GetMIMEName(),
	 *      BCharacterSet::AliasAt()
	 */
	static const BCharacterSet * FindCharacterSetByName(const char * name);

private:
	uint32 index; ///< State variable for sequential iteration
};

}

#endif // CHARACTER_SET_ROSTER_H
