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
 *   Copyright 2010 Haiku, Inc. / Copyright 2006 Ingo Weinhold. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold <bonefish@cs.tu-berlin.de>
 */


/**
 * @file ViewLayoutItem.cpp
 * @brief Implementation of BViewLayoutItem, a layout item that wraps a BView
 *
 * BViewLayoutItem bridges the layout system and BView: it reads the view's
 * explicit min/max/preferred sizes and alignment, and applies layout-computed
 * frames back to the view via ResizeTo/MoveTo.
 *
 * @see BLayoutItem, BView, BAbstractLayoutItem
 */


#include "ViewLayoutItem.h"

#include <new>

#include <Layout.h>
#include <View.h>
#include <ViewPrivate.h>


namespace {
	/** @brief BMessage field name used to store the wrapped view reference during archiving. */
	const char* const kViewField = "BViewLayoutItem:view";
}


/**
 * @brief Construct a BViewLayoutItem that wraps the given view.
 *
 * @param view  The BView to wrap. The item does not take ownership of the view.
 * @see   BLayoutItem
 */
BViewLayoutItem::BViewLayoutItem(BView* view)
	:
	fView(view),
	fAncestorsVisible(true)
{
}


/**
 * @brief Unarchiving constructor; restores a BViewLayoutItem from a BMessage.
 *
 * Locates the previously archived BView via BUnarchiver using the field
 * name kViewField. The item does not assume ownership of the view.
 *
 * @param from  The archive message produced by Archive().
 * @see   Instantiate(), Archive()
 */
BViewLayoutItem::BViewLayoutItem(BMessage* from)
	:
	BLayoutItem(BUnarchiver::PrepareArchive(from)),
	fView(NULL),
	fAncestorsVisible(true)
{
	BUnarchiver unarchiver(from);
	unarchiver.Finish(unarchiver.FindObject<BView>(kViewField, 0,
		BUnarchiver::B_DONT_ASSUME_OWNERSHIP, fView));
}


/**
 * @brief Destroy the BViewLayoutItem.
 *
 * The wrapped view is not deleted; the view's owner (typically its parent
 * view or the layout itself) is responsible for its lifetime.
 */
BViewLayoutItem::~BViewLayoutItem()
{
}


/**
 * @brief Return the minimum size of the wrapped view.
 *
 * Delegates directly to BView::MinSize().
 *
 * @return The minimum BSize reported by the wrapped view.
 */
BSize
BViewLayoutItem::MinSize()
{
	return fView->MinSize();
}


/**
 * @brief Return the maximum size of the wrapped view.
 *
 * Delegates directly to BView::MaxSize().
 *
 * @return The maximum BSize reported by the wrapped view.
 */
BSize
BViewLayoutItem::MaxSize()
{
	return fView->MaxSize();
}


/**
 * @brief Return the preferred size of the wrapped view.
 *
 * Delegates directly to BView::PreferredSize().
 *
 * @return The preferred BSize reported by the wrapped view.
 */
BSize
BViewLayoutItem::PreferredSize()
{
	return fView->PreferredSize();
}


/**
 * @brief Return the layout alignment of the wrapped view.
 *
 * Delegates to BView::LayoutAlignment() so that the view's explicit alignment
 * setting is reflected in the layout computation.
 *
 * @return The BAlignment of the wrapped view.
 */
BAlignment
BViewLayoutItem::Alignment()
{
	return fView->LayoutAlignment();
}


void
BViewLayoutItem::SetExplicitMinSize(BSize size)
{
	fView->SetExplicitMinSize(size);
}


void
BViewLayoutItem::SetExplicitMaxSize(BSize size)
{
	fView->SetExplicitMaxSize(size);
}


void
BViewLayoutItem::SetExplicitPreferredSize(BSize size)
{
	fView->SetExplicitPreferredSize(size);
}


void
BViewLayoutItem::SetExplicitAlignment(BAlignment alignment)
{
	fView->SetExplicitAlignment(alignment);
}


/**
 * @brief Return whether the wrapped view is currently visible.
 *
 * Computes visibility by comparing the view's internal show-level with the
 * ancestor visibility flag. The view is considered visible when its effective
 * show-level is zero or negative.
 *
 * @return true if the view is visible, false if it is hidden.
 * @see   SetVisible(), AncestorVisibilityChanged()
 */
bool
BViewLayoutItem::IsVisible()
{
	int16 showLevel = BView::Private(fView).ShowLevel();
	return showLevel - (fAncestorsVisible ? 0 : 1) <= 0;
}


/**
 * @brief Show or hide the wrapped view.
 *
 * Calls BView::Show() or BView::Hide() only when the requested visibility
 * differs from the current state, avoiding redundant show/hide cycles.
 *
 * @param visible  true to show the view, false to hide it.
 * @see   IsVisible()
 */
void
BViewLayoutItem::SetVisible(bool visible)
{
	if (visible != IsVisible()) {
		if (visible)
			fView->Show();
		else
			fView->Hide();
	}
}


/**
 * @brief Return the current frame of the wrapped view.
 *
 * Delegates to BView::Frame(), returning the view's position and size in
 * its parent's coordinate system.
 *
 * @return The BRect frame of the wrapped view.
 * @see   SetFrame()
 */
BRect
BViewLayoutItem::Frame()
{
	return fView->Frame();
}


