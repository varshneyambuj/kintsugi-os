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
 * MIT License. Copyright 2019, Haiku, Inc.
 * Original author: Preetpal Kaur.
 */

/** @file InputWindow.h
    @brief Declares InputWindow, the top-level Input preference window. */

#ifndef INPUT_WINDOW_H
#define INPUT_WINDOW_H


#include <CardView.h>
#include <Input.h>
#include <ListView.h>
#include <Message.h>
#include <Window.h>

#include "MouseSettings.h"


/**
 * @brief Top-level BWindow for the Input preferences panel.
 *
 * Hosts the device list (BListView) and a BCardView whose visible card
 * matches the selected device. Reacts to B_INPUT_DEVICES_CHANGED so that
 * hot-plug events update the list and the corresponding cards.
 */
class InputWindow : public BWindow
{
public:
							InputWindow(BRect rect);
		void				MessageReceived(BMessage* message);
		void				Show();
		void				Hide();

private:
		status_t			FindDevice();
		void				AddDevice(BInputDevice* device);

private:
		BListView*			fDeviceListView;
		BCardView*			fCardView;

		MultipleMouseSettings 	fMultipleMouseSettings;
};

#endif /* INPUT_WINDOW_H */
