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
 *   Copyright 2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file IOScheduler.cpp
 * @brief Abstract base for I/O schedulers — defines the interface for enqueue and dequeue of IORequests.
 *
 * @see IOScheduler.h
 */


#include "IOScheduler.h"

#include <stdlib.h>
#include <string.h>

#include "IOSchedulerRoster.h"


/**
 * @brief Constructs an IOScheduler, associating it with the given DMA resource.
 *
 * Allocates a unique scheduler ID from the global IOSchedulerRoster. The
 * scheduler is not registered in the roster until Init() is called successfully.
 *
 * @param resource  The DMAResource this scheduler will use when fragmenting
 *                  large IORequests into DMA-safe IOOperations. May be NULL
 *                  for schedulers that do not require DMA constraints.
 */
IOScheduler::IOScheduler(DMAResource* resource)
	:
	fDMAResource(resource),
	fName(NULL),
	fID(IOSchedulerRoster::Default()->NextID()),
	fIOCallback(NULL),
	fIOCallbackData(NULL),
	fSchedulerRegistered(false)
{
}


/**
 * @brief Destructor. Removes this scheduler from the global roster and frees its name.
 *
 * If Init() was called successfully (fSchedulerRegistered == true), the
 * scheduler is removed from the IOSchedulerRoster before the name buffer is
 * freed, ensuring no dangling roster entries.
 */
IOScheduler::~IOScheduler()
{
	if (fSchedulerRegistered)
		IOSchedulerRoster::Default()->RemoveScheduler(this);

	free(fName);
}


/**
 * @brief Initialises the scheduler with a human-readable name and registers it.
 *
 * Duplicates @p name into an internally owned buffer, then registers this
 * scheduler with the global IOSchedulerRoster so that it is visible to
 * monitoring tools.
 *
 * @param name  A null-terminated string used to identify this scheduler
 *              in debugging and monitoring output. Must not be NULL.
 * @retval B_OK        Initialisation succeeded.
 * @retval B_NO_MEMORY The name string could not be duplicated.
 */
status_t
IOScheduler::Init(const char* name)
{
	fName = strdup(name);
	if (fName == NULL)
		return B_NO_MEMORY;

	IOSchedulerRoster::Default()->AddScheduler(this);
	fSchedulerRegistered = true;

	return B_OK;
}


/**
 * @brief Registers an IOCallback object as the completion handler.
 *
 * Wraps the IOCallback in the C-linkage trampoline provided by
 * IOCallback::WrapperFunction so that the stored callback pointer conforms
 * to the @c io_callback function-pointer type.
 *
 * @param callback  Reference to the IOCallback whose DoIO() method will be
 *                  invoked when an IOOperation completes.
 */
void
IOScheduler::SetCallback(IOCallback& callback)
{
	SetCallback(&IOCallback::WrapperFunction, &callback);
}


/**
 * @brief Registers a raw C-style function pointer as the completion handler.
 *
 * Stores both the function pointer and its opaque data argument for later
 * invocation when an IOOperation finishes.
 *
 * @param callback  The C-linkage completion callback function.
 * @param data      Opaque pointer passed verbatim as the first argument to
 *                  @p callback on each invocation.
 */
void
IOScheduler::SetCallback(io_callback callback, void* data)
{
	fIOCallback = callback;
	fIOCallbackData = data;
}


/**
 * @brief Notifies the scheduler of the total usable capacity of the device.
 *
 * The base implementation is a no-op. Concrete schedulers that use device
 * capacity to optimise seek ordering (e.g. elevator algorithms) should
 * override this method.
 *
 * @param deviceCapacity  Total number of addressable bytes on the device.
 */
void
IOScheduler::SetDeviceCapacity(off_t deviceCapacity)
{
}


/**
 * @brief Notifies the scheduler that removable media has changed.
 *
 * The base implementation is a no-op. Concrete schedulers may override
 * this method to reset seek-position state or flush pending requests after
 * a media change event.
 */
void
IOScheduler::MediaChanged()
{
}
