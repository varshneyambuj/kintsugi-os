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
 *   Copyright 2006-2009 Ingo Weinhold / Copyright 2015 Rene Gollent.
 *   All rights reserved. Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 *       Rene Gollent, rene@gollent.com
 */


/**
 * @file SplitLayout.cpp
 * @brief Implementation of BSplitLayout, a resizable split-pane layout manager
 *
 * BSplitLayout arranges items in a horizontal or vertical row separated by
 * draggable dividers, allowing the user to resize panes interactively. Items
 * can be weighted to control initial size distribution.
 *
 * @see BSplitView, BLayout
 */


#include "SplitLayout.h"

#include <new>
#include <stdio.h>

#include <ControlLook.h>
#include <LayoutItem.h>
#include <LayoutUtils.h>
#include <Message.h>
#include <View.h>

#include "OneElementLayouter.h"
#include "SimpleLayouter.h"


using std::nothrow;


// archivng constants
namespace {
	/** @brief Archive field name for the per-item collapsible flag. */
	const char* const kItemCollapsibleField = "BSplitLayout:item:collapsible";
	/** @brief Archive field name for the per-item size weight. */
	const char* const kItemWeightField = "BSplitLayout:item:weight";
	/** @brief Archive field name for the inter-item spacing value. */
	const char* const kSpacingField = "BSplitLayout:spacing";
	/** @brief Archive field name for the splitter bar thickness. */
	const char* const kSplitterSizeField = "BSplitLayout:splitterSize";
	/** @brief Archive field name storing whether orientation is vertical. */
	const char* const kIsVerticalField = "BSplitLayout:vertical";
	/** @brief Archive field name storing the layout insets as a BRect. */
	const char* const kInsetsField = "BSplitLayout:insets";
}


/**
 * @brief Per-item layout metadata stored as the item's opaque layout data.
 *
 * Holds the resolved size constraints and current visibility state for a
 * single content item or splitter bar so that BSplitLayout can perform
 * interactive dragging without a full re-layout pass.
 */
class BSplitLayout::ItemLayoutInfo {
public:
	/** @brief Relative size weight used by the layouter to distribute space. */
	float		weight;
	/** @brief Pixel frame assigned to this item during the last DoLayout(). */
	BRect		layoutFrame;
	/** @brief Cached minimum size (updated each DoLayout pass). */
	BSize		min;
	/** @brief Cached maximum size (updated each DoLayout pass). */
	BSize		max;
	/** @brief True when the item occupies space in the layout; false when collapsed. */
	bool		isVisible;
	/** @brief Whether the user may collapse this item by dragging a splitter. */
	bool		isCollapsible;

	/**
	 * @brief Construct an ItemLayoutInfo with default values.
	 *
	 * The item starts visible, collapsible, with weight 1.0 and an empty frame.
	 */
	ItemLayoutInfo()
		:
		weight(1.0f),
		layoutFrame(0, 0, -1, -1),
		min(),
		max(),
		isVisible(true),
		isCollapsible(true)
	{
	}
};


/**
 * @brief Captures the size constraints and current sizes of the two items
 *        adjacent to a splitter during an interactive drag operation.
 *
 * All values are expressed in pixels (integer distances, not BLayout
 * pixel-minus-one distances).
 */
class BSplitLayout::ValueRange {
public:
	/** @brief Combined pixel size of both items plus their spacing slots. */
	int32 sumValue;	// including spacing
	/** @brief Minimum pixel size of the item before the splitter. */
	int32 previousMin;
	/** @brief Maximum pixel size of the item before the splitter. */
	int32 previousMax;
	/** @brief Current pixel size of the item before the splitter. */
	int32 previousSize;
	/** @brief Minimum pixel size of the item after the splitter. */
	int32 nextMin;
	/** @brief Maximum pixel size of the item after the splitter. */
	int32 nextMax;
	/** @brief Current pixel size of the item after the splitter. */
	int32 nextSize;
};


/**
 * @brief Internal BLayoutItem subclass representing a draggable splitter bar.
 *
 * SplitterItem sits between every pair of adjacent content items in the layout
 * and reports a fixed thickness equal to BSplitLayout::SplitterSize() in the
 * primary axis while expanding freely in the cross axis.  Explicit size and
 * visibility overrides are intentionally ignored so the splitter always matches
 * the layout's current settings.
 */
class BSplitLayout::SplitterItem : public BLayoutItem {
public:
	/**
	 * @brief Construct a SplitterItem owned by the given BSplitLayout.
	 * @param layout The parent layout that controls this splitter's geometry.
	 */
	SplitterItem(BSplitLayout* layout)
		:
		fLayout(layout),
		fFrame()
	{
	}


	/**
	 * @brief Return the minimum size for the splitter bar.
	 *
	 * The thickness in the primary axis equals SplitterSize()-1 (BLayout
	 * pixel-minus-one convention). The cross-axis minimum is unconstrained.
	 *
	 * @return Minimum BSize of the splitter bar.
	 */
	virtual BSize MinSize()
	{
		if (fLayout->Orientation() == B_HORIZONTAL)
			return BSize(fLayout->SplitterSize() - 1, -1);
		else
			return BSize(-1, fLayout->SplitterSize() - 1);
	}

	/**
	 * @brief Return the maximum size for the splitter bar.
	 *
	 * The primary axis is fixed to SplitterSize()-1; the cross axis is
	 * B_SIZE_UNLIMITED so the bar always fills the full height or width.
	 *
	 * @return Maximum BSize of the splitter bar.
	 */
	virtual BSize MaxSize()
	{
		if (fLayout->Orientation() == B_HORIZONTAL)
			return BSize(fLayout->SplitterSize() - 1, B_SIZE_UNLIMITED);
		else
			return BSize(B_SIZE_UNLIMITED, fLayout->SplitterSize() - 1);
	}

	/**
	 * @brief Return the preferred size, identical to the minimum size.
	 * @return Preferred BSize of the splitter bar.
	 */
	virtual BSize PreferredSize()
	{
		return MinSize();
	}

	/**
	 * @brief Return the alignment for the splitter bar (always centered).
	 * @return BAlignment with horizontal and vertical centering.
	 */
	virtual BAlignment Alignment()
	{
		return BAlignment(B_ALIGN_HORIZONTAL_CENTER, B_ALIGN_VERTICAL_CENTER);
	}

	/** @brief No-op: explicit minimum-size overrides are not permitted on splitters. */
	virtual void SetExplicitMinSize(BSize size)
	{
		// not allowed
	}

	/** @brief No-op: explicit maximum-size overrides are not permitted on splitters. */
	virtual void SetExplicitMaxSize(BSize size)
	{
		// not allowed
	}

	/** @brief No-op: explicit preferred-size overrides are not permitted on splitters. */
	virtual void SetExplicitPreferredSize(BSize size)
	{
		// not allowed
	}

	/** @brief No-op: explicit alignment overrides are not permitted on splitters. */
	virtual void SetExplicitAlignment(BAlignment alignment)
	{
		// not allowed
	}

	/**
	 * @brief Splitter items are always visible.
	 * @return Always true.
	 */
	virtual bool IsVisible()
	{
		return true;
	}

	/** @brief No-op: splitter items cannot be hidden individually. */
	virtual void SetVisible(bool visible)
	{
		// not allowed
	}


	/**
	 * @brief Return the current frame of the splitter bar.
	 * @return The BRect last set by SetFrame().
	 */
	virtual BRect Frame()
	{
		return fFrame;
	}

	/**
	 * @brief Set the pixel frame of the splitter bar.
	 * @param frame New frame in the owner view's coordinate space.
	 */
	virtual void SetFrame(BRect frame)
	{
		fFrame = frame;
	}

private:
	/** @brief The parent BSplitLayout that owns this splitter. */
	BSplitLayout*	fLayout;
	/** @brief Most-recently assigned pixel frame. */
	BRect			fFrame;
};


// #pragma mark -


