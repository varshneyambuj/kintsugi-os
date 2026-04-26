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
 *   Copyright 2004-2006 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Sandor Vroemisse
 *       Jerome Duval
 */


/**
 * @file KeymapListItem.cpp
 * @brief Implementation of the keymap list item used by KeymapWindow.
 */


#include "KeymapListItem.h"


/**
 * @brief Construct a KeymapListItem with the given entry_ref and label.
 *
 * @param keymap entry_ref of the keymap file. The ref is copied into the
 *               item; the caller can let the original go out of scope.
 * @param name   Optional display name; falls back to @a keymap.name.
 */
KeymapListItem::KeymapListItem(entry_ref& keymap, const char* name)
	:
	BStringItem(name != NULL ? name : keymap.name),
	fKeymap(keymap)
{
}
