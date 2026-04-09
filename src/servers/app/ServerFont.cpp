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
 *       Jérôme Duval, jerome.duval@free.fr
 *       Michael Lotz <mmlr@mlotz.ch>
 *       Stephan Aßmus <superstippi@gmx.de>
 */

/** @file ServerFont.cpp
    @brief Server-side font object wrapping a FreeType FontStyle with metrics and glyph query operations. */


#include "ServerFont.h"

#include "Angle.h"
#include "AppFontManager.h"
#include "GlyphLayoutEngine.h"
#include "GlobalFontManager.h"
#include "truncate_string.h"
#include "utf8_functions.h"

#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H

#ifdef FONTCONFIG_ENABLED

#include <fontconfig.h>
#include <fcfreetype.h>

#endif // FONTCONFIG_ENABLED

#include <Shape.h>
#include <String.h>
#include <UnicodeBlockObjects.h>
#include <UTF8.h>

#include <agg_bounding_rect.h>

#include <stdio.h>
#include <string.h>


// functions needed to convert a freetype vector graphics to a BShape

/** @brief Converts a FreeType 26.6 fixed-point vector to a BPoint.
    @param vector Pointer to the FT_Vector to convert.
    @return The resulting BPoint in floating-point coordinates. */
inline BPoint
VectorToPoint(const FT_Vector *vector)
{
	BPoint result;
	result.x = float(vector->x) / 64;
	result.y = -float(vector->y) / 64;
	return result;
}


/** @brief FreeType outline decomposition callback: move-to.
    @param to   Target point for the move.
    @param user Pointer to the target BShape.
    @return 0 on success. */
int
MoveToFunc(const FT_Vector *to, void *user)
{
	((BShape *)user)->MoveTo(VectorToPoint(to));
	return 0;
}


/** @brief FreeType outline decomposition callback: line-to.
    @param to   Target endpoint of the line.
    @param user Pointer to the target BShape.
    @return 0 on success. */
int
LineToFunc(const FT_Vector *to, void *user)
{
	((BShape *)user)->LineTo(VectorToPoint(to));
	return 0;
}


/** @brief FreeType outline decomposition callback: conic (quadratic) Bezier.
    @param control  Single control point of the conic curve.
    @param to       Target endpoint.
    @param user     Pointer to the target BShape.
    @return 0 on success. */
int
ConicToFunc(const FT_Vector *control, const FT_Vector *to, void *user)
{
	BPoint controls[3];

	controls[0] = VectorToPoint(control);
	controls[1] = controls[0];
	controls[2] = VectorToPoint(to);

	((BShape *)user)->BezierTo(controls);
	return 0;
}


/** @brief FreeType outline decomposition callback: cubic Bezier.
    @param control1 First control point.
    @param control2 Second control point.
    @param to       Target endpoint.
    @param user     Pointer to the target BShape.
    @return 0 on success. */
int
CubicToFunc(const FT_Vector *control1, const FT_Vector *control2, const FT_Vector *to, void *user)
{
	BPoint controls[3];

	controls[0] = VectorToPoint(control1);
	controls[1] = VectorToPoint(control2);
	controls[2] = VectorToPoint(to);

	((BShape *)user)->BezierTo(controls);
	return 0;
}


/** @brief Returns whether the given Unicode code point is a whitespace character.
    @param charCode The Unicode code point to test.
    @return true if charCode is a recognised whitespace character. */
inline bool
is_white_space(uint32 charCode)
{
	switch (charCode) {
		case 0x0009:	/* tab */
		case 0x000b:	/* vertical tab */
		case 0x000c:	/* form feed */
		case 0x0020:	/* space */
		case 0x00a0:	/* non breaking space */
		case 0x000a:	/* line feed */
		case 0x000d:	/* carriage return */
		case 0x2028:	/* line separator */
		case 0x2029:	/* paragraph separator */
			return true;
	}

	return false;
}


//	#pragma mark -


/** @brief Constructs a ServerFont from a FontStyle with explicit metrics parameters.
    @param style          The FontStyle this font is an instance of.
    @param size           Character size in points.
    @param rotation       Rotation in degrees.
    @param shear          Shear (slant) in degrees; 45 <= shear <= 135.
    @param falseBoldWidth Width added to each glyph for synthetic bold.
    @param flags          Style flags as defined in Font.h.
    @param spacing        String spacing mode as defined in Font.h. */
ServerFont::ServerFont(FontStyle& style, float size, float rotation,
		float shear, float falseBoldWidth, uint16 flags, uint8 spacing)
	:
	fStyle(&style, false),
	fSize(size),
	fRotation(rotation),
	fShear(shear),
	fFalseBoldWidth(falseBoldWidth),
	fBounds(0, 0, 0, 0),
	fFlags(flags),
	fSpacing(spacing),
	fDirection(style.Direction()),
	fFace(style.Face()),
	fEncoding(B_UNICODE_UTF8)
{
}


/** @brief Default constructor. Initialises the font to the global default plain font. */
ServerFont::ServerFont()
	:
	fStyle(NULL)
{
	*this = *gFontManager->DefaultPlainFont();
}


/** @brief Copy constructor.
    @param font The ServerFont to copy from. */
ServerFont::ServerFont(const ServerFont &font)
	:
	fStyle(NULL)
{
	*this = font;
}


/** @brief Destructor. Removes the font's reference to its owning FontStyle. */
ServerFont::~ServerFont()
{
}


/** @brief Assignment operator; copies all font attributes from another ServerFont.
    @param font The source ServerFont to copy from.
    @return Reference to this ServerFont. */
ServerFont&
ServerFont::operator=(const ServerFont& font)
{
	fSize = font.fSize;
	fRotation = font.fRotation;
	fShear = font.fShear;
	fFalseBoldWidth = font.fFalseBoldWidth;
	fFlags = font.fFlags;
	fSpacing = font.fSpacing;
	fEncoding = font.fEncoding;
	fBounds = font.fBounds;

	SetStyle(font.fStyle);

	fFace = font.fFace;

	return *this;
}


