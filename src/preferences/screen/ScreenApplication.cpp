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
 *   Copyright 2001-2009, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Rafael Romo
 *       Stefano Ceccherini (burton666@libero.it)
 *       Andrew Bachmann
 *       Sergei Panteleev
 */


/**
 * @file ScreenApplication.cpp
 * @brief Top-level BApplication and main() entry point for the Screen app.
 *
 * Constructs the main ScreenWindow, forwards refresh / desktop-color
 * messages from satellite windows, and shows the About alert.
 */


#include "ScreenApplication.h"
#include "ScreenWindow.h"
#include "ScreenSettings.h"
#include "Constants.h"

#include <Alert.h>
#include <Catalog.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Screen"


/** @brief Application MIME signature recognized by the launch_roster. */
static const char* kAppSignature = "application/x-vnd.Haiku-Screen";


/**
 * @brief Create the main ScreenWindow and show it on launch.
 */
ScreenApplication::ScreenApplication()
	:	BApplication(kAppSignature),
	fScreenWindow(new ScreenWindow(new ScreenSettings()))
{
	fScreenWindow->Show();
}


/**
 * @brief Show a simple About alert in response to the About menu item.
 */
void
ScreenApplication::AboutRequested()
{
	BAlert *aboutAlert = new BAlert(B_TRANSLATE("About"),
		B_TRANSLATE("Screen preferences by the Haiku team"), B_TRANSLATE("OK"),
		NULL, NULL, B_WIDTH_AS_USUAL, B_OFFSET_SPACING, B_INFO_ALERT);
	aboutAlert->SetFlags(aboutAlert->Flags() | B_CLOSE_ON_ESCAPE);
	aboutAlert->Go();
}


/**
 * @brief Forward a small set of UI messages to the main ScreenWindow.
 *
 * @c SET_CUSTOM_REFRESH_MSG is posted by the "Other..." refresh dialog,
 * @c MAKE_INITIAL_MSG by the confirmation alert, and
 * @c UPDATE_DESKTOP_COLOR_MSG by the Backgrounds preferences app.
 *
 * @param message Incoming message to dispatch.
 */
void
ScreenApplication::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case SET_CUSTOM_REFRESH_MSG:
		case MAKE_INITIAL_MSG:
		case UPDATE_DESKTOP_COLOR_MSG:
			fScreenWindow->PostMessage(message);
			break;

		default:
			BApplication::MessageReceived(message);
			break;
	}
}


//	#pragma mark -


/**
 * @brief Process entry point: instantiate the application and enter its loop.
 *
 * @return Always zero on a clean exit.
 */
int
main()
{
	ScreenApplication app;
	app.Run();

	return 0;
}
