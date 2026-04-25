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
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DwarfFunctionDebugInfo.cpp
 * @brief Implementation of DwarfFunctionDebugInfo, a FunctionDebugInfo backed
 *        by a DWARF DIESubprogram and its address ranges.
 *
 * Each instance binds a function as described in the DWARF tree to the
 * surrounding compilation unit, the owning DwarfImageDebugInfo, and the
 * located source file. Address queries account for the image's relocation
 * delta so callers can compare against runtime addresses.
 *
 * @see FunctionDebugInfo, DwarfImageDebugInfo
 */


#include "DwarfFunctionDebugInfo.h"

#include "DebugInfoEntries.h"
#include "DwarfImageDebugInfo.h"
#include "LocatableFile.h"
#include "TargetAddressRangeList.h"


/**
 * @brief Constructs a DWARF-backed function descriptor and acquires
 *        references on its dependencies.
 *
 * @param imageDebugInfo    Owning DwarfImageDebugInfo; reference acquired.
 * @param compilationUnit   CU containing the subprogram entry.
 * @param subprogramEntry   The DIESubprogram describing this function.
 * @param addressRanges     PC ranges covered by the function;
 *                          reference acquired.
 * @param name              Function name as recorded in DWARF.
 * @param sourceFile        Located source file; may be @c NULL. Reference
 *                          acquired when non-null.
 * @param sourceLocation    Declaration line/column within @a sourceFile.
 */
DwarfFunctionDebugInfo::DwarfFunctionDebugInfo(
	DwarfImageDebugInfo* imageDebugInfo, CompilationUnit* compilationUnit,
	DIESubprogram* subprogramEntry, TargetAddressRangeList* addressRanges,
	const BString& name, LocatableFile* sourceFile,
	const SourceLocation& sourceLocation)
	:
	fImageDebugInfo(imageDebugInfo),
	fCompilationUnit(compilationUnit),
	fSubprogramEntry(subprogramEntry),
	fAddressRanges(addressRanges),
	fName(name),
	fSourceFile(sourceFile),
	fSourceLocation(sourceLocation)
{
	fImageDebugInfo->AcquireReference();
	fAddressRanges->AcquireReference();

	if (fSourceFile != NULL)
		fSourceFile->AcquireReference();
}


/**
 * @brief Destroys the descriptor and releases all held references.
 */
DwarfFunctionDebugInfo::~DwarfFunctionDebugInfo()
{
	if (fSourceFile != NULL)
		fSourceFile->ReleaseReference();

	fAddressRanges->ReleaseReference();
	fImageDebugInfo->ReleaseReference();
}


/**
 * @brief Returns the owning DwarfImageDebugInfo.
 *
 * @return Borrowed pointer to the SpecificImageDebugInfo (a
 *         DwarfImageDebugInfo) supplied at construction.
 */
SpecificImageDebugInfo*
DwarfFunctionDebugInfo::GetSpecificImageDebugInfo() const
{
	return fImageDebugInfo;
}


/**
 * @brief Returns the relocated start address of the function.
 *
 * @return The lowest address of the function's PC ranges adjusted by the
 *         image's relocation delta so it matches addresses observed at run
 *         time.
 */
target_addr_t
DwarfFunctionDebugInfo::Address() const
{
	return fAddressRanges->LowestAddress() + fImageDebugInfo->RelocationDelta();
}


/**
 * @brief Returns the total byte length of the function.
 *
 * @return The size of the address range that covers all PC ranges declared
 *         for this function.
 */
target_size_t
DwarfFunctionDebugInfo::Size() const
{
	return fAddressRanges->CoveringRange().Size();
}


/**
 * @brief Returns the function name as recorded in DWARF.
 *
 * @return Reference to the stored name string.
 */
const BString&
DwarfFunctionDebugInfo::Name() const
{
	return fName;
}


/**
 * @brief Returns a human-friendly form of the name.
 *
 * @return The same value as Name(); DWARF names are already in a readable
 *         form so no demangling is performed here.
 */
const BString&
DwarfFunctionDebugInfo::PrettyName() const
{
	return fName;
}


/**
 * @brief Reports whether this is the program's @c main entry point.
 *
 * @return Result of the underlying DIESubprogram::IsMain() flag.
 */
bool
DwarfFunctionDebugInfo::IsMain() const
{
	return fSubprogramEntry->IsMain();
}


/**
 * @brief Returns the source file declaring the function.
 *
 * @return The LocatableFile recorded at construction, or @c NULL if not
 *         known.
 */
LocatableFile*
DwarfFunctionDebugInfo::SourceFile() const
{
	return fSourceFile;
}


/**
 * @brief Returns the source location where the function declaration begins.
 *
 * @return The stored declaration SourceLocation.
 */
SourceLocation
DwarfFunctionDebugInfo::SourceStartLocation() const
{
	return fSourceLocation;
}


/**
 * @brief Returns the source location where the function declaration ends.
 *
 * @return The same SourceLocation as the start; DWARF only exposes a single
 *         declaration line for this view.
 */
SourceLocation
DwarfFunctionDebugInfo::SourceEndLocation() const
{
	return fSourceLocation;
}
