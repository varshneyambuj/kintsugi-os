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
 *   Copyright 2003-2009, Ingo Weinhold <ingo_weinhold@gmx.de>.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file AVLTreeBase.cpp
 * @brief Untyped core of the kernel's intrusive AVL tree.
 *
 * Implements insertion, deletion, traversal, and rebalancing on AVLTreeNode
 * links without knowing the element type. Key ordering is delegated to an
 * AVLTreeCompare callback, so typed wrappers layer value semantics on top of
 * this file. Rebalancing uses explicit stacks bounded by kMaxAVLTreeHeight.
 */

#include <util/AVLTreeBase.h>

#ifndef FS_SHELL
#	include <algorithm>
#	include <KernelExport.h>
#endif

#ifdef _KERNEL_MODE
#	define CHECK_FAILED(message...)	panic(message)
#else
#	ifndef FS_SHELL
#		include <stdio.h>
#		include <OS.h>
#		define CHECK_FAILED(message...)					\
			do {										\
				fprintf(stderr, message);				\
				fprintf(stderr, "\n");					\
				debugger("AVLTreeBase check failed");	\
			} while (false)
#	else
#		define CHECK_FAILED(message...) dprintf(message)
#	endif
#endif


// maximal height of a tree
static const int kMaxAVLTreeHeight = 32;


// #pragma mark - AVLTreeCompare


/**
 * @brief Virtual destructor for the comparator interface.
 */
AVLTreeCompare::~AVLTreeCompare()
{
}


// #pragma mark - AVLTreeBase


/**
 * @brief Construct an empty tree bound to the given comparator.
 * @param compare Comparator used for all key/node ordering decisions.
 */
AVLTreeBase::AVLTreeBase(AVLTreeCompare* compare)
	: fRoot(NULL),
	  fNodeCount(0),
	  fCompare(compare)
{
}


/**
 * @brief Destroy the tree structure. Nodes are not freed.
 */
AVLTreeBase::~AVLTreeBase()
{
}


/**
 * @brief Detach every node by forgetting the root and count.
 *
 * Does not walk or free the nodes; caller owns their storage.
 */
void
AVLTreeBase::MakeEmpty()
{
	fRoot = NULL;
	fNodeCount = 0;
}


/**
 * @brief Return the leftmost descendant of @a node.
 * @param node Subtree root, or NULL.
 * @return The smallest node in the subtree, or NULL if @a node is NULL.
 */
AVLTreeNode*
AVLTreeBase::LeftMost(AVLTreeNode* node) const
{
	if (node) {
		while (node->left)
			node = node->left;
	}

	return node;
}


/**
 * @brief Return the rightmost descendant of @a node.
 * @param node Subtree root, or NULL.
 * @return The largest node in the subtree, or NULL if @a node is NULL.
 */
AVLTreeNode*
AVLTreeBase::RightMost(AVLTreeNode* node) const
{
	if (node) {
		while (node->right)
			node = node->right;
	}

	return node;
}


/**
 * @brief In-order predecessor of @a node.
 * @param node Starting node.
 * @return The node with the immediately smaller key, or NULL if none.
 */
AVLTreeNode*
AVLTreeBase::Previous(AVLTreeNode* node) const
{
	if (node) {
		// The previous node cannot be in the right subtree.
		if (node->left) {
			// We have a left subtree, so go to the right-most node.
			node = node->left;
			while (node->right)
				node = node->right;
		} else {
			// No left subtree: Backtrack our path and stop, where we
			// took the right branch.
			AVLTreeNode* previous;
			do {
				previous = node;
				node = node->parent;
			} while (node && previous == node->left);
		}
	}

	return node;
}


/**
 * @brief In-order successor of @a node.
 * @param node Starting node.
 * @return The node with the immediately larger key, or NULL if none.
 */
