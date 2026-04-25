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
 *   Copyright 2010, Rene Gollent, rene@gollent.com
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file CStringValueNode.cpp
 * @brief Implementation of CStringValueNode -- renders char, int8, and uint8 pointers (and 1-D arrays of these) as strings.
 *
 * For pointer types, the node first reads the pointer's bytes to obtain the
 * address; for array types, it uses the array's start address directly and
 * caps the read at the declared dimension. The string is then read with
 * ValueLoader::LoadStringValue() and wrapped in a StringValue.
 *
 * @see CStringTypeHandler, StringValue
 */


#include "CStringValueNode.h"

#include <new>

#include "Architecture.h"
#include "StringValue.h"
#include "Tracing.h"
#include "Type.h"
#include "ValueLoader.h"
#include "ValueLocation.h"
#include "ValueNodeContainer.h"


// #pragma mark - CStringValueNode


/**
 * @brief Constructs the node and references the underlying type.
 *
 * @param nodeChild  Child this node renders for.
 * @param type       Pointer-to-byte or array-of-byte type recognised by CStringTypeHandler.
 */
CStringValueNode::CStringValueNode(ValueNodeChild* nodeChild,
	Type* type)
	:
	ChildlessValueNode(nodeChild),
	fType(type)
{
	fType->AcquireReference();
}


/**
 * @brief Releases the reference held on the type.
 */
CStringValueNode::~CStringValueNode()
{
	fType->ReleaseReference();
}


/**
 * @brief Returns the wrapped DWARF type.
 *
 * @return The pointer/array type.
 */
Type*
CStringValueNode::GetType() const
{
	return fType;
}


/**
 * @brief Reads the target string and produces a StringValue plus a single-piece location.
 *
 * For pointer-typed nodes it loads the pointer first; for array-typed nodes
 * it uses the array's start address and caps the read at the declared
 * element count. The maximum bytes read is also bounded by ValueLoader's
 * own kMaxStringSize (255).
 *
 * @param valueLoader  Loader used to read target memory.
 * @param _location    Set to a freshly allocated single-piece location whose
 *                     size matches the string length actually read.
 * @param _value       Set to a freshly allocated StringValue on success.
 * @retval B_OK         On success.
 * @retval B_BAD_VALUE  When the parent location is missing.
 * @retval B_NO_MEMORY  On allocation failure.
 * @return Other status_t propagated from the loader.
 */
status_t
CStringValueNode::ResolvedLocationAndValue(ValueLoader* valueLoader,
	ValueLocation*& _location, Value*& _value)
{
	// get the location
	ValueLocation* location = NodeChild()->Location();
	if (location == NULL)
		return B_BAD_VALUE;

	TRACE_LOCALS("  TYPE_ADDRESS (C string)\n");

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

	BVariant addressData;
	BString valueData;
	status_t error = B_OK;
	size_t maxSize = 255;
	if (dynamic_cast<AddressType*>(fType) != NULL) {
		error = valueLoader->LoadValue(location, valueType, false,
			addressData);
		if (error != B_OK)
			return error;
	} else {
		addressData.SetTo(location->PieceAt(0).address);
		maxSize = dynamic_cast<ArrayType*>(fType)
			->DimensionAt(0)->CountElements();
	}

	ValuePieceLocation piece;
	piece.SetToMemory(addressData.ToUInt64());

	TRACE_LOCALS("    Address found: %#" B_PRIx64 "\n",
		addressData.ToUInt64());

	error = valueLoader->LoadStringValue(addressData, maxSize, valueData);
	if (error != B_OK)
		return error;

	piece.size = valueData.Length();

	TRACE_LOCALS("    String value found, length: %" B_PRIu64 "bytes\n",
		piece.size);

	ValueLocation* stringLocation = new(std::nothrow) ValueLocation(
		valueLoader->GetArchitecture()->IsBigEndian(), piece);

	if (stringLocation == NULL)
		return B_NO_MEMORY;

	BReference<ValueLocation> locationReference(stringLocation, true);

	error = valueLoader->LoadStringValue(addressData, maxSize, valueData);
	if (error != B_OK)
		return error;

	// create the type object
	Value* value = new(std::nothrow) StringValue(valueData);
	if (value == NULL)
		return B_NO_MEMORY;

	NodeChild()->SetLocation(stringLocation, B_OK);
	_location = locationReference.Detach();
	_value = value;
	return B_OK;
}
