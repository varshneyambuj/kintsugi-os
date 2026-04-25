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
 *   Copyright 2013-2015, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ValueWriter.cpp
 * @brief Implementation of ValueWriter, the inverse of ValueLoader for committing edits.
 *
 * Where ValueLoader reads bytes out of the target into a BVariant, ValueWriter
 * accepts a freshly edited BVariant and pushes it back into the target via
 * memory writes and CPU-state updates. Used by the variables view's inline
 * edit path.
 *
 * @see ValueLoader, DebuggerInterface
 */


#include "ValueWriter.h"

#include "Architecture.h"
#include "BitBuffer.h"
#include "CpuState.h"
#include "DebuggerInterface.h"
#include "Register.h"
#include "TeamMemory.h"
#include "Tracing.h"
#include "ValueLocation.h"


/**
 * @brief Constructs a writer bound to a target architecture and debugger interface.
 *
 * Acquires references on all collaborators so the writer is safe to use even
 * if the caller drops its own references.
 *
 * @param architecture   Architecture providing endianness and registers.
 * @param interface      Debugger interface used to write memory and push CPU state.
 * @param cpuState       Optional CPU state for register pieces; may be NULL.
 * @param targetThread   Thread whose CPU state should be updated.
 */
ValueWriter::ValueWriter(Architecture* architecture,
	DebuggerInterface* interface, CpuState* cpuState, thread_id targetThread)
	:
	fArchitecture(architecture),
	fDebuggerInterface(interface),
	fCpuState(cpuState),
	fTargetThread(targetThread)
{
	fArchitecture->AcquireReference();
	fDebuggerInterface->AcquireReference();
	if (fCpuState != NULL)
		fCpuState->AcquireReference();
}


/**
 * @brief Releases the references held on the architecture, debugger interface,
 *        and CPU state.
 */
ValueWriter::~ValueWriter()
{
	fArchitecture->ReleaseReference();
	fDebuggerInterface->ReleaseReference();
	if (fCpuState != NULL)
		fCpuState->ReleaseReference();
}


/**
 * @brief Writes a BVariant back into the target through a piece-wise location.
 *
 * Memory pieces are pushed via DebuggerInterface::WriteMemory(); register
 * pieces are folded into the cached CpuState which is then committed once at
 * the end via DebuggerInterface::SetCpuState().
 *
 * @param location  Destination piece list -- must be writable.
 * @param value     Value to commit; must hold at least the bytes implied by @a location.
 * @retval B_OK            On success.
 * @retval B_BAD_VALUE     When @a location is not writable.
 * @retval B_BAD_ADDRESS   When a memory write was short.
 * @retval B_NO_MEMORY     When updating a register failed.
 * @retval B_UNSUPPORTED   When a register piece is requested but no CpuState
 *                         was provided, or a piece size cannot be packed into
 *                         a single register write.
 */
status_t
ValueWriter::WriteValue(ValueLocation* location, BVariant& value)
{
	if (!location->IsWritable())
		return B_BAD_VALUE;

	int32 count = location->CountPieces();
	if (fCpuState == NULL) {
		for (int32 i = 0; i < count; i++) {
			const ValuePieceLocation piece = location->PieceAt(i);
			if (piece.type == VALUE_PIECE_LOCATION_REGISTER) {
				TRACE_LOCALS("  -> asked to write value with register piece, "
					"but no CPU state to write to.\n");
				return B_UNSUPPORTED;
			}
		}
	}

	bool cpuStateWriteNeeded = false;
	size_t byteOffset = 0;
	bool bigEndian = fArchitecture->IsBigEndian();
	const Register* registers = fArchitecture->Registers();
	for (int32 i = 0; i < count; i++) {
		ValuePieceLocation piece = location->PieceAt(
			bigEndian ? i : count - i - 1);
		uint32 bytesToWrite = piece.size;

		uint8* targetData = (uint8*)value.Bytes() + byteOffset;

		switch (piece.type) {
			case VALUE_PIECE_LOCATION_MEMORY:
			{
				target_addr_t address = piece.address;

				TRACE_LOCALS("  piece %" B_PRId32 ": memory address: %#"
					B_PRIx64 ", bits: %" B_PRIu32 "\n", i, address,
					bytesToWrite * 8);

				ssize_t bytesWritten = fDebuggerInterface->WriteMemory(address,
					targetData, bytesToWrite);

				if (bytesWritten < 0)
					return bytesWritten;
				if ((uint32)bytesWritten != bytesToWrite)
					return B_BAD_ADDRESS;

				break;
			}
			case VALUE_PIECE_LOCATION_REGISTER:
			{
				TRACE_LOCALS("  piece %" B_PRId32 ": register: %" B_PRIu32
					", bits: %" B_PRIu64 "\n", i, piece.reg, piece.bitSize);

				const Register* target = registers + piece.reg;
				BVariant pieceValue;
				switch (bytesToWrite) {
					case 1:
						pieceValue.SetTo(*(uint8*)targetData);
						break;
					case 2:
						pieceValue.SetTo(*(uint16*)targetData);
						break;
					case 4:
						pieceValue.SetTo(*(uint32*)targetData);
						break;
					case 8:
						pieceValue.SetTo(*(uint64*)targetData);
						break;
					default:
						TRACE_LOCALS("Asked to write unsupported piece size %"
							B_PRId32 " to register\n", bytesToWrite);
						return B_UNSUPPORTED;
				}

				if (!fCpuState->SetRegisterValue(target, pieceValue))
					return B_NO_MEMORY;

				cpuStateWriteNeeded = true;
				break;
			}
			default:
				return B_UNSUPPORTED;
		}

		byteOffset += bytesToWrite;
	}

	if (cpuStateWriteNeeded)
		return fDebuggerInterface->SetCpuState(fTargetThread, fCpuState);

	return B_OK;
}
