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
 *   Copyright 2011-2013, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DwarfUtils.cpp
 * @brief Free helpers that walk DIE chains to extract names and locations.
 *
 * Many DWARF DIEs do not carry a name directly: they reference an
 * abstract-origin or specification DIE that does.  These helpers chase
 * those references uniformly and also synthesise C++-style names
 * (qualified, parameter list, modifier suffix, ...) from a DIE tree.
 */

#include "DwarfUtils.h"

#include <String.h>

#include "CompilationUnit.h"
#include "Dwarf.h"
#include "DwarfFile.h"


/**
 * @brief Extracts the simple (unqualified) DIE name into @a _name.
 *
 * Falls back to @c DW_AT_abstract_origin and then @c DW_AT_specification
 * when @a entry has no direct DW_AT_name.
 *
 * @param entry  DIE to inspect.
 * @param _name  Output BString receiving the name (cleared if not found).
 */
/*static*/ void
DwarfUtils::GetDIEName(const DebugInfoEntry* entry, BString& _name)
{
	// If we don't seem to have a name but an abstract origin, return the
	// origin's name.
	const char* name = entry->Name();
	if (name == NULL) {
		if (DebugInfoEntry* abstractOrigin = entry->AbstractOrigin()) {
			entry = abstractOrigin;
			name = entry->Name();
		}
	}

	// If we still don't have a name but a specification, return the
	// specification's name.
	if (name == NULL) {
		if (DebugInfoEntry* specification = entry->Specification()) {
			entry = specification;
			name = entry->Name();
		}
	}

	_name = name;
}


/**
 * @brief Synthesises a human-readable type name including modifiers.
 *
 * Walks the chain of DIEModifiedType wrappers (DW_TAG_pointer_type,
 * DW_TAG_reference_type, DW_TAG_const_type) and prepends or suffixes
 * modifier glyphs to produce names like "int * const".
 *
 * @param entry           Root type DIE.
 * @param _name           Output BString that receives the name.
 * @param requestingEntry Optional DIE whose namespace should be elided.
 */
/*static*/ void
DwarfUtils::GetDIETypeName(const DebugInfoEntry* entry, BString& _name,
	const DebugInfoEntry* requestingEntry)
{
	const DIEType* type = dynamic_cast<const DIEType*>(entry);
	if (type == NULL)
		return;

	const DIEModifiedType* modifiedType = dynamic_cast<const DIEModifiedType*>(
		type);
	BString typeName;
	BString modifier;

	if (modifiedType != NULL) {
		const DIEType* baseType = type;
		while ((modifiedType = dynamic_cast<const DIEModifiedType*>(
			baseType)) != NULL) {
			switch (modifiedType->Tag()) {
				case DW_TAG_pointer_type:
					modifier.Prepend("*");
					break;
				case DW_TAG_reference_type:
					modifier.Prepend("&");
					break;
				case DW_TAG_const_type:
					modifier.Prepend(" const ");
					break;
				default:
					break;
			}

			baseType = modifiedType->GetType();
		}
		type = baseType;
	}

	// if the parameter has no type associated,
	// then it's the unspecified type.
	if (type == NULL)
		typeName = "void";
	else
		GetFullyQualifiedDIEName(type, typeName, requestingEntry);

	if (modifier.Length() > 0) {
		if (modifier[modifier.Length() - 1] == ' ')
			modifier.Truncate(modifier.Length() - 1);

		// if the modifier has a leading const, treat it
		// as the degenerate case and prepend it to the
		// type name since that's the more typically used
		// representation in source
		if (modifier[0] == ' ') {
			typeName.Prepend("const ");
			modifier.Remove(0, 7);
		}
		typeName += modifier;
	}

	_name = typeName;
}


/**
 * @brief Builds a "name(parameters)" rendering for subprogram DIEs.
 *
 * For non-subprogram DIEs it returns the simple name (with the same
 * abstract-origin / specification fallback as @ref GetDIEName).
 * For subprograms it walks the formal-parameter children and renders
 * each parameter type, skipping artificial parameters ("this") and
 * substituting "..." for unspecified parameters.
 *
 * @param entry DIE to inspect.
 * @param _name Output BString that receives the name.
 */
/*static*/ void
DwarfUtils::GetFullDIEName(const DebugInfoEntry* entry, BString& _name)
{
	BString generatedName;
	// If we don't seem to have a name but an abstract origin, return the
	// origin's name.
	const char* name = entry->Name();
	if (name == NULL) {
		if (DebugInfoEntry* abstractOrigin = entry->AbstractOrigin()) {
			entry = abstractOrigin;
			name = entry->Name();
		}
	}

	// If we still don't have a name but a specification, return the
	// specification's name.
	if (name == NULL) {
		if (DebugInfoEntry* specification = entry->Specification()) {
			entry = specification;
			name = entry->Name();
		}
	}

	if (name == NULL) {
		if (dynamic_cast<const DIEModifiedType*>(entry) != NULL)
			GetDIETypeName(entry, _name);

		// we found no name for this entry whatsoever, abort.
		return;
	}

	generatedName = name;

	const DIESubprogram* subProgram = dynamic_cast<const DIESubprogram*>(
		entry);
	if (subProgram != NULL) {
		generatedName += "(";
		BString parameters;
		DebugInfoEntryList::ConstIterator iterator
			= subProgram->Parameters().GetIterator();

		bool firstParameter = true;
		while (iterator.HasNext()) {
			DebugInfoEntry* parameterEntry = iterator.Next();
			if (dynamic_cast<DIEUnspecifiedParameters*>(parameterEntry)
				!= NULL) {
				parameters += ", ...";
				continue;
			}

			const DIEFormalParameter* parameter
				= dynamic_cast<DIEFormalParameter*>(parameterEntry);
			if (parameter == NULL) {
				// this shouldn't happen
				return;
			}

			if (parameter->IsArtificial())
				continue;

			BString paramName;
			BString modifier;
			DIEType* type = parameter->GetType();
			GetDIETypeName(type, paramName, entry);

			if (firstParameter)
				firstParameter = false;
			else
				parameters += ", ";

			parameters += paramName;
		}

		if (parameters.Length() > 0)
			generatedName += parameters;
		else
			generatedName += "void";
		generatedName += ")";
	}
	_name = generatedName;
}


