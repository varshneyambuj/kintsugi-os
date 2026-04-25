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
 *   Copyright 2006-2014 Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Rene Gollent <rene@gollent.com>
 *       John Scipione <jscipione@gmail.com>
 *       Ingo Weinhold <bonefish@cs.tu-berlin.de>
 */


/**
 * @file CLanguageExpressionEvaluator.cpp
 * @brief Hand-written recursive-descent evaluator for C/C++ expressions.
 *
 * The evaluator implements a small expression VM operating on three kinds
 * of operands -- primitive values (BVariant), value nodes (live debugged
 * variables), and types (for casts). The grammar is parsed directly into
 * results; there is no intermediate AST. The two parsing entry points are
 * @c _ParseSum() (additive precedence) and @c _ParseProduct() (which in
 * this implementation also folds in multiplicative, bitwise, logical, and
 * comparison operators so they all share precedence above unary).
 *
 * When the user references a variable whose value has not been resolved
 * yet, the evaluator throws @c ValueNeededException; the caller then
 * schedules a ResolveValueNodeValueJob and re-runs the evaluation.
 */


#include "CLanguageExpressionEvaluator.h"

#include <algorithm>

#include "AutoLocker.h"

#include "CLanguageTokenizer.h"
#include "ExpressionInfo.h"
#include "FloatValue.h"
#include "IntegerFormatter.h"
#include "IntegerValue.h"
#include "ObjectID.h"
#include "StackFrame.h"
#include "SyntheticPrimitiveType.h"
#include "TeamTypeInformation.h"
#include "Thread.h"
#include "Type.h"
#include "TypeHandlerRoster.h"
#include "TypeLookupConstraints.h"
#include "Value.h"
#include "ValueLocation.h"
#include "ValueNode.h"
#include "ValueNodeManager.h"
#include "Variable.h"
#include "VariableValueNodeChild.h"


using namespace CLanguage;


/** @brief Discriminator for the three Operand value categories. */
enum operand_kind {
	OPERAND_KIND_UNKNOWN = 0,
	OPERAND_KIND_PRIMITIVE,
	OPERAND_KIND_TYPE,
	OPERAND_KIND_VALUE_NODE
};


/**
 * @brief Returns a human-readable rendering of a token type code.
 *
 * Used to format diagnostics produced by @c _EatToken().
 *
 * @param type  One of the @c TOKEN_* enumerators.
 * @return BString containing the operator text or a fallback "Unknown".
 */
static BString TokenTypeToString(int32 type)
{
	BString token;

	switch (type) {
		case TOKEN_PLUS:
			token = "+";
			break;

		case TOKEN_MINUS:
			token = "-";
			break;

		case TOKEN_STAR:
			token = "*";
			break;

		case TOKEN_SLASH:
			token = "/";
			break;

		case TOKEN_MODULO:
			token = "%";
			break;

		case TOKEN_OPENING_PAREN:
			token = "(";
			break;

		case TOKEN_CLOSING_PAREN:
			token = ")";
			break;

		case TOKEN_LOGICAL_AND:
			token = "&&";
			break;

		case TOKEN_LOGICAL_OR:
			token = "||";
			break;

		case TOKEN_LOGICAL_NOT:
			token = "!";
			break;

		case TOKEN_BITWISE_AND:
			token = "&";
			break;

		case TOKEN_BITWISE_OR:
			token = "|";
			break;

		case TOKEN_BITWISE_NOT:
			token = "~";
			break;

		case TOKEN_BITWISE_XOR:
			token = "^";
			break;

		case TOKEN_EQ:
			token = "==";
			break;

		case TOKEN_NE:
			token = "!=";
			break;

		case TOKEN_GT:
			token = ">";
			break;

		case TOKEN_GE:
			token = ">=";
			break;

		case TOKEN_LT:
			token = "<";
			break;

		case TOKEN_LE:
			token = "<=";
			break;

		case TOKEN_MEMBER_PTR:
			token = "->";
			break;

		default:
			token.SetToFormat("Unknown token type %" B_PRId32, type);
			break;
	}

	return token;
}


// #pragma mark - CLanguageExpressionEvaluator::InternalVariableID


/**
 * @brief ObjectID identifying a synthetic variable created on-the-fly to
 *        hold a primitive operand involved in a typecast.
 *
 * Equality is decided by comparing the wrapped BVariant.
 */
class CLanguageExpressionEvaluator::InternalVariableID : public ObjectID {
public:
	/**
	 * @brief Construct an InternalVariableID wrapping @a value.
	 *
	 * @param value  Primitive value the synthetic variable will represent.
	 */
	InternalVariableID(const BVariant& value)
		:
		fValue(value)
	{
	}

	virtual ~InternalVariableID()
	{
	}

	virtual	bool operator==(const ObjectID& other) const
	{
		const InternalVariableID* otherID
			= dynamic_cast<const InternalVariableID*>(&other);
		if (otherID == NULL)
			return false;

		return fValue == otherID->fValue;
	}

protected:
	virtual	uint32 ComputeHashValue() const
	{
		return *(uint32*)(&fValue);
	}

private:
	BVariant fValue;
};


// #pragma mark - CLanguageExpressionEvaluator::Operand


/**
 * @brief Tagged union representing a value on the evaluator's stack.
 *
 * An Operand holds exactly one of the three @c operand_kind variants:
 *   - @c OPERAND_KIND_PRIMITIVE: a numeric BVariant value.
 *   - @c OPERAND_KIND_VALUE_NODE: a reference-counted ValueNode whose
 *     primitive contents are mirrored into @c fPrimitive on demand.
 *   - @c OPERAND_KIND_TYPE: a Type reference produced by a cast token.
 *
 * Arithmetic/relational/bitwise operators are defined as compound-assign
 * methods that promote both sides to a common numeric type via
 * @c _ResolveTypesIfNeeded() before performing the operation.
 */
class CLanguageExpressionEvaluator::Operand {
public:
	/** @brief Construct an empty (kind unknown) operand. */
	Operand()
		:
		fPrimitive(),
		fValueNode(NULL),
		fType(NULL),
		fKind(OPERAND_KIND_UNKNOWN)
	{
	}

	/** @brief Construct a primitive operand from an int64. */
	Operand(int64 value)
		:
		fPrimitive(value),
		fValueNode(NULL),
		fType(NULL),
		fKind(OPERAND_KIND_PRIMITIVE)
	{
	}

	/** @brief Construct a primitive operand from a double. */
	Operand(double value)
		:
		fPrimitive(value),
		fValueNode(NULL),
		fType(NULL),
		fKind(OPERAND_KIND_PRIMITIVE)
	{
	}

	/** @brief Construct a value-node operand. */
	Operand(ValueNode* node)
		:
		fPrimitive(),
		fValueNode(NULL),
		fType(NULL),
		fKind(OPERAND_KIND_UNKNOWN)
	{
		SetTo(node);
	}

	/** @brief Construct a type operand for cast expressions. */
	Operand(Type* type)
		:
		fPrimitive(),
		fValueNode(NULL),
		fType(NULL),
		fKind(OPERAND_KIND_UNKNOWN)
	{
		SetTo(type);
	}

	/** @brief Copy-construct via assignment. */
	Operand(const Operand& X)
		:
		fPrimitive(),
		fValueNode(NULL),
		fType(NULL),
		fKind(OPERAND_KIND_UNKNOWN)
	{
		*this = X;
	}


	/** @brief Destructor; releases value-node / type references. */
	virtual ~Operand()
	{
		Unset();
	}

