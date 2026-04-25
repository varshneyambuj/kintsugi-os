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
 *   Copyright 2001-2016, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       DarkWyrm <bpmagic@columbus.rr.com>
 *       Axel Dörfler, axeld@pinc-software.de
 */


/**
 * @file FontManager.cpp
 * @brief Implementation of the abstract FontManager catalog base class.
 *
 * Holds the family list, the (familyID, styleID) hash, the parallel
 * "delisted" hash for styles still referenced after removal, and the
 * shared FreeType library handle. Subclasses (GlobalFontManager,
 * AppFontManager) layer locking and discovery on top.
 */


#include "FontManager.h"

#include <new>

#include <Debug.h>

#include "FontFamily.h"


//#define TRACE_FONT_MANAGER
#ifdef TRACE_FONT_MANAGER
#	define FTRACE(x) printf x
#else
#	define FTRACE(x) ;
#endif


/** @brief Process-wide FreeType library handle initialized by GlobalFontManager. */
FT_Library gFreeTypeLibrary;


/**
 * @brief Comparator used by BinaryInsert / BinarySearch over family names.
 */
static int
compare_font_families(const FontFamily* a, const FontFamily* b)
{
	return strcmp(a->Name(), b->Name());
}


//	#pragma mark -


/**
 * @brief Constructs an empty manager with a fresh revision counter.
 */
FontManager::FontManager()
	:
	fFamilies(20),
	fRevision(0),
	fNextID(0)
{
}


/**
 * @brief Destroys the manager and releases all owned fonts.
 */
FontManager::~FontManager()
{
	_RemoveAllFonts();
}


/**
 * @brief Returns the first usable Unicode-capable charmap from @a face.
 *
 * Prefers Microsoft (platform 3) Symbol or Unicode encodings, then
 * Apple Unicode (platform 1), then Apple Roman (platform 0); other
 * platforms are skipped.
 *
 * @param face  Font handle obtained from FT_Load_Face().
 * @return  Pointer to the chosen FT_CharMap, or NULL when none qualifies.
 */
FT_CharMap
FontManager::_GetSupportedCharmap(const FT_Face& face)
{
	for (int32 i = 0; i < face->num_charmaps; i++) {
		FT_CharMap charmap = face->charmaps[i];

		switch (charmap->platform_id) {
			case 3:
				// if Windows Symbol or Windows Unicode
				if (charmap->encoding_id == 0 || charmap->encoding_id == 1)
					return charmap;
				break;

			case 1:
				// if Apple Unicode
				if (charmap->encoding_id == 0)
					return charmap;
				break;

			case 0:
				// if Apple Roman
				if (charmap->encoding_id == 0)
					return charmap;
				break;

			default:
				break;
		}
	}

	return NULL;
}



/**
 * @brief Returns the number of font families known to the manager.
 *
 * @return Family count.
 */
int32
FontManager::CountFamilies()
{
	return fFamilies.CountItems();
}


/**
 * @brief Returns the number of styles in the named family.
 *
 * @param familyName  Name of the family to scan.
 * @return  Style count, or 0 when @a familyName is unknown.
 */
int32
FontManager::CountStyles(const char *familyName)
{
	FontFamily *family = GetFamily(familyName);
	if (family != NULL)
		return family->CountStyles();

	return 0;
}


/**
 * @brief Returns the number of styles in the family identified by @a familyID.
 *
 * @param familyID  Numeric family ID.
 * @return  Style count, or 0 when @a familyID is unknown.
 */
int32
FontManager::CountStyles(uint16 familyID)
{
	FontFamily *family = GetFamily(familyID);
	if (family != NULL)
		return family->CountStyles();

	return 0;
}


/**
 * @brief Returns the family at position @a index in the sorted family list.
 *
 * @param index  Zero-based index.
 * @return Family pointer, or NULL when out of range.
 *
 * @note  Caller must hold the manager lock.
 */
