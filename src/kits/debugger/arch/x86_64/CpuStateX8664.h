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
 * MIT License. Copyright 2009-2013, Haiku.
 * Original authors: Alex Smith, Ingo Weinhold, Rene Gollent.
 */

/** @file CpuStateX8664.h
    @brief x86_64 CPU state declaration plus register-index enumeration. */

#ifndef CPU_STATE_X86_64_H
#define CPU_STATE_X86_64_H

#include <bitset>

#include <debugger.h>

#include "CpuState.h"


/** @brief Native register indices for the x86_64 CPU state. */
enum {
	X86_64_REGISTER_RIP = 0,
	X86_64_REGISTER_RSP,
	X86_64_REGISTER_RBP,

	X86_64_REGISTER_RAX,
	X86_64_REGISTER_RBX,
	X86_64_REGISTER_RCX,
	X86_64_REGISTER_RDX,

	X86_64_REGISTER_RSI,
	X86_64_REGISTER_RDI,

	X86_64_REGISTER_R8,
	X86_64_REGISTER_R9,
	X86_64_REGISTER_R10,
	X86_64_REGISTER_R11,
	X86_64_REGISTER_R12,
	X86_64_REGISTER_R13,
	X86_64_REGISTER_R14,
	X86_64_REGISTER_R15,

	X86_64_REGISTER_CS,
	X86_64_REGISTER_DS,
	X86_64_REGISTER_ES,
	X86_64_REGISTER_FS,
	X86_64_REGISTER_GS,
	X86_64_REGISTER_SS,

	X86_64_INT_REGISTER_END,

	X86_64_REGISTER_ST0,
	X86_64_REGISTER_ST1,
	X86_64_REGISTER_ST2,
	X86_64_REGISTER_ST3,
	X86_64_REGISTER_ST4,
	X86_64_REGISTER_ST5,
	X86_64_REGISTER_ST6,
	X86_64_REGISTER_ST7,

	X86_64_FP_REGISTER_END,

	X86_64_REGISTER_MM0,
	X86_64_REGISTER_MM1,
	X86_64_REGISTER_MM2,
	X86_64_REGISTER_MM3,
	X86_64_REGISTER_MM4,
	X86_64_REGISTER_MM5,
	X86_64_REGISTER_MM6,
	X86_64_REGISTER_MM7,

	X86_64_MMX_REGISTER_END,

	X86_64_REGISTER_XMM0,
	X86_64_REGISTER_XMM1,
	X86_64_REGISTER_XMM2,
	X86_64_REGISTER_XMM3,
	X86_64_REGISTER_XMM4,
	X86_64_REGISTER_XMM5,
	X86_64_REGISTER_XMM6,
	X86_64_REGISTER_XMM7,
	X86_64_REGISTER_XMM8,
	X86_64_REGISTER_XMM9,
	X86_64_REGISTER_XMM10,
	X86_64_REGISTER_XMM11,
	X86_64_REGISTER_XMM12,
	X86_64_REGISTER_XMM13,
	X86_64_REGISTER_XMM14,
	X86_64_REGISTER_XMM15,

	X86_64_XMM_REGISTER_END,

	X86_64_REGISTER_COUNT
};


#define X86_64_INT_REGISTER_COUNT X86_64_INT_REGISTER_END
#define X86_64_FP_REGISTER_COUNT (X86_64_FP_REGISTER_END \
	- X86_64_INT_REGISTER_END)
#define X86_64_MMX_REGISTER_COUNT (X86_64_MMX_REGISTER_END \
	- X86_64_FP_REGISTER_END)
#define X86_64_XMM_REGISTER_COUNT (X86_64_XMM_REGISTER_END \
	- X86_64_MMX_REGISTER_END)


/** @brief 256-bit YMM register payload (4 x 64-bit words). */
struct x86_64_ymm_register {
	unsigned long value[4];
};


/** @brief x86_64 implementation of the CpuState interface. */
class CpuStateX8664 : public CpuState {
public:
								CpuStateX8664();
								CpuStateX8664(const x86_64_debug_cpu_state& state);
	virtual						~CpuStateX8664();

	virtual	status_t			Clone(CpuState*& _clone) const;

	virtual	status_t			UpdateDebugState(void* state, size_t size)
									const;

	virtual	target_addr_t		InstructionPointer() const;
	virtual void				SetInstructionPointer(target_addr_t address);

	virtual	target_addr_t		StackFramePointer() const;
	virtual	target_addr_t		StackPointer() const;
	virtual	bool				GetRegisterValue(const Register* reg,
									BVariant& _value) const;
	virtual	bool				SetRegisterValue(const Register* reg,
									const BVariant& value);

			/** @brief Return the interrupt vector that produced this state. */
			uint64				InterruptVector() const
									{ return fInterruptVector; }

			bool				IsRegisterSet(int32 index) const;

			uint64				IntRegisterValue(int32 index) const;
			void				SetIntRegister(int32 index, uint64 value);

private:
			double				FloatRegisterValue(int32 index) const;
			void				SetFloatRegister(int32 index, double value);

			const void*			MMXRegisterValue(int32 index) const;
			void				SetMMXRegister(int32 index,
									const uint8* value);

			const void*			XMMRegisterValue(int32 index) const;
			void				SetXMMRegister(int32 index,
									const uint8* highValue, const uint8* lowValue);

			void				UnsetRegister(int32 index);

private:
	typedef std::bitset<X86_64_REGISTER_COUNT> RegisterBitSet;

private:
			uint64				fIntRegisters[X86_64_INT_REGISTER_COUNT];
			double				fFloatRegisters[X86_64_FP_REGISTER_COUNT];
			x86_64_fp_register	fMMXRegisters[X86_64_MMX_REGISTER_COUNT];
			x86_64_ymm_register	fXMMRegisters[X86_64_XMM_REGISTER_COUNT];
			RegisterBitSet		fSetRegisters;
			uint64				fInterruptVector;
};


#endif	// CPU_STATE_X86_64_H