/** @brief Equality comparison operator.
    @param other The ServerFont to compare against.
    @return true if all font attributes (family, style, size, rotation, shear, etc.) are equal. */
bool
ServerFont::operator==(const ServerFont& other) const
{
	if (GetFamilyAndStyle() != other.GetFamilyAndStyle())
		return false;

	return fSize == other.fSize && fRotation == other.fRotation
		&& fShear == other.fShear && fFalseBoldWidth == other.fFalseBoldWidth
		&& fFlags == other.fFlags && fSpacing == other.fSpacing
		&& fEncoding == other.fEncoding && fBounds == other.fBounds
		&& fDirection == other.fDirection && fFace == other.fFace;
}


/** @brief Returns the number of tuned (hinted) strike sizes available for this font.
    @return The count of tuned strikes in the underlying FontStyle. */
int32
ServerFont::CountTuned()
{
	return fStyle->TunedCount();
}


/** @brief Returns the file format of the font.
    @return The font_file_format (typically B_TRUETYPE_WINDOWS). */
font_file_format
ServerFont::FileFormat()
{
	return fStyle->FileFormat();
}


/** @brief Returns the style name of this font.
    @return A C string containing the style name. */
const char*
ServerFont::Style() const
{
	return fStyle->Name();
}


/** @brief Returns the family name of this font.
    @return A C string containing the family name. */
const char*
ServerFont::Family() const
{
	return fStyle->Family()->Name();
}


/** @brief Sets the FontStyle for this font, updating face and direction accordingly.
    @param style Pointer to the new FontStyle. No-op if style is NULL or already set. */
void
ServerFont::SetStyle(FontStyle* style)
{
	if (style && style != fStyle) {
		fStyle.SetTo(style, false);

		fFace = fStyle->PreservedFace(fFace);
		fDirection = fStyle->Direction();

		// invalidate fBounds
		fBounds.Set(0, -1, 0, -1);
	}
}


/** @brief Sets the font family and style by numeric IDs, searching the global and app font managers.
    @param familyID    Numeric family identifier.
    @param styleID     Numeric style identifier within the family.
    @param fontManager Optional AppFontManager used as a fallback for application-installed fonts.
    @return B_OK on success, B_ERROR if the specified family/style combination was not found. */
status_t
ServerFont::SetFamilyAndStyle(uint16 familyID, uint16 styleID,
	AppFontManager* fontManager)
{

	BReference<FontStyle> style;

	if (gFontManager->Lock()) {
		style.SetTo(gFontManager->GetStyle(familyID, styleID), false);

		gFontManager->Unlock();
	}

	if (style == NULL) {
		if (fontManager != NULL && fontManager->Lock()) {
			style.SetTo(fontManager->GetStyle(familyID, styleID), false);

			fontManager->Unlock();
		}

		if (style == NULL)
			return B_ERROR;
	}

	SetStyle(style);

	// invalidate fBounds
	fBounds.Set(0, -1, 0, -1);

	return B_OK;
}


/** @brief Sets the font family and style from a combined 32-bit font ID.
    @param fontID      The combined family/style ID (family in upper 16 bits, style in lower 16 bits).
    @param fontManager Optional AppFontManager used as a fallback.
    @return B_OK on success, B_ERROR if the font was not found. */
status_t
ServerFont::SetFamilyAndStyle(uint32 fontID, AppFontManager* fontManager)
{
	uint16 style = fontID & 0xFFFF;
	uint16 family = (fontID & 0xFFFF0000) >> 16;

	return SetFamilyAndStyle(family, style, fontManager);
}


/** @brief Sets the font size in points, invalidating the cached bounding box.
    @param value The new font size in points. */
void
ServerFont::SetSize(float value)
{
	fSize = value;

	// invalidate fBounds
	fBounds.Set(0, -1, 0, -1);
}


/** @brief Sets the font face flags, searching for a matching style within the same family if needed.
    @param face The face flags to apply (e.g. B_BOLD_FACE, B_ITALIC_FACE).
    @return B_OK on success, B_ERROR if no matching style was found. */
status_t
ServerFont::SetFace(uint16 face)
{
	// Don't confuse the Be API "face" with the Freetype face, which is just
	// an index in case a single font file exports multiple font faces. The
	// FontStyle class takes care of mapping the font style name to the Be
	// API face flags in FontStyle::_TranslateStyleToFace().

	if (fStyle->PreservedFace(face) == face) {
		fFace = face;
		return B_OK;
	}

	BReference <FontStyle> style;
	uint16 familyID = FamilyID();
	if (gFontManager->Lock()) {
		int32 count = gFontManager->CountStyles(familyID);
		for (int32 i = 0; i < count; i++) {
			style.SetTo(gFontManager->GetStyleByIndex(familyID, i), false);
			if (style == NULL)
				break;
			if (style->PreservedFace(face) == face)
				break;
			else
				style = NULL;
		}

		gFontManager->Unlock();
	}

	if (!style)
		return B_ERROR;

	fFace = face;
	SetStyle(style);

	// invalidate fBounds
	fBounds.Set(0, -1, 0, -1);

	return B_OK;
}


/** @brief Returns the combined family and style ID for this font.
    @return A 32-bit value with the family ID in the upper 16 bits and style ID in the lower 16 bits,
            or 0 if no style is set. */
uint32
ServerFont::GetFamilyAndStyle() const
{
	if (fStyle == NULL || fStyle->Family() == NULL)
		return 0;

	return (FamilyID() << 16) | StyleID();
}


/** @brief Retrieves a transformed FreeType face, locking the FontStyle.

    The caller MUST release the face via PutTransformedFace() when done.
    @param rotate If true, applies the font's rotation to the face matrix.
    @param shear  If true, applies the font's shear to the face matrix.
    @return The FT_Face on success, or NULL if unavailable. */