	/**
	 * @brief Copy-assignment that dispatches on the source operand's kind.
	 *
	 * @param X  Source operand whose state is duplicated.
	 * @return Reference to @c *this.
	 */
	Operand& operator=(const Operand& X)
	{
		switch (X.fKind) {
			case OPERAND_KIND_UNKNOWN:
				Unset();
				break;

			case OPERAND_KIND_PRIMITIVE:
				SetTo(X.fPrimitive);
				break;

			case OPERAND_KIND_VALUE_NODE:
				SetTo(X.fValueNode);
				break;

			case OPERAND_KIND_TYPE:
				SetTo(X.fType);
				break;
		}

		return *this;
	}

	/** @brief Replace the operand state with a primitive value. */
	void SetTo(const BVariant& value)
	{
		Unset();
		fPrimitive = value;
		fKind = OPERAND_KIND_PRIMITIVE;
	}

	/**
	 * @brief Replace the operand with a value-node reference.
	 *
	 * Acquires a reference on @a node and snapshots its current primitive
	 * value into @c fPrimitive when one is available.
	 */
	void SetTo(ValueNode* node)
	{
		Unset();
		fValueNode = node;
		fValueNode->AcquireReference();

		Value* value = node->GetValue();
		if (value != NULL)
			value->ToVariant(fPrimitive);

		fKind = OPERAND_KIND_VALUE_NODE;
	}

	/**
	 * @brief Replace the operand with a type reference.
	 *
	 * @param type  Type to wrap. A reference is acquired.
	 */
	void SetTo(Type* type)
	{
		Unset();
		fType = type;
		fType->AcquireReference();

		fKind = OPERAND_KIND_TYPE;
	}

	/**
	 * @brief Releases any held references and resets the operand to an
	 *        empty/unknown state.
	 */
	void Unset()
	{
		if (fValueNode != NULL)
			fValueNode->ReleaseReference();

		if (fType != NULL)
			fType->ReleaseReference();

		fValueNode = NULL;
		fType = NULL;
		fKind = OPERAND_KIND_UNKNOWN;
	}

	/** @brief Returns the operand's category. */
	inline operand_kind Kind() const
	{
		return fKind;
	}

	/** @brief Returns the underlying primitive value. */
	inline const BVariant& PrimitiveValue() const
	{
		return fPrimitive;
	}

	/** @brief Returns the held ValueNode, or @c NULL when not value-node-typed. */
	inline ValueNode* GetValueNode() const
	{
		return fValueNode;

	}

	/** @brief Returns the held Type, or @c NULL when not type-typed. */
	inline Type* GetType() const
	{
		return fType;
	}

	/**
	 * @brief Numeric @c += across all integer and floating types.
	 *
	 * Promotes both sides to a common type, then dispatches by
	 * @c BVariant::Type() for the actual addition.
	 *
	 * @param rhs  Right-hand operand.
	 * @return Reference to @c *this.
	 */
	Operand& operator+=(const Operand& rhs)
	{
		Operand temp = rhs;
		_ResolveTypesIfNeeded(temp);

		switch (fPrimitive.Type()) {
			case B_INT8_TYPE:
			{
				fPrimitive.SetTo((int8)(fPrimitive.ToInt8()
					+ temp.fPrimitive.ToInt8()));
				break;
			}

			case B_UINT8_TYPE:
			{
				fPrimitive.SetTo((uint8)(fPrimitive.ToUInt8()
					+ temp.fPrimitive.ToUInt8()));
				break;
			}

			case B_INT16_TYPE:
			{
				fPrimitive.SetTo((int16)(fPrimitive.ToInt16()
					+ temp.fPrimitive.ToInt16()));
				break;
			}

			case B_UINT16_TYPE:
			{
				fPrimitive.SetTo((uint16)(fPrimitive.ToUInt16()
					+ temp.fPrimitive.ToUInt16()));
				break;
			}

			case B_INT32_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToInt32()
					+ temp.fPrimitive.ToInt32());
				break;
			}

