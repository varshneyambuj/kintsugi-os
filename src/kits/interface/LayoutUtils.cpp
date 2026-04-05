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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2006-2014 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       John Scipione, jscipione@gmail.com
 *       Ingo Weinhold, ingo_weinhold@gmx.de
 */


/**
 * @file LayoutUtils.cpp
 * @brief Utility functions for the layout system
 *
 * Provides helper functions used throughout the layout framework, including
 * size constraint arithmetic (ComposeSize, AddSizes) and alignment computation
 * helpers.
 *
 * @see BLayout, BLayoutItem, BSize
 */


#include <LayoutUtils.h>

#include <algorithm>

#include <ClassInfo.h>
#include <Layout.h>
#include <View.h>

#include "ViewLayoutItem.h"


// // AddSizesFloat
// float
// BLayoutUtils::AddSizesFloat(float a, float b)
// {
// 	float sum = a + b + 1;
// 	if (sum >= B_SIZE_UNLIMITED)
// 		return B_SIZE_UNLIMITED;
//
// 	return sum;
// }
//
// // AddSizesFloat
// float
// BLayoutUtils::AddSizesFloat(float a, float b, float c)
// {
// 	return AddSizesFloat(AddSizesFloat(a, b), c);
// }


/**
 * @brief Adds two layout pixel dimensions, clamping to B_SIZE_UNLIMITED on overflow.
 *
 * Layout integer dimensions encode pixel extents where the physical pixel count
 * is (value + 1). This function performs saturating addition so that the result
 * never exceeds B_SIZE_UNLIMITED.
 *
 * @param a First integer dimension.
 * @param b Second integer dimension.
 * @return The sum @c a+b, clamped to @c (int32)B_SIZE_UNLIMITED.
 * @see AddSizesInt32(int32, int32, int32)
 */
int32
BLayoutUtils::AddSizesInt32(int32 a, int32 b)
{
	if (a >= (int32)B_SIZE_UNLIMITED - b)
		return (int32)B_SIZE_UNLIMITED;
	return a + b;
}


/**
 * @brief Adds three layout pixel dimensions, clamping to B_SIZE_UNLIMITED on overflow.
 *
 * Equivalent to calling the two-argument overload twice. Useful when combining
 * two spacing values with a content dimension.
 *
 * @param a First integer dimension.
 * @param b Second integer dimension.
 * @param c Third integer dimension.
 * @return The saturating sum @c a+b+c.
 * @see AddSizesInt32(int32, int32)
 */
int32
BLayoutUtils::AddSizesInt32(int32 a, int32 b, int32 c)
{
	return AddSizesInt32(AddSizesInt32(a, b), c);
}


/**
 * @brief Adds two layout float distances, clamping to B_SIZE_UNLIMITED on overflow.
 *
 * Float layout distances use the convention that a distance of @c d corresponds
 * to @c (d+1) pixels, so this function adds 1 to the raw sum. The result is
 * clamped to B_SIZE_UNLIMITED.
 *
 * @param a First float distance.
 * @param b Second float distance.
 * @return The sum @c a+b+1, or @c B_SIZE_UNLIMITED if the result would overflow.
 * @see AddDistances(float, float, float)
 */
float
BLayoutUtils::AddDistances(float a, float b)
{
	float sum = a + b + 1;
	if (sum >= B_SIZE_UNLIMITED)
		return B_SIZE_UNLIMITED;

	return sum;
}


/**
 * @brief Adds three layout float distances, clamping to B_SIZE_UNLIMITED on overflow.
 *
 * Equivalent to calling the two-argument overload twice.
 *
 * @param a First float distance.
 * @param b Second float distance.
 * @param c Third float distance.
 * @return The saturating sum of all three distances.
 * @see AddDistances(float, float)
 */
float
BLayoutUtils::AddDistances(float a, float b, float c)
{
	return AddDistances(AddDistances(a, b), c);
}


/**
 * @brief Subtracts two layout integer dimensions.
 *
 * Returns @c a-b, or @c 0 if @a a is less than @a b to prevent negative
 * dimensions.
 *
 * @param a The dimension to subtract from.
 * @param b The dimension to subtract.
 * @return @c a-b, clamped to 0 from below.
 */
int32
BLayoutUtils::SubtractSizesInt32(int32 a, int32 b)
{
	if (a < b)
		return 0;
	return a - b;
}


/**
 * @brief Subtracts two layout float distances.
 *
 * Accounts for the distance encoding convention (distances are @c pixel-1) by
 * subtracting an additional 1 from the result. Returns @c -1 if @a a is less
 * than @a b, indicating an empty or invalid span.
 *
 * @param a The distance to subtract from.
 * @param b The distance to subtract.
 * @return @c a-b-1, or @c -1 if @a a < @a b.
 */
