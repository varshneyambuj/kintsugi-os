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
 *   Open Tracker License
 *
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of
 *   this software and associated documentation files (the "Software"), to deal in
 *   the Software without restriction, including without limitation the rights to
 *   use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *   of the Software, and to permit persons to whom the Software is furnished to do
 *   so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice applies to all licensees
 *   and shall be included in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *   BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 *   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN
 *   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *   Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered
 *   trademarks of Be Incorporated in the United States and other countries.
 *   All rights reserved.
 */


/**
 * @file GroupedMenu.cpp
 * @brief BMenu subclass that organises items into named, separator-delimited groups.
 *
 * TGroupedMenu and TMenuItemGroup allow items to be added and removed in logical
 * groups. When groups are added to a menu, automatic BSeparatorItems are inserted
 * between groups, and their positions are updated as groups grow or shrink.
 *
 * @see BMenu, BMenuItem
 */


#include "GroupedMenu.h"

#include <stdlib.h>
#include <string.h>


using namespace BPrivate;


/**
 * @brief Construct an empty item group with an optional name.
 *
 * @param name  Display name for the group; NULL or "" for an unnamed group.
 */
TMenuItemGroup::TMenuItemGroup(const char* name)
	:
	fMenu(NULL),
	fFirstItemIndex(-1),
	fItemsTotal(0),
	fHasSeparator(false)
{
	if (name != NULL && name[0] != '\0')
		fName = strdup(name);
	else
		fName = NULL;
}


/**
 * @brief Destructor; frees the group name and, if unowned, deletes all item objects.
 */
TMenuItemGroup::~TMenuItemGroup()
{
	free((char*)fName);

	if (fMenu == NULL) {
		BMenuItem* item;
		while ((item = RemoveItem((int32)0)) != NULL)
			delete item;
	}
}


/**
 * @brief Append @a item to this group, notifying the owning menu if present.
 *
 * @param item  The BMenuItem to add.
 * @return true on success.
 */
bool
TMenuItemGroup::AddItem(BMenuItem* item)
{
	if (!fList.AddItem(item))
		return false;

	if (fMenu)
		fMenu->AddGroupItem(this, item, fList.IndexOf(item));

	fItemsTotal++;
	return true;
}


/**
 * @brief Insert @a item at @a atIndex within this group, notifying the owning menu.
 *
 * @param item     The BMenuItem to insert.
 * @param atIndex  Position within the group's own list.
 * @return true on success.
 */
bool
TMenuItemGroup::AddItem(BMenuItem* item, int32 atIndex)
{
	if (!fList.AddItem(item, atIndex))
		return false;

	if (fMenu)
		fMenu->AddGroupItem(this, item, atIndex);

	fItemsTotal++;
	return true;
}


/**
 * @brief Wrap @a menu in a BMenuItem and append it to this group.
 *
 * @param menu  The submenu to add.
 * @return true on success.
 */
bool
TMenuItemGroup::AddItem(BMenu* menu)
{
	BMenuItem* item = new BMenuItem(menu);
	if (item == NULL)
		return false;

	if (!AddItem(item)) {
		delete item;
		return false;
	}

	return true;
}


/**
 * @brief Wrap @a menu in a BMenuItem and insert it at @a atIndex within this group.
 *
 * @param menu     The submenu to insert.
 * @param atIndex  Position within the group's own list.
 * @return true on success.
 */
bool
TMenuItemGroup::AddItem(BMenu* menu, int32 atIndex)
{
	BMenuItem* item = new BMenuItem(menu);
	if (item == NULL)
		return false;

	if (!AddItem(item, atIndex)) {
		delete item;
		return false;
	}

	return true;
}


/**
 * @brief Remove @a item from this group, notifying the owning menu.
 *
 * @param item  The BMenuItem to remove.
 * @return true if the item was found and removed.
 */
bool
TMenuItemGroup::RemoveItem(BMenuItem* item)
{
	if (fMenu)
		fMenu->RemoveGroupItem(this, item);

	return fList.RemoveItem(item);
}


