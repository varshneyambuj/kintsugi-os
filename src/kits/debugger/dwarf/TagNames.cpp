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
 *   Copyright 2013, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file TagNames.cpp
 * @brief Mapping from DW_TAG_* numeric tag codes to their canonical name strings.
 *
 * The DWARF specification (v2 through v5) defines a fixed enumeration of tag
 * codes identifying each kind of debug information entry (DIE).  This
 * translation unit provides a sparse lookup table covering both the standard
 * range (up to DW_TAG_immutable_type) and the GNU vendor extensions used by
 * GCC (DW_TAG_GNU_*).  The table is consulted by debugger UI and diagnostics
 * to render readable names for each DIE.
 */

#include "TagNames.h"

#include "Dwarf.h"


/**
 * @brief Single (tag-code, tag-name) pair used to populate the lookup table.
 */
struct tag_name_info {
	uint16					tag;
	const char*				name;
};


#undef ENTRY
#define ENTRY(name)	{ DW_TAG_##name, "DW_TAG_" #name }

/** @brief Static list of all known DW_TAG_* codes paired with their names. */
static const tag_name_info kTagNameInfos[] = {
	ENTRY(array_type),
	ENTRY(class_type),
	ENTRY(entry_point),
	ENTRY(enumeration_type),
	ENTRY(formal_parameter),
	ENTRY(imported_declaration),
	ENTRY(label),
	ENTRY(lexical_block),
	ENTRY(member),
	ENTRY(pointer_type),
	ENTRY(reference_type),
	ENTRY(compile_unit),
	ENTRY(string_type),
	ENTRY(structure_type),
	ENTRY(subroutine_type),
	ENTRY(typedef),
	ENTRY(union_type),
	ENTRY(unspecified_parameters),
	ENTRY(variant),
	ENTRY(common_block),
	ENTRY(common_inclusion),
	ENTRY(inheritance),
	ENTRY(inlined_subroutine),
	ENTRY(module),
	ENTRY(ptr_to_member_type),
	ENTRY(set_type),
	ENTRY(subrange_type),
	ENTRY(with_stmt),
	ENTRY(access_declaration),
	ENTRY(base_type),
	ENTRY(catch_block),
	ENTRY(const_type),
	ENTRY(constant),
	ENTRY(enumerator),
	ENTRY(file_type),
	ENTRY(friend),
	ENTRY(namelist),
	ENTRY(namelist_item),
	ENTRY(packed_type),
	ENTRY(subprogram),
	ENTRY(template_type_parameter),
	ENTRY(template_value_parameter),
	ENTRY(thrown_type),
	ENTRY(try_block),
	ENTRY(variant_part),
	ENTRY(variable),
	ENTRY(volatile_type),
	ENTRY(dwarf_procedure),
	ENTRY(restrict_type),
	ENTRY(interface_type),
	ENTRY(namespace),
	ENTRY(imported_module),
	ENTRY(unspecified_type),
	ENTRY(partial_unit),
	ENTRY(imported_unit),
	ENTRY(condition),
	ENTRY(shared_type),
	ENTRY(type_unit),
	ENTRY(rvalue_reference_type),
	ENTRY(template_alias),
	ENTRY(coarray_type),
	ENTRY(generic_subrange),
	ENTRY(dynamic_type),
	ENTRY(atomic_type),
	ENTRY(call_site),
	ENTRY(call_site_parameter),
	ENTRY(skeleton_unit),
	ENTRY(immutable_type),
	ENTRY(GNU_template_parameter_pack),
	ENTRY(GNU_formal_parameter_pack),
	ENTRY(GNU_call_site),
	ENTRY(GNU_call_site_parameter),
	{}
};


/** @brief Size of the dense @c sTagNames index table (standard range plus GNU slots). */
static const uint32 kTagNameInfoCount = DW_TAG_immutable_type + 5;
/** @brief Dense lookup table indexed by tag code (or remapped GNU slot). */
static const char* sTagNames[kTagNameInfoCount];

/**
 * @brief One-shot initialiser that fills @ref sTagNames at static-init time.
 */
static struct InitTagNames {
	InitTagNames()
	{
		for (uint32 i = 0; kTagNameInfos[i].name != NULL; i++) {
			const tag_name_info& info = kTagNameInfos[i];
			if (info.tag <= DW_TAG_immutable_type)
				sTagNames[info.tag] = info.name;
			else {
				sTagNames[DW_TAG_immutable_type + 1 + (info.tag
						- DW_TAG_GNU_template_parameter_pack)] = info.name;
			}
		}
	}
} sInitTagNames;


/**
 * @brief Resolves the human-readable DW_TAG_* name for a numeric tag code.
 *
 * @param tag DWARF tag code as encoded in a DIE abbreviation.
 * @return Pointer to a static string of the form "DW_TAG_xxx", or NULL if
 *         the tag falls outside both the standard DWARF range and the
 *         supported GNU vendor extension range.
 */
const char*
get_entry_tag_name(uint16 tag)
{
	if (tag <= DW_TAG_immutable_type)
		return sTagNames[tag];
	else if (tag >= DW_TAG_GNU_template_parameter_pack
		&& tag <= DW_TAG_GNU_call_site_parameter) {
		return sTagNames[DW_TAG_immutable_type + 1 + (tag
				- DW_TAG_GNU_template_parameter_pack)];
	}

	return NULL;
}
