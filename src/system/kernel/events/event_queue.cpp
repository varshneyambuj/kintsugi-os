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
 *   Copyright 2015, Hamish Morrison, hamishm53@gmail.com.
 *   Copyright 2023, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file event_queue.cpp
 * @brief kqueue-style event queue primitive for the kernel.
 *
 * Implements the EventQueue object exposed as a file descriptor to user space.
 * Callers register (object, type) selections with desired event flags and
 * behaviors (level-triggered, one-shot), then wait on the queue for ready
 * events. Internally an AVL tree indexes active select_event records by
 * (object, type) while a doubly-linked list holds events currently queued as
 * ready. Events are fed by the generic select_sync Notify() path so any kernel
 * object that supports select_object()/deselect_object() can drive the queue.
 */

#include <event_queue.h>

#include <OS.h>

#include <AutoDeleter.h>

#include <fs/fd.h>
#include <port.h>
#include <sem.h>
#include <syscalls.h>
#include <syscall_restart.h>
#include <thread.h>
#include <util/AutoLock.h>
#include <util/AVLTree.h>
#include <util/DoublyLinkedList.h>
#include <AutoDeleterDrivers.h>
#include <StackOrHeapArray.h>
#include <wait_for_objects.h>

#include "select_ops.h"
#include "select_sync.h"


enum {
	B_EVENT_QUEUED			= (1 << 28),
	B_EVENT_SELECTING		= (1 << 29),
	B_EVENT_DELETING		= (1 << 30),
	/* (signed) */
	B_EVENT_PRIVATE_MASK	= (0xf0000000)
};


#define EVENT_BEHAVIOR(events) ((events) & (B_EVENT_LEVEL_TRIGGERED | B_EVENT_ONE_SHOT))
#define USER_EVENTS(events) ((events) & ~B_EVENT_PRIVATE_MASK)

#define B_EVENT_NON_MASKABLE (B_EVENT_INVALID | B_EVENT_ERROR | B_EVENT_DISCONNECTED)



struct select_event : select_info, AVLTreeNode,
		DoublyLinkedListLinkImpl<select_event> {
	int32				object;
	uint16				type;
	uint32				behavior;
	void*				user_data;
};


struct EventQueueTreeDefinition {
	typedef struct {
		int32 object;
		uint16 type;
	} 						Key;
	typedef select_event	Value;

	AVLTreeNode* GetAVLTreeNode(Value* value) const
	{
		return value;
	}

	Value* GetValue(AVLTreeNode* node) const
	{
		return static_cast<Value*>(node);
	}

	int Compare(Key a, const Value* b) const
	{
		if (a.object != b->object)
			return a.object - b->object;
		else
			return a.type - b->type;
	}

	int Compare(const Value* a, const Value* b) const
	{
		if (a->object != b->object)
			return a->object - b->object;
		else
			return a->type - b->type;
	}
};


//	#pragma mark -- EventQueue implementation


class EventQueue : public select_sync {
public:
						EventQueue(bool kernel);
						~EventQueue();

	void				Closed();

	status_t			Select(int32 object, uint16 type, uint32 events, void* userData);
	status_t			Query(int32 object, uint16 type, uint32* selectedEvents, void** userData);
	status_t			Deselect(int32 object, uint16 type);

	status_t			Notify(select_info* info, uint16 events);

	ssize_t				Wait(event_wait_info* infos, int numInfos,
							int32 flags, bigtime_t timeout);

private:
	void				_Notify(select_event* event, uint16 events);
	status_t			_DeselectEvent(select_event* event);

	ssize_t				_DequeueEvents(event_wait_info* infos, int numInfos);

	select_event*		_GetEvent(int32 object, uint16 type);

private:
	typedef AVLTree<EventQueueTreeDefinition> EventTree;
	typedef DoublyLinkedList<select_event> EventList;

	bool				fKernel;
	bool				fClosing;

	/*
	 * This flag is set in _DequeueEvents when we have to drop the lock to
	 * deselect an object to prevent another _DequeueEvents call concurrently
	 * modifying the list.
	 */
	bool				fDequeueing;

	EventList			fEventList;
	EventTree			fEventTree;

	/*
	 * Protects the queue. We cannot call select or deselect while holding
	 * this, because it will invert the locking order with EventQueue::Notify.
	 */
	mutex				fQueueLock;