			case B_UINT32_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToUInt32()
					+ temp.fPrimitive.ToUInt32());
				break;
			}

			case B_INT64_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToInt64()
					+ temp.fPrimitive.ToInt64());
				break;
			}

			case B_UINT64_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToUInt64()
					+ temp.fPrimitive.ToUInt64());
				break;
			}

			case B_FLOAT_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToFloat()
					+ temp.fPrimitive.ToFloat());
				break;
			}

			case B_DOUBLE_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToDouble()
					+ temp.fPrimitive.ToDouble());
				break;
			}
		}

		return *this;
	}

	/**
	 * @brief Numeric @c -= across all integer and floating types.
	 *
	 * @param rhs  Right-hand operand.
	 * @return Reference to @c *this.
	 */
	Operand& operator-=(const Operand& rhs)
	{
		Operand temp = rhs;
		_ResolveTypesIfNeeded(temp);

		switch (fPrimitive.Type()) {
			case B_INT8_TYPE:
			{
				fPrimitive.SetTo((int8)(fPrimitive.ToInt8()
					- temp.fPrimitive.ToInt8()));
				break;
			}

			case B_UINT8_TYPE:
			{
				fPrimitive.SetTo((uint8)(fPrimitive.ToUInt8()
					- temp.fPrimitive.ToUInt8()));
				break;
			}

			case B_INT16_TYPE:
			{
				fPrimitive.SetTo((int16)(fPrimitive.ToInt16()
					- temp.fPrimitive.ToInt16()));
				break;
			}

			case B_UINT16_TYPE:
			{
				fPrimitive.SetTo((uint16)(fPrimitive.ToUInt16()
					- temp.fPrimitive.ToUInt16()));
				break;
			}

			case B_INT32_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToInt32()
					- temp.fPrimitive.ToInt32());
				break;
			}

			case B_UINT32_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToUInt32()
					- temp.fPrimitive.ToUInt32());
				break;
			}

			case B_INT64_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToInt64()
					- temp.fPrimitive.ToInt64());
				break;
			}

			case B_UINT64_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToUInt64()
					- temp.fPrimitive.ToUInt64());
				break;
			}

			case B_FLOAT_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToFloat()
					- temp.fPrimitive.ToFloat());
				break;
			}

			case B_DOUBLE_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToDouble()
					- temp.fPrimitive.ToDouble());
				break;
			}
		}

		return *this;
	}

	/**
	 * @brief Numeric @c /= across all integer and floating types.
	 *
	 * @param rhs  Right-hand operand.
	 * @return Reference to @c *this.
	 * @note The caller is responsible for rejecting division by zero;
	 *       this overload performs the raw operation.
	 */
	Operand& operator/=(const Operand& rhs)
	{
		Operand temp = rhs;
		_ResolveTypesIfNeeded(temp);

		switch (fPrimitive.Type()) {
			case B_INT8_TYPE:
			{
				fPrimitive.SetTo((int8)(fPrimitive.ToInt8()
					/ temp.fPrimitive.ToInt8()));
				break;
			}

			case B_UINT8_TYPE:
			{
				fPrimitive.SetTo((uint8)(fPrimitive.ToUInt8()
					/ temp.fPrimitive.ToUInt8()));
				break;
			}

			case B_INT16_TYPE:
			{
				fPrimitive.SetTo((int16)(fPrimitive.ToInt16()
					/ temp.fPrimitive.ToInt16()));
				break;
			}

			case B_UINT16_TYPE:
			{
				fPrimitive.SetTo((uint16)(fPrimitive.ToUInt16()
					/ temp.fPrimitive.ToUInt16()));
				break;
			}

			case B_INT32_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToInt32()
					/ temp.fPrimitive.ToInt32());
				break;
			}

			case B_UINT32_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToUInt32()
					/ temp.fPrimitive.ToUInt32());
				break;
			}

			case B_INT64_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToInt64()
					/ temp.fPrimitive.ToInt64());
				break;
			}

			case B_UINT64_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToUInt64()
					/ temp.fPrimitive.ToUInt64());
				break;
			}

			case B_FLOAT_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToFloat()
					/ temp.fPrimitive.ToFloat());
				break;
			}

			case B_DOUBLE_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToDouble()
					/ temp.fPrimitive.ToDouble());
				break;
			}
		}

		return *this;
	}

	/**
	 * @brief Numeric @c *= across all integer and floating types.
	 *
	 * @param rhs  Right-hand operand.
	 * @return Reference to @c *this.
	 */
	Operand& operator*=(const Operand& rhs)
	{
		Operand temp = rhs;
		_ResolveTypesIfNeeded(temp);

		switch (fPrimitive.Type()) {
			case B_INT8_TYPE:
			{
				fPrimitive.SetTo((int8)(fPrimitive.ToInt8()
					* temp.fPrimitive.ToInt8()));
				break;
			}

			case B_UINT8_TYPE:
			{
				fPrimitive.SetTo((uint8)(fPrimitive.ToUInt8()
					* temp.fPrimitive.ToUInt8()));
				break;
			}

			case B_INT16_TYPE:
			{
				fPrimitive.SetTo((int16)(fPrimitive.ToInt16()
					* temp.fPrimitive.ToInt16()));
				break;
			}

			case B_UINT16_TYPE:
			{
				fPrimitive.SetTo((uint16)(fPrimitive.ToUInt16()
					* temp.fPrimitive.ToUInt16()));
				break;
			}

			case B_INT32_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToInt32()
					* temp.fPrimitive.ToInt32());
				break;
			}

			case B_UINT32_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToUInt32()
					* temp.fPrimitive.ToUInt32());
				break;
			}

			case B_INT64_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToInt64()
					* temp.fPrimitive.ToInt64());
				break;
			}

			case B_UINT64_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToUInt64()
					* temp.fPrimitive.ToUInt64());
				break;
			}

			case B_FLOAT_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToFloat()
					* temp.fPrimitive.ToFloat());
				break;
			}

			case B_DOUBLE_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToDouble()
					* temp.fPrimitive.ToDouble());
				break;
			}
		}

		return *this;
	}

	/**
	 * @brief Integer @c %= across all integer types.
	 *
	 * @param rhs  Right-hand operand.
	 * @return Reference to @c *this.
	 * @note Float and double types are silently no-ops here; the parser
	 *       rejects them earlier.
	 */
	Operand& operator%=(const Operand& rhs)
	{
		Operand temp = rhs;
		_ResolveTypesIfNeeded(temp);

		switch (fPrimitive.Type()) {
			case B_INT8_TYPE:
			{
				fPrimitive.SetTo((int8)(fPrimitive.ToInt8()
					% temp.fPrimitive.ToInt8()));
				break;
			}

			case B_UINT8_TYPE:
			{
				fPrimitive.SetTo((uint8)(fPrimitive.ToUInt8()
					% temp.fPrimitive.ToUInt8()));
				break;
			}

			case B_INT16_TYPE:
			{
				fPrimitive.SetTo((int16)(fPrimitive.ToInt16()
					% temp.fPrimitive.ToInt16()));
				break;
			}

			case B_UINT16_TYPE:
			{
				fPrimitive.SetTo((uint16)(fPrimitive.ToUInt16()
					% temp.fPrimitive.ToUInt16()));
				break;
			}

			case B_INT32_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToInt32()
					% temp.fPrimitive.ToInt32());
				break;
			}

			case B_UINT32_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToUInt32()
					% temp.fPrimitive.ToUInt32());
				break;
			}

			case B_INT64_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToInt64()
					% temp.fPrimitive.ToInt64());
				break;
			}

			case B_UINT64_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToUInt64()
					% temp.fPrimitive.ToUInt64());
				break;
			}
		}

		return *this;
	}

	/**
	 * @brief Bitwise @c &= across all integer types.
	 *
	 * @param rhs  Right-hand operand.
	 * @return Reference to @c *this.
	 */
	Operand& operator&=(const Operand& rhs)
	{
		Operand temp = rhs;
		_ResolveTypesIfNeeded(temp);

		switch (fPrimitive.Type()) {
			case B_INT8_TYPE:
			{
				fPrimitive.SetTo((int8)(fPrimitive.ToInt8()
					& temp.fPrimitive.ToInt8()));
				break;
			}

			case B_UINT8_TYPE:
			{
				fPrimitive.SetTo((uint8)(fPrimitive.ToUInt8()
					& temp.fPrimitive.ToUInt8()));
				break;
			}

			case B_INT16_TYPE:
			{
				fPrimitive.SetTo((int16)(fPrimitive.ToInt16()
					& temp.fPrimitive.ToInt16()));
				break;
			}

			case B_UINT16_TYPE:
			{
				fPrimitive.SetTo((uint16)(fPrimitive.ToUInt16()
					& temp.fPrimitive.ToUInt16()));
				break;
			}

			case B_INT32_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToInt32()
					& temp.fPrimitive.ToInt32());
				break;
			}

			case B_UINT32_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToUInt32()
					& temp.fPrimitive.ToUInt32());
				break;
			}

			case B_INT64_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToInt64()
					& temp.fPrimitive.ToInt64());
				break;
			}

			case B_UINT64_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToUInt64()
					& temp.fPrimitive.ToUInt64());
				break;
			}
		}

		return *this;
	}

	/**
	 * @brief Bitwise @c |= across all integer types.
	 *
	 * @param rhs  Right-hand operand.
	 * @return Reference to @c *this.
	 */
	Operand& operator|=(const Operand& rhs)
	{
		Operand temp = rhs;
		_ResolveTypesIfNeeded(temp);

		switch (fPrimitive.Type()) {
			case B_INT8_TYPE:
			{
				fPrimitive.SetTo((int8)(fPrimitive.ToInt8()
					| temp.fPrimitive.ToInt8()));
				break;
			}

			case B_UINT8_TYPE:
			{
				fPrimitive.SetTo((uint8)(fPrimitive.ToUInt8()
					| temp.fPrimitive.ToUInt8()));
				break;
			}

			case B_INT16_TYPE:
			{
				fPrimitive.SetTo((int16)(fPrimitive.ToInt16()
					| temp.fPrimitive.ToInt16()));
				break;
			}

			case B_UINT16_TYPE:
			{
				fPrimitive.SetTo((uint16)(fPrimitive.ToUInt16()
					| temp.fPrimitive.ToUInt16()));
				break;
			}

			case B_INT32_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToInt32()
					| temp.fPrimitive.ToInt32());
				break;
			}

			case B_UINT32_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToUInt32()
					| temp.fPrimitive.ToUInt32());
				break;
			}

			case B_INT64_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToInt64()
					| temp.fPrimitive.ToInt64());
				break;
			}

			case B_UINT64_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToUInt64()
					| temp.fPrimitive.ToUInt64());
				break;
			}
		}

		return *this;
	}

	/**
	 * @brief Bitwise @c ^= across all integer types.
	 *
	 * @param rhs  Right-hand operand.
	 * @return Reference to @c *this.
	 */
	Operand& operator^=(const Operand& rhs)
	{
		Operand temp = rhs;
		_ResolveTypesIfNeeded(temp);

		switch (fPrimitive.Type()) {
			case B_INT8_TYPE:
			{
				fPrimitive.SetTo((int8)(fPrimitive.ToInt8()
					^ temp.fPrimitive.ToInt8()));
				break;
			}

			case B_UINT8_TYPE:
			{
				fPrimitive.SetTo((uint8)(fPrimitive.ToUInt8()
					^ temp.fPrimitive.ToUInt8()));
				break;
			}

			case B_INT16_TYPE:
			{
				fPrimitive.SetTo((int16)(fPrimitive.ToInt16()
					^ temp.fPrimitive.ToInt16()));
				break;
			}

			case B_UINT16_TYPE:
			{
				fPrimitive.SetTo((uint16)(fPrimitive.ToUInt16()
					^ temp.fPrimitive.ToUInt16()));
				break;
			}

			case B_INT32_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToInt32()
					^ temp.fPrimitive.ToInt32());
				break;
			}

			case B_UINT32_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToUInt32()
					^ temp.fPrimitive.ToUInt32());
				break;
			}

			case B_INT64_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToInt64()
					^ temp.fPrimitive.ToInt64());
				break;
			}

			case B_UINT64_TYPE:
			{
				fPrimitive.SetTo(fPrimitive.ToUInt64()
					^ temp.fPrimitive.ToUInt64());
				break;
			}
		}

		return *this;
	}

	/**
	 * @brief Unary minus.
	 *
	 * Promotes the operand to its primitive form and negates per its
	 * underlying numeric type.
	 *
	 * @return Negated copy.
	 */
	Operand operator-() const
	{
		Operand value(*this);
		value._ResolveToPrimitive();

		switch (fPrimitive.Type()) {
			case B_INT8_TYPE:
			{
				value.fPrimitive.SetTo((int8)-fPrimitive.ToInt8());
				break;
			}

			case B_UINT8_TYPE:
			{
				value.fPrimitive.SetTo((uint8)-fPrimitive.ToUInt8());
				break;
			}

			case B_INT16_TYPE:
			{
				value.fPrimitive.SetTo((int16)-fPrimitive.ToInt16());
				break;
			}

			case B_UINT16_TYPE:
			{
				value.fPrimitive.SetTo((uint16)-fPrimitive.ToUInt16());
				break;
			}

			case B_INT32_TYPE:
			{
				value.fPrimitive.SetTo(-fPrimitive.ToInt32());
				break;
			}

			case B_UINT32_TYPE:
			{
				value.fPrimitive.SetTo(-fPrimitive.ToUInt32());
				break;
			}

			case B_INT64_TYPE:
			{
				value.fPrimitive.SetTo(-fPrimitive.ToInt64());
				break;
			}

			case B_UINT64_TYPE:
			{
				value.fPrimitive.SetTo(-fPrimitive.ToUInt64());
				break;
			}

			case B_FLOAT_TYPE:
			{
				value.fPrimitive.SetTo(-fPrimitive.ToFloat());
				break;
			}

			case B_DOUBLE_TYPE:
			{
				value.fPrimitive.SetTo(-fPrimitive.ToDouble());
				break;
			}
		}

		return value;
	}

	/**
	 * @brief Unary bitwise complement.
	 *
	 * @return Bit-inverted copy. Float and double types are left
	 *         unchanged (the parser does not allow @c ~ on those).
	 */
	Operand operator~() const
	{
		Operand value(*this);
		value._ResolveToPrimitive();

		switch (fPrimitive.Type()) {
			case B_INT8_TYPE:
			{
				value.fPrimitive.SetTo((int8)~fPrimitive.ToInt8());
				break;
			}

			case B_UINT8_TYPE:
			{
				value.fPrimitive.SetTo((uint8)~fPrimitive.ToUInt8());
				break;
			}

			case B_INT16_TYPE:
			{
				value.fPrimitive.SetTo((int16)~fPrimitive.ToInt16());
				break;
			}

			case B_UINT16_TYPE:
			{
				value.fPrimitive.SetTo((uint16)~fPrimitive.ToUInt16());
				break;
			}

			case B_INT32_TYPE:
			{
				value.fPrimitive.SetTo(~fPrimitive.ToInt32());
				break;
			}

			case B_UINT32_TYPE:
			{
				value.fPrimitive.SetTo(~fPrimitive.ToUInt32());
				break;
			}

			case B_INT64_TYPE:
			{
				value.fPrimitive.SetTo(~fPrimitive.ToInt64());
				break;
			}

			case B_UINT64_TYPE:
			{
				value.fPrimitive.SetTo(~fPrimitive.ToUInt64());
				break;
			}
		}

		return value;
	}

	/**
	 * @brief Less-than comparison after promoting to a common type.
	 *
	 * @param rhs  Right-hand operand.
	 * @return Non-zero when this is strictly less than @a rhs.
	 */
	int operator<(const Operand& rhs) const
	{
		Operand lhs = *this;
		Operand temp = rhs;

		lhs._ResolveTypesIfNeeded(temp);

		int result = 0;
		switch (fPrimitive.Type()) {
			case B_INT8_TYPE:
			{
				result = lhs.fPrimitive.ToInt8() < temp.fPrimitive.ToInt8();
				break;
			}

			case B_UINT8_TYPE:
			{
				result = lhs.fPrimitive.ToUInt8() < temp.fPrimitive.ToUInt8();
				break;
			}

			case B_INT16_TYPE:
			{
				result = lhs.fPrimitive.ToInt16() < temp.fPrimitive.ToInt16();
				break;
			}

			case B_UINT16_TYPE:
			{
				result = lhs.fPrimitive.ToUInt16()
					< temp.fPrimitive.ToUInt16();
				break;
			}

			case B_INT32_TYPE:
			{
				result = lhs.fPrimitive.ToInt32() < temp.fPrimitive.ToInt32();
				break;
			}

			case B_UINT32_TYPE:
			{
				result = lhs.fPrimitive.ToUInt32()
					< temp.fPrimitive.ToUInt32();
				break;
			}

			case B_INT64_TYPE:
			{
				result = lhs.fPrimitive.ToInt64() < temp.fPrimitive.ToInt64();
				break;
			}

			case B_UINT64_TYPE:
			{
				result = lhs.fPrimitive.ToUInt64()
					< temp.fPrimitive.ToUInt64();
				break;
			}

			case B_FLOAT_TYPE:
			{
				result = lhs.fPrimitive.ToFloat() < temp.fPrimitive.ToFloat();
				break;
			}

			case B_DOUBLE_TYPE:
			{
				result = lhs.fPrimitive.ToDouble()
					< temp.fPrimitive.ToDouble();
				break;
			}
		}

		return result;
	}

	/**
	 * @brief Less-or-equal comparison; expressed in terms of @c < and @c ==.
	 */
	int operator<=(const Operand& rhs) const
	{
		return (*this < rhs) || (*this == rhs);
	}

	/**
	 * @brief Greater-than comparison after promoting to a common type.
	 *
	 * @param rhs  Right-hand operand.
	 * @return Non-zero when this is strictly greater than @a rhs.
	 */
	int operator>(const Operand& rhs) const
	{
		Operand lhs = *this;
		Operand temp = rhs;
		lhs._ResolveTypesIfNeeded(temp);

		int result = 0;
		switch (fPrimitive.Type()) {
			case B_INT8_TYPE:
			{
				result = lhs.fPrimitive.ToInt8() > temp.fPrimitive.ToInt8();
				break;
			}

			case B_UINT8_TYPE:
			{
				result = lhs.fPrimitive.ToUInt8() > temp.fPrimitive.ToUInt8();
				break;
			}

			case B_INT16_TYPE:
			{
				result = lhs.fPrimitive.ToInt16() > temp.fPrimitive.ToInt16();
				break;
			}

			case B_UINT16_TYPE:
			{
				result = lhs.fPrimitive.ToUInt16()
					> temp.fPrimitive.ToUInt16();
				break;
			}

			case B_INT32_TYPE:
			{
				result = lhs.fPrimitive.ToInt32() > temp.fPrimitive.ToInt32();
				break;
			}

			case B_UINT32_TYPE:
			{
				result = lhs.fPrimitive.ToUInt32()
					> temp.fPrimitive.ToUInt32();
				break;
			}

			case B_INT64_TYPE:
			{
				result = lhs.fPrimitive.ToInt64() > temp.fPrimitive.ToInt64();
				break;
			}

			case B_UINT64_TYPE:
			{
				result = lhs.fPrimitive.ToUInt64()
					> temp.fPrimitive.ToUInt64();
				break;
			}

			case B_FLOAT_TYPE:
			{
				result = lhs.fPrimitive.ToFloat() > temp.fPrimitive.ToFloat();
				break;
			}

			case B_DOUBLE_TYPE:
			{
				result = lhs.fPrimitive.ToDouble()
					> temp.fPrimitive.ToDouble();
				break;
			}
		}

		return result;
	}

	/**
	 * @brief Greater-or-equal comparison; expressed in terms of @c > and @c ==.
	 */
	int operator>=(const Operand& rhs) const
	{
		return (*this > rhs) || (*this == rhs);
	}

	/**
	 * @brief Equality comparison after promoting to a common type.
	 *
	 * @param rhs  Right-hand operand.
	 * @return Non-zero when both sides compare equal under their common type.
	 */
	int	operator==(const Operand& rhs) const
	{
		Operand lhs = *this;
		Operand temp = rhs;
		lhs._ResolveTypesIfNeeded(temp);

		int result = 0;
		switch (fPrimitive.Type()) {
			case B_INT8_TYPE:
			{
				result = lhs.fPrimitive.ToInt8() == temp.fPrimitive.ToInt8();
				break;
			}

			case B_UINT8_TYPE:
			{
				result = lhs.fPrimitive.ToUInt8() == temp.fPrimitive.ToUInt8();
				break;
			}

			case B_INT16_TYPE:
			{
				result = lhs.fPrimitive.ToInt16() == temp.fPrimitive.ToInt16();
				break;
			}

			case B_UINT16_TYPE:
			{
				result = lhs.fPrimitive.ToUInt16()
					== temp.fPrimitive.ToUInt16();
				break;
			}

			case B_INT32_TYPE:
			{
				result = lhs.fPrimitive.ToInt32() == temp.fPrimitive.ToInt32();
				break;
			}

			case B_UINT32_TYPE:
			{
				result = lhs.fPrimitive.ToUInt32()
					== temp.fPrimitive.ToUInt32();
				break;
			}

			case B_INT64_TYPE:
			{
				result = lhs.fPrimitive.ToInt64() == temp.fPrimitive.ToInt64();
				break;
			}

			case B_UINT64_TYPE:
			{
				result = lhs.fPrimitive.ToUInt64()
					== temp.fPrimitive.ToUInt64();
				break;
			}

			case B_FLOAT_TYPE:
			{
				result = lhs.fPrimitive.ToFloat() == temp.fPrimitive.ToFloat();
				break;
			}

			case B_DOUBLE_TYPE:
			{
				result = lhs.fPrimitive.ToDouble()
					== temp.fPrimitive.ToDouble();
				break;
			}
		}

		return result;
	}

	/**
	 * @brief Inequality comparison; expressed as the logical complement
	 *        of @c ==.
	 */
	int operator!=(const Operand& rhs) const
	{
		return !(*this == rhs);
	}

