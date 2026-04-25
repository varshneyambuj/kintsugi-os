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
 *   Copyright 2008, François Revol, revol@free.fr
 *   Copyright 2016, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DisassemblerX8664.cpp
 * @brief x86_64 disassembler wrapper around the Zydis library.
 *
 * Walks a contiguous code buffer producing AT&T-style disassembly text and
 * decoded InstructionInfo records. Used by ArchitectureX8664 to render
 * source views and to introspect call/jump targets during stack walking.
 */

#include "DisassemblerX8664.h"

#include <new>

#include "Zycore/Format.h"
#include "Zydis/Zydis.h"

#include <OS.h>


#include "CpuStateX8664.h"
#include "InstructionInfo.h"


/**
 * @brief Copy general-purpose register values from a CpuStateX8664 into a Zydis context.
 *
 * Used so Zydis can compute absolute target addresses for RIP-relative and
 * register-relative operands.
 *
 * @param state    Source CPU state.
 * @param context  Output Zydis register context populated with RIP/RSP/RAX/etc.
 */
void
CpuStateToZydisRegContext(CpuStateX8664* state, ZydisRegisterContext* context)
{
	context->values[ZYDIS_REGISTER_RAX] = state->IntRegisterValue(X86_64_REGISTER_RAX);
	context->values[ZYDIS_REGISTER_RSP] = state->IntRegisterValue(X86_64_REGISTER_RSP);
	context->values[ZYDIS_REGISTER_RIP] = state->IntRegisterValue(X86_64_REGISTER_RIP);
	// context->values[ZYDIS_REGISTER_RFLAGS] = eflags;
	context->values[ZYDIS_REGISTER_RCX] = state->IntRegisterValue(X86_64_REGISTER_RCX);
	context->values[ZYDIS_REGISTER_RDX] = state->IntRegisterValue(X86_64_REGISTER_RDX);
	context->values[ZYDIS_REGISTER_RBX] = state->IntRegisterValue(X86_64_REGISTER_RBX);
	context->values[ZYDIS_REGISTER_RBP] = state->IntRegisterValue(X86_64_REGISTER_RBP);
	context->values[ZYDIS_REGISTER_RSI] = state->IntRegisterValue(X86_64_REGISTER_RSI);
	context->values[ZYDIS_REGISTER_RDI] = state->IntRegisterValue(X86_64_REGISTER_RDI);
	context->values[ZYDIS_REGISTER_R8] = state->IntRegisterValue(X86_64_REGISTER_R8);
	context->values[ZYDIS_REGISTER_R9] = state->IntRegisterValue(X86_64_REGISTER_R9);
	context->values[ZYDIS_REGISTER_R10] = state->IntRegisterValue(X86_64_REGISTER_R10);
	context->values[ZYDIS_REGISTER_R11] = state->IntRegisterValue(X86_64_REGISTER_R11);
	context->values[ZYDIS_REGISTER_R12] = state->IntRegisterValue(X86_64_REGISTER_R12);
	context->values[ZYDIS_REGISTER_R13] = state->IntRegisterValue(X86_64_REGISTER_R13);
	context->values[ZYDIS_REGISTER_R14] = state->IntRegisterValue(X86_64_REGISTER_R14);
	context->values[ZYDIS_REGISTER_R15] = state->IntRegisterValue(X86_64_REGISTER_R15);
}


/** @brief Opaque holder for the Zydis decoder/formatter and current decode offset. */
struct DisassemblerX8664::ZydisData {
	ZydisDecoder decoder ;
	ZydisFormatter formatter;
	ZyanUSize offset;
};


/** @brief Construct an uninitialized disassembler; call Init() before use. */
DisassemblerX8664::DisassemblerX8664()
	:
	fAddress(0),
	fCode(NULL),
	fCodeSize(0),
	fZydisData(NULL)
{
}


/** @brief Destroy the disassembler and release the Zydis state. */
DisassemblerX8664::~DisassemblerX8664()
{
	delete fZydisData;
}


/**
 * @brief Initialize the disassembler over a code buffer.
 *
 * Configures Zydis for AT&T-style 64-bit decoding with no operand padding.
 * Subsequent GetNextInstruction() calls advance through @a code.
 *
 * @param address   Absolute address corresponding to the first byte of @a code.
 * @param code      Pointer to the code bytes (already read from the target).
 * @param codeSize  Number of bytes in @a code.
 * @retval B_OK         Initialized.
 * @retval B_NO_MEMORY  Could not allocate the Zydis state.
 */
