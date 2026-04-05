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
 *   Copyright 2010 Haiku, Inc. All rights reserved.
 *   Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file GroupLayout.cpp
 * @brief Implementation of BGroupLayout, a one-dimensional linear layout manager
 *
 * BGroupLayout arranges items in a single row or column, distributing space according
 * to item size constraints and optional weighting. Supports horizontal and vertical
 * orientations.
 *
 * @see BTwoDimensionalLayout, BGroupView, BGroupLayoutBuilder
 */


#include <GroupLayout.h>

#include <ControlLook.h>
#include <LayoutItem.h>
#include <Message.h>

#include <new>


using std::nothrow;


namespace {
	/** @brief Archive field name for per-item weight values. */
	const char* const kItemWeightField = "BGroupLayout:item:weight";
	/** @brief Archive field name storing whether the layout orientation is vertical. */
	const char* const kVerticalField = "BGroupLayout:vertical";
}


struct BGroupLayout::ItemLayoutData {
	float	weight;

	ItemLayoutData()
		: weight(1)
	{
	}
};


/**
 * @brief Constructs a BGroupLayout with the given orientation and inter-item spacing.
 *
 * @param orientation @c B_HORIZONTAL to arrange items left-to-right,
 *                    @c B_VERTICAL to arrange them top-to-bottom.
 * @param spacing     Spacing between items in pixels, or a spacing constant
 *                    such as @c B_USE_DEFAULT_SPACING.
 *
 * @see SetOrientation(), SetSpacing()
 */
BGroupLayout::BGroupLayout(orientation orientation, float spacing)
	:
	BTwoDimensionalLayout(),
	fOrientation(orientation)
{
	SetSpacing(spacing);
}


/**
 * @brief Unarchives a BGroupLayout from a BMessage.
 *
 * Restores the orientation from the archived boolean field; if the field is
 * absent the orientation defaults to @c B_HORIZONTAL.
 *
 * @param from The archive message produced by a previous call to Archive().
 *
 * @see BGroupLayout::Archive(), BGroupLayout::Instantiate()
 */
BGroupLayout::BGroupLayout(BMessage* from)
	:
	BTwoDimensionalLayout(from)
{
	bool isVertical;
	if (from->FindBool(kVerticalField, &isVertical) != B_OK)
		isVertical = false;
	fOrientation = isVertical ? B_VERTICAL : B_HORIZONTAL;
}


/**
 * @brief Destroys the BGroupLayout.
 */
BGroupLayout::~BGroupLayout()
{
}


/**
 * @brief Returns the current spacing between items.
 *
 * Because BGroupLayout uses a single spacing value for both axes, this
 * returns @c fHSpacing, which is kept in sync with @c fVSpacing via
 * SetSpacing().
 *
 * @return The inter-item spacing in pixels.
 *
 * @see SetSpacing()
 */
float
BGroupLayout::Spacing() const
{
	return fHSpacing;
}


/**
 * @brief Sets the spacing between items and invalidates the layout.
 *
 * The value is resolved through BControlLook::ComposeSpacing() so that
 * symbolic constants such as @c B_USE_DEFAULT_SPACING are expanded.
 * Both fHSpacing and fVSpacing are updated to the same resolved value.
 *
 * @param spacing The desired spacing in pixels, or a spacing constant.
 *
 * @see Spacing()
 */
void
BGroupLayout::SetSpacing(float spacing)
{
	spacing = BControlLook::ComposeSpacing(spacing);
	if (spacing != fHSpacing) {
		fHSpacing = spacing;
		fVSpacing = spacing;
		InvalidateLayout();
	}
}


/**
 * @brief Returns the current orientation of the group layout.
 *
 * @return @c B_HORIZONTAL if items are arranged in a row,
 *         @c B_VERTICAL if they are arranged in a column.
 *
 * @see SetOrientation()
 */
orientation
BGroupLayout::Orientation() const
{
	return fOrientation;
}


