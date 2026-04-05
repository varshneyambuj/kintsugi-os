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
 *   Copyright 2006-2010 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold, bonefish@cs.tu-berlin.de
 */


/**
 * @file Layout.cpp
 * @brief Implementation of BLayout, the abstract base class for all layout managers
 *
 * BLayout manages a collection of BLayoutItem objects and is responsible for
 * distributing available space among them. It ties into the BView hierarchy via
 * an owning view and handles invalidation and re-layout on size changes.
 *
 * @see BLayoutItem, BAbstractLayout, BView
 */


#include <Layout.h>

#include <algorithm>
#include <new>
#include <syslog.h>

#include <AutoDeleter.h>
#include <LayoutContext.h>
#include <Message.h>
#include <View.h>
#include <ViewPrivate.h>

#include "ViewLayoutItem.h"


using BPrivate::AutoDeleter;

using std::nothrow;
using std::swap;


namespace {
	/** @brief State flag: layout must be recalculated from scratch. */
	const uint32 B_LAYOUT_INVALID = 0x80000000UL; // needs layout
	/** @brief State flag: cached size hints are stale and must be recomputed. */
	const uint32 B_LAYOUT_CACHE_INVALID = 0x40000000UL; // needs recalculation
	/** @brief State flag: layout must run at least once even if already valid. */
	const uint32 B_LAYOUT_REQUIRED = 0x20000000UL; // needs layout
	/** @brief State flag: a layout pass is currently executing. */
	const uint32 B_LAYOUT_IN_PROGRESS = 0x10000000UL;
	/** @brief State value representing a fully up-to-date layout with no pending work. */
	const uint32 B_LAYOUT_ALL_CLEAR = 0UL;

	/** @brief Mask of states during which calling InvalidateLayout() is disallowed. */
	const uint32 B_LAYOUT_INVALIDATION_ILLEGAL
		= B_LAYOUT_CACHE_INVALID | B_LAYOUT_IN_PROGRESS;
	/** @brief Mask of states that together indicate a layout pass is needed. */
	const uint32 B_LAYOUT_NECESSARY
		= B_LAYOUT_INVALID | B_LAYOUT_REQUIRED | B_LAYOUT_CACHE_INVALID;
	/** @brief Mask of states during which Relayout() should defer work. */
	const uint32 B_RELAYOUT_NOT_OK
		= B_LAYOUT_INVALID | B_LAYOUT_IN_PROGRESS;

	/** @brief Archive field name used to store child BLayoutItem objects. */
	const char* const kLayoutItemField = "BLayout:items";


	/**
	 * @brief RAII helper that calls BView::Private::RemoveSelf() on a BView.
	 *
	 * Used with AutoDeleter to automatically un-parent a view when an error
	 * occurs during AddItem(), preventing the view from being stranded in the
	 * wrong parent.
	 */
	struct ViewRemover {
		inline void operator()(BView* view) {
			if (view)
				BView::Private(view).RemoveSelf();
		}
	};
}


/**
 * @brief Construct a default BLayout with no owner and an empty item list.
 *
 * Initialises all state flags to B_LAYOUT_ALL_CLEAR and preallocates space
 * for up to 20 layout items.
 */
BLayout::BLayout()
	:
	fState(B_LAYOUT_ALL_CLEAR),
	fAncestorsVisible(true),
	fInvalidationDisabled(0),
	fContext(NULL),
	fOwner(NULL),
	fTarget(NULL),
	fItems(20)
{
}


/**
 * @brief Unarchiving constructor — restore a BLayout from a BMessage archive.
 *
 * Prepares the BUnarchiver infrastructure via BUnarchiver::PrepareArchive(),
 * then ensures that each archived child BLayoutItem is scheduled for
 * unarchiving. Items are actually added to the layout in AllUnarchived().
 *
 * @param from The archive message produced by Archive().
 * @see Archive(), AllUnarchived()
 */