/**
 * @brief Construct a BSplitLayout with the given orientation and spacing.
 *
 * Creates an empty split layout. Items and their associated splitter bars are
 * added later via AddView() or AddItem().
 *
 * @param orientation B_HORIZONTAL to split left/right, B_VERTICAL to split
 *                    top/bottom.
 * @param spacing     Gap in pixels between adjacent items and splitter bars.
 *                    Passed through BControlLook::ComposeSpacing() to respect
 *                    the UI scale factor.
 * @see AddView()
 * @see AddItem()
 */
BSplitLayout::BSplitLayout(orientation orientation, float spacing)
	:
	fOrientation(orientation),
	fLeftInset(0),
	fRightInset(0),
	fTopInset(0),
	fBottomInset(0),
	fSplitterSize(6),
	fSpacing(BControlLook::ComposeSpacing(spacing)),

	fSplitterItems(),
	fVisibleItems(),
	fMin(),
	fMax(),
	fPreferred(),

	fHorizontalLayouter(NULL),
	fVerticalLayouter(NULL),
	fHorizontalLayoutInfo(NULL),
	fVerticalLayoutInfo(NULL),

	fHeightForWidthItems(),
	fHeightForWidthVerticalLayouter(NULL),
	fHeightForWidthHorizontalLayoutInfo(NULL),

	fLayoutValid(false),

	fCachedHeightForWidthWidth(-2),
	fHeightForWidthVerticalLayouterWidth(-2),
	fCachedMinHeightForWidth(-1),
	fCachedMaxHeightForWidth(-1),
	fCachedPreferredHeightForWidth(-1),

	fDraggingStartPoint(),
	fDraggingStartValue(0),
	fDraggingCurrentValue(0),
	fDraggingSplitterIndex(-1)
{
}


/**
 * @brief Reconstruct a BSplitLayout from an archived BMessage.
 *
 * Reads orientation, insets, splitter size, and spacing from the archive.
 * Per-item weights and collapsible flags are restored later by ItemUnarchived().
 *
 * @param from The archive message produced by Archive().
 * @see Archive()
 * @see Instantiate()
 */
BSplitLayout::BSplitLayout(BMessage* from)
	:
	BAbstractLayout(BUnarchiver::PrepareArchive(from)),
	fOrientation(B_HORIZONTAL),
	fLeftInset(0),
	fRightInset(0),
	fTopInset(0),
	fBottomInset(0),
	fSplitterSize(6),
	fSpacing(be_control_look->DefaultItemSpacing()),

	fSplitterItems(),
	fVisibleItems(),
	fMin(),
	fMax(),
	fPreferred(),

	fHorizontalLayouter(NULL),
	fVerticalLayouter(NULL),
	fHorizontalLayoutInfo(NULL),
	fVerticalLayoutInfo(NULL),

	fHeightForWidthItems(),
	fHeightForWidthVerticalLayouter(NULL),
	fHeightForWidthHorizontalLayoutInfo(NULL),

	fLayoutValid(false),

	fCachedHeightForWidthWidth(-2),
	fHeightForWidthVerticalLayouterWidth(-2),
	fCachedMinHeightForWidth(-1),
	fCachedMaxHeightForWidth(-1),
	fCachedPreferredHeightForWidth(-1),

	fDraggingStartPoint(),
	fDraggingStartValue(0),
	fDraggingCurrentValue(0),
	fDraggingSplitterIndex(-1)
{
	BUnarchiver unarchiver(from);

	bool isVertical;
	status_t err = from->FindBool(kIsVerticalField, &isVertical);
	if (err != B_OK) {
		unarchiver.Finish(err);
		return;
	}
	fOrientation = (isVertical) ? B_VERTICAL : B_HORIZONTAL ;

	BRect insets;
	err = from->FindRect(kInsetsField, &insets);
	if (err != B_OK) {
		unarchiver.Finish(err);
		return;
	}
	SetInsets(insets.left, insets.top, insets.right, insets.bottom);

	err = from->FindFloat(kSplitterSizeField, &fSplitterSize);
	if (err == B_OK)
		err = from->FindFloat(kSpacingField, &fSpacing);

	unarchiver.Finish(err);
}


/**
 * @brief Destroy the BSplitLayout and release all associated resources.
 *
 * Splitter items and their ItemLayoutInfo records are owned by the layout
 * and are freed by ItemRemoved() as the base class removes each item.
 */
BSplitLayout::~BSplitLayout()
{
}


/**
 * @brief Set the border insets applied around all items.
 *
 * The insets shrink the usable layout area inside the owner view's bounds.
 * Calling this method invalidates the layout.
 *
 * @param left   Left inset in pixels.
 * @param top    Top inset in pixels.
 * @param right  Right inset in pixels.
 * @param bottom Bottom inset in pixels.
 * @see GetInsets()
 */
void
BSplitLayout::SetInsets(float left, float top, float right, float bottom)
{
	fLeftInset = left;
	fTopInset = top;
	fRightInset = right;
	fBottomInset = bottom;

	InvalidateLayout();
}


/**
 * @brief Retrieve the current border insets.
 *
 * Any of the output pointer arguments may be NULL if that value is not needed.
 *
 * @param left   Output: left inset in pixels, or NULL.
 * @param top    Output: top inset in pixels, or NULL.
 * @param right  Output: right inset in pixels, or NULL.
 * @param bottom Output: bottom inset in pixels, or NULL.
 * @see SetInsets()
 */
void
BSplitLayout::GetInsets(float* left, float* top, float* right,
	float* bottom) const
{
	if (left)
		*left = fLeftInset;
	if (top)
		*top = fTopInset;
	if (right)
		*right = fRightInset;
	if (bottom)
		*bottom = fBottomInset;
}


/**
 * @brief Return the pixel gap inserted between items and splitter bars.
 * @return Spacing value in pixels.
 * @see SetSpacing()
 */
float
BSplitLayout::Spacing() const
{
	return fSpacing;
}


/**
 * @brief Set the pixel gap between adjacent items and splitter bars.
 *
 * The value is scaled by BControlLook::ComposeSpacing() to honour the
 * current UI scale factor. The layout is invalidated only when the
 * composed value differs from the current spacing.
 *
 * @param spacing New spacing, subject to UI-scale composition.
 * @see Spacing()
 */
void
BSplitLayout::SetSpacing(float spacing)
{
	spacing = BControlLook::ComposeSpacing(spacing);
	if (spacing != fSpacing) {
		fSpacing = spacing;

		InvalidateLayout();
	}
}


/**
 * @brief Return the current split orientation.
 * @return B_HORIZONTAL when items are arranged side-by-side;
 *         B_VERTICAL when items are stacked top-to-bottom.
 * @see SetOrientation()
 */
orientation
BSplitLayout::Orientation() const
{
	return fOrientation;
}


/**
 * @brief Change the split orientation and invalidate the layout.
 *
 * Switching orientation recalculates all splitter geometry on the next layout
 * pass. No-op when the new orientation matches the current one.
 *
 * @param orientation B_HORIZONTAL or B_VERTICAL.
 * @see Orientation()
 */
void
BSplitLayout::SetOrientation(orientation orientation)
{
	if (orientation != fOrientation) {
		fOrientation = orientation;

		InvalidateLayout();
	}
}


/**
 * @brief Return the thickness of each splitter bar in pixels.
 * @return Splitter bar thickness.
 * @see SetSplitterSize()
 */
float
BSplitLayout::SplitterSize() const
{
	return fSplitterSize;
}


/**
 * @brief Set the thickness of all splitter bars and invalidate the layout.
 *
 * No-op when @a size equals the current splitter size.
 *
 * @param size New splitter thickness in pixels.
 * @see SplitterSize()
 */
void
BSplitLayout::SetSplitterSize(float size)
{
	if (size != fSplitterSize) {
		fSplitterSize = size;

		InvalidateLayout();
	}
}


/**
 * @brief Add a view as the last content item with default weight 1.0.
 * @param child The view to add.
 * @return The BLayoutItem created for @a child, or NULL on failure.
 * @see AddItem()
 */
BLayoutItem*
BSplitLayout::AddView(BView* child)
{
	return BAbstractLayout::AddView(child);
}


/**
 * @brief Insert a view at a specific position with default weight 1.0.
 * @param index Zero-based position to insert at, or -1 to append.
 * @param child The view to add.
 * @return The BLayoutItem created for @a child, or NULL on failure.
 */