FT_Face
ServerFont::GetTransformedFace(bool rotate, bool shear) const
{
	fStyle->Lock();
	FT_Face face = fStyle->FreeTypeFace();
	if (!face) {
		fStyle->Unlock();
		return NULL;
	}

	FT_Set_Char_Size(face, 0, int32(fSize * 64), 72, 72);

	if ((rotate && fRotation != 0) || (shear && fShear != 90)) {
		FT_Matrix rmatrix, smatrix;

		Angle rotationAngle(fRotation);
		rmatrix.xx = (FT_Fixed)( rotationAngle.Cosine() * 0x10000);
		rmatrix.xy = (FT_Fixed)(-rotationAngle.Sine() * 0x10000);
		rmatrix.yx = (FT_Fixed)( rotationAngle.Sine() * 0x10000);
		rmatrix.yy = (FT_Fixed)( rotationAngle.Cosine() * 0x10000);

		Angle shearAngle(fShear);
		smatrix.xx = (FT_Fixed)(0x10000);
		smatrix.xy = (FT_Fixed)(-shearAngle.Cosine() * 0x10000);
		smatrix.yx = (FT_Fixed)(0);
		smatrix.yy = (FT_Fixed)(0x10000);

		// Multiply togheter and apply transform
		FT_Matrix_Multiply(&rmatrix, &smatrix);
		FT_Set_Transform(face, &smatrix, NULL);
	}

	// fStyle will be unlocked in PutTransformedFace()
	return face;
}


/** @brief Releases a transformed FreeType face obtained from GetTransformedFace(), unlocking the FontStyle.
    @param face The FT_Face to release. */
void
ServerFont::PutTransformedFace(FT_Face face) const
{
	// Reset transformation
	FT_Set_Transform(face, NULL, NULL);
	fStyle->Unlock();
}


/** @brief Decomposes the outlines of the specified characters into BShape objects.
    @param charArray  UTF-8 encoded string of characters to decompose.
    @param numChars   Number of characters in charArray.
    @param shapeArray Caller-allocated array of BShape pointers to receive the outlines.
    @return B_OK on success; B_BAD_DATA if arguments are invalid; B_NO_MEMORY or B_ERROR on failure. */
status_t
ServerFont::GetGlyphShapes(const char charArray[], int32 numChars,
	BShape* shapeArray[]) const
{
	if (!charArray || numChars <= 0 || !shapeArray)
		return B_BAD_DATA;

	FT_Face face = GetTransformedFace(true, true);
	if (!face)
		return B_ERROR;

	FT_Outline_Funcs funcs;
	funcs.move_to = MoveToFunc;
	funcs.line_to = LineToFunc;
	funcs.conic_to = ConicToFunc;
	funcs.cubic_to = CubicToFunc;
	funcs.shift = 0;
	funcs.delta = 0;

	const char* string = charArray;
	for (int i = 0; i < numChars; i++) {
		shapeArray[i] = new (std::nothrow) BShape();
		if (shapeArray[i] == NULL) {
			PutTransformedFace(face);
			return B_NO_MEMORY;
		}
		FT_Load_Char(face, UTF8ToCharCode(&string), FT_LOAD_NO_BITMAP);
		FT_Outline outline = face->glyph->outline;
		FT_Outline_Decompose(&outline, &funcs, shapeArray[i]);
		shapeArray[i]->Close();
	}

	PutTransformedFace(face);
	return B_OK;
}


#ifdef FONTCONFIG_ENABLED

/** @brief Binary-searches the Unicode block map for the block containing the given code point.
    @param codePoint  The Unicode code point to locate.
    @param startGuess Starting index for the binary search (0 for no hint).
    @return Index into kUnicodeBlockMap of the enclosing block, or -1 if not found. */
static
int32
FindBlockForCodepoint(uint32 codePoint, uint32 startGuess)
{
	uint32 min = 0;
	uint32 max = kNumUnicodeBlockRanges;
	uint32 guess = (max + min) / 2;

	if (startGuess > 0)
		guess = startGuess;

	if (codePoint > kUnicodeBlockMap[max-1].end)
		return -1;

	while ((max >= min) && (guess < kNumUnicodeBlockRanges)) {
		uint32 start = kUnicodeBlockMap[guess].start;
		uint32 end = kUnicodeBlockMap[guess].end;

		if (start <= codePoint && end >= codePoint)
			return guess;

		if (end < codePoint) {
			min = guess + 1;
		} else {
			max = guess - 1;
		}

		guess = (max + min) / 2;
	}

	return -1;
}

/** @brief Parses a fontconfig character map page and updates the unicode_block bitmap.

    See fontconfig docs for FcCharSetFirstPage and FcCharSetNextPage for details on format.
    @param charMap        A fontconfig character map page.
    @param baseCodePoint  The base code point returned by fontconfig for this page.
    @param blocksForMap   The unicode_block to update with the blocks found in the map. */