BLayout::BLayout(BMessage* from)
	:
	BLayoutItem(BUnarchiver::PrepareArchive(from)),
	fState(B_LAYOUT_ALL_CLEAR),
	fAncestorsVisible(true),
	fInvalidationDisabled(0),
	fContext(NULL),
	fOwner(NULL),
	fTarget(NULL),
	fItems(20)
{
	BUnarchiver unarchiver(from);

	int32 i = 0;
	while (unarchiver.EnsureUnarchived(kLayoutItemField, i++) == B_OK)
		;
}


/**
 * @brief Destroy the BLayout.
 *
 * Notifies the owning view that the layout is being deleted (to prevent a
 * double-free), and emits a debugger() warning if items remain — subclass
 * destructors are expected to remove all items before chaining to this
 * destructor.
 *
 * @note Subclasses must remove all layout items before their own destructor
 *       returns, otherwise the ItemRemoved() hook will not be called for the
 *       remaining items.
 */
BLayout::~BLayout()
{
	// in case we have a view, but have been added to a layout as a BLayoutItem
	// we will get deleted before our view, so we should tell it that we're
	// going, so that we aren't double-freed.
	if (fOwner && this == fOwner->GetLayout())
		fOwner->_LayoutLeft(this);

	if (CountItems() > 0) {
		debugger("Deleting a BLayout that still has items. Subclass hooks "
			"will not be called");
	}
}


/**
 * @brief Return the view that owns this layout.
 *
 * The owner is the view for which this layout manages children. It differs
 * from the target only when the layout is nested inside another layout as a
 * BLayoutItem.
 *
 * @return The owning BView, or NULL if the layout has no owner.
 * @see TargetView(), SetOwner()
 */
BView*
BLayout::Owner() const
{
	return fOwner;
}


/**
 * @brief Return the view into whose child list managed views are inserted.
 *
 * For top-level layouts the target equals the owner. For nested layouts
 * (layouts added as BLayoutItems inside another layout) the target is the
 * ancestor view that actually holds all the child views.
 *
 * @return The target BView, or NULL if no target has been set.
 * @see Owner()
 */
BView*
BLayout::TargetView() const
{
	return fTarget;
}


/**
 * @brief Return the owning view (synonym for Owner()).
 *
 * Provided for API symmetry with BLayoutItem::View().
 *
 * @return The owning BView, or NULL.
 * @see Owner()
 */
BView*
BLayout::View()
{
	return fOwner;
}


/**
 * @brief Add a child view to the end of this layout.
 *
 * Convenience overload that appends \a child at index -1 (i.e. the end).
 *
 * @param child The view to add.
 * @return The BLayoutItem created to represent \a child, or NULL on failure.
 * @see AddView(int32, BView*)
 */
BLayoutItem*
BLayout::AddView(BView* child)
{
	return AddView(-1, child);
}


/**
 * @brief Add a child view at a specific position in this layout.
 *
 * If \a child already has a layout of its own, that layout is used as the
 * BLayoutItem; otherwise a new BViewLayoutItem is created. The view is also
 * added to the target view's child hierarchy if it is not already there.
 *
 * @param index The position at which to insert the item, or -1 to append.
 * @param child The view to add.
 * @return The BLayoutItem wrapping \a child, or NULL if the operation fails.
 * @see RemoveView(), AddItem()
 */
BLayoutItem*
BLayout::AddView(int32 index, BView* child)
{
	BLayoutItem* item = child->GetLayout();
	ObjectDeleter<BLayoutItem> itemDeleter(NULL);
	if (!item) {
		item = new(nothrow) BViewLayoutItem(child);
		itemDeleter.SetTo(item);
	}

	if (item && AddItem(index, item)) {
		itemDeleter.Detach();
		return item;
	}

	return NULL;
}


/**
 * @brief Add a BLayoutItem at the end of this layout.
 *
 * Convenience overload that appends \a item at index -1.
 *
 * @param item The layout item to add.
 * @return True if the item was added, false on failure.
 * @see AddItem(int32, BLayoutItem*)
 */
bool
BLayout::AddItem(BLayoutItem* item)
{
	return AddItem(-1, item);
}