private:
	/**
	 * @brief Coerces the stored primitive value to the given type.
	 *
	 * @param type  Destination @c B_*_TYPE code.
	 */
	void _GetAsType(type_code type)
	{
		switch (type) {
			case B_INT8_TYPE:
				fPrimitive.SetTo(fPrimitive.ToInt8());
				break;
			case B_UINT8_TYPE:
				fPrimitive.SetTo(fPrimitive.ToUInt8());
				break;
			case B_INT16_TYPE:
				fPrimitive.SetTo(fPrimitive.ToInt16());
				break;
			case B_UINT16_TYPE:
				fPrimitive.SetTo(fPrimitive.ToUInt16());
				break;
			case B_INT32_TYPE:
				fPrimitive.SetTo(fPrimitive.ToInt32());
				break;
			case B_UINT32_TYPE:
				fPrimitive.SetTo(fPrimitive.ToUInt32());
				break;
			case B_INT64_TYPE:
				fPrimitive.SetTo(fPrimitive.ToInt64());
				break;
			case B_UINT64_TYPE:
				fPrimitive.SetTo(fPrimitive.ToUInt64());
				break;
			case B_FLOAT_TYPE:
				fPrimitive.SetTo(fPrimitive.ToFloat());
				break;
			case B_DOUBLE_TYPE:
				fPrimitive.SetTo(fPrimitive.ToDouble());
				break;
		}
	}

	/**
	 * @brief Promotes both operands to a single common numeric type.
	 *
	 * Both sides are first reduced to primitive form, then a priority
	 * type is chosen (largest size, signed/unsigned reconciled, float
	 * dominates). Throws @c ParseException when either side is not
	 * numeric or the types cannot be reconciled.
	 *
	 * @param other  Operand on the other side of the binary expression.
	 */
	void _ResolveTypesIfNeeded(Operand& other)
	{
		_ResolveToPrimitive();
		other._ResolveToPrimitive();

		if (!fPrimitive.IsNumber() || !other.fPrimitive.IsNumber()) {
				throw ParseException("Cannot perform mathematical operations "
					"between non-numerical objects.", 0);
		}

		type_code thisType = fPrimitive.Type();
		type_code otherType = other.fPrimitive.Type();

		if (thisType == otherType)
			return;

		type_code resolvedType = _ResolvePriorityType(thisType, otherType);
		if (thisType != resolvedType)
			_GetAsType(resolvedType);

		if (otherType != resolvedType)
			other._GetAsType(resolvedType);
	}

	/**
	 * @brief Reduces a value-node operand to its primitive value.
	 *
	 * Throws @c ParseException for type operands or when the value node's
	 * underlying value cannot be retrieved.
	 */
	void _ResolveToPrimitive()
	{
		if (Kind() == OPERAND_KIND_PRIMITIVE)
			return;
		else if (Kind() == OPERAND_KIND_TYPE) {
			throw ParseException("Cannot perform mathematical operations "
				"between type objects.", 0);
		}

		status_t error = fValueNode->LocationAndValueResolutionState();
		if (error != B_OK) {
			BString errorMessage;
			errorMessage.SetToFormat("Failed to resolve value of %s: %"
				B_PRId32 ".", fValueNode->Name().String(), error);
			throw ParseException(errorMessage.String(), 0);
		}

		Value* value = fValueNode->GetValue();
		BVariant tempValue;
		if (value->ToVariant(tempValue))
			SetTo(tempValue);
		else {
			BString error;
			error.SetToFormat("Failed to retrieve value of %s.",
				fValueNode->Name().String());
			throw ParseException(error.String(), 0);
		}
	}

	/**
	 * @brief Picks a single numeric type to promote two operands to.
	 *
	 * Floats dominate ints; when both are integer the largest size wins;
	 * signed wins when either side is signed.
	 *
	 * @param lhs  Type of the left operand.
	 * @param rhs  Type of the right operand.
	 * @return The chosen common @c B_*_TYPE code.
	 * @note Throws @c ParseException when the sizes are not 1/2/4/8.
	 */
	type_code _ResolvePriorityType(type_code lhs, type_code rhs) const
	{
		size_t byteSize = std::max(BVariant::SizeOfType(lhs),
			BVariant::SizeOfType(rhs));
		bool isFloat = BVariant::TypeIsFloat(lhs)
			|| BVariant::TypeIsFloat(rhs);
		bool isSigned = isFloat;
		if (!isFloat) {
			BVariant::TypeIsInteger(lhs, &isSigned);
			if (!isSigned)
				BVariant::TypeIsInteger(rhs, &isSigned);
		}

		if (isFloat) {
			if (byteSize == sizeof(float))
				return B_FLOAT_TYPE;
			return B_DOUBLE_TYPE;
		}

		switch (byteSize) {
			case 1:
				return isSigned ? B_INT8_TYPE : B_UINT8_TYPE;
			case 2:
				return isSigned ? B_INT16_TYPE : B_UINT16_TYPE;
			case 4:
				return isSigned ? B_INT32_TYPE : B_UINT32_TYPE;
			case 8:
				return isSigned ? B_INT64_TYPE : B_UINT64_TYPE;
			default:
				break;
		}

		BString error;
		error.SetToFormat("Unable to reconcile types %#" B_PRIx32
			" and %#" B_PRIx32, lhs, rhs);
		throw ParseException(error.String(), 0);
	}

