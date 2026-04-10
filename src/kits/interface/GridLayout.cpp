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
 *   Copyright 2010-2011 Haiku, Inc. All rights reserved.
 *   Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file GridLayout.cpp
 * @brief Implementation of BGridLayout, a layout manager that arranges items in a grid
 *
 * BGridLayout places items in a two-dimensional grid of rows and columns. Items can
 * span multiple cells. Row heights and column widths are computed from item size
 * constraints using the layout engine.
 *
 * @see BTwoDimensionalLayout, BGridView, BGridLayoutBuilder
 */


#include <GridLayout.h>

#include <algorithm>
#include <new>
#include <string.h>

#include <ControlLook.h>
#include <LayoutItem.h>
#include <List.h>
#include <Message.h>

#include "ViewLayoutItem.h"


using std::nothrow;
using std::swap;


/** @brief Maximum permitted number of columns or rows in a single BGridLayout. */
enum {
	MAX_COLUMN_ROW_COUNT	= 1024,
};


namespace {
	/** @brief Sentinel value placed in grid cells occupied by a multi-cell item's non-origin cells. */
	BLayoutItem* const OCCUPIED_GRID_CELL = (BLayoutItem*)0x1;

	/** @brief Archive field name for per-row {min, max} size pairs. */
	const char* const kRowSizesField = "BGridLayout:rowsizes";
		// kRowSizesField = {min, max}
	/** @brief Archive field name for per-row weight values. */
	const char* const kRowWeightField = "BGridLayout:rowweight";
	/** @brief Archive field name for per-column {min, max} size pairs. */
	const char* const kColumnSizesField = "BGridLayout:columnsizes";
		// kColumnSizesField = {min, max}
	/** @brief Archive field name for per-column weight values. */
	const char* const kColumnWeightField = "BGridLayout:columnweight";
	/** @brief Archive field name encoding each item's {x, y, width, height} grid dimensions. */
	const char* const kItemDimensionsField = "BGridLayout:item:dimensions";
		// kItemDimensionsField = {x, y, width, height}
}


struct BGridLayout::ItemLayoutData {
	Dimensions	dimensions;

	ItemLayoutData()
	{
		dimensions.x = 0;
		dimensions.y = 0;
		dimensions.width = 1;
		dimensions.height = 1;
	}
};


class BGridLayout::RowInfoArray {
public:
	RowInfoArray()
	{
	}

	~RowInfoArray()
	{
		for (int32 i = 0; Info* info = (Info*)fInfos.ItemAt(i); i++)
			delete info;
	}

	int32 Count() const
	{
		return fInfos.CountItems();
	}

	float Weight(int32 index) const
	{
		if (Info* info = _InfoAt(index))
			return info->weight;
		return 1;
	}

	void SetWeight(int32 index, float weight)
	{
		if (Info* info = _InfoAt(index, true))
			info->weight = weight;
	}

	float MinSize(int32 index) const
	{
		if (Info* info = _InfoAt(index))
			return info->minSize;
		return B_SIZE_UNSET;
	}

	void SetMinSize(int32 index, float size)
	{
		if (Info* info = _InfoAt(index, true))
			info->minSize = size;
	}

	float MaxSize(int32 index) const
	{
		if (Info* info = _InfoAt(index))
			return info->maxSize;
		return B_SIZE_UNSET;
	}

	void SetMaxSize(int32 index, float size)
	{
		if (Info* info = _InfoAt(index, true))
			info->maxSize = size;
	}

private:
	struct Info {
		float	weight;
		float	minSize;
		float	maxSize;
	};

	Info* _InfoAt(int32 index) const
	{
		return (Info*)fInfos.ItemAt(index);
	}

	Info* _InfoAt(int32 index, bool resize)
	{
		if (index < 0 || index >= MAX_COLUMN_ROW_COUNT)
			return NULL;

		// resize, if necessary and desired
		int32 count = Count();
		if (index >= count) {
			if (!resize)
				return NULL;

			for (int32 i = count; i <= index; i++) {
				Info* info = new Info;
				info->weight = 1;
				info->minSize = B_SIZE_UNSET;
				info->maxSize = B_SIZE_UNSET;
				fInfos.AddItem(info);
			}
		}

		return _InfoAt(index);
	}

	BList		fInfos;
};


/**
 * @brief Constructs a BGridLayout with the specified inter-cell spacing.
 *
 * @param horizontal Horizontal spacing between columns, in pixels.
 *                   Pass @c B_USE_DEFAULT_SPACING to use the system default.
 * @param vertical   Vertical spacing between rows, in pixels.
 *                   Pass @c B_USE_DEFAULT_SPACING to use the system default.
 */
