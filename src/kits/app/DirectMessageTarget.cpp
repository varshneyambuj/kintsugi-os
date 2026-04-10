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
 *   Copyright 2007, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 */

/** @file DirectMessageTarget.cpp
 *  @brief BPrivate::BDirectMessageTarget implementation for direct message delivery.
 *
 *  Provides a reference-counted message target that allows direct delivery
 *  of BMessage objects to a handler's message queue, bypassing the normal
 *  port-based messaging path. The target can be closed to reject further
 *  messages.
 */

#include <DirectMessageTarget.h>


namespace BPrivate {


/** @brief Constructs a BDirectMessageTarget with a reference count of 1 and open state. */
BDirectMessageTarget::BDirectMessageTarget()
	:
	fReferenceCount(1),
	fClosed(false)
{
}


/** @brief Destructor. */
BDirectMessageTarget::~BDirectMessageTarget()
{
}


/** @brief Adds a message to the target's queue.
 *  @param message The BMessage to enqueue. Ownership is transferred to the target.
 *  @return true if the message was successfully enqueued, false if the target
 *          is closed (in which case the message is deleted).
 */
bool
BDirectMessageTarget::AddMessage(BMessage* message)
{
	if (fClosed) {
		delete message;
		return false;
	}

	fQueue.AddMessage(message);
	return true;
}


/** @brief Closes the target, causing all subsequent AddMessage() calls to fail.
 *
 *  Once closed, any messages passed to AddMessage() will be deleted and the
 *  call will return false.
 */
void
BDirectMessageTarget::Close()
{
	fClosed = true;
}


/** @brief Increments the reference count of this target. */
void
BDirectMessageTarget::Acquire()
{
	atomic_add(&fReferenceCount, 1);
}


/** @brief Decrements the reference count, deleting the target when it reaches zero. */
void
BDirectMessageTarget::Release()
{
	if (atomic_add(&fReferenceCount, -1) == 1)
		delete this;
}

}	// namespace BPrivate