private:
	BVariant 		fPrimitive;
	ValueNode*		fValueNode;
	Type*			fType;
	operand_kind	fKind;
};


// #pragma mark - CLanguageExpressionEvaluator


/**
 * @brief Construct an evaluator with its own owned Tokenizer instance.
 */
CLanguageExpressionEvaluator::CLanguageExpressionEvaluator()
	:
	fTokenizer(new Tokenizer()),
	fTypeInfo(NULL),
	fNodeManager(NULL)
{
}


/**
 * @brief Destructor; deletes the owned Tokenizer.
 */
CLanguageExpressionEvaluator::~CLanguageExpressionEvaluator()
{
	delete fTokenizer;
}


/**
 * @brief Parses and evaluates a complete expression string.
 *
 * Sets up the tokeniser, drives the recursive-descent parser, and packages
 * the resulting Operand into a freshly-allocated ExpressionResult. The
 * caller takes ownership of the returned object.
 *
 * @param expressionString  Null-terminated source expression.
 * @param manager           Value-node manager for variable lookups.
 * @param info              Type-information service for type lookups.
 * @return Newly allocated ExpressionResult, or @c NULL on allocation
 *         failure for primitive output values.
 * @note Throws @c ParseException on malformed input or
 *       @c ValueNeededException when an unresolved variable's value is
 *       needed.
 */