float
BLayoutUtils::SubtractDistances(float a, float b)
{
	if (a < b)
		return -1;
	return a - b - 1;
}


/**
 * @brief Clamps a set of size constraints so that they form a valid, ordered triple.
 *
 * Ensures @c max >= min, then clamps @c preferred into [min, max]. This
 * overload operates on plain floats and is typically used when processing a
 * single dimension (width or height) independently.
 *
 * @param min       On entry, the minimum value. Unchanged on exit.
 * @param max       On entry, the maximum value. Clamped to >= @a min on exit.
 * @param preferred On entry, the preferred value. Clamped to [min, max] on exit.
 */
void
BLayoutUtils::FixSizeConstraints(float& min, float& max, float& preferred)
{
	if (max < min)
		max = min;
	if (preferred < min)
		preferred = min;
	else if (preferred > max)
		preferred = max;
}


/**
 * @brief Clamps a set of BSize constraints so that they form a valid, ordered triple.
 *
 * Applies the scalar FixSizeConstraints() overload independently to the width
 * and height dimensions of each BSize.
 *
 * @param min       On entry, the minimum BSize. Unchanged on exit.
 * @param max       On entry, the maximum BSize. Clamped to >= @a min on exit.
 * @param preferred On entry, the preferred BSize. Clamped to [min, max] on exit.
 * @see FixSizeConstraints(float&, float&, float&)
 */
void
BLayoutUtils::FixSizeConstraints(BSize& min, BSize& max, BSize& preferred)
{
	FixSizeConstraints(min.width, max.width, preferred.width);
	FixSizeConstraints(min.height, max.height, preferred.height);
}


/**
 * @brief Composes an explicit size override with a layout-computed size.
 *
 * For each dimension, the value from @a size is used if it is set; otherwise
 * the corresponding dimension from @a layoutSize is used. This allows explicit
 * overrides set by the user to shadow the layout engine's computation without
 * requiring a full replacement of the size struct.
 *
 * @param size       The explicit (override) size, possibly with unset dimensions.
 * @param layoutSize The fallback size from the layout engine.
 * @return A BSize with each dimension resolved to the override or the fallback.
 * @see BAbstractLayoutItem::MinSize()
 */
BSize
BLayoutUtils::ComposeSize(BSize size, BSize layoutSize)
{
	if (!size.IsWidthSet())
		size.width = layoutSize.width;
	if (!size.IsHeightSet())
		size.height = layoutSize.height;

	return size;
}


/**
 * @brief Composes an explicit alignment override with a layout-computed alignment.
 *
 * For each axis, the value from @a alignment is used if it is set; otherwise
 * the corresponding axis from @a layoutAlignment is used.
 *
 * @param alignment       The explicit (override) alignment, possibly with unset axes.
 * @param layoutAlignment The fallback alignment from the layout engine.
 * @return A BAlignment with each axis resolved to the override or the fallback.
 * @see BAbstractLayoutItem::Alignment()
 */
BAlignment
BLayoutUtils::ComposeAlignment(BAlignment alignment, BAlignment layoutAlignment)
{
	if (!alignment.IsHorizontalSet())
		alignment.horizontal = layoutAlignment.horizontal;
	if (!alignment.IsVerticalSet())
		alignment.vertical = layoutAlignment.vertical;

	return alignment;
}


/**
 * @brief Aligns a rectangle within an available frame according to a max size and alignment.
 *
 * Restricts the resulting rectangle's dimensions to those of @a maxSize, then
 * positions it within @a frame according to @a alignment. If a dimension of
 * @a maxSize is B_ALIGN_USE_FULL_WIDTH / B_ALIGN_USE_FULL_HEIGHT, the full
 * frame extent is used for that axis.
 *
 * @param frame     The available area offered by the layout engine.
 * @param maxSize   The maximum permitted size; restricts the output rectangle.
 * @param alignment How to position the rectangle within @a frame.
 * @return The aligned and size-constrained rectangle.
 * @see AlignInFrame(BView*, BRect)
 */
BRect
BLayoutUtils::AlignInFrame(BRect frame, BSize maxSize, BAlignment alignment)
{
	// align according to the given alignment
	if (maxSize.width < frame.Width()
		&& alignment.horizontal != B_ALIGN_USE_FULL_WIDTH) {
		frame.left += (int)((frame.Width() - maxSize.width)
			* alignment.RelativeHorizontal());
		frame.right = frame.left + maxSize.width;
	}
	if (maxSize.height < frame.Height()
		&& alignment.vertical != B_ALIGN_USE_FULL_HEIGHT) {
		frame.top += (int)((frame.Height() - maxSize.height)
			* alignment.RelativeVertical());
		frame.bottom = frame.top + maxSize.height;
	}

	return frame;
}


