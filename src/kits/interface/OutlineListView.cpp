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
 *   Copyright 2001-2013 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marc Flerackers (mflerackers@androme.be)
 *       Axel Dörfler, axeld@pinc-software.de
 *       Rene Gollent (rene@gollent.com)
 *       Philippe Saint-Pierre, stpere@gmail.com
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file OutlineListView.cpp
 * @brief Implementation of BOutlineListView, a hierarchical expandable list view
 *
 * BOutlineListView extends BListView with collapsible/expandable item hierarchies.
 * Items at deeper levels are indented and shown only when their parent is expanded.
 * Supports sorting within each level.
 *
 * @see BListView, BListItem
 */


#include <OutlineListView.h>

#include <algorithm>

#include <stdio.h>
#include <stdlib.h>

#include <ControlLook.h>
#include <Window.h>

#include <binary_compatibility/Interface.h>


/** @brief Function pointer type for comparing two BListItem objects during sorting. */
typedef int (*compare_func)(const BListItem* a, const BListItem* b);


/**
 * @brief Comparator adaptor that wraps a compare_func for use with std::sort.
 *
 * Converts the C-style comparison function (returning negative/zero/positive)
 * into the strict-weak-ordering boolean predicate required by standard
 * algorithms.
 */
struct ListItemComparator {
	/**
	 * @brief Constructs the comparator with the given comparison function.
	 * @param compareFunc The C-style comparison function to wrap.
	 */
	ListItemComparator(compare_func compareFunc)
		:
		fCompareFunc(compareFunc)
	{
	}

	/**
	 * @brief Returns true if \a a should be ordered before \a b.
	 * @param a First item to compare.
	 * @param b Second item to compare.
	 * @return true if compareFunc(a, b) < 0.
	 */
	bool operator()(const BListItem* a, const BListItem* b) const
	{
		return fCompareFunc(a, b) < 0;
	}

private:
	/** @brief The wrapped C-style comparison function. */
	compare_func	fCompareFunc;
};


/**
 * @brief Collects all items in \a sourceList that are children of \a parent
 *        starting at \a start into \a destList.
 *
 * Iteration stops as soon as an item whose outline level is less than or equal
 * to \a parent's level is encountered, signalling that the subtree has ended.
 *
 * @param sourceList The full flat list to scan.
 * @param destList   Destination list that receives the child items.
 * @param parent     The parent item whose subtree is collected.
 * @param start      Index in \a sourceList at which to begin scanning.
 */
static void
_GetSubItems(BList& sourceList, BList& destList, BListItem* parent, int32 start)
{
	for (int32 i = start; i < sourceList.CountItems(); i++) {
		BListItem* item = (BListItem*)sourceList.ItemAt(i);
		if (item->OutlineLevel() <= parent->OutlineLevel())
			break;
		destList.AddItem(item);
	}
}


/**
 * @brief Swaps two items (and their subtrees) within a flat list.
 *
 * After swapping the two root items at \a firstIndex and \a secondIndex, the
 * method moves each item's subtree (provided in \a firstItems and
 * \a secondItems) so that the children follow their new parent.
 *
 * @param list         The flat list to modify in place.
 * @param firstIndex   Index of the first root item.
 * @param secondIndex  Index of the second root item.
 * @param firstItems   Children of the first root item.
 * @param secondItems  Children of the second root item.
 */
static void
_DoSwap(BList& list, int32 firstIndex, int32 secondIndex, BList* firstItems,
	BList* secondItems)
{
	BListItem* item = (BListItem*)list.ItemAt(firstIndex);
	list.SwapItems(firstIndex, secondIndex);
	list.RemoveItems(secondIndex + 1, secondItems->CountItems());
	list.RemoveItems(firstIndex + 1, firstItems->CountItems());
	list.AddList(secondItems, firstIndex + 1);
	int32 newIndex = list.IndexOf(item);
	if (newIndex + 1 < list.CountItems())
		list.AddList(firstItems, newIndex + 1);
	else
		list.AddList(firstItems);
}


//	#pragma mark - BOutlineListView


/**
 * @brief Constructs a BOutlineListView with an explicit frame rectangle.
 *
 * Creates the view using the legacy frame-based layout model. The underlying
 * BListView is initialised with the same parameters.
 *
 * @param frame         The position and size of the view in its parent's
 *                      coordinate system.
 * @param name          The internal name of the view.
 * @param type          Selection mode: B_SINGLE_SELECTION_LIST or
 *                      B_MULTIPLE_SELECTION_LIST.
 * @param resizingMode  Resizing behaviour flags (B_FOLLOW_*).
 * @param flags         View option flags (B_WILL_DRAW, B_NAVIGABLE, etc.).
 */
BOutlineListView::BOutlineListView(BRect frame, const char* name,
	list_view_type type, uint32 resizingMode, uint32 flags)
	:
	BListView(frame, name, type, resizingMode, flags)
{
}


/**
 * @brief Constructs a BOutlineListView suitable for use in a layout.
 *
 * Uses the layout-aware BListView constructor; frame and resizing mode are
 * managed by the layout engine rather than set explicitly.
 *
 * @param name  The internal name of the view.
 * @param type  Selection mode: B_SINGLE_SELECTION_LIST or
 *              B_MULTIPLE_SELECTION_LIST.
 * @param flags View option flags (B_WILL_DRAW, B_NAVIGABLE, etc.).
 */
BOutlineListView::BOutlineListView(const char* name, list_view_type type,
	uint32 flags)
	:
	BListView(name, type, flags)
{
}


/**
 * @brief Reconstructs a BOutlineListView from a BMessage archive.
 *
 * Restores the list type and all items (including subitems) stored under the
 * "_l_full_items" field by the Archive() method. Each archived item is
 * instantiated via instantiate_object() and added through AddItem().
 *
 * @param archive The archive message produced by Archive().
 * @see Archive(), Instantiate()
 */
BOutlineListView::BOutlineListView(BMessage* archive)
	:
	BListView(archive)
{
	int32 i = 0;
	BMessage subData;
	while (archive->FindMessage("_l_full_items", i++, &subData) == B_OK) {
		BArchivable* object = instantiate_object(&subData);
		if (!object)
			continue;

		BListItem* item = dynamic_cast<BListItem*>(object);
		if (item)
			AddItem(item);
	}
}


/**
 * @brief Destroys the BOutlineListView and empties the full item list.
 *
 * Only the internal tracking list is cleared here; actual item deletion is
 * the caller's responsibility (matching BListView's ownership convention).
 */
BOutlineListView::~BOutlineListView()
{
	fFullList.MakeEmpty();
}


/**
 * @brief Creates a new BOutlineListView from an archive message.
 *
 * This static factory method is called by the archiving system when
 * restoring a BOutlineListView from a BMessage.
 *
 * @param archive The archive message to restore from.
 * @return A newly allocated BOutlineListView, or NULL if validation fails.
 */
BArchivable*
BOutlineListView::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "BOutlineListView"))
		return new BOutlineListView(archive);

	return NULL;
}


