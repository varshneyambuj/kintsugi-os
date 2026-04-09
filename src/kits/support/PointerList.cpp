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
 *   Copyright 2003-2004, Stefano Ceccherini (burton666@libero.it).
 *   Copyright 2004, Michael Pfeiffer (laplace@users.sourceforge.net).
 *   Distributed under the terms of the MIT License.
 *
 *   History:
 *     2003-2004  Initial implementation by Stefano Ceccherini.
 *     2004/08/03 Testing, bug fixing and implementation of quick sort,
 *                refactoring by Michael Pfeiffer.
 */

/**
 * @file PointerList.cpp
 * @brief Implementation of _PointerList_ and the private sorting/searching
 *        helper hierarchy used by BObjectList<T>.
 *
 * AbstractPointerListHelper and its three concrete subclasses
 * (PointerListHelper, PointerListHelperWithState,
 * PointerListHelperUsePredicate) provide a uniform interface over the three
 * comparison-function variants supported by _PointerList_.  _PointerList_
 * itself extends BList with type-safe iteration, sorting, searching, and
 * in-place item manipulation.
 *
 * @see BObjectList, BList
 */

// TODO:
//   - Rewrite to use STL
//   - Include ObjectList.h
//   - Test if building with jam works

// Note: Method Owning() is inlined in header file ObjectList.h

#include <ObjectList.h>

#include <algorithm>
#include <assert.h>
#include <functional>
#include <string.h>

#include <List.h>


using namespace std;


// TODO: The implementation of _PointerList_ should be completely rewritten to
// use STL in a more efficent way!

struct comparator;


/**
 * @brief Abstract base that implements binary search and sort algorithms on a
 *        raw BList of pointers, delegating only the item comparison to
 *        subclasses.
 *
 * Concrete subclasses supply a Compare() method that wraps one of the three
 * comparison-function variants (plain, with-state, unary-predicate).  All
 * sorting is done via std::sort (QuickSort wrapper) with an insertion-sort
 * fallback driven by kQuickSortThreshold.
 */
class AbstractPointerListHelper {
public:
	AbstractPointerListHelper() {};
	virtual ~AbstractPointerListHelper();

	/**
	 * @brief Binary-search \a list for \a key and return its index.
	 *
	 * @param key   The search key (passed as the first argument to Compare()).
	 * @param list  The sorted BList to search.
	 * @return The non-negative index of the matching element, or a negative
	 *         number -(insertionPoint+1) if not found.
	 * @see BinarySearch(const void*, const BList*)
	 */
	int32 BinarySearchIndex(const void *key, const BList *list);

	/**
	 * @brief Binary-search \a list for \a key and return the matching item.
	 *
	 * @param key   The search key.
	 * @param list  The sorted BList to search.
	 * @return The matching item pointer, or NULL if not found.
	 * @see BinarySearchIndex()
	 */
	void* BinarySearch(const void *key, const BList *list);

	/**
	 * @brief Sort all items in \a list using Compare().
	 *
	 * @param list  The BList whose items are to be sorted in place.
	 */
	void SortItems(BList *list);

	/**
	 * @brief Move the first item of \a list to the end and sort the remaining
	 *        items.
	 *
	 * Used to implement heap-sort-style operations where the minimum/maximum
	 * element is consumed and the heap is re-sorted.
	 *
	 * @param list  The BList to operate on.
	 */
	void HSortItems(BList *list);

	friend struct comparator;

private:
	enum {
		// Use insertion sort if number of elements in list is less than
		// kQuickSortThreshold.
		kQuickSortThreshold = 11,
		// Use simple pivot element computation if number of elements in
		// list is less than kPivotThreshold.
		kPivotThreshold     = 5,
	};

	// Methods that do the actual work:

	/**
	 * @brief Swap the items at positions \a i and \a j in \a items.
	 *
	 * @param items  The raw pointer array.
	 * @param i      First index.
	 * @param j      Second index.
	 */
	inline void Swap(void **items, int32 i, int32 j);

	/**
	 * @brief Core binary-search implementation operating on a raw array.
	 *
	 * @param key       The search key.
	 * @param items     The sorted item array.
	 * @param numItems  Number of elements in \a items.
	 * @param index     Receives the index of the match, or -(pos+1) if not
	 *                  found.
	 * @return The matching item pointer, or NULL if not found.
	 */
	void* BinarySearch(const void *key, const void **items, int32 numItems,
			int32 &index);