BLayoutItem*
BSplitLayout::AddView(int32 index, BView* child)
{
	return BAbstractLayout::AddView(index, child);
}


/**
 * @brief Append a view with an explicit size weight.
 * @param child  The view to add.
 * @param weight Relative size weight; larger values get proportionally more space.
 * @return The BLayoutItem created for @a child, or NULL on failure.
 */
BLayoutItem*
BSplitLayout::AddView(BView* child, float weight)
{
	return AddView(-1, child, weight);
}


/**
 * @brief Insert a view at a specific position with an explicit size weight.
 * @param index  Zero-based position to insert at, or -1 to append.
 * @param child  The view to add.
 * @param weight Relative size weight.
 * @return The BLayoutItem created for @a child, or NULL on failure.
 */
BLayoutItem*
BSplitLayout::AddView(int32 index, BView* child, float weight)
{
	BLayoutItem* item = AddView(index, child);
	if (item)
		SetItemWeight(item, weight);

	return item;
}


/**
 * @brief Append a pre-constructed layout item with default weight 1.0.
 * @param item The BLayoutItem to add.
 * @return true on success, false on failure.
 */
bool
BSplitLayout::AddItem(BLayoutItem* item)
{
	return BAbstractLayout::AddItem(item);
}


/**
 * @brief Insert a pre-constructed layout item at a specific position.
 * @param index Zero-based position to insert at, or -1 to append.
 * @param item  The BLayoutItem to add.
 * @return true on success, false on failure.
 */
bool
BSplitLayout::AddItem(int32 index, BLayoutItem* item)
{
	return BAbstractLayout::AddItem(index, item);
}


/**
 * @brief Append a layout item with an explicit size weight.
 * @param item   The BLayoutItem to add.
 * @param weight Relative size weight.
 * @return true on success, false on failure.
 */
bool
BSplitLayout::AddItem(BLayoutItem* item, float weight)
{
	return AddItem(-1, item, weight);
}


/**
 * @brief Insert a layout item at a specific position with an explicit weight.
 * @param index  Zero-based position to insert at, or -1 to append.
 * @param item   The BLayoutItem to add.
 * @param weight Relative size weight.
 * @return true on success, false on failure.
 */
bool
BSplitLayout::AddItem(int32 index, BLayoutItem* item, float weight)
{
	bool success = AddItem(index, item);
	if (success)
		SetItemWeight(item, weight);

	return success;
}


/**
 * @brief Return the size weight of the item at the given index.
 * @param index Zero-based content item index.
 * @return The item's weight, or 0 if @a index is out of range.
 * @see SetItemWeight()
 */
float
BSplitLayout::ItemWeight(int32 index) const
{
	if (index < 0 || index >= CountItems())
		return 0;

	return ItemWeight(ItemAt(index));
}


/**
 * @brief Return the size weight of a specific layout item.
 * @param item The layout item to query.
 * @return The item's weight, or 0 if @a item has no layout data.
 * @see SetItemWeight()
 */
float
BSplitLayout::ItemWeight(BLayoutItem* item) const
{
	if (ItemLayoutInfo* info = _ItemLayoutInfo(item))
		return info->weight;
	return 0;
}


/**
 * @brief Set the size weight of a content item by index.
 *
 * Immediately updates the weight inside the active layouter (if one exists) so
 * that the next layout pass distributes space correctly without rebuilding the
 * layouter from scratch.
 *
 * @param index            Zero-based content item index.
 * @param weight           New relative size weight (positive).
 * @param invalidateLayout If true, invalidate the layout so it is recalculated.
 * @see ItemWeight()
 */
void
BSplitLayout::SetItemWeight(int32 index, float weight, bool invalidateLayout)
{
	if (index < 0 || index >= CountItems())
		return;

	BLayoutItem* item = ItemAt(index);
	SetItemWeight(item, weight);

	if (fHorizontalLayouter) {
		int32 visibleIndex = fVisibleItems.IndexOf(item);
		if (visibleIndex >= 0) {
			if (fOrientation == B_HORIZONTAL)
				fHorizontalLayouter->SetWeight(visibleIndex, weight);
			else
				fVerticalLayouter->SetWeight(visibleIndex, weight);
		}
	}

	if (invalidateLayout)
		InvalidateLayout();
}


/**
 * @brief Set the size weight of a specific layout item.
 *
 * Only updates the cached ItemLayoutInfo; the active layouter is not updated.
 * Use the index-based overload with @c invalidateLayout=true to also refresh
 * the layouter.
 *
 * @param item   The layout item to update.
 * @param weight New relative size weight.
 * @see ItemWeight()
 */
void
BSplitLayout::SetItemWeight(BLayoutItem* item, float weight)
{
	if (ItemLayoutInfo* info = _ItemLayoutInfo(item))
		info->weight = weight;
}


/**
 * @brief Return whether the item at @a index may be collapsed by dragging.
 * @param index Zero-based content item index.
 * @return true if the item is collapsible, false otherwise.
 * @see SetCollapsible()
 */
bool
BSplitLayout::IsCollapsible(int32 index) const
{
	return _ItemLayoutInfo(ItemAt(index))->isCollapsible;
}


/**
 * @brief Set whether all content items may be collapsed by the user.
 * @param collapsible true to allow collapsing, false to prevent it.
 * @see IsCollapsible()
 */
void
BSplitLayout::SetCollapsible(bool collapsible)
{
	SetCollapsible(0, CountItems() - 1, collapsible);
}


/**
 * @brief Set whether a single content item may be collapsed by the user.
 * @param index      Zero-based content item index.
 * @param collapsible true to allow collapsing, false to prevent it.
 * @see IsCollapsible()
 */
void
BSplitLayout::SetCollapsible(int32 index, bool collapsible)
{
	SetCollapsible(index, index, collapsible);
}


/**
 * @brief Set the collapsible flag for a contiguous range of content items.
 * @param first       First item index in the range (inclusive).
 * @param last        Last item index in the range (inclusive).
 * @param collapsible true to allow collapsing, false to prevent it.
 * @see IsCollapsible()
 */
void
BSplitLayout::SetCollapsible(int32 first, int32 last, bool collapsible)
{
	for (int32 i = first; i <= last; i++)
		_ItemLayoutInfo(ItemAt(i))->isCollapsible = collapsible;
}


/**
 * @brief Return whether the item at @a index is currently collapsed.
 * @param index Zero-based content item index.
 * @return true if the item has zero size (is collapsed), false otherwise.
 * @see SetItemCollapsed()
 */
bool
BSplitLayout::IsItemCollapsed(int32 index) const
{
	return !_ItemLayoutInfo(ItemAt(index))->isVisible;
}


/**
 * @brief Programmatically collapse or expand a content item.
 *
 * Hides or shows the item and forces a full layout invalidation so the change
 * takes effect immediately.
 *
 * @param index     Zero-based content item index.
 * @param collapsed true to collapse (hide) the item, false to expand it.
 * @see IsItemCollapsed()
 */
void
BSplitLayout::SetItemCollapsed(int32 index, bool collapsed)
{
	ItemAt(index)->SetVisible(!collapsed);

	InvalidateLayout(true);
}


/**
 * @brief Compute and return the minimum size of the entire layout.
 *
 * Validates the min/max constraints if necessary, then adds border insets
 * and splitter space to the inner minimum size.
 *
 * @return Minimum BSize including insets and splitter bars.
 * @see BaseMaxSize()
 * @see BasePreferredSize()
 */
BSize
BSplitLayout::BaseMinSize()
{
	_ValidateMinMax();

	return _AddInsets(fMin);
}


/**
 * @brief Compute and return the maximum size of the entire layout.
 *
 * Validates the min/max constraints if necessary, then adds border insets
 * and splitter space to the inner maximum size.
 *
 * @return Maximum BSize including insets and splitter bars.
 * @see BaseMinSize()
 */
BSize
BSplitLayout::BaseMaxSize()
{
	_ValidateMinMax();

	return _AddInsets(fMax);
}


