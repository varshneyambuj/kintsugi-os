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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2014, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/** @file Register.cpp
    @brief Architecture-neutral CPU register descriptor used by Architecture subclasses. */


#include "Register.h"

#include <TypeConstants.h>


/**
 * @brief Construct a register descriptor and infer its display format from the value type.
 *
 * Integer types yield REGISTER_FORMAT_INTEGER, floating-point types yield
 * REGISTER_FORMAT_FLOAT, and B_RAW_TYPE yields REGISTER_FORMAT_SIMD.
 *
 * @param index            Architecture-specific register index.
 * @param name             Display name (e.g. "rax", "eip"). Storage is referenced, not copied.
 * @param bitSize          Width of the register in bits.
 * @param valueType        BVariant type code describing the natural value type.
 * @param type             Semantic role (instruction pointer, stack pointer, etc.).
 * @param calleePreserved  true if callees must save and restore the register.
 */
Register::Register(int32 index, const char* name, uint32 bitSize,
	uint32 valueType, register_type type, bool calleePreserved)
	:
	fIndex(index),
	fName(name),
	fBitSize(bitSize),
	fValueType(valueType),
	fType(type),
	fCalleePreserved(calleePreserved)
{
	switch (fValueType) {
		case B_INT8_TYPE:
		case B_UINT8_TYPE:
		case B_INT16_TYPE:
		case B_UINT16_TYPE:
		case B_INT32_TYPE:
		case B_UINT32_TYPE:
		case B_INT64_TYPE:
		case B_UINT64_TYPE:
			fFormat = REGISTER_FORMAT_INTEGER;
			break;
		case B_FLOAT_TYPE:
		case B_DOUBLE_TYPE:
			fFormat = REGISTER_FORMAT_FLOAT;
			break;
		case B_RAW_TYPE:
			fFormat = REGISTER_FORMAT_SIMD;
			break;
		default:
			fFormat = REGISTER_FORMAT_INTEGER;
			break;
	}
}
