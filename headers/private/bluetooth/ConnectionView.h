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
 *   Copyright 2007-2008 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 *   Copyright 2021, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Tri-Edge AI <triedgeai@gmail.com>
 */

/** @file ConnectionView.h
 *  @brief View that displays device name, address, and an animated status
 *         message for an incoming Bluetooth connection dialog. */

#ifndef _CONNECTION_VIEW_H_
#define _CONNECTION_VIEW_H_

#include <Window.h>
#include <View.h>
#include <StringView.h>
#include <GroupLayout.h>
#include <GroupLayoutBuilder.h>
#include <Font.h>
#include <String.h>

namespace Bluetooth {

class BluetoothIconView;

/** @brief A BView subclass that lays out the Bluetooth icon, a status message,
 *         and the remote device's name and address for the connection-incoming
 *         dialog. The status message is animated via Pulse(). */
class ConnectionView : public BView {
public:
	/** @brief Constructs the ConnectionView, populates all child widgets, and
	 *         arranges them using a GroupLayout.
	 *  @param frame   The bounding rectangle of the view.
	 *  @param device  Human-readable name of the remote Bluetooth device.
	 *  @param address Bluetooth address string of the remote device. */
						ConnectionView(BRect frame,
							BString device, BString address);

	/** @brief Called at regular intervals to animate the status message,
	 *         cycling through a waiting indicator. */
	void				Pulse();

private:
	BString				strMessage;
	BluetoothIconView*	fIcon;
	BStringView* 		fMessage;
	BStringView*		fDeviceLabel;
	BStringView*		fDeviceText;
	BStringView*		fAddressLabel;
	BStringView*		fAddressText;
};

}

#endif /* _CONNECTION_VIEW_H_ */
