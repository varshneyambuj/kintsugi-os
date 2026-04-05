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
 * @file GroupLayoutBuilder.cpp
 * @brief Implementation of BGroupLayoutBuilder, a fluent builder for BGroupLayout
 *
 * BGroupLayoutBuilder wraps BGroupLayout with a builder-pattern API for convenient
 * programmatic construction of linear layouts. Supports method chaining and nested
 * group builders.
 *
 * @see BGroupLayout, BGroupView
 */


#include <GroupLayoutBuilder.h>

#include <new>

#include <SpaceLayoutItem.h>


using std::nothrow;


/**
 * @brief Constructs a BGroupLayoutBuilder backed by a new BGroupView.
 *
 * A BGroupView is allocated internally with the given @a orientation and
 * @a spacing. Its group layout is pushed onto the builder's internal layout
 * stack and becomes the initial "top" layout.
 *
 * @param orientation Axis along which children are arranged (@c B_HORIZONTAL
 *                    or @c B_VERTICAL).
 * @param spacing     Pixel spacing between consecutive children.
 * @see BGroupView, BGroupLayout
 */
BGroupLayoutBuilder::BGroupLayoutBuilder(orientation orientation,
	float spacing)
	: fRootLayout((new BGroupView(orientation, spacing))->GroupLayout())
{
	_PushLayout(fRootLayout);
}

/**
 * @brief Constructs a BGroupLayoutBuilder that wraps an existing BGroupLayout.
 *
 * The supplied layout is pushed onto the layout stack as the initial top
 * layout. The builder does not take ownership of @a layout.
 *
 * @param layout The BGroupLayout to wrap.
 */
BGroupLayoutBuilder::BGroupLayoutBuilder(BGroupLayout* layout)
	: fRootLayout(layout)
{
	_PushLayout(fRootLayout);
}

/**
 * @brief Constructs a BGroupLayoutBuilder that wraps the BGroupLayout of an existing BGroupView.
 *
 * The view's group layout is pushed onto the layout stack as the initial top
 * layout. The view retains ownership of the layout.
 *
 * @param view The BGroupView whose layout this builder will manage.
 */
BGroupLayoutBuilder::BGroupLayoutBuilder(BGroupView* view)
	: fRootLayout(view->GroupLayout())
{
	_PushLayout(fRootLayout);
}

/**
 * @brief Returns the root BGroupLayout — the layout created or supplied at construction time.
 *
 * The root layout is always the bottom of the layout stack and is unaffected
 * by AddGroup()/End() calls.
 *
 * @return A pointer to the root BGroupLayout.
 * @see TopLayout()
 */
BGroupLayout*
BGroupLayoutBuilder::RootLayout() const
{
	return fRootLayout;
}

/**
 * @brief Returns the BGroupLayout at the top of the current layout stack.
 *
 * Initially this is the root layout. After each call to AddGroup() it is the
 * most recently pushed nested layout; after each call to End() it is the
 * layout one level above.
 *
 * @return A pointer to the current top BGroupLayout, or @c NULL if the stack
 *         is empty.
 * @see RootLayout(), AddGroup(), End()
 */
BGroupLayout*
BGroupLayoutBuilder::TopLayout() const
{
	int32 count = fLayoutStack.CountItems();
	return (count > 0
		? (BGroupLayout*)fLayoutStack.ItemAt(count - 1) : NULL);
}

/**
 * @brief Stores the current top BGroupLayout into @a _layout and returns this builder.
 *
 * Useful for capturing a reference to the top layout mid-chain without
 * breaking the fluent call sequence.
 *
 * @param _layout Receives a pointer to the current top BGroupLayout.
 * @return A reference to this builder for method chaining.
 * @see TopLayout()
 */
BGroupLayoutBuilder&
BGroupLayoutBuilder::GetTopLayout(BGroupLayout** _layout)
{
	*_layout = TopLayout();
	return *this;
}

/**
 * @brief Returns the BView that owns the current top BGroupLayout.
 *
 * @return A pointer to the owning BView, or @c NULL if the stack is empty or
 *         the top layout has no owner.
 * @see TopLayout()
 */