/**
 * @brief Compute and return the preferred size of the entire layout.
 *
 * Validates the min/max constraints if necessary, then adds border insets
 * and splitter space to the inner preferred size.
 *
 * @return Preferred BSize including insets and splitter bars.
 * @see BaseMinSize()
 */
BSize
BSplitLayout::BasePreferredSize()
{
	_ValidateMinMax();

	return _AddInsets(fPreferred);
}


/**
 * @brief Return the layout alignment (delegates to BAbstractLayout).
 * @return Default BAlignment from BAbstractLayout::BaseAlignment().
 */
BAlignment
BSplitLayout::BaseAlignment()
{
	return BAbstractLayout::BaseAlignment();
}


/**
 * @brief Return whether any content item reports height-for-width dependency.
 *
 * Validates min/max before checking. When true, GetHeightForWidth() must be
 * called after horizontal layout to obtain correct vertical constraints.
 *
 * @return true if at least one item is height-for-width sensitive.
 * @see GetHeightForWidth()
 */
bool
BSplitLayout::HasHeightForWidth()
{
	_ValidateMinMax();

	return !fHeightForWidthItems.IsEmpty();
}


/**
 * @brief Query the height range for a given allocated width.
 *
 * Subtracts horizontal insets and splitter space from @a width, delegates to
 * the internal height-for-width solver, then adds vertical insets and splitter
 * space back to the results. Any output pointer may be NULL.
 *
 * @param width     Available width in pixels (including insets).
 * @param min       Output: minimum height, or NULL.
 * @param max       Output: maximum height, or NULL.
 * @param preferred Output: preferred height, or NULL.
 * @see HasHeightForWidth()
 */
void
BSplitLayout::GetHeightForWidth(float width, float* min, float* max,
	float* preferred)
{
	if (!HasHeightForWidth())
		return;

	float innerWidth = _SubtractInsets(BSize(width, 0)).width;
	_InternalGetHeightForWidth(innerWidth, false, min, max, preferred);
	_AddInsets(min, max, preferred);
}


/**
 * @brief React to a layout invalidation by tearing down cached layouter state.
 *
 * Deletes the horizontal and vertical layouters and their layout info objects
 * so they are rebuilt from scratch on the next layout pass.  The
 * height-for-width cache is also flushed.
 *
 * @param children Unused; passed by BLayout infrastructure.
 */
void
BSplitLayout::LayoutInvalidated(bool children)
{
	delete fHorizontalLayouter;
	delete fVerticalLayouter;
	delete fHorizontalLayoutInfo;
	delete fVerticalLayoutInfo;

	fHorizontalLayouter = NULL;
	fVerticalLayouter = NULL;
	fHorizontalLayoutInfo = NULL;
	fVerticalLayoutInfo = NULL;

	_InvalidateCachedHeightForWidth();

	fLayoutValid = false;
}


/**
 * @brief Perform the complete layout pass, placing all items and splitters.
 *
 * Validates the size constraints, runs the horizontal and (possibly
 * height-for-width) vertical layouters, then iterates over all content items
 * and splitter bars to set their final pixel frames.
 *
 * @note Called by the layout infrastructure; do not call directly.
 * @see LayoutInvalidated()
 * @see DoLayout() in BAbstractLayout
 */
void
BSplitLayout::DoLayout()
{
	_ValidateMinMax();

	// layout the elements
	BSize size = _SubtractInsets(LayoutArea().Size());
	fHorizontalLayouter->Layout(fHorizontalLayoutInfo, size.width);

	Layouter* verticalLayouter;
	if (HasHeightForWidth()) {
		float minHeight, maxHeight, preferredHeight;
		_InternalGetHeightForWidth(size.width, true, &minHeight, &maxHeight,
			&preferredHeight);
		size.height = max_c(size.height, minHeight);
		verticalLayouter = fHeightForWidthVerticalLayouter;
	} else
		verticalLayouter = fVerticalLayouter;

	verticalLayouter->Layout(fVerticalLayoutInfo, size.height);

	float xOffset = fLeftInset;
	float yOffset = fTopInset;
	float splitterWidth = 0;	// pixel counts, no distances
	float splitterHeight = 0;	//
	float xSpacing = 0;
	float ySpacing = 0;
	if (fOrientation == B_HORIZONTAL) {
		splitterWidth = fSplitterSize;
		splitterHeight = size.height + 1;
		xSpacing = fSpacing;
	} else {
		splitterWidth = size.width + 1;
		splitterHeight = fSplitterSize;
		ySpacing = fSpacing;
	}

	int itemCount = CountItems();
	for (int i = 0; i < itemCount; i++) {
		// layout the splitter
		if (i > 0) {
			SplitterItem* splitterItem = _SplitterItemAt(i - 1);

			_LayoutItem(splitterItem, BRect(xOffset, yOffset,
				xOffset + splitterWidth - 1, yOffset + splitterHeight - 1),
				true);

			if (fOrientation == B_HORIZONTAL)
				xOffset += splitterWidth + xSpacing;
			else
				yOffset += splitterHeight + ySpacing;
		}

		// layout the item
		BLayoutItem* item = ItemAt(i);
		int32 visibleIndex = fVisibleItems.IndexOf(item);
		if (visibleIndex < 0) {
			_LayoutItem(item, BRect(), false);
			continue;
		}

		// get the dimensions of the item
		float width = fHorizontalLayoutInfo->ElementSize(visibleIndex);
		float height = fVerticalLayoutInfo->ElementSize(visibleIndex);

		// place the component
		_LayoutItem(item, BRect(xOffset, yOffset, xOffset + width,
			yOffset + height), true);

		if (fOrientation == B_HORIZONTAL)
			xOffset += width + xSpacing + 1;
		else
			yOffset += height + ySpacing + 1;
	}

	fLayoutValid = true;
}


/**
 * @brief Return the pixel frame of the splitter bar at the given index.
 *
 * Splitter bars are numbered 0 to CountItems()-2; bar @a index lies between
 * content item @a index and content item @a index+1.
 *
 * @param index Zero-based splitter index.
 * @return The BRect of the splitter bar, or an invalid BRect if @a index is
 *         out of range.
 * @see GetSplitterFrame()
 */
BRect
BSplitLayout::SplitterItemFrame(int32 index) const
{
	if (SplitterItem* item = _SplitterItemAt(index))
		return item->Frame();
	return BRect();
}


bool
BSplitLayout::IsAboveSplitter(const BPoint& point) const
{
	return _SplitterItemAt(point) != NULL;
}


bool
BSplitLayout::StartDraggingSplitter(BPoint point)
{
	StopDraggingSplitter();

	// Layout must be valid. Bail out, if it isn't.
	if (!fLayoutValid)
		return false;

	// Things shouldn't be draggable, if we have a >= max layout.
	BSize size = _SubtractInsets(LayoutArea().Size());
	if ((fOrientation == B_HORIZONTAL && size.width >= fMax.width)
		|| (fOrientation == B_VERTICAL && size.height >= fMax.height)) {
		return false;
	}

	int32 index = -1;
	if (_SplitterItemAt(point, &index) != NULL) {
		fDraggingStartPoint = Owner()->ConvertToScreen(point);
		fDraggingStartValue = _SplitterValue(index);
		fDraggingCurrentValue = fDraggingStartValue;
		fDraggingSplitterIndex = index;

		return true;
	}

	return false;
}


bool
BSplitLayout::DragSplitter(BPoint point)
{
	if (fDraggingSplitterIndex < 0)
		return false;

	point = Owner()->ConvertToScreen(point);

	int32 valueDiff;
	if (fOrientation == B_HORIZONTAL)
		valueDiff = int32(point.x - fDraggingStartPoint.x);
	else
		valueDiff = int32(point.y - fDraggingStartPoint.y);

	return _SetSplitterValue(fDraggingSplitterIndex,
		fDraggingStartValue + valueDiff);
}


bool
BSplitLayout::StopDraggingSplitter()
{
	if (fDraggingSplitterIndex < 0)
		return false;

	// update the item weights
	_UpdateSplitterWeights();

	fDraggingSplitterIndex = -1;

	return true;
}