BGridLayout::BGridLayout(float horizontal, float vertical)
	:
	fGrid(NULL),
	fColumnCount(0),
	fRowCount(0),
	fRowInfos(new RowInfoArray),
	fColumnInfos(new RowInfoArray),
	fMultiColumnItems(0),
	fMultiRowItems(0)
{
	SetSpacing(horizontal, vertical);
}


/**
 * @brief Unarchives a BGridLayout from a BMessage.
 *
 * Restores column and row counts, per-column and per-row weights and size
 * constraints, and then delegates to the base class for item restoration.
 *
 * @param from The archive message produced by a previous call to Archive().
 *
 * @see BGridLayout::Archive(), BGridLayout::Instantiate()
 */
BGridLayout::BGridLayout(BMessage* from)
	:
	BTwoDimensionalLayout(BUnarchiver::PrepareArchive(from)),
	fGrid(NULL),
	fColumnCount(0),
	fRowCount(0),
	fRowInfos(new RowInfoArray),
	fColumnInfos(new RowInfoArray),
	fMultiColumnItems(0),
	fMultiRowItems(0)
{
	BUnarchiver unarchiver(from);
	int32 columns;
	from->GetInfo(kColumnWeightField, NULL, &columns);

	int32 rows;
	from->GetInfo(kRowWeightField, NULL, &rows);

	// sets fColumnCount && fRowCount on success
	if (!_ResizeGrid(columns, rows)) {
		unarchiver.Finish(B_NO_MEMORY);
		return;
	}

	for (int32 i = 0; i < fRowCount; i++) {
		float getter;
		if (from->FindFloat(kRowWeightField, i, &getter) == B_OK)
			fRowInfos->SetWeight(i, getter);

		if (from->FindFloat(kRowSizesField, i * 2, &getter) == B_OK)
			fRowInfos->SetMinSize(i, getter);

		if (from->FindFloat(kRowSizesField, i * 2 + 1, &getter) == B_OK)
			fRowInfos->SetMaxSize(i, getter);
	}

	for (int32 i = 0; i < fColumnCount; i++) {
		float getter;
		if (from->FindFloat(kColumnWeightField, i, &getter) == B_OK)
			fColumnInfos->SetWeight(i, getter);

		if (from->FindFloat(kColumnSizesField, i * 2, &getter) == B_OK)
			fColumnInfos->SetMinSize(i, getter);

		if (from->FindFloat(kColumnSizesField, i * 2 + 1, &getter) == B_OK)
			fColumnInfos->SetMaxSize(i, getter);
	}
}


/**
 * @brief Destroys the BGridLayout, releasing the internal grid and info arrays.
 */
BGridLayout::~BGridLayout()
{
	delete fRowInfos;
	delete fColumnInfos;

	for (int32 i = 0; i < fColumnCount; i++)
		delete[] fGrid[i];
	delete[] fGrid;
}


/**
 * @brief Returns the number of columns currently in the grid.
 *
 * @return The column count, which may be 0 if no items have been added.
 *
 * @see CountRows(), ColumnWeight()
 */
int32
BGridLayout::CountColumns() const
{
	return fColumnCount;
}


/**
 * @brief Returns the number of rows currently in the grid.
 *
 * @return The row count, which may be 0 if no items have been added.
 *
 * @see CountColumns(), RowWeight()
 */
int32
BGridLayout::CountRows() const
{
	return fRowCount;
}


/**
 * @brief Returns the horizontal spacing between columns.
 *
 * @return Spacing in pixels between adjacent columns.
 *
 * @see SetHorizontalSpacing(), VerticalSpacing()
 */
float
BGridLayout::HorizontalSpacing() const
{
	return fHSpacing;
}


/**
 * @brief Returns the vertical spacing between rows.
 *
 * @return Spacing in pixels between adjacent rows.
 *
 * @see SetVerticalSpacing(), HorizontalSpacing()
 */
float
BGridLayout::VerticalSpacing() const
{
	return fVSpacing;
}


/**
 * @brief Sets the horizontal spacing between columns and invalidates the layout.
 *
 * The value is first passed through BControlLook::ComposeSpacing() so that
 * symbolic constants such as @c B_USE_DEFAULT_SPACING are resolved.
 *
 * @param spacing The desired horizontal spacing in pixels, or a spacing constant.
 *
 * @see SetVerticalSpacing(), SetSpacing()
 */
void
BGridLayout::SetHorizontalSpacing(float spacing)
{
	spacing = BControlLook::ComposeSpacing(spacing);
	if (spacing != fHSpacing) {
		fHSpacing = spacing;

		InvalidateLayout();
	}
}


/**
 * @brief Sets the vertical spacing between rows and invalidates the layout.
 *
 * The value is first passed through BControlLook::ComposeSpacing() so that
 * symbolic constants such as @c B_USE_DEFAULT_SPACING are resolved.
 *
 * @param spacing The desired vertical spacing in pixels, or a spacing constant.
 *
 * @see SetHorizontalSpacing(), SetSpacing()
 */
