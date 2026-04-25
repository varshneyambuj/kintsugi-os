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
 *   Copyright 2011-2016, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ArchitectureX86.cpp
 * @brief 32-bit x86 (IA-32) backend for the architecture-neutral Architecture base.
 *
 * Defines the IA-32 register set, DWARF register-number maps, frame
 * unwinding (handling syscall, frameless, and standard prologue cases),
 * Zydis-based disassembly, and ABI-specific return-value placement (EAX,
 * EAX:EDX, or memory).
 *
 * @see Architecture, CpuStateX86, DisassemblerX86
 */


#include "ArchitectureX86.h"

#include <new>

#include <String.h>

#include <AutoDeleter.h>

#include "CfaContext.h"
#include "CpuStateX86.h"
#include "DisassembledCode.h"
#include "FunctionDebugInfo.h"
#include "InstructionInfo.h"
#include "NoOpStackFrameDebugInfo.h"
#include "RegisterMap.h"
#include "StackFrame.h"
#include "Statement.h"
#include "TeamMemory.h"
#include "ValueLocation.h"
#include "X86AssemblyLanguage.h"

#include "disasm/DisassemblerX86.h"


/** @brief CPUID feature bit indicating MMX support. */
#define IA32_FEATURE_MMX	(1 << 23)
/** @brief CPUID feature bit indicating SSE support. */
#define IA32_FEATURE_SSE	(1 << 25)


/**
 * @brief DWARF-register-number to IA-32 register-index lookup table.
 *
 * Indexed by the DWARF register number assigned by the System V IA-32 ABI.
 * Entries with the value -1 indicate registers the debugger kit does not model.
 */
static const int32 kFromDwarfRegisters[] = {
	X86_REGISTER_EAX,
	X86_REGISTER_ECX,
	X86_REGISTER_EDX,
	X86_REGISTER_EBX,
	X86_REGISTER_ESP,
	X86_REGISTER_EBP,
	X86_REGISTER_ESI,
	X86_REGISTER_EDI,
	X86_REGISTER_EIP,
	-1,	// eflags
	-1,	// trap number
	X86_REGISTER_ST0,
	X86_REGISTER_ST1,
	X86_REGISTER_ST2,
	X86_REGISTER_ST3,
	X86_REGISTER_ST4,
	X86_REGISTER_ST5,
	X86_REGISTER_ST6,
	X86_REGISTER_ST7,
	-1,	// ?
	-1,	// ?
	X86_REGISTER_XMM0,
	X86_REGISTER_XMM1,
	X86_REGISTER_XMM2,
	X86_REGISTER_XMM3,
	X86_REGISTER_XMM4,
	X86_REGISTER_XMM5,
	X86_REGISTER_XMM6,
	X86_REGISTER_XMM7,
	X86_REGISTER_MM0,
	X86_REGISTER_MM1,
	X86_REGISTER_MM2,
	X86_REGISTER_MM3,
	X86_REGISTER_MM4,
	X86_REGISTER_MM5,
	X86_REGISTER_MM6,
	X86_REGISTER_MM7,
};

/** @brief Number of entries in @c kFromDwarfRegisters. */
static const int32 kFromDwarfRegisterCount = sizeof(kFromDwarfRegisters) / 4;


// #pragma mark - ToDwarfRegisterMap


/**
 * @brief Maps native IA-32 register indices to DWARF register numbers.
 *
 * Builds the inverse of @c kFromDwarfRegisters at construction time so
 * MapRegisterIndex() runs in O(1).
 */
struct ArchitectureX86::ToDwarfRegisterMap : RegisterMap {
	/** @brief Build the reverse-lookup index from kFromDwarfRegisters. */
	ToDwarfRegisterMap()
	{
		// init the index array from the reverse map
		memset(fIndices, -1, sizeof(fIndices));
		for (int32 i = 0; i < kFromDwarfRegisterCount; i++) {
			if (kFromDwarfRegisters[i] >= 0)
				fIndices[kFromDwarfRegisters[i]] = i;
		}
	}

	/** @brief Number of native IA-32 registers. */
	virtual int32 CountRegisters() const
	{
		return X86_REGISTER_COUNT;
	}

	/**
	 * @brief Translate a native register index to a DWARF register number.
	 * @param index  Native IA-32 register index.
	 * @return DWARF register number, or -1 when @a index is out of range.
	 */
	virtual int32 MapRegisterIndex(int32 index) const
	{
		return index >= 0 && index < X86_REGISTER_COUNT ? fIndices[index] : -1;
	}

private:
	int32	fIndices[X86_REGISTER_COUNT];
};


// #pragma mark - FromDwarfRegisterMap


/**
 * @brief Maps DWARF register numbers to native IA-32 register indices.
 */
struct ArchitectureX86::FromDwarfRegisterMap : RegisterMap {
	/** @brief Number of DWARF entries this map knows about. */
	virtual int32 CountRegisters() const
	{
		return kFromDwarfRegisterCount;
	}