/**
 * @brief Changes the layout orientation and invalidates the layout.
 *
 * @param orientation @c B_HORIZONTAL or @c B_VERTICAL.
 *
 * @see Orientation()
 */
void
BGroupLayout::SetOrientation(orientation orientation)
{
	if (orientation != fOrientation) {
		fOrientation = orientation;

		InvalidateLayout();
	}
}


/**
 * @brief Returns the layout weight of the item at the given index.
 *
 * The weight determines how surplus space along the group's axis is
 * distributed: an item with weight 2.0 grows twice as fast as one with
 * weight 1.0.
 *
 * @param index Zero-based index of the item in the layout's item list.
 * @return The item's weight, or 0 if @a index is out of range.
 *
 * @see SetItemWeight()
 */
float
BGroupLayout::ItemWeight(int32 index) const
{
	if (index < 0 || index >= CountItems())
		return 0;

	ItemLayoutData* data = _LayoutDataForItem(ItemAt(index));
	return (data ? data->weight : 0);
}


/**
 * @brief Sets the layout weight for the item at the given index.
 *
 * @param index  Zero-based index of the item.
 * @param weight The new weight; values greater than 1.0 cause the item to
 *               receive proportionally more space during layout.
 *
 * @see ItemWeight()
 */
void
BGroupLayout::SetItemWeight(int32 index, float weight)
{
	if (index < 0 || index >= CountItems())
		return;

	if (ItemLayoutData* data = _LayoutDataForItem(ItemAt(index)))
		data->weight = weight;

	InvalidateLayout();
}


/**
 * @brief Adds a view to the layout using the default weight and next available position.
 *
 * Delegates to BTwoDimensionalLayout::AddView().
 *
 * @param child The view to add.
 * @return The BLayoutItem created for the view, or @c NULL on failure.
 *
 * @see AddView(BView*, float), AddItem()
 */
BLayoutItem*
BGroupLayout::AddView(BView* child)
{
	return BTwoDimensionalLayout::AddView(child);
}


/**
 * @brief Adds a view to the layout at a specific list index with the default weight.
 *
 * Delegates to BTwoDimensionalLayout::AddView().
 *
 * @param index The insertion index in the layout's item list.
 * @param child The view to add.
 * @return The BLayoutItem created for the view, or @c NULL on failure.
 *
 * @see AddView(int32, BView*, float)
 */
BLayoutItem*
BGroupLayout::AddView(int32 index, BView* child)
{
	return BTwoDimensionalLayout::AddView(index, child);
}


/**
 * @brief Adds a view to the end of the layout with an explicit weight.
 *
 * Equivalent to AddView(-1, child, weight).
 *
 * @param child  The view to add.
 * @param weight The layout weight assigned to the view's item.
 * @return The BLayoutItem created for the view, or @c NULL on failure.
 *
 * @see AddView(int32, BView*, float), ItemWeight()
 */
BLayoutItem*
BGroupLayout::AddView(BView* child, float weight)
{
	return AddView(-1, child, weight);
}


/**
 * @brief Adds a view to the layout at a specific list index with an explicit weight.
 *
 * @param index  The insertion index in the layout's item list; pass -1 to append.
 * @param child  The view to add.
 * @param weight The layout weight assigned to the view's item.
 * @return The BLayoutItem created for the view, or @c NULL on failure.
 *
 * @see AddView(BView*, float), SetItemWeight()
 */
BLayoutItem*
BGroupLayout::AddView(int32 index, BView* child, float weight)
{
	BLayoutItem* item = AddView(index, child);
	if (ItemLayoutData* data = _LayoutDataForItem(item))
		data->weight = weight;

	return item;
}


/**
 * @brief Adds a layout item to the end of the layout with the default weight.
 *
 * Delegates to BTwoDimensionalLayout::AddItem().
 *
 * @param item The BLayoutItem to add.
 * @return @c true on success, @c false on failure.
 *
 * @see AddItem(BLayoutItem*, float)
 */
