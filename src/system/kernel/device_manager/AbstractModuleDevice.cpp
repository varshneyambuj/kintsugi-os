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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2008-2009, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file AbstractModuleDevice.cpp
 * @brief Base implementation for kernel devices backed by a module interface, bridging the device manager and module system.
 *
 * @see AbstractModuleDevice.h
 */


#include "AbstractModuleDevice.h"

#include "IORequest.h"


/**
 * @brief Default constructor. Initialises all module-facing fields to NULL/zero.
 *
 * Sets fNode to NULL, fInitialized to 0, fDeviceModule to NULL, and
 * fDeviceData to NULL so that the object is in a clean, uninitialised state
 * until the subclass completes setup.
 */
AbstractModuleDevice::AbstractModuleDevice()
	:
	fNode(NULL),
	fInitialized(0),
	fDeviceModule(NULL),
	fDeviceData(NULL)
{
}


/** @brief Destructor. No dynamic resources are owned at this level. */
AbstractModuleDevice::~AbstractModuleDevice()
{
}


/**
 * @brief Reports whether the underlying module exposes a @c select hook.
 *
 * @return @c true if Module()->select is non-NULL, @c false otherwise.
 */
bool
AbstractModuleDevice::HasSelect() const
{
	return Module()->select != NULL;
}


/**
 * @brief Reports whether the underlying module exposes a @c deselect hook.
 *
 * @return @c true if Module()->deselect is non-NULL, @c false otherwise.
 */
bool
AbstractModuleDevice::HasDeselect() const
{
	return Module()->deselect != NULL;
}


/**
 * @brief Reports whether the underlying module exposes a @c read hook.
 *
 * @return @c true if Module()->read is non-NULL, @c false otherwise.
 */
bool
AbstractModuleDevice::HasRead() const
{
	return Module()->read != NULL;
}


/**
 * @brief Reports whether the underlying module exposes a @c write hook.
 *
 * @return @c true if Module()->write is non-NULL, @c false otherwise.
 */
bool
AbstractModuleDevice::HasWrite() const
{
	return Module()->write != NULL;
}


/**
 * @brief Reports whether the underlying module exposes an @c io hook.
 *
 * @return @c true if Module()->io is non-NULL, @c false otherwise.
 */
bool
AbstractModuleDevice::HasIO() const
{
	return Module()->io != NULL;
}


/**
 * @brief Opens the device by delegating to the module's @c open hook.
 *
 * @param path     The device path being opened.
 * @param openMode Flags describing the requested open mode (O_RDONLY, etc.).
 * @param _cookie  Out-parameter: receives the per-open cookie allocated by the module.
 * @return @c B_OK on success, or a negative error code on failure.
 */
status_t
AbstractModuleDevice::Open(const char* path, int openMode, void** _cookie)
{
	return Module()->open(Data(), path, openMode, _cookie);
}


/**
 * @brief Internal helper that performs a synchronous read or write via the
 *        module's @c io hook by building and waiting on an IORequest.
 *
 * Used when the module provides an @c io hook but no dedicated @c read or
 * @c write hook, allowing callers to block until the request completes.
 *
 * @param cookie   Per-open cookie previously returned by Open().
 * @param pos      Byte offset within the device at which to begin the transfer.
 * @param buffer   User or kernel buffer to read into / write from.
 * @param _length  On entry the requested transfer size in bytes; on return the
 *                 number of bytes actually transferred.
 * @param isWrite  @c true to write, @c false to read.
 * @return @c B_OK on success, or a negative error code on failure.
 */
status_t
AbstractModuleDevice::_DoIO(void* cookie, off_t pos,
	void* buffer, size_t* _length, bool isWrite)
{
	IORequest request;
	status_t status = request.Init(pos, (addr_t)buffer, *_length, isWrite, 0);
	if (status != B_OK)
		return status;

	status = IO(cookie, &request);
	if (status != B_OK)
		return status;

	status = request.Wait(0, 0);
	*_length = request.TransferredBytes();
	return status;
}


/**
 * @brief Reads data from the device.
 *
 * Dispatches to the module's @c read hook when available. Falls back to the
 * module's @c io hook (via _DoIO()) if only @c io is present, and ultimately
 * falls back to BaseDevice::Read() if neither hook is provided.
 *
 * @param cookie   Per-open cookie previously returned by Open().
 * @param pos      Byte offset within the device at which to begin reading.
 * @param buffer   Destination buffer for the data read.
 * @param _length  On entry the maximum number of bytes to read; on return the
 *                 number of bytes actually read.
 * @return @c B_OK on success, or a negative error code on failure.
 */
