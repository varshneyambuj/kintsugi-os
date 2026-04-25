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
 * @file ValueLoader.cpp
 * @brief Implementation of ValueLoader, the bridge that materialises a ValueLocation into bytes.
 *
 * Given a ValueLocation (a list of memory and/or register pieces produced by
 * the DWARF location expression evaluator), the loader fetches the bytes from
 * the live target via TeamMemory plus the cached CpuState, normalises endian,
 * and packs them into a BVariant typed by @c type_code.
 *
 * @see ValueWriter, Architecture, TeamMemory
 */


#include "ValueLoader.h"

#include "Architecture.h"
#include "BitBuffer.h"
#include "CpuState.h"
#include "Register.h"
#include "TeamMemory.h"
#include "Tracing.h"
#include "ValueLocation.h"


/**
 * @brief Constructs a loader bound to a target architecture and memory view.
 *
 * Acquires references on all collaborators so the loader is safe to use even
 * if the caller drops its own references.
 *
 * @param architecture  Architecture providing endianness, address size, and registers.
 * @param teamMemory    Live target memory accessor.
 * @param cpuState      Optional CPU state for resolving register pieces; may be NULL.
 */
ValueLoader::ValueLoader(Architecture* architecture, TeamMemory* teamMemory,
	CpuState* cpuState)
	:
	fArchitecture(architecture),
	fTeamMemory(teamMemory),
	fCpuState(cpuState)
{
	fArchitecture->AcquireReference();
	fTeamMemory->AcquireReference();
	if (fCpuState != NULL)
		fCpuState->AcquireReference();
}


/**
 * @brief Releases the references held on the architecture, memory, and CPU state.
 */
ValueLoader::~ValueLoader()
{
	fArchitecture->ReleaseReference();
	fTeamMemory->ReleaseReference();
	if (fCpuState != NULL)
		fCpuState->ReleaseReference();
}


/**
 * @brief Reads a typed value from a list of location pieces.
 *
 * Walks @a location piece by piece, reading memory or registers as required,
 * normalising endianness, and accumulating bits into a BitBuffer that is then
 * decoded into @a _value as @a valueType.
 *
 * @param location          The piece list produced by location-expression evaluation.
 * @param valueType         The B_*_TYPE describing the desired interpretation of the bytes.
 * @param shortValueIsFine  When true, accept fewer bits than @a valueType expects (zero-extending).
 * @param _value            Set to the decoded BVariant on success.
 * @retval B_OK              On successful decode.
 * @retval B_ENTRY_NOT_FOUND When a piece is invalid/unknown or no bits are available.
 * @retval B_UNSUPPORTED     When a piece is too large or the total exceeds 64 bits.
 * @retval B_BAD_VALUE       When the buffer is shorter than @a valueType requires
 *                           and @a shortValueIsFine is false.
 * @retval B_BAD_ADDRESS     When a memory piece could not be fully read.
 * @retval B_NO_MEMORY       When buffer assembly failed.
 * @note  Bit ordering currently follows C/C++ conventions; non-C/C++ source
 *        languages will need additional plumbing.
 */