static
void
ParseFcMap(FcChar32 charMap[], FcChar32 baseCodePoint, unicode_block& blocksForMap)
{
	uint32 block = 0;
	const uint8 BITS_PER_BLOCK = 32;
	uint32 currentCodePoint = 0;

	if (baseCodePoint > kUnicodeBlockMap[kNumUnicodeBlockRanges-1].end)
		return;

	for (int i = 0; i < FC_CHARSET_MAP_SIZE; ++i) {
		FcChar32 curMapBlock = charMap[i];
		int32 rangeStart = -1;
		int32 startBlock = -1;
		int32 endBlock = -1;
		uint32 startPoint = 0;

		currentCodePoint = baseCodePoint + block;

		for (int bit = 0; bit < BITS_PER_BLOCK; ++bit) {
			if (curMapBlock == 0 && startBlock < 0)
				// if no more bits are set then short-circuit the loop
				break;

			if ((curMapBlock & 0x1) != 0 && rangeStart < 0) {
				rangeStart = bit;
				startPoint = currentCodePoint + rangeStart;
				startBlock = FindBlockForCodepoint(startPoint, 0);
				if (startBlock >= 0) {
					blocksForMap = blocksForMap
						| kUnicodeBlockMap[startBlock].block;
				}
			} else if (rangeStart >= 0 && startBlock >= 0) {
					// when we find an empty bit, that's the end of the range
				uint32 endPoint = currentCodePoint + (bit - 1);

				endBlock = FindBlockForCodepoint(endPoint,
					startBlock);
					// start the binary search at the block where we found the
					// start codepoint to ideally find the end in the same
					// block.
				++startBlock;

				while (startBlock <= endBlock) {
					// if the starting codepoint is found in a different block
					// than the ending codepoint, we should add all the blocks
					// inbetween.
					blocksForMap = blocksForMap
						| kUnicodeBlockMap[startBlock].block;
					++startBlock;
				}

				startBlock = -1;
				endBlock = -1;
				rangeStart = -1;
			}

			curMapBlock >>= 1;
		}

		if (rangeStart >= 0 && startBlock >= 0) {
				// if we hit the end of the block and had
				// found a start of the range then we
				// should end the range at the end of the block
			uint32 endPoint = currentCodePoint + BITS_PER_BLOCK - 1;

			endBlock = FindBlockForCodepoint(endPoint,
				startBlock);
				// start the binary search at the block where we found the
				// start codepoint to ideally find the end in the same
				// block.
			++startBlock;

			while (startBlock <= endBlock) {
				// if the starting codepoint is found in a different block
				// than the ending codepoint, we should add all the blocks
				// inbetween.
				blocksForMap = blocksForMap
					| kUnicodeBlockMap[startBlock].block;
				++startBlock;
			}
		}

		block += BITS_PER_BLOCK;
	}
}

#endif // FONTCONFIG_ENABLED


/** @brief Returns a bitmap indicating which Unicode blocks are covered by this font.
    @param blocksForFont Output parameter receiving the unicode_block bitmap.
    @return B_OK; the bitmap will be empty if fontconfig is not available or an error occurred. */
status_t
ServerFont::GetUnicodeBlocks(unicode_block& blocksForFont)
{
	blocksForFont = unicode_block();

#ifdef FONTCONFIG_ENABLED
	FT_Face face = GetTransformedFace(true, true);
	if (face == NULL)
		return B_ERROR;

	FcCharSet *charSet = FcFreeTypeCharSet(face, NULL);
	if (charSet == NULL) {
		PutTransformedFace(face);
		return B_ERROR;
	}

	FcChar32 charMap[FC_CHARSET_MAP_SIZE];
	FcChar32 next = 0;
	FcChar32 baseCodePoint = FcCharSetFirstPage(charSet, charMap, &next);

	while ((baseCodePoint != FC_CHARSET_DONE) && (next != FC_CHARSET_DONE)) {
		ParseFcMap(charMap, baseCodePoint, blocksForFont);
		baseCodePoint = FcCharSetNextPage(charSet, charMap, &next);
	}

	FcCharSetDestroy(charSet);
	PutTransformedFace(face);
#endif // FONTCONFIG_ENABLED

	return B_OK;
}

/** @brief Checks whether the font contains any character in the given Unicode code-point range.
    @param start     Start of the Unicode block range (inclusive).
    @param end       End of the Unicode block range (inclusive).
    @param hasBlock  Output parameter set to true if at least one code point in [start, end] is present.
    @return B_OK; hasBlock will be false if fontconfig is unavailable or an error occurred. */
status_t
ServerFont::IncludesUnicodeBlock(uint32 start, uint32 end, bool& hasBlock)
{
	hasBlock = false;

#ifdef FONTCONFIG_ENABLED
	FT_Face face = GetTransformedFace(true, true);
	if (face == NULL)
		return B_ERROR;

	FcCharSet *charSet = FcFreeTypeCharSet(face, NULL);
	if (charSet == NULL) {
		PutTransformedFace(face);
		return B_ERROR;
	}

	uint32 curCodePoint = start;

	while (curCodePoint <= end && hasBlock == false) {
		// loop through range; if any character in the range is in the charset
		// then the block is represented.
		if (FcCharSetHasChar(charSet, (FcChar32)curCodePoint) == FcTrue) {
			hasBlock = true;
			break;
		}

		++curCodePoint;
	}

	FcCharSetDestroy(charSet);
	PutTransformedFace(face);
#endif // FONTCONFIG_ENABLED

	return B_OK;
}


/** @brief Fills a boolean array indicating which characters in the string have glyphs in this font.
    @param string       UTF-8 encoded string to query.
    @param numBytes     Length in bytes of the string.
    @param numChars     Number of characters to query.
    @param hasArray     Caller-allocated boolean array of numChars entries to fill.
    @param useFallbacks If true, also checks fallback fonts when a glyph is missing.
    @return B_OK on success; B_BAD_DATA if arguments are invalid; B_ERROR if the font cache is unavailable. */
status_t
ServerFont::GetHasGlyphs(const char* string, int32 numBytes, int32 numChars, bool* hasArray,
	bool useFallbacks) const
{
	if (string == NULL || numBytes <= 0 || numChars <= 0 || hasArray == NULL)
		return B_BAD_DATA;

	FontCacheEntry* entry = NULL;
	FontCacheReference cacheReference;
	BObjectList<FontCacheReference, true> fallbacks(21);

	entry = GlyphLayoutEngine::FontCacheEntryFor(*this, false);
	if (entry == NULL)
		return B_ERROR;

	cacheReference.SetTo(entry);

	uint32 charCode;
	int32 charIndex = 0;
	const char* start = string;
	while (charIndex < numChars && (charCode = UTF8ToCharCode(&string)) != 0) {
		hasArray[charIndex] = entry->CanCreateGlyph(charCode);

		if (hasArray[charIndex] == false && useFallbacks) {
			if (fallbacks.IsEmpty())
				GlyphLayoutEngine::PopulateFallbacks(fallbacks, *this, false);

			if (GlyphLayoutEngine::GetFallbackReference(fallbacks, charCode) != NULL)
				hasArray[charIndex] = true;
		}

		charIndex++;
		if (string - start + 1 > numBytes)
			break;
	}

	return B_OK;
}