	/**
	 * @brief Translate a DWARF register number to a native register index.
	 * @param index  DWARF register number.
	 * @return Native register index, or -1 when out of range or unmapped.
	 */
	virtual int32 MapRegisterIndex(int32 index) const
	{
		return index >= 0 && index < kFromDwarfRegisterCount
			? kFromDwarfRegisters[index] : -1;
	}
};


// #pragma mark - ArchitectureX86


/**
 * @brief Construct the IA-32 architecture wrapper.
 *
 * Defers register-table population until Init().
 *
 * @param teamMemory  Memory accessor used to read target memory.
 */
ArchitectureX86::ArchitectureX86(TeamMemory* teamMemory)
	:
	Architecture(teamMemory, 4, sizeof(x86_debug_cpu_state), false),
	fFeatureFlags(0),
	fAssemblyLanguage(NULL),
	fToDwarfRegisterMap(NULL),
	fFromDwarfRegisterMap(NULL)
{
}


/** @brief Release references on the register maps and assembly language helper. */
ArchitectureX86::~ArchitectureX86()
{
	if (fToDwarfRegisterMap != NULL)
		fToDwarfRegisterMap->ReleaseReference();
	if (fFromDwarfRegisterMap != NULL)
		fFromDwarfRegisterMap->ReleaseReference();
	if (fAssemblyLanguage != NULL)
		fAssemblyLanguage->ReleaseReference();
}


/**
 * @brief Build the IA-32 register table and DWARF maps.
 *
 * Detects MMX/SSE support via cpuid (only when running natively on i386),
 * then registers EIP, ESP, EBP, GP, segment, x87 ST*, and any optional
 * MMX/XMM banks the host CPU advertises.
 *
 * @retval B_OK         Architecture initialized.
 * @retval B_NO_MEMORY  Allocation failed for one of the helper objects.
 * @return Other status codes propagated from get_cpuid().
 */