/**
 * @brief Archives the view and all its items into \a archive.
 *
 * Because BOutlineListView maintains a separate full list that also contains
 * hidden subitems, it cannot reuse BListView::Archive() directly. Instead it
 * calls BView::Archive() and then stores every item from fFullList under the
 * key "_l_full_items" (rather than "_l_items" used by BListView).
 *
 * @param archive The message to write the archived data into.
 * @param deep    If true, each item is archived recursively.
 * @return B_OK on success, or an error code if archiving fails.
 */
status_t
BOutlineListView::Archive(BMessage* archive, bool deep) const
{
	// Note: We can't call the BListView Archive function here, as we are also
	// interested in subitems BOutlineListView can have. They are even stored
	// with a different field name (_l_full_items vs. _l_items).

	status_t status = BView::Archive(archive, deep);
	if (status != B_OK)
		return status;

	status = archive->AddInt32("_lv_type", fListType);
	if (status == B_OK && deep) {
		int32 i = 0;
		BListItem* item = NULL;
		while ((item = static_cast<BListItem*>(fFullList.ItemAt(i++)))) {
			BMessage subData;
			status = item->Archive(&subData, true);
			if (status >= B_OK)
				status = archive->AddMessage("_l_full_items", &subData);

			if (status < B_OK)
				break;
		}
	}

	if (status >= B_OK && InvocationMessage() != NULL)
		status = archive->AddMessage("_msg", InvocationMessage());

	if (status == B_OK && fSelectMessage != NULL)
		status = archive->AddMessage("_2nd_msg", fSelectMessage);

	return status;
}


/**
 * @brief Handles mouse-button press events.
 *
 * If the click lands on the latch triangle of an item that has children, the
 * item is expanded or collapsed. Otherwise the event is forwarded to
 * BListView::MouseDown() for normal selection handling.
 *
 * @param where The click position in the view's coordinate system.
 */
void
BOutlineListView::MouseDown(BPoint where)
{
	MakeFocus();

	int32 index = IndexOf(where);

	if (index != -1) {
		BListItem* item = ItemAt(index);

		if (item->fHasSubitems
			&& LatchRect(ItemFrame(index), item->fLevel).Contains(where)) {
			if (item->IsExpanded())
				Collapse(item);
			else
				Expand(item);
		} else
			BListView::MouseDown(where);
	}
}


/**
 * @brief Handles keyboard navigation within the outline list.
 *
 * Intercepts the arrow keys to provide tree-aware navigation:
 * - Right arrow expands the selected item (if it has children), or moves
 *   selection to the first child when already expanded.
 * - Left arrow collapses the selected item (if expanded and has children),
 *   or moves selection to the parent item.
 *
 * All other keys are forwarded to BListView::KeyDown().
 *
 * @param bytes    Pointer to the raw key bytes.
 * @param numBytes Number of bytes in \a bytes.
 */
void
BOutlineListView::KeyDown(const char* bytes, int32 numBytes)
{
	if (numBytes == 1) {
		int32 currentSel = CurrentSelection();
		switch (bytes[0]) {
			case B_RIGHT_ARROW:
			{
				BListItem* item = ItemAt(currentSel);
				if (item && item->fHasSubitems) {
					if (!item->IsExpanded())
						Expand(item);
					else {
						Select(currentSel + 1);
						ScrollToSelection();
					}
				}
				return;
			}

			case B_LEFT_ARROW:
			{
				BListItem* item = ItemAt(currentSel);
				if (item) {
					if (item->fHasSubitems && item->IsExpanded())
						Collapse(item);
					else {
						item = Superitem(item);
						if (item) {
							Select(IndexOf(item));
							ScrollToSelection();
						}
					}
				}
				return;
			}
		}
	}

	BListView::KeyDown(bytes, numBytes);
}


/**
 * @brief Notifies the view that its frame has been moved.
 *
 * Delegates to BListView::FrameMoved() to handle any scroll-bar adjustments
 * or child view updates required after the view origin changes.
 *
 * @param newPosition The new top-left corner of the view in the parent's
 *                    coordinate system.
 */
void
BOutlineListView::FrameMoved(BPoint newPosition)
{
	BListView::FrameMoved(newPosition);
}


/**
 * @brief Notifies the view that its frame has been resized.
 *
 * Delegates to BListView::FrameResized() to recalculate scroll ranges and
 * repaint as necessary after a size change.
 *
 * @param newWidth  The new width of the view in pixels.
 * @param newHeight The new height of the view in pixels.
 */
void
BOutlineListView::FrameResized(float newWidth, float newHeight)
{
	BListView::FrameResized(newWidth, newHeight);
}


/**
 * @brief Handles mouse-button release events.
 *
 * Delegates directly to BListView::MouseUp() for standard selection
 * finalisation behaviour.
 *
 * @param where The release position in the view's coordinate system.
 */
void
BOutlineListView::MouseUp(BPoint where)
{
	BListView::MouseUp(where);
}


/**
 * @brief Inserts \a item as a direct child of \a superItem.
 *
 * The new item is placed immediately after \a superItem in the full list and
 * its outline level is set to one greater than \a superItem's level. If
 * \a superItem is NULL the item is appended at the top level via AddItem().
 * The item is made visible only if \a superItem is both visible and expanded.
 *
 * @param item      The item to insert.
 * @param superItem The parent item, or NULL to insert at the root level.
 * @return true if the item was successfully added, false otherwise.
 */
bool
BOutlineListView::AddUnder(BListItem* item, BListItem* superItem)
{
	if (superItem == NULL)
		return AddItem(item);

	fFullList.AddItem(item, FullListIndexOf(superItem) + 1);

	item->fLevel = superItem->OutlineLevel() + 1;
	superItem->fHasSubitems = true;

	if (superItem->IsItemVisible() && superItem->IsExpanded()) {
		item->SetItemVisible(true);

		int32 index = BListView::IndexOf(superItem);

		BListView::AddItem(item, index + 1);
		Invalidate(LatchRect(ItemFrame(index), superItem->OutlineLevel()));
	} else
		item->SetItemVisible(false);

	return true;
}


/**
 * @brief Appends \a item at the end of the full list.
 *
 * This is a convenience overload that calls AddItem(item, FullListCountItems())
 * so the item is inserted after all existing items regardless of level.
 *
 * @param item The item to append.
 * @return true on success, false on failure.
 */
bool
BOutlineListView::AddItem(BListItem* item)
{
	return AddItem(item, FullListCountItems());
}


/**
 * @brief Inserts \a item at position \a fullListIndex in the full list.
 *
 * The method clamps \a fullListIndex to the valid range, adds the item to the
 * full list, determines whether the item should be visible based on its parent's
 * state, and conditionally inserts it into BListView's display list.
 *
 * @param item          The item to insert.
 * @param fullListIndex The desired position in the full (unfiltered) list.
 * @return true on success, false if either the full-list or display-list
 *         insertion fails.
 */
