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
 * @file Navigator.cpp
 * @brief BNavigator — a BToolBar providing back, forward, up, and location controls.
 *
 * BNavigator maintains a back/forward history of entry_ref paths and renders
 * a toolbar with icon buttons plus a BTextControl for direct path entry.  It
 * responds to button presses, location bar commits, and dropped refs to drive
 * kSwitchDirectory messages to the parent container window.
 *
 * @see BContainerWindow, BToolBar
 */


#include "Navigator.h"

#include <ControlLook.h>
#include <TextControl.h>
#include <Window.h>

#include "Bitmaps.h"
#include "Commands.h"
#include "FSUtils.h"
#include "Tracker.h"


namespace BPrivate {

static const int32 kMaxHistory = 32;

}


//	#pragma mark - BNavigator


/**
 * @brief Construct a BNavigator initialised to the path of @p model.
 *
 * Sets up the location text control and inset values.  Toolbar buttons are
 * added in AttachedToWindow().
 *
 * @param model  The Model whose path is the starting location.
 */
BNavigator::BNavigator(const Model* model)
	:
	BToolBar(),
	fBackHistory(8),
	fForwHistory(8)
{
	// Get initial path
	model->GetPath(&fPath);

	fLocation = new BTextControl("Location", "", "",
		new BMessage(kNavigatorCommandLocation));
	fLocation->SetDivider(0);

	GroupLayout()->SetInsets(0.0f, 0.0f, B_USE_HALF_ITEM_INSETS, 1.0f);
		// 1px bottom inset used for border

	// Needed to draw the bottom border
	SetFlags(Flags() | B_WILL_DRAW);
}


/**
 * @brief Destroy the BNavigator.
 */
BNavigator::~BNavigator()
{
}


/**
 * @brief Create toolbar buttons and attach the location control once the window exists.
 */
void
BNavigator::AttachedToWindow()
{
	BToolBar::AttachedToWindow();

	const BRect iconRect(BPoint(0, 0),
		be_control_look->ComposeIconSize(20));

	// Set up toolbar items
	BBitmap* bmpBack = new BBitmap(iconRect, B_RGBA32);
	GetTrackerResources()->GetIconResource(R_ResBackNav, B_MINI_ICON, bmpBack);
	AddAction(kNavigatorCommandBackward, this, bmpBack);
	SetActionEnabled(kNavigatorCommandBackward, false);
	delete bmpBack;

	BBitmap* bmpForw = new BBitmap(iconRect, B_RGBA32);
	GetTrackerResources()->GetIconResource(R_ResForwNav, B_MINI_ICON, bmpForw);
	AddAction(kNavigatorCommandForward, this, bmpForw);
	SetActionEnabled(kNavigatorCommandForward, false);
	delete bmpForw;

	BBitmap* bmpUp = new BBitmap(iconRect, B_RGBA32);
	GetTrackerResources()->GetIconResource(R_ResUpNav, B_MINI_ICON, bmpUp);
	AddAction(kNavigatorCommandUp, this, bmpUp);
	SetActionEnabled(kNavigatorCommandUp, false);
	delete bmpUp;

	AddView(fLocation);
	fLocation->SetTarget(this);
}


/**
 * @brief Perform the initial widget-state update after all children are attached.
 */
void
BNavigator::AllAttached()
{
	// Inital setup of widget states
	UpdateLocation(0, kActionSet);
}


/**
 * @brief Draw the toolbar, adding a 1 px bottom border for visual separation.
 *
 * @param updateRect  The dirty rectangle that needs repainting.
 */
void
BNavigator::Draw(BRect updateRect)
{
	// Draw a 1px bottom border, like BMenuBar
	BRect rect(Bounds());
	rgb_color base = LowColor();
	uint32 flags = 0;

	be_control_look->DrawBorder(this, rect, updateRect, base,
		B_PLAIN_BORDER, flags, BControlLook::B_BOTTOM_BORDER);

	_inherited::Draw(rect & updateRect);
}


