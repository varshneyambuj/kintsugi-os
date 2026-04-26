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
 *   Copyright 2020, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file InputIcons.cpp
 * @brief Implementation of InputIcons, a small bundle of preset BBitmaps.
 *
 * Loads the mouse, touchpad, and keyboard vector icons from the
 * application's executable resources at construction and rasterises them
 * into BBitmap members sized for the device list view.
 */


#include "InputIcons.h"

#include <Application.h>
#include <ControlLook.h>
#include <File.h>
#include <IconUtils.h>
#include <Resources.h>
#include <Roster.h>

#include "IconHandles.h"


/** @brief Cached icon size (computed once on first construction). */
const BRect InputIcons::sBounds;


/**
 * @brief Loads the vector icon resources and rasterises them into BBitmaps.
 *
 * Computes the cached @ref sBounds once, opens the application executable
 * via BRoster, preloads the vector-icon resource type, then defers to
 * _LoadBitmap() for the per-icon rasterisation.
 */
InputIcons::InputIcons()
	:
	mouseIcon(NULL, false),
	touchpadIcon(NULL, false),
	keyboardIcon(NULL, false)
{
	if (!sBounds.IsValid()) {
		*const_cast<BRect*>(&sBounds) = BRect(BPoint(0, 0),
			be_control_look->ComposeIconSize(B_MINI_ICON));
	}

	app_info info;
	be_app->GetAppInfo(&info);
	BFile executableFile(&info.ref, B_READ_ONLY);
	BResources resources(&executableFile);
	resources.PreloadResourceType(B_VECTOR_ICON_TYPE);

	_LoadBitmap(&resources);
}


/**
 * @brief Rasterises the three named vector icons into BBitmap members.
 *
 * Looks up the "mouse_icon", "touchpad_icon", and "keyboard_icon" entries
 * from @a resources and hands each to BIconUtils::GetVectorIcon. Missing
 * icons are silently skipped, leaving the corresponding BBitmap member
 * default-initialised.
 *
 * @param resources  BResources opened from the application executable.
 */
void
InputIcons::_LoadBitmap(BResources* resources)
{
	const uint8* mouse;
	const uint8* touchpad;
	const uint8* keyboard;

	size_t size;

	mouse = (const uint8*)resources->LoadResource(
		B_VECTOR_ICON_TYPE, "mouse_icon", &size);
	if (mouse) {
		mouseIcon = new BBitmap(sBounds, 0, B_RGBA32);
		BIconUtils::GetVectorIcon(mouse, size, &mouseIcon);
	}

	touchpad = (const uint8*)resources->LoadResource(
		B_VECTOR_ICON_TYPE, "touchpad_icon", &size);
	if (touchpad) {
		touchpadIcon = new BBitmap(sBounds, 0, B_RGBA32);
		BIconUtils::GetVectorIcon(touchpad, size, &touchpadIcon);
	}

	keyboard = (const uint8*)resources->LoadResource(
		B_VECTOR_ICON_TYPE, "keyboard_icon", &size);
	if (keyboard) {
		keyboardIcon = new BBitmap(sBounds, 0, B_RGBA32);
		BIconUtils::GetVectorIcon(keyboard, size, &keyboardIcon);
	}
}


/**
 * @brief Returns the icon's bounding rectangle offset to a screen point.
 *
 * @param topLeft  Origin (top-left corner) for the resulting rectangle.
 * @return BRect with the cached icon size positioned at @a topLeft.
 */
BRect
InputIcons::IconRectAt(const BPoint& topLeft)
{
	return BRect(sBounds).OffsetToSelf(topLeft);
}
