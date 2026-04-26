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
 *   Copyright 2019, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Preetpal Kaur <preetpalok123@gmail.com>
 */


/**
 * @file Input.cpp
 * @brief Implementation of the Input preferences BApplication.
 *
 * InputApplication is the entry point of the Input preflet. It owns the
 * shared InputIcons resource set, opens the InputWindow, and forwards
 * mouse, keyboard, and touchpad notification messages to the active
 * settings card.
 *
 * @see InputWindow, DeviceListItemView, InputIcons
 */


#include "Input.h"

#include <GroupLayout.h>
#include <GroupLayoutBuilder.h>

#include "InputConstants.h"
#include "InputDeviceView.h"
#include "InputTouchpadPrefView.h"
#include "InputWindow.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "InputApplication"

/** @brief MIME signature used when registering the Input preferences app. */
const char* kSignature = "application/x-vnd.Haiku-Input";


/**
 * @brief Constructs the Input preferences application and shows the window.
 *
 * Registers the application with @ref kSignature, instantiates the shared
 * InputIcons resource bundle, creates the InputWindow, and shows it. The
 * device list view is told about the icons up front so freshly added
 * DeviceListItemView entries can render immediately.
 */
InputApplication::InputApplication()
	:
	BApplication(kSignature),
	fIcons()
{
	BRect rect(0, 0, 600, 500);
	InputWindow* window = new InputWindow(rect);
	DeviceListItemView::SetIcons(&fIcons);
	window->Show();
}


/**
 * @brief Routes preference messages from controls into the active card.
 *
 * Mouse, touchpad and keyboard controls live on different cards inside
 * the InputWindow's BCardView. The application does not know which card
 * is active, so it forwards every relevant message to the window which
 * dispatches to the visible card.
 *
 * @param message  Incoming BMessage. Unrecognised messages fall through
 *                 to BApplication::MessageReceived.
 */
void
InputApplication::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgMouseType:
		case kMsgMouseMap:
		case kMsgMouseFocusMode:
		case kMsgFollowsMouseMode:
		case kMsgAcceptFirstClick:
		case kMsgDoubleClickSpeed:
		case kMsgMouseSpeed:
		case kMsgAccelerationFactor:
		case kMsgDefaults:
		case kMsgRevert:
		{
			fWindow->PostMessage(message);
			break;
		}
		case SCROLL_AREA_CHANGED:
		case SCROLL_CONTROL_CHANGED:
		case TAP_CONTROL_CHANGED:
		case PAD_SPEED_CHANGED:
		case PAD_ACCELERATION_CHANGED:
		case DEFAULT_SETTINGS:
		case REVERT_SETTINGS:
		{
			fWindow->PostMessage(message);
			break;
		}
		case kMsgSliderrepeatrate:
		case kMsgSliderdelayrate:
		{
			fWindow->PostMessage(message);
			break;
		}
		default:
			BApplication::MessageReceived(message);
	}
};


/**
 * @brief Process entry point for the Input preferences panel.
 *
 * Instantiates InputApplication and runs its message loop until the user
 * closes the window.
 *
 * @return Always returns 0 once the application loop terminates.
 */
int
main(int /*argc*/, char** /*argv*/)
{
	InputApplication app;
	app.Run();

	return 0;
}
