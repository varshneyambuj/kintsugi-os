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
 *   Copyright 2004-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Alexander von Gluck IV, kallisti5@unixzen.com
 *       Philippe Houdoin
 *       Fredrik Modéen
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file InterfaceListItem.cpp
 * @brief Implementation of InterfaceListItem, the rich row renderer used
 *        by the Network preflet's interface list.
 *
 * Each row paints a vector icon overlaid with a state badge (offline /
 * pending / online), the bold device name, a small "no link" / "connected"
 * status string aligned to the right edge, and a media-type subtitle on
 * the second line. The renderer reacts to configuration updates posted by
 * net_server so it can repaint when the link state changes.
 */


#include "InterfaceListItem.h"

#include <algorithm>

#include <Application.h>
#include <Bitmap.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <IconUtils.h>
#include <OutlineListView.h>
#include <Resources.h>
#include <String.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "InterfaceListItem"


/**
 * @brief Constructs a list row for the named interface.
 *
 * Loads the appropriate icon set based on @a type and resolves the
 * BNetworkInterface so subsequent updates can query its state.
 *
 * @param name  Device name (e.g. "/dev/net/eth0").
 * @param type  Family of interface used to choose icons and subtitles.
 */
InterfaceListItem::InterfaceListItem(const char* name,
	BNetworkInterfaceType type)
	:
	BListItem(0, false),
	fType(type),
	fIcon(NULL),
	fIconOffline(NULL),
	fIconPending(NULL),
	fIconOnline(NULL),
	fFirstLineOffset(0),
	fLineOffset(0),
	fDisabled(false),
	fHasLink(false),
	fConnecting(false)
{
	fInterface.SetTo(name);
	_Init();
}


/**
 * @brief Destructor. Frees all owned icon bitmaps.
 */
InterfaceListItem::~InterfaceListItem()
{
	delete fIcon;
	delete fIconOffline;
	delete fIconPending;
	delete fIconOnline;
}


// #pragma mark - InterfaceListItem public methods


/**
 * @brief Renders the two-line row inside @a owner.
 *
 * Composes a selection background, the interface bitmap with a state icon
 * overlay, the bold device name on the first line, the right-aligned state
 * text, and the subtitle on the second line. When the interface is
 * disabled, the icon and label are drawn with reduced contrast.
 *
 * @param owner     View receiving the drawing operations.
 * @param bounds    Rectangle to fill / draw into.
 * @param complete  When true, repaint the entire background (not just the
 *                  selection rectangle).
 */
void
InterfaceListItem::DrawItem(BView* owner, BRect bounds, bool complete)
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

	BBitmap* stateIcon = _StateIcon();
	const int32 stateIconWidth = stateIcon->Bounds().IntegerWidth() + 1;
	const char* stateText = _StateText();

	// Set the initial bounds of item contents
	BPoint iconPoint = bounds.LeftTop()
		+ BPoint(be_control_look->DefaultLabelSpacing(), 2);
	BPoint statePoint = bounds.RightTop() + BPoint(0, fFirstLineOffset)
		- BPoint(be_plain_font->StringWidth(stateText)
			+ be_control_look->DefaultLabelSpacing(), 0);
	BPoint namePoint = bounds.LeftTop()
		+ BPoint(stateIconWidth	+ (be_control_look->DefaultLabelSpacing() * 2),
			fFirstLineOffset);

	if (fDisabled) {
		owner->SetDrawingMode(B_OP_ALPHA);
		owner->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
		owner->SetHighColor(0, 0, 0, 32);
	} else
		owner->SetDrawingMode(B_OP_OVER);

	owner->DrawBitmapAsync(fIcon, iconPoint);
	owner->DrawBitmapAsync(stateIcon, iconPoint);

	if (fDisabled) {
		rgb_color textColor;
		if (IsSelected())
			textColor = ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR);
		else
			textColor = ui_color(B_LIST_ITEM_TEXT_COLOR);

		if (textColor.red + textColor.green + textColor.blue > 128 * 3)
			owner->SetHighColor(tint_color(textColor, B_DARKEN_1_TINT));
		else
			owner->SetHighColor(tint_color(textColor, B_LIGHTEN_1_TINT));
	} else {
		if (IsSelected())
			owner->SetHighColor(ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR));
		else
			owner->SetHighColor(ui_color(B_LIST_ITEM_TEXT_COLOR));
	}

	owner->SetFont(be_bold_font);

	owner->DrawString(fDeviceName, namePoint);
	owner->SetFont(be_plain_font);
	owner->DrawString(stateText, statePoint);

	BPoint linePoint = bounds.LeftTop()
		+ BPoint(stateIconWidth + (be_control_look->DefaultLabelSpacing() * 2),
		fFirstLineOffset + fLineOffset);
	owner->DrawString(fSubtitle, linePoint);

	owner->PopState();
}


