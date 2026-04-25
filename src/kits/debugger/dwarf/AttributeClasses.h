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
 * MIT License. Copyright 2009, Ingo Weinhold; Copyright 2013, Rene Gollent.
 */

/** @file AttributeClasses.h
    @brief Lookup helpers and attribute-class constants for DWARF DIE attributes. */

#ifndef ATTRIBUTE_TABLES_H
#define ATTRIBUTE_TABLES_H

#include "DebugInfoEntry.h"


/** @brief Pointer-to-member type for DIE attribute setters used by the parser. */
typedef status_t (DebugInfoEntry::* DebugInfoEntrySetter)(uint16,
	const AttributeValue&);


/**
 * @brief DWARF attribute class enumerators.
 *
 * Each DIE attribute belongs to exactly one class, which determines the
 * representation of its value (address, block, constant, flag, ...).
 */
enum {
	ATTRIBUTE_CLASS_UNKNOWN			= 0,
	ATTRIBUTE_CLASS_ADDRESS			= 1,
	ATTRIBUTE_CLASS_ADDRPTR			= 2,
	ATTRIBUTE_CLASS_BLOCK			= 3,
	ATTRIBUTE_CLASS_CONSTANT		= 4,
	ATTRIBUTE_CLASS_FLAG			= 5,
	ATTRIBUTE_CLASS_LINEPTR			= 6,
	ATTRIBUTE_CLASS_LOCLIST   		= 7,
	ATTRIBUTE_CLASS_LOCLISTPTR		= 8,
	ATTRIBUTE_CLASS_MACPTR			= 9,
	ATTRIBUTE_CLASS_RANGELIST		= 10,
	ATTRIBUTE_CLASS_RANGELISTPTR	= 11,
	ATTRIBUTE_CLASS_REFERENCE		= 12,
	ATTRIBUTE_CLASS_STRING			= 13,
	ATTRIBUTE_CLASS_STROFFSETSPTR	= 14,
};


/** @brief Returns the bitmask of valid attribute classes for a DW_AT_* name. */
uint16	get_attribute_name_classes(uint32 name);
/** @brief Returns the bitmask of valid attribute classes for a DW_FORM_* code. */
uint16	get_attribute_form_classes(uint32 form);
/** @brief Resolves the unique attribute class for a (name, form) pair. */
uint8	get_attribute_class(uint32 name, uint32 form);

/** @brief Returns the textual DW_AT_* name for an attribute code, or NULL. */
const char*	get_attribute_name_name(uint32 name);
/** @brief Returns the textual DW_FORM_* name for a form code, or NULL. */
const char*	get_attribute_form_name(uint32 form);

/** @brief Returns the DIE setter member-function pointer for an attribute name. */
DebugInfoEntrySetter	get_attribute_name_setter(uint32 name);


#endif	// ATTRIBUTE_TABLES_H
