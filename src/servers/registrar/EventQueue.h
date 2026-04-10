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
   Copyright (c) 2001-2002, Haiku
   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:
   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.
   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.
   File Name:		EventQueue.h
   Author:			Ingo Weinhold (bonefish@users.sf.net)
   YellowBites (http://www.yellowbites.com)
   Description:	A class providing a mechanism for executing events at
   specified times.
 */

/** @file EventQueue.h
 *  @brief Thread-safe priority queue that executes timed Event objects on a dedicated looper thread. */
#ifndef EVENT_QUEUE_H
#define EVENT_QUEUE_H

#include <List.h>
#include <Locker.h>
#include <OS.h>

class Event;

/** @brief Schedules and dispatches timed events from a sorted list using a background thread. */
class EventQueue : public BLocker {
public:
	EventQueue(const char *name = NULL);
	virtual ~EventQueue();

	/** @brief Returns the initialization status of the event queue. */
	status_t InitCheck();

	/** @brief Signals the event looper thread to terminate. */
	void Die();

	/** @brief Inserts an event into the queue at its scheduled time. */
	bool AddEvent(Event *event);
	/** @brief Removes a previously added event from the queue. */
	bool RemoveEvent(Event *event);
	/** @brief Changes the scheduled time of an event already in the queue. */
	void ModifyEvent(Event *event, bigtime_t newTime);

 private:
	bool _AddEvent(Event *event);
	bool _RemoveEvent(Event *event);
	Event *_EventAt(int32 index) const;
	int32 _IndexOfEvent(Event *event) const;
	int32 _FindInsertionIndex(bigtime_t time) const;

	static	int32 _EventLooperEntry(void *data);
	int32 _EventLooper();
	void _Reschedule();

	BList				fEvents;
	thread_id			fEventLooper;
	sem_id				fLooperControl;
	volatile bigtime_t	fNextEventTime;
	status_t			fStatus;
	volatile bool		fTerminating;
};

#endif	// EVENT_QUEUE_H
