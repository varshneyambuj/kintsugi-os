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
 *   Copyright 2007 Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 *       Ryan Leavengood, leavengood@gmail.com
 */


/**
 * @file Joysticks.cpp
 * @brief BApplication entry point for the Joysticks preference panel.
 *
 * Creates the application object, registers with the app server using the
 * vendor signature, and instantiates the main JoyWin window once the
 * application is ready to run.
 *
 * @see JoyWin
 */


#include "Joysticks.h"
#include "JoyWin.h"

#include <stdio.h>
#include <stdlib.h>

#include <Window.h>
#include <View.h>
#include <Button.h>
#include <Box.h>
#include <StringView.h>

/**
 * @brief Process entry point.
 *
 * Constructs the Joysticks application with its vendor signature and runs
 * the BApplication message loop until the user quits.
 *
 * @return Always 0 once the application loop exits.
 */
int main(void)
{
	Joysticks application("application/x-vnd.Haiku-Joysticks");
	application.Run();
	return 0;
}


/**
 * @brief Constructs the Joysticks BApplication.
 *
 * @param signature MIME application signature used by the registrar to
 *                  identify this preference panel.
 */
Joysticks::Joysticks(const char *signature)
	: BApplication(signature)
{
}


/**
 * @brief Destructor; posts B_QUIT_REQUESTED so the message loop tears down
 *        cleanly.
 */
Joysticks::~Joysticks()
{
	be_app_messenger.SendMessage(B_QUIT_REQUESTED);
}


/**
 * @brief Handles a quit request from the application or user.
 *
 * @return The default BApplication response, allowing termination.
 */
bool
Joysticks::QuitRequested()
{
	return BApplication::QuitRequested();
}


/**
 * @brief Hook invoked when the application is ready for its main window.
 *
 * Allocates the main JoyWin instance and shows it. If allocation fails the
 * application posts a quit request to itself.
 */
void
Joysticks::ReadyToRun()
{
	fJoywin = new JoyWin(BRect(100, 100, 500, 400), "Joysticks");
	if (fJoywin != NULL)
		fJoywin->Show();
	else
		be_app->PostMessage(B_QUIT_REQUESTED);
}

