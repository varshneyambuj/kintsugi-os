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
 *   Copyright 2012, Alex Smith, alex@alex-smith.me.uk.
 *   Copyright 2009-2012, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2011-2013, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file CpuStateX8664.cpp
 * @brief x86_64 implementation of the CpuState interface.
 *
 * Stores the integer GP registers, segment registers, x87 ST*, MMX MM*,
 * and XMM/YMM SIMD registers. Provides bidirectional conversion between
 * the kernel debugger's @c x86_64_debug_cpu_state blob and the kit's
 * accessor interface.
 */

#include "CpuStateX8664.h"

#include <new>

#include <string.h>

#include "Register.h"


/** @brief Construct an empty state with all register-set bits cleared. */
CpuStateX8664::CpuStateX8664()
	:
	fSetRegisters()
{
}


/**
 * @brief Decode an x86_64_debug_cpu_state blob into the per-register fields.
 *
 * Copies all integer/segment/x87/MMX registers verbatim. The 256-bit YMM
 * register value is split between fp_ymm[i] (upper half) and
 * fp_fxsave.xmm[i] (lower half) in the source blob and is rejoined here.
 *
 * @param state  Kernel debugger CPU-state blob.
 */
CpuStateX8664::CpuStateX8664(const x86_64_debug_cpu_state& state)
	:
	fSetRegisters(),
	fInterruptVector(0)
{
	SetIntRegister(X86_64_REGISTER_RIP, state.rip);
	SetIntRegister(X86_64_REGISTER_RSP, state.rsp);
	SetIntRegister(X86_64_REGISTER_RBP, state.rbp);
	SetIntRegister(X86_64_REGISTER_RAX, state.rax);
	SetIntRegister(X86_64_REGISTER_RBX, state.rbx);
	SetIntRegister(X86_64_REGISTER_RCX, state.rcx);
	SetIntRegister(X86_64_REGISTER_RDX, state.rdx);
	SetIntRegister(X86_64_REGISTER_RSI, state.rsi);
	SetIntRegister(X86_64_REGISTER_RDI, state.rdi);
	SetIntRegister(X86_64_REGISTER_R8, state.r8);
	SetIntRegister(X86_64_REGISTER_R9, state.r9);
	SetIntRegister(X86_64_REGISTER_R10, state.r10);
	SetIntRegister(X86_64_REGISTER_R11, state.r11);
	SetIntRegister(X86_64_REGISTER_R12, state.r12);
	SetIntRegister(X86_64_REGISTER_R13, state.r13);
	SetIntRegister(X86_64_REGISTER_R14, state.r14);
	SetIntRegister(X86_64_REGISTER_R15, state.r15);
	SetIntRegister(X86_64_REGISTER_CS, state.cs);
	SetIntRegister(X86_64_REGISTER_DS, state.ds);
	SetIntRegister(X86_64_REGISTER_ES, state.es);
	SetIntRegister(X86_64_REGISTER_FS, state.fs);
	SetIntRegister(X86_64_REGISTER_GS, state.gs);
	SetIntRegister(X86_64_REGISTER_SS, state.ss);

	const struct savefpu& extended = state.extended_registers;

	SetFloatRegister(X86_64_REGISTER_ST0,
		(double)(*(long double*)(extended.fp_fxsave.fp[0].value)));
	SetFloatRegister(X86_64_REGISTER_ST1,
		(double)(*(long double*)(extended.fp_fxsave.fp[1].value)));
	SetFloatRegister(X86_64_REGISTER_ST2,
		(double)(*(long double*)(extended.fp_fxsave.fp[2].value)));
	SetFloatRegister(X86_64_REGISTER_ST3,
		(double)(*(long double*)(extended.fp_fxsave.fp[3].value)));
	SetFloatRegister(X86_64_REGISTER_ST4,
		(double)(*(long double*)(extended.fp_fxsave.fp[4].value)));
	SetFloatRegister(X86_64_REGISTER_ST5,
		(double)(*(long double*)(extended.fp_fxsave.fp[5].value)));
	SetFloatRegister(X86_64_REGISTER_ST6,
		(double)(*(long double*)(extended.fp_fxsave.fp[6].value)));
	SetFloatRegister(X86_64_REGISTER_ST7,
		(double)(*(long double*)(extended.fp_fxsave.fp[7].value)));

	SetMMXRegister(X86_64_REGISTER_MM0, extended.fp_fxsave.mmx[0].value);
	SetMMXRegister(X86_64_REGISTER_MM1, extended.fp_fxsave.mmx[1].value);
	SetMMXRegister(X86_64_REGISTER_MM2, extended.fp_fxsave.mmx[2].value);
	SetMMXRegister(X86_64_REGISTER_MM3, extended.fp_fxsave.mmx[3].value);
	SetMMXRegister(X86_64_REGISTER_MM4, extended.fp_fxsave.mmx[4].value);
	SetMMXRegister(X86_64_REGISTER_MM5, extended.fp_fxsave.mmx[5].value);
	SetMMXRegister(X86_64_REGISTER_MM6, extended.fp_fxsave.mmx[6].value);
	SetMMXRegister(X86_64_REGISTER_MM7, extended.fp_fxsave.mmx[7].value);

	// The YMM register value is split in two halves in the saved CPU context,
	// so we have to reassemble it here.
	// TODO check extended.xstate_hdr to see if the YMM values are present at
	// all.
	SetXMMRegister(X86_64_REGISTER_XMM0, extended.fp_ymm[0].value,
		extended.fp_fxsave.xmm[0].value);
	SetXMMRegister(X86_64_REGISTER_XMM1, extended.fp_ymm[1].value,
		extended.fp_fxsave.xmm[1].value);
	SetXMMRegister(X86_64_REGISTER_XMM2, extended.fp_ymm[2].value,
		extended.fp_fxsave.xmm[2].value);
	SetXMMRegister(X86_64_REGISTER_XMM3, extended.fp_ymm[3].value,
		extended.fp_fxsave.xmm[3].value);
	SetXMMRegister(X86_64_REGISTER_XMM4, extended.fp_ymm[4].value,
		extended.fp_fxsave.xmm[4].value);
	SetXMMRegister(X86_64_REGISTER_XMM5, extended.fp_ymm[5].value,
		extended.fp_fxsave.xmm[5].value);
	SetXMMRegister(X86_64_REGISTER_XMM6, extended.fp_ymm[6].value,
		extended.fp_fxsave.xmm[6].value);
	SetXMMRegister(X86_64_REGISTER_XMM7, extended.fp_ymm[7].value,
		extended.fp_fxsave.xmm[7].value);
	SetXMMRegister(X86_64_REGISTER_XMM8, extended.fp_ymm[8].value,
		extended.fp_fxsave.xmm[8].value);
	SetXMMRegister(X86_64_REGISTER_XMM9, extended.fp_ymm[9].value,
		extended.fp_fxsave.xmm[9].value);
	SetXMMRegister(X86_64_REGISTER_XMM10, extended.fp_ymm[10].value,
		extended.fp_fxsave.xmm[10].value);
	SetXMMRegister(X86_64_REGISTER_XMM11, extended.fp_ymm[11].value,
		extended.fp_fxsave.xmm[11].value);
	SetXMMRegister(X86_64_REGISTER_XMM12, extended.fp_ymm[12].value,
		extended.fp_fxsave.xmm[12].value);
	SetXMMRegister(X86_64_REGISTER_XMM13, extended.fp_ymm[13].value,
		extended.fp_fxsave.xmm[13].value);
	SetXMMRegister(X86_64_REGISTER_XMM14, extended.fp_ymm[14].value,
		extended.fp_fxsave.xmm[14].value);
	SetXMMRegister(X86_64_REGISTER_XMM15, extended.fp_ymm[15].value,
		extended.fp_fxsave.xmm[15].value);

	fInterruptVector = state.vector;
}


