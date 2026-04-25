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
 *   Copyright 2001-2008, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       DarkWyrm <bpmagic@columbus.rr.com>
 *       Axel Dörfler, axeld@pinc-software.de
 */


/**
 * @file FontFamily.cpp
 * @brief Implementation of FontFamily, a sorted collection of FontStyle objects.
 *
 * The styles inside a family are kept in a stable, score-sorted order
 * (regular before bold before italic) so the first-style-of-a-family
 * lookup hands callers a sensible default.  Aggregate flags (fixed,
 * tuned, etc.) are derived lazily and invalidated on add/remove via
 * the @c kInvalidFamilyFlags sentinel.
 */


#include "FontFamily.h"

#include <FontPrivate.h>


/** @brief Sentinel stored in fFlags when the cached aggregate is stale. */
const uint32 kInvalidFamilyFlags = ~(uint32)0;


/**
 * @brief Sorting score used to order styles by usefulness.
 *
 * Regular faces score highest, bold faces next, italic faces lowest;
 * the result is fed into compare_font_styles for a stable sort.
 *
 * @param style  Style to score.
 * @return  Numeric score; higher means "show first".
 */
static int
font_score(const FontStyle* style)
{
	int score = 0;
	if (style->Face() & B_REGULAR_FACE)
		score += 10;
	else {
		if (style->Face() & B_BOLD_FACE)
			score += 5;
		if (style->Face() & B_ITALIC_FACE)
			score--;
	}

	return score;
}


/**
 * @brief Comparator used by BinaryInsert to keep styles in score order.
 *
 * @param a  First style.
 * @param b  Second style.
 * @return  Negative when @a a should come before @a b, positive when after.
 */
static int
compare_font_styles(const FontStyle* a, const FontStyle* b)
{
	// Regular fonts come first, then bold, then italics
	return font_score(b) - font_score(a);
}


//	#pragma mark -


/**
 * @brief Constructs an empty family with the given display name and ID.
 *
 * @param name  Family name; truncated to B_FONT_FAMILY_LENGTH for Be API parity.
 * @param id    Numeric ID assigned by the FontManager.
 */
FontFamily::FontFamily(const char *name, uint16 id)
	:
	fName(name),
	fID(id),
	fNextID(0),
	fFlags(kInvalidFamilyFlags)
{
	fName.Truncate(B_FONT_FAMILY_LENGTH);
		// make sure this family can be found using the Be API
}


/**
 * @brief Returns the family's display name.
 *
 * @return  Pointer to a NUL-terminated name owned by the family.
 */
const char*
FontFamily::Name() const
{
	return fName.String();
}


/**
 * @brief Inserts @a style into the family in sorted order.
 *
 * Refuses duplicates (identified by name) and assigns the new style a
 * fresh per-family ID. The aggregate flag cache is invalidated.
 *
 * @param style  Style to add; must remain owned by the caller / manager.
 * @return  true on insertion, false on duplicate name or out-of-memory.
 */
bool
FontFamily::AddStyle(FontStyle* style)
{
	if (!style)
		return false;

	// Don't add if it already is in the family.
	int32 count = fStyles.CountItems();
	for (int32 i = 0; i < count; i++) {
		FontStyle *item = fStyles.ItemAt(i);
		if (!strcmp(item->Name(), style->Name()))
			return false;
	}

	if (!fStyles.BinaryInsert(style, compare_font_styles))
		return false;

	style->_SetFontFamily(this, fNextID++);

	// force a refresh if a request for font flags is needed
	fFlags = kInvalidFamilyFlags;

	return true;
}


/**
 * @brief Detaches @a style from the family.
 *
 * Does not delete the style object; the FontManager remains responsible
 * for its lifetime. The aggregate flag cache is invalidated.
 *
 * @param style  Style to remove.
 * @return  true when the style was present and removed.
 */
bool
FontFamily::RemoveStyle(FontStyle* style)
{
	if (style == NULL)
		return false;

	if (!fStyles.RemoveItem(style))
		return false;

	// force a refresh if a request for font flags is needed
	fFlags = kInvalidFamilyFlags;
	return true;
}


/**
 * @brief Returns the number of styles registered under the family.
 *
 * @return  Style count; zero for a freshly constructed family.
 */
int32
FontFamily::CountStyles() const
{
	return fStyles.CountItems();
}


/**
 * @brief Linear lookup by exact style name.
 *
 * @param name  Style name to match (e.g. "Bold", "Regular").
 * @return  Matching FontStyle, or NULL when @a name is NULL or unknown.
 */
