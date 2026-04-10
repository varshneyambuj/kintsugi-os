/*
 * Copyright 2026 Kintsugi OS Project. All rights reserved.
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
 * Authors:
 *     Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2004-2005, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Jérôme Duval
 */

/** @file BottomlineWindow.h
 *  @brief Floating preedit window used by input methods that have no native UI. */

#ifndef BOTTOMLINE_WINDOW_H
#define BOTTOMLINE_WINDOW_H


#include "InputServer.h"

#include <Message.h>
#include <Window.h>

class BTextView;


/** @brief Floating window that shows the input method's preedit/composition text.
 *
 * Acts as the default UI for input methods that do not provide their own
 * editor: it displays a single line of text, forwards keystrokes back to
 * the method, and converts B_INPUT_METHOD_EVENT messages into the standard
 * input event stream produced by the input server. */
class BottomlineWindow : public BWindow {
	public:
		BottomlineWindow();
		virtual ~BottomlineWindow();

		/** @brief Handles preedit and method-related messages. */
		virtual void MessageReceived(BMessage* message);
		/** @brief Returns true to allow the window to close. */
		virtual bool QuitRequested();

		/** @brief Translates an input-method event into normal events queued for delivery. */
		void HandleInputMethodEvent(BMessage* event, EventList& newEvents);

	private:
		BTextView *fTextView;  /**< Embedded text view rendering the preedit string. */
};

#endif	// BOTTOMLINE_WINDOW_H
