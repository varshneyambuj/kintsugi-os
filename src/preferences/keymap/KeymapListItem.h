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
 * MIT License. Copyright 2004-2009, Haiku Inc.
 * Original authors: Sandor Vroemisse, Jerome Duval.
 */

/** @file KeymapListItem.h
    @brief BStringItem subclass that carries the entry_ref of a keymap file. */

#ifndef KEYMAP_LIST_ITEM_H
#define KEYMAP_LIST_ITEM_H


#include <ListItem.h>
#include <Entry.h>


/**
 * @brief List entry that pairs a display name with the @c entry_ref of a keymap.
 *
 * Used by the system / user keymap list views in KeymapWindow so the
 * selection handler can load the underlying keymap file directly without
 * a second name-based lookup.
 */
class KeymapListItem : public BStringItem {
public:
								KeymapListItem(entry_ref& keymap,
									const char* name = NULL);

			/** @brief Reference to the on-disk keymap file backing this item. */
			entry_ref&			EntryRef() { return fKeymap; };

protected:
			entry_ref			fKeymap;
};

#endif	// KEYMAP_LIST_ITEM_H
