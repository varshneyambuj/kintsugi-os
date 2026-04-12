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
 *   Copyright (c) Haiku, Inc. and contributors. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file character_sets.cpp
 * @brief Global BCharacterSet registry for all supported text encodings.
 *
 * Defines all BCharacterSet instances recognised by the Kintsugi text-encoding
 * subsystem and organises them into two lookup structures: a dense array indexed
 * by the internal Kintsugi encoding ID, and a sparse array indexed by IANA
 * MIBenum.  The MIBenum array is built at static initialisation time by the
 * MIBenumArrayInitializer singleton.
 *
 * To add a new character set: define the BCharacterSet object and its alias
 * array above the character_sets_by_id table (incrementing the ID), then
 * append a pointer to the end of that table.  No other changes are required.
 *
 * @see BCharacterSet
 */


#include <string.h>
#include <Catalog.h>
#include <Locale.h>
#include <CharacterSet.h>
#include <Debug.h>
#include "character_sets.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "textencodings"

namespace BPrivate {

/**
 * These variables are used in defining the character_sets_by_id array below.
 * @see http://www.iana.org/assignments/character-sets
 * @see http://java.sun.com/j2se/1.5.0/docs/guide/intl/encoding.doc.html
 * @see http://www.openi18n.org/subgroups/sa/locnameguide/final/CodesetAliasTable.html
 **/

/** @brief IANA and Java alias names for UTF-8 Unicode. */
static const char * unicodeAliases[] = {
	// IANA aliases
	// java aliases
	"UTF8", "unicode-1-1-utf-8",
	NULL
};
/** @brief UTF-8 Unicode character set (ID 0, MIBenum 106). */
static const BCharacterSet unicode(0,106, B_TRANSLATE("Unicode"),
	"UTF-8", "UTF-8",unicodeAliases);

/** @brief IANA and Java alias names for ISO 8859-1 (Latin-1). */
static const char * isoLatin1aliases[] = {
	// IANA aliases
	"iso-ir-100", "ISO_8859-1", "ISO-8859-1", "latin1", "11", "IBM819", "CP819", "csISOLatin1",
	// java aliases
	"819", "IBM-819", "ISO8859_1", "8859_1", "ISO8859-1",
	NULL
};
/** @brief ISO 8859-1 West European character set (ID 1, MIBenum 4). */
static const BCharacterSet isoLatin1(1,4, B_TRANSLATE("ISO West European"),
	"ISO_8859-1:1987","ISO-8859-1",isoLatin1aliases);

/** @brief IANA and Java alias names for ISO 8859-2 (Latin-2). */
static const char * isoLatin2aliases[] = {
	// IANA aliases
	"iso-ir-101", "ISO_8859-2", "ISO-8859-2", "latin2", "12", "csISOLatin2",
	// java aliases
	"iso8859_2", "8859_2", "ISO8859-2", "ibm912", "ibm-912", "cp912", "912",
	NULL
};
/** @brief ISO 8859-2 East European character set (ID 2, MIBenum 5). */
static const BCharacterSet isoLatin2(2,5, B_TRANSLATE("ISO East European"),
	"ISO_8859-2:1987","ISO-8859-2",isoLatin2aliases);

/** @brief IANA and Java alias names for ISO 8859-3 (Latin-3). */
static const char * isoLatin3aliases[] = {
	// IANA aliases
	"iso-ir-109", "ISO_8859-3", "ISO-8859-3", "latin3", "13", "csISOLatin3",
	// java aliases
	"iso8859_3", "8859_3", "iso8859-3", "ibm913", "ibm-913", "cp913", "913",
	NULL
};
/** @brief ISO 8859-3 South European character set (ID 3, MIBenum 6). */
static const BCharacterSet isoLatin3(3,6, B_TRANSLATE("ISO South European"),
	"ISO_8859-3:1988","ISO-8859-3",isoLatin3aliases);

/** @brief IANA and Java alias names for ISO 8859-4 (Latin-4). */
static const char * isoLatin4aliases[] = {
	// IANA aliases
	"iso-ir-110", "ISO_8859-4", "ISO-8859-4", "latin4", "14", "csISOLatin4",
	// java aliases
	"iso8859_4", "iso8859-4", "8859_4", "ibm914", "ibm-914", "cp914", "914",
	NULL
};
/** @brief ISO 8859-4 North European character set (ID 4, MIBenum 7). */
static const BCharacterSet isoLatin4(4,7, B_TRANSLATE("ISO North European"),
	"ISO_8859-4:1988","ISO-8859-4",isoLatin4aliases);

/** @brief IANA and Java alias names for ISO 8859-5 (Cyrillic). */
static const char * isoLatin5aliases[] = {
	// IANA aliases
	"iso-ir-144", "ISO_8859-5", "ISO-8859-5", "cyrillic", "csISOLatinCyrillic",
	// java aliases
	"iso8859_5", "8859_5", "ISO8859-5", "ibm915", "ibm-915", "cp915", "915",
	NULL
};
/** @brief ISO 8859-5 Cyrillic character set (ID 5, MIBenum 8). */
static const BCharacterSet isoLatin5(5,8, B_TRANSLATE("ISO Cyrillic"),
	"ISO_8859-5:1988","ISO-8859-5",isoLatin5aliases);

/** @brief IANA and Java alias names for ISO 8859-6 (Arabic). */
static const char * isoLatin6aliases[] = {
	// IANA aliases
	"iso-ir-127", "ISO_8859-6", "ISO-8859-6", "ECMA-114", "ASMO-708", "arabic", "csISOLatinArabic",
	// java aliases
	"iso8859_6", "8859_6", "ISO8859-6", "ibm1089", "ibm-1089", "cp1089", "1089",
	NULL
};
/** @brief ISO 8859-6 Arabic character set (ID 6, MIBenum 9). */
static const BCharacterSet isoLatin6(6,9, B_TRANSLATE("ISO Arabic"),
	"ISO_8859-6:1987","ISO-8859-6",isoLatin6aliases);

/** @brief IANA and Java alias names for ISO 8859-7 (Greek). */
static const char * isoLatin7aliases[] = {
	// IANA aliases
	"iso-ir-126", "ISO_8859-7", "ISO-8859-7", "ELOT_928", "ECMA-118", "greek", "greek8", "csISOLatinGreek",
	// java aliases
	"iso8859_7", "8859_7", "iso8859-7", "sun_eu_greek", "ibm813", "ibm-813", "813", "cp813",
	NULL
};
/** @brief ISO 8859-7 Greek character set (ID 7, MIBenum 10). */
static const BCharacterSet isoLatin7(7,10, B_TRANSLATE("ISO Greek"),
	"ISO_8859-7:1987","ISO-8859-7",isoLatin7aliases);

/** @brief IANA and Java alias names for ISO 8859-8 (Hebrew). */
static const char * isoLatin8aliases[] = {
	// IANA aliases
	"iso-ir-138", "ISO_8859-8", "ISO-8859-8", "hebrew", "csISOLatinHebrew",
	// java aliases
	"iso8859_8", "8859_8", "ISO8859-8", "cp916", "916", "ibm916", "ibm-916",
	NULL
};
/** @brief ISO 8859-8 Hebrew character set (ID 8, MIBenum 11). */
static const BCharacterSet isoLatin8(8,11, B_TRANSLATE("ISO Hebrew"),
	"ISO_8859-8:1988","ISO-8859-8",isoLatin8aliases);

/** @brief IANA and Java alias names for ISO 8859-9 (Turkish / Latin-5). */
static const char * isoLatin9aliases[] = {
	// IANA aliases
	"iso-ir-148", "ISO_8859-9", "ISO-8859-9", "latin5", "15", "csISOLatin5",
	// java aliases
	"iso8859_9", "8859_9", "ibm920", "ibm-920", "920", "cp920",
	NULL
};
/** @brief ISO 8859-9 Turkish character set (ID 9, MIBenum 12). */
const BCharacterSet isoLatin9(9,12, B_TRANSLATE("ISO Turkish"),
	"ISO_8859-9:1989","ISO-8859-9",isoLatin9aliases);

/** @brief IANA alias names for ISO 8859-10 (Nordic / Latin-6). */
static const char * isoLatin10aliases[] = {
	// IANA aliases
	"iso-ir-157", "16", "ISO_8859-10:1992", "csISOLatin6", "latin6",
	// java aliases
	NULL
};
/** @brief ISO 8859-10 Nordic character set (ID 10, MIBenum 13). */
static const BCharacterSet isoLatin10(10,13, B_TRANSLATE("ISO Nordic"),
	"ISO-8859-10","ISO-8859-10",isoLatin10aliases);

/** @brief IANA, Java, and mail-kit alias names for Macintosh Roman. */
static const char * macintoshAliases[] = {
	// IANA aliases
	"mac", "csMacintosh",
	// java aliases
	"MacRoman",
	// mail kit aliases
	"x-mac-roman",
	NULL
};
/** @brief Macintosh Roman character set (ID 11, MIBenum 2027). */
static const BCharacterSet macintosh(11,2027, B_TRANSLATE("Macintosh Roman"),
	"macintosh",NULL,macintoshAliases);

/** @brief IANA, Java, and mail-kit alias names for Shift JIS. */
static const char * shiftJISaliases[] = {
	// IANA aliases
	"MS_Kanji", "csShiftJIS",
	// java aliases
	"sjis", "shift_jis", "shift-jis", "x-sjis",
	// mail kit aliases
	"shift_jisx0213",
	NULL
};
/** @brief Japanese Shift JIS character set (ID 12, MIBenum 17). */
static const BCharacterSet shiftJIS(12,17, B_TRANSLATE("Japanese Shift JIS"),
	"Shift_JIS","Shift_JIS",shiftJISaliases);

/** @brief IANA, Java, and mail-kit alias names for EUC Packed-Format Japanese. */
static const char * EUCPackedJapaneseAliases[] = {
	// IANA aliases
	"EUC-JP", "csEUCPkdFmtJapanese",
	// java aliases
	"eucjis", "eucjp", "x-euc-jp", "x-eucjp",
	// mail kit aliases
	"euc-jisx0213",
	NULL
};
/** @brief Japanese EUC character set (ID 13, MIBenum 18). */
static const BCharacterSet packedJapanese(13,18, B_TRANSLATE("Japanese EUC"),
                                   "Extended_UNIX_Code_Packed_Format_for_Japanese","EUC-JP",
                                   EUCPackedJapaneseAliases);

/** @brief IANA and Java alias names for ISO-2022-JP (JIS). */
static const char * iso2022jpAliases[] = {
	// IANA aliases
	"csISO2022JP",
	// java aliases
	"iso2022jp", "jis", "jis_encoding", "csjisencoding",
	NULL
};
/** @brief Japanese JIS character set (ID 14, MIBenum 39). */
static const BCharacterSet iso2022jp(14,39, B_TRANSLATE("Japanese JIS"),
	"ISO-2022-JP","ISO-2022-JP",iso2022jpAliases);

/** @brief Java alias names for Windows-1252 (Latin-1 superset). */
static const char * windows1252aliases[] = {
	// IANA aliases
	// java aliases
	"cp1252", "cp5348",
	NULL
};
/** @brief Windows Latin-1 (CP 1252) character set (ID 15, MIBenum 2252). */
static const BCharacterSet windows1252(15,2252, B_TRANSLATE("Windows Latin-1 "
	"(CP 1252)"),"windows-1252",NULL,windows1252aliases);

/** @brief IANA and Java alias names for ISO-10646-UCS-2 (Unicode UCS-2). */
static const char * unicode2aliases[] = {
	// IANA aliases
	"csUnicode",
	// java aliases
	"UTF-16BE", "UTF_16BE", "X-UTF-16BE", "UnicodeBigUnmarked",
	NULL
};
/** @brief Unicode UCS-2 character set (ID 16, MIBenum 1000). */
static const BCharacterSet unicode2(16,1000, B_TRANSLATE("Unicode (UCS-2)"),
	"ISO-10646-UCS-2",NULL,unicode2aliases);

/** @brief IANA and Java alias names for KOI8-R (Russian Cyrillic). */
static const char * KOI8Raliases[] = {
	// IANA aliases
	"csKOI8R",
	// java aliases
	"koi8_r", "koi8", "cskoi8r",
	NULL
};
/** @brief KOI8-R Russian Cyrillic character set (ID 17, MIBenum 2084). */
static const BCharacterSet KOI8R(17,2084, B_TRANSLATE("KOI8-R Cyrillic"),
	"KOI8-R","KOI8-R",KOI8Raliases);

/** @brief Java alias names for Windows-1251 (Windows Cyrillic). */
static const char * windows1251aliases[] = {
	// IANA aliases
	// java aliases
	"cp1251", "cp5347", "ansi-1251",
	NULL
};
/** @brief Windows Cyrillic (CP 1251) character set (ID 18, MIBenum 2251). */
static const BCharacterSet windows1251(18,2251, B_TRANSLATE("Windows Cyrillic "
	"(CP 1251)"), "windows-1251",NULL,windows1251aliases);

/** @brief IANA, Java, and mail-kit alias names for IBM866 (DOS Cyrillic). */
static const char * IBM866aliases[] = {
	// IANA aliases
	"cp866", "866", "csIBM866",
	// java aliases
	"ibm-866",
	// mail kit aliases
	"dos-866",
	NULL
};
/** @brief IBM866 DOS Cyrillic character set (ID 19, MIBenum 2086). */
static const BCharacterSet IBM866(19,2086, B_TRANSLATE("DOS Cyrillic"),
	"IBM866","IBM866",IBM866aliases);

/** @brief IANA, Java, and mail-kit alias names for IBM437 (DOS Latin-US). */
static const char * IBM437aliases[] = {
	// IANA aliases
	"cp437", "437", "csPC8CodePage437",
	// java aliases
	"ibm-437", "windows-437",
	// mail kit aliases
	"dos-437",
	NULL
};
/** @brief IBM437 DOS Latin-US character set (ID 20, MIBenum 2011). */
static const BCharacterSet IBM437(20,2011, B_TRANSLATE("DOS Latin-US"),
	"IBM437","IBM437",IBM437aliases);

/** @brief IANA and Java alias names for EUC-KR (Korean). */
static const char * eucKRaliases[] = {
	// IANA aliases
	"csEUCKR",
	// java aliases
	"ksc5601", "euckr", "ks_c_5601-1987", "ksc5601-1987",
	"ksc5601_1987", "ksc_5601", "5601",
	NULL
};
/** @brief EUC Korean character set (ID 21, MIBenum 38). */
static const BCharacterSet eucKR(21,38, B_TRANSLATE("EUC Korean"),
	"EUC-KR","EUC-KR",eucKRaliases);

/** @brief Java alias names for ISO 8859-13 (Baltic). */
static const char * iso13aliases[] = {
	// IANA aliases
	// java aliases
	"iso8859_13", "8859_13", "iso_8859-13", "ISO8859-13",
	NULL
};
/** @brief ISO 8859-13 Baltic character set (ID 22, MIBenum 109). */
static const BCharacterSet iso13(22,109, B_TRANSLATE("ISO Baltic"),
	"ISO-8859-13","ISO-8859-13",iso13aliases);

/** @brief IANA alias names for ISO 8859-14 (Celtic). */
static const char * iso14aliases[] = {
	// IANA aliases
	"iso-ir-199", "ISO_8859-14:1998", "ISO_8859-14", "latin8", "iso-celtic", "l8",
	NULL
};
/** @brief ISO 8859-14 Celtic character set (ID 23, MIBenum 110). */
static const BCharacterSet iso14(23,110, B_TRANSLATE("ISO Celtic"),
	"ISO-8859-14","ISO-8859-14",iso14aliases);

/** @brief IANA and Java alias names for ISO 8859-15 (Latin-9). */
static const char * iso15aliases[] = {
	// IANA aliases
	"ISO_8859-15", "Latin-9",
	// java aliases
	"8859_15", "ISO8859_15", "ISO8859-15", "IBM923", "IBM-923", "cp923", "923",
	"LATIN0", "LATIN9", "L9", "csISOlatin0", "csISOlatin9", "ISO8859_15_FDIS",
	NULL
};
/** @brief ISO 8859-15 Latin-9 character set (ID 24, MIBenum 111). */
static const BCharacterSet iso15(24,111, B_TRANSLATE("ISO Latin 9"),
	"ISO-8859-15","ISO-8859-15",iso15aliases);

// chinese character set testing

/** @brief IANA alias names for Big5 (Traditional Chinese). */
static const char * big5aliases[] = {
	// IANA aliases
	"csBig5",
	NULL
};
/** @brief Chinese Big5 character set (ID 25, MIBenum 2026). */
static const BCharacterSet big5(25,2026, B_TRANSLATE("Chinese Big5"),
	"Big5","Big5",big5aliases);

/** @brief Java and mail-kit alias names for GB18030 (Simplified Chinese). */
static const char * gb18030aliases[] = {
	// java aliases
	"gb18030-2000",
	// mail kit aliases
	"gb2312",
	"gbk",
	NULL
};
/** @brief Chinese GB18030 character set (ID 26, MIBenum 114). */
static const BCharacterSet gb18030(26,114, B_TRANSLATE("Chinese GB18030"),
	"GB18030",NULL,gb18030aliases);

/** @brief IANA and Java alias names for UTF-16. */
static const char* kUTF16Aliases[] = {
	// IANA aliases
	"UTF-16",
	// java aliases
	"UTF-16BE", "X-UTF-16BE", "UnicodeBigUnmarked",
	NULL
};
/** @brief UTF-16 Unicode character set (ID 27, MIBenum 1000). */
static const BCharacterSet kUTF16(27, 1000, B_TRANSLATE("Unicode"), "UTF-16", "UTF-16",
	kUTF16Aliases);

/** @brief IANA and Java alias names for Windows-1250 (Central European). */
static const char* kWindows1250Aliases[] = {
	// IANA aliases
	"cswindows1250",
	// java aliases
	"cp1250",
	"ms-ee",
	NULL
};
/** @brief Windows Central European (CP 1250) character set (ID 28, MIBenum 2250). */
static const BCharacterSet kWindows1250(28, 2250, B_TRANSLATE("Windows Central "
	"European (CP 1250)"), "windows-1250", NULL, kWindows1250Aliases);

/**
 * The following initializes the global character set array.
 * It is organized by id for efficient retrieval using predefined constants in UTF8.h and Font.h.
 * Character sets are stored contiguously and may be efficiently iterated over.
 * To add a new character set, define the character set above -- remember to increment the id --
 * and then add &<charSetName> to the _end_ of the following list.  That's all.
 **/

/** @brief Dense array of all registered BCharacterSet objects, indexed by ID. */
const BCharacterSet * character_sets_by_id[] = {
	&unicode,
	&isoLatin1, &isoLatin2, &isoLatin3,	&isoLatin4,	&isoLatin5,
	&isoLatin6,	&isoLatin7, &isoLatin8, &isoLatin9, &isoLatin10,
	&macintosh,
	// R5 BFont encodings end here
	&shiftJIS, &packedJapanese, &iso2022jp,
	&windows1252, &unicode2, &KOI8R, &windows1251,
	&IBM866, &IBM437, &eucKR, &iso13, &iso14, &iso15,
	// R5 convert_to/from_utf8 encodings end here
	&big5,&gb18030,
	&kUTF16,
	&kWindows1250,
};

/** @brief Number of entries in character_sets_by_id. */
const uint32 character_sets_by_id_count = sizeof(character_sets_by_id)/sizeof(const BCharacterSet*);

/**
 * The following code initializes the global MIBenum array.
 * This sparsely populated array exists as an efficient way to access character sets by MIBenum.
 * The MIBenum array is automatically allocated, and initialized by the following class.
 * The following class should only be instantiated once, this is assured by using an assertion.
 * No changes are required to the following code to add a new character set.
 **/

/** @brief Sparse array mapping IANA MIBenum values to BCharacterSet pointers. */
const BCharacterSet ** character_sets_by_MIBenum;

/** @brief Largest valid MIBenum value among all registered character sets. */
uint32 maximum_valid_MIBenum;

/**
 * @brief One-time initialiser that builds the character_sets_by_MIBenum array.
 *
 * Scans character_sets_by_id to find the maximum MIBenum, allocates a
 * zero-filled array of that size + 2, and fills it so that each BCharacterSet
 * can be looked up directly by its MIBenum index.
 */
static class MIBenumArrayInitializer {
public:
	MIBenumArrayInitializer() {
		DEBUG_ONLY(static int onlyOneTime = 0;)
		ASSERT_WITH_MESSAGE(onlyOneTime++ == 0,"MIBenumArrayInitializer should be instantiated only one time.");
		// analyzing character_sets_by_id
		uint32 max_MIBenum = 0;
		for (uint32 index = 0 ; index < character_sets_by_id_count ; index++ ) {
			if (max_MIBenum < character_sets_by_id[index]->GetMIBenum()) {
				max_MIBenum = character_sets_by_id[index]->GetMIBenum();
			}
		}
		// initializing extern variables
		character_sets_by_MIBenum = new const BCharacterSet*[max_MIBenum+2];
		maximum_valid_MIBenum = max_MIBenum;
		// initializing MIBenum array
		memset(character_sets_by_MIBenum,0,sizeof(BCharacterSet*)*(max_MIBenum+2));
		for (uint32 index2 = 0 ; index2 < character_sets_by_id_count ; index2++ ) {
			const BCharacterSet * charset = character_sets_by_id[index2];
			character_sets_by_MIBenum[charset->GetMIBenum()] = charset;
		}
	}
	/**
	 * @brief Frees the MIBenum lookup array at program exit.
	 */
	~MIBenumArrayInitializer()
	{
		delete [] character_sets_by_MIBenum;
	}
} runTheInitializer;

}
