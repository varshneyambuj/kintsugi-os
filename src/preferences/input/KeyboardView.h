/*
 * Copyright 2026, Kintsugi OS Contributors. All rights reserved.
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
 * MIT License. Copyright 2004-2006, the Haiku project.
 * Original authors (chronological): mccall@digitalparadise.co.uk,
 *                   Jérôme Duval, Marcus Overhagen.
 */

/** @file KeyboardView.h
    @brief Declares KeyboardView, the visual pane of the Keyboard preferences applet. */

#ifndef KEYBOARD_VIEW_H
#define KEYBOARD_VIEW_H


#include <Bitmap.h>
#include <GroupView.h>
#include <InterfaceDefs.h>
#include <Slider.h>
#include <SupportDefs.h>


/**
 * @brief BGroupView containing the Keyboard preferences sliders and decorations.
 *
 * Hosts the key repeat rate slider, the repeat delay slider, a typing
 * test text control, and draws an icon and a clock bitmap alongside the
 * sliders to illustrate what each slider controls.
 */
class KeyboardView : public BGroupView
{
public:
	KeyboardView();
	virtual ~KeyboardView();
	void	Draw(BRect frame);

private:
	BBitmap		*fIconBitmap;
	BBitmap		*fClockBitmap;
	BSlider		*fDelaySlider;
	BSlider		*fRepeatSlider;
};

#endif
