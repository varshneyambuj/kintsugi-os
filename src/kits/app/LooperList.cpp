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
 *   Copyright 2001-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Erik Jaesler (erik@cgsoftware.com)
 */

/** @file LooperList.cpp
 *  @brief BPrivate::BLooperList implementation for tracking active loopers.
 *
 *  Maintains a global, thread-safe list of all BLooper instances in the
 *  current team. Provides lookup by looper pointer, thread ID, name, or
 *  port ID. Uses a reader-writer lock for concurrent read access.
 */

//! Maintains a global list of all loopers in a given team.


#include "LooperList.h"

#include <Looper.h>

#include <algorithm>
#include <string.h>


using std::vector;

namespace BPrivate {

/** @brief Global looper list instance shared by all loopers in the team. */
BLooperList gLooperList;


/** @brief Constructs the BLooperList and initializes its reader-writer lock. */
BLooperList::BLooperList()
{
	rw_lock_init(&fLock, "BLooperList rwlock");
}


/** @brief Acquires the write lock on the looper list.
 *  @return true if the lock was successfully acquired, false otherwise.
 */
bool
BLooperList::Lock()
{
	return rw_lock_write_lock(&fLock) == B_OK;
}


/** @brief Releases the write lock on the looper list. */
void
BLooperList::Unlock()
{
	rw_lock_write_unlock(&fLock);
}


/** @brief Checks whether the current thread holds the write lock.
 *  @return true if the current thread holds the lock, false otherwise.
 */
bool
BLooperList::IsLocked()
{
	return fLock.holder == find_thread(NULL);
}


/** @brief Adds a looper to the global list.
 *
 *  If the looper is not already registered, it is added to the first empty
 *  slot or appended at the end. The looper is locked upon insertion.
 *
 *  @param looper The BLooper to add to the list.
 */
void
BLooperList::AddLooper(BLooper* looper)
{
	WriteLocker locker(fLock);
	AssertLocked();
	if (!IsLooperValid(looper)) {
		LooperDataIterator i
			= find_if(fData.begin(), fData.end(), EmptySlotPred);
		if (i == fData.end()) {
			fData.push_back(LooperData(looper));
			looper->Lock();
		} else {
			i->looper = looper;
			looper->Lock();
		}
	}
}


/** @brief Checks whether a looper is registered in the list.
 *  @param looper The BLooper pointer to search for.
 *  @return true if the looper is found in the list, false otherwise.
 */
bool
BLooperList::IsLooperValid(const BLooper* looper)
{
	ReadLocker locker(fLock);
	return find_if(fData.begin(), fData.end(),
		FindLooperPred(looper)) != fData.end();
}


/** @brief Removes a looper from the list by setting its slot to NULL.
 *  @param looper The BLooper to remove.
 *  @return true if the looper was found and removed, false otherwise.
 */
bool
BLooperList::RemoveLooper(BLooper* looper)
{
	WriteLocker locker(fLock);
	AssertLocked();

	LooperDataIterator i = find_if(fData.begin(), fData.end(),
		FindLooperPred(looper));
	if (i != fData.end()) {
		i->looper = NULL;
		return true;
	}

	return false;
}


/** @brief Populates a BList with all active loopers.
 *  @param list The BList to fill with BLooper pointers. Non-NULL entries
 *         from the internal list are appended.
 */
void
BLooperList::GetLooperList(BList* list)
{
	ReadLocker locker(fLock);

	for (uint32 i = 0; i < fData.size(); ++i) {
		if (fData[i].looper)
			list->AddItem(fData[i].looper);
	}
}


/** @brief Returns the total number of slots in the looper list (including empty slots).
 *  @return The number of slots in the internal vector.
 */
int32
BLooperList::CountLoopers()
{
	ReadLocker locker(fLock);
	return (int32)fData.size();
}


/** @brief Returns the looper at the given index.
 *  @param index The zero-based index into the looper list.
 *  @return The BLooper at the given index, or NULL if out of range or empty.
 */
BLooper*
BLooperList::LooperAt(int32 index)
{
	ReadLocker locker(fLock);

	BLooper* looper = NULL;
	if (index < (int32)fData.size())
		looper = fData[(uint32)index].looper;

	return looper;
}


/** @brief Finds the looper running on the specified thread.
 *  @param thread The thread ID to search for.
 *  @return The BLooper associated with the thread, or NULL if not found.
 */
BLooper*
BLooperList::LooperForThread(thread_id thread)
{
	ReadLocker locker(fLock);

	BLooper* looper = NULL;
	LooperDataIterator i
		= find_if(fData.begin(), fData.end(), FindThreadPred(thread));
	if (i != fData.end())
		looper = i->looper;

	return looper;
}


/** @brief Finds a looper by its name.
 *  @param name The name string to match against looper names.
 *  @return The first BLooper with a matching name, or NULL if not found.
 */
BLooper*
BLooperList::LooperForName(const char* name)
{
	ReadLocker locker(fLock);

	BLooper* looper = NULL;
	LooperDataIterator i
		= find_if(fData.begin(), fData.end(), FindNamePred(name));
	if (i != fData.end())
		looper = i->looper;

	return looper;
}


/** @brief Finds the looper associated with the specified port.
 *  @param port The port ID to search for.
 *  @return The BLooper using the given port, or NULL if not found.
 */
BLooper*
BLooperList::LooperForPort(port_id port)
{
	ReadLocker locker(fLock);

	BLooper* looper = NULL;
	LooperDataIterator i
		= find_if(fData.begin(), fData.end(), FindPortPred(port));
	if (i != fData.end())
		looper = i->looper;

	return looper;
}


/** @brief Reinitializes the looper list after a fork.
 *
 *  Reinitializes the reader-writer lock and clears all looper entries,
 *  since looper threads from the parent process are not valid in the child.
 */
void
BLooperList::InitAfterFork()
{
	rw_lock_init(&fLock, "BLooperList lock");
	fData.clear();
}


/** @brief Predicate that tests whether a slot is empty (looper is NULL).
 *  @param data The LooperData entry to test.
 *  @return true if the slot has no looper, false otherwise.
 */
bool
BLooperList::EmptySlotPred(LooperData& data)
{
	return data.looper == NULL;
}


/** @brief Asserts that the looper list is currently write-locked.
 *
 *  Invokes the debugger if the list is not locked by the current thread.
 */
void
BLooperList::AssertLocked()
{
	if (!IsLocked())
		debugger("looperlist is not locked; proceed at great risk!");
}


//	#pragma mark - BLooperList::LooperData


/** @brief Default constructor, initializes the looper pointer to NULL. */
BLooperList::LooperData::LooperData()
	:
	looper(NULL)
{
}


/** @brief Constructs a LooperData entry for the given looper.
 *  @param looper The BLooper to store in this entry.
 */
BLooperList::LooperData::LooperData(BLooper* looper)
	:
	looper(looper)
{
}


/** @brief Copy constructor.
 *  @param other The LooperData to copy from.
 */
BLooperList::LooperData::LooperData(const LooperData& other)
{
	*this = other;
}


/** @brief Assignment operator.
 *  @param other The LooperData to assign from.
 *  @return Reference to this LooperData.
 */
BLooperList::LooperData&
BLooperList::LooperData::operator=(const LooperData& other)
{
	if (this != &other)
		looper = other.looper;

	return *this;
}


/** @brief Tests whether the given data entry matches the target looper pointer.
 *  @param data The LooperData entry to test.
 *  @return true if the entry's looper matches, false otherwise.
 */
bool
BLooperList::FindLooperPred::operator()(BLooperList::LooperData& data)
{
	return data.looper && looper == data.looper;
}


/** @brief Tests whether the given data entry's looper runs on the target thread.
 *  @param data The LooperData entry to test.
 *  @return true if the entry's looper thread matches, false otherwise.
 */
bool
BLooperList::FindThreadPred::operator()(LooperData& data)
{
	return data.looper && thread == data.looper->Thread();
}


/** @brief Tests whether the given data entry's looper has the target name.
 *  @param data The LooperData entry to test.
 *  @return true if the entry's looper name matches, false otherwise.
 */
bool
BLooperList::FindNamePred::operator()(LooperData& data)
{
	return data.looper && !strcmp(name, data.looper->Name());
}


/** @brief Tests whether the given data entry's looper uses the target port.
 *  @param data The LooperData entry to test.
 *  @return true if the entry's looper port matches, false otherwise.
 */
bool
BLooperList::FindPortPred::operator()(LooperData& data)
{
	return data.looper && port == _get_looper_port_(data.looper);
}

}	// namespace BPrivate