	/*
	 * Notified when events are available on the queue.
	 */
	ConditionVariable	fQueueCondition;

	/*
	 * Used to wait on a changing select_event while the queue lock is dropped
	 * during a call to select/deselect.
	 */
	ConditionVariable	fEventCondition;
};


/**
 * @brief Constructs an empty event queue.
 *
 * Initializes the queue lock and the two condition variables used to wake
 * waiters and to serialize concurrent select/deselect transitions on an
 * individual select_event.
 *
 * @param kernel True if the queue is created for kernel use, false for a
 *     user-space owned queue; propagated to select_object()/deselect_object().
 */
EventQueue::EventQueue(bool kernel)
	:
	fKernel(kernel),
	fClosing(false),
	fDequeueing(false)
{
	mutex_init(&fQueueLock, "event_queue lock");
	fQueueCondition.Init(this, "evtq wait");
	fEventCondition.Init(this, "event_queue event change wait");
}


/**
 * @brief Destroys the queue and all remaining select_event records.
 *
 * Must be invoked only after Closed() has been called. Walks the event tree
 * deselecting each registered object, then frees any list-only remnants and
 * destroys the queue lock.
 */
EventQueue::~EventQueue()
{
	mutex_lock(&fQueueLock);
	ASSERT(fClosing && !fDequeueing);

	EventTree::Iterator iter = fEventTree.GetIterator();
	while (iter.HasNext()) {
		select_event* event = iter.Next();
		event->events |= B_EVENT_DELETING;

		mutex_unlock(&fQueueLock);
		_DeselectEvent(event);
		mutex_lock(&fQueueLock);

		iter.Remove();
		if ((event->events & B_EVENT_QUEUED) != 0)
			fEventList.Remove(event);
		delete event;
	}

	EventList::Iterator listIter = fEventList.GetIterator();
	while (listIter.HasNext()) {
		select_event* event = listIter.Next();

		// We already removed all events in the tree from this list.
		// The only remaining events will be INVALID ones already deselected.
		delete event;
	}

	mutex_destroy(&fQueueLock);
}


/**
 * @brief Marks the queue as closing and wakes every waiting Wait() call.
 *
 * After this returns, new Wait() calls return B_FILE_ERROR. The destructor
 * later relies on fClosing being set.
 */
void
EventQueue::Closed()
{
	MutexLocker locker(&fQueueLock);

	fClosing = true;
	locker.Unlock();

	// Wake up all waiters
	fQueueCondition.NotifyAll(B_FILE_ERROR);
}


/**
 * @brief Registers or updates interest in events for an (object, type) pair.
 *
 * If the event is already registered with the same flags/behavior, this is a
 * no-op. Otherwise any existing registration is deselected and a fresh
 * select_event is inserted in the AVL tree, the queue lock is dropped, and the
 * underlying select_object() callback is invoked. The B_EVENT_SELECTING flag
 * shields the in-progress event from concurrent use or deletion.
 *
 * @param object   Kernel object identifier (fd, port, sem, thread, ...).
 * @param type     Selector type identifying the object's kind.
 * @param events   Selected event mask combined with B_EVENT_LEVEL_TRIGGERED or
 *                 B_EVENT_ONE_SHOT behavior bits.
 * @param userData Opaque cookie returned in event_wait_info::user_data.
 * @return B_OK on success, EEXIST if it was concurrently re-selected,
 *     B_NO_MEMORY on allocation failure, or any status_t from select_object().
 */
