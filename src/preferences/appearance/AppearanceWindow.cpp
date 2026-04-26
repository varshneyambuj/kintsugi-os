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
 *   Copyright 2002-2025, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       DarkWyrm (darkwyrm@earthlink.net)
 *       Alexander von Gluck, kallisti5@unixzen.com
 *       Stephan Aßmus <superstippi@gmx.de>
 */


/**
 * @file AppearanceWindow.cpp
 * @brief Top-level tabbed window of the Appearance preference application.
 *
 * AppearanceWindow hosts the Fonts, Colors, Look-and-Feel, and Antialiasing
 * setting panes inside a BTabView, plus the global Defaults/Revert buttons.
 * It listens for kMsgUpdate notifications from the panes to refresh the
 * enabled state of those buttons.
 *
 * @see FontView, ColorsView, LookAndFeelSettingsView, AntialiasingSettingsView
 */


#include "AppearanceWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <Messenger.h>
#include <SeparatorView.h>
#include <TabView.h>

#include "AntialiasingSettingsView.h"
#include "ColorsView.h"
#include "FontView.h"
#include "LookAndFeelSettingsView.h"


// This file used to be called APRWindow, left this to avoid retranslating everything
#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "APRWindow"


/** @brief Message constant for the Defaults button. */
static const uint32 kMsgSetDefaults = 'dflt';
/** @brief Message constant for the Revert button. */
static const uint32 kMsgRevert = 'rvrt';


/**
 * @brief Constructs the Appearance window and assembles its tabbed UI.
 *
 * Creates the four settings panes, the Defaults/Revert buttons, and
 * arranges them inside a BTabView with a separator and footer.
 *
 * @param frame Initial window rectangle in screen coordinates.
 */
AppearanceWindow::AppearanceWindow(BRect frame)
	:
	BWindow(frame, B_TRANSLATE_SYSTEM_NAME("Appearance"), B_TITLED_WINDOW,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_QUIT_ON_WINDOW_CLOSE, B_ALL_WORKSPACES)
{
	fDefaultsButton = new BButton("defaults", B_TRANSLATE("Defaults"),
		new BMessage(kMsgSetDefaults), B_WILL_DRAW);

	fRevertButton = new BButton("revert", B_TRANSLATE("Revert"),
		new BMessage(kMsgRevert), B_WILL_DRAW);

	BTabView* tabView = new BTabView("tabview", B_WIDTH_FROM_LABEL);

	fFontSettings = new FontView(B_TRANSLATE("Fonts"));

	fColorsView = new ColorsView(B_TRANSLATE("Colors"));

	fLookAndFeelSettings = new LookAndFeelSettingsView(
		B_TRANSLATE("Look and feel"));

	fAntialiasingSettings = new AntialiasingSettingsView(
		B_TRANSLATE("Antialiasing"));

	tabView->AddTab(fFontSettings);
	tabView->AddTab(fColorsView);
	tabView->AddTab(fLookAndFeelSettings);
	tabView->AddTab(fAntialiasingSettings);
	tabView->SetBorder(B_NO_BORDER);

	_UpdateButtons();

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0, B_USE_DEFAULT_SPACING, 0, B_USE_DEFAULT_SPACING)
		.Add(tabView)
		.Add(new BSeparatorView(B_HORIZONTAL))
		.AddGroup(B_HORIZONTAL)
			.Add(fDefaultsButton)
			.Add(fRevertButton)
			.SetInsets(B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING,
				B_USE_DEFAULT_SPACING, 0)
			.AddGlue();
}


/**
 * @brief Dispatches messages from child panes and toolbar buttons.
 *
 * Handles kMsgUpdate notifications by refreshing button state, and the
 * Defaults / Revert button presses by forwarding the action to every
 * settings pane.
 *
 * @param message The incoming BMessage from a pane or button.
 */
void
AppearanceWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgUpdate:
			_UpdateButtons();
			break;

		case kMsgSetDefaults:
			fFontSettings->SetDefaults();
			fColorsView->SetDefaults();
			fLookAndFeelSettings->SetDefaults();
			fAntialiasingSettings->SetDefaults();

			_UpdateButtons();
			break;

		case kMsgRevert:
			fFontSettings->Revert();
			fColorsView->Revert();
			fLookAndFeelSettings->Revert();
			fAntialiasingSettings->Revert();

			_UpdateButtons();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


/**
 * @brief Recomputes the enabled state of the Defaults and Revert buttons.
 *
 * Asks each pane whether it currently differs from defaults or saved
 * values and updates the toolbar accordingly.
 */
void
AppearanceWindow::_UpdateButtons()
{
	fDefaultsButton->SetEnabled(_IsDefaultable());
	fRevertButton->SetEnabled(_IsRevertable());
}


/**
 * @brief Returns whether any pane has a non-default value to reset.
 *
 * @return @c true if at least one pane reports IsDefaultable().
 */
bool
AppearanceWindow::_IsDefaultable() const
{
//	printf("fonts defaultable: %d\n", fFontSettings->IsDefaultable());
//	printf("colors defaultable: %d\n", fColorsView->IsDefaultable());
//	printf("AA defaultable: %d\n", fAntialiasingSettings->IsDefaultable());
//	printf("decor defaultable: %d\n", fLookAndFeelSettings->IsDefaultable());
	return fFontSettings->IsDefaultable()
		|| fColorsView->IsDefaultable()
		|| fLookAndFeelSettings->IsDefaultable()
		|| fAntialiasingSettings->IsDefaultable();
}


/**
 * @brief Returns whether any pane has unsaved edits to undo.
 *
 * @return @c true if at least one pane reports IsRevertable().
 */
bool
AppearanceWindow::_IsRevertable() const
{
//	printf("fonts revertable: %d\n", fFontSettings->IsRevertable());
//	printf("colors revertable: %d\n", fColorsView->IsRevertable());
//	printf("AA revertable: %d\n", fAntialiasingSettings->IsRevertable());
//	printf("decor revertable: %d\n", fLookAndFeelSettings->IsRevertable());
	return fFontSettings->IsRevertable()
		|| fColorsView->IsRevertable()
		|| fLookAndFeelSettings->IsRevertable()
		|| fAntialiasingSettings->IsRevertable();
}
