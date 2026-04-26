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
 *   Copyright 2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Josef Gajdusek
 */


/**
 * @file PopUpColumn.cpp
 * @brief Implementation of PopUpColumn, a BStringColumn that exposes a
 *        pop-up menu or inline editor on click.
 *
 * MouseDown() interprets primary/secondary mouse clicks and, depending on the
 * column's mode, opens a context menu, advances the value to the next menu
 * item, or pops up an EditWindow for free-form text entry. The chosen value
 * is then posted back to the parent window via HOTKEY_ITEM_MODIFIED.
 */


#include "PopUpColumn.h"

#include <PopUpMenu.h>
#include <MenuItem.h>
#include <Window.h>

#include "EditWindow.h"
#include "ShortcutsWindow.h"

/**
 * @brief Constructs a PopUpColumn bound to a given pop-up menu.
 *
 * @param menu       Pop-up menu instance whose ownership transfers to this
 *                   column (deleted in the destructor).
 * @param name       Column header name.
 * @param width      Initial column width.
 * @param minWidth   Minimum allowed width.
 * @param maxWidth   Maximum allowed width.
 * @param truncate   Truncation mode passed to BStringColumn.
 * @param editable   When true, primary clicks open an EditWindow on the cell.
 * @param cycle      When true, primary clicks cycle to the next menu item.
 * @param cycleInit  Index used as the cycle start value when no current
 *                   value matches.
 * @param align      Text alignment mode passed to BStringColumn.
 */
PopUpColumn::PopUpColumn(BPopUpMenu* menu, const char* name, float width,
	float minWidth, float maxWidth, uint32 truncate, bool editable,
	bool cycle, int cycleInit, alignment align)
	:
	BStringColumn(name, width, minWidth, maxWidth, truncate, align),
	fEditable(editable),
	fCycle(cycle),
	fCycleInit(cycleInit),
	fMenu(menu)
{
	SetWantsEvents(true);
}


/**
 * @brief Destructor; releases the owned pop-up menu.
 */
PopUpColumn::~PopUpColumn()
{
	delete fMenu;
}

/**
 * @brief Mouse handler that drives pop-up menus, cycling, or text edits.
 *
 * On secondary click, displays the column's pop-up menu and forwards the
 * selection. On primary click of an editable column, opens an EditWindow.
 * On primary click of a cycling column, advances to the next menu item.
 * The chosen value is dispatched to the parent window as a
 * HOTKEY_ITEM_MODIFIED message.
 *
 * @param parent     The owning BColumnListView.
 * @param row        Row that received the click.
 * @param field      Cell field within @a row.
 * @param fieldRect  Rectangle of the clicked cell.
 * @param point      Click location in view coordinates.
 * @param buttons    Pressed mouse-button bitmask.
 */
void
PopUpColumn::MouseDown(BColumnListView* parent, BRow* row, BField* field,
	BRect fieldRect, BPoint point, uint32 buttons)
{
	if ((buttons & B_SECONDARY_MOUSE_BUTTON)
		|| (buttons & B_PRIMARY_MOUSE_BUTTON && (fEditable || fCycle))) {
		BMessage* msg = new BMessage(ShortcutsWindow::HOTKEY_ITEM_MODIFIED);
		msg->SetInt32("row", parent->IndexOf(row));
		msg->SetInt32("column", LogicalFieldNum());
		if (buttons & B_SECONDARY_MOUSE_BUTTON) {
			BMenuItem* selected = fMenu->Go(parent->ConvertToScreen(point));
			if (selected) {
				msg->SetString("text", selected->Label());
				parent->Window()->PostMessage(msg);
			}
		}
		if (buttons & B_PRIMARY_MOUSE_BUTTON && row->IsSelected()) {
			BStringField* stringField = static_cast<BStringField*>(field);
			if (fEditable) {
				EditWindow* edit = new EditWindow(stringField->String(), 0);
				msg->SetString("text", edit->Go());
			} else if (fCycle) {
				BMenuItem* item;
				for (int i = 0; (item = fMenu->ItemAt(i)) != NULL; i++)
					if (strcmp(stringField->String(), item->Label()) == 0) {
						item = fMenu->ItemAt((i + 1) % fMenu->CountItems());
						break;
					}
				if (item == NULL)
					item = fMenu->ItemAt(fCycleInit);
				msg->SetString("text", item->Label());
			}
			parent->Window()->PostMessage(msg);
		}
	}
	BStringColumn::MouseDown(parent, row, field, fieldRect, point, buttons);
}

