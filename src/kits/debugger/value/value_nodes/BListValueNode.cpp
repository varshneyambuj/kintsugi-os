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
 *   Copyright 2012-2015, Rene Gollent, rene@gollent.com
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file BListValueNode.cpp
 * @brief Implementation of BListValueNode -- renders a BList/BObjectList in the variables view.
 *
 * Resolves the BList's @c fObjectList pointer, @c fItemCount, and (for
 * BObjectList) the templated element type, then exposes a "Capacity" pseudo
 * child plus one indexed element child per real list slot. Children are
 * created in user-controlled ranges so very long lists do not deluge the UI.
 *
 * @see BListTypeHandler, ValueNode
 */


#include "BListValueNode.h"

#include <new>

#include <AutoDeleter.h>

#include "AddressValueNode.h"
#include "Architecture.h"
#include "StringValue.h"
#include "TeamTypeInformation.h"
#include "Tracing.h"
#include "Type.h"
#include "TypeLookupConstraints.h"
#include "ValueLoader.h"
#include "ValueLocation.h"
#include "ValueNodeContainer.h"


/** @brief Default upper bound on the number of element children created on first expand. */
static const int64 kMaxArrayElementCount = 20;


//#pragma mark - BListValueNode::BListElementNodeChild


/**
 * @brief Internal child representing one slot of a BList/BObjectList.
 *
 * Holds a back-pointer to the parent BListValueNode, the slot index, and the
 * element's type. ResolveLocation() computes the address as
 * @c parent->fDataLocation + index * addressSize.
 */
class BListValueNode::BListElementNodeChild : public ValueNodeChild {
public:
								BListElementNodeChild(BListValueNode* parent,
									int64 elementIndex, Type* type);
	virtual						~BListElementNodeChild();

	virtual	const BString&		Name() const { return fName; };
	virtual	Type*				GetType() const { return fType; };
	virtual	ValueNode*			Parent() const { return fParent; };

	virtual status_t			ResolveLocation(ValueLoader* valueLoader,
									ValueLocation*& _location);

private:
	Type*						fType;
	BListValueNode*				fParent;
	int64						fElementIndex;
	BString						fName;
};


/**
 * @brief Constructs an element child bound to a slot index.
 *
 * @param parent        Owning BListValueNode.
 * @param elementIndex  Zero-based slot index in the BList.
 * @param type          Element type for this slot.
 */
BListValueNode::BListElementNodeChild::BListElementNodeChild(
	BListValueNode* parent, int64 elementIndex, Type* type)
	:
	ValueNodeChild(),
	fType(type),
	fParent(parent),
	fElementIndex(elementIndex),
	fName()
{
	fType->AcquireReference();
	fParent->AcquireReference();
	fName.SetToFormat("[%" B_PRId64 "]", fElementIndex);
}


/**
 * @brief Releases the references held on the type and parent.
 */
BListValueNode::BListElementNodeChild::~BListElementNodeChild()
{
	fType->ReleaseReference();
	fParent->ReleaseReference();
}


/**
 * @brief Computes the in-target address of this list slot.
 *
 * @param valueLoader  Loader carrying the architecture's address size.
 * @param _location    Set to a freshly allocated single-piece memory ValueLocation.
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  On allocation failure.
 */
status_t
BListValueNode::BListElementNodeChild::ResolveLocation(
	ValueLoader* valueLoader, ValueLocation*& _location)
{
	uint8 addressSize = valueLoader->GetArchitecture()->AddressSize();
	ValueLocation* location = new(std::nothrow) ValueLocation();
	if (location == NULL)
		return B_NO_MEMORY;


	uint64 listAddress = fParent->fDataLocation.ToUInt64();
	listAddress += fElementIndex * addressSize;

	ValuePieceLocation piece;
	piece.SetToMemory(listAddress);
	piece.SetSize(addressSize);
	location->AddPiece(piece);

	_location = location;
	return B_OK;
}


//#pragma mark - BListItemCountNodeChild

