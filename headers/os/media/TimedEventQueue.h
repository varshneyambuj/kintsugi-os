/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from Haiku, Inc. covered by:
 * Copyright 2009-2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/** @file TimedEventQueue.h
 *  @brief Defines BTimedEventQueue and media_timed_event for scheduling media events.
 */

#ifndef _TIMED_EVENT_QUEUE_H
#define _TIMED_EVENT_QUEUE_H


#include <MediaDefs.h>


/** @brief Carries the data for a single timed media event in a BTimedEventQueue. */
struct media_timed_event {
	/** @brief Default constructor; zeroes all fields. */
								media_timed_event();

	/** @brief Constructs a minimal event with the given time and type.
	 *  @param inTime The performance time for this event.
	 *  @param inType One of the BTimedEventQueue::event_type values.
	 */
								media_timed_event(bigtime_t inTime, int32 inType);

	/** @brief Constructs an event with a pointer and cleanup flag.
	 *  @param inTime The performance time for this event.
	 *  @param inType The event type.
	 *  @param inPointer Optional data pointer associated with this event.
	 *  @param inCleanup One of the BTimedEventQueue::cleanup_flag values.
	 */
								media_timed_event(bigtime_t inTime, int32 inType,
									void* inPointer, uint32 inCleanup);

	/** @brief Constructs a fully specified event.
	 *  @param inTime The performance time.
	 *  @param inType The event type.
	 *  @param inPointer Optional data pointer.
	 *  @param inCleanup Cleanup flag.
	 *  @param inData Arbitrary 32-bit data value.
	 *  @param inBigdata Arbitrary 64-bit data value.
	 *  @param inUserData Optional user data string.
	 *  @param dataSize Number of bytes to copy from inUserData (0 for full string).
	 */
								media_timed_event(bigtime_t inTime, int32 inType,
									void* inPointer, uint32 inCleanup,
									int32 inData, int64 inBigdata,
									const char* inUserData, size_t dataSize = 0);

	/** @brief Copy constructor.
	 *  @param other The event to copy.
	 */
								media_timed_event(
									const media_timed_event& other);

								~media_timed_event();

	/** @brief Assigns another event to this one. */
			void				operator=(const media_timed_event& other);

public:
			bigtime_t			event_time;    /**< Performance time of this event. */
			int32				type;          /**< Event type (event_type enum). */
			void*				pointer;       /**< Optional event data pointer. */
			uint32				cleanup;       /**< Cleanup action (cleanup_flag enum). */
			int32				data;          /**< Arbitrary 32-bit event data. */
			int64				bigdata;       /**< Arbitrary 64-bit event data. */
			char				user_data[64]; /**< Optional user-supplied event data string. */

private:
			uint32				_reserved_media_timed_event_[8];
};


/** @brief Compares two events for equality (by time, type, and pointer). */
bool operator==(const media_timed_event& a, const media_timed_event& b);
/** @brief Returns true if the two events are not equal. */
bool operator!=(const media_timed_event& a, const media_timed_event& b);
/** @brief Orders events chronologically; earlier events are less. */
bool operator<(const media_timed_event& a, const media_timed_event& b);
/** @brief Orders events chronologically; later events are greater. */
bool operator>(const media_timed_event& a, const media_timed_event&b);


namespace BPrivate {
	class TimedEventQueueData;
};

/** @brief A time-sorted priority queue of media_timed_event objects.
 *
 *  BTimedEventQueue is used by BMediaEventLooper to schedule events in
 *  performance-time order.  Events can be added, removed, searched, and
 *  iterated.
 */
class BTimedEventQueue {
public:
	/** @brief Predefined event type codes; values >= B_USER_EVENT are application-defined. */
			enum event_type {
				B_NO_EVENT = -1,       /**< Placeholder indicating no event. */
				B_ANY_EVENT = 0,       /**< Wildcard matching any event type. */

