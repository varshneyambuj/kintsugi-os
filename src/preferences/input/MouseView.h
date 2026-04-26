/*
 * Copyright 2026, Kintsugi OS Contributors. All rights reserved.
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
 * MIT License. Copyright 2019, Haiku, Inc.
 * Original author: Preetpal Kaur.
 */

/** @file MouseView.h
    @brief Schematic mouse-button widget for the input preferences pane. */

#ifndef MOUSE_VIEW_H
#define MOUSE_VIEW_H


#include <Bitmap.h>
#include <Picture.h>
#include <PopUpMenu.h>
#include <View.h>


class MouseSettings;

/**
 * @brief BView that draws a stylised mouse with one to six labelled buttons.
 *
 * Reflects the live state of an associated MouseSettings model (button
 * count and per-button mapping) and lets the user click each button to
 * pop up a menu for reassigning its logical role. Pressed-button
 * highlights animate in real time when the user clicks.
 */
class MouseView : public BView {
public:
								MouseView(const MouseSettings& settings);
		virtual					~MouseView();

				void			SetMouseType(int32 type);
				void			MouseMapUpdated();
				void			UpdateFromSettings();

		virtual	void			GetPreferredSize(float* _width, float* _height);
		virtual	void			AttachedToWindow();
		virtual	void			MouseUp(BPoint where);
		virtual	void			MouseDown(BPoint where);
		virtual	void			Draw(BRect frame);
		/** @brief Returns whether the modelled mouse is currently connected. */
		bool					IsMouseConnected()
								{ return fConnected; }

private:
				BRect			_ButtonsRect() const;
				BRect			_ButtonRect(const int32* offsets,
									int index) const;
				int32			_ConvertFromVisualOrder(int32 button);
				void			_CreateButtonsPicture();

private:
	typedef BView inherited;

		const	MouseSettings&	fSettings;

				BPicture		fButtonsPicture;
				int32			fDigitBaseline;
				int32			fDigitHeight;
				float			fScaling;

				int32			fType;
				uint32			fButtons;
				uint32			fOldButtons;
				bool			fConnected;
};


#endif	/* MOUSE_VIEW_H */