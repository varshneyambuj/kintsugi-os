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
 * @file CalibWin.cpp
 * @brief Placeholder calibration window for joystick preferences.
 *
 * This window currently only assembles a static set of controls so that the
 * "Calibrate" button has something to display. The control set is intended
 * to be rebuilt dynamically from the joystick descriptor; until that work
 * lands the layout below is a visual stub.
 */


#include "CalibWin.h"

#include <View.h>
#include <CheckBox.h>
#include <StringView.h>
#include <ListView.h>
#include <Button.h>
#include <Box.h>

/*
	All this code is here is just to not have an empty view at
	Clicking the Calibrate function.

	All controls in this view needs to be created and placed dynamically according
	with the Joystick descriptors
*/

/**
 * @brief Constructs the calibration window and lays out a placeholder
 *        control panel.
 *
 * @param frame     Initial screen frame for the window.
 * @param title     Window title shown in the title bar.
 * @param look      BWindow look passed to the base class.
 * @param feel      BWindow feel passed to the base class.
 * @param flags     BWindow flags (e.g. resizable, zoomable).
 * @param workspace Workspace mask the window should appear in.
 *
 * @note The buttons and string views constructed here are stand-ins; the
 *       final widget set must be generated from the connected joystick
 *       descriptor.
 */
CalibWin::CalibWin(BRect frame,const char *title, window_look look,
	window_feel feel, uint32 flags, uint32 workspace)
	: BWindow(frame,title,look,feel,flags,workspace)
{
	// Allocate object
	fButton12 = new BButton(BRect(213.00,86.00,268.00,105.00),"Button12","Button",NULL);
	fButton11 = new BButton(BRect(148.00,88.00,206.00,110.00),"Button11","Button",NULL);
	fButton10 = new BButton(BRect(205.00,168.00,260.00,190.00),"Button10","Button",NULL);
	fButton9  = new BButton(BRect(213.00,137.00,263.00,163.00),"Button9","Button", NULL);
	fButton8  = new BButton(BRect(144.00,173.00,189.00,194.00),"Button8","Button",NULL);
	fButton7  = new BButton(BRect(145.00,145.00,193.00,168.00),"Button7","Button",NULL);
	fButton6  = new BButton(BRect(217.00,109.00,261.00,131.00),"Button6","Button",NULL);
	fButton5  = new BButton(BRect(147.00,116.00,194.00,139.00),"Button5","Button",NULL);
	fButton4 = new BButton(BRect(189.00,263.00,271.00,288.00),"Button4","Button",NULL);
	fButton3 = new BButton(BRect(17.00,254.00,100.00,284.00),"Button3","Button",NULL);

	fStringView9 = new BStringView(BRect(8.00,175.00,116.00,190.00),"StringView9","Text");
	fStringView8 = new BStringView(BRect(10.00,154.00,112.00,172.00),"StringView8","Text");
	fStringView7 = new BStringView(BRect(9.00,132.00,116.00,148.00),"StringView7","Text");
	fStringView6 = new BStringView(BRect(11.00,114.00,120.00,128.00),"StringView6","Text");
	fStringView5 = new BStringView(BRect(11.00,93.00,121.00,108.00),"StringView5","Text");
	fStringView4 = new BStringView(BRect(12.00,73.00,121.00,88.00),"StringView4","Text");
	fStringView3 = new BStringView(BRect(26.00,17.00,258.00,45.00),"StringView3","Text3");

	fBox = new BBox(BRect(12.00,7.00,280.00,67.00),"Box1",
		B_FOLLOW_LEFT | B_FOLLOW_TOP,B_WILL_DRAW | B_NAVIGABLE, B_FANCY_BORDER);

	fView = new BView(Bounds(),"View3", B_FOLLOW_NONE,B_WILL_DRAW);

	fView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	// Adding object
  	fBox->AddChild(fStringView3);

	fView->AddChild(fButton3);
	fView->AddChild(fButton4);
	fView->AddChild(fBox);

	fView->AddChild(fStringView4);
	fView->AddChild(fStringView5);
	fView->AddChild(fStringView6);
	fView->AddChild(fStringView7);
	fView->AddChild(fStringView8);
	fView->AddChild(fStringView9);

	fView->AddChild(fButton5);
	fView->AddChild(fButton6);
	fView->AddChild(fButton7);
	fView->AddChild(fButton8);
	fView->AddChild(fButton9);
	fView->AddChild(fButton10);
	fView->AddChild(fButton11);
	fView->AddChild(fButton12);

	AddChild(fView);
}


/**
 * @brief BWindow message hook.
 *
 * Currently forwards every message straight to BWindow::MessageReceived(),
 * since the placeholder layout has no interactive behaviour yet.
 *
 * @param message Incoming BMessage to dispatch.
 */
void CalibWin::MessageReceived(BMessage *message)
{
	switch(message->what)
	{
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


/**
 * @brief Handles a request to close the calibration window.
 *
 * @return Whatever BWindow::QuitRequested() returns, normally true.
 */
bool CalibWin::QuitRequested()
{
	return BWindow::QuitRequested();
}