/**
 * @brief Moves and resizes a BView to fill an available frame, respecting its constraints.
 *
 * Reads the view's MaxSize() and LayoutAlignment(), resolves height-for-width
 * if applicable, and then calls the BRect overload to compute the final frame.
 * The view is repositioned and resized via MoveTo() and ResizeTo().
 *
 * @param view  The view to position and resize.
 * @param frame The available area offered by the layout engine.
 * @see AlignInFrame(BRect, BSize, BAlignment)
 */
void
BLayoutUtils::AlignInFrame(BView* view, BRect frame)
{
	BSize maxSize = view->MaxSize();
	BAlignment alignment = view->LayoutAlignment();
	if (view->HasHeightForWidth()) {
		// The view has height for width, so we do the horizontal alignment
		// ourselves and restrict the height max constraint respectively.
		if (maxSize.width < frame.Width()
			&& alignment.horizontal != B_ALIGN_USE_FULL_WIDTH) {
			frame.OffsetBy(floorf((frame.Width() - maxSize.width)
				* alignment.RelativeHorizontal()), 0);
			frame.right = frame.left + maxSize.width;
		}
		alignment.horizontal = B_ALIGN_USE_FULL_WIDTH;
		float minHeight;
		float maxHeight;
		float preferredHeight;
		view->GetHeightForWidth(frame.Width(), &minHeight, &maxHeight,
			&preferredHeight);
		frame.bottom = frame.top + std::max(frame.Height(), minHeight);
		maxSize.height = minHeight;
	}
	frame = AlignInFrame(frame, maxSize, alignment);
	view->MoveTo(frame.LeftTop());
	view->ResizeTo(frame.Size());
}


/**
 * @brief Centers a rectangle of a given size on a reference rect, possibly exceeding its bounds.
 *
 * Unlike AlignInFrame(), this function does not clamp the output to @a rect.
 * The result may be larger than @a rect when @a size exceeds the rect's
 * dimensions. Useful for computing ideal positions without enforcing containment.
 *
 * @param rect      The reference rectangle to align within.
 * @param size      The desired size of the output rectangle.
 * @param alignment How to position the output rectangle relative to @a rect.
 * @return The aligned rectangle, which may extend beyond @a rect.
 * @see AlignInFrame(BRect, BSize, BAlignment)
 */
BRect
BLayoutUtils::AlignOnRect(BRect rect, BSize size, BAlignment alignment)
{
	rect.left += (int)((rect.Width() - size.width)
		* alignment.RelativeHorizontal());
	rect.top += (int)(((rect.Height() - size.height))
		* alignment.RelativeVertical());
	rect.right = rect.left + size.width;
	rect.bottom = rect.top + size.height;

	return rect;
}


/*!	Offsets a rectangle's location so that it lies fully in a given rectangular
	frame.

	If the rectangle is too wide/high to fully fit in the frame, its left/top
	edge is offset to 0. The rect's size always remains unchanged.

	\param rect The rectangle to be moved.
	\param frameSize The size of the frame the rect shall be moved into. The
		frame's left-top is (0, 0).
	\return The modified rect.
*/
/**
 * @brief Offsets a rectangle so that it lies fully within a frame of the given size.
 *
 * If the rectangle is too large to fit, its left or top edge is clamped to 0.
 * The rectangle's size is never modified.
 *
 * @param rect      The rectangle to reposition.
 * @param frameSize The size of the containing frame; its origin is (0, 0).
 * @return The repositioned rectangle, with the same size as @a rect.
 */
/*static*/ BRect
BLayoutUtils::MoveIntoFrame(BRect rect, BSize frameSize)
{
	BPoint leftTop(rect.LeftTop());

	// enforce horizontal limits; favor left edge
	if (rect.right > frameSize.width)
		leftTop.x -= rect.right - frameSize.width;
	if (leftTop.x < 0)
		leftTop.x = 0;

	// enforce vertical limits; favor top edge
	if (rect.bottom > frameSize.height)
		leftTop.y -= rect.bottom - frameSize.height;
	if (leftTop.y < 0)
		leftTop.y = 0;

	return rect.OffsetToSelf(leftTop);
}


/**
 * @brief Returns a multi-line string representation of the layout tree rooted at a BView.
 *
 * Recursively descends through the view hierarchy (and any attached layouts),
 * formatting each node's frame, min, max, and preferred sizes into a human-readable
 * indented string for debugging purposes.
 *
 * @param view The root view of the subtree to dump.
 * @return A BString containing the formatted layout tree.
 * @see GetLayoutTreeDump(BLayoutItem*)
 */