status_t
EventQueue::Select(int32 object, uint16 type, uint32 events, void* userData)
{
	MutexLocker locker(&fQueueLock);

	select_event* event = _GetEvent(object, type);
	if (event != NULL) {
		if ((event->selected_events | event->behavior)
				== (USER_EVENTS(events) | B_EVENT_NON_MASKABLE))
			return B_OK;

		// Rather than try to reuse the event object, which would be complicated
		// and error-prone, perform a full de-selection and then re-selection.
		locker.Unlock();
		status_t status = Deselect(object, type);
		if (status != B_OK)
			return status;
		locker.Lock();

		// Make sure nothing else re-selected before we reacquired the lock.
		event = _GetEvent(object, type);
		if (event != NULL)
			return EEXIST;
	}

	event = new(std::nothrow) select_event;
	if (event == NULL)
		return B_NO_MEMORY;
	ObjectDeleter<select_event> eventDeleter(event);

	event->sync = this;
	event->object = object;
	event->type = type;
	event->behavior = EVENT_BEHAVIOR(events);
	event->user_data = userData;
	event->events = 0;

	status_t result = fEventTree.Insert(event);
	if (result != B_OK)
		return result;

	// We drop the lock before calling select() to avoid inverting the
	// locking order with Notify(). Setting the B_EVENT_SELECTING flag prevents
	// the event from being used or even deleted before it is ready.
	event->events |= B_EVENT_SELECTING;
	event->selected_events = USER_EVENTS(events) | B_EVENT_NON_MASKABLE;

	locker.Unlock();

	status_t status = select_object(event->type, event->object, event, fKernel);
	if (status < 0) {
		locker.Lock();
		fEventTree.Remove(event);
		fEventCondition.NotifyAll();
		return status;
	}

	eventDeleter.Detach();

	atomic_and(&event->events, ~B_EVENT_SELECTING);
	fEventCondition.NotifyAll();

	return B_OK;
}


/**
 * @brief Retrieves the currently registered event mask and user cookie.
 *
 * @param object         Kernel object identifier to look up.
 * @param type           Selector type identifying the object's kind.
 * @param selectedEvents Out parameter: combined selected events and behavior.
 * @param userData       Out parameter: the cookie passed to Select().
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no such registration exists.
 */
status_t
EventQueue::Query(int32 object, uint16 type, uint32* selectedEvents, void** userData)
{
	MutexLocker locker(&fQueueLock);

	select_event* event = _GetEvent(object, type);
	if (event == NULL)
		return B_ENTRY_NOT_FOUND;

	*selectedEvents = event->selected_events | event->behavior;
	*userData = event->user_data;

	return B_OK;
}


/**
 * @brief Removes a previously registered (object, type) selection.
 *
 * Marks the event with B_EVENT_DELETING, calls deselect_object() with the
 * queue lock dropped, then removes the event from both the AVL tree and the
 * ready list before freeing it. Safe against concurrent deletion.
 *
 * @param object Kernel object identifier.
 * @param type   Selector type identifying the object's kind.
 * @return B_OK on success or when a delete is already in progress,
 *     B_ENTRY_NOT_FOUND if no such registration exists.
 */
status_t
EventQueue::Deselect(int32 object, uint16 type)
{
	MutexLocker locker(&fQueueLock);

	select_event* event = _GetEvent(object, type);
	if (event == NULL)
		return B_ENTRY_NOT_FOUND;

	if ((atomic_or(&event->events, B_EVENT_DELETING) & B_EVENT_DELETING) != 0)
		return B_OK;

	locker.Unlock();
	_DeselectEvent(event);
	locker.Lock();

	if ((event->events & B_EVENT_INVALID) == 0)
		fEventTree.Remove(event);
	if ((event->events & B_EVENT_QUEUED) != 0)
		fEventList.Remove(event);

	delete event;

	locker.Unlock();
	fEventCondition.NotifyAll();
	return B_OK;
}


/**
 * @brief Dispatches a deselect_object() call for the given event.
 *
 * Helper used from both Deselect() and ~EventQueue().
 *
 * @param event The select_event to tear down.
 * @return Status returned by deselect_object().
 */
status_t
EventQueue::_DeselectEvent(select_event* event)
{
	return deselect_object(event->type, event->object, event, fKernel);
}


/**
 * @brief select_sync Notify() override invoked by underlying kernel objects.
 *
 * Downcasts to select_event and forwards to _Notify().
 *
 * @param info   select_info pointer known to be a select_event for this queue.
 * @param events Event flags being signalled.
 * @return B_OK.
 */
status_t
EventQueue::Notify(select_info* info, uint16 events)
{
	select_event* event = static_cast<select_event*>(info);
	_Notify(event, events);
	return B_OK;
}


/**
 * @brief Core event delivery: marks an event ready and enqueues it if needed.
 *
 * Filters out events not in the event's selected mask, ignores notifications
 * for events already marked for deletion, and handles B_EVENT_INVALID by
 * removing the event from the tree (its object id may now be reused). Adds
 * the event to the ready list and wakes a waiter when it transitions from
 * unqueued to queued.
 *
 * @param event  Target select_event.
 * @param events Event flags being signalled.
 */
