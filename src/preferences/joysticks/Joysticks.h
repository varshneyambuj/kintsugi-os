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

/** @file Joysticks.h
    @brief BApplication subclass for the Joysticks preference panel. */

#ifndef _JOYSTICKS_H
#define _JOYSTICKS_H


#include <Application.h>

class JoyWin;
class BWindow;


/**
 * @brief Top-level BApplication for the Joysticks preference panel.
 *
 * Owns the main JoyWin window and handles application lifecycle messages.
 */
class Joysticks : public BApplication
{
	public:
		Joysticks(const char *signature);
		~Joysticks();

		virtual void	ReadyToRun();
		virtual bool	QuitRequested();

	protected:
		JoyWin*			fJoywin;

};


#endif	/* _JOYSTICKS_H */