/*static*/ BString
BLayoutUtils::GetLayoutTreeDump(BView* view)
{
	BString result;
	_GetLayoutTreeDump(view, 0, result);
	return result;
}


/**
 * @brief Returns a multi-line string representation of the layout tree rooted at a BLayoutItem.
 *
 * Recursively descends through the item hierarchy, formatting each node's frame,
 * min, max, and preferred sizes into a human-readable indented string for
 * debugging purposes.
 *
 * @param item The root layout item of the subtree to dump.
 * @return A BString containing the formatted layout tree.
 * @see GetLayoutTreeDump(BView*)
 */
/*static*/ BString
BLayoutUtils::GetLayoutTreeDump(BLayoutItem* item)
{
	BString result;
	_GetLayoutTreeDump(item, 0, false, result);
	return result;
}


/**
 * @brief Internal recursive helper that formats a BView subtree into @a _output.
 *
 * Appends the view's class name, pointer, frame, and size constraints at the
 * given indentation @a level, then recurses into the view's attached layout or
 * direct children.
 *
 * @param view    The view to format.
 * @param level   Current indentation depth (each level adds 4 spaces).
 * @param _output The BString to append formatted output to.
 */
/*static*/ void
BLayoutUtils::_GetLayoutTreeDump(BView* view, int level, BString& _output)
{
	BString indent;
	indent.SetTo(' ', level * 4);

	if (view == NULL) {
		_output << indent << "<null view>\n";
		return;
	}

	BRect frame = view->Frame();
	BSize min = view->MinSize();
	BSize max = view->MaxSize();
	BSize preferred = view->PreferredSize();
	_output << BString().SetToFormat(
		"%sview %p (%s %s):\n"
		"%s  frame: (%f, %f, %f, %f)\n"
		"%s  min:   (%f, %f)\n"
		"%s  max:   (%f, %f)\n"
		"%s  pref:  (%f, %f)\n",
		indent.String(), view, class_name(view), view->Name(),
		indent.String(), frame.left, frame.top, frame.right, frame.bottom,
		indent.String(), min.width, min.height,
		indent.String(), max.width, max.height,
		indent.String(), preferred.width, preferred.height);

	if (BLayout* layout = view->GetLayout()) {
		_GetLayoutTreeDump(layout, level, true, _output);
		return;
	}

	int32 count = view->CountChildren();
	for (int32 i = 0; i < count; i++) {
		_output << indent << "    ---\n";
		_GetLayoutTreeDump(view->ChildAt(i), level + 1, _output);
	}
}


/**
 * @brief Internal recursive helper that formats a BLayoutItem subtree into @a _output.
 *
 * If the item is a BViewLayoutItem, delegates to the BView overload. Otherwise
 * appends the item's (or layout's) class name, pointer, frame, and size
 * constraints at the given indentation level, then recurses into child items.
 *
 * @param item         The layout item to format.
 * @param level        Current indentation depth (each level adds 4 spaces).
 * @param isViewLayout @c true when the item is the layout directly owned by a view,
 *                     which changes the output prefix.
 * @param _output      The BString to append formatted output to.
 */
/*static*/ void
BLayoutUtils::_GetLayoutTreeDump(BLayoutItem* item, int level,
	bool isViewLayout, BString& _output)
{
	if (BViewLayoutItem* viewItem = dynamic_cast<BViewLayoutItem*>(item)) {
		_GetLayoutTreeDump(viewItem->View(), level, _output);
		return;
	}

	BString indent;
	indent.SetTo(' ', level * 4);

	if (item == NULL) {
		_output << indent << "<null item>\n";
		return;
	}

	BLayout* layout = dynamic_cast<BLayout*>(item);
	BRect frame = item->Frame();
	BSize min = item->MinSize();
	BSize max = item->MaxSize();
	BSize preferred = item->PreferredSize();
	if (isViewLayout) {
		_output << indent << BString().SetToFormat("  [layout %p (%s)]\n",
			layout, class_name(layout));
	} else {
		_output << indent << BString().SetToFormat("item %p (%s):\n",
			item, class_name(item));
	}
	_output << BString().SetToFormat(
		"%s  frame: (%f, %f, %f, %f)\n"
		"%s  min:   (%f, %f)\n"
		"%s  max:   (%f, %f)\n"
		"%s  pref:  (%f, %f)\n",
		indent.String(), frame.left, frame.top, frame.right, frame.bottom,
		indent.String(), min.width, min.height,
		indent.String(), max.width, max.height,
		indent.String(), preferred.width, preferred.height);

	if (layout == NULL)
		return;

	int32 count = layout->CountItems();
	for (int32 i = 0; i < count; i++) {
		_output << indent << "    ---\n";
		_GetLayoutTreeDump(layout->ItemAt(i), level + 1, false, _output);
	}
}
