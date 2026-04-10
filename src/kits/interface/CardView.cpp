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
 *   Copyright 2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file CardView.cpp
 * @brief Implementation of BCardView, a convenience view backed by a BCardLayout
 *
 * BCardView wraps BCardLayout in a ready-to-use BView, eliminating boilerplate
 * when building card-based UIs.
 *
 * @see BCardLayout, BView
 */


#include <CardLayout.h>
#include <CardView.h>


/**
 * @brief Construct an anonymous BCardView with a default BCardLayout.
 *
 * Creates a view with no name and installs a freshly allocated
 * BCardLayout as its layout manager. System colours are adopted so
 * the view blends with the desktop theme.
 *
 * @see BCardLayout, BView::AdoptSystemColors()
 */
BCardView::BCardView()
	:
	BView(NULL, 0, new BCardLayout())
{
	AdoptSystemColors();
}


/**
 * @brief Construct a named BCardView with a default BCardLayout.
 *
 * @param name The view name passed to BView; may be NULL.
 * @see BCardLayout, BView::AdoptSystemColors()
 */
BCardView::BCardView(const char* name)
	:
	BView(name, 0, new BCardLayout())
{
	AdoptSystemColors();
}


/**
 * @brief Reconstruct a BCardView from an archive message.
 *
 * This is the unarchiving constructor invoked by Instantiate(). The
 * BView base class restores its own state (including the embedded
 * BCardLayout) from \a from; system colours are then adopted to
 * match the current theme.
 *
 * @param from The archive message previously produced by Archive().
 * @see Instantiate(), Archive()
 */
BCardView::BCardView(BMessage* from)
	:
	BView(from)
{
	AdoptSystemColors();
}


/**
 * @brief Destroy the BCardView.
 *
 * The owned BCardLayout is deleted by the BView base class destructor;
 * no additional cleanup is required here.
 */
BCardView::~BCardView()
{
}


/**
 * @brief Set the layout of this view, accepting only BCardLayout instances.
 *
 * Overrides BView::SetLayout() to enforce the invariant that a BCardView
 * always uses a BCardLayout. If \a layout is not a BCardLayout the call
 * is silently ignored.
 *
 * @param layout The new layout; must be a BCardLayout or a subclass thereof.
 */
void
BCardView::SetLayout(BLayout* layout)
{
	if (dynamic_cast<BCardLayout*>(layout) == NULL)
		return;

	BView::SetLayout(layout);
}


/**
 * @brief Return the BCardLayout that manages this view's children.
 *
 * @return A pointer to the BCardLayout; never NULL for a properly
 *         constructed BCardView.
 */
BCardLayout*
BCardView::CardLayout() const
{
	return static_cast<BCardLayout*>(GetLayout());
}


/**
 * @brief Create a new BCardView from an archive message.
 *
 * @param from The archive message to instantiate from.
 * @return A newly allocated BCardView on success, or NULL if \a from is
 *         not a valid BCardView archive.
 * @see Archive()
 */
BArchivable*
BCardView::Instantiate(BMessage* from)
{
	if (validate_instantiation(from, "BCardView"))
		return new BCardView(from);
	return NULL;
}


/**
 * @brief Hook for future binary-compatible extensions (FBC).
 *
 * Forwards to BView::Perform() unchanged. Subclasses should not override
 * this method.
 *
 * @param d   Perform code identifying the virtual method to call.
 * @param arg Opaque argument whose meaning depends on \a d.
 * @return The result from BView::Perform().
 */
status_t
BCardView::Perform(perform_code d, void* arg)
{
	return BView::Perform(d, arg);
}


void BCardView::_ReservedCardView1() {}
void BCardView::_ReservedCardView2() {}
void BCardView::_ReservedCardView3() {}
void BCardView::_ReservedCardView4() {}
void BCardView::_ReservedCardView5() {}
void BCardView::_ReservedCardView6() {}
void BCardView::_ReservedCardView7() {}
void BCardView::_ReservedCardView8() {}
void BCardView::_ReservedCardView9() {}
void BCardView::_ReservedCardView10() {}
