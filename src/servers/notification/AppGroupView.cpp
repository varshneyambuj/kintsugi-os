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
 *   Copyright 2010-2017, Haiku, Inc. All Rights Reserved.
 *   Copyright 2008-2009, Pier Luigi Fiorini. All Rights Reserved.
 *   Copyright 2004-2008, Michael Davidson. All Rights Reserved.
 *   Copyright 2004-2007, Mikael Eiman. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Davidson, slaad@bong.com.au
 *       Mikael Eiman, mikael@eiman.tv
 *       Pier Luigi Fiorini, pierluigi.fiorini@gmail.com
 *       Brian Hill, supernova@tycho.email
 */

/** @file AppGroupView.cpp
 *  @brief Implementation of the per-application notification group view. */

#include <algorithm>

#include <Beep.h>
#include <ControlLook.h>
#include <GroupLayout.h>
#include <GroupView.h>

#include "AppGroupView.h"

#include "NotificationWindow.h"
#include "NotificationView.h"


/**
 * @brief Constructs a group view that holds notifications for one application group.
 *
 * Sets up the header area (collapse arrow, label, close button) and a vertical
 * group layout with top inset equal to the header height.
 *
 * @param messenger BMessenger targeting the NotificationWindow, used to send
 *                  removal requests.
 * @param label     Application or group name displayed in the header; may be NULL.
 */
AppGroupView::AppGroupView(const BMessenger& messenger, const char* label)
	:
	BGroupView("appGroup", B_VERTICAL, 0),
	fLabel(label),
	fMessenger(messenger),
	fCollapsed(false),
	fCloseClicked(false),
	fPreviewModeOn(false)
{
	SetFlags(Flags() | B_WILL_DRAW);

	fHeaderSize = be_bold_font->Size() + be_control_look->ComposeSpacing(B_USE_ITEM_SPACING);
	static_cast<BGroupLayout*>(GetLayout())->SetInsets(0, fHeaderSize, 0, 0);
}


/**
 * @brief Draws the group header: background, collapse arrow, close button, and label.
 *
 * The header background uses the tinted panel colour.  When collapsed, the
 * child count is appended to the label.
 *
 * @param updateRect The dirty rectangle that needs to be redrawn.
 */
void
AppGroupView::Draw(BRect updateRect)
{
	rgb_color menuColor = ViewColor();
	BRect bounds = Bounds();
	bounds.bottom = bounds.top + fHeaderSize;

	// Draw the header background
	SetHighColor(tint_color(menuColor, 1.22));
	SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	StrokeLine(bounds.LeftTop(), bounds.LeftBottom());
	uint32 borders = BControlLook::B_TOP_BORDER | BControlLook::B_BOTTOM_BORDER
		| BControlLook::B_RIGHT_BORDER;
	be_control_look->DrawButtonBackground(this, bounds, bounds, menuColor, 0, borders);

	// Draw the buttons
	float buttonSize = CloseButtonSize();
	float arrowButtonSize = buttonSize * 1.5;
		// make arrow button a bit larger

	fCollapseRect.top = (fHeaderSize - arrowButtonSize) / 2;
	fCollapseRect.left = buttonSize + 1;
		// button left padding
	fCollapseRect.right = fCollapseRect.left + arrowButtonSize;
	fCollapseRect.bottom = fCollapseRect.top + arrowButtonSize;

	fCloseRect = bounds;
	fCloseRect.top = (fHeaderSize - buttonSize) / 2;
	fCloseRect.right -= buttonSize - 1;
	fCloseRect.left = fCloseRect.right - buttonSize;
	fCloseRect.bottom = fCloseRect.top + buttonSize;

	SetPenSize(1);

	// Draw the arrow button
	uint32 direction = fCollapsed ? BControlLook::B_DOWN_ARROW : BControlLook::B_UP_ARROW;
	be_control_look->DrawArrowShape(this, fCollapseRect, fCollapseRect, LowColor(), direction, 0,
		LowColor().IsLight() ? B_DARKEN_3_TINT : B_LIGHTEN_2_TINT);

	// Draw the dismiss widget
	DrawCloseButton(updateRect);

	// Draw the label
	SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
	BString label = fLabel;
	if (fCollapsed)
		label << " (" << fInfo.size() << ")";

	SetFont(be_bold_font);
	font_height fontHeight;
	GetFontHeight(&fontHeight);
	float y = (bounds.top + bounds.bottom - ceilf(fontHeight.ascent)
		- ceilf(fontHeight.descent)) / 2.0 + ceilf(fontHeight.ascent);

	float x = fCollapseRect.right + buttonSize * 1.5;
	DrawString(label.String(), BPoint(x, y));
}