	/**
	 * @brief Sort the sub-array items[low..high] in place using std::sort.
	 *
	 * @param items  The full pointer array.
	 * @param low    Inclusive lower bound.
	 * @param high   Inclusive upper bound.
	 */
	void QuickSort(void **items, int32 low, int32 high);

	// Method to be implemented by sub classes
	int virtual Compare(const void *key, const void* item) = 0;
};

struct comparator
{
	comparator(AbstractPointerListHelper* helper) : helper(helper) {}

	bool operator()(const void* a, const void* b) {
		return helper->Compare(b, a) > 0;
	}

	AbstractPointerListHelper* helper;
};


AbstractPointerListHelper::~AbstractPointerListHelper()
{
}


void
AbstractPointerListHelper::Swap(void **items, int32 i, int32 j)
{
	void *swap = items[i];
	items[i] = items[j];
	items[j] = swap;
}


int32
AbstractPointerListHelper::BinarySearchIndex(const void *key, const BList *list)
{
	int32 index;
	const void **items = static_cast<const void**>(list->Items());
	BinarySearch(key, items, list->CountItems(), index);
	return index;
}


void *
AbstractPointerListHelper::BinarySearch(const void *key, const BList *list)
{
	int32 index;
	const void **items = static_cast<const void**>(list->Items());
	return BinarySearch(key, items, list->CountItems(), index);
}


void
AbstractPointerListHelper::SortItems(BList *list)
{
	void **items = static_cast<void**>(list->Items());
	QuickSort(items, 0, list->CountItems()-1);
}


void
AbstractPointerListHelper::HSortItems(BList *list)
{
	void **items = static_cast<void**>(list->Items());
	int32 numItems = list->CountItems();
	if (numItems > 1) {
		// swap last with first item
		Swap(items, 0, numItems-1);
	}
	// sort all items but last item
	QuickSort(items, 0, numItems-2);
}


void *
AbstractPointerListHelper::BinarySearch(const void *key, const void **items,
	int32 numItems, int32 &index)
{
	const void** end = &items[numItems];
	const void** found = lower_bound(items, end, key, comparator(this));
	index = found - items;
	if (index != numItems && Compare(key, *found) == 0) {
		return const_cast<void*>(*found);
	} else {
		index = -(index + 1);
		return NULL;
	}
}


void
AbstractPointerListHelper::QuickSort(void **items, int32 low, int32 high)
{
	if (low <= high) {
		sort(&items[low], &items[high+1], comparator(this));
	}
}


/**
 * @brief Concrete helper that wraps a plain GenericCompareFunction (no extra
 *        state).
 */
class PointerListHelper : public AbstractPointerListHelper {
public:
	PointerListHelper(_PointerList_::GenericCompareFunction compareFunc)
		: fCompareFunc(compareFunc)
	{
		// nothing to do
	}

	int Compare(const void *a, const void *b)
	{
		return fCompareFunc(a, b);
	}

private:
	_PointerList_::GenericCompareFunction fCompareFunc;
};


/**
 * @brief Concrete helper that wraps a GenericCompareFunctionWithState,
 *        forwarding an opaque state pointer to every comparison call.
 */
class PointerListHelperWithState : public AbstractPointerListHelper
{
public:
	PointerListHelperWithState(
		_PointerList_::GenericCompareFunctionWithState compareFunc,
		void* state)
		: fCompareFunc(compareFunc)
		, fState(state)
	{
		// nothing to do
	}

	int Compare(const void *a, const void *b)
	{
		return fCompareFunc(a, b, fState);
	}

private:
	_PointerList_::GenericCompareFunctionWithState fCompareFunc;
	void* fState;
};


/**
 * @brief Concrete helper that adapts a UnaryPredicateGlue to the binary
 *        Compare() interface required by AbstractPointerListHelper.
 *
 * The predicate receives (item, key) and returns negative/zero/positive;
 * the argument order and sign are inverted here to match the Compare()
 * convention of (key, item).
 */