void
EventQueue::_Notify(select_event* event, uint16 events)
{
	if ((events & event->selected_events) == 0)
		return;

	const int32 previousEvents = atomic_or(&event->events, (events & ~B_EVENT_INVALID));

	// If the event is already being deleted, we should ignore this notification.
	if ((previousEvents & B_EVENT_DELETING) != 0)
		return;

	// If the event is already queued, and it is not becoming invalid,
	// we don't need to do anything more.
	if ((previousEvents & B_EVENT_QUEUED) != 0 && (events & B_EVENT_INVALID) == 0)
		return;

	{
		MutexLocker _(&fQueueLock);

		// We need to recheck B_EVENT_DELETING now we have the lock.
		if ((event->events & B_EVENT_DELETING) != 0)
			return;

		// If we get B_EVENT_INVALID it means the object we were monitoring was
		// deleted. The object's ID may now be reused, so we must remove it
		// from the event tree.
		if ((events & B_EVENT_INVALID) != 0) {
			atomic_or(&event->events, B_EVENT_INVALID);
			fEventTree.Remove(event);
		}

		// If it's not already queued, it's our responsibility to queue it.
		if ((atomic_or(&event->events, B_EVENT_QUEUED) & B_EVENT_QUEUED) == 0) {
			fEventList.Add(event);
			fQueueCondition.NotifyAll();
		}
	}
}


/**
 * @brief Blocks until ready events exist, then fills the caller's buffer.
 *
 * Sleeps on fQueueCondition while the queue is empty or another thread is
 * dequeuing. Respects B_ABSOLUTE_TIMEOUT and B_INFINITE_TIMEOUT semantics and
 * is interruptible via B_CAN_INTERRUPT. Loops to re-sleep when level-triggered
 * re-selection transiently drains the list without producing results.
 *
 * @param infos    Output array to be filled with ready event descriptors.
 * @param numInfos Capacity of @p infos; may be 0 to test for activity.
 * @param flags    Timeout flags (B_ABSOLUTE_TIMEOUT, B_RELATIVE_TIMEOUT, ...).
 * @param timeout  Absolute or relative timeout, or B_INFINITE_TIMEOUT.
 * @return Number of events written on success, 0 if numInfos was 0 and
 *     events were available, a negative status_t on close/timeout/interrupt.
 */
ssize_t
EventQueue::Wait(event_wait_info* infos, int numInfos,
	int32 flags, bigtime_t timeout)
{
	ASSERT((flags & B_ABSOLUTE_TIMEOUT) != 0
		|| (timeout == B_INFINITE_TIMEOUT || timeout == 0));

	MutexLocker queueLocker(&fQueueLock);

	ssize_t count = 0;
	while (timeout == 0 || (system_time() < timeout)) {
		while ((fDequeueing || fEventList.IsEmpty()) && !fClosing) {
			status_t status = fQueueCondition.Wait(queueLocker.Get(),
				flags | B_CAN_INTERRUPT, timeout);
			if (status != B_OK)
				return status;
		}

		if (fClosing)
			return B_FILE_ERROR;

		if (numInfos == 0)
			return B_OK;

		fDequeueing = true;
		count = _DequeueEvents(infos, numInfos);
		fDequeueing = false;

		if (count != 0)
			break;

		// Due to level-triggered events, it is possible for the event list to have
		// been not empty and _DequeueEvents() still returns nothing. Hence, we loop.
	}

	return count;
}


/**
 * @brief Pulls ready events off the list, applying level/one-shot semantics.
 *
 * Uses a marker node to bound the pass so that dropping the lock for
 * re-selection or deferred deselect cannot cause an infinite loop. For
 * level-triggered events the registration is torn down and rebuilt so the
 * object can re-evaluate readiness; for one-shot events the registration is
 * marked for deletion and batch-deselected after the lock is released (up to
 * kMaxToDeselect per call). Events marked B_EVENT_INVALID are freed here.
 *
 * @param infos    Output array to fill with ready events.
 * @param numInfos Maximum number of events to produce.
 * @return Count of events populated in @p infos.
 */
