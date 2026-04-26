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
 *   Copyright 2019, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Preetpal Kaur <preetpalok123@gmail.com>
 */


/**
 * @file InputDeviceView.cpp
 * @brief Implementation of DeviceListItemView, a custom BListItem.
 *
 * DeviceListItemView is the row used in the Input preferences device list.
 * It draws an icon (mouse, touchpad, or keyboard depending on the device
 * type) followed by the device's friendly name. An internal Renderer
 * struct keeps Update() and DrawItem() in sync without exposing the
 * drawing details.
 *
 * @see InputIcons, InputWindow
 */


#include "InputDeviceView.h"


#include <Catalog.h>
#include <Locale.h>
#include <String.h>

#include "InputIcons.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DeviceList"


/** @brief Shared icon set used to render every DeviceListItemView. */
InputIcons* DeviceListItemView::sIcons = NULL;


/**
 * @brief Constructs a DeviceListItemView for a single input device.
 *
 * @param title  Friendly name shown next to the icon.
 * @param type   Input device category that selects which icon to draw.
 */
DeviceListItemView::DeviceListItemView(BString title, input_type type)
	:
	BListItem((uint32)0),
	fTitle(title),
	fInputType(type)
{
}


/**
 * @brief Internal helper that renders or measures a DeviceListItemView.
 *
 * Renderer collects the title text, the primary icon, and selection state
 * before deciding how to lay them out in either DrawItem() (Render()) or
 * Update() (ItemWidth()).
 */
struct DeviceListItemView::Renderer {
	/**
	 * @brief Initialises the renderer to an empty, deselected state.
	 */
	Renderer()
		:
		fTitle(NULL),
		fPrimaryIcon(NULL),
		fSelected(false)
	{
	}

	/**
	 * @brief Adopts @a icon as the primary icon if none has been set.
	 *
	 * @param icon  BBitmap to use; ignored if a previous icon is present.
	 */
	void AddIcon(BBitmap* icon)
	{
		if (!fPrimaryIcon)
			fPrimaryIcon = icon;
	}

	/**
	 * @brief Stores the title to render next to the icon.
	 *
	 * @param title  C-string label; not copied, must outlive the renderer.
	 */
	void SetTitle(const char* title)
	{
		fTitle = title;
	}

	/**
	 * @brief Marks the row as selected so Render() paints the highlight.
	 *
	 * @param selected  true when the underlying BListItem is selected.
	 */
	void SetSelected(bool selected)
	{
		fSelected = selected;
	}

	/**
	 * @brief Paints the icon and title into @a onto.
	 *
	 * Draws the selected-row background when applicable, then composites
	 * the primary icon over the panel and renders the label using the
	 * plain font.
	 *
	 * @param onto      View providing the drawing context.
	 * @param frame     Rectangle in @a onto-local coordinates to fill.
	 * @param complete  Repaint the entire frame even when not selected.
	 */
	void Render(BView* onto, BRect frame, bool complete = false)
	{
		const rgb_color lowColor = onto->LowColor();
		const rgb_color highColor = onto->HighColor();

		if (fSelected || complete) {
			if (fSelected)
				onto->SetLowColor(ui_color(B_LIST_SELECTED_BACKGROUND_COLOR));
			onto->FillRect(frame, B_SOLID_LOW);
		}

		BPoint point(frame.left + 4.0f,
			frame.top + (frame.Height() - InputIcons::sBounds.Height()) / 2.0f);

		BRect iconFrame(InputIcons::IconRectAt(point + BPoint(1, 0)));

		onto->SetDrawingMode(B_OP_OVER);
		if (fPrimaryIcon) {
			onto->DrawBitmap(fPrimaryIcon, iconFrame);
			point.x = iconFrame.right + 1;
		}

		onto->SetDrawingMode(B_OP_COPY);

		BFont font = be_plain_font;
		font_height fontInfo;
		font.GetHeight(&fontInfo);

		onto->SetFont(&font);
		onto->MovePenTo(point.x + 8,
			frame.top + fontInfo.ascent
				+ (frame.Height() - ceilf(fontInfo.ascent + fontInfo.descent))
					/ 2.0f);
		onto->DrawString(fTitle);

		onto->SetHighColor(highColor);
		onto->SetLowColor(lowColor);
	}

	/**
	 * @brief Returns the preferred row width for the current title and icon.
	 *
	 * @return Sum of fixed left padding, label string width, and icon
	 *         width (or a default 16 if no icon was set).
	 */
	float ItemWidth()
	{
		float width = 4.0f;
		width += be_plain_font->StringWidth(fTitle) +
			(fPrimaryIcon != NULL ? fPrimaryIcon->Bounds().Width() : 16.0f);
		return width;
	}

private:

	BString		fTitle;
	BBitmap*	fPrimaryIcon;
	bool		fSelected;
};


/**
 * @brief Recomputes the row's size from the current font and icon.
 *
 * Ensures the height accommodates the icon plus margin, then asks an
 * internal Renderer to compute the preferred width.
 *
 * @param owner  BView providing the drawing context.
 * @param font   Font in use by the BListView.
 */
void
DeviceListItemView::Update(BView* owner, const BFont* font)
{
	BListItem::Update(owner, font);

	float iconHeight = InputIcons::sBounds.Height() + 1;
	if ((Height() < iconHeight + kITEM_MARGIN * 2))
		SetHeight(iconHeight + kITEM_MARGIN * 2);

	Renderer renderer;
	renderer.SetTitle(Label());
	renderer.SetTitle(fTitle);
	SetRenderParameters(renderer);
	SetWidth(renderer.ItemWidth());
}


/**
 * @brief Draws the row into @a owner via an internal Renderer.
 *
 * @param owner     BView providing the drawing context.
 * @param frame     Rectangle in @a owner-local coordinates to draw into.
 * @param complete  Repaint the entire frame regardless of selection.
 */
void
DeviceListItemView::DrawItem(BView* owner, BRect frame, bool complete)
{
	Renderer renderer;
	renderer.SetSelected(IsSelected());
	renderer.SetTitle(Label());
	SetRenderParameters(renderer);
	renderer.Render(owner, frame, complete);
}


/**
 * @brief Picks the appropriate device icon for the renderer.
 *
 * Looks up the shared InputIcons set and hands the matching BBitmap to
 * @a renderer based on the input device type.
 *
 * @param renderer  Renderer that will receive the chosen icon.
 */
void
DeviceListItemView::SetRenderParameters(Renderer& renderer)
{
	if (Icons() != NULL) {
		if (fInputType == MOUSE_TYPE)
			renderer.AddIcon(&Icons()->mouseIcon);
		else if (fInputType == TOUCHPAD_TYPE)
			renderer.AddIcon(&Icons()->touchpadIcon);
		else if (fInputType == KEYBOARD_TYPE)
			renderer.AddIcon(&Icons()->keyboardIcon);
	}
}