bool
BOutlineListView::AddItem(BListItem* item, int32 fullListIndex)
{
	if (fullListIndex < 0)
		fullListIndex = 0;
	else if (fullListIndex > FullListCountItems())
		fullListIndex = FullListCountItems();

	if (!fFullList.AddItem(item, fullListIndex))
		return false;

	// Check if this item is visible, and if it is, add it to the
	// other list, too

	if (item->fLevel > 0) {
		BListItem* super = _SuperitemForIndex(fullListIndex, item->fLevel);
		if (super == NULL)
			return true;

		bool hadSubitems = super->fHasSubitems;
		super->fHasSubitems = true;

		if (!super->IsItemVisible() || !super->IsExpanded()) {
			item->SetItemVisible(false);
			return true;
		}

		if (!hadSubitems) {
			Invalidate(LatchRect(ItemFrame(IndexOf(super)),
				super->OutlineLevel()));
		}
	}

	int32 listIndex = _FindPreviousVisibleIndex(fullListIndex);

	if (!BListView::AddItem(item, IndexOf(FullListItemAt(listIndex)) + 1)) {
		// adding didn't work out, we need to remove it from the main list again
		fFullList.RemoveItem(fullListIndex);
		return false;
	}

	return true;
}


/**
 * @brief Appends all items in \a newItems to the end of the full list.
 *
 * Convenience overload that calls AddList(newItems, FullListCountItems()).
 *
 * @param newItems The list of BListItem pointers to append.
 * @return true if at least one item was added, false if \a newItems is NULL
 *         or empty.
 */
bool
BOutlineListView::AddList(BList* newItems)
{
	return AddList(newItems, FullListCountItems());
}


/**
 * @brief Inserts all items from \a newItems starting at \a fullListIndex.
 *
 * Each item is inserted in order using AddItem() so that parent/child
 * relationships and visibility states are maintained correctly.
 *
 * @param newItems      The list of BListItem pointers to insert.
 * @param fullListIndex Starting position in the full list for the first item.
 * @return true if items were added, false if \a newItems is NULL or empty.
 */
bool
BOutlineListView::AddList(BList* newItems, int32 fullListIndex)
{
	if ((newItems == NULL) || (newItems->CountItems() == 0))
		return false;

	for (int32 i = 0; i < newItems->CountItems(); i++)
		AddItem((BListItem*)newItems->ItemAt(i), fullListIndex + i);

	return true;
}


/**
 * @brief Removes \a item and all of its descendants from the list.
 *
 * Looks up the item's full-list index and delegates to _RemoveItem().
 *
 * @param item The item to remove.
 * @return true if the item was found and removed, false otherwise.
 */
bool
BOutlineListView::RemoveItem(BListItem* item)
{
	return _RemoveItem(item, FullListIndexOf(item)) != NULL;
}


/**
 * @brief Removes the item at \a fullListIndex from the full list.
 *
 * Delegates to _RemoveItem() and returns the removed item so the caller
 * can delete it if desired.
 *
 * @param fullListIndex Position of the item to remove in the full list.
 * @return The removed BListItem, or NULL if the index is out of range.
 */
BListItem*
BOutlineListView::RemoveItem(int32 fullListIndex)
{
	return _RemoveItem(FullListItemAt(fullListIndex), fullListIndex);
}


/**
 * @brief Removes \a count consecutive items starting at \a fullListIndex.
 *
 * Items are removed one at a time from the same logical position so that
 * child items shifted up by each removal are handled correctly.
 *
 * @param fullListIndex Starting position in the full list.
 * @param count         Number of items to remove.
 * @return true if the starting index was valid, false otherwise.
 * @note This implementation is O(count * n) and may be slow for large lists.
 */
bool
BOutlineListView::RemoveItems(int32 fullListIndex, int32 count)
{
	if (fullListIndex >= FullListCountItems())
		fullListIndex = -1;
	if (fullListIndex < 0)
		return false;

	// TODO: very bad for performance!!
	while (count--)
		BOutlineListView::RemoveItem(fullListIndex);

	return true;
}


/**
 * @brief Returns the item at \a fullListIndex in the unfiltered full list.
 *
 * Unlike ItemAt(), this method can return items that are currently hidden
 * because their parent is collapsed.
 *
 * @param fullListIndex Zero-based index into the full item list.
 * @return The BListItem at that index, or NULL if out of range.
 */
BListItem*
BOutlineListView::FullListItemAt(int32 fullListIndex) const
{
	return (BListItem*)fFullList.ItemAt(fullListIndex);
}


/**
 * @brief Returns the full-list index of the item at the given view point.
 *
 * Converts the visible display index (obtained via BListView::IndexOf()) to
 * the corresponding position in the full item list.
 *
 * @param where A point in the view's coordinate system.
 * @return The full-list index of the item under \a where, or -1 if none.
 */
int32
BOutlineListView::FullListIndexOf(BPoint where) const
{
	int32 index = BListView::IndexOf(where);

	if (index > 0)
		index = _FullListIndex(index);

	return index;
}


/**
 * @brief Returns the position of \a item in the full (unfiltered) list.
 *
 * @param item The item to locate.
 * @return The zero-based full-list index, or -1 if the item is not found.
 */
int32
BOutlineListView::FullListIndexOf(BListItem* item) const
{
	return fFullList.IndexOf(item);
}


/**
 * @brief Returns the first item in the full list.
 *
 * @return The first BListItem, or NULL if the list is empty.
 */
BListItem*
BOutlineListView::FullListFirstItem() const
{
	return (BListItem*)fFullList.FirstItem();
}


/**
 * @brief Returns the last item in the full list.
 *
 * @return The last BListItem, or NULL if the list is empty.
 */
BListItem*
BOutlineListView::FullListLastItem() const
{
	return (BListItem*)fFullList.LastItem();
}


/**
 * @brief Returns whether \a item is present in the full list.
 *
 * @param item The item to search for.
 * @return true if found, false otherwise.
 */
bool
BOutlineListView::FullListHasItem(BListItem* item) const
{
	return fFullList.HasItem(item);
}


/**
 * @brief Returns the total number of items in the full list.
 *
 * This count includes items that are currently hidden due to collapsed
 * parents, unlike CountItems() which returns only visible items.
 *
 * @return The number of items in the full list.
 */
int32
BOutlineListView::FullListCountItems() const
{
	return fFullList.CountItems();
}


/**
 * @brief Returns the full-list index of the nth selected item.
 *
 * Retrieves the nth selected item from BListView's selection and maps its
 * display index back to the full list.
 *
 * @param index Zero-based rank of the selected item to query (default 0).
 * @return The full-list index of the nth selected item, or -1 if none.
 */
int32
BOutlineListView::FullListCurrentSelection(int32 index) const
{
	int32 i = BListView::CurrentSelection(index);

	BListItem* item = BListView::ItemAt(i);
	if (item)
		return fFullList.IndexOf(item);

	return -1;
}


/**
 * @brief Removes all items from both the full list and the display list.
 */
