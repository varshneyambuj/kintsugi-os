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
 *   Copyright 2007 Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 *       Ryan Leavengood, leavengood@gmail.com
 */


/**
 * @file MessageWin.cpp
 * @brief Lightweight, non-editable text window used to display progress
 *        notes during long-running operations (e.g. probing).
 */


#include "MessageWin.h"

#include <Box.h>
#include <Message.h>
#include <TextView.h>
#include <View.h>

/**
 * @brief Constructs a transient message window centred on a parent frame.
 *
 * Builds a non-editable BTextView inside a plain BBox and resizes itself
 * to a third of the parent's height, then centres vertically.
 *
 * @param parentFrame Screen frame of the owning window; used for centering.
 * @param title       Title string shown in the window's title bar.
 * @param look        BWindow look passed to the base class.
 * @param feel        BWindow feel passed to the base class.
 * @param flags       BWindow flags.
 * @param workspace   Workspace mask the window appears in.
 */
MessageWin::MessageWin(BRect parentFrame, const char *title,
	window_look look, window_feel feel, uint32 flags, uint32 workspace)
	: BWindow(parentFrame ,title ,look ,feel, flags, workspace)
{
	fBox = new BBox(Bounds(), "", B_FOLLOW_ALL, B_WILL_DRAW, B_PLAIN_BORDER);
	fBox->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fBox->SetLowColor(fBox->ViewColor());

	// Rects for the text view
	BRect outside(fBox->Bounds());
	outside.InsetBy(10, 10);
	BRect insider(outside);
	insider.OffsetTo(B_ORIGIN);

	fText = new BTextView(outside, "message", insider, B_FOLLOW_NONE, B_WILL_DRAW);
	fText->MakeEditable(false);
	fText->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fText->SetLowColor(fText->ViewColor());

	fBox->AddChild(fText);
 	AddChild(fBox);

 	/* Relocate the window to the center of what its being given */
  	ResizeTo(parentFrame.Width(), floor(parentFrame.Height() / 3));
 	MoveBy(0, floor(parentFrame.Height() / 2 - (parentFrame.Height()/3) / 2 ));

}


/**
 * @brief Replaces the current message text in a thread-safe way.
 *
 * Locks the window, swaps the text, flushes the looper queue, and unlocks.
 *
 * @param str New text to display.
 */
void MessageWin::SetText(const char* str)
{
	Lock();
	fText->SetText(str);
	fText->Flush();
	Unlock();
}


/**
 * @brief Forwards every message to BWindow.
 *
 * @param message Incoming message.
 */
void MessageWin::MessageReceived(BMessage *message)
{
	switch(message->what)
	{
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


/**
 * @brief Standard close handler.
 *
 * @return The default BWindow::QuitRequested() value.
 */
bool MessageWin::QuitRequested()
{
	return BWindow::QuitRequested();
}

