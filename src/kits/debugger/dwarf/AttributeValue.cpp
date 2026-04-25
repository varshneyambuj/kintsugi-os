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
 *   Copyright 2009-2012, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file AttributeValue.cpp
 * @brief Pretty-printer for parsed DWARF DIE attribute values.
 *
 * Each DIE attribute carries a typed value belonging to one of the DWARF
 * attribute classes (address, block, constant, flag, reference, ...).
 * AttributeValue is the discriminated-union wrapper used during parsing;
 * the @ref AttributeValue::ToString helper here renders a debug-friendly
 * string used by diagnostic logging and the debug_info dumper.
 */

#include "AttributeValue.h"

#include <stdio.h>

#include "AttributeClasses.h"


/**
 * @brief Formats this attribute value into @a buffer for diagnostic output.
 *
 * The chosen rendering depends on @c attributeClass; addresses and
 * pointers are printed in hex, references as raw pointer values, strings
 * in quotes, and unknown classes as the literal "<unknown>".
 *
 * @param buffer Caller-provided output buffer of at least @a size bytes.
 * @param size   Capacity of @a buffer in bytes.
 * @return Pointer to a NUL-terminated string; either @a buffer or a
 *         static literal for the unknown case.
 */
const char*
AttributeValue::ToString(char* buffer, size_t size)
{
	switch (attributeClass) {
		case ATTRIBUTE_CLASS_ADDRESS:
			snprintf(buffer, size, "%#" B_PRIx64, address);
			return buffer;
		case ATTRIBUTE_CLASS_BLOCK:
			snprintf(buffer, size, "(%p, %#" B_PRIx64 ")", block.data,
				block.length);
			return buffer;
		case ATTRIBUTE_CLASS_CONSTANT:
			snprintf(buffer, size, "%#" B_PRIx64, constant);
			return buffer;
		case ATTRIBUTE_CLASS_FLAG:
			snprintf(buffer, size, "%s", flag ? "true" : "false");
			return buffer;
		case ATTRIBUTE_CLASS_ADDRPTR:
		case ATTRIBUTE_CLASS_LINEPTR:
		case ATTRIBUTE_CLASS_LOCLIST:
		case ATTRIBUTE_CLASS_LOCLISTPTR:
		case ATTRIBUTE_CLASS_MACPTR:
		case ATTRIBUTE_CLASS_RANGELIST:
		case ATTRIBUTE_CLASS_RANGELISTPTR:
		case ATTRIBUTE_CLASS_STROFFSETSPTR:
			snprintf(buffer, size, "%#" B_PRIx64, pointer);
			return buffer;
		case ATTRIBUTE_CLASS_REFERENCE:
			snprintf(buffer, size, "%p", reference);
			return buffer;
		case ATTRIBUTE_CLASS_STRING:
			snprintf(buffer, size, "\"%s\"", string);
			return buffer;

		default:
		case ATTRIBUTE_CLASS_UNKNOWN:
			return "<unknown>";
	}

	return buffer;
}
