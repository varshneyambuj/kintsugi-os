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
 * MIT License. Copyright 2015, Haiku, Inc.
 * Original author: Josef Gajdusek.
 */

/** @file EditWindow.h
    @brief Modal mini-window used to edit a single text value (a shortcut command). */

#ifndef EDITWINDOW_H
#define EDITWINDOW_H

#include <Window.h>

class BTextControl;

/**
 * @brief Small modal window that lets the user edit a single string value.
 *
 * EditWindow is constructed with placeholder text, displayed via Go(), and
 * blocks the calling thread on a semaphore until the user accepts the entry.
 * Used by the Shortcuts preference panel to edit cell text in-place.
 */
class EditWindow : public BWindow {
public:
						EditWindow(const char* placeholder, uint32 flags);

		void			MessageReceived(BMessage* message);
		BString			Go();

private:
	sem_id				fSem;
	BTextControl*		fTextControl;
};

#endif	// EDITWINDOW_H