status_t
AbstractModuleDevice::Read(void* cookie, off_t pos, void* buffer, size_t* _length)
{
	if (Module()->read == NULL) {
		if (Module()->io == NULL)
			return BaseDevice::Read(cookie, pos, buffer, _length);

		return _DoIO(cookie, pos, buffer, _length, false);
	}
	return Module()->read(cookie, pos, buffer, _length);
}


/**
 * @brief Writes data to the device.
 *
 * Dispatches to the module's @c write hook when available. Falls back to the
 * module's @c io hook (via _DoIO()) if only @c io is present, and ultimately
 * falls back to BaseDevice::Write() if neither hook is provided.
 *
 * @param cookie   Per-open cookie previously returned by Open().
 * @param pos      Byte offset within the device at which to begin writing.
 * @param buffer   Source buffer containing the data to write.
 * @param _length  On entry the number of bytes to write; on return the number
 *                 of bytes actually written.
 * @return @c B_OK on success, or a negative error code on failure.
 */
status_t
AbstractModuleDevice::Write(void* cookie, off_t pos, const void* buffer, size_t* _length)
{
	if (Module()->write == NULL) {
		if (Module()->io == NULL)
			return BaseDevice::Write(cookie, pos, buffer, _length);

		return _DoIO(cookie, pos, const_cast<void*>(buffer), _length, true);
	}
	return Module()->write(cookie, pos, buffer, _length);
}


/**
 * @brief Issues an asynchronous I/O request to the module.
 *
 * Delegates to the module's @c io hook if present; falls back to
 * BaseDevice::IO() otherwise.
 *
 * @param cookie   Per-open cookie previously returned by Open().
 * @param request  The IORequest descriptor describing the transfer.
 * @return @c B_OK if the request was accepted, or a negative error code.
 */
status_t
AbstractModuleDevice::IO(void* cookie, io_request* request)
{
	if (Module()->io == NULL)
		return BaseDevice::IO(cookie, request);
	return Module()->io(cookie, request);
}


/**
 * @brief Issues a device control (ioctl) operation.
 *
 * Delegates to the module's @c control hook if present; falls back to
 * BaseDevice::Control() otherwise.
 *
 * @param cookie  Per-open cookie previously returned by Open().
 * @param op      The ioctl operation code.
 * @param buffer  In/out buffer whose meaning depends on @p op.
 * @param length  Size of @p buffer in bytes.
 * @return @c B_OK on success, or a negative error code on failure.
 */
status_t
AbstractModuleDevice::Control(void* cookie, int32 op, void* buffer, size_t length)
{
	if (Module()->control == NULL)
		return BaseDevice::Control(cookie, op, buffer, length);
	return Module()->control(cookie, op, buffer, length);
}


/**
 * @brief Registers interest in an I/O event on the device.
 *
 * Delegates to the module's @c select hook if present; falls back to
 * BaseDevice::Select() otherwise.
 *
 * @param cookie  Per-open cookie previously returned by Open().
 * @param event   The event to monitor (B_SELECT_READ, B_SELECT_WRITE, etc.).
 * @param sync    The selectsync object to notify when the event occurs.
 * @return @c B_OK on success, or a negative error code on failure.
 */
status_t
AbstractModuleDevice::Select(void* cookie, uint8 event, selectsync* sync)
{
	if (Module()->select == NULL)
		return BaseDevice::Select(cookie, event, sync);
	return Module()->select(cookie, event, sync);
}


/**
 * @brief Deregisters interest in an I/O event previously registered via Select().
 *
 * Delegates to the module's @c deselect hook if present; falls back to
 * BaseDevice::Deselect() otherwise.
 *
 * @param cookie  Per-open cookie previously returned by Open().
 * @param event   The event that was previously selected.
 * @param sync    The selectsync object that was passed to Select().
 * @return @c B_OK on success, or a negative error code on failure.
 */
status_t
AbstractModuleDevice::Deselect(void* cookie, uint8 event, selectsync* sync)
{
	if (Module()->deselect == NULL)
		return BaseDevice::Deselect(cookie, event, sync);
	return Module()->deselect(cookie, event, sync);
}


/**
 * @brief Closes a previously opened device instance.
 *
 * Delegates directly to the module's mandatory @c close hook.
 *
 * @param cookie  Per-open cookie previously returned by Open().
 * @return @c B_OK on success, or a negative error code on failure.
 */
status_t
AbstractModuleDevice::Close(void* cookie)
{
	return Module()->close(cookie);
}


/**
 * @brief Frees the per-open cookie allocated during Open().
 *
 * Delegates directly to the module's mandatory @c free hook. Called after
 * Close() once all references to the cookie have been dropped.
 *
 * @param cookie  The cookie to release.
 * @return @c B_OK on success, or a negative error code on failure.
 */
status_t
AbstractModuleDevice::Free(void* cookie)
{
	return Module()->free(cookie);
}