/**
 * @brief Internal pseudo-child that exposes the BList's @c fItemCount field as "Capacity".
 *
 * Lets users inspect the list's logical size in the variables view without
 * expanding every element.
 */
class BListValueNode::BListItemCountNodeChild : public ValueNodeChild {
public:
								BListItemCountNodeChild(BVariant location,
									BListValueNode* parent, Type* type);
	virtual						~BListItemCountNodeChild();

	virtual	const BString&		Name() const { return fName; };
	virtual	Type*				GetType() const { return fType; };
	virtual	ValueNode*			Parent() const { return fParent; };

	virtual status_t			ResolveLocation(ValueLoader* valueLoader,
									ValueLocation*& _location);

private:
	Type*						fType;
	BListValueNode*				fParent;
	BVariant					fLocation;
	BString						fName;
};


/**
 * @brief Constructs the pseudo-child wrapping the BList's @c fItemCount field.
 *
 * @param location  Address of the @c fItemCount integer in the target.
 * @param parent    Owning BListValueNode.
 * @param type      Type of the @c fItemCount field.
 */
BListValueNode::BListItemCountNodeChild::BListItemCountNodeChild(
	BVariant location, BListValueNode* parent, Type* type)
	:
	ValueNodeChild(),
	fType(type),
	fParent(parent),
	fLocation(location),
	fName("Capacity")
{
	fType->AcquireReference();
	fParent->AcquireReference();
}


/**
 * @brief Releases the references held on the type and parent.
 */
BListValueNode::BListItemCountNodeChild::~BListItemCountNodeChild()
{
	fType->ReleaseReference();
	fParent->ReleaseReference();
}


/**
 * @brief Builds a single-piece location pointing at the cached @c fItemCount address.
 *
 * @param valueLoader  Unused.
 * @param _location    Set to a freshly allocated location on success.
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  On allocation failure.
 */
status_t
BListValueNode::BListItemCountNodeChild::ResolveLocation(
	ValueLoader* valueLoader, ValueLocation*& _location)
{
	ValueLocation* location = new(std::nothrow) ValueLocation();
	if (location == NULL)
		return B_NO_MEMORY;

	ValuePieceLocation piece;
	piece.SetToMemory(fLocation.ToUInt64());
	piece.SetSize(sizeof(int32));
	location->AddPiece(piece);

	_location = location;
	return B_OK;
}


//#pragma mark - BListValueNode

/**
 * @brief Constructs the BListValueNode and references its DWARF type.
 *
 * @param nodeChild  Child this node renders for.
 * @param type       Compound type for BList or a BObjectList instantiation.
 */
BListValueNode::BListValueNode(ValueNodeChild* nodeChild,
	Type* type)
	:
	ValueNode(nodeChild),
	fType(type),
	fItemCountType(NULL),
	fItemCount(0),
	fCountChildCreated(false)
{
	fType->AcquireReference();
}


/**
 * @brief Releases all element children, the type, and the cached count type.
 */
BListValueNode::~BListValueNode()
{
	fType->ReleaseReference();
	for (int32 i = 0; i < fChildren.CountItems(); i++)
		fChildren.ItemAt(i)->ReleaseReference();

	if (fItemCountType != NULL)
		fItemCountType->ReleaseReference();
}


/**
 * @brief Returns the wrapped DWARF type.
 *
 * @return The compound BList/BObjectList type.
 */
Type*
BListValueNode::GetType() const
{
	return fType;
}


/**
 * @brief Resolves the BList's internal pointers, count, and dispatch state.
 *
 * For BObjectList, walks the inheritance chain BObjectList -> _PointerList_
 * -> BList to reach the underlying BList members. Then iterates the BList's
 * data members and pulls @c fObjectList (into fDataLocation), @c fItemCount
 * (into fItemCount), caching their type and address for later use.
 *
 * @param valueLoader  Loader used to read target memory.
 * @param _location    Receives a re-referenced copy of the parent location.
 * @param _value       Always set to NULL -- this node has no scalar value.
 * @retval B_OK             On success.
 * @retval B_BAD_VALUE      When the parent location is missing.
 * @retval B_BAD_DATA       When the BObjectList hierarchy walk fails.
 * @return Other status_t propagated from the loader.
 */