AVLTreeNode*
AVLTreeBase::Next(AVLTreeNode* node) const
{
	if (node) {
		// The next node cannot be in the left subtree.
		if (node->right) {
			// We have a right subtree, so go to the left-most node.
			node = node->right;
			while (node->left)
				node = node->left;
		} else {
			// No right subtree: Backtrack our path and stop, where we
			// took the left branch.
			AVLTreeNode* previous;
			do {
				previous = node;
				node = node->parent;
			} while (node && previous == node->right);
		}
	}

	return node;
}


/**
 * @brief Look up the node whose key equals @a key.
 * @param key Caller-supplied key passed to the comparator.
 * @return Matching node, or NULL if not present.
 */
AVLTreeNode*
AVLTreeBase::Find(const void* key) const
{
	AVLTreeNode* node = fRoot;

	while (node) {
		int cmp = fCompare->CompareKeyNode(key, node);
		if (cmp == 0)
			return node;

		if (cmp < 0)
			node = node->left;
		else
			node = node->right;
	}

	return NULL;
}


/**
 * @brief Find the closest node to @a key in a given direction.
 * @param key  Caller-supplied key.
 * @param less If true, return the largest node less-or-equal; else the
 *             smallest greater-or-equal.
 * @return Matching neighbor, or NULL if the tree has no node in that
 *         direction.
 */
AVLTreeNode*
AVLTreeBase::FindClosest(const void* key, bool less) const
{
	AVLTreeNode* node = fRoot;
	AVLTreeNode* parent = NULL;

	while (node) {
		int cmp = fCompare->CompareKeyNode(key, node);
		if (cmp == 0)
			break;

		parent = node;
		if (cmp < 0)
			node = node->left;
		else
			node = node->right;
	}

	// not found: try to get close
	if (!node && parent) {
		node = parent;
		int expectedCmp = (less ? 1 : -1);
		int cmp = fCompare->CompareKeyNode(key, node);
		if (cmp != expectedCmp) {
			// The node's value is less although we were asked for a greater
			// value, or the other way around. We need to iterate to the next
			// node in the respective direction. If there is no node, we fail.
			if (less)
				return Previous(node);
			return Next(node);
		}
	}

	return node;
}


/**
 * @brief Insert a node into the tree, rebalancing as necessary.
 * @param nodeToInsert Caller-owned node; its link fields are overwritten.
 * @return B_OK on success, B_BAD_VALUE if a duplicate key already exists.
 */
status_t
AVLTreeBase::Insert(AVLTreeNode* nodeToInsert)
{
	int result = _Insert(nodeToInsert);
	switch (result) {
		case OK:
		case HEIGHT_CHANGED:
			return B_OK;
		case NO_MEMORY:
			return B_NO_MEMORY;
		case DUPLICATE:
		default:
			return B_BAD_VALUE;
	}
}


/**
 * @brief Remove and return the node matching @a key.
 * @param key Key identifying the node to remove.
 * @return Removed node (still caller-owned), or NULL if not found.
 */
AVLTreeNode*
AVLTreeBase::Remove(const void* key)
{
	// find node
	AVLTreeNode* node = fRoot;
	while (node) {
		int cmp = fCompare->CompareKeyNode(key, node);
		if (cmp == 0)
			break;
		else {
			if (cmp < 0)
				node = node->left;
			else
				node = node->right;
		}
	}

	// remove it
	if (node)
		_Remove(node);

	return node;
}


/**
 * @brief Remove the specific node @a node from the tree.
 * @param node Node previously linked into this tree.
 * @return true on success, false if the node was not found.
 */
bool
AVLTreeBase::Remove(AVLTreeNode* node)
{
	switch (_Remove(node)) {
		case OK:
		case HEIGHT_CHANGED:
			return true;
		case NOT_FOUND:
		default:
			return false;
	}
}


/**
 * @brief Debug helper that verifies parent pointers, balance factors, and
 *        the node count.
 *
 * Panics via CHECK_FAILED() on any detected inconsistency.
 */
