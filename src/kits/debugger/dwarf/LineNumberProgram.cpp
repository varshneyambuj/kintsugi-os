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


/**
 * @file LineNumberProgram.cpp
 * @brief Interpreter for DWARF .debug_line line-number programs.
 *
 * The DWARF line number program is a tiny VM whose state is the matrix
 * row (address, file, line, column, flags) and whose instruction stream
 * advances or emits new rows.  This file implements row-by-row playback:
 * the caller starts with @ref GetInitialState and then repeatedly calls
 * @ref GetNextRow to enumerate every (address -> file:line) mapping for a
 * compilation unit.
 */

#include "LineNumberProgram.h"

#include <algorithm>

#include <stdio.h>
#include <string.h>

#include "Dwarf.h"
#include "Tracing.h"


/** @brief Expected operand count for each DW_LNS_* standard opcode. */
static const uint8 kLineNumberStandardOpcodeOperands[]
	= { 0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1 };
/** @brief Number of standard opcodes the interpreter knows. */
static const uint32 kLineNumberStandardOpcodeCount = 12;


/**
 * @brief Constructs an uninitialised line program.
 *
 * @param addressSize Width of a target address in bytes.
 * @param isBigEndian @c true if the target uses big-endian byte order.
 */
LineNumberProgram::LineNumberProgram(uint8 addressSize, bool isBigEndian)
	:
	fProgram(NULL),
	fProgramSize(0),
	fMinInstructionLength(0),
	fDefaultIsStatement(0),
	fLineBase(0),
	fLineRange(0),
	fOpcodeBase(0),
	fAddressSize(addressSize),
	fIsBigEndian(isBigEndian),
	fStandardOpcodeLengths(NULL)
{
}


/**
 * @brief Destroys the line program.  Does not own the byte stream.
 */
LineNumberProgram::~LineNumberProgram()
{
}


/**
 * @brief Binds the interpreter to a parsed line-program prologue.
 *
 * Validates that each standard-opcode operand count matches DWARF's
 * specification before retaining pointers into the section.
 *
 * @param program                Pointer to the program byte stream.
 * @param programSize            Size of @a program in bytes.
 * @param minInstructionLength   DW_LNS prologue field controlling pc advance.
 * @param defaultIsStatement     Default value of the @c is_stmt flag.
 * @param lineBase               Signed bias for special-opcode line decoding.
 * @param lineRange              Range divisor for special-opcode decoding.
 * @param opcodeBase             First special-opcode value.
 * @param standardOpcodeLengths  Operand counts for each DW_LNS_* opcode.
 * @retval B_OK         Program accepted; interpreter is ready.
 * @retval B_BAD_DATA   Operand-length table disagrees with the DWARF spec.
 */
status_t
LineNumberProgram::Init(const void* program, size_t programSize,
	uint8 minInstructionLength, bool defaultIsStatement, int8 lineBase,
	uint8 lineRange, uint8 opcodeBase, const uint8* standardOpcodeLengths)
{
	// first check the operand counts for the standard opcodes
	uint8 standardOpcodeCount = std::min((uint32)opcodeBase - 1,
		kLineNumberStandardOpcodeCount);
	for (uint8 i = 0; i < standardOpcodeCount; i++) {
		if (standardOpcodeLengths[i] != kLineNumberStandardOpcodeOperands[i]) {
			WARNING("operand count for standard opcode %u does not what we "
				"expect\n", i + 1);
			return B_BAD_DATA;
		}
	}

	fProgram = program;
	fProgramSize = programSize;
	fMinInstructionLength = minInstructionLength;
	fDefaultIsStatement = defaultIsStatement;
	fLineBase = lineBase;
	fLineRange = lineRange;
	fOpcodeBase = opcodeBase;
	fStandardOpcodeLengths = standardOpcodeLengths;

	return B_OK;
}


/**
 * @brief Resets @a state to the initial row defined by the DWARF spec.
 *
 * Also rewinds the embedded DataReader to the start of the program.
 *
 * @param state State object to reset.
 */
void
LineNumberProgram::GetInitialState(State& state) const
{
	if (!IsValid())
		return;

	_SetToInitial(state);
	state.dataReader.SetTo(fProgram, fProgramSize, fAddressSize, fIsBigEndian);
}


/**
 * @brief Advances the interpreter until the next matrix row is emitted.
 *
 * Decodes special, standard, and extended opcodes in turn.  When an
 * end-of-sequence is observed the interpreter resets to the initial
 * state on the next call so that multiple concatenated programs can be
 * walked back-to-back.
 *
 * @param state In/out state describing the current row.
 * @return @c true when @a state holds a freshly produced row;
 *         @c false when the program is exhausted or has overflowed.
 */
