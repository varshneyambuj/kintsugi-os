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
 * MIT License. Copyright 2009-2014, Haiku.
 * Original authors: Ingo Weinhold, Rene Gollent.
 */

/** @file DebuggerImageDebugInfo.h
    @brief Symbol-only SpecificImageDebugInfo backed by the live debugger
           interface, used as a last-resort source for function records. */

#ifndef DEBUGGER_IMAGE_DEBUG_INFO_H
#define DEBUGGER_IMAGE_DEBUG_INFO_H


#include "ImageInfo.h"
#include "SpecificImageDebugInfo.h"


class Architecture;
class DebuggerInterface;
class SymbolInfo;


/** @brief Per-image debug info backend that derives function descriptors
           solely from the symbol table fetched from the running target. */
class DebuggerImageDebugInfo : public SpecificImageDebugInfo {
public:
								DebuggerImageDebugInfo(
									const ImageInfo& imageInfo,
									DebuggerInterface* debuggerInterface,
									Architecture* architecture);
	virtual						~DebuggerImageDebugInfo();

			status_t			Init();

	virtual	status_t			GetFunctions(
									const BObjectList<SymbolInfo, true>& symbols,
									BObjectList<FunctionDebugInfo>& functions);
	virtual	status_t			GetType(GlobalTypeCache* cache,
									const BString& name,
									const TypeLookupConstraints& constraints,
									Type*& _type);
	virtual	bool				HasType(const BString& name,
									const TypeLookupConstraints& constraints)
									const;

	virtual AddressSectionType	GetAddressSectionType(target_addr_t address);
	virtual	status_t			CreateFrame(Image* image,
									FunctionInstance* functionInstance,
									CpuState* cpuState,
									bool getFullFrameInfo,
									ReturnValueInfoList* returnValueInfos,
									StackFrame*& _previousFrame,
									CpuState*& _previousCpuState);
	virtual	status_t			GetStatement(FunctionDebugInfo* function,
									target_addr_t address,
									Statement*& _statement);
	virtual	status_t			GetStatementAtSourceLocation(
									FunctionDebugInfo* function,
									const SourceLocation& sourceLocation,
									Statement*& _statement);

	virtual	status_t			GetSourceLanguage(FunctionDebugInfo* function,
									SourceLanguage*& _language);

	virtual	ssize_t				ReadCode(target_addr_t address, void* buffer,
									size_t size);

	virtual	status_t			AddSourceCodeInfo(LocatableFile* file,
									FileSourceCode* sourceCode);

private:
			ImageInfo			fImageInfo;
			DebuggerInterface*	fDebuggerInterface;
			Architecture*		fArchitecture;
};


#endif	// DEBUGGER_IMAGE_DEBUG_INFO_H