/**
 * @brief Add a BLayoutItem at a specific position in this layout.
 *
 * If the item wraps a BView, the view is added to the target view's child
 * list (if not already there). The ItemAdded() hook is called, and the item's
 * visibility is synchronised with the layout's ancestor visibility state.
 * The layout is invalidated on success.
 *
 * @param index The zero-based position at which to insert the item. Values
 *              less than 0 or greater than CountItems() are clamped to the end.
 * @param item  The layout item to add.
 * @return True on success, false if the item is NULL, this layout has no
 *         target, the item is already in this layout, the view cannot be
 *         added to the target, the list insertion fails, or ItemAdded()
 *         returns false.
 * @see RemoveItem(), ItemAdded()
 */
bool
BLayout::AddItem(int32 index, BLayoutItem* item)
{
	if (!fTarget || !item || fItems.HasItem(item))
		return false;

	// if the item refers to a BView, we make sure it is added to the parent
	// view
	BView* view = item->View();
	AutoDeleter<BView, ViewRemover> remover(NULL);
		// In case of errors, we don't want to leave this view added where it
		// shouldn't be.
	if (view && view->fParent != fTarget) {
		if (!fTarget->_AddChild(view, NULL))
			return false;
		else
			remover.SetTo(view);
	}

	// validate the index
	if (index < 0 || index > fItems.CountItems())
		index = fItems.CountItems();

	if (!fItems.AddItem(item, index))
		return false;

	if (!ItemAdded(item, index)) {
		fItems.RemoveItem(index);
		return false;
	}

	item->SetLayout(this);
	if (!fAncestorsVisible)
		item->AncestorVisibilityChanged(fAncestorsVisible);
	InvalidateLayout();
	remover.Detach();
	return true;
}


/**
 * @brief Remove all layout items that wrap \a child from this layout.
 *
 * A view may be represented by more than one BLayoutItem (e.g. when it
 * participates in multiple layout rows/columns). This method removes all of
 * them. The BViewLayoutItem wrappers are deleted; the view's own layout item
 * (returned by BView::GetLayout()) is not deleted.
 *
 * @param child The view whose items should be removed.
 * @return True if at least one item was removed, false if none were found.
 * @see RemoveItem(BLayoutItem*), RemoveItem(int32)
 */
bool
BLayout::RemoveView(BView* child)
{
	bool removed = false;

	// a view can have any number of layout items - we need to remove them all
	int32 remaining = BView::Private(child).CountLayoutItems();
	for (int32 i = CountItems() - 1; i >= 0 && remaining > 0; i--) {
		BLayoutItem* item = ItemAt(i);

		if (item->View() != child)
			continue;

		RemoveItem(i);
		if (item != child->GetLayout())
			delete item;

		remaining--;
		removed = true;
	}

	return removed;
}


/**
 * @brief Remove a specific BLayoutItem from this layout.
 *
 * Looks up the item's index and delegates to RemoveItem(int32).
 *
 * @param item The item to remove.
 * @return True if the item was found and removed, false otherwise.
 * @see RemoveItem(int32)
 */
bool
BLayout::RemoveItem(BLayoutItem* item)
{
	int32 index = IndexOfItem(item);
	return (index >= 0 ? RemoveItem(index) != NULL : false);
}


/**
 * @brief Remove the BLayoutItem at position \a index from this layout.
 *
 * Calls the ItemRemoved() hook, clears the item's layout pointer, and removes
 * the associated view from the target view's child list if this was the last
 * layout item referencing that view. The layout is invalidated afterward.
 *
 * @param index The zero-based position of the item to remove.
 * @return The removed BLayoutItem, or NULL if \a index is out of range.
 * @see RemoveItem(BLayoutItem*), RemoveView(), ItemRemoved()
 */
BLayoutItem*
BLayout::RemoveItem(int32 index)
{
	if (index < 0 || index >= fItems.CountItems())
		return NULL;

	BLayoutItem* item = (BLayoutItem*)fItems.RemoveItem(index);
	ItemRemoved(item, index);
	item->SetLayout(NULL);

	// If this is the last item in use that refers to its BView,
	// that BView now needs to be removed. UNLESS fTarget is NULL,
	// in which case we leave the view as is. (See SetTarget() for more info)
	BView* view = item->View();
	if (fTarget && view && BView::Private(view).CountLayoutItems() == 0)
		view->_RemoveSelf();

	InvalidateLayout();
	return item;
}


