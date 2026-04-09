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
 *   Copyright 2011-2013, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2011, Clemens Zeidler <haiku@clemens-zeidler.de>.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file StringList.cpp
 * @brief Implementation of BStringList, a reference-counted ordered list of
 *        BString objects with Flattenable support.
 *
 * BStringList stores strings using BString's internal private-data pointer so
 * that reference counting is used instead of per-element copies.  The class
 * implements BFlattenable (TypeCode B_STRING_LIST_TYPE) for persistence and
 * IPC, and provides sorting, searching, joining, and per-element iteration.
 *
 * @see BString, BFlattenable
 */


#include <StringList.h>

#include <algorithm>

#include <StringPrivate.h>
#include <TypeConstants.h>


/**
 * @brief Case-sensitive comparator for BString private-data pointers, used by
 *        Sort() and the underlying BList sort implementation.
 */
static int
compare_private_data(const void* a, const void* b)
{
	return BString::Private::StringFromData(*(char**)a).Compare(
		BString::Private::StringFromData(*(char**)b));
}


/**
 * @brief Case-insensitive comparator for BString private-data pointers, used
 *        by Sort(ignoreCase=true).
 */
static int
compare_private_data_ignore_case(const void* a, const void* b)
{
	return BString::Private::StringFromData(*(char**)a).ICompare(
		BString::Private::StringFromData(*(char**)b));
}


// #pragma mark - BStringList


/**
 * @brief Construct an empty BStringList, optionally pre-allocating space.
 *
 * @param count  Initial capacity hint (number of items to pre-allocate).
 */
BStringList::BStringList(int32 count)
	:
	fStrings(count)
{
}


/**
 * @brief Copy constructor — shares string data via reference counting.
 *
 * Increments the reference count of every string in \a other so that the
 * two lists share the underlying character buffers until one of them is
 * modified.
 *
 * @param other  The BStringList to copy.
 */
BStringList::BStringList(const BStringList& other)
	:
	fStrings(other.fStrings)
{
	_IncrementRefCounts();
}


/**
 * @brief Destructor — decrements the reference count of all stored strings.
 */
BStringList::~BStringList()
{
	_DecrementRefCounts();
}


/**
 * @brief Insert a copy of \a _string at \a index.
 *
 * @param _string  The string to insert.
 * @param index    The position at which to insert (0-based).
 * @return true on success, false if memory allocation fails or the string
 *         cannot be made shareable.
 * @see Add(const BString&)
 */
bool
BStringList::Add(const BString& _string, int32 index)
{
	BString string(_string);
		// makes sure the string is shareable
	if (string.Length() != _string.Length())
		return false;

	char* privateData = BString::Private(string).Data();
	if (!fStrings.AddItem(privateData, index))
		return false;

	BString::Private::IncrementDataRefCount(privateData);
	return true;
}


/**
 * @brief Append a copy of \a _string to the end of the list.
 *
 * @param _string  The string to append.
 * @return true on success, false if memory allocation fails or the string
 *         cannot be made shareable.
 * @see Add(const BString&, int32)
 */
bool
BStringList::Add(const BString& _string)
{
	BString string(_string);
		// makes sure the string is shareable
	if (string.Length() != _string.Length())
		return false;

	char* privateData = BString::Private(string).Data();
	if (!fStrings.AddItem(privateData))
		return false;

	BString::Private::IncrementDataRefCount(privateData);
	return true;
}


/**
 * @brief Insert all strings from \a list starting at \a index.
 *
 * Increments the reference count of each string in \a list.
 *
 * @param list   The source list whose strings are to be inserted.
 * @param index  The position at which to start inserting.
 * @return true on success, false on memory allocation failure.
 * @see Add(const BStringList&)
 */
bool
BStringList::Add(const BStringList& list, int32 index)
{
	if (!fStrings.AddList(&list.fStrings, index))
		return false;

	list._IncrementRefCounts();
	return true;
}


/**
 * @brief Append all strings from \a list to the end of this list.
 *
 * Increments the reference count of each string in \a list.
 *
 * @param list  The source list to append.
 * @return true on success, false on memory allocation failure.
 * @see Add(const BStringList&, int32)
 */
bool
BStringList::Add(const BStringList& list)
{
	if (!fStrings.AddList(&list.fStrings))
		return false;

	list._IncrementRefCounts();
	return true;
}


/**
 * @brief Remove all occurrences of \a string from the list.
 *
 * Scans the list from back to front and removes each element that equals
 * \a string.  Case sensitivity is controlled by \a ignoreCase.
 *
 * @param string      The string value to remove.
 * @param ignoreCase  If true, perform a case-insensitive comparison.
 * @return true if at least one element was removed, false otherwise.
 * @see Remove(int32), Remove(const BStringList&, bool)
 */