/**
 * @brief Recomputes layout metrics and width/height to match the current
 *        font and interface state.
 *
 * @param owner  View this item belongs to.
 * @param font   Font used for measuring labels.
 */
void
InterfaceListItem::Update(BView* owner, const BFont* font)
{
	BListItem::Update(owner, font);
	font_height height;
	font->GetHeight(&height);

	float lineHeight = ceilf(height.ascent) + ceilf(height.descent)
		+ ceilf(height.leading);

	fFirstLineOffset = 2 + ceilf(height.ascent + height.leading / 2);
	fLineOffset = lineHeight;

	_UpdateState();

	SetWidth(fIcon->Bounds().Width() + 36
		+ be_control_look->DefaultLabelSpacing()
		+ be_bold_font->StringWidth(fDeviceName.String())
		+ owner->StringWidth(_StateText()));
	SetHeight(std::max(2 * lineHeight + 4, fIcon->Bounds().Height() + 4));
		// either to the text height or icon height, whichever is taller
}


/**
 * @brief BNetworkConfigurationListener hook: refreshes cached state after
 *        a configuration change so the next paint reflects it.
 *
 * @param message  Configuration message describing the change (unused).
 */
void
InterfaceListItem::ConfigurationUpdated(const BMessage& message)
{
	_UpdateState();
}


// #pragma mark - InterfaceListItem private methods


/**
 * @brief Selects an icon set based on the interface type and loads bitmaps.
 */
void
InterfaceListItem::_Init()
{
	switch (fType) {
		case B_NETWORK_INTERFACE_TYPE_WIFI:
			_PopulateBitmaps("wifi");
			break;
		case B_NETWORK_INTERFACE_TYPE_ETHERNET:
			_PopulateBitmaps("ether");
			break;
		case B_NETWORK_INTERFACE_TYPE_VPN:
			_PopulateBitmaps("vpn");
			break;
		case B_NETWORK_INTERFACE_TYPE_DIAL_UP:
			_PopulateBitmaps("dialup");
			break;
		default:
			_PopulateBitmaps(NULL);
			break;
	}
}


/**
 * @brief Loads the main interface icon and the offline/pending/online state
 *        badges from the application's resources.
 *
 * The lookup falls back from the per-interface name to @a mediaType to a
 * generic "ether" icon. State badges are loaded by their fixed names.
 *
 * @param mediaType  Resource hint such as "wifi", "ether", or NULL.
 */
void
InterfaceListItem::_PopulateBitmaps(const char* mediaType)
{
	const uint8* interfaceHVIF;
	const uint8* offlineHVIF;
	const uint8* pendingHVIF;
	const uint8* onlineHVIF;

	BBitmap* interfaceBitmap = NULL;
	BBitmap* offlineBitmap = NULL;
	BBitmap* pendingBitmap = NULL;
	BBitmap* onlineBitmap = NULL;

	BResources* resources = BApplication::AppResources();

	size_t iconDataSize;

	// Try specific interface icon?
	interfaceHVIF = (const uint8*)resources->LoadResource(
		B_VECTOR_ICON_TYPE, Name(), &iconDataSize);

	if (interfaceHVIF == NULL && mediaType != NULL)
		// Not found, try interface media type?
		interfaceHVIF = (const uint8*)resources->LoadResource(
			B_VECTOR_ICON_TYPE, mediaType, &iconDataSize);
	if (interfaceHVIF == NULL)
		// Not found, try default interface icon?
		interfaceHVIF = (const uint8*)resources->LoadResource(
			B_VECTOR_ICON_TYPE, "ether", &iconDataSize);

	const BSize iconSize = be_control_look->ComposeIconSize(B_LARGE_ICON);
	if (interfaceHVIF != NULL) {
		// Now build the bitmap
		interfaceBitmap = new(std::nothrow) BBitmap(
			BRect(BPoint(0, 0), iconSize), 0, B_RGBA32);
		if (BIconUtils::GetVectorIcon(interfaceHVIF,
				iconDataSize, interfaceBitmap) == B_OK)
			fIcon = interfaceBitmap;
		else
			delete interfaceBitmap;
	}

	// Load possible state icons
	offlineHVIF = (const uint8*)resources->LoadResource(
		B_VECTOR_ICON_TYPE, "offline", &iconDataSize);

	if (offlineHVIF != NULL) {
		offlineBitmap = new(std::nothrow) BBitmap(
			BRect(BPoint(0, 0), iconSize), 0, B_RGBA32);
		if (BIconUtils::GetVectorIcon(offlineHVIF,
				iconDataSize, offlineBitmap) == B_OK)
			fIconOffline = offlineBitmap;
		else
			delete offlineBitmap;
	}

	pendingHVIF = (const uint8*)resources->LoadResource(
		B_VECTOR_ICON_TYPE, "pending", &iconDataSize);

	if (pendingHVIF != NULL) {
		pendingBitmap = new(std::nothrow) BBitmap(
			BRect(BPoint(0, 0), iconSize), 0, B_RGBA32);
		if (BIconUtils::GetVectorIcon(pendingHVIF,
				iconDataSize, pendingBitmap) == B_OK)
			fIconPending = pendingBitmap;
		else
			delete pendingBitmap;
	}

	onlineHVIF = (const uint8*)resources->LoadResource(
		B_VECTOR_ICON_TYPE, "online", &iconDataSize);

	if (onlineHVIF != NULL) {
		onlineBitmap = new(std::nothrow) BBitmap(
			BRect(BPoint(0, 0), iconSize), 0, B_RGBA32);
		if (BIconUtils::GetVectorIcon(onlineHVIF,
				iconDataSize, onlineBitmap) == B_OK)
			fIconOnline = onlineBitmap;
		else
			delete onlineBitmap;
	}
}