int32
BSplitLayout::DraggedSplitter() const
{
	return fDraggingSplitterIndex;
}


/**
 * @brief Archive the layout's configuration into a BMessage.
 *
 * Stores orientation, border insets, splitter size, and spacing. Per-item
 * weight and collapsible state are stored separately by ItemArchived().
 *
 * @param into The message to archive into.
 * @param deep If true, child objects are archived as well.
 * @return B_OK on success, or a negative error code.
 * @see Instantiate()
 * @see ItemArchived()
 */
status_t
BSplitLayout::Archive(BMessage* into, bool deep) const
{
	BArchiver archiver(into);
	status_t err = BAbstractLayout::Archive(into, deep);

	if (err == B_OK)
		err = into->AddBool(kIsVerticalField, fOrientation == B_VERTICAL);

	if (err == B_OK) {
		BRect insets(fLeftInset, fTopInset, fRightInset, fBottomInset);
		err = into->AddRect(kInsetsField, insets);
	}

	if (err == B_OK)
		err = into->AddFloat(kSplitterSizeField, fSplitterSize);

	if (err == B_OK)
		err = into->AddFloat(kSpacingField, fSpacing);

	return archiver.Finish(err);
}


/**
 * @brief Instantiate a BSplitLayout from an archived BMessage.
 * @param from The archive message produced by Archive().
 * @return A new BSplitLayout on success, or NULL if validation fails.
 * @see Archive()
 */
BArchivable*
BSplitLayout::Instantiate(BMessage* from)
{
	if (validate_instantiation(from, "BSplitLayout"))
		return new(std::nothrow) BSplitLayout(from);
	return NULL;
}


/**
 * @brief Archive per-item layout metadata (weight and collapsible flag).
 *
 * Called by the archiving infrastructure for each item after Archive().
 *
 * @param into  The message to write item data into.
 * @param item  The layout item being archived.
 * @param index Zero-based item index.
 * @return B_OK on success, or a negative error code.
 * @see ItemUnarchived()
 */
status_t
BSplitLayout::ItemArchived(BMessage* into, BLayoutItem* item, int32 index) const
{
	ItemLayoutInfo* info = _ItemLayoutInfo(item);

	status_t err = into->AddFloat(kItemWeightField, info->weight);
	if (err == B_OK)
		err = into->AddBool(kItemCollapsibleField, info->isCollapsible);

	return err;
}


/**
 * @brief Restore per-item layout metadata from an archive.
 *
 * Called by the archiving infrastructure for each item after AllUnarchived().
 *
 * @param from  The archive message to read item data from.
 * @param item  The layout item being restored.
 * @param index Zero-based item index (used to index into repeated fields).
 * @return B_OK on success, or a negative error code.
 * @see ItemArchived()
 */
status_t
BSplitLayout::ItemUnarchived(const BMessage* from,
	BLayoutItem* item, int32 index)
{
	ItemLayoutInfo* info = _ItemLayoutInfo(item);
	status_t err = from->FindFloat(kItemWeightField, index, &info->weight);

	if (err == B_OK) {
		bool* collapsible = &info->isCollapsible;
		err = from->FindBool(kItemCollapsibleField, index, collapsible);
	}
	return err;
}


/**
 * @brief Called when a new content item is added to the layout.
 *
 * Allocates an ItemLayoutInfo for @a item and, if this is the second or later
 * item, also allocates a SplitterItem with its own ItemLayoutInfo to insert
 * between the new item and its predecessor.
 *
 * @param item    The newly added BLayoutItem.
 * @param atIndex The index at which the item was inserted.
 * @return true on success; false if memory allocation fails.
 */
bool
BSplitLayout::ItemAdded(BLayoutItem* item, int32 atIndex)
{
	ItemLayoutInfo* itemInfo = new(nothrow) ItemLayoutInfo();
	if (!itemInfo)
		return false;

	if (CountItems() > 1) {
		SplitterItem* splitter = new(nothrow) SplitterItem(this);
		ItemLayoutInfo* splitterInfo = new(nothrow) ItemLayoutInfo();
		if (!splitter || !splitterInfo || !fSplitterItems.AddItem(splitter)) {
			delete itemInfo;
			delete splitter;
			delete splitterInfo;
			return false;
		}
		splitter->SetLayoutData(splitterInfo);
		SetItemWeight(splitter, 0);
	}

	item->SetLayoutData(itemInfo);
	SetItemWeight(item, 1);
	return true;
}


/**
 * @brief Called when a content item is removed from the layout.
 *
 * Removes and deletes the last SplitterItem (if any remain) and frees the
 * ItemLayoutInfo owned by @a item.
 *
 * @param item    The BLayoutItem that was removed.
 * @param atIndex The index the item occupied before removal.
 */
void
BSplitLayout::ItemRemoved(BLayoutItem* item, int32 atIndex)
{
	if (fSplitterItems.CountItems() > 0) {
		SplitterItem* splitterItem = (SplitterItem*)fSplitterItems.RemoveItem(
			fSplitterItems.CountItems() - 1);
		delete _ItemLayoutInfo(splitterItem);
		delete splitterItem;
	}

	delete _ItemLayoutInfo(item);
	item->SetLayoutData(NULL);
}


/**
 * @brief Discard all cached height-for-width solver state.
 *
 * Frees the cloned vertical layouter and horizontal layout info used during
 * height-for-width queries, and resets the cached-width sentinel values so
 * the next call to _InternalGetHeightForWidth() rebuilds them.
 */
void
BSplitLayout::_InvalidateCachedHeightForWidth()
{
	delete fHeightForWidthVerticalLayouter;
	delete fHeightForWidthHorizontalLayoutInfo;

	fHeightForWidthVerticalLayouter = NULL;
	fHeightForWidthHorizontalLayoutInfo = NULL;

	fCachedHeightForWidthWidth = -2;
	fHeightForWidthVerticalLayouterWidth = -2;
}


/**
 * @brief Find the splitter bar that contains the given view-local point.
 *
 * Iterates over all splitter bars and returns the first one whose frame
 * contains @a point.
 *
 * @param point The point to hit-test, in owner-view coordinates.
 * @param index Output: set to the zero-based splitter index on a hit, or
 *              unchanged on a miss. May be NULL.
 * @return The matching SplitterItem, or NULL if no splitter was hit.
 * @see _SplitterItemAt(int32)
 */
BSplitLayout::SplitterItem*
BSplitLayout::_SplitterItemAt(const BPoint& point, int32* index) const
{
	int32 splitterCount = fSplitterItems.CountItems();
	for (int32 i = 0; i < splitterCount; i++) {
		SplitterItem* splitItem = _SplitterItemAt(i);
		BRect frame = splitItem->Frame();
		if (frame.Contains(point)) {
			if (index != NULL)
				*index = i;
			return splitItem;
		}
	}
	return NULL;
}


/**
 * @brief Return the SplitterItem at the given index.
 * @param index Zero-based splitter index.
 * @return The SplitterItem, or NULL if @a index is out of range.
 */
BSplitLayout::SplitterItem*
BSplitLayout::_SplitterItemAt(int32 index) const
{
	return (SplitterItem*)fSplitterItems.ItemAt(index);
}


/**
 * @brief Populate a ValueRange describing the drag constraints for a splitter.
 *
 * Reads the current size and min/max constraints of the two items adjacent to
 * the splitter at @a index and fills @a range with pixel values (not BLayout
 * pixel-minus-one distances).
 *
 * @param index Zero-based splitter index.
 * @param range Output: filled with the constraint values.
 */
