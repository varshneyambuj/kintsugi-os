/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
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
 * This file incorporates work from the Haiku project:
 *   Copyright 2009, Haiku Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 * Author:
 *   Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file TranslationDefs.h
 *  @brief Core types, version macros, and format/info structs for the Translation Kit. */

#ifndef _TRANSLATION_DEFS_H
#define _TRANSLATION_DEFS_H


#include <SupportDefs.h>


#define B_TRANSLATION_CURRENT_VERSION	B_BEOS_VERSION
#define B_TRANSLATION_MIN_VERSION		161

/** @brief Packs major, minor, and revision into a single translator version integer. */
#define B_TRANSLATION_MAKE_VERSION(major, minor, revision) \
	((major << 8) | ((minor << 4) & 0xf0) | (revision & 0x0f))
/** @brief Extracts the major version from a packed translator version integer. */
#define B_TRANSLATION_MAJOR_VERSION(v)		(v >> 8)
/** @brief Extracts the minor version from a packed translator version integer. */
#define B_TRANSLATION_MINOR_VERSION(v)		((v >> 4) & 0xf)
/** @brief Extracts the revision from a packed translator version integer. */
#define B_TRANSLATION_REVISION_VERSION(v)	(v & 0xf)


extern const char* B_TRANSLATOR_MIME_TYPE;

/** @brief Opaque numeric identifier assigned to a loaded translator by the roster. */
typedef unsigned long translator_id;


/** @brief Describes a single data format that a translator can read or write. */
struct translation_format {
	uint32		type;				// type_code
	uint32		group;
	float		quality;			// between 0.0 and 1.0
	float		capability;			// between 0.0 and 1.0
	char		MIME[251];
	char		name[251];
};

/** @brief Result record returned by BTranslatorRoster::Identify() describing the best match. */
struct translator_info {
	uint32			type;
	translator_id	translator;
	uint32			group;
	float			quality;
	float			capability;
	char			name[251];
	char			MIME[251];
};


#endif	// _TRANSLATION_DEFS_H