status_t
ArchitectureX86::Init()
{
	fAssemblyLanguage = new(std::nothrow) X86AssemblyLanguage;
	if (fAssemblyLanguage == NULL)
		return B_NO_MEMORY;

#if defined(__i386__)
	// TODO: this needs to be determined/retrieved indirectly from the
	// target host interface, as in the remote case the CPU features may
	// differ from those of the local CPU.
	cpuid_info info;
	status_t error = get_cpuid(&info, 1, 0);
	if (error != B_OK)
		return error;

	if ((info.eax_1.features & IA32_FEATURE_MMX) != 0)
		fFeatureFlags |= X86_CPU_FEATURE_FLAG_MMX;

	if ((info.eax_1.features & IA32_FEATURE_SSE) != 0)
		fFeatureFlags |= X86_CPU_FEATURE_FLAG_SSE;

#endif

	try {
		_AddIntegerRegister(X86_REGISTER_EIP, "eip", B_UINT32_TYPE,
			REGISTER_TYPE_INSTRUCTION_POINTER, false);
		_AddIntegerRegister(X86_REGISTER_ESP, "esp", B_UINT32_TYPE,
			REGISTER_TYPE_STACK_POINTER, true);
		_AddIntegerRegister(X86_REGISTER_EBP, "ebp", B_UINT32_TYPE,
			REGISTER_TYPE_GENERAL_PURPOSE, true);

		_AddIntegerRegister(X86_REGISTER_EAX, "eax", B_UINT32_TYPE,
			REGISTER_TYPE_GENERAL_PURPOSE, false);
		_AddIntegerRegister(X86_REGISTER_EBX, "ebx", B_UINT32_TYPE,
			REGISTER_TYPE_GENERAL_PURPOSE, true);
		_AddIntegerRegister(X86_REGISTER_ECX, "ecx", B_UINT32_TYPE,
			REGISTER_TYPE_GENERAL_PURPOSE, false);
		_AddIntegerRegister(X86_REGISTER_EDX, "edx", B_UINT32_TYPE,
			REGISTER_TYPE_GENERAL_PURPOSE, false);

		_AddIntegerRegister(X86_REGISTER_ESI, "esi", B_UINT32_TYPE,
			REGISTER_TYPE_GENERAL_PURPOSE, true);
		_AddIntegerRegister(X86_REGISTER_EDI, "edi", B_UINT32_TYPE,
			REGISTER_TYPE_GENERAL_PURPOSE, true);

		_AddIntegerRegister(X86_REGISTER_CS, "cs", B_UINT16_TYPE,
			REGISTER_TYPE_SPECIAL_PURPOSE, true);
		_AddIntegerRegister(X86_REGISTER_DS, "ds", B_UINT16_TYPE,
			REGISTER_TYPE_SPECIAL_PURPOSE, true);
		_AddIntegerRegister(X86_REGISTER_ES, "es", B_UINT16_TYPE,
			REGISTER_TYPE_SPECIAL_PURPOSE, true);
		_AddIntegerRegister(X86_REGISTER_FS, "fs", B_UINT16_TYPE,
			REGISTER_TYPE_SPECIAL_PURPOSE, true);
		_AddIntegerRegister(X86_REGISTER_GS, "gs", B_UINT16_TYPE,
			REGISTER_TYPE_SPECIAL_PURPOSE, true);
		_AddIntegerRegister(X86_REGISTER_SS, "ss", B_UINT16_TYPE,
			REGISTER_TYPE_SPECIAL_PURPOSE, true);

		_AddFPRegister(X86_REGISTER_ST0, "st0");
		_AddFPRegister(X86_REGISTER_ST1, "st1");
		_AddFPRegister(X86_REGISTER_ST2, "st2");
		_AddFPRegister(X86_REGISTER_ST3, "st3");
		_AddFPRegister(X86_REGISTER_ST4, "st4");
		_AddFPRegister(X86_REGISTER_ST5, "st5");
		_AddFPRegister(X86_REGISTER_ST6, "st6");
		_AddFPRegister(X86_REGISTER_ST7, "st7");

		if ((fFeatureFlags & X86_CPU_FEATURE_FLAG_MMX) != 0) {
			_AddSIMDRegister(X86_REGISTER_MM0, "mm0", sizeof(uint64));
			_AddSIMDRegister(X86_REGISTER_MM1, "mm1", sizeof(uint64));
			_AddSIMDRegister(X86_REGISTER_MM2, "mm2", sizeof(uint64));
			_AddSIMDRegister(X86_REGISTER_MM3, "mm3", sizeof(uint64));
			_AddSIMDRegister(X86_REGISTER_MM4, "mm4", sizeof(uint64));
			_AddSIMDRegister(X86_REGISTER_MM5, "mm5", sizeof(uint64));
			_AddSIMDRegister(X86_REGISTER_MM6, "mm6", sizeof(uint64));
			_AddSIMDRegister(X86_REGISTER_MM7, "mm7", sizeof(uint64));
		}

		if ((fFeatureFlags & X86_CPU_FEATURE_FLAG_SSE) != 0) {
			_AddSIMDRegister(X86_REGISTER_XMM0, "xmm0",
				sizeof(x86_xmm_register));
			_AddSIMDRegister(X86_REGISTER_XMM1, "xmm1",
				sizeof(x86_xmm_register));
			_AddSIMDRegister(X86_REGISTER_XMM2, "xmm2",
				sizeof(x86_xmm_register));
			_AddSIMDRegister(X86_REGISTER_XMM3, "xmm3",
				sizeof(x86_xmm_register));
			_AddSIMDRegister(X86_REGISTER_XMM4, "xmm4",
				sizeof(x86_xmm_register));
			_AddSIMDRegister(X86_REGISTER_XMM5, "xmm5",
				sizeof(x86_xmm_register));
			_AddSIMDRegister(X86_REGISTER_XMM6, "xmm6",
				sizeof(x86_xmm_register));
			_AddSIMDRegister(X86_REGISTER_XMM7, "xmm7",
				sizeof(x86_xmm_register));
		}

	} catch (std::bad_alloc&) {
		return B_NO_MEMORY;
	}

	fToDwarfRegisterMap = new(std::nothrow) ToDwarfRegisterMap;
	fFromDwarfRegisterMap = new(std::nothrow) FromDwarfRegisterMap;

	if (fToDwarfRegisterMap == NULL || fFromDwarfRegisterMap == NULL)
		return B_NO_MEMORY;

	return B_OK;
}


/** @brief Return STACK_GROWTH_DIRECTION_NEGATIVE; the IA-32 stack grows downward. */
int32
ArchitectureX86::StackGrowthDirection() const
{
	return STACK_GROWTH_DIRECTION_NEGATIVE;
}


/** @brief Number of registers populated by Init(). */
int32
ArchitectureX86::CountRegisters() const
{
	return fRegisters.Count();
}


/** @brief Pointer to the contiguous register-descriptor array. */
const Register*
ArchitectureX86::Registers() const
{
	return fRegisters.Elements();
}



/**
 * @brief Install IA-32-specific DWARF register rules on top of the defaults.
 *
 * Calls the base implementation then sets EIP to a CFA-relative location
 * (offset -4), as required by the System V IA-32 unwind ABI.
 *
 * @param context  CFA context to mutate.
 * @return Status from the base call.
 */
status_t
ArchitectureX86::InitRegisterRules(CfaContext& context) const
{
	status_t error = Architecture::InitRegisterRules(context);
	if (error != B_OK)
		return error;

	// set up rule for EIP register
	context.RegisterRule(fToDwarfRegisterMap->MapRegisterIndex(
		X86_REGISTER_EIP))->SetToLocationOffset(-4);

	return B_OK;
}


/**
 * @brief Hand out additional references to the cached DWARF maps.
 *
 * Either output pointer may be NULL.
 *
 * @param _toDwarf    Output that receives a referenced ToDwarfRegisterMap.
 * @param _fromDwarf  Output that receives a referenced FromDwarfRegisterMap.
 * @retval B_OK  Always; the caller must release any returned references.
 */
