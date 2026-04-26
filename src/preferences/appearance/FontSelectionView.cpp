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
 *   Copyright 2001-2024 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Mark Hogben
 *       DarkWyrm <bpmagic@columbus.rr.com>
 *       Axel Dörfler, axeld@pinc-software.de
 *       Philippe Saint-Pierre, stpere@gmail.com
 *       Stephan Aßmus <superstippi@gmx.de>
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file FontSelectionView.cpp
 * @brief One-line family/style/size picker plus preview for a system font.
 *
 * Each FontSelectionView instance manages a single named system font
 * (plain, bold, fixed, or menu). It builds the family/style menu and
 * size spinner, renders a sample sentence in the chosen font, and
 * pushes new selections back to the app_server.
 */


#include "FontSelectionView.h"

#include <Box.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <LayoutItem.h>
#include <Locale.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Spinner.h>
#include <String.h>
#include <TextView.h>

#include <FontPrivate.h>

#include <stdio.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Font Selection view"


/** @brief Smallest font size offered by the size spinner. */
static const float kMinSize = 8.0;
/** @brief Largest font size offered by the size spinner. */
static const float kMaxSize = 72.0;

static const char* kPreviewText = B_TRANSLATE_COMMENT(
	"The quick brown fox jumps over the lazy dog.",
	"Don't translate this literally ! Use a phrase showing all chars "
	"from A to Z.");


// private font API
extern void _set_system_font_(const char *which, font_family family,
	font_style style, float size);
extern status_t _get_system_default_font_(const char* which,
	font_family family, font_style style, float* _size);


// #pragma mark -


/**
 * @brief Constructs the picker for a single named system font.
 *
 * Resolves @a currentFont (or asks the system for the appropriate
 * BFont when @c NULL is passed), builds the family/style menu, size
 * spinner, and preview text view, and lays them out in a grid.
 *
 * @param name        One of "plain", "bold", "fixed", or "menu".
 * @param label       Translatable label shown next to the family menu.
 * @param currentFont Optional initial font; @c NULL means look up
 *                    the system default for @a name.
 */
FontSelectionView::FontSelectionView(const char* name,
	const char* label, const BFont* currentFont)
	:
	BView(name, B_WILL_DRAW),
	fMessageTarget(this)
{
	if (currentFont == NULL) {
		if (!strcmp(Name(), "plain"))
			fCurrentFont = *be_plain_font;
		else if (!strcmp(Name(), "bold"))
			fCurrentFont = *be_bold_font;
		else if (!strcmp(Name(), "fixed"))
			fCurrentFont = *be_fixed_font;
		else if (!strcmp(Name(), "menu")) {
			menu_info info;
			get_menu_info(&info);

			fCurrentFont.SetFamilyAndStyle(info.f_family, info.f_style);
			fCurrentFont.SetSize(info.font_size);
		}
	} else
		fCurrentFont = *currentFont;

	fSavedFont = fCurrentFont;

	fFontsMenu = new BPopUpMenu("font menu");

	// font menu
	fFontsMenuField = new BMenuField("fonts", label, fFontsMenu);
	fFontsMenuField->SetAlignment(B_ALIGN_RIGHT);

	// font size
	BMessage* fontSizeMessage = new BMessage(kMsgSetSize);
	fontSizeMessage->AddString("name", Name());

	fFontSizeSpinner = new BSpinner("font size", B_TRANSLATE("Size:"), fontSizeMessage);

	fFontSizeSpinner->SetRange(kMinSize, kMaxSize);
	fFontSizeSpinner->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	// preview
	// A string view would be enough if only it handled word-wrap.
	fPreviewTextView = new BTextView("preview text");
	fPreviewTextView->SetFontAndColor(&fCurrentFont);
	fPreviewTextView->SetText(kPreviewText);
	fPreviewTextView->MakeResizable(false);
	fPreviewTextView->SetWordWrap(true);
	fPreviewTextView->MakeEditable(false);
	fPreviewTextView->MakeSelectable(false);
	fPreviewTextView->SetInsets(0, 0, 0, 0);
	fPreviewTextView->SetViewUIColor(ViewUIColor());
	fPreviewTextView->SetLowUIColor(LowUIColor());
	fPreviewTextView->SetHighUIColor(HighUIColor());

	// determine initial line count using fCurrentFont
	fPreviewTextWidth = be_control_look->DefaultLabelSpacing() * 58.0f;
	float lineCount = ceilf(fCurrentFont.StringWidth(kPreviewText) / fPreviewTextWidth);
	fPreviewTextView->SetExplicitSize(BSize(fPreviewTextWidth,
		fPreviewTextView->LineHeight(0) * lineCount));

	// box around preview
	fPreviewBox = new BBox("preview box", B_WILL_DRAW | B_FRAME_EVENTS);
	fPreviewBox->AddChild(BLayoutBuilder::Group<>(B_HORIZONTAL, 0)
		.Add(fPreviewTextView)
		.AddGlue()
		.SetInsets(B_USE_SMALL_SPACING)
		.View());

	BLayoutBuilder::Grid<>(this, 5, 5)
		// add fonts menu and font size spinner
		.Add(fFontsMenuField->CreateLabelLayoutItem(), 0, 0)
		.Add(fFontsMenuField->CreateMenuBarLayoutItem(), 1, 0)
		.Add(BSpaceLayoutItem::CreateGlue(), 2, 0)
		.Add(fFontSizeSpinner, 4, 0)
		// add font preview
		.Add(BSpaceLayoutItem::CreateGlue(), 0, 1)
		.Add(fPreviewBox, 1, 1, 4)
		.SetInsets(0, B_USE_SMALL_SPACING, 0, B_USE_SMALL_SPACING);

	_SelectCurrentSize();
}


