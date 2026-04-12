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
 *   Open Tracker License
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *   Distributed under the terms of the OpenTracker License.
 */


/**
 * @file OverrideAlert.cpp
 * @brief An alert dialog whose buttons become enabled when specific modifier keys are held.
 *
 * OverrideAlert extends BAlert to allow individual buttons to require modifier
 * keys (e.g., Shift, Option) before they become active.  It positions itself
 * over the calling thread's window to work well with focus-follows-mouse.
 *
 * @see BAlert
 */


#include <Button.h>
#include <Screen.h>

#include "OverrideAlert.h"


/**
 * @brief Construct a three-button override alert without custom button spacing.
 *
 * @param title      Window title.
 * @param text       Alert message text.
 * @param button1    Label for the first button.
 * @param modifiers1 Modifier-key mask required to enable button 1.
 * @param button2    Label for the second button (may be NULL).
 * @param modifiers2 Modifier-key mask required to enable button 2.
 * @param button3    Label for the third button (may be NULL).
 * @param modifiers3 Modifier-key mask required to enable button 3.
 * @param width      Button-width constant passed to BAlert.
 * @param type       Alert type icon constant.
 */
OverrideAlert::OverrideAlert(const char* title, const char* text,
	const char* button1, uint32 modifiers1,
	const char* button2, uint32 modifiers2,
	const char* button3, uint32 modifiers3,
	button_width width, alert_type type)
	:
	BAlert(title, text, button1, button2, button3, width, type),
	fCurModifiers(0)
{
	fButtonModifiers[0] = modifiers1;
	fButtonModifiers[1] = modifiers2;
	fButtonModifiers[2] = modifiers3;
	UpdateButtons(modifiers(), true);

	BPoint where = OverPosition(Frame().Width(), Frame().Height());
	MoveTo(where.x, where.y);
}


/**
 * @brief Construct a three-button override alert with explicit button spacing.
 *
 * @param title      Window title.
 * @param text       Alert message text.
 * @param button1    Label for the first button.
 * @param modifiers1 Modifier-key mask required to enable button 1.
 * @param button2    Label for the second button (may be NULL).
 * @param modifiers2 Modifier-key mask required to enable button 2.
 * @param button3    Label for the third button (may be NULL).
 * @param modifiers3 Modifier-key mask required to enable button 3.
 * @param width      Button-width constant passed to BAlert.
 * @param spacing    Button-spacing constant passed to BAlert.
 * @param type       Alert type icon constant.
 */
OverrideAlert::OverrideAlert(const char* title, const char* text,
	const char* button1, uint32 modifiers1,
	const char* button2, uint32 modifiers2,
	const char* button3, uint32 modifiers3,
	button_width width, button_spacing spacing, alert_type type)
	:
	BAlert(title, text, button1, button2, button3, width, spacing, type),
	fCurModifiers(0)
{
	fButtonModifiers[0] = modifiers1;
	fButtonModifiers[1] = modifiers2;
	fButtonModifiers[2] = modifiers3;
	UpdateButtons(modifiers(), true);

	BPoint where = OverPosition(Frame().Width(), Frame().Height());
	MoveTo(where.x, where.y);
}


/**
 * @brief Destroy the OverrideAlert.
 */
OverrideAlert::~OverrideAlert()
{
}


/**
 * @brief Intercept key events to update button enabled states when modifiers change.
 *
 * @param message  The message to dispatch.
 * @param handler  The target handler.
 */
void
OverrideAlert::DispatchMessage(BMessage* message, BHandler* handler)
{
	if (message->what == B_KEY_DOWN || message->what == B_KEY_UP
		|| message->what == B_UNMAPPED_KEY_DOWN
		|| message->what == B_UNMAPPED_KEY_UP) {
		uint32 modifiers;
		if (message->FindInt32("modifiers", (int32*)&modifiers) == B_OK)
			UpdateButtons(modifiers);
	}
	BAlert::DispatchMessage(message, handler);
}


/**
 * @brief Calculate the position to place the alert, preferring over the calling window.
 *
 * If the calling thread has an associated BWindow the alert is placed roughly
 * over its upper-centre area, otherwise the main screen centre is used.
 *
 * @param width   Width of the alert window.
 * @param height  Height of the alert window.
 * @return The BPoint to pass to MoveTo().
 */
BPoint
OverrideAlert::OverPosition(float width, float height)
{
	// This positions the alert window like a normal alert, put
	// places it on top of the calling window if possible.

	BWindow* window
		= dynamic_cast<BWindow*>(BLooper::LooperForThread(find_thread(NULL)));
	BRect desirableRect;

	if (window != NULL) {
		// If we found a window associated with this calling thread,
		// place alert over that window so that the first button is
		// on top of it.  This allows name editing confirmations to
		// work with focus follows mouse -- when the alert goes away,
		// the underlying window will still have focus.

		desirableRect = window->Frame();
		float midX = (desirableRect.left + desirableRect.right) / 2.0f;
		float midY = (desirableRect.top * 3.0f + desirableRect.bottom) / 4.0f;

		desirableRect.left = midX - ceilf(width / 2.0f);
		desirableRect.right = desirableRect.left+width;
		desirableRect.top = midY - ceilf(height / 3.0f);
		desirableRect.bottom = desirableRect.top + height;
	} else {
		// Otherwise, place alert in center of (main) screen.

		desirableRect = BScreen().Frame();
		float midX = (desirableRect.left + desirableRect.right) / 2.0f;
		float midY = (desirableRect.top * 3.0f + desirableRect.bottom) / 4.0f;

		desirableRect.left = midX - ceilf(width / 2.0f);
		desirableRect.right = desirableRect.left + width;
		desirableRect.top = midY - ceilf(height / 3.0f);
		desirableRect.bottom = desirableRect.top + height;
	}

	return desirableRect.LeftTop();
}


/**
 * @brief Refresh button enabled states to match the current modifier keys.
 *
 * Skips the update if @p modifiers equals the cached value and @p force is
 * false.  Enables a button when its modifier mask is a subset of @p modifiers.
 *
 * @param modifiers  The current modifier-key bitmask from BMessage or modifiers().
 * @param force      If true, always update even when modifiers are unchanged.
 */
void
OverrideAlert::UpdateButtons(uint32 modifiers, bool force)
{
	if (modifiers == fCurModifiers && !force)
		return;

	fCurModifiers = modifiers;
	for (int32 i = 0; i < 3; i++) {
		BButton* button = ButtonAt(i);
		if (button != NULL) {
			button->SetEnabled(((fButtonModifiers[i] & fCurModifiers)
				== fButtonModifiers[i]));
		}
	}
}
