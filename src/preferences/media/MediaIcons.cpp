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
 *   Copyright 2010, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file MediaIcons.cpp
 * @brief Loads the media-list bitmap resources from the running executable.
 */


#include "MediaIcons.h"

#include <Application.h>
#include <File.h>
#include <Resources.h>
#include <Roster.h>

#include "IconHandles.h"


/** @brief Common bounds shared by every icon (16x16 in CMAP8). */
const BRect MediaIcons::sBounds(0, 0, 15, 15);


/**
 * @brief Constructs the input/output pair with the standard icon bounds.
 */
MediaIcons::IconSet::IconSet()
	:
	inputIcon(MediaIcons::sBounds, B_CMAP8),
	outputIcon(MediaIcons::sBounds, B_CMAP8)
{
}



/**
 * @brief Loads every Media preflet icon resource from the executable.
 *
 * Opens the running app's executable as a BResources, preloads the
 * 8-bit color resource type, and dispatches the bitmaps into the
 * matching members. The loader does not validate the resource ids; a
 * missing resource yields an empty bitmap.
 */
MediaIcons::MediaIcons()
	:
	devicesIcon(sBounds, B_CMAP8),
	mixerIcon(sBounds, B_CMAP8)
{
	app_info info;
	be_app->GetAppInfo(&info);
	BFile executableFile(&info.ref, B_READ_ONLY);
	BResources resources(&executableFile);
	resources.PreloadResourceType(B_COLOR_8_BIT_TYPE);

	_LoadBitmap(&resources, devices_icon, &devicesIcon);
	_LoadBitmap(&resources, mixer_icon, &mixerIcon);
	_LoadBitmap(&resources, tv_icon, &videoIcons.outputIcon);
	_LoadBitmap(&resources, cam_icon, &videoIcons.inputIcon);
	_LoadBitmap(&resources, mic_icon, &audioIcons.inputIcon);
	_LoadBitmap(&resources, speaker_icon, &audioIcons.outputIcon);
}


/**
 * @brief Pushes a CMAP8 resource into a pre-sized BBitmap.
 *
 * @param resources Resource container open on the executable.
 * @param id        Resource id to read; from IconHandles.h.
 * @param bitmap    Destination bitmap; must already be sized to @c sBounds.
 */
void
MediaIcons::_LoadBitmap(BResources* resources, int32 id, BBitmap* bitmap)
{
	size_t size;
	const void* bits = resources->LoadResource(B_COLOR_8_BIT_TYPE, id, &size);
	bitmap->SetBits(bits, size, 0, B_CMAP8);
}


/**
 * @brief Returns a 16x16 BRect anchored at @a topLeft, sized like the
 *        media icons.
 *
 * @param topLeft Top-left point in target view coordinates.
 * @return @c sBounds offset to @a topLeft.
 */
BRect
MediaIcons::IconRectAt(const BPoint& topLeft)
{
	return BRect(sBounds).OffsetToSelf(topLeft);
}