ExpressionResult*
CLanguageExpressionEvaluator::Evaluate(const char* expressionString,
	ValueNodeManager* manager, TeamTypeInformation* info)
{
	fNodeManager = manager;
	fTypeInfo = info;
	fTokenizer->SetTo(expressionString);

	Operand value = _ParseSum();
	Token token = fTokenizer->NextToken();
	if (token.type != TOKEN_END_OF_LINE)
		throw ParseException("parse error", token.position);

	ExpressionResult* result = new(std::nothrow)ExpressionResult;
	if (result != NULL) {
		BReference<ExpressionResult> resultReference(result, true);
		if (value.Kind() == OPERAND_KIND_PRIMITIVE) {
			Value* outputValue = NULL;
			BVariant primitive = value.PrimitiveValue();
			if (primitive.IsInteger())
				outputValue = new(std::nothrow) IntegerValue(primitive);
			else if (primitive.IsFloat()) {
				outputValue = new(std::nothrow) FloatValue(
					primitive.ToDouble());
			}

			BReference<Value> valueReference;
			if (outputValue != NULL) {
				valueReference.SetTo(outputValue, true);
				result->SetToPrimitive(outputValue);
			} else
				return NULL;
		} else if (value.Kind() == OPERAND_KIND_VALUE_NODE)
			result->SetToValueNode(value.GetValueNode()->NodeChild());
		else if (value.Kind() == OPERAND_KIND_TYPE)
			result->SetToType(value.GetType());

		resultReference.Detach();
	}

	return result;
}


/**
 * @brief Parses an additive (sum/difference) expression.
 *
 * Grammar level: @c sum := product { (@c + | @c -) product }*. Higher
 * precedence (multiplicative, bitwise, comparison, logical) is handled
 * inside @c _ParseProduct(); lower precedence (none in this dialect) does
 * not exist.
 *
 * @return Operand carrying the evaluated sum.
 */
CLanguageExpressionEvaluator::Operand
CLanguageExpressionEvaluator::_ParseSum()
{
	Operand value = _ParseProduct();

	while (true) {
		Token token = fTokenizer->NextToken();
		switch (token.type) {
			case TOKEN_PLUS:
				value += _ParseProduct();
				break;
			case TOKEN_MINUS:
				value -= _ParseProduct();
				break;

			default:
				fTokenizer->RewindToken();
				return value;
		}
	}
}


/**
 * @brief Parses a product-level expression.
 *
 * Despite the name, this level folds together every binary operator above
 * additive precedence: multiplicative (@c * @c / @c %), bitwise (@c & @c
 * | @c ^), logical (@c && @c ||), and comparison/equality (@c == @c != @c
 * < @c <= @c > @c >=). Each binary operator dispatches to the matching
 * Operand operator; @c && / @c || and the comparison operators produce
 * an int64-typed primitive truth value.
 *
 * @return Operand carrying the evaluated subexpression.
 * @note Throws @c ParseException on division/modulo by zero.
 */
CLanguageExpressionEvaluator::Operand
CLanguageExpressionEvaluator::_ParseProduct()
{
	static Operand zero(int64(0LL));

	Operand value = _ParseUnary();

	while (true) {
		Token token = fTokenizer->NextToken();
		switch (token.type) {
			case TOKEN_STAR:
				value *= _ParseUnary();
				break;
			case TOKEN_SLASH:
			{
				Operand rhs = _ParseUnary();
				if (rhs == zero)
					throw ParseException("division by zero", token.position);
				value /= rhs;
				break;
			}

			case TOKEN_MODULO:
			{
				Operand rhs = _ParseUnary();
				if (rhs == zero)
					throw ParseException("modulo by zero", token.position);
				value %= rhs;
				break;
			}

			case TOKEN_LOGICAL_AND:
			{
				value.SetTo((value != zero)
					&& (_ParseUnary() != zero));

				break;
			}

			case TOKEN_LOGICAL_OR:
			{
				value.SetTo((value != zero)
					|| (_ParseUnary() != zero));
				break;
			}

			case TOKEN_BITWISE_AND:
				value &= _ParseUnary();
				break;

			case TOKEN_BITWISE_OR:
				value |= _ParseUnary();
				break;

			case TOKEN_BITWISE_XOR:
				value ^= _ParseUnary();
				break;

			case TOKEN_EQ:
				value.SetTo((int64)(value == _ParseUnary()));
				break;

			case TOKEN_NE:
				value.SetTo((int64)(value != _ParseUnary()));
				break;

			case TOKEN_GT:
				value.SetTo((int64)(value > _ParseUnary()));
				break;

			case TOKEN_GE:
				value.SetTo((int64)(value >= _ParseUnary()));
				break;

			case TOKEN_LT:
				value.SetTo((int64)(value < _ParseUnary()));
				break;

			case TOKEN_LE:
				value.SetTo((int64)(value <= _ParseUnary()));
				break;

			default:
				fTokenizer->RewindToken();
				return value;
		}
	}
}


/**
 * @brief Parses a unary expression.
 *
 * Handles unary @c +, unary @c -, bitwise @c ~, logical @c !, and the
 * special-case dispatch into @c _ParseIdentifier() for identifier-led
 * expressions. Anything else delegates to @c _ParseAtom().
 *
 * @return Operand carrying the evaluated unary expression.
 * @note Throws @c ParseException on premature end-of-input.
 */