status_t
DisassemblerX8664::Init(target_addr_t address, const void* code, size_t codeSize)
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
	ZydisDecoderInit(&fZydisData->decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
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
DisassemblerX8664::GetNextInstruction(BString& line, target_addr_t& _address,
	target_size_t& _size, bool& _breakpointAllowed)
{
	const uint8* buffer = fCode + fZydisData->offset;
	ZydisDecodedInstruction instruction;
	ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
	if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&fZydisData->decoder, buffer,
		fCodeSize - fZydisData->offset, &instruction, operands))) {
		return B_ENTRY_NOT_FOUND;
	}

	target_addr_t address = fAddress + fZydisData->offset;
	fZydisData->offset += instruction.length;

	char hexString[32];
	char* srcHex = hexString;
	for (ZyanUSize i = 0; i < instruction.length; i++) {
		sprintf(srcHex, "%02" PRIx8, buffer[i]);
		srcHex += 2;
	}

	char formatted[1024];
	if (ZYAN_SUCCESS(ZydisFormatterFormatInstruction(&fZydisData->formatter, &instruction, operands,
		instruction.operand_count_visible, formatted, sizeof(formatted), address, NULL))) {
		line.SetToFormat("0x%016" B_PRIx64 ": %16.16s  %s", address, hexString, formatted);
	} else {
		line.SetToFormat("0x%016" B_PRIx64 ": failed-to-format", address);
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
 * Walks instructions linearly until the offset matches @a nextAddress, used
 * by ArchitectureX8664::UpdateStackFrameCpuState() to back up RIP from a
 * return address to the calling instruction.
 *
 * @param nextAddress  Address at which the previous instruction ends.
 * @param _address     Output address (currently equals @a nextAddress on success).
 * @param _size        Output size of the previous instruction.
 * @retval B_OK               Previous instruction located.
 * @retval B_BAD_VALUE        @a nextAddress lies outside the buffer.
 * @retval B_ENTRY_NOT_FOUND  Decoding failed before reaching @a nextAddress.
 */
status_t
DisassemblerX8664::GetPreviousInstruction(target_addr_t nextAddress,
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
 * correct INSTRUCTION_TYPE_*. When @a state is non-NULL, populates the
 * Zydis register context so absolute target addresses are resolved.
 *
 * @param _info  Output info record describing address, target, size, and text.
 * @param state  Optional CPU state used for absolute-target resolution.
 * @retval B_OK               Instruction decoded.
 * @retval B_NO_MEMORY        SetTo() on @a _info failed.
 * @retval B_ENTRY_NOT_FOUND  No more bytes remain or decoding failed.
 */
status_t
DisassemblerX8664::GetNextInstructionInfo(InstructionInfo& _info,
	CpuState* state)
{
	const uint8* buffer = fCode + fZydisData->offset;
	ZydisDecodedInstruction instruction;
	ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
	if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&fZydisData->decoder, buffer,
		fCodeSize - fZydisData->offset, &instruction, operands))) {
		return B_ENTRY_NOT_FOUND;
	}

	target_addr_t address = fAddress + fZydisData->offset;
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
		CpuStateX8664* x64State = dynamic_cast<CpuStateX8664*>(state);
		if (x64State != NULL) {
			ZydisRegisterContext registers;
			CpuStateToZydisRegContext(x64State, &registers);
			ZYAN_CHECK(ZydisCalcAbsoluteAddressEx(&instruction, operands,
				address, &registers, &targetAddress));
		}
	}

	char string[1024];
	int written = snprintf(string, sizeof(string), "0x%016" B_PRIx64 ": %16.16s  ", address,
		hexString);
	char* formatted = string + written;
	if (!ZYAN_SUCCESS(ZydisFormatterFormatInstruction(&fZydisData->formatter, &instruction,
		operands, instruction.operand_count_visible, formatted, sizeof(string) - written,
		address, NULL))) {
		snprintf(string, sizeof(string), "0x%016" B_PRIx64 ": failed-to-format", address);
	}

		// TODO: Resolve symbols!

	if (!_info.SetTo(address, targetAddress, instruction.length, type, true, string))
		return B_NO_MEMORY;

	return B_OK;
}