void
BGridLayout::SetVerticalSpacing(float spacing)
{
	spacing = BControlLook::ComposeSpacing(spacing);
	if (spacing != fVSpacing) {
		fVSpacing = spacing;

		InvalidateLayout();
	}
}


/**
 * @brief Sets both horizontal and vertical spacing in a single call.
 *
 * Equivalent to calling SetHorizontalSpacing() and SetVerticalSpacing()
 * together, but only triggers one layout invalidation.
 *
 * @param horizontal Horizontal spacing in pixels, or a spacing constant.
 * @param vertical   Vertical spacing in pixels, or a spacing constant.
 *
 * @see SetHorizontalSpacing(), SetVerticalSpacing()
 */
void
BGridLayout::SetSpacing(float horizontal, float vertical)
{
	horizontal = BControlLook::ComposeSpacing(horizontal);
	vertical = BControlLook::ComposeSpacing(vertical);
	if (horizontal != fHSpacing || vertical != fVSpacing) {
		fHSpacing = horizontal;
		fVSpacing = vertical;

		InvalidateLayout();
	}
}


/**
 * @brief Returns the layout weight for the specified column.
 *
 * The weight governs how surplus horizontal space is distributed among
 * columns: a column with weight 2.0 receives twice as much extra space
 * as one with weight 1.0.
 *
 * @param column Zero-based column index.
 * @return The column's weight, defaulting to 1.0 if not explicitly set.
 *
 * @see SetColumnWeight(), RowWeight()
 */
float
BGridLayout::ColumnWeight(int32 column) const
{
	return fColumnInfos->Weight(column);
}


/**
 * @brief Sets the layout weight for the specified column.
 *
 * @param column Zero-based column index.
 * @param weight The new weight. Values greater than 1.0 cause the column
 *               to receive proportionally more space during layout.
 *
 * @see ColumnWeight(), SetRowWeight()
 */
void
BGridLayout::SetColumnWeight(int32 column, float weight)
{
	fColumnInfos->SetWeight(column, weight);
}


/**
 * @brief Returns the minimum width constraint for the specified column.
 *
 * @param column Zero-based column index.
 * @return The minimum width in pixels, or @c B_SIZE_UNSET if unconstrained.
 *
 * @see SetMinColumnWidth(), MaxColumnWidth()
 */
float
BGridLayout::MinColumnWidth(int32 column) const
{
	return fColumnInfos->MinSize(column);
}


/**
 * @brief Sets the minimum width constraint for the specified column.
 *
 * @param column Zero-based column index.
 * @param width  The minimum width in pixels.
 *
 * @see MinColumnWidth(), SetMaxColumnWidth()
 */
void
BGridLayout::SetMinColumnWidth(int32 column, float width)
{
	fColumnInfos->SetMinSize(column, width);
}


/**
 * @brief Returns the maximum width constraint for the specified column.
 *
 * @param column Zero-based column index.
 * @return The maximum width in pixels, or @c B_SIZE_UNSET if unconstrained.
 *
 * @see SetMaxColumnWidth(), MinColumnWidth()
 */
float
BGridLayout::MaxColumnWidth(int32 column) const
{
	return fColumnInfos->MaxSize(column);
}


/**
 * @brief Sets the maximum width constraint for the specified column.
 *
 * @param column Zero-based column index.
 * @param width  The maximum width in pixels.
 *
 * @see MaxColumnWidth(), SetMinColumnWidth()
 */
void
BGridLayout::SetMaxColumnWidth(int32 column, float width)
{
	fColumnInfos->SetMaxSize(column, width);
}


/**
 * @brief Returns the layout weight for the specified row.
 *
 * The weight governs how surplus vertical space is distributed among rows.
 *
 * @param row Zero-based row index.
 * @return The row's weight, defaulting to 1.0 if not explicitly set.
 *
 * @see SetRowWeight(), ColumnWeight()
 */
float
BGridLayout::RowWeight(int32 row) const
{
	return fRowInfos->Weight(row);
}


/**
 * @brief Sets the layout weight for the specified row.
 *
 * @param row    Zero-based row index.
 * @param weight The new weight. Values greater than 1.0 cause the row
 *               to receive proportionally more space during layout.
 *
 * @see RowWeight(), SetColumnWeight()
 */
void
BGridLayout::SetRowWeight(int32 row, float weight)
{
	fRowInfos->SetWeight(row, weight);
}


/**
 * @brief Returns the minimum height constraint for the specified row.
 *
 * @param row Zero-based row index.
 * @return The minimum height in pixels, or @c B_SIZE_UNSET if unconstrained.
 *
 * @see SetMinRowHeight(), MaxRowHeight()
 */
float
BGridLayout::MinRowHeight(int row) const
{
	return fRowInfos->MinSize(row);
}