/** @brief Helper consumer class that retrieves glyph edge (inset) information. */
class EdgesConsumer {
 public:
	/** @brief Constructs an EdgesConsumer.
	    @param edges Caller-allocated array of edge_info to fill.
	    @param size  The font size in points, used to normalise edge values. */
	EdgesConsumer(edge_info* edges, float size)
		:
		fEdges(edges),
		fSize(size)
	{
	}

	/** @brief Returns false; vector data is not required for edge queries. */
	bool NeedsVector() { return false; }
	/** @brief Called before layout begins; no-op. */
	void Start() {}
	/** @brief Called after layout ends; no-op. */
	void Finish(double x, double y) {}
	/** @brief Sets edge values to zero for an empty (missing) glyph.
	    @param index    The glyph index.
	    @param charCode The Unicode code point.
	    @param x        Current pen x position.
	    @param y        Current pen y position. */
	void ConsumeEmptyGlyph(int32 index, uint32 charCode, double x, double y)
	{
		fEdges[index].left = 0.0;
		fEdges[index].right = 0.0;
	}

	/** @brief Stores normalised inset values for a glyph.
	    @param index    The glyph index.
	    @param charCode The Unicode code point.
	    @param glyph    The cached glyph providing inset data.
	    @param entry    The font cache entry (unused here).
	    @param x        Current pen x position.
	    @param y        Current pen y position.
	    @param advanceX Horizontal advance (unused here).
	    @param advanceY Vertical advance (unused here).
	    @return true always. */
	bool ConsumeGlyph(int32 index, uint32 charCode, const GlyphCache* glyph,
		FontCacheEntry* entry, double x, double y, double advanceX,
			double advanceY)
	{
		fEdges[index].left = glyph->inset_left / fSize;
		fEdges[index].right = glyph->inset_right / fSize;
		return true;
	}

 private:
	edge_info* fEdges;
	float fSize;
};


/** @brief Returns glyph edge (inset) information for each character in a string.
    @param string   UTF-8 encoded string.
    @param numBytes Length in bytes of the string.
    @param numChars Number of characters to process.
    @param edges    Caller-allocated array of edge_info to fill.
    @return B_OK on success; B_BAD_DATA if arguments are invalid; B_ERROR if layout failed. */
status_t
ServerFont::GetEdges(const char* string, int32 numBytes, int32 numChars,
	edge_info* edges) const
{
	if (string == NULL || numBytes <= 0 || numChars <= 0 || edges == NULL)
		return B_BAD_DATA;

	EdgesConsumer consumer(edges, fSize);
	if (GlyphLayoutEngine::LayoutGlyphs(consumer, *this, string, numBytes,
			numChars, NULL, fSpacing)) {
		return B_OK;
	}

	return B_ERROR;

//	FT_Face face = GetTransformedFace(false, false);
//	if (!face)
//		return B_ERROR;
//
//	const char *string = charArray;
//	for (int i = 0; i < numChars; i++) {
//		FT_Load_Char(face, UTF8ToCharCode(&string), FT_LOAD_NO_BITMAP);
//		edgeArray[i].left = float(face->glyph->metrics.horiBearingX)
//			/ 64 / fSize;
//		edgeArray[i].right = float(face->glyph->metrics.horiBearingX
//			+ face->glyph->metrics.width - face->glyph->metrics.horiAdvance)
//			/ 64 / fSize;
//	}
//
//	PutTransformedFace(face);
//	return B_OK;
}


/** @brief Helper consumer class that computes BPoint escapements for each glyph. */
class BPointEscapementConsumer {
public:
	/** @brief Constructs a BPointEscapementConsumer.
	    @param escapements Caller-allocated array of BPoint escapements to fill.
	    @param offsets     Optional caller-allocated array of BPoint offsets to fill.
	    @param size        The font size in points, used to normalise escapement values. */
	BPointEscapementConsumer(BPoint* escapements, BPoint* offsets, float size)
		:
		fEscapements(escapements),
		fOffsets(offsets),
		fSize(size)
	{
	}

	/** @brief Returns false; vector data is not required. */
	bool NeedsVector() { return false; }
	/** @brief Called before layout begins; no-op. */
	void Start() {}
	/** @brief Called after layout ends; no-op. */
	void Finish(double x, double y) {}
	/** @brief Sets escapement to zero for a missing glyph.
	    @param index    The glyph index.
	    @param charCode The Unicode code point.
	    @param x        Current pen x position.
	    @param y        Current pen y position. */
	void ConsumeEmptyGlyph(int32 index, uint32 charCode, double x, double y)
	{
		_Set(index, 0, 0);
	}

	/** @brief Stores the normalised advance as an escapement.
	    @param index    The glyph index.
	    @param charCode The Unicode code point.
	    @param glyph    The cached glyph (unused here).
	    @param entry    The font cache entry (unused here).
	    @param x        Current pen x position.
	    @param y        Current pen y position.
	    @param advanceX Horizontal advance in font units.
	    @param advanceY Vertical advance in font units.
	    @return true always. */
	bool ConsumeGlyph(int32 index, uint32 charCode, const GlyphCache* glyph,
		FontCacheEntry* entry, double x, double y, double advanceX,
			double advanceY)
	{
		return _Set(index, advanceX, advanceY);
	}

private:
	/** @brief Sets the escapement and optional offset for a single glyph.
	    @param index The glyph index.
	    @param x     Horizontal advance in font units.
	    @param y     Vertical advance in font units.
	    @return true always. */
	inline bool _Set(int32 index, double x, double y)
	{
		fEscapements[index].x = x / fSize;
		fEscapements[index].y = y / fSize;
		if (fOffsets) {
			// ToDo: According to the BeBook: "The offsetArray is applied by
			// the dynamic spacing in order to improve the relative position
			// of the character's width with relation to another character,
			// without altering the width." So this will probably depend on
			// the spacing mode.
			fOffsets[index].x = 0;
			fOffsets[index].y = 0;
		}
		return true;
	}