status_t
ArchitectureX86::GetDwarfRegisterMaps(RegisterMap** _toDwarf,
	RegisterMap** _fromDwarf) const
{
	if (_toDwarf != NULL) {
		*_toDwarf = fToDwarfRegisterMap;
		fToDwarfRegisterMap->AcquireReference();
	}

	if (_fromDwarf != NULL) {
		*_fromDwarf = fFromDwarfRegisterMap;
		fFromDwarfRegisterMap->AcquireReference();
	}

	return B_OK;
}


/**
 * @brief Report the CPU feature bitset detected at Init() time.
 *
 * @param flags  Output that receives the feature flags.
 * @retval B_OK  Always.
 */
status_t
ArchitectureX86::GetCpuFeatures(uint32& flags)
{
	flags = fFeatureFlags;

	return B_OK;
}


/**
 * @brief Allocate a default-constructed CpuStateX86.
 *
 * @param _state  Output that receives the new state.
 * @retval B_OK         Allocation succeeded.
 * @retval B_NO_MEMORY  Out of memory.
 */
status_t
ArchitectureX86::CreateCpuState(CpuState*& _state)
{
	CpuStateX86* state = new(std::nothrow) CpuStateX86;
	if (state == NULL)
		return B_NO_MEMORY;

	_state = state;
	return B_OK;
}


/**
 * @brief Construct a CpuStateX86 from an x86_debug_cpu_state blob.
 *
 * @param cpuStateData  Pointer to a x86_debug_cpu_state value.
 * @param size          Size in bytes; must equal sizeof(x86_debug_cpu_state).
 * @param _state        Output that receives the new state.
 * @retval B_OK         State decoded successfully.
 * @retval B_BAD_VALUE  @a size does not match.
 * @retval B_NO_MEMORY  Allocation failed.
 */
status_t
ArchitectureX86::CreateCpuState(const void* cpuStateData, size_t size,
	CpuState*& _state)
{
	if (size != sizeof(x86_debug_cpu_state))
		return B_BAD_VALUE;

	CpuStateX86* state = new(std::nothrow) CpuStateX86(
		*(const x86_debug_cpu_state*)cpuStateData);
	if (state == NULL)
		return B_NO_MEMORY;

	_state = state;
	return B_OK;
}


/**
 * @brief Build a single IA-32 stack frame from a CPU state.
 *
 * Detects syscall frames (interrupt vector 99), recognizes the standard
 * "push %ebp; mov %esp, %ebp" prologue, and falls back to reading the
 * return address from ESP for frameless functions or instructions that
 * lie before the prologue.
 *
 * @param image              Image hosting @a function.
 * @param function           Function metadata used for the prologue check.
 * @param _cpuState          CPU state at the entry point of this frame.
 * @param isTopFrame         true when this is the most recent frame.
 * @param _frame             Output that receives the new StackFrame.
 * @param _previousCpuState  Output that receives the inferred caller's CpuState
 *                           (may be NULL).
 * @retval B_OK         Frame created.
 * @retval B_NO_MEMORY  Allocation failed.
 */
