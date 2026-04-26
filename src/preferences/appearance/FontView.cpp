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
 *   Copyright 2001-2012, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Mark Hogben
 *       DarkWyrm <bpmagic@columbus.rr.com>
 *       Axel Dörfler, axeld@pinc-software.de
 *       Philippe St-Pierre, stpere@gmail.com
 *       Stephan Aßmus <superstippi@gmx.de>
 */


/**
 * @file FontView.cpp
 * @brief Aggregates the four FontSelectionView pickers in the Fonts tab.
 *
 * Owns the plain, bold, fixed, and menu pickers, aligns their first
 * column to the longest label and polls the system font registry
 * every three seconds so the menus reflect newly installed fonts.
 */


#include "FontView.h"

#include <algorithm>

#include <string.h>

#include <Catalog.h>
#include <ControlLook.h>
#include <GridLayout.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MessageRunner.h>
#include <SpaceLayoutItem.h>

#include "AppearanceWindow.h"
#include "FontSelectionView.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Font view"


/** @brief Tick message that triggers a font registry refresh. */
static const uint32 kMsgCheckFonts = 'chkf';


/**
 * @brief Constructs the Fonts tab and its four font pickers.
 *
 * Computes the maximum label width across the four pickers and pins
 * each picker's first column to that width so the labels line up.
 *
 * @param name Identifier passed to the BView base class.
 */
FontView::FontView(const char* name)
	:
	BView(name, B_WILL_DRAW )
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	const char* plainLabel = B_TRANSLATE("Plain font:");
	const char* boldLabel = B_TRANSLATE("Bold font:");
	const char* fixedLabel = B_TRANSLATE("Fixed font:");
	const char* menuLabel = B_TRANSLATE("Menu font:");

	fPlainView = new FontSelectionView("plain", plainLabel);
	fBoldView = new FontSelectionView("bold", boldLabel);
	fFixedView = new FontSelectionView("fixed", fixedLabel);
	fMenuView = new FontSelectionView("menu", menuLabel);

	// find the longest label
	float longestLabel = StringWidth(plainLabel);
	longestLabel = std::max(longestLabel, StringWidth(boldLabel));
	longestLabel = std::max(longestLabel, StringWidth(fixedLabel));
	longestLabel = std::max(longestLabel, StringWidth(menuLabel));
	longestLabel += be_control_look->DefaultLabelSpacing();

	// set the first column to the width of the longest label
	BGridLayout* gridLayout;
	gridLayout = (BGridLayout*)fPlainView->GetLayout();
	gridLayout->SetMinColumnWidth(0, longestLabel);
	gridLayout = (BGridLayout*)fBoldView->GetLayout();
	gridLayout->SetMinColumnWidth(0, longestLabel);
	gridLayout = (BGridLayout*)fFixedView->GetLayout();
	gridLayout->SetMinColumnWidth(0, longestLabel);
	gridLayout = (BGridLayout*)fMenuView->GetLayout();
	gridLayout->SetMinColumnWidth(0, longestLabel);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fPlainView)
		.Add(fBoldView)
		.Add(fFixedView)
		.Add(fMenuView)
		.Add(BSpaceLayoutItem::CreateVerticalStrut(5))
		.SetInsets(B_USE_WINDOW_SPACING);
}


/**
 * @brief Wires picker targets and starts the font-update poll timer.
 *
 * Posts kMsgCheckFonts every three seconds so the family/style menus
 * pick up newly installed fonts without restarting the preflet.
 */
void
FontView::AttachedToWindow()
{
	fPlainView->SetTarget(this);
	fBoldView->SetTarget(this);
	fFixedView->SetTarget(this);
	fMenuView->SetTarget(this);

	UpdateFonts();
	fRunner = new BMessageRunner(this, new BMessage(kMsgCheckFonts), 3000000);
		// every 3 seconds
}


/**
 * @brief Stops the font-update poll timer when the view leaves a window.
 */
void
FontView::DetachedFromWindow()
{
	delete fRunner;
	fRunner = NULL;
}


/**
 * @brief Resets every picker to its compiled-in system default.
 */
void
FontView::SetDefaults()
{
	fPlainView->SetDefaults();
	fBoldView->SetDefaults();
	fFixedView->SetDefaults();
	fMenuView->SetDefaults();
}


/**
 * @brief Routes per-picker messages to the FontSelectionView they target.
 *
 * Looks up the destination picker via the "name" string carried in the
 * message ("plain", "bold", "fixed", "menu") and forwards the message
 * unchanged. Also services kMsgCheckFonts ticks by reloading menus
 * when @c update_font_families() reports a change.
 *
 * @param message The incoming BMessage.
 */
void
FontView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSetSize:
		case kMsgSetFamily:
		case kMsgSetStyle:
		{
			const char* name;
			if (message->FindString("name", &name) != B_OK)
				break;

			if (!strcmp(name, "plain"))
				fPlainView->MessageReceived(message);
			else if (!strcmp(name, "bold"))
				fBoldView->MessageReceived(message);
			else if (!strcmp(name, "fixed"))
				fFixedView->MessageReceived(message);
			else if (!strcmp(name, "menu"))
				fMenuView->MessageReceived(message);
			else
				break;

			Window()->PostMessage(kMsgUpdate);
			break;
		}

		case kMsgCheckFonts:
			if (update_font_families(true))
				UpdateFonts();
			break;

		default:
			BView::MessageReceived(message);
	}
}


/**
 * @brief Restores every picker to its constructor-time snapshot.
 */
void
FontView::Revert()
{
	fPlainView->Revert();
	fBoldView->Revert();
	fFixedView->Revert();
	fMenuView->Revert();
}


/**
 * @brief Rebuilds the family/style menu of every picker.
 *
 * Called on attach and whenever @c update_font_families() reports that
 * the list of installed fonts has changed.
 */
void
FontView::UpdateFonts()
{
	fPlainView->UpdateFontsMenu();
	fBoldView->UpdateFontsMenu();
	fFixedView->UpdateFontsMenu();
	fMenuView->UpdateFontsMenu();
}


/**
 * @brief Reports whether any picker has a non-default font selected.
 *
 * @return @c true if at least one picker reports IsDefaultable().
 */
bool
FontView::IsDefaultable()
{
	return fPlainView->IsDefaultable()
		|| fBoldView->IsDefaultable()
		|| fFixedView->IsDefaultable()
		|| fMenuView->IsDefaultable();
}


/**
 * @brief Reports whether any picker has unsaved edits.
 *
 * @return @c true if at least one picker reports IsRevertable().
 */
bool
FontView::IsRevertable()
{
	return fPlainView->IsRevertable()
		|| fBoldView->IsRevertable()
		|| fFixedView->IsRevertable()
		|| fMenuView->IsRevertable();
}

