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
 *   Copyright 2024, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file TimedEventQueue.cpp
 *  @brief Implements the time-ordered media event queue used by BMediaEventLooper. */


#include <TimedEventQueue.h>

#include <Autolock.h>
#include <Buffer.h>
#include <MediaDebug.h>
#include <InterfaceDefs.h>
#include <util/DoublyLinkedList.h>
#include <locks.h>


//	#pragma mark - media_timed_event


/** @brief Default constructor; zero-initialises all fields of the event. */
media_timed_event::media_timed_event()
{
	CALLED();
	memset(this, 0, sizeof(*this));
}


/** @brief Constructs a timed event with a time and type, zeroing all other fields.
 *
 *  @param inTime The performance time at which the event should fire.
 *  @param inType The event type (one of the BTimedEventQueue constants). */
media_timed_event::media_timed_event(bigtime_t inTime, int32 inType)
{
	CALLED();
	memset(this, 0, sizeof(*this));

	event_time = inTime;
	type = inType;
}


/** @brief Constructs a timed event with time, type, a pointer, and a cleanup policy.
 *
 *  @param inTime    The performance time at which the event should fire.
 *  @param inType    The event type.
 *  @param inPointer User-defined pointer (e.g. a BBuffer*).
 *  @param inCleanup Cleanup policy (e.g. BTimedEventQueue::B_RECYCLE_BUFFER). */
media_timed_event::media_timed_event(bigtime_t inTime, int32 inType,
	void* inPointer, uint32 inCleanup)
{
	CALLED();
	memset(this, 0, sizeof(*this));

	event_time = inTime;
	type = inType;
	pointer = inPointer;
	cleanup = inCleanup;
}


/** @brief Constructs a fully specified timed event.
 *
 *  @param inTime     The performance time at which the event should fire.
 *  @param inType     The event type.
 *  @param inPointer  User-defined pointer.
 *  @param inCleanup  Cleanup policy.
 *  @param inData     32-bit user data field.
 *  @param inBigdata  64-bit user data field.
 *  @param inUserData Pointer to arbitrary user data bytes.
 *  @param dataSize   Number of bytes to copy from \a inUserData (clamped to user_data capacity). */
media_timed_event::media_timed_event(bigtime_t inTime, int32 inType,
	void* inPointer, uint32 inCleanup,
	int32 inData, int64 inBigdata,
	const char* inUserData,  size_t dataSize)
{
	CALLED();
	memset(this, 0, sizeof(*this));

	event_time = inTime;
	type = inType;
	pointer = inPointer;
	cleanup = inCleanup;
	data = inData;
	bigdata = inBigdata;
	memcpy(user_data, inUserData,
		min_c(sizeof(media_timed_event::user_data), dataSize));
}


/** @brief Copy constructor; performs a bitwise copy of all fields.
 *  @param clone The event to copy. */
media_timed_event::media_timed_event(const media_timed_event& clone)
{
	CALLED();
	*this = clone;
}


/** @brief Assignment operator; performs a bitwise copy of all fields.
 *  @param clone The event to copy. */
void
media_timed_event::operator=(const media_timed_event& clone)
{
	CALLED();
	memcpy(this, &clone, sizeof(*this));
}


/** @brief Destructor for media_timed_event. */
media_timed_event::~media_timed_event()
{
	CALLED();
}


//	#pragma mark - media_timed_event global operators


/** @brief Returns true if two timed events are bitwise equal.
 *  @param a Left-hand event.
 *  @param b Right-hand event.
 *  @return true if all fields match. */
bool
operator==(const media_timed_event& a, const media_timed_event& b)
{
	CALLED();
	return (memcmp(&a, &b, sizeof(media_timed_event)) == 0);
}


/** @brief Returns true if two timed events differ in any field.
 *  @param a Left-hand event.
 *  @param b Right-hand event.
 *  @return true if any field differs. */
bool
operator!=(const media_timed_event& a, const media_timed_event& b)
{
	CALLED();
	return (memcmp(&a, &b, sizeof(media_timed_event)) != 0);
}


/** @brief Returns true if event \a a fires before event \a b.
 *  @param a Left-hand event.
 *  @param b Right-hand event.
 *  @return true if a.event_time < b.event_time. */
bool
operator<(const media_timed_event& a, const media_timed_event& b)
{
	CALLED();
	return a.event_time < b.event_time;
}


/** @brief Returns true if event \a a fires after event \a b.
 *  @param a Left-hand event.
 *  @param b Right-hand event.
 *  @return true if a.event_time > b.event_time. */
