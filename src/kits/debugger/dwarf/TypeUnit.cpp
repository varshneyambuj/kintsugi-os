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
 *   Copyright 2013, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file TypeUnit.cpp
 * @brief Implementation of TypeUnit, the .debug_types compilation-unit variant.
 *
 * DWARF v4 introduced separate "type units" stored in the .debug_types
 * section so that compilers can emit identical type definitions only
 * once, deduplicated across translation units by a 64-bit type signature.
 * TypeUnit specialises @ref BaseUnit with a signature and a top-level
 * type entry offset.
 */

#include "TypeUnit.h"

#include <new>

#include "DebugInfoEntries.h"


/**
 * @brief Constructs a type unit from header offsets and the type signature.
 *
 * @param headerOffset       Byte offset of the unit header within .debug_types.
 * @param contentOffset      Byte offset of the first DIE following the header.
 * @param totalSize          Total size of the unit (header + content).
 * @param abbreviationOffset Offset into .debug_abbrev for the unit's table.
 * @param typeOffset         Offset (within the unit) of the canonical type DIE.
 * @param addressSize        Target address width in bytes.
 * @param isBigEndian        @c true for big-endian targets.
 * @param signature          64-bit type signature deduplicating the type.
 * @param isDwarf64          @c true if this is a 64-bit DWARF unit.
 */
TypeUnit::TypeUnit(off_t headerOffset, off_t contentOffset,
	off_t totalSize, off_t abbreviationOffset, off_t typeOffset,
	uint8 addressSize, bool isBigEndian, uint64 signature, bool isDwarf64)
	:
	BaseUnit(headerOffset, contentOffset, totalSize, abbreviationOffset,
		addressSize, isBigEndian, isDwarf64),
	fUnitEntry(NULL),
	fTypeEntry(NULL),
	fSignature(signature),
	fTypeOffset(typeOffset)
{
}


/**
 * @brief Destroys the type unit.
 */
TypeUnit::~TypeUnit()
{
}


/**
 * @brief Records the DIE describing this unit (DW_TAG_type_unit).
 *
 * @param entry Pointer to the unit-level DIE; ownership unchanged.
 */
void
TypeUnit::SetUnitEntry(DIETypeUnit* entry)
{
	fUnitEntry = entry;
}


/**
 * @brief Returns the canonical type DIE referenced from the unit header.
 */
DebugInfoEntry*
TypeUnit::TypeEntry() const
{
	return fTypeEntry;
}


/**
 * @brief Records the canonical type DIE referenced from the unit header.
 *
 * @param entry The DIE at @c fTypeOffset, e.g. the DW_TAG_class_type for
 *              which this signature was emitted.
 */
void
TypeUnit::SetTypeEntry(DebugInfoEntry* entry)
{
	fTypeEntry = entry;
}


/**
 * @brief Reports the unit kind to upstream code.
 *
 * @return Always @c dwarf_unit_kind_type.
 */
dwarf_unit_kind
TypeUnit::Kind() const
{
	return dwarf_unit_kind_type;
}
