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
 * MIT License. Copyright 2001-2006, Haiku.
 * Original authors: Rafael Romo, Stefano Ceccherini (burton666@libero.it),
 *                   Axel Doerfler (axeld@pinc-software.de).
 */

/** @file RefreshSlider.h
    @brief BSlider subclass for entering a custom refresh rate in 0.1 Hz steps. */

#ifndef REFRESH_SLIDER_H
#define REFRESH_SLIDER_H


#include <Slider.h>


/**
 * @brief Slider that exposes a custom refresh rate in 0.1 Hz increments.
 *
 * Internally the slider uses an integer scale of (Hz * 10) to give one
 * decimal of precision; the @c UpdateText() override formats it back to
 * "%.1f Hz" for display.
 */
class RefreshSlider : public BSlider {
	public:
		RefreshSlider(BRect frame, float min, float max, uint32 resizingMode);
		virtual ~RefreshSlider();

		virtual void DrawFocusMark();
		virtual const char* UpdateText() const;
		virtual void KeyDown(const char* bytes, int32 numBytes);

	private:
		char* fStatus;
};

#endif	// REFRESH_SLIDER_H
