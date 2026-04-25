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
 *   Copyright 2014, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ExpressionInfo.cpp
 * @brief Implementation of ExpressionInfo and ExpressionResult.
 *
 * ExpressionInfo carries a textual expression entered by the user and
 * dispatches evaluation results through a Listener interface.
 * ExpressionResult is the discriminated union returned by evaluation: it
 * may hold a primitive Value, a ValueNodeChild reference, or a Type,
 * each independently reference-counted.
 */


#include "ExpressionInfo.h"

#include "Type.h"
#include "Value.h"
#include "ValueNode.h"


// #pragma mark - ExpressionResult


/**
 * @brief Constructs an empty ExpressionResult of unknown kind.
 */
ExpressionResult::ExpressionResult()
	:
	fResultKind(EXPRESSION_RESULT_KIND_UNKNOWN),
	fPrimitiveValue(NULL),
	fValueNodeValue(NULL),
	fTypeResult(NULL)
{
}


/**
 * @brief Releases all held value, value-node, and type references.
 */
ExpressionResult::~ExpressionResult()
{
	if (fPrimitiveValue != NULL)
		fPrimitiveValue->ReleaseReference();

	if (fValueNodeValue != NULL)
		fValueNodeValue->ReleaseReference();

	if (fTypeResult != NULL)
		fTypeResult->ReleaseReference();
}


/**
 * @brief Stores a primitive Value as the result.
 *
 * Any previously held result is released first.
 *
 * @param value Primitive value to take a reference on, or NULL to clear.
 */
void
ExpressionResult::SetToPrimitive(Value* value)
{
	_Unset();

	fPrimitiveValue = value;
	if (fPrimitiveValue != NULL) {
		fPrimitiveValue->AcquireReference();
		fResultKind = EXPRESSION_RESULT_KIND_PRIMITIVE;
	}
}


/**
 * @brief Stores a ValueNodeChild as the result and mirrors any resolved value.
 *
 * If the underlying ValueNode already has a resolved primitive value, that
 * value is also stored so consumers can use it without re-resolving the
 * node.
 *
 * @param child ValueNodeChild reference to store, or NULL to clear.
 */
void
ExpressionResult::SetToValueNode(ValueNodeChild* child)
{
	_Unset();

	fValueNodeValue = child;
	if (fValueNodeValue != NULL) {
		fValueNodeValue->AcquireReference();
		fResultKind = EXPRESSION_RESULT_KIND_VALUE_NODE;
	}

	// if the child has a node with a resolved value, store
	// it as a primitive, so the consumer of the expression
	// can use it as-is if desired.

	ValueNode* node = child->Node();
	if (node == NULL)
		return;

	fPrimitiveValue = node->GetValue();
	if (fPrimitiveValue != NULL)
		fPrimitiveValue->AcquireReference();
}


/**
 * @brief Stores a Type as the result.
 *
 * @param type Type to take a reference on, or NULL to clear.
 */
void
ExpressionResult::SetToType(Type* type)
{
	_Unset();

	fTypeResult = type;
	if (fTypeResult != NULL) {
		fTypeResult->AcquireReference();
		fResultKind = EXPRESSION_RESULT_KIND_TYPE;
	}
}


/**
 * @brief Releases all stored result references and resets the kind to unknown.
 */
void
ExpressionResult::_Unset()
{
	if (fPrimitiveValue != NULL) {
		fPrimitiveValue->ReleaseReference();
		fPrimitiveValue = NULL;
	}

	if (fValueNodeValue != NULL) {
		fValueNodeValue->ReleaseReference();
		fValueNodeValue = NULL;
	}

	if (fTypeResult != NULL) {
		fTypeResult->ReleaseReference();
		fTypeResult = NULL;
	}

	fResultKind = EXPRESSION_RESULT_KIND_UNKNOWN;
}


// #pragma mark - ExpressionInfo


/**
 * @brief Constructs an empty ExpressionInfo with no expression text.
 */
ExpressionInfo::ExpressionInfo()
	:
	fExpression()
{
}


/**
 * @brief Copy-constructs the expression text from another instance.
 *
 * @param other Source ExpressionInfo to copy.
 */
ExpressionInfo::ExpressionInfo(const ExpressionInfo& other)
	:
	fExpression(other.fExpression)
{
}


/**
 * @brief Destroys the ExpressionInfo. Listeners are not deleted.
 */
ExpressionInfo::~ExpressionInfo()
{
}


/**
 * @brief Constructs an ExpressionInfo carrying @a expression.
 *
 * @param expression Source-form expression text.
 */
ExpressionInfo::ExpressionInfo(const BString& expression)
	:
	fExpression(expression)
{
}


/**
 * @brief Replaces the stored expression text.
 *
 * @param expression Replacement expression text.
 */
void
ExpressionInfo::SetTo(const BString& expression)
{
	fExpression = expression;
}


/**
 * @brief Subscribes @a listener for evaluation-result notifications.
 *
 * @param listener Listener to register; caller retains ownership.
 */
void
ExpressionInfo::AddListener(Listener* listener)
{
	fListeners.Add(listener);
}


/**
 * @brief Unsubscribes @a listener.
 *
 * @param listener Listener previously passed to @c AddListener().
 */
void
ExpressionInfo::RemoveListener(Listener* listener)
{
	fListeners.Remove(listener);
}


/**
 * @brief Dispatches an evaluation result to every subscribed listener.
 *
 * @param result Status code of the evaluation.
 * @param value  Resulting value (may be NULL on error).
 */
void
ExpressionInfo::NotifyExpressionEvaluated(status_t result,
	ExpressionResult* value)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->ExpressionEvaluated(this, result, value);
	}
}


// #pragma mark - ExpressionInfo::Listener


/**
 * @brief Virtual destructor anchor for the Listener interface.
 */
ExpressionInfo::Listener::~Listener()
{
}