FontFamily*
FontManager::FamilyAt(int32 index) const
{
	ASSERT(IsLocked());

	return fFamilies.ItemAt(index);
}


/**
 * @brief Looks up a FontFamily by name.
 *
 * @param name  Family name to find; NULL is permitted and returns NULL.
 * @return Family pointer, or NULL when no matching family exists.
 */
FontFamily*
FontManager::GetFamily(const char* name)
{
	if (name == NULL)
		return NULL;

	return _FindFamily(name);
}


/**
 * @brief Looks up a FontFamily by ID, with a fast-path on (familyID, 0).
 *
 * Probes the (familyID, 0) hash slot first since style 0 is the most
 * common; falls back to a linear search if style 0 has been delisted.
 *
 * @param familyID  Numeric family ID.
 * @return Family pointer, or NULL when @a familyID is unknown.
 */
FontFamily*
FontManager::GetFamily(uint16 familyID) const
{
	FontKey key(familyID, 0);
	FontStyle* style = fStyleHashTable.Get(key);
	if (style != NULL)
		return style->Family();

	// Try the slow route in case style 0 was removed
	return _FindFamily(familyID);
}


/**
 * @brief Returns style @a index of the named family.
 *
 * @param familyName  Family name to look up.
 * @param index       Zero-based style index.
 * @return Style pointer, or NULL on lookup failure.
 */
FontStyle*
FontManager::GetStyleByIndex(const char* familyName, int32 index)
{
	FontFamily* family = GetFamily(familyName);
	if (family != NULL)
		return family->StyleAt(index);

	return NULL;
}


/**
 * @brief Returns style @a index of the family identified by @a familyID.
 *
 * @param familyID  Numeric family ID.
 * @param index     Zero-based style index.
 * @return Style pointer, or NULL on lookup failure.
 */
FontStyle*
FontManager::GetStyleByIndex(uint16 familyID, int32 index)
{
	FontFamily* family = GetFamily(familyID);
	if (family != NULL)
		return family->StyleAt(index);

	return NULL;
}


/**
 * @brief Looks up a FontStyle by composite key.
 *
 * Hits the live style table first, then the delisted table so callers
 * can still resolve a style that has been removed but is still referenced.
 *
 * @param familyID  Numeric family ID.
 * @param styleID   Numeric style ID.
 * @return Matching FontStyle, or NULL when neither table holds the key.
 *
 * @note  Caller must hold the manager lock.
 */
FontStyle*
FontManager::GetStyle(uint16 familyID, uint16 styleID) const
{
	ASSERT(IsLocked());

	FontKey key(familyID, styleID);
	FontStyle* style = fStyleHashTable.Get(key);
	if (style != NULL)
		return style;

	return fDelistedStyleHashTable.Get(key);
}


/**
 * @brief Resolves the closest matching style for a flexible request.
 *
 * Looks up the family by name first, otherwise by ID; then resolves the
 * style by name, otherwise by face mask. When everything but @a styleID
 * is empty the call collapses to GetStyle(familyID, styleID).
 *
 * @param familyName  Family name, or NULL/"" to use @a familyID.
 * @param styleName   Style name, or NULL/"" to use @a styleID / @a face.
 * @param familyID    Family ID fallback when @a familyName is empty.
 * @param styleID     Style ID fallback when both names are empty.
 * @param face        Face mask used as a last-resort selector.
 * @return The closest FontStyle, or NULL when no family matches.
 *
 * @note  Caller must hold the manager lock.
 */
FontStyle*
FontManager::GetStyle(const char* familyName, const char* styleName,
	uint16 familyID, uint16 styleID, uint16 face)
{
	ASSERT(IsLocked());

	FontFamily* family;

	if (styleID != 0xffff && (familyName == NULL || !familyName[0])
		&& (styleName == NULL || !styleName[0])) {
		return GetStyle(familyID, styleID);
	}

	// find family

	if (familyName != NULL && familyName[0])
		family = GetFamily(familyName);
	else
		family = GetFamily(familyID);

	if (family == NULL)
		return NULL;

	// find style

	if (styleName != NULL && styleName[0])
		return family->GetStyle(styleName);

	// try to get from face
	return family->GetStyleMatchingFace(face);
}