/**
 * @brief Handle navigation commands and dropped refs.
 *
 * Dispatches kNavigatorCommandBackward, Forward, Up, Location, and
 * kNavigatorCommandSetFocus.  Dropped refs resolve to a directory and
 * post kSwitchDirectory to the window.
 *
 * @param message  The incoming BMessage.
 */
void
BNavigator::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kNavigatorCommandBackward:
			GoBackward((modifiers() & B_OPTION_KEY) == B_OPTION_KEY);
			break;

		case kNavigatorCommandForward:
			GoForward((modifiers() & B_OPTION_KEY) == B_OPTION_KEY);
			break;

		case kNavigatorCommandUp:
			GoUp((modifiers() & B_OPTION_KEY) == B_OPTION_KEY);
			break;

		case kNavigatorCommandLocation:
			GoTo();
			break;

		case kNavigatorCommandSetFocus:
			fLocation->MakeFocus();
			break;

		default:
		{
			// Catch any dropped refs and try to switch to this new directory
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK) {
				BMessage message(kSwitchDirectory);
				BEntry entry(&ref, true);
				if (!entry.IsDirectory()) {
					entry.GetRef(&ref);
					BPath path(&ref);
					path.GetParent(&path);
					get_ref_for_path(path.Path(), &ref);
				}
				message.AddRef("refs", &ref);
				message.AddInt32("action", kActionSet);
				Window()->PostMessage(&message);
			}
		}
	}
}


/**
 * @brief Navigate to the previous directory in the back history.
 *
 * @param option  If true, open the target in a new window instead.
 */
void
BNavigator::GoBackward(bool option)
{
	int32 itemCount = fBackHistory.CountItems();
	if (itemCount >= 2 && fBackHistory.ItemAt(itemCount - 2)) {
		BEntry entry;
		if (entry.SetTo(fBackHistory.ItemAt(itemCount - 2)->Path()) == B_OK)
			SendNavigationMessage(kActionBackward, &entry, option);
	}
}


/**
 * @brief Navigate to the next directory in the forward history.
 *
 * @param option  If true, open the target in a new window instead.
 */
void
BNavigator::GoForward(bool option)
{
	if (fForwHistory.CountItems() >= 1) {
		BEntry entry;
		if (entry.SetTo(fForwHistory.LastItem()->Path()) == B_OK)
			SendNavigationMessage(kActionForward, &entry, option);
	}
}


/**
 * @brief Navigate to the parent directory of the current location.
 *
 * @param option  If true, open the parent in a new window instead.
 */
void
BNavigator::GoUp(bool option)
{
	BEntry entry;
	if (entry.SetTo(fPath.Path()) == B_OK) {
		BEntry parentEntry;
		if (entry.GetParent(&parentEntry) == B_OK) {
			SendNavigationMessage(kActionUp, &parentEntry, option);
		}
	}
}


/**
 * @brief Build and dispatch a kSwitchDirectory (or B_REFS_RECEIVED) message.
 *
 * With @p option false the message goes to the current window; with @p option
 * true it goes to be_app so the directory opens in a new window.
 *
 * @param action  The navigation action constant (kActionBackward, etc.).
 * @param entry   The target BEntry to switch to.
 * @param option  If true, open in a new window rather than the current one.
 */
