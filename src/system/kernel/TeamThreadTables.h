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
 *   Copyright 2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file TeamThreadTables.h
 *  @brief Hash + linked list container template used by the team and thread registries. */

#ifndef KERNEL_TEAM_THREAD_TABLES_H
#define KERNEL_TEAM_THREAD_TABLES_H


#include <thread_types.h>


namespace BKernel {


/** @brief Combined hash table and ordered list of teams or threads.
 *
 * Used to back the global team and thread registries: the hash table
 * provides O(1) lookup by id, while the linked list supports stable
 * iteration that survives concurrent insertions and deletions through
 * sentinel @c IteratorEntry objects threaded into the list. Each element
 * is also tagged with a serial number so iterators can identify entries
 * that were added after iteration began. */
template<typename Element>
struct TeamThreadTable {
public:
	typedef typename Element::id_type		id_type;
	typedef typename Element::iterator_type	IteratorEntry;

	/** @brief Forward iterator that walks the table's linked list. */
	struct Iterator {
		Iterator()
			:
			fNext(NULL)
		{
		}

		Iterator(IteratorEntry* nextEntry)
		{
			_SetNext(nextEntry);
		}

		bool HasNext() const
		{
			return fNext != NULL;
		}

		Element* Next()
		{
			Element* result = fNext;
			if (result != NULL)
				_SetNext(result->GetDoublyLinkedListLink()->next);

			return result;
		}

	private:
		void _SetNext(IteratorEntry* entry)
		{
			while (entry != NULL) {
				if (entry->id >= 0) {
					fNext = static_cast<Element*>(entry);
					return;
				}

				entry = entry->GetDoublyLinkedListLink()->next;
			}

			fNext = NULL;
		}

	private:
		Element*	fNext;
	};

public:
	TeamThreadTable()
		:
		fNextSerialNumber(1)
	{
	}

	status_t Init(size_t initialSize)
	{
		return fTable.Init(initialSize);
	}

	void Insert(Element* element)
	{
		element->serial_number = fNextSerialNumber++;
		fTable.InsertUnchecked(element);
		fList.Add(element);
	}

	void Remove(Element* element)
	{
		fTable.RemoveUnchecked(element);
		fList.Remove(element);
	}

	Element* Lookup(id_type id, bool visibleOnly = true) const
	{
		Element* element = fTable.Lookup(id);
		return element != NULL && (!visibleOnly || element->visible)
			? element : NULL;
	}

	/*! Gets an iterator.
		The iterator iterates through all, including invisible, entries!
	*/
	Iterator GetIterator() const
	{
		return Iterator(fList.Head());
	}

	void InsertIteratorEntry(IteratorEntry* entry)
	{
		// add to front
		entry->id = -1;
		entry->visible = false;
		fList.Add(entry, false);
	}

	void RemoveIteratorEntry(IteratorEntry* entry)
	{
		fList.Remove(entry);
	}

	Element* NextElement(IteratorEntry* entry, bool visibleOnly = true)
	{
		if (entry == fList.Tail())
			return NULL;

		IteratorEntry* nextEntry = entry;

		while (true) {
			nextEntry = nextEntry->GetDoublyLinkedListLink()->next;
			if (nextEntry == NULL) {
				// end of list -- requeue entry at the end and return NULL
				fList.Remove(entry);
				fList.Add(entry);
				return NULL;
			}

			if (nextEntry->id >= 0 && (!visibleOnly || nextEntry->visible)) {
				// found an element -- requeue entry after element
				Element* element = static_cast<Element*>(nextEntry);
				fList.Remove(entry);
				fList.InsertAfter(nextEntry, entry);
				return element;
			}
		}
	}

private:
	struct HashDefinition {
		typedef id_type		KeyType;
		typedef	Element		ValueType;

		size_t HashKey(id_type key) const
		{
			return key;
		}

		size_t Hash(Element* value) const
		{
			return HashKey(value->id);
		}

		bool Compare(id_type key, Element* value) const
		{
			return value->id == key;
		}

		Element*& GetLink(Element* value) const
		{
			return value->hash_next;
		}
	};

	typedef BOpenHashTable<HashDefinition> ElementTable;
	typedef DoublyLinkedList<IteratorEntry> List;

private:
	ElementTable	fTable;
	List			fList;
	int64			fNextSerialNumber;
};


}	// namespace BKernel


#endif	// KERNEL_TEAM_THREAD_TABLES_H