void
BSplitLayout::_GetSplitterValueRange(int32 index, ValueRange& range)
{
	ItemLayoutInfo* previousInfo = _ItemLayoutInfo(ItemAt(index));
	ItemLayoutInfo* nextInfo = _ItemLayoutInfo(ItemAt(index + 1));
	if (fOrientation == B_HORIZONTAL) {
		range.previousMin = (int32)previousInfo->min.width + 1;
		range.previousMax = (int32)previousInfo->max.width + 1;
		range.previousSize = previousInfo->layoutFrame.IntegerWidth() + 1;
		range.nextMin = (int32)nextInfo->min.width + 1;
		range.nextMax = (int32)nextInfo->max.width + 1;
		range.nextSize = nextInfo->layoutFrame.IntegerWidth() + 1;
	} else {
		range.previousMin = (int32)previousInfo->min.height + 1;
		range.previousMax = (int32)previousInfo->max.height + 1;
		range.previousSize = previousInfo->layoutFrame.IntegerHeight() + 1;
		range.nextMin = (int32)nextInfo->min.height + 1;
		range.nextMax = (int32)nextInfo->max.height + 1;
		range.nextSize = (int32)nextInfo->layoutFrame.IntegerHeight() + 1;
	}

	range.sumValue = range.previousSize + range.nextSize;
	if (previousInfo->isVisible)
		range.sumValue += (int32)fSpacing;
	if (nextInfo->isVisible)
		range.sumValue += (int32)fSpacing;
}


/**
 * @brief Return the current drag value for the splitter at @a index.
 *
 * The drag value equals the pixel size of the preceding item plus the spacing
 * gap. Returns 0 when the preceding item is collapsed (not visible).
 *
 * @param index Zero-based splitter index.
 * @return Current drag value in pixels, or 0 if the preceding item is
 *         collapsed.
 */
int32
BSplitLayout::_SplitterValue(int32 index) const
{
	ItemLayoutInfo* info = _ItemLayoutInfo(ItemAt(index));
	if (info && info->isVisible) {
		if (fOrientation == B_HORIZONTAL)
			return info->layoutFrame.IntegerWidth() + 1 + (int32)fSpacing;
		else
			return info->layoutFrame.IntegerHeight() + 1 + (int32)fSpacing;
	} else
		return 0;
}


/**
 * @brief Assign a frame to an item and update its cached layout state.
 *
 * Records the frame and visibility in the item's ItemLayoutInfo, refreshes the
 * cached min/max (adjusting for height-for-width if needed), and positions the
 * item within @a frame via AlignInFrame().
 *
 * @param item    The BLayoutItem to position.
 * @param frame   The target pixel frame (ignored when @a visible is false).
 * @param visible true to show the item at @a frame; false to hide it.
 */
void
BSplitLayout::_LayoutItem(BLayoutItem* item, BRect frame, bool visible)
{
	// update the layout frame
	ItemLayoutInfo* info = _ItemLayoutInfo(item);
	info->isVisible = visible;
	if (visible)
		info->layoutFrame = frame;
	else
		info->layoutFrame = BRect(0, 0, -1, -1);

	// update min/max
	info->min = item->MinSize();
	info->max = item->MaxSize();

	if (item->HasHeightForWidth()) {
		BSize size = _SubtractInsets(LayoutArea().Size());
		float minHeight, maxHeight;
		item->GetHeightForWidth(size.width, &minHeight, &maxHeight, NULL);
		info->min.height = max_c(info->min.height, minHeight);
		info->max.height = min_c(info->max.height, maxHeight);
	}

	// layout the item
	if (visible)
		item->AlignInFrame(frame);
}


/**
 * @brief Apply a previously computed ItemLayoutInfo to an item.
 *
 * Synchronises the item's BLayoutItem visibility with the cached
 * ItemLayoutInfo::isVisible flag, then calls AlignInFrame() to position it.
 * If the item just became visible, Relayout() is called to update its
 * internal layout.
 *
 * @param item The BLayoutItem to update.
 * @param info The pre-computed layout info to apply.
 */
void
BSplitLayout::_LayoutItem(BLayoutItem* item, ItemLayoutInfo* info)
{
	// update the visibility of the item
	bool isVisible = item->IsVisible();
	bool visibilityChanged = (info->isVisible != isVisible);
	if (visibilityChanged)
		item->SetVisible(info->isVisible);

	// nothing more to do, if the item is not visible
	if (!info->isVisible)
		return;

	item->AlignInFrame(info->layoutFrame);

	// if the item became visible, we need to update its internal layout
	if (visibilityChanged &&
		(fOrientation != B_HORIZONTAL || !HasHeightForWidth())) {
		item->Relayout(true);
	}
}


/**
 * @brief Move the splitter at @a index to the given drag value and relayout.
 *
 * Enforces min/max constraints and collapse logic for the two adjacent items,
 * recomputes their pixel frames and the splitter frame without a full layout
 * pass, then applies the frames via _LayoutItem().
 *
 * @param index Zero-based splitter index.
 * @param value New drag value in pixels (size of preceding item + spacing).
 * @return true if the splitter moved (layout was updated); false if nothing
 *         changed or the drag could not be applied.
 */
