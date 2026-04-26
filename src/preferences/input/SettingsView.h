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

/** @file SettingsView.h
    @brief Shared mouse-preferences layout combining sliders, popups, and a MouseView. */

#ifndef SETTINGS_VIEW_H
#define SETTINGS_VIEW_H


#include <Box.h>
#include <CheckBox.h>
#include <OptionPopUp.h>
#include <Slider.h>


class MouseSettings;
class MouseView;


/**
 * @brief Composite control panel that exposes mouse settings to the user.
 *
 * Owns the layout of the mouse-type popup, the click and movement
 * speed sliders, the embedded MouseView, the focus-mode popup, and
 * the accept-first-click checkbox. Reads from and forwards user
 * actions to the supplied MouseSettings model.
 */
class SettingsView : public BBox {
	public:
								SettingsView(MouseSettings &settings);
		virtual 				~SettingsView();

		virtual void 			AttachedToWindow();

				void 			SetMouseType(int32 type);
				void 			MouseMapUpdated();
				void 			UpdateFromSettings();

	public:
				// FIXME use proper getters/setters for this?
				BCheckBox*		fAcceptFirstClickBox;

	private:
		typedef	BBox			inherited;

		const	MouseSettings&	fSettings;

				BOptionPopUp*	fTypeMenu;
				BOptionPopUp*	fFocusMenu;
				MouseView*		fMouseView;
				BSlider*		fClickSpeedSlider;
				BSlider*		fMouseSpeedSlider;
				BSlider*		fAccelerationSlider;
};

#endif	/* SETTINGS_VIEW_H */
