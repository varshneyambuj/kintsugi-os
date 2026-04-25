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
 * @file ValueNodeContainer.cpp
 * @brief Implementation of ValueNodeContainer, the locked root holding a tree of ValueNodeChild instances.
 *
 * The container owns the top-level children (one per displayed variable) and
 * the listener list that the variables view subscribes to. Mutations are
 * serialised through fLock, and listener fan-out happens for child arrival,
 * removal, and value/structure changes.
 *
 * @see ValueNode, ValueNodeChild
 */


#include "ValueNodeContainer.h"

#include <AutoLocker.h>

#include "ValueNode.h"


// #pragma mark - ValueNodeContainer


/**
 * @brief Constructs an empty container with its own lock.
 */
ValueNodeContainer::ValueNodeContainer()
	:
	fLock("value node container"),
	fChildren(20),
	fListeners(20)
{
}


/**
 * @brief Releases all children and clears the listener list.
 */
ValueNodeContainer::~ValueNodeContainer()
{
	RemoveAllChildren();
	fListeners.MakeEmpty();
}


/**
 * @brief Verifies that the embedded lock initialised correctly.
 *
 * @return Status of fLock.InitCheck().
 * @retval B_OK  Lock is usable.
 */
status_t
ValueNodeContainer::Init()
{
	return fLock.InitCheck();
}


/**
 * @brief Returns the number of top-level children currently held.
 *
 * @return Count of ValueNodeChild entries.
 */
int32
ValueNodeContainer::CountChildren() const
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
ValueNodeContainer::ChildAt(int32 index) const
{
	return fChildren.ItemAt(index);
}


/**
 * @brief Adds a top-level child and takes a reference on it.
 *
 * The container locks itself, appends @a child, acquires a reference, and
 * back-points the child at this container.
 *
 * @param child  The child to insert; must not be NULL.
 * @return true on success, false on allocation failure.
 */
bool
ValueNodeContainer::AddChild(ValueNodeChild* child)
{
	AutoLocker<ValueNodeContainer> locker(this);

	if (!fChildren.AddItem(child))
		return false;

	child->AcquireReference();
	child->SetContainer(this);

	return true;
}


/**
 * @brief Removes a top-level child and releases its reference.
 *
 * Detaches @a child from its node and container before releasing the
 * container's reference. No-op if @a child was not owned by this container.
 *
 * @param child  The child to remove.
 */
void
ValueNodeContainer::RemoveChild(ValueNodeChild* child)
{
	if (child->Container() != this || !fChildren.RemoveItem(child))
		return;

	child->SetNode(NULL);
	child->SetContainer(NULL);
	child->ReleaseReference();
}


/**
 * @brief Removes every child and releases the references the container held.
 */
void
ValueNodeContainer::RemoveAllChildren()
{
	for (int32 i = 0; ValueNodeChild* child = ChildAt(i); i++) {
		child->SetNode(NULL);
		child->SetContainer(NULL);
		child->ReleaseReference();
	}

	fChildren.MakeEmpty();
}


/**
 * @brief Subscribes a listener to value-tree change notifications.
 *
 * @param listener  Listener instance owned by the caller.
 * @return true on success, false on allocation failure.
 */
bool
ValueNodeContainer::AddListener(Listener* listener)
{
	return fListeners.AddItem(listener);
}


/**
 * @brief Unsubscribes a previously registered listener.
 *
 * @param listener  Listener to remove.
 */
void
ValueNodeContainer::RemoveListener(Listener* listener)
{
	fListeners.RemoveItem(listener);
}


/**
 * @brief Notifies listeners that the node behind @a nodeChild has changed.
 *
 * Iterates listeners in reverse so removal during dispatch is safe.
 *
 * @param nodeChild  The child whose backing node was swapped.
 * @param oldNode    Previous node (may be NULL).
 * @param newNode    Replacement node (may be NULL).
 */
void
ValueNodeContainer::NotifyValueNodeChanged(ValueNodeChild* nodeChild,
	ValueNode* oldNode, ValueNode* newNode)
{
	for (int32 i = fListeners.CountItems() - 1; i >= 0; i--)
		fListeners.ItemAt(i)->ValueNodeChanged(nodeChild, oldNode, newNode);
}


/**
 * @brief Notifies listeners that @a node has populated its child list.
 *
 * @param node  The node that just produced new children.
 */
void
ValueNodeContainer::NotifyValueNodeChildrenCreated(ValueNode* node)
{
	for (int32 i = fListeners.CountItems() - 1; i >= 0; i--)
		fListeners.ItemAt(i)->ValueNodeChildrenCreated(node);
}


/**
 * @brief Notifies listeners that @a node has discarded its previously created children.
 *
 * @param node  The node whose children were cleared.
 */
void
ValueNodeContainer::NotifyValueNodeChildrenDeleted(ValueNode* node)
{
	for (int32 i = fListeners.CountItems() - 1; i >= 0; i--)
		fListeners.ItemAt(i)->ValueNodeChildrenDeleted(node);
}


/**
 * @brief Notifies listeners that @a node has resolved a fresh value/location pair.
 *
 * @param node  The node whose Value was refreshed.
 */
void
ValueNodeContainer::NotifyValueNodeValueChanged(ValueNode* node)
{
	for (int32 i = fListeners.CountItems() - 1; i >= 0; i--)
		fListeners.ItemAt(i)->ValueNodeValueChanged(node);
}


// #pragma mark - ValueNodeContainer


/**
 * @brief Destructor for the Listener abstract base.
 *
 * Defined out-of-line so the vtable has a single home translation unit.
 */
ValueNodeContainer::Listener::~Listener()
{
}


/**
 * @brief Default no-op handler for child-node replacements.
 *
 * @param nodeChild  Affected child.
 * @param oldNode    Previous node.
 * @param newNode    Replacement node.
 */
void
ValueNodeContainer::Listener::ValueNodeChanged(ValueNodeChild* nodeChild,
	ValueNode* oldNode, ValueNode* newNode)
{
}


/**
 * @brief Default no-op handler for child-creation events.
 *
 * @param node  Node whose children were just created.
 */
void
ValueNodeContainer::Listener::ValueNodeChildrenCreated(ValueNode* node)
{
}


/**
 * @brief Default no-op handler for child-deletion events.
 *
 * @param node  Node whose children were just discarded.
 */
void
ValueNodeContainer::Listener::ValueNodeChildrenDeleted(ValueNode* node)
{
}


/**
 * @brief Default no-op handler for value-resolution events.
 *
 * @param node  Node whose Value was refreshed.
 */
void
ValueNodeContainer::Listener::ValueNodeValueChanged(ValueNode* node)
{
}