void
AVLTreeBase::CheckTree() const
{
	int nodeCount = 0;
	_CheckTree(NULL, fRoot, nodeCount);
	if (nodeCount != fNodeCount) {
		CHECK_FAILED("AVLTreeBase::CheckTree(): node count mismatch: %d vs %d",
			nodeCount, fNodeCount);
	}
}


/**
 * @brief Perform a single right rotation around *@a nodeP.
 *
 * Updates parent links and balance factors in place.
 *
 * @param nodeP Pointer-to-pointer at the pivot slot (so its parent link
 *              is updated too).
 */
void
AVLTreeBase::_RotateRight(AVLTreeNode** nodeP)
{
	// rotate the nodes
	AVLTreeNode* node = *nodeP;
	AVLTreeNode* left = node->left;

	*nodeP = left;

	left->parent = node->parent;
	node->left = left->right;
	if (left->right)
		left->right->parent = node;
	left->right = node;
	node->parent = left;

	// adjust the balance factors
	// former pivot
	if (left->balance_factor >= 0)
		node->balance_factor++;
	else
		node->balance_factor += 1 - left->balance_factor;

	// former left
	if (node->balance_factor <= 0)
		left->balance_factor++;
	else
		left->balance_factor += node->balance_factor + 1;
}


/**
 * @brief Perform a single left rotation around *@a nodeP.
 * @param nodeP Pointer-to-pointer at the pivot slot.
 */
void
AVLTreeBase::_RotateLeft(AVLTreeNode** nodeP)
{
	// rotate the nodes
	AVLTreeNode* node = *nodeP;
	AVLTreeNode* right = node->right;

	*nodeP = right;

	right->parent = node->parent;
	node->right = right->left;
	if (right->left)
		right->left->parent = node;
	right->left = node;
	node->parent = right;

	// adjust the balance factors
	// former pivot
	if (right->balance_factor <= 0)
		node->balance_factor--;
	else
		node->balance_factor -= right->balance_factor + 1;

	// former right
	if (node->balance_factor >= 0)
		right->balance_factor--;
	else
		right->balance_factor += node->balance_factor - 1;
}


/**
 * @brief Rebalance after an insert into the left subtree.
 * @param node Pointer-to-pointer to the node whose balance factor just
 *             decreased.
 * @return OK if the subtree height is unchanged, HEIGHT_CHANGED otherwise.
 */
int
AVLTreeBase::_BalanceInsertLeft(AVLTreeNode** node)
{
	if ((*node)->balance_factor < LEFT) {
		// tree is left heavy
		AVLTreeNode** left = &(*node)->left;
		if ((*left)->balance_factor == LEFT) {
			// left left heavy
			_RotateRight(node);
		} else {
			// left right heavy
			_RotateLeft(left);
			_RotateRight(node);
		}
		return OK;
	}

	if ((*node)->balance_factor == BALANCED)
		return OK;

	return HEIGHT_CHANGED;
}


/**
 * @brief Rebalance after an insert into the right subtree.
 * @param node Pointer-to-pointer to the node whose balance factor just
 *             increased.
 * @return OK if the subtree height is unchanged, HEIGHT_CHANGED otherwise.
 */
int
AVLTreeBase::_BalanceInsertRight(AVLTreeNode** node)
{
	if ((*node)->balance_factor > RIGHT) {
		// tree is right heavy
		AVLTreeNode** right = &(*node)->right;
		if ((*right)->balance_factor == RIGHT) {
			// right right heavy
			_RotateLeft(node);
		} else {
			// right left heavy
			_RotateRight(right);
			_RotateLeft(node);
		}
		return OK;
	}

	if ((*node)->balance_factor == BALANCED)
		return OK;

	return HEIGHT_CHANGED;
}


/**
 * @brief Iterative AVL insert that records the descent path on a stack.
 *
 * Walks the stack back up, adjusting balance factors and rotating until the
 * subtree height stops growing.
 *
 * @param nodeToInsert Node whose link fields get initialized here.
 * @return OK, HEIGHT_CHANGED, or DUPLICATE.
 */
