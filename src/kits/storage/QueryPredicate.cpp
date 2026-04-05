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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2002-2008, Haiku Inc.
 *   Authors:
 *       Tyler Dauwalder
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file QueryPredicate.cpp
 * @brief BQuery predicate helper classes implementation.
 *
 * This file implements the node hierarchy used to build BQuery predicate
 * expressions from a stack of pushed attribute names, operators, and values.
 * Nodes are organized into leaf, unary, and binary categories and can render
 * themselves as predicate substring strings. QueryStack manages the ordered
 * collection of nodes and converts them into an expression tree for evaluation.
 *
 * @see BQuery
 */

#include "QueryPredicate.h"

#include <ctype.h>

#include <UnicodeChar.h>


namespace BPrivate {
namespace Storage {

// #pragma mark - QueryNode


/**
 * @brief Constructs a QueryNode base instance.
 */
QueryNode::QueryNode()
{
}


/**
 * @brief Destroys the QueryNode base instance.
 */
QueryNode::~QueryNode()
{
}


// #pragma mark - LeafNode


/**
 * @brief Constructs a LeafNode with no children.
 */
LeafNode::LeafNode()
{
}


/**
 * @brief Destroys the LeafNode.
 */
LeafNode::~LeafNode()
{
}


/**
 * @brief Returns the arity (number of children) of this leaf node.
 *
 * @return Always returns 0.
 */
uint32
LeafNode::Arity() const
{
	return 0;
}


/**
 * @brief Attempts to set a child node; always fails for leaf nodes.
 *
 * @param child The child node to set (ignored).
 * @param index The child index (ignored).
 * @return Always returns B_BAD_VALUE.
 */
status_t
LeafNode::SetChildAt(QueryNode *child, int32 index)
{
	return B_BAD_VALUE;
}


/**
 * @brief Returns the child node at the given index; always NULL for leaf nodes.
 *
 * @param index The child index (ignored).
 * @return Always returns NULL.
 */
QueryNode *
LeafNode::ChildAt(int32 index)
{
	return NULL;
}


// #pragma mark - UnaryNode


/**
 * @brief Constructs a UnaryNode with no child set.
 */
UnaryNode::UnaryNode()
	:
	fChild(NULL)
{
}


/**
 * @brief Destroys the UnaryNode and its child.
 */
UnaryNode::~UnaryNode()
{
	delete fChild;
}


/**
 * @brief Returns the arity of this unary node.
 *
 * @return Always returns 1.
 */
uint32
UnaryNode::Arity() const
{
	return 1;
}


/**
 * @brief Sets the child node at the given index.
 *
 * @param child The child node to assign.
 * @param index Must be 0; any other value returns B_BAD_VALUE.
 * @return B_OK on success, or B_BAD_VALUE if index is not 0.
 */
status_t
UnaryNode::SetChildAt(QueryNode *child, int32 index)
{
	status_t error = B_OK;
	if (index == 0) {
		delete fChild;
		fChild = child;
	} else
		error = B_BAD_VALUE;
	return error;
}


/**
 * @brief Returns the child node at the given index.
 *
 * @param index Must be 0 to retrieve the single child.
 * @return The child node if index is 0, or NULL otherwise.
 */
QueryNode *
UnaryNode::ChildAt(int32 index)
{
	QueryNode *result = NULL;
	if (index == 0)
		result = fChild;
	return result;
}


// #pragma mark - BinaryNode


/**
 * @brief Constructs a BinaryNode with no children set.
 */
BinaryNode::BinaryNode()
	:
	fChild1(NULL),
	fChild2(NULL)
{
}


/**
 * @brief Destroys the BinaryNode and both of its children.
 */
BinaryNode::~BinaryNode()
{
	delete fChild1;
	delete fChild2;
}


/**
 * @brief Returns the arity of this binary node.
 *
 * @return Always returns 2.
 */
uint32
BinaryNode::Arity() const
{
	return 2;
}


/**
 * @brief Sets the child node at the given index (0 or 1).
 *
 * @param child The child node to assign.
 * @param index 0 for the left child, 1 for the right child.
 * @return B_OK on success, or B_BAD_VALUE if index is not 0 or 1.
 */
status_t
BinaryNode::SetChildAt(QueryNode *child, int32 index)
{
	status_t error = B_OK;
	if (index == 0) {
		delete fChild1;
		fChild1 = child;
	} else if (index == 1) {
		delete fChild2;
		fChild2 = child;
	} else
		error = B_BAD_VALUE;
	return error;
}


/**
 * @brief Returns the child node at the given index.
 *
 * @param index 0 for the left child, 1 for the right child.
 * @return The requested child node, or NULL if index is out of range.
 */
QueryNode *
BinaryNode::ChildAt(int32 index)
{
	QueryNode *result = NULL;
	if (index == 0)
		result = fChild1;
	else if (index == 1)
		result = fChild2;
	return result;
}


// #pragma mark - AttributeNode


/**
 * @brief Constructs an AttributeNode representing the given attribute name.
 *
 * @param attribute The file attribute name to represent in the predicate.
 */
AttributeNode::AttributeNode(const char *attribute)
	:
	fAttribute(attribute)
{
}


/**
 * @brief Renders this attribute node as its attribute name string.
 *
 * @param predicate BString to be set to the attribute name.
 * @return Always returns B_OK.
 */
status_t
AttributeNode::GetString(BString &predicate)
{
	predicate.SetTo(fAttribute);
	return B_OK;
}


// #pragma mark - StringNode


/**
 * @brief Constructs a StringNode with the given string value.
 *
 * If caseInsensitive is true, alphabetic characters are converted to
 * [lowerUPPER] bracket expressions, and spaces are replaced with wildcards.
 *
 * @param value The string value to represent.
 * @param caseInsensitive If true, builds a case-insensitive match pattern.
 */
StringNode::StringNode(const char *value, bool caseInsensitive)
{
	if (value == NULL)
		return;

	if (caseInsensitive) {
		while (uint32 codePoint = BUnicodeChar::FromUTF8(&value)) {
			char utf8Buffer[4];
			char *utf8 = utf8Buffer;
			if (BUnicodeChar::IsAlpha(codePoint)) {
				uint32 lower = BUnicodeChar::ToLower(codePoint);
				uint32 upper = BUnicodeChar::ToUpper(codePoint);
				if (lower == upper) {
					BUnicodeChar::ToUTF8(codePoint, &utf8);
					fValue.Append(utf8Buffer, utf8 - utf8Buffer);
				} else {
					fValue << "[";
					BUnicodeChar::ToUTF8(lower, &utf8);
					fValue.Append(utf8Buffer, utf8 - utf8Buffer);
					utf8 = utf8Buffer;
					BUnicodeChar::ToUTF8(upper, &utf8);
					fValue.Append(utf8Buffer, utf8 - utf8Buffer);
					fValue << "]";
				}
			} else if (codePoint == L' ') {
				fValue << '*';
			} else {
				BUnicodeChar::ToUTF8(codePoint, &utf8);
				fValue.Append(utf8Buffer, utf8 - utf8Buffer);
			}
		}
	} else {
		fValue = value;
		fValue.ReplaceAll(' ', '*');
	}
}


/**
 * @brief Renders this string node as a quoted, escaped predicate substring.
 *
 * @param predicate BString to be set to the quoted string expression.
 * @return Always returns B_OK.
 */
status_t
StringNode::GetString(BString &predicate)
{
	BString escaped(fValue);
	escaped.CharacterEscape("\"\\'", '\\');
	predicate.SetTo("");
	predicate << "\"" << escaped << "\"";
	return B_OK;
}


// #pragma mark - DateNode


/**
 * @brief Constructs a DateNode with the given date string value.
 *
 * @param value A human-readable date string (e.g., "today", "2026-04-01").
 */
DateNode::DateNode(const char *value)
	:
	fValue(value)
{
}


/**
 * @brief Renders this date node as a percent-delimited, escaped date string.
 *
 * @param predicate BString to be set to the date expression (e.g., "%2026-04-01%").
 * @return Always returns B_OK.
 */
status_t
DateNode::GetString(BString &predicate)
{
	BString escaped(fValue);
	escaped.CharacterEscape("%\"\\'", '\\');
	predicate.SetTo("");
	predicate << "%" << escaped << "%";
	return B_OK;
}


// #pragma mark - ValueNode


/**
 * @brief Renders a float ValueNode as a hex-encoded IEEE 754 representation.
 *
 * @param predicate BString to be set to the hex string for the float value.
 * @return Always returns B_OK.
 */
template<>
status_t
ValueNode<float>::GetString(BString &predicate)
{
	char buffer[32];
	union {
		int32 asInteger;
		float asFloat;
	} value;
	value.asFloat = fValue;
//	int32 value = *reinterpret_cast<int32*>(&fValue);
	sprintf(buffer, "0x%08" B_PRIx32, value.asInteger);
	predicate.SetTo(buffer);
	return B_OK;
}


/**
 * @brief Renders a double ValueNode as a hex-encoded IEEE 754 representation.
 *
 * @param predicate BString to be set to the hex string for the double value.
 * @return Always returns B_OK.
 */
template<>
status_t
ValueNode<double>::GetString(BString &predicate)
{
	char buffer[32];
	union {
		int64 asInteger;
		double asFloat;
	} value;
//	int64 value = *reinterpret_cast<int64*>(&fValue);
	value.asFloat = fValue;
	sprintf(buffer, "0x%016" B_PRIx64, value.asInteger);
	predicate.SetTo(buffer);
	return B_OK;
}


// #pragma mark - SpecialOpNode


/**
 * @brief Constructs a SpecialOpNode for the given query operator.
 *
 * @param op The special query_op value to represent.
 */
SpecialOpNode::SpecialOpNode(query_op op)
	:
	fOp(op)
{
}


/**
 * @brief Renders this special op node as a predicate string; always fails.
 *
 * @param predicate BString (unused).
 * @return Always returns B_BAD_VALUE.
 */
status_t
SpecialOpNode::GetString(BString &predicate)
{
	return B_BAD_VALUE;
}


// #pragma mark - UnaryOpNode


/**
 * @brief Constructs a UnaryOpNode for the given unary query operator.
 *
 * @param op The unary query_op value (e.g., B_NOT).
 */
UnaryOpNode::UnaryOpNode(query_op op)
	:
	fOp(op)
{
}


/**
 * @brief Renders this unary operator node as a predicate substring.
 *
 * @param predicate BString to be set to the operator expression (e.g., "(!child)").
 * @return B_OK on success, or B_BAD_VALUE if the child is not set or op is unsupported.
 */
status_t
UnaryOpNode::GetString(BString &predicate)
{
	status_t error = (fChild ? B_OK : B_BAD_VALUE);
	if (error == B_OK) {
		if (fOp == B_NOT) {
			BString childString;
			error = fChild->GetString(childString);
			predicate.SetTo("(!");
			predicate << childString << ")";
		} else
			error = B_BAD_VALUE;
	}
	return error;
}


// #pragma mark - BinaryOpNode


/**
 * @brief Constructs a BinaryOpNode for the given binary query operator.
 *
 * @param op The binary query_op value (e.g., B_EQ, B_AND, B_CONTAINS).
 */
BinaryOpNode::BinaryOpNode(query_op op)
	:
	fOp(op)
{
}


/**
 * @brief Renders this binary operator node as a predicate substring.
 *
 * Handles all standard binary operators including equality, comparison,
 * string-matching operators (CONTAINS, BEGINS_WITH, ENDS_WITH), and
 * logical operators (AND, OR).
 *
 * @param predicate BString to be set to the complete binary expression.
 * @return B_OK on success, or B_BAD_VALUE if children are missing or op is unknown.
 */
status_t
BinaryOpNode::GetString(BString &predicate)
{
	status_t error = (fChild1 && fChild2 ? B_OK : B_BAD_VALUE);
	BString childString1;
	BString childString2;
	if (error == B_OK)
		error = fChild1->GetString(childString1);
	if (error == B_OK)
		error = fChild2->GetString(childString2);
	predicate.SetTo("");
	if (error == B_OK) {
		switch (fOp) {
			case B_EQ:
				predicate << "(" << childString1 << "=="
					<< childString2 << ")";
				break;
			case B_GT:
				predicate << "(" << childString1 << ">"
					<< childString2 << ")";
				break;
			case B_GE:
				predicate << "(" << childString1 << ">="
					<< childString2 << ")";
				break;
			case B_LT:
				predicate << "(" << childString1 << "<"
					<< childString2 << ")";
				break;
			case B_LE:
				predicate << "(" << childString1 << "<="
					<< childString2 << ")";
				break;
			case B_NE:
				predicate << "(" << childString1 << "!="
					<< childString2 << ")";
				break;
			case B_CONTAINS:
				if (StringNode *strNode = dynamic_cast<StringNode*>(fChild2)) {
					BString value;
					value << "*" << strNode->Value() << "*";
					error = StringNode(value.String()).GetString(childString2);
				}
				if (error == B_OK) {
					predicate << "(" << childString1 << "=="
						<< childString2 << ")";
				}
				break;
			case B_BEGINS_WITH:
				if (StringNode *strNode = dynamic_cast<StringNode*>(fChild2)) {
					BString value;
					value << strNode->Value() << "*";
					error = StringNode(value.String()).GetString(childString2);
				}
				if (error == B_OK) {
					predicate << "(" << childString1 << "=="
						<< childString2 << ")";
				}
				break;
			case B_ENDS_WITH:
				if (StringNode *strNode = dynamic_cast<StringNode*>(fChild2)) {
					BString value;
					value << "*" << strNode->Value();
					error = StringNode(value.String()).GetString(childString2);
				}
				if (error == B_OK) {
					predicate << "(" << childString1 << "=="
						<< childString2 << ")";
				}
				break;
			case B_AND:
				predicate << "(" << childString1 << "&&"
					<< childString2 << ")";
				break;
			case B_OR:
				predicate << "(" << childString1 << "||"
					<< childString2 << ")";
				break;
			default:
				error = B_BAD_VALUE;
				break;
		}
	}
	return error;
}


// #pragma mark - QueryStack


/**
 * @brief Constructs an empty QueryStack.
 */
QueryStack::QueryStack()
{
}


/**
 * @brief Destroys the QueryStack and all nodes it owns.
 */
QueryStack::~QueryStack()
{
	for (int32 i = 0; QueryNode *node = (QueryNode*)fNodes.ItemAt(i); i++)
		delete node;
}


/**
 * @brief Pushes a QueryNode onto the stack.
 *
 * @param node The node to push; must not be NULL.
 * @return B_OK on success, B_BAD_VALUE if node is NULL, or B_NO_MEMORY on failure.
 */
status_t
QueryStack::PushNode(QueryNode *node)
{
	status_t error = (node ? B_OK : B_BAD_VALUE);
	if (error == B_OK) {
		if (!fNodes.AddItem(node))
			error = B_NO_MEMORY;
	}
	return error;
}


/**
 * @brief Pops and returns the top QueryNode from the stack.
 *
 * @return The top QueryNode, or NULL if the stack is empty.
 */
QueryNode *
QueryStack::PopNode()
{
	return (QueryNode*)fNodes.RemoveItem(fNodes.CountItems() - 1);
}


/**
 * @brief Converts the flat node stack into an expression tree and returns the root.
 *
 * @param rootNode Output reference to be set to the root of the expression tree.
 * @return B_OK on success, or B_BAD_VALUE if leftover nodes remain after tree construction.
 */
status_t
QueryStack::ConvertToTree(QueryNode *&rootNode)
{
	status_t error = _GetSubTree(rootNode);
	if (error == B_OK && !fNodes.IsEmpty()) {
		error = B_BAD_VALUE;
		delete rootNode;
		rootNode = NULL;
	}
	return error;
}


/**
 * @brief Recursively pops nodes and builds a subtree rooted at the popped node.
 *
 * @param rootNode Output reference to be set to the subtree root.
 * @return B_OK on success, or B_BAD_VALUE if the stack is empty or a child cannot be set.
 */
status_t
QueryStack::_GetSubTree(QueryNode *&rootNode)
{
	QueryNode *node = PopNode();
	status_t error = (node ? B_OK : B_BAD_VALUE);
	if (error == B_OK) {
		uint32 arity = node->Arity();
		for (int32 i = (int32)arity - 1; error == B_OK && i >= 0; i--) {
			QueryNode *child = NULL;
			error = _GetSubTree(child);
			if (error == B_OK) {
				error = node->SetChildAt(child, i);
				if (error != B_OK)
					delete child;
			}
		}
	}
	// clean up, if something went wrong
	if (error != B_OK && node) {
		delete node;
		node = NULL;
	}
	rootNode = node;
	return error;
}


}	// namespace Storage
}	// namespace BPrivate
