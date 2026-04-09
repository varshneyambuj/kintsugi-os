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
 *   CQuantizer (c) 1996-1997 Jeff Prosise
 *   Modifications: Davide Pizzolato (31/08/2003), David Powell, Stephan Aßmus
 *   Permission is given by the author to freely redistribute and include
 *   this code in any program as long as this credit is given where due.
 *
 *   Authors:
 *       Jeff Prosise
 *       Davide Pizzolato
 *       David Powell
 *       Stephan Aßmus
 */

/** @file ColorQuantizer.cpp
 *  @brief Octree-based colour quantizer that reduces an arbitrary RGBA image
 *         to at most N representative colours using adaptive palette reduction.
 */

#include "ColorQuantizer.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>


/** @brief Clamps a float colour component to the range [0, 255] and rounds it.
 *  @param component  The floating-point component value.
 *  @return The clamped uint8 value.
 */
static inline uint8
clip(float component)
{
	if (component > 255.0)
		return 255;

	return (uint8)(component + 0.5f);
}

/** @brief Internal octree node used during colour quantisation.
 *
 *  Leaf nodes accumulate pixel statistics (sum of RGB/A components and count).
 *  Non-leaf nodes store pointers to up to eight children (one per bit triplet).
 */
struct BColorQuantizer::Node {
	bool			isLeaf;		// TRUE if node has no children
	uint32			pixelCount;	// Number of pixels represented by this leaf
	uint32			sumR;		// Sum of red components
	uint32			sumG;		// Sum of green components
	uint32			sumB;		// Sum of blue components
	uint32			sumA;		// Sum of alpha components
	Node*			child[8];	// Pointers to child nodes
	Node*			next;		// Pointer to next reducible node
};


/** @brief Constructs the colour quantizer with the given constraints.
 *
 *  @p maxColors is clamped to a minimum of 16 to ensure the octree can
 *  always produce a reasonable palette. @p bitsPerColor is clamped to 8.
 *
 *  @param maxColors    Maximum number of colours in the output palette.
 *  @param bitsPerColor Precision of the octree (1–8 bits per colour channel).
 */
BColorQuantizer::BColorQuantizer(uint32 maxColors, uint32 bitsPerColor)
	: fTree(NULL),
	  fLeafCount(0),
	  fMaxColors(maxColors),
	  fOutputMaxColors(maxColors),
	  fBitsPerColor(bitsPerColor)
{
	// override parameters if out of range
	if (fBitsPerColor > 8)
		fBitsPerColor = 8;

	if (fMaxColors < 16)
		fMaxColors = 16;

	for (int i = 0; i <= (int)fBitsPerColor; i++)
		fReducibleNodes[i] = NULL;
}


/** @brief Destroys the quantizer and frees the entire octree. */
BColorQuantizer::~BColorQuantizer()
{
	if (fTree != NULL)
		_DeleteTree(&fTree);
}


/** @brief Processes rows of BGR pixels and adds them to the octree.
 *
 *  Each pixel is inserted into the octree via _AddColor(). When the leaf
 *  count exceeds fMaxColors the tree is reduced by merging the deepest
 *  reducible sibling group.
 *
 *  @param rowPtrs  Array of @p height pointers, each pointing to a row of
 *                  @p width * 3 bytes in BGR order.
 *  @param width    Width of the image in pixels (stride is width * 3 bytes).
 *  @param height   Height of the image in pixels (number of row pointers).
 *  @return Always returns true.
 */
bool
BColorQuantizer::ProcessImage(const uint8* const * rowPtrs, int width,
	int height)
{
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x += 3) {
			uint8 b = rowPtrs[y][x];
			uint8 g = rowPtrs[y][x + 1];
			uint8 r = rowPtrs[y][x + 2];
			_AddColor(&fTree, r, g, b, 0, fBitsPerColor, 0, &fLeafCount,
				fReducibleNodes);

			while (fLeafCount > fMaxColors)
				_ReduceTree(fBitsPerColor, &fLeafCount, fReducibleNodes);
		}
	}

	return true;
}


/** @brief Returns the number of distinct colours currently in the octree.
 *  @return The current leaf count.
 */
