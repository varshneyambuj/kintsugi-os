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
 * MIT License. Copyright 2019, Haiku, Inc.
 * Original author: Preetpal Kaur.
 */

/** @file InputDeviceView.h
    @brief Declares DeviceListItemView, the icon+label row used in the device list. */

#ifndef _INPUT_DEVICE_VIEW_H
#define _INPUT_DEVICE_VIEW_H

#include <ListItem.h>
#include <String.h>
#include <View.h>


#define ITEM_SELECTED 'I1s'

#define kITEM_MARGIN	1


class InputIcons;


/**
 * @brief Categorical tag identifying which input device a row represents.
 */
enum input_type {
	MOUSE_TYPE,
	TOUCHPAD_TYPE,
	KEYBOARD_TYPE
};


/**
 * @brief BListItem rendering an input device row with icon and label.
 *
 * Used by InputWindow's device list. The actual drawing is performed by a
 * private Renderer struct which is shared between Update() and DrawItem().
 */
class DeviceListItemView : public BListItem {
public:
						DeviceListItemView(BString title, input_type type);

	void				Update(BView* owner, const BFont* font);
	void				DrawItem(BView* owner, BRect frame,
						bool complete = false);

	/** @brief Returns the row's title as a C string. */
	const char*			Label() { return fTitle.String();}


	/** @brief Returns the shared icon set used by every list item. */
	static	InputIcons*	Icons() { return sIcons; }
	/** @brief Sets the shared icon set used by every list item. */
	static	void		SetIcons(InputIcons* icons) { sIcons = icons; }

protected:
	struct Renderer;

	void				SetRenderParameters(Renderer& renderer);

private:
	static InputIcons*	sIcons;
	BString				fTitle;
	input_type			fInputType;
};


#endif	// _INPUT_DEVICE_VIEW_H */
