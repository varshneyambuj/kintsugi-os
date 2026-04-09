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
 * @file CharacterSet.cpp
 * @brief Implementation of BCharacterSet, which describes a single IANA
 *        character encoding.
 *
 * BCharacterSet holds the metadata for one character encoding as derived
 * from the IANA character-sets registry: a numeric font/conversion ID,
 * an MIB enum, standard IANA and MIME names, and an optional list of
 * aliases.  Instances are normally obtained via BCharacterSetRoster rather
 * than constructed directly.
 *
 * @see BCharacterSetRoster, convert_to_utf8(), convert_from_utf8()
 */


#include <CharacterSet.h>

namespace BPrivate {


/**
 * @brief Default constructor; initialises the object to represent UTF-8.
 *
 * Produces a character-set object whose IANA name, MIME name, and print
 * name are all set to "UTF-8" / "Unicode", with an MIB enum of 106
 * (the registered value for UTF-8) and no aliases.  Intended for
 * stack-allocated objects that will be filled in by
 * BCharacterSetRoster::GetNextCharacterSet().
 */
BCharacterSet::BCharacterSet()
{
	id = 0;
	MIBenum = 106;
	print_name = "Unicode";
	iana_name = "UTF-8";
	mime_name = "UTF-8";
	aliases_count = 0;
	aliases = NULL;
}


/**
 * @brief Full constructor; for internal use by the character-set table.
 *
 * Initialises every field from the provided arguments.  The @p _aliases
 * array must be NULL-terminated if non-NULL; the constructor counts the
 * entries to populate \c aliases_count.
 *
 * @param _id          Numeric ID used by BFont::SetEncoding() and the
 *                     convert_to/from_utf8() functions (offset by one
 *                     between the two — see GetFontID() vs
 *                     GetConversionID()).
 * @param _MIBenum     IANA MIB enum identifying this coded character set.
 * @param _print_name  Human-readable display name (e.g. "Western European
 *                     (ISO-8859-1)").
 * @param _iana_name   Canonical IANA name (e.g. "ISO-8859-1").
 * @param _mime_name   Preferred MIME name, or NULL if none is registered.
 * @param _aliases     NULL-terminated array of alias strings, or NULL.
 */
BCharacterSet::BCharacterSet(uint32 _id, uint32 _MIBenum, const char * _print_name,
                             const char * _iana_name, const char * _mime_name,
                             const char ** _aliases)
{
	id = _id;
	MIBenum = _MIBenum;
	print_name = _print_name;
	iana_name = _iana_name;
	mime_name = _mime_name;
	aliases_count = 0;
	if (_aliases != 0) {
		while (_aliases[aliases_count] != 0) {
			aliases_count++;
		}
	}
	aliases = _aliases;
}


/**
 * @brief Returns the font encoding ID for use with BFont::SetEncoding().
 *
 * This ID is one greater than the conversion ID returned by
 * GetConversionID().
 *
 * @return The font-encoding identifier for this character set.
 */
uint32
BCharacterSet::GetFontID() const
{
	return id;
}


/**
 * @brief Returns the conversion ID for use with convert_to/from_utf8().
 *
 * Equal to GetFontID() - 1.
 *
 * @return The iconv-layer conversion identifier for this character set.
 */
uint32
BCharacterSet::GetConversionID() const
{
	return id-1;
}


/**
 * @brief Returns the IANA MIB enum for this character set.
 *
 * MIB (Management Information Base) enums are assigned by IANA and
 * provide a standardised numeric identifier for each registered encoding.
 *
 * @return The MIB enum value.
 * @see http://www.iana.org/assignments/character-sets
 */
uint32
BCharacterSet::GetMIBenum() const
{
	return MIBenum;
}


/**
 * @brief Returns the canonical IANA name for this character set.
 *
 * This is the preferred name as registered with IANA, e.g. "UTF-8" or
 * "ISO-8859-1".  It is suitable for use as the \c from / \c to argument
 * to iconv_open().
 *
 * @return A pointer to a NUL-terminated IANA name string.
 */
const char *
BCharacterSet::GetName() const
{
	return iana_name;
}


/**
 * @brief Returns a human-readable display name for this character set.
 *
 * Intended for presentation in user-interface elements such as menus or
 * combo boxes, e.g. "Western European (ISO-8859-1)".
 *
 * @return A pointer to a NUL-terminated display-name string.
 */
const char *
BCharacterSet::GetPrintName() const
{
	return print_name;
}


/**
 * @brief Returns the preferred MIME name for this character set.
 *
 * Some character sets have a separate, IANA-registered name specifically
 * for use in MIME content-type headers (e.g. "windows-1252").  Returns
 * NULL if no such name exists for this encoding.
 *
 * @return A pointer to a NUL-terminated MIME name string, or NULL.
 */
const char *
BCharacterSet::GetMIMEName() const
{
	return mime_name;
}


/**
 * @brief Returns the number of registered aliases for this character set.
 *
 * @return The alias count (0 if none are registered).
 */
int32
BCharacterSet::CountAliases() const
{
	return aliases_count;
}


/**
 * @brief Returns the alias at the given index.
 *
 * Aliases are alternative names for the encoding as listed in the IANA
 * registry (e.g. "latin1", "iso_8859-1", "csISOLatin1" for ISO-8859-1).
 *
 * @param index Zero-based index into the alias list.
 * @return A pointer to the alias string, or NULL if @p index is out of
 *         range.
 */
const char *
BCharacterSet::AliasAt(uint32 index) const
{
	if (index >= aliases_count) {
		return 0;
	}
	return aliases[index];
}

}
