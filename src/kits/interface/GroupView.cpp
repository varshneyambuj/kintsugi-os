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
 *   Copyright 2010-2015 Haiku, Inc. All rights reserved.
 *   Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file GroupView.cpp
 * @brief Implementation of BGroupView, a convenience view backed by a BGroupLayout
 *
 * BGroupView pairs a BView with a BGroupLayout so that child views are automatically
 * arranged linearly. Eliminates boilerplate when building group-based UIs.
 *
 * @see BGroupLayout, BGroupLayoutBuilder
 */


#include <GroupView.h>


/**
 * @brief Construct an anonymous BGroupView with the given orientation and spacing.
 *
 * Creates a view with no name and installs a freshly allocated BGroupLayout
 * configured with \a orientation and \a spacing. System colours are adopted
 * so the view blends with the current desktop theme.
 *
 * @param orientation @c B_HORIZONTAL or @c B_VERTICAL arrangement of children.
 * @param spacing     Gap (in pixels) between adjacent children.
 * @see BGroupLayout, BView::AdoptSystemColors()
 */
BGroupView::BGroupView(orientation orientation, float spacing)
	:
	BView(NULL, 0, new BGroupLayout(orientation, spacing))
{
	AdoptSystemColors();
}


/**
 * @brief Construct a named BGroupView with the given orientation and spacing.
 *
 * @param name        The view name passed to BView; may be NULL.
 * @param orientation @c B_HORIZONTAL or @c B_VERTICAL arrangement of children.
 * @param spacing     Gap (in pixels) between adjacent children.
 * @see BGroupLayout, BView::AdoptSystemColors()
 */
BGroupView::BGroupView(const char* name, orientation orientation,
	float spacing)
	:
	BView(name, 0, new BGroupLayout(orientation, spacing))
{
	AdoptSystemColors();
}


/**
 * @brief Reconstruct a BGroupView from an archive message.
 *
 * This is the unarchiving constructor invoked by Instantiate(). The BView
 * base class restores its own state (including the embedded BGroupLayout)
 * from \a from; system colours are then adopted to match the current theme.
 *
 * @param from The archive message previously produced by Archive().
 * @see Instantiate(), Archive()
 */
BGroupView::BGroupView(BMessage* from)
	:
	BView(from)
{
	AdoptSystemColors();
}


/**
 * @brief Destroy the BGroupView.
 *
 * The owned BGroupLayout is deleted by the BView base class destructor;
 * no additional cleanup is required here.
 */
BGroupView::~BGroupView()
{
}


/**
 * @brief Set the layout of this view, accepting only BGroupLayout instances.
 *
 * Overrides BView::SetLayout() to enforce the invariant that a BGroupView
 * always uses a BGroupLayout. If \a layout is not a BGroupLayout the call
 * is silently ignored.
 *
 * @param layout The new layout; must be a BGroupLayout or a subclass thereof.
 */
void
BGroupView::SetLayout(BLayout* layout)
{
	// only BGroupLayouts are allowed
	if (!dynamic_cast<BGroupLayout*>(layout))
		return;

	BView::SetLayout(layout);
}


/**
 * @brief Create a new BGroupView from an archive message.
 *
 * @param from The archive message to instantiate from.
 * @return A newly allocated BGroupView on success, or NULL if \a from is
 *         not a valid BGroupView archive.
 * @see Archive()
 */
BArchivable*
BGroupView::Instantiate(BMessage* from)
{
	if (validate_instantiation(from, "BGroupView"))
		return new BGroupView(from);

	return NULL;
}


/**
 * @brief Return the BGroupLayout that manages this view's children.
 *
 * @return A pointer to the BGroupLayout, or NULL if GetLayout() does not
 *         return a BGroupLayout (should not happen for a properly constructed
 *         BGroupView).
 */
BGroupLayout*
BGroupView::GroupLayout() const
{
	return dynamic_cast<BGroupLayout*>(GetLayout());
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
BGroupView::Perform(perform_code code, void* _data)
{
	return BView::Perform(code, _data);
}


void BGroupView::_ReservedGroupView1() {}
void BGroupView::_ReservedGroupView2() {}
void BGroupView::_ReservedGroupView3() {}
void BGroupView::_ReservedGroupView4() {}
void BGroupView::_ReservedGroupView5() {}
void BGroupView::_ReservedGroupView6() {}
void BGroupView::_ReservedGroupView7() {}
void BGroupView::_ReservedGroupView8() {}
void BGroupView::_ReservedGroupView9() {}
void BGroupView::_ReservedGroupView10() {}