int
AVLTreeBase::_Insert(AVLTreeNode* nodeToInsert)
{
	struct node_info {
		AVLTreeNode**	node;
		bool			left;
	};

	node_info stack[kMaxAVLTreeHeight];
	node_info* top = stack;
	const node_info* const bottom = stack;
	AVLTreeNode** node = &fRoot;

	// find insertion point
	while (*node) {
		int cmp = fCompare->CompareNodes(nodeToInsert, *node);
		if (cmp == 0)	// duplicate node
			return DUPLICATE;
		else {
			top->node = node;
			if (cmp < 0) {
				top->left = true;
				node = &(*node)->left;
			} else {
				top->left = false;
				node = &(*node)->right;
			}
			top++;
		}
	}

	// insert node
	*node = nodeToInsert;
	nodeToInsert->left = NULL;
	nodeToInsert->right = NULL;
	nodeToInsert->balance_factor = BALANCED;
	fNodeCount++;

	if (top == bottom)
		nodeToInsert->parent = NULL;
	else
		(*node)->parent = *top[-1].node;

	// do the balancing
	int result = HEIGHT_CHANGED;
	while (result == HEIGHT_CHANGED && top != bottom) {
		top--;
		node = top->node;
		if (top->left) {
			// left
			(*node)->balance_factor--;
			result = _BalanceInsertLeft(node);
		} else {
			// right
			(*node)->balance_factor++;
			result = _BalanceInsertRight(node);
		}
	}

	return result;
}


/**
 * @brief Rebalance after a remove from the left subtree.
 * @param node Pointer-to-pointer to the node whose balance factor just
 *             increased.
 * @return OK if propagation stops here, HEIGHT_CHANGED if the parent must
 *         also be rebalanced.
 */
int
AVLTreeBase::_BalanceRemoveLeft(AVLTreeNode** node)
{
	int result = HEIGHT_CHANGED;

	if ((*node)->balance_factor > RIGHT) {
		// tree is right heavy
		AVLTreeNode** right = &(*node)->right;
		if ((*right)->balance_factor == RIGHT) {
			// right right heavy
			_RotateLeft(node);
		} else if ((*right)->balance_factor == BALANCED) {
			// right none heavy
			_RotateLeft(node);
			result = OK;
		} else {
			// right left heavy
			_RotateRight(right);
			_RotateLeft(node);
		}
	} else if ((*node)->balance_factor == RIGHT)
		result = OK;

	return result;
}


/**
 * @brief Rebalance after a remove from the right subtree.
 * @param node Pointer-to-pointer to the node whose balance factor just
 *             decreased.
 * @return OK if propagation stops here, HEIGHT_CHANGED otherwise.
 */
int
AVLTreeBase::_BalanceRemoveRight(AVLTreeNode** node)
{
	int result = HEIGHT_CHANGED;

	if ((*node)->balance_factor < LEFT) {
		// tree is left heavy
		AVLTreeNode** left = &(*node)->left;
		if ((*left)->balance_factor == LEFT) {
			// left left heavy
			_RotateRight(node);
		} else if ((*left)->balance_factor == BALANCED) {
			// left none heavy
			_RotateRight(node);
			result = OK;
		} else {
			// left right heavy
			_RotateLeft(left);
			_RotateRight(node);
		}
	} else if ((*node)->balance_factor == LEFT)
		result = OK;

	return result;
}


/**
 * @brief Detach the rightmost node of the subtree rooted at *@a node.
 *
 * Used when removing an interior node: its in-order predecessor takes its
 * place. Rebalancing walks the recorded descent path upwards.
 *
 * @param node       Pointer-to-pointer at the subtree root.
 * @param foundNode  Out-pointer receiving the detached rightmost node.
 * @return OK or HEIGHT_CHANGED.
 */
