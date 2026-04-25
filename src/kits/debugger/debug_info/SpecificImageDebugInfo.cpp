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
 * @file SpecificImageDebugInfo.cpp
 * @brief Implementation of common SpecificImageDebugInfo helpers.
 *
 * The class itself is abstract (subclassed by DwarfImageDebugInfo and
 * DebuggerImageDebugInfo). This translation unit defines the virtual
 * destructor and a static helper that lifts a symbol-table view into
 * BasicFunctionDebugInfo descriptors usable by every backend.
 *
 * @see DwarfImageDebugInfo, DebuggerImageDebugInfo
 */


#include "SpecificImageDebugInfo.h"

#include "BasicFunctionDebugInfo.h"
#include "DebuggerInterface.h"
#include "Demangler.h"
#include "ImageInfo.h"
#include "SymbolInfo.h"


/**
 * @brief Virtual destructor for safe polymorphic deletion.
 */
SpecificImageDebugInfo::~SpecificImageDebugInfo()
{
}


/**
 * @brief Builds BasicFunctionDebugInfo objects from a symbol list.
 *
 * For every text symbol in @a symbols, a new BasicFunctionDebugInfo is
 * created with name, address and size taken from the symbol record. The
 * pretty name is produced via Demangler. On allocation failure, all
 * already-added entries are released and removed.
 *
 * @param symbols       Input symbol list (sorted by address by the caller).
 * @param functions     Out parameter receiving the new descriptors.
 * @param interface     Debugger interface (currently unused but kept for
 *                      symmetry with backends that need it).
 * @param imageInfo     Image identity used in error reporting.
 * @param info          The SpecificImageDebugInfo to attach descriptors
 *                      to.
 * @retval B_OK         Functions were appended to @a functions.
 * @retval B_NO_MEMORY  Allocation failed; partial work was rolled back.
 */
/*static*/ status_t
SpecificImageDebugInfo::GetFunctionsFromSymbols(
	const BObjectList<SymbolInfo, true>& symbols,
	BObjectList<FunctionDebugInfo>& functions, DebuggerInterface* interface,
	const ImageInfo& imageInfo, SpecificImageDebugInfo* info)
{
	// create the function infos
	int32 functionsAdded = 0;
	for (int32 i = 0; SymbolInfo* symbol = symbols.ItemAt(i); i++) {
		if (symbol->Type() != B_SYMBOL_TYPE_TEXT)
			continue;

		FunctionDebugInfo* function = new(std::nothrow) BasicFunctionDebugInfo(
			info, symbol->Address(), symbol->Size(), symbol->Name(),
			Demangler::Demangle(symbol->Name()));
		if (function == NULL || !functions.AddItem(function)) {
			delete function;
			int32 index = functions.CountItems() - 1;
			for (; functionsAdded >= 0; functionsAdded--, index--) {
				function = functions.RemoveItemAt(index);
				delete function;
			}
			return B_NO_MEMORY;
		}

		functionsAdded++;
	}

	return B_OK;
}
