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
 *   Copyright 2008, François Revol, revol@free.fr
 *   Copyright 2016, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DisassemblerX86.cpp
 * @brief IA-32 disassembler wrapper around the Zydis library.
 *
 * Mirrors DisassemblerX8664 for 32-bit x86 code, producing AT&T-style
 * disassembly text and InstructionInfo records used by ArchitectureX86.
 */

#include "DisassemblerX86.h"

#include <new>

#include "Zycore/Format.h"
#include "Zydis/Zydis.h"

#include <OS.h>


#include "CpuStateX86.h"
#include "InstructionInfo.h"


/**
 * @brief Copy general-purpose register values from a CpuStateX86 into a Zydis context.
 *
 * @param state    Source CPU state.
 * @param context  Output Zydis register context populated with EIP/ESP/EAX/etc.
 */
void
CpuStateToZydisRegContext(CpuStateX86* state, ZydisRegisterContext* context)
{
	context->values[ZYDIS_REGISTER_EAX] = state->IntRegisterValue(X86_REGISTER_EAX);
	context->values[ZYDIS_REGISTER_ESP] = state->IntRegisterValue(X86_REGISTER_ESP);
	context->values[ZYDIS_REGISTER_EIP] = state->IntRegisterValue(X86_REGISTER_EIP);
	// context->values[ZYDIS_REGISTER_RFLAGS] = eflags;
	context->values[ZYDIS_REGISTER_ECX] = state->IntRegisterValue(X86_REGISTER_ECX);
	context->values[ZYDIS_REGISTER_EDX] = state->IntRegisterValue(X86_REGISTER_EDX);
	context->values[ZYDIS_REGISTER_EBX] = state->IntRegisterValue(X86_REGISTER_EBX);
	context->values[ZYDIS_REGISTER_EBP] = state->IntRegisterValue(X86_REGISTER_EBP);
	context->values[ZYDIS_REGISTER_ESI] = state->IntRegisterValue(X86_REGISTER_ESI);
	context->values[ZYDIS_REGISTER_EDI] = state->IntRegisterValue(X86_REGISTER_EDI);
}


/** @brief Opaque holder for the Zydis decoder/formatter and current decode offset. */
struct DisassemblerX86::ZydisData {
	ZydisDecoder decoder ;
	ZydisFormatter formatter;
	ZyanUSize offset;
};


/** @brief Construct an uninitialized disassembler; call Init() before use. */
DisassemblerX86::DisassemblerX86()
	:
	fAddress(0),
	fCode(NULL),
	fCodeSize(0),
	fZydisData(NULL)
{
}


/** @brief Destroy the disassembler and release the Zydis state. */
DisassemblerX86::~DisassemblerX86()
{
	delete fZydisData;
}


/**
 * @brief Initialize the disassembler over a code buffer.
 *
 * Configures Zydis for AT&T-style 32-bit decoding with no operand padding.
 *
 * @param address   Absolute address corresponding to the first byte of @a code.
 * @param code      Pointer to the code bytes.
 * @param codeSize  Number of bytes in @a code.
 * @retval B_OK         Initialized.
 * @retval B_NO_MEMORY  Could not allocate the Zydis state.
 */
status_t
DisassemblerX86::Init(target_addr_t address, const void* code, size_t codeSize)
{
	// unset old data
	delete fZydisData;
	fZydisData = NULL;

	// set new data
	fZydisData = new(std::nothrow) ZydisData;
	if (fZydisData == NULL)
		return B_NO_MEMORY;

	fAddress = address;
	fCode = (const uint8*)code;
	fCodeSize = codeSize;

	// init zydis
	fZydisData->offset = 0;
	ZydisDecoderInit(&fZydisData->decoder, ZYDIS_MACHINE_MODE_LEGACY_32, ZYDIS_STACK_WIDTH_32);
	ZydisFormatterInit(&fZydisData->formatter, ZYDIS_FORMATTER_STYLE_ATT);
	ZydisFormatterSetProperty(&fZydisData->formatter, ZYDIS_FORMATTER_PROP_FORCE_SIZE, ZYAN_TRUE);
	ZydisFormatterSetProperty(&fZydisData->formatter, ZYDIS_FORMATTER_PROP_HEX_UPPERCASE,
		ZYAN_FALSE);
	ZydisFormatterSetProperty(&fZydisData->formatter, ZYDIS_FORMATTER_PROP_ADDR_PADDING_ABSOLUTE,
		ZYDIS_PADDING_DISABLED);
	ZydisFormatterSetProperty(&fZydisData->formatter, ZYDIS_FORMATTER_PROP_ADDR_PADDING_RELATIVE,
		ZYDIS_PADDING_DISABLED);
	ZydisFormatterSetProperty(&fZydisData->formatter, ZYDIS_FORMATTER_PROP_DISP_PADDING,
		ZYDIS_PADDING_DISABLED);
	ZydisFormatterSetProperty(&fZydisData->formatter, ZYDIS_FORMATTER_PROP_IMM_PADDING,
		ZYDIS_PADDING_DISABLED);
		// TODO: Set the correct vendor!

	return B_OK;
}


/**
 * @brief Decode and format the next instruction in the buffer.
 *
 * Produces a line of the form "0xADDRESS: hex-bytes  mnemonic operands".
 *
 * @param line                 Output BString that receives the formatted line.
 * @param _address             Output address of the decoded instruction.
 * @param _size                Output instruction length in bytes.
 * @param _breakpointAllowed   Output flag; currently always set to true.
 * @retval B_OK               Instruction decoded.
 * @retval B_ENTRY_NOT_FOUND  No more bytes remain or decoding failed.
 */