int
AVLTreeBase::_RemoveRightMostChild(AVLTreeNode** node, AVLTreeNode** foundNode)
{
	AVLTreeNode** stack[kMaxAVLTreeHeight];
	AVLTreeNode*** top = stack;
	const AVLTreeNode* const* const* const bottom = stack;

	// find the child
	while ((*node)->right) {
		*top = node;
		top++;
		node = &(*node)->right;
	}

	// found the rightmost child: remove it
	// the found node might have a left child: replace the node with the
	// child
	*foundNode = *node;
	AVLTreeNode* left = (*node)->left;
	if (left)
		left->parent = (*node)->parent;
	*node = left;
	(*foundNode)->left = NULL;
	(*foundNode)->parent = NULL;

	// balancing
	int result = HEIGHT_CHANGED;
	while (result == HEIGHT_CHANGED && top != bottom) {
		top--;
		node = *top;
		(*node)->balance_factor--;
		result = _BalanceRemoveRight(node);
	}

	return result;
}


int
AVLTreeBase::_Remove(AVLTreeNode* node)
{
	if (!node)
		return NOT_FOUND;

	// remove node
	AVLTreeNode* parent = node->parent;
	bool isLeft = (parent && parent->left == node);
	AVLTreeNode** nodeP
		= (parent ? (isLeft ? &parent->left : &parent->right) : &fRoot);
	int result = HEIGHT_CHANGED;
	AVLTreeNode* replace = NULL;

	if (node->left && node->right) {
		// node has two children
		result = _RemoveRightMostChild(&node->left, &replace);
		replace->parent = parent;
		replace->left = node->left;
		replace->right = node->right;
		if (node->left)	// check necessary, if node->left == replace
			node->left->parent = replace;
		node->right->parent = replace;
		replace->balance_factor = node->balance_factor;
		*nodeP = replace;

		if (result == HEIGHT_CHANGED) {
			replace->balance_factor++;
			result = _BalanceRemoveLeft(nodeP);
		}
	} else if (node->left) {
		// node has only left child
		replace = node->left;
		replace->parent = parent;
		replace->balance_factor = node->balance_factor + 1;
		*nodeP = replace;
	} else if (node->right) {
		// node has only right child
		replace = node->right;
		replace->parent = node->parent;
		replace->balance_factor = node->balance_factor - 1;
		*nodeP = replace;
	} else {
		// node has no child
		*nodeP = NULL;
	}

	node->parent = node->left = node->right = NULL;
	fNodeCount--;

	// do the balancing
	while (result == HEIGHT_CHANGED && parent) {
		node = parent;
		parent = node->parent;
		bool oldIsLeft = isLeft;
		isLeft = (parent && parent->left == node);
		nodeP = (parent ? (isLeft ? &parent->left : &parent->right) : &fRoot);
		if (oldIsLeft) {
			// left
			node->balance_factor++;
			result = _BalanceRemoveLeft(nodeP);
		} else {
			// right
			node->balance_factor--;
			result = _BalanceRemoveRight(nodeP);
		}
	}

	return result;
}


int
AVLTreeBase::_CheckTree(AVLTreeNode* parent, AVLTreeNode* node,
	int& _nodeCount) const
{
	if (node == NULL) {
		_nodeCount = 0;
		return 0;
	}

	if (parent != node->parent) {
		CHECK_FAILED("AVLTreeBase::_CheckTree(): node %p parent mismatch: "
			"%p vs %p", node, parent, node->parent);
	}

	int leftNodeCount;
	int leftDepth = _CheckTree(node, node->left, leftNodeCount);

	int rightNodeCount;
	int rightDepth = _CheckTree(node, node->right, rightNodeCount);

	int balance = rightDepth - leftDepth;
	if (balance < LEFT || balance > RIGHT) {
		CHECK_FAILED("AVLTreeBase::_CheckTree(): unbalanced subtree: %p", node);
	} else if (balance != node->balance_factor) {
		CHECK_FAILED("AVLTreeBase::_CheckTree(): subtree %p balance mismatch: "
			"%d vs %d", node, balance, node->balance_factor);
	}

	_nodeCount = leftNodeCount + rightNodeCount + 1;
	return max_c(leftDepth, rightDepth) + 1;
}
