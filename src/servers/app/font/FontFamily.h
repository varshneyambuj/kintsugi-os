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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2001-2008, Haiku.
 * Original authors: DarkWyrm, Axel Dörfler.
 */

/** @file FontFamily.h
    @brief Model class grouping FontStyle instances under one family name. */

#ifndef FONT_FAMILY_H_
#define FONT_FAMILY_H_


#include <ObjectList.h>
#include <Referenceable.h>
#include <String.h>

#include "FontStyle.h"


/**
 * @brief Reference-counted container holding the styles of one font family.
 *
 * A FontFamily is identified by a numeric @c ID() and a textual @c Name()
 * (truncated to B_FONT_FAMILY_LENGTH for Be API parity). It exposes
 * sorted style lookups (by name, by face mask, by index) and maintains a
 * cached aggregate flag set computed lazily from its members.
 */
class FontFamily : public BReferenceable {
public:
						FontFamily(const char* name, uint16 id);

			const char*	Name() const;

			bool		AddStyle(FontStyle* style);
			bool		RemoveStyle(FontStyle* style);

			FontStyle*	GetStyle(const char* style) const;
			FontStyle*	GetStyleMatchingFace(uint16 face) const;

			/** @brief Returns the numeric ID assigned by the FontManager. */
			uint16		ID() const
							{ return fID; }
			uint32		Flags();

			bool		HasStyle(const char* style) const;
			int32		CountStyles() const;
			FontStyle*	StyleAt(int32 index) const;

private:
			FontStyle*	_FindStyle(const char* name) const;

			BString		fName;
			BObjectList<FontStyle> fStyles;
			uint16		fID;
			uint16		fNextID;
			uint32		fFlags;
};

#endif	// FONT_FAMILY_H_