/**
 * @brief Return the BLayoutItem at position \a index.
 *
 * @param index Zero-based item index.
 * @return The item at that index, or NULL if \a index is out of range.
 * @see CountItems(), IndexOfItem()
 */
BLayoutItem*
BLayout::ItemAt(int32 index) const
{
	return (BLayoutItem*)fItems.ItemAt(index);
}


/**
 * @brief Return the number of items currently managed by this layout.
 *
 * @return The item count, which may be zero.
 * @see ItemAt(), IndexOfItem()
 */
int32
BLayout::CountItems() const
{
	return fItems.CountItems();
}


/**
 * @brief Return the index of a given BLayoutItem in this layout.
 *
 * @param item The item to search for.
 * @return The zero-based index, or -1 if the item is not in this layout.
 * @see ItemAt(), IndexOfView()
 */
int32
BLayout::IndexOfItem(const BLayoutItem* item) const
{
	return fItems.IndexOf(item);
}


/**
 * @brief Return the index of the first layout item that wraps \a child.
 *
 * Because a view can be represented by multiple BLayoutItems, only the index
 * of the first matching item is returned.
 *
 * @param child The view to search for.
 * @return The zero-based index of the first item whose View() equals \a child,
 *         or -1 if the view is not in this layout or \a child is NULL.
 * @see IndexOfItem(), AddView()
 */
int32
BLayout::IndexOfView(BView* child) const
{
	if (child == NULL)
		return -1;

	// A BView can have many items, so we just do our best and return the
	// index of the first one in this layout.
	BView::Private viewPrivate(child);
	int32 itemCount = viewPrivate.CountLayoutItems();
	for (int32 i = 0; i < itemCount; i++) {
		BLayoutItem* item = viewPrivate.LayoutItemAt(i);
		if (item->Layout() == this)
			return IndexOfItem(item);
	}
	return -1;
}


/**
 * @brief Return whether all ancestor views of this layout are currently visible.
 *
 * @return True if no ancestor view is hidden, false if at least one is hidden.
 * @see AncestorVisibilityChanged()
 */
bool
BLayout::AncestorsVisible() const
{
	return fAncestorsVisible;
}


/**
 * @brief Mark this layout (and optionally all children) as needing a layout pass.
 *
 * Sets B_LAYOUT_NECESSARY on the state flags, calls the LayoutInvalidated()
 * hook, propagates to child items when \a children is true, and notifies the
 * owner view and any enclosing layout so that the invalidation bubbles up the
 * hierarchy.
 *
 * Invalidation is silently suppressed while layout invalidation is disabled
 * (see DisableLayoutInvalidation()) or while a layout pass is in progress.
 *
 * @param children If true, every child BLayoutItem is also invalidated.
 * @see RequireLayout(), Relayout(), LayoutItems()
 */
void
BLayout::InvalidateLayout(bool children)
{
	// printf("BLayout(%p)::InvalidateLayout(%i) : state %x, disabled %li\n",
	// this, children, (unsigned int)fState, fInvalidationDisabled);

	if (fTarget && fTarget->IsLayoutInvalidationDisabled())
		return;
	if (fInvalidationDisabled > 0
		|| (fState & B_LAYOUT_INVALIDATION_ILLEGAL) != 0) {
		return;
	}

	fState |= B_LAYOUT_NECESSARY;
	LayoutInvalidated(children);

	if (children) {
		for (int32 i = CountItems() - 1; i >= 0; i--)
			ItemAt(i)->InvalidateLayout(children);
	}

	if (fOwner)
		fOwner->InvalidateLayout(children);

	if (BLayout* nestedIn = Layout()) {
		nestedIn->InvalidateLayout();
	} else if (fOwner) {
		// If we weren't added as a BLayoutItem, we still have to invalidate
		// whatever layout our owner is in.
		fOwner->_InvalidateParentLayout();
	}
}


