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
 *   Copyright 2010-2017, Haiku, Inc. All Rights Reserved.
 *   Copyright 2009, Pier Luigi Fiorini.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Pier Luigi Fiorini, pierluigi.fiorini@gmail.com
 */


/**
 * @file Notifications.cpp
 * @brief Entry point for the Kintsugi OS Notifications preference
 *        application.
 *
 * Registers the application with its MIME signature and shows the
 * PrefletWin once the runtime is ready. The Notifications preflet drives
 * configuration of notification_server: enabled apps, display position,
 * width, and auto-hide duration.
 *
 * @see PrefletWin
 */


#include <Application.h>

#include "PrefletWin.h"


/**
 * @brief BApplication subclass that owns the Notifications preflet window.
 */
class PrefletApp : public BApplication {
public:
						PrefletApp();

	virtual	void		ReadyToRun();
	virtual	bool		QuitRequested();

private:
			PrefletWin*	fWindow;
};


/**
 * @brief Constructs the application and registers its MIME signature.
 */
PrefletApp::PrefletApp()
	:
	BApplication("application/x-vnd.Haiku-Notifications")
{
}


/**
 * @brief Creates the preflet window once the BApplication runtime is ready.
 *
 * The window is allocated with @c new and shows itself in its constructor;
 * the BLooper hierarchy reclaims it on quit.
 */
void
PrefletApp::ReadyToRun()
{
	fWindow = new PrefletWin;
}


/**
 * @brief Allows the application to terminate when the last window closes.
 *
 * @return Always true.
 */
bool
PrefletApp::QuitRequested()
{
	return true;
}


/**
 * @brief Process entry point: instantiates and runs PrefletApp.
 *
 * @param argc  Standard argc, unused.
 * @param argv  Standard argv, unused.
 * @return Zero on normal termination.
 */
int
main(int argc, char* argv[])
{
	PrefletApp app;
	app.Run();
	return 0;
}