CLanguageExpressionEvaluator::Operand
CLanguageExpressionEvaluator::_ParseUnary()
{
	Token token = fTokenizer->NextToken();
	if (token.type == TOKEN_END_OF_LINE)
		throw ParseException("unexpected end of expression", token.position);

	switch (token.type) {
		case TOKEN_PLUS:
			return _ParseUnary();

		case TOKEN_MINUS:
			return -_ParseUnary();

		case TOKEN_BITWISE_NOT:
			return ~_ParseUnary();

		case TOKEN_LOGICAL_NOT:
		{
			Operand zero((int64)0);
			return Operand((int64)(_ParseUnary() == zero));
		}

		case TOKEN_IDENTIFIER:
			fTokenizer->RewindToken();
			return _ParseIdentifier();

		default:
			fTokenizer->RewindToken();
			return _ParseAtom();
	}

	return Operand();
}


/**
 * @brief Parses an identifier reference, possibly chained via @c ->.
 *
 * Resolves the identifier against the value-node container (or the
 * current parent value node when chasing a member access), falling back
 * on a type lookup if no variable matches. Handles the implicit @c this
 * lookup, address-type indirection, and recursive member-pointer
 * traversal.
 *
 * @param parentNode  When non-NULL, restricts the search to children of
 *                    @a parentNode (post-@c -> chain).
 * @return Operand referencing the located ValueNode or Type.
 * @note Throws @c ParseException when the identifier cannot be resolved
 *       or @c ValueNeededException when an unresolved value is required.
 */
CLanguageExpressionEvaluator::Operand
CLanguageExpressionEvaluator::_ParseIdentifier(ValueNode* parentNode)
{
	Token token = fTokenizer->NextToken();
	const BString& identifierName = token.string;

	ValueNodeChild* child = NULL;
	if (fNodeManager != NULL) {
		ValueNodeContainer* container = fNodeManager->GetContainer();
		AutoLocker<ValueNodeContainer> containerLocker(container);

		if (parentNode == NULL) {
			ValueNodeChild* thisChild = NULL;
			for (int32 i = 0; i < container->CountChildren(); i++) {
				ValueNodeChild* current = container->ChildAt(i);
				const BString& nodeName = current->Name();
				if (nodeName == identifierName) {
					child = current;
					break;
				} else if (nodeName == "this")
					thisChild = current;
			}

			if (child == NULL && thisChild != NULL) {
				// the name was not found in the variables or parameters,
				// but we have a class pointer. Try to find the name in the
				// list of members.
				_RequestValueIfNeeded(token, thisChild);
				ValueNode* thisNode = thisChild->Node();
				fTokenizer->RewindToken();
				return _ParseIdentifier(thisNode);
			}
		} else {
			// skip intermediate address nodes
			if (parentNode->GetType()->Kind() == TYPE_ADDRESS
				&& parentNode->CountChildren() == 1) {
				child = parentNode->ChildAt(0);

				_RequestValueIfNeeded(token, child);
				parentNode = child->Node();
				fTokenizer->RewindToken();
				return _ParseIdentifier(parentNode);
			}

			for (int32 i = 0; i < parentNode->CountChildren(); i++) {
				ValueNodeChild* current = parentNode->ChildAt(i);
				const BString& nodeName = current->Name();
				if (nodeName == identifierName) {
					child = current;
					break;
				}
			}
		}
	}

	if (child == NULL && fTypeInfo != NULL) {
		Type* resultType = NULL;
		status_t error = fTypeInfo->LookupTypeByName(identifierName,
			TypeLookupConstraints(), resultType);
		if (error == B_OK) {
			BReference<Type> typeReference(resultType, true);
			return _ParseType(resultType);
		} else if (error != B_ENTRY_NOT_FOUND) {
			BString errorMessage;
			errorMessage.SetToFormat("Failed to look up type name '%s': %"
				B_PRId32 ".", identifierName.String(), error);
			throw ParseException(errorMessage.String(), token.position);
		}
	}

	BString errorMessage;
	if (child == NULL) {
		errorMessage.SetToFormat("Unable to resolve variable name: '%s'",
			identifierName.String());
		throw ParseException(errorMessage, token.position);
	}

	_RequestValueIfNeeded(token, child);
	ValueNode* node = child->Node();

	token = fTokenizer->NextToken();
	if (token.type == TOKEN_MEMBER_PTR) {
		token = fTokenizer->NextToken();
		if (token.type == TOKEN_IDENTIFIER) {
			fTokenizer->RewindToken();
			return _ParseIdentifier(node);
		} else {
			throw ParseException("Expected identifier after member "
				"dereference.", token.position);
		}
	} else
		fTokenizer->RewindToken();

	return Operand(node);
}


/**
 * @brief Parses an atomic expression: a constant, parenthesised subexpression,
 *        or a typecast.
 *
 * If the parenthesised expression evaluates to a type and more tokens
 * follow, this is interpreted as a C-style cast: the rest of the
 * expression is parsed and a synthetic ValueNode is wrapped over the
 * casted result.
 *
 * @return Operand carrying the evaluated atom.
 * @note Throws @c ParseException on premature end-of-input or when a
 *       cast cannot be applied to the right-hand expression.
 */
CLanguageExpressionEvaluator::Operand
CLanguageExpressionEvaluator::_ParseAtom()
{
	Token token = fTokenizer->NextToken();
	if (token.type == TOKEN_END_OF_LINE)
		throw ParseException("Unexpected end of expression", token.position);

	Operand value;

	if (token.type == TOKEN_CONSTANT)
		value.SetTo(token.value);
	else {
		fTokenizer->RewindToken();

		_EatToken(TOKEN_OPENING_PAREN);

		value = _ParseSum();

		_EatToken(TOKEN_CLOSING_PAREN);
	}

	if (value.Kind() == OPERAND_KIND_TYPE) {
		token = fTokenizer->NextToken();
		if (token.type == TOKEN_END_OF_LINE)
			return value;

		Type* castType = value.GetType();
		// if our evaluated result was a type, and there still remain
		// further tokens to evaluate, then this is a typecast for
		// a subsequent expression. Attempt to evaluate it, and then
		// apply the cast to the result.
		fTokenizer->RewindToken();
		value = _ParseSum();
		ValueNodeChild* child = NULL;
		if (value.Kind() != OPERAND_KIND_PRIMITIVE
			&& value.Kind() != OPERAND_KIND_VALUE_NODE) {
			throw ParseException("Expected value or variable expression after"
				" typecast.", token.position);
		}

		if (value.Kind() == OPERAND_KIND_VALUE_NODE)
			child = value.GetValueNode()->NodeChild();
		else if (value.Kind() == OPERAND_KIND_PRIMITIVE)
			_GetNodeChildForPrimitive(token, value.PrimitiveValue(), child);

		ValueNode* newNode = NULL;
		status_t error = TypeHandlerRoster::Default()->CreateValueNode(child,
			castType, NULL, newNode);
		if (error != B_OK) {
			throw ParseException("Unable to create value node for typecast"
				" operation.", token.position);
		}
		child->SetNode(newNode);
		value.SetTo(newNode);
	}

	return value;
}


/**
 * @brief Consumes a token of the expected type or throws.
 *
 * Used to enforce required punctuation (e.g. closing parens, brackets).
 *
 * @param type  Expected token type (a @c TOKEN_* enumerator).
 * @note Throws @c ParseException when the next token does not match.
 */
