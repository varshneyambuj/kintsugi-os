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

/** @file MessageWin.h
    @brief Transient window that displays progress text during probing. */

#ifndef _MESSAGE_WIN_H
#define _MESSAGE_WIN_H


#include <Window.h>

class BBox;
class BButton;
class BCheckBox;
class BStringView;
class BView;
class BTextView;


/**
 * @brief Modal information window with a single read-only text view.
 *
 * Used to show transient progress messages while the joystick code is
 * walking through descriptors during a probe.
 */
class MessageWin : public BWindow
{
	public:
		MessageWin(BRect parent_frame, const char *title,
			window_look look,
			window_feel feel,
			uint32 flags,
			uint32 workspace = B_CURRENT_WORKSPACE);

		void			SetText(const char* str);
		virtual	void	MessageReceived(BMessage *message);
		virtual	bool	QuitRequested();

	protected:
		BBox*			fBox;
		BTextView*	 	fText;
};

#endif	/* _MESSAGE_WIN_H */

