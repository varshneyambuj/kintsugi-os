/*
 * Copyright 2025, Kintsugi OS Contributors.
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
 * This file incorporates work from the Haiku project, originally
 * distributed under the MIT License.
 * Copyright 2001-2008, Haiku.
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Jérôme Duval, jerome.duval@free.fr
 *		Axel Dörfler, axeld@pinc-software.de
 *		Stephan Aßmus <superstippi@gmx.de>
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file ServerFont.h
 *  @brief Server-side font descriptor wrapping a FontStyle with size, shear, and rotation. */

#ifndef SERVER_FONT_H
#define SERVER_FONT_H


#include <Font.h>
#include <Rect.h>

#include "AppFontManager.h"
#include "FontFamily.h"
#include "FontManager.h"
#include "GlobalSubpixelSettings.h"
#include "Transformable.h"

class BShape;
class BString;


/** @brief Represents a fully-specified font instance on the server, including metric queries. */
class ServerFont {
 public:
	/** @brief Constructs a default ServerFont using system defaults. */
								ServerFont();

	/** @brief Constructs a ServerFont from a FontStyle and rendering parameters.
	 *  @param style          FontStyle to use.
	 *  @param size           Point size (default 12.0).
	 *  @param rotation       Rotation in degrees (default 0.0).
	 *  @param shear          Shear in degrees (default 90.0).
	 *  @param falseBoldWidth Additional width for synthetic bold (default 0.0).
	 *  @param flags          Font flags bitfield (default 0).
	 *  @param spacing        Spacing mode (default B_BITMAP_SPACING). */
								ServerFont(FontStyle& style,
									float size = 12.0, float rotation = 0.0,
									float shear = 90.0,
									float falseBoldWidth = 0.0,
									uint16 flags = 0,
									uint8 spacing = B_BITMAP_SPACING);

	/** @brief Copy-constructs from another ServerFont.
	 *  @param font Source font. */
								ServerFont(const ServerFont& font);
	virtual						~ServerFont();

	/** @brief Assigns from another ServerFont.
	 *  @param font Source font.
	 *  @return Reference to this object. */
			ServerFont			&operator=(const ServerFont& font);

	/** @brief Compares this font with another for equality.
	 *  @param other Font to compare.
	 *  @return true if all properties are equal. */
			bool				operator==(const ServerFont& other) const;

	/** @brief Returns the text direction.
	 *  @return font_direction constant. */
			font_direction		Direction() const
									{ return fDirection; }

	/** @brief Returns the character encoding.
	 *  @return Encoding constant. */
			uint32				Encoding() const
									{ return fEncoding; }

	/** @brief Returns the font flags.
	 *  @return Flags bitfield. */
			uint32				Flags() const
									{ return fFlags; }

	/** @brief Returns the spacing mode.
	 *  @return Spacing constant. */
			uint32				Spacing() const
									{ return fSpacing; }

	/** @brief Returns the shear angle in degrees.
	 *  @return Shear value. */
			float				Shear() const
									{ return fShear; }

	/** @brief Returns the rotation angle in degrees.
	 *  @return Rotation value. */
			float				Rotation() const
									{ return fRotation; }

	/** @brief Returns the synthetic bold extra width.
	 *  @return False bold width in points. */
			float				FalseBoldWidth() const
									{ return fFalseBoldWidth; }

	/** @brief Returns the point size of the font.
	 *  @return Size in points. */
			float				Size() const
									{ return fSize; }

	/** @brief Returns the face flags (bold, italic, etc.).
	 *  @return Face flags bitfield. */
			uint16				Face() const
									{ return fFace; }

	/** @brief Returns the total number of glyphs in the underlying font style.
	 *  @return Glyph count. */
			uint32				CountGlyphs()
									{ return fStyle->GlyphCount(); }

	/** @brief Returns the number of tuned (bitmap) sizes available for this font.
	 *  @return Tuned size count. */
			int32				CountTuned();

	/** @brief Returns the file format of the underlying font file.
	 *  @return font_file_format constant. */
			font_file_format	FileFormat();