bool
operator>(const media_timed_event& a, const media_timed_event& b)
{
	CALLED();
	return a.event_time > b.event_time;
}


//	#pragma mark - BTimedEventQueue


namespace {

struct queue_entry : public DoublyLinkedListLinkImpl<queue_entry> {
	media_timed_event	event;
};
typedef DoublyLinkedList<queue_entry> QueueEntryList;

} // namespace


class BPrivate::TimedEventQueueData {
public:
	TimedEventQueueData()
		:
		fLock("BTimedEventQueue")
	{
		mutex_init(&fEntryAllocationLock, "BTimedEventQueue entry allocation");

		fEventCount = 0;
		fCleanupHook = NULL;
		fCleanupHookContext = NULL;

		for (size_t i = 0; i < B_COUNT_OF(fInlineEntries); i++)
			fFreeEntries.Add(&fInlineEntries[i]);
	}
	~TimedEventQueueData()
	{
		while (queue_entry* chunk = fChunkHeads.RemoveHead())
			free(chunk);
	}

	queue_entry* AllocateEntry()
	{
		MutexLocker locker(fEntryAllocationLock);
		if (fFreeEntries.Head() != NULL)
			return fFreeEntries.RemoveHead();

		// We need a new chunk.
		const size_t chunkSize = B_PAGE_SIZE;
		queue_entry* newEntries = (queue_entry*)malloc(chunkSize);
		fChunkHeads.Add(&newEntries[0]);
		for (size_t i = 1; i < (chunkSize / sizeof(queue_entry)); i++)
			fFreeEntries.Add(&newEntries[i]);

		return fFreeEntries.RemoveHead();
	}

	void FreeEntry(queue_entry* entry)
	{
		MutexLocker locker(fEntryAllocationLock);
		fFreeEntries.Add(entry);

		// TODO: Chunks are currently only freed in the destructor.
		// (Is that a problem? They're probably rarely used, anyway.)
	}

	status_t	AddEntry(queue_entry* newEntry);
	void		RemoveEntry(queue_entry* newEntry);
	void		CleanupAndFree(queue_entry* entry);

public:
	BLocker				fLock;
	QueueEntryList		fEvents;
	int32				fEventCount;

	BTimedEventQueue::cleanup_hook 	fCleanupHook;
	void* 				fCleanupHookContext;

private:
	mutex				fEntryAllocationLock;
	QueueEntryList		fFreeEntries;
	QueueEntryList		fChunkHeads;
	queue_entry			fInlineEntries[8];
};

using BPrivate::TimedEventQueueData;


/** @brief Default constructor; allocates the internal data structure. */
BTimedEventQueue::BTimedEventQueue()
	: fData(new TimedEventQueueData)
{
	CALLED();
}


/** @brief Destructor; flushes all events with cleanup, then deletes the internal data. */
BTimedEventQueue::~BTimedEventQueue()
{
	CALLED();

	FlushEvents(0, B_ALWAYS);
	delete fData;
}


/** @brief Inserts a timed event into the queue in chronological order.
 *
 *  @param event The event to add; its type must be >= B_START.
 *  @return B_OK on success, B_BAD_VALUE if the type is too small, or B_NO_MEMORY. */
status_t
BTimedEventQueue::AddEvent(const media_timed_event& event)
{
	CALLED();

	if (event.type < B_START)
		return B_BAD_VALUE;

	queue_entry* newEntry = fData->AllocateEntry();
	if (newEntry == NULL)
		return B_NO_MEMORY;

	newEntry->event = event;

	BAutolock locker(fData->fLock);
	return fData->AddEntry(newEntry);
}


/** @brief Removes the first event that bitwise-matches \a event from the queue without cleanup.
 *
 *  @param event Pointer to the event to search for and remove.
 *  @return B_OK on success, or B_ERROR if the event was not found. */
status_t
BTimedEventQueue::RemoveEvent(const media_timed_event* event)
{
	CALLED();
	BAutolock locker(fData->fLock);

	QueueEntryList::Iterator it = fData->fEvents.GetIterator();
	while (queue_entry* entry = it.Next()) {
		if (entry->event != *event)
			continue;

		fData->RemoveEntry(entry);

		locker.Unlock();

		// No cleanup.
		fData->FreeEntry(entry);
		return B_OK;
	}

	return B_ERROR;
}


