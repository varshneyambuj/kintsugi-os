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
 *   Copyright 2007-2011 Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ryan Leavengood, leavengood@gmail.com
 *       Jonas Sundström, jonas@kirilla.se
 */

/** @file AboutMenuItem.cpp
 *  @brief Implements \c BAboutMenuItem, a standard "About <AppName>" menu item
 *         that dispatches \c B_ABOUT_REQUESTED when activated.
 */

#include <AboutMenuItem.h>
#include <Application.h>
#include <Roster.h>
#include <String.h>
#include <SystemCatalog.h>

using BPrivate::gSystemCatalog;


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AboutMenuItem"


/**
 * @brief Constructs a localised "About <AppName>" menu item.
 *
 * Queries \c be_app for the running application's name via \c GetAppInfo()
 * and builds a translated label of the form "About <AppName>". If the
 * application name cannot be determined the placeholder \c "(NULL)" is used
 * instead. The item is pre-wired to send \c B_ABOUT_REQUESTED on invocation.
 */
BAboutMenuItem::BAboutMenuItem()
	:
	BMenuItem("", new BMessage(B_ABOUT_REQUESTED))
{
	app_info info;
	const char* name = NULL;
	if (be_app != NULL && be_app->GetAppInfo(&info) == B_OK)
		name = B_TRANSLATE_NOCOLLECT_SYSTEM_NAME(info.ref.name);

	const char* string = B_TRANSLATE_MARK("About %app%");
	string = gSystemCatalog.GetString(string, "AboutMenuItem");

	BString label = string;
	if (name != NULL)
		label.ReplaceFirst("%app%", name);
	else
		label.ReplaceFirst("%app%", "(NULL)");
	SetLabel(label.String());
}
