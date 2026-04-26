/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2009, Oliver Ruiz Dorantes.
 */

/** @file DeviceListItem.h
    @brief Declares Bluetooth::DeviceListItem, a custom BListItem for remote devices. */

#ifndef DEVICELISTITEM_H_
#define DEVICELISTITEM_H_

#include <ListItem.h>
#include <String.h>

#include "bluetooth/RemoteDevice.h"

namespace Bluetooth {


/**
 * @brief Custom BListItem rendering a discovered remote device.
 *
 * Caches the device's BD_ADDR, class of device, and friendly name so that
 * drawing during an inquiry does not block on kit calls.
 */
class DeviceListItem : public BListItem
{
	public:
		DeviceListItem(RemoteDevice*	bDevice);

		~DeviceListItem();

		void DrawItem(BView*, BRect, bool = false);
		void Update(BView* owner, const BFont* font);

		static int Compare(const void* firstArg, const void* secondArg);
		void SetDevice(RemoteDevice* bDevice);
		RemoteDevice* Device() const;

	private:
		RemoteDevice*	fDevice;
		bdaddr_t			fAddress;
		DeviceClass			fClass;
		BString				fName;
		int32				fRSSI;

};

}


#endif
