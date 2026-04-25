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
 *   Copyright 2013, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DwarfExpressionEvaluator.cpp
 * @brief Stack-machine interpreter for DWARF DW_OP_* expressions.
 *
 * DWARF location and value expressions are encoded as a small bytecode
 * executed against a target_addr_t-sized stack.  This evaluator reads
 * the byte stream, dispatches each opcode to a switch arm, and feeds
 * memory or register reads through a pluggable DwarfTargetInterface.
 * Two top-level entry points cover the two ways DWARF uses these
 * expressions: @ref Evaluate produces a final value and
 * @ref EvaluateLocation produces a ValueLocation that may name registers
 * or composite "piece" descriptors.
 */


#include "DwarfExpressionEvaluator.h"

#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <new>

#include <Variant.h>

#include "DataReader.h"
#include "Dwarf.h"
#include "DwarfTargetInterface.h"
#include "Tracing.h"
#include "ValueLocation.h"


/** @brief Number of stack slots added each time the value stack grows. */
// number of elements to increase the stack capacity when the stack is full
static const size_t kStackCapacityIncrement = 64;

/** @brief Hard upper bound on the value-stack depth (defends against runaway expressions). */
// maximum number of elements we allow to be pushed on the stack
static const size_t kMaxStackCapacity			= 1024;

/** @brief Maximum number of opcodes interpreted before bailing out (anti-loop). */
// maximum number of operations we allow to be performed for a single expression
// (to avoid running infinite loops forever)
static const uint32 kMaxOperationCount			= 10000;


// #pragma mark - DwarfExpressionEvaluationContext


/**
 * @brief Constructs the evaluation context with target metadata.
 *
 * @param targetInterface  Provider of register and memory reads.
 * @param addressSize      Width of a target address in bytes.
 * @param isBigEndian      @c true for big-endian targets.
 * @param relocationDelta  Adjustment added to DW_OP_addr operands.
 */
DwarfExpressionEvaluationContext::DwarfExpressionEvaluationContext(
	const DwarfTargetInterface* targetInterface, uint8 addressSize,
	bool isBigEndian, target_addr_t relocationDelta)
	:
	fTargetInterface(targetInterface),
	fAddressSize(addressSize),
	fIsBigEndian(isBigEndian),
	fRelocationDelta(relocationDelta)
{
}


/**
 * @brief Destroys the context.  Anchors the vtable.
 */
DwarfExpressionEvaluationContext::~DwarfExpressionEvaluationContext()
{
}


// #pragma mark - EvaluationException


/**
 * @brief Thrown internally to abort an in-progress expression evaluation.
 *
 * Caught by the public entry points and translated into a @c status_t.
 */
struct DwarfExpressionEvaluator::EvaluationException {
	const char* message;

	EvaluationException(const char* message)
		:
		message(message)
	{
	}
};


// #pragma mark - DwarfExpressionEvaluator


/**
 * @brief Throws if the stack contains fewer than @a size elements.
 *
 * @param size Minimum stack depth required by the next opcode.
 */
void
DwarfExpressionEvaluator::_AssertMinStackSize(size_t size) const
{
	if (fStackSize < size)
		throw EvaluationException("pop from empty stack");
}


/**
 * @brief Pushes @a value onto the stack, growing it on demand.
 *
 * Throws @c EvaluationException on overflow and @c std::bad_alloc on
 * allocation failure.
 *
 * @param value Word to push.
 */
void
DwarfExpressionEvaluator::_Push(target_addr_t value)
{
	// resize the stack, if we hit the capacity
	if (fStackSize == fStackCapacity) {
		if (fStackCapacity >= kMaxStackCapacity)
			throw EvaluationException("stack overflow");

		size_t newCapacity = fStackCapacity + kStackCapacityIncrement;
		target_addr_t* newStack = (target_addr_t*)realloc(fStack,
			newCapacity * sizeof(target_addr_t));
		if (newStack == NULL)
			throw std::bad_alloc();

		fStack = newStack;
		fStackCapacity = newCapacity;
	}

	fStack[fStackSize++] = value;
}


