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
 *   Copyright 2009-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file io_requests.cpp
 * @brief C API wrappers for creating, queueing, and completing kernel IORequest objects.
 */

#include <io_requests.h>

#include "IORequest.h"


// #pragma mark - static helpers


/**
 * @brief Copies data between a caller buffer and an IORequest, then advances
 *        the request's transfer pointer.
 *
 * Validates direction and remaining capacity, locks user-space buffer memory
 * on demand, performs the copy, and advances the request by @p size bytes.
 *
 * @param request         The IORequest to transfer data to or from.
 * @param buffer          Caller-supplied memory region.
 * @param size            Number of bytes to transfer.
 * @param writeToRequest  @c true to copy @p buffer into the request (write
 *                        path); @c false to copy from the request into
 *                        @p buffer (read path).
 * @retval B_OK        Transfer succeeded.
 * @retval B_BAD_VALUE Direction mismatch or insufficient remaining bytes.
 * @return Any error returned by IOBuffer::LockMemory() or IORequest::CopyData().
 */
static status_t
transfer_io_request_data(io_request* request, void* buffer, size_t size,
	bool writeToRequest)
{
	if (writeToRequest == request->IsWrite()
		|| request->RemainingBytes() < size) {
		return B_BAD_VALUE;
	}

	// lock the request buffer memory, if it is user memory
	IOBuffer* ioBuffer = request->Buffer();
	if (ioBuffer->IsUser() && !ioBuffer->IsMemoryLocked()) {
		status_t error = ioBuffer->LockMemory(request->TeamID(),
			!writeToRequest);
		if (error != B_OK)
			return error;
	}

	// read/write
	off_t offset = request->Offset() + request->TransferredBytes();
	status_t error = writeToRequest
		? request->CopyData(buffer, offset, size)
		: request->CopyData(offset, buffer, size);
	if (error != B_OK)
		return error;

	request->Advance(size);
	return B_OK;
}


// #pragma mark - public API


/**
 * @brief Returns whether the given I/O request is a write request.
 *
 * @param request  The I/O request to query.
 * @return @c true if @p request is a write request; @c false if it is a
 *         read request.
 */
bool
io_request_is_write(const io_request* request)
{
	return request->IsWrite();
}


/**
 * @brief Returns whether the I/O request has VIP status.
 *
 * VIP requests are given scheduling priority over normal requests.
 *
 * @param request  The I/O request to query.
 * @return @c true if the @c B_VIP_IO_REQUEST flag is set; @c false otherwise.
 */
bool
io_request_is_vip(const io_request* request)
{
	return (request->Flags() & B_VIP_IO_REQUEST) != 0;
}


/**
 * @brief Returns the read/write offset of the given I/O request.
 *
 * This is the immutable offset the request was created with;
 * read_from_io_request() and write_to_io_request() don't change it.
 *
 * @param request  The I/O request to query.
 * @return The byte offset within the target medium at which the transfer begins.
 */
off_t
io_request_offset(const io_request* request)
{
	return request->Offset();
}


/**
 * @brief Returns the read/write length of the given I/O request.
 *
 * This is the immutable length the request was created with;
 * read_from_io_request() and write_to_io_request() don't change it.
 *
 * @param request  The I/O request to query.
 * @return The total number of bytes to be transferred by this request.
 */
off_t
io_request_length(const io_request* request)
{
	return request->Length();
}


/**
 * @brief Reads data from the given I/O request into the given buffer and
 *        advances the request's transferred data pointer.
 *
 * Multiple calls to read_from_io_request() are allowed, but the total size
 * must not exceed io_request_length(request).
 *
 * @param request  The I/O request to read data from.
 * @param buffer   Destination buffer to receive the data.
 * @param size     Number of bytes to read.
 * @retval B_OK        Data was read successfully.
 * @retval B_BAD_VALUE Request is not a write-originated request, or
 *                     insufficient remaining bytes.
 * @return Any error propagated from the underlying copy or memory-lock
 *         operations.
 */
status_t
read_from_io_request(io_request* request, void* buffer, size_t size)
{
	return transfer_io_request_data(request, buffer, size, false);
}


/**
 * @brief Writes data from the given buffer to the given I/O request and
 *        advances the request's transferred data pointer.
 *
 * Multiple calls to write_to_io_request() are allowed, but the total size
 * must not exceed io_request_length(request).
 *
 * @param request  The I/O request to write data into.
 * @param buffer   Source buffer containing the data to write.
 * @param size     Number of bytes to write.
 * @retval B_OK        Data was written successfully.
 * @retval B_BAD_VALUE Request is not a read-originated request, or
 *                     insufficient remaining bytes.
 * @return Any error propagated from the underlying copy or memory-lock
 *         operations.
 */
status_t
write_to_io_request(io_request* request, const void* buffer, size_t size)
{
	return transfer_io_request_data(request, (void*)buffer, size, true);
}


/**
 * @brief Sets the given I/O request's status and notifies listeners that
 *        the request is finished.
 *
 * @param request  The I/O request to finalise.
 * @param status   The completion status to store in the request (e.g.
 *                 @c B_OK on success, or an error code on failure).
 */
void
notify_io_request(io_request* request, status_t status)
{
	request->SetStatusAndNotify(status);
}
