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
 *   Copyright 2009-2012, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file CompoundValueNode.cpp
 * @brief Implementation of CompoundValueNode -- renders structs/classes/unions in the variables view.
 *
 * Children come in two flavours: BaseTypeChild (one per inherited base class)
 * and MemberChild (one per data member). Both delegate to the CompoundType
 * to compute their location relative to the parent struct's address.
 *
 * @see CompoundType, ValueNode
 */


#include "CompoundValueNode.h"

#include <new>

#include "Architecture.h"
#include "IntegerValue.h"
#include "Tracing.h"
#include "Type.h"
#include "ValueLoader.h"
#include "ValueLocation.h"
#include "ValueNodeContainer.h"


// #pragma mark - Child


/**
 * @brief Common base for the two compound child kinds: holds parent + display name.
 */
class CompoundValueNode::Child : public ValueNodeChild {
public:
	/**
	 * @brief Constructs the child with a parent back-pointer and display name.
	 *
	 * @param parent  Owning CompoundValueNode.
	 * @param name    Display name.
	 */
	Child(CompoundValueNode* parent, const BString& name)
		:
		fParent(parent),
		fName(name)
	{
	}

	/**
	 * @brief Returns the cached display name.
	 *
	 * @return Reference to the name string.
	 */
	virtual const BString& Name() const
	{
		return fName;
	}

	/**
	 * @brief Returns the owning CompoundValueNode.
	 *
	 * @return The parent node.
	 */
	virtual ValueNode* Parent() const
	{
		return fParent;
	}

protected:
	CompoundValueNode*	fParent;
	BString				fName;
};


// #pragma mark - BaseTypeChild


/**
 * @brief Compound child wrapping an inherited base class subobject.
 *
 * Display name is the base class's type name. ResolveLocation() asks the
 * CompoundType to compute the base-subobject offset within the parent.
 */
class CompoundValueNode::BaseTypeChild : public Child {
public:
	/**
	 * @brief Constructs the base-class subobject child.
	 *
	 * @param parent    Owning CompoundValueNode.
	 * @param baseType  BaseType description from DWARF.
	 */
	BaseTypeChild(CompoundValueNode* parent, BaseType* baseType)
		:
		Child(parent, baseType->GetType()->Name()),
		fBaseType(baseType)
	{
		fBaseType->AcquireReference();
	}

	/**
	 * @brief Releases the reference held on the BaseType.
	 */
	virtual ~BaseTypeChild()
	{
		fBaseType->ReleaseReference();
	}

	/**
	 * @brief Returns the inherited base class's type.
	 *
	 * @return The base class Type.
	 */
	virtual Type* GetType() const
	{
		return fBaseType->GetType();
	}

	/**
	 * @brief Computes the location of the base-class subobject within the parent.
	 *
	 * @param valueLoader  Unused.
	 * @param _location    Set to the resolved location on success.
	 * @retval B_OK         On success.
	 * @retval B_BAD_VALUE  When the parent location is missing.
	 * @return Other status_t propagated from CompoundType::ResolveBaseTypeLocation().
	 */
	virtual status_t ResolveLocation(ValueLoader* valueLoader,
		ValueLocation*& _location)
	{
		// The parent's location refers to the location of the complete
		// object. We want to extract the location of a member.
		ValueLocation* parentLocation = fParent->Location();
		if (parentLocation == NULL)
			return B_BAD_VALUE;

		ValueLocation* location;
		status_t error = fParent->fType->ResolveBaseTypeLocation(fBaseType,
			*parentLocation, location);
		if (error != B_OK) {
			TRACE_LOCALS("CompoundValueNode::BaseTypeChild::ResolveLocation(): "
				"ResolveBaseTypeLocation() failed: %s\n", strerror(error));
			return error;
		}

		_location = location;
		return B_OK;
	}

private:
	BaseType*	fBaseType;
};


// #pragma mark - MemberChild


/**
 * @brief Compound child wrapping a single named data member.
 */
class CompoundValueNode::MemberChild : public Child {
public:
	/**
	 * @brief Constructs the data-member child.
	 *
	 * @param parent  Owning CompoundValueNode.
	 * @param member  DataMember description from DWARF.
	 */
	MemberChild(CompoundValueNode* parent, DataMember* member)
		:
		Child(parent, member->Name()),
		fMember(member)
	{
		fMember->AcquireReference();
	}

	/**
	 * @brief Releases the reference held on the DataMember.
	 */
	virtual ~MemberChild()
	{
		fMember->ReleaseReference();
	}

	/**
	 * @brief Returns the data member's declared type.
	 *
	 * @return The member's Type.
	 */
	virtual Type* GetType() const
	{
		return fMember->GetType();
	}

