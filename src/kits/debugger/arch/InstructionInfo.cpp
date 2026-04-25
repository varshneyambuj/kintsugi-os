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
 *   Distributed under the terms of the MIT License.
 */


/** @file InstructionInfo.cpp
    @brief Plain-data record describing a single decoded machine instruction. */

#include "InstructionInfo.h"


/** @brief Construct an empty record with type INSTRUCTION_TYPE_OTHER and zeroed addresses. */
InstructionInfo::InstructionInfo()
	:
	fAddress(0),
	fTargetAddress(0),
	fSize(0),
	fType(INSTRUCTION_TYPE_OTHER),
	fBreakpointAllowed(false),
	fDisassembledLine()
{
}


/**
 * @brief Construct a fully populated record describing a decoded instruction.
 *
 * @param address            Address at which the instruction lives.
 * @param targetAddress      Resolved branch/call target, or 0 if not applicable.
 * @param size               Length of the instruction in bytes.
 * @param type               Instruction category (call, jump, other).
 * @param breakpointAllowed  true if a software breakpoint may be planted here.
 * @param disassembledLine   Pre-formatted disassembly text.
 */
InstructionInfo::InstructionInfo(target_addr_t address,
	target_addr_t targetAddress, target_size_t size,
	instruction_type type, bool breakpointAllowed,
	const BString& disassembledLine)
	:
	fAddress(address),
	fTargetAddress(targetAddress),
	fSize(size),
	fType(type),
	fBreakpointAllowed(breakpointAllowed),
	fDisassembledLine(disassembledLine)
{
}


/**
 * @brief Reinitialize the record in place.
 *
 * @param address            Instruction address.
 * @param targetAddress      Resolved branch/call target.
 * @param size               Instruction size in bytes.
 * @param type               Instruction category.
 * @param breakpointAllowed  Whether breakpoints may be set here.
 * @param disassembledLine   Disassembly text.
 * @return true on success. Returns false if the disassembly text could not
 *         be assigned (out of memory) and a non-empty input was provided.
 */
bool
InstructionInfo::SetTo(target_addr_t address, target_addr_t targetAddress,
	target_size_t size, instruction_type type, bool breakpointAllowed,
	const BString& disassembledLine)
{
	fAddress = address;
	fTargetAddress = targetAddress;
	fSize = size;
	fType = type;
	fBreakpointAllowed = breakpointAllowed;
	fDisassembledLine = disassembledLine;
	return disassembledLine.Length() == 0 || fDisassembledLine.Length() > 0;
}