/**
 * @brief Remove the BMenuItem that hosts @a menu from this group.
 *
 * @param menu  The submenu whose super-item should be removed.
 * @return true if the item was found and removed.
 */
bool
TMenuItemGroup::RemoveItem(BMenu* menu)
{
	BMenuItem* item = menu->Superitem();
	if (item == NULL)
		return false;

	return RemoveItem(item);
}


/**
 * @brief Remove and return the item at @a index within this group.
 *
 * @param index  Zero-based position within the group's own list.
 * @return The removed BMenuItem, or NULL if @a index is out of range.
 */
BMenuItem*
TMenuItemGroup::RemoveItem(int32 index)
{
	BMenuItem* item = ItemAt(index);
	if (item == NULL)
		return NULL;

	if (RemoveItem(item))
		return item;

	return NULL;
}


/**
 * @brief Return the item at @a index within this group.
 *
 * @param index  Zero-based position within the group's own list.
 * @return The BMenuItem, or NULL if @a index is out of range.
 */
BMenuItem*
TMenuItemGroup::ItemAt(int32 index)
{
	return static_cast<BMenuItem*>(fList.ItemAt(index));
}


/**
 * @brief Return the number of items currently in this group.
 *
 * @return Item count (not including any separator).
 */
int32
TMenuItemGroup::CountItems()
{
	return fList.CountItems();
}


/**
 * @brief Record whether this group currently has a leading separator in the menu.
 *
 * Adjusts fItemsTotal to account for the separator's menu-level slot.
 *
 * @param separated  true if a separator precedes this group; false if not.
 */
void
TMenuItemGroup::Separated(bool separated)
{
	if (separated == fHasSeparator)
		return;

	fHasSeparator = separated;

	if (separated)
		fItemsTotal++;
	else
		fItemsTotal--;
}


/**
 * @brief Return whether this group currently has a leading separator.
 *
 * @return true if a separator is present before this group's items.
 */
bool
TMenuItemGroup::HasSeparator()
{
	return fHasSeparator;
}


//	#pragma mark -


/**
 * @brief Construct an empty grouped menu with the given title.
 *
 * @param name  Menu label.
 */
TGroupedMenu::TGroupedMenu(const char* name)
	: BMenu(name)
{
}


/**
 * @brief Destructor; deletes all owned TMenuItemGroup objects.
 */
TGroupedMenu::~TGroupedMenu()
{
	TMenuItemGroup* group;
	while ((group = static_cast<TMenuItemGroup*>(fGroups.RemoveItem((int32)0)))
			!= NULL) {
		delete group;
	}
}


/**
 * @brief Append @a group to this menu and insert all its current items.
 *
 * @param group  The TMenuItemGroup to add; ownership is NOT transferred.
 * @return true on success.
 */
bool
TGroupedMenu::AddGroup(TMenuItemGroup* group)
{
	if (!fGroups.AddItem(group))
		return false;

	group->fMenu = this;

	for (int32 i = 0; i < group->CountItems(); i++) {
		AddGroupItem(group, group->ItemAt(i), i);
	}

	return true;
}


/**
 * @brief Insert @a group at @a atIndex in the group list.
 *
 * @param group    The TMenuItemGroup to insert.
 * @param atIndex  Position in the group list.
 * @return true on success.
 */
bool
TGroupedMenu::AddGroup(TMenuItemGroup* group, int32 atIndex)
{
	if (!fGroups.AddItem(group, atIndex))
		return false;

	group->fMenu = this;

	for (int32 i = 0; i < group->CountItems(); i++) {
		AddGroupItem(group, group->ItemAt(i), i);
	}

	return true;
}


/**
 * @brief Remove @a group from this menu, removing its items and separator.
 *
 * @param group  The TMenuItemGroup to detach from this menu.
 * @return true on success.
 */