bool
BStringList::Remove(const BString& string, bool ignoreCase)
{
	bool result = false;
	int32 count = fStrings.CountItems();

	if (ignoreCase) {
		int32 length = string.Length();

		for (int32 i = count - 1; i >= 0; i--) {
			BString element(StringAt(i));
			if (length == element.Length() && string.ICompare(element) == 0) {
				Remove(i);
				result = true;
			}
		}
	} else {
		for (int32 i = count - 1; i >= 0; i--) {
			if (string == StringAt(i)) {
				Remove(i);
				result = true;
			}
		}
	}

	return result;
}


/**
 * @brief Remove all strings from this list that also appear in \a list.
 *
 * @param list        The set of strings to remove.
 * @param ignoreCase  If true, comparisons are case-insensitive.
 * @return true if at least one element was removed, false otherwise.
 * @see Remove(const BString&, bool)
 */
bool
BStringList::Remove(const BStringList& list, bool ignoreCase)
{
	bool removedAnything = false;
	int32 stringCount = list.CountStrings();
	for (int32 i = 0; i < stringCount; i++)
		removedAnything |= Remove(list.StringAt(i), ignoreCase);

	return removedAnything;
}


/**
 * @brief Remove and return the string at \a index.
 *
 * Decrements the reference count of the removed string's data.
 *
 * @param index  The 0-based position to remove.
 * @return The removed BString, or an empty BString if \a index is out of
 *         range.
 * @see Remove(const BString&, bool), Remove(int32, int32)
 */
BString
BStringList::Remove(int32 index)
{
	if (index < 0 || index >= fStrings.CountItems())
		return BString();

	char* privateData = (char*)fStrings.RemoveItem(index);
	BString string(BString::Private::StringFromData(privateData));
	BString::Private::DecrementDataRefCount(privateData);
	return string;
}


/**
 * @brief Remove \a count strings starting at \a index.
 *
 * Decrements the reference count of each removed string.
 *
 * @param index  The 0-based start position.
 * @param count  The number of strings to remove.
 * @return true on success, false if \a index is out of range.
 * @see Remove(int32)
 */
bool
BStringList::Remove(int32 index, int32 count)
{
	int32 stringCount = fStrings.CountItems();
	if (index < 0 || index > stringCount)
		return false;

	int32 end = index + std::min(stringCount - index, count);
	for (int32 i = index; i < end; i++)
		BString::Private::DecrementDataRefCount((char*)fStrings.ItemAt(i));

	fStrings.RemoveItems(index, end - index);
	return true;
}


/**
 * @brief Replace the string at \a index with \a string.
 *
 * Decrements the reference count of the old string and increments the count
 * for the new one.
 *
 * @param index   The 0-based position to replace.
 * @param string  The replacement string.
 * @return true on success, false if \a index is out of range.
 */
bool
BStringList::Replace(int32 index, const BString& string)
{
	if (index < 0 || index >= fStrings.CountItems())
		return false;

	BString::Private::DecrementDataRefCount((char*)fStrings.ItemAt(index));

	char* privateData = BString::Private(string).Data();
	BString::Private::IncrementDataRefCount(privateData);
	fStrings.ReplaceItem(index, privateData);

	return true;
}


/**
 * @brief Remove all strings from the list and decrement their reference counts.
 */
void
BStringList::MakeEmpty()
{
	_DecrementRefCounts();
	fStrings.MakeEmpty();
}


/**
 * @brief Sort the strings in place.
 *
 * @param ignoreCase  If true, sort using case-insensitive comparison.
 */
void
BStringList::Sort(bool ignoreCase)
{
	fStrings.SortItems(ignoreCase
		? compare_private_data_ignore_case : compare_private_data);
}


/**
 * @brief Swap the strings at positions \a indexA and \a indexB.
 *
 * @param indexA  First position.
 * @param indexB  Second position.
 * @return true on success, false if either index is out of range.
 */
bool
BStringList::Swap(int32 indexA, int32 indexB)
{
	return fStrings.SwapItems(indexA, indexB);
}


/**
 * @brief Move the string at \a fromIndex to \a toIndex, shifting intervening
 *        elements.
 *
 * @param fromIndex  The current position of the string to move.
 * @param toIndex    The destination position.
 * @return true on success, false if either index is out of range.
 */
bool
BStringList::Move(int32 fromIndex, int32 toIndex)
{
	return fStrings.MoveItem(fromIndex, toIndex);
}


/**
 * @brief Return the string at \a index.
 *
 * @param index  The 0-based position to read.
 * @return The BString at \a index, or an empty BString if out of range.
 */
BString
BStringList::StringAt(int32 index) const
{
	return BString::Private::StringFromData((char*)fStrings.ItemAt(index));
}


/**
 * @brief Return the first string in the list.
 *
 * @return The first BString, or an empty BString if the list is empty.
 */
