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
 * @file ValueNode.cpp
 * @brief Implementation of ValueNode and ValueNodeChild, the renderer-tree primitives.
 *
 * The variables view models each "show me this value" entry as a tree of
 * ValueNode/ValueNodeChild pairs. The child knows where the value lives
 * (location resolution); the node knows what it means (type-driven decoding
 * and child enumeration). This file holds the abstract bases plus the
 * ChildlessValueNode convenience subclass.
 *
 * @see ValueNodeContainer, TypeHandler
 */


#include "ValueNode.h"

#include "Value.h"
#include "ValueLocation.h"
#include "ValueNodeContainer.h"


// #pragma mark - ValueNode


/**
 * @brief Constructs a node tied to its owning child.
 *
 * Acquires a reference on @a nodeChild so the node remains valid even if its
 * parent's external reference is dropped.
 *
 * @param nodeChild  The child this node renders for.
 */
ValueNode::ValueNode(ValueNodeChild* nodeChild)
	:
	fContainer(NULL),
	fNodeChild(nodeChild),
	fLocation(NULL),
	fValue(NULL),
	fLocationResolutionState(VALUE_NODE_UNRESOLVED),
	fChildrenCreated(false)
{
	fNodeChild->AcquireReference();
}


/**
 * @brief Releases the cached location/value, container, and node child.
 */
ValueNode::~ValueNode()
{
	SetLocationAndValue(NULL, NULL, VALUE_NODE_UNRESOLVED);
	SetContainer(NULL);
	fNodeChild->ReleaseReference();
}


/**
 * @brief Returns the display name borrowed from the owning child.
 *
 * @return Reference to the child's name string.
 */
const BString&
ValueNode::Name() const
{
	return fNodeChild->Name();
}


/**
 * @brief Sets the container that owns this node and its descendants.
 *
 * Reference-counts the swap and recursively pushes the new container into
 * every existing child so notifications stay routed correctly.
 *
 * @param container  New owning container, or NULL to detach.
 */
void
ValueNode::SetContainer(ValueNodeContainer* container)
{
	if (container == fContainer)
		return;

	if (fContainer != NULL)
		fContainer->ReleaseReference();

	fContainer = container;

	if (fContainer != NULL)
		fContainer->AcquireReference();

	// propagate to children
	int32 childCount = CountChildren();
	for (int32 i = 0; i < childCount; i++)
		ChildAt(i)->SetContainer(fContainer);
}


/**
 * @brief Reports whether this node hands out children in user-controlled ranges.
 *
 * Default returns false; arrays and BList override this so the UI can show
 * paged child windows.
 *
 * @return true if CreateChildrenInRange() is meaningful.
 */
bool
ValueNode::IsRangedContainer() const
{
	return false;
}


/**
 * @brief Reports whether the supported child range cannot be expanded by the user.
 *
 * Default returns false; subclasses with hard-known bounds override.
 *
 * @return true if the range reported by SupportedChildRange() is fixed.
 */
bool
ValueNode::IsContainerRangeFixed() const
{
	return false;
}


/**
 * @brief Drops any previously created children. Default is a no-op.
 */
void
ValueNode::ClearChildren()
{
	// do nothing
}


/**
 * @brief Materialises children in the inclusive index range. Default refuses.
 *
 * @param info       Type-information service for type lookups.
 * @param lowIndex   Lower bound of the requested window.
 * @param highIndex  Upper bound of the requested window.
 * @retval B_NOT_SUPPORTED  Always, on the abstract base.
 */
status_t
ValueNode::CreateChildrenInRange(TeamTypeInformation* info, int32 lowIndex,
	int32 highIndex)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Reports the legal index range for ranged containers. Default refuses.
 *
 * @param lowIndex   Set to the inclusive lower bound on success.
 * @param highIndex  Set to the inclusive upper bound on success.
 * @retval B_NOT_SUPPORTED  Always, on the abstract base.
 */
status_t
ValueNode::SupportedChildRange(int32& lowIndex, int32& highIndex) const
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Caches a freshly resolved location/value pair and notifies listeners.
 *
 * Transfers reference ownership: the previous location and value are released
 * and references are taken on the replacements (if non-NULL).
 *
 * @param location         New location, or NULL.
 * @param value            New decoded value, or NULL.
 * @param resolutionState  Status code summarising the resolution attempt.
 */
void
ValueNode::SetLocationAndValue(ValueLocation* location, Value* value,
	status_t resolutionState)
{
	if (fLocation != location) {
		if (fLocation != NULL)
			fLocation->ReleaseReference();

		fLocation = location;

		if (fLocation != NULL)
			fLocation->AcquireReference();
	}

	if (fValue != value) {
		if (fValue != NULL)
			fValue->ReleaseReference();

		fValue = value;

		if (fValue != NULL)
			fValue->AcquireReference();
	}

	fLocationResolutionState = resolutionState;

	// notify listeners
	if (fContainer != NULL)
		fContainer->NotifyValueNodeValueChanged(this);
}