status_t
ArchitectureX86::CreateStackFrame(Image* image, FunctionDebugInfo* function,
	CpuState* _cpuState, bool isTopFrame, StackFrame*& _frame,
	CpuState*& _previousCpuState)
{
	CpuStateX86* cpuState = dynamic_cast<CpuStateX86*>(_cpuState);

	uint32 framePointer = cpuState->IntRegisterValue(X86_REGISTER_EBP);
	uint32 eip = cpuState->IntRegisterValue(X86_REGISTER_EIP);

	bool readStandardFrame = true;
	uint32 previousFramePointer = 0;
	uint32 returnAddress = 0;

	// check for syscall frames
	stack_frame_type frameType;
	bool hasPrologue = false;
	if (isTopFrame && cpuState->InterruptVector() == 99) {
		// The thread is performing a syscall. So this frame is not really the
		// top-most frame and we need to adjust the eip.
		frameType = STACK_FRAME_TYPE_SYSCALL;
		eip -= 2;
			// int 99, sysenter, and syscall all are 2 byte instructions

		// The syscall stubs are frameless, the return address is on top of the
		// stack.
		uint32 esp = cpuState->IntRegisterValue(X86_REGISTER_ESP);
		uint32 address;
		if (fTeamMemory->ReadMemory(esp, &address, 4) == 4) {
			returnAddress = address;
			previousFramePointer = framePointer;
			framePointer = 0;
			readStandardFrame = false;
		}
	} else {
		hasPrologue = _HasFunctionPrologue(function);
		if (hasPrologue)
			frameType = STACK_FRAME_TYPE_STANDARD;
		else
			frameType = STACK_FRAME_TYPE_FRAMELESS;
		// TODO: Handling for frameless functions. It's not trivial to find the
		// return address on the stack, though.

		// If the function is not frameless and we're at the top frame we need
		// to check whether the prologue has not been executed (completely) or
		// we're already after the epilogue.
		if (isTopFrame) {
			uint32 stack = 0;
			if (hasPrologue) {
				if (eip < function->Address() + 3) {
					// The prologue has not been executed yet, i.e. there's no
					// stack frame yet. Get the return address from the stack.
					stack = cpuState->IntRegisterValue(X86_REGISTER_ESP);
					if (eip > function->Address()) {
						// The "push %ebp" has already been executed.
						stack += 4;
					}
				} else {
					// Not in the function prologue, but maybe after the
					// epilogue. The epilogue is a single "pop %ebp", so we
					// check whether the current instruction is already a
					// "ret".
					uint8 code[1];
					if (fTeamMemory->ReadMemory(eip, &code, 1) == 1
						&& code[0] == 0xc3) {
						stack = cpuState->IntRegisterValue(X86_REGISTER_ESP);
					}
				}
			} else {
				// Check if the instruction pointer is at a readable location.
				// If it isn't, then chances are we got here via a bogus
				// function pointer, and the prologue hasn't actually been
				// executed. In such a case, what we need is right at the top
				// of the stack.
				uint8 data[1];
				if (fTeamMemory->ReadMemory(eip, &data, 1) != 1)
					stack = cpuState->IntRegisterValue(X86_REGISTER_ESP);
			}

			if (stack != 0) {
				uint32 address;
				if (fTeamMemory->ReadMemory(stack, &address, 4) == 4) {
					returnAddress = address;
					previousFramePointer = framePointer;
					framePointer = 0;
					readStandardFrame = false;
					frameType = STACK_FRAME_TYPE_FRAMELESS;
				}
			}
		}
	}

	// create the stack frame
	StackFrameDebugInfo* stackFrameDebugInfo
		= new(std::nothrow) NoOpStackFrameDebugInfo;
	if (stackFrameDebugInfo == NULL)
		return B_NO_MEMORY;
	BReference<StackFrameDebugInfo> stackFrameDebugInfoReference(
		stackFrameDebugInfo, true);

	StackFrame* frame = new(std::nothrow) StackFrame(frameType, cpuState,
		framePointer, eip, stackFrameDebugInfo);
	if (frame == NULL)
		return B_NO_MEMORY;
	BReference<StackFrame> frameReference(frame, true);

	status_t error = frame->Init();
	if (error != B_OK)
		return error;

	// read the previous frame and return address, if this is a standard frame
	if (readStandardFrame) {
		uint32 frameData[2];
		if (framePointer != 0
			&& fTeamMemory->ReadMemory(framePointer, frameData, 8) == 8) {
			previousFramePointer = frameData[0];
			returnAddress = frameData[1];
		}
	}

	// create the CPU state, if we have any info
	CpuStateX86* previousCpuState = NULL;
	if (returnAddress != 0) {
		// prepare the previous CPU state
		previousCpuState = new(std::nothrow) CpuStateX86;
		if (previousCpuState == NULL)
			return B_NO_MEMORY;

		previousCpuState->SetIntRegister(X86_REGISTER_EBP,
			previousFramePointer);
		previousCpuState->SetIntRegister(X86_REGISTER_EIP, returnAddress);
		frame->SetPreviousCpuState(previousCpuState);
	}

	frame->SetReturnAddress(returnAddress);

	_frame = frameReference.Detach();
	_previousCpuState = previousCpuState;
	return B_OK;
}


/**
 * @brief Adjust the inherited CPU state so EIP points at the call site, not after it.
 *
 * For non-top frames the saved instruction pointer points just past the
 * call instruction. To map back to the calling instruction we disassemble
 * the function's code and subtract the size of the previous instruction.
 *
 * @param frame             Frame whose previous CPU state is being adjusted.
 * @param previousImage     Image hosting @a previousFunction (unused).
 * @param previousFunction  Function the previous frame is executing.
 * @param previousCpuState  Inherited CPU state to adjust in place.
 */
void
ArchitectureX86::UpdateStackFrameCpuState(const StackFrame* frame,
	Image* previousImage, FunctionDebugInfo* previousFunction,
	CpuState* previousCpuState)
{
	// This is not a top frame, so we want to offset eip to the previous
	// (calling) instruction.
	CpuStateX86* cpuState = dynamic_cast<CpuStateX86*>(previousCpuState);

	// get eip
	uint32 eip = cpuState->IntRegisterValue(X86_REGISTER_EIP);
	if (previousFunction == NULL || eip <= previousFunction->Address())
		return;
	target_addr_t functionAddress = previousFunction->Address();

	// allocate a buffer for the function code to disassemble
	size_t bufferSize = eip - functionAddress;
	void* buffer = malloc(bufferSize);
	if (buffer == NULL)
		return;
	MemoryDeleter bufferDeleter(buffer);

	// read the code
	ssize_t bytesRead = fTeamMemory->ReadMemory(functionAddress, buffer,
		bufferSize);
	if (bytesRead != (ssize_t)bufferSize)
		return;

	// disassemble to get the previous instruction
	DisassemblerX86 disassembler;
	target_addr_t instructionAddress;
	target_size_t instructionSize;
	if (disassembler.Init(functionAddress, buffer, bufferSize) == B_OK
		&& disassembler.GetPreviousInstruction(eip, instructionAddress,
			instructionSize) == B_OK) {
		eip -= instructionSize;
		cpuState->SetIntRegister(X86_REGISTER_EIP, eip);
	}
}