/**
 * @brief Destructor; child views are owned by the BView hierarchy.
 */
FontSelectionView::~FontSelectionView()
{
}


/**
 * @brief Routes future control messages to @a messageTarget.
 *
 * Used by FontView to funnel messages from all four pickers through a
 * single handler.
 *
 * @param messageTarget Handler that should receive the control messages.
 */
void
FontSelectionView::SetTarget(BHandler* messageTarget)
{
	fMessageTarget = messageTarget;
	fFontSizeSpinner->SetTarget(messageTarget);
}


/**
 * @brief Handles family-, style-, size-, and color-change messages.
 *
 * Translates B_COLORS_UPDATED into a recolor of the preview text and
 * the kMsgSet* messages into family/style/size mutations on @c
 * fCurrentFont, refreshing the preview after each change.
 *
 * @param msg The incoming BMessage.
 */
void
FontSelectionView::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case B_COLORS_UPDATED:
		{
			if (msg->HasColor(ui_color_name(B_PANEL_TEXT_COLOR))) {
				rgb_color textColor;
				if (msg->FindColor(ui_color_name(B_PANEL_TEXT_COLOR), &textColor) == B_OK)
					fPreviewTextView->SetFontAndColor(&fCurrentFont, B_FONT_ALL, &textColor);
			}
			break;
		}

		case kMsgSetSize:
		{
			int32 size = fFontSizeSpinner->Value();
			if (size == fCurrentFont.Size())
				break;

			fCurrentFont.SetSize(size);
			_UpdateFontPreview();
			break;
		}

		case kMsgSetFamily:
		{
			const char* family;
			if (msg->FindString("family", &family) != B_OK)
				break;

			font_style style;
			fCurrentFont.GetFamilyAndStyle(NULL, &style);

			BMenuItem* familyItem = fFontsMenu->FindItem(family);
			if (familyItem != NULL) {
				_SelectCurrentFont(false);

				BMenuItem* item = familyItem->Submenu()->FindItem(style);
				if (item == NULL)
					item = familyItem->Submenu()->ItemAt(0);

				if (item != NULL) {
					item->SetMarked(true);
					fCurrentFont.SetFamilyAndStyle(family, item->Label());
					_UpdateFontPreview();
				}
			}
			break;
		}

		case kMsgSetStyle:
		{
			const char* family;
			const char* style;
			if (msg->FindString("family", &family) != B_OK
				|| msg->FindString("style", &style) != B_OK)
				break;

			BMenuItem *familyItem = fFontsMenu->FindItem(family);
			if (!familyItem)
				break;

			_SelectCurrentFont(false);
			familyItem->SetMarked(true);

			fCurrentFont.SetFamilyAndStyle(family, style);
			_UpdateFontPreview();
			break;
		}

		default:
			BView::MessageReceived(msg);
	}
}