/** @brief Removes the earliest event from the queue.
 *
 *  If \a _event is non-NULL, the event is returned without cleanup; otherwise
 *  the event is cleaned up and freed.
 *
 *  @param _event Out-parameter that receives the removed event, or NULL to discard with cleanup.
 *  @return B_OK on success, or B_ERROR if the queue is empty. */
status_t
BTimedEventQueue::RemoveFirstEvent(media_timed_event* _event)
{
	CALLED();
	BAutolock locker(fData->fLock);

	if (fData->fEventCount == 0)
		return B_ERROR;

	queue_entry* entry = fData->fEvents.Head();
	fData->RemoveEntry(entry);

	locker.Unlock();

	if (_event != NULL) {
		// No cleanup.
		*_event = entry->event;
		fData->FreeEntry(entry);
	} else {
		fData->CleanupAndFree(entry);
	}
	return B_OK;
}


/** @brief Inserts a queue_entry into the event list maintaining chronological order.
 *
 *  @param newEntry The pre-filled queue_entry to insert.
 *  @return B_OK on success, or B_ERROR if the queue is in an invalid state. */
status_t
TimedEventQueueData::AddEntry(queue_entry* newEntry)
{
	ASSERT(fLock.IsLocked());

	if (fEvents.IsEmpty()) {
		fEvents.Add(newEntry);
		fEventCount++;
		return B_OK;
	}
	if (fEvents.First()->event.event_time > newEntry->event.event_time) {
		fEvents.Add(newEntry, false);
		fEventCount++;
		return B_OK;
	}

	QueueEntryList::ReverseIterator it = fEvents.GetReverseIterator();
	while (queue_entry* entry = it.Next()) {
		if (newEntry->event.event_time < entry->event.event_time)
			continue;

		// Insert the new event after this entry.
		fEvents.InsertAfter(entry, newEntry);
		fEventCount++;
		return B_OK;
	}

	debugger("BTimedEventQueue::AddEvent: Invalid queue!");
	return B_ERROR;
}


/** @brief Removes a queue_entry from the event list and decrements the event count.
 *
 *  @param entry The entry to remove; must be present in the list. */
void
TimedEventQueueData::RemoveEntry(queue_entry* entry)
{
	ASSERT(fLock.IsLocked());

	fEvents.Remove(entry);
	fEventCount--;
}


/** @brief Performs the cleanup action for an event and frees the entry.
 *
 *  Handles B_DELETE (treated as B_USER_CLEANUP), B_NO_CLEANUP (no-op),
 *  B_RECYCLE_BUFFER, B_EXPIRE_TIMER, and B_USER_CLEANUP (calls the hook).
 *
 *  @param entry The entry to clean up and free. */
void
TimedEventQueueData::CleanupAndFree(queue_entry* entry)
{
	uint32 cleanup = entry->event.cleanup;
	if (cleanup == B_DELETE) {
		// B_DELETE is a keyboard code, but the Be Book indicates it's a valid
		// cleanup value. (Early sample code may have used it too.)
		cleanup = BTimedEventQueue::B_USER_CLEANUP;
	}

	if (cleanup == BTimedEventQueue::B_NO_CLEANUP) {
		// Nothing to do.
	} else if (entry->event.type == BTimedEventQueue::B_HANDLE_BUFFER
			&& cleanup == BTimedEventQueue::B_RECYCLE_BUFFER) {
		(reinterpret_cast<BBuffer*>(entry->event.pointer))->Recycle();
	} else if (cleanup == BTimedEventQueue::B_EXPIRE_TIMER) {
		// TimerExpired() is invoked in BMediaEventLooper::DispatchEvent; nothing to do.
	} else if (cleanup >= BTimedEventQueue::B_USER_CLEANUP) {
		if (fCleanupHook != NULL)
			(*fCleanupHook)(&entry->event, fCleanupHookContext);
	} else {
		ERROR("BTimedEventQueue: Unhandled cleanup! (type %" B_PRId32 ", "
			"cleanup %" B_PRId32 ")\n", entry->event.type, entry->event.cleanup);
	}

	FreeEntry(entry);
}


/** @brief Registers a hook function called when events with user-defined cleanup are flushed.
 *
 *  @param hook    The cleanup_hook function pointer, or NULL to clear.
 *  @param context Opaque context pointer forwarded to \a hook on each invocation. */
void
BTimedEventQueue::SetCleanupHook(cleanup_hook hook, void* context)
{
	CALLED();

	BAutolock lock(fData->fLock);
	fData->fCleanupHook = hook;
	fData->fCleanupHookContext = context;
}