// #pragma mark - ValueNodeChild


/**
 * @brief Constructs a child placeholder with no container, node, or location yet.
 */
ValueNodeChild::ValueNodeChild()
	:
	fContainer(NULL),
	fNode(NULL),
	fLocation(NULL),
	fLocationResolutionState(VALUE_NODE_UNRESOLVED)
{
}


/**
 * @brief Releases the cached location, attached node, and container.
 */
ValueNodeChild::~ValueNodeChild()
{
	SetLocation(NULL, VALUE_NODE_UNRESOLVED);
	SetNode(NULL);
	SetContainer(NULL);
}


/**
 * @brief Reports whether this child requires an internal node (no real backing
 *        location).
 *
 * Default returns false; subclasses such as InternalArrayValueNodeChild
 * override.
 *
 * @return true for purely structural children.
 */
bool
ValueNodeChild::IsInternal() const
{
	return false;
}


/**
 * @brief Allocates the matching internal ValueNode. Default refuses.
 *
 * @param _node  Set to the freshly allocated node on success.
 * @retval B_BAD_VALUE  Always, on the abstract base.
 */
status_t
ValueNodeChild::CreateInternalNode(ValueNode*& _node)
{
	return B_BAD_VALUE;
}


/**
 * @brief Sets the container that should be notified of changes to this child.
 *
 * Propagates the container into the attached node, if any, so the entire
 * subtree converges on a single notification target.
 *
 * @param container  New owning container, or NULL.
 */
void
ValueNodeChild::SetContainer(ValueNodeContainer* container)
{
	if (container == fContainer)
		return;

	if (fContainer != NULL)
		fContainer->ReleaseReference();

	fContainer = container;

	if (fContainer != NULL)
		fContainer->AcquireReference();

	// propagate to node
	if (fNode != NULL)
		fNode->SetContainer(fContainer);
}


/**
 * @brief Replaces the node currently rendering for this child.
 *
 * Detaches the previous node from its container (so it stops receiving
 * updates), takes a reference on the replacement, and pushes the current
 * container into it. Listeners are notified of the swap.
 *
 * @param node  Replacement node, or NULL to clear.
 */
void
ValueNodeChild::SetNode(ValueNode* node)
{
	if (node == fNode)
		return;

	ValueNode* oldNode = fNode;
	BReference<ValueNode> oldNodeReference(oldNode, true);

	if (fNode != NULL)
		fNode->SetContainer(NULL);

	fNode = node;

	if (fNode != NULL) {
		fNode->AcquireReference();
		fNode->SetContainer(fContainer);
	}

	if (fContainer != NULL)
		fContainer->NotifyValueNodeChanged(this, oldNode, fNode);
}


/**
 * @brief Returns the cached location previously stored by SetLocation().
 *
 * @return The cached location, or NULL if not yet resolved.
 */
ValueLocation*
ValueNodeChild::Location() const
{
	return fLocation;
}


/**
 * @brief Caches a resolved location and the status that produced it.
 *
 * Reference-counts the swap of @a location.
 *
 * @param location         New location, or NULL.
 * @param resolutionState  Status from the resolver.
 */
void
ValueNodeChild::SetLocation(ValueLocation* location, status_t resolutionState)
{
	if (fLocation != location) {
		if (fLocation != NULL)
			fLocation->ReleaseReference();

		fLocation = location;

		if (fLocation != NULL)
			fLocation->AcquireReference();
	}

	fLocationResolutionState = resolutionState;
}


// #pragma mark - ChildlessValueNode


/**
 * @brief Constructs a value node that announces "no children" up front.
 *
 * Sets fChildrenCreated to true so the variables view never tries to expand it.
 *
 * @param nodeChild  The child this node renders for.
 */
ChildlessValueNode::ChildlessValueNode(ValueNodeChild* nodeChild)
	:
	ValueNode(nodeChild)
{
	fChildrenCreated = true;
}


/**
 * @brief Trivial CreateChildren implementation -- there are none.
 *
 * @param info  Unused type-information service.
 * @retval B_OK  Always.
 */
status_t
ChildlessValueNode::CreateChildren(TeamTypeInformation* info)
{
	return B_OK;
}


/**
 * @brief Always reports zero children.
 *
 * @return 0.
 */
int32
ChildlessValueNode::CountChildren() const
{
	return 0;
}


/**
 * @brief Always returns NULL since this node has no children.
 *
 * @param index  Ignored.
 * @return NULL.
 */
ValueNodeChild*
ChildlessValueNode::ChildAt(int32 index) const
{
	return NULL;
}