/** @brief Virtual destructor. */
CpuStateX8664::~CpuStateX8664()
{
}


/**
 * @brief Allocate and return a deep copy of this CPU state.
 *
 * Copies the integer/float/MMX/XMM register banks plus the bitmask of
 * which registers have been set and the interrupt vector.
 *
 * @param _clone  Output that receives the new CpuState.
 * @retval B_OK         Clone allocated.
 * @retval B_NO_MEMORY  Allocation failed.
 */
status_t
CpuStateX8664::Clone(CpuState*& _clone) const
{
	CpuStateX8664* newState = new(std::nothrow) CpuStateX8664();
	if (newState == NULL)
		return B_NO_MEMORY;


	memcpy(newState->fIntRegisters, fIntRegisters, sizeof(fIntRegisters));
	memcpy(newState->fFloatRegisters, fFloatRegisters,
		sizeof(fFloatRegisters));
	memcpy(newState->fMMXRegisters, fMMXRegisters, sizeof(fMMXRegisters));
	memcpy(newState->fXMMRegisters, fXMMRegisters, sizeof(fXMMRegisters));

	newState->fSetRegisters = fSetRegisters;
	newState->fInterruptVector = fInterruptVector;

	_clone = newState;

	return B_OK;
}


/**
 * @brief Serialize this state back into a kernel x86_64_debug_cpu_state blob.
 *
 * Writes integer/segment registers unconditionally and only emits MMX/XMM
 * registers that have actually been set; the remaining XMM slots are zeroed.
 *
 * @param state  Output blob; must be at least sizeof(x86_64_debug_cpu_state) bytes.
 * @param size   Size of @a state. Must equal sizeof(x86_64_debug_cpu_state).
 * @retval B_OK         Blob written.
 * @retval B_BAD_VALUE  @a size mismatch.
 */