	/** @brief Returns the style name of the font (e.g. "Bold").
	 *  @return Null-terminated style name string. */
			const char*			Style() const;

	/** @brief Returns the family name of the font (e.g. "Noto Sans").
	 *  @return Null-terminated family name string. */
			const char*			Family() const;

	/** @brief Returns the file system path to the font file.
	 *  @return Null-terminated path string. */
			const char*			Path() const
									{ return fStyle->Path(); }

	/** @brief Returns the FreeType face index within the font file.
	 *  @return Face index. */
			long				FaceIndex() const
									{ return fStyle->FreeTypeFace()->face_index; }

	/** @brief Switches the font to use the given FontStyle directly.
	 *  @param style New FontStyle to adopt. */
			void				SetStyle(FontStyle* style);

	/** @brief Sets the font family and style by numeric IDs.
	 *  @param familyID   Family identifier.
	 *  @param styleID    Style identifier.
	 *  @param fontManager Optional per-app font manager to search first.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			SetFamilyAndStyle(uint16 familyID,
									uint16 styleID,
									AppFontManager* fontManager = NULL);

	/** @brief Sets the font family and style from a packed 32-bit ID.
	 *  @param fontID      Packed family+style ID.
	 *  @param fontManager Optional per-app font manager to search first.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			SetFamilyAndStyle(uint32 fontID,
									AppFontManager* fontManager = NULL);

	/** @brief Returns the numeric style ID.
	 *  @return Style ID. */
			uint16				StyleID() const
									{ return fStyle->ID(); }

	/** @brief Returns the numeric family ID.
	 *  @return Family ID. */
			uint16				FamilyID() const
									{ return fStyle->Family()->ID(); }

	/** @brief Returns the packed 32-bit family+style identifier.
	 *  @return Combined family and style ID. */
			uint32				GetFamilyAndStyle() const;

	/** @brief Sets the text direction.
	 *  @param dir New font_direction constant. */
			void				SetDirection(font_direction dir)
									{ fDirection = dir; }

	/** @brief Sets the character encoding.
	 *  @param encoding New encoding constant. */
			void				SetEncoding(uint32 encoding)
									{ fEncoding = encoding; }

	/** @brief Sets the font flags.
	 *  @param value New flags bitfield. */
			void				SetFlags(uint32 value)
									{ fFlags = value; }

	/** @brief Sets the spacing mode.
	 *  @param value New spacing constant. */
			void				SetSpacing(uint32 value)
									{ fSpacing = value; }

	/** @brief Sets the shear angle.
	 *  @param value Shear in degrees. */
			void				SetShear(float value)
									{ fShear = value; }

	/** @brief Sets the point size.
	 *  @param value New size in points. */
			void				SetSize(float value);

	/** @brief Sets the rotation angle.
	 *  @param value Rotation in degrees. */
			void				SetRotation(float value)
									{ fRotation = value; }

	/** @brief Sets the synthetic bold extra width.
	 *  @param value False bold width in points. */
			void				SetFalseBoldWidth(float value)
									{ fFalseBoldWidth = value; }

	/** @brief Sets the face flags, updating the underlying FontStyle as needed.
	 *  @param face New face flags bitfield.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			SetFace(uint16 face);

	/** @brief Returns whether the font has a fixed (monospaced) advance width.
	 *  @return true if monospaced. */
			bool				IsFixedWidth() const
									{ return fStyle->IsFixedWidth(); }

	/** @brief Returns whether the font is a scalable (outline) font.
	 *  @return true if scalable. */
			bool				IsScalable() const
									{ return fStyle->IsScalable(); }

	/** @brief Returns whether the font supports kerning.
	 *  @return true if kerning is available. */
			bool				HasKerning() const
									{ return fStyle->HasKerning(); }

	/** @brief Returns whether the font has pre-rendered (tuned) bitmap sizes.
	 *  @return true if tuned bitmaps exist. */
			bool				HasTuned() const
									{ return fStyle->HasTuned(); }

