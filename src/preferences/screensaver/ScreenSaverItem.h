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
 * MIT License. Copyright 2005-2013, Haiku.
 * Original authors: Jérôme Duval.
 */

/** @file ScreenSaverItem.h
    @brief BStringItem subclass that pairs a screensaver display name with its add-on path. */

#ifndef SCREEN_SAVER_ITEM_H
#define SCREEN_SAVER_ITEM_H


#include <ListItem.h>
#include <String.h>
#include <Entry.h>


/**
 * @brief List entry that represents one available screensaver add-on.
 *
 * The display name is stored in the BStringItem base class; the absolute
 * path to the add-on file is kept alongside so that the modules view can
 * locate and load it on demand.
 */
class ScreenSaverItem : public BStringItem {
public:
	/**
	 * @brief Constructs an item with display name @a eventName and add-on @a path.
	 *
	 * @param eventName Visible label shown in the list.
	 * @param path      Absolute file system path to the screensaver add-on.
	 */
	ScreenSaverItem(const char* eventName, const char* path)
		: BStringItem(eventName), fPath(path) {}

	/** @brief Returns the absolute path to the add-on backing this item. */
	const char* Path() const { return fPath.String(); }

private:
	BString fPath;
};


#endif	// SCREEN_SAVER_ITEM_H