/**
 * @brief Sets the minimum height constraint for the specified row.
 *
 * @param row    Zero-based row index.
 * @param height The minimum height in pixels.
 *
 * @see MinRowHeight(), SetMaxRowHeight()
 */
void
BGridLayout::SetMinRowHeight(int32 row, float height)
{
	fRowInfos->SetMinSize(row, height);
}


/**
 * @brief Returns the maximum height constraint for the specified row.
 *
 * @param row Zero-based row index.
 * @return The maximum height in pixels, or @c B_SIZE_UNSET if unconstrained.
 *
 * @see SetMaxRowHeight(), MinRowHeight()
 */
float
BGridLayout::MaxRowHeight(int32 row) const
{
	return fRowInfos->MaxSize(row);
}


/**
 * @brief Sets the maximum height constraint for the specified row.
 *
 * @param row    Zero-based row index.
 * @param height The maximum height in pixels.
 *
 * @see MaxRowHeight(), SetMinRowHeight()
 */
void
BGridLayout::SetMaxRowHeight(int32 row, float height)
{
	fRowInfos->SetMaxSize(row, height);
}


/**
 * @brief Returns the layout item occupying the specified grid cell.
 *
 * Only the origin cell of a multi-cell item holds a real BLayoutItem pointer;
 * the remaining spanned cells contain the internal @c OCCUPIED_GRID_CELL sentinel.
 *
 * @param column Zero-based column index.
 * @param row    Zero-based row index.
 * @return The BLayoutItem at (@a column, @a row), or @c NULL if the cell
 *         is empty or the indices are out of range.
 *
 * @see AddItem(), CountColumns(), CountRows()
 */
BLayoutItem*
BGridLayout::ItemAt(int32 column, int32 row) const
{
	if (column < 0 || column >= CountColumns()
		|| row < 0 || row >= CountRows())
		return NULL;

	return fGrid[column][row];
}


/**
 * @brief Adds a view to the layout at an automatically chosen position.
 *
 * Delegates to BTwoDimensionalLayout::AddView(), which wraps the view in a
 * BViewLayoutItem before calling the grid-aware AddItem() overload.
 *
 * @param child The view to add.
 * @return The BLayoutItem created for the view, or @c NULL on failure.
 *
 * @see AddView(BView*, int32, int32, int32, int32), AddItem()
 */
BLayoutItem*
BGridLayout::AddView(BView* child)
{
	return BTwoDimensionalLayout::AddView(child);
}


/**
 * @brief Adds a view to the layout at a specific list index.
 *
 * The @a index parameter refers to the layout's item list, not a grid cell.
 * Actual grid placement is still determined automatically.
 *
 * @param index The insertion index in the layout's item list.
 * @param child The view to add.
 * @return The BLayoutItem created for the view, or @c NULL on failure.
 *
 * @see AddView(BView*, int32, int32, int32, int32)
 */
BLayoutItem*
BGridLayout::AddView(int32 index, BView* child)
{
	return BTwoDimensionalLayout::AddView(index, child);
}


/**
 * @brief Adds a view to the grid at a specific cell position, optionally spanning multiple cells.
 *
 * Creates a BViewLayoutItem wrapper and delegates to
 * AddItem(BLayoutItem*, int32, int32, int32, int32).
 *
 * @param child       The view to add; must not be @c NULL.
 * @param column      Zero-based starting column.
 * @param row         Zero-based starting row.
 * @param columnCount Number of columns the view spans (minimum 1).
 * @param rowCount    Number of rows the view spans (minimum 1).
 * @return The BLayoutItem created for the view, or @c NULL if @a child is
 *         @c NULL, the target cells are occupied, or allocation fails.
 *
 * @see AddItem(BLayoutItem*, int32, int32, int32, int32)
 */
BLayoutItem*
BGridLayout::AddView(BView* child, int32 column, int32 row, int32 columnCount,
	int32 rowCount)
{
	if (!child)
		return NULL;

	BLayoutItem* item = new BViewLayoutItem(child);
	if (!AddItem(item, column, row, columnCount, rowCount)) {
		delete item;
		return NULL;
	}

	return item;
}


/**
 * @brief Adds a layout item to the first available empty grid cell.
 *
 * Scans the existing grid left-to-right, top-to-bottom for an empty cell.
 * If none is found, the item is placed in a new column appended to the grid.
 *
 * @param item The BLayoutItem to add; must not be @c NULL.
 * @return @c true on success, @c false if the item could not be added.
 *
 * @see AddItem(BLayoutItem*, int32, int32, int32, int32)
 */
bool
BGridLayout::AddItem(BLayoutItem* item)
{
	// find a free spot
	for (int32 row = 0; row < fRowCount; row++) {
		for (int32 column = 0; column < fColumnCount; column++) {
			if (_IsGridCellEmpty(column, row))
				return AddItem(item, column, row, 1, 1);
		}
	}

	// no free spot, start a new column
	return AddItem(item, fColumnCount, 0, 1, 1);
}