	BPoint* fEscapements;
	BPoint* fOffsets;
	float fSize;
};


/** @brief Returns the BPoint escapements (normalised advances) for each character in a string.
    @param string           UTF-8 encoded string.
    @param numBytes         Length in bytes of the string.
    @param numChars         Number of characters.
    @param delta            Escapement deltas for space and non-space characters.
    @param escapementArray  Caller-allocated BPoint array of numChars entries.
    @param offsetArray      Optional caller-allocated BPoint offset array.
    @return B_OK on success; B_BAD_DATA if arguments are invalid; B_ERROR if layout failed. */
status_t
ServerFont::GetEscapements(const char* string, int32 numBytes, int32 numChars,
	escapement_delta delta, BPoint escapementArray[],
	BPoint offsetArray[]) const
{
	if (string == NULL || numBytes <= 0 || numChars <= 0
		|| escapementArray == NULL) {
		return B_BAD_DATA;
	}

	BPointEscapementConsumer consumer(escapementArray, offsetArray, fSize);
	if (GlyphLayoutEngine::LayoutGlyphs(consumer, *this, string, numBytes,
			numChars, &delta, fSpacing)) {
		return B_OK;
	}

	return B_ERROR;
}


/** @brief Helper consumer class that computes float width escapements for each glyph. */
class WidthEscapementConsumer {
public:
	/** @brief Constructs a WidthEscapementConsumer.
	    @param widths Caller-allocated float array to fill with normalised widths.
	    @param size   The font size in points. */
	WidthEscapementConsumer(float* widths, float size)
		:
		fWidths(widths),
		fSize(size)
	{
	}

	/** @brief Returns false; vector data is not required. */
	bool NeedsVector() { return false; }
	/** @brief Called before layout begins; no-op. */
	void Start() {}
	/** @brief Called after layout ends; no-op. */
	void Finish(double x, double y) {}
	/** @brief Sets the width to zero for a missing glyph.
	    @param index    The glyph index.
	    @param charCode The Unicode code point.
	    @param x        Current pen x position.
	    @param y        Current pen y position. */
	void ConsumeEmptyGlyph(int32 index, uint32 charCode, double x, double y)
	{
		fWidths[index] = 0.0;
	}

	/** @brief Stores the normalised horizontal advance as the width.
	    @param index    The glyph index.
	    @param charCode The Unicode code point.
	    @param glyph    The cached glyph (unused here).
	    @param entry    The font cache entry (unused here).
	    @param x        Current pen x position.
	    @param y        Current pen y position.
	    @param advanceX Horizontal advance in font units.
	    @param advanceY Vertical advance (unused here).
	    @return true always. */
	bool ConsumeGlyph(int32 index, uint32 charCode, const GlyphCache* glyph,
		FontCacheEntry* entry, double x, double y, double advanceX,
			double advanceY)
	{
		fWidths[index] = advanceX / fSize;
		return true;
	}

 private:
	float* fWidths;
	float fSize;
};



/** @brief Returns float width escapements for each character in a string.
    @param string      UTF-8 encoded string.
    @param numBytes    Length in bytes of the string.
    @param numChars    Number of characters.
    @param delta       Escapement deltas for space and non-space characters.
    @param widthArray  Caller-allocated float array of numChars entries.
    @return B_OK on success; B_BAD_DATA if arguments are invalid; B_ERROR if layout failed. */
status_t
ServerFont::GetEscapements(const char* string, int32 numBytes, int32 numChars,
	escapement_delta delta, float widthArray[]) const
{
	if (string == NULL || numBytes <= 0 || numChars <= 0 || widthArray == NULL)
		return B_BAD_DATA;

	WidthEscapementConsumer consumer(widthArray, fSize);
	if (GlyphLayoutEngine::LayoutGlyphs(consumer, *this, string, numBytes,
			numChars, &delta, fSpacing)) {
		return B_OK;
	}

	return B_ERROR;
}


/** @brief Helper consumer class that computes per-glyph or string-level bounding boxes. */
class BoundingBoxConsumer {
 public:
	/** @brief Constructs a BoundingBoxConsumer.
	    @param transform  The affine transform to apply to glyph outlines.
	    @param rectArray  Optional caller-allocated BRect array for per-glyph boxes.
	    @param asString   If true, accumulates a single string-level bounding box instead. */
	BoundingBoxConsumer(Transformable& transform, BRect* rectArray,
			bool asString)
		:
		rectArray(rectArray),
		stringBoundingBox(INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN),
		fAsString(asString),
		fCurves(fPathAdaptor),
		fContour(fCurves),
		fTransformedOutline(fCurves, transform),
		fTransformedContourOutline(fContour, transform),
		fTransform(transform)
	{
	}

	/** @brief Returns false; vector data is not required. */
	bool NeedsVector() { return false; }
	/** @brief Called before layout begins; no-op. */
	void Start() {}
	/** @brief Called after layout ends; no-op. */
	void Finish(double x, double y) {}
	/** @brief No-op for empty glyphs (no contribution to bounding box).
	    @param index    The glyph index.
	    @param charCode The Unicode code point.
	    @param x        Current pen x position.
	    @param y        Current pen y position. */
	void ConsumeEmptyGlyph(int32 index, uint32 charCode, double x, double y) {}