				B_START,               /**< Node start event. */
				B_STOP,                /**< Node stop event. */
				B_SEEK,                /**< Node seek event. */
				B_WARP,                /**< Time-warp event. */
				B_TIMER,               /**< User-scheduled timer event. */
				B_HANDLE_BUFFER,       /**< Buffer delivery event. */
				B_DATA_STATUS,         /**< Data-status change event. */
				B_HARDWARE,            /**< Hardware-level event. */
				B_PARAMETER,           /**< Parameter-change event. */

				B_USER_EVENT = 0x4000  /**< First value available for user-defined events. */
			};

	/** @brief Actions the queue should take when an event is removed. */
			enum cleanup_flag {
				B_NO_CLEANUP = 0,          /**< No special cleanup needed. */
				B_RECYCLE_BUFFER,          /**< Recycle a BBuffer referenced by the event. */
				B_EXPIRE_TIMER,            /**< Cancel a timer associated with the event. */
				B_USER_CLEANUP = 0x4000    /**< First value for user-defined cleanup. */
			};

	/** @brief Controls which events relative to a time point are matched. */
			enum time_direction {
				B_ALWAYS = -1,       /**< Match all events regardless of time. */
				B_BEFORE_TIME = 0,   /**< Match events before the given time. */
				B_AT_TIME,           /**< Match events exactly at the given time. */
				B_AFTER_TIME         /**< Match events after the given time. */
			};

public:
	/** @brief Allocates the queue from the system heap. */
			void*				operator new(size_t size);
	/** @brief Frees the queue back to the system heap. */
			void				operator delete(void* ptr, size_t size);

	/** @brief Constructs an empty timed event queue. */
								BTimedEventQueue();
	virtual						~BTimedEventQueue();

	/** @brief Callback type invoked when the queue removes an event.
	 *  @param event The event being removed.
	 *  @param context The user-supplied context pointer.
	 */
			typedef void (*cleanup_hook)(const media_timed_event* event,
				void* context);

	/** @brief Registers a cleanup callback invoked when events are removed.
	 *  @param hook The callback function.
	 *  @param context Arbitrary pointer passed to the callback.
	 */
			void				SetCleanupHook(cleanup_hook hook,
									void* context);

	/** @brief Inserts an event into the queue in time order.
	 *  @param event The event to add.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			AddEvent(const media_timed_event& event);

	/** @brief Removes a specific event from the queue.
	 *  @param event Pointer to the event to remove.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			RemoveEvent(const media_timed_event* event);

	/** @brief Removes the earliest event from the queue.
	 *  @param _event If non-NULL, receives a copy of the removed event.
	 *  @return B_OK on success, or an error code.
	 */
			status_t  			RemoveFirstEvent(
									media_timed_event* _event = NULL);

	/** @brief Returns true if the queue contains at least one event. */
			bool				HasEvents() const;

	/** @brief Returns the number of events currently in the queue.
	 *  @return Event count.
	 */
			int32				EventCount() const;

	/** @brief Returns a pointer to the earliest event in the queue without removing it.
	 *  @return Pointer to the first event, or NULL if empty.
	 */
			const media_timed_event* FirstEvent() const;

	/** @brief Returns the performance time of the earliest event.
	 *  @return Time of the first event, or B_INFINITE_TIMEOUT if empty.
	 */
			bigtime_t			FirstEventTime() const;

	/** @brief Returns a pointer to the latest event in the queue without removing it.
	 *  @return Pointer to the last event, or NULL if empty.
	 */
			const media_timed_event* LastEvent() const;

	/** @brief Returns the performance time of the latest event.
	 *  @return Time of the last event.
	 */
			bigtime_t			LastEventTime() const;

