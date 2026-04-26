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
 *   Copyright 2003-2013 Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Jérôme Duval, jerome.duval@free.fr
 *       Julun, host.haiku@gmx.de
 *       Michael Phipps
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file PasswordWindow.cpp
 * @brief Modal dialog implementation for picking the screensaver password.
 *
 * Builds a small layout with two radio buttons (system or custom) and a
 * pair of text controls for the custom password and confirmation. The
 * dialog hides itself rather than quitting so the parent ScreenSaverWindow
 * can re-open it without recreating the layout.
 *
 * @see ScreenSaverSettings
 */


#include "PasswordWindow.h"

#include <Alert.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <LayoutItem.h>
#include <RadioButton.h>
#include <Screen.h>
#include <Size.h>
#include <TextControl.h>

#include <ctype.h>

#include "ScreenSaverSettings.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ScreenSaver"


/** @brief Width of the password text controls in default-spacing units. */
static const uint32 kPasswordTextWidth = 12;

/** @brief Message dispatched by the Done button. */
static const uint32 kMsgDone = 'done';
/** @brief Message dispatched when either password-mode radio button is toggled. */
static const uint32 kMsgPasswordTypeChanged = 'pwtp';


/**
 * @brief Constructs the modal password window bound to the shared settings.
 *
 * @param settings ScreenSaverSettings whose lock method and password are
 *                 read on entry and updated on Done.
 */
PasswordWindow::PasswordWindow(ScreenSaverSettings& settings)
	:
	BWindow(BRect(100, 100, 300, 200), B_TRANSLATE("Password Window"),
		B_MODAL_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL, B_NOT_RESIZABLE
			| B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fSettings(settings)
{
	_Setup();
	Update();
}


/**
 * @brief Builds the layout of the password window: two boxes and a button row.
 *
 * The system box hosts only the "use system password" radio. The custom
 * box hosts the matching radio together with the password and confirm
 * text controls (which hide typing). A Cancel/Done button row sits at the
 * bottom; Done is the default button.
 */
void
PasswordWindow::_Setup()
{
	float spacing = be_control_look->DefaultItemSpacing();

	BView* topView = new BView("topView", B_WILL_DRAW);
	topView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	topView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));

	BBox* systemBox = new BBox("systemBox");
	systemBox->SetBorder(B_NO_BORDER);

	fUseSystem = new BRadioButton("useSystem",
		B_TRANSLATE("Use system password"),
		new BMessage(kMsgPasswordTypeChanged));
	systemBox->SetLabel(fUseSystem);

	BBox* customBox = new BBox("customBox");

	fUseCustom = new BRadioButton("useCustom",
		B_TRANSLATE("Use custom password"),
		new BMessage(kMsgPasswordTypeChanged));
	customBox->SetLabel(fUseCustom);

	fPasswordControl = new BTextControl("passwordTextView",
		B_TRANSLATE("Password:"), B_EMPTY_STRING, NULL);
	fPasswordControl->TextView()->HideTyping(true);
	fPasswordControl->SetAlignment(B_ALIGN_RIGHT, B_ALIGN_LEFT);

	BLayoutItem* passwordTextView
		= fPasswordControl->CreateTextViewLayoutItem();
	passwordTextView->SetExplicitMinSize(BSize(spacing * kPasswordTextWidth,
		B_SIZE_UNSET));

	fConfirmControl = new BTextControl("confirmTextView",
		B_TRANSLATE("Confirm password:"), B_EMPTY_STRING, NULL);
	fConfirmControl->SetExplicitMinSize(BSize(spacing * kPasswordTextWidth,
		B_SIZE_UNSET));
	fConfirmControl->TextView()->HideTyping(true);
	fConfirmControl->SetAlignment(B_ALIGN_RIGHT, B_ALIGN_LEFT);

	BLayoutItem* confirmTextView = fConfirmControl->CreateTextViewLayoutItem();
	confirmTextView->SetExplicitMinSize(BSize(spacing * kPasswordTextWidth,
		B_SIZE_UNSET));

	customBox->AddChild(BLayoutBuilder::Group<>(B_VERTICAL)
		.SetInsets(B_USE_SMALL_SPACING)
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.Add(fPasswordControl->CreateLabelLayoutItem(), 0, 0)
			.Add(passwordTextView, 1, 0)
			.Add(fConfirmControl->CreateLabelLayoutItem(), 0, 1)
			.Add(confirmTextView, 1, 1)
			.End()
		.View());

	BButton* doneButton = new BButton("done", B_TRANSLATE("Done"),
		new BMessage(kMsgDone));

	BButton* cancelButton = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(B_CANCEL));

	BLayoutBuilder::Group<>(topView, B_VERTICAL, 0)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(systemBox)
		.Add(customBox)
		.AddStrut(B_USE_DEFAULT_SPACING)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancelButton)
			.Add(doneButton)
			.End()
		.End();

	doneButton->MakeDefault(true);

	SetLayout(new BGroupLayout(B_VERTICAL));
	GetLayout()->AddView(topView);
}


/**
 * @brief Synchronizes the radio buttons and text-control enable state with
 *        the underlying ScreenSaverSettings.
 *
 * The system-password radio is checked when settings indicate the system
 * password is in use; otherwise the custom radio is checked. The password
 * and confirm text controls are enabled only in custom mode.
 */
void
PasswordWindow::Update()
{
	if (fSettings.UseSystemPassword())
		fUseSystem->SetValue(B_CONTROL_ON);
	else
		fUseCustom->SetValue(B_CONTROL_ON);

	bool useSysPassword = (fUseCustom->Value() > 0);
	fConfirmControl->SetEnabled(useSysPassword);
	fPasswordControl->SetEnabled(useSysPassword);
}


/**
 * @brief BWindow message hook: handles Done, Cancel, and mode toggles.
 *
 * On Done, the lock method is committed and (in custom mode) the typed
 * password is hashed via @c crypt() and stored. A mismatched confirmation
 * shows an alert and aborts. On Cancel, the text fields are cleared and
 * the window hides without committing changes. Mode toggles update the
 * settings and refresh the enable state.
 *
 * @param message Incoming message; unhandled messages fall through to
 *                BWindow::MessageReceived().
 */
void
PasswordWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgDone:
			fSettings.SetLockMethod(fUseCustom->Value() ? "custom" : "system");
			if (fUseCustom->Value()) {
				if (strcmp(fPasswordControl->Text(), fConfirmControl->Text())
						!= 0) {
					BAlert* alert = new BAlert("noMatch",
						B_TRANSLATE("Passwords don't match. Please try again."),
						B_TRANSLATE("OK"));
					alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
					alert->Go();
					break;
				}
				fSettings.SetPassword(crypt(fPasswordControl->Text(), NULL));
			} else
				fSettings.SetPassword("");

			fPasswordControl->SetText("");
			fConfirmControl->SetText("");
			fSettings.Save();
			Hide();
			break;

		case B_CANCEL:
			fPasswordControl->SetText("");
			fConfirmControl->SetText("");
			Hide();
			break;

		case kMsgPasswordTypeChanged:
			fSettings.SetLockMethod(fUseCustom->Value() > 0 ? "custom" : "system");
			Update();
			break;

		default:
			BWindow::MessageReceived(message);
 	}
}
