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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2008-2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2004-2009, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file IOCallback.cpp
 * @brief IOCallback base class — asynchronous I/O completion notification interface.
 *
 * @see IOCallback.h
 */


#include "IOCallback.h"


/** @brief Virtual destructor. Ensures correct destruction through base pointers. */
IOCallback::~IOCallback()
{
}


/**
 * @brief Called by the I/O subsystem when an I/O operation completes.
 *
 * The base implementation returns @c B_ERROR. Concrete subclasses must
 * override this method to perform their completion handling (e.g. waking
 * waiting threads, updating transfer counts, or chaining further requests).
 *
 * @param operation  The IOOperation that has completed.
 * @return @c B_ERROR always in the base class; subclasses should return
 *         @c B_OK on success or a negative error code on failure.
 */
status_t
IOCallback::DoIO(IOOperation* operation)
{
	return B_ERROR;
}


/**
 * @brief C-linkage trampoline that bridges the low-level io_callback function
 *        pointer convention to an IOCallback object's virtual DoIO() method.
 *
 * This static function matches the @c io_callback function pointer signature
 * used by the I/O scheduler. It casts @p data back to the originating
 * IOCallback instance and forwards the call to DoIO().
 *
 * @param data       Pointer to the IOCallback instance (passed as opaque void*).
 * @param operation  The io_operation that has completed.
 * @return The value returned by the IOCallback::DoIO() virtual method.
 */
/*static*/ status_t
IOCallback::WrapperFunction(void* data, io_operation* operation)
{
	return ((IOCallback*)data)->DoIO(operation);
}
