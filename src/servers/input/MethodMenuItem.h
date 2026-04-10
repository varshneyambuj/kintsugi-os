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

/** @file MethodMenuItem.h
 *  @brief BMenuItem subclass that draws an input method's icon next to its label. */

#ifndef METHOD_MENUITEM_H_
#define METHOD_MENUITEM_H_

#include <Bitmap.h>
#include <MenuItem.h>

/** @brief Side length of the input method icon, in pixels. */
#define MENUITEM_ICON_SIZE 16

/** @brief BMenuItem that pairs an input method's icon with its label.
 *
 * Used in the input method picker to render each available method with its
 * 16×16 icon, label, and a back-pointer cookie understood by the owning
 * input server method. */
class MethodMenuItem : public BMenuItem {
	public:
		/** @brief Constructs a menu item with a submenu and a messenger target. */
		MethodMenuItem(int32 cookie, const char *label, const uchar *icon, BMenu *subMenu, BMessenger &messenger);
		/** @brief Constructs a leaf menu item with no submenu. */
		MethodMenuItem(int32 cookie, const char *label, const uchar *icon);

		virtual ~MethodMenuItem();

		/** @brief Draws the icon followed by the label. */
		virtual void DrawContent();
		/** @brief Reports the icon-plus-label size to the menu layout code. */
		virtual void GetContentSize(float *width, float *height);

		/** @brief Updates the displayed method name. */
		void SetName(const char *name);
		/** @brief Returns the displayed method name. */
		const char *Name() { return Label(); };

		/** @brief Replaces the displayed icon. */
		void SetIcon(const uchar *icon);
		/** @brief Returns a pointer to the raw icon bits. */
		const uchar *Icon() { return(uchar *)fIcon.Bits(); };

		/** @brief Returns the input-method cookie associated with this item. */
		int32 Cookie() { return fCookie; };
	private:
		BBitmap fIcon;          /**< Cached icon bitmap. */
		int32 fCookie;          /**< Opaque cookie identifying the owning input method. */
		BMessenger fMessenger;  /**< Messenger that receives notifications when this item is invoked. */
};

#endif