bool
TGroupedMenu::RemoveGroup(TMenuItemGroup* group)
{
	if (group->HasSeparator()) {
		delete RemoveItem(group->fFirstItemIndex);
		group->Separated(false);
	}

	group->fMenu = NULL;
	group->fFirstItemIndex = -1;

	for (int32 i = 0; i < group->CountItems(); i++) {
		RemoveItem(group->ItemAt(i));
	}

	return fGroups.RemoveItem(group);
}


/**
 * @brief Return the group at @a index in the group list.
 *
 * @param index  Zero-based group index.
 * @return The TMenuItemGroup, or NULL if out of range.
 */
TMenuItemGroup*
TGroupedMenu::GroupAt(int32 index)
{
	return static_cast<TMenuItemGroup*>(fGroups.ItemAt(index));
}


/**
 * @brief Return the number of groups currently registered.
 *
 * @return Group count.
 */
int32
TGroupedMenu::CountGroups()
{
	return fGroups.CountItems();
}


/**
 * @brief Internal helper: insert @a item from @a group into the underlying BMenu.
 *
 * Computes the correct absolute menu index from the group's position, inserts
 * a BSeparatorItem if this is the group's first item and it needs one, then
 * increments the first-item offsets of all subsequent groups.
 *
 * @param group    The owning TMenuItemGroup.
 * @param item     The BMenuItem to insert into the menu.
 * @param atIndex  Position within the group's own list.
 */
void
TGroupedMenu::AddGroupItem(TMenuItemGroup* group, BMenuItem* item,
	int32 atIndex)
{
	int32 groupIndex = fGroups.IndexOf(group);
	bool addSeparator = false;

	if (group->fFirstItemIndex == -1) {
		// find new home for this group
		if (groupIndex > 0) {
			// add this group after an existing one
			TMenuItemGroup* previous = GroupAt(groupIndex - 1);
			group->fFirstItemIndex = previous->fFirstItemIndex
				+ previous->fItemsTotal;
			addSeparator = true;
		} else {
			// this is the first group
			TMenuItemGroup* successor = GroupAt(groupIndex + 1);
			if (successor != NULL) {
				group->fFirstItemIndex = successor->fFirstItemIndex;
				if (successor->fHasSeparator) {
					// we'll need one as well
					addSeparator = true;
				}
			} else {
				group->fFirstItemIndex = CountItems();
				if (group->fFirstItemIndex > 0)
					addSeparator = true;
			}
		}

		if (addSeparator) {
			AddItem(new BSeparatorItem(), group->fFirstItemIndex);
			group->Separated(true);
		}
	}

	// insert item for real

	AddItem(item,
		atIndex + group->fFirstItemIndex + (group->HasSeparator() ? 1 : 0));

	// move the groups after this one

	for (int32 i = groupIndex + 1; i < CountGroups(); i++) {
		group = GroupAt(i);
		group->fFirstItemIndex += addSeparator ? 2 : 1;
	}
}


/**
 * @brief Internal helper: remove @a item of @a group from the underlying BMenu.
 *
 * Removes the separator if it was the group's last item, then decrements the
 * first-item offsets of all subsequent groups.
 *
 * @param group  The owning TMenuItemGroup.
 * @param item   The BMenuItem to remove from the menu.
 */
void
TGroupedMenu::RemoveGroupItem(TMenuItemGroup* group, BMenuItem* item)
{
	int32 groupIndex = fGroups.IndexOf(group);
	bool removedSeparator = false;

	if (group->CountItems() == 1) {
		// this is the last item
		if (group->HasSeparator()) {
			RemoveItem(group->fFirstItemIndex);
			group->Separated(false);
			removedSeparator = true;
		}
	}

	// insert item for real

	RemoveItem(item);

	// move the groups after this one

	for (int32 i = groupIndex + 1; i < CountGroups(); i++) {
		group = GroupAt(i);
		group->fFirstItemIndex -= removedSeparator ? 2 : 1;
	}
}
