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
 *   Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 *   All rights reserved. Distributed under the terms of the MIT License.
 */


/**
 * @file GridLayoutBuilder.cpp
 * @brief Implementation of BGridLayoutBuilder, a fluent builder for BGridLayout
 *
 * BGridLayoutBuilder wraps BGridLayout with a builder-pattern API for convenient
 * programmatic construction of grid layouts. Supports method chaining for adding
 * views at specific grid positions.
 *
 * @see BGridLayout, BGridView
 */


#include <GridLayoutBuilder.h>

#include <new>

#include <SpaceLayoutItem.h>


using std::nothrow;


/**
 * @brief Constructs a BGridLayoutBuilder backed by a new BGridView with the given spacing.
 *
 * A BGridView is allocated internally; its lifetime is tied to the view
 * hierarchy it is added to rather than to this builder object.
 *
 * @param horizontalSpacing Pixel spacing inserted between columns.
 * @param verticalSpacing   Pixel spacing inserted between rows.
 * @see BGridView, BGridLayout
 */
BGridLayoutBuilder::BGridLayoutBuilder(float horizontalSpacing,
		float verticalSpacing)
	: fLayout((new BGridView(horizontalSpacing, verticalSpacing))
					->GridLayout())
{
}

/**
 * @brief Constructs a BGridLayoutBuilder that wraps an existing BGridLayout.
 *
 * The builder does not take ownership of @a layout; the caller is responsible
 * for the layout's lifetime.
 *
 * @param layout The BGridLayout to wrap.
 */
BGridLayoutBuilder::BGridLayoutBuilder(BGridLayout* layout)
	: fLayout(layout)
{
}


/**
 * @brief Constructs a BGridLayoutBuilder that wraps the BGridLayout of an existing BGridView.
 *
 * The builder holds a pointer to the view's grid layout; the view retains
 * ownership of the layout.
 *
 * @param view The BGridView whose layout this builder will manage.
 */
BGridLayoutBuilder::BGridLayoutBuilder(BGridView* view)
	: fLayout(view->GridLayout())
{
}

/**
 * @brief Returns the BGridLayout managed by this builder.
 *
 * @return A pointer to the underlying BGridLayout.
 * @see View()
 */
BGridLayout*
BGridLayoutBuilder::GridLayout() const
{
	return fLayout;
}

/**
 * @brief Returns the BView that owns the BGridLayout managed by this builder.
 *
 * @return A pointer to the owning BView, or @c NULL if the layout has no owner.
 * @see GridLayout()
 */
BView*
BGridLayoutBuilder::View() const
{
	return fLayout->Owner();
}

/**
 * @brief Stores the underlying BGridLayout pointer into @a _layout and returns this builder.
 *
 * Useful for capturing a reference to the layout mid-chain without breaking
 * the fluent call sequence.
 *
 * @param _layout Receives a pointer to the underlying BGridLayout.
 * @return A reference to this builder for method chaining.
 * @see GridLayout()
 */
BGridLayoutBuilder&
BGridLayoutBuilder::GetGridLayout(BGridLayout** _layout)
{
	*_layout = fLayout;
	return *this;
}

/**
 * @brief Stores the owning BView pointer into @a _view and returns this builder.
 *
 * Useful for capturing a reference to the view mid-chain without breaking the
 * fluent call sequence.
 *
 * @param _view Receives a pointer to the owning BView.
 * @return A reference to this builder for method chaining.
 * @see View()
 */
BGridLayoutBuilder&
BGridLayoutBuilder::GetView(BView** _view)
{
	*_view = fLayout->Owner();
	return *this;
}

/**
 * @brief Adds a BView to the grid at the specified cell position and span.
 *
 * Delegates to BGridLayout::AddView(). The view occupies the rectangle defined
 * by (@a column, @a row) with extent (@a columnCount, @a rowCount).
 *
 * @param view        The view to add.
 * @param column      Zero-based column index of the cell's left edge.
 * @param row         Zero-based row index of the cell's top edge.
 * @param columnCount Number of columns the view spans (default 1).
 * @param rowCount    Number of rows the view spans (default 1).
 * @return A reference to this builder for method chaining.
 * @see Add(BLayoutItem*, int32, int32, int32, int32)
 */
BGridLayoutBuilder&
BGridLayoutBuilder::Add(BView* view, int32 column, int32 row,
	int32 columnCount, int32 rowCount)
{
	fLayout->AddView(view, column, row, columnCount, rowCount);
	return *this;
}

/**
 * @brief Adds a BLayoutItem to the grid at the specified cell position and span.
 *
 * Delegates to BGridLayout::AddItem(). The item occupies the rectangle defined
 * by (@a column, @a row) with extent (@a columnCount, @a rowCount).
 *
 * @param item        The layout item to add.
 * @param column      Zero-based column index of the cell's left edge.
 * @param row         Zero-based row index of the cell's top edge.
 * @param columnCount Number of columns the item spans (default 1).
 * @param rowCount    Number of rows the item spans (default 1).
 * @return A reference to this builder for method chaining.
 * @see Add(BView*, int32, int32, int32, int32)
 */
BGridLayoutBuilder&
BGridLayoutBuilder::Add(BLayoutItem* item, int32 column, int32 row,
	int32 columnCount, int32 rowCount)
{
	fLayout->AddItem(item, column, row, columnCount, rowCount);
	return *this;
}

/**
 * @brief Sets the relative weight of a column for distributing extra horizontal space.
 *
 * Columns with higher weight receive a proportionally larger share of any
 * surplus horizontal space. A weight of @c 0.0 means the column is not
 * stretched.
 *
 * @param column Zero-based index of the column to configure.
 * @param weight The weight value (>= 0.0).
 * @return A reference to this builder for method chaining.
 * @see SetRowWeight()
 */
BGridLayoutBuilder&
BGridLayoutBuilder::SetColumnWeight(int32 column, float weight)
{
	fLayout->SetColumnWeight(column, weight);
	return *this;
}

/**
 * @brief Sets the relative weight of a row for distributing extra vertical space.
 *
 * Rows with higher weight receive a proportionally larger share of any surplus
 * vertical space. A weight of @c 0.0 means the row is not stretched.
 *
 * @param row    Zero-based index of the row to configure.
 * @param weight The weight value (>= 0.0).
 * @return A reference to this builder for method chaining.
 * @see SetColumnWeight()
 */
BGridLayoutBuilder&
BGridLayoutBuilder::SetRowWeight(int32 row, float weight)
{
	fLayout->SetRowWeight(row, weight);
	return *this;
}

/**
 * @brief Sets the insets (padding) around the grid's content area.
 *
 * The insets are applied inside the owning view's bounds, reducing the space
 * available to the grid cells.
 *
 * @param left   Pixels of padding on the left edge.
 * @param top    Pixels of padding on the top edge.
 * @param right  Pixels of padding on the right edge.
 * @param bottom Pixels of padding on the bottom edge.
 * @return A reference to this builder for method chaining.
 */
BGridLayoutBuilder&
BGridLayoutBuilder::SetInsets(float left, float top, float right, float bottom)
{
	fLayout->SetInsets(left, top, right, bottom);
	return *this;
}

/**
 * @brief Implicit conversion operator to BGridLayout*.
 *
 * Allows a BGridLayoutBuilder to be passed directly wherever a BGridLayout
 * pointer is expected, without an explicit call to GridLayout().
 *
 * @return A pointer to the underlying BGridLayout.
 */
BGridLayoutBuilder::operator BGridLayout*()
{
	return fLayout;
}
