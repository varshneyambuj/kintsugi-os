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
 * MIT License. Copyright 2004-2014, Haiku, Inc.
 * Original authors: Jerome Duval, John Scipione (jscipione@gmail.com),
 *                   Sandor Vroemisse.
 */

/** @file KeymapApplication.h
    @brief Top-level BApplication for the Keymap preferences app. */

#ifndef KEYMAP_APPLICATION_H
#define KEYMAP_APPLICATION_H


#include "KeymapWindow.h"

#include <Application.h>
#include <Catalog.h>
#include <Entry.h>
#include <Locale.h>

#include "ModifierKeysWindow.h"


/** @brief Request the modifier-keys editor to be shown. */
static const uint32 kMsgShowModifierKeysWindow = 'smkw';
/** @brief Notification that the modifier-keys editor has closed. */
static const uint32 kMsgCloseModifierKeysWindow = 'hmkw';
/** @brief A modifier role mapping was updated; refresh the keymap. */
static const uint32 kMsgUpdateModifierKeys = 'umod';
/** @brief A normal (non-modifier) key was reassigned. */
static const uint32 kMsgUpdateNormalKeys = 'ukey';


/**
 * @brief BApplication subclass that owns the Keymap and ModifierKeys windows.
 *
 * Routes inter-window messages between the main KeymapWindow and the
 * modal ModifierKeysWindow, and ensures only one ModifierKeysWindow is
 * ever open at a time.
 */
class KeymapApplication : public BApplication {
public:
		KeymapApplication();

		void					MessageReceived(BMessage* message);
		bool					UseKeymap(BEntry* keymap);

protected:
		void					_ShowModifierKeysWindow();

private:
		KeymapWindow*			fWindow;
		ModifierKeysWindow*		fModifierKeysWindow;
};


#endif	// KEYMAP_APPLICATION_H
