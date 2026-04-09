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
 * Incorporates work from Haiku, Inc. covered by:
 * Copyright 2006, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

/** @file MediaTheme.h
 *  @brief Defines BMediaTheme, the base class for media parameter control themes.
 */

#ifndef _CONTROL_THEME_H
#define _CONTROL_THEME_H


#include <Entry.h>
#include <MediaDefs.h>

class BBitmap;
class BControl;
class BParameter;
class BParameterWeb;
class BRect;
class BView;


/** @brief Provides visual appearance and widget creation for media parameter controls.
 *
 *  BMediaTheme allows applications to customize the look and feel of
 *  BParameterWeb control panels.  Subclass it and install it via
 *  SetPreferredTheme() to apply a custom theme system-wide.
 */
class BMediaTheme {
	public:
		virtual	~BMediaTheme();

		/** @brief Returns the human-readable name of this theme.
		 *  @return The theme name string.
		 */
		const char* Name();

		/** @brief Returns a descriptive text for this theme.
		 *  @return The theme info string.
		 */
		const char* Info();

		/** @brief Returns the numeric ID of this theme.
		 *  @return The theme ID.
		 */
		int32 ID();

		/** @brief Returns the add-on entry_ref for this theme, if any.
		 *  @param ref On return, the entry_ref of the add-on image.
		 *  @return True if a ref is available, false otherwise.
		 */
		bool GetRef(entry_ref* ref);

		/** @brief Builds a BView for the given parameter web using the preferred theme.
		 *  @param web The BParameterWeb to visualize.
		 *  @param hintRect Optional suggested size/position hint.
		 *  @param usingTheme Optional specific theme to use; NULL uses the preferred theme.
		 *  @return A new BView containing controls for all parameters.
		 */
		static BView* ViewFor(BParameterWeb* web, const BRect* hintRect = NULL,
			BMediaTheme* usingTheme = NULL);

		/** @brief Sets the system-preferred theme.
		 *  @param defaultTheme The theme to install, or NULL to restore the built-in default.
		 *  @return B_OK on success, or an error code.
		 */
		static status_t SetPreferredTheme(BMediaTheme* defaultTheme = NULL);

		/** @brief Returns the currently installed preferred theme.
		 *  @return Pointer to the preferred BMediaTheme.
		 */
		static BMediaTheme* PreferredTheme();

		/** @brief Creates a BControl widget for a single BParameter.
		 *  @param control The parameter to represent.
		 *  @return A new BControl, or NULL if unsupported.
		 */
		virtual	BControl* MakeControlFor(BParameter* control) = 0;

		/** @brief Background type identifiers for BackgroundBitmapFor() and BackgroundColorFor(). */
		enum bg_kind {
			B_GENERAL_BG = 0,
			B_SETTINGS_BG,
			B_PRESENTATION_BG,
			B_EDIT_BG,
			B_CONTROL_BG,
			B_HILITE_BG
		};

		/** @brief Foreground type identifiers for ForegroundColorFor(). */
		enum fg_kind {
			B_GENERAL_FG = 0,
			B_SETTINGS_FG,
			B_PRESENTATION_FG,
			B_EDIT_FG,
			B_CONTROL_FG,
			B_HILITE_FG
		};

		/** @brief Returns a background bitmap for the given usage context.
		 *  @param bg The background kind.
		 *  @return Pointer to a BBitmap, or NULL for a solid-color background.
		 */
		virtual	BBitmap* BackgroundBitmapFor(bg_kind bg = B_GENERAL_BG);

		/** @brief Returns a background color for the given usage context.
		 *  @param bg The background kind.
		 *  @return The background rgb_color.
		 */
		virtual	rgb_color BackgroundColorFor(bg_kind bg = B_GENERAL_BG);

		/** @brief Returns a foreground (text/icon) color for the given usage context.
		 *  @param fg The foreground kind.
		 *  @return The foreground rgb_color.
		 */
		virtual	rgb_color ForegroundColorFor(fg_kind fg = B_GENERAL_FG);

	protected:
		/** @brief Constructs the theme with the given name, info, and optional add-on ref.
		 *  @param name Human-readable theme name.
		 *  @param info Descriptive text.
		 *  @param addOn Optional entry_ref of the add-on image.
		 *  @param themeID Numeric theme identifier.
		 */
		BMediaTheme(const char* name, const char* info,
			const entry_ref* addOn = NULL, int32 themeID = 0);

		/** @brief Creates a BView for the parameter web; implement in your subclass.
		 *  @param web The BParameterWeb to visualize.
		 *  @param hintRect Optional size/position hint.
		 *  @return A new BView containing controls for all parameters.
		 */
		virtual	BView* MakeViewFor(BParameterWeb* web,
			const BRect* hintRect = NULL) = 0;

		/** @brief Creates a fallback default control for a parameter.
		 *  @param control The parameter to represent.
		 *  @return A standard BControl widget.
		 */
		static BControl* MakeFallbackViewFor(BParameter* control);

	private:
		BMediaTheme();		/* private unimplemented */
		BMediaTheme(const BMediaTheme& other);
		BMediaTheme& operator=(const BMediaTheme& other);

		virtual status_t _Reserved_ControlTheme_0(void *);
		virtual status_t _Reserved_ControlTheme_1(void *);
		virtual status_t _Reserved_ControlTheme_2(void *);
		virtual status_t _Reserved_ControlTheme_3(void *);
		virtual status_t _Reserved_ControlTheme_4(void *);
		virtual status_t _Reserved_ControlTheme_5(void *);
		virtual status_t _Reserved_ControlTheme_6(void *);
		virtual status_t _Reserved_ControlTheme_7(void *);

		char*		fName;
		char*		fInfo;
		int32		fID;
		bool		fIsAddOn;
		entry_ref	fAddOnRef;

		uint32 _reserved[8];

		static BMediaTheme* sDefaultTheme;
};


// Theme add-ons should export these functions:
#if defined(_BUILDING_THEME_ADDON)
extern "C" BMediaTheme* make_theme(int32 id, image_id you);
extern "C" status_t get_theme_at(int32 index, const char** _name,
	const char** _info, int32* _id);
#endif	// _BUILDING_THEME_ADDON

#endif	// _CONTROL_THEME_H
