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
 *   Copyright 2006 Ingo Weinhold. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold <bonefish@cs.tu-berlin.de>
 */


/**
 * @file SplitLayoutBuilder.cpp
 * @brief Implementation of BSplitLayoutBuilder, a fluent builder for BSplitLayout
 *
 * BSplitLayoutBuilder wraps BSplitLayout with a builder-pattern API for
 * convenient programmatic construction of split layouts. Supports method
 * chaining and setting item weights.
 *
 * @see BSplitLayout, BSplitView
 */


#include <SplitLayoutBuilder.h>

#include <new>


using std::nothrow;


/**
 * @brief Constructs a BSplitLayoutBuilder backed by a new BSplitView.
 *
 * A BSplitView is allocated internally with the given @a orientation and
 * @a spacing. The builder holds a non-owning pointer to this view; the view's
 * lifetime is managed by the view hierarchy.
 *
 * @param orientation Axis along which panes are split (@c B_HORIZONTAL or
 *                    @c B_VERTICAL).
 * @param spacing     Pixel spacing of the splitter divider between panes.
 * @see BSplitView, BSplitLayout
 */
BSplitLayoutBuilder::BSplitLayoutBuilder(orientation orientation,
		float spacing)
	: fView(new BSplitView(orientation, spacing))
{
}

/**
 * @brief Constructs a BSplitLayoutBuilder that wraps an existing BSplitView.
 *
 * The builder holds a non-owning pointer to @a view; the caller is responsible
 * for the view's lifetime.
 *
 * @param view The BSplitView to wrap.
 */
BSplitLayoutBuilder::BSplitLayoutBuilder(BSplitView* view)
	: fView(view)
{
}

/**
 * @brief Returns the BSplitView managed by this builder.
 *
 * @return A pointer to the underlying BSplitView.
 */
BSplitView*
BSplitLayoutBuilder::SplitView() const
{
	return fView;
}

/**
 * @brief Stores the underlying BSplitView pointer into @a view and returns this builder.
 *
 * Useful for capturing a reference to the split view mid-chain without
 * breaking the fluent call sequence.
 *
 * @param view Receives a pointer to the underlying BSplitView.
 * @return A reference to this builder for method chaining.
 * @see SplitView()
 */
BSplitLayoutBuilder&
BSplitLayoutBuilder::GetSplitView(BSplitView** view)
{
	*view = fView;
	return *this;
}

/**
 * @brief Adds a BView as a new pane with the default weight.
 *
 * Delegates to BSplitView::AddChild(view). The pane receives equal share of
 * any surplus space relative to other default-weight panes.
 *
 * @param view The view to add as a split pane.
 * @return A reference to this builder for method chaining.
 * @see Add(BView*, float), Add(BLayoutItem*)
 */
BSplitLayoutBuilder&
BSplitLayoutBuilder::Add(BView* view)
{
	fView->AddChild(view);
	return *this;
}

/**
 * @brief Adds a BView as a new pane with an explicit weight.
 *
 * The @a weight controls how surplus space is distributed among panes.
 * A pane with weight 2.0 receives twice as much extra space as one with 1.0.
 *
 * @param view   The view to add as a split pane.
 * @param weight The relative weight for space distribution (>= 0.0).
 * @return A reference to this builder for method chaining.
 * @see Add(BView*), Add(BLayoutItem*, float)
 */
BSplitLayoutBuilder&
BSplitLayoutBuilder::Add(BView* view, float weight)
{
	fView->AddChild(view, weight);
	return *this;
}

/**
 * @brief Adds a BLayoutItem as a new pane with the default weight.
 *
 * Delegates to BSplitView::AddChild(item). Useful when the pane content is
 * represented by a layout item rather than a raw view.
 *
 * @param item The layout item to add as a split pane.
 * @return A reference to this builder for method chaining.
 * @see Add(BLayoutItem*, float), Add(BView*)
 */
BSplitLayoutBuilder&
BSplitLayoutBuilder::Add(BLayoutItem* item)
{
	fView->AddChild(item);
	return *this;
}

/**
 * @brief Adds a BLayoutItem as a new pane with an explicit weight.
 *
 * The @a weight controls how surplus space is distributed among panes.
 *
 * @param item   The layout item to add as a split pane.
 * @param weight The relative weight for space distribution (>= 0.0).
 * @return A reference to this builder for method chaining.
 * @see Add(BLayoutItem*), Add(BView*, float)
 */
BSplitLayoutBuilder&
BSplitLayoutBuilder::Add(BLayoutItem* item, float weight)
{
	fView->AddChild(item, weight);
	return *this;
}

/**
 * @brief Sets whether the most recently added pane can be collapsed to zero size.
 *
 * When @a collapsible is @c true the user may drag the adjacent splitter all
 * the way to the pane's edge, hiding it completely. Applies to the last pane
 * that was added (i.e. the pane at index CountChildren() - 1).
 *
 * Has no effect if no children have been added yet.
 *
 * @param collapsible @c true to allow the pane to collapse, @c false to prevent it.
 * @return A reference to this builder for method chaining.
 */
BSplitLayoutBuilder&
BSplitLayoutBuilder::SetCollapsible(bool collapsible)
{
	int32 count = fView->CountChildren();
	if (count > 0)
		fView->SetCollapsible(count - 1, collapsible);
	return *this;
}

/**
 * @brief Sets the insets (padding) around the split view's content area.
 *
 * The insets are applied inside the view's bounds before any panes or splitters
 * are placed, reducing the total space available to the layout.
 *
 * @param left   Pixels of padding on the left edge.
 * @param top    Pixels of padding on the top edge.
 * @param right  Pixels of padding on the right edge.
 * @param bottom Pixels of padding on the bottom edge.
 * @return A reference to this builder for method chaining.
 */
BSplitLayoutBuilder&
BSplitLayoutBuilder::SetInsets(float left, float top, float right, float bottom)
{
	fView->SetInsets(left, top, right, bottom);

	return *this;
}

/**
 * @brief Implicit conversion operator to BSplitView*.
 *
 * Allows a BSplitLayoutBuilder to be passed directly wherever a BSplitView
 * pointer is expected, without an explicit call to SplitView().
 *
 * @return A pointer to the underlying BSplitView.
 */
BSplitLayoutBuilder::operator BSplitView*()
{
	return fView;
}
