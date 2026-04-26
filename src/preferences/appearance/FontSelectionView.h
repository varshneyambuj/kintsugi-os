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
 * MIT License. Copyright 2001-2022 Haiku, Inc.
 * Original authors: Mark Hogben, DarkWyrm, Axel Dörfler, Philippe Saint-Pierre.
 */

/** @file FontSelectionView.h
    @brief Per-font family/style/size picker with a live preview. */

#ifndef FONT_SELECTION_VIEW_H
#define FONT_SELECTION_VIEW_H


#include <View.h>

class BLayoutItem;
class BBox;
class BMenuField;
class BPopUpMenu;
class BSpinner;
class BTextView;

/** @brief Message constant emitted when the user picks a different family. */
static const int32 kMsgSetFamily = 'fmly';
/** @brief Message constant emitted when the user picks a different style. */
static const int32 kMsgSetStyle = 'styl';
/** @brief Message constant emitted when the user changes the font size. */
static const int32 kMsgSetSize = 'size';


/**
 * @brief BView subclass that drives a single named system font.
 *
 * The "name" argument selects which system font this view manages
 * ("plain", "bold", "fixed" or "menu").
 */
class FontSelectionView : public BView {
public:
								FontSelectionView(const char* name,
									const char* label,
									const BFont* font = NULL);
	virtual						~FontSelectionView();

	virtual void				MessageReceived(BMessage* message);

			void				SetTarget(BHandler* messageTarget);

			void				SetDefaults();
			void				Revert();
			bool				IsDefaultable();
			bool				IsRevertable();

			void				UpdateFontsMenu();

private:
			void				_SelectCurrentFont(bool select);
			void				_SelectCurrentSize();
			void				_UpdateFontPreview();
			void				_UpdateSystemFont();
			void				_BuildSizesMenu();

protected:
			BHandler*			fMessageTarget;

			BMenuField*			fFontsMenuField;
			BPopUpMenu*			fFontsMenu;

			BSpinner*			fFontSizeSpinner;

			BBox*				fPreviewBox;
			BTextView*			fPreviewTextView;
			float				fPreviewTextWidth;

			BFont				fSavedFont;
			BFont				fCurrentFont;
};

#endif	// FONT_SELECTION_VIEW_H
