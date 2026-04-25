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
 * @file VariableValueNodeChild.cpp
 * @brief Implementation of VariableValueNodeChild -- the root child for a stack/local Variable.
 *
 * The variables view spawns one VariableValueNodeChild per Variable in scope.
 * It just wraps a Variable: name, type, and pre-resolved location come
 * straight from the Variable's debug info, and ResolveLocation() is a simple
 * acquire-and-return.
 *
 * @see Variable, ValueNode
 */


#include "VariableValueNodeChild.h"

#include "Variable.h"
#include "ValueLocation.h"


/**
 * @brief Constructs the child and seeds its location from the Variable.
 *
 * @param variable  Variable whose name, type, and location back this child.
 */
VariableValueNodeChild::VariableValueNodeChild(Variable* variable)
	:
	fVariable(variable)
{
	fVariable->AcquireReference();
	SetLocation(fVariable->Location(), B_OK);
}


/**
 * @brief Releases the reference held on the Variable.
 */
VariableValueNodeChild::~VariableValueNodeChild()
{
	fVariable->ReleaseReference();
}


/**
 * @brief Returns the variable's display name.
 *
 * @return Reference to the Variable's name string.
 */
const BString&
VariableValueNodeChild::Name() const
{
	return fVariable->Name();
}


/**
 * @brief Returns the variable's declared DWARF type.
 *
 * @return The Variable's Type.
 */
Type*
VariableValueNodeChild::GetType() const
{
	return fVariable->GetType();
}


/**
 * @brief Top-level child has no parent node.
 *
 * @return NULL.
 */
ValueNode*
VariableValueNodeChild::Parent() const
{
	return NULL;
}


/**
 * @brief Returns the variable's pre-resolved location.
 *
 * @param valueLoader  Unused.
 * @param _location    Set to the Variable's location with a fresh reference.
 * @retval B_OK  Always.
 */
status_t
VariableValueNodeChild::ResolveLocation(ValueLoader* valueLoader,
	ValueLocation*& _location)
{
	_location = fVariable->Location();
	_location->AcquireReference();
	return B_OK;
}
