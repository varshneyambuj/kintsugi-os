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
 * MIT License. Copyright 2011, Haiku.
 * Original authors: Clemens Zeidler.
 */

/** @file MagneticBorder.h
    @brief Snap-to-edge helper that nudges a dragged window's delta toward screen edges. */

#ifndef MAGNETIC_BORDRER_H
#define MAGNETIC_BORDRER_H


#include <Point.h>
#include <Screen.h>


class Screen;
class Window;


/** @brief Stateful helper that adjusts a proposed window-move delta so the
           window snaps to the edges of its screen when dragged close enough,
           with hysteresis to prevent flicker. */
class MagneticBorder {
public:
								MagneticBorder();

			bool				AlterDeltaForSnap(Window* window, BPoint& delta,
									bigtime_t now);
			bool				AlterDeltaForSnap(const Screen* screen,
									BRect& frame, BPoint& delta, bigtime_t now);

private:
			bigtime_t			fLastSnapTime;
};


#endif // MAGNETIC_BORDRER_H