	/** @brief Returns the number of available tuned bitmap sizes.
	 *  @return Tuned size count. */
			int32				TunedCount() const
									{ return fStyle->TunedCount(); }

	/** @brief Returns the total number of glyphs in the font style.
	 *  @return Glyph count. */
			uint16				GlyphCount() const
									{ return fStyle->GlyphCount(); }

	/** @brief Returns the number of character maps available in the font.
	 *  @return Character map count. */
			uint16				CharMapCount() const
									{ return fStyle->CharMapCount(); }

	/** @brief Returns whether hinting is enabled for this font instance.
	 *  @return true if hinting should be applied. */
	inline	bool				Hinting() const;

	/** @brief Fills an array of BShape objects with outlines for the given characters.
	 *  @param charArray   UTF-8 string of characters.
	 *  @param numChars    Number of characters.
	 *  @param shapeArray  Output array of BShape pointers (caller owns).
	 *  @return B_OK on success, an error code otherwise. */
			status_t			GetGlyphShapes(const char charArray[],
									int32 numChars, BShape *shapeArray[]) const;

	/** @brief Queries which characters in the array have corresponding glyphs.
	 *  @param charArray   UTF-8 string of characters.
	 *  @param numBytes    Byte length of charArray.
	 *  @param numChars    Number of characters.
	 *  @param hasArray    Output boolean array; true where a glyph exists.
	 *  @param useFallbacks Whether to consider fallback fonts.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			GetHasGlyphs(const char charArray[],
									int32 numBytes, int32 numChars,
									bool hasArray[], bool useFallbacks) const;

	/** @brief Returns the edge (side-bearing) info for the given characters.
	 *  @param charArray  UTF-8 string of characters.
	 *  @param numBytes   Byte length of charArray.
	 *  @param numChars   Number of characters.
	 *  @param edgeArray  Output array of edge_info structures.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			GetEdges(const char charArray[], int32 numBytes,
									int32 numChars, edge_info edgeArray[])
									const;

	/** @brief Returns escapement vectors and offsets for the given characters.
	 *  @param charArray        UTF-8 string of characters.
	 *  @param numBytes         Byte length of charArray.
	 *  @param numChars         Number of characters.
	 *  @param delta            Extra horizontal/vertical spacing delta.
	 *  @param escapementArray  Output array of escapement BPoints.
	 *  @param offsetArray      Output array of offset BPoints.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			GetEscapements(const char charArray[],
									int32 numBytes, int32 numChars,
									escapement_delta delta,
									BPoint escapementArray[],
									BPoint offsetArray[]) const;

	/** @brief Returns cumulative advance widths for the given characters.
	 *  @param charArray   UTF-8 string of characters.
	 *  @param numBytes    Byte length of charArray.
	 *  @param numChars    Number of characters.
	 *  @param delta       Extra horizontal/vertical spacing delta.
	 *  @param widthArray  Output array of per-character widths.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			GetEscapements(const char charArray[],
									int32 numBytes, int32 numChars,
									escapement_delta delta,
									float widthArray[]) const;

	/** @brief Returns per-character bounding boxes.
	 *  @param charArray        UTF-8 string of characters.
	 *  @param numBytes         Byte length of charArray.
	 *  @param numChars         Number of characters.
	 *  @param rectArray        Output array of bounding BRects.
	 *  @param stringEscapement If true, rects are offset by escapement.
	 *  @param mode             Metric mode (screen or printing).
	 *  @param delta            Extra spacing delta.
	 *  @param asString         If true, return the string bounding box.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			GetBoundingBoxes(const char charArray[],
									int32 numBytes, int32 numChars,
									BRect rectArray[], bool stringEscapement,
									font_metric_mode mode,
									escapement_delta delta,
									bool asString);

	/** @brief Returns bounding boxes for multiple strings.
	 *  @param charArray   Array of UTF-8 string pointers.
	 *  @param lengthArray Array of byte lengths for each string.
	 *  @param numStrings  Number of strings.
	 *  @param rectArray   Output array of bounding BRects.
	 *  @param mode        Metric mode.
	 *  @param deltaArray  Array of per-string spacing deltas.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			GetBoundingBoxesForStrings(char *charArray[],
									size_t lengthArray[], int32 numStrings,
									BRect rectArray[], font_metric_mode mode,
									escapement_delta deltaArray[]);

	/** @brief Returns the pixel width of the given string.
	 *  @param string   UTF-8 string to measure.
	 *  @param numBytes Byte length of the string.
	 *  @param delta    Optional extra spacing delta.
	 *  @return Width in pixels. */
			float				StringWidth(const char *string,
									int32 numBytes,
									const escapement_delta* delta = NULL) const;

