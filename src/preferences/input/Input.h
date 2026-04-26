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

/** @file Input.h
    @brief Declares InputApplication, the BApplication for the Input preflet. */

#ifndef INPUT_H
#define INPUT_H


#include <Application.h>
#include <Catalog.h>
#include <Locale.h>

#include "InputIcons.h"
#include "InputWindow.h"


/**
 * @brief BApplication subclass driving the Input preferences panel.
 *
 * Owns the singleton InputWindow, holds the shared InputIcons resource
 * bundle used by the device list view, and forwards mouse, touchpad,
 * and keyboard messages to the active settings card.
 */
class InputApplication : public BApplication {
public:
				InputApplication();
	void		MessageReceived(BMessage* message);
private:
	InputIcons	fIcons;
	InputWindow*	fWindow;
};

#endif	/* INPUT_H */