/**
 * @brief Builds a "Namespace::...::name" rendering for the DIE.
 *
 * Walks parents looking for namespace-bearing DIEs and prefixes their
 * names with "::".  Stops early if @a requestingEntry is encountered so
 * that names are rendered relative to the caller's context.
 *
 * @param entry           DIE whose qualified name is requested.
 * @param _name           Output BString that receives the name.
 * @param requestingEntry Optional DIE marking the relative-name root.
 */
/*static*/ void
DwarfUtils::GetFullyQualifiedDIEName(const DebugInfoEntry* entry,
	BString& _name, const DebugInfoEntry* requestingEntry)
{
	// If we don't seem to have a name but an abstract origin, return the
	// origin's name.
	if (entry->Name() == NULL) {
		if (DebugInfoEntry* abstractOrigin = entry->AbstractOrigin())
			entry = abstractOrigin;
	}

	// If we don't still don't have a name but a specification, get the
	// specification's name.
	if (entry->Name() == NULL) {
		if (DebugInfoEntry* specification = entry->Specification())
			entry = specification;
	}

	_name.Truncate(0);
	BString generatedName;

	// Get the namespace, if any.
	DebugInfoEntry* parent = entry->Parent();
	while (parent != NULL) {
		if (parent == requestingEntry)
			break;
		if (parent->IsNamespace()) {
			BString parentName;
			GetFullyQualifiedDIEName(parent, parentName);
			if (parentName.Length() > 0) {
				parentName += "::";
				generatedName.Prepend(parentName);
			}
			break;
		}

		parent = parent->Parent();
	}

	BString name;
	GetFullDIEName(entry, name);
	if (name.Length() == 0)
		return;

	generatedName += name;

	_name = generatedName;
}


/**
 * @brief Resolves a DIE's declaration location to (directory, file, line, col).
 *
 * Reads DW_AT_decl_file / DW_AT_decl_line / DW_AT_decl_column from
 * @a entry and falls back to its abstract-origin and specification DIEs
 * for any value that is missing.  The file index is then resolved
 * against the owning compilation unit's file table.
 *
 * @param dwarfFile   DWARF file containing @a entry.
 * @param entry       DIE whose declaration location is requested.
 * @param _directory  Output directory string (owned by the CU file table).
 * @param _file       Output file name string (owned by the CU file table).
 * @param _line       Output line number, zero-based.
 * @param _column     Output column number, zero-based.
 * @return @c true if a complete declaration location was resolved,
 *         @c false if the file index was missing or could not be looked up.
 */
/*static*/ bool
DwarfUtils::GetDeclarationLocation(DwarfFile* dwarfFile,
	const DebugInfoEntry* entry, const char*& _directory, const char*& _file,
	int32& _line, int32& _column)
{
	uint32 file = 0;
	uint32 line = 0;
	uint32 column = 0;
	bool fileSet = entry->GetDeclarationFile(file);
	bool lineSet = entry->GetDeclarationLine(line);
	bool columnSet = entry->GetDeclarationColumn(column);

	// if something is not set yet, try the abstract origin (if any)
	if (!fileSet || !lineSet || !columnSet) {
		if (DebugInfoEntry* abstractOrigin = entry->AbstractOrigin()) {
			entry = abstractOrigin;
			if (!fileSet)
				fileSet = entry->GetDeclarationFile(file);
			if (!lineSet)
				lineSet = entry->GetDeclarationLine(line);
			if (!columnSet)
				columnSet = entry->GetDeclarationColumn(column);
		}
	}

	// something is not set yet, try the specification (if any)
	if (!fileSet || !lineSet || !columnSet) {
		if (DebugInfoEntry* specification = entry->Specification()) {
			entry = specification;
			if (!fileSet)
				fileSet = entry->GetDeclarationFile(file);
			if (!lineSet)
				lineSet = entry->GetDeclarationLine(line);
			if (!columnSet)
				columnSet = entry->GetDeclarationColumn(column);
		}
	}

	if (file == 0)
		return false;

	// get the compilation unit
	CompilationUnit* unit = dwarfFile->CompilationUnitForDIE(entry);
	if (unit == NULL)
		return false;

	const char* directoryName;
	const char* fileName = unit->FileAt(file - 1, &directoryName);
	if (fileName == NULL)
		return false;

	_directory = directoryName;
	_file = fileName;
	_line = (int32)line - 1;
	_column = (int32)column - 1;
	return true;
}
