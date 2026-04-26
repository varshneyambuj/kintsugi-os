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
 * MIT License. Copyright 2002-2025, Haiku.
 * Original authors: DarkWyrm, Rene Gollent, Stephan Aßmus, Joseph Groover.
 */

/** @file ColorsView.h
    @brief Tab pane that lets the user edit the system UI palette. */

#ifndef COLORS_VIEW_H_
#define COLORS_VIEW_H_


#include <Button.h>
#include <CheckBox.h>
#include <ColorControl.h>
#include <ColorPreview.h>
#include <ListItem.h>
#include <ListView.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <String.h>
#include <StringView.h>
#include <View.h>

#include <DecorInfo.h>


/**
 * @brief BView subclass that drives the Colors tab and its color picker.
 */
class ColorsView : public BView {
public:
								ColorsView(const char *name);
	virtual						~ColorsView();

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage *msg);

			void				LoadSettings();

			void				SetDefaults();
			void				Revert();

			bool				IsDefaultable();
			bool				IsRevertable();

private:
			void				_CreateItems();
			void				_UpdatePreviews(const BMessage& colors);

			void				_SetColor(int32 index, rgb_color color);
			void				_SetColor(color_which which, rgb_color color);
			void				_SetCurrentColor(rgb_color color);
			void				_SetUIColors(const BMessage& colors);

private:
			BColorControl*		fPicker;

			BCheckBox*			fAutoSelectCheckBox;
			BListView*			fAttrList;

			color_which			fWhich;

			BScrollView*		fScrollView;

			BPrivate::BColorPreview*	fColorPreview;

			BMessage			fPrevColors;
			BMessage			fDefaultColors;
			BMessage			fCurrentColors;
};


#endif	// COLORS_VIEW_H_
