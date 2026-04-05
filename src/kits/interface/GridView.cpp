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
 *   Copyright 2010 Haiku, Inc. All rights reserved.
 *   Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file GridView.cpp
 * @brief Implementation of BGridView, a convenience view backed by a BGridLayout
 *
 * BGridView pairs a BView with a BGridLayout so that child views are automatically
 * arranged in a grid. Eliminates boilerplate when building grid-based UIs.
 *
 * @see BGridLayout, BGridLayoutBuilder
 */


#include <GridView.h>


/**
 * @brief Construct an anonymous BGridView with configurable cell spacing.
 *
 * Creates a view with no name and installs a freshly allocated BGridLayout
 * using the given spacing values. System colours are adopted so the view
 * blends with the current desktop theme.
 *
 * @param horizontalSpacing Horizontal gap (in pixels) between grid columns.
 * @param verticalSpacing   Vertical gap (in pixels) between grid rows.
 * @see BGridLayout, BView::AdoptSystemColors()
 */
BGridView::BGridView(float horizontalSpacing, float verticalSpacing)
	:
	BView(NULL, 0, new BGridLayout(horizontalSpacing, verticalSpacing))
{
	AdoptSystemColors();
}


/**
 * @brief Construct a named BGridView with configurable cell spacing.
 *
 * @param name              The view name passed to BView; may be NULL.
 * @param horizontalSpacing Horizontal gap (in pixels) between grid columns.
 * @param verticalSpacing   Vertical gap (in pixels) between grid rows.
 * @see BGridLayout, BView::AdoptSystemColors()
 */
BGridView::BGridView(const char* name, float horizontalSpacing,
	float verticalSpacing)
	:
	BView(name, 0, new BGridLayout(horizontalSpacing, verticalSpacing))
{
	AdoptSystemColors();
}


/**
 * @brief Reconstruct a BGridView from an archive message.
 *
 * This is the unarchiving constructor invoked by Instantiate(). The BView
 * base class restores its own state (including the embedded BGridLayout)
 * from \a from.
 *
 * @param from The archive message previously produced by Archive().
 * @see Instantiate(), Archive()
 */
BGridView::BGridView(BMessage* from)
	:
	BView(from)
{
}


/**
 * @brief Destroy the BGridView.
 *
 * The owned BGridLayout is deleted by the BView base class destructor;
 * no additional cleanup is required here.
 */
BGridView::~BGridView()
{
}


/**
 * @brief Set the layout of this view, accepting only BGridLayout instances.
 *
 * Overrides BView::SetLayout() to enforce the invariant that a BGridView
 * always uses a BGridLayout. If \a layout is not a BGridLayout the call
 * is silently ignored.
 *
 * @param layout The new layout; must be a BGridLayout or a subclass thereof.
 */
void
BGridView::SetLayout(BLayout* layout)
{
	// only BGridLayouts are allowed
	if (!dynamic_cast<BGridLayout*>(layout))
		return;

	BView::SetLayout(layout);
}


/**
 * @brief Return the BGridLayout that manages this view's children.
 *
 * @return A pointer to the BGridLayout, or NULL if GetLayout() does not
 *         return a BGridLayout (should not happen for a properly constructed
 *         BGridView).
 */
BGridLayout*
BGridView::GridLayout() const
{
	return dynamic_cast<BGridLayout*>(GetLayout());
}


/**
 * @brief Create a new BGridView from an archive message.
 *
 * @param from The archive message to instantiate from.
 * @return A newly allocated BGridView on success, or NULL if \a from is
 *         not a valid BGridView archive.
 * @see Archive()
 */
BArchivable*
BGridView::Instantiate(BMessage* from)
{
	if (validate_instantiation(from, "BGridView"))
		return new BGridView(from);
	return NULL;
}


/**
 * @brief Hook for future binary-compatible extensions (FBC).
 *
 * Forwards to BView::Perform() unchanged. Subclasses should not override
 * this method.
 *
 * @param code  Perform code identifying the virtual method to call.
 * @param _data Opaque argument whose meaning depends on \a code.
 * @return The result from BView::Perform().
 */
status_t
BGridView::Perform(perform_code code, void* _data)
{
	return BView::Perform(code, _data);
}


void BGridView::_ReservedGridView1() {}
void BGridView::_ReservedGridView2() {}
void BGridView::_ReservedGridView3() {}
void BGridView::_ReservedGridView4() {}
void BGridView::_ReservedGridView5() {}
void BGridView::_ReservedGridView6() {}
void BGridView::_ReservedGridView7() {}
void BGridView::_ReservedGridView8() {}
void BGridView::_ReservedGridView9() {}
void BGridView::_ReservedGridView10() {}