BString
BStringList::First() const
{
	return BString::Private::StringFromData((char*)fStrings.FirstItem());
}


/**
 * @brief Return the last string in the list.
 *
 * @return The last BString, or an empty BString if the list is empty.
 */
BString
BStringList::Last() const
{
	return BString::Private::StringFromData((char*)fStrings.LastItem());
}


/**
 * @brief Find the first occurrence of \a string in the list.
 *
 * @param string      The string to search for.
 * @param ignoreCase  If true, use a case-insensitive comparison.
 * @return The 0-based index of the first match, or -1 if not found.
 */
int32
BStringList::IndexOf(const BString& string, bool ignoreCase) const
{
	int32 count = fStrings.CountItems();

	if (ignoreCase) {
		int32 length = string.Length();

		for (int32 i = 0; i < count; i++) {
			BString element(StringAt(i));
			if (length == element.Length() && string.ICompare(element) == 0)
				return i;
		}
	} else {
		for (int32 i = 0; i < count; i++) {
			if (string == StringAt(i))
				return i;
		}
	}

	return -1;
}


/**
 * @brief Return the number of strings in the list.
 *
 * @return The element count.
 */
int32
BStringList::CountStrings() const
{
	return fStrings.CountItems();
}


/**
 * @brief Test whether the list contains no strings.
 *
 * @return true if the list is empty, false otherwise.
 */
bool
BStringList::IsEmpty() const
{
	return fStrings.IsEmpty();
}


/**
 * @brief Concatenate all strings with \a separator between each pair.
 *
 * @param separator  The separator string.  If \a length is negative the full
 *                   NUL-terminated length is used; otherwise only the first
 *                   \a length bytes are used.
 * @param length     Maximum number of bytes from \a separator to use, or -1
 *                   to use the full string.
 * @return The joined BString.
 * @see _Join()
 */
BString
BStringList::Join(const char* separator, int32 length) const
{
	return _Join(separator,
		length >= 0 ? strnlen(separator, length) : strlen(separator));
}


/**
 * @brief Call \a func for each string in the list, stopping early if the
 *        callback returns true.
 *
 * @param func  A callback that receives each string.  Return true to stop
 *              iteration, false to continue.
 */
void
BStringList::DoForEach(bool (*func)(const BString& string))
{
	bool terminate = false;
	int32 count = fStrings.CountItems();
	for (int32 i = 0; i < count && !terminate; i++)
		terminate = func(StringAt(i));
}


/**
 * @brief Call \a func for each string in the list, passing an extra argument,
 *        and stopping early if the callback returns true.
 *
 * @param func  A callback that receives each string and \a arg2.  Return true
 *              to stop iteration, false to continue.
 * @param arg2  An opaque pointer forwarded to each \a func invocation.
 */
void
BStringList::DoForEach(bool (*func)(const BString& string, void* arg2),
	void* arg2)
{
	bool terminate = false;
	int32 count = fStrings.CountItems();
	for (int32 i = 0; i < count && !terminate; i++)
		terminate = func(StringAt(i), arg2);
}


/**
 * @brief Assignment operator — replaces the contents of this list with a
 *        reference-counted copy of \a other.
 *
 * Decrements ref counts for all current strings, replaces the underlying
 * BList, then increments ref counts for the new strings.
 *
 * @param other  The source list.
 * @return A reference to this list.
 */
BStringList&
BStringList::operator=(const BStringList& other)
{
	if (this != &other) {
		_DecrementRefCounts();
		fStrings = other.fStrings;
		_IncrementRefCounts();
	}

	return *this;
}


/**
 * @brief Equality operator — returns true iff both lists contain the same
 *        strings in the same order (case-sensitive).
 *
 * @param other  The list to compare with.
 * @return true if identical, false otherwise.
 */
bool
BStringList::operator==(const BStringList& other) const
{
	if (this == &other)
		return true;

	int32 count = fStrings.CountItems();
	if (count != other.fStrings.CountItems())
		return false;

	for (int32 i = 0; i < count; i++) {
		if (StringAt(i) != other.StringAt(i))
			return false;
	}

	return true;
}


/**
 * @brief Return false — BStringList is a variable-size flattenable type.
 *
 * @return false always.
 */
bool
BStringList::IsFixedSize() const
{
	return false;
}


/**
 * @brief Return the type code that identifies the flattened format.
 *
 * @return B_STRING_LIST_TYPE.
 */
type_code
BStringList::TypeCode() const
{
	return B_STRING_LIST_TYPE;
}


/**
 * @brief Test whether \a code is a type code this list can unflatten.
 *
 * @param code  The type code to test.
 * @return true only if \a code equals B_STRING_LIST_TYPE.
 */
bool
BStringList::AllowsTypeCode(type_code code) const
{
	return code == B_STRING_LIST_TYPE;
}