status_t
ValueLoader::LoadValue(ValueLocation* location, type_code valueType,
	bool shortValueIsFine, BVariant& _value)
{
	static const size_t kMaxPieceSize = 16;
	uint64 totalBitSize = 0;
	int32 count = location->CountPieces();
	for (int32 i = 0; i < count; i++) {
		ValuePieceLocation piece = location->PieceAt(i);
		switch (piece.type) {
			case VALUE_PIECE_LOCATION_INVALID:
			case VALUE_PIECE_LOCATION_UNKNOWN:
				return B_ENTRY_NOT_FOUND;
			case VALUE_PIECE_LOCATION_MEMORY:
			case VALUE_PIECE_LOCATION_REGISTER:
			case VALUE_PIECE_LOCATION_IMPLICIT:
				break;
		}

		if (piece.size > kMaxPieceSize) {
			TRACE_LOCALS("  -> overly long piece size (%" B_PRIu64 " bytes)\n",
				piece.size);
			return B_UNSUPPORTED;
		}

		totalBitSize += piece.bitSize;
	}

	TRACE_LOCALS("  -> totalBitSize: %" B_PRIu64 "\n", totalBitSize);

	if (totalBitSize == 0) {
		TRACE_LOCALS("  -> no size\n");
		return B_ENTRY_NOT_FOUND;
	}

	if (totalBitSize > 64) {
		TRACE_LOCALS("  -> longer than 64 bits: unsupported\n");
		return B_UNSUPPORTED;
	}

	uint64 valueBitSize = BVariant::SizeOfType(valueType) * 8;
	if (!shortValueIsFine && totalBitSize < valueBitSize) {
		TRACE_LOCALS("  -> too short for value type (%" B_PRIu64 " vs. %"
			B_PRIu64 " bits)\n", totalBitSize, valueBitSize);
		return B_BAD_VALUE;
	}

	// Load the data. Since the BitBuffer class we're using only supports big
	// endian bit semantics, we convert all data to big endian before pushing
	// them to the buffer. For later conversion to BVariant we need to make sure
	// the final buffer has the size of the value type, so we pad the most
	// significant bits with zeros.
	BitBuffer valueBuffer;
	if (totalBitSize < valueBitSize)
		valueBuffer.AddZeroBits(valueBitSize - totalBitSize);

	bool bigEndian = fArchitecture->IsBigEndian();
	const Register* registers = fArchitecture->Registers();
	for (int32 i = 0; i < count; i++) {
		ValuePieceLocation piece = location->PieceAt(
			bigEndian ? i : count - i - 1);
		uint32 bytesToRead = piece.size;
		uint32 bitSize = piece.bitSize;
		uint8 bitOffset = piece.bitOffset;
			// TODO: the offset's ordinal position and direction aren't
			// specified by DWARF, and simply follow the target language.
			// To handle non C/C++ languages properly, the corresponding
			// SourceLanguage will need to be passed in and extended to
			// return the relevant information.

		switch (piece.type) {
			case VALUE_PIECE_LOCATION_INVALID:
			case VALUE_PIECE_LOCATION_UNKNOWN:
				return B_ENTRY_NOT_FOUND;
			case VALUE_PIECE_LOCATION_MEMORY:
			case VALUE_PIECE_LOCATION_IMPLICIT:
			{
				target_addr_t address = piece.address;

				if (piece.type == VALUE_PIECE_LOCATION_MEMORY) {
					TRACE_LOCALS("  piece %" B_PRId32 ": memory address: %#"
						B_PRIx64 ", bits: %" B_PRIu32 "\n", i, address,
						bitSize);
				} else {
					TRACE_LOCALS("  piece %" B_PRId32 ": implicit value, "
						"bits: %" B_PRIu32 "\n", i, bitSize);
				}

				uint8 pieceBuffer[kMaxPieceSize];
				ssize_t bytesRead;
				if (piece.type == VALUE_PIECE_LOCATION_MEMORY) {
					bytesRead = fTeamMemory->ReadMemory(address,
						pieceBuffer, bytesToRead);
				} else {
					memcpy(pieceBuffer, piece.value, piece.size);
					bytesRead = piece.size;
				}

				if (bytesRead < 0)
					return bytesRead;
				if ((uint32)bytesRead != bytesToRead)
					return B_BAD_ADDRESS;

				TRACE_LOCALS_ONLY(
					TRACE_LOCALS("  -> read: ");
					for (ssize_t k = 0; k < bytesRead; k++)
						TRACE_LOCALS("%02x", pieceBuffer[k]);
					TRACE_LOCALS("\n");
				)

				// convert to big endian
				if (!bigEndian) {
					for (int32 k = bytesRead / 2 - 1; k >= 0; k--) {
						std::swap(pieceBuffer[k],
							pieceBuffer[bytesRead - k - 1]);
					}
				}

				valueBuffer.AddBits(pieceBuffer, bitSize, bitOffset);
				break;
			}
			case VALUE_PIECE_LOCATION_REGISTER:
			{
				TRACE_LOCALS("  piece %" B_PRId32 ": register: %" B_PRIu32
					", bits: %" B_PRIu32 "\n", i, piece.reg, bitSize);

				if (fCpuState == NULL) {
					WARNING("ValueLoader::LoadValue(): register piece, but no "
						"CpuState\n");
					return B_UNSUPPORTED;
				}

				BVariant registerValue;
				if (!fCpuState->GetRegisterValue(registers + piece.reg,
						registerValue)) {
					return B_ENTRY_NOT_FOUND;
				}
				if (registerValue.Size() < bytesToRead)
					return B_ENTRY_NOT_FOUND;

				if (!bigEndian) {
					registerValue.SwapEndianess();
					bitOffset = registerValue.Size() * 8 - bitOffset - bitSize;
				}
				valueBuffer.AddBits(registerValue.Bytes(), bitSize, bitOffset);
				break;
			}
		}
	}

	// If we don't have enough bits in the buffer apparently adding some failed.
	if (valueBuffer.BitSize() < valueBitSize)
		return B_NO_MEMORY;

	// convert the bits into something we can work with
	BVariant value;
	status_t error = value.SetToTypedData(valueBuffer.Bytes(), valueType);
	if (error != B_OK) {
		TRACE_LOCALS("  -> failed to set typed data: %s\n", strerror(error));
		return error;
	}

	// convert to host endianess
	#if B_HOST_IS_LENDIAN
		value.SwapEndianess();
	#endif

	_value = value;
	return B_OK;
}


/**
 * @brief Reads a raw byte sequence from the target at the address in @a location.
 *
 * Used by clients that need to copy a struct or buffer verbatim, bypassing
 * type-driven decoding.
 *
 * @param location     BVariant whose 64-bit value is the source address.
 * @param bytesToRead  Number of bytes to copy into @a _value.
 * @param _value       Caller-supplied destination buffer of at least @a bytesToRead bytes.
 * @retval B_OK          On a complete read.
 * @retval B_BAD_ADDRESS On a short read.
 * @return Negative status_t propagated from TeamMemory::ReadMemory().
 */
status_t
ValueLoader::LoadRawValue(BVariant& location, size_t bytesToRead, void* _value)
{
	ssize_t bytesRead = fTeamMemory->ReadMemory(location.ToUInt64(),
		_value, bytesToRead);
	if (bytesRead < 0)
		return bytesRead;
	if ((uint32)bytesRead != bytesToRead)
		return B_BAD_ADDRESS;
	return B_OK;
}


/**
 * @brief Reads a NUL-terminated string from the target into @a _value.
 *
 * Caps the read at min(@a maxSize, 255) to avoid runaway scans on bad pointers.
 *
 * @param location  BVariant whose 64-bit value is the source address.
 * @param maxSize   Caller-requested maximum number of bytes to read.
 * @param _value    Receives the resulting string.
 * @return Status from TeamMemory::ReadMemoryString().
 */
status_t
ValueLoader::LoadStringValue(BVariant& location, size_t maxSize, BString& _value)
{
	static const size_t kMaxStringSize = 255;

	return fTeamMemory->ReadMemoryString(location.ToUInt64(),
		std::min(maxSize, kMaxStringSize), _value);
}
