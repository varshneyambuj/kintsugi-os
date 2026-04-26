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
 * MIT License. Copyright 2010-2017, Haiku, Inc.
 */

/** @file GeneralView.h
    @brief General settings tab of the Notifications preflet (server
           on/off, window width, display duration, position). */

#ifndef _GENERAL_VIEW_H
#define _GENERAL_VIEW_H


#include <Button.h>
#include <CheckBox.h>
#include <Menu.h>
#include <MenuField.h>
#include <Mime.h>
#include <PopUpMenu.h>
#include <RadioButton.h>
#include <Slider.h>
#include <StringView.h>
#include <TextControl.h>

#include "SettingsPane.h"


/**
 * @brief Settings pane that controls global notification behaviour.
 *
 * Hosts the master Enable/Disable checkbox, sliders for window width and
 * timeout, and a position pop-up. Snapshots the loaded values so Revert()
 * can restore them and Defaults() can compare against the factory values.
 */
class GeneralView : public SettingsPane {
public:
							GeneralView(SettingsHost* host);

	virtual	void			AttachedToWindow();
	virtual	void			MessageReceived(BMessage* msg);

			// SettingsPane hooks
			status_t		Load(BMessage&);
			status_t		Save(BMessage&);
			status_t		Revert();
			bool			RevertPossible();
			status_t		Defaults();
			bool			DefaultsPossible();
			bool			UseDefaultRevertButtons();

private:
		BCheckBox*			fNotificationBox;
		BSlider*			fDurationSlider;
		BSlider*			fWidthSlider;
		BPopUpMenu*			fPositionMenu;


		int32				fOriginalTimeout;
		float				fOriginalWidth;
		icon_size			fOriginalIconSize;
		uint32				fOriginalPosition;
		uint32				fNewPosition;

		void				_EnableControls();
		void				_SetTimeoutLabel(int32 value);
		bool				_IsServerRunning();
};

#endif // _GENERAL_VIEW_H
