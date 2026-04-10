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
   File Name:		Watcher.cpp
   Author:			Ingo Weinhold (bonefish@users.sf.net)
   Description:	A Watcher represents a target of a watching service.
   A WatcherFilter represents a predicate on Watchers.
 */

/** @file Watcher.cpp
 *  @brief Implements the Watcher and WatcherFilter base classes for notification delivery. */
#include <Message.h>

#include "MessageDeliverer.h"
#include "Watcher.h"

/**
 * @brief Creates a new Watcher with the specified message target.
 *
 * The supplied BMessenger is copied; the caller retains ownership of the
 * original.
 *
 * @param target The watcher's message target.
 */
Watcher::Watcher(const BMessenger &target)
	: fTarget(target)
{
}

/** @brief Frees all resources associated with the Watcher. */
Watcher::~Watcher()
{
}

/**
 * @brief Returns the watcher's message target.
 *
 * @return A reference to the BMessenger identifying the watcher's target.
 */
const BMessenger&
Watcher::Target() const
{
	return fTarget;
}

/**
 * @brief Sends the supplied message to the watcher's message target.
 *
 * Subclasses may override this to augment the message, but must copy the
 * message rather than modifying it directly since other watchers may also
 * receive it.
 *
 * @param message The message to deliver.
 * @return @c B_OK on success, or an error code on failure.
 */
status_t
Watcher::SendMessage(BMessage *message)
{
	return MessageDeliverer::Default()->DeliverMessage(message, fTarget);
}


/** @brief Constructs a WatcherFilter with default (pass-all) behavior. */
WatcherFilter::WatcherFilter()
{
}

/** @brief Frees all resources associated with the WatcherFilter. */
WatcherFilter::~WatcherFilter()
{
}

/**
 * @brief Tests whether the watcher-message pair satisfies this filter's predicate.
 *
 * The base implementation always returns @c true. Subclasses override this
 * to implement specific filtering logic.
 *
 * @param watcher The watcher in question.
 * @param message The message in question.
 * @return @c true (always, in the base class).
 */
bool
WatcherFilter::Filter(Watcher *watcher, BMessage *message)
{
	return true;
}

