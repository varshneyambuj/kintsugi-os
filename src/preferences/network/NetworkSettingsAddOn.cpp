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
 *   Copyright 2004-2015 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file NetworkSettingsAddOn.cpp
 * @brief Base classes for pluggable network settings entries used by the
 *        Network preflet.
 *
 * Defines the runtime contracts shared by every network settings add-on:
 *   - BNetworkSettingsItem: a single configurable item shown in the preflet.
 *   - BNetworkSettingsInterfaceItem: an item bound to a named interface.
 *   - BNetworkInterfaceListItem: a BListItem renderer for interfaces.
 *   - BNetworkSettingsAddOn: the host loaded from an image_id.
 *
 * Add-ons are discovered, instantiated, and embedded in NetworkWindow.
 *
 * @see NetworkWindow, BNetworkSettings
 */


#include <NetworkSettingsAddOn.h>

#include <stdio.h>
#include <stdlib.h>

#include <ControlLook.h>
#include <NetworkAddress.h>
#include <NetworkInterface.h>
#include <NetworkSettings.h>

#include "NetworkWindow.h"


using namespace BNetworkKit;


/**
 * @brief Constructs an unbound settings item with no associated profile.
 */
BNetworkSettingsItem::BNetworkSettingsItem()
	:
	fProfile(NULL)
{
}


/**
 * @brief Destructor. Subclasses release any owned UI state.
 */
BNetworkSettingsItem::~BNetworkSettingsItem()
{
}


/**
 * @brief Notifies the item that the active profile has changed.
 *
 * Default implementation simply caches the new profile pointer; subclasses
 * may override to repopulate fields from the profile.
 *
 * @param newProfile  Profile that should now back this item; may be NULL.
 * @return Always B_OK.
 */
status_t
BNetworkSettingsItem::ProfileChanged(const BNetworkProfile* newProfile)
{
	fProfile = newProfile;
	return B_OK;
}


/**
 * @brief Returns the profile currently bound to this item.
 *
 * @return Cached profile pointer, or NULL if no profile is set.
 */
const BNetworkProfile*
BNetworkSettingsItem::Profile() const
{
	return fProfile;
}


/**
 * @brief Hook invoked when generic settings of @a type change.
 *
 * Default implementation is a no-op; subclasses override to react to
 * specific settings types.
 *
 * @param type  Settings category that changed.
 */
void
BNetworkSettingsItem::SettingsUpdated(uint32 type)
{
}


/**
 * @brief Hook invoked when a runtime configuration message arrives.
 *
 * Default implementation is a no-op.
 *
 * @param message  Configuration update payload.
 */
void
BNetworkSettingsItem::ConfigurationUpdated(const BMessage& message)
{
}


/**
 * @brief Posts a kMsgSettingsItemUpdated notification to the network window.
 *
 * Subclasses call this after mutating their underlying settings so the
 * NetworkWindow can repaint dependent UI.
 *
 * @note Sends asynchronously; does not wait for acknowledgement.
 */
void
BNetworkSettingsItem::NotifySettingsUpdated()
{
	// TODO: post to network window
	BMessage updated(kMsgSettingsItemUpdated);
	updated.AddPointer("item", this);
	gNetworkWindow.SendMessage(&updated);
}


// #pragma mark -


/**
 * @brief Constructs an interface-scoped settings item.
 *
 * @param interface  Name of the interface this item configures (e.g. "/eth0").
 */
BNetworkSettingsInterfaceItem::BNetworkSettingsInterfaceItem(
	const char* interface)
	:
	fInterface(interface)
{
}


/**
 * @brief Destructor.
 */
BNetworkSettingsInterfaceItem::~BNetworkSettingsInterfaceItem()
{
}


/**
 * @brief Reports the settings type of this item.
 *
 * @return Always B_NETWORK_SETTINGS_TYPE_INTERFACE.
 */
BNetworkSettingsType
BNetworkSettingsInterfaceItem::Type() const
{
	return B_NETWORK_SETTINGS_TYPE_INTERFACE;
}


/**
 * @brief Returns the interface this item is bound to.
 *
 * @return Interface name string owned by this item.
 */
const char*
BNetworkSettingsInterfaceItem::Interface() const
{
	return fInterface;
}


// #pragma mark -


/**
 * @brief Constructs a list item describing one address family on a network
 *        interface.
 *
 * @param family     Address family (AF_INET, AF_INET6, ...).
 * @param interface  Interface name.
 * @param label      Human-readable label drawn next to the address.
 * @param settings   Reference to the live network settings the row reads.
 */
BNetworkInterfaceListItem::BNetworkInterfaceListItem(int family,
	const char* interface, const char* label, BNetworkSettings& settings)
	:
	fSettings(settings),
	fFamily(family),
	fInterface(interface),
	fLabel(label),
	fDisabled(false),
	fLineOffset(0),
	fSpacing(0)
{
}


/**
 * @brief Destructor.
 */
BNetworkInterfaceListItem::~BNetworkInterfaceListItem()
{
}


/**
 * @brief Returns the textual label drawn for this row.
 *
 * @return Label string passed in at construction time.
 */
const char*
BNetworkInterfaceListItem::Label() const
{
	return fLabel;
}


/**
 * @brief Renders the list row into @a owner.
 *
 * Draws the selection background (if any), the label in the appropriate
 * list-item color, and a parenthesized italic address suffix when one is
 * available.
 *
 * @param owner     View receiving the drawing operations.
 * @param bounds    Rectangle to fill / draw into.
 * @param complete  When true, fill the background even if not selected.
 */
