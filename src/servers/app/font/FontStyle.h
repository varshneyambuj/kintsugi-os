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

/** @file FontStyle.h
    @brief Model class describing one installed font style (face + metrics). */

#ifndef FONT_STYLE_H_
#define FONT_STYLE_H_


#include <Font.h>
#include <Locker.h>
#include <Node.h>
#include <ObjectList.h>
#include <Path.h>
#include <Rect.h>
#include <Referenceable.h>
#include <String.h>

#include <ft2build.h>
#include FT_FREETYPE_H


struct node_ref;
class FontFamily;
class FontManager;
class ServerFont;


/**
 * @brief Reference-counted descriptor for a single font style.
 *
 * A FontStyle owns the FT_Face for one installed style file, exposes the
 * Be font metrics derived from it (height, fixed-width flags, glyph count,
 * etc.), and ties the style to its parent FontFamily. Instances are shared
 * across ServerFonts that target the same style and are managed by a
 * FontManager subclass.
 */
class FontStyle : public BReferenceable {
	public:
						FontStyle(node_ref& nodeRef, const char* path,
							FT_Face face, FontManager* fontManager);
		virtual			~FontStyle();

		/** @brief Returns the node_ref of the on-disk font file. */
		const node_ref& NodeRef() const { return fNodeRef; }

		bool			Lock();
		void			Unlock();

		/** @brief True when the underlying face advertises a fixed character width. */
		bool			IsFixedWidth() const
							{ return FT_IS_FIXED_WIDTH(fFreeTypeFace); }


		/** @brief True when the face has two distinct fixed widths (full and half). */
		bool			IsFullAndHalfFixed() const
							{ return fFullAndHalfFixed; };

		/** @brief True when the face is scalable (vector outlines available). */
		bool			IsScalable() const
							{ return FT_IS_SCALABLE(fFreeTypeFace); }
		/** @brief True when the face carries a kerning table. */
		bool			HasKerning() const
							{ return FT_HAS_KERNING(fFreeTypeFace); }
		/** @brief True when the face contains hand-tuned bitmap strikes. */
		bool			HasTuned() const
							{ return FT_HAS_FIXED_SIZES(fFreeTypeFace); }
		/** @brief Returns the number of bitmap strikes embedded in the face. */
		int32			TunedCount() const
							{ return fFreeTypeFace->num_fixed_sizes; }
		/** @brief Returns the total number of glyphs in the face. */
		uint16			GlyphCount() const
							{ return fFreeTypeFace->num_glyphs; }
		/** @brief Returns the number of character maps the face provides. */
		uint16			CharMapCount() const
							{ return fFreeTypeFace->num_charmaps; }

		/** @brief Returns the style's display name (e.g. "Bold Italic"). */
		const char*		Name() const
							{ return fName.String(); }
		/** @brief Returns the FontFamily this style belongs to. */
		FontFamily*		Family() const
							{ return fFamily; }
		/** @brief Returns the per-family unique numeric ID assigned to this style. */
		uint16			ID() const
							{ return fID; }
		uint32			Flags() const;

		/** @brief Returns the Be face mask (B_BOLD_FACE, B_ITALIC_FACE, ...). */
		uint16			Face() const
							{ return fFace; }
		uint16			PreservedFace(uint16) const;

		const char*		Path() const;
		void			UpdatePath(const node_ref& parentNodeRef);

		void			GetHeight(float size, font_height &heigth) const;
		/** @brief Returns the writing direction of the style (always LTR for now). */
		font_direction	Direction() const
							{ return B_FONT_LEFT_TO_RIGHT; }
		/** @brief Returns the file format used by this style (always TrueType for now). */
		font_file_format FileFormat() const
							{ return B_TRUETYPE_WINDOWS; }

		/** @brief Returns the underlying FT_Face for engines that need it. */
		FT_Face			FreeTypeFace() const
							{ return fFreeTypeFace; }

		status_t		UpdateFace(FT_Face face);

		/** @brief Returns the FontManager owning this style. */
		FontManager*	Manager() const
							{ return fFontManager; }

		/** @brief Returns the in-memory size of the loaded font file, or 0 if disk-backed. */
		uint32			FontDataSize() const
							{ return fFontDataSize; }

		void 			SetFontData(FT_Byte* location, uint32 size);
		/** @brief Returns the in-memory font data buffer, or NULL if disk-backed. */
		FT_Byte*  		FontData() const
							{ return fFontData; }

	private:
		friend class FontFamily;
		friend class FontManager;
		uint16			_TranslateStyleToFace(const char *name) const;
		void			_SetFontFamily(FontFamily* family, uint16 id);
	private:
		FT_Face			fFreeTypeFace;
		BString			fName;
		BPath			fPath;
		node_ref		fNodeRef;

		BReference<FontFamily>
						fFamily;
		uint16			fID;

		BRect			fBounds;

		font_height		fHeight;
		uint16			fFace;
		bool			fFullAndHalfFixed;

		FT_Byte*		fFontData;
		uint32			fFontDataSize;
		FontManager*	fFontManager;
};

#endif	// FONT_STYLE_H_
