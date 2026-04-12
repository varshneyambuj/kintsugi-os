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
 * @file SelectionWindow.cpp
 * @brief A floating window for selecting poses by filename expression.
 *
 * SelectionWindow provides a small floating panel attached to a
 * BContainerWindow that lets the user select or deselect poses whose names
 * match a typed expression.  The expression type (starts-with, ends-with,
 * contains, wildcard, or regex) is chosen from a pop-up menu.  On clicking
 * Select, a kSelectMatchingEntries message is posted to the container window.
 *
 * @see BContainerWindow, BPoseView
 */


#include <Alert.h>
#include <Box.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <WindowPrivate.h>

#include "AutoLock.h"
#include "ContainerWindow.h"
#include "Commands.h"
#include "Screen.h"
#include "SelectionWindow.h"


const uint32 kSelectButtonPressed = 'sbpr';


//	#pragma mark - SelectionWindow


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SelectionWindow"


/**
 * @brief Construct a SelectionWindow and attach it to @p window.
 *
 * Builds the layout (matching-type menu, expression text control, invert and
 * ignore-case checkboxes, Select button), positions the window near the mouse
 * cursor, and starts the looper.
 *
 * @param window  The BContainerWindow that will receive kSelectMatchingEntries messages.
 */
SelectionWindow::SelectionWindow(BContainerWindow* window)
	:
	BWindow(BRect(0, 0, 270, 0), B_TRANSLATE("Select"),	B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_NOT_V_RESIZABLE
			| B_NO_WORKSPACE_ACTIVATION | B_ASYNCHRONOUS_CONTROLS
			| B_NOT_ANCHORED_ON_ACTIVATE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_CLOSE_ON_ESCAPE),
	fParentWindow(window)
{
	if (window->Feel() & kDesktopWindowFeel) {
		// The window will not show up if we have
		// B_FLOATING_SUBSET_WINDOW_FEEL and use it with the desktop window
		// since it's never in front.
		SetFeel(B_NORMAL_WINDOW_FEEL);
	}

	AddToSubset(fParentWindow);

	BMenu* menu = new BPopUpMenu("popup");
	menu->AddItem(new BMenuItem(B_TRANSLATE("starts with"),	NULL));
	menu->AddItem(new BMenuItem(B_TRANSLATE("ends with"), NULL));
	menu->AddItem(new BMenuItem(B_TRANSLATE("contains"), NULL));
	menu->AddItem(new BMenuItem(B_TRANSLATE("matches wildcard expression"),
		NULL));
	menu->AddItem(new BMenuItem(B_TRANSLATE("matches regular expression"),
		NULL));

	menu->SetLabelFromMarked(true);
	menu->ItemAt(3)->SetMarked(true);
		// Set wildcard matching to default.

	// Set up the menu field
	fMatchingTypeMenuField = new BMenuField("name", B_TRANSLATE("Name"), menu);

	// Set up the expression text control
	fExpressionTextControl = new BTextControl("expression", NULL, "", NULL);

	// Set up the Invert checkbox
	fInverseCheckBox = new BCheckBox("inverse", B_TRANSLATE("Invert"), NULL);
	fInverseCheckBox->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	// Set up the Ignore Case checkbox
	fIgnoreCaseCheckBox = new BCheckBox(
		"ignore", B_TRANSLATE("Ignore case"), NULL);
	fIgnoreCaseCheckBox->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fIgnoreCaseCheckBox->SetValue(1);

	// Set up the Select button
	fSelectButton = new BButton("select", B_TRANSLATE("Select"),
		new BMessage(kSelectButtonPressed));
	fSelectButton->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fSelectButton->MakeDefault(true);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_HALF_ITEM_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fMatchingTypeMenuField)
		.Add(fExpressionTextControl)
		.AddGroup(B_HORIZONTAL)
			.Add(fInverseCheckBox)
			.Add(fIgnoreCaseCheckBox)
			.AddGlue(100.0)
			.Add(fSelectButton)
			.End()
		.End();

	Run();

	Lock();
	MoveCloseToMouse();
	fExpressionTextControl->MakeFocus(true);
	Unlock();
}


/**
 * @brief Handle kSelectButtonPressed by posting a selection request to the parent window.
 *
 * Hides the window before posting so the container window regains focus when
 * the message is processed.
 *
 * @param message  The incoming BMessage.
 */
