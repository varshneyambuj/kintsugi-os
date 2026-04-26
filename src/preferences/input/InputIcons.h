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
 * MIT License. Copyright 2020, Haiku, Inc.
 */

/** @file InputIcons.h
    @brief Declares InputIcons, the bundle of icons used by the device list. */

#ifndef __INPUT_ICONS_H
#define __INPUT_ICONS_H


#include <Bitmap.h>


class BResources;


/**
 * @brief Bundle of mini icons for mouse, touchpad, and keyboard rows.
 *
 * Loads its bitmaps from B_VECTOR_ICON_TYPE resources embedded in the
 * Input preferences executable. The icons are stored as BBitmap members
 * so callers can pass &foo.mouseIcon to BView::DrawBitmap directly.
 */
struct InputIcons {
								InputIcons();

			BBitmap				mouseIcon;
			BBitmap				touchpadIcon;
			BBitmap				keyboardIcon;

	static	BRect				IconRectAt(const BPoint& topLeft);

	static	const BRect			sBounds;

private:

			void				_LoadBitmap(BResources* resources);
};

#endif

