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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2016 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file MenuItemPrivate.cpp
 * @brief Private implementation helpers for BMenuItem
 *
 * Contains BPrivate::BMenuItemPrivate, an internal class that exposes
 * package-private BMenuItem functionality needed by BMenu for item layout
 * and drawing.
 *
 * @see BMenuItem, BMenu
 */


#include <MenuItemPrivate.h>

#include <Menu.h>


namespace BPrivate {

/**
 * @brief Constructs a MenuItemPrivate proxy for the given BMenuItem.
 *
 * @param menuItem The BMenuItem whose private internals will be accessed
 *                 through this proxy. Must not be NULL.
 */
MenuItemPrivate::MenuItemPrivate(BMenuItem* menuItem)
	:
	fMenuItem(menuItem)
{
}


/**
 * @brief Replaces the submenu attached to the proxied BMenuItem.
 *
 * Deletes the existing submenu (if any), then calls BMenuItem::_InitMenuData()
 * to attach @a submenu. If the item is already installed in a super-menu,
 * the super-menu's layout is invalidated and the menu is redrawn.
 *
 * @param submenu The new BMenu to attach as a submenu, or NULL to detach
 *                the existing submenu. Ownership is transferred to the item.
 *
 * @see Install(), Uninstall()
 */
void
MenuItemPrivate::SetSubmenu(BMenu* submenu)
{
	delete fMenuItem->fSubmenu;

	fMenuItem->_InitMenuData(submenu);

	if (fMenuItem->fSuper != NULL) {
		fMenuItem->fSuper->InvalidateLayout();

		if (fMenuItem->fSuper->LockLooper()) {
			fMenuItem->fSuper->Invalidate();
			fMenuItem->fSuper->UnlockLooper();
		}
	}
}


/**
 * @brief Installs the proxied BMenuItem into the given window's handler chain.
 *
 * Delegates to BMenuItem::Install() so that the item can receive messages
 * from the window (e.g., for shortcut key handling).
 *
 * @param window The window to install the menu item into.
 *
 * @see Uninstall()
 */
void
MenuItemPrivate::Install(BWindow* window)
{
	fMenuItem->Install(window);
}


/**
 * @brief Removes the proxied BMenuItem from its installed window's handler chain.
 *
 * Delegates to BMenuItem::Uninstall(). Safe to call even if the item is not
 * currently installed.
 *
 * @see Install()
 */
void
MenuItemPrivate::Uninstall()
{
	fMenuItem->Uninstall();
}


}	// namespace BPrivate