BView*
BGroupLayoutBuilder::TopView() const
{
	if (BGroupLayout* layout = TopLayout())
		return layout->Owner();
	return NULL;
}

/**
 * @brief Stores the owning BView of the current top layout into @a _view and returns this builder.
 *
 * Useful for capturing a reference to the view mid-chain without breaking the
 * fluent call sequence. Writes @c NULL into @a _view if the stack is empty.
 *
 * @param _view Receives a pointer to the owning BView.
 * @return A reference to this builder for method chaining.
 * @see TopView()
 */
BGroupLayoutBuilder&
BGroupLayoutBuilder::GetTopView(BView** _view)
{
	if (BGroupLayout* layout = TopLayout())
		*_view = layout->Owner();
	else
		*_view = NULL;

	return *this;
}

/**
 * @brief Adds a BView to the current top layout with the default weight.
 *
 * Delegates to BGroupLayout::AddView(view). Has no effect if the layout stack
 * is empty.
 *
 * @param view The view to add.
 * @return A reference to this builder for method chaining.
 * @see Add(BView*, float), Add(BLayoutItem*)
 */
BGroupLayoutBuilder&
BGroupLayoutBuilder::Add(BView* view)
{
	if (BGroupLayout* layout = TopLayout())
		layout->AddView(view);
	return *this;
}

/**
 * @brief Adds a BView to the current top layout with an explicit weight.
 *
 * The @a weight controls how surplus space is distributed among children that
 * share it. Has no effect if the layout stack is empty.
 *
 * @param view   The view to add.
 * @param weight The relative weight for space distribution (>= 0.0).
 * @return A reference to this builder for method chaining.
 * @see Add(BView*), Add(BLayoutItem*, float)
 */
BGroupLayoutBuilder&
BGroupLayoutBuilder::Add(BView* view, float weight)
{
	if (BGroupLayout* layout = TopLayout())
		layout->AddView(view, weight);
	return *this;
}

/**
 * @brief Adds a BLayoutItem to the current top layout with the default weight.
 *
 * Delegates to BGroupLayout::AddItem(item). Has no effect if the layout stack
 * is empty.
 *
 * @param item The layout item to add.
 * @return A reference to this builder for method chaining.
 * @see Add(BLayoutItem*, float), Add(BView*)
 */
BGroupLayoutBuilder&
BGroupLayoutBuilder::Add(BLayoutItem* item)
{
	if (BGroupLayout* layout = TopLayout())
		layout->AddItem(item);
	return *this;
}

/**
 * @brief Adds a BLayoutItem to the current top layout with an explicit weight.
 *
 * The @a weight controls how surplus space is distributed among children that
 * share it. Has no effect if the layout stack is empty.
 *
 * @param item   The layout item to add.
 * @param weight The relative weight for space distribution (>= 0.0).
 * @return A reference to this builder for method chaining.
 * @see Add(BLayoutItem*), Add(BView*, float)
 */
BGroupLayoutBuilder&
BGroupLayoutBuilder::Add(BLayoutItem* item, float weight)
{
	if (BGroupLayout* layout = TopLayout())
		layout->AddItem(item, weight);
	return *this;
}

/**
 * @brief Adds a nested BGroupView to the current top layout and pushes it onto the stack.
 *
 * Creates a new BGroupView with the given @a orientation and @a spacing, adds
 * it to the current top layout with @a weight, and makes its group layout the
 * new top of the stack. Subsequent Add() calls target the nested group until
 * End() is called.
 *
 * Has no effect if the current top layout is @c NULL or if the allocation fails.
 *
 * @param orientation Axis for the nested group (@c B_HORIZONTAL or @c B_VERTICAL).
 * @param spacing     Pixel spacing between children of the nested group.
 * @param weight      Weight of the nested group within the parent layout.
 * @return A reference to this builder for method chaining.
 * @see End()
 */