void
BOutlineListView::MakeEmpty()
{
	fFullList.MakeEmpty();
	BListView::MakeEmpty();
}


/**
 * @brief Returns whether the full list contains no items.
 *
 * @return true if the full list is empty, false otherwise.
 */
bool
BOutlineListView::FullListIsEmpty() const
{
	return fFullList.IsEmpty();
}


/**
 * @brief Calls \a func for every item in the full list.
 *
 * Iteration stops early if \a func returns true for any item.
 *
 * @param func The callback to invoke. Receives each BListItem* in turn.
 */
void
BOutlineListView::FullListDoForEach(bool(*func)(BListItem* item))
{
	fFullList.DoForEach(reinterpret_cast<bool (*)(void*)>(func));
}


/**
 * @brief Calls \a func with an extra argument for every item in the full list.
 *
 * Iteration stops early if \a func returns true for any item.
 *
 * @param func The callback to invoke. Receives each BListItem* and \a arg.
 * @param arg  An arbitrary pointer passed through to \a func.
 */
void
BOutlineListView::FullListDoForEach(bool (*func)(BListItem* item, void* arg),
	void* arg)
{
	fFullList.DoForEach(reinterpret_cast<bool (*)(void*, void*)>(func), arg);
}


/**
 * @brief Returns the parent item of \a item, or NULL if it is a root item.
 *
 * Looks up the item's full-list index and walks backwards to find the first
 * ancestor at a strictly lower outline level.
 *
 * @param item The item whose parent is requested.
 * @return The parent BListItem, or NULL if \a item is at the root level or
 *         not found in the list.
 */
BListItem*
BOutlineListView::Superitem(const BListItem* item)
{
	int32 index = FullListIndexOf((BListItem*)item);
	if (index == -1)
		return NULL;

	return _SuperitemForIndex(index, item->OutlineLevel());
}


/**
 * @brief Expands \a item, making its immediate children visible.
 *
 * If the item is already expanded or has no children this call has no effect.
 * Delegates to ExpandOrCollapse().
 *
 * @param item The item to expand.
 * @see Collapse(), IsExpanded()
 */
void
BOutlineListView::Expand(BListItem* item)
{
	ExpandOrCollapse(item, true);
}


/**
 * @brief Collapses \a item, hiding all of its descendants.
 *
 * If the item is already collapsed or has no children this call has no effect.
 * Delegates to ExpandOrCollapse().
 *
 * @param item The item to collapse.
 * @see Expand(), IsExpanded()
 */
void
BOutlineListView::Collapse(BListItem* item)
{
	ExpandOrCollapse(item, false);
}


/**
 * @brief Returns whether the item at \a fullListIndex is currently expanded.
 *
 * @param fullListIndex Zero-based index into the full list.
 * @return true if the item exists and is expanded, false otherwise.
 */
bool
BOutlineListView::IsExpanded(int32 fullListIndex)
{
	BListItem* item = FullListItemAt(fullListIndex);
	if (!item)
		return false;

	return item->IsExpanded();
}


/**
 * @brief Resolves a scripting specifier to the appropriate handler.
 *
 * Delegates to BListView::ResolveSpecifier() unchanged.
 *
 * @param message   The scripting message.
 * @param index     The specifier index.
 * @param specifier The specifier message.
 * @param what      The specifier type constant.
 * @param property  The name of the property being targeted.
 * @return The BHandler that should process the scripting message.
 */
BHandler*
BOutlineListView::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 what, const char* property)
{
	return BListView::ResolveSpecifier(message, index, specifier, what,
		property);
}


/**
 * @brief Fills \a data with the scripting suites supported by this view.
 *
 * Delegates to BListView::GetSupportedSuites() unchanged.
 *
 * @param data The message to populate with suite information.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BOutlineListView::GetSupportedSuites(BMessage* data)
{
	return BListView::GetSupportedSuites(data);
}


/**
 * @brief Dispatches binary-compatibility perform codes for layout hooks.
 *
 * Handles the set of PERFORM_CODE_* constants introduced for layout
 * compatibility (MinSize, MaxSize, PreferredSize, LayoutAlignment,
 * HasHeightForWidth, GetHeightForWidth, SetLayout, LayoutInvalidated,
 * DoLayout). Unknown codes are forwarded to BListView::Perform().
 *
 * @param code  The perform code identifying the operation.
 * @param _data Pointer to a code-specific data structure.
 * @return B_OK if the code was handled, otherwise the result from
 *         BListView::Perform().
 */
status_t
BOutlineListView::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_MIN_SIZE:
			((perform_data_min_size*)_data)->return_value
				= BOutlineListView::MinSize();
			return B_OK;
		case PERFORM_CODE_MAX_SIZE:
			((perform_data_max_size*)_data)->return_value
				= BOutlineListView::MaxSize();
			return B_OK;
		case PERFORM_CODE_PREFERRED_SIZE:
			((perform_data_preferred_size*)_data)->return_value
				= BOutlineListView::PreferredSize();
			return B_OK;
		case PERFORM_CODE_LAYOUT_ALIGNMENT:
			((perform_data_layout_alignment*)_data)->return_value
				= BOutlineListView::LayoutAlignment();
			return B_OK;
		case PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH:
			((perform_data_has_height_for_width*)_data)->return_value
				= BOutlineListView::HasHeightForWidth();
			return B_OK;
		case PERFORM_CODE_GET_HEIGHT_FOR_WIDTH:
		{
			perform_data_get_height_for_width* data
				= (perform_data_get_height_for_width*)_data;
			BOutlineListView::GetHeightForWidth(data->width, &data->min,
				&data->max, &data->preferred);
			return B_OK;
		}
		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BOutlineListView::SetLayout(data->layout);
			return B_OK;
		}
		case PERFORM_CODE_LAYOUT_INVALIDATED:
		{
			perform_data_layout_invalidated* data
				= (perform_data_layout_invalidated*)_data;
			BOutlineListView::LayoutInvalidated(data->descendants);
			return B_OK;
		}
		case PERFORM_CODE_DO_LAYOUT:
		{
			BOutlineListView::DoLayout();
			return B_OK;
		}
	}

	return BListView::Perform(code, _data);
}


/**
 * @brief Resizes the view to its preferred dimensions.
 *
 * Delegates to BListView::ResizeToPreferred().
 */
void
BOutlineListView::ResizeToPreferred()
{
	BListView::ResizeToPreferred();
}


/**
 * @brief Calculates and returns the preferred width and height of the view.
 *
 * The preferred width is the maximum of all item widths after accounting for
 * each item's outline level indent and the space reserved for the latch. The
 * preferred height is the bottom edge of the last item.
 *
 * @param[out] _width  Set to the preferred width, or left unchanged if NULL.
 * @param[out] _height Set to the preferred height, or left unchanged if NULL.
 */
