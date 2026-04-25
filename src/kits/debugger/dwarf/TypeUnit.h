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
 * MIT License. Copyright 2013, Rene Gollent.
 */

/** @file TypeUnit.h
    @brief BaseUnit specialisation modelling a DWARF .debug_types type unit. */

#ifndef TYPE_UNIT_H
#define TYPE_UNIT_H


#include <ObjectList.h>
#include <String.h>

#include "BaseUnit.h"


class DIETypeUnit;


/**
 * @brief Type unit deduplicated across translation units by a 64-bit signature.
 *
 * Type units (DWARF v4 .debug_types or DWARF v5 DW_UT_type) carry a single
 * canonical type definition referenced by other compilation units via the
 * @c Signature value.
 */
class TypeUnit : public BaseUnit {
public:
								TypeUnit(off_t headerOffset,
									off_t contentOffset,
									off_t totalSize,
									off_t abbreviationOffset,
									off_t typeOffset,
									uint8 addressSize,
									bool isBigEndian,
									uint64 typeSignature,
									bool isDwarf64);
								~TypeUnit();

			uint64				Signature() const	{ return fSignature; }

			off_t				TypeOffset() const	{ return fTypeOffset; }


			DIETypeUnit*		UnitEntry() const	{ return fUnitEntry; }
			void				SetUnitEntry(DIETypeUnit* entry);

			DebugInfoEntry*		TypeEntry() const;
			void				SetTypeEntry(DebugInfoEntry* entry);

	virtual	dwarf_unit_kind		Kind() const;

private:
			DIETypeUnit*		fUnitEntry;
			DebugInfoEntry*		fTypeEntry;
			uint64				fSignature;
			off_t				fTypeOffset;
};


/** @brief Hash-table node mapping a 64-bit type signature to its TypeUnit. */
struct TypeUnitTableEntry {
	uint64					signature;
	TypeUnit*				unit;
	TypeUnitTableEntry*		next;

	TypeUnitTableEntry(uint64 signature, TypeUnit* unit)
		:
		signature(signature),
		unit(unit)
	{
	}
};


/** @brief Hash-table policy used by the global type-unit signature table. */
struct TypeUnitTableHashDefinition {
	typedef uint64					KeyType;
	typedef	TypeUnitTableEntry		ValueType;

	size_t HashKey(uint64 key) const
	{
		return (size_t)key;
	}

	size_t Hash(TypeUnitTableEntry* value) const
	{
		return HashKey(value->signature);
	}

	bool Compare(uint64 key, TypeUnitTableEntry* value) const
	{
		return value->signature == key;
	}

	TypeUnitTableEntry*& GetLink(TypeUnitTableEntry* value) const
	{
		return value->next;
	}
};


#endif	// TYPE_UNIT_H
