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
 * Original authors: Alex Smith, Ingo Weinhold.
 */

/** @file DisassemblerX8664.h
    @brief x86_64 disassembler interface backed by the Zydis library. */

#ifndef DISASSEMBLER_X86_64_H
#define DISASSEMBLER_X86_64_H

#include <String.h>

#include "Types.h"


class CpuState;
class InstructionInfo;


/** @brief x86_64 disassembler that wraps Zydis to render AT&T-style listings. */
class DisassemblerX8664 {
public:
								DisassemblerX8664();
	virtual						~DisassemblerX8664();

	virtual	status_t			Init(target_addr_t address, const void* code,
									size_t codeSize);

	virtual	status_t			GetNextInstruction(BString& line,
									target_addr_t& _address,
									target_size_t& _size,
									bool& _breakpointAllowed);
	virtual	status_t			GetPreviousInstruction(
									target_addr_t nextAddress,
									target_addr_t& _address,
									target_size_t& _size);
	virtual	status_t			GetNextInstructionInfo(
									InstructionInfo& _info,
									CpuState* state);

private:
			struct ZydisData;

private:
			target_addr_t		fAddress;
			const uint8*		fCode;
			size_t				fCodeSize;
			ZydisData*			fZydisData;
};


#endif	// DISASSEMBLER_X86_64_H