/**
 * @brief Adds a layout item ignoring the index hint, delegating to AddItem(BLayoutItem*).
 *
 * BGridLayout manages its own spatial indexing, so the @a index parameter is
 * ignored and the item is placed in the next available cell.
 *
 * @param index Ignored.
 * @param item  The BLayoutItem to add.
 * @return @c true on success, @c false otherwise.
 *
 * @see AddItem(BLayoutItem*)
 */
bool
BGridLayout::AddItem(int32 index, BLayoutItem* item)
{
	return AddItem(item);
}


/**
 * @brief Adds a layout item to the grid at the specified position, optionally spanning cells.
 *
 * All cells in the target region must be empty. On success, the item's layout
 * data is populated with the given dimensions, the grid is expanded if necessary,
 * and multi-span counters are updated.
 *
 * @param item        The BLayoutItem to add; must not be @c NULL.
 * @param column      Zero-based starting column.
 * @param row         Zero-based starting row.
 * @param columnCount Number of columns to span (minimum 1).
 * @param rowCount    Number of rows to span (minimum 1).
 * @return @c true on success, @c false if any target cell is occupied or
 *         memory allocation fails.
 *
 * @see ItemAt(), RemoveItem()
 */
bool
BGridLayout::AddItem(BLayoutItem* item, int32 column, int32 row,
	int32 columnCount, int32 rowCount)
{
	if (!_AreGridCellsEmpty(column, row, columnCount, rowCount))
		return false;

	bool success = BTwoDimensionalLayout::AddItem(-1, item);
	if (!success)
		return false;

	// set item dimensions
	if (ItemLayoutData* data = _LayoutDataForItem(item)) {
		data->dimensions.x = column;
		data->dimensions.y = row;
		data->dimensions.width = columnCount;
		data->dimensions.height = rowCount;
	}

	if (!_InsertItemIntoGrid(item)) {
		RemoveItem(item);
		return false;
	}

	if (columnCount > 1)
		fMultiColumnItems++;
	if (rowCount > 1)
		fMultiRowItems++;

	return success;
}


/**
 * @brief Serializes the BGridLayout to a BMessage archive.
 *
 * Stores per-row and per-column weights and size constraints in addition
 * to the data serialized by BTwoDimensionalLayout::Archive().
 *
 * @param into The destination BMessage; must not be @c NULL.
 * @param deep If @c true, child items are also archived.
 * @return @c B_OK on success, or an error code on failure.
 *
 * @see Instantiate(), AllArchived()
 */
