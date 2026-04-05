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
 *   Copyright 2006-2009 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 */


/**
 * @file CardLayout.cpp
 * @brief Implementation of BCardLayout, a layout manager that shows one child at a time
 *
 * BCardLayout stacks its items like cards in a deck, making only one item
 * visible at a time. It is useful for implementing tabbed panels, wizard
 * pages, and similar UIs.
 *
 * @see BLayout, BCardView
 */


#include <CardLayout.h>

#include <LayoutItem.h>
#include <Message.h>
#include <View.h>


namespace {
	/** @brief Archive field name used to persist the index of the visible item. */
	const char* kVisibleItemField = "BCardLayout:visibleItem";
}


/**
 * @brief Construct an empty BCardLayout.
 *
 * Initialises all cached size fields to their safe defaults and marks the
 * min/max cache as invalid so it will be (re-)computed on first use.
 *
 * @see BAbstractLayout, SetVisibleItem()
 */
BCardLayout::BCardLayout()
	:
	BAbstractLayout(),
	fMin(0, 0),
	fMax(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED),
	fPreferred(0, 0),
	fVisibleItem(NULL),
	fMinMaxValid(false)
{
}


/**
 * @brief Reconstruct a BCardLayout from an archive message.
 *
 * This is the unarchiving constructor invoked by Instantiate(). The visible
 * item index is restored later in AllUnarchived() once all child items have
 * been unarchived.
 *
 * @param from The archive message previously produced by Archive().
 * @see Instantiate(), AllUnarchived()
 */
BCardLayout::BCardLayout(BMessage* from)
	:
	BAbstractLayout(BUnarchiver::PrepareArchive(from)),
	fMin(0, 0),
	fMax(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED),
	fPreferred(0, 0),
	fVisibleItem(NULL),
	fMinMaxValid(false)
{
	BUnarchiver(from).Finish();
}


/**
 * @brief Destroy the BCardLayout.
 *
 * All owned layout items are deleted by the BAbstractLayout base class
 * destructor; no additional cleanup is required here.
 */
BCardLayout::~BCardLayout()
{
}


/**
 * @brief Return the currently visible layout item.
 *
 * @return A pointer to the visible BLayoutItem, or NULL if no item is
 *         currently visible (e.g., the layout has no items yet).
 * @see VisibleIndex(), SetVisibleItem()
 */
BLayoutItem*
BCardLayout::VisibleItem() const
{
	return fVisibleItem;
}


/**
 * @brief Return the zero-based index of the currently visible item.
 *
 * @return The index of the visible item, or -1 if no item is currently
 *         visible.
 * @see VisibleItem(), SetVisibleItem()
 */
int32
BCardLayout::VisibleIndex() const
{
	return IndexOfItem(fVisibleItem);
}


/**
 * @brief Make the item at \a index the visible card.
 *
 * Convenience overload that looks up the item by its zero-based index
 * and delegates to SetVisibleItem(BLayoutItem*).
 *
 * @param index Zero-based index of the item to show.
 * @see SetVisibleItem(BLayoutItem*), VisibleIndex()
 */
void
BCardLayout::SetVisibleItem(int32 index)
{
	SetVisibleItem(ItemAt(index));
}


/**
 * @brief Make \a item the visible card, hiding the previously visible one.
 *
 * The old visible item's visibility is set to false and the new item's
 * visibility is set to true. The min/max cache validity flag is preserved
 * across this operation because showing/hiding cards does not change the
 * aggregated size constraints.
 *
 * @param item The item to make visible, or NULL to hide all items.
 * @note Passing an item that does not belong to this layout triggers a
 *       debugger break.
 * @see SetVisibleItem(int32), VisibleItem()
 */
void
BCardLayout::SetVisibleItem(BLayoutItem* item)
{
	if (item == fVisibleItem)
		return;

	if (item != NULL && IndexOfItem(item) < 0) {
		debugger("BCardLayout::SetVisibleItem(BLayoutItem*): this item is not "
			"part of this layout, or the item does not exist.");
		return;
	}

	// Changing an item's visibility will invalidate its parent's layout (us),
	// which would normally cause the min-max to be re-computed. But in this
	// case, that is unnecessary, and so we can skip it.
	const bool minMaxValid = fMinMaxValid;

	if (fVisibleItem != NULL)
		fVisibleItem->SetVisible(false);

	fVisibleItem = item;

	if (fVisibleItem != NULL)
		fVisibleItem->SetVisible(true);

	fMinMaxValid = minMaxValid;
}


