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
 *   Copyright 2017 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Brian Hill
 */


/**
 * @file AddRepoWindow.cpp
 * @brief Modal dialog asking the user for a new repository URL.
 *
 * Pre-fills the text field from the system clipboard if it contains a
 * valid URL, validates the entered URL via BUrl, and on success posts an
 * ADD_REPO_URL message to the parent window.
 */


#include "AddRepoWindow.h"

#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <Clipboard.h>
#include <LayoutBuilder.h>
#include <Url.h>

#include "constants.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AddRepoWindow"

/** @brief Last user-chosen window width, remembered across invocations. */
static float sAddWindowWidth = 500.0;


/**
 * @brief Constructs the modal Add Repo dialog and shows it.
 *
 * Builds the URL text control and Add/Cancel buttons, attempts to
 * pre-populate the text from the clipboard, and centres the dialog
 * inside @a size.
 *
 * @param size      Frame of the parent window; used to centre the dialog.
 * @param messenger Reply target for ADD_REPO_URL and ADD_WINDOW_CLOSED.
 */
AddRepoWindow::AddRepoWindow(BRect size, const BMessenger& messenger)
	:
	BWindow(BRect(0, 0, sAddWindowWidth, 10), "AddWindow", B_MODAL_WINDOW,
		B_ASYNCHRONOUS_CONTROLS	| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fReplyMessenger(messenger)
{
	fText = new BTextControl("text", B_TRANSLATE_COMMENT("Repository URL:",
		"Text box label"), "", new BMessage(ADD_BUTTON_PRESSED));
	fAddButton = new BButton(B_TRANSLATE_COMMENT("Add", "Button label"),
		new BMessage(ADD_BUTTON_PRESSED));
	fAddButton->MakeDefault(true);
	fCancelButton = new BButton(kCancelLabel,
		new BMessage(CANCEL_BUTTON_PRESSED));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fText)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGlue()
			.Add(fCancelButton)
			.Add(fAddButton)
		.End()
	.End();
	_GetClipboardData();
	fText->MakeFocus();

	// Move to the center of the preflet window
	CenterIn(size);
	float widthDifference = size.Width() - Frame().Width();
	if (widthDifference < 0)
		MoveBy(widthDifference / 2.0, 0);
	Show();
}


/**
 * @brief Notifies the parent window before tearing down.
 *
 * Sends ADD_WINDOW_CLOSED so the parent can re-enable its Add button,
 * then defers to BWindow::Quit().
 */
void
AddRepoWindow::Quit()
{
	fReplyMessenger.SendMessage(ADD_WINDOW_CLOSED);
	BWindow::Quit();
}


/**
 * @brief Dispatches messages from the dialog's controls.
 *
 * Validates the URL on Add. Invalid URLs trigger an inline error alert;
 * valid URLs are forwarded to the parent via ADD_REPO_URL and the dialog
 * closes itself.
 *
 * @param message Incoming BMessage.
 */
void
AddRepoWindow::MessageReceived(BMessage* message)
{
	switch (message->what)
	{
		case CANCEL_BUTTON_PRESSED:
			if (QuitRequested())
				Quit();
			break;

		case ADD_BUTTON_PRESSED:
		{
			BString url(fText->Text());
			if (url != "") {
				// URL must have a protocol
				BUrl newRepoUrl(url, true);
				if (!newRepoUrl.IsValid()) {
					BAlert* alert = new BAlert("error",
						B_TRANSLATE_COMMENT("This is not a valid URL.",
							"Add URL error message"),
						kOKLabel, NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
					alert->SetFeel(B_MODAL_APP_WINDOW_FEEL);
					alert->Go(NULL);
					// Center the alert to this window and move down some
					alert->CenterIn(Frame());
					alert->MoveBy(0, kAddWindowOffset);
				} else {
					BMessage* addMessage = new BMessage(ADD_REPO_URL);
					addMessage->AddString(key_url, url);
					fReplyMessenger.SendMessage(addMessage);
					Quit();
				}
			}
			break;
		}
		
		default:
			BWindow::MessageReceived(message);
	}
}


void
AddRepoWindow::FrameResized(float newWidth, float newHeight)
{
	sAddWindowWidth = newWidth;
}


status_t
AddRepoWindow::_GetClipboardData()
{
	if (be_clipboard->Lock()) {
		const char* string;
		ssize_t stringLen;
		BMessage* clip = be_clipboard->Data();
		clip->FindData("text/plain", B_MIME_TYPE, (const void **)&string,
			&stringLen);
		be_clipboard->Unlock();

		// The string must be a valid url
		BString clipString(string, stringLen);
		BUrl testUrl(clipString.String(), true);
		if (!testUrl.IsValid())
			return B_ERROR;
		else
			fText->SetText(clipString);
	}
	return B_OK;
}