bool
BSplitLayout::_SetSplitterValue(int32 index, int32 value)
{
	// if both items are collapsed, nothing can be dragged
	BLayoutItem* previousItem = ItemAt(index);
	BLayoutItem* nextItem = ItemAt(index + 1);
	ItemLayoutInfo* previousInfo = _ItemLayoutInfo(previousItem);
	ItemLayoutInfo* nextInfo = _ItemLayoutInfo(nextItem);
	ItemLayoutInfo* splitterInfo = _ItemLayoutInfo(_SplitterItemAt(index));
	bool previousVisible = previousInfo->isVisible;
	bool nextVisible = nextInfo->isVisible;
	if (!previousVisible && !nextVisible)
		return false;

	ValueRange range;
	_GetSplitterValueRange(index, range);

	value = max_c(min_c(value, range.sumValue), -(int32)fSpacing);

	int32 previousSize = value - (int32)fSpacing;
	int32 nextSize = range.sumValue - value - (int32)fSpacing;

	// Note: While this collapsed-check is mathmatically correct (i.e. we
	// collapse an item, if it would become smaller than half its minimum
	// size), we might want to change it, since for the user it looks like
	// collapsing happens earlier. The reason being that the only visual mark
	// the user has is the mouse cursor which indeed hasn't crossed the middle
	// of the item yet.
	bool previousCollapsed = (previousSize <= range.previousMin / 2)
		&& previousInfo->isCollapsible;
	bool nextCollapsed = (nextSize <= range.nextMin / 2)
		&& nextInfo->isCollapsible;
	if (previousCollapsed && nextCollapsed) {
		// we cannot collapse both items; we have to decide for one
		if (previousSize < nextSize) {
			// collapse previous
			nextCollapsed = false;
			nextSize = range.sumValue - (int32)fSpacing;
		} else {
			// collapse next
			previousCollapsed = false;
			previousSize = range.sumValue - (int32)fSpacing;
		}
	}

	if (previousCollapsed || nextCollapsed) {
		// one collapsed item -- check whether that violates the constraints
		// of the other one
		int32 availableSpace = range.sumValue - (int32)fSpacing;
		if (previousCollapsed) {
			if (availableSpace < range.nextMin
				|| availableSpace > range.nextMax) {
				// we cannot collapse the previous item
				previousCollapsed = false;
			}
		} else {
			if (availableSpace < range.previousMin
				|| availableSpace > range.previousMax) {
				// we cannot collapse the next item
				nextCollapsed = false;
			}
		}
	}

	if (!(previousCollapsed || nextCollapsed)) {
		// no collapsed item -- check whether there is a close solution
		previousSize = value - (int32)fSpacing;
		nextSize = range.sumValue - value - (int32)fSpacing;

		if (range.previousMin + range.nextMin + 2 * fSpacing > range.sumValue) {
			// we don't have enough space to uncollapse both items
			int32 availableSpace = range.sumValue - (int32)fSpacing;
			if (previousSize < nextSize && availableSpace >= range.nextMin
				&& availableSpace <= range.nextMax
				&& previousInfo->isCollapsible) {
				previousCollapsed = true;
			} else if (availableSpace >= range.previousMin
				&& availableSpace <= range.previousMax
				&& nextInfo->isCollapsible) {
				nextCollapsed = true;
			} else if (availableSpace >= range.nextMin
				&& availableSpace <= range.nextMax
				&& previousInfo->isCollapsible) {
				previousCollapsed = true;
			} else {
				if (previousSize < nextSize && previousInfo->isCollapsible) {
					previousCollapsed = true;
				} else if (nextInfo->isCollapsible) {
					nextCollapsed = true;
				} else {
					// Neither item is collapsible although there's not enough
					// space: Give them both their minimum size.
					previousSize = range.previousMin;
					nextSize = range.nextMin;
				}
			}

		} else {
			// there is enough space for both items
			// make sure the min constraints are satisfied
			if (previousSize < range.previousMin) {
				previousSize = range.previousMin;
				nextSize = range.sumValue - previousSize - 2 * (int32)fSpacing;
			} else if (nextSize < range.nextMin) {
				nextSize = range.nextMin;
				previousSize = range.sumValue - nextSize - 2 * (int32)fSpacing;
			}

			// if we can, also satisfy the max constraints
			if (range.previousMax + range.nextMax + 2 * (int32)fSpacing
					>= range.sumValue) {
				if (previousSize > range.previousMax) {
					previousSize = range.previousMax;
					nextSize = range.sumValue - previousSize
						- 2 * (int32)fSpacing;
				} else if (nextSize > range.nextMax) {
					nextSize = range.nextMax;
					previousSize = range.sumValue - nextSize
						- 2 * (int32)fSpacing;
				}
			}
		}
	}

	// compute the size for one collapsed item; for none collapsed item we
	// already have correct values
	if (previousCollapsed || nextCollapsed) {
		int32 availableSpace = range.sumValue - (int32)fSpacing;
		if (previousCollapsed) {
			previousSize = 0;
			nextSize = availableSpace;
		} else {
			previousSize = availableSpace;
			nextSize = 0;
		}
	}

	int32 newValue = previousSize + (previousCollapsed ? 0 : (int32)fSpacing);
	if (newValue == fDraggingCurrentValue) {
		// nothing changed
		return false;
	}

	// something changed: we need to recompute the layout
	int32 baseOffset = -fDraggingCurrentValue;
		// offset to the current splitter position
	int32 splitterOffset = baseOffset + newValue;
	int32 nextOffset = splitterOffset + (int32)fSplitterSize + (int32)fSpacing;

	BRect splitterFrame(splitterInfo->layoutFrame);
	if (fOrientation == B_HORIZONTAL) {
		// horizontal layout
		// previous item
		float left = splitterFrame.left + baseOffset;
		previousInfo->layoutFrame.Set(
			left,
			splitterFrame.top,
			left + previousSize - 1,
			splitterFrame.bottom);

		// next item
		left = splitterFrame.left + nextOffset;
		nextInfo->layoutFrame.Set(
			left,
			splitterFrame.top,
			left + nextSize - 1,
			splitterFrame.bottom);

		// splitter
		splitterInfo->layoutFrame.left += splitterOffset;
		splitterInfo->layoutFrame.right += splitterOffset;
	} else {
		// vertical layout
		// previous item
		float top = splitterFrame.top + baseOffset;
		previousInfo->layoutFrame.Set(
			splitterFrame.left,
			top,
			splitterFrame.right,
			top + previousSize - 1);

		// next item
		top = splitterFrame.top + nextOffset;
		nextInfo->layoutFrame.Set(
			splitterFrame.left,
			top,
			splitterFrame.right,
			top + nextSize - 1);

		// splitter
		splitterInfo->layoutFrame.top += splitterOffset;
		splitterInfo->layoutFrame.bottom += splitterOffset;
	}

	previousInfo->isVisible = !previousCollapsed;
	nextInfo->isVisible = !nextCollapsed;

	bool heightForWidth = (fOrientation == B_HORIZONTAL && HasHeightForWidth());

	// If the item visibility is to be changed, we need to update the splitter
	// values now, since the visibility change will cause an invalidation.
	if (previousVisible != previousInfo->isVisible
		|| nextVisible != nextInfo->isVisible || heightForWidth) {
		_UpdateSplitterWeights();
	}

	// If we have height for width items, we need to invalidate the previous
	// and the next item. Actually we would only need to invalidate height for
	// width items, but since non height for width items might be aligned with
	// height for width items, we need to trigger a layout that creates a
	// context that spans all aligned items.
	// We invalidate already here, so that changing the items' size won't cause
	// an immediate relayout.
	if (heightForWidth) {
		previousItem->InvalidateLayout();
		nextItem->InvalidateLayout();
	}

	// do the layout
	_LayoutItem(previousItem, previousInfo);
	_LayoutItem(_SplitterItemAt(index), splitterInfo);
	_LayoutItem(nextItem, nextInfo);

	fDraggingCurrentValue = newValue;

	return true;
}


/**
 * @brief Return the ItemLayoutInfo stored in a layout item's opaque data slot.
 * @param item The layout item to query.
 * @return Pointer to the item's ItemLayoutInfo, or NULL if none is set.
 */
BSplitLayout::ItemLayoutInfo*
BSplitLayout::_ItemLayoutInfo(BLayoutItem* item) const
{
	return (ItemLayoutInfo*)item->LayoutData();
}


/**
 * @brief Synchronise each item's weight with its current pixel size.
 *
 * After an interactive splitter drag the pixel frames reflect the actual
 * distribution of space. This method translates those frames back into
 * weights so the next full layout pass reproduces the same distribution.
 * The height-for-width cache is flushed when orientation is B_VERTICAL.
 */
void
BSplitLayout::_UpdateSplitterWeights()
{
	int32 count = CountItems();
	for (int32 i = 0; i < count; i++) {
		float weight;
		if (fOrientation == B_HORIZONTAL)
			weight = _ItemLayoutInfo(ItemAt(i))->layoutFrame.Width() + 1;
		else
			weight = _ItemLayoutInfo(ItemAt(i))->layoutFrame.Height() + 1;

		SetItemWeight(i, weight, false);
	}

	// Just updating the splitter weights is fine in principle. The next
	// LayoutItems() will use the correct values. But, if our orientation is
	// vertical, the cached height for width info needs to be flushed, or the
	// obsolete cached values will be used.
	if (fOrientation == B_VERTICAL)
		_InvalidateCachedHeightForWidth();
}


/**
 * @brief Build (or rebuild) the horizontal and vertical layouters from scratch.
 *
 * This is the core constraint-solver setup step. It:
 *  - filters visible items and height-for-width items into working lists,
 *  - creates SimpleLayouter / OneElementLayouter instances for both axes,
 *  - feeds each item's min/max/preferred and weight into the layouters,
 *  - derives fMin, fMax, fPreferred from the layouters' aggregate sizes, and
 *  - allocates LayoutInfo objects for the layout pass.
 *
 * Does nothing if the layouters are already valid (fHorizontalLayouter != NULL).
 */
void
BSplitLayout::_ValidateMinMax()
{
	if (fHorizontalLayouter != NULL)
		return;

	fLayoutValid = false;

	fVisibleItems.MakeEmpty();
	fHeightForWidthItems.MakeEmpty();

	_InvalidateCachedHeightForWidth();

	// filter the visible items
	int32 itemCount = CountItems();
	for (int32 i = 0; i < itemCount; i++) {
		BLayoutItem* item = ItemAt(i);
		if (item->IsVisible())
			fVisibleItems.AddItem(item);

		// Add "height for width" items even, if they aren't visible. Otherwise
		// we may get our parent into trouble, since we could change from
		// "height for width" to "not height for width".
		if (item->HasHeightForWidth())
			fHeightForWidthItems.AddItem(item);
	}
	itemCount = fVisibleItems.CountItems();

	// create the layouters
	Layouter* itemLayouter = new SimpleLayouter(itemCount, 0);

	if (fOrientation == B_HORIZONTAL) {
		fHorizontalLayouter = itemLayouter;
		fVerticalLayouter = new OneElementLayouter();
	} else {
		fHorizontalLayouter = new OneElementLayouter();
		fVerticalLayouter = itemLayouter;
	}

	// tell the layouters about our constraints
	if (itemCount > 0) {
		for (int32 i = 0; i < itemCount; i++) {
			BLayoutItem* item = (BLayoutItem*)fVisibleItems.ItemAt(i);
			BSize min = item->MinSize();
			BSize max = item->MaxSize();
			BSize preferred = item->PreferredSize();

			fHorizontalLayouter->AddConstraints(i, 1, min.width, max.width,
				preferred.width);
			fVerticalLayouter->AddConstraints(i, 1, min.height, max.height,
				preferred.height);

			float weight = ItemWeight(item);
			fHorizontalLayouter->SetWeight(i, weight);
			fVerticalLayouter->SetWeight(i, weight);
		}
	}

	fMin.width = fHorizontalLayouter->MinSize();
	fMin.height = fVerticalLayouter->MinSize();
	fMax.width = fHorizontalLayouter->MaxSize();
	fMax.height = fVerticalLayouter->MaxSize();
	fPreferred.width = fHorizontalLayouter->PreferredSize();
	fPreferred.height = fVerticalLayouter->PreferredSize();

	fHorizontalLayoutInfo = fHorizontalLayouter->CreateLayoutInfo();
	if (fHeightForWidthItems.IsEmpty())
		fVerticalLayoutInfo = fVerticalLayouter->CreateLayoutInfo();

	ResetLayoutInvalidation();
}