status_t
BListValueNode::ResolvedLocationAndValue(ValueLoader* valueLoader,
	ValueLocation*& _location, Value*& _value)
{
	// get the location
	ValueLocation* location = NodeChild()->Location();
	if (location == NULL)
		return B_BAD_VALUE;


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

	status_t error = B_OK;

	ValueLocation* memberLocation = NULL;
	CompoundType* baseType = dynamic_cast<CompoundType*>(fType);

	if (baseType->CountTemplateParameters() != 0) {
		// for BObjectList we need to walk up
		// the hierarchy: BObjectList -> _PointerList_ -> BList
		if (baseType->CountBaseTypes() == 0)
			return B_BAD_DATA;

		baseType = dynamic_cast<CompoundType*>(baseType->BaseTypeAt(0)
			->GetType());
		if (baseType == NULL || baseType->Name() != "_PointerList_")
			return B_BAD_DATA;

		if (baseType->CountBaseTypes() == 0)
			return B_BAD_DATA;

		baseType = dynamic_cast<CompoundType*>(baseType->BaseTypeAt(0)
			->GetType());
		if (baseType == NULL || baseType->Name() != "BList")
			return B_BAD_DATA;

	}

	for (int32 i = 0; i < baseType->CountDataMembers(); i++) {
		DataMember* member = baseType->DataMemberAt(i);
		if (strcmp(member->Name(), "fObjectList") == 0) {
			error = baseType->ResolveDataMemberLocation(member,
				*location, memberLocation);
			BReference<ValueLocation> locationRef(memberLocation, true);
			if (error != B_OK) {
				TRACE_LOCALS(
					"BListValueNode::ResolvedLocationAndValue(): "
					"failed to resolve location of header member: %s\n",
					strerror(error));
				return error;
			}

			error = valueLoader->LoadValue(memberLocation, valueType,
				false, fDataLocation);
			if (error != B_OK)
				return error;
		} else if (strcmp(member->Name(), "fItemCount") == 0) {
			error = baseType->ResolveDataMemberLocation(member,
				*location, memberLocation);
			BReference<ValueLocation> locationRef(memberLocation, true);
			if (error != B_OK) {
				TRACE_LOCALS(
					"BListValueNode::ResolvedLocationAndValue(): "
					"failed to resolve location of header member: %s\n",
					strerror(error));
				return error;
			}

			fItemCountType = member->GetType();
			fItemCountType->AcquireReference();

			fItemCountLocation = memberLocation->PieceAt(0).address;

			BVariant listSize;
			error = valueLoader->LoadValue(memberLocation, B_INT32_TYPE,
				false, listSize);
			if (error != B_OK)
				return error;

			fItemCount = listSize.ToInt32();
			TRACE_LOCALS(
				"BListValueNode::ResolvedLocationAndValue(): "
				"detected list size %" B_PRId32 "\n",
				fItemCount);
		}
		memberLocation = NULL;
	}

	location->AcquireReference();
	_location = location;
	_value = NULL;

	return B_OK;
}


/**
 * @brief Initial-expand entry point: creates up to kMaxArrayElementCount slots.
 *
 * @param info  Type-information service for type lookups.
 * @return Status from CreateChildrenInRange().
 */
status_t
BListValueNode::CreateChildren(TeamTypeInformation* info)
{
	return CreateChildrenInRange(info, 0, kMaxArrayElementCount);
}


/**
 * @brief Returns the number of currently materialised children.
 *
 * @return Count including the "Capacity" pseudo child if present.
 */
int32
BListValueNode::CountChildren() const
{
	return fChildren.CountItems();
}


/**
 * @brief Returns the child at @a index, or NULL if out of range.
 *
 * @param index  Zero-based index into the children list.
 * @return The child reference, or NULL.
 */
