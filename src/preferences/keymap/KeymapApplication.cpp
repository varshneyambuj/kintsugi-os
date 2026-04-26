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
 *   Copyright 2004-2014 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Jerome Duval
 *       John Scipione, jscipione@gmail.com
 *       Sandor Vroemisse
 */


/**
 * @file KeymapApplication.cpp
 * @brief BApplication and main() entry point for the Keymap preferences app.
 */


#include "KeymapApplication.h"


//	#pragma mark - KeymapApplication


/**
 * @brief Construct the application and immediately show the main window.
 */
KeymapApplication::KeymapApplication()
	:
	BApplication("application/x-vnd.Haiku-Keymap"),
	fModifierKeysWindow(NULL)
{
	// create the window
	fWindow = new KeymapWindow();
	fWindow->Show();
}


/**
 * @brief Route inter-window messages.
 *
 * Opens the ModifierKeysWindow on demand, clears the cached pointer when
 * it closes, and forwards modifier-update messages to the main window.
 *
 * @param message Incoming message.
 */
void
KeymapApplication::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgShowModifierKeysWindow:
			_ShowModifierKeysWindow();
			break;
		case kMsgCloseModifierKeysWindow:
			fModifierKeysWindow = NULL;
			break;
		case kMsgUpdateModifierKeys:
			fWindow->PostMessage(message);
			break;
	}

	BApplication::MessageReceived(message);
}


/**
 * @brief Show the modifier-keys editor, raising it if already open.
 *
 * The new window is centered on the main KeymapWindow's frame.
 */
void
KeymapApplication::_ShowModifierKeysWindow()
{
	if (fModifierKeysWindow != NULL)
		fModifierKeysWindow->Activate();
	else {
		fModifierKeysWindow = new ModifierKeysWindow();
		fModifierKeysWindow->CenterIn(fWindow->Frame());
		fModifierKeysWindow->Show();
	}
}


//	#pragma mark - main method


/**
 * @brief Process entry point: instantiate the application and enter its loop.
 *
 * @return Always @c B_OK on a clean exit.
 */
int
main(int, char**)
{
	new KeymapApplication;
	be_app->Run();
	delete be_app;
	return B_OK;
}