class PointerListHelperUsePredicate : public AbstractPointerListHelper
{
public:
	PointerListHelperUsePredicate(
		_PointerList_::UnaryPredicateGlue predicate)
		: fPredicate(predicate)
	{
		// nothing to do
	}

	int Compare(const void *arg, const void *item)
	{
		// need to adapt arguments and return value
		return -fPredicate(item, const_cast<void *>(arg));
	}

private:
	_PointerList_::UnaryPredicateGlue fPredicate;
};


// Implementation of class _PointerList_

/**
 * @brief Construct an empty _PointerList_ with the given block size and
 *        ownership flag.
 *
 * @param itemsPerBlock  Number of pointer slots to allocate per BList block.
 * @param own            Initial value of the legacy ownership flag (used by
 *                       BObjectList subclass to decide whether to delete items).
 */
_PointerList_::_PointerList_(int32 itemsPerBlock, bool own)
	:
	BList(itemsPerBlock),
	fLegacyOwning(own)
{

}


/**
 * @brief Copy constructor — duplicates the item array and ownership flag.
 *
 * @param list  The source list to copy.
 */
_PointerList_::_PointerList_(const _PointerList_ &list)
	:
	BList(list),
	fLegacyOwning(list.fLegacyOwning)
{
}


/**
 * @brief Destructor — no-op by design.
 *
 * Item deletion is the responsibility of BObjectList<T> (the owning
 * subclass), not of _PointerList_ itself.
 */
_PointerList_::~_PointerList_()
{
	// This is empty by design, the "owning" variable
	// is used by the BObjectList subclass
}


// Note: function pointers must not be NULL!!!

/**
 * @brief Iterate over all elements, calling \a function for each one until a
 *        non-NULL result is returned.
 *
 * @param function  A callback receiving (item, arg).  Return non-NULL to stop
 *                  iteration early and propagate the return value.
 * @param arg       An opaque pointer forwarded to every \a function call.
 * @return The first non-NULL value returned by \a function, or NULL if the
 *         entire list was traversed.
 */
void *
_PointerList_::EachElement(GenericEachFunction function, void *arg)
{
	int32 numItems = CountItems();
	void *result = NULL;

	for (int32 index = 0; index < numItems; index++) {
		result = function(ItemAtFast(index), arg);
		if (result != NULL)
			break;
	}

	return result;
}


/**
 * @brief Sort all items using a plain comparison function.
 *
 * @param compareFunc  A function returning negative/zero/positive for
 *                     less-than/equal/greater-than between two items.
 * @see SortItems(GenericCompareFunctionWithState, void*)
 */
void
_PointerList_::SortItems(GenericCompareFunction compareFunc)
{
	PointerListHelper helper(compareFunc);
	helper.SortItems(this);
}


/**
 * @brief Sort all items using a comparison function that takes an extra state
 *        pointer.
 *
 * @param compareFunc  A stateful comparison function.
 * @param state        Opaque pointer forwarded to every comparison call.
 * @see SortItems(GenericCompareFunction)
 */
void
_PointerList_::SortItems(GenericCompareFunctionWithState compareFunc,
	void *state)
{
	PointerListHelperWithState helper(compareFunc, state);
	helper.SortItems(this);
}


/**
 * @brief Move the first item to the end and sort the remaining items using a
 *        plain comparison function.
 *
 * Useful for consuming the minimum element in a heap-ordered list.
 *
 * @param compareFunc  The comparison function to use for re-sorting.
 * @see HSortItems(GenericCompareFunctionWithState, void*)
 */
void
_PointerList_::HSortItems(GenericCompareFunction compareFunc)
{
	PointerListHelper helper(compareFunc);
	helper.HSortItems(this);
}


/**
 * @brief Move the first item to the end and sort the remaining items using a
 *        stateful comparison function.
 *
 * @param compareFunc  The stateful comparison function.
 * @param state        Opaque pointer forwarded to every comparison call.
 * @see HSortItems(GenericCompareFunction)
 */
void
_PointerList_::HSortItems(GenericCompareFunctionWithState compareFunc,
	void *state)
{
	PointerListHelperWithState helper(compareFunc, state);
	helper.HSortItems(this);
}