/**
 * @brief Internal height-for-width solver.
 *
 * Clones the vertical layouter, performs (or reuses) a horizontal layout for
 * @a width, then adds each height-for-width item's constraints to the cloned
 * vertical layouter to derive the height range. When @a realLayout is true the
 * cloned layouter is kept for use by DoLayout(); otherwise it is discarded and
 * results are cached by @a width.
 *
 * @param width         Inner layout width (insets already subtracted).
 * @param realLayout    true when called from DoLayout(); false for query-only.
 * @param minHeight     Output: minimum inner height, or NULL.
 * @param maxHeight     Output: maximum inner height, or NULL.
 * @param preferredHeight Output: preferred inner height, or NULL.
 */
void
BSplitLayout::_InternalGetHeightForWidth(float width, bool realLayout,
	float* minHeight, float* maxHeight, float* preferredHeight)
{
	if ((realLayout && fHeightForWidthVerticalLayouterWidth != width)
		|| (!realLayout && fCachedHeightForWidthWidth != width)) {
		// The general strategy is to clone the vertical layouter, which only
		// knows the general min/max constraints, do a horizontal layout for the
		// given width, and add the children's height for width constraints to
		// the cloned vertical layouter. If this method is invoked internally,
		// we keep the cloned vertical layouter, for it will be used for doing
		// the layout. Otherwise we just drop it after we've got the height for
		// width info.

		// clone the vertical layouter and get the horizontal layout info to be used
		LayoutInfo* horizontalLayoutInfo = NULL;
		Layouter* verticalLayouter = fVerticalLayouter->CloneLayouter();
		if (realLayout) {
			horizontalLayoutInfo = fHorizontalLayoutInfo;
			delete fHeightForWidthVerticalLayouter;
			fHeightForWidthVerticalLayouter = verticalLayouter;
			delete fVerticalLayoutInfo;
			fVerticalLayoutInfo = verticalLayouter->CreateLayoutInfo();
			fHeightForWidthVerticalLayouterWidth = width;
		} else {
			if (fHeightForWidthHorizontalLayoutInfo == NULL) {
				delete fHeightForWidthHorizontalLayoutInfo;
				fHeightForWidthHorizontalLayoutInfo
					= fHorizontalLayouter->CreateLayoutInfo();
			}
			horizontalLayoutInfo = fHeightForWidthHorizontalLayoutInfo;
		}

		// do the horizontal layout (already done when doing this for the real
		// layout)
		if (!realLayout)
			fHorizontalLayouter->Layout(horizontalLayoutInfo, width);

		// add the children's height for width constraints
		int32 count = fHeightForWidthItems.CountItems();
		for (int32 i = 0; i < count; i++) {
			BLayoutItem* item = (BLayoutItem*)fHeightForWidthItems.ItemAt(i);
			int32 index = fVisibleItems.IndexOf(item);
			if (index >= 0) {
				float itemMinHeight, itemMaxHeight, itemPreferredHeight;
				item->GetHeightForWidth(
					horizontalLayoutInfo->ElementSize(index),
					&itemMinHeight, &itemMaxHeight, &itemPreferredHeight);
				verticalLayouter->AddConstraints(index, 1, itemMinHeight,
					itemMaxHeight, itemPreferredHeight);
			}
		}

		// get the height for width info
		fCachedHeightForWidthWidth = width;
		fCachedMinHeightForWidth = verticalLayouter->MinSize();
		fCachedMaxHeightForWidth = verticalLayouter->MaxSize();
		fCachedPreferredHeightForWidth = verticalLayouter->PreferredSize();
	}

	if (minHeight)
		*minHeight = fCachedMinHeightForWidth;
	if (maxHeight)
		*maxHeight = fCachedMaxHeightForWidth;
	if (preferredHeight)
		*preferredHeight = fCachedPreferredHeightForWidth;
}


/**
 * @brief Compute the total pixel space consumed by all splitter bars and gaps.
 *
 * Accounts for the spacing slot on each side of every splitter plus the fixed
 * splitter bar thickness. Returns 0 when there are no splitters.
 *
 * @return Total splitter+spacing overhead in pixels.
 */
float
BSplitLayout::_SplitterSpace() const
{
	int32 splitters = fSplitterItems.CountItems();
	float space = 0;
	if (splitters > 0) {
		space = (fVisibleItems.CountItems() + splitters - 1) * fSpacing
			+ splitters * fSplitterSize;
	}

	return space;
}


/**
 * @brief Add border insets and splitter overhead to an inner BSize.
 *
 * Converts an inner (content-area) size to an outer (view-area) size by
 * adding border insets on both axes and splitter space on the primary axis.
 *
 * @param size Inner BSize to expand.
 * @return The outer BSize.
 * @see _SubtractInsets()
 */
BSize
BSplitLayout::_AddInsets(BSize size)
{
	size.width = BLayoutUtils::AddDistances(size.width,
		fLeftInset + fRightInset - 1);
	size.height = BLayoutUtils::AddDistances(size.height,
		fTopInset + fBottomInset - 1);

	float spacing = _SplitterSpace();
	if (fOrientation == B_HORIZONTAL)
		size.width = BLayoutUtils::AddDistances(size.width, spacing - 1);
	else
		size.height = BLayoutUtils::AddDistances(size.height, spacing - 1);

	return size;
}


/**
 * @brief Add vertical insets and (for B_VERTICAL layouts) splitter space to
 *        a set of height values.
 *
 * Mutates the three optional output heights in-place. Any pointer may be NULL.
 *
 * @param minHeight       In/out: minimum height to adjust.
 * @param maxHeight       In/out: maximum height to adjust.
 * @param preferredHeight In/out: preferred height to adjust.
 */
void
BSplitLayout::_AddInsets(float* minHeight, float* maxHeight,
	float* preferredHeight)
{
	float insets = fTopInset + fBottomInset - 1;
	if (fOrientation == B_VERTICAL)
		insets += _SplitterSpace();
	if (minHeight)
		*minHeight = BLayoutUtils::AddDistances(*minHeight, insets);
	if (maxHeight)
		*maxHeight = BLayoutUtils::AddDistances(*maxHeight, insets);
	if (preferredHeight)
		*preferredHeight = BLayoutUtils::AddDistances(*preferredHeight, insets);
}


/**
 * @brief Subtract border insets and splitter overhead from an outer BSize.
 *
 * Converts an outer (view-area) size to an inner (content-area) size by
 * removing border insets on both axes and splitter space on the primary axis.
 *
 * @param size Outer BSize to reduce.
 * @return The inner BSize.
 * @see _AddInsets()
 */
BSize
BSplitLayout::_SubtractInsets(BSize size)
{
	size.width = BLayoutUtils::SubtractDistances(size.width,
		fLeftInset + fRightInset - 1);
	size.height = BLayoutUtils::SubtractDistances(size.height,
		fTopInset + fBottomInset - 1);

	float spacing = _SplitterSpace();
	if (fOrientation == B_HORIZONTAL)
		size.width = BLayoutUtils::SubtractDistances(size.width, spacing - 1);
	else
		size.height = BLayoutUtils::SubtractDistances(size.height, spacing - 1);

	return size;
}