status_t
CpuStateX8664::UpdateDebugState(void* state, size_t size) const
{
	if (size != sizeof(x86_64_debug_cpu_state))
		return B_BAD_VALUE;

	x86_64_debug_cpu_state* x64State = (x86_64_debug_cpu_state*)state;

	x64State->rip = InstructionPointer();
	x64State->rsp = StackPointer();
	x64State->rbp = StackFramePointer();
	x64State->rax = IntRegisterValue(X86_64_REGISTER_RAX);
	x64State->rbx = IntRegisterValue(X86_64_REGISTER_RBX);
	x64State->rcx = IntRegisterValue(X86_64_REGISTER_RCX);
	x64State->rdx = IntRegisterValue(X86_64_REGISTER_RDX);
	x64State->rsi = IntRegisterValue(X86_64_REGISTER_RSI);
	x64State->rdi = IntRegisterValue(X86_64_REGISTER_RDI);
	x64State->r8 = IntRegisterValue(X86_64_REGISTER_R8);
	x64State->r9 = IntRegisterValue(X86_64_REGISTER_R9);
	x64State->r10 = IntRegisterValue(X86_64_REGISTER_R10);
	x64State->r11 = IntRegisterValue(X86_64_REGISTER_R11);
	x64State->r12 = IntRegisterValue(X86_64_REGISTER_R12);
	x64State->r13 = IntRegisterValue(X86_64_REGISTER_R13);
	x64State->r14 = IntRegisterValue(X86_64_REGISTER_R14);
	x64State->r15 = IntRegisterValue(X86_64_REGISTER_R15);
	x64State->cs = IntRegisterValue(X86_64_REGISTER_CS);
	x64State->ds = IntRegisterValue(X86_64_REGISTER_DS);
	x64State->es = IntRegisterValue(X86_64_REGISTER_ES);
	x64State->fs = IntRegisterValue(X86_64_REGISTER_FS);
	x64State->gs = IntRegisterValue(X86_64_REGISTER_GS);
	x64State->ss = IntRegisterValue(X86_64_REGISTER_SS);

	for (int32 i = 0; i < 8; i++) {
		*(long double*)(x64State->extended_registers.fp_fxsave.fp[i].value)
			= (long double)FloatRegisterValue(X86_64_REGISTER_ST0 + i);

		if (IsRegisterSet(X86_64_REGISTER_MM0 + i)) {
			memcpy(&x64State->extended_registers.fp_fxsave.mmx[i],
				&fMMXRegisters[i], sizeof(x86_64_fp_register));
		}
	}

	for (int32 i = 0; i < 16; i++) {
		if (IsRegisterSet(X86_64_REGISTER_XMM0 + i)) {
			memcpy(&x64State->extended_registers.fp_fxsave.xmm[i],
				&fXMMRegisters[i], sizeof(x86_64_xmm_register));
		} else {
			memset(&x64State->extended_registers.fp_fxsave.xmm[i],
				0, sizeof(x86_64_xmm_register));
		}
	}

	return B_OK;
}


/** @brief Return RIP if set, otherwise zero. */
target_addr_t
CpuStateX8664::InstructionPointer() const
{
	return IsRegisterSet(X86_64_REGISTER_RIP)
		? IntRegisterValue(X86_64_REGISTER_RIP) : 0;
}


/**
 * @brief Update RIP to @a address.
 *
 * @param address  New value of the instruction pointer.
 */
void
CpuStateX8664::SetInstructionPointer(target_addr_t address)
{
	SetIntRegister(X86_64_REGISTER_RIP, address);
}


