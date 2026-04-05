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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2021 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Tri-Edge AI <triedgeai@gmail.com>
 */


/**
 * @file BluetoothIconView.cpp
 * @brief Implementation of BluetoothIconView, a shared Bluetooth icon widget
 *
 * BluetoothIconView displays the system Bluetooth icon as a BBitmap in a
 * BView. The bitmap is reference-counted and shared among all instances to
 * minimize memory usage in dialogs that display the Bluetooth icon.
 *
 * @see ConnectionView, ConnectionIncoming
 */


#include <BluetoothIconView.h>

#include <stdio.h>

namespace Bluetooth {

/** @brief Shared BBitmap holding the Bluetooth server vector icon, lazily created on first use. */
BBitmap* 	BluetoothIconView::fBitmap = NULL;

/** @brief Reference count tracking how many BluetoothIconView instances share fBitmap. */
int32		BluetoothIconView::fRefCount = 0;

/**
 * @brief Construct a BluetoothIconView and initialise the shared icon bitmap.
 *
 * Creates an 80x80 BView with alpha-blending enabled. On the first
 * instantiation (@c fRefCount == 0) the Bluetooth server's vector icon is
 * loaded via @c BMimeType and rasterised into a 64x64 @c B_RGBA32 BBitmap
 * that is shared by all subsequent instances. Every constructor call
 * increments @c fRefCount to track shared ownership of the bitmap.
 *
 * @see BluetoothIconView::~BluetoothIconView(), BluetoothIconView::Draw()
 */
BluetoothIconView::BluetoothIconView()
	:
	BView(BRect(0, 0, 80, 80), "", B_FOLLOW_ALL, B_WILL_DRAW)
{
	if (fRefCount == 0) {
		fBitmap = new BBitmap(BRect(0, 0, 64, 64), 0, B_RGBA32);

		uint8* tempIcon;
		size_t tempSize;

		BMimeType mime("application/x-vnd.Haiku-bluetooth_server");
		mime.GetIcon(&tempIcon, &tempSize);

		BIconUtils::GetVectorIcon(tempIcon, tempSize, fBitmap);

		fRefCount++;
	} else {
		fRefCount++;
	}

	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
}


/**
 * @brief Destroy the BluetoothIconView and release the shared icon bitmap when no longer needed.
 *
 * Decrements the shared reference count. When the count reaches zero the
 * shared @c fBitmap is deleted, freeing the rasterised icon memory.
 *
 * @see BluetoothIconView::BluetoothIconView()
 */
BluetoothIconView::~BluetoothIconView()
{
	fRefCount--;

	if (fRefCount <= 0)
		delete fBitmap;
}


/**
 * @brief Draw the Bluetooth icon bitmap into the view.
 *
 * Renders the shared @c fBitmap at the view's origin using the alpha-overlay
 * blending mode established in the constructor.
 *
 * @param rect The update rectangle passed by the rendering system (unused;
 *             the entire bitmap is always redrawn).
 */
void
BluetoothIconView::Draw(BRect rect)
{
	this->DrawBitmap(fBitmap);
}

}