	/** @brief Finds the first event matching the given time constraint and type.
	 *  @param eventTime Reference time for the search.
	 *  @param direction B_BEFORE_TIME, B_AT_TIME, or B_AFTER_TIME.
	 *  @param inclusive If true, include events at exactly eventTime.
	 *  @param eventType Event type to match, or B_ANY_EVENT.
	 *  @return Pointer to the matching event, or NULL if not found.
	 */
			const media_timed_event* FindFirstMatch(bigtime_t eventTime,
								time_direction direction,
								bool inclusive = true,
								int32 eventType = B_ANY_EVENT);

	/** @brief Action codes returned by a for_each_hook callback. */
			enum queue_action {
				B_DONE = -1,         /**< Stop iterating immediately. */
				B_NO_ACTION = 0,     /**< Continue without modifying the event. */
				B_REMOVE_EVENT,      /**< Remove the current event from the queue. */
				B_RESORT_QUEUE       /**< Re-sort the queue (event_time was changed). */
			};

	/** @brief Callback type for DoForEach(); return a queue_action to control iteration. */
			typedef queue_action (*for_each_hook)(media_timed_event* event,
				void* context);

	/** @brief Iterates over events matching a time constraint, invoking a callback for each.
	 *  @param hook Callback invoked for each matching event.
	 *  @param context Arbitrary pointer passed to the callback.
	 *  @param eventTime Reference time for filtering.
	 *  @param direction Time direction for filtering.
	 *  @param inclusive If true, include events at exactly eventTime.
	 *  @param eventType Event type to match, or B_ANY_EVENT.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			DoForEach(for_each_hook hook, void* context,
									bigtime_t eventTime = 0,
									time_direction direction = B_ALWAYS,
									bool inclusive = true,
									int32 eventType = B_ANY_EVENT);

	/** @brief Removes all events matching a time constraint and type.
	 *  @param eventTime Reference time for filtering.
	 *  @param direction Time direction for filtering.
	 *  @param inclusive If true, include events at exactly eventTime.
	 *  @param eventType Event type to flush, or B_ANY_EVENT.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			FlushEvents(bigtime_t eventTime,
									time_direction direction,
									bool inclusive = true,
									int32 eventType = B_ANY_EVENT);

private:
	// FBC padding and forbidden methods
	virtual	void				_ReservedTimedEventQueue0();
	virtual	void				_ReservedTimedEventQueue1();
	virtual	void				_ReservedTimedEventQueue2();
	virtual	void				_ReservedTimedEventQueue3();
	virtual	void				_ReservedTimedEventQueue4();
	virtual	void				_ReservedTimedEventQueue5();
	virtual	void				_ReservedTimedEventQueue6();
	virtual	void				_ReservedTimedEventQueue7();
	virtual	void				_ReservedTimedEventQueue8();
	virtual	void				_ReservedTimedEventQueue9();
	virtual	void				_ReservedTimedEventQueue10();
	virtual	void				_ReservedTimedEventQueue11();
	virtual	void				_ReservedTimedEventQueue12();
	virtual	void				_ReservedTimedEventQueue13();
	virtual	void				_ReservedTimedEventQueue14();
	virtual	void				_ReservedTimedEventQueue15();
	virtual	void				_ReservedTimedEventQueue16();
	virtual	void				_ReservedTimedEventQueue17();
	virtual	void				_ReservedTimedEventQueue18();
	virtual	void				_ReservedTimedEventQueue19();
	virtual	void				_ReservedTimedEventQueue20();
	virtual	void				_ReservedTimedEventQueue21();
	virtual	void				_ReservedTimedEventQueue22();
	virtual	void				_ReservedTimedEventQueue23();

								BTimedEventQueue(const BTimedEventQueue&);
			BTimedEventQueue&	operator=(const BTimedEventQueue&);

private:
	static	int					_Match(const media_timed_event& event,
										 bigtime_t eventTime,
										 time_direction direction,
										 bool inclusive, int32 eventType);

private:
			BPrivate::TimedEventQueueData* fData;

			uint32 				_reserved[6];
};


#endif
