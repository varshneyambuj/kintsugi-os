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
   File Name:		Event.cpp
   Author:			Ingo Weinhold (bonefish@users.sf.net)
   YellowBites (http://www.yellowbites.com)
   Description:	Base class for events as handled by EventQueue.
 */

/** @file Event.cpp
 *  @brief Base class implementation for timed events processed by the EventQueue. */
#include "Event.h"

/**
 * @brief Creates a new Event with the time initialized to 0.
 *
 * The event time should be set via SetTime() before pushing the event into
 * an event queue.
 *
 * @param autoDelete If @c true, the event queue will delete this object after
 *                   execution.
 */
Event::Event(bool autoDelete)
	: fTime(0),
	  fAutoDelete(autoDelete)
{
}

/**
 * @brief Creates a new Event with the specified execution time.
 *
 * @param time       The time at which the event should fire.
 * @param autoDelete If @c true, the event queue will delete this object after
 *                   execution.
 */
Event::Event(bigtime_t time, bool autoDelete)
	: fTime(time),
	  fAutoDelete(autoDelete)
{
}

/** @brief Destroys the Event. */
Event::~Event()
{
}

/**
 * @brief Sets a new event time.
 *
 * Must not be called while the event is in an event queue; use
 * EventQueue::ModifyEvent() instead.
 *
 * @param time The new event time.
 */
void
Event::SetTime(bigtime_t time)
{
	fTime = time;
}

/**
 * @brief Returns the scheduled execution time of the event.
 *
 * @return The event time in microseconds since boot.
 */
bigtime_t
Event::Time() const
{
	return fTime;
}

/**
 * @brief Sets whether the event should be automatically deleted after execution.
 *
 * @param autoDelete If @c true, the event queue will delete this object after
 *                   it fires.
 */
void
Event::SetAutoDelete(bool autoDelete)
{
	fAutoDelete = autoDelete;
}

/**
 * @brief Returns whether the event is set to auto-delete after execution.
 *
 * @return @c true if the event queue will delete this object after firing.
 */
bool
Event::IsAutoDelete() const
{
	return fAutoDelete;
}

/**
 * @brief Hook method invoked when the event's scheduled time has arrived.
 *
 * Subclasses override this to perform their action. The return value is
 * OR-ed with IsAutoDelete() to decide whether the event queue deletes this
 * object. The base implementation returns the current auto-delete flag.
 *
 * This method runs on the event queue's timer thread and should complete
 * quickly. The event queue is not locked and no longer contains this event
 * when Do() is called, so the event may re-push itself if desired.
 *
 * @param queue The event queue executing the event.
 * @return @c true to request deletion by the queue, @c false to defer to
 *         IsAutoDelete().
 */
bool
Event::Do(EventQueue *queue)
{
	return fAutoDelete;
}