/**
 * @brief Force the layout to run at least once on the next LayoutItems() call.
 *
 * Sets B_LAYOUT_REQUIRED so that LayoutItems() will execute DoLayout() even
 * if the layout considers itself valid.
 *
 * @see LayoutItems(), InvalidateLayout()
 */
void
BLayout::RequireLayout()
{
	fState |= B_LAYOUT_REQUIRED;
}


/**
 * @brief Return whether the layout's cached geometry is currently up to date.
 *
 * @return True if B_LAYOUT_INVALID is not set, false if a layout pass is
 *         pending.
 * @see InvalidateLayout(), RequireLayout()
 */
bool
BLayout::IsValid()
{
	return (fState & B_LAYOUT_INVALID) == 0;
}


/**
 * @brief Suppress InvalidateLayout() calls until EnableLayoutInvalidation() is called.
 *
 * Increments a counter; each call to DisableLayoutInvalidation() must be
 * paired with a corresponding call to EnableLayoutInvalidation().
 *
 * @see EnableLayoutInvalidation()
 */
void
BLayout::DisableLayoutInvalidation()
{
	fInvalidationDisabled++;
}


/**
 * @brief Re-enable InvalidateLayout() after it was suppressed.
 *
 * Decrements the invalidation-disabled counter. When the counter reaches zero,
 * InvalidateLayout() calls will be honoured again.
 *
 * @see DisableLayoutInvalidation()
 */
void
BLayout::EnableLayoutInvalidation()
{
	if (fInvalidationDisabled > 0)
		fInvalidationDisabled--;
}


/**
 * @brief Run a layout pass if one is needed, unless a parent layout is running.
 *
 * Skips the pass if the state is clean and \a force is false, if a parent
 * layout pass is in progress (waiting for the parent to position us), or if
 * the owner view already has an active layout context. Otherwise creates a
 * fresh BLayoutContext and calls _LayoutWithinContext().
 *
 * @param force If true, the layout runs even if the state is clean.
 * @see Relayout(), RequireLayout(), InvalidateLayout()
 */
void
BLayout::LayoutItems(bool force)
{
	if ((fState & B_LAYOUT_NECESSARY) == 0 && !force)
		return;

	if (Layout() && (Layout()->fState & B_LAYOUT_IN_PROGRESS) != 0)
		return; // wait for parent layout to lay us out.

	if (fTarget && fTarget->LayoutContext())
		return;

	BLayoutContext context;
	_LayoutWithinContext(force, &context);
}


/**
 * @brief Request that the layout re-run as soon as it is safe to do so.
 *
 * Sets B_LAYOUT_REQUIRED and calls LayoutItems(false) unless the layout is
 * currently invalid or a layout pass is in progress. The \a immediate flag
 * bypasses those guards.
 *
 * @param immediate If true, the layout runs immediately regardless of state.
 * @see LayoutItems(), InvalidateLayout()
 */
void
BLayout::Relayout(bool immediate)
{
	if ((fState & B_RELAYOUT_NOT_OK) == 0 || immediate) {
		fState |= B_LAYOUT_REQUIRED;
		LayoutItems(false);
	}
}


/**
 * @brief Execute a layout pass within an existing BLayoutContext.
 *
 * If the owner view's Private::WillLayout() returns true the owner decides
 * when to trigger the pass; otherwise DoLayout() is called directly and all
 * nested view-less layouts are given the opportunity to run as well.
 *
 * @param force   If true, DoLayout() is called even on a clean layout.
 * @param context The active layout context to use for this pass.
 * @see LayoutItems(), DoLayout()
 */
