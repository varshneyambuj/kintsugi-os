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
 *   Copyright 2013-2015, Rene Gollent, rene@gollent.com.
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ArrayValueNode.cpp
 * @brief Implementation of the array-rendering node hierarchy: one node per array dimension.
 *
 * Multidimensional arrays are decomposed into a chain of nodes:
 * ArrayValueNode handles the outermost dimension and InternalArrayValueNode
 * handles each non-final dimension. Children are either
 * ArrayValueNodeChild (final dimension -- has a real element location) or
 * InternalArrayValueNodeChild (non-final -- spawns the next dimension's
 * internal node).
 *
 * @see ArrayType, ValueNode
 */


#include "ArrayValueNode.h"

#include <new>

#include "Architecture.h"
#include "ArrayIndexPath.h"
#include "IntegerValue.h"
#include "Tracing.h"
#include "Type.h"
#include "ValueLoader.h"
#include "ValueLocation.h"
#include "ValueNodeContainer.h"


/** @brief Default upper bound on the number of element children created on first expand. */
static const uint64 kMaxArrayElementCount = 10;


// #pragma mark - AbstractArrayValueNode


/**
 * @brief Constructs the per-dimension array node and references its type.
 *
 * @param nodeChild  Child this node renders for.
 * @param type       Whole-array DWARF type.
 * @param dimension  Zero-based index of the dimension this node represents.
 */
AbstractArrayValueNode::AbstractArrayValueNode(ValueNodeChild* nodeChild,
	ArrayType* type, int32 dimension)
	:
	ValueNode(nodeChild),
	fType(type),
	fDimension(dimension),
	fLowerBound(0),
	fUpperBound(0),
	fBoundsInitialized(false)
{
	fType->AcquireReference();
}


/**
 * @brief Releases all element children and the type.
 */
AbstractArrayValueNode::~AbstractArrayValueNode()
{
	fType->ReleaseReference();

	for (int32 i = 0; AbstractArrayValueNodeChild* child = fChildren.ItemAt(i);
			i++) {
		child->ReleaseReference();
	}
}


/**
 * @brief Returns the wrapped array type.
 *
 * @return The DWARF ArrayType.
 */
Type*
AbstractArrayValueNode::GetType() const
{
	return fType;
}


/**
 * @brief Returns the parent location verbatim; arrays have no scalar value.
 *
 * @param valueLoader  Unused.
 * @param _location    Receives a re-referenced copy of the parent location.
 * @param _value       Always set to NULL.
 * @retval B_OK         On success.
 * @retval B_BAD_VALUE  When the parent location is missing.
 */