BGroupLayoutBuilder&
BGroupLayoutBuilder::AddGroup(orientation orientation, float spacing,
	float weight)
{
	if (BGroupLayout* layout = TopLayout()) {
		BGroupView* group = new(nothrow) BGroupView(orientation, spacing);
		if (group) {
			if (layout->AddView(group, weight))
				_PushLayout(group->GroupLayout());
			else
				delete group;
		}
	}

	return *this;
}

/**
 * @brief Ends the current nested group and pops the layout stack.
 *
 * After this call, subsequent Add() calls target the layout that was on top
 * before the matching AddGroup() call. Has no effect if only the root layout
 * is on the stack.
 *
 * @return A reference to this builder for method chaining.
 * @see AddGroup()
 */
BGroupLayoutBuilder&
BGroupLayoutBuilder::End()
{
	_PopLayout();
	return *this;
}

/**
 * @brief Adds an invisible, flexible glue item to the current top layout.
 *
 * Glue items expand to consume surplus space in the layout's primary axis.
 * The @a weight controls how multiple glue items share any remaining space.
 * Has no effect if the layout stack is empty.
 *
 * @param weight The relative weight for this glue item (>= 0.0).
 * @return A reference to this builder for method chaining.
 * @see AddStrut()
 */
BGroupLayoutBuilder&
BGroupLayoutBuilder::AddGlue(float weight)
{
	if (BGroupLayout* layout = TopLayout())
		layout->AddItem(BSpaceLayoutItem::CreateGlue(), weight);

	return *this;
}

/**
 * @brief Adds a fixed-size strut to the current top layout.
 *
 * A strut is a rigid invisible spacer. Its orientation (horizontal or vertical)
 * is determined by the top layout's current orientation. Has no effect if the
 * layout stack is empty.
 *
 * @param size The fixed pixel size of the strut along the layout's primary axis.
 * @return A reference to this builder for method chaining.
 * @see AddGlue()
 */
BGroupLayoutBuilder&
BGroupLayoutBuilder::AddStrut(float size)
{
	if (BGroupLayout* layout = TopLayout()) {
		if (layout->Orientation() == B_HORIZONTAL)
			layout->AddItem(BSpaceLayoutItem::CreateHorizontalStrut(size));
		else
			layout->AddItem(BSpaceLayoutItem::CreateVerticalStrut(size));
	}

	return *this;
}

/**
 * @brief Sets the insets (padding) around the current top layout's content area.
 *
 * The insets are applied inside the owning view's bounds, reducing the space
 * available to the group's children. Has no effect if the layout stack is empty.
 *
 * @param left   Pixels of padding on the left edge.
 * @param top    Pixels of padding on the top edge.
 * @param right  Pixels of padding on the right edge.
 * @param bottom Pixels of padding on the bottom edge.
 * @return A reference to this builder for method chaining.
 */
BGroupLayoutBuilder&
BGroupLayoutBuilder::SetInsets(float left, float top, float right, float bottom)
{
	if (BGroupLayout* layout = TopLayout())
		layout->SetInsets(left, top, right, bottom);

	return *this;
}

/**
 * @brief Implicit conversion operator to BGroupLayout*.
 *
 * Returns the root BGroupLayout, allowing the builder to be passed directly
 * wherever a BGroupLayout pointer is expected.
 *
 * @return A pointer to the root BGroupLayout.
 * @see RootLayout()
 */
BGroupLayoutBuilder::operator BGroupLayout*()
{
	return fRootLayout;
}

/**
 * @brief Pushes @a layout onto the internal layout stack, making it the new top.
 *
 * @param layout The BGroupLayout to push.
 * @return @c true if the push succeeded, @c false on allocation failure.
 * @see _PopLayout()
 */
bool
BGroupLayoutBuilder::_PushLayout(BGroupLayout* layout)
{
	return fLayoutStack.AddItem(layout);
}

/**
 * @brief Pops the topmost entry from the internal layout stack.
 *
 * Has no effect if the stack is empty.
 *
 * @see _PushLayout()
 */
void
BGroupLayoutBuilder::_PopLayout()
{
	int32 count = fLayoutStack.CountItems();
	if (count > 0)
		fLayoutStack.RemoveItem(count - 1);
}
