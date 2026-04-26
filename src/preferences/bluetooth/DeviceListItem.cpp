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
 *   Copyright 2009, Oliver Ruiz Dorantes,
 *       <oliver.ruiz.dorantes_at_gmail.com>
 *   All rights reserved. Distributed under the terms of the MIT License.
 */


/**
 * @file DeviceListItem.cpp
 * @brief Implementation of Bluetooth::DeviceListItem, a custom BListItem.
 *
 * DeviceListItem renders a single discovered remote device inside the
 * inquiry panel and the main remote-devices BListView. It draws the
 * device-class icon, the friendly name, and a secondary line containing
 * the BD_ADDR plus major/minor class.
 */


#include <Bitmap.h>
#include <View.h>

#include <bluetooth/bdaddrUtils.h>
#include <bluetooth/BluetoothDevice.h>

#include "DeviceListItem.h"

/** @brief Pixel padding inside each list item. */
#define INSETS  5
/** @brief Number of text rows the item reserves vertical space for. */
#define TEXT_ROWS  2

namespace Bluetooth {


/**
 * @brief Constructs a DeviceListItem wrapping a RemoteDevice.
 *
 * Caches the BD_ADDR, the device class, and the cached friendly name from
 * @a bDevice. The cached name is preferred so that drawing during an
 * INQUIRY does not trigger a blocking name request.
 *
 * @param bDevice  Remote device to represent in the list.
 */
DeviceListItem::DeviceListItem(RemoteDevice* bDevice)
	:
	BListItem(),
	fDevice(bDevice)
{
	fAddress = bDevice->GetBluetoothAddress();
	fClass = bDevice->GetDeviceClass();
	// we always use the cached name here as we dont want to fire a query in the middle of an
	// INQUIRY
	fName = bDevice->GetCachedFriendlyName();
	if (fName.IsEmpty())
		fName = "unknown";
}


/**
 * @brief Re-binds the item to a different RemoteDevice and refreshes data.
 *
 * Unlike the constructor, this method calls GetFriendlyName() rather than
 * the cached variant, so it should only be invoked outside the inquiry
 * critical path.
 *
 * @param bDevice  New remote device to represent.
 */
void
DeviceListItem::SetDevice(RemoteDevice* bDevice)
{
	fAddress = bDevice->GetBluetoothAddress();
	fClass = bDevice->GetDeviceClass();
	fName = bDevice->GetFriendlyName();
	// AKAIR rssi we can just have it @ inquiry time...
}


/**
 * @brief Destroys the item.
 *
 * @note Does not delete the wrapped RemoteDevice; ownership stays with
 *       the kit's discovery agent.
 */
DeviceListItem::~DeviceListItem()
{

}


/**
 * @brief Draws the list item into the owning view.
 *
 * Renders the selection background when applicable, lays out the friendly
 * name and the BD_ADDR + class line on two separate rows, and draws the
 * device-class icon at the left edge.
 *
 * @param owner     BView that owns the BListView and provides the drawing
 *                  context.
 * @param itemRect  Rectangle in @a owner-local coordinates the item should
 *                  paint into.
 * @param complete  If true, repaint the entire background even when the
 *                  item is not selected.
 */
void
DeviceListItem::DrawItem(BView* owner, BRect itemRect, bool	complete)
{
	rgb_color	kBlack = { 0, 0, 0, 0 };
	rgb_color	kHighlight = { 156, 154, 156, 0 };

	if (IsSelected() || complete) {
		rgb_color	color;
		if (IsSelected())
			color = kHighlight;
		else
			color = owner->ViewColor();

		owner->SetHighColor(color);
		owner->SetLowColor(color);
		owner->FillRect(itemRect);
		owner->SetHighColor(kBlack);

	} else {
		owner->SetLowColor(owner->ViewColor());
	}

	font_height	finfo;
	be_plain_font->GetHeight(&finfo);

	BPoint point = BPoint(itemRect.left	+ DeviceClass::PixelsForIcon
		+ 2 * INSETS, itemRect.bottom - finfo.descent + 1);
	owner->SetFont(be_fixed_font);
	owner->SetHighColor(kBlack);
	owner->MovePenTo(point);

	BString secondLine;

	secondLine << bdaddrUtils::ToString(fAddress) << "   ";
	fClass.GetMajorDeviceClass(secondLine);
	secondLine << " / ";
	fClass.GetMinorDeviceClass(secondLine);

	owner->DrawString(secondLine.String());

	point -= BPoint(0, (finfo.ascent + finfo.descent + finfo.leading) + INSETS);

	owner->SetFont(be_plain_font);
	owner->MovePenTo(point);
	owner->DrawString(fName.String());

	fClass.Draw(owner, BPoint(itemRect.left, itemRect.top));

#if 0
	switch (fClass.GetMajorDeviceClass()) {
		case 1:
		{
			BRect iconRect(0, 0, 15, 15);
			BBitmap* icon  = new BBitmap(iconRect, B_CMAP8);
			icon->SetBits(kTVBits, kTVWidth * kTVHeight, 0, kTVColorSpace);
			owner->DrawBitmap(icon, iconRect, BRect(itemRect.left + INSETS,
				itemRect.top + INSETS, itemRect.left + INSETS + PIXELS_FOR_ICON,
				itemRect.top + INSETS + PIXELS_FOR_ICON));
			break;
		}
		case 4:
		{
			BRect iconRect(0, 0, 15, 15);
			BBitmap* icon = new BBitmap(iconRect, B_CMAP8);
			icon->SetBits(kMixerBits, kMixerWidth * kMixerHeight, 0, kMixerColorSpace);
			owner->DrawBitmap(icon, iconRect, BRect(itemRect.left + INSETS,
				itemRect.top + INSETS, itemRect.left + INSETS + PIXELS_FOR_ICON,
				itemRect.top + INSETS + PIXELS_FOR_ICON));
			break;
		}
	}
#endif

	owner->SetHighColor(kBlack);

}


/**
 * @brief Recomputes the item height from the current font metrics.
 *
 * Sets the height to the maximum of two text rows plus padding and the
 * height needed to display the device-class icon plus padding.
 *
 * @param owner  BView providing the drawing context.
 * @param font   Font BListView is currently using.
 */
void
DeviceListItem::Update(BView* owner, const BFont* font)
{
	BListItem::Update(owner, font);

	font_height height;
	font->GetHeight(&height);
	SetHeight(MAX((height.ascent + height.descent + height.leading) * TEXT_ROWS
		+ (TEXT_ROWS + 1)*INSETS, DeviceClass::PixelsForIcon + 2 * INSETS));

}


/**
 * @brief Comparison hook used by BListView::SortItems.
 *
 * Orders items by their cached BD_ADDR using bdaddrUtils::Compare so the
 * list is deterministic and duplicates can be detected easily.
 *
 * @param firstArg   Pointer to a pointer to the first DeviceListItem.
 * @param secondArg  Pointer to a pointer to the second DeviceListItem.
 * @return Negative, zero, or positive matching strcmp-style ordering of
 *         the two BD_ADDRs.
 */
int
DeviceListItem::Compare(const void	*firstArg, const void	*secondArg)
{
	const DeviceListItem* item1 = *static_cast<const DeviceListItem* const *>
		(firstArg);
	const DeviceListItem* item2 = *static_cast<const DeviceListItem* const *>
		(secondArg);

	return (int)bdaddrUtils::Compare(item1->fAddress, item2->fAddress);
}


/**
 * @brief Returns the wrapped remote device.
 *
 * @return Pointer to the RemoteDevice owned by the discovery agent.
 */
RemoteDevice*
DeviceListItem::Device() const
{
	return fDevice;
}


}
