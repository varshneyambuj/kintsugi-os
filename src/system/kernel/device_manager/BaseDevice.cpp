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
 *   Copyright 2008-2009, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file BaseDevice.cpp
 * @brief Base class for all kernel device objects — provides open/close/read/write/ioctl dispatch.
 *
 * @see BaseDevice.h
 */


#include "BaseDevice.h"


/** @brief Default constructor. No resources are allocated at this level. */
BaseDevice::BaseDevice()
{
}


/** @brief Destructor. No resources are released at this level. */
BaseDevice::~BaseDevice()
{
}


/**
 * @brief Initialises the physical or logical device represented by this object.
 *
 * The base implementation always returns @c B_ERROR to signal that the
 * concrete subclass must override this method.
 *
 * @return @c B_ERROR always; subclasses should return @c B_OK on success.
 */
status_t
BaseDevice::InitDevice()
{
	return B_ERROR;
}


/**
 * @brief Tears down any state set up by InitDevice().
 *
 * The base implementation is a no-op; subclasses override this to release
 * hardware resources, cancel DMA transactions, etc.
 */
void
BaseDevice::UninitDevice()
{
}


/**
 * @brief Notifies the device that it has been removed from the device tree.
 *
 * The base implementation is a no-op. Subclasses that need to react to
 * hot-removal (e.g. to self-delete or to abort pending I/O) should override
 * this method.
 */
void
BaseDevice::Removed()
{
}


/**
 * @brief Indicates whether this device supports the @c select operation.
 *
 * The base implementation always returns @c false. Override in subclasses
 * that implement Select().
 *
 * @return @c false always.
 */
bool
BaseDevice::HasSelect() const
{
	return false;
}


/**
 * @brief Indicates whether this device supports the @c deselect operation.
 *
 * The base implementation always returns @c false. Override in subclasses
 * that implement Deselect().
 *
 * @return @c false always.
 */
bool
BaseDevice::HasDeselect() const
{
	return false;
}


/**
 * @brief Indicates whether this device supports synchronous read operations.
 *
 * The base implementation always returns @c false. Override in subclasses
 * that implement Read().
 *
 * @return @c false always.
 */
bool
BaseDevice::HasRead() const
{
	return false;
}


/**
 * @brief Indicates whether this device supports synchronous write operations.
 *
 * The base implementation always returns @c false. Override in subclasses
 * that implement Write().
 *
 * @return @c false always.
 */
bool
BaseDevice::HasWrite() const
{
	return false;
}


/**
 * @brief Indicates whether this device supports asynchronous I/O operations.
 *
 * The base implementation always returns @c false. Override in subclasses
 * that implement IO().
 *
 * @return @c false always.
 */
bool
BaseDevice::HasIO() const
{
	return false;
}


/**
 * @brief Synchronous read stub — always returns @c B_UNSUPPORTED.
 *
 * Subclasses that advertise HasRead() == true must override this method.
 *
 * @param cookie   Per-open cookie returned by Open().
 * @param pos      Byte offset within the device.
 * @param buffer   Destination buffer for read data.
 * @param _length  Requested transfer length; unchanged on error.
 * @return @c B_UNSUPPORTED always.
 */
status_t
BaseDevice::Read(void* cookie, off_t pos, void* buffer, size_t* _length)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Synchronous write stub — always returns @c B_UNSUPPORTED.
 *
 * Subclasses that advertise HasWrite() == true must override this method.
 *
 * @param cookie   Per-open cookie returned by Open().
 * @param pos      Byte offset within the device.
 * @param buffer   Source buffer containing data to write.
 * @param _length  Requested transfer length; unchanged on error.
 * @return @c B_UNSUPPORTED always.
 */
status_t
BaseDevice::Write(void* cookie, off_t pos, const void* buffer, size_t* _length)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Asynchronous I/O stub — always returns @c B_UNSUPPORTED.
 *
 * Subclasses that advertise HasIO() == true must override this method.
 *
 * @param cookie   Per-open cookie returned by Open().
 * @param request  Descriptor of the I/O request to service.
 * @return @c B_UNSUPPORTED always.
 */
status_t
BaseDevice::IO(void* cookie, io_request* request)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Device control (ioctl) stub — always returns @c B_UNSUPPORTED.
 *
 * Subclasses must override this method to handle device-specific control
 * operations.
 *
 * @param cookie  Per-open cookie returned by Open().
 * @param op      The ioctl operation code.
 * @param buffer  In/out buffer whose meaning depends on @p op.
 * @param length  Size of @p buffer in bytes.
 * @return @c B_UNSUPPORTED always.
 */
status_t
BaseDevice::Control(void* cookie, int32 op, void* buffer, size_t length)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Event-select stub — always returns @c B_UNSUPPORTED.
 *
 * Subclasses that advertise HasSelect() == true must override this method.
 *
 * @param cookie  Per-open cookie returned by Open().
 * @param event   The event to monitor.
 * @param sync    The selectsync object to notify when the event fires.
 * @return @c B_UNSUPPORTED always.
 */
status_t
BaseDevice::Select(void* cookie, uint8 event, selectsync* sync)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Event-deselect stub — always returns @c B_UNSUPPORTED.
 *
 * Subclasses that advertise HasDeselect() == true must override this method.
 *
 * @param cookie  Per-open cookie returned by Open().
 * @param event   The event that was previously selected.
 * @param sync    The selectsync object passed to Select().
 * @return @c B_UNSUPPORTED always.
 */
status_t
BaseDevice::Deselect(void* cookie, uint8 event, selectsync* sync)
{
	return B_UNSUPPORTED;
}