ssize_t
EventQueue::_DequeueEvents(event_wait_info* infos, int numInfos)
{
	ssize_t count = 0;

	const int32 kMaxToDeselect = 8;
	select_event* deselect[kMaxToDeselect];
	int32 deselectCount = 0;

	// Add a marker element, so we don't loop forever after unlocking the list.
	// (There is only one invocation of _DequeueEvents() at a time.)
	select_event marker = {};
	fEventList.Add(&marker);

	for (select_event* event = NULL; count < numInfos; ) {
		if (fEventList.Head() == NULL || fEventList.Head() == &marker)
			break;

		event = fEventList.RemoveHead();
		int32 events = atomic_and(&event->events,
			~(event->selected_events | B_EVENT_QUEUED));

		if ((events & B_EVENT_DELETING) != 0)
			continue;

		if ((events & B_EVENT_INVALID) == 0
				&& (event->behavior & B_EVENT_LEVEL_TRIGGERED) != 0) {
			// This event is level-triggered. We need to deselect and reselect it,
			// as its state may have changed since we were notified.
			const select_event tmp = *event;

			mutex_unlock(&fQueueLock);
			status_t status = Deselect(tmp.object, tmp.type);
			if (status == B_OK) {
				event = NULL;
				status = Select(tmp.object, tmp.type,
					tmp.selected_events | tmp.behavior, tmp.user_data);
			}
			mutex_lock(&fQueueLock);

			if (status == B_OK) {
				// Is the event still queued?
				event = _GetEvent(tmp.object, tmp.type);
				if (event == NULL)
					continue;
				events = atomic_get(&event->events);
				if ((events & B_EVENT_QUEUED) == 0)
					continue;
			} else if (event == NULL) {
				continue;
			}
		}

		infos[count].object = event->object;
		infos[count].type = event->type;
		infos[count].user_data = event->user_data;
		infos[count].events = USER_EVENTS(events);
		count++;

		// All logic past this point has to do with deleting events.
		if ((events & B_EVENT_INVALID) == 0 && (event->behavior & B_EVENT_ONE_SHOT) == 0)
			continue;

		// Check if the event was requeued.
		if ((atomic_and(&event->events, ~B_EVENT_QUEUED) & B_EVENT_QUEUED) != 0)
			fEventList.Remove(event);

		if ((events & B_EVENT_INVALID) != 0) {
			// The event will already have been removed from the tree.
			delete event;
		} else if ((event->behavior & B_EVENT_ONE_SHOT) != 0) {
			// We already checked B_EVENT_INVALID above, so we don't need to again.
			fEventTree.Remove(event);
			event->events = B_EVENT_DELETING;

			deselect[deselectCount++] = event;
			if (deselectCount == kMaxToDeselect)
				break;
		}
	}

	fEventList.Remove(&marker);

	if (deselectCount != 0) {
		mutex_unlock(&fQueueLock);
		for (int32 i = 0; i < deselectCount; i++) {
			select_event* event = deselect[i];

			_DeselectEvent(event);
			delete event;
		}
		mutex_lock(&fQueueLock);

		// We don't need to notify waiters, as we removed the events
		// from anywhere they could be found before dropping the lock.
	}

	return count;
}


/**
 * @brief Looks up a select_event by (object, type), waiting out transitions.
 *
 * Must be called with fQueueLock held. If the matching event has
 * B_EVENT_SELECTING or B_EVENT_DELETING set, sleeps on fEventCondition and
 * retries until the transition completes, at which point the event may have
 * been removed entirely.
 *
 * @param object Kernel object identifier.
 * @param type   Selector type.
 * @return Pointer to the stable select_event, or NULL if none exists.
 */
select_event*
EventQueue::_GetEvent(int32 object, uint16 type)
{
	EventQueueTreeDefinition::Key key = { object, type };

	while (true) {
		select_event* event = fEventTree.Find(key);
		if (event == NULL)
			return NULL;

		if ((event->events & (B_EVENT_SELECTING | B_EVENT_DELETING)) == 0)
			return event;

		fEventCondition.Wait(&fQueueLock);

		// At this point the select_event might have been deleted, so we
		// need to refetch it.
	}
}


//	#pragma mark -- File descriptor ops