void
BLayout::_LayoutWithinContext(bool force, BLayoutContext* context)
{
// printf("BLayout(%p)::_LayoutWithinContext(%i, %p), state %x, fContext %p\n",
// this, force, context, (unsigned int)fState, fContext);

	if ((fState & B_LAYOUT_NECESSARY) == 0 && !force)
		return;

	BLayoutContext* oldContext = fContext;
	fContext = context;

	if (fOwner && BView::Private(fOwner).WillLayout()) {
		// in this case, let our owner decide whether or not to have us
		// do our layout, if they do, we won't end up here again.
		fOwner->_Layout(force, context);
	} else {
		fState |= B_LAYOUT_IN_PROGRESS;
		DoLayout();
		// we must ensure that all items are laid out, layouts with a view will
		// have their layout process triggered by their view, but nested
		// view-less layouts must have their layout triggered here (if it hasn't
		// already been triggered).
		int32 nestedLayoutCount = fNestedLayouts.CountItems();
		for (int32 i = 0; i < nestedLayoutCount; i++) {
			BLayout* layout = (BLayout*)fNestedLayouts.ItemAt(i);
			if ((layout->fState & B_LAYOUT_NECESSARY) != 0)
				layout->_LayoutWithinContext(force, context);
		}
		fState = B_LAYOUT_ALL_CLEAR;
	}

	fContext = oldContext;
}


/**
 * @brief Return the rectangle in which this layout may position its items.
 *
 * For layouts with an owner view the area is the owner's bounds (offset to
 * the origin); for nested layouts the area is the frame provided by the
 * enclosing layout.
 *
 * @return The available layout area in the layout's local coordinates.
 */
BRect
BLayout::LayoutArea()
{
	BRect area(Frame());
	if (fOwner)
		area.OffsetTo(B_ORIGIN);
	return area;
}


/**
 * @brief Archive this BLayout into a BMessage.
 *
 * When \a deep is true, each child BLayoutItem is archived via
 * BArchiver::AddArchivable() and ItemArchived() is called to let subclasses
 * store per-item data. The archive is finalised with BArchiver::Finish().
 *
 * @param into The message to archive into.
 * @param deep If true, child items are recursively archived.
 * @return B_OK on success, or the first error encountered.
 * @see AllArchived(), AllUnarchived()
 */
status_t
BLayout::Archive(BMessage* into, bool deep) const
{
	BArchiver archiver(into);
	status_t err = BLayoutItem::Archive(into, deep);

	if (deep) {
		int32 count = CountItems();
		for (int32 i = 0; i < count && err == B_OK; i++) {
			BLayoutItem* item = ItemAt(i);
			err = archiver.AddArchivable(kLayoutItemField, item, deep);

			if (err == B_OK) {
				err = ItemArchived(into, item, i);
				if (err != B_OK)
					syslog(LOG_ERR, "ItemArchived() failed at index: %d.", i);
			}
		}
	}

	return archiver.Finish(err);
}


/**
 * @brief Hook called after all objects in an archive tree have been archived.
 *
 * Default implementation delegates to BLayoutItem::AllArchived().
 *
 * @param archive The archive message being built.
 * @return B_OK on success.
 * @see Archive()
 */
status_t
BLayout::AllArchived(BMessage* archive) const
{
	return BLayoutItem::AllArchived(archive);
}


/**
 * @brief Hook called after all objects in an archive tree have been unarchived.
 *
 * Restores the child BLayoutItem list from the archive message by calling
 * BUnarchiver::FindObject() for each item index, then calling ItemAdded() and
 * ItemUnarchived() for each item. The layout is invalidated on completion.
 *
 * @param from The archive message that was passed to the unarchiving constructor.
 * @return B_OK on success, or an error if any item cannot be unarchived.
 * @see Archive(), AllArchived()
 */
status_t
BLayout::AllUnarchived(const BMessage* from)
{
	BUnarchiver unarchiver(from);
	status_t err = BLayoutItem::AllUnarchived(from);
	if (err != B_OK)
		return err;

	int32 itemCount = 0;
	unarchiver.ArchiveMessage()->GetInfo(kLayoutItemField, NULL, &itemCount);
	for (int32 i = 0; i < itemCount && err == B_OK; i++) {
		BLayoutItem* item;
		err = unarchiver.FindObject(kLayoutItemField,
			i, BUnarchiver::B_DONT_ASSUME_OWNERSHIP, item);
		if (err != B_OK)
			return err;

		if (!fItems.AddItem(item, i) || !ItemAdded(item, i)) {
			fItems.RemoveItem(i);
			return B_ERROR;
		}

		err = ItemUnarchived(from, item, i);
		if (err != B_OK) {
			fItems.RemoveItem(i);
			ItemRemoved(item, i);
			return err;
		}

		item->SetLayout(this);
		unarchiver.AssumeOwnership(item);
	}

	InvalidateLayout();
	return err;
}


