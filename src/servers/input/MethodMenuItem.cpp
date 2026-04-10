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

MethodMenuItem::MethodMenuItem(int32 cookie, const char* name, const uchar* icon, BMenu* subMenu, BMessenger& messenger)
	: BMenuItem(subMenu),
	fIcon(BRect(0, 0, MENUITEM_ICON_SIZE - 1, MENUITEM_ICON_SIZE - 1), B_CMAP8),
	fCookie(cookie)
{
	SetLabel(name);
	fIcon.SetBits(icon, MENUITEM_ICON_SIZE * MENUITEM_ICON_SIZE, 0, B_CMAP8);
	fMessenger = messenger;
}


MethodMenuItem::MethodMenuItem(int32 cookie, const char* name, const uchar* icon)
	: BMenuItem(name, NULL),
	fIcon(BRect(0, 0, MENUITEM_ICON_SIZE - 1, MENUITEM_ICON_SIZE - 1), B_CMAP8),
	fCookie(cookie)
{
	fIcon.SetBits(icon, MENUITEM_ICON_SIZE * MENUITEM_ICON_SIZE, 0, B_CMAP8);
}


MethodMenuItem::~MethodMenuItem()
{
}


void
MethodMenuItem::SetName(const char *name)
{
	SetLabel(name);
}

void
MethodMenuItem::SetIcon(const uchar *icon)
{
	fIcon.SetBits(icon, MENUITEM_ICON_SIZE * MENUITEM_ICON_SIZE, 0, B_CMAP8);
}


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

