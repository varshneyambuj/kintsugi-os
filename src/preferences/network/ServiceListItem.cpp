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
 *       Axel Dörfler, <axeld@pinc-software.de>
 */


/**
 * @file ServiceListItem.cpp
 * @brief Implementation of ServiceListItem, the row renderer used by the
 *        Network preflet's Services list.
 *
 * Each row paints the service's label and a right-aligned, dimmed-when-off
 * "on"/"off" badge. The row repaints when net_server reports a service
 * settings update.
 */


#include "ServiceListItem.h"

#include <Catalog.h>
#include <ControlLook.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ServiceListItem"


/** @brief Translation-marked label drawn when the service is running. */
static const char* kEnabledState = B_TRANSLATE_MARK("on");
/** @brief Translation-marked label drawn when the service is stopped. */
static const char* kDisabledState = B_TRANSLATE_MARK("off");


/**
 * @brief Constructs a service row.
 *
 * @param name      Service identifier (matches the settings key).
 * @param label     Human-readable label drawn in the row.
 * @param settings  Reference to the live settings used to query the
 *                  service's running state.
 */
ServiceListItem::ServiceListItem(const char* name, const char* label,
	const BNetworkSettings& settings)
	:
	fName(name),
	fLabel(label),
	fSettings(settings),
	fOwner(NULL),
	fLineOffset(0),
	fEnabled(false)
{
}


/**
 * @brief Destructor.
 */
ServiceListItem::~ServiceListItem()
{
}


/**
 * @brief Renders the row.
 *
 * Draws the selection background (if any), the service label on the left,
 * and a right-aligned "on"/"off" badge. The badge is drawn dimmer when the
 * service is disabled.
 *
 * @param owner     View receiving the drawing operations.
 * @param bounds    Rectangle to fill / draw into.
 * @param complete  When true, repaint the entire background.
 */
void
ServiceListItem::DrawItem(BView* owner, BRect bounds, bool complete)
{
	owner->PushState();

	rgb_color lowColor = owner->LowColor();

	if (IsSelected() || complete) {
		if (IsSelected()) {
			owner->SetHighColor(ui_color(B_LIST_SELECTED_BACKGROUND_COLOR));
			owner->SetLowColor(owner->HighColor());
		} else
			owner->SetHighColor(lowColor);

		owner->FillRect(bounds);
	}

	const char* stateText = fEnabled ? B_TRANSLATE(kEnabledState)
		: B_TRANSLATE(kDisabledState);

	// Set the initial bounds of item contents
	BPoint statePoint = bounds.RightTop() + BPoint(0, fLineOffset)
		- BPoint(be_plain_font->StringWidth(stateText)
			+ be_control_look->DefaultLabelSpacing(), 0);
	BPoint namePoint = bounds.LeftTop()
		+ BPoint(be_control_look->DefaultLabelSpacing(), fLineOffset);

	owner->SetDrawingMode(B_OP_OVER);

	rgb_color textColor;
	if (IsSelected())
		textColor = ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR);
	else
		textColor = ui_color(B_LIST_ITEM_TEXT_COLOR);

	owner->SetHighColor(textColor);
	owner->DrawString(fLabel, namePoint);

	if (!fEnabled) {
		if (textColor.red + textColor.green + textColor.blue > 128 * 3)
			owner->SetHighColor(tint_color(textColor, B_DARKEN_1_TINT));
		else
			owner->SetHighColor(tint_color(textColor, B_LIGHTEN_1_TINT));
	}
	owner->DrawString(stateText, statePoint);

	owner->PopState();
}


/**
 * @brief Recomputes layout metrics, re-reads the enabled flag, and sizes
 *        the row to fit label and badge.
 *
 * @param owner  View this item belongs to.
 * @param font   Font used for measuring text.
 */
void
ServiceListItem::Update(BView* owner, const BFont* font)
{
	fOwner = owner;
	fEnabled = IsEnabled();

	BListItem::Update(owner, font);
	font_height height;
	font->GetHeight(&height);

	fLineOffset = 2 + ceilf(height.ascent + height.leading / 2);

	float maxStateWidth = std::max(font->StringWidth(B_TRANSLATE(kEnabledState)),
		font->StringWidth(B_TRANSLATE(kDisabledState)));
	SetWidth(font->StringWidth(fLabel)
		+ 3 * be_control_look->DefaultLabelSpacing() + maxStateWidth);
	SetHeight(4 + ceilf(height.ascent + height.leading + height.descent));
}


/**
 * @brief BNetworkSettingsListener hook. Refreshes enabled state and asks
 *        the owning view to repaint when the badge needs to flip.
 *
 * @param type  Settings category that changed; only service updates are
 *              acted upon.
 */
void
ServiceListItem::SettingsUpdated(uint32 type)
{
	if (type == BNetworkSettings::kMsgServiceSettingsUpdated) {
		bool wasEnabled = fEnabled;
		fEnabled = IsEnabled();
		if (wasEnabled != fEnabled)
			fOwner->Invalidate();
	}
}


/**
 * @brief Reports whether the underlying service is currently running.
 *
 * @return true if the service is enabled and live.
 */
bool
ServiceListItem::IsEnabled()
{
	return fSettings.Service(fName).IsRunning();
}