/**
 * @brief Pops and returns the top of the stack.
 *
 * @return Top stack value.
 * @note Throws @c EvaluationException on underflow.
 */
target_addr_t
DwarfExpressionEvaluator::_Pop()
{
	_AssertMinStackSize(1);
	return fStack[--fStackSize];
}


/**
 * @brief Constructs the evaluator bound to a given context.
 *
 * @param context Provider of target memory, registers, frame info, etc.
 */
DwarfExpressionEvaluator::DwarfExpressionEvaluator(
	DwarfExpressionEvaluationContext* context)
	:
	fContext(context),
	fStack(NULL),
	fStackSize(0),
	fStackCapacity(0)
{
}


/**
 * @brief Destroys the evaluator and releases the value stack.
 */
DwarfExpressionEvaluator::~DwarfExpressionEvaluator()
{
	free(fStack);
}


/**
 * @brief Pushes a value onto the stack from outside the evaluator.
 *
 * Used by callers that want to seed the stack (e.g. with a known frame
 * address) before calling @ref Evaluate.
 *
 * @param value Value to push.
 * @retval B_OK         Pushed successfully.
 * @retval B_BAD_VALUE  Stack overflow.
 * @retval B_NO_MEMORY  Allocation failure.
 */
status_t
DwarfExpressionEvaluator::Push(target_addr_t value)
{
	try {
		_Push(value);
		return B_OK;
	} catch (const EvaluationException& exception) {
		return B_BAD_VALUE;
	} catch (const std::bad_alloc& exception) {
		return B_NO_MEMORY;
	}
}


/**
 * @brief Evaluates a value-producing DWARF expression.
 *
 * Runs the bytecode interpreter and returns the final top-of-stack value.
 *
 * @param expression Pointer to the expression byte stream.
 * @param size       Length of the expression in bytes.
 * @param _result    Output value left on the stack at end of execution.
 * @retval B_OK         Expression produced a value.
 * @retval B_BAD_VALUE  Malformed bytecode or runtime exception.
 * @retval B_NO_MEMORY  Allocation failure during stack growth.
 */
status_t
DwarfExpressionEvaluator::Evaluate(const void* expression, size_t size,
	target_addr_t& _result)
{
	fDataReader.SetTo(expression, size, fContext->AddressSize(), fContext->IsBigEndian());

	try {
		status_t error = _Evaluate(NULL);
		if (error != B_OK)
			return error;
		_result = _Pop();
		return B_OK;
	} catch (const EvaluationException& exception) {
		WARNING("DwarfExpressionEvaluator::Evaluate(): %s\n",
			exception.message);
		return B_BAD_VALUE;
	} catch (const std::bad_alloc& exception) {
		return B_NO_MEMORY;
	}
}


/**
 * @brief Evaluates a location-producing DWARF expression.
 *
 * Recognises composite-location operators (DW_OP_piece, DW_OP_bit_piece)
 * and packages each piece into the supplied ValueLocation, preserving
 * register references so callers can read register-resident values.
 *
 * @param expression Pointer to the expression byte stream.
 * @param size       Length of the expression in bytes (zero is valid).
 * @param _location  Output location populated with one or more pieces.
 * @retval B_OK         Location decoded successfully.
 * @retval B_BAD_DATA   Malformed composite-location stream.
 * @retval B_BAD_VALUE  Runtime evaluation exception.
 * @retval B_NO_MEMORY  Allocation failure adding a piece.
 */