void
BNetworkInterfaceListItem::DrawItem(BView* owner, BRect bounds, bool complete)
{
	owner->PushState();

	if (IsSelected() || complete) {
		if (IsSelected()) {
			owner->SetHighColor(ui_color(B_LIST_SELECTED_BACKGROUND_COLOR));
			owner->SetLowColor(owner->HighColor());
		} else
			owner->SetHighColor(owner->LowColor());

		owner->FillRect(bounds);
	}

	// Set the initial bounds of item contents
	BPoint labelLocation = bounds.LeftTop() + BPoint(fSpacing, fLineOffset);

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

	owner->SetFont(be_plain_font);
	owner->DrawString(fLabel, labelLocation);

	if (!fAddress.IsEmpty()) {
		BFont font = _AddressFont();
		owner->MovePenBy(fSpacing, 0);
		owner->SetFont(&font);
		owner->DrawString(fAddress);
	}

	owner->PopState();
}


/**
 * @brief Recomputes layout metrics (line height, width, spacing) for the row.
 *
 * Called by BListView before drawing whenever the font or list state may
 * have changed.
 *
 * @param owner  View this item belongs to.
 * @param font   Font used for measuring the label.
 */
void
BNetworkInterfaceListItem::Update(BView* owner, const BFont* font)
{
	_UpdateState();

	fSpacing = be_control_look->DefaultLabelSpacing();

	BListItem::Update(owner, font);
	font_height height;
	font->GetHeight(&height);

	float lineHeight = ceilf(height.ascent) + ceilf(height.descent)
		+ ceilf(height.leading);
	fLineOffset = 2 + ceilf(height.ascent + height.leading / 2);

	SetWidth(owner->StringWidth(fLabel) + 2 * fSpacing
		+ _AddressFont().StringWidth(fAddress.String()));
	SetHeight(lineHeight + 4);
}


/**
 * @brief Refreshes cached state when the underlying interface changes.
 *
 * @param message  Configuration message describing the change (unused).
 */
void
BNetworkInterfaceListItem::ConfigurationUpdated(const BMessage& message)
{
	_UpdateState();
}


/**
 * @brief Builds the italic, slightly smaller font used for the address suffix.
 *
 * @return Configured BFont (returned by value).
 */
BFont
BNetworkInterfaceListItem::_AddressFont()
{
	BFont font;
	font.SetFace(B_ITALIC_FACE);
	font.SetSize(font.Size() * 0.9f);
	return font;
}


/**
 * @brief Re-reads the interface state and updates the cached label/address.
 *
 * Marks the row disabled when neither auto-configuration nor a static
 * address is available.
 */
void
BNetworkInterfaceListItem::_UpdateState()
{
	BNetworkInterfaceAddress address;
	BNetworkInterface interface(fInterface);

	bool autoConfigure = fSettings.Interface(fInterface).IsAutoConfigure(
		fFamily);

	fAddress = "";
	fDisabled = !autoConfigure;

	int32 index = interface.FindFirstAddress(fFamily);
	if (index < 0)
		return;

	interface.GetAddressAt(index, address);

	fDisabled = address.Address().IsEmpty() && !autoConfigure;
	if (!address.Address().IsEmpty())
		fAddress << "(" << address.Address().ToString() << ")";
}


// #pragma mark -


/**
 * @brief Constructs an add-on tied to a loaded image and the host settings.
 *
 * @param image     image_id of the loaded add-on; used to load resources.
 * @param settings  Reference to the live BNetworkSettings the add-on edits.
 */
BNetworkSettingsAddOn::BNetworkSettingsAddOn(image_id image,
	BNetworkSettings& settings)
	:
	fImage(image),
	fResources(NULL),
	fSettings(settings)
{
}


/**
 * @brief Destructor. Releases the lazily loaded resource set.
 */
BNetworkSettingsAddOn::~BNetworkSettingsAddOn()
{
	delete fResources;
}


/**
 * @brief Iterates interface-scoped items the add-on contributes.
 *
 * Default implementation provides none.
 *
 * @param cookie     Iteration state owned by the caller; updated in place.
 * @param interface  Interface to enumerate items for.
 * @return Newly allocated item, or NULL when iteration is complete.
 */
BNetworkSettingsInterfaceItem*
BNetworkSettingsAddOn::CreateNextInterfaceItem(uint32& cookie,
	const char* interface)
{
	return NULL;
}


/**
 * @brief Iterates non-interface-scoped items the add-on contributes.
 *
 * Default implementation provides none.
 *
 * @param cookie  Iteration state owned by the caller; updated in place.
 * @return Newly allocated item, or NULL when iteration is complete.
 */
BNetworkSettingsItem*
BNetworkSettingsAddOn::CreateNextItem(uint32& cookie)
{
	return NULL;
}


/**
 * @brief Returns the image_id this add-on was loaded from.
 *
 * @return image_id passed at construction.
 */
image_id
BNetworkSettingsAddOn::Image()
{
	return fImage;
}


/**
 * @brief Lazily opens and returns the BResources bundled with the add-on
 *        binary.
 *
 * The resource handle is cached for the add-on's lifetime.
 *
 * @return Pointer to the BResources, or NULL if loading failed.
 */
BResources*
BNetworkSettingsAddOn::Resources()
{
	if (fResources == NULL) {
		image_info info;
		if (get_image_info(fImage, &info) != B_OK)
			return NULL;

		BResources* resources = new BResources();
		BFile file(info.name, B_READ_ONLY);
		if (resources->SetTo(&file) == B_OK)
			fResources = resources;
		else
			delete resources;
	}
	return fResources;
}


/**
 * @brief Returns the live settings reference passed at construction.
 *
 * @return Reference to the host's BNetworkSettings.
 */
BNetworkSettings&
BNetworkSettingsAddOn::Settings()
{
	return fSettings;
}
