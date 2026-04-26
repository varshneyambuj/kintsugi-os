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
 *   Copyright 2001-2015, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Rafael Romo
 *       Stefano Ceccherini (burton666@libero.it)
 *       Axel Doerfler, axeld@pinc-software.de
 *       Augustin Cavalier <waddlesplash>
 */


/**
 * @file AlertWindow.cpp
 * @brief Confirmation dialog with countdown for screen-mode changes.
 *
 * After the Screen preferences app applies a new display mode, this alert is
 * shown with a "Keep" / "Undo" choice and a pulsing countdown. If the user
 * does not respond before the countdown elapses, the mode is reverted
 * automatically so that an unreadable display does not lock them out.
 */


#include "AlertWindow.h"
#include "Constants.h"

#include <Button.h>
#include <Catalog.h>
#include <String.h>
#include <TextView.h>
#include <Window.h>
#include <TimeUnitFormat.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Screen"


/**
 * @brief Construct the confirmation alert with a 12 second countdown.
 *
 * @param handler Messenger that will receive @c MAKE_INITIAL_MSG when the
 *                user confirms the change, or @c BUTTON_UNDO_MSG when the
 *                user reverts (or the countdown expires).
 */
AlertWindow::AlertWindow(BMessenger handler)
	: BAlert(B_TRANSLATE("Confirm changes"),
			 "", B_TRANSLATE("Undo"), B_TRANSLATE("Keep")),
	// we will wait 12 seconds until we send a message
	fSeconds(12),
	fHandler(handler)
{
	SetType(B_WARNING_ALERT);
	SetPulseRate(1000000);
	TextView()->SetStylable(true);
	TextView()->GetFontAndColor(0, &fOriginalFont);
	fFont = fOriginalFont;
	fFont.SetFace(B_BOLD_FACE);
	UpdateCountdownView();
}


/**
 * @brief Intercept @c B_PULSE messages to drive the countdown.
 *
 * When the countdown reaches zero, sends @c BUTTON_UNDO_MSG to the
 * configured handler and asks the alert to quit. Other messages are
 * forwarded to the base implementation.
 *
 * @param message Incoming message to dispatch.
 * @param handler Target handler from the looper.
 */
void
AlertWindow::DispatchMessage(BMessage* message, BHandler* handler)
{
	if (message->what == B_PULSE) {
		if (--fSeconds == 0) {
			fHandler.SendMessage(BUTTON_UNDO_MSG);
			PostMessage(B_QUIT_REQUESTED);
			Hide();
		} else
			UpdateCountdownView();
	}

	BAlert::DispatchMessage(message, handler);
}


/**
 * @brief Handle button presses and the Escape shortcut.
 *
 * Pressing "Keep" sends @c MAKE_INITIAL_MSG, pressing "Undo" or Escape
 * sends @c BUTTON_UNDO_MSG, and either closes the dialog.
 *
 * @param message Incoming alert button (@c 'ALTB') or key-down message.
 */
void
AlertWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case 'ALTB': // alert button message
		{
			int32 which;
			if (message->FindInt32("which", &which) == B_OK) {
				if (which == 1)
					fHandler.SendMessage(MAKE_INITIAL_MSG);
				else if (which == 0)
					fHandler.SendMessage(BUTTON_UNDO_MSG);
				PostMessage(B_QUIT_REQUESTED);
				Hide();
			}
			break;
		}

		case B_KEY_DOWN:
		{
			int8 val;
			if (message->FindInt8("byte", &val) == B_OK && val == B_ESCAPE) {
				fHandler.SendMessage(BUTTON_UNDO_MSG);
				PostMessage(B_QUIT_REQUESTED);
				Hide();
				break;
			}
			// fall through
		}

		default:
			BAlert::MessageReceived(message);
			break;
	}
}


/**
 * @brief Refresh the alert text to show the remaining countdown seconds.
 *
 * Builds a localized string of the form
 * "Do you wish to keep these settings? Settings will revert in N seconds."
 * and applies bold styling to the question while keeping the countdown
 * line in the original font.
 *
 * @note The styling uses BTextView's range-based font API; modifying
 *       the order of text replacement and font calls will misalign the
 *       styled regions.
 */
void
AlertWindow::UpdateCountdownView()
{
	BString str1 = B_TRANSLATE("Do you wish to keep these settings?");
	BString string = str1;
	string += "\n";
	string += B_TRANSLATE("Settings will revert in %seconds.");

	BTimeUnitFormat format;
	BString tmp;
	format.Format(tmp, fSeconds, B_TIME_UNIT_SECOND);

	string.ReplaceFirst("%seconds", tmp);
	// The below is black magic, do not touch. We really need to refactor
	// BTextView sometime...
	TextView()->SetFontAndColor(0, str1.Length() + 1, &fOriginalFont,
		B_FONT_ALL);
	TextView()->SetText(string.String());
	TextView()->SetFontAndColor(0, str1.Length(), &fFont, B_FONT_ALL);
}