/**
 * @brief Hook called for each item during Archive() to store per-item data.
 *
 * Subclasses may override this to archive additional per-item information
 * (e.g. grid row/column indices). The default implementation does nothing.
 *
 * @param into  The archive message being built.
 * @param item  The BLayoutItem currently being archived.
 * @param index The zero-based index of \a item in this layout.
 * @return B_OK on success, or an error to abort archiving.
 * @see ItemUnarchived()
 */
status_t
BLayout::ItemArchived(BMessage* into, BLayoutItem* item, int32 index) const
{
	return B_OK;
}


/**
 * @brief Hook called for each item during AllUnarchived() to restore per-item data.
 *
 * Subclasses may override this to read back data previously written by
 * ItemArchived(). The default implementation does nothing.
 *
 * @param from  The archive message being restored.
 * @param item  The BLayoutItem that was just unarchived.
 * @param index The zero-based index at which the item was inserted.
 * @return B_OK on success, or an error to abort unarchiving.
 * @see ItemArchived()
 */
status_t
BLayout::ItemUnarchived(const BMessage* from, BLayoutItem* item, int32 index)
{
	return B_OK;
}


/**
 * @brief Hook called after a new item has been successfully inserted.
 *
 * Subclasses may override this to update internal bookkeeping (e.g. row/column
 * maps in BGridLayout). Returning false causes AddItem() to roll back the
 * insertion.
 *
 * @param item    The newly inserted BLayoutItem.
 * @param atIndex The index at which \a item was inserted.
 * @return True to accept the insertion, false to reject and roll back.
 * @see ItemRemoved()
 */
bool
BLayout::ItemAdded(BLayoutItem* item, int32 atIndex)
{
	return true;
}


/**
 * @brief Hook called just before an item is removed from this layout.
 *
 * Subclasses may override this to clean up internal bookkeeping associated
 * with \a item. The default implementation does nothing.
 *
 * @param item      The BLayoutItem about to be removed.
 * @param fromIndex The index the item occupied before removal.
 * @see ItemAdded()
 */
void
BLayout::ItemRemoved(BLayoutItem* item, int32 fromIndex)
{
}


/**
 * @brief Hook called when the layout is invalidated.
 *
 * Subclasses may override this to discard cached size computations. The
 * default implementation does nothing.
 *
 * @param children True if child items are also being invalidated.
 * @see InvalidateLayout()
 */
void
BLayout::LayoutInvalidated(bool children)
{
}


/**
 * @brief Hook called when the owning view changes.
 *
 * Subclasses may override this to respond to the layout being attached to or
 * detached from a BView. The default implementation does nothing.
 *
 * @param was The previous owner, or NULL if there was none.
 * @see SetOwner()
 */
void
BLayout::OwnerChanged(BView* was)
{
}


/**
 * @brief Hook called when this layout is added to an enclosing BLayout.
 *
 * Registers this layout as a nested layout of the parent and sets the
 * shared target view.
 *
 * @see DetachedFromLayout()
 */
void
BLayout::AttachedToLayout()
{
	if (!fOwner) {
		Layout()->fNestedLayouts.AddItem(this);
		SetTarget(Layout()->TargetView());
	}
}


/**
 * @brief Hook called when this layout is removed from its enclosing BLayout.
 *
 * Unregisters this layout from the parent's nested-layout list and clears the
 * target view.
 *
 * @param from The parent BLayout this layout was removed from.
 * @see AttachedToLayout()
 */
void
BLayout::DetachedFromLayout(BLayout* from)
{
	if (!fOwner) {
		from->fNestedLayouts.RemoveItem(this);
		SetTarget(NULL);
	}
}