/**
 * @brief Cross-family search for the first style whose Face() matches @a face.
 *
 * Useful when no family preference exists but the caller wants any
 * available rendering of, for example, B_BOLD_FACE | B_ITALIC_FACE.
 *
 * @param face  Desired face mask.
 * @return First matching FontStyle, or NULL when none qualifies.
 */
FontStyle*
FontManager::FindStyleMatchingFace(uint16 face) const
{
	int32 count = fFamilies.CountItems();

	for (int32 i = 0; i < count; i++) {
		FontFamily* family = fFamilies.ItemAt(i);
		FontStyle* style = family->GetStyleMatchingFace(face);
		if (style != NULL)
			return style;
	}

	return NULL;
}


/**
 * @brief Removes @a style from the family list and the delisted hash.
 *
 * Called only by the FontStyle destructor; by the time it runs the
 * style is already invisible to user code, so we just update the
 * tables consistently.
 *
 * @param style  Style being torn down.
 *
 * @note  Caller must hold the manager lock.
 * @warning Reserved for FontStyle's own use; do not invoke from elsewhere.
 */
void
FontManager::RemoveStyle(FontStyle* style)
{
	ASSERT(IsLocked());

	FontFamily* family = style->Family();
	if (family == NULL)
		debugger("family is NULL!");

	family->RemoveStyle(style);
	fDelistedStyleHashTable.Remove(FontKey(family->ID(), style->ID()));
}


/**
 * @brief Returns the monotonic catalog revision counter.
 *
 * Bumped on every successful add or remove; clients (BFont) poll this
 * to know when their cached family/style lists are stale.
 *
 * @return Current revision number.
 */
uint32
FontManager::Revision()
{
	return fRevision;
}


/**
 * @brief Registers a FreeType face under (familyID, styleID).
 *
 * Creates a new FontFamily if @a face's family name is unknown, refuses
 * a re-add of the same family/style (returns @c B_NAME_IN_USE), and
 * otherwise wraps the face in a FontStyle, links it into the family,
 * and indexes it in the live style hash.
 *
 * @param face      FreeType face; ownership transfers on success and is
 *                  released by FT_Done_Face on failure paths.
 * @param nodeRef   node_ref of the on-disk font file.
 * @param path      File path to the font.
 * @param familyID  Output: assigned family ID on success.
 * @param styleID   Output: assigned style ID on success.
 * @retval B_OK            On success.
 * @retval B_NAME_IN_USE   The (family, style) pair was already present.
 * @retval B_NO_MEMORY     Allocation of family or style failed.
 *
 * @note  Caller must hold the manager lock.
 */
status_t
FontManager::_AddFont(FT_Face face, node_ref nodeRef, const char* path,
	uint16& familyID, uint16& styleID)
{
	ASSERT(IsLocked());

	BReference<FontFamily> family(_FindFamily(face->family_name));
	bool isNewFontFamily = !family.IsSet();

	if (family.IsSet() && family->HasStyle(face->style_name)) {
		// prevent adding the same style twice
		// (this indicates a problem with the installed fonts maybe?)
		FT_Done_Face(face);
		return B_NAME_IN_USE;
	}

	if (!family.IsSet()) {
		family.SetTo(new (std::nothrow) FontFamily(face->family_name, _NextID()), true);

		if (!family.IsSet() || !fFamilies.BinaryInsert(family, compare_font_families)) {
			FT_Done_Face(face);
			return B_NO_MEMORY;
		}
	}

	FTRACE(("\tadd style: %s, %s\n", face->family_name, face->style_name));

	// the FontStyle takes over ownership of the FT_Face object
	FontStyle* style = new (std::nothrow) FontStyle(nodeRef, path, face, this);

	if (style == NULL || !family->AddStyle(style)) {
		delete style;
		if (isNewFontFamily)
			fFamilies.RemoveItem(family);
		return B_NO_MEMORY;
	}

	familyID = style->Family()->ID();
	styleID = style->ID();

	fStyleHashTable.Put(FontKey(familyID, styleID), style);
	style->ReleaseReference();

	fRevision++;
	return B_OK;
}