/**
 * @brief Draws the X-shaped close/dismiss button in the group header.
 *
 * If the button is in a clicked state, a depressed button frame is drawn
 * behind the X glyph with a slightly darker tint.
 *
 * @param updateRect The dirty rectangle used for clipping.
 */
void
AppGroupView::DrawCloseButton(const BRect& updateRect)
{
	PushState();
	BRect closeRect = fCloseRect;

	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	float tint = ui_color(B_PANEL_BACKGROUND_COLOR).IsLight() ? B_DARKEN_2_TINT : B_LIGHTEN_2_TINT;

	if (fCloseClicked) {
		BRect buttonRect(closeRect.InsetByCopy(-4, -4));
		be_control_look->DrawButtonFrame(this, buttonRect, updateRect, base, base,
			BControlLook::B_ACTIVATED | BControlLook::B_BLEND_FRAME);
		be_control_look->DrawButtonBackground(this, buttonRect, updateRect, base,
			BControlLook::B_ACTIVATED);
		tint *= 1.2;
		closeRect.OffsetBy(1, 1);
	}

	base = tint_color(base, tint);
	SetHighColor(base);
	SetPenSize(2);
	StrokeLine(closeRect.LeftTop(), closeRect.RightBottom());
	StrokeLine(closeRect.LeftBottom(), closeRect.RightTop());
	PopState();
}


/**
 * @brief Returns the size (width and height) of the close button in pixels.
 *
 * @return The close button dimension derived from the default label spacing.
 */
float
AppGroupView::CloseButtonSize() const
{
	return be_control_look->DefaultLabelSpacing() + 1;
}


/**
 * @brief Handles mouse clicks on the header's close and collapse buttons.
 *
 * A click on the close button removes all child notification views and asks
 * the parent window to remove this group.  A click on the collapse arrow
 * toggles the collapsed state, hiding or showing the child views.
 * Clicks are ignored when preview mode is active.
 *
 * @param point The location of the mouse click in view coordinates.
 */
void
AppGroupView::MouseDown(BPoint point)
{
	// Preview Mode ignores any mouse clicks
	if (fPreviewModeOn)
		return;

	if (BRect(fCloseRect).InsetBySelf(-5, -5).Contains(point)) {
		int32 children = fInfo.size();
		for (int32 i = 0; i < children; i++) {
			fInfo[i]->RemoveSelf();
			delete fInfo[i];
		}

		fInfo.clear();

		// Remove ourselves from the parent view
		BMessage message(kRemoveGroupView);
		message.AddPointer("view", this);
		fMessenger.SendMessage(&message);
	} else if (BRect(fCollapseRect).InsetBySelf(-5, -5).Contains(point)) {
		fCollapsed = !fCollapsed;
		int32 children = fInfo.size();
		if (fCollapsed) {
			for (int32 i = 0; i < children; i++) {
				if (!fInfo[i]->IsHidden())
					fInfo[i]->Hide();
			}
			GetLayout()->SetExplicitMaxSize(GetLayout()->MinSize());
		} else {
			for (int32 i = 0; i < children; i++) {
				if (fInfo[i]->IsHidden())
					fInfo[i]->Show();
			}
			GetLayout()->SetExplicitMaxSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
		}

		InvalidateLayout();
		Invalidate(); // Need to redraw the collapse indicator and title
	}
}