/**
 * @brief Propagate an ancestor-visibility change to this layout and its items.
 *
 * Called by BLayoutItem::AncestorVisibilityChanged() when a view in the
 * ancestor chain is shown or hidden. Updates fAncestorsVisible and calls the
 * VisibilityChanged() hook.
 *
 * @param shown True if the ancestors became visible, false if they were hidden.
 * @see VisibilityChanged()
 */
void
BLayout::AncestorVisibilityChanged(bool shown)
{
	if (fAncestorsVisible == shown)
		return;

	fAncestorsVisible = shown;
	VisibilityChanged(shown);
}


/**
 * @brief Propagate a visibility change to child items of a view-less layout.
 *
 * For layouts that have no owner view of their own, forwards the visibility
 * change down to every managed BLayoutItem. Layouts with an owner view do
 * nothing here because the view's own mechanism handles propagation.
 *
 * @param show True if becoming visible, false if being hidden.
 * @see AncestorVisibilityChanged()
 */
void
BLayout::VisibilityChanged(bool show)
{
	if (fOwner)
		return;

	for (int32 i = CountItems() - 1; i >= 0; i--)
		ItemAt(i)->AncestorVisibilityChanged(show);
}


/**
 * @brief Clear the B_LAYOUT_CACHE_INVALID flag without triggering a full layout.
 *
 * Called by the layout infrastructure after cached size hints have been
 * successfully recomputed but before DoLayout() is invoked.
 *
 * @see InvalidateLayout()
 */
void
BLayout::ResetLayoutInvalidation()
{
	fState &= ~B_LAYOUT_CACHE_INVALID;
}


/**
 * @brief Return the BLayoutContext for the currently executing layout pass.
 *
 * @return The active context, or NULL if no layout pass is in progress.
 * @see _LayoutWithinContext()
 */
BLayoutContext*
BLayout::LayoutContext() const
{
	return fContext;
}


/**
 * @brief Set the view that owns this layout.
 *
 * Updates the target view to match \a owner, swaps the stored owner pointer,
 * and calls the OwnerChanged() hook with the previous owner.
 *
 * @param owner The new owning BView, or NULL to detach the layout.
 * @see Owner(), OwnerChanged()
 */
void
BLayout::SetOwner(BView* owner)
{
	if (fOwner == owner)
		return;

	SetTarget(owner);
	swap(fOwner, owner);

	OwnerChanged(owner);
		// call hook
}


/**
 * @brief Set the target view into which managed child views are inserted.
 *
 * Temporarily sets fTarget to NULL so that RemoveItem() does not touch the
 * view hierarchy while clearing all existing items (preventing views from
 * being orphaned). After all items are removed the new target is installed
 * and the layout is invalidated.
 *
 * @param target The new target BView, or NULL to detach all views.
 * @see TargetView(), SetOwner()
 */
void
BLayout::SetTarget(BView* target)
{
	if (fTarget != target) {
		/* With fTarget NULL, RemoveItem() will not remove the views from their
		 * parent. This ensures that the views are not lost to the void.
		 */
		fTarget = NULL;

		// remove and delete all items
		for (int32 i = CountItems() - 1; i >= 0; i--)
			delete RemoveItem(i);

		fTarget = target;

		InvalidateLayout();
	}
}


// Binary compatibility stuff


/**
 * @brief Binary-compatibility hook for BLayout virtual methods.
 *
 * Forwards unknown perform codes to BLayoutItem::Perform().
 *
 * @param code  The perform code identifying the operation.
 * @param _data Pointer to the perform_data structure for \a code.
 * @return B_OK on success, or an error from BLayoutItem::Perform().
 */
status_t
BLayout::Perform(perform_code code, void* _data)
{
	return BLayoutItem::Perform(code, _data);
}


void BLayout::_ReservedLayout1() {}
void BLayout::_ReservedLayout2() {}
void BLayout::_ReservedLayout3() {}
void BLayout::_ReservedLayout4() {}
void BLayout::_ReservedLayout5() {}
void BLayout::_ReservedLayout6() {}
void BLayout::_ReservedLayout7() {}
void BLayout::_ReservedLayout8() {}
void BLayout::_ReservedLayout9() {}
void BLayout::_ReservedLayout10() {}