status_t
BGridLayout::Archive(BMessage* into, bool deep) const
{
	BArchiver archiver(into);
	status_t result = BTwoDimensionalLayout::Archive(into, deep);

	for (int32 i = 0; i < fRowCount && result == B_OK; i++) {
		result = into->AddFloat(kRowWeightField, fRowInfos->Weight(i));
		if (result == B_OK)
			result = into->AddFloat(kRowSizesField, fRowInfos->MinSize(i));
		if (result == B_OK)
			result = into->AddFloat(kRowSizesField, fRowInfos->MaxSize(i));
	}

	for (int32 i = 0; i < fColumnCount && result == B_OK; i++) {
		result = into->AddFloat(kColumnWeightField, fColumnInfos->Weight(i));
		if (result == B_OK)
			result = into->AddFloat(kColumnSizesField, fColumnInfos->MinSize(i));
		if (result == B_OK)
			result = into->AddFloat(kColumnSizesField, fColumnInfos->MaxSize(i));
	}

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
BGridLayout::AllArchived(BMessage* into) const
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
 * @see BGridLayout(BMessage*)
 */
status_t
BGridLayout::AllUnarchived(const BMessage* from)
{
	return BTwoDimensionalLayout::AllUnarchived(from);
}


/**
 * @brief Instantiates a BGridLayout from an archive message.
 *
 * Validates the archive class name before constructing the object.
 *
 * @param from The archive message to restore from.
 * @return A newly allocated BGridLayout, or @c NULL if the message is invalid.
 *
 * @see Archive(), BGridLayout(BMessage*)
 */
BArchivable*
BGridLayout::Instantiate(BMessage* from)
{
	if (validate_instantiation(from, "BGridLayout"))
		return new BGridLayout(from);
	return NULL;
}


/**
 * @brief Archives per-item grid placement data (x, y, width, height) into a message.
 *
 * Called by the archiving framework for each item. Stores the item's four
 * dimension fields as consecutive int32 values under @c kItemDimensionsField.
 *
 * @param into  The message to write into.
 * @param item  The item whose placement data is archived.
 * @param index The item's index in the layout's item list.
 * @return @c B_OK on success, or an error code on failure.
 *
 * @see ItemUnarchived()
 */
status_t
BGridLayout::ItemArchived(BMessage* into, BLayoutItem* item, int32 index) const
{
	ItemLayoutData* data =	_LayoutDataForItem(item);

	status_t result = into->AddInt32(kItemDimensionsField, data->dimensions.x);
	if (result == B_OK)
		result = into->AddInt32(kItemDimensionsField, data->dimensions.y);

	if (result == B_OK)
		result = into->AddInt32(kItemDimensionsField, data->dimensions.width);

	if (result == B_OK)
		result = into->AddInt32(kItemDimensionsField, data->dimensions.height);

	return result;
}


/**
 * @brief Restores per-item grid placement data from an archive message.
 *
 * Reads the four consecutive int32 dimension values written by ItemArchived(),
 * validates that the target cells are unoccupied, and inserts the item into
 * the grid.
 *
 * @param from  The archive message to read from.
 * @param item  The item whose placement data is being restored.
 * @param index The item's index in the layout's item list.
 * @return @c B_OK on success, @c B_BAD_DATA if the target cells are occupied,
 *         @c B_NO_MEMORY if grid expansion fails, or another error code on
 *         message read failure.
 *
 * @see ItemArchived()
 */
status_t
BGridLayout::ItemUnarchived(const BMessage* from,
	BLayoutItem* item, int32 index)
{
	ItemLayoutData* data = _LayoutDataForItem(item);
	Dimensions& dimensions = data->dimensions;

	index *= 4;
		// each item stores 4 int32s into kItemDimensionsField
	status_t result = from->FindInt32(kItemDimensionsField, index, &dimensions.x);
	if (result == B_OK)
		result = from->FindInt32(kItemDimensionsField, ++index, &dimensions.y);

	if (result == B_OK)
		result = from->FindInt32(kItemDimensionsField, ++index, &dimensions.width);

	if (result == B_OK) {
		result = from->FindInt32(kItemDimensionsField,
			++index, &dimensions.height);
	}

	if (result != B_OK)
		return result;

	if (!_AreGridCellsEmpty(dimensions.x, dimensions.y,
		dimensions.width, dimensions.height))
		return B_BAD_DATA;

	if (!_InsertItemIntoGrid(item))
		return B_NO_MEMORY;

	if (dimensions.width > 1)
		fMultiColumnItems++;

	if (dimensions.height > 1)
		fMultiRowItems++;

	return result;
}


/**
 * @brief Called by the layout framework when an item has been added to the layout.
 *
 * Allocates and attaches a fresh ItemLayoutData to the item, which tracks its
 * grid position and span. Returns @c false if allocation fails.
 *
 * @param item    The item that was added.
 * @param atIndex The index at which it was inserted.
 * @return @c true if the layout data was successfully allocated, @c false on
 *         allocation failure.
 *
 * @see ItemRemoved()
 */
bool
BGridLayout::ItemAdded(BLayoutItem* item, int32 atIndex)
{
	item->SetLayoutData(new(nothrow) ItemLayoutData);
	return item->LayoutData() != NULL;
}


/**
 * @brief Called by the layout framework when an item has been removed from the layout.
 *
 * Frees the item's ItemLayoutData, clears its grid cells, updates multi-span
 * counters, and shrinks the grid if trailing empty columns or rows result.
 *
 * @param item      The item that was removed.
 * @param fromIndex The index from which it was removed.
 *
 * @see ItemAdded()
 */
void
BGridLayout::ItemRemoved(BLayoutItem* item, int32 fromIndex)
{
	ItemLayoutData* data = _LayoutDataForItem(item);
	Dimensions itemDimensions = data->dimensions;
	item->SetLayoutData(NULL);
	delete data;

	if (itemDimensions.width > 1)
		fMultiColumnItems--;

	if (itemDimensions.height > 1)
		fMultiRowItems--;

	// remove the item from the grid
	for (int x = 0; x < itemDimensions.width; x++) {
		for (int y = 0; y < itemDimensions.height; y++)
			fGrid[itemDimensions.x + x][itemDimensions.y + y] = NULL;
	}

	// check whether we can shrink the grid
	if (itemDimensions.x + itemDimensions.width == fColumnCount
		|| itemDimensions.y + itemDimensions.height == fRowCount) {
		int32 columnCount = fColumnCount;
		int32 rowCount = fRowCount;

		// check for empty columns
		bool empty = true;
		for (; columnCount > 0; columnCount--) {
			for (int32 row = 0; empty && row < rowCount; row++)
				empty &= (fGrid[columnCount - 1][row] == NULL);

			if (!empty)
				break;
		}

		// check for empty rows
		empty = true;
		for (; rowCount > 0; rowCount--) {
			for (int32 column = 0; empty && column < columnCount; column++)
				empty &= (fGrid[column][rowCount - 1] == NULL);

			if (!empty)
				break;
		}

		// resize the grid
		if (columnCount != fColumnCount || rowCount != fRowCount)
			_ResizeGrid(columnCount, rowCount);
	}
}


/**
 * @brief Returns whether the layout contains any item that spans more than one column.
 *
 * @return @c true if at least one multi-column item is present.
 *
 * @see HasMultiRowItems()
 */
bool
BGridLayout::HasMultiColumnItems()
{
	return fMultiColumnItems > 0;
}


/**
 * @brief Returns whether the layout contains any item that spans more than one row.
 *
 * @return @c true if at least one multi-row item is present.
 *
 * @see HasMultiColumnItems()
 */
bool
BGridLayout::HasMultiRowItems()
{
	return fMultiRowItems > 0;
}


/**
 * @brief Returns the internal column count used by the layout engine.
 *
 * Called by BTwoDimensionalLayout during layout computation.
 *
 * @return The current column count.
 *
 * @see InternalCountRows()
 */
int32
BGridLayout::InternalCountColumns()
{
	return fColumnCount;
}


/**
 * @brief Returns the internal row count used by the layout engine.
 *
 * Called by BTwoDimensionalLayout during layout computation.
 *
 * @return The current row count.
 *
 * @see InternalCountColumns()
 */
int32
BGridLayout::InternalCountRows()
{
	return fRowCount;
}


/**
 * @brief Fills in size and weight constraints for a specific column or row.
 *
 * Called by the layout engine to obtain the constraints it needs when solving
 * for column widths (B_HORIZONTAL) or row heights (B_VERTICAL).
 *
 * @param orientation B_HORIZONTAL to query a column, B_VERTICAL to query a row.
 * @param index       Zero-based column or row index.
 * @param constraints Output structure populated with min, max, and weight values.
 *
 * @see ColumnWeight(), RowWeight(), MinColumnWidth(), MinRowHeight()
 */
void
BGridLayout::GetColumnRowConstraints(orientation orientation, int32 index,
	ColumnRowConstraints* constraints)
{
	if (orientation == B_HORIZONTAL) {
		constraints->min = MinColumnWidth(index);
		constraints->max = MaxColumnWidth(index);
		constraints->weight = ColumnWeight(index);
	} else {
		constraints->min = MinRowHeight(index);
		constraints->max = MaxRowHeight(index);
		constraints->weight = RowWeight(index);
	}
}


/**
 * @brief Retrieves the grid placement dimensions for a given layout item.
 *
 * Populates @a dimensions with the item's column origin, row origin, column
 * span, and row span as stored in its ItemLayoutData.
 *
 * @param item       The layout item to query.
 * @param dimensions Output structure populated with the item's grid dimensions.
 *
 * @see AddItem(BLayoutItem*, int32, int32, int32, int32)
 */
void
BGridLayout::GetItemDimensions(BLayoutItem* item, Dimensions* dimensions)
{
	if (ItemLayoutData* data = _LayoutDataForItem(item))
		*dimensions = data->dimensions;
}


/**
 * @brief Returns whether a single grid cell is unoccupied.
 *
 * A cell is considered empty if it has no item pointer (including no sentinel).
 * Cells beyond the current grid bounds are treated as empty.
 *
 * @param column Zero-based column index.
 * @param row    Zero-based row index.
 * @return @c true if the cell is empty or beyond current bounds, @c false if
 *         it holds a real item or the OCCUPIED_GRID_CELL sentinel.
 *
 * @see _AreGridCellsEmpty()
 */
bool
BGridLayout::_IsGridCellEmpty(int32 column, int32 row)
{
	if (column < 0 || row < 0)
		return false;

	if (column >= fColumnCount || row >= fRowCount)
		return true;

	return (fGrid[column][row] == NULL);
}


/**
 * @brief Returns whether all cells in a rectangular region are unoccupied.
 *
 * Only checks cells that already exist within the current grid dimensions;
 * cells beyond the grid boundary are considered empty.
 *
 * @param column      Starting column (zero-based).
 * @param row         Starting row (zero-based).
 * @param columnCount Number of columns in the region.
 * @param rowCount    Number of rows in the region.
 * @return @c true if every cell in the region is empty, @c false otherwise.
 *
 * @see _IsGridCellEmpty(), AddItem()
 */
bool
BGridLayout::_AreGridCellsEmpty(int32 column, int32 row, int32 columnCount,
	int32 rowCount)
{
	if (column < 0 || row < 0)
		return false;
	int32 toColumn = min_c(column + columnCount, fColumnCount);
	int32 toRow = min_c(row + rowCount, fRowCount);

	for (int32 x = column; x < toColumn; x++) {
		for (int32 y = row; y < toRow; y++) {
			if (fGrid[x][y] != NULL)
				return false;
		}
	}

	return true;
}


/**
 * @brief Writes an item's pointer into the grid cells it occupies.
 *
 * Expands the grid if the item's dimensions exceed the current bounds.
 * The origin cell receives the actual item pointer; all other spanned cells
 * receive the @c OCCUPIED_GRID_CELL sentinel.
 *
 * @param item The item to insert; its ItemLayoutData must already be set.
 * @return @c true on success, @c false if grid expansion fails.
 *
 * @see _ResizeGrid(), AddItem()
 */
bool
BGridLayout::_InsertItemIntoGrid(BLayoutItem* item)
{
	BGridLayout::ItemLayoutData* data = _LayoutDataForItem(item);
	int32 column = data->dimensions.x;
	int32 columnCount = data->dimensions.width;
	int32 row = data->dimensions.y;
	int32 rowCount = data->dimensions.height;

	// resize the grid, if necessary
	int32 newColumnCount = max_c(fColumnCount, column + columnCount);
	int32 newRowCount = max_c(fRowCount, row + rowCount);
	if (newColumnCount > fColumnCount || newRowCount > fRowCount) {
		if (!_ResizeGrid(newColumnCount, newRowCount))
			return false;
	}

	// enter the item in the grid
	for (int32 x = 0; x < columnCount; x++) {
		for (int32 y = 0; y < rowCount; y++) {
			if (x == 0 && y == 0)
				fGrid[column + x][row + y] = item;
			else
				fGrid[column + x][row + y] = OCCUPIED_GRID_CELL;
		}
	}

	return true;
}


/**
 * @brief Reallocates the internal grid to the given column and row counts.
 *
 * Allocates a new 2-D array, copies existing cell pointers for columns and
 * rows that are retained, then swaps the new grid into place. If allocation
 * fails partway through, the existing grid is left intact.
 *
 * @param columnCount New number of columns.
 * @param rowCount    New number of rows.
 * @return @c true on success, @c false if memory allocation fails.
 *
 * @see _InsertItemIntoGrid(), ItemRemoved()
 */
bool
BGridLayout::_ResizeGrid(int32 columnCount, int32 rowCount)
{
	if (columnCount == fColumnCount && rowCount == fRowCount)
		return true;

	int32 rowsToKeep = min_c(rowCount, fRowCount);

	// allocate new grid
	BLayoutItem*** grid = new(nothrow) BLayoutItem**[columnCount];
	if (grid == NULL)
		return false;

	memset(grid, 0, sizeof(BLayoutItem**) * columnCount);

	bool success = true;
	for (int32 i = 0; i < columnCount; i++) {
		BLayoutItem** column = new(nothrow) BLayoutItem*[rowCount];
		if (!column) {
			success = false;
			break;
		}
		grid[i] = column;

		memset(column, 0, sizeof(BLayoutItem*) * rowCount);
		if (i < fColumnCount && rowsToKeep > 0)
			memcpy(column, fGrid[i], sizeof(BLayoutItem*) * rowsToKeep);
	}

	// if everything went fine, set the new grid
	if (success) {
		swap(grid, fGrid);
		swap(columnCount, fColumnCount);
		swap(rowCount, fRowCount);
	}

	// delete the old, respectively on error the partially created grid
	for (int32 i = 0; i < columnCount; i++)
		delete[] grid[i];

	delete[] grid;

	return success;
}


/**
 * @brief Returns the ItemLayoutData associated with a layout item.
 *
 * @param item The layout item to query; may be @c NULL.
 * @return A pointer to the item's ItemLayoutData, or @c NULL if @a item is @c NULL.
 *
 * @see ItemAdded(), ItemRemoved()
 */
BGridLayout::ItemLayoutData*
BGridLayout::_LayoutDataForItem(BLayoutItem* item) const
{
	if (!item)
		return NULL;
	return (ItemLayoutData*)item->LayoutData();
}


/**
 * @brief Dispatches perform codes to the base class implementation.
 *
 * @param d   The perform code identifying the operation.
 * @param arg Opaque argument passed through to the base class.
 * @return The result returned by BTwoDimensionalLayout::Perform().
 */
status_t
BGridLayout::Perform(perform_code d, void* arg)
{
	return BTwoDimensionalLayout::Perform(d, arg);
}


void BGridLayout::_ReservedGridLayout1() {}
void BGridLayout::_ReservedGridLayout2() {}
void BGridLayout::_ReservedGridLayout3() {}
void BGridLayout::_ReservedGridLayout4() {}
void BGridLayout::_ReservedGridLayout5() {}
void BGridLayout::_ReservedGridLayout6() {}
void BGridLayout::_ReservedGridLayout7() {}
void BGridLayout::_ReservedGridLayout8() {}
void BGridLayout::_ReservedGridLayout9() {}
void BGridLayout::_ReservedGridLayout10() {}