/**
 * @brief Return the base minimum size of the card layout.
 *
 * The minimum size is the component-wise maximum of all items' minimum sizes,
 * ensuring the layout is at least large enough to show any card.
 *
 * @return The cached minimum BSize, recomputed if the cache is stale.
 * @see BaseMaxSize(), BasePreferredSize()
 */
BSize
BCardLayout::BaseMinSize()
{
	_ValidateMinMax();
	return fMin;
}


/**
 * @brief Return the base maximum size of the card layout.
 *
 * The maximum size is the component-wise minimum of all items' maximum sizes,
 * clamped so it is never smaller than the minimum size.
 *
 * @return The cached maximum BSize, recomputed if the cache is stale.
 * @see BaseMinSize(), BasePreferredSize()
 */
BSize
BCardLayout::BaseMaxSize()
{
	_ValidateMinMax();
	return fMax;
}


/**
 * @brief Return the base preferred size of the card layout.
 *
 * The preferred size is the component-wise maximum of all items' preferred
 * sizes, clamped to [min, max].
 *
 * @return The cached preferred BSize, recomputed if the cache is stale.
 * @see BaseMinSize(), BaseMaxSize()
 */
BSize
BCardLayout::BasePreferredSize()
{
	_ValidateMinMax();
	return fPreferred;
}


/**
 * @brief Return the base alignment of the card layout.
 *
 * Always returns full-width, full-height alignment so that the visible card
 * occupies the entire layout area.
 *
 * @return A BAlignment with @c B_ALIGN_USE_FULL_WIDTH and
 *         @c B_ALIGN_USE_FULL_HEIGHT.
 */
BAlignment
BCardLayout::BaseAlignment()
{
	return BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_USE_FULL_HEIGHT);
}


/**
 * @brief Report whether any child item has a height-for-width dependency.
 *
 * Returns true as soon as a single item reports HasHeightForWidth() == true,
 * because the layout must satisfy that constraint when it is active.
 *
 * @return true if at least one child item has height-for-width constraints,
 *         false otherwise.
 * @see GetHeightForWidth()
 */
bool
BCardLayout::HasHeightForWidth()
{
	int32 count = CountItems();
	for (int32 i = 0; i < count; i++) {
		if (ItemAt(i)->HasHeightForWidth())
			return true;
	}

	return false;
}


/**
 * @brief Compute height constraints for a given \a width.
 *
 * Queries every item that has a height-for-width relationship and aggregates
 * the results: the minimum height is the component-wise maximum, the maximum
 * height is the component-wise minimum, and the preferred height is chosen
 * within [min, max]. Items without a height-for-width dependency use the
 * cached baseline sizes.
 *
 * @param width       The proposed width in pixels.
 * @param[out] min    Set to the aggregate minimum height; may be NULL.
 * @param[out] max    Set to the aggregate maximum height; may be NULL.
 * @param[out] preferred Set to the aggregate preferred height; may be NULL.
 * @see HasHeightForWidth()
 */
void
BCardLayout::GetHeightForWidth(float width, float* min, float* max,
	float* preferred)
{
	_ValidateMinMax();

	// init with useful values
	float minHeight = fMin.height;
	float maxHeight = fMax.height;
	float preferredHeight = fPreferred.height;

	// apply the items' constraints
	int32 count = CountItems();
	for (int32 i = 0; i < count; i++) {
		BLayoutItem* item = ItemAt(i);
		if (item->HasHeightForWidth()) {
			float itemMinHeight;
			float itemMaxHeight;
			float itemPreferredHeight;
			item->GetHeightForWidth(width, &itemMinHeight, &itemMaxHeight,
				&itemPreferredHeight);
			minHeight = max_c(minHeight, itemMinHeight);
			maxHeight = min_c(maxHeight, itemMaxHeight);
			preferredHeight = min_c(preferredHeight, itemPreferredHeight);
		}
	}

	// adjust max and preferred, if necessary
	maxHeight = max_c(maxHeight, minHeight);
	preferredHeight = max_c(preferredHeight, minHeight);
	preferredHeight = min_c(preferredHeight, maxHeight);

	if (min)
		*min = minHeight;
	if (max)
		*max = maxHeight;
	if (preferred)
		*preferred = preferredHeight;
}


/**
 * @brief Invalidate the cached min/max/preferred sizes when the layout changes.
 *
 * Called by the framework whenever the layout is marked dirty, for example
 * when a child item is added or its constraints change.
 *
 * @param children If true, child items have also been invalidated; unused
 *                 by BCardLayout but passed by the framework for completeness.
 */
void
BCardLayout::LayoutInvalidated(bool children)
{
	fMinMaxValid = false;
}


