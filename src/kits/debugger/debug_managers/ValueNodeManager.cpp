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
 *   Copyright 2012-2016, Rene Gollent, rene@gollent.com.
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ValueNodeManager.cpp
 * @brief Owns the ValueNodeContainer for the currently-focused stack frame and
 *        bridges container events out to UI listeners.
 *
 * When the user selects a different frame, SetStackFrame() rebuilds the
 * container from the frame's parameters and locals. Asynchronous value
 * resolution drives ValueNodeChanged()/ValueNodeValueChanged() callbacks,
 * which the manager forwards to its registered listeners and uses to lazily
 * materialize child nodes once their parents become resolvable.
 */


#include "ValueNodeManager.h"

#include "AutoLocker.h"

#include "model/Thread.h"
#include "StackFrame.h"
#include "Team.h"
#include "TypeHandlerRoster.h"
#include "ValueNode.h"
#include "Variable.h"
#include "VariableValueNodeChild.h"


/**
 * @brief Constructs an empty manager and clears any prior frame state.
 *
 * @param addFrameNodes  If true, the manager will add nodes for the active
 *                       frame's parameters and locals when SetStackFrame() is
 *                       called with a non-NULL frame.
 */
ValueNodeManager::ValueNodeManager(bool addFrameNodes)
	:
	fAddFrameNodes(addFrameNodes),
	fContainer(NULL),
	fStackFrame(NULL),
	fThread(NULL)
{
	SetStackFrame(NULL, NULL);
}


/**
 * @brief Drops any owned container and clears the active frame.
 */
ValueNodeManager::~ValueNodeManager()
{
	SetStackFrame(NULL, NULL);
}


/**
 * @brief Switches the manager to a new (thread, stack frame) pair.
 *
 * Tears down the previous container, allocates a new one if @a stackFrame is
 * non-NULL, and (when fAddFrameNodes is set) seeds it with nodes for every
 * parameter and local on the frame.
 *
 * @param thread      Thread the frame belongs to; may be NULL when clearing.
 * @param stackFrame  Frame to focus on; pass NULL to clear the manager.
 * @return B_OK on success, B_NO_MEMORY on container allocation failure, or
 *         any error from ValueNodeContainer::Init().
 */
status_t
ValueNodeManager::SetStackFrame(Thread* thread,
	StackFrame* stackFrame)
{
	if (fContainer != NULL) {
		AutoLocker<ValueNodeContainer> containerLocker(fContainer);

		fContainer->RemoveListener(this);

		fContainer->RemoveAllChildren();
		containerLocker.Unlock();
		fContainer->ReleaseReference();
		fContainer = NULL;
	}

	fStackFrame = stackFrame;
	fThread = thread;

	if (fStackFrame == NULL)
		return B_OK;

	fContainer = new(std::nothrow) ValueNodeContainer;
	if (fContainer == NULL)
		return B_NO_MEMORY;

	status_t error = fContainer->Init();
	if (error != B_OK) {
		delete fContainer;
		fContainer = NULL;
		return error;
	}

	AutoLocker<ValueNodeContainer> containerLocker(fContainer);

	fContainer->AddListener(this);

	if (fStackFrame != NULL && fAddFrameNodes) {
		for (int32 i = 0; Variable* variable = fStackFrame->ParameterAt(i);
				i++) {
			_AddNode(variable);
		}

		for (int32 i = 0; Variable* variable
				= fStackFrame->LocalVariableAt(i); i++) {
			_AddNode(variable);
		}
	}

	return B_OK;
}


/**
 * @brief Registers @a listener for forwarded container events.
 *
 * @param listener  Listener instance owned by the caller.
 * @return true on success, false if the listener could not be appended.
 */
bool
ValueNodeManager::AddListener(ValueNodeContainer::Listener* listener)
{
	return fListeners.AddItem(listener);
}


/**
 * @brief Unregisters a previously-added listener.
 *
 * @param listener  Listener to remove.
 */
void
ValueNodeManager::RemoveListener(ValueNodeContainer::Listener* listener)
{
	fListeners.RemoveItem(listener);
}


/**
 * @brief Container hook: forwards a node-replacement event and may eagerly
 *        materialize children of the new node.
 *
 * @param nodeChild  Child whose backing value node has changed.
 * @param oldNode    Previously-bound node, or NULL if first binding.
 * @param newNode    New node now bound to @a nodeChild.
 */
void
ValueNodeManager::ValueNodeChanged(ValueNodeChild* nodeChild,
	ValueNode* oldNode, ValueNode* newNode)
{
	if (fContainer == NULL)
		return;

	AutoLocker<ValueNodeContainer> containerLocker(fContainer);

	for (int32 i = fListeners.CountItems() - 1; i >= 0; i--)
		fListeners.ItemAt(i)->ValueNodeChanged(nodeChild, oldNode, newNode);

	if (oldNode != NULL && !newNode->ChildCreationNeedsValue())
		newNode->CreateChildren(fThread->GetTeam()->GetTeamTypeInformation());
}


