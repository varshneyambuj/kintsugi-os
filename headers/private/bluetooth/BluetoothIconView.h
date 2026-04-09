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
 *   Copyright 2021, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Tri-Edge AI <triedgeai@gmail.com>
 */

/** @file BluetoothIconView.h
 *  @brief View that renders the Bluetooth icon using the system MIME type icon. */

#ifndef _BLUETOOTH_ICON_VIEW_H_
#define _BLUETOOTH_ICON_VIEW_H_

#include <View.h>
#include <Bitmap.h>
#include <MimeType.h>
#include <IconUtils.h>

namespace Bluetooth {

/** @brief A BView subclass that displays the Bluetooth application icon,
 *         sharing a single bitmap instance across all view instances via
 *         reference counting. */
class BluetoothIconView : public BView {
public:
	/** @brief Constructs a BluetoothIconView and acquires a reference to the
	 *         shared Bluetooth icon bitmap, loading it from the MIME database
	 *         if this is the first instance. */
							BluetoothIconView();

	/** @brief Destroys the view and releases the shared bitmap reference,
	 *         freeing the bitmap when the last instance is destroyed. */
							~BluetoothIconView();

	/** @brief Draws the Bluetooth icon bitmap within the given update rectangle.
	 *  @param rect The rectangular region that needs to be redrawn. */
	void 				Draw(BRect rect);

private:
	static BBitmap*		fBitmap;
	static int32		fRefCount;
};

}

#endif /* _BLUETOOTH_ICON_VIEW_H_ */