	/** @brief Locks the underlying FontStyle for exclusive access.
	 *  @return true if the lock was acquired. */
			bool				Lock() const { return fStyle->Lock(); }

	/** @brief Unlocks the underlying FontStyle. */
			void				Unlock() const { fStyle->Unlock(); }

//			FT_Face				GetFTFace() const
//									{ return fStyle->FreeTypeFace(); };

	/** @brief Returns the font's bounding box in font units.
	 *  @return BRect encompassing all glyphs. */
			BRect				BoundingBox();

	/** @brief Fills in ascent, descent, and leading metrics.
	 *  @param height Reference that receives the font_height values. */
			void				GetHeight(font_height& height) const;

	/** @brief Truncates a string to fit within a given pixel width.
	 *  @param inOut  String to truncate in place.
	 *  @param mode   Truncation mode (B_TRUNCATE_*).
	 *  @param width  Maximum pixel width. */
			void				TruncateString(BString* inOut,
									uint32 mode, float width) const;

	/** @brief Returns the affine transformation embedded in this font.
	 *  @return A Transformable object representing the font's transform. */
			Transformable		EmbeddedTransformation() const;

	/** @brief Retrieves the unicode block coverage of the font.
	 *  @param blocksForFont Receives the unicode_block bitset.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			GetUnicodeBlocks(unicode_block &blocksForFont);

	/** @brief Checks whether the font covers a specific Unicode range.
	 *  @param start    First code point of the range.
	 *  @param end      Last code point of the range.
	 *  @param hasBlock Receives true if the range is covered.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			IncludesUnicodeBlock(uint32 start, uint32 end,
									bool &hasBlock);

	/** @brief Returns the FontManager that owns this font's style.
	 *  @return Pointer to the FontManager. */
			FontManager*		Manager() const
									{ return fStyle->Manager(); }

	/** @brief Sets in-memory font data for fonts not backed by a file.
	 *  @param location Pointer to the font data.
	 *  @param size     Byte size of the data. */
			void  				SetFontData(FT_Byte* location, uint32 size);

	/** @brief Returns the byte size of in-memory font data.
	 *  @return Size in bytes, or 0 if file-backed. */
			uint32				FontDataSize() const
									{ return fStyle->FontDataSize(); }

	/** @brief Returns the pointer to in-memory font data.
	 *  @return Pointer to the FT_Byte data, or NULL if file-backed. */
			FT_Byte* 			FontData() const
									{ return fStyle->FontData(); }

protected:
	friend class FontStyle;

			FT_Face				GetTransformedFace(bool rotate,
									bool shear) const;
			void				PutTransformedFace(FT_Face face) const;

			BReference<FontStyle>
								fStyle;
			float				fSize;
			float				fRotation;
			float				fShear;
			float				fFalseBoldWidth;
			BRect				fBounds;
			uint32				fFlags;
			uint32				fSpacing;
			font_direction		fDirection;
			uint16				fFace;
			uint32				fEncoding;
};

inline bool ServerFont::Hinting() const
{
	switch (gDefaultHintingMode) {
		case HINTING_MODE_OFF:
			return false;
		default:
		case HINTING_MODE_ON:
			return true;
		case HINTING_MODE_MONOSPACED_ONLY:
			return IsFixedWidth();
	}
}

#endif	/* SERVER_FONT_H */
