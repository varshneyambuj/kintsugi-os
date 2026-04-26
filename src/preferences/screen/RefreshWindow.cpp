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
 *   Copyright 2001-2006, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Rafael Romo
 *       Stefano Ceccherini (burton666@libero.it)
 *       Axel Doerfler, axeld@pinc-software.de
 */


/**
 * @file RefreshWindow.cpp
 * @brief Modal dialog wrapping a RefreshSlider for custom refresh rates.
 */


#include "RefreshWindow.h"

#include "Constants.h"
#include "RefreshSlider.h"

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <String.h>
#include <StringView.h>
#include <Window.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Screen"


/**
 * @brief Build a refresh-rate picker dialog at @a position.
 *
 * Hosts a slider initialized to @a current Hz with the configured Hz
 * range, plus Done and Cancel buttons. Done posts
 * @c SET_CUSTOM_REFRESH_MSG to be_app and quits.
 *
 * @param position Initial top-left coordinate of the dialog.
 * @param current  Refresh rate (Hz) shown initially on the slider.
 * @param min      Lower bound (Hz) accepted by the slider.
 * @param max      Upper bound (Hz) accepted by the slider.
 */
RefreshWindow::RefreshWindow(BPoint position, float current, float min, float max)
	: BWindow(BRect(0, 0, 300, 200), B_TRANSLATE("Refresh rate"), B_MODAL_WINDOW,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS, B_ALL_WORKSPACES)
{
	min = ceilf(min);
	max = floorf(max);

	BView* topView = new BView(Bounds(), NULL, B_FOLLOW_ALL, B_WILL_DRAW);
	topView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	topView->SetHighUIColor(B_PANEL_TEXT_COLOR);
	AddChild(topView);

	BRect rect = Bounds().InsetByCopy(8, 8);
	BStringView* stringView = new BStringView(rect, "info",
		B_TRANSLATE("Type or use the left and right arrow keys."));
	stringView->ResizeToPreferred();
	topView->AddChild(stringView);

	rect.top += stringView->Bounds().Height() + 14;
	fRefreshSlider = new RefreshSlider(rect, min, max, B_FOLLOW_TOP | B_FOLLOW_LEFT_RIGHT);
	fRefreshSlider->SetValue((int32)rintf(current * 10));
	fRefreshSlider->SetModificationMessage(new BMessage(SLIDER_MODIFICATION_MSG));
	float width, height;
	fRefreshSlider->GetPreferredSize(&width, &height);
	fRefreshSlider->ResizeTo(rect.Width(), height);
	topView->AddChild(fRefreshSlider);

	BButton* doneButton = new BButton(rect, "DoneButton", B_TRANSLATE("Done"), 
		new BMessage(BUTTON_DONE_MSG), B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM);
	doneButton->ResizeToPreferred();
	doneButton->MoveTo(Bounds().Width() - doneButton->Bounds().Width() - 8,
		Bounds().Height() - doneButton->Bounds().Height() - 8);
	topView->AddChild(doneButton);

	BButton* button = new BButton(doneButton->Frame(), "CancelButton",
		B_TRANSLATE("Cancel"), new BMessage(B_QUIT_REQUESTED),
		B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM);
	button->ResizeToPreferred();
	button->MoveBy(-button->Bounds().Width() - 10, 0);
	topView->AddChild(button);

	doneButton->MakeDefault(true);

	width = stringView->Bounds().Width() + 100;
	if (width < Bounds().Width())
		width = Bounds().Width();
	height = fRefreshSlider->Frame().bottom + button->Bounds().Height() + 20.0f;

	ResizeTo(width, height);
	MoveTo(position.x - width / 2.5f, position.y - height / 1.9f);
}


/**
 * @brief Move keyboard focus to the slider whenever the window activates.
 *
 * @param active True when the window is being activated.
 */
void
RefreshWindow::WindowActivated(bool active)
{
	fRefreshSlider->MakeFocus(active);
}


/**
 * @brief Handle Done/Cancel buttons and slider invocation messages.
 *
 * On @c BUTTON_DONE_MSG posts @c SET_CUSTOM_REFRESH_MSG to be_app with the
 * chosen refresh rate, then quits.
 *
 * @param message Incoming message.
 */
void
RefreshWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case BUTTON_DONE_MSG:
		{
			float value = (float)fRefreshSlider->Value() / 10;

			BMessage message(SET_CUSTOM_REFRESH_MSG);
			message.AddFloat("refresh", value);
			be_app->PostMessage(&message);

			PostMessage(B_QUIT_REQUESTED);
			break;
		}

		case SLIDER_INVOKE_MSG:
			fRefreshSlider->MakeFocus(true);
			break;

		default:
			BWindow::MessageReceived(message);		
			break;
	}
}