/** @brief Return RBP if set, otherwise zero. */
target_addr_t
CpuStateX8664::StackFramePointer() const
{
	return IsRegisterSet(X86_64_REGISTER_RBP)
		? IntRegisterValue(X86_64_REGISTER_RBP) : 0;
}


/** @brief Return RSP if set, otherwise zero. */
target_addr_t
CpuStateX8664::StackPointer() const
{
	return IsRegisterSet(X86_64_REGISTER_RSP)
		? IntRegisterValue(X86_64_REGISTER_RSP) : 0;
}


/**
 * @brief Read the value of a register described by @a reg into @a _value.
 *
 * Honors the register's value type, so 16-bit segment registers are
 * returned as uint16, x87 doubles as float or double, and MMX/XMM
 * registers as raw byte spans.
 *
 * @param reg     Register descriptor.
 * @param _value  Output that receives the value.
 * @return true if the register was set and decoded; false otherwise.
 */
bool
CpuStateX8664::GetRegisterValue(const Register* reg, BVariant& _value) const
{
	int32 index = reg->Index();
	if (!IsRegisterSet(index))
		return false;

	if (index >= X86_64_XMM_REGISTER_END)
		return false;

	if (BVariant::TypeIsInteger(reg->ValueType())) {
		if (reg->BitSize() == 16)
			_value.SetTo((uint16)fIntRegisters[index]);
		else
			_value.SetTo(fIntRegisters[index]);
	} else if (BVariant::TypeIsFloat(reg->ValueType())) {
		index -= X86_64_REGISTER_ST0;
		if (reg->ValueType() == B_FLOAT_TYPE)
			_value.SetTo((float)fFloatRegisters[index]);
		else
			_value.SetTo(fFloatRegisters[index]);
	} else {
		if (index >= X86_64_REGISTER_MM0 && index < X86_64_REGISTER_XMM0) {
			index -= X86_64_REGISTER_MM0;
			_value.SetTo(fMMXRegisters[index].value);
		} else {
			index -= X86_64_REGISTER_XMM0;
			_value.SetTo(fXMMRegisters[index].value);
		}
	}

	return true;
}


/**
 * @brief Update the value of the register described by @a reg.
 *
 * @param reg    Register descriptor.
 * @param value  New value.
 * @return true on success, false when @a reg is unknown or @a value is too large.
 */
bool
CpuStateX8664::SetRegisterValue(const Register* reg, const BVariant& value)
{
	int32 index = reg->Index();
	if (index >= X86_64_XMM_REGISTER_END)
		return false;

	if (index < X86_64_INT_REGISTER_END)
		fIntRegisters[index] = value.ToUInt64();
	else if (index >= X86_64_REGISTER_ST0 && index < X86_64_FP_REGISTER_END)
		fFloatRegisters[index - X86_64_REGISTER_ST0] = value.ToDouble();
	else if (index >= X86_64_REGISTER_MM0 && index < X86_64_MMX_REGISTER_END) {
		if (value.Size() > sizeof(int64))
			return false;
		memset(&fMMXRegisters[index - X86_64_REGISTER_MM0], 0,
			sizeof(x86_64_fp_register));
		memcpy(fMMXRegisters[index - X86_64_REGISTER_MM0].value,
			value.ToPointer(), value.Size());
	} else if (index >= X86_64_REGISTER_XMM0
			&& index < X86_64_XMM_REGISTER_END) {
		if (value.Size() > sizeof(x86_64_xmm_register))
			return false;

		memset(&fXMMRegisters[index - X86_64_REGISTER_XMM0], 0,
			sizeof(x86_64_xmm_register));
		memcpy(fXMMRegisters[index - X86_64_REGISTER_XMM0].value,
			value.ToPointer(), value.Size());
	} else
		return false;

	fSetRegisters[index] = 1;
	return true;
}


/**
 * @brief Test whether the register at @a index has been written.
 *
 * @param index  Native register index.
 * @return true if the register is set, false if it is unknown or unset.
 */
bool
CpuStateX8664::IsRegisterSet(int32 index) const
{
	return index >= 0 && index < X86_64_REGISTER_COUNT && fSetRegisters[index];
}


/**
 * @brief Read an integer register by index, returning 0 if unset.
 *
 * @param index  Integer register index.
 * @return The 64-bit register value, or 0 if unset or out of range.
 */
