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
 * MIT License. Copyright 2001-2015, Haiku.
 * Original authors: Rafael Romo, Stefano Ceccherini (burton666@libero.it),
 *                   Axel Doerfler (axeld@pinc-software.de),
 *                   Augustin Cavalier <waddlesplash>.
 */

/** @file AlertWindow.h
    @brief Confirmation alert with a countdown that auto-reverts a screen mode change. */

#ifndef ALERT_WINDOW_H
#define ALERT_WINDOW_H


#include <Alert.h>
#include <Font.h>
#include <Messenger.h>
#include <String.h>

class BWindow;


/**
 * @brief Modal alert displayed after applying a new screen mode.
 *
 * Asks the user to confirm or revert the change. A pulsing countdown
 * automatically reverts after a fixed number of seconds so that an
 * unreadable mode does not strand the user.
 */
class AlertWindow : public BAlert {
	public:
		AlertWindow(BMessenger handler);

		virtual void MessageReceived(BMessage* message);
		virtual void DispatchMessage(BMessage* message, BHandler* handler);

	private:
		void UpdateCountdownView();

		int32			fSeconds;
		BMessenger		fHandler;
		BFont			fOriginalFont;
		BFont			fFont;
};

#endif	/* ALERT_WINDOW_H */