/**
 * @brief fd_ops close hook for event-queue descriptors.
 *
 * Invokes EventQueue::Closed() which wakes all waiters with B_FILE_ERROR.
 *
 * @param descriptor The event queue file descriptor being closed.
 * @return B_OK.
 */
static status_t
event_queue_close(file_descriptor* descriptor)
{
	EventQueue* queue = (EventQueue*)descriptor->cookie;
	queue->Closed();
	return B_OK;
}


/**
 * @brief fd_ops free hook; drops the reference that the descriptor held.
 *
 * When the last reference goes away the EventQueue is destroyed via the
 * select_sync reference-counting machinery.
 *
 * @param descriptor The descriptor whose cookie is the owning EventQueue.
 */
static void
event_queue_free(file_descriptor* descriptor)
{
	EventQueue* queue = (EventQueue*)descriptor->cookie;
	put_select_sync(queue);
}


#define GET_QUEUE_FD_OR_RETURN(fd, kernel, descriptor)	\
	do {												\
		status_t getError = get_queue_descriptor(fd, kernel, descriptor); \
		if (getError != B_OK)							\
			return getError;							\
	} while (false)


static struct fd_ops sEventQueueFDOps = {
	&event_queue_close,
	&event_queue_free
};


/**
 * @brief Resolves an fd to a file_descriptor that must be an event queue.
 *
 * Validates the fd range, fetches the descriptor (acquiring a reference), and
 * ensures the ops table is the event-queue ops. On failure the reference is
 * released before returning.
 *
 * @param fd         Caller-supplied file descriptor.
 * @param kernel     True when resolving against the kernel's io_context.
 * @param descriptor Out parameter: the resolved descriptor on success.
 * @return B_OK on success, B_FILE_ERROR for a bad fd, B_BAD_VALUE if the
 *     descriptor exists but is not an event queue.
 */
static status_t
get_queue_descriptor(int fd, bool kernel, file_descriptor*& descriptor)
{
	if (fd < 0)
		return B_FILE_ERROR;

	descriptor = get_fd(get_current_io_context(kernel), fd);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	if (descriptor->ops != &sEventQueueFDOps) {
		put_fd(descriptor);
		return B_BAD_VALUE;
	}

	return B_OK;
}


//	#pragma mark - User syscalls


/**
 * @brief Syscall: creates a new event queue and returns its file descriptor.
 *
 * Allocates an EventQueue, wraps it in a file_descriptor using sEventQueueFDOps,
 * and installs it in the current user io_context. Honors O_CLOEXEC and
 * O_CLOFORK from @p openFlags when setting close-on-exec/close-on-fork.
 *
 * @param openFlags open() style flags; only O_CLOEXEC and O_CLOFORK are honored.
 * @return Non-negative fd on success, B_NO_MEMORY on allocation failure, or a
 *     negative status from new_fd() on descriptor installation failure.
 */
int
_user_event_queue_create(int openFlags)
{
	EventQueue* queue = new(std::nothrow) EventQueue(false);
	if (queue == NULL)
		return B_NO_MEMORY;

	ObjectDeleter<EventQueue> deleter(queue);

	file_descriptor* descriptor = alloc_fd();
	if (descriptor == NULL)
		return B_NO_MEMORY;

	descriptor->ops = &sEventQueueFDOps;
	descriptor->cookie = (struct event_queue*)queue;
	descriptor->open_mode = O_RDWR | openFlags;

	io_context* context = get_current_io_context(false);
	int fd = new_fd(context, descriptor);
	if (fd < 0) {
		free(descriptor);
		return fd;
	}

	rw_lock_write_lock(&context->lock);
	fd_set_close_on_exec(context, fd, (openFlags & O_CLOEXEC) != 0);
	fd_set_close_on_fork(context, fd, (openFlags & O_CLOFORK) != 0);
	rw_lock_write_unlock(&context->lock);

	deleter.Detach();
	return fd;
}


/**
 * @brief Syscall: applies a batch of select/query/deselect ops to a queue.
 *
 * Each entry in @p userInfos is dispatched based on its events field: a
 * positive mask performs Select(), a negative mask performs Query() (and the
 * current mask is written back to user space), and zero performs Deselect().
 * Per-entry errors are encoded back into userInfos[i].events while the overall
 * return value reports whether any entry failed.
 *
 * @param queue     Event queue file descriptor.
 * @param userInfos User pointer to array of event_wait_info records.
 * @param numInfos  Number of entries in @p userInfos (must be > 0).
 * @return B_OK if all entries succeeded, B_ERROR if any entry failed,
 *     B_BAD_VALUE/B_BAD_ADDRESS/B_NO_MEMORY on argument/resource failures.
 */