void
BOutlineListView::GetPreferredSize(float* _width, float* _height)
{
	int32 count = CountItems();

	if (count > 0) {
		float maxWidth = 0.0;
		for (int32 i = 0; i < count; i++) {
			// The item itself does not take his OutlineLevel into account, so
			// we must make up for that. Also add space for the latch.
			float itemWidth = ItemAt(i)->Width() + be_plain_font->Size()
				+ (ItemAt(i)->OutlineLevel() + 1)
					* be_control_look->DefaultItemSpacing();
			if (itemWidth > maxWidth)
				maxWidth = itemWidth;
		}

		if (_width != NULL)
			*_width = maxWidth;
		if (_height != NULL)
			*_height = ItemAt(count - 1)->Bottom();
	} else
		BView::GetPreferredSize(_width, _height);
}


/**
 * @brief Sets or removes the keyboard focus from the view.
 *
 * Delegates to BListView::MakeFocus() for standard focus handling and
 * redraw.
 *
 * @param state true to acquire focus, false to release it.
 */
void
BOutlineListView::MakeFocus(bool state)
{
	BListView::MakeFocus(state);
}


/**
 * @brief Called after all children of the view have been attached to the window.
 *
 * Delegates to BListView::AllAttached().
 */
void
BOutlineListView::AllAttached()
{
	BListView::AllAttached();
}


/**
 * @brief Called before all children of the view are detached from the window.
 *
 * Delegates to BListView::AllDetached().
 */
void
BOutlineListView::AllDetached()
{
	BListView::AllDetached();
}


/**
 * @brief Called when the view is removed from a window's view hierarchy.
 *
 * Delegates to BListView::DetachedFromWindow().
 */
void
BOutlineListView::DetachedFromWindow()
{
	BListView::DetachedFromWindow();
}


/**
 * @brief Sorts all items in the full list using \a compareFunc.
 *
 * Convenience wrapper that delegates to SortItemsUnder() with a NULL super
 * item and oneLevelOnly set to false, which sorts the entire tree recursively.
 *
 * @param compareFunc The comparison function used to order items.
 * @see SortItemsUnder()
 */
void
BOutlineListView::FullListSortItems(int (*compareFunc)(const BListItem* a,
	const BListItem* b))
{
	SortItemsUnder(NULL, false, compareFunc);
}


/**
 * @brief Sorts the children of \a superItem using \a compareFunc.
 *
 * Constructs an in-memory tree from the relevant portion of the full list,
 * sorts each level as requested, then writes the sorted order back to both
 * the full list and BListView's display list, invalidating the changed region.
 *
 * @param superItem    The parent item whose subtree is sorted, or NULL to sort
 *                     the entire list from the root.
 * @param oneLevelOnly If true, only the direct children of \a superItem are
 *                     reordered; grandchildren keep their relative order.
 * @param compareFunc  The comparison function used to order items at each level.
 */
void
BOutlineListView::SortItemsUnder(BListItem* superItem, bool oneLevelOnly,
	int (*compareFunc)(const BListItem* a, const BListItem* b))
{
	// This method is quite complicated: basically, it creates a real tree
	// from the items of the full list, sorts them as needed, and then
	// populates the entries back into the full and display lists

	int32 firstIndex = FullListIndexOf(superItem) + 1;
	int32 lastIndex = firstIndex;
	BList* tree = _BuildTree(superItem, lastIndex);

	_SortTree(tree, oneLevelOnly, compareFunc);

	// Populate to the full list
	_PopulateTree(tree, fFullList, firstIndex, false);

	if (superItem == NULL
		|| (superItem->IsItemVisible() && superItem->IsExpanded())) {
		// Populate to BListView's list
		firstIndex = fList.IndexOf(superItem) + 1;
		lastIndex = firstIndex;
		_PopulateTree(tree, fList, lastIndex, true);

		if (fFirstSelected != -1) {
			// update selection hints
			fFirstSelected = _CalcFirstSelected(0);
			fLastSelected = _CalcLastSelected(CountItems());
		}

		// only invalidate what may have changed
		_RecalcItemTops(firstIndex);
		BRect top = ItemFrame(firstIndex);
		BRect bottom = ItemFrame(lastIndex - 1);
		BRect update(top.left, top.top, bottom.right, bottom.bottom);
		Invalidate(update);
	}

	_DestructTree(tree);
}


/**
 * @brief Returns the number of items that are direct or indirect children of
 *        \a superItem.
 *
 * When \a oneLevelOnly is true only direct children (outline level equal to
 * superItem's level + 1) are counted. When false, all descendants regardless
 * of depth are counted.
 *
 * @param superItem   The parent item, or NULL to count from the root.
 * @param oneLevelOnly If true, count only direct children; if false, count all
 *                     descendants.
 * @return The number of matching items, or 0 if \a superItem is not found.
 */
int32
BOutlineListView::CountItemsUnder(BListItem* superItem, bool oneLevelOnly) const
{
	int32 i = 0;
	uint32 baseLevel = 0;
	if (_ItemsUnderSetup(superItem, i, baseLevel) != B_OK)
		return 0;

	int32 count = 0;
	for (; i < FullListCountItems(); i++) {
		BListItem* item = FullListItemAt(i);

		// If we jump out of the subtree, return count
		if (item->fLevel < baseLevel)
			return count;

		// If the level matches, increase count
		if (!oneLevelOnly || item->fLevel == baseLevel)
			count++;
	}

	return count;
}


/**
 * @brief Calls \a eachFunc on each child item of \a superItem in order.
 *
 * Iterates over the children of \a superItem (or all root items if
 * \a superItem is NULL) and invokes \a eachFunc on each matching item.
 * Iteration stops when \a eachFunc returns a non-NULL pointer, which is then
 * returned as the result.
 *
 * @param superItem   The parent item, or NULL to iterate from the root.
 * @param oneLevelOnly If true, visit only direct children; if false, visit all
 *                     descendants.
 * @param eachFunc    Callback invoked for each item; return non-NULL to stop.
 * @param arg         Arbitrary pointer passed through to \a eachFunc.
 * @return The first non-NULL value returned by \a eachFunc, or NULL if
 *         \a eachFunc never returned non-NULL.
 */
BListItem*
BOutlineListView::EachItemUnder(BListItem* superItem, bool oneLevelOnly,
	BListItem* (*eachFunc)(BListItem* item, void* arg), void* arg)
{
	int32 i = 0;
	uint32 baseLevel = 0;
	if (_ItemsUnderSetup(superItem, i, baseLevel) != B_OK)
		return NULL;

	while (i < FullListCountItems()) {
		BListItem* item = FullListItemAt(i);

		// If we jump out of the subtree, return NULL
		if (item->fLevel < baseLevel)
			return NULL;

		// If the level matches, check the index
		if (!oneLevelOnly || item->fLevel == baseLevel) {
			item = eachFunc(item, arg);
			if (item != NULL)
				return item;
		}

		i++;
	}

	return NULL;
}


/**
 * @brief Returns the nth child item of \a superItem.
 *
 * When \a oneLevelOnly is true, \a index counts only direct children;
 * otherwise it counts all descendants in depth-first order.
 *
 * @param superItem   The parent item, or NULL to index from the root.
 * @param oneLevelOnly If true, count only direct children; if false, count all
 *                     descendants.
 * @param index       Zero-based rank of the desired child item.
 * @return The item at position \a index, or NULL if out of range.
 */