	/** @brief Computes the bounding box for a single glyph and stores or accumulates it.
	    @param index    The glyph index.
	    @param charCode The Unicode code point.
	    @param glyph    The cached glyph providing raster or outline data.
	    @param entry    The font cache entry used for outline adaptor initialisation.
	    @param x        Current pen x position.
	    @param y        Current pen y position.
	    @param advanceX Horizontal advance (unused here).
	    @param advanceY Vertical advance (unused here).
	    @return true always. */
	bool ConsumeGlyph(int32 index, uint32 charCode, const GlyphCache* glyph,
		FontCacheEntry* entry, double x, double y, double advanceX,
			double advanceY)
	{
		if (glyph->data_type != glyph_data_outline) {
			const agg::rect_i& r = glyph->bounds;
			if (fAsString) {
				if (rectArray) {
					rectArray[index].left = r.x1 + x;
					rectArray[index].top = r.y1 + y;
					rectArray[index].right = r.x2 + x + 1;
					rectArray[index].bottom = r.y2 + y + 1;
				} else {
					stringBoundingBox = stringBoundingBox
						| BRect(r.x1 + x, r.y1 + y,
							r.x2 + x + 1, r.y2 + y + 1);
				}
			} else {
				rectArray[index].left = r.x1;
				rectArray[index].top = r.y1;
				rectArray[index].right = r.x2 + 1;
				rectArray[index].bottom = r.y2 + 1;
			}
		} else {
			if (fAsString) {
				entry->InitAdaptors(glyph, x, y,
						fMonoAdaptor, fGray8Adaptor, fPathAdaptor);
			} else {
				entry->InitAdaptors(glyph, 0, 0,
						fMonoAdaptor, fGray8Adaptor, fPathAdaptor);
			}
			double left = 0.0;
			double top = 0.0;
			double right = -1.0;
			double bottom = -1.0;
			uint32 pathID[1];
			pathID[0] = 0;
			// TODO: use fContour if falseboldwidth is > 0
			agg::bounding_rect(fTransformedOutline, pathID, 0, 1,
				&left, &top, &right, &bottom);

			if (rectArray) {
				rectArray[index] = BRect(left, top, right, bottom);
			} else {
				stringBoundingBox = stringBoundingBox
					| BRect(left, top, right, bottom);
			}
		}
		return true;
	}

	BRect*								rectArray;
	BRect								stringBoundingBox;

 private:
	bool								fAsString;
	FontCacheEntry::GlyphPathAdapter	fPathAdaptor;
	FontCacheEntry::GlyphGray8Adapter	fGray8Adaptor;
	FontCacheEntry::GlyphMonoAdapter	fMonoAdaptor;

	FontCacheEntry::CurveConverter		fCurves;
	FontCacheEntry::ContourConverter	fContour;

	FontCacheEntry::TransformedOutline	fTransformedOutline;
	FontCacheEntry::TransformedContourOutline fTransformedContourOutline;

	Transformable&						fTransform;
};


/** @brief Returns per-character or string bounding boxes for a UTF-8 string.
    @param string            UTF-8 encoded string.
    @param numBytes          Length in bytes of the string.
    @param numChars          Number of characters.
    @param rectArray         Caller-allocated BRect array of numChars entries.
    @param stringEscapement  If true, applies the delta to escapement computation.
    @param mode              Metric mode (currently unused).
    @param delta             Escapement delta for space and non-space characters.
    @param asString          If true, returns a single string-level bounding box in rectArray[0].
    @return B_OK on success; B_BAD_DATA if arguments are invalid; B_ERROR if layout failed. */
status_t
ServerFont::GetBoundingBoxes(const char* string, int32 numBytes, int32 numChars,
	BRect rectArray[], bool stringEscapement, font_metric_mode mode,
	escapement_delta delta, bool asString)
{
	// TODO: The font_metric_mode is not used
	if (string == NULL || numBytes <= 0 || numChars <= 0 || rectArray == NULL)
		return B_BAD_DATA;

	Transformable transform(EmbeddedTransformation());

	BoundingBoxConsumer consumer(transform, rectArray, asString);
	if (GlyphLayoutEngine::LayoutGlyphs(consumer, *this, string, numBytes,
			numChars, stringEscapement ? &delta : NULL, fSpacing)) {
		return B_OK;
	}
	return B_ERROR;
}


/** @brief Returns string-level bounding boxes for an array of UTF-8 strings.
    @param charArray   Array of UTF-8 string pointers.
    @param lengthArray Array of byte lengths, one per string.
    @param numStrings  Number of strings.
    @param rectArray   Caller-allocated BRect array of numStrings entries.
    @param mode        Metric mode (currently unused).
    @param deltaArray  Escapement delta array, one per string.
    @return B_OK on success; B_BAD_DATA if arguments are invalid; B_ERROR if layout failed for any string. */
status_t
ServerFont::GetBoundingBoxesForStrings(char *charArray[], size_t lengthArray[],
	int32 numStrings, BRect rectArray[], font_metric_mode mode,
	escapement_delta deltaArray[])
{
	// TODO: The font_metric_mode is never used
	if (charArray == NULL || lengthArray == NULL || numStrings <= 0
		|| rectArray == NULL || deltaArray == NULL) {
		return B_BAD_DATA;
	}

	Transformable transform(EmbeddedTransformation());

	for (int32 i = 0; i < numStrings; i++) {
		size_t numBytes = lengthArray[i];
		const char* string = charArray[i];
		escapement_delta delta = deltaArray[i];

		BoundingBoxConsumer consumer(transform, NULL, true);
		if (!GlyphLayoutEngine::LayoutGlyphs(consumer, *this, string, numBytes,
				INT32_MAX, &delta, fSpacing)) {
			return B_ERROR;
		}

		rectArray[i] = consumer.stringBoundingBox;
	}

	return B_OK;
}


