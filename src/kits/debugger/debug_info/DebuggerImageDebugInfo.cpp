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
 *   Copyright 2013-2014, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DebuggerImageDebugInfo.cpp
 * @brief Implementation of the symbol-only image debug info backend.
 *
 * DebuggerImageDebugInfo provides the bare minimum SpecificImageDebugInfo
 * implementation: it enumerates functions from the image's symbol table and
 * delegates statement decoding to the architecture. It deliberately reports
 * @c B_UNSUPPORTED for any operation that requires source-level information
 * (types, source code, frame unwinding).
 *
 * @see SpecificImageDebugInfo, BasicFunctionDebugInfo
 */


#include "DebuggerImageDebugInfo.h"

#include <algorithm>
#include <new>

#include <AutoDeleter.h>

#include "Architecture.h"
#include "BasicFunctionDebugInfo.h"
#include "DebuggerInterface.h"
#include "Demangler.h"
#include "SymbolInfo.h"


/**
 * @brief Constructs a symbol-only image debug info object.
 *
 * @param imageInfo            Description of the loaded image.
 * @param debuggerInterface    Interface used to access the live target;
 *                             reference acquired.
 * @param architecture         Target architecture, used for instruction
 *                             decoding.
 */
DebuggerImageDebugInfo::DebuggerImageDebugInfo(const ImageInfo& imageInfo,
	DebuggerInterface* debuggerInterface, Architecture* architecture)
	:
	fImageInfo(imageInfo),
	fDebuggerInterface(debuggerInterface),
	fArchitecture(architecture)
{
	fDebuggerInterface->AcquireReference();
}


/**
 * @brief Destroys the object and releases the debugger interface reference.
 */
DebuggerImageDebugInfo::~DebuggerImageDebugInfo()
{
	fDebuggerInterface->ReleaseReference();
}


/**
 * @brief Performs deferred initialization.
 *
 * @retval B_OK Always; this backend has no resources to bring up.
 */
status_t
DebuggerImageDebugInfo::Init()
{
	return B_OK;
}


/**
 * @brief Builds FunctionDebugInfo descriptors for every text symbol.
 *
 * Delegates to SpecificImageDebugInfo::GetFunctionsFromSymbols(), which
 * creates a BasicFunctionDebugInfo per @c B_SYMBOL_TYPE_TEXT entry.
 *
 * @param symbols     Image symbol list previously fetched from the target.
 * @param functions   Out parameter receiving the new descriptors.
 * @return Status code from GetFunctionsFromSymbols().
 */
status_t
DebuggerImageDebugInfo::GetFunctions(const BObjectList<SymbolInfo, true>& symbols,
	BObjectList<FunctionDebugInfo>& functions)
{
	return SpecificImageDebugInfo::GetFunctionsFromSymbols(symbols, functions,
		fDebuggerInterface, fImageInfo, this);
}


/**
 * @brief Type lookup is not supported by the symbol-only backend.
 *
 * @retval B_UNSUPPORTED Always.
 */
status_t
DebuggerImageDebugInfo::GetType(GlobalTypeCache* cache,
	const BString& name, const TypeLookupConstraints& constraints,
	Type*& _type)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Reports whether a type is known to this backend.
 *
 * @return Always false; symbol info has no type catalog.
 */
bool
DebuggerImageDebugInfo::HasType(const BString& name,
	const TypeLookupConstraints& constraints) const
{
	return false;
}


/**
 * @brief Classifies which image section an address belongs to.
 *
 * @param address  Image-relative address to classify.
 * @return Always @c ADDRESS_SECTION_TYPE_UNKNOWN; section data is not
 *         available without ELF parsing.
 */
AddressSectionType
DebuggerImageDebugInfo::GetAddressSectionType(target_addr_t address)
{
	return ADDRESS_SECTION_TYPE_UNKNOWN;
}


/**
 * @brief Frame creation is not supported in the symbol-only backend.
 *
 * @retval B_UNSUPPORTED Always; unwinding requires CFI or a richer source.
 */
status_t
DebuggerImageDebugInfo::CreateFrame(Image* image,
	FunctionInstance* functionInstance, CpuState* cpuState,
	bool getFullFrameInfo, ReturnValueInfoList* returnValueInfos,
	StackFrame*& _previousFrame, CpuState*& _previousCpuState)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Disassembles the instruction at @a address into a Statement.
 *
 * Defers fully to the architecture's GetStatement() implementation.
 *
 * @param function   Function in which the address resides.
 * @param address    Image-relative instruction address.
 * @param _statement Out parameter receiving the disassembled statement.
 * @return Status code propagated from Architecture::GetStatement().
 */
status_t
DebuggerImageDebugInfo::GetStatement(FunctionDebugInfo* function,
	target_addr_t address, Statement*& _statement)
{
	return fArchitecture->GetStatement(function, address, _statement);
}


/**
 * @brief Source-location lookup is not supported here.
 *
 * @retval B_ENTRY_NOT_FOUND Always; no source mapping is available.
 */
status_t
DebuggerImageDebugInfo::GetStatementAtSourceLocation(
	FunctionDebugInfo* function, const SourceLocation& sourceLocation,
	Statement*& _statement)
{
	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Source-language identification is not supported.
 *
 * @retval B_UNSUPPORTED Always.
 */
status_t
DebuggerImageDebugInfo::GetSourceLanguage(FunctionDebugInfo* function,
	SourceLanguage*& _language)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Reads raw instruction bytes from the live target.
 *
 * @param address  Image-relative address to read from.
 * @param buffer   Destination buffer.
 * @param size     Number of bytes requested.
 * @return Number of bytes read, or a negative error code.
 */
ssize_t
DebuggerImageDebugInfo::ReadCode(target_addr_t address, void* buffer,
	size_t size)
{
	return fDebuggerInterface->ReadMemory(address, buffer, size);
}


/**
 * @brief Source code annotation is not supported.
 *
 * @retval B_UNSUPPORTED Always.
 */
status_t
DebuggerImageDebugInfo::AddSourceCodeInfo(LocatableFile* file,
	FileSourceCode* sourceCode)
{
	return B_UNSUPPORTED;
}
