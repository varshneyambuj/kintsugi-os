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
 * MIT License. Copyright 2009, Ingo Weinhold.
 */

/** @file TagNames.h
    @brief Helper that maps DW_TAG_* numeric tag codes to their human-readable names. */

#ifndef DWARF_TAG_NAMES_H
#define DWARF_TAG_NAMES_H

#include <SupportDefs.h>


/** @brief Returns the textual DW_TAG_* name for a given DIE tag, or NULL if unknown. */
const char*	get_entry_tag_name(uint16 tag);


#endif	// DWARF_TAG_NAMES_H
