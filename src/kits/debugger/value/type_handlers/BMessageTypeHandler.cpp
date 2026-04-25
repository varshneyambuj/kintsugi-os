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
 *   Copyright 2011-2018, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file BMessageTypeHandler.cpp
 * @brief TypeHandler that recognises BMessage compound types and yields BMessageValueNode.
 *
 * BMessage is a structured container; rendering it as a raw struct would be
 * useless, so this handler claims a perfect (1.0) score for any compound type
 * named "BMessage" so the BMessageValueNode field-aware renderer outranks the
 * generic compound handler.
 *
 * @see BMessageValueNode, TypeHandlerRoster
 */


#include "BMessageTypeHandler.h"

#include <new>

#include "BMessageValueNode.h"
#include "Type.h"


/**
 * @brief Trivial destructor.
 */
BMessageTypeHandler::~BMessageTypeHandler()
{
}


/**
 * @brief Returns the user-visible handler name shown in the variables view menu.
 *
 * @return The literal "Message content".
 */
const char*
BMessageTypeHandler::Name() const
{
	return "Message content";
}


/**
 * @brief Reports a perfect support score for compound types named "BMessage".
 *
 * @param type  Type to score.
 * @return 1.0 for BMessage compound types, 0.0 otherwise.
 */
float
BMessageTypeHandler::SupportsType(Type* type) const
{
	if (dynamic_cast<CompoundType*>(type) != NULL
		&& type->Name() == "BMessage")
		return 1.0f;

	return 0.0f;
}


/**
 * @brief Allocates a BMessageValueNode for @a nodeChild.
 *
 * @param nodeChild  Child the node will render for.
 * @param type       BMessage compound type.
 * @param _node      Set to the freshly allocated node on success.
 * @retval B_OK         On success.
 * @retval B_BAD_VALUE  When SupportsType() rejects @a type.
 * @retval B_NO_MEMORY  On allocation failure.
 */
status_t
BMessageTypeHandler::CreateValueNode(ValueNodeChild* nodeChild, Type* type,
	ValueNode*& _node)
{
	if (SupportsType(type) == 0.0f)
		return B_BAD_VALUE;

	ValueNode* node = new(std::nothrow) BMessageValueNode(nodeChild,
		type);

	if (node == NULL)
		return B_NO_MEMORY;

	_node = node;

	return B_OK;
}