bool
BGroupLayout::AddItem(BLayoutItem* item)
{
	return BTwoDimensionalLayout::AddItem(item);
}


/**
 * @brief Adds a layout item at a specific list index with the default weight.
 *
 * Delegates to BTwoDimensionalLayout::AddItem().
 *
 * @param index The insertion index in the layout's item list; pass -1 to append.
 * @param item  The BLayoutItem to add.
 * @return @c true on success, @c false on failure.
 *
 * @see AddItem(int32, BLayoutItem*, float)
 */
bool
BGroupLayout::AddItem(int32 index, BLayoutItem* item)
{
	return BTwoDimensionalLayout::AddItem(index, item);
}


/**
 * @brief Adds a layout item to the end of the layout with an explicit weight.
 *
 * Equivalent to AddItem(-1, item, weight).
 *
 * @param item   The BLayoutItem to add.
 * @param weight The layout weight assigned to the item.
 * @return @c true on success, @c false on failure.
 *
 * @see AddItem(int32, BLayoutItem*, float), ItemWeight()
 */
bool
BGroupLayout::AddItem(BLayoutItem* item, float weight)
{
	return AddItem(-1, item, weight);
}


/**
 * @brief Adds a layout item at a specific list index with an explicit weight.
 *
 * @param index  The insertion index; pass -1 to append.
 * @param item   The BLayoutItem to add.
 * @param weight The layout weight assigned to the item.
 * @return @c true on success, @c false on failure.
 *
 * @see AddItem(BLayoutItem*, float), SetItemWeight()
 */
bool
BGroupLayout::AddItem(int32 index, BLayoutItem* item, float weight)
{
	bool success = AddItem(index, item);
	if (success) {
		if (ItemLayoutData* data = _LayoutDataForItem(item))
			data->weight = weight;
	}

	return success;
}


/**
 * @brief Serializes the BGroupLayout to a BMessage archive.
 *
 * Stores the orientation in addition to the data serialized by the base class.
 *
 * @param into The destination BMessage; must not be @c NULL.
 * @param deep If @c true, child items are also archived.
 * @return @c B_OK on success, or an error code on failure.
 *
 * @see Instantiate(), AllArchived()
 */
status_t
BGroupLayout::Archive(BMessage* into, bool deep) const
{
	BArchiver archiver(into);
	status_t result = BTwoDimensionalLayout::Archive(into, deep);

	if (result == B_OK)
		result = into->AddBool(kVerticalField, fOrientation == B_VERTICAL);

	return archiver.Finish(result);
}


/**
 * @brief Called after all objects in the archive have been archived.
 *
 * Delegates to BTwoDimensionalLayout::AllArchived().
 *
 * @param into The archive message.
 * @return @c B_OK on success, or an error code on failure.
 *
 * @see Archive()
 */
status_t
BGroupLayout::AllArchived(BMessage* into) const
{
	return BTwoDimensionalLayout::AllArchived(into);
}


/**
 * @brief Called after all objects in the archive have been unarchived.
 *
 * Delegates to BTwoDimensionalLayout::AllUnarchived().
 *
 * @param from The archive message.
 * @return @c B_OK on success, or an error code on failure.
 *
 * @see BGroupLayout(BMessage*)
 */
status_t
BGroupLayout::AllUnarchived(const BMessage* from)
{
	return BTwoDimensionalLayout::AllUnarchived(from);
}


/**
 * @brief Instantiates a BGroupLayout from an archive message.
 *
 * Validates the archive class name before constructing the object.
 *
 * @param from The archive message to restore from.
 * @return A newly allocated BGroupLayout, or @c NULL if the message is invalid.
 *
 * @see Archive(), BGroupLayout(BMessage*)
 */
BArchivable*
BGroupLayout::Instantiate(BMessage* from)
{
	if (validate_instantiation(from, "BGroupLayout"))
		return new(nothrow) BGroupLayout(from);
	return NULL;
}