/**
 * @brief Marks or unmarks the menu items matching the active font.
 *
 * Used to keep the family submenu in sync when the family/style is
 * changed programmatically.
 *
 * @param select Pass @c true to mark the items, @c false to unmark them.
 */
void
FontSelectionView::_SelectCurrentFont(bool select)
{
	font_family family;
	font_style style;
	fCurrentFont.GetFamilyAndStyle(&family, &style);

	BMenuItem *item = fFontsMenu->FindItem(family);
	if (item != NULL) {
		item->SetMarked(select);

		if (item->Submenu() != NULL) {
			item = item->Submenu()->FindItem(style);
			if (item != NULL)
				item->SetMarked(select);
		}
	}
}


/**
 * @brief Snaps the size spinner to the active font's size.
 */
void
FontSelectionView::_SelectCurrentSize()
{
	fFontSizeSpinner->SetValue((int32)fCurrentFont.Size());
}


/**
 * @brief Re-renders the preview after a font mutation.
 *
 * Pushes the new font to the system, repaints the preview, and resizes
 * it to fit the wrapped text.
 */
void
FontSelectionView::_UpdateFontPreview()
{
	_UpdateSystemFont();

	fPreviewTextView->SetFontAndColor(&fCurrentFont);
	fPreviewTextView->SetExplicitSize(BSize(fPreviewTextWidth,
		fPreviewTextView->LineHeight(0) * fPreviewTextView->CountLines()));
}


/**
 * @brief Pushes the active font into the system font registry.
 *
 * For the menu font this updates the global @c menu_info; for plain,
 * bold and fixed fonts it calls into the private
 * @c _set_system_font_() helper.
 */
void
FontSelectionView::_UpdateSystemFont()
{
	font_family family;
	font_style style;
	fCurrentFont.GetFamilyAndStyle(&family, &style);

	if (strcmp(Name(), "menu") == 0) {
		// The menu font is not handled as a system font
		menu_info info;
		get_menu_info(&info);

		strlcpy(info.f_family, (const char*)family, B_FONT_FAMILY_LENGTH);
		strlcpy(info.f_style, (const char*)style, B_FONT_STYLE_LENGTH);
		info.font_size = fCurrentFont.Size();

		set_menu_info(&info);
	} else
		_set_system_font_(Name(), family, style, fCurrentFont.Size());
}


/**
 * @brief Resets the active font to the system's compiled-in default.
 *
 * Reads the default through the private @c _get_system_default_font_()
 * helper. Falls back to Revert() if the call fails.
 */
void
FontSelectionView::SetDefaults()
{
	font_family family;
	font_style style;
	float size;
	const char* fontName;

	if (strcmp(Name(), "menu") == 0)
		fontName = "plain";
	else
		fontName = Name();

	if (_get_system_default_font_(fontName, family, style, &size) != B_OK) {
		Revert();
		return;
	}

	BFont defaultFont;
	defaultFont.SetFamilyAndStyle(family, style);
	defaultFont.SetSize(size);

	if (defaultFont == fCurrentFont)
		return;

	_SelectCurrentFont(false);

	fCurrentFont = defaultFont;
	_UpdateFontPreview();

	_SelectCurrentFont(true);
	_SelectCurrentSize();
}