void
CLanguageExpressionEvaluator::_EatToken(int32 type)
{
	Token token = fTokenizer->NextToken();
	if (token.type != type) {
		BString expected;
		switch (type) {
			case TOKEN_IDENTIFIER:
				expected = "an identifier";
				break;

			case TOKEN_CONSTANT:
				expected = "a constant";
				break;

			case TOKEN_SLASH:
				expected = "'/', '\\', or ':'";
				break;

			case TOKEN_END_OF_LINE:
				expected = "'\\n'";
				break;

			default:
				expected << "'" << TokenTypeToString(type) << "'";
				break;
		}

		BString temp;
		temp << "Expected " << expected.String() << " got '" << token.string
			<< "'";
		throw ParseException(temp.String(), token.position);
	}
}


/**
 * @brief Parses optional pointer/reference/array modifiers on a type.
 *
 * After the caller has identified a base Type, this routine consumes any
 * trailing @c *, @c &, or @c [N] modifiers, building up the appropriate
 * derived type via Type::CreateDerivedAddressType() and
 * CreateDerivedArrayType().
 *
 * @param baseType  Initial base type already recognised.
 * @return Operand carrying the final derived type.
 * @note Throws @c ParseException on invalid array sizes or when the
 *       runtime cannot construct the requested derived type.
 */
CLanguageExpressionEvaluator::Operand
CLanguageExpressionEvaluator::_ParseType(Type* baseType)
{
	BReference<Type> typeReference;
	Type* finalType = baseType;

	bool arraySpecifierEncountered = false;
	status_t error;
	for (;;) {
		Token token = fTokenizer->NextToken();
		if (token.type == TOKEN_STAR || token.type == TOKEN_BITWISE_AND) {
			if (arraySpecifierEncountered)
				break;

			address_type_kind addressKind = (token.type == TOKEN_STAR)
					? DERIVED_TYPE_POINTER : DERIVED_TYPE_REFERENCE;
			AddressType* derivedType = NULL;
			error = finalType->CreateDerivedAddressType(addressKind,
				derivedType);
			if (error != B_OK) {
				BString errorMessage;
				errorMessage.SetToFormat("Failed to create derived address"
					" type %d for base type %s: %s (%" B_PRId32 ")",
					addressKind, finalType->Name().String(), strerror(error),
					error);
				throw ParseException(errorMessage, token.position);
			}

			finalType = derivedType;
			typeReference.SetTo(finalType, true);
		} else if (token.type == TOKEN_OPENING_SQUARE_BRACKET) {
			Operand indexSize = _ParseSum();
			if (indexSize.Kind() == OPERAND_KIND_TYPE) {
				throw ParseException("Cannot specify type name as array"
					" subscript.", token.position);
			}

			_EatToken(TOKEN_CLOSING_SQUARE_BRACKET);

			uint32 resolvedSize = indexSize.PrimitiveValue().ToUInt32();
			if (resolvedSize == 0) {
				throw ParseException("Non-zero array size required in type"
					" specifier.", token.position);
			}

			ArrayType* derivedType = NULL;
			error = finalType->CreateDerivedArrayType(0, resolvedSize, true,
				derivedType);
			if (error != B_OK) {
				BString errorMessage;
				errorMessage.SetToFormat("Failed to create derived array type"
					" of size %" B_PRIu32 " for base type %s: %s (%"
					B_PRId32 ")", resolvedSize, finalType->Name().String(),
					strerror(error), error);
				throw ParseException(errorMessage, token.position);
			}

			arraySpecifierEncountered = true;
			finalType = derivedType;
			typeReference.SetTo(finalType, true);
		} else
			break;
	}

	typeReference.Detach();
	fTokenizer->RewindToken();
	return Operand(finalType);
}


/**
 * @brief Ensures the supplied value-node child has a resolved value.
 *
 * Requests child enumeration when the node has no children yet, then
 * inspects the location/value resolution states. An unresolved state is
 * reported by throwing @c ValueNeededException so the calling job can
 * suspend, schedule a resolve, and re-evaluate.
 *
 * @param token  Token used to anchor any thrown ParseException position.
 * @param child  Value-node child whose value the evaluator needs.
 * @note Throws @c ParseException on resolver errors and
 *       @c ValueNeededException when a follow-up resolve is required.
 */
void
CLanguageExpressionEvaluator::_RequestValueIfNeeded(
	const Token& token, ValueNodeChild* child)
{
	status_t state;
	BString errorMessage;
	if (child->Node() == NULL) {
		state = fNodeManager->AddChildNodes(child);
		if (state != B_OK) {
			errorMessage.SetToFormat("Unable to add children for node '%s': "
				"%s", child->Name().String(), strerror(state));
			throw ParseException(errorMessage, token.position);
		}
	}

	state = child->LocationResolutionState();
	if (state == VALUE_NODE_UNRESOLVED)
		throw ValueNeededException(child->Node());
	else if (state != B_OK) {
		errorMessage.SetToFormat("Unable to resolve variable value for '%s': "
			"%s", child->Name().String(), strerror(state));
		throw ParseException(errorMessage, token.position);
	}

	ValueNode* node = child->Node();
	state = node->LocationAndValueResolutionState();
	if (state == VALUE_NODE_UNRESOLVED)
		throw ValueNeededException(child->Node());
	else if (state != B_OK) {
		errorMessage.SetToFormat("Unable to resolve variable value for '%s': "
			"%s", child->Name().String(), strerror(state));
		throw ParseException(errorMessage, token.position);
	}
}


/**
 * @brief Builds a synthetic ValueNodeChild wrapping a primitive value.
 *
 * Used to re-enter the regular value-node machinery for primitive operands
 * that participate in a typecast. Allocates a SyntheticPrimitiveType, a
 * ValueLocation backed by the value's bytes, an InternalVariableID, and a
 * Variable, and finally a VariableValueNodeChild bound to that Variable.
 *
 * @param token   Token used to anchor any thrown ParseException position.
 * @param value   Primitive value to wrap.
 * @param _output Out: receives the freshly allocated child. Caller takes
 *                ownership.
 * @note Throws @c ParseException on any allocation/formatting failure.
 */
void
CLanguageExpressionEvaluator::_GetNodeChildForPrimitive(const Token& token,
	const BVariant& value, ValueNodeChild*& _output) const
{
	Type* type = new(std::nothrow) SyntheticPrimitiveType(value.Type());
	if (type == NULL) {
		throw ParseException("Out of memory while creating type object.",
			token.position);
	}

	BReference<Type> typeReference(type, true);
	ValueLocation* location = new(std::nothrow) ValueLocation();
	if (location == NULL) {
		throw ParseException("Out of memory while creating location object.",
			token.position);
	}

	BReference<ValueLocation> locationReference(location, true);
	ValuePieceLocation piece;
	if (!piece.SetToValue(value.Bytes(), value.Size())
		|| !location->AddPiece(piece)) {
		throw ParseException("Out of memory populating location"
			" object.", token.position);
	}

	char variableName[32];
	if (!IntegerFormatter::FormatValue(value, INTEGER_FORMAT_HEX_DEFAULT,
		variableName, sizeof(variableName))) {
		throw ParseException("Failed to generate internal variable name.",
			token.position);
	}

	InternalVariableID* id = new(std::nothrow) InternalVariableID(value);
	if (id == NULL) {
		throw ParseException("Out of memory while creating ID object.",
			token.position);
	}

	BReference<ObjectID> idReference(id, true);
	Variable* variable = new(std::nothrow) Variable(id, variableName, type,
		location);
	if (variable == NULL) {
		throw ParseException("Out of memory while creating variable object.",
			token.position);
	}

	BReference<Variable> variableReference(variable, true);
	_output = new(std::nothrow) VariableValueNodeChild(
		variable);
	if (_output == NULL) {
		throw ParseException("Out of memory while creating node child object.",
			token.position);
	}
}
