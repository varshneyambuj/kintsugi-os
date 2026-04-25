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
 *   Copyright 2012-2018, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file BListTypeHandler.cpp
 * @brief TypeHandler that recognises BList and BObjectList compound types.
 *
 * Both BList and BObjectList<...> are detected; the latter is matched by name
 * prefix so any template instantiation routes to BListValueNode regardless of
 * its concrete element type.
 *
 * @see BListValueNode, TypeHandlerRoster
 */


#include "BListTypeHandler.h"

#include <new>

#include "BListValueNode.h"
#include "Type.h"


/**
 * @brief Trivial destructor.
 */
BListTypeHandler::~BListTypeHandler()
{
}


/**
 * @brief Returns the user-visible handler name shown in the variables view menu.
 *
 * @return The literal "List content".
 */
const char*
BListTypeHandler::Name() const
{
	return "List content";
}


/**
 * @brief Reports a perfect support score for compound types named BList or BObjectList<...>.
 *
 * @param type  Type to score.
 * @return 1.0 for matching compound types, 0.0 otherwise.
 */
float
BListTypeHandler::SupportsType(Type* type) const
{
	if (dynamic_cast<CompoundType*>(type) != NULL
		&& (type->Name() == "BList"
			|| type->Name().Compare("BObjectList", 11) == 0))
		return 1.0f;

	return 0.0f;
}


/**
 * @brief Allocates a BListValueNode for @a nodeChild.
 *
 * @param nodeChild  Child the node will render for.
 * @param type       BList/BObjectList compound type.
 * @param _node      Set to the freshly allocated node on success.
 * @retval B_OK         On success.
 * @retval B_BAD_VALUE  When SupportsType() rejects @a type.
 * @retval B_NO_MEMORY  On allocation failure.
 */
status_t
BListTypeHandler::CreateValueNode(ValueNodeChild* nodeChild, Type* type,
	ValueNode*& _node)
{
	if (SupportsType(type) == 0.0f)
		return B_BAD_VALUE;

	ValueNode* node = new(std::nothrow) BListValueNode(nodeChild,
		type);

	if (node == NULL)
		return B_NO_MEMORY;

	_node = node;

	return B_OK;
}