uint64
CpuStateX8664::IntRegisterValue(int32 index) const
{
	if (!IsRegisterSet(index) || index >= X86_64_INT_REGISTER_END)
		return 0;

	return fIntRegisters[index];
}


/**
 * @brief Update an integer register and mark it as set.
 *
 * @param index  Integer register index.
 * @param value  New value. Out-of-range indices are silently ignored.
 */
void
CpuStateX8664::SetIntRegister(int32 index, uint64 value)
{
	if (index < 0 || index >= X86_64_INT_REGISTER_END)
		return;

	fIntRegisters[index] = value;
	fSetRegisters[index] = 1;
}


/**
 * @brief Read an x87 floating-point register.
 *
 * @param index  Index in the ST0..ST7 range.
 * @return The double value, or 0.0 if unset or out of range.
 */
double
CpuStateX8664::FloatRegisterValue(int32 index) const
{
	if (index < X86_64_REGISTER_ST0 || index >= X86_64_FP_REGISTER_END
		|| !IsRegisterSet(index)) {
		return 0.0;
	}

	return fFloatRegisters[index - X86_64_REGISTER_ST0];
}


/**
 * @brief Update an x87 floating-point register.
 *
 * @param index  Index in the ST0..ST7 range.
 * @param value  New value.
 */
void
CpuStateX8664::SetFloatRegister(int32 index, double value)
{
	if (index < X86_64_REGISTER_ST0 || index >= X86_64_FP_REGISTER_END)
		return;

	fFloatRegisters[index - X86_64_REGISTER_ST0] = value;
	fSetRegisters[index] = 1;
}


/**
 * @brief Pointer to the raw bytes of an MMX register.
 *
 * @param index  Index in the MM0..MM7 range.
 * @return Pointer to the 64-bit raw value, or NULL if unset or out of range.
 */
const void*
CpuStateX8664::MMXRegisterValue(int32 index) const
{
	if (index < X86_64_REGISTER_MM0 || index >= X86_64_MMX_REGISTER_END
		|| !IsRegisterSet(index)) {
		return 0;
	}

	return fMMXRegisters[index - X86_64_REGISTER_MM0].value;
}


/**
 * @brief Update an MMX register from an 8-byte source buffer.
 *
 * @param index  Index in the MM0..MM7 range.
 * @param value  Source bytes; assumed to be sizeof(uint64) bytes long.
 */
void
CpuStateX8664::SetMMXRegister(int32 index, const uint8* value)
{
	if (index < X86_64_REGISTER_MM0 || index >= X86_64_MMX_REGISTER_END)
		return;

	memcpy(fMMXRegisters[index - X86_64_REGISTER_MM0].value, value,
		sizeof(uint64));
	fSetRegisters[index] = 1;
}


/**
 * @brief Pointer to the raw bytes of an XMM (or YMM) register.
 *
 * @param index  Index in the XMM0..XMM15 range.
 * @return Pointer to the raw value, or NULL if unset or out of range.
 */
const void*
CpuStateX8664::XMMRegisterValue(int32 index) const
{
	if (index < X86_64_REGISTER_XMM0 || index >= X86_64_XMM_REGISTER_END
		|| !IsRegisterSet(index)) {
		return NULL;
	}

	return fXMMRegisters[index - X86_64_REGISTER_XMM0].value;
}


/**
 * @brief Update a 256-bit XMM/YMM register from a high/low pair.
 *
 * @param index      XMM register index.
 * @param highValue  Upper 128 bits (YMM extension).
 * @param lowValue   Lower 128 bits (legacy XMM).
 */
void
CpuStateX8664::SetXMMRegister(int32 index, const uint8* highValue, const uint8* lowValue)
{
	if (index < X86_64_REGISTER_XMM0 || index >= X86_64_XMM_REGISTER_END)
		return;

	memcpy(&fXMMRegisters[index - X86_64_REGISTER_XMM0].value[0], lowValue,
		sizeof(x86_64_xmm_register));
	memcpy(&fXMMRegisters[index - X86_64_REGISTER_XMM0].value[2], highValue,
		sizeof(x86_64_xmm_register));
	fSetRegisters[index] = 1;
}


/**
 * @brief Mark a register as unset, clearing its bit in @c fSetRegisters.
 *
 * @param index  Native register index. Out-of-range values are ignored.
 */
void
CpuStateX8664::UnsetRegister(int32 index)
{
	if (index < 0 || index >= X86_64_REGISTER_COUNT)
		return;

	fSetRegisters[index] = 0;
}
