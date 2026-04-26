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
 *   Copyright 2006-2010, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file TypeListWindow.cpp
 * @brief Implementation of the modal MIME-type chooser window. Lets the
 *        user pick a registered MIME type from the system database and
 *        forwards the choice back to a target BWindow via BMessenger.
 */


#include "MimeTypeListView.h"
#include "TypeListWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <ScrollView.h>

#include <string.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Type List Window"


/** @brief Internal selection-changed message from the inner list view. */
const uint32 kMsgTypeSelected = 'tpsl';
/** @brief Internal "Done" message: commit the current selection. */
const uint32 kMsgSelected = 'seld';


/**
 * @brief Builds the modal type-chooser window centred near @a target.
 *
 * @param currentType  Reserved for future "preselect this type" behaviour;
 *                     not used at construction time.
 * @param what         Message constant to post back to @a target when the
 *                     user confirms a selection.
 * @param target       Window that should receive the resulting message.
 */
TypeListWindow::TypeListWindow(const char* currentType, uint32 what,
		BWindow* target)
	:
	BWindow(BRect(100, 100, 360, 440), B_TRANSLATE("Choose type"),
		B_MODAL_WINDOW,
		B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fTarget(target),
	fWhat(what)
{
	float padding = be_control_look->DefaultItemSpacing();

	fSelectButton = new BButton("select", B_TRANSLATE("Done"),
		new BMessage(kMsgSelected));
	fSelectButton->SetEnabled(false);

	BButton* button = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(B_CANCEL));

	fSelectButton->MakeDefault(true);

	fListView = new MimeTypeListView("typeview", NULL, true, false);
	fListView->SetSelectionMessage(new BMessage(kMsgTypeSelected));
	fListView->SetInvocationMessage(new BMessage(kMsgSelected));

	BScrollView* scrollView = new BScrollView("scrollview", fListView,
		B_FRAME_EVENTS | B_WILL_DRAW, false, true);

	BLayoutBuilder::Group<>(this, B_VERTICAL, padding)
		.SetInsets(padding)
		.Add(scrollView)
		.AddGroup(B_HORIZONTAL, padding)
			.AddGlue()
			.Add(button)
			.Add(fSelectButton);

	BAlignment buttonAlignment =
		BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_VERTICAL_CENTER);
	button->SetExplicitAlignment(buttonAlignment);
	fSelectButton->SetExplicitAlignment(buttonAlignment);

	MoveTo(target->Frame().LeftTop() + BPoint(15.0f, 15.0f));
}


/**
 * @brief Destructor; the embedded views are owned by the BWindow base.
 */
TypeListWindow::~TypeListWindow()
{
}


/**
 * @brief Routes user actions: enables the Done button on selection,
 *        sends the chosen MIME type to the target on confirm, and quits
 *        on cancel.
 *
 * @param message  Incoming BMessage; recognised what codes are
 *                 kMsgTypeSelected, kMsgSelected, and B_CANCEL.
 */
void
TypeListWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgTypeSelected:
			fSelectButton->SetEnabled(fListView->CurrentSelection() >= 0);
			break;

		case kMsgSelected:
		{
			MimeTypeItem* item = dynamic_cast<MimeTypeItem*>(fListView->ItemAt(
				fListView->CurrentSelection()));
			if (item != NULL) {
				BMessage select(fWhat);
				select.AddString("type", item->Type());
				fTarget.SendMessage(&select);
			}

			// supposed to fall through
		}
		case B_CANCEL:
			PostMessage(B_QUIT_REQUESTED);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}

