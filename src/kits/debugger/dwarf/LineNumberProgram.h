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
 * MIT License. Copyright 2009, Ingo Weinhold.
 */

/** @file LineNumberProgram.h
    @brief Interpreter for DWARF .debug_line line-number programs. */

#ifndef LINE_NUMBER_PROGRAM_H
#define LINE_NUMBER_PROGRAM_H

#include "DataReader.h"
#include "Types.h"


/**
 * @brief Replays a DWARF line program and emits matrix rows on demand.
 */
class LineNumberProgram {
public:
	struct State;

public:
								LineNumberProgram(uint8 addressSize, bool isBigEndian);
								~LineNumberProgram();

			status_t			Init(const void* program, size_t programSize,
									uint8 minInstructionLength,
									bool defaultIsStatement, int8 lineBase,
									uint8 lineRange, uint8 opcodeBase,
									const uint8* standardOpcodeLengths);

			bool				IsValid() const	{ return fProgram != NULL; }
			void				GetInitialState(State& state) const;
			bool				GetNextRow(State& state) const;

private:
			void				_SetToInitial(State& state) const;

private:
			const void*			fProgram;
			size_t				fProgramSize;
			uint8				fMinInstructionLength;
			bool				fDefaultIsStatement;
			int8				fLineBase;
			uint8				fLineRange;
			uint8				fOpcodeBase;
			uint8				fAddressSize;
			bool				fIsBigEndian;
			const uint8*		fStandardOpcodeLengths;
};


/**
 * @brief Mutable matrix row + reader cursor used by the line-program VM.
 */
struct LineNumberProgram::State {
	target_addr_t	address;
	int32			file;
	int32			line;
	int32			column;
	bool			isStatement;
	bool			isBasicBlock;
	bool			isSequenceEnd;
	bool			isPrologueEnd;
	bool			isEpilogueBegin;
	uint32			instructionSet;
	uint32			discriminator;

	// when file is set to -1
	const char*		explicitFile;
	uint32			explicitFileDirIndex;

	DataReader		dataReader;
};


#endif	// LINE_NUMBER_PROGRAM_H
