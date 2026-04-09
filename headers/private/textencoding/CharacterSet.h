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

#ifndef CHARACTER_SET_H
#define CHARACTER_SET_H

#include <SupportDefs.h>

namespace BPrivate {

/**
 * @file CharacterSet.h
 * @brief Declares BCharacterSet, which describes a single IANA-registered
 *        character encoding.
 *
 * @see http://www.iana.org/assignments/character-sets
 * @see BCharacterSetRoster
 */

/**
 * @class BCharacterSet
 * @brief Metadata container for one IANA-registered character encoding.
 *
 * Holds the numeric IDs, canonical names, MIME name, and alias list for a
 * single character set as derived from the IANA character-sets registry.
 * Instances are normally obtained through BCharacterSetRoster rather than
 * constructed directly; the default constructor produces a UTF-8 placeholder
 * suitable for use with BCharacterSetRoster::GetNextCharacterSet().
 *
 * @see BCharacterSetRoster, convert_to_utf8(), convert_from_utf8()
 */
class BCharacterSet {
public:
	/**
	 * @brief Default constructor; initialises the object to represent UTF-8.
	 *
	 * Suitable for stack-allocated objects that will be filled in by
	 * BCharacterSetRoster::GetNextCharacterSet().
	 */
	BCharacterSet();

	/**
	 * @brief Full constructor; for internal use by the character-set table.
	 *
	 * @param id          Numeric ID used by BFont::SetEncoding() and the
	 *                    convert_to/from_utf8() functions.
	 * @param MIBenum     IANA MIB enum identifying this coded character set.
	 * @param print_name  Human-readable display name.
	 * @param iana_name   Canonical IANA name (e.g. "ISO-8859-1").
	 * @param mime_name   Preferred MIME name, or NULL if none is registered.
	 * @param aliases     NULL-terminated array of alias strings, or NULL.
	 */
	BCharacterSet(uint32 id, uint32 MIBenum, const char * print_name,
	              const char * iana_name, const char * mime_name,
	              const char ** aliases);

	/**
	 * @brief Returns the font encoding ID for use with BFont::SetEncoding().
	 *
	 * One greater than the conversion ID returned by GetConversionID().
	 *
	 * @return The font-encoding identifier for this character set.
	 */
	uint32 GetFontID(void) const;

	/**
	 * @brief Returns the conversion ID for use with convert_to/from_utf8().
	 *
	 * Equal to GetFontID() - 1.
	 *
	 * @return The iconv-layer conversion identifier for this character set.
	 */
	uint32 GetConversionID(void) const;

	/**
	 * @brief Returns the IANA MIB enum for this character set.
	 *
	 * @return The MIB enum value.
	 * @see http://www.iana.org/assignments/character-sets
	 */
	uint32 GetMIBenum(void) const;

	/**
	 * @brief Returns the canonical IANA name (e.g. "UTF-8", "ISO-8859-1").
	 *
	 * Suitable for use as the \c from / \c to argument to iconv_open().
	 *
	 * @return A pointer to a NUL-terminated IANA name string.
	 */
	const char * GetName(void) const;

	/**
	 * @brief Returns a human-readable display name for this character set.
	 *
	 * Intended for presentation in menus or combo boxes.
	 *
	 * @return A pointer to a NUL-terminated display-name string.
	 */
	const char * GetPrintName(void) const;

	/**
	 * @brief Returns the preferred MIME name, or NULL if none exists.
	 *
	 * @return A pointer to a NUL-terminated MIME name string, or NULL.
	 */
	const char * GetMIMEName(void) const;

	/**
	 * @brief Returns the number of registered aliases for this character set.
	 *
	 * @return The alias count (0 if none are registered).
	 */
	int32 CountAliases(void) const;

	/**
	 * @brief Returns the alias at the given index.
	 *
	 * @param index Zero-based index into the alias list.
	 * @return A pointer to the alias string, or NULL if @p index is out of
	 *         range.
	 */
	const char * AliasAt(uint32 index) const;

private:
	uint32       id;            ///< ID from convert_to_utf8/convert_from_utf8
	uint32       MIBenum;       ///< For use in MIBs to identify coded character sets
	const char * print_name;    ///< User-interface-friendly name
	const char * iana_name;     ///< Standard IANA name
	const char * mime_name;     ///< Preferred MIME name (may be NULL)
	const char **aliases;       ///< Alias strings for this character set
	uint32       aliases_count; ///< Number of entries in the aliases array
};

}

#endif // CHARACTER_SET_H