/**
 * @brief Perform the actual layout pass, positioning the visible item.
 *
 * Ensures the allocated size is at least the minimum size (when an owner
 * view is present so that clipping is handled correctly), then aligns the
 * visible item within the full layout area. Non-visible items are simply
 * left in place without being moved or resized.
 *
 * @see SetVisibleItem(), BaseMinSize()
 */
void
BCardLayout::DoLayout()
{
	_ValidateMinMax();

	BSize size(LayoutArea().Size());

	// this cannot be done when we are viewless, as our children
	// would not get cut off in the right place.
	if (Owner()) {
		size.width = max_c(size.width, fMin.width);
		size.height = max_c(size.height, fMin.height);
	}

	if (fVisibleItem != NULL)
		fVisibleItem->AlignInFrame(BRect(LayoutArea().LeftTop(), size));
}


/**
 * @brief Archive the BCardLayout into a BMessage.
 *
 * Stores the visible item's index in addition to all fields saved by the
 * base class. The index is used by AllUnarchived() to restore the visible
 * card after all items have been reconstructed.
 *
 * @param into The message to archive into.
 * @param deep If true, child items are archived recursively.
 * @return @c B_OK on success, or a negative error code on failure.
 * @see AllUnarchived(), Instantiate()
 */
status_t
BCardLayout::Archive(BMessage* into, bool deep) const
{
	BArchiver archiver(into);
	status_t err = BAbstractLayout::Archive(into, deep);

	if (err == B_OK && deep)
		err = into->AddInt32(kVisibleItemField, IndexOfItem(fVisibleItem));

	return archiver.Finish(err);
}


/**
 * @brief Called after all objects in the archive graph have been archived.
 *
 * Delegates to BAbstractLayout::AllArchived(). No BCardLayout-specific
 * post-processing is required.
 *
 * @param archive The completed archive message.
 * @return @c B_OK, or a negative error code from the base class.
 * @see Archive()
 */
status_t
BCardLayout::AllArchived(BMessage* archive) const
{
	return BAbstractLayout::AllArchived(archive);
}


/**
 * @brief Called after all objects in the archive graph have been unarchived.
 *
 * Restores the visible item by reading the previously saved index from
 * \a from and calling SetVisibleItem(). This must be deferred until all
 * child items exist and have been added to the layout.
 *
 * @param from The archive message that was used to reconstruct the layout.
 * @return @c B_OK on success, or a negative error code if the base class
 *         unarchiving or the field lookup fails.
 * @see Archive(), SetVisibleItem()
 */
status_t
BCardLayout::AllUnarchived(const BMessage* from)
{
	status_t err = BLayout::AllUnarchived(from);
	if (err != B_OK)
		return err;

	int32 visibleIndex;
	err = from->FindInt32(kVisibleItemField, &visibleIndex);
	if (err == B_OK)
		SetVisibleItem(visibleIndex);

	return err;
}


/**
 * @brief Archive a single layout item into \a into.
 *
 * Delegates to BAbstractLayout::ItemArchived(). BCardLayout adds no
 * per-item archive fields beyond those the base class provides.
 *
 * @param into  The message to archive the item into.
 * @param item  The item being archived.
 * @param index The zero-based index of \a item within this layout.
 * @return @c B_OK on success, or a negative error code on failure.
 * @see ItemUnarchived()
 */
status_t
BCardLayout::ItemArchived(BMessage* into, BLayoutItem* item, int32 index) const
{
	return BAbstractLayout::ItemArchived(into, item, index);
}


/**
 * @brief Restore a single layout item from \a from.
 *
 * Delegates to BAbstractLayout::ItemUnarchived(). BCardLayout adds no
 * per-item restoration logic beyond what the base class provides; visible
 * item selection is handled in AllUnarchived() instead.
 *
 * @param from  The archive message being unarchived.
 * @param item  The item that was just reconstructed.
 * @param index The zero-based index at which \a item was inserted.
 * @return @c B_OK on success, or a negative error code on failure.
 * @see ItemArchived(), AllUnarchived()
 */
status_t
BCardLayout::ItemUnarchived(const BMessage* from, BLayoutItem* item,
	int32 index)
{
	return BAbstractLayout::ItemUnarchived(from, item, index);
}



/**
 * @brief Create a new BCardLayout from an archive message.
 *
 * @param from The archive message to instantiate from.
 * @return A newly allocated BCardLayout on success, or NULL if \a from is
 *         not a valid BCardLayout archive.
 * @see Archive()
 */
