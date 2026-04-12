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
 *   Copyright 2009-2016 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ryan Leavengood, leavengood@gmail.com
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file DefaultSettingsView.cpp
 * @brief Helper that populates a BView with a standard "no settings" layout.
 *
 * Provides BPrivate::BuildDefaultSettingsView(), which fills the given view
 * with a bold module-name label and an informational string. Screen saver
 * add-ons that have no user-configurable settings call this from their
 * StartConfig() implementation to produce a consistent appearance in the
 * Screen Saver preferences panel.
 *
 * @see BScreenSaver, BLayoutBuilder
 */


#include <DefaultSettingsView.h>

#include <LayoutBuilder.h>
#include <StringView.h>


namespace BPrivate {


/**
 * @brief Populate @a view with a default "no settings available" layout.
 *
 * Sets the view's background to B_PANEL_BACKGROUND_COLOR and adds two
 * BStringView children: @a moduleName in bold and @a info in the regular
 * font. A glue item at the bottom pushes the labels to the top of the view.
 *
 * @param view        The BView to populate; it must already be attached to a
 *                    window or have a valid layout context.
 * @param moduleName  Display name of the screen saver module, shown in bold.
 * @param info        Short informational string (e.g. version or author),
 *                    shown below the module name in the regular font.
 */
void
BuildDefaultSettingsView(BView* view, const char* moduleName, const char* info)
{
	view->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	BStringView* nameStringView = new BStringView("module", moduleName);
	nameStringView->SetFont(be_bold_font);

	BStringView* infoStringView = new BStringView("info", info);

	BLayoutBuilder::Group<>(view, B_VERTICAL, B_USE_SMALL_SPACING)
		.Add(nameStringView)
		.Add(infoStringView)
		.AddGlue()
		.SetInsets(B_USE_DEFAULT_SPACING)
		.End();
}

}	// namespace BPrivate
