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
 *   Copyright 2010-2018, Rene Gollent, rene@gollent.com
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file CStringTypeHandler.cpp
 * @brief TypeHandler that detects C strings (pointers/arrays of int8/uint8) and yields CStringValueNode.
 *
 * Looks through pointer and one-dimensional array types for an underlying
 * 8-bit byte primitive (B_INT8_TYPE/B_UINT8_TYPE), unwinding typedefs/modifiers
 * along the way. Returns a 0.8 score so it outranks the generic Address/Array
 * handlers (0.5) but still leaves room for an even more specialised future
 * handler.
 *
 * @see CStringValueNode, TypeHandlerRoster
 */


#include "CStringTypeHandler.h"

#include <new>

#include <stdio.h>

#include "CStringValueNode.h"
#include "Type.h"


/**
 * @brief Trivial destructor.
 */
CStringTypeHandler::~CStringTypeHandler()
{
}


/**
 * @brief Returns the user-visible handler name shown in the variables view menu.
 *
 * @return The literal "String".
 */
const char*
CStringTypeHandler::Name() const
{
	return "String";
}


/**
 * @brief Reports support score by checking if @a type is a pointer/array to int8/uint8.
 *
 * Walks through Address-of-pointer and 1-D Array types, unwinding any
 * intervening typedef/modifier wrappers, until it finds the underlying
 * primitive type. A B_INT8_TYPE or B_UINT8_TYPE primitive yields 0.8.
 *
 * @param type  Type to score.
 * @return 0.8 when @a type names a C string, 0.0 otherwise.
 */
float
CStringTypeHandler::SupportsType(Type* type) const
{
	AddressType* addressType = dynamic_cast<AddressType*>(type);
	ArrayType* arrayType = dynamic_cast<ArrayType*>(type);
	PrimitiveType* baseType = NULL;
	ModifiedType* modifiedType = NULL;
	if (addressType != NULL && addressType->AddressKind()
		== DERIVED_TYPE_POINTER) {
			baseType = dynamic_cast<PrimitiveType*>(
				addressType->BaseType());
		if (baseType == NULL) {
			modifiedType = dynamic_cast<ModifiedType*>(
				addressType->BaseType());
		}
	} else if (arrayType != NULL && arrayType->CountDimensions() == 1) {
		baseType = dynamic_cast<PrimitiveType*>(
				arrayType->BaseType());
		if (baseType == NULL) {
			modifiedType = dynamic_cast<ModifiedType*>(
				arrayType->BaseType());
		}
	}

	if (baseType == NULL && modifiedType == NULL)
		return 0.0f;
	else if (modifiedType != NULL) {
		baseType = dynamic_cast<PrimitiveType*>(
			modifiedType->ResolveRawType(false));
		if (baseType == NULL)
			return 0.0f;
	}

	if (baseType->TypeConstant() == B_UINT8_TYPE
		|| baseType->TypeConstant() == B_INT8_TYPE)
		return 0.8f;

	return 0.0f;
}


/**
 * @brief Allocates a CStringValueNode for @a nodeChild.
 *
 * @param nodeChild  Child the node will render for.
 * @param type       Pointer-to-byte or array-of-byte type.
 * @param _node      Set to the freshly allocated node on success.
 * @retval B_OK         On success.
 * @retval B_BAD_VALUE  When SupportsType() rejects @a type.
 * @retval B_NO_MEMORY  On allocation failure.
 */
status_t
CStringTypeHandler::CreateValueNode(ValueNodeChild* nodeChild, Type* type,
	ValueNode*& _node)
{
	if (SupportsType(type) == 0.0f)
		return B_BAD_VALUE;

	ValueNode* node = new(std::nothrow) CStringValueNode(nodeChild,
		type);

	if (node == NULL)
		return B_NO_MEMORY;

	_node = node;

	return B_OK;
}