/**
 * @brief Archives the weight of a single layout item into a message.
 *
 * Called by the archiving framework for each item. Stores the item weight
 * as a float under @c kItemWeightField.
 *
 * @param into  The message to write into.
 * @param item  The item whose weight is archived.
 * @param index The item's index in the layout's item list.
 * @return @c B_OK on success, or an error code on failure.
 *
 * @see ItemUnarchived()
 */
status_t
BGroupLayout::ItemArchived(BMessage* into,
	BLayoutItem* item, int32 index) const
{
	return into->AddFloat(kItemWeightField, _LayoutDataForItem(item)->weight);
}


/**
 * @brief Restores the weight of a single layout item from an archive message.
 *
 * Called by the archiving framework for each item during unarchiving.
 *
 * @param from  The archive message to read from.
 * @param item  The item whose weight is restored.
 * @param index The item's index in the layout's item list (used to locate
 *              the correct value in the float array).
 * @return @c B_OK on success, or an error code if the field is missing.
 *
 * @see ItemArchived()
 */
status_t
BGroupLayout::ItemUnarchived(const BMessage* from,
	BLayoutItem* item, int32 index)
{
	float weight;
	status_t result = from->FindFloat(kItemWeightField, index, &weight);

	if (result == B_OK)
		_LayoutDataForItem(item)->weight = weight;

	return result;
}


/**
 * @brief Called by the layout framework when an item has been added to the layout.
 *
 * Allocates and attaches a fresh ItemLayoutData (default weight 1.0) to the item.
 *
 * @param item    The item that was added.
 * @param atIndex The index at which it was inserted.
 * @return @c true if the layout data was successfully allocated, @c false on failure.
 *
 * @see ItemRemoved()
 */
bool
BGroupLayout::ItemAdded(BLayoutItem* item, int32 atIndex)
{
	item->SetLayoutData(new(nothrow) ItemLayoutData);
	return item->LayoutData() != NULL;
}


/**
 * @brief Called by the layout framework when an item has been removed from the layout.
 *
 * Frees the ItemLayoutData that was attached to the item in ItemAdded().
 *
 * @param item      The item that was removed.
 * @param fromIndex The index from which it was removed.
 *
 * @see ItemAdded()
 */
void
BGroupLayout::ItemRemoved(BLayoutItem* item, int32 fromIndex)
{
	if (ItemLayoutData* data = _LayoutDataForItem(item)) {
		item->SetLayoutData(NULL);
		delete data;
	}
}


/**
 * @brief Builds the visible-items list used during layout computation.
 *
 * Called by the layout engine before column/row counting and constraint
 * queries. Iterates all items and collects those that report IsVisible().
 *
 * @param orientation The orientation being computed (passed through from the
 *                    engine; not used to filter items).
 *
 * @see InternalCountColumns(), InternalCountRows(), GetColumnRowConstraints()
 */
void
BGroupLayout::PrepareItems(orientation orientation)
{
	// filter the visible items
	fVisibleItems.MakeEmpty();
	int32 itemCount = CountItems();
	for (int i = 0; i < itemCount; i++) {
		BLayoutItem* item = ItemAt(i);
		if (item->IsVisible())
			fVisibleItems.AddItem(item);
	}
}


/**
 * @brief Returns the number of logical columns seen by the layout engine.
 *
 * For a horizontal group this equals the number of visible items; for a
 * vertical group it is always 1.
 *
 * @return The column count used during layout computation.
 *
 * @see InternalCountRows(), PrepareItems()
 */
int32
BGroupLayout::InternalCountColumns()
{
	return (fOrientation == B_HORIZONTAL ? fVisibleItems.CountItems() : 1);
}