BListItem*
BOutlineListView::ItemUnderAt(BListItem* superItem, bool oneLevelOnly,
	int32 index) const
{
	int32 i = 0;
	uint32 baseLevel = 0;
	if (_ItemsUnderSetup(superItem, i, baseLevel) != B_OK)
		return NULL;

	while (i < FullListCountItems()) {
		BListItem* item = FullListItemAt(i);

		// If we jump out of the subtree, return NULL
		if (item->fLevel < baseLevel)
			return NULL;

		// If the level matches, check the index
		if (!oneLevelOnly || item->fLevel == baseLevel) {
			if (index == 0)
				return item;

			index--;
		}

		i++;
	}

	return NULL;
}


/**
 * @brief Dispatches miscellaneous list operations, currently handling item swaps.
 *
 * When \a code is B_SWAP_OP the two visible display-list indices stored in
 * \a data are swapped (including their subtrees) via _SwapItems(). All other
 * codes are forwarded to BListView::DoMiscellaneous().
 *
 * @param code The operation code.
 * @param data Pointer to the operation-specific data union.
 * @return true if the operation succeeded, false otherwise.
 */
bool
BOutlineListView::DoMiscellaneous(MiscCode code, MiscData* data)
{
	if (code == B_SWAP_OP)
		return _SwapItems(data->swap.a, data->swap.b);

	return BListView::DoMiscellaneous(code, data);
}


/**
 * @brief Handles incoming BMessages; delegates to BListView::MessageReceived().
 *
 * @param msg The message to process.
 */
void
BOutlineListView::MessageReceived(BMessage* msg)
{
	BListView::MessageReceived(msg);
}


void BOutlineListView::_ReservedOutlineListView1() {}
void BOutlineListView::_ReservedOutlineListView2() {}
void BOutlineListView::_ReservedOutlineListView3() {}
void BOutlineListView::_ReservedOutlineListView4() {}


/**
 * @brief Expands or collapses \a item and updates the display list accordingly.
 *
 * When expanding, all immediate children (and recursively their expanded
 * descendants) are inserted into the display list and their visibility flag
 * is set. When collapsing, all visible descendants are removed from the
 * display list, deselected, and their visibility flag is cleared. In both
 * cases the scroll bar range and selection hint indices are updated.
 *
 * @param item   The item to expand or collapse.
 * @param expand true to expand, false to collapse.
 * @see Expand(), Collapse()
 */
void
BOutlineListView::ExpandOrCollapse(BListItem* item, bool expand)
{
	if (item->IsExpanded() == expand || !FullListHasItem(item))
		return;

	item->fExpanded = expand;

	// TODO: merge these cases together, they are pretty similar

	if (expand) {
		uint32 level = item->fLevel;
		int32 fullListIndex = FullListIndexOf(item);
		int32 index = IndexOf(item) + 1;
		int32 startIndex = index;
		int32 count = FullListCountItems() - fullListIndex - 1;
		BListItem** items = (BListItem**)fFullList.Items() + fullListIndex + 1;

		BFont font;
		GetFont(&font);
		while (count-- > 0) {
			item = items[0];
			if (item->fLevel <= level)
				break;

			if (!item->IsItemVisible()) {
				// fix selection hints
				if (index <= fFirstSelected)
					fFirstSelected++;
				if (index <= fLastSelected)
					fLastSelected++;

				fList.AddItem(item, index++);
				item->Update(this, &font);
				item->SetItemVisible(true);
			}

			if (item->HasSubitems() && !item->IsExpanded()) {
				// Skip hidden children
				uint32 subLevel = item->fLevel;
				items++;

				while (count > 0 && items[0]->fLevel > subLevel) {
					items++;
					count--;
				}
			} else
				items++;
		}
		_RecalcItemTops(startIndex);
	} else {
		// collapse
		const uint32 level = item->fLevel;
		const int32 fullListIndex = FullListIndexOf(item);
		const int32 index = IndexOf(item);
		int32 max = FullListCountItems() - fullListIndex - 1;
		int32 count = 0;
		bool selectionChanged = false;

		BListItem** items = (BListItem**)fFullList.Items() + fullListIndex + 1;

		while (max-- > 0) {
			item = items[0];
			if (item->fLevel <= level)
				break;

			if (item->IsItemVisible()) {
				fList.RemoveItem(item);
				item->SetItemVisible(false);
				if (item->IsSelected()) {
					selectionChanged = true;
					item->Deselect();
				}
				count++;
			}

			items++;
		}

		_RecalcItemTops(index);
		// fix selection hints
		// if the selected item was just removed by collapsing, select its
		// parent
		if (selectionChanged) {
			if (fFirstSelected > index && fFirstSelected <= index + count) {
					fFirstSelected = index;
			}
			if (fLastSelected > index && fLastSelected <= index + count) {
				fLastSelected = index;
			}
		}
		if (index + count < fFirstSelected) {
				// all items removed were higher than the selection range,
				// adjust the indexes to correspond to their new visible positions
				fFirstSelected -= count;
				fLastSelected -= count;
		}

		int32 maxIndex = fList.CountItems() - 1;
		if (fFirstSelected > maxIndex)
			fFirstSelected = maxIndex;

		if (fLastSelected > maxIndex)
			fLastSelected = maxIndex;

		if (selectionChanged)
			Select(fFirstSelected, fLastSelected);
	}

	_FixupScrollBar();
	Invalidate();
}


/**
 * @brief Calculates the bounding rectangle of the latch triangle for an item.
 *
 * The latch is a small arrow drawn to the left of the item text, indented by
 * the item's outline level. Its size is derived from the plain font size.
 *
 * @param itemRect The bounding rectangle of the item row.
 * @param level    The outline nesting level of the item.
 * @return The bounding rectangle of the latch in view coordinates.
 */
BRect
BOutlineListView::LatchRect(BRect itemRect, int32 level) const
{
	float latchWidth = be_plain_font->Size();
	float latchHeight = be_plain_font->Size();
	float indentOffset = level * be_control_look->DefaultItemSpacing();
	float heightOffset = itemRect.Height() / 2 - latchHeight / 2;

	return BRect(0, 0, latchWidth, latchHeight)
		.OffsetBySelf(itemRect.left, itemRect.top)
		.OffsetBySelf(indentOffset, heightOffset);
}


/**
 * @brief Draws the expand/collapse latch (arrow) for a list item.
 *
 * The arrow points right when the item is collapsed and down when expanded.
 * The tint is adjusted so it remains visible on both light and dark panel
 * backgrounds.
 *
 * @param itemRect    The full bounding rectangle of the item row.
 * @param level       The outline nesting level used to position the latch.
 * @param collapsed   true if the item is currently collapsed (arrow points right).
 * @param highlighted true if the row is selected or being fully redrawn.
 * @param misTracked  true if the mouse is outside the latch during tracking
 *                    (reserved for future visual feedback; currently unused).
 */
