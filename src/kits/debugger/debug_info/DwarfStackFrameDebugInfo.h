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
 * MIT License. Copyright 2009-2012, Haiku.
 * Original authors: Ingo Weinhold, Rene Gollent.
 */

/** @file DwarfStackFrameDebugInfo.h
    @brief DWARF-driven StackFrameDebugInfo that materializes parameters,
           local variables and return values for a single stack frame. */

#ifndef DWARF_STACK_FRAME_DEBUG_INFO_H
#define DWARF_STACK_FRAME_DEBUG_INFO_H


#include <image.h>
#include <String.h>

#include "StackFrameDebugInfo.h"


class CompilationUnit;
class CpuState;
class DIEFormalParameter;
class DIESubprogram;
class DIEType;
class DIEVariable;
class DwarfFile;
class DwarfTargetInterface;
class DwarfTypeContext;
class DwarfTypeFactory;
class FunctionID;
class GlobalTypeCache;
class GlobalTypeLookup;
struct LocationDescription;
class ObjectID;
class RegisterMap;
class Variable;


/** @brief StackFrameDebugInfo that uses a DwarfTypeFactory and a DWARF
           type context to construct Variables for parameters, locals, and
           return values. */
class DwarfStackFrameDebugInfo : public StackFrameDebugInfo {
public:
								DwarfStackFrameDebugInfo(
									Architecture* architecture,
									image_id imageID, DwarfFile* file,
									CompilationUnit* compilationUnit,
									DIESubprogram* subprogramEntry,
									GlobalTypeLookup* typeLookup,
									GlobalTypeCache* typeCache,
									target_addr_t instructionPointer,
									target_addr_t framePointer,
									target_addr_t relocationDelta,
									DwarfTargetInterface* targetInterface,
									RegisterMap* fromDwarfRegisterMap);
								~DwarfStackFrameDebugInfo();

			status_t			Init();

			status_t			CreateParameter(FunctionID* functionID,
									DIEFormalParameter* parameterEntry,
									Variable*& _parameter);
									// returns reference
			status_t			CreateLocalVariable(FunctionID* functionID,
									DIEVariable* variableEntry,
									Variable*& _variable);
									// returns reference
			status_t			CreateReturnValue(FunctionID* functionID,
									DIEType* returnType,
									ValueLocation* location,
									CpuState* state,
									Variable*& _variable);
									// returns reference

private:
			struct DwarfFunctionParameterID;
			struct DwarfLocalVariableID;
			struct DwarfReturnValueID;

private:
			status_t			_CreateVariable(ObjectID* id,
									const BString& name, DIEType* typeEntry,
									LocationDescription* locationDescription,
									Variable*& _variable);

	template<typename EntryType>
	static	DIEType*			_GetDIEType(EntryType* entry);

private:
			DwarfTypeContext*	fTypeContext;
			GlobalTypeLookup*	fTypeLookup;
			GlobalTypeCache*	fTypeCache;
			DwarfTypeFactory*	fTypeFactory;
};


#endif	// DWARF_STACK_FRAME_DEBUG_INFO_H