/** @brief Helper consumer that computes the total advance width of a string. */
class StringWidthConsumer {
 public:
	/** @brief Constructs a StringWidthConsumer with width initialised to zero. */
	StringWidthConsumer()
		:
		width(0.0)
	{
	}

	/** @brief Returns false; vector data is not required. */
	bool NeedsVector() { return false; }
	/** @brief Called before layout begins; no-op. */
	void Start() {}
	/** @brief Captures the final pen x position as the string width.
	    @param x Final pen x position after all glyphs.
	    @param y Final pen y position (unused). */
	void Finish(double x, double y) { width = x; }
	/** @brief No-op for empty glyphs. */
	void ConsumeEmptyGlyph(int32 index, uint32 charCode, double x, double y) {}
	/** @brief No-op; width is derived from the final pen position.
	    @return true always. */
	bool ConsumeGlyph(int32 index, uint32 charCode, const GlyphCache* glyph,
		FontCacheEntry* entry, double x, double y, double advanceX,
			double advanceY)
	{
		return true;
	}

	float width;
};


/** @brief Returns the total pixel width of a UTF-8 string in this font.
    @param string     UTF-8 encoded string.
    @param numBytes   Length in bytes of the string.
    @param deltaArray Optional escapement delta (may be NULL).
    @return The total advance width in pixels, or 0.0 if the string is empty or layout fails. */
float
ServerFont::StringWidth(const char *string, int32 numBytes,
	const escapement_delta* deltaArray) const
{
	if (!string || numBytes <= 0)
		return 0.0;

	StringWidthConsumer consumer;
	if (!GlyphLayoutEngine::LayoutGlyphs(consumer, *this, string, numBytes,
			INT32_MAX, deltaArray, fSpacing)) {
		return 0.0;
	}

	return consumer.width;
}


/** @brief Returns a BRect that encloses the entire font at its current size.
    @return A BRect enclosing the font's global bounding box. */
BRect
ServerFont::BoundingBox()
{
	FT_Face face = fStyle->FreeTypeFace();

	if (fBounds.IsValid() &&
		fBounds.IntegerWidth() > 0 &&
		fBounds.IntegerHeight() > 0)
		return fBounds;

	// if font has vector outlines, get the bounding box
	// from freetype and scale it by the font size
	if (IsScalable()) {
		FT_BBox bounds = face->bbox;
		fBounds.left = (float)bounds.xMin / (float)face->units_per_EM;
		fBounds.right = (float)bounds.xMax / (float)face->units_per_EM;
		fBounds.top = (float)bounds.yMin / (float)face->units_per_EM;
		fBounds.bottom = (float)bounds.yMax / (float)face->units_per_EM;

		float scaledWidth = fBounds.Width() * fSize;
		float scaledHeight = fBounds.Height() * fSize;

		fBounds.InsetBy((fBounds.Width() - scaledWidth) / 2.f,
			(fBounds.Height() - scaledHeight) / 2.f);
	} else {
		// otherwise find the bitmap that is closest in size
		// to the requested size
		float pixelSize = fSize * 64.f;
		float minDelta = abs(face->available_sizes[0].size - pixelSize);
		float width = face->available_sizes[0].x_ppem;
		float height = face->available_sizes[0].y_ppem;

		for (int i = 1; i < face->num_fixed_sizes; ++i) {
			float delta = abs(face->available_sizes[i].size - pixelSize);
			if (delta < minDelta) {
				width = face->available_sizes[i].x_ppem;
				height = face->available_sizes[i].y_ppem;
			}
		}

		fBounds.top = 0;
		fBounds.left = 0;
		fBounds.right = width / 64.f;
		fBounds.bottom = height / 64.f;
	}

	return fBounds;
}


/** @brief Retrieves the ascent, descent, and leading height values for this font.
    @param height Reference to a font_height struct to fill with the metric values. */
void
ServerFont::GetHeight(font_height& height) const
{
	fStyle->GetHeight(fSize, height);
}


/** @brief Truncates a BString to fit within a given pixel width using the specified truncation mode.
    @param inOut  Pointer to the BString to truncate in place.
    @param mode   One of the B_TRUNCATE_* constants specifying where to truncate.
    @param width  Maximum allowed pixel width for the resulting string. */
void
ServerFont::TruncateString(BString* inOut, uint32 mode, float width) const
{
	if (!inOut)
		return;

	// the width of the "…" glyph
	float ellipsisWidth = StringWidth(B_UTF8_ELLIPSIS, strlen(B_UTF8_ELLIPSIS));

	// count the individual glyphs
	int32 numChars = inOut->CountChars();

	// get the escapement of each glyph in font units
	float* escapementArray = new (std::nothrow) float[numChars];
	if (escapementArray == NULL)
		return;

	static escapement_delta delta = (escapement_delta){ 0.0, 0.0 };
	if (GetEscapements(inOut->String(), inOut->Length(), numChars, delta,
		escapementArray) == B_OK) {
		truncate_string(*inOut, mode, width, escapementArray, fSize,
			ellipsisWidth, numChars);
	}

	delete[] escapementArray;
}


/** @brief Builds and returns the embedded affine transform (shear + rotation) for this font.
    @return A Transformable encoding the font's shear and rotation. */
Transformable
ServerFont::EmbeddedTransformation() const
{
	// TODO: cache this?
	Transformable transform;

	transform.ShearBy(B_ORIGIN, (90.0 - fShear) * M_PI / 180.0, 0.0);
	transform.RotateBy(B_ORIGIN, -fRotation * M_PI / 180.0);

	return transform;
}


/** @brief Sets the raw font data buffer on the underlying FontStyle, used for embedded font data.
    @param location Pointer to the FreeType byte buffer holding the font data.
    @param size     Size in bytes of the font data buffer. */
void
ServerFont::SetFontData(FT_Byte* location, uint32 size)
{
	if (fStyle != NULL)
		fStyle->SetFontData(location, size);
}
