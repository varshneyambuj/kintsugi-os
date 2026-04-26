/*
 * Copyright 2026, Kintsugi OS Contributors. All rights reserved.
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
 * MIT License. Copyright 2009, Axel Dörfler.
 * Original author: Axel Dörfler.
 */

/** @file KeyboardLayout.h
    @brief Geometric model of a keyboard: per-key positions, shapes, and scancodes. */

#ifndef KEYBOARD_LAYOUT_H
#define KEYBOARD_LAYOUT_H


#include <map>

#include <Entry.h>
#include <ObjectList.h>
#include <Point.h>
#include <Rect.h>
#include <String.h>


/**
 * @brief Geometric shape of a single key in the rendered layout.
 */
enum key_shape {
	kRectangleKeyShape,
	kCircleKeyShape,
	kEnterKeyShape
};

/**
 * @brief One physical key in a keyboard layout.
 *
 * Bundles the primary scancode, up to three alternate codes (selected
 * by alternate modifier masks), a geometric shape, a frame in
 * layout-local coordinates, and a "dark" flag controlling background
 * tinting. For kEnterKeyShape keys, @c second_row holds the width of
 * the lower portion of the L-shaped key.
 */
struct Key {
	uint32		code;
	uint32		alternate_code[3];
	uint32		alternate_modifier[3];
	key_shape	shape;
	BRect		frame;
	float		second_row;
		// this is the width of the second row of a kEnterKeyShape key
	bool		dark;
};

/**
 * @brief LED indicator overlay (Caps/Num/Scroll Lock) on the layout.
 */
struct Indicator {
	int32		modifier;
	BRect		frame;
};

/** @brief Substitution table mapping "$name" tokens to their expansion text. */
typedef std::map<BString, BString> VariableMap;

/**
 * @brief Parses and represents a keyboard layout description file.
 *
 * The layout is a list of Key records (with primary and alternate
 * scancodes, geometric shape, and a frame) and a list of LED
 * Indicator records. The model is loaded from a small text-based
 * description language; see SetDefault() for an embedded example and
 * the language sketch in KeyboardLayout.cpp for syntax details.
 */
class KeyboardLayout {
public:
							KeyboardLayout();
							~KeyboardLayout();

			const char*		Name();

			int32			CountKeys();
			Key*			KeyAt(int32 index);

			int32			CountIndicators();
			Indicator*		IndicatorAt(int32 index);

			BRect			Bounds();
			BSize			DefaultKeySize();
			int32			IndexForModifier(int32 modifier);

			status_t		Load(const char* path);
			status_t		Load(entry_ref& ref);

			void			SetDefault();
			/** @brief Returns true if the loaded layout is the built-in default. */
			bool			IsDefault() const { return fIsDefault; }

private:
	enum parse_mode {
		kPairs,
		kSize,
		kRowStart,
		kKeyShape,
		kKeyCodes
	};
	struct parse_state {
		parse_mode	mode;
		int32		line;
	};

			void			_FreeKeys();
			void			_Error(const parse_state& state,
								const char* reason, ...);
			void			_AddAlternateKeyCode(Key* key, int32 modifier,
								int32 code);
			bool			_AddKey(const Key& key);
			void			_SkipCommentsAndSpace(parse_state& state,
								const char*& data);
			void			_Trim(BString& string, bool stripComments);
			bool			_GetPair(const parse_state& state,
								const char*& data, BString& name,
								BString& value);
			bool			_AddKeyCodes(const parse_state& state,
								BPoint& rowLeftTop, Key& key, const char* data,
								int32& lastCount);
			bool			_GetSize(const parse_state& state, const char* data,
								float& x, float& y, float* _secondRow = NULL);
			bool			_GetShape(const parse_state& state,
								const char* data, Key& key);
			const char*		_Delimiter(parse_mode mode);
			bool			_GetTerm(const char*& data, const char* delimiter,
								BString& term, bool closingBracketAllowed);
			bool			_SubstituteVariables(BString& term,
								VariableMap& variables, BString& unknown);
			bool			_ParseTerm(const parse_state& state,
								const char*& data, BString& term,
								VariableMap& variables);

			status_t		_InitFrom(const char* data);

			BString			fName;
			Key*			fKeys;
			int32			fKeyCount;
			int32			fKeyCapacity;
			BRect			fBounds;
			BSize			fDefaultKeySize;
			int32			fAlternateIndex[3];
			BObjectList<Indicator, true> fIndicators;
			bool			fIsDefault;
};

#endif	// KEYBOARD_LAYOUT_H
