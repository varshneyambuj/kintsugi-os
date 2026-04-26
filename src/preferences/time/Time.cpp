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
 *   Copyright 2002-2021, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Andrew McCall <mccall@digitalparadise.co.uk>
 *       Mike Berg <mike@berg-net.us>
 *       Julun <host.haiku@gmx.de>
 *       Hamish Morrison <hamish@lavabit.com>
 *       Panagiotis "Ivory" Vasilopoulos <git@n0toose.net>
 */


/**
 * @file Time.cpp
 * @brief Implementation of the Time & Date preference application.
 *
 * Defines the BApplication subclass that owns the preference window and the
 * program entry point. Also exposes a "--update" command-line mode that
 * forces an NTP synchronization without bringing up the GUI.
 */


#include "Time.h"

#include <locale.h>
#include <stdio.h>
#include <unistd.h>

#include <AboutWindow.h>
#include <Alert.h>
#include <Catalog.h>
#include <Locale.h>
#include <LocaleRoster.h>

#include "NetworkTimeView.h"
#include "TimeMessages.h"
#include "TimeWindow.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Time"


/** @brief MIME signature used to register the application with the roster. */
const char* kAppSignature = "application/x-vnd.Haiku-Time";


/**
 * @brief Constructs the Time application and creates its main window.
 */
TimeApplication::TimeApplication()
	:
	BApplication(kAppSignature),
	fWindow(NULL)
{
	fWindow = new TTimeWindow();
}


/**
 * @brief Destructor; the window owns its own lifetime.
 */
TimeApplication::~TimeApplication()
{
}


/**
 * @brief Shows the main preference window once the message loop is running.
 */
void
TimeApplication::ReadyToRun()
{
	fWindow->Show();
}


/**
 * @brief Displays the application's About dialog.
 *
 * Composes a BAboutWindow with the canonical author list and the upstream
 * Haiku copyright.
 */
void
TimeApplication::AboutRequested()
{
	BAboutWindow* window = new BAboutWindow(B_TRANSLATE_SYSTEM_NAME(
		"Time & Date"), kAppSignature);

	const char* authors[] = {
		"Mike Berg",
		"Andrew Edward McCall",
		"Hamish Morrison",
		"Philippe Saint-Pierre",
		"Panagiotis \"Ivory\" Vasilopoulos",
		"Julun",
		NULL
	};

	window->AddCopyright(2021, "Haiku, Inc.");
	window->AddAuthors(authors);

	window->Show();
}


/**
 * @brief Forwards a small set of messages to the preference window.
 *
 * Intercepts kSelectClockTab, kShowHideTime, and B_LOCALE_CHANGED so that
 * external clients (notably the Deskbar) can drive the Time window without
 * having to know its messenger directly. Everything else falls through to
 * BApplication.
 *
 * @param message Incoming message.
 */
void
TimeApplication::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kSelectClockTab:
		case kShowHideTime:
		case B_LOCALE_CHANGED:
			fWindow->PostMessage(message);
			break;

		default:
			BApplication::MessageReceived(message);
			break;
	}
}


/**
 * @brief Program entry point.
 *
 * Without arguments, runs the GUI as root so that set_real_time_clock()
 * succeeds. With "--update", performs an NTP sync using the saved settings
 * and prints the result instead of starting the application loop.
 *
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument vector. argv[1] of "--update" triggers
 *             a non-interactive NTP synchronization.
 * @return Zero on a clean shutdown.
 */
int
main(int argc, char** argv)
{
	if (argc > 1) {
		if (strcmp(argv[1], "--update") != 0) {
			fprintf(stderr, "Usage: %s [--update]\n", argv[0]);
			fprintf(stderr, "    --update    Optionally force an NTP clock sync and exit\n\n");
			return 0;
		}

		Settings settings;
		const char* errorString = NULL;
		int32 errorCode = 0;
		if (update_time(settings, &errorString, &errorCode) == B_OK) {
			printf("Synchronization successful\n");
		} else if (errorCode != 0) {
			printf("The following error occured "
					"while synchronizing:\n%s: %s\n",
				errorString, strerror(errorCode));
		} else {
			printf("The following error occured while synchronizing:\n%s\n",
				errorString);
		}
	} else {
		setlocale(LC_ALL, "");

		TimeApplication app;
		setuid(0);
		app.Run();
	}

	return 0;
}