/**
 * @brief Returns the number of logical rows seen by the layout engine.
 *
 * For a vertical group this equals the number of visible items; for a
 * horizontal group it is always 1.
 *
 * @return The row count used during layout computation.
 *
 * @see InternalCountColumns(), PrepareItems()
 */
int32
BGroupLayout::InternalCountRows()
{
	return (fOrientation == B_VERTICAL ? fVisibleItems.CountItems() : 1);
}


/**
 * @brief Fills in size and weight constraints for a specific column or row.
 *
 * The group layout does not enforce per-column/row min or max sizes directly
 * (those come from the items themselves); only the per-item weight is applied.
 *
 * @param orientation B_HORIZONTAL to query a column slot, B_VERTICAL for a row slot.
 * @param index       Zero-based slot index within the visible items list.
 * @param constraints Output structure populated with min (-1), max
 *                    (B_SIZE_UNLIMITED), and the item's weight.
 *
 * @see PrepareItems(), ItemWeight()
 */
void
BGroupLayout::GetColumnRowConstraints(orientation orientation, int32 index,
	ColumnRowConstraints* constraints)
{
	if (index >= 0 && index < fVisibleItems.CountItems()) {
		BLayoutItem* item = (BLayoutItem*)fVisibleItems.ItemAt(index);
		constraints->min = -1;
		constraints->max = B_SIZE_UNLIMITED;
		if (ItemLayoutData* data = _LayoutDataForItem(item))
			constraints->weight = data->weight;
		else
			constraints->weight = 1;
	}
}


/**
 * @brief Returns the grid-style dimensions for a given item within the visible list.
 *
 * Maps the item's position in the visible items list to a single-cell grid
 * coordinate: for horizontal groups the column index increments; for vertical
 * groups the row index increments.
 *
 * @param item       The layout item to query.
 * @param dimensions Output structure populated with the item's logical position
 *                   and a 1x1 span.
 *
 * @see PrepareItems(), GetColumnRowConstraints()
 */
void
BGroupLayout::GetItemDimensions(BLayoutItem* item, Dimensions* dimensions)
{
	int32 index = fVisibleItems.IndexOf(item);
	if (index < 0)
		return;

	if (fOrientation == B_HORIZONTAL) {
		dimensions->x = index;
		dimensions->y = 0;
		dimensions->width = 1;
		dimensions->height = 1;
	} else {
		dimensions->x = 0;
		dimensions->y = index;
		dimensions->width = 1;
		dimensions->height = 1;
	}
}


/**
 * @brief Returns the ItemLayoutData associated with a layout item.
 *
 * @param item The layout item to query; may be @c NULL.
 * @return A pointer to the item's ItemLayoutData, or @c NULL if @a item is @c NULL.
 *
 * @see ItemAdded(), ItemRemoved()
 */
BGroupLayout::ItemLayoutData*
BGroupLayout::_LayoutDataForItem(BLayoutItem* item) const
{
	return item == NULL ? NULL : (ItemLayoutData*)item->LayoutData();
}


/**
 * @brief Dispatches perform codes to the base class implementation.
 *
 * @param code The perform code identifying the operation.
 * @param _data Opaque argument passed through to the base class.
 * @return The result returned by BTwoDimensionalLayout::Perform().
 */
status_t
BGroupLayout::Perform(perform_code code, void* _data)
{
	return BTwoDimensionalLayout::Perform(code, _data);
}


void BGroupLayout::_ReservedGroupLayout1() {}
void BGroupLayout::_ReservedGroupLayout2() {}
void BGroupLayout::_ReservedGroupLayout3() {}
void BGroupLayout::_ReservedGroupLayout4() {}
void BGroupLayout::_ReservedGroupLayout5() {}
void BGroupLayout::_ReservedGroupLayout6() {}
void BGroupLayout::_ReservedGroupLayout7() {}
void BGroupLayout::_ReservedGroupLayout8() {}
void BGroupLayout::_ReservedGroupLayout9() {}
void BGroupLayout::_ReservedGroupLayout10() {}
