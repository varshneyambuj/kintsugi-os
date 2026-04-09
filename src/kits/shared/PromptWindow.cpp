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
 *   Copyright 2012-2013, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */

/** @file PromptWindow.cpp
 *  @brief Implements \c PromptWindow, a lightweight modal-style floating
 *         window that presents a single-line text entry to the user and
 *         forwards the accepted text to a \c BMessenger target.
 */

#include "PromptWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <TextControl.h>


/** Internal message code sent when the user accepts the input. */
static const uint32 kAcceptInput = 'acin';


/**
 * @brief Constructs a \c PromptWindow.
 *
 * Builds the window UI using \c BLayoutBuilder::Group: an optional
 * informational \c BStringView (\a info), a \c BTextControl labelled with
 * \a label, and Cancel / Accept buttons. If \a info is \c NULL the info
 * view is hidden. The \c BTextControl minimum width is set to 200 pixels and
 * it receives the initial keyboard focus. The Accept button is set as the
 * default button so pressing Enter accepts the input.
 *
 * @param title   Window title string.
 * @param label   Label shown to the left of the text entry field.
 * @param info    Optional descriptive text shown above the entry field; may
 *                be \c NULL.
 * @param target  \c BMessenger to which the accepted message will be sent.
 * @param message The \c BMessage to send on acceptance; the entered text is
 *                appended as a \c "text" string field. Ownership is
 *                transferred to the window.
 */
PromptWindow::PromptWindow(const char* title, const char* label,
	const char* info, BMessenger target, BMessage* message)
	:
	BWindow(BRect(), title, B_FLOATING_WINDOW, B_NOT_RESIZABLE
			| B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fTarget(target),
	fMessage(message)
{
	fInfoView = new BStringView("info", info);
	fTextControl = new BTextControl("promptcontrol", label, NULL,
		new BMessage(kAcceptInput));
	BButton* cancelButton = new BButton("Cancel", new
		BMessage(B_QUIT_REQUESTED));
	BButton* acceptButton = new BButton("Accept", new
		BMessage(kAcceptInput));
	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fInfoView)
		.Add(fTextControl)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancelButton)
			.Add(acceptButton)
		.End()
	.End();

	if (info == NULL)
		fInfoView->Hide();

	fTextControl->TextView()->SetExplicitMinSize(BSize(200.0, B_SIZE_UNSET));
	fTextControl->SetTarget(this);
	acceptButton->SetTarget(this);
	cancelButton->SetTarget(this);
	fTextControl->MakeFocus(true);

	SetDefaultButton(acceptButton);
}


/**
 * @brief Destructor. Deletes the owned \c BMessage.
 */
PromptWindow::~PromptWindow()
{
	delete fMessage;
}


/**
 * @brief Handles messages sent to the window.
 *
 * When a \c kAcceptInput message is received the current text from the
 * \c BTextControl is appended to \c fMessage under the key \c "text", and
 * the message is forwarded to \c fTarget via \c BMessenger::SendMessage().
 * The window then posts \c B_QUIT_REQUESTED to close itself. All other
 * messages are forwarded to \c BWindow::MessageReceived().
 *
 * @param message The incoming \c BMessage.
 */
void
PromptWindow::MessageReceived(BMessage* message)
{
	switch (message->what)
	{
		case kAcceptInput:
		{
			fMessage->AddString("text", fTextControl->TextView()->Text());
			fTarget.SendMessage(fMessage);
			PostMessage(B_QUIT_REQUESTED);
		}
		default:
		{
			BWindow::MessageReceived(message);
			break;
		}
	}
}


/**
 * @brief Replaces the messenger target for accepted-input notifications.
 *
 * @param messenger The new \c BMessenger to send the accepted message to.
 * @return \c B_OK unconditionally.
 */
status_t
PromptWindow::SetTarget(BMessenger messenger)
{
	fTarget = messenger;
	return B_OK;
}


/**
 * @brief Replaces the message sent when the user accepts the prompt.
 *
 * The previously held message is deleted before \a message is stored.
 * Ownership of \a message is transferred to the window.
 *
 * @param message The new \c BMessage to send on acceptance.
 * @return \c B_OK unconditionally.
 */
status_t
PromptWindow::SetMessage(BMessage* message)
{
	delete fMessage;
	fMessage = message;
	return B_OK;
}
