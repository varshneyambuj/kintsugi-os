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
 * MIT License. Copyright 2009, Haiku.
 * Original authors: Ingo Weinhold.
 */

/** @file DwarfFunctionDebugInfo.h
    @brief FunctionDebugInfo subclass that wraps a DWARF DIESubprogram and
           its associated address ranges. */

#ifndef DWARF_FUNCTION_DEBUG_INFO_H
#define DWARF_FUNCTION_DEBUG_INFO_H

#include <String.h>

#include "FunctionDebugInfo.h"
#include "SourceLocation.h"


class CompilationUnit;
class DIESubprogram;
class DwarfImageDebugInfo;
class TargetAddressRangeList;


/** @brief FunctionDebugInfo describing a function recorded in DWARF, bound
           to its compilation unit, subprogram entry and source file. */
class DwarfFunctionDebugInfo : public FunctionDebugInfo {
public:
								DwarfFunctionDebugInfo(
									DwarfImageDebugInfo* imageDebugInfo,
									CompilationUnit* compilationUnit,
									DIESubprogram* subprogramEntry,
									TargetAddressRangeList* addressRanges,
									const BString& name,
									LocatableFile* sourceFile,
									const SourceLocation& sourceLocation);
	virtual						~DwarfFunctionDebugInfo();

	virtual	SpecificImageDebugInfo* GetSpecificImageDebugInfo() const;
	virtual	target_addr_t		Address() const;
	virtual	target_size_t		Size() const;
	virtual	const BString&		Name() const;
	virtual	const BString&		PrettyName() const;

	virtual	bool				IsMain() const;

	virtual	LocatableFile*		SourceFile() const;
	virtual	SourceLocation		SourceStartLocation() const;
	virtual	SourceLocation		SourceEndLocation() const;

			/** @brief Returns the compilation unit that contains this
			           function's DIE. */
			CompilationUnit*	GetCompilationUnit() const
									{ return fCompilationUnit; }
			/** @brief Returns the DIESubprogram describing this function. */
			DIESubprogram*		SubprogramEntry() const
									{ return fSubprogramEntry; }

private:
			DwarfImageDebugInfo* fImageDebugInfo;
			CompilationUnit*	fCompilationUnit;
			DIESubprogram*		fSubprogramEntry;
			TargetAddressRangeList* fAddressRanges;
			BString				fName;
			LocatableFile*		fSourceFile;
			SourceLocation		fSourceLocation;
};


#endif	// DWARF_FUNCTION_DEBUG_INFO_H
