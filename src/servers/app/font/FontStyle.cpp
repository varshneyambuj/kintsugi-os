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
 * @file FontStyle.cpp
 * @brief Implementation of FontStyle, the per-style descriptor over an FT_Face.
 *
 * Each FontStyle owns the FT_Face for one installed font file and
 * exposes the metrics and flags the higher-level app_server code needs
 * (height, fixed-width detection, file path, face mask). The class also
 * detects "full and half" CJK widths by scanning the basic-Latin range
 * after the FreeType-reported fixed-width flag is found false.
 */


#include "FontFamily.h"
#include "FontManager.h"
#include "ServerFont.h"

#include <FontPrivate.h>

#include <Entry.h>


/** @brief Process-wide lock guarding mutating access (e.g. UpdateFace) to FontStyle. */
static BLocker sFontLock("font lock");


/**
 * @brief Constructs a FontStyle taking ownership of @a face.
 *
 * Computes scalable or bitmap-strike-derived height metrics, derives the
 * Be face mask from the style name, and (for non-fixed-width faces) walks
 * the printable ASCII range to detect "full and half" CJK widths.
 *
 * @param nodeRef      node_ref of the font file on disk.
 * @param path         File path to the font.
 * @param face         FreeType face handle; ownership transfers to this style
 *                     and is kept open until destruction.
 * @param fontManager  Owning FontManager used by RemoveStyle in the dtor.
 */
FontStyle::FontStyle(node_ref& nodeRef, const char* path, FT_Face face,
	FontManager* fontManager)
	:
	fFreeTypeFace(face),
	fName(face->style_name),
	fPath(path),
	fNodeRef(nodeRef),
	fFamily(NULL),
	fID(0),
	fBounds(0, 0, 0, 0),
	fFace(_TranslateStyleToFace(face->style_name)),
	fFullAndHalfFixed(false),
	fFontData(NULL),
	fFontManager(fontManager)
{
	fName.Truncate(B_FONT_STYLE_LENGTH);
		// make sure this style can be found using the Be API

	if (IsScalable()) {
		fHeight.ascent = (double)face->ascender / face->units_per_EM;
		fHeight.descent = (double)-face->descender / face->units_per_EM;
			// FT2's descent numbers are negative. Be's is positive

		// FT2 doesn't provide a linegap, but according to the docs, we can
		// calculate it because height = ascending + descending + leading
		fHeight.leading = (double)(face->height - face->ascender
			+ face->descender) / face->units_per_EM;
	} else {
		// We don't have global metrics, get them from a bitmap
		FT_Pos size = face->available_sizes[0].size;
		for (int i = 1; i < face->num_fixed_sizes; i++)
			size = max_c(size, face->available_sizes[i].size);
		FT_Set_Pixel_Sizes(face, 0, size / 64);
			// Size is encoded as 26.6 fixed point, while FT_Set_Pixel_Sizes
			// uses the integer unencoded value

		FT_Size_Metrics metrics = face->size->metrics;
		fHeight.ascent = (double)metrics.ascender / size;
		fHeight.descent = (double)-metrics.descender / size;
		fHeight.leading = (double)(metrics.height - metrics.ascender
			+ metrics.descender) / size;
	}

	if (IsFixedWidth())
		return;

	// manually check if all applicable chars are the same width

	FT_Int32 loadFlags = FT_LOAD_NO_SCALE | FT_LOAD_TARGET_NORMAL;
	if (FT_Load_Char(face, (uint32)' ', loadFlags) != 0)
		return;

	int firstWidth = face->glyph->advance.x;
	for (uint32 c = ' ' + 1; c <= 0x7e; c++) {
		if (FT_Load_Char(face, c, loadFlags) != 0)
			return;

		if (face->glyph->advance.x != firstWidth)
			return;
	}

	fFullAndHalfFixed = true;
}


/**
 * @brief Destroys the style, removes it from its FontManager, and frees its face.
 *
 * If the style is still attached to a family the manager's catalog is
 * updated under its lock first; the FT_Face is then released, followed
 * by any in-memory font data the style was owning.
 */
FontStyle::~FontStyle()
{
	// make sure the font server is ours
	if (fFamily.IsSet() && fFontManager->Lock()) {
		fFontManager->RemoveStyle(this);
		fFontManager->Unlock();
	}

	FT_Done_Face(fFreeTypeFace);

	if (fFontData != NULL)
		free(fFontData);
}


/**
 * @brief Acquires the static font lock, blocking until available.
 *
 * @return  true once the lock has been acquired.
 */
bool
FontStyle::Lock()
{
	return sFontLock.Lock();
}


/**
 * @brief Releases the static font lock previously acquired with Lock().
 */
void
FontStyle::Unlock()
{
	sFontLock.Unlock();
}


/**
 * @brief Computes the font height for a given em size.
 *
 * Scales the cached @c fHeight ratios (ascender, descender, leading) by
 * @a size so callers get pixel values directly.
 *
 * @param size    Em size in pixels.
 * @param height  Output structure populated with ascent/descent/leading.
 */
void
FontStyle::GetHeight(float size, font_height& height) const
{
	height.ascent = fHeight.ascent * size;
	height.descent = fHeight.descent * size;
	height.leading = fHeight.leading * size;
}


/**
 * @brief Returns the on-disk path to the style's font file.
 *
 * @return  Pointer to a NUL-terminated path; valid for the style's lifetime.
 */
const char*
FontStyle::Path() const
{
	return fPath.Path();
}


/**
 * @brief Refreshes the cached path after the style's directory has moved.
 *
 * Reconstructs an entry_ref from the new parent node_ref plus the
 * current leaf name, so subsequent Path() queries return the new
 * location.
 *
 * @param parentNodeRef  node_ref of the directory the style now lives in.
 */