FontStyle*
FontFamily::_FindStyle(const char* name) const
{
	int32 count = fStyles.CountItems();
	if (!name || count < 1)
		return NULL;

	for (int32 i = 0; i < count; i++) {
		FontStyle *style = fStyles.ItemAt(i);
		if (!strcmp(style->Name(), name))
			return style;
	}

	return NULL;
}


/**
 * @brief Returns true when a style with the given name is registered.
 *
 * @param styleName  Name to look up.
 * @return  true when present, false otherwise.
 */
bool
FontFamily::HasStyle(const char *styleName) const
{
	return _FindStyle(styleName) != NULL;
}


/**
 * @brief Returns the style at @a index in the sorted style list.
 *
 * @param index  Zero-based index in the sorted list.
 * @return  Style pointer, or NULL when @a index is out of range.
 */
FontStyle*
FontFamily::StyleAt(int32 index) const
{
	return fStyles.ItemAt(index);
}


/**
 * @brief Locates a style by name, with common alias fallbacks.
 *
 * Tries the exact name first, then alternate forms ("Roman" /
 * "Regular" / "Book") and the Italic <-> Oblique aliasing pair.
 *
 * @param name  Style name requested by the caller.
 * @return  The matching FontStyle, or NULL when no acceptable
 *          alternative exists.
 *
 * @note  The returned style belongs to the family and must not be deleted.
 */
FontStyle*
FontFamily::GetStyle(const char *name) const
{
	if (name == NULL || !name[0])
		return NULL;

	FontStyle* style = _FindStyle(name);
	if (style != NULL)
		return style;

	// try alternative names

	if (!strcmp(name, "Roman") || !strcmp(name, "Regular")
		|| !strcmp(name, "Book")) {
		style = _FindStyle("Roman");
		if (style == NULL) {
			style = _FindStyle("Regular");
			if (style == NULL)
				style = _FindStyle("Book");
		}
		return style;
	}

	BString alternative = name;
	if (alternative.FindFirst("Italic") >= 0) {
		alternative.ReplaceFirst("Italic", "Oblique");
		return _FindStyle(alternative.String());
	}
	if (alternative.FindFirst("Oblique") >= 0) {
		alternative.ReplaceFirst("Oblique", "Italic");
		return _FindStyle(alternative.String());
	}

	return NULL;
}


/**
 * @brief Returns the style whose Face() bits exactly match @a face.
 *
 * Only the slant/weight/width bits are considered; other bits are
 * filtered out before comparison. An empty mask is treated as
 * @c B_REGULAR_FACE so callers can ask for "the default" cleanly.
 *
 * @param face  Desired face mask.
 * @return  The matching style, or NULL when none of the family's
 *          styles has the same Face() value.
 */
FontStyle*
FontFamily::GetStyleMatchingFace(uint16 face) const
{
	// Other face flags do not impact the font selection (they are applied
	// during drawing)
	face &= B_BOLD_FACE | B_ITALIC_FACE | B_REGULAR_FACE | B_CONDENSED_FACE
		| B_LIGHT_FACE | B_HEAVY_FACE;
	if (face == 0)
		face = B_REGULAR_FACE;

	int32 count = fStyles.CountItems();
	for (int32 i = 0; i < count; i++) {
		FontStyle* style = fStyles.ItemAt(i);

		if (style->Face() == face)
			return style;
	}

	return NULL;
}


/**
 * @brief Returns the aggregate flag word, computing it lazily on first use.
 *
 * Walks the styles once and ORs in fixed-width, full/half-fixed, and
 * tuned-strike bits, then caches the result until the next add/remove
 * resets fFlags to ::kInvalidFamilyFlags.
 *
 * @return Composite @c B_*_FACE / @c B_*_FONT flag mask for the family.
 */
uint32
FontFamily::Flags()
{
	if (fFlags == kInvalidFamilyFlags) {
		fFlags = 0;

		int32 count = fStyles.CountItems();
		for (int32 i = 0; i < count; i++) {
			FontStyle* style = fStyles.ItemAt(i);

			if (style->IsFixedWidth())
				fFlags |= B_IS_FIXED;
			if (style->IsFullAndHalfFixed())
				fFlags |= B_PRIVATE_FONT_IS_FULL_AND_HALF_FIXED;
			if (style->TunedCount() > 0)
				fFlags |= B_HAS_TUNED_FONT;
		}
	}

	return fFlags;
}