/**
 * @brief Read a primitive value from target memory.
 *
 * @param address    Address to read from.
 * @param valueType  BVariant type code identifying the primitive.
 * @param _value     Output that receives the decoded value.
 * @retval B_OK         Read succeeded and the value was decoded.
 * @retval B_BAD_VALUE  Unsupported @a valueType or zero/over-large size.
 * @retval B_ERROR      Short read.
 * @return Other status codes propagated from TeamMemory::ReadMemory().
 */
status_t
ArchitectureX86::ReadValueFromMemory(target_addr_t address, uint32 valueType,
	BVariant& _value) const
{
	uint8 buffer[64];
	size_t size = BVariant::SizeOfType(valueType);
	if (size == 0 || size > sizeof(buffer))
		return B_BAD_VALUE;

	ssize_t bytesRead = fTeamMemory->ReadMemory(address, buffer, size);
	if (bytesRead < 0)
		return bytesRead;
	if ((size_t)bytesRead != size)
		return B_ERROR;

	// TODO: We need to swap endianess, if the host is big endian!

	switch (valueType) {
		case B_INT8_TYPE:
			_value.SetTo(*(int8*)buffer);
			return B_OK;
		case B_UINT8_TYPE:
			_value.SetTo(*(uint8*)buffer);
			return B_OK;
		case B_INT16_TYPE:
			_value.SetTo(*(int16*)buffer);
			return B_OK;
		case B_UINT16_TYPE:
			_value.SetTo(*(uint16*)buffer);
			return B_OK;
		case B_INT32_TYPE:
			_value.SetTo(*(int32*)buffer);
			return B_OK;
		case B_UINT32_TYPE:
			_value.SetTo(*(uint32*)buffer);
			return B_OK;
		case B_INT64_TYPE:
			_value.SetTo(*(int64*)buffer);
			return B_OK;
		case B_UINT64_TYPE:
			_value.SetTo(*(uint64*)buffer);
			return B_OK;
		case B_FLOAT_TYPE:
			_value.SetTo(*(float*)buffer);
				// TODO: float on the host might work differently!
			return B_OK;
		case B_DOUBLE_TYPE:
			_value.SetTo(*(double*)buffer);
				// TODO: double on the host might work differently!
			return B_OK;
		default:
			return B_BAD_VALUE;
	}
}


/**
 * @brief Address-space aware overload; not applicable on IA-32.
 *
 * @param addressSpace  Ignored.
 * @param address       Ignored.
 * @param valueType     Ignored.
 * @param _value        Ignored.
 * @retval B_BAD_VALUE  Always.
 */
status_t
ArchitectureX86::ReadValueFromMemory(target_addr_t addressSpace,
	target_addr_t address, uint32 valueType, BVariant& _value) const
{
	// n/a on this architecture
	return B_BAD_VALUE;
}


/**
 * @brief Disassemble a contiguous code buffer into a DisassembledCode listing.
 *
 * @param function     Function whose code is being disassembled.
 * @param buffer       Pointer to the code bytes (already read from the target).
 * @param bufferSize   Number of bytes in @a buffer.
 * @param _sourceCode  Output that receives the assembled DisassembledCode.
 * @retval B_OK         Listing produced.
 * @retval B_NO_MEMORY  Allocation failed.
 * @return Other status codes propagated from DisassemblerX86::Init().
 */
status_t
ArchitectureX86::DisassembleCode(FunctionDebugInfo* function,
	const void* buffer, size_t bufferSize, DisassembledCode*& _sourceCode)
{
	DisassembledCode* source = new(std::nothrow) DisassembledCode(
		fAssemblyLanguage);
	if (source == NULL)
		return B_NO_MEMORY;
	BReference<DisassembledCode> sourceReference(source, true);

	// init disassembler
	DisassemblerX86 disassembler;
	status_t error = disassembler.Init(function->Address(), buffer, bufferSize);
	if (error != B_OK)
		return error;

	// add a function name line
	BString functionName(function->PrettyName());
	if (!source->AddCommentLine((functionName << ':').String()))
		return B_NO_MEMORY;

	// disassemble the instructions
	BString line;
	target_addr_t instructionAddress;
	target_size_t instructionSize;
	bool breakpointAllowed;
	while (disassembler.GetNextInstruction(line, instructionAddress,
				instructionSize, breakpointAllowed) == B_OK) {
// TODO: Respect breakpointAllowed!
		if (!source->AddInstructionLine(line, instructionAddress,
				instructionSize)) {
			return B_NO_MEMORY;
		}
	}

	_sourceCode = sourceReference.Detach();
	return B_OK;
}