void
FontStyle::UpdatePath(const node_ref& parentNodeRef)
{
	entry_ref ref;
	ref.device = parentNodeRef.device;
	ref.directory = parentNodeRef.node;
	ref.set_name(fPath.Leaf());

	fPath.SetTo(&ref);
}


/**
 * @brief Returns the private BFont flag word for this style.
 *
 * Encodes writing direction and a few capability bits (fixed width,
 * full/half fixed, presence of tuned bitmap strikes, presence of
 * kerning) the BFont private API exposes to clients.
 *
 * @return  Flag word using the @c B_PRIVATE_FONT_* and @c B_*_FACE bits.
 */
uint32
FontStyle::Flags() const
{
	uint32 flags = uint32(Direction()) << B_PRIVATE_FONT_DIRECTION_SHIFT;

	if (IsFixedWidth())
		flags |= B_IS_FIXED;
	if (IsFullAndHalfFixed())
		flags |= B_PRIVATE_FONT_IS_FULL_AND_HALF_FIXED;
	if (TunedCount() > 0)
		flags |= B_HAS_TUNED_FONT;
	if (HasKerning())
		flags |= B_PRIVATE_FONT_HAS_KERNING;

	return flags;
}


/**
 * @brief Merges this style's face bits over the request's other attributes.
 *
 * Strips the slant/weight/width bits this style controls from @a face
 * and replaces them with the style's own Face() bits, so the renderer
 * can emulate any attributes the style itself does not provide on top
 * of the actual style.
 *
 * @param face  Caller-supplied face mask.
 * @return  Composite face mask describing the style plus the caller's
 *          unaltered attributes.
 *
 * @todo  Improve coverage of additional face attributes.
 */
uint16
FontStyle::PreservedFace(uint16 face) const
{
	// TODO: make this better
	face &= ~(B_REGULAR_FACE | B_BOLD_FACE | B_ITALIC_FACE | B_CONDENSED_FACE
		| B_LIGHT_FACE | B_HEAVY_FACE);
	face |= Face();

	return face;
}


/**
 * @brief Replaces the underlying FreeType face with @a face.
 *
 * Used when a font file on disk is rewritten; the new face must keep
 * the same style name or the call is rejected so cached signatures and
 * client-side lookups stay valid.
 *
 * @param face  New FreeType face; ownership transfers on success.
 * @return  B_OK on success, B_BAD_VALUE when style name diverges, or
 *          B_ERROR if FontStyle::Lock() was not held.
 *
 * @note Caller must hold FontStyle::Lock().
 */
status_t
FontStyle::UpdateFace(FT_Face face)
{
	if (!sFontLock.IsLocked()) {
		debugger("UpdateFace() called without having locked FontStyle!");
		return B_ERROR;
	}

	// we only accept the face if it hasn't change its style

	BString name = face->style_name;
	name.Truncate(B_FONT_STYLE_LENGTH);

	if (name != fName)
		return B_BAD_VALUE;

	FT_Done_Face(fFreeTypeFace);
	fFreeTypeFace = face;
	return B_OK;
}


/**
 * @brief Re-parents this style under @a family with the given per-family ID.
 *
 * Removes the style from its previous family if any, sets the new
 * parent reference, and stores the per-family numeric ID.
 *
 * @param family  New owning family, or NULL to detach.
 * @param id      Style ID assigned by the family.
 */
void
FontStyle::_SetFontFamily(FontFamily* family, uint16 id)
{
	if (fFamily.IsSet())
		fFamily->RemoveStyle(this);

	fFamily.SetTo(family);
	fID = id;
}


/**
 * @brief Heuristically derives the Be face mask from a style name.
 *
 * Scans @a name case-insensitively for substrings such as "Bold",
 * "Italic", "Oblique", "Condensed", "Light", "Thin", "Heavy", and
 * "Black", combining matches into a B_*_FACE bit mask.
 *
 * @param name  Style name as reported by FreeType.
 * @return  Face mask; B_REGULAR_FACE when no keyword matched.
 *
 * @note The detection is lexical and may misfire on stylized names.
 */
uint16
FontStyle::_TranslateStyleToFace(const char* name) const
{
	if (name == NULL)
		return 0;

	BString string(name);
	uint16 face = 0;

	if (string.IFindFirst("bold") >= 0)
		face |= B_BOLD_FACE;

	if (string.IFindFirst("italic") >= 0
		|| string.IFindFirst("oblique") >= 0)
		face |= B_ITALIC_FACE;

	if (string.IFindFirst("condensed") >= 0)
		face |= B_CONDENSED_FACE;

	if (string.IFindFirst("light") >= 0
		|| string.IFindFirst("thin") >= 0)
		face |= B_LIGHT_FACE;

	if (string.IFindFirst("heavy") >= 0
		|| string.IFindFirst("black") >= 0)
		face |= B_HEAVY_FACE;

	if (face == 0)
		return B_REGULAR_FACE;

	return face;
}


/**
 * @brief Adopts an in-memory font data buffer for this style.
 *
 * Frees any previously held buffer to avoid leaks, then takes
 * ownership of @a location which must have been allocated with malloc()
 * since SetFontData() and the destructor use free().
 *
 * @param location  Pointer to font bytes; ownership transfers.
 * @param size      Length of the buffer in bytes.
 *
 * @warning The buffer must be malloc-allocated; it is released with free().
 */
void
FontStyle::SetFontData(FT_Byte* location, uint32 size)
{
	// if memory was already allocated here, we should free it so it's not leaked
	if (fFontData != NULL)
		free(fFontData);

	fFontDataSize = size;
	fFontData = location;
}
