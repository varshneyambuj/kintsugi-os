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

/** @file AttributeValue.h
    @brief Discriminated-union wrappers describing parsed DWARF DIE attribute values. */

#ifndef ATTRIBUTE_VALUE_H
#define ATTRIBUTE_VALUE_H

#include "AttributeClasses.h"
#include "Types.h"


class DebugInfoEntry;


/**
 * @brief Discriminated union covering every DWARF attribute-class payload.
 *
 * Used during parsing to deliver an attribute's value to its owning DIE,
 * which then routes it into its specialised typed storage.
 */
struct AttributeValue {
	union {
		target_addr_t		address;
		struct {
			const void*		data;
			off_t			length;
		}					block;
		uint64				constant;
		bool				flag;
		off_t				pointer;
		DebugInfoEntry*		reference;
		const char*			string;
	};

	uint16				attributeForm;
	uint8				attributeClass;
	bool				isSigned;

	AttributeValue()
		:
		attributeClass(ATTRIBUTE_CLASS_UNKNOWN)
	{
	}

	~AttributeValue()
	{
		Unset();
	}

	void SetToAddress(target_addr_t address)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_ADDRESS;
		this->address = address;
	}

	void SetToAddrPtr(off_t value)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_ADDRPTR;
		this->pointer = value;
	}

	void SetToBlock(const void* data, off_t length)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_BLOCK;
		block.data = data;
		block.length = length;
	}

	void SetToConstant(uint64 value, bool isSigned)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_CONSTANT;
		this->constant = value;
		this->isSigned = isSigned;
	}

	void SetToFlag(bool value)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_FLAG;
		this->flag = value;
	}

	void SetToLinePointer(off_t value)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_LINEPTR;
		this->pointer = value;
	}

	void SetToLocationList(off_t value)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_LOCLIST;
		this->pointer = value;
	}

	void SetToLocationListPointer(off_t value)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_LOCLISTPTR;
		this->pointer = value;
	}

	void SetToMacroPointer(off_t value)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_MACPTR;
		this->pointer = value;
	}

	void SetToRangeList(off_t value)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_RANGELIST;
		this->pointer = value;
	}

	void SetToRangeListPointer(off_t value)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_RANGELISTPTR;
		this->pointer = value;
	}

	void SetToReference(DebugInfoEntry* entry)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_REFERENCE;
		this->reference = entry;
	}

	void SetToString(const char* string)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_STRING;
		this->string = string;
	}

	void SetToStrOffsetsPtr(off_t value)
	{
		Unset();
		attributeClass = ATTRIBUTE_CLASS_STROFFSETSPTR;
		this->pointer = value;
	}

	void Unset()
	{
		attributeClass = ATTRIBUTE_CLASS_UNKNOWN;
	}

	const char* ToString(char* buffer, size_t size);
};


/**
 * @brief Smaller value carrier for attributes that may resolve dynamically.
 *
 * Stores a constant, a DIE reference, or a block; used for attributes
 * whose value can be a constant expression or a DIE-bound expression.
 */
struct DynamicAttributeValue {
	union {
		uint64				constant;
		DebugInfoEntry*		reference;
		struct {
			const void*		data;
			off_t			length;
		}					block;
	};
	uint8				attributeClass;

	DynamicAttributeValue()
		:
		attributeClass(ATTRIBUTE_CLASS_UNKNOWN)
	{
		this->constant = 0;
	}

	bool IsValid() const
	{
		return attributeClass != ATTRIBUTE_CLASS_UNKNOWN;
	}

	void SetTo(uint64 constant)
	{
		this->constant = constant;
		attributeClass = ATTRIBUTE_CLASS_CONSTANT;
	}

	void SetTo(DebugInfoEntry* reference)
	{
		this->reference = reference;
		attributeClass = ATTRIBUTE_CLASS_REFERENCE;
	}

	void SetTo(const void* data, off_t length)
	{
		block.data = data;
		block.length = length;
		attributeClass = ATTRIBUTE_CLASS_BLOCK;
	}
};


/**
 * @brief Compile-time constant attribute value (constant, string, or block).
 */
struct ConstantAttributeValue {
	union {
		uint64				constant;
		const char*			string;
		struct {
			const void*		data;
			off_t			length;
		}					block;
	};
	uint8				attributeClass;

