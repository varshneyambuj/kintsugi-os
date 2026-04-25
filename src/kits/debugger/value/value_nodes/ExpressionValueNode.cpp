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
 *   Copyright 2014, Rene Gollent, rene@gollent.com
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ExpressionValueNode.cpp
 * @brief Implementation of the watch-expression node and its associated child.
 *
 * Watch expressions ("show me the value of `*p->next`") are externally
 * evaluated; the node itself does not resolve a location. The child carries
 * the source-text expression as its name and the expected result type, and
 * the node hands back B_NOT_SUPPORTED from ResolvedLocationAndValue() so the
 * upper layers know to populate the value out-of-band.
 *
 * @see ExpressionValues, ValueNode
 */


#include "ExpressionValueNode.h"

#include <new>

#include "Type.h"


// #pragma mark - ExpressionValueNode


/**
 * @brief Constructs a childless node bound to its expression child.
 *
 * @param nodeChild  Child carrying the expression text and result type.
 * @param type       Expected result type.
 */
ExpressionValueNode::ExpressionValueNode(ExpressionValueNodeChild* nodeChild,
	Type* type)
	:
	ChildlessValueNode(nodeChild),
	fType(type)
{
	fType->AcquireReference();
}


/**
 * @brief Releases the reference held on the result type.
 */
ExpressionValueNode::~ExpressionValueNode()
{
	fType->ReleaseReference();
}


/**
 * @brief Returns the expected result type.
 *
 * @return The result Type.
 */
Type*
ExpressionValueNode::GetType() const
{
	return fType;
}


/**
 * @brief Refuses local resolution -- value is supplied externally.
 *
 * @param valueLoader  Unused.
 * @param _location    Unset.
 * @param _value       Unset.
 * @retval B_NOT_SUPPORTED  Always.
 */
status_t
ExpressionValueNode::ResolvedLocationAndValue(ValueLoader* valueLoader,
	ValueLocation*& _location, Value*& _value)
{
	return B_NOT_SUPPORTED;
}


// #pragma mark - ExpressionValueNodeChild


/**
 * @brief Constructs the child carrying the expression source and result type.
 *
 * @param expression  Source text typed by the user (used as the display name).
 * @param resultType  Expected type of the evaluated expression.
 */
ExpressionValueNodeChild::ExpressionValueNodeChild(const BString& expression,
	Type* resultType)
	:
	fExpression(expression),
	fResultType(resultType)
{
	fResultType->AcquireReference();
}


/**
 * @brief Releases the reference held on the result type.
 */
ExpressionValueNodeChild::~ExpressionValueNodeChild()
{
	fResultType->ReleaseReference();
}


/**
 * @brief Returns the expression source text used as the display name.
 *
 * @return Reference to the cached expression string.
 */
const BString&
ExpressionValueNodeChild::Name() const
{
	return fExpression;
}


/**
 * @brief Returns the expected result type.
 *
 * @return The result Type.
 */
Type*
ExpressionValueNodeChild::GetType() const
{
	return fResultType;
}


/**
 * @brief Top-level expression child has no parent node.
 *
 * @return NULL.
 */
ValueNode*
ExpressionValueNodeChild::Parent() const
{
	return NULL;
}


/**
 * @brief Trivial location resolver: expressions have no in-target location.
 *
 * @param valueLoader  Unused.
 * @param _location    Set to NULL.
 * @retval B_OK  Always.
 */
status_t
ExpressionValueNodeChild::ResolveLocation(ValueLoader* valueLoader,
	ValueLocation*& _location)
{
	_location = NULL;
	return B_OK;
}