uint32
BColorQuantizer::GetColorCount() const
{
	return fLeafCount;
}


/** @brief Fills @p table with the quantised palette colours.
 *
 *  When fOutputMaxColors < 16 adjacent leaf clusters are merged by a
 *  weighted average to further reduce the palette size. Otherwise the
 *  palette is extracted directly from the leaf nodes.
 *
 *  @param table  Output array of at least fOutputMaxColors RGBA entries.
 */
void
BColorQuantizer::GetColorTable(RGBA* table) const
{
	uint32 index = 0;
	if (fOutputMaxColors < 16) {
		uint32 sums[16];
		RGBA tmpPalette[16];
		_GetPaletteColors(fTree, tmpPalette, &index, sums);
		if (fLeafCount > fOutputMaxColors) {
			for (uint32 j = 0; j < fOutputMaxColors; j++) {
				uint32 a = (j * fLeafCount) / fOutputMaxColors;
				uint32 b = ((j + 1) * fLeafCount) / fOutputMaxColors;
				uint32 nr = 0;
				uint32 ng = 0;
				uint32 nb = 0;
				uint32 na = 0;
				uint32 ns = 0;
				for (uint32 k = a; k < b; k++){
					nr += tmpPalette[k].r * sums[k];
					ng += tmpPalette[k].g * sums[k];
					nb += tmpPalette[k].b * sums[k];
					na += tmpPalette[k].a * sums[k];
					ns += sums[k];
				}
				table[j].r = clip((float)nr / ns);
				table[j].g = clip((float)ng / ns);
				table[j].b = clip((float)nb / ns);
				table[j].a = clip((float)na / ns);
			}
		} else {
			memcpy(table, tmpPalette, fLeafCount * sizeof(RGBA));
		}
	} else {
		_GetPaletteColors(fTree, table, &index, NULL);
	}
}


// #pragma mark - private


/** @brief Inserts an RGBA pixel into the octree, creating nodes as needed.
 *
 *  At each level the 3-bit octant index is derived from the MSB of the
 *  red, green, and blue components. New nodes are created by _CreateNode().
 *
 *  @param _node           In/out pointer to the current node pointer.
 *  @param r               Red component of the pixel.
 *  @param g               Green component of the pixel.
 *  @param b               Blue component of the pixel.
 *  @param a               Alpha component of the pixel.
 *  @param bitsPerColor    Tree depth limit.
 *  @param level           Current recursion depth (0 = root).
 *  @param _leafCount      In/out counter of leaf nodes.
 *  @param reducibleNodes  Per-level list of reducible (non-leaf) nodes.
 */
void
BColorQuantizer::_AddColor(Node** _node, uint8 r, uint8 g, uint8 b, uint8 a,
	uint32 bitsPerColor, uint32 level, uint32* _leafCount,
	Node** reducibleNodes)
{
	static const uint8 kMask[8]
		= {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};

	// If the node doesn't exist, create it.
	if (*_node == NULL)
		*_node = _CreateNode(level, bitsPerColor, _leafCount, reducibleNodes);

	// Update color information if it's a leaf node.
	if ((*_node)->isLeaf) {
		(*_node)->pixelCount++;
		(*_node)->sumR += r;
		(*_node)->sumG += g;
		(*_node)->sumB += b;
		(*_node)->sumA += a;
	} else {
		// Recurse a level deeper if the node is not a leaf.
		int shift = 7 - level;
		int index = (((r & kMask[level]) >> shift) << 2) |
					 (((g & kMask[level]) >> shift) << 1) |
					 (( b & kMask[level]) >> shift);
		_AddColor(&((*_node)->child[index]), r, g, b, a, bitsPerColor,
			level + 1, _leafCount, reducibleNodes);
	}
}


/** @brief Allocates and initialises a new octree node.
 *
 *  Nodes at the maximum depth are leaves; all others are added to the
 *  reducible-nodes list for their level.
 *
 *  @param level           Depth at which the node is created.
 *  @param bitsPerColor    Maximum tree depth.
 *  @param _leafCount      In/out leaf counter; incremented for new leaf nodes.
 *  @param reducibleNodes  Per-level list prepended with this node if non-leaf.
 *  @return Pointer to the new node, or NULL on allocation failure.
 */