bool
LineNumberProgram::GetNextRow(State& state) const
{
	if (state.isSequenceEnd)
		_SetToInitial(state);

	DataReader& dataReader = state.dataReader;

	while (dataReader.BytesRemaining() > 0) {
		bool appendRow = false;
		uint8 opcode = dataReader.Read<uint8>(0);
		if (opcode >= fOpcodeBase) {
			// special opcode
			uint adjustedOpcode = opcode - fOpcodeBase;
			state.address += (adjustedOpcode / fLineRange)
				* fMinInstructionLength;
			state.line += adjustedOpcode % fLineRange + fLineBase;
			state.isBasicBlock = false;
			state.isPrologueEnd = false;
			state.isEpilogueBegin = false;
			state.discriminator = 0;
			appendRow = true;
		} else if (opcode > 0) {
			// standard opcode
			switch (opcode) {
				case DW_LNS_copy:
					state.isBasicBlock = false;
					state.isPrologueEnd = false;
					state.isEpilogueBegin = false;
					appendRow = true;
					state.discriminator = 0;
					break;
				case DW_LNS_advance_pc:
					state.address += dataReader.ReadUnsignedLEB128(0)
						* fMinInstructionLength;
					break;
				case DW_LNS_advance_line:
					state.line += dataReader.ReadSignedLEB128(0);
					break;
				case DW_LNS_set_file:
					state.file = dataReader.ReadUnsignedLEB128(0);
					break;
				case DW_LNS_set_column:
					state.column = dataReader.ReadUnsignedLEB128(0);
					break;
				case DW_LNS_negate_stmt:
					state.isStatement = !state.isStatement;
					break;
				case DW_LNS_set_basic_block:
					state.isBasicBlock = true;
					break;
				case DW_LNS_const_add_pc:
					state.address += ((255 - fOpcodeBase) / fLineRange)
						* fMinInstructionLength;
					break;
				case DW_LNS_fixed_advance_pc:
					state.address += dataReader.Read<uint16>(0);
					break;
				case DW_LNS_set_prologue_end:
					state.isPrologueEnd = true;
					break;
				case DW_LNS_set_epilogue_begin:
					state.isEpilogueBegin = true;
					break;
				case DW_LNS_set_isa:
					state.instructionSet = dataReader.ReadUnsignedLEB128(0);
					break;
				default:
					WARNING("unsupported standard opcode %u\n", opcode);
					for (int32 i = 0; i < fStandardOpcodeLengths[opcode - 1];
							i++) {
						dataReader.ReadUnsignedLEB128(0);
					}
			}
		} else {
			// extended opcode
			uint32 instructionLength = dataReader.ReadUnsignedLEB128(0);
			off_t instructionOffset = dataReader.Offset();
			uint8 extendedOpcode = dataReader.Read<uint8>(0);

			switch (extendedOpcode) {
				case DW_LNE_end_sequence:
					state.isSequenceEnd = true;
					appendRow = true;
					break;
				case DW_LNE_set_address:
					state.address = dataReader.ReadAddress(0);
					break;
				case DW_LNE_define_file:
				{
					state.explicitFile = dataReader.ReadString();
					state.explicitFileDirIndex
						= dataReader.ReadUnsignedLEB128(0);
					dataReader.ReadUnsignedLEB128(0);	// modification time
					dataReader.ReadUnsignedLEB128(0);	// file length
					state.file = -1;
					break;
				}
				case DW_LNE_set_discriminator:
				{
					state.discriminator = dataReader.ReadUnsignedLEB128(0);
					break;
				}
				default:
					WARNING("unsupported extended opcode: %u\n",
						extendedOpcode);
					break;
			}

			dataReader.Skip(instructionLength
				- (dataReader.Offset() - instructionOffset));
		}

		if (dataReader.HasOverflow())
			return false;

		if (appendRow)
			return true;
	}

	return false;
}


/**
 * @brief Initialises @a state to the canonical starting row.
 *
 * Mirrors the values listed in DWARF v5 section 6.2.2.
 *
 * @param state State object to initialise.
 */
void
LineNumberProgram::_SetToInitial(State& state) const
{
	state.address = 0;
	state.file = 1;
	state.line = 1;
	state.column = 0;
	state.isStatement = fDefaultIsStatement;
	state.isBasicBlock = false;
	state.isSequenceEnd = false;
	state.isPrologueEnd = false;
	state.isEpilogueBegin = false;
	state.instructionSet = 0;
	state.discriminator = 0;
}