status_t
DisassemblerX86::GetNextInstruction(BString& line, target_addr_t& _address,
	target_size_t& _size, bool& _breakpointAllowed)
{
	const uint8* buffer = fCode + fZydisData->offset;
	ZydisDecodedInstruction instruction;
	ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
	if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&fZydisData->decoder, buffer,
		fCodeSize - fZydisData->offset, &instruction, operands))) {
		return B_ENTRY_NOT_FOUND;
	}

	uint32 address = (uint32)(fAddress + fZydisData->offset);
	fZydisData->offset += instruction.length;

	char hexString[32];
	char* srcHex = hexString;
	for (ZyanUSize i = 0; i < instruction.length; i++) {
		sprintf(srcHex, "%02" PRIx8, buffer[i]);
		srcHex += 2;
	}

	char formatted[1024];
	if (ZYAN_SUCCESS(ZydisFormatterFormatInstruction(&fZydisData->formatter, &instruction,
		operands, instruction.operand_count_visible, formatted, sizeof(formatted), address,
		NULL))) {
		line.SetToFormat("0x%08" B_PRIx32 ": %16.16s  %s", address, hexString, formatted);
	} else {
		line.SetToFormat("0x%08" B_PRIx32 ": failed-to-format", address);
	}
		// TODO: Resolve symbols!

	_address = address;
	_size = instruction.length;
	_breakpointAllowed = true;
		// TODO: Implement (rep!)!

	return B_OK;
}


/**
 * @brief Find the address and size of the instruction that ends at @a nextAddress.
 *
 * @param nextAddress  Address at which the previous instruction ends.
 * @param _address     Output address.
 * @param _size        Output size of the previous instruction.
 * @retval B_OK               Previous instruction located.
 * @retval B_BAD_VALUE        @a nextAddress lies outside the buffer.
 * @retval B_ENTRY_NOT_FOUND  Decoding failed before reaching @a nextAddress.
 */
status_t
DisassemblerX86::GetPreviousInstruction(target_addr_t nextAddress,
	target_addr_t& _address, target_size_t& _size)
{
	if (nextAddress < fAddress || nextAddress > fAddress + fCodeSize)
		return B_BAD_VALUE;

	// loop until hitting the last instruction
	while (true) {
		const uint8* buffer = fCode + fZydisData->offset;
		ZydisDecodedInstruction instruction;
		if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&fZydisData->decoder,
				(ZydisDecoderContext*)ZYAN_NULL, buffer, fCodeSize - fZydisData->offset,
				&instruction))) {
			return B_ENTRY_NOT_FOUND;
		}

		fZydisData->offset += instruction.length;
		target_addr_t address = fAddress + fZydisData->offset;
		if (address == nextAddress) {
			_address = address;
			_size = instruction.length;
			return B_OK;
		}
	}
}


/**
 * @brief Decode the next instruction into an InstructionInfo (rather than just text).
 *
 * Recognizes call and jmp mnemonics so the resulting record carries the
 * correct INSTRUCTION_TYPE_*. When @a state is non-NULL the Zydis register
 * context is populated so absolute target addresses are resolved.
 *
 * @param _info  Output info record describing address, target, size, and text.
 * @param state  Optional CPU state used for absolute-target resolution.
 * @retval B_OK               Instruction decoded.
 * @retval B_NO_MEMORY        SetTo() on @a _info failed.
 * @retval B_ENTRY_NOT_FOUND  No more bytes remain or decoding failed.
 */
status_t
DisassemblerX86::GetNextInstructionInfo(InstructionInfo& _info,
	CpuState* state)
{
	const uint8* buffer = fCode + fZydisData->offset;
	ZydisDecodedInstruction instruction;
	ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
	if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&fZydisData->decoder, buffer,
		fCodeSize - fZydisData->offset, &instruction, operands))) {
		return B_ENTRY_NOT_FOUND;
	}

	uint32 address = (uint32)(fAddress + fZydisData->offset);
	fZydisData->offset += instruction.length;

	char hexString[32];
	char* srcHex = hexString;
	for (ZyanUSize i = 0; i < instruction.length; i++) {
		sprintf(srcHex, "%02" PRIx8, buffer[i]);
		srcHex += 2;
	}

	instruction_type type = INSTRUCTION_TYPE_OTHER;
	target_addr_t targetAddress = 0;
	if (instruction.mnemonic == ZYDIS_MNEMONIC_CALL)
		type = INSTRUCTION_TYPE_SUBROUTINE_CALL;
	else if (instruction.mnemonic == ZYDIS_MNEMONIC_JMP)
		type = INSTRUCTION_TYPE_JUMP;
	if (state != NULL) {
		CpuStateX86* x86State = dynamic_cast<CpuStateX86*>(state);
		if (x86State != NULL) {
			ZydisRegisterContext registers;
			CpuStateToZydisRegContext(x86State, &registers);
			ZYAN_CHECK(ZydisCalcAbsoluteAddressEx(&instruction, operands,
				address, &registers, &targetAddress));
		}
	}

	char string[1024];
	int written = snprintf(string, sizeof(string), "0x%08" B_PRIx32 ": %16.16s  ", address,
		hexString);
	char* formatted = string + written;
	if (!ZYAN_SUCCESS(ZydisFormatterFormatInstruction(&fZydisData->formatter, &instruction,
		operands, instruction.operand_count_visible, formatted, sizeof(string) - written, address,
		NULL))) {
		snprintf(string, sizeof(string), "0x%08" B_PRIx32 ": failed-to-format", address);
	}

		// TODO: Resolve symbols!

	if (!_info.SetTo(address, targetAddress, instruction.length, type, true, string))
		return B_NO_MEMORY;

	return B_OK;
}

