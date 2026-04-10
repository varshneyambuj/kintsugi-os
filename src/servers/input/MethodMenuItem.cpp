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
 *   Copyright (c) 2004, Haiku
 *   This software is part of the Haiku distribution and is covered
 *   by the Haiku license.
 *
 *   Authors:
 *       Jérôme Duval
 */

/** @file MethodMenuItem.cpp
 *  @brief Implementation of the icon-and-label menu item for input methods. */

#include <string.h>
#include "MethodMenuItem.h"

/**
 * @brief Constructs a menu item with an icon, label, and attached submenu.
 *
 * The icon bitmap is copied from the supplied raw pixel data.
 *
 * @param cookie    Unique cookie identifying the associated input method.
 * @param name      Display label for the menu item.
 * @param icon      Raw CMAP8 pixel data for the 16x16 icon.
 * @param subMenu   Submenu to attach to this item (ownership transferred to BMenuItem).
 * @param messenger Messenger targeting the input method add-on.
 */
MethodMenuItem::MethodMenuItem(int32 cookie, const char* name, const uchar* icon, BMenu* subMenu, BMessenger& messenger)
	: BMenuItem(subMenu),
	fIcon(BRect(0, 0, MENUITEM_ICON_SIZE - 1, MENUITEM_ICON_SIZE - 1), B_CMAP8),
	fCookie(cookie)
{
	SetLabel(name);
	fIcon.SetBits(icon, MENUITEM_ICON_SIZE * MENUITEM_ICON_SIZE, 0, B_CMAP8);
	fMessenger = messenger;
}


/**
 * @brief Constructs a simple menu item with an icon and label but no submenu.
 *
 * @param cookie Unique cookie identifying the associated input method.
 * @param name   Display label for the menu item.
 * @param icon   Raw CMAP8 pixel data for the 16x16 icon.
 */
MethodMenuItem::MethodMenuItem(int32 cookie, const char* name, const uchar* icon)
	: BMenuItem(name, NULL),
	fIcon(BRect(0, 0, MENUITEM_ICON_SIZE - 1, MENUITEM_ICON_SIZE - 1), B_CMAP8),
	fCookie(cookie)
{
	fIcon.SetBits(icon, MENUITEM_ICON_SIZE * MENUITEM_ICON_SIZE, 0, B_CMAP8);
}


/** @brief Destructor. */
MethodMenuItem::~MethodMenuItem()
{
}


/**
 * @brief Updates the display label of this menu item.
 *
 * @param name The new label string.
 */
void
MethodMenuItem::SetName(const char *name)
{
	SetLabel(name);
}

/**
 * @brief Replaces the icon bitmap with new pixel data.
 *
 * @param icon Raw CMAP8 pixel data for the 16x16 icon.
 */
void
MethodMenuItem::SetIcon(const uchar *icon)
{
	fIcon.SetBits(icon, MENUITEM_ICON_SIZE * MENUITEM_ICON_SIZE, 0, B_CMAP8);
}


/**
 * @brief Computes the content size needed to draw the icon and label.
 *
 * The width accounts for the icon, a small gap, and the text width.
 * The height is the larger of the font height or the icon height.
 *
 * @param width  Output: the required content width in pixels.
 * @param height Output: the required content height in pixels.
 */
void
MethodMenuItem::GetContentSize(float *width, float *height)
{
	*width = be_plain_font->StringWidth(Label()) + MENUITEM_ICON_SIZE + 3;

	font_height fheight;
	be_plain_font->GetHeight(&fheight);

	*height = fheight.ascent + fheight.descent + fheight.leading - 2;
	if (*height < MENUITEM_ICON_SIZE)
		*height = MENUITEM_ICON_SIZE;
}


/**
 * @brief Draws the icon bitmap followed by the text label into the parent menu.
 *
 * The icon is drawn with B_OP_OVER compositing, then the pen is advanced
 * past the icon before delegating to BMenuItem::DrawContent() for the label.
 */
void
MethodMenuItem::DrawContent()
{
	BMenu *menu = Menu();
	BPoint contLoc = ContentLocation();

	menu->SetDrawingMode(B_OP_OVER);
	menu->MovePenTo(contLoc);
	menu->DrawBitmapAsync(&fIcon);
	menu->SetDrawingMode(B_OP_COPY);
	menu->MovePenBy(MENUITEM_ICON_SIZE + 3, 2);
	BMenuItem::DrawContent();
}

