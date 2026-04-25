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
 * MIT License. Copyright 2009-2015, Haiku.
 * Original authors: Ingo Weinhold, Rene Gollent.
 */

/** @file ArchitectureX86.h
    @brief IA-32 backend declaration for the architecture-neutral Architecture base. */

#ifndef ARCHITECTURE_X86_H
#define ARCHITECTURE_X86_H


#include <Array.h>

#include "Architecture.h"
#include "Register.h"


/** @brief Bitmask flags describing optional IA-32 CPU features. */
enum {
	X86_CPU_FEATURE_FLAG_NONE = 0,
	X86_CPU_FEATURE_FLAG_MMX = 1,
	X86_CPU_FEATURE_FLAG_SSE = 2
};


class SourceLanguage;


/** @brief IA-32 (32-bit x86) implementation of the architecture-neutral Architecture interface. */
class ArchitectureX86 : public Architecture {
public:
								ArchitectureX86(TeamMemory* teamMemory);
	virtual						~ArchitectureX86();

	virtual	status_t			Init();

	virtual int32				StackGrowthDirection() const;

	virtual	int32				CountRegisters() const;
	virtual	const Register*		Registers() const;
	virtual status_t			InitRegisterRules(CfaContext& context) const;

	virtual	status_t			GetDwarfRegisterMaps(RegisterMap** _toDwarf,
									RegisterMap** _fromDwarf) const;

	virtual	status_t			GetCpuFeatures(uint32& flags);

	virtual	status_t			CreateCpuState(CpuState*& _state);
	virtual	status_t			CreateCpuState(const void* cpuStateData,
									size_t size, CpuState*& _state);
	virtual	status_t			CreateStackFrame(Image* image,
									FunctionDebugInfo* function,
									CpuState* cpuState, bool isTopFrame,
									StackFrame*& _previousFrame,
									CpuState*& _previousCpuState);
	virtual	void				UpdateStackFrameCpuState(
									const StackFrame* frame,
									Image* previousImage,
									FunctionDebugInfo* previousFunction,
									CpuState* previousCpuState);

	virtual	status_t			ReadValueFromMemory(target_addr_t address,
									uint32 valueType, BVariant& _value) const;
	virtual	status_t			ReadValueFromMemory(target_addr_t addressSpace,
									target_addr_t address, uint32 valueType,
									BVariant& _value) const;

	virtual	status_t			DisassembleCode(FunctionDebugInfo* function,
									const void* buffer, size_t bufferSize,
									DisassembledCode*& _sourceCode);
	virtual	status_t			GetStatement(FunctionDebugInfo* function,
									target_addr_t address,
									Statement*& _statement);
	virtual	status_t			GetInstructionInfo(target_addr_t address,
									InstructionInfo& _info, CpuState* state);
	virtual	status_t			ResolvePICFunctionAddress(
									target_addr_t instructionAddress,
									CpuState* state,
									target_addr_t& _targetAddress);

	virtual	status_t			GetWatchpointDebugCapabilities(
									int32& _maxRegisterCount,
									int32& _maxBytesPerRegister,
									uint8& _watchpointCapabilityFlags);

	virtual	status_t			GetReturnAddressLocation(
									StackFrame* frame, target_size_t valueSize,
									ValueLocation*& _location);


private:
			struct ToDwarfRegisterMap;
			struct FromDwarfRegisterMap;

private:
			void				_AddRegister(int32 index, const char* name,
									uint32 bitSize, uint32 valueType,
									register_type type, bool calleePreserved);
			void				_AddIntegerRegister(int32 index,
									const char* name, uint32 valueType,
									register_type type, bool calleePreserved);
			void				_AddFPRegister(int32 index,
									const char* name);
			void				_AddSIMDRegister(int32 index,
									const char* name, uint32 byteSize);
			bool				_HasFunctionPrologue(
									FunctionDebugInfo* function) const;

private:
			uint32				fFeatureFlags;
			Array<Register>		fRegisters;
			SourceLanguage*		fAssemblyLanguage;
			ToDwarfRegisterMap*	fToDwarfRegisterMap;
			FromDwarfRegisterMap* fFromDwarfRegisterMap;
};


#endif	// ARCHITECTURE_X86_H
