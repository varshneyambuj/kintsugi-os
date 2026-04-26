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
 * MIT License. Copyright 2010, Haiku.
 */

/** @file MediaIcons.h
    @brief Bitmap holder used by Media list items to draw input/output badges. */

#ifndef __MEDIA_ICONS_H
#define __MEDIA_ICONS_H

#include <Bitmap.h>


class BResources;


/**
 * @brief Container for the small CMAP8 icons displayed by the Media preflet.
 *
 * The constructor opens the running application's executable, loads each
 * of the embedded color-mapped bitmaps via @c BResources, and stores them
 * in member fields. List items consult the static @c IconRectAt() helper
 * to position the icons consistently.
 */
struct MediaIcons {
								MediaIcons();

	/**
	 * @brief Pair of bitmaps used to mark a node as default input or output.
	 */
	struct IconSet {
								IconSet();

			BBitmap				inputIcon;
			BBitmap				outputIcon;
	};


			BBitmap				devicesIcon;
			BBitmap				mixerIcon;

			IconSet				audioIcons;
			IconSet				videoIcons;

	/**
	 * @brief Returns a 16x16 BRect anchored at @a topLeft, sized like the
	 *        media icons.
	 */
	static	BRect				IconRectAt(const BPoint& topLeft);

	/** @brief Common bounds shared by every icon (16x16 in CMAP8). */
	static	const BRect			sBounds;

private:

			void				_LoadBitmap(BResources* resources, int32 id,
									BBitmap* bitmap);
};

#endif