/**
 * @brief Move and resize the wrapped view to match the given frame.
 *
 * Uses BView::MoveTo() and BView::ResizeTo() to apply the layout-computed
 * frame to the view.
 *
 * @param frame  The target position and size in the parent's coordinate system.
 * @see   Frame()
 */
void
BViewLayoutItem::SetFrame(BRect frame)
{
	fView->MoveTo(frame.LeftTop());
	fView->ResizeTo(frame.Width(), frame.Height());
}


/**
 * @brief Return whether the wrapped view supports height-for-width layout.
 *
 * Delegates to BView::HasHeightForWidth().
 *
 * @return true if the view adjusts its height based on its width.
 * @see   GetHeightForWidth()
 */
bool
BViewLayoutItem::HasHeightForWidth()
{
	return fView->HasHeightForWidth();
}


/**
 * @brief Query the wrapped view's height constraints for a given width.
 *
 * Delegates to BView::GetHeightForWidth(). Only meaningful when
 * HasHeightForWidth() returns true.
 *
 * @param width      The width to evaluate height constraints for.
 * @param min        Receives the minimum height for @a width.
 * @param max        Receives the maximum height for @a width.
 * @param preferred  Receives the preferred height for @a width.
 * @see   HasHeightForWidth()
 */
void
BViewLayoutItem::GetHeightForWidth(float width, float* min, float* max,
	float* preferred)
{
	fView->GetHeightForWidth(width, min, max, preferred);
}


/**
 * @brief Return the wrapped BView.
 *
 * @return Pointer to the BView this item wraps.
 */
BView*
BViewLayoutItem::View()
{
	return fView;
}


void
BViewLayoutItem::Relayout(bool immediate)
{
	if (immediate)
		fView->Layout(false);
	else
		fView->Relayout();
}


/**
 * @brief Archive the layout item into a BMessage.
 *
 * Delegates to BLayoutItem::Archive() and finalises through BArchiver so that
 * cross-referenced objects (e.g. the wrapped view) are handled correctly by
 * the archiving framework.
 *
 * @param into  The message to archive into.
 * @param deep  If true, child objects are archived recursively.
 * @return B_OK on success, or an error code on failure.
 * @retval B_OK On success.
 * @see   AllArchived(), Instantiate()
 */
status_t
BViewLayoutItem::Archive(BMessage* into, bool deep) const
{
	BArchiver archiver(into);
	status_t err = BLayoutItem::Archive(into, deep);

	return archiver.Finish(err);
}


/**
 * @brief Complete the archiving pass by storing the wrapped view reference.
 *
 * Called by the archiving framework after all objects have been archived.
 * Adds a reference to the wrapped view under kViewField only when the view
 * itself has already been archived by the same BArchiver session.
 *
 * @param into  The archive message being assembled.
 * @return B_OK on success, B_NAME_NOT_FOUND if the view was not archived
 *         in the same session, or another error code on failure.
 * @see   Archive()
 */
status_t
BViewLayoutItem::AllArchived(BMessage* into) const
{
	BArchiver archiver(into);
	status_t err = BLayoutItem::AllArchived(into);

	if (err == B_OK) {
		if (archiver.IsArchived(fView))
			err = archiver.AddArchivable(kViewField, fView);
		else
			err = B_NAME_NOT_FOUND;
	}

	return err;
}


/**
 * @brief Complete the unarchiving pass for this layout item.
 *
 * Verifies that the wrapped view was successfully resolved before delegating
 * to BLayoutItem::AllUnarchived().
 *
 * @param from  The archive message being read.
 * @return B_OK on success, or B_ERROR if the view pointer is still NULL.
 * @see   Archive(), AllArchived()
 */
status_t
BViewLayoutItem::AllUnarchived(const BMessage* from)
{
	if (!fView)
		return B_ERROR;

	return BLayoutItem::AllUnarchived(from);
}


/**
 * @brief Instantiate a BViewLayoutItem from an archived BMessage.
 *
 * @param from  The archive message to instantiate from.
 * @return A newly allocated BViewLayoutItem, or NULL if @a from is not a
 *         valid BViewLayoutItem archive.
 * @see   Archive()
 */
BArchivable*
BViewLayoutItem::Instantiate(BMessage* from)
{
	if (validate_instantiation(from, "BViewLayoutItem"))
		return new(std::nothrow) BViewLayoutItem(from);
	return NULL;
}


/**
 * @brief Propagate a layout-invalidation event to the wrapped view.
 *
 * Called by the layout system when the cached layout information is stale.
 * Delegates to BView::InvalidateLayout() with the same @a children flag.
 *
 * @param children  If true, the layout of descendant views is also invalidated.
 */
void
BViewLayoutItem::LayoutInvalidated(bool children)
{
	fView->InvalidateLayout(children);
}


/**
 * @brief Respond to a change in the visibility of ancestor views.
 *
 * Updates the cached ancestor-visibility flag and calls Show() or Hide()
 * on the wrapped view to match. No action is taken if the visibility has
 * not actually changed.
 *
 * @param shown  true if ancestor views have become visible, false if they
 *               have been hidden.
 * @see   IsVisible(), SetVisible()
 */
void
BViewLayoutItem::AncestorVisibilityChanged(bool shown)
{
	if (fAncestorsVisible == shown)
		return;

	fAncestorsVisible = shown;
	if (shown)
		fView->Show();
	if (!shown)
		fView->Hide();
}