BArchivable*
BCardLayout::Instantiate(BMessage* from)
{
	if (validate_instantiation(from, "BCardLayout"))
		return new BCardLayout(from);
	return NULL;
}


/**
 * @brief Called by the framework after a new item has been added to the layout.
 *
 * If this is the first item, it is made visible automatically. Subsequent
 * items are hidden immediately so that only one card is ever visible.
 *
 * @param item    The newly added layout item.
 * @param atIndex The zero-based index at which the item was inserted.
 * @return Always returns true to indicate the item was accepted.
 * @see ItemRemoved(), SetVisibleItem()
 */
bool
BCardLayout::ItemAdded(BLayoutItem* item, int32 atIndex)
{
	if (CountItems() <= 1)
		SetVisibleItem(item);
	else
		item->SetVisible(false);
	return true;
}


/**
 * @brief Called by the framework after an item has been removed from the layout.
 *
 * Invalidates the cached min/max sizes. If the removed item was the visible
 * card, the visible item pointer is cleared (no new card is automatically
 * selected; the caller is responsible for calling SetVisibleItem() if needed).
 *
 * @param item      The layout item that was removed.
 * @param fromIndex The zero-based index from which the item was removed.
 * @see ItemAdded(), SetVisibleItem()
 */
void
BCardLayout::ItemRemoved(BLayoutItem* item, int32 fromIndex)
{
	fMinMaxValid = false;

	if (fVisibleItem == item) {
		BLayoutItem* newVisibleItem = NULL;
		SetVisibleItem(newVisibleItem);
	}
}


/**
 * @brief Recompute and cache min/max/preferred sizes from all child items.
 *
 * Iterates over every item regardless of visibility, taking the
 * component-wise maximum for minimum and preferred sizes and the
 * component-wise minimum for maximum sizes. Results are clamped so that
 * preferred and maximum are never smaller than minimum. Marks the cache
 * valid and resets the layout invalidation flag when finished.
 *
 * @note This is a no-op when the cache is already valid (@c fMinMaxValid is
 *       true).
 * @see LayoutInvalidated(), BaseMinSize(), BaseMaxSize(), BasePreferredSize()
 */
void
BCardLayout::_ValidateMinMax()
{
	if (fMinMaxValid)
		return;

	fMin.width = 0;
	fMin.height = 0;
	fMax.width = B_SIZE_UNLIMITED;
	fMax.height = B_SIZE_UNLIMITED;
	fPreferred.width = 0;
	fPreferred.height = 0;

	int32 itemCount = CountItems();
	for (int32 i = 0; i < itemCount; i++) {
		BLayoutItem* item = ItemAt(i);

		BSize min = item->MinSize();
		BSize max = item->MaxSize();
		BSize preferred = item->PreferredSize();

		fMin.width = max_c(fMin.width, min.width);
		fMin.height = max_c(fMin.height, min.height);

		fMax.width = min_c(fMax.width, max.width);
		fMax.height = min_c(fMax.height, max.height);

		fPreferred.width = max_c(fPreferred.width, preferred.width);
		fPreferred.height = max_c(fPreferred.height, preferred.height);
	}

	fMax.width = max_c(fMax.width, fMin.width);
	fMax.height = max_c(fMax.height, fMin.height);

	fPreferred.width = max_c(fPreferred.width, fMin.width);
	fPreferred.height = max_c(fPreferred.height, fMin.height);
	fPreferred.width = min_c(fPreferred.width, fMax.width);
	fPreferred.height = min_c(fPreferred.height, fMax.height);

	fMinMaxValid = true;
	ResetLayoutInvalidation();
}


/**
 * @brief Hook for future binary-compatible extensions (FBC).
 *
 * Forwards to BAbstractLayout::Perform() unchanged. Subclasses should not
 * override this method.
 *
 * @param d   Perform code identifying the virtual method to call.
 * @param arg Opaque argument whose meaning depends on \a d.
 * @return The result from BAbstractLayout::Perform().
 */
status_t
BCardLayout::Perform(perform_code d, void* arg)
{
	return BAbstractLayout::Perform(d, arg);
}


void BCardLayout::_ReservedCardLayout1() {}
void BCardLayout::_ReservedCardLayout2() {}
void BCardLayout::_ReservedCardLayout3() {}
void BCardLayout::_ReservedCardLayout4() {}
void BCardLayout::_ReservedCardLayout5() {}
void BCardLayout::_ReservedCardLayout6() {}
void BCardLayout::_ReservedCardLayout7() {}
void BCardLayout::_ReservedCardLayout8() {}
void BCardLayout::_ReservedCardLayout9() {}
void BCardLayout::_ReservedCardLayout10() {}

