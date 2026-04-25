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
 * MIT License. Copyright 2009, Haiku.
 * Original authors: Ingo Weinhold.
 */

/** @file InstructionInfo.h
    @brief Plain-data record describing a single decoded machine instruction. */

#ifndef INSTRUCTION_INFO_H
#define INSTRUCTION_INFO_H

#include <String.h>

#include "Types.h"


/** @brief Coarse classification of a decoded instruction. */
enum instruction_type {
	INSTRUCTION_TYPE_SUBROUTINE_CALL,
	INSTRUCTION_TYPE_JUMP,
	INSTRUCTION_TYPE_OTHER
};


/** @brief Holds the decoded address, target, size, type, and disassembly text of one instruction. */
class InstructionInfo {
public:
								InstructionInfo();
								InstructionInfo(target_addr_t address,
									target_addr_t targetAddress,
									target_size_t size, instruction_type type,
									bool breakpointAllowed,
									const BString& disassembledLine);

			bool				SetTo(target_addr_t address,
									target_addr_t targetAddress,
									target_size_t size,
									instruction_type type,
									bool breakpointAllowed,
									const BString& disassembledLine);

			/** @brief Address at which the instruction is located. */
			target_addr_t		Address() const		{ return fAddress; }
			/** @brief Resolved branch/call target, or 0 if not applicable. */
			target_addr_t		TargetAddress() const
									{ return fTargetAddress; }
			/** @brief Length of the instruction in bytes. */
			target_size_t		Size() const		{ return fSize; }
			/** @brief Coarse instruction category. */
			instruction_type	Type() const		{ return fType; }
			/** @brief Whether a software breakpoint may be planted on this instruction. */
			bool				IsBreakpointAllowed() const
									{ return fBreakpointAllowed; }
			/** @brief Pre-formatted disassembly text. */
			const char*			DisassembledLine() const
									{ return fDisassembledLine.String(); }


private:
			target_addr_t		fAddress;
			target_addr_t		fTargetAddress;
			target_size_t		fSize;
			instruction_type	fType;
			bool				fBreakpointAllowed;
			BString				fDisassembledLine;
};


#endif	// INSTRUCTION_INFO_H
