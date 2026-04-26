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
 *   Copyright 2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Josef Gajdusek
 */


/**
 * @file EditWindow.cpp
 * @brief Implementation of the modal text-entry window used during cell edits.
 *
 * EditWindow presents a single BTextControl plus an OK button, blocks the
 * calling thread on a semaphore in Go() until the user confirms, and returns
 * the typed string.
 */


#include "EditWindow.h"

#include <math.h>

#include <Button.h>
#include <LayoutBuilder.h>
#include <TextControl.h>
#include <String.h>
#include <StringView.h>

#include "ShortcutsWindow.h"


/**
 * @brief Constructs a modal edit window seeded with placeholder text.
 *
 * Lays out a BTextControl above an OK button that posts B_CONTROL_MODIFIED
 * to release the semaphore acquired by Go().
 *
 * @param placeholder Initial text shown in the edit field.
 * @param flags       BWindow look-and-feel flags forwarded to BWindow.
 */
EditWindow::EditWindow(const char* placeholder, uint32 flags)
	:
	BWindow(BRect(0, 0, 0, 0), "", B_MODAL_WINDOW, flags)
{
	fTextControl = new BTextControl("", placeholder, NULL);

	BButton* okButton = new BButton("OK", new BMessage(B_CONTROL_MODIFIED));
	okButton->SetExplicitAlignment(BAlignment(B_ALIGN_RIGHT, B_ALIGN_TOP));
	SetDefaultButton(okButton);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fTextControl)
		.Add(okButton);
}


/**
 * @brief Handles messages posted to the window.
 *
 * Releases the blocking semaphore on B_CONTROL_MODIFIED (OK pressed) so that
 * the thread waiting in Go() can return; everything else is forwarded to
 * BWindow.
 *
 * @param message The incoming BMessage.
 */
void
EditWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_CONTROL_MODIFIED:
			delete_sem(fSem);
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


/**
 * @brief Shows the window modally and blocks until the user confirms.
 *
 * Allocates a private semaphore, sizes the window around the placeholder
 * text, displays it centered, then sleeps on the semaphore until OK is
 * pressed. After acquiring, returns the current text and asks the window to
 * quit.
 *
 * @return The string the user entered, or an empty string if the semaphore
 *         could not be created.
 */
BString
EditWindow::Go()
{
	fSem = create_sem(0, "EditSem");
	if (fSem < B_OK) {
		Quit();
		return "";
	}

	BSize psize = GetLayout()->PreferredSize();
	ResizeTo(max_c(be_plain_font->StringWidth(fTextControl->Text()) * 1.5,
				psize.Width()),
		psize.Height());
	Show();
	CenterOnScreen();

	acquire_sem(fSem);
	BString result = fTextControl->Text();
	if (Lock())
		Quit();

	return result;
}