status_t
DwarfExpressionEvaluator::EvaluateLocation(const void* expression, size_t size,
	ValueLocation& _location)
{
	_location.Clear();

	// the empty expression is a valid one
	if (size == 0) {
		ValuePieceLocation piece;
		piece.SetToUnknown();
		piece.SetSize(0);
		return _location.AddPiece(piece) ? B_OK : B_NO_MEMORY;
	}

	fDataReader.SetTo(expression, size, fContext->AddressSize(), fContext->IsBigEndian());

	// parse the first (and maybe only) expression
	try {
		// push the object address, if any
		target_addr_t objectAddress;
		if (fContext->GetObjectAddress(objectAddress))
			_Push(objectAddress);

		ValuePieceLocation piece;
		status_t error = _Evaluate(&piece);
		if (error != B_OK)
			return error;

		// if that's all, it's only a simple expression without composition
		if (fDataReader.BytesRemaining() == 0) {
			if (!piece.IsValid())
				piece.SetToMemory(_Pop());
			piece.SetSize(0);
			return _location.AddPiece(piece) ? B_OK : B_NO_MEMORY;
		}

		// there's more, so it must be a composition operator
		uint8 opcode = fDataReader.Read<uint8>(0);
		if (opcode == DW_OP_piece) {
			piece.SetSize(fDataReader.ReadUnsignedLEB128(0));
		} else if (opcode == DW_OP_bit_piece) {
			uint64 bitSize = fDataReader.ReadUnsignedLEB128(0);
			piece.SetSize(bitSize, fDataReader.ReadUnsignedLEB128(0));
		} else
			return B_BAD_DATA;

		// If there's a composition operator, there must be at least two
		// simple expressions, so this must not be the end.
		if (fDataReader.BytesRemaining() == 0)
			return B_BAD_DATA;
	} catch (const EvaluationException& exception) {
		WARNING("DwarfExpressionEvaluator::EvaluateLocation(): %s\n",
			exception.message);
		return B_BAD_VALUE;
	} catch (const std::bad_alloc& exception) {
		return B_NO_MEMORY;
	}

	// parse subsequent expressions (at least one)
	while (fDataReader.BytesRemaining() > 0) {
		// Restrict the data reader to the remaining bytes to prevent jumping
		// back.
		fDataReader.SetTo(fDataReader.Data(), fDataReader.BytesRemaining(),
			fDataReader.AddressSize(), fDataReader.IsBigEndian());

		try {
			// push the object address, if any
			target_addr_t objectAddress;
			if (fContext->GetObjectAddress(objectAddress))
				_Push(objectAddress);

			ValuePieceLocation piece;
			status_t error = _Evaluate(&piece);
			if (error != B_OK)
				return error;

			if (!piece.IsValid())
				piece.SetToMemory(_Pop());

			// each expression must be followed by a composition operator
			if (fDataReader.BytesRemaining() == 0)
				return B_BAD_DATA;

			uint8 opcode = fDataReader.Read<uint8>(0);
			if (opcode == DW_OP_piece) {
				piece.SetSize(fDataReader.ReadUnsignedLEB128(0));
			} else if (opcode == DW_OP_bit_piece) {
				uint64 bitSize = fDataReader.ReadUnsignedLEB128(0);
				piece.SetSize(bitSize, fDataReader.ReadUnsignedLEB128(0));
			} else
				return B_BAD_DATA;
		} catch (const EvaluationException& exception) {
			WARNING("DwarfExpressionEvaluator::EvaluateLocation(): %s\n",
				exception.message);
			return B_BAD_VALUE;
		} catch (const std::bad_alloc& exception) {
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


/**
 * @brief Inner expression interpreter loop.
 *
 * Drains the DataReader one opcode at a time, dispatching each DW_OP_*
 * to its handler.  Many handlers manipulate the value stack directly
 * (constants, arithmetic, comparisons, dup/swap/...) while a few
 * delegate to helpers (memory dereference, register read, expression
 * call).  Decoding stops at end of stream, an error opcode, or one of
 * the location-producing terminators (DW_OP_reg*, DW_OP_implicit_*).
 *
 * @param _piece  Optional output piece descriptor populated when a
 *                location-producing opcode is encountered.  May be NULL
 *                for value-producing evaluation.
 * @retval B_OK        Interpreter halted normally.
 * @retval B_BAD_DATA  Bytecode contained an unknown opcode or overflow.
 * @retval B_NO_MEMORY Stack growth failed.
 */
status_t
DwarfExpressionEvaluator::_Evaluate(ValuePieceLocation* _piece)
{
	TRACE_EXPR_ONLY({
		TRACE_EXPR("DwarfExpressionEvaluator::_Evaluate(%p, %" B_PRIdOFF ")\n",
			fDataReader.Data(), fDataReader.BytesRemaining());
		const uint8* data = (const uint8*)fDataReader.Data();
		int32 count = fDataReader.BytesRemaining();
		for (int32 i = 0; i < count; i++)
			TRACE_EXPR(" %02x", data[i]);
		TRACE_EXPR("\n");
	})

	uint32 operationsExecuted = 0;

	while (fDataReader.BytesRemaining() > 0) {
		uint8 opcode = fDataReader.Read<uint8>(0);

		switch (opcode) {
			case DW_OP_addr:
				TRACE_EXPR("  DW_OP_addr\n");
				_Push(fDataReader.ReadAddress(0) + fContext->RelocationDelta());
				break;
			case DW_OP_const1u:
				TRACE_EXPR("  DW_OP_const1u\n");
				_Push(fDataReader.Read<uint8>(0));
				break;
			case DW_OP_const1s:
				TRACE_EXPR("  DW_OP_const1s\n");
				_Push(fDataReader.Read<int8>(0));
				break;
			case DW_OP_const2u:
				TRACE_EXPR("  DW_OP_const2u\n");
				_Push(fDataReader.Read<uint16>(0));
				break;
			case DW_OP_const2s:
				TRACE_EXPR("  DW_OP_const2s\n");
				_Push(fDataReader.Read<int16>(0));
				break;
			case DW_OP_const4u:
				TRACE_EXPR("  DW_OP_const4u\n");
				_Push(fDataReader.Read<uint32>(0));
				break;
			case DW_OP_const4s:
				TRACE_EXPR("  DW_OP_const4s\n");
				_Push(fDataReader.Read<int32>(0));
				break;
			case DW_OP_const8u:
				TRACE_EXPR("  DW_OP_const8u\n");
				_Push(fDataReader.Read<uint64>(0));
				break;
			case DW_OP_const8s:
				TRACE_EXPR("  DW_OP_const8s\n");
				_Push(fDataReader.Read<int64>(0));
				break;
			case DW_OP_constu:
				TRACE_EXPR("  DW_OP_constu\n");
				_Push(fDataReader.ReadUnsignedLEB128(0));
				break;
			case DW_OP_consts:
				TRACE_EXPR("  DW_OP_consts\n");
				_Push(fDataReader.ReadSignedLEB128(0));
				break;
			case DW_OP_dup:
				TRACE_EXPR("  DW_OP_dup\n");
				_AssertMinStackSize(1);
				_Push(fStack[fStackSize - 1]);
				break;
			case DW_OP_drop:
				TRACE_EXPR("  DW_OP_drop\n");
				_Pop();
				break;
			case DW_OP_over:
				TRACE_EXPR("  DW_OP_over\n");
				_AssertMinStackSize(1);
				_Push(fStack[fStackSize - 2]);
				break;
			case DW_OP_pick:
			{
				TRACE_EXPR("  DW_OP_pick\n");
				uint8 index = fDataReader.Read<uint8>(0);
				_AssertMinStackSize(index + 1);
				_Push(fStack[fStackSize - index - 1]);
				break;
			}
			case DW_OP_swap:
			{
				TRACE_EXPR("  DW_OP_swap\n");
				_AssertMinStackSize(2);
				std::swap(fStack[fStackSize - 1], fStack[fStackSize - 2]);
				break;
			}
			case DW_OP_rot:
			{
				TRACE_EXPR("  DW_OP_rot\n");
				_AssertMinStackSize(3);
				target_addr_t tmp = fStack[fStackSize - 1];
				fStack[fStackSize - 1] = fStack[fStackSize - 2];
				fStack[fStackSize - 2] = fStack[fStackSize - 3];
				fStack[fStackSize - 3] = tmp;
				break;
			}

			case DW_OP_deref:
				TRACE_EXPR("  DW_OP_deref\n");
				_DereferenceAddress(fContext->AddressSize());
				break;
			case DW_OP_deref_size:
				TRACE_EXPR("  DW_OP_deref_size\n");
				_DereferenceAddress(fDataReader.Read<uint8>(0));
				break;
			case DW_OP_xderef:
				TRACE_EXPR("  DW_OP_xderef\n");
				_DereferenceAddressSpaceAddress(fContext->AddressSize());
				break;
			case DW_OP_xderef_size:
				TRACE_EXPR("  DW_OP_xderef_size\n");
				_DereferenceAddressSpaceAddress(fDataReader.Read<uint8>(0));
				break;

			case DW_OP_abs:
			{
				TRACE_EXPR("  DW_OP_abs\n");
				target_addr_t value = _Pop();
				if (fContext->AddressSize() == 4) {
					int32 signedValue = (int32)value;
					_Push(signedValue >= 0 ? signedValue : -signedValue);
				} else {
					int64 signedValue = (int64)value;
					_Push(signedValue >= 0 ? signedValue : -signedValue);
				}
				break;
			}
			case DW_OP_and:
				TRACE_EXPR("  DW_OP_and\n");
				_Push(_Pop() & _Pop());
				break;
			case DW_OP_div:
			{
				TRACE_EXPR("  DW_OP_div\n");
				int64 top = (int64)_Pop();
				int64 second = (int64)_Pop();
				_Push(top != 0 ? second / top : 0);
				break;
			}
			case DW_OP_minus:
			{
				TRACE_EXPR("  DW_OP_minus\n");
				target_addr_t top = _Pop();
				_Push(_Pop() - top);
				break;
			}
			case DW_OP_mod:
			{
				TRACE_EXPR("  DW_OP_mod\n");
				// While the specs explicitly speak of signed integer division
				// for "div", nothing is mentioned for "mod".
				target_addr_t top = _Pop();
				target_addr_t second = _Pop();
				_Push(top != 0 ? second % top : 0);
				break;
			}
			case DW_OP_mul:
				TRACE_EXPR("  DW_OP_mul\n");
				_Push(_Pop() * _Pop());
				break;
			case DW_OP_neg:
			{
				TRACE_EXPR("  DW_OP_neg\n");
				if (fContext->AddressSize() == 4)
					_Push(-(int32)_Pop());
				else
					_Push(-(int64)_Pop());
				break;
			}
			case DW_OP_not:
				TRACE_EXPR("  DW_OP_not\n");
				_Push(~_Pop());
				break;
			case DW_OP_or:
				TRACE_EXPR("  DW_OP_or\n");
				_Push(_Pop() | _Pop());
				break;
			case DW_OP_plus:
				TRACE_EXPR("  DW_OP_plus\n");
				_Push(_Pop() + _Pop());
				break;
			case DW_OP_plus_uconst:
				TRACE_EXPR("  DW_OP_plus_uconst\n");
				_Push(_Pop() + fDataReader.ReadUnsignedLEB128(0));
				break;
			case DW_OP_shl:
			{
				TRACE_EXPR("  DW_OP_shl\n");
				target_addr_t top = _Pop();
				_Push(_Pop() << top);
				break;
			}
			case DW_OP_shr:
			{
				TRACE_EXPR("  DW_OP_shr\n");
				target_addr_t top = _Pop();
				_Push(_Pop() >> top);
				break;
			}
			case DW_OP_shra:
			{
				TRACE_EXPR("  DW_OP_shra\n");
				target_addr_t top = _Pop();
				int64 second = (int64)_Pop();
				_Push(second >= 0 ? second >> top : -(-second >> top));
					// right shift on negative values is implementation defined
				break;
			}
			case DW_OP_xor:
				TRACE_EXPR("  DW_OP_xor\n");
				_Push(_Pop() ^ _Pop());
				break;

			case DW_OP_bra:
				TRACE_EXPR("  DW_OP_bra\n");
				if (_Pop() == 0)
					break;
				// fall through
			case DW_OP_skip:
			{
				TRACE_EXPR("  DW_OP_skip\n");
				int16 offset = fDataReader.Read<int16>(0);
				if (offset >= 0 ? offset > fDataReader.BytesRemaining()
						: -offset > fDataReader.Offset()) {
					throw EvaluationException("bra/skip: invalid offset");
				}
				fDataReader.SeekAbsolute(fDataReader.Offset() + offset);
				break;
			}

			case DW_OP_eq:
				TRACE_EXPR("  DW_OP_eq\n");
				_Push(_Pop() == _Pop() ? 1 : 0);
				break;
			case DW_OP_ge:
			{
				TRACE_EXPR("  DW_OP_ge\n");
				int64 top = (int64)_Pop();
				_Push((int64)_Pop() >= top ? 1 : 0);
				break;
			}
			case DW_OP_gt:
			{
				TRACE_EXPR("  DW_OP_gt\n");
				int64 top = (int64)_Pop();
				_Push((int64)_Pop() > top ? 1 : 0);
				break;
			}
			case DW_OP_le:
			{
				TRACE_EXPR("  DW_OP_le\n");
				int64 top = (int64)_Pop();
				_Push((int64)_Pop() <= top ? 1 : 0);
				break;
			}
			case DW_OP_lt:
			{
				TRACE_EXPR("  DW_OP_lt\n");
				int64 top = (int64)_Pop();
				_Push((int64)_Pop() < top ? 1 : 0);
				break;
			}
			case DW_OP_ne:
				TRACE_EXPR("  DW_OP_ne\n");
				_Push(_Pop() == _Pop() ? 1 : 0);
				break;

			case DW_OP_push_object_address:
			{
				TRACE_EXPR("  DW_OP_push_object_address\n");
				target_addr_t address;
				if (!fContext->GetObjectAddress(address))
					throw EvaluationException("failed to get object address");
				_Push(address);
				break;
			}
			case DW_OP_call_frame_cfa:
			{
				TRACE_EXPR("  DW_OP_call_frame_cfa\n");
				target_addr_t address;
				if (!fContext->GetFrameAddress(address))
					throw EvaluationException("failed to get frame address");
				_Push(address);
				break;
			}
			case DW_OP_fbreg:
			{
				int64 offset = fDataReader.ReadSignedLEB128(0);
				TRACE_EXPR("  DW_OP_fbreg(%" B_PRId64 ")\n", offset);
				target_addr_t address;
				if (!fContext->GetFrameBaseAddress(address)) {
					throw EvaluationException(
						"failed to get frame base address");
				}
				_Push(address + offset);
				break;
			}
			case DW_OP_form_tls_address:
			{
				TRACE_EXPR("  DW_OP_form_tls_address\n");
				target_addr_t address;
				if (!fContext->GetTLSAddress(_Pop(), address))
					throw EvaluationException("failed to get tls address");
				_Push(address);
				break;
			}

			case DW_OP_regx:
			{
				TRACE_EXPR("  DW_OP_regx\n");
				if (_piece == NULL) {
					throw EvaluationException(
						"DW_OP_regx in non-location expression");
				}
				uint32 reg = fDataReader.ReadUnsignedLEB128(0);
				if (fDataReader.HasOverflow())
					throw EvaluationException("unexpected end of expression");
				_piece->SetToRegister(reg);
				return B_OK;
			}

			case DW_OP_bregx:
			{
				TRACE_EXPR("  DW_OP_bregx\n");
				uint32 reg = fDataReader.ReadUnsignedLEB128(0);
				_PushRegister(reg, fDataReader.ReadSignedLEB128(0));
				break;
			}

			case DW_OP_call2:
				TRACE_EXPR("  DW_OP_call2\n");
				_Call(fDataReader.Read<uint16>(0), dwarf_reference_type_local);
				break;
			case DW_OP_call4:
				TRACE_EXPR("  DW_OP_call4\n");
				_Call(fDataReader.Read<uint32>(0), dwarf_reference_type_local);
				break;
			case DW_OP_call_ref:
				TRACE_EXPR("  DW_OP_call_ref\n");
				if (fContext->AddressSize() == 4) {
					_Call(fDataReader.Read<uint32>(0),
						dwarf_reference_type_global);
				} else {
					_Call(fDataReader.Read<uint64>(0),
						dwarf_reference_type_global);
				}
				break;

			case DW_OP_piece:
			case DW_OP_bit_piece:
				// are handled in EvaluateLocation()
				if (_piece == NULL)
					return B_BAD_DATA;

				fDataReader.SeekAbsolute(fDataReader.Offset() - 1);
					// put back the operation
				return B_OK;

			case DW_OP_nop:
				TRACE_EXPR("  DW_OP_nop\n");
				break;

			case DW_OP_implicit_value:
			{
				TRACE_EXPR("  DW_OP_implicit_value\n");
				if (_piece == NULL) {
					throw EvaluationException(
						"DW_OP_implicit_value in non-location expression");
				}
				uint32 length = fDataReader.ReadUnsignedLEB128(0);
				if (length == 0)
					return B_BAD_DATA;

				if (fDataReader.BytesRemaining() < length)
					return B_BAD_DATA;

				if (!_piece->SetToValue(fDataReader.Data(), length))
					return B_NO_MEMORY;

				return B_OK;
			}
			case DW_OP_stack_value:
			{
				TRACE_EXPR("  DW_OP_stack_value\n");
				if (_piece == NULL) {
					throw EvaluationException(
						"DW_OP_stack_value in non-location expression");
				}
				if (fStackSize == 0)
					return B_BAD_DATA;
				target_addr_t value = _Pop();
				if (!_piece->SetToValue(&value, sizeof(target_addr_t)))
					return B_NO_MEMORY;

				return B_OK;
			}
			default:
				if (opcode >= DW_OP_lit0 && opcode <= DW_OP_lit31) {
					TRACE_EXPR("  DW_OP_lit%u\n", opcode - DW_OP_lit0);
					_Push(opcode - DW_OP_lit0);
				} else if (opcode >= DW_OP_reg0 && opcode <= DW_OP_reg31) {
					TRACE_EXPR("  DW_OP_reg%u\n", opcode - DW_OP_reg0);
					if (_piece == NULL) {
						// NOTE: Using these opcodes is actually only allowed in
						// location expression, but gcc 2.95.3 does otherwise.
						_PushRegister(opcode - DW_OP_reg0, 0);
					} else {
						_piece->SetToRegister(opcode - DW_OP_reg0);
						return B_OK;
					}
				} else if (opcode >= DW_OP_breg0 && opcode <= DW_OP_breg31) {
					int64 offset = fDataReader.ReadSignedLEB128(0);
					TRACE_EXPR("  DW_OP_breg%u(%" B_PRId64 ")\n",
						opcode - DW_OP_breg0, offset);
					_PushRegister(opcode - DW_OP_breg0, offset);
				} else {
					WARNING("DwarfExpressionEvaluator::_Evaluate(): "
						"unsupported opcode: %u\n", opcode);
					return B_BAD_DATA;
				}
				break;
		}

		if (++operationsExecuted >= kMaxOperationCount)
			return B_BAD_DATA;
	}

	return fDataReader.HasOverflow() ? B_BAD_DATA : B_OK;
}


/**
 * @brief Reads an integer of @a addressSize bytes from the popped address.
 *
 * Implements DW_OP_deref / DW_OP_deref_size by mapping the requested
 * width to a BVariant value type, asking the target interface to read
 * the value, and pushing the result.
 *
 * @param addressSize Width of the value to read (1, 2, 4, or 8 bytes).
 */
void
DwarfExpressionEvaluator::_DereferenceAddress(uint8 addressSize)
{
	uint32 valueType;
	switch (addressSize) {
		case 1:
			valueType = B_UINT8_TYPE;
			break;
		case 2:
			valueType = B_UINT16_TYPE;
			break;
		case 4:
			valueType = B_UINT32_TYPE;
			break;
		case 8:
			if (fContext->AddressSize() == 8) {
				valueType = B_UINT64_TYPE;
				break;
			}
			// fall through
		default:
			throw EvaluationException("invalid dereference size");
	}

	target_addr_t address = _Pop();
	BVariant value;
	if (!fContext->TargetInterface()->ReadValueFromMemory(address, valueType,
			value)) {
		throw EvaluationException("failed to read memory");
	}

	_Push(value.ToUInt64());
}


/**
 * @brief Reads from an address in a specific address space.
 *
 * Implements DW_OP_xderef / DW_OP_xderef_size by popping (address space,
 * address) and asking the target interface to perform a read.
 *
 * @param addressSize Width of the value to read in bytes.
 */
void
DwarfExpressionEvaluator::_DereferenceAddressSpaceAddress(uint8 addressSize)
{
	uint32 valueType;
	switch (addressSize) {
		case 1:
			valueType = B_UINT8_TYPE;
			break;
		case 2:
			valueType = B_UINT16_TYPE;
			break;
		case 4:
			valueType = B_UINT32_TYPE;
			break;
		case 8:
			if (fContext->AddressSize() == 8) {
				valueType = B_UINT64_TYPE;
				break;
			}
			// fall through
		default:
			throw EvaluationException("invalid dereference size");
	}

	target_addr_t address = _Pop();
	target_addr_t addressSpace = _Pop();
	BVariant value;
	if (!fContext->TargetInterface()->ReadValueFromMemory(addressSpace, address,
			valueType, value)) {
		throw EvaluationException("failed to read memory");
	}

	_Push(value.ToUInt64());
}


/**
 * @brief Reads register @a reg, adds @a offset, and pushes the result.
 *
 * Implements the DW_OP_breg* family.
 *
 * @param reg    Architectural register number to read.
 * @param offset Signed offset added to the register value.
 */
void
DwarfExpressionEvaluator::_PushRegister(uint32 reg, target_addr_t offset)
{
	BVariant value;
	if (!fContext->TargetInterface()->GetRegisterValue(reg, value))
		throw EvaluationException("failed to get register");

	_Push(value.ToUInt64() + offset);
}


/**
 * @brief Implements the DW_OP_call* family by recursively evaluating a target.
 *
 * Asks the context to resolve the call target (a referenced DIE's
 * location attribute), saves and restores the data-reader state across
 * the recursive call, and shares the value stack with the caller.
 *
 * @param offset   Section-relative offset of the called DIE.
 * @param refType  Reference form indicator (DW_OP_call2/4/ref).
 */
void
DwarfExpressionEvaluator::_Call(uint64 offset, uint8 refType)
{
	if (fDataReader.HasOverflow())
		throw EvaluationException("unexpected end of expression");

	// get the expression to "call"
	const void* block;
	off_t size;
	if (fContext->GetCallTarget(offset, refType, block, size) != B_OK)
		throw EvaluationException("failed to get call target");

	// no expression is OK, then this is just a no-op
	if (block == NULL)
		return;

	// save the current data reader state
	DataReader savedReader = fDataReader;

	// set the reader to the target expression
	fDataReader.SetTo(block, size, savedReader.AddressSize(), savedReader.IsBigEndian());

	// and evaluate it
	try {
		if (_Evaluate(NULL) != B_OK)
			throw EvaluationException("call failed");
	} catch (...) {
		fDataReader = savedReader;
		throw;
	}

	fDataReader = savedReader;
}