void
BOutlineListView::DrawLatch(BRect itemRect, int32 level, bool collapsed,
	bool highlighted, bool misTracked)
{
	BRect latchRect(LatchRect(itemRect, level));
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	int32 arrowDirection = collapsed ? BControlLook::B_RIGHT_ARROW
		: BControlLook::B_DOWN_ARROW;

	float tintColor = B_DARKEN_4_TINT;
	if (base.red + base.green + base.blue <= 128 * 3) {
		tintColor = B_LIGHTEN_2_TINT;
	}

	be_control_look->DrawArrowShape(this, latchRect, itemRect, base,
		arrowDirection, 0, tintColor);
}


/**
 * @brief Draws a single list item, prefixed by its latch if it has children.
 *
 * If the item has sub-items, DrawLatch() is called first and then
 * \a itemRect is narrowed by the latch width before forwarding to
 * BListView::DrawItem().
 *
 * @param item     The item to draw.
 * @param itemRect The bounding rectangle in which the item should be drawn.
 * @param complete If true, the background must be fully repainted.
 */
void
BOutlineListView::DrawItem(BListItem* item, BRect itemRect, bool complete)
{
	if (item->fHasSubitems) {
		DrawLatch(itemRect, item->fLevel, !item->IsExpanded(),
			item->IsSelected() || complete, false);
	}

	itemRect.left += LatchRect(itemRect, item->fLevel).right;
	BListView::DrawItem(item, itemRect, complete);
}


/**
 * @brief Maps a visible display-list index to its position in the full list.
 *
 * @param index Zero-based index in the visible display list.
 * @return The corresponding full-list index, or -1 if \a index is out of range.
 */
int32
BOutlineListView::_FullListIndex(int32 index) const
{
	BListItem* item = ItemAt(index);

	if (item == NULL)
		return -1;

	return FullListIndexOf(item);
}


/**
 * @brief Writes the items in \a tree back into \a target in depth-first order.
 *
 * Used by SortItemsUnder() to flush the sorted in-memory tree structure into
 * either the full list or the display list. When \a onlyVisible is true,
 * collapsed items' subtrees are not descended.
 *
 * @param tree        The source tree (a BList of BListItem pointers, where each
 *                    item may have a fTemporaryList subtree).
 * @param target      The flat destination list to write into.
 * @param firstIndex  Current write position in \a target; updated in place.
 * @param onlyVisible If true, skip subtrees of collapsed items.
 */
void
BOutlineListView::_PopulateTree(BList* tree, BList& target,
	int32& firstIndex, bool onlyVisible)
{
	BListItem** items = (BListItem**)target.Items();
	int32 count = tree->CountItems();

	for (int32 index = 0; index < count; index++) {
		BListItem* item = (BListItem*)tree->ItemAtFast(index);

		items[firstIndex++] = item;

		if (item->HasSubitems() && (!onlyVisible || item->IsExpanded())) {
			_PopulateTree(item->fTemporaryList, target, firstIndex,
				onlyVisible);
		}
	}
}


/**
 * @brief Recursively sorts the items in \a tree using \a compareFunc.
 *
 * The items at the current level of \a tree are sorted with std::sort.
 * If \a oneLevelOnly is false the method recurses into the subtrees of each
 * item that has children.
 *
 * @param tree        The tree level to sort (a BList of BListItem pointers).
 * @param oneLevelOnly If true, only this level is sorted; subtrees are left in
 *                     their original order.
 * @param compareFunc  The comparison function passed to ListItemComparator.
 */
void
BOutlineListView::_SortTree(BList* tree, bool oneLevelOnly,
	int (*compareFunc)(const BListItem* a, const BListItem* b))
{
	BListItem** items = (BListItem**)tree->Items();
	std::sort(items, items + tree->CountItems(),
		ListItemComparator(compareFunc));

	if (oneLevelOnly)
		return;

	for (int32 index = tree->CountItems(); index-- > 0;) {
		BListItem* item = (BListItem*)tree->ItemAt(index);

		if (item->HasSubitems())
			_SortTree(item->fTemporaryList, false, compareFunc);
	}
}


/**
 * @brief Recursively frees the temporary BList objects attached to each item
 *        in \a tree and deletes the tree itself.
 *
 * Used by SortItemsUnder() after _PopulateTree() has written the sorted
 * results back into the flat lists.
 *
 * @param tree The tree root (BList*) to destruct; must not be NULL.
 */
void
BOutlineListView::_DestructTree(BList* tree)
{
	for (int32 index = tree->CountItems(); index-- > 0;) {
		BListItem* item = (BListItem*)tree->ItemAt(index);

		if (item->HasSubitems())
			_DestructTree(item->fTemporaryList);
	}

	delete tree;
}


/**
 * @brief Builds an in-memory tree from a contiguous range of the full list.
 *
 * Starting at \a fullListIndex, items are read until an item whose outline
 * level is less than the expected level for \a superItem's children is
 * encountered. Each item that itself has children is recursively expanded into
 * its own subtree stored in the item's fTemporaryList field.
 *
 * @param superItem      The parent item whose children are collected, or NULL to
 *                       build a tree of all root-level items.
 * @param fullListIndex  On entry, the starting index in the full list; updated
 *                       on return to point past the last item consumed.
 * @return A newly allocated BList representing this level of the tree.
 *         Ownership is transferred to the caller (use _DestructTree() to free).
 */
BList*
BOutlineListView::_BuildTree(BListItem* superItem, int32& fullListIndex)
{
	int32 fullCount = FullListCountItems();
	uint32 level = superItem != NULL ? superItem->OutlineLevel() + 1 : 0;
	BList* list = new BList;
	if (superItem != NULL)
		superItem->fTemporaryList = list;

	while (fullListIndex < fullCount) {
		BListItem* item = FullListItemAt(fullListIndex);

		// If we jump out of the subtree, break out
		if (item->fLevel < level)
			break;

		// If the level matches, put them into the list
		// (we handle the case of a missing sublevel gracefully)
		list->AddItem(item);
		fullListIndex++;

		if (item->HasSubitems()) {
			// we're going deeper
			_BuildTree(item, fullListIndex);
		}
	}

	return list;
}


/**
 * @brief Removes all invisible items from \a list in place.
 *
 * Used by _SwapItems() to strip hidden items from a subtree copy before
 * operating on the visible display list.
 *
 * @param list The flat list to filter; modified in place.
 */
void
BOutlineListView::_CullInvisibleItems(BList& list)
{
	int32 index = 0;
	while (index < list.CountItems()) {
		if (reinterpret_cast<BListItem*>(list.ItemAt(index))->IsItemVisible())
			++index;
		else
			list.RemoveItem(index);
	}
}