/**
 * @brief Handles the kRemoveView message to remove a single notification view.
 *
 * Locates the NotificationView pointer in the message, removes it from the
 * internal list and the view hierarchy, and forwards the message to the parent
 * window.  If no children remain, requests that this group itself be removed.
 *
 * @param msg The incoming BMessage.
 */
void
AppGroupView::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kRemoveView:
		{
			NotificationView* view = NULL;
			if (msg->FindPointer("view", (void**)&view) != B_OK)
				return;

			infoview_t::iterator vIt = find(fInfo.begin(), fInfo.end(), view);
			if (vIt == fInfo.end())
				break;

			fInfo.erase(vIt);
			view->RemoveSelf();
			delete view;

			fMessenger.SendMessage(msg);

			if (!this->HasChildren()) {
				Hide();
				BMessage removeSelfMessage(kRemoveGroupView);
				removeSelfMessage.AddPointer("view", this);
				fMessenger.SendMessage(&removeSelfMessage);
			}

			break;
		}
		default:
			BView::MessageReceived(msg);
	}
}


/**
 * @brief Adds a notification view to this group, replacing an existing one with the same ID.
 *
 * If a notification with the same MessageID already exists, it is replaced
 * in-place (emitting a progress beep if the percentage changed).  Otherwise
 * the new view is appended.  All siblings are invalidated so close buttons
 * update, and the group is shown if it was hidden.
 *
 * @param view The NotificationView to add; ownership is transferred to the layout.
 */
void
AppGroupView::AddInfo(NotificationView* view)
{
	BString id = view->MessageID();
	bool found = false;

	if (id.Length() > 0) {
		int32 children = fInfo.size();
		for (int32 i = 0; i < children; i++) {
			if (id == fInfo[i]->MessageID()) {
				NotificationView* oldView = fInfo[i];
				if(view->ProgressPercent() != oldView->ProgressPercent()
					&& view->ProgressPercent() >= 0) {
					system_beep("Notification progress");
				}
				oldView->RemoveSelf();
				delete oldView;
				fInfo[i] = view;
				found = true;
				break;
			}
		}
	}

	// Invalidate all children to show or hide the close buttons in the
	// notification view
	int32 children = fInfo.size();
	for (int32 i = 0; i < children; i++) {
		fInfo[i]->Invalidate();
	}

	if (!found) {
		if(view->ProgressPercent() >= 0)
			system_beep("Notification progress");
		fInfo.push_back(view);
	}
	GetLayout()->AddView(view);

	if (IsHidden())
		Show();
	if (view->IsHidden(view) && !fCollapsed)
		view->Show();
}


/**
 * @brief Enables or disables preview mode, which suppresses mouse interaction.
 *
 * @param enabled @c true to enter preview mode, @c false to leave it.
 */
void
AppGroupView::SetPreviewModeOn(bool enabled)
{
	fPreviewModeOn = enabled;
}


/**
 * @brief Returns the group label string.
 *
 * @return A reference to the group name.
 */
const BString&
AppGroupView::Group() const
{
	return fLabel;
}


/**
 * @brief Changes the group label and invalidates the header for redrawing.
 *
 * @param group The new group name.
 */
void
AppGroupView::SetGroup(const char* group)
{
	fLabel.SetTo(group);
	Invalidate();
}


/**
 * @brief Returns whether this group contains any notification views.
 *
 * @return @c true if at least one child notification exists, @c false otherwise.
 */
bool
AppGroupView::HasChildren()
{
	return !fInfo.empty();
}


/**
 * @brief Returns the number of notification views in this group.
 *
 * @return The child count.
 */
int32
AppGroupView::ChildrenCount()
{
	return fInfo.size();
}