BColorQuantizer::Node*
BColorQuantizer::_CreateNode(uint32 level, uint32 bitsPerColor,
	uint32* _leafCount, Node** reducibleNodes)
{
	Node* node = (Node*)calloc(1, sizeof(Node));

	if (node == NULL)
		return NULL;

	node->isLeaf = (level == bitsPerColor) ? true : false;
	if (node->isLeaf)
		(*_leafCount)++;
	else {
		node->next = reducibleNodes[level];
		reducibleNodes[level] = node;
	}
	return node;
}


/** @brief Merges the shallowest reducible node's children into itself.
 *
 *  Finds the deepest level with a reducible node, removes it from the
 *  list, sums the child statistics into the parent, frees all children,
 *  and marks the parent as a leaf. The leaf count is reduced by
 *  (childCount - 1).
 *
 *  @param bitsPerColor    Tree depth limit used to start the search.
 *  @param _leafCount      In/out leaf counter; adjusted after reduction.
 *  @param reducibleNodes  Per-level reducible-node lists.
 */
void
BColorQuantizer::_ReduceTree(uint32 bitsPerColor, uint32* _leafCount,
	Node** reducibleNodes)
{
	int i = bitsPerColor - 1;
	// Find the deepest level containing at least one reducible node.
	for (; i > 0 && reducibleNodes[i] == NULL; i--)
		;

	// Reduce the node most recently added to the list at level i.
	Node* node = reducibleNodes[i];
	reducibleNodes[i] = node->next;

	uint32 sumR = 0;
	uint32 sumG = 0;
	uint32 sumB = 0;
	uint32 sumA = 0;
	uint32 childCount = 0;

	for (i = 0; i < 8; i++) {
		if (node->child[i] != NULL) {
			sumR += node->child[i]->sumR;
			sumG += node->child[i]->sumG;
			sumB += node->child[i]->sumB;
			sumA += node->child[i]->sumA;
			node->pixelCount += node->child[i]->pixelCount;

			free(node->child[i]);
			node->child[i] = NULL;

			childCount++;
		}
	}

	node->isLeaf = true;
	node->sumR = sumR;
	node->sumG = sumG;
	node->sumB = sumB;
	node->sumA = sumA;

	*_leafCount -= (childCount - 1);
}


/** @brief Recursively frees an entire octree.
 *
 *  Traverses the tree depth-first, freeing all nodes and setting @p _node
 *  to NULL after each free.
 *
 *  @param _node  In/out pointer to the root of the sub-tree to delete.
 */
void
BColorQuantizer::_DeleteTree(Node** _node)
{
	for (int i = 0; i < 8; i++) {
		if ((*_node)->child[i] != NULL)
			_DeleteTree(&((*_node)->child[i]));
	}
	free(*_node);
	*_node = NULL;
}


/** @brief Walks the octree and fills @p table with the average colour of each leaf.
 *
 *  Average colour is computed as sumX / pixelCount for each channel.
 *  If @p sums is non-NULL the pixel count of each leaf is stored there.
 *
 *  @param node    Current node in the depth-first traversal (may be NULL).
 *  @param table   Output palette array filled left-to-right.
 *  @param _index  Running index into @p table; incremented for each leaf.
 *  @param sums    Optional output array of per-leaf pixel counts, or NULL.
 */
void
BColorQuantizer::_GetPaletteColors(Node* node, RGBA* table, uint32* _index,
	uint32* sums) const
{
	if (node == NULL)
		return;

	if (node->isLeaf) {
		table[*_index].r = clip((float)node->sumR / node->pixelCount);
		table[*_index].g = clip((float)node->sumG / node->pixelCount);
		table[*_index].b = clip((float)node->sumB / node->pixelCount);
		table[*_index].a = clip((float)node->sumA / node->pixelCount);
		if (sums)
			sums[*_index] = node->pixelCount;
		(*_index)++;
	} else {
		for (int i = 0; i < 8; i++) {
			if (node->child[i] != NULL)
				_GetPaletteColors(node->child[i], table, _index, sums);
		}
	}
}