/** @brief Returns true if the queue contains at least one event.
 *  @return true if EventCount() > 0. */
bool
BTimedEventQueue::HasEvents() const
{
	CALLED();

	BAutolock locker(fData->fLock);
	return fData->fEventCount != 0;
}


/** @brief Returns the number of events currently in the queue.
 *  @return The event count. */
int32
BTimedEventQueue::EventCount() const
{
	CALLED();

	BAutolock locker(fData->fLock);
	return fData->fEventCount;
}


/** @brief Returns a pointer to the earliest event without removing it.
 *  @return Pointer to the first media_timed_event, or NULL if the queue is empty. */
const media_timed_event*
BTimedEventQueue::FirstEvent() const
{
	CALLED();
	BAutolock locker(fData->fLock);

	queue_entry* entry = fData->fEvents.First();
	if (entry == NULL)
		return NULL;
	return &entry->event;
}


/** @brief Returns the performance time of the earliest event.
 *  @return The event_time of the first event, or B_INFINITE_TIMEOUT if the queue is empty. */
bigtime_t
BTimedEventQueue::FirstEventTime() const
{
	CALLED();
	BAutolock locker(fData->fLock);

	queue_entry* entry = fData->fEvents.First();
	if (entry == NULL)
		return B_INFINITE_TIMEOUT;
	return entry->event.event_time;
}


/** @brief Returns a pointer to the latest event without removing it.
 *  @return Pointer to the last media_timed_event, or NULL if the queue is empty. */
const media_timed_event*
BTimedEventQueue::LastEvent() const
{
	CALLED();
	BAutolock locker(fData->fLock);

	queue_entry* entry = fData->fEvents.Last();
	if (entry == NULL)
		return NULL;
	return &entry->event;
}


/** @brief Returns the performance time of the latest event.
 *  @return The event_time of the last event, or B_INFINITE_TIMEOUT if the queue is empty. */
bigtime_t
BTimedEventQueue::LastEventTime() const
{
	CALLED();
	BAutolock locker(fData->fLock);

	queue_entry* entry = fData->fEvents.Last();
	if (entry == NULL)
		return B_INFINITE_TIMEOUT;
	return entry->event.event_time;
}


/** @brief Finds the first event matching the given time, direction, and type criteria.
 *
 *  @param eventTime  Reference time for the search.
 *  @param direction  One of B_BEFORE_TIME, B_AT_TIME, B_AFTER_TIME, or B_ALWAYS.
 *  @param inclusive  Whether the event at exactly \a eventTime qualifies.
 *  @param eventType  Required event type, or B_ANY_EVENT to match all types.
 *  @return Pointer to the matching event, or NULL if none found. */
const media_timed_event*
BTimedEventQueue::FindFirstMatch(bigtime_t eventTime,
	time_direction direction, bool inclusive, int32 eventType)
{
	CALLED();
	BAutolock locker(fData->fLock);

	QueueEntryList::Iterator it = fData->fEvents.GetIterator();
	while (queue_entry* entry = it.Next()) {
		int match = _Match(entry->event, eventTime, direction, inclusive, eventType);
		if (match == B_DONE)
			break;
		if (match == B_NO_ACTION)
			continue;

		return &entry->event;
	}

	return NULL;
}


/** @brief Iterates over matching events and calls \a hook on each one.
 *
 *  The hook may return B_REMOVE_EVENT (removes and cleans up), B_RESORT_QUEUE
 *  (re-sorts after all iterations), B_DONE (stop), or B_NO_ACTION (continue).
 *
 *  @param hook       Callback invoked for each matching event.
 *  @param context    Opaque context forwarded to \a hook.
 *  @param eventTime  Reference time for matching.
 *  @param direction  Matching direction relative to \a eventTime.
 *  @param inclusive  Whether events at exactly \a eventTime qualify.
 *  @param eventType  Required event type, or B_ANY_EVENT.
 *  @return B_OK always. */
