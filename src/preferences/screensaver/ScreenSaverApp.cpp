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
 *   Copyright 2003-2014 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Jérôme Duval, jerome.duval@free.fr
 *       Michael Phipps
 */


/**
 * @file ScreenSaverApp.cpp
 * @brief BApplication entry point for the ScreenSaver preflet.
 *
 * Constructs and shows the ScreenSaverWindow as soon as the application
 * is created. The window persists its own state, so the application has
 * no other responsibilities.
 *
 * @see ScreenSaverWindow
 */


#include <Application.h>
#include <Entry.h>
#include <Path.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "ScreenSaverWindow.h"


/**
 * @brief Thin BApplication wrapper that owns the ScreenSaverWindow.
 */
class ScreenSaverApp : public BApplication {
public:
	ScreenSaverApp();

private:
	BWindow* fScreenSaverWindow;
};


//	#pragma mark - ScreenSaverApp


/**
 * @brief Constructs the application, creates the main window, and shows it.
 *
 * The window is constructed eagerly (not in @c ReadyToRun) so that it is
 * already visible by the time @c Run() begins dispatching messages.
 */
ScreenSaverApp::ScreenSaverApp()
	:
	BApplication("application/x-vnd.Haiku-ScreenSaver")
{
	fScreenSaverWindow = new ScreenSaverWindow();
	fScreenSaverWindow->Show();
}


//	#pragma mark - main()


/**
 * @brief Process entry point: creates the ScreenSaverApp and runs its loop.
 *
 * @return Always zero.
 */
int
main()
{
	ScreenSaverApp app;
	app.Run();
	return 0;
}