/**
 * @brief Swaps the two visible items at display-list positions \a first and
 *        \a second, together with their respective subtrees.
 *
 * Both items must share the same parent and must currently be visible. The
 * swap is performed on both the full list and the display list. Selection
 * hints and item top positions are recalculated afterwards and the view is
 * fully invalidated.
 *
 * @param first  Display-list index of the first item to swap.
 * @param second Display-list index of the second item to swap.
 * @return true on success, false if either index is out of range, the items
 *         have different parents, or either item is invisible.
 */
bool
BOutlineListView::_SwapItems(int32 first, int32 second)
{
	// same item, do nothing
	if (first == second)
		return true;

	// fail, first item out of bounds
	if ((first < 0) || (first >= CountItems()))
		return false;

	// fail, second item out of bounds
	if ((second < 0) || (second >= CountItems()))
		return false;

	int32 firstIndex = min_c(first, second);
	int32 secondIndex = max_c(first, second);
	BListItem* firstItem = ItemAt(firstIndex);
	BListItem* secondItem = ItemAt(secondIndex);
	BList firstSubItems, secondSubItems;

	if (Superitem(firstItem) != Superitem(secondItem))
		return false;

	if (!firstItem->IsItemVisible() || !secondItem->IsItemVisible())
		return false;

	int32 fullFirstIndex = _FullListIndex(firstIndex);
	int32 fullSecondIndex = _FullListIndex(secondIndex);
	_GetSubItems(fFullList, firstSubItems, firstItem, fullFirstIndex + 1);
	_GetSubItems(fFullList, secondSubItems, secondItem, fullSecondIndex + 1);
	_DoSwap(fFullList, fullFirstIndex, fullSecondIndex, &firstSubItems,
		&secondSubItems);

	_CullInvisibleItems(firstSubItems);
	_CullInvisibleItems(secondSubItems);
	_DoSwap(fList, firstIndex, secondIndex, &firstSubItems,
		&secondSubItems);

	_RecalcItemTops(firstIndex);
	_RescanSelection(firstIndex, secondIndex + secondSubItems.CountItems());
	Invalidate(Bounds());

	return true;
}


/*!	\brief Removes a single item from the list and all of its children.

	Unlike the BeOS version, this one will actually delete the children, too,
	as there should be no reference left to them. This may cause problems for
	applications that actually take the misbehaviour of the Be classes into
	account.
*/
/**
 * @brief Removes \a item and all of its descendants from both lists.
 *
 * All child items (those with an outline level greater than \a item's) are
 * deleted automatically. The parent's fHasSubitems flag is cleared if the
 * removal leaves it with no remaining children.
 *
 * @param item          The item to remove.
 * @param fullListIndex The position of \a item in the full list.
 * @return The removed item on success, or NULL if \a item is NULL or
 *         \a fullListIndex is out of range.
 * @note Child items are deleted by this method; do not hold external pointers
 *       to them after calling _RemoveItem().
 */
BListItem*
BOutlineListView::_RemoveItem(BListItem* item, int32 fullListIndex)
{
	if (item == NULL || fullListIndex < 0
		|| fullListIndex >= FullListCountItems()) {
		return NULL;
	}

	uint32 level = item->OutlineLevel();
	int32 superIndex;
	BListItem* super = _SuperitemForIndex(fullListIndex, level, &superIndex);

	if (item->IsItemVisible()) {
		// remove children, too
		while (fullListIndex + 1 < FullListCountItems()) {
			BListItem* subItem = FullListItemAt(fullListIndex + 1);

			if (subItem->OutlineLevel() <= level)
				break;

			if (subItem->IsItemVisible())
				BListView::RemoveItem(subItem);

			fFullList.RemoveItem(fullListIndex + 1);
			delete subItem;
		}
		BListView::RemoveItem(item);
	}

	fFullList.RemoveItem(fullListIndex);

	if (super != NULL) {
		// we might need to change the fHasSubitems field of the parent
		BListItem* child = FullListItemAt(superIndex + 1);
		if (child == NULL || child->OutlineLevel() <= super->OutlineLevel())
			super->fHasSubitems = false;
	}

	return item;
}


/**
 * @brief Finds the nearest ancestor of the item at \a fullListIndex that has
 *        a strictly lower outline level than \a level.
 *
 * Walks backwards through the full list from \a fullListIndex - 1, returning
 * the first item whose outline level is less than \a level.
 *
 * @param fullListIndex  The full-list index of the child item.
 * @param level          The outline level of the child item; the parent must
 *                       have a level strictly less than this.
 * @param[out] _superIndex If non-NULL, set to the full-list index of the
 *                         returned parent item.
 * @return The parent BListItem, or NULL if no ancestor with a lower level
 *         exists (i.e., the item is at the root level).
 */
BListItem*
BOutlineListView::_SuperitemForIndex(int32 fullListIndex, int32 level,
	int32* _superIndex)
{
	BListItem* item;
	fullListIndex--;

	while (fullListIndex >= 0) {
		if ((item = FullListItemAt(fullListIndex))->OutlineLevel()
				< (uint32)level) {
			if (_superIndex != NULL)
				*_superIndex = fullListIndex;
			return item;
		}

		fullListIndex--;
	}

	return NULL;
}


/**
 * @brief Returns the full-list index of the nearest visible item before
 *        position \a fullListIndex.
 *
 * Used by AddItem() to locate the insertion point in the display list when
 * adding a new item: the new item should appear immediately after the last
 * visible item that precedes it in the full list.
 *
 * @param fullListIndex Starting position (exclusive) to search backwards from.
 * @return The full-list index of the previous visible item, or -1 if none
 *         exists before position \a fullListIndex.
 */
int32
BOutlineListView::_FindPreviousVisibleIndex(int32 fullListIndex)
{
	fullListIndex--;

	while (fullListIndex >= 0) {
		if (FullListItemAt(fullListIndex)->fVisible)
			return fullListIndex;

		fullListIndex--;
	}

	return -1;
}


/**
 * @brief Initialises the loop variables used by CountItemsUnder(),
 *        EachItemUnder(), and ItemUnderAt().
 *
 * When \a superItem is non-NULL, \a startIndex is set to the full-list index
 * immediately after \a superItem and \a baseLevel is set to one greater than
 * \a superItem's outline level. When \a superItem is NULL the entire full list
 * is targeted (startIndex = 0, baseLevel = 0).
 *
 * @param superItem        The parent item, or NULL for the root.
 * @param[out] startIndex  Set to the first full-list index to examine.
 * @param[out] baseLevel   Set to the minimum outline level of direct children.
 * @return B_OK on success, or B_ENTRY_NOT_FOUND if \a superItem is not in the
 *         full list.
 */
status_t
BOutlineListView::_ItemsUnderSetup(BListItem* superItem, int32& startIndex, uint32& baseLevel) const
{
	if (superItem != NULL) {
		startIndex = FullListIndexOf(superItem) + 1;
		if (startIndex == 0)
			return B_ENTRY_NOT_FOUND;
		baseLevel = superItem->OutlineLevel() + 1;
	} else {
		startIndex = 0;
		baseLevel = 0;
	}
	return B_OK;
}