ValueNodeChild*
BListValueNode::ChildAt(int32 index) const
{
	return fChildren.ItemAt(index);
}


/**
 * @brief Reports that this node hands out children in user-controlled ranges.
 *
 * @return true.
 */
bool
BListValueNode::IsRangedContainer() const
{
	return true;
}


/**
 * @brief Reports that the supported range is fixed by the BList's @c fItemCount.
 *
 * @return true.
 */
bool
BListValueNode::IsContainerRangeFixed() const
{
	return true;
}


/**
 * @brief Drops every materialised child and notifies listeners.
 */
void
BListValueNode::ClearChildren()
{
	fChildren.MakeEmpty();
	fCountChildCreated = false;
	if (fContainer != NULL)
		fContainer->NotifyValueNodeChildrenDeleted(this);
}


/**
 * @brief Materialises element children in the inclusive index range.
 *
 * On the first call also creates the "Capacity" pseudo child. For
 * BObjectList<T>, the element type is constructed as a pointer to T from the
 * compound's first template parameter; for plain BList, the type is looked up
 * via @c TeamTypeInformation as @c void*.
 *
 * @param info       Type-information service used for type lookup.
 * @param lowIndex   Lower bound of the requested window (clamped to 0).
 * @param highIndex  Upper bound of the requested window (clamped to fItemCount-1).
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  On allocation failure.
 * @return The cached resolution status if it was non-OK, or other status_t
 *         propagated from the type system.
 */
status_t
BListValueNode::CreateChildrenInRange(TeamTypeInformation* info,
	int32 lowIndex, int32 highIndex)
{
	if (fLocationResolutionState != B_OK)
		return fLocationResolutionState;

	if (lowIndex < 0)
		lowIndex = 0;
	if (highIndex >= fItemCount)
		highIndex = fItemCount - 1;

	if (!fCountChildCreated && fItemCountType != NULL) {
		BListItemCountNodeChild* countChild = new(std::nothrow)
			BListItemCountNodeChild(fItemCountLocation, this, fItemCountType);

		if (countChild == NULL)
			return B_NO_MEMORY;

		fCountChildCreated = true;
		countChild->SetContainer(fContainer);
		fChildren.AddItem(countChild);
	}

	BReference<Type> addressTypeRef;
	Type* type = NULL;
	CompoundType* objectType = dynamic_cast<CompoundType*>(fType);
	if (objectType->CountTemplateParameters() != 0) {
		AddressType* addressType = NULL;
		status_t result = objectType->TemplateParameterAt(0)->GetType()
			->CreateDerivedAddressType(DERIVED_TYPE_POINTER, addressType);
		if (result != B_OK)
			return result;

		type = addressType;
		addressTypeRef.SetTo(type, true);
	} else {
		BString typeName;
		TypeLookupConstraints constraints;
		constraints.SetTypeKind(TYPE_ADDRESS);
		constraints.SetBaseTypeName("void");
		status_t result = info->LookupTypeByName(typeName, constraints,
			type);
		if (result != B_OK)
			return result;
	}

	for (int32 i = lowIndex; i <= highIndex; i++)
	{
		BListElementNodeChild* child =
			new(std::nothrow) BListElementNodeChild(this, i, type);
		if (child == NULL)
			return B_NO_MEMORY;
		child->SetContainer(fContainer);
		fChildren.AddItem(child);
	}

	fChildrenCreated = true;

	if (fContainer != NULL)
		fContainer->NotifyValueNodeChildrenCreated(this);

	return B_OK;
}


/**
 * @brief Reports the legal slot index range for this BList.
 *
 * @param lowIndex   Set to 0.
 * @param highIndex  Set to fItemCount - 1.
 * @retval B_OK  Always.
 */
status_t
BListValueNode::SupportedChildRange(int32& lowIndex, int32& highIndex) const
{
	lowIndex = 0;
	highIndex = fItemCount - 1;

	return B_OK;
}

