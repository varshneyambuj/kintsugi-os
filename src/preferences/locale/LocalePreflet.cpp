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
 *   Copyright 2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 *   Copyright 2010, Adrien Destugues <pulkomandy@pulkomandy.ath.cx>.
 *       All rightts reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file LocalePreflet.cpp
 * @brief Entry point and BApplication for the Kintsugi OS Locale
 *        preference application.
 *
 * Hosts the LocaleWindow, forwards live B_LOCALE_CHANGED notifications to
 * it, restarts Tracker and Deskbar on demand after the filesystem
 * translation flag changes, and provides the standard About box.
 */


#include <AboutWindow.h>
#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <Locale.h>
#include <Roster.h>
#include <TextView.h>

#include "LocalePreflet.h"
#include "LocaleWindow.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Locale Preflet"


/** @brief Localized application name shown in the About window. */
const char* kAppName = B_TRANSLATE_SYSTEM_NAME("Locale");
/** @brief MIME signature this application registers under. */
const char* kSignature = "application/x-vnd.Haiku-Locale";


/**
 * @brief BApplication subclass that owns the Locale preflet window and
 *        handles preflet-wide messages.
 */
class LocalePreflet : public BApplication {
	public:
							LocalePreflet();
		virtual				~LocalePreflet();

		virtual	void		MessageReceived(BMessage* message);

private:
		status_t			_RestartApp(const char* signature) const;

		LocaleWindow*		fLocaleWindow;
};


//	#pragma mark -


/**
 * @brief Constructs the application, creates the LocaleWindow, and shows it.
 */
LocalePreflet::LocalePreflet()
	:
	BApplication(kSignature),
	fLocaleWindow(new LocaleWindow())
{
	fLocaleWindow->Show();
}


/**
 * @brief Destructor.
 */
LocalePreflet::~LocalePreflet()
{
}


/**
 * @brief Application message dispatcher.
 *
 * Refreshes the locale roster on B_LOCALE_CHANGED so live settings updates
 * propagate to the window, restarts Tracker and Deskbar when prompted by
 * the Locale window, and shows the About box on B_ABOUT_REQUESTED.
 *
 * @param message  Incoming BMessage.
 */
void
LocalePreflet::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_LOCALE_CHANGED:
			BLocaleRoster::Default()->Refresh();
			fLocaleWindow->PostMessage(message);
			break;

		case kMsgRestartTrackerAndDeskbar:
			if (message->FindInt32("which") == 1) {
				_RestartApp("application/x-vnd.Be-TRAK");
				_RestartApp("application/x-vnd.Be-TSKB");
			}
			break;

		case B_ABOUT_REQUESTED:
		{
			BAboutWindow* window;

			const char* authors[] = {
				"Axel Dörfler",
				"Adrien Destugues",
				"Oliver Tappe",
				NULL
			};

			window = new BAboutWindow(kAppName, kSignature);
			window->AddCopyright(2005, "Haiku, Inc.");
			window->AddAuthors(authors);

			window->Show();

			break;
		}

		default:
			BApplication::MessageReceived(message);
			break;
	}
}


/**
 * @brief Asks the running instance of @a signature to quit, waits for it
 *        to exit, then relaunches it.
 *
 * Used to apply the filesystem translation flag to Tracker and Deskbar in
 * the same session.
 *
 * @param signature  MIME signature of the application to bounce.
 * @return Status from the relaunch (or the earliest failing step).
 * @retval B_OK  When the application quit cleanly and was relaunched.
 */
status_t
LocalePreflet::_RestartApp(const char* signature) const
{
	app_info info;
	status_t status = be_roster->GetAppInfo(signature, &info);
	if (status != B_OK)
		return status;

	BMessenger application(signature);
	status = application.SendMessage(B_QUIT_REQUESTED);
	if (status != B_OK)
		return status;

	status_t exit;
	wait_for_thread(info.thread, &exit);

	return be_roster->Launch(signature);
}


//	#pragma mark -


/**
 * @brief Process entry point: instantiates LocalePreflet and runs it.
 *
 * @param argc  Standard argc, unused.
 * @param argv  Standard argv, unused.
 * @return Zero on normal termination.
 */
int
main(int argc, char** argv)
{
	LocalePreflet app;
	app.Run();
	return 0;
}