/**
 * @brief Removes a style from the live tables and stashes it as delisted.
 *
 * The style continues to exist (kept alive by other references) until
 * the last holder drops it; an eventual FontStyle destructor then
 * calls RemoveStyle() to clean up the delisted entry.
 *
 * @param familyID  Family ID of the style.
 * @param styleID   Style ID of the style.
 * @return  Pointer to the removed style on success, or NULL when the
 *          (familyID, styleID) pair was unknown.
 *
 * @note  Caller must hold the manager lock.
 */
FontStyle*
FontManager::_RemoveFont(uint16 familyID, uint16 styleID)
{
	ASSERT(IsLocked());

	FontKey key(familyID, styleID);
	FontStyle* style = fStyleHashTable.Get(key);
	if (style != NULL) {
		fDelistedStyleHashTable.Put(key, style);
		FontFamily* family = style->Family();
		if (family->RemoveStyle(style) && family->CountStyles() == 0)
			fFamilies.RemoveItem(family);
		fStyleHashTable.Remove(key);
	}

	fRevision++;
	return style;
}


/**
 * @brief Detaches every style from its family and clears all tables.
 *
 * Used by the destructor (and AppFontManager teardown) to break the
 * style->family back-references before the families themselves go
 * away, preventing dangling-pointer callbacks from late destructors.
 */
void
FontManager::_RemoveAllFonts()
{
	fFamilies.MakeEmpty();

	// Disconnect the styles from their families before removing them; once we
	// get to this point, we are in the dtor and don't want them to call back.

	HashMap<FontKey, FontStyle*>::Iterator delisted = fDelistedStyleHashTable.GetIterator();
	while (delisted.HasNext())
		delisted.Next().value->_SetFontFamily(NULL, -1);
	fDelistedStyleHashTable.Clear();

	HashMap<FontKey, BReference<FontStyle> >::Iterator referenced = fStyleHashTable.GetIterator();
	while (referenced.HasNext())
		referenced.Next().value->_SetFontFamily(NULL, -1);
	fStyleHashTable.Clear();
}


/**
 * @brief Binary search of the sorted family list by name.
 *
 * @param name  Family name; NULL is permitted and returns NULL.
 * @return  Family pointer, or NULL when @a name is unknown.
 */
FontFamily*
FontManager::_FindFamily(const char* name) const
{
	if (name == NULL)
		return NULL;

	FontFamily family(name, 0);
	return const_cast<FontFamily*>(fFamilies.BinarySearch(family,
		compare_font_families));
}


/**
 * @brief Linear search of the family list by numeric ID.
 *
 * @param familyID  Numeric family ID.
 * @return  Family pointer, or NULL when @a familyID is unknown.
 */
FontFamily*
FontManager::_FindFamily(uint16 familyID) const
{
	int32 count = fFamilies.CountItems();

	for (int32 i = 0; i < count; i++) {
		FontFamily* family = fFamilies.ItemAt(i);
		if (family->ID() == familyID)
			return family;
	}

	return NULL;
}


/**
 * @brief Allocates the next ascending family/style ID.
 *
 * Subclasses (e.g. AppFontManager) may override to draw IDs from a
 * different range so user fonts cannot collide with system fonts.
 *
 * @return Freshly allocated 16-bit ID.
 */
uint16
FontManager::_NextID()
{
	return fNextID++;
}