	/**
	 * @brief Computes the location of this data member within the parent.
	 *
	 * @param valueLoader  Unused.
	 * @param _location    Set to the resolved location on success.
	 * @retval B_OK         On success.
	 * @retval B_BAD_VALUE  When the parent location is missing.
	 * @return Other status_t propagated from CompoundType::ResolveDataMemberLocation().
	 */
	virtual status_t ResolveLocation(ValueLoader* valueLoader,
		ValueLocation*& _location)
	{
		// The parent's location refers to the location of the complete
		// object. We want to extract the location of a member.
		ValueLocation* parentLocation = fParent->Location();
		if (parentLocation == NULL)
			return B_BAD_VALUE;

		ValueLocation* location;
		status_t error = fParent->fType->ResolveDataMemberLocation(fMember,
			*parentLocation, location);
		if (error != B_OK) {
			TRACE_LOCALS("CompoundValueNode::MemberChild::ResolveLocation(): "
				"ResolveDataMemberLocation() failed: %s\n", strerror(error));
			return error;
		}

		_location = location;
		return B_OK;
	}

private:
	DataMember*	fMember;
};


// #pragma mark - CompoundValueNode


/**
 * @brief Constructs the node and references the CompoundType.
 *
 * @param nodeChild  Child this node renders for.
 * @param type       Compound (struct/class/union) DWARF type.
 */
CompoundValueNode::CompoundValueNode(ValueNodeChild* nodeChild,
	CompoundType* type)
	:
	ValueNode(nodeChild),
	fType(type)
{
	fType->AcquireReference();
}


/**
 * @brief Releases the type and every materialised child.
 */
CompoundValueNode::~CompoundValueNode()
{
	fType->ReleaseReference();

	for (int32 i = 0; Child* child = fChildren.ItemAt(i); i++)
		child->ReleaseReference();
}


/**
 * @brief Returns the wrapped CompoundType.
 *
 * @return The DWARF compound type.
 */
Type*
CompoundValueNode::GetType() const
{
	return fType;
}


/**
 * @brief Returns the parent location verbatim; compounds have no scalar value.
 *
 * @param valueLoader  Unused.
 * @param _location    Receives a re-referenced copy of the parent location.
 * @param _value       Always set to NULL.
 * @retval B_OK         On success.
 * @retval B_BAD_VALUE  When the parent location is missing.
 */
status_t
CompoundValueNode::ResolvedLocationAndValue(ValueLoader* valueLoader,
	ValueLocation*& _location, Value*& _value)
{
	// get the location
	ValueLocation* location = NodeChild()->Location();
	if (location == NULL)
		return B_BAD_VALUE;

	location->AcquireReference();
	_location = location;
	_value = NULL;
	return B_OK;
}


/**
 * @brief Materialises one BaseTypeChild per inherited base, then one MemberChild per data member.
 *
 * Idempotent: a second call with children already present is a no-op.
 *
 * @param info  Unused.
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  On allocation failure.
 */
status_t
CompoundValueNode::CreateChildren(TeamTypeInformation* info)
{
	if (!fChildren.IsEmpty())
		return B_OK;

	// base types
	for (int32 i = 0; BaseType* baseType = fType->BaseTypeAt(i); i++) {
		TRACE_LOCALS("  base %" B_PRId32 "\n", i);

		BaseTypeChild* child = new(std::nothrow) BaseTypeChild(this, baseType);
		if (child == NULL || !fChildren.AddItem(child)) {
			delete child;
			return B_NO_MEMORY;
		}

		child->SetContainer(fContainer);
	}

	// members
	for (int32 i = 0; DataMember* member = fType->DataMemberAt(i); i++) {
		TRACE_LOCALS("  member %" B_PRId32 ": \"%s\"\n", i, member->Name());

		MemberChild* child = new(std::nothrow) MemberChild(this, member);
		if (child == NULL || !fChildren.AddItem(child)) {
			delete child;
			return B_NO_MEMORY;
		}

		child->SetContainer(fContainer);
	}

	if (fContainer != NULL)
		fContainer->NotifyValueNodeChildrenCreated(this);

	return B_OK;
}


/**
 * @brief Returns the number of currently materialised children.
 *
 * @return Count of children (bases + members).
 */
int32
CompoundValueNode::CountChildren() const
{
	return fChildren.CountItems();
}


/**
 * @brief Returns the child at @a index, or NULL if out of range.
 *
 * @param index  Zero-based index.
 * @return The child reference, or NULL.
 */
ValueNodeChild*
CompoundValueNode::ChildAt(int32 index) const
{
	return fChildren.ItemAt(index);
}
