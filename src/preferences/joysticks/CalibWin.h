/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2007, Haiku.
 * Original authors: Oliver Ruiz Dorantes, Ryan Leavengood.
 */

/** @file CalibWin.h
    @brief Placeholder calibration window class for joystick preferences. */

#ifndef _CALIB_WIN_H
#define _CALIB_WIN_H


#include <Window.h>

class BView;
class BCheckBox;
class BStringView;
class BButton;
class BBox;


/*
	All this code is here is just to not have an empty view at
	Clicking the Calibrate function.

	All controls in this view needs to be created and placed dynamically according
	with the Joystick descriptors
*/


/**
 * @brief Placeholder calibration window.
 *
 * Owns a static set of dummy buttons and labels so the calibration entry
 * point has something to display. The intent is for the controls to be
 * rebuilt dynamically from the joystick descriptor.
 */
class CalibWin : public BWindow
{
	public:
		CalibWin(BRect frame, const char *title,
			window_look look,
			window_feel feel,
			uint32 flags,
			uint32 workspace = B_CURRENT_WORKSPACE);
							
		virtual	void	MessageReceived(BMessage *message);
		virtual	bool	QuitRequested();

	protected:
		BStringView*	fStringView3;
		BStringView*	fStringView4;
		BStringView*	fStringView5;
		BStringView*	fStringView6;
		BStringView*	fStringView7;
		BStringView*	fStringView8;
		BStringView*	fStringView9;

		BButton*		fButton3;
		BButton*		fButton4;

		BButton*		fButton5;
		BButton*		fButton6;
		BButton*		fButton7;
		BButton*		fButton8;
		BButton*		fButton9;
		BButton*		fButton10;
		BButton*		fButton11;
		BButton*		fButton12;

		BBox*			fBox;
		BView*			fView;
};


#endif	/* _CALIB_WIN_H */