	ConstantAttributeValue()
		:
		attributeClass(ATTRIBUTE_CLASS_UNKNOWN)
	{
	}

	bool IsValid() const
	{
		return attributeClass != ATTRIBUTE_CLASS_UNKNOWN;
	}

	void SetTo(uint64 constant)
	{
		this->constant = constant;
		attributeClass = ATTRIBUTE_CLASS_CONSTANT;
	}

	void SetTo(const char* string)
	{
		this->string = string;
		attributeClass = ATTRIBUTE_CLASS_STRING;
	}

	void SetTo(const void* data, off_t length)
	{
		block.data = data;
		block.length = length;
		attributeClass = ATTRIBUTE_CLASS_BLOCK;
	}
};


/**
 * @brief Member-offset description: constant, expression, or location list.
 *
 * Mirrors the three possible forms of DW_AT_data_member_location.
 */
struct MemberLocation {
	union {
		uint64				constant;
		off_t				listOffset;
		struct {
			const void*		data;
			off_t			length;
		}					expression;
	};
	uint8				attributeClass;

	MemberLocation()
		:
		attributeClass(ATTRIBUTE_CLASS_UNKNOWN)
	{
	}

	bool IsValid() const
	{
		return attributeClass != ATTRIBUTE_CLASS_UNKNOWN;
	}

	bool IsConstant() const
	{
		return attributeClass == ATTRIBUTE_CLASS_CONSTANT;
	}

	bool IsExpression() const
	{
		return attributeClass == ATTRIBUTE_CLASS_BLOCK
			&& expression.data != NULL;
	}

	bool IsLocationList() const
	{
		return attributeClass == ATTRIBUTE_CLASS_LOCLIST;
	}

	void SetToConstant(uint64 constant)
	{
		this->constant = constant;
		attributeClass = ATTRIBUTE_CLASS_CONSTANT;
	}

	void SetToExpression(const void* data, off_t length)
	{
		expression.data = data;
		expression.length = length;
		attributeClass = ATTRIBUTE_CLASS_BLOCK;
	}

	void SetToLocationList(off_t listOffset)
	{
		this->listOffset = listOffset;
		attributeClass = ATTRIBUTE_CLASS_LOCLIST;
	}
};


/**
 * @brief Location description: either an inline expression or a location list.
 *
 * Mirrors the two forms of DW_AT_location.
 */
struct LocationDescription {
	union {
		off_t			listOffset;	// location list
		struct {
			const void*	data;
			off_t		length;
		}				expression;	// location expression
	};
	uint8				attributeClass;

	LocationDescription()
		:
		attributeClass(ATTRIBUTE_CLASS_BLOCK)
	{
		expression.data = NULL;
		expression.length = 0;
	}

	bool IsExpression() const
	{
		return attributeClass == ATTRIBUTE_CLASS_BLOCK
			&& expression.data != NULL;
	}

	bool IsLocationList() const
	{
		return attributeClass == ATTRIBUTE_CLASS_LOCLIST;
	}

	bool IsValid() const
	{
		return IsExpression() || IsLocationList();
	}

	void SetToLocationList(off_t offset)
	{
		listOffset = offset;
		attributeClass = ATTRIBUTE_CLASS_LOCLIST;
	}

	void SetToExpression(const void* data, off_t length)
	{
		expression.data = data;
		expression.length = length;
		attributeClass = ATTRIBUTE_CLASS_BLOCK;
	}
};


/**
 * @brief Source-file declaration location (file/line/column triple).
 *
 * Stores the DW_AT_decl_file / DW_AT_decl_line / DW_AT_decl_column
 * attributes as 0xffffffff-sentinelled fields.
 */
struct DeclarationLocation {
	uint32	file;
	uint32	line;
	uint32	column;

	DeclarationLocation()
		:
		file(0xffffffff),
		line(0xffffffff),
		column(0xffffffff)
	{
	}

	void SetFile(uint32 file)
	{
		this->file = file;
	}

	void SetLine(uint32 line)
	{
		this->line = line;
	}

	void SetColumn(uint32 column)
	{
		this->column = column;
	}

	bool IsFileSet() const
	{
		return file != 0xffffffff;
	}

	bool IsLineSet() const
	{
		return line != 0xffffffff;
	}

	bool IsColumnSet() const
	{
		return column != 0xffffffff;
	}
};

#endif	// ATTRIBUTE_VALUE_H
