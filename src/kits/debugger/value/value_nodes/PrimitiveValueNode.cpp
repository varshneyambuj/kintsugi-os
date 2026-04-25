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
 * @file PrimitiveValueNode.cpp
 * @brief Implementation of PrimitiveValueNode -- renders bool, integer, and float primitives.
 *
 * Picks the right Value subclass for the type code: B_BOOL_TYPE -> BoolValue,
 * any integer type code -> IntegerValue, any float type code -> FloatValue.
 * The node is childless.
 *
 * @see PrimitiveType, BoolValue, IntegerValue, FloatValue
 */


#include "PrimitiveValueNode.h"

#include <new>

#include "BoolValue.h"
#include "FloatValue.h"
#include "IntegerValue.h"
#include "Tracing.h"
#include "Type.h"
#include "ValueLoader.h"
#include "ValueLocation.h"


/**
 * @brief Constructs the node and references its PrimitiveType.
 *
 * @param nodeChild  Child this node renders for.
 * @param type       DWARF primitive type description.
 */
PrimitiveValueNode::PrimitiveValueNode(ValueNodeChild* nodeChild,
	PrimitiveType* type)
	:
	ChildlessValueNode(nodeChild),
	fType(type)
{
	fType->AcquireReference();
}


/**
 * @brief Releases the reference held on the type.
 */
PrimitiveValueNode::~PrimitiveValueNode()
{
	fType->ReleaseReference();
}


/**
 * @brief Returns the wrapped PrimitiveType.
 *
 * @return The DWARF primitive type.
 */
Type*
PrimitiveValueNode::GetType() const
{
	return fType;
}


/**
 * @brief Loads the primitive's bytes and wraps them in the appropriate Value subclass.
 *
 * Integer and bool reads tolerate short values (the bit-buffer pads with
 * zeros); float reads do not. Bool yields BoolValue, integer-typed values
 * yield IntegerValue, and float/double yield FloatValue.
 *
 * @param valueLoader  Loader used to read target memory.
 * @param _location    Receives a re-referenced copy of the parent location.
 * @param _value       Set to a freshly allocated Value on success.
 * @retval B_OK           On success.
 * @retval B_BAD_VALUE    When the parent location is missing.
 * @retval B_NO_MEMORY    On allocation failure.
 * @retval B_UNSUPPORTED  When the type code is neither numeric nor B_BOOL_TYPE.
 * @return Other status_t propagated from the loader.
 */
status_t
PrimitiveValueNode::ResolvedLocationAndValue(ValueLoader* valueLoader,
	ValueLocation*& _location, Value*& _value)
{
	// get the location
	ValueLocation* location = NodeChild()->Location();
	if (location == NULL)
		return B_BAD_VALUE;

	// get the value type
	type_code valueType = fType->TypeConstant();
	if (!BVariant::TypeIsNumber(valueType) && valueType != B_BOOL_TYPE) {
		TRACE_LOCALS("  -> unknown type constant\n");
		return B_UNSUPPORTED;
	}

	bool shortValueIsFine = BVariant::TypeIsInteger(valueType)
		|| valueType == B_BOOL_TYPE;

	TRACE_LOCALS("  TYPE_PRIMITIVE: '%c%c%c%c'\n",
		int(valueType >> 24), int(valueType >> 16),
		int(valueType >> 8), int(valueType));

	// load the value data
	BVariant valueData;
	status_t error = valueLoader->LoadValue(location, valueType,
		shortValueIsFine, valueData);
	if (error != B_OK)
		return error;

	// create the type object
	Value* value;
	if (valueType == B_BOOL_TYPE)
		value = new(std::nothrow) BoolValue(valueData.ToBool());
	else if (BVariant::TypeIsInteger(valueType))
		value = new(std::nothrow) IntegerValue(valueData);
	else if (BVariant::TypeIsFloat(valueType))
		value = new(std::nothrow) FloatValue(valueData);
	else
		return B_UNSUPPORTED;

	if (value == NULL)
		return B_NO_MEMORY;

	location->AcquireReference();
	_location = location;
	_value = value;
	return B_OK;
}