/**
 * @brief Binary-search the list for \a key using a plain comparison function.
 *
 * The list must be sorted in the same order defined by \a compareFunc.
 *
 * @param key          The search key.
 * @param compareFunc  The comparison function used to locate \a key.
 * @return The matching item pointer, or NULL if not found.
 * @see BinarySearch(const void*, GenericCompareFunctionWithState, void*) const
 */
void *
_PointerList_::BinarySearch(const void *key,
	GenericCompareFunction compareFunc) const
{
	PointerListHelper helper(compareFunc);
	return helper.BinarySearch(key, this);
}


/**
 * @brief Binary-search the list for \a key using a stateful comparison
 *        function.
 *
 * @param key          The search key.
 * @param compareFunc  The stateful comparison function.
 * @param state        Opaque pointer forwarded to every comparison call.
 * @return The matching item pointer, or NULL if not found.
 * @see BinarySearch(const void*, GenericCompareFunction) const
 */
void *
_PointerList_::BinarySearch(const void *key,
			GenericCompareFunctionWithState compareFunc, void *state) const
{
	PointerListHelperWithState helper(compareFunc, state);
	return helper.BinarySearch(key, this);
}


/**
 * @brief Binary-search the list and return the index of the matching item,
 *        using a plain comparison function.
 *
 * @param key          The search key.
 * @param compareFunc  The comparison function.
 * @return The non-negative index of the match, or a negative number
 *         -(insertionPoint+1) if not found.
 * @see BinarySearchIndex(const void*, GenericCompareFunctionWithState, void*) const
 */
int32
_PointerList_::BinarySearchIndex(const void *key,
	GenericCompareFunction compareFunc) const
{
	PointerListHelper helper(compareFunc);
	return helper.BinarySearchIndex(key, this);
}


/**
 * @brief Binary-search the list and return the index of the matching item,
 *        using a stateful comparison function.
 *
 * @param key          The search key.
 * @param compareFunc  The stateful comparison function.
 * @param state        Opaque pointer forwarded to every comparison call.
 * @return The non-negative index of the match, or a negative number
 *         -(insertionPoint+1) if not found.
 * @see BinarySearchIndex(const void*, GenericCompareFunction) const
 */
int32
_PointerList_::BinarySearchIndex(const void *key,
			GenericCompareFunctionWithState compareFunc, void *state) const
{
	PointerListHelperWithState helper(compareFunc, state);
	return helper.BinarySearchIndex(key, this);
}


/**
 * @brief Binary-search the list using a unary predicate and return the index
 *        of the matching item.
 *
 * The predicate receives (item, key) and returns negative/zero/positive.  The
 * list must be sorted according to the same predicate ordering.
 *
 * @param key        The search key passed as the second argument to \a predicate.
 * @param predicate  The unary predicate glue function.
 * @return The non-negative index of the match, or a negative number
 *         -(insertionPoint+1) if not found.
 */
int32
_PointerList_::BinarySearchIndexByPredicate(const void *key,
	UnaryPredicateGlue predicate) const
{
	PointerListHelperUsePredicate helper(predicate);
	return helper.BinarySearchIndex(key, this);
}

/**
 * @brief Replace the item at \a index with \a newItem.
 *
 * @param index    The 0-based position to overwrite.
 * @param newItem  The replacement pointer.
 * @return true on success, false if \a index is out of range.
 */
bool
_PointerList_::ReplaceItem(int32 index, void *newItem)
{
	if (index < 0 || index >= CountItems())
		return false;

	void **items = static_cast<void **>(Items());
	items[index] = newItem;

	return true;
}


/**
 * @brief Move the item at \a from to position \a to, shifting intervening
 *        items by one slot.
 *
 * @param from  The current index of the item to move.
 * @param to    The destination index.
 * @return true on success, false if either index is out of range (or if
 *         either position holds a NULL pointer).
 */
bool
_PointerList_::MoveItem(int32 from, int32 to)
{
	if (from == to)
		return true;

	void* fromItem = ItemAt(from);
	void* toItem = ItemAt(to);
	if (fromItem == NULL || toItem == NULL)
		return false;

	void** items = static_cast<void**>(Items());
	if (from < to)
		memmove(items + from, items + from + 1, (to - from) * sizeof(void*));
	else
		memmove(items + to + 1, items + to, (from - to) * sizeof(void*));

	items[to] = fromItem;
	return true;
}
