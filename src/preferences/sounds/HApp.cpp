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
 *   Copyright 2003-2015, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Atsushi Takamatsu
 *       Jérôme Duval
 *       Oliver Ruiz Dorantes
 */


/**
 * @file HApp.cpp
 * @brief BApplication entry point for the Sounds preferences app.
 *
 * Creates the HWindow that lets the user map system event names (the
 * notification beeps registered via BMediaFiles) to wav files on disk, and
 * provides the standard About box.
 *
 * @see HWindow, BMediaFiles
 */


#include "HApp.h"
#include "HWindow.h"

#include <AboutWindow.h>
#include <Alert.h>
#include <Catalog.h>
#include <Locale.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SoundsHApp"


/**
 * @brief Registers the application signature and shows the main window.
 */
HApp::HApp()
	:
	BApplication("application/x-vnd.Haiku-Sounds")
{
	HWindow* window = new HWindow(BRect(-1, -1, 390, 420),
		B_TRANSLATE_SYSTEM_NAME("Sounds"));
	window->Show();
}


/**
 * @brief Destructor; the framework reaps owned windows.
 */
HApp::~HApp()
{
}


/**
 * @brief Shows the standard About box for the Sounds preferences app.
 *
 * Lists the original Haiku contributors so attribution remains visible to
 * the end user.
 */
void
HApp::AboutRequested()
{
	BAboutWindow* window = new BAboutWindow(B_TRANSLATE_SYSTEM_NAME("Sounds"),
		"application/x-vnd.Haiku-Sounds");

	const char* authors[] = {
		"Atsushi Takamatsu",
		"Oliver Ruiz Dorantes",
		"Jérôme DUVAL",
		NULL
	};

	window->AddCopyright(2003, "Haiku, Inc.");
	window->AddAuthors(authors);
	window->Show();
}


//	#pragma mark -


/**
 * @brief Program entry point; instantiates and runs the application.
 *
 * @return Process exit status (always 0 once Run() returns).
 */
int
main()
{
	HApp app;
	app.Run();

	return 0;
}