/**
 * @brief Restores the font that was active when the view was constructed.
 *
 * No-op when the font has not changed.
 */
void
FontSelectionView::Revert()
{
	if (!IsRevertable())
		return;

	_SelectCurrentFont(false);

	fCurrentFont = fSavedFont;
	_UpdateFontPreview();

	_SelectCurrentFont(true);
	_SelectCurrentSize();
}


/**
 * @brief Reports whether the active font differs from the system default.
 *
 * @return @c true when family, style or size differs from the compiled-in
 *         default. @c false on lookup failure.
 */
bool
FontSelectionView::IsDefaultable()
{
	font_family defaultFamily;
	font_style defaultStyle;
	float defaultSize;
	const char* fontName;

	if (strcmp(Name(), "menu") == 0)
		fontName = "plain";
	else
		fontName = Name();

	if (_get_system_default_font_(fontName, defaultFamily, defaultStyle,
		&defaultSize) != B_OK) {
		return false;
	}

	font_family currentFamily;
	font_style currentStyle;
	float currentSize;

	fCurrentFont.GetFamilyAndStyle(&currentFamily, &currentStyle);
	currentSize = fCurrentFont.Size();

	return strcmp(currentFamily, defaultFamily) != 0
		|| strcmp(currentStyle, defaultStyle) != 0
		|| currentSize != defaultSize;
}


/**
 * @brief Reports whether the active font differs from the saved snapshot.
 *
 * @return @c true if @c fCurrentFont differs from @c fSavedFont.
 */
bool
FontSelectionView::IsRevertable()
{
	return fCurrentFont != fSavedFont;
}


/**
 * @brief Rebuilds the family/style menu from the current font registry.
 *
 * Honours the "fixed" name by including only fixed-width and
 * full-and-half-fixed families, and pre-marks the entry that matches
 * @c fCurrentFont.
 */
void
FontSelectionView::UpdateFontsMenu()
{
	int32 numFamilies = count_font_families();

	fFontsMenu->RemoveItems(0, fFontsMenu->CountItems(), true);
	BFont font;
	fFontsMenu->GetFont(&font);

	font_family currentFamily;
	font_style currentStyle;
	fCurrentFont.GetFamilyAndStyle(&currentFamily, &currentStyle);

	for (int32 i = 0; i < numFamilies; i++) {
		font_family family;
		uint32 flags;
		if (get_font_family(i, &family, &flags) != B_OK)
			continue;

		// if we're setting the fixed font, we only want to show fixed and
		// full-and-half-fixed fonts
		if (strcmp(Name(), "fixed") == 0
			&& (flags
				& (B_IS_FIXED | B_PRIVATE_FONT_IS_FULL_AND_HALF_FIXED)) == 0) {
			continue;
		}

		BMenu* stylesMenu = new BMenu(family);
		stylesMenu->SetRadioMode(true);
		stylesMenu->SetFont(&font);

		BMessage* message = new BMessage(kMsgSetFamily);
		message->AddString("family", family);
		message->AddString("name", Name());

		BMenuItem* familyItem = new BMenuItem(stylesMenu, message);
		fFontsMenu->AddItem(familyItem);

		int32 numStyles = count_font_styles(family);

		for (int32 j = 0; j < numStyles; j++) {
			font_style style;
			if (get_font_style(family, j, &style, &flags) != B_OK)
				continue;

			message = new BMessage(kMsgSetStyle);
			message->AddString("family", (char*)family);
			message->AddString("style", (char*)style);
			message->AddString("name", Name());

			BMenuItem* item = new BMenuItem(style, message);

			if (!strcmp(style, currentStyle)
				&& !strcmp(family, currentFamily)) {
				item->SetMarked(true);
				familyItem->SetMarked(true);
			}
			stylesMenu->AddItem(item);
		}

		stylesMenu->SetTargetForItems(fMessageTarget);
	}

	fFontsMenu->SetTargetForItems(fMessageTarget);
}