status_t
BTimedEventQueue::DoForEach(for_each_hook hook, void* context,
	bigtime_t eventTime, time_direction direction,
	bool inclusive, int32 eventType)
{
	CALLED();
	BAutolock locker(fData->fLock);

	bool resort = false;

	QueueEntryList::Iterator it = fData->fEvents.GetIterator();
	while (queue_entry* entry = it.Next()) {
		int match = _Match(entry->event, eventTime, direction, inclusive, eventType);
		if (match == B_DONE)
			break;
		if (match == B_NO_ACTION)
			continue;

		queue_action action = hook(&entry->event, context);
		if (action == B_DONE)
			break;

		switch (action) {
			case B_REMOVE_EVENT:
				fData->RemoveEntry(entry);
				fData->CleanupAndFree(entry);
				break;

			case B_RESORT_QUEUE:
				resort = true;
				break;

			case B_NO_ACTION:
			default:
				break;
		}
	}

	if (resort) {
		QueueEntryList entries;
		entries.TakeFrom(&fData->fEvents);
		fData->fEventCount = 0;

		while (queue_entry* entry = entries.RemoveHead())
			fData->AddEntry(entry);
	}

	return B_OK;
}


/** @brief Removes and cleans up all events matching the given time, direction, and type criteria.
 *
 *  @param eventTime  Reference time for matching.
 *  @param direction  Matching direction relative to \a eventTime.
 *  @param inclusive  Whether events at exactly \a eventTime qualify.
 *  @param eventType  Required event type, or B_ANY_EVENT.
 *  @return B_OK always. */
status_t
BTimedEventQueue::FlushEvents(bigtime_t eventTime, time_direction direction,
	bool inclusive, int32 eventType)
{
	CALLED();
	BAutolock locker(fData->fLock);

	QueueEntryList::Iterator it = fData->fEvents.GetIterator();
	while (queue_entry* entry = it.Next()) {
		int match = _Match(entry->event, eventTime, direction, inclusive, eventType);
		if (match == B_DONE)
			break;
		if (match == B_NO_ACTION)
			continue;

		fData->RemoveEntry(entry);
		fData->CleanupAndFree(entry);
	}

	return B_OK;
}


/** @brief Internal helper: tests whether a single event satisfies the match criteria.
 *
 *  @param event      The event to test.
 *  @param eventTime  Reference time.
 *  @param direction  One of B_BEFORE_TIME, B_AT_TIME, B_AFTER_TIME, or B_ALWAYS.
 *  @param inclusive  Whether events at exactly \a eventTime qualify.
 *  @param eventType  Required event type, or B_ANY_EVENT.
 *  @return 1 if the event matches, B_NO_ACTION to skip it, or B_DONE to stop iteration. */
int
BTimedEventQueue::_Match(const media_timed_event& event,
	bigtime_t eventTime, time_direction direction,
	bool inclusive, int32 eventType)
{
	if (direction == B_ALWAYS) {
		// Nothing to check.
	} else if (direction == B_BEFORE_TIME) {
		if (event.event_time > eventTime)
			return B_DONE;
		if (event.event_time == eventTime && !inclusive)
			return B_DONE;
	} else if (direction == B_AT_TIME) {
		if (event.event_time > eventTime)
			return B_DONE;
		if (event.event_time != eventTime)
			return B_NO_ACTION;
	} else if (direction == B_AFTER_TIME) {
		if (event.event_time < eventTime)
			return B_NO_ACTION;
		if (event.event_time == eventTime && !inclusive)
			return B_NO_ACTION;
	}

	if (eventType != B_ANY_EVENT && eventType != event.type)
		return B_NO_ACTION;

	return 1;
}


//	#pragma mark - C++ binary compatibility


/** @brief Custom operator new for binary-compatible heap allocation.
 *  @param size Number of bytes to allocate.
 *  @return Pointer to allocated memory. */
void*
BTimedEventQueue::operator new(size_t size)
{
	CALLED();
	return ::operator new(size);
}


/** @brief Custom operator delete for binary-compatible deallocation.
 *  @param ptr Pointer to the memory to free.
 *  @param s   Size (unused). */
void
BTimedEventQueue::operator delete(void* ptr, size_t s)
{
	CALLED();
	return ::operator delete(ptr);
}


/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue0() {}
/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue1() {}
/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue2() {}
/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue3() {}
/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue4() {}
/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue5() {}
/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue6() {}
/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue7() {}
/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue8() {}
/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue9() {}
/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue10() {}
/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue11() {}
/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue12() {}
/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue13() {}
/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue14() {}
/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue15() {}
/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue16() {}
/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue17() {}
/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue18() {}
/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue19() {}
/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue20() {}
/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue21() {}
/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue22() {}
/** @brief Reserved for future binary compatibility. */
void BTimedEventQueue::_ReservedTimedEventQueue23() {}
