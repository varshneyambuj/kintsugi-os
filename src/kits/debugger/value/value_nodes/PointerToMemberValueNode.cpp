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
 * @file PointerToMemberValueNode.cpp
 * @brief Implementation of PointerToMemberValueNode -- renders C++ "pointer to member" values.
 *
 * Pointer-to-member values are stored as integers (the offset/index of the
 * member); this node loads them as a uint32 or uint64 depending on the
 * target's address size and wraps the result in an IntegerValue. The node is
 * childless.
 *
 * @see PointerToMemberType, IntegerValue
 */


#include "PointerToMemberValueNode.h"

#include <new>

#include "Architecture.h"
#include "IntegerValue.h"
#include "Tracing.h"
#include "Type.h"
#include "ValueLoader.h"
#include "ValueLocation.h"


/**
 * @brief Constructs the node and references the PointerToMemberType.
 *
 * @param nodeChild  Child this node renders for.
 * @param type       DWARF pointer-to-member type description.
 */
PointerToMemberValueNode::PointerToMemberValueNode(ValueNodeChild* nodeChild,
	PointerToMemberType* type)
	:
	ChildlessValueNode(nodeChild),
	fType(type)
{
	fType->AcquireReference();
}


/**
 * @brief Releases the reference held on the PointerToMemberType.
 */
PointerToMemberValueNode::~PointerToMemberValueNode()
{
	fType->ReleaseReference();
}


/**
 * @brief Returns the wrapped PointerToMemberType.
 *
 * @return The DWARF type.
 */
Type*
PointerToMemberValueNode::GetType() const
{
	return fType;
}


/**
 * @brief Loads the pointer-to-member integer payload as an IntegerValue.
 *
 * @param valueLoader  Loader used to read target memory.
 * @param _location    Receives a re-referenced copy of the parent location.
 * @param _value       Set to a freshly allocated IntegerValue on success.
 * @retval B_OK         On success.
 * @retval B_BAD_VALUE  When the parent location is missing.
 * @retval B_NO_MEMORY  On allocation failure.
 * @return Other status_t propagated from the loader.
 */
status_t
PointerToMemberValueNode::ResolvedLocationAndValue(ValueLoader* valueLoader,
	ValueLocation*& _location, Value*& _value)
{
	// get the location
	ValueLocation* location = NodeChild()->Location();
	if (location == NULL)
		return B_BAD_VALUE;

	TRACE_LOCALS("  TYPE_POINTER_TO_MEMBER\n");

	// get the value type
	type_code valueType;
	if (valueLoader->GetArchitecture()->AddressSize() == 4) {
		valueType = B_UINT32_TYPE;
		TRACE_LOCALS("    -> 32 bit\n");
	} else {
		valueType = B_UINT64_TYPE;
		TRACE_LOCALS("    -> 64 bit\n");
	}

	// load the value data
	BVariant valueData;
	status_t error = valueLoader->LoadValue(location, valueType, false,
		valueData);
	if (error != B_OK)
		return error;

	// create the type object
	Value* value = new(std::nothrow) IntegerValue(valueData);
	if (value == NULL)
		return B_NO_MEMORY;

	location->AcquireReference();
	_location = location;
	_value = value;
	return B_OK;
}
