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
 * @file EnumerationValueNode.cpp
 * @brief Implementation of EnumerationValueNode -- renders an enum-typed variable.
 *
 * Loads the enumerator's underlying integer payload using either the
 * declared base-type or, failing that, a width-based fallback (1/2/4/8 bytes
 * map to int8/int16/int32/int64). The node is childless; rendering happens
 * via EnumerationValueFormatter which substitutes enumerator names.
 *
 * @see EnumerationValue, EnumerationValueFormatter
 */


#include "EnumerationValueNode.h"

#include <new>

#include "EnumerationValue.h"
#include "Tracing.h"
#include "Type.h"
#include "ValueLoader.h"
#include "ValueLocation.h"


/**
 * @brief Constructs the node and references its EnumerationType.
 *
 * @param nodeChild  Child this node renders for.
 * @param type       DWARF enumeration type description.
 */
EnumerationValueNode::EnumerationValueNode(ValueNodeChild* nodeChild,
	EnumerationType* type)
	:
	ChildlessValueNode(nodeChild),
	fType(type)
{
	fType->AcquireReference();
}


/**
 * @brief Releases the reference held on the EnumerationType.
 */
EnumerationValueNode::~EnumerationValueNode()
{
	fType->ReleaseReference();
}


/**
 * @brief Returns the wrapped EnumerationType.
 *
 * @return The DWARF enumeration type.
 */
Type*
EnumerationValueNode::GetType() const
{
	return fType;
}


/**
 * @brief Loads the enum's integer payload and wraps it in an EnumerationValue.
 *
 * Picks the @c valueType from the declared base type when it is a primitive
 * integer; otherwise falls back to choosing by enum byte-size. The chosen
 * type is C/C++ specific.
 *
 * @param valueLoader  Loader used to read target memory.
 * @param _location    Receives a re-referenced copy of the parent location.
 * @param _value       Set to a freshly allocated EnumerationValue on success.
 * @retval B_OK         On success.
 * @retval B_BAD_VALUE  When the parent location is missing.
 * @retval B_NO_MEMORY  On allocation failure.
 * @return Other status_t propagated from the loader.
 * @todo  Width-based fallback assumes C/C++ size semantics.
 */
status_t
EnumerationValueNode::ResolvedLocationAndValue(ValueLoader* valueLoader,
	ValueLocation*& _location, Value*& _value)
{
	// get the location
	ValueLocation* location = NodeChild()->Location();
	if (location == NULL)
		return B_BAD_VALUE;

	TRACE_LOCALS("  TYPE_ENUMERATION\n");

	// get the value type
	type_code valueType = 0;

	// If a base type is known, try that.
	if (PrimitiveType* baseType = dynamic_cast<PrimitiveType*>(
			fType->BaseType())) {
		valueType = baseType->TypeConstant();
		if (!BVariant::TypeIsInteger(valueType))
			valueType = 0;
	}

	// If we don't have a value type yet, guess it from the type size.
	if (valueType == 0) {
		// TODO: This is C source language specific!
		switch (fType->ByteSize()) {
			case 1:
				valueType = B_INT8_TYPE;
				break;
			case 2:
				valueType = B_INT16_TYPE;
				break;
			case 4:
			default:
				valueType = B_INT32_TYPE;
				break;
			case 8:
				valueType = B_INT64_TYPE;
				break;
		}
	}

	// load the value data
	BVariant valueData;
	status_t error = valueLoader->LoadValue(location, valueType, true,
		valueData);
	if (error != B_OK)
		return error;

	// create the type object
	Value* value = new(std::nothrow) EnumerationValue(fType, valueData);
	if (value == NULL)
		return B_NO_MEMORY;

	location->AcquireReference();
	_location = location;
	_value = value;
	return B_OK;
}
