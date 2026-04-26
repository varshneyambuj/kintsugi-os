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
 * MIT License. Copyright 2001-2005, Haiku.
 * Original authors: Rafael Romo, Stefano Ceccherini (burton666@libero.it),
 *                   Andrew Bachmann, Sergei Panteleev.
 */

/** @file ScreenApplication.h
    @brief Top-level BApplication for the Screen preferences app. */

#ifndef SCREEN_APPLICATION_H
#define SCREEN_APPLICATION_H


#include <Application.h>


class ScreenWindow;

/**
 * @brief BApplication subclass that owns the single ScreenWindow instance.
 *
 * Routes refresh-rate, mode-confirmation, and desktop-color update messages
 * from auxiliary windows back to the main ScreenWindow.
 */
class ScreenApplication : public BApplication {
	public:
		ScreenApplication();

		virtual void MessageReceived(BMessage *message);
		virtual void AboutRequested();

	private:
		ScreenWindow *fScreenWindow;
};

#endif	/* SCREEN_APPLICATION_H */
