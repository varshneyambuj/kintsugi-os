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
 * MIT License. Copyright 2001-2006, Haiku.
 * Original authors: Rafael Romo, Stefano Ceccherini (burton666@libero.it),
 *                   Axel Doerfler (axeld@pinc-software.de).
 */

/** @file RefreshWindow.h
    @brief Modal dialog hosting RefreshSlider for picking a custom refresh rate. */

#ifndef REFRESH_WINDOW_H
#define REFRESH_WINDOW_H


#include <Window.h>

class BSlider;


/**
 * @brief Modal floater that lets the user pick an arbitrary refresh rate.
 *
 * Hosts a RefreshSlider plus a Done/Cancel button pair. The chosen rate is
 * posted to be_app via @c SET_CUSTOM_REFRESH_MSG when the user accepts.
 */
class RefreshWindow : public BWindow {
	public:
		RefreshWindow(BPoint position, float current, float min, float max);

		virtual void MessageReceived(BMessage* message);
		virtual void WindowActivated(bool active);

	private:
		BSlider* fRefreshSlider;
};

#endif	// REFRESH_WINDOW_H