/**
 * @brief Synthesize a single-instruction Statement for an address.
 *
 * Used as a fallback when no source-level statement information is available.
 *
 * @param function    Function the statement belongs to (unused).
 * @param address     Address that should be wrapped in a statement.
 * @param _statement  Output that receives the new ContiguousStatement.
 * @retval B_OK         Statement created.
 * @retval B_NO_MEMORY  Allocation failed.
 * @return Other status codes propagated from GetInstructionInfo().
 */
status_t
ArchitectureX86::GetStatement(FunctionDebugInfo* function,
	target_addr_t address, Statement*& _statement)
{
// TODO: This is not architecture dependent anymore!
	// get the instruction info
	InstructionInfo info;
	status_t error = GetInstructionInfo(address, info, NULL);
	if (error != B_OK)
		return error;

	// create a statement
	ContiguousStatement* statement = new(std::nothrow) ContiguousStatement(
		SourceLocation(-1), TargetAddressRange(info.Address(), info.Size()));
	if (statement == NULL)
		return B_NO_MEMORY;

	_statement = statement;
	return B_OK;
}


/**
 * @brief Decode the instruction at @a address into an InstructionInfo record.
 *
 * Reads up to 16 bytes (the maximum x86 instruction length) from the target
 * and dispatches to DisassemblerX86.
 *
 * @param address  Address of the instruction.
 * @param _info    Output that receives the decoded info.
 * @param state    Optional CPU state used for absolute target resolution.
 * @return Status from TeamMemory::ReadMemory(), DisassemblerX86::Init(),
 *         or DisassemblerX86::GetNextInstructionInfo().
 */
status_t
ArchitectureX86::GetInstructionInfo(target_addr_t address,
	InstructionInfo& _info, CpuState* state)
{
	// read the code - maximum x86{-64} instruction size = 15 bytes
	uint8 buffer[16];
	ssize_t bytesRead = fTeamMemory->ReadMemory(address, buffer,
		sizeof(buffer));
	if (bytesRead < 0)
		return bytesRead;

	// init disassembler
	DisassemblerX86 disassembler;
	status_t error = disassembler.Init(address, buffer, bytesRead);
	if (error != B_OK)
		return error;

	return disassembler.GetNextInstructionInfo(_info, state);
}


/**
 * @brief Resolve the actual target of a call routed through a PLT slot.
 *
 * Uses the disassembled jump target as a memory pointer and dereferences it.
 *
 * @param instructionAddress  Address of the instruction that produced the
 *                            apparent call into the PLT.
 * @param state               CPU state (unused on IA-32 but part of the signature).
 * @param _targetAddress      Output that receives the resolved address.
 * @retval B_OK         Resolution succeeded.
 * @retval B_BAD_VALUE  Disassembly or memory read failed.
 */
status_t
ArchitectureX86::ResolvePICFunctionAddress(target_addr_t instructionAddress,
	CpuState* state, target_addr_t& _targetAddress)
{
	// if the function in question is position-independent, the call
	// will actually have taken us to its corresponding PLT slot.
	// in such a case, look at the disassembled jump to determine
	// where to find the actual function address.
	InstructionInfo info;
	if (GetInstructionInfo(instructionAddress, info, state) != B_OK) {
		return B_BAD_VALUE;
	}
	target_addr_t subroutineAddress = info.TargetAddress();

	ssize_t bytesRead = fTeamMemory->ReadMemory(info.TargetAddress(),
		&subroutineAddress, fAddressSize);

	if (bytesRead != fAddressSize)
		return B_BAD_VALUE;

	_targetAddress = subroutineAddress;
	return B_OK;
}


/**
 * @brief Report the watchpoint capabilities of the IA-32 debug registers.
 *
 * Two of the four hardware debug registers are available for watchpoints
 * (one is reserved by the kernel and one is needed for breakpoints).
 *
 * @param _maxRegisterCount         Output: number of available registers (2).
 * @param _maxBytesPerRegister      Output: maximum span per register (4).
 * @param _watchpointCapabilityFlags Output: supported access modes.
 * @retval B_OK  Always.
 */
status_t
ArchitectureX86::GetWatchpointDebugCapabilities(int32& _maxRegisterCount,
	int32& _maxBytesPerRegister, uint8& _watchpointCapabilityFlags)
{
	// while x86 technically has 4 hardware debug registers, one is reserved by
	// the kernel, and one is required for breakpoint support, which leaves
	// two available for watchpoints.
	_maxRegisterCount = 2;
	_maxBytesPerRegister = 4;

	// x86 only supports write and read/write watchpoints.
	_watchpointCapabilityFlags = WATCHPOINT_CAPABILITY_FLAG_WRITE
		| WATCHPOINT_CAPABILITY_FLAG_READ_WRITE;

	return B_OK;
}


/**
 * @brief Describe where a return value of size @a valueSize is placed by the SysV ABI.
 *
 * Values up to 4 bytes are returned in EAX; up to 8 bytes are returned in
 * the EAX:EDX pair; larger values are returned via an implicit memory
 * pointer in EAX.
 *
 * @param frame      Frame whose return value is being requested.
 * @param valueSize  Size of the return value in bytes.
 * @param _location  Output that receives the new ValueLocation.
 * @retval B_OK         Location described.
 * @retval B_NO_MEMORY  Allocation failed.
 */
