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
 *   Copyright 2015, Rene Gollent, rene@gollent.com.
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file AddressValueNode.cpp
 * @brief Implementation of AddressValueNode and its dereference child.
 *
 * Renders any address-typed variable (pointers, references, function
 * pointers) as an AddressValue (hex). Non-function-pointer nodes also expose
 * a single dereference child named "*name" so the user can chase the pointer
 * one level deeper; function pointers do not expand because the printed
 * address already names the target instruction.
 *
 * @see AddressValue, ValueNode
 */


#include "AddressValueNode.h"

#include <new>

#include "AddressValue.h"
#include "Architecture.h"
#include "Tracing.h"
#include "Type.h"
#include "ValueLoader.h"
#include "ValueLocation.h"
#include "ValueNodeContainer.h"


// #pragma mark - AddressValueNode


/**
 * @brief Constructs the node and references its AddressType.
 *
 * @param nodeChild  Child this node renders for.
 * @param type       DWARF address (pointer/reference) type.
 */
AddressValueNode::AddressValueNode(ValueNodeChild* nodeChild,
	AddressType* type)
	:
	ValueNode(nodeChild),
	fType(type),
	fChild(NULL)
{
	fType->AcquireReference();
}


/**
 * @brief Releases the dereference child (if any) and the type.
 */
AddressValueNode::~AddressValueNode()
{
	if (fChild != NULL)
		fChild->ReleaseReference();
	fType->ReleaseReference();
}


/**
 * @brief Returns the wrapped AddressType.
 *
 * @return The DWARF address type.
 */
Type*
AddressValueNode::GetType() const
{
	return fType;
}


/**
 * @brief Loads the pointer's bytes and wraps them in an AddressValue.
 *
 * @param valueLoader  Loader used to read target memory.
 * @param _location    Receives a re-referenced copy of the parent location.
 * @param _value       Set to a freshly allocated AddressValue on success.
 * @retval B_OK         On success.
 * @retval B_BAD_VALUE  When the parent location is missing.
 * @retval B_NO_MEMORY  On allocation failure.
 * @return Other status_t propagated from the loader.
 */
status_t
AddressValueNode::ResolvedLocationAndValue(ValueLoader* valueLoader,
	ValueLocation*& _location, Value*& _value)
{
	// get the location
	ValueLocation* location = NodeChild()->Location();
	if (location == NULL)
		return B_BAD_VALUE;

	TRACE_LOCALS("  TYPE_ADDRESS\n");

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
	Value* value = new(std::nothrow) AddressValue(valueData);
	if (value == NULL)
		return B_NO_MEMORY;

	location->AcquireReference();
	_location = location;
	_value = value;
	return B_OK;
}


/**
 * @brief Lazily creates the single dereference child named "*name".
 *
 * For function-pointer types this is a no-op: there is no useful payload to
 * show beyond the address itself (which is already the function's
 * instruction pointer).
 *
 * @param info  Unused.
 * @retval B_OK         On success or when no child is needed.
 * @retval B_NO_MEMORY  On allocation failure.
 * @todo An eventual future possibility might be for a child node to indicate
 *       the name of the function being pointed to, if the target address is
 *       valid.
 */
status_t
AddressValueNode::CreateChildren(TeamTypeInformation* info)
{
	if (fChild != NULL)
		return B_OK;

	// For function pointers, don't bother creating a child, as there
	// currently isn't any useful information that can be presented there,
	// and the address node's value already indicates the instruction pointer
	// of the target function.
	// TODO: an eventual future possibility might be for a child node to
	// indicate the name of the function being pointed to, if target address
	// is valid.
	Type* baseType = fType->BaseType();
	if (baseType != NULL && baseType->Kind() == TYPE_FUNCTION)
		return B_OK;

	// construct name
	BString name = "*";
	name << Name();

	// create the child
	fChild = new(std::nothrow) AddressValueNodeChild(this, name,
		baseType);
	if (fChild == NULL)
		return B_NO_MEMORY;

	fChild->SetContainer(fContainer);

	if (fContainer != NULL)
		fContainer->NotifyValueNodeChildrenCreated(this);

	return B_OK;
}


/**
 * @brief Reports 1 when a dereference child exists, 0 otherwise.
 *
 * @return 0 or 1.
 */
int32
AddressValueNode::CountChildren() const
{
	return fChild != NULL ? 1 : 0;
}


/**
 * @brief Returns the single dereference child or NULL.
 *
 * @param index  Must be 0 to obtain the child.
 * @return The child, or NULL when @a index != 0 or no child was created.
 */
ValueNodeChild*
AddressValueNode::ChildAt(int32 index) const
{
	return index == 0 ? fChild : NULL;
}


// #pragma mark - AddressValueNodeChild


/**
 * @brief Constructs the dereference child for an AddressValueNode.
 *
 * @param parent  Owning AddressValueNode.
 * @param name    Display name (typically "*originalName").
 * @param type    Pointee type.
 */
AddressValueNodeChild::AddressValueNodeChild(AddressValueNode* parent,
	const BString& name, Type* type)
	:
	fParent(parent),
	fName(name),
	fType(type)
{
	fType->AcquireReference();
}


/**
 * @brief Releases the reference held on the pointee type.
 */
AddressValueNodeChild::~AddressValueNodeChild()
{
	fType->ReleaseReference();
}


/**
 * @brief Returns the dereference child's display name.
 *
 * @return Reference to the cached name.
 */
const BString&
AddressValueNodeChild::Name() const
{
	return fName;
}


/**
 * @brief Returns the pointee type.
 *
 * @return The pointee Type.
 */
Type*
AddressValueNodeChild::GetType() const
{
	return fType;
}


/**
 * @brief Returns the owning AddressValueNode.
 *
 * @return The parent node.
 */
ValueNode*
AddressValueNodeChild::Parent() const
{
	return fParent;
}


/**
 * @brief Computes the location of the pointee using the parent's address.
 *
 * @param valueLoader  Loader (passed through to the type system).
 * @param _location    Set to the resolved location on success.
 * @retval B_OK         On success.
 * @retval B_BAD_VALUE  When the parent's value is not an AddressValue.
 * @return Other status_t propagated from Type::ResolveObjectDataLocation().
 */
status_t
AddressValueNodeChild::ResolveLocation(ValueLoader* valueLoader,
	ValueLocation*& _location)
{
	// The parent's value is an address pointing to this component.
	AddressValue* parentValue = dynamic_cast<AddressValue*>(
		fParent->GetValue());
	if (parentValue == NULL)
		return B_BAD_VALUE;

	// resolve the location
	ValueLocation* location;
	status_t error = fType->ResolveObjectDataLocation(parentValue->ToUInt64(),
		location);
	if (error != B_OK) {
		TRACE_LOCALS("AddressValueNodeChild::ResolveLocation(): "
			"ResolveObjectDataLocation() failed: %s\n", strerror(error));
		return error;
	}

	_location = location;
	return B_OK;
}