void
BNavigator::SendNavigationMessage(NavigationAction action, BEntry* entry,
	bool option)
{
	entry_ref ref;

	if (entry->GetRef(&ref) == B_OK) {
		BMessage message;
		message.AddRef("refs", &ref);
		message.AddInt32("action", action);

		// get the node of this folder for selecting it in the new location
		const node_ref* nodeRef;
		if (Window() && Window()->TargetModel())
			nodeRef = Window()->TargetModel()->NodeRef();
		else
			nodeRef = NULL;

		// if the option key was held down, open in new window (send message
		// to be_app) otherwise send message to this window. TTracker
		// (be_app) understands nodeRefToSlection, BContainerWindow doesn't,
		// so we have to select the item manually
		if (option) {
			message.what = B_REFS_RECEIVED;
			if (nodeRef != NULL) {
				message.AddData("nodeRefToSelect", B_RAW_TYPE, nodeRef,
					sizeof(node_ref));
			}
			be_app->PostMessage(&message);
		} else {
			message.what = kSwitchDirectory;
			Window()->PostMessage(&message);
			UnlockLooper();
				// This is to prevent a dead-lock situation.
				// SelectChildInParentSoon() eventually locks the
				// TaskLoop::fLock. Later, when StandAloneTaskLoop::Run()
				// runs, it also locks TaskLoop::fLock and subsequently
				// locks this window's looper. Therefore we can't call
				// SelectChildInParentSoon with our Looper locked,
				// because we would get different orders of locking
				// (thus the risk of dead-locking).
				//
				// Todo: Change the locking behaviour of
				// StandAloneTaskLoop::Run() and subsequently called
				// functions.
			if (nodeRef != NULL) {
				TTracker* tracker = dynamic_cast<TTracker*>(be_app);
				if (tracker != NULL)
					tracker->SelectChildInParentSoon(&ref, nodeRef);
			}

			LockLooper();
		}
	}
}


/**
 * @brief Navigate to the path typed in the location text control.
 *
 * Resolves the text to an entry_ref and posts kSwitchDirectory to the window.
 * If the path is invalid, restores the current path text.
 */
void
BNavigator::GoTo()
{
	BString pathname = fLocation->Text();

	if (pathname.Compare("") == 0)
		pathname = "/";

	BEntry entry;
	entry_ref ref;

	if (entry.SetTo(pathname.String()) == B_OK
		&& entry.GetRef(&ref) == B_OK) {
		BMessage message(kSwitchDirectory);
		message.AddRef("refs", &ref);
		message.AddInt32("action", kActionLocation);
		Window()->PostMessage(&message);
	} else {
		BPath path;

		if (Window() && Window()->TargetModel()) {
			Window()->TargetModel()->GetPath(&path);
			fLocation->SetText(path.Path());
		}
	}
}


/**
 * @brief Synchronise history, button states, and location text with a new directory.
 *
 * Updates back/forward history based on @p action, then refreshes the enabled
 * state of the Back, Forward, and Up buttons and (unless the action is
 * kActionLocation) sets the text control to the new path.
 *
 * @param newmodel  The new Model to use as the current location, or NULL.
 * @param action    One of the NavigationAction constants describing what happened.
 */
void
BNavigator::UpdateLocation(const Model* newmodel, int32 action)
{
	if (newmodel)
		newmodel->GetPath(&fPath);

	// Modify history according to commands
	switch (action) {
		case kActionBackward:
			fForwHistory.AddItem(fBackHistory.RemoveItemAt(
				fBackHistory.CountItems() - 1));
			break;

		case kActionForward:
			fBackHistory.AddItem(fForwHistory.RemoveItemAt(
				fForwHistory.CountItems() - 1));
			break;

		case kActionUpdatePath:
			break;

		default:
			fForwHistory.MakeEmpty();
			fBackHistory.AddItem(new BPath(fPath));

			while (fBackHistory.CountItems() > kMaxHistory)
				fBackHistory.RemoveItem(fBackHistory.FirstItem(), true);
			break;
	}

	// Enable Up button when there is any parent
	BEntry entry;
	if (entry.SetTo(fPath.Path()) == B_OK) {
		BEntry parentEntry;
		bool enable = entry.GetParent(&parentEntry) == B_OK;
		SetActionEnabled(kNavigatorCommandUp, enable);
	}

	// Enable history buttons if history contains something
	SetActionEnabled(kNavigatorCommandForward, fForwHistory.CountItems() > 0);
	SetActionEnabled(kNavigatorCommandBackward, fBackHistory.CountItems() > 1);

	// Avoid loss of selection and cursor position
	if (action != kActionLocation)
		fLocation->SetText(fPath.Path());
}