void
SelectionWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kSelectButtonPressed:
		{
			Hide();
				// Order of posting and hiding important
				// since we want to activate the target
				// window when the message arrives.
				// (Hide is synhcronous, while PostMessage is not.)
				// See PoseView::SelectMatchingEntries().

			BMessage* selectionInfo = new BMessage(kSelectMatchingEntries);
			selectionInfo->AddInt32("ExpressionType", ExpressionType());
			BString expression;
			Expression(expression);
			selectionInfo->AddString("Expression", expression.String());
			selectionInfo->AddBool("InvertSelection", Invert());
			selectionInfo->AddBool("IgnoreCase", IgnoreCase());
			fParentWindow->PostMessage(selectionInfo);
			break;
		}

		default:
			_inherited::MessageReceived(message);
			break;
	}
}


/**
 * @brief Hide the window instead of closing it when the user clicks the close box.
 *
 * @return False — always prevents the window from being destroyed.
 */
bool
SelectionWindow::QuitRequested()
{
	Hide();
	return false;
}


/**
 * @brief Position the window centred on the current mouse cursor location.
 *
 * Constrains the position so the window remains fully within the screen bounds.
 * Also sets the window to the current workspace.
 */
void
SelectionWindow::MoveCloseToMouse()
{
	uint32 buttons;
	BPoint mousePosition;

	ChildAt((int32)0)->GetMouse(&mousePosition, &buttons);
	ConvertToScreen(&mousePosition);

	// Position the window centered around the mouse...
	BPoint windowPosition = BPoint(mousePosition.x - Frame().Width() / 2,
		mousePosition.y	- Frame().Height() / 2);

	// ... unless that's outside of the current screen size:
	BScreen screen;
	windowPosition.x
		= MAX(20, MIN(screen.Frame().right - 20 - Frame().Width(),
		windowPosition.x));
	windowPosition.y = MAX(20,
		MIN(screen.Frame().bottom - 20 - Frame().Height(), windowPosition.y));

	MoveTo(windowPosition);
	SetWorkspaces(1UL << current_workspace());
}


/**
 * @brief Return the currently-selected expression match type.
 *
 * Reads the marked item from the matching-type pop-up menu and converts it
 * to a TrackerStringExpressionType constant.
 *
 * @return One of kStartsWith, kEndsWith, kContains, kGlobMatch, kRegexpMatch,
 *         or kNone on error.
 */
TrackerStringExpressionType
SelectionWindow::ExpressionType() const
{
	if (!fMatchingTypeMenuField->LockLooper())
		return kNone;

	BMenuItem* item = fMatchingTypeMenuField->Menu()->FindMarked();
	if (item == NULL) {
		fMatchingTypeMenuField->UnlockLooper();
		return kNone;
	}

	int32 index = fMatchingTypeMenuField->Menu()->IndexOf(item);

	fMatchingTypeMenuField->UnlockLooper();

	if (index < kStartsWith || index > kRegexpMatch)
		return kNone;

	TrackerStringExpressionType typeArray[] = {	kStartsWith, kEndsWith,
		kContains, kGlobMatch, kRegexpMatch};

	return typeArray[index];
}


/**
 * @brief Copy the current expression text into @p result.
 *
 * @param result  Output BString that receives the expression text.
 */
void
SelectionWindow::Expression(BString &result) const
{
	if (!fExpressionTextControl->LockLooper())
		return;

	result = fExpressionTextControl->Text();

	fExpressionTextControl->UnlockLooper();
}


/**
 * @brief Return whether the case-insensitive matching checkbox is checked.
 *
 * @return True if case should be ignored during matching.
 */
bool
SelectionWindow::IgnoreCase() const
{
	if (!fIgnoreCaseCheckBox->LockLooper()) {
		// default action
		return true;
	}

	bool ignore = fIgnoreCaseCheckBox->Value() != 0;

	fIgnoreCaseCheckBox->UnlockLooper();

	return ignore;
}


/**
 * @brief Return whether the invert-selection checkbox is checked.
 *
 * @return True if the selection should be inverted (select non-matching poses).
 */
bool
SelectionWindow::Invert() const
{
	if (!fInverseCheckBox->LockLooper()) {
		// default action
		return false;
	}

	bool inverse = fInverseCheckBox->Value() != 0;

	fInverseCheckBox->UnlockLooper();

	return inverse;
}