status_t
ArchitectureX86::GetReturnAddressLocation(StackFrame* frame,
	target_size_t valueSize, ValueLocation*& _location)
{
	// for the calling conventions currently in use on Haiku,
	// the x86 rules for how values are returned are as follows:
	//
	// - 32 bits or smaller values are returned directly in EAX.
	// - 32-64 bit values are returned across EAX:EDX.
	// - > 64 bit values are returned on the stack.
	ValueLocation* location = new(std::nothrow) ValueLocation(
		IsBigEndian());
	if (location == NULL)
		return B_NO_MEMORY;
	BReference<ValueLocation> locationReference(location,
		true);

	if (valueSize <= 4) {
		ValuePieceLocation piece;
		piece.SetSize(valueSize);
		piece.SetToRegister(X86_REGISTER_EAX);
		if (!location->AddPiece(piece))
			return B_NO_MEMORY;
	} else if (valueSize <= 8) {
		ValuePieceLocation piece;
		piece.SetSize(4);
		piece.SetToRegister(X86_REGISTER_EAX);
		if (!location->AddPiece(piece))
			return B_NO_MEMORY;
		piece.SetToRegister(X86_REGISTER_EDX);
		piece.SetSize(valueSize - 4);
		if (!location->AddPiece(piece))
			return B_NO_MEMORY;
	} else {
		ValuePieceLocation piece;
		CpuStateX86* state = dynamic_cast<CpuStateX86*>(frame->GetCpuState());
		piece.SetToMemory(state->IntRegisterValue(X86_REGISTER_EAX));
		piece.SetSize(valueSize);
		if (!location->AddPiece(piece))
			return B_NO_MEMORY;
	}

	_location = locationReference.Detach();
	return B_OK;
}


/**
 * @brief Append a register descriptor to fRegisters, throwing on allocation failure.
 *
 * @param index            Native register index.
 * @param name             Display name.
 * @param bitSize          Width in bits.
 * @param valueType        BVariant type code.
 * @param type             Semantic role.
 * @param calleePreserved  ABI calling-convention flag.
 * @throws std::bad_alloc on allocation failure.
 */
void
ArchitectureX86::_AddRegister(int32 index, const char* name,
	uint32 bitSize, uint32 valueType, register_type type, bool calleePreserved)
{
	if (!fRegisters.Add(Register(index, name, bitSize, valueType, type,
			calleePreserved))) {
		throw std::bad_alloc();
	}
}


/**
 * @brief Add an integer register, deriving the bit size from @a valueType.
 *
 * @param index            Native register index.
 * @param name             Display name.
 * @param valueType        Integer BVariant type code.
 * @param type             Semantic role.
 * @param calleePreserved  Calling-convention flag.
 */
void
ArchitectureX86::_AddIntegerRegister(int32 index, const char* name,
	uint32 valueType, register_type type, bool calleePreserved)
{
	_AddRegister(index, name, 8 * BVariant::SizeOfType(valueType), valueType,
		type, calleePreserved);
}


/**
 * @brief Add an x87 floating-point register modeled as B_DOUBLE_TYPE.
 *
 * @param index  Native register index.
 * @param name   Display name (e.g. "st0").
 */
void
ArchitectureX86::_AddFPRegister(int32 index, const char* name)
{
	_AddRegister(index, name, 8 * BVariant::SizeOfType(B_DOUBLE_TYPE),
		B_DOUBLE_TYPE, REGISTER_TYPE_GENERAL_PURPOSE, true);
}


/**
 * @brief Add a SIMD (MMX/XMM) register modeled as B_RAW_TYPE bytes.
 *
 * @param index     Native register index.
 * @param name      Display name (e.g. "xmm0").
 * @param byteSize  Width in bytes.
 */
void
ArchitectureX86::_AddSIMDRegister(int32 index, const char* name,
	uint32 byteSize)
{
	_AddRegister(index, name, byteSize * 8, B_RAW_TYPE,
		REGISTER_TYPE_GENERAL_PURPOSE, true);
}


/**
 * @brief Test whether @a function begins with the canonical IA-32 prologue.
 *
 * Looks for the bytes "push %ebp; mov %esp, %ebp" (55 89 e5).
 *
 * @param function  Function metadata. May be NULL.
 * @return true if the prologue was found, false otherwise.
 */
bool
ArchitectureX86::_HasFunctionPrologue(FunctionDebugInfo* function) const
{
	if (function == NULL)
		return false;

	// check whether the function has the typical prologue
	if (function->Size() < 3)
		return false;

	uint8 buffer[3];
	if (fTeamMemory->ReadMemory(function->Address(), buffer, 3) != 3)
		return false;

	return buffer[0] == 0x55 && buffer[1] == 0x89 && buffer[2] == 0xe5;
}
