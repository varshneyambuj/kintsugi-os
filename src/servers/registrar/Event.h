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
   File Name:		Event.h
   Author:			Ingo Weinhold (bonefish@users.sf.net)
   YellowBites (http://www.yellowbites.com)
   Description:	Base class for events as handled by EventQueue.
 */

/** @file Event.h
 *  @brief Base class for timed events that can be scheduled in an EventQueue. */
#ifndef EVENT_H
#define EVENT_H

#include <OS.h>

class EventQueue;

/** @brief Abstract timed event with optional auto-deletion, executed by the EventQueue. */
class Event {
public:
	Event(bool autoDelete = true);
	Event(bigtime_t time, bool autoDelete = true);
	virtual ~Event();

	/** @brief Sets the scheduled execution time for this event. */
	void SetTime(bigtime_t time);
	/** @brief Returns the scheduled execution time. */
	bigtime_t Time() const;

	/** @brief Controls whether the event is automatically deleted after execution. */
	void SetAutoDelete(bool autoDelete);
	/** @brief Returns whether the event will be auto-deleted after execution. */
	bool IsAutoDelete() const;

	/** @brief Executes the event action; subclasses override to define behavior. */
	virtual	bool Do(EventQueue *queue);

 private:
	bigtime_t		fTime;	/**< Scheduled execution timestamp in microseconds. */
	bool			fAutoDelete;	/**< Whether the event should be deleted after firing. */
};

#endif	// EVENT_H