/**
 * @brief Container hook: forwards a children-created event to listeners.
 *
 * @param node  Node whose children have just been instantiated.
 */
void
ValueNodeManager::ValueNodeChildrenCreated(ValueNode* node)
{
	if (fContainer == NULL)
		return;

	for (int32 i = fListeners.CountItems() - 1; i >= 0; i--)
		fListeners.ItemAt(i)->ValueNodeChildrenCreated(node);
}


/**
 * @brief Container hook: forwards a children-deleted event to listeners.
 *
 * @param node  Node whose children have been torn down.
 */
void
ValueNodeManager::ValueNodeChildrenDeleted(ValueNode* node)
{
	if (fContainer == NULL)
		return;

	for (int32 i = fListeners.CountItems() - 1; i >= 0; i--)
		fListeners.ItemAt(i)->ValueNodeChildrenDeleted(node);
}


/**
 * @brief Container hook: forwards a value-resolved event and lazily expands
 *        children whose creation was deferred until the parent's value was known.
 *
 * @param valueNode  Node whose resolved value just became available.
 */
void
ValueNodeManager::ValueNodeValueChanged(ValueNode* valueNode)
{
	if (fContainer == NULL)
		return;

	AutoLocker<ValueNodeContainer> containerLocker(fContainer);

	// check whether we know the node
	ValueNodeChild* nodeChild = valueNode->NodeChild();
	if (nodeChild == NULL)
		return;

	if (valueNode->ChildCreationNeedsValue()
		&& !valueNode->ChildrenCreated()) {
		status_t error = valueNode->CreateChildren(
			fThread->GetTeam()->GetTeamTypeInformation());
		if (error == B_OK) {
			for (int32 i = 0; i < valueNode->CountChildren(); i++) {
				ValueNodeChild* child = valueNode->ChildAt(i);
				_CreateValueNode(child);
				AddChildNodes(child);
			}
		}
	}

	for (int32 i = fListeners.CountItems() - 1; i >= 0; i--)
		fListeners.ItemAt(i)->ValueNodeValueChanged(valueNode);
}


/**
 * @brief Adds a top-level variable as a fresh ValueNodeChild and seeds children.
 *
 * @param variable  Variable (parameter or local) from the active stack frame.
 */
void
ValueNodeManager::_AddNode(Variable* variable)
{
	// create the node child for the variable
	ValueNodeChild* nodeChild = new (std::nothrow) VariableValueNodeChild(
		variable);
	BReference<ValueNodeChild> nodeChildReference(nodeChild, true);
	if (nodeChild == NULL || !fContainer->AddChild(nodeChild)) {
		delete nodeChild;
		return;
	}

	// automatically add child nodes for the top level nodes
	AddChildNodes(nodeChild);
}


/**
 * @brief Creates the ValueNode for @a nodeChild if it does not yet have one.
 *
 * Internal children create their own node; non-internal children consult the
 * TypeHandlerRoster to find an appropriate value-node implementation.
 *
 * @param nodeChild  Child that needs a backing value node; must be non-NULL.
 * @return B_OK on success or if a node already exists, an error code from the
 *         underlying CreateInternalNode/TypeHandlerRoster path.
 */
status_t
ValueNodeManager::_CreateValueNode(ValueNodeChild* nodeChild)
{
	if (nodeChild->Node() != NULL)
		return B_OK;

	// create the node
	ValueNode* valueNode;
	status_t error;
	if (nodeChild->IsInternal()) {
		error = nodeChild->CreateInternalNode(valueNode);
	} else {
		error = TypeHandlerRoster::Default()->CreateValueNode(nodeChild,
			nodeChild->GetType(), NULL, valueNode);
	}

	if (error != B_OK)
		return error;

	nodeChild->SetNode(valueNode);
	valueNode->ReleaseReference();

	return B_OK;
}


/**
 * @brief Materializes the value node for @a nodeChild and (when possible) its
 *        immediate children.
 *
 * If the node's child creation must wait until its value resolves, the call
 * returns B_OK without creating children; ValueNodeValueChanged() will pick up
 * the work later.
 *
 * @param nodeChild  Child whose subtree should be expanded; must be non-NULL.
 * @return B_OK on success or when child creation is deferred, otherwise an
 *         error code from the type-handler path.
 */
status_t
ValueNodeManager::AddChildNodes(ValueNodeChild* nodeChild)
{
	AutoLocker<ValueNodeContainer> containerLocker(fContainer);

	// create a value node for the value node child, if doesn't have one yet
	ValueNode* valueNode = nodeChild->Node();
	if (valueNode == NULL) {
		status_t error = _CreateValueNode(nodeChild);
		if (error != B_OK)
			return error;
		valueNode = nodeChild->Node();
	}

	// check if this node requires child creation
	// to be deferred until after its location/value have been resolved
	if (valueNode->ChildCreationNeedsValue())
		return B_OK;

	// create the children, if not done yet
	if (valueNode->ChildrenCreated())
		return B_OK;

	return valueNode->CreateChildren(
		fThread->GetTeam()->GetTeamTypeInformation());
}