status_t
_user_event_queue_select(int queue, event_wait_info* userInfos, int numInfos)
{
	if (numInfos <= 0)
		return B_BAD_VALUE;
	if (userInfos == NULL || !IS_USER_ADDRESS(userInfos))
		return B_BAD_ADDRESS;

	BStackOrHeapArray<event_wait_info, 16> infos(numInfos);
	if (!infos.IsValid())
		return B_NO_MEMORY;

	file_descriptor* descriptor;
	GET_QUEUE_FD_OR_RETURN(queue, false, descriptor);
	FileDescriptorPutter _(descriptor);

	EventQueue* eventQueue = (EventQueue*)descriptor->cookie;

	if (user_memcpy(infos, userInfos, sizeof(event_wait_info) * numInfos) != B_OK)
		return B_BAD_ADDRESS;

	status_t result = B_OK;

	for (int i = 0; i < numInfos; i++) {
		status_t error;
		if (infos[i].events > 0) {
			error = eventQueue->Select(infos[i].object, infos[i].type,
				infos[i].events, infos[i].user_data);
		} else if (infos[i].events < 0) {
			uint32 selectedEvents = 0;
			error = eventQueue->Query(infos[i].object, infos[i].type,
				&selectedEvents, &infos[i].user_data);
			if (error == B_OK) {
				infos[i].events = selectedEvents;
				error = user_memcpy(&userInfos[i], &infos[i], sizeof(event_wait_info));
			}
		} else /* == 0 */ {
			error = eventQueue->Deselect(infos[i].object, infos[i].type);
		}

		if (error != B_OK) {
			user_memcpy(&userInfos[i].events, &error, sizeof(userInfos[i].events));
			result = B_ERROR;
		}
	}

	return result;
}


/**
 * @brief Syscall: waits for ready events on an event queue.
 *
 * Defaults @p timeout to B_INFINITE_TIMEOUT when neither relative nor absolute
 * timeout flags are set, bounces into EventQueue::Wait(), and copies any
 * produced event_wait_info records back to user space. Integrates with the
 * syscall timeout restart machinery so the call can be resumed after signals.
 *
 * @param queue     Event queue file descriptor.
 * @param userInfos User buffer to receive ready events.
 * @param numInfos  Size of @p userInfos (may be 0).
 * @param flags     B_RELATIVE_TIMEOUT / B_ABSOLUTE_TIMEOUT / 0.
 * @param timeout   Relative or absolute timeout.
 * @return Number of events returned, or a negative status on error.
 */
ssize_t
_user_event_queue_wait(int queue, event_wait_info* userInfos, int numInfos,
	uint32 flags, bigtime_t timeout)
{
	syscall_restart_handle_timeout_pre(flags, timeout);

	if (numInfos < 0)
		return B_BAD_VALUE;
	if (numInfos > 0 && (userInfos == NULL || !IS_USER_ADDRESS(userInfos)))
		return B_BAD_ADDRESS;

	if ((flags & (B_RELATIVE_TIMEOUT | B_ABSOLUTE_TIMEOUT)) == 0)
		timeout = B_INFINITE_TIMEOUT;

	BStackOrHeapArray<event_wait_info, 16> infos(numInfos);
	if (!infos.IsValid())
		return B_NO_MEMORY;

	file_descriptor* descriptor;
	GET_QUEUE_FD_OR_RETURN(queue, false, descriptor);
	FileDescriptorPutter _(descriptor);

	EventQueue* eventQueue = (EventQueue*)descriptor->cookie;

	ssize_t result = eventQueue->Wait(infos, numInfos, flags, timeout);
	if (result < 0)
		return syscall_restart_handle_timeout_post(result, timeout);

	status_t status = B_OK;
	if (numInfos != 0)
		status = user_memcpy(userInfos, infos, sizeof(event_wait_info) * numInfos);

	return status == B_OK ? result : status;
}