/**
 * @brief Return the number of bytes needed to flatten this list.
 *
 * Each string is stored as its UTF-8 content followed by a NUL terminator.
 *
 * @return The total flattened byte count.
 */
ssize_t
BStringList::FlattenedSize() const
{
	ssize_t size = 0;
	int32 count = CountStrings();
	for (int32 i = 0; i < count; i++)
		size += StringAt(i).Length() + 1;

	return size;
}


/**
 * @brief Write the list into \a buf as a sequence of NUL-terminated UTF-8
 *        strings.
 *
 * @param buf   Destination buffer of at least FlattenedSize() bytes.
 * @param size  Size of \a buf in bytes.
 * @return B_OK on success, B_NO_MEMORY if \a size < FlattenedSize().
 * @see Unflatten()
 */
status_t
BStringList::Flatten(void* buf, ssize_t size) const
{
	const char* buffer = (const char*)buf;

	if (size < FlattenedSize())
		return B_NO_MEMORY;

	int32 count = CountStrings();
	for (int32 i = 0; i < count; i++) {
		BString item = StringAt(i);
		ssize_t storeSize = item.Length() + 1;
		memcpy((void*)buffer, (const void*)item.String(), storeSize);
		buffer += storeSize;
	}

	return B_OK;
}


/**
 * @brief Replace the contents of this list by parsing \a buffer as a flat
 *        string list.
 *
 * Expects \a buffer to contain a sequence of NUL-terminated UTF-8 strings
 * exactly as produced by Flatten().  The existing list is cleared before
 * parsing begins.
 *
 * @param code    Must be B_STRING_LIST_TYPE; returns B_ERROR otherwise.
 * @param buffer  The flat data to parse.
 * @param size    The number of bytes in \a buffer.
 * @return B_OK on success, B_ERROR if \a code is wrong, B_BAD_VALUE if the
 *         last string is not NUL-terminated within the buffer, or B_NO_MEMORY
 *         on allocation failure.
 * @see Flatten()
 */
status_t
BStringList::Unflatten(type_code code, const void* buffer, ssize_t size)
{
	if (code != B_STRING_LIST_TYPE)
		return B_ERROR;
	const char* bufferStart = (const char*)buffer;

	MakeEmpty();

	off_t offset = 0;
	while (offset < size) {
		const char* string = bufferStart + offset;
		size_t restSize = size - offset;
		size_t read = strnlen(string, restSize);
		if (read == restSize)
			return B_BAD_VALUE;

		if (!Add(string))
			return B_NO_MEMORY;
		offset += read + 1;
	}

	return B_OK;
}


/**
 * @brief Increment the reference count of every string stored in the list.
 *
 * Called whenever the list is copied to ensure strings are not freed while
 * still referenced by multiple lists.
 */
void
BStringList::_IncrementRefCounts() const
{
	int32 count = fStrings.CountItems();
	for (int32 i = 0; i < count; i++) {
		BString::Private::IncrementDataRefCount((char*)fStrings.ItemAt(i));
	}
}


/**
 * @brief Decrement the reference count of every string stored in the list.
 *
 * Called before clearing or destroying the list so that string data whose
 * reference count drops to zero is freed.
 */
void
BStringList::_DecrementRefCounts() const
{
	int32 count = fStrings.CountItems();
	for (int32 i = 0; i < count; i++)
		BString::Private::DecrementDataRefCount((char*)fStrings.ItemAt(i));
}


/**
 * @brief Concatenate all strings using \a separator of exactly \a length
 *        bytes between each pair, returning the result.
 *
 * Handles the degenerate cases of 0 or 1 element directly.  For larger lists,
 * pre-computes the total length and fills a single locked buffer for
 * efficiency.
 *
 * @param separator  The separator byte sequence (not required to be
 *                   NUL-terminated).
 * @param length     The exact number of bytes of \a separator to use.
 * @return The joined BString, or an empty string if allocation fails.
 * @see Join()
 */
BString
BStringList::_Join(const char* separator, int32 length) const
{
	// handle simple cases (0 or 1 element)
	int32 count = CountStrings();
	if (count == 0)
		return BString();
	if (count == 1)
		return StringAt(0);

	// determine the total length
	int32 totalLength = length * (count - 1);
	for (int32 i = 0; i < count; i++)
		totalLength += StringAt(i).Length();

	// compose the result string
	BString result;
	char* buffer = result.LockBuffer(totalLength);
	if (buffer == NULL)
		return result;

	for (int32 i = 0; i < count; i++) {
		if (i > 0 && length > 0) {
			memcpy(buffer, separator, length);
			buffer += length;
		}

		BString string = StringAt(i);
		memcpy(buffer, string.String(), string.Length());
		buffer += string.Length();
	}

	return result.UnlockBuffer(totalLength);
}