status_t
AbstractArrayValueNode::ResolvedLocationAndValue(ValueLoader* valueLoader,
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
 * @brief Initial-expand entry point: creates up to kMaxArrayElementCount slots.
 *
 * @param info  Type-information service.
 * @return Status from CreateChildrenInRange().
 */
status_t
AbstractArrayValueNode::CreateChildren(TeamTypeInformation* info)
{
	if (!fChildren.IsEmpty())
		return B_OK;

	return CreateChildrenInRange(info, 0, kMaxArrayElementCount - 1);
}


/**
 * @brief Returns the number of currently materialised element children.
 *
 * @return Count of children.
 */
int32
AbstractArrayValueNode::CountChildren() const
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
AbstractArrayValueNode::ChildAt(int32 index) const
{
	return fChildren.ItemAt(index);
}


/**
 * @brief Reports that this node hands out children in user-controlled ranges.
 *
 * @return true.
 */
bool
AbstractArrayValueNode::IsRangedContainer() const
{
	return true;
}


/**
 * @brief Drops every materialised child and notifies listeners.
 */
void
AbstractArrayValueNode::ClearChildren()
{
	fChildren.MakeEmpty();
	fLowerBound = 0;
	fUpperBound = 0;
	if (fContainer != NULL)
		fContainer->NotifyValueNodeChildrenDeleted(this);
}


/**
 * @brief Materialises element children in the inclusive index range.
 *
 * For the final dimension creates ArrayValueNodeChild instances (with real
 * element locations); for inner dimensions creates
 * InternalArrayValueNodeChild instances which spawn the next dimension's
 * internal node on expand.
 *
 * @param info       Type-information service.
 * @param lowIndex   Lower bound of the requested window (clamped to fLowerBound).
 * @param highIndex  Upper bound of the requested window (clamped to fUpperBound).
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  On allocation failure.
 * @return Other status_t propagated from SupportedChildRange().
 * @todo  Skip indices that already have a child rather than re-creating them.
 */
status_t
AbstractArrayValueNode::CreateChildrenInRange(TeamTypeInformation* info,
	int32 lowIndex, int32 highIndex)
{
	// TODO: ensure that we don't already have children in the specified
	// index range. These need to be skipped if so.
	TRACE_LOCALS("TYPE_ARRAY\n");

	int32 dimensionCount = fType->CountDimensions();
	bool isFinalDimension = fDimension + 1 == dimensionCount;
	status_t error = B_OK;

	if (!fBoundsInitialized) {
		int32 lowerBound, upperBound;
		error = SupportedChildRange(lowerBound, upperBound);
		if (error != B_OK)
			return error;

		fLowerBound = lowerBound;
		fUpperBound = upperBound;
		fBoundsInitialized = true;
	}

	if (lowIndex < fLowerBound)
		lowIndex = fLowerBound;
	if (highIndex > fUpperBound)
		highIndex = fUpperBound;

	// create children for the array elements
	for (int32 i = lowIndex; i <= highIndex; i++) {
		BString name(Name());
		name << '[' << i << ']';
		if (name.Length() <= Name().Length())
			return B_NO_MEMORY;

		AbstractArrayValueNodeChild* child;
		if (isFinalDimension) {
			child = new(std::nothrow) ArrayValueNodeChild(this, name, i,
				fType->BaseType());
		} else {
			child = new(std::nothrow) InternalArrayValueNodeChild(this, name, i,
				fType);
		}

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
 * @brief Reports the legal index range for this dimension.
 *
 * Reads the bounds from the dimension's SubrangeType on first call and
 * caches them in fLowerBound/fUpperBound.
 *
 * @param lowIndex   Set to the inclusive lower bound on success.
 * @param highIndex  Set to the inclusive upper bound on success.
 * @retval B_OK           On success.
 * @retval B_UNSUPPORTED  When the dimension type is not a SubrangeType.
 */
status_t
AbstractArrayValueNode::SupportedChildRange(int32& lowIndex,
	int32& highIndex) const
{
	if (!fBoundsInitialized) {
		ArrayDimension* dimension = fType->DimensionAt(fDimension);

		SubrangeType* dimensionType = dynamic_cast<SubrangeType*>(
			dimension->GetType());

		if (dimensionType != NULL) {
			lowIndex = dimensionType->LowerBound().ToInt32();
			highIndex = dimensionType->UpperBound().ToInt32();
		} else
			return B_UNSUPPORTED;
	} else {
		lowIndex = fLowerBound;
		highIndex = fUpperBound;
	}

	return B_OK;
}


// #pragma mark - ArrayValueNode


/**
 * @brief Constructs the outermost (dimension 0) array node.
 *
 * @param nodeChild  Child this node renders for.
 * @param type       Whole-array DWARF type.
 */
ArrayValueNode::ArrayValueNode(ValueNodeChild* nodeChild, ArrayType* type)
	:
	AbstractArrayValueNode(nodeChild, type, 0)
{
}


/**
 * @brief Trivial destructor.
 */
ArrayValueNode::~ArrayValueNode()
{
}


// #pragma mark - InternalArrayValueNode


/**
 * @brief Constructs an inner-dimension array node spawned from a non-final child.
 *
 * @param nodeChild  Child this node renders for.
 * @param type       Whole-array DWARF type.
 * @param dimension  Zero-based index of the dimension this node represents.
 */
InternalArrayValueNode::InternalArrayValueNode(ValueNodeChild* nodeChild,
	ArrayType* type, int32 dimension)
	:
	AbstractArrayValueNode(nodeChild, type, dimension)
{
}


/**
 * @brief Trivial destructor.
 */
InternalArrayValueNode::~InternalArrayValueNode()
{
}


// #pragma mark - AbstractArrayValueNodeChild


/**
 * @brief Constructs the per-element child base.
 *
 * @param parent        Owning array node.
 * @param name          Display name (typically "name[i]").
 * @param elementIndex  Index inside this dimension.
 */
AbstractArrayValueNodeChild::AbstractArrayValueNodeChild(
	AbstractArrayValueNode* parent, const BString& name, int64 elementIndex)
	:
	fParent(parent),
	fName(name),
	fElementIndex(elementIndex)
{
}


/**
 * @brief Trivial destructor.
 */
AbstractArrayValueNodeChild::~AbstractArrayValueNodeChild()
{
}


/**
 * @brief Returns the child's display name.
 *
 * @return Reference to the cached name string.
 */
const BString&
AbstractArrayValueNodeChild::Name() const
{
	return fName;
}


/**
 * @brief Returns the owning array node.
 *
 * @return The parent node.
 */
ValueNode*
AbstractArrayValueNodeChild::Parent() const
{
	return fParent;
}


// #pragma mark - ArrayValueNodeChild


/**
 * @brief Constructs a final-dimension element child.
 *
 * @param parent        Owning array node.
 * @param name          Display name (typically "name[i]").
 * @param elementIndex  Index inside this dimension.
 * @param type          Element type (the array's BaseType()).
 */
ArrayValueNodeChild::ArrayValueNodeChild(AbstractArrayValueNode* parent,
	const BString& name, int64 elementIndex, Type* type)
	:
	AbstractArrayValueNodeChild(parent, name, elementIndex),
	fType(type)
{
	fType->AcquireReference();
}


/**
 * @brief Releases the reference held on the element type.
 */
ArrayValueNodeChild::~ArrayValueNodeChild()
{
	fType->ReleaseReference();
}


/**
 * @brief Returns the element type.
 *
 * @return The array's BaseType().
 */
Type*
ArrayValueNodeChild::GetType() const
{
	return fType;
}


/**
 * @brief Computes the in-target address of this element.
 *
 * Walks back up through internal-array ancestor children to assemble a full
 * ArrayIndexPath (one index per dimension), then asks the ArrayType to
 * resolve the resulting element location.
 *
 * @param valueLoader  Loader (passed through to the type system).
 * @param _location    Set to the resolved location on success.
 * @retval B_OK         On success.
 * @retval B_BAD_VALUE  When the parent's location is missing.
 * @retval B_NO_MEMORY  On allocation failure when assembling the index path.
 * @return Other status_t propagated from ArrayType::ResolveElementLocation().
 */
status_t
ArrayValueNodeChild::ResolveLocation(ValueLoader* valueLoader,
	ValueLocation*& _location)
{
	// get the parent (== array) location
	ValueLocation* parentLocation = fParent->Location();
	if (parentLocation == NULL)
		return B_BAD_VALUE;

	// create an array index path
	ArrayType* arrayType = fParent->GetArrayType();
	int32 dimensionCount = arrayType->CountDimensions();

	// add dummy indices first -- we'll replace them on our way back through
	// our ancestors
	ArrayIndexPath indexPath;
	for (int32 i = 0; i < dimensionCount; i++) {
		if (!indexPath.AddIndex(0))
			return B_NO_MEMORY;
	}

	AbstractArrayValueNodeChild* child = this;
	for (int32 i = dimensionCount - 1; i >= 0; i--) {
		indexPath.SetIndexAt(i, child->ElementIndex());

		child = dynamic_cast<AbstractArrayValueNodeChild*>(
			child->ArrayParent()->NodeChild());
	}

	// resolve the element location
	ValueLocation* location;
	status_t error = arrayType->ResolveElementLocation(indexPath,
		*parentLocation, location);
	if (error != B_OK) {
		TRACE_LOCALS("ArrayValueNodeChild::ResolveLocation(): "
			"ResolveElementLocation() failed: %s\n", strerror(error));
		return error;
	}

	_location = location;
	return B_OK;
}


// #pragma mark - InternalArrayValueNodeChild


/**
 * @brief Constructs a non-final-dimension placeholder child.
 *
 * @param parent        Owning array node.
 * @param name          Display name.
 * @param elementIndex  Index inside this dimension.
 * @param type          Whole-array type (passed through to the spawned inner node).
 */
InternalArrayValueNodeChild::InternalArrayValueNodeChild(
	AbstractArrayValueNode* parent, const BString& name, int64 elementIndex,
	ArrayType* type)
	:
	AbstractArrayValueNodeChild(parent, name, elementIndex),
	fType(type)
{
	fType->AcquireReference();
}


/**
 * @brief Releases the reference held on the array type.
 */
InternalArrayValueNodeChild::~InternalArrayValueNodeChild()
{
	fType->ReleaseReference();
}


/**
 * @brief Returns the array type.
 *
 * @return The whole-array DWARF type.
 */
Type*
InternalArrayValueNodeChild::GetType() const
{
	return fType;
}


/**
 * @brief Reports that this child requires an internal (no-real-location) node.
 *
 * @return true.
 */
bool
InternalArrayValueNodeChild::IsInternal() const
{
	return true;
}


/**
 * @brief Allocates the next-dimension InternalArrayValueNode.
 *
 * @param _node  Set to the freshly allocated node on success.
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  On allocation failure.
 */
status_t
InternalArrayValueNodeChild::CreateInternalNode(ValueNode*& _node)
{
	ValueNode* node = new(std::nothrow) InternalArrayValueNode(this, fType,
		fParent->Dimension() + 1);
	if (node == NULL)
		return B_NO_MEMORY;

	_node = node;
	return B_OK;
}


/**
 * @brief Inner-dimension children share the parent's location verbatim.
 *
 * @param valueLoader  Unused.
 * @param _location    Set to a re-referenced copy of the parent's location.
 * @retval B_OK         On success.
 * @retval B_BAD_VALUE  When the parent location is missing.
 */
status_t
InternalArrayValueNodeChild::ResolveLocation(ValueLoader* valueLoader,
	ValueLocation*& _location)
{
	// This is an internal child node for a non-final dimension -- just clone
	// the parent's location.
	ValueLocation* parentLocation = fParent->Location();
	if (parentLocation == NULL)
		return B_BAD_VALUE;

	parentLocation->AcquireReference();
	_location = parentLocation;

	return B_OK;
}