/**
 * @brief Refreshes cached display fields (device name, link state, subtitle)
 *        from the live BNetworkInterface.
 *
 * @note Dial-up and VPN interfaces are never marked disabled here; their
 *       enabled/disabled state is implied by the connection itself.
 */
void
InterfaceListItem::_UpdateState()
{
	fDeviceName = Name();
	fDeviceName.RemoveFirst("/dev/net/");

	fDisabled = (fInterface.Flags() & IFF_UP) == 0;
	fHasLink = fInterface.HasLink();
	fConnecting = (fInterface.Flags() & IFF_CONFIGURING) != 0;

	switch (fType) {
		case B_NETWORK_INTERFACE_TYPE_WIFI:
			fSubtitle = B_TRANSLATE("Wireless device");
			break;
		case B_NETWORK_INTERFACE_TYPE_ETHERNET:
			fSubtitle = B_TRANSLATE("Ethernet device");
			break;
		case B_NETWORK_INTERFACE_TYPE_DIAL_UP:
			fSubtitle = B_TRANSLATE("Dial-up connection");
			fDisabled = false;
			break;
		case B_NETWORK_INTERFACE_TYPE_VPN:
			fSubtitle = B_TRANSLATE("VPN connection");
			fDisabled = false;
			break;
		default:
			fSubtitle = "";
	}
}


/**
 * @brief Selects which state badge bitmap to overlay on the interface icon.
 *
 * @return Pointer to fIconOffline when disabled or no link, otherwise
 *         fIconOnline.
 * @todo Wire up fIconPending once auto-configuration progress is exposed.
 */
BBitmap*
InterfaceListItem::_StateIcon() const
{
	if (fDisabled)
		return fIconOffline;
	if (!fHasLink)
		return fIconOffline;
	// TODO!
//	} else if ((fSettings->IPAddr(AF_INET).IsEmpty()
//		&& fSettings->IPAddr(AF_INET6).IsEmpty())
//		&& (fSettings->AutoConfigure(AF_INET)
//		|| fSettings->AutoConfigure(AF_INET6))) {
//		interfaceState = "connecting" B_UTF8_ELLIPSIS;
//		stateIcon = fIconPending;

	return fIconOnline;
}


/**
 * @brief Returns the localized status string drawn at the right edge of
 *        the row.
 *
 * VPN and dial-up interfaces map "no link" to "disconnected"; other
 * families use "no link" / "connected" / "disabled".
 *
 * @return Localized C string; lifetime managed by the catalog.
 * @todo Reflect "connecting" while auto-configuration is in progress.
 */
const char*
InterfaceListItem::_StateText() const
{
	if (fDisabled)
		return B_TRANSLATE("disabled");

	if (!fInterface.HasLink()) {
		switch (fType) {
			case B_NETWORK_INTERFACE_TYPE_VPN:
			case B_NETWORK_INTERFACE_TYPE_DIAL_UP:
				return B_TRANSLATE("disconnected");
			default:
				return B_TRANSLATE("no link");
		}
	}

	// TODO!
//	} else if ((fSettings->IPAddr(AF_INET).IsEmpty()
//		&& fSettings->IPAddr(AF_INET6).IsEmpty())
//		&& (fSettings->AutoConfigure(AF_INET)
//		|| fSettings->AutoConfigure(AF_INET6))) {
//		interfaceState = "connecting" B_UTF8_ELLIPSIS;
//		stateIcon = fIconPending;

	return B_TRANSLATE("connected");
}
