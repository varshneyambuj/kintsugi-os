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
 * MIT License. Copyright 2001-2009, Haiku.
 * Original authors: DarkWyrm, Axel Dörfler.
 */

/** @file FontManager.h
    @brief Abstract base font catalog shared by GlobalFontManager and AppFontManager. */

#ifndef FONT_MANAGER_H
#define FONT_MANAGER_H


#include <HashMap.h>
#include <Node.h>
#include <ObjectList.h>
#include <Referenceable.h>

#include <ft2build.h>
#include FT_FREETYPE_H


class FontFamily;
class FontStyle;


/**
 * @brief Abstract base class managing a sorted catalog of font families and styles.
 *
 * Subclasses (GlobalFontManager, AppFontManager) provide a concrete locking
 * strategy via the pure-virtual Lock()/Unlock()/IsLocked() trio. The base
 * keeps the family list, the (familyID, styleID) -> FontStyle hash table,
 * a parallel "delisted" table for styles still referenced after removal,
 * and a monotonic revision counter that BFont clients poll for change
 * detection.
 */
class FontManager {
public:
								FontManager();
	virtual						~FontManager();

	virtual	bool				Lock() = 0;
	virtual	void				Unlock() = 0;
	virtual	bool				IsLocked() const = 0;

	virtual	int32				CountFamilies();

	virtual	int32				CountStyles(const char* family);
	virtual	int32				CountStyles(uint16 familyID);
			FontFamily*			FamilyAt(int32 index) const;

	virtual	FontFamily*			GetFamily(uint16 familyID) const;
	virtual	FontFamily*			GetFamily(const char* name);

			FontStyle*			GetStyleByIndex(const char* family,
									int32 index);
			FontStyle*			GetStyleByIndex(uint16 familyID, int32 index);

	virtual	FontStyle*			GetStyle(uint16 familyID,
									uint16 styleID) const;
	virtual	FontStyle*			GetStyle(const char* familyName,
									const char* styleName,
									uint16 familyID = 0xffff,
									uint16 styleID = 0xffff,
									uint16 face = 0);
			FontStyle*			FindStyleMatchingFace(uint16 face) const;

			void				RemoveStyle(FontStyle* style);
				// This call must not be used by anything else than class
				// FontStyle.

	virtual	uint32				Revision();


protected:
			FT_CharMap			_GetSupportedCharmap(const FT_Face& face);

			FontFamily*			_FindFamily(const char* family) const;
			FontFamily*			_FindFamily(uint16 familyID) const;

			status_t			_AddFont(FT_Face face, node_ref nodeRef,
									const char* path,
									uint16& familyID, uint16& styleID);
			FontStyle*			_RemoveFont(uint16 familyID, uint16 styleID);
			void				_RemoveAllFonts();

	virtual	uint16				_NextID();

private:
			/**
			 * @brief Composite key (familyID, styleID) used to index FontStyle.
			 */
			struct FontKey {
				FontKey()
					: familyID(0xffff), styleID(0xffff) {}

				FontKey(uint16 family, uint16 style)
					: familyID(family), styleID(style) {}

				/** @brief 32-bit hash combining family and style IDs. */
				uint32 GetHashCode() const
				{
					return familyID | (styleID << 16UL);
				}

				/** @brief Equality on (familyID, styleID) pair. */
				bool operator==(const FontKey& other) const
				{
					return familyID == other.familyID
						&& styleID == other.styleID;
				}

				uint16 familyID, styleID;
			};

private:
			typedef BObjectList<FontFamily>			FamilyList;
			FamilyList			fFamilies;

			HashMap<FontKey, BReference<FontStyle> > fStyleHashTable;
			HashMap<FontKey, FontStyle*> fDelistedStyleHashTable;

			uint32				fRevision;
			uint16				fNextID;
};

#endif	/* FONT_MANAGER_H */
