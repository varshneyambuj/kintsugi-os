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
 *   Copyright 2008-2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file vfs_request_io.cpp
 * @brief VFS-level scatter-gather I/O helpers used internally by vfs.cpp for vectored reads and writes.
 */

// included by vfs.cpp


//#define TRACE_VFS_REQUEST_IO
#ifdef TRACE_VFS_REQUEST_IO
#	define TRACE_RIO(x...) dprintf(x)
#else
#	define TRACE_RIO(x...) do {} while (false)
#endif


#include <heap.h>
#include <AutoDeleterDrivers.h>


// #pragma mark - AsyncIOCallback


/** @brief Virtual destructor for AsyncIOCallback.
 *
 * Ensures that subclass destructors are called correctly when an
 * AsyncIOCallback pointer is deleted through the base class.
 */
AsyncIOCallback::~AsyncIOCallback()
{
}


/** @brief Static trampoline registered with the I/O request as a finish callback.
 *
 * Casts @p data back to an AsyncIOCallback and delegates to its
 * IOFinished() virtual method.
 *
 * @param data              Opaque pointer to the AsyncIOCallback instance.
 * @param request           The completed io_request.
 * @param status            Final status code of the I/O operation.
 * @param partialTransfer   @c true if fewer bytes were transferred than
 *                          requested.
 * @param bytesTransferred  Number of bytes actually transferred.
 */
/* static */ void
AsyncIOCallback::IORequestCallback(void* data, io_request* request,
	status_t status, bool partialTransfer, generic_size_t bytesTransferred)
{
	((AsyncIOCallback*)data)->IOFinished(status, partialTransfer,
		bytesTransferred);
}


// #pragma mark - StackableAsyncIOCallback


/** @brief Construct a StackableAsyncIOCallback that chains to @p next.
 *
 * @param next  The next callback in the chain to invoke after this one
 *              completes its work.  May be NULL if there is no next callback.
 */
StackableAsyncIOCallback::StackableAsyncIOCallback(AsyncIOCallback* next)
	:
	fNextCallback(next)
{
}


// #pragma mark -


/** @brief Cookie used by the iterative file-descriptor I/O subsystem.
 *
 * Carries all state needed to drive repeated calls to do_iterative_fd_io_iterate()
 * and the final do_iterative_fd_io_finish() callback.
 */
struct iterative_io_cookie {
	struct vnode*					vnode;
	file_descriptor*				descriptor;
	iterative_io_get_vecs			get_vecs;
	iterative_io_finished			finished;
	void*							cookie;
	off_t							request_offset;
	io_request_finished_callback	next_finished_callback;
	void*							next_finished_cookie;
};


/** @brief Abstract base class for a single-shot synchronous I/O operation.
 *
 * Subclasses implement IO() to perform the actual read or write using
 * different underlying mechanisms (callback-based or vnode-based).
 */
class DoIO {
public:
	/** @brief Construct a DoIO for the given transfer direction.
	 *
	 * @param write @c true for write operations, @c false for reads.
	 */
	DoIO(bool write)
		:
		fWrite(write)
	{
	}

	/** @brief Virtual destructor. */
	virtual	~DoIO()
	{
	}

	/** @brief Perform a single contiguous I/O segment.
	 *
	 * @param offset  Byte offset within the file/device at which to start.
	 * @param buffer  User/kernel buffer to read into or write from.
	 * @param length  On entry the maximum number of bytes to transfer; on
	 *                return the actual number of bytes transferred.
	 * @return B_OK on success, or an error code on failure.
	 */
	virtual status_t IO(off_t offset, void* buffer, size_t* length) = 0;

protected:
	bool	fWrite;
};


/** @brief DoIO implementation that delegates to a caller-supplied callback.
 *
 * Used by vfs_synchronous_io() to wrap arbitrary read/write functions in the
 * synchronous_io() scatter-gather loop.
 */
class CallbackIO : public DoIO {
public:
	/** @brief Construct a CallbackIO.
	 *
	 * @param write   @c true for write, @c false for read.
	 * @param doIO    Function pointer to the actual I/O routine.
	 * @param cookie  Opaque value forwarded as the first argument to @p doIO.
	 */
	CallbackIO(bool write,
			status_t (*doIO)(void* cookie, off_t offset, void* buffer,
				size_t* length),
			void* cookie)
		:
		DoIO(write),
		fDoIO(doIO),
		fCookie(cookie)
	{
	}

	/** @brief Invoke the stored callback to transfer one segment.
	 *
	 * @param offset  Byte offset within the file at which to start.
	 * @param buffer  Buffer to read into or write from.
	 * @param length  In/out byte count.
	 * @return Status returned by the callback.
	 */
	virtual status_t IO(off_t offset, void* buffer, size_t* length)
	{
		return fDoIO(fCookie, offset, buffer, length);
	}

private:
	status_t (*fDoIO)(void*, off_t, void*, size_t*);
	void*		fCookie;
};


/** @brief DoIO implementation that calls vnode read/write_pages (or read/write).
 *
 * Prefers the pages variants to bypass the page cache when available.
 */
class VnodeIO : public DoIO {
public:
	/** @brief Construct a VnodeIO.
	 *
	 * @param write   @c true for write, @c false for read.
	 * @param vnode   The vnode to issue the I/O against.
	 * @param cookie  The open-file cookie associated with the vnode.
	 */
	VnodeIO(bool write, struct vnode* vnode, void* cookie)
		:
		DoIO(write),
		fVnode(vnode),
		fCookie(cookie)
	{
	}

	/** @brief Perform a single I/O segment against the vnode.
	 *
	 * Builds a single-element iovec and dispatches to write_pages/read_pages
	 * if the file system provides them; otherwise falls back to write/read.
	 *
	 * @param offset  Byte offset within the file.
	 * @param buffer  Buffer to read into or write from.
	 * @param length  In/out byte count.
	 * @return Status code from the file-system operation.
	 */
	virtual status_t IO(off_t offset, void* buffer, size_t* length)
	{
		iovec vec;
		vec.iov_base = buffer;
		vec.iov_len = *length;

		// We need to use write_pages (if it exists) to bypass caches.

		if (fWrite) {
			if (!HAS_FS_CALL(fVnode, write_pages))
				return FS_CALL(fVnode, write, fCookie, offset, buffer, length);
			return FS_CALL(fVnode, write_pages, fCookie, offset, &vec, 1,
				length);
		}

		if (!HAS_FS_CALL(fVnode, read_pages))
			return FS_CALL(fVnode, read, fCookie, offset, buffer, length);
		return FS_CALL(fVnode, read_pages, fCookie, offset, &vec, 1, length);
	}

private:
	struct vnode*	fVnode;
	void*			fCookie;
};


/** @brief Iteration callback: build and schedule sub-requests from file vectors.
 *
 * Called repeatedly by the I/O scheduler to translate logical file offsets
 * into physical device offsets.  Fetches up to kMaxSubRequests file vectors
 * from the get_vecs callback, handles sparse (zero-fill) regions inline, and
 * dispatches sub-requests to the vnode's io() entry point.
 *
 * @param _cookie           Pointer to the iterative_io_cookie for this request.
 * @param request           The parent io_request being iterated.
 * @param _partialTransfer  Set to @c true if the transfer could not be
 *                          completed fully (e.g. get_vecs returned no vectors).
 * @return B_OK on success, or an error code if a fatal failure occurred.
 */
static status_t
do_iterative_fd_io_iterate(void* _cookie, io_request* request,
	bool* _partialTransfer)
{
	TRACE_RIO("[%ld] do_iterative_fd_io_iterate(request: %p)\n",
		find_thread(NULL), request);

	static const size_t kMaxSubRequests = 8;

	iterative_io_cookie* cookie = (iterative_io_cookie*)_cookie;

	request->DeleteSubRequests();

	off_t requestOffset = cookie->request_offset;
	size_t requestLength = request->Length()
		- (requestOffset - request->Offset());

	// get the next file vecs
	file_io_vec vecs[kMaxSubRequests];
	size_t vecCount = kMaxSubRequests;
	status_t error = cookie->get_vecs(cookie->cookie, request, requestOffset,
		requestLength, vecs, &vecCount);
	if (error != B_OK && error != B_BUFFER_OVERFLOW)
		return error;
	if (vecCount == 0) {
		*_partialTransfer = true;
		return B_OK;
	}
	TRACE_RIO("[%ld]  got %zu file vecs\n", find_thread(NULL), vecCount);

	// Reset the error code for the loop below
	error = B_OK;

	// create subrequests for the file vecs we've got
	size_t subRequestCount = 0;
	for (size_t i = 0;
		i < vecCount && subRequestCount < kMaxSubRequests && error == B_OK;
		i++) {
		off_t vecOffset = vecs[i].offset;
		off_t vecLength = min_c(vecs[i].length, (off_t)requestLength);
		TRACE_RIO("[%ld]    vec %lu offset: %lld, length: %lld\n",
			find_thread(NULL), i, vecOffset, vecLength);

		// Special offset -1 means that this is part of sparse file that is
		// zero. We fill it in right here.
		if (vecOffset == -1) {
			if (request->IsWrite()) {
				panic("do_iterative_fd_io_iterate(): write to sparse file "
					"vector");
				error = B_BAD_VALUE;
				break;
			}

			error = request->ClearData(requestOffset, vecLength);
			if (error != B_OK)
				break;

			requestOffset += vecLength;
			requestLength -= vecLength;
			continue;
		}

		while (vecLength > 0 && subRequestCount < kMaxSubRequests) {
			TRACE_RIO("[%ld]    creating subrequest: offset: %lld, length: "
				"%lld\n", find_thread(NULL), vecOffset, vecLength);
			IORequest* subRequest;
			error = request->CreateSubRequest(requestOffset, vecOffset,
				vecLength, subRequest);
			if (error != B_OK)
				break;

			subRequestCount++;

			size_t lengthProcessed = subRequest->Length();
			vecOffset += lengthProcessed;
			vecLength -= lengthProcessed;
			requestOffset += lengthProcessed;
			requestLength -= lengthProcessed;
		}
	}

	// Only if we couldn't create any subrequests, we fail.
	if (error != B_OK && subRequestCount == 0)
		return error;

	// Reset the error code for the loop below
	error = B_OK;

	request->Advance(requestOffset - cookie->request_offset);
	cookie->request_offset = requestOffset;

	// If we don't have any sub requests at this point, that means all that
	// remained were zeroed sparse file vectors. So the request is done now.
	if (subRequestCount == 0) {
		ASSERT(request->RemainingBytes() == 0);
		request->SetStatusAndNotify(B_OK);
		return B_OK;
	}

	// Schedule the subrequests.
	IORequest* nextSubRequest = request->FirstSubRequest();
	while (nextSubRequest != NULL) {
		IORequest* subRequest = nextSubRequest;
		nextSubRequest = request->NextSubRequest(subRequest);

		if (error == B_OK) {
			TRACE_RIO("[%ld]  scheduling subrequest: %p\n", find_thread(NULL),
				subRequest);
			error = vfs_vnode_io(cookie->vnode, cookie->descriptor->cookie,
				subRequest);
		} else {
			// Once scheduling a subrequest failed, we cancel all subsequent
			// subrequests.
			subRequest->SetStatusAndNotify(B_CANCELED);
		}
	}

	// TODO: Cancel the subrequests that were scheduled successfully.

	return B_OK;
}


/** @brief Completion callback for do_iterative_fd_io().
 *
 * Invokes the caller-supplied finished callback, releases the file descriptor,
 * chains to any previously registered finished callback, and deletes the
 * iterative_io_cookie.
 *
 * @param _cookie           Pointer to the iterative_io_cookie to finalise.
 * @param request           The completed io_request.
 * @param status            Final status of the I/O operation.
 * @param partialTransfer   @c true if the transfer was incomplete.
 * @param bytesTransferred  Number of bytes successfully transferred.
 */
static void
do_iterative_fd_io_finish(void* _cookie, io_request* request, status_t status,
	bool partialTransfer, generic_size_t bytesTransferred)
{
	iterative_io_cookie* cookie = (iterative_io_cookie*)_cookie;

	if (cookie->finished != NULL) {
		cookie->finished(cookie->cookie, request, status, partialTransfer,
			bytesTransferred);
	}

	put_fd(cookie->descriptor);

	if (cookie->next_finished_callback != NULL) {
		cookie->next_finished_callback(cookie->next_finished_cookie, request,
			status, partialTransfer, bytesTransferred);
	}

	delete cookie;
}


/** @brief Perform iterative vnode I/O synchronously, without an io() hook.
 *
 * Used as a fallback when the vnode's file system does not provide an
 * asynchronous io() entry point.  Iterates over the virtual address vectors
 * in the request buffer, translating logical file offsets to physical ones
 * via @p getVecs, and performs each segment synchronously via VnodeIO.
 *
 * @param vnode       Vnode to perform I/O on.
 * @param openCookie  Open-file cookie for the vnode.
 * @param request     The io_request describing the transfer.
 * @param getVecs     Callback that maps logical offsets to file_io_vec arrays.
 *                    May be NULL to use a 1:1 identity mapping.
 * @param finished    Optional completion callback invoked before the request
 *                    status is set.
 * @param cookie      Opaque value forwarded to @p getVecs and @p finished.
 * @return The final I/O status (B_OK or an error code).
 */
static status_t
do_synchronous_iterative_vnode_io(struct vnode* vnode, void* openCookie,
	io_request* request, iterative_io_get_vecs getVecs,
	iterative_io_finished finished, void* cookie)
{
	IOBuffer* buffer = request->Buffer();
	VnodeIO io(request->IsWrite(), vnode, openCookie);

	iovec vector;
	void* virtualVecCookie = NULL;
	off_t offset = request->Offset();
	generic_size_t length = request->Length();

	status_t error = B_OK;
	bool partial = false;

	for (; error == B_OK && length > 0 && !partial
			&& buffer->GetNextVirtualVec(virtualVecCookie, vector) == B_OK;) {
		uint8* vecBase = (uint8*)vector.iov_base;
		generic_size_t vecLength = min_c(vector.iov_len, length);

		while (error == B_OK && vecLength > 0) {
			file_io_vec fileVecs[8];
			size_t fileVecCount = 8;
			if (getVecs != NULL) {
				error = getVecs(cookie, request, offset, vecLength, fileVecs,
					&fileVecCount);
			} else {
				fileVecs[0].offset = offset;
				fileVecs[0].length = vecLength;
				fileVecCount = 1;
			}
			if (error != B_OK)
				break;
			if (fileVecCount == 0) {
				partial = true;
				break;
			}

			for (size_t i = 0; i < fileVecCount; i++) {
				const file_io_vec& fileVec = fileVecs[i];
				size_t toTransfer = min_c(fileVec.length, (off_t)length);
				size_t transferred = toTransfer;
				error = io.IO(fileVec.offset, vecBase, &transferred);
				if (error != B_OK)
					break;

				offset += transferred;
				length -= transferred;
				vecBase += transferred;
				vecLength -= transferred;

				if (transferred != toTransfer) {
					partial = true;
					break;
				}
			}
		}
	}

	buffer->FreeVirtualVecCookie(virtualVecCookie);

	partial = (partial || length > 0);
	size_t bytesTransferred = request->Length() - length;
	request->SetTransferredBytes(partial, bytesTransferred);
	if (finished != NULL)
		finished(cookie, request, error, partial, bytesTransferred);
	request->SetStatusAndNotify(error);
	return error;
}


/** @brief Drive an io_request synchronously through a DoIO strategy object.
 *
 * Iterates the virtual address vectors in the request's IOBuffer, calling
 * @p io.IO() for each segment until all bytes are transferred, a short
 * transfer is detected, or an error occurs.
 *
 * @param request  The io_request to satisfy.
 * @param io       Reference to the DoIO strategy to use for each segment.
 * @return B_OK on full success, or an error code on failure; the request
 *         status is also set and notified before returning.
 */
static status_t
synchronous_io(io_request* request, DoIO& io)
{
	TRACE_RIO("[%" B_PRId32 "] synchronous_io(request: %p (offset: %" B_PRIdOFF
		", length: %" B_PRIuGENADDR "))\n", find_thread(NULL), request,
		request->Offset(), request->Length());

	IOBuffer* buffer = request->Buffer();

	iovec vector;
	void* virtualVecCookie = NULL;
	off_t offset = request->Offset();
	generic_size_t length = request->Length();

	status_t status = B_OK;
	while (length > 0) {
		status = buffer->GetNextVirtualVec(virtualVecCookie, vector);
		if (status != B_OK)
			break;

		void* vecBase = (void*)(addr_t)vector.iov_base;
		size_t vecLength = min_c(vector.iov_len, length);

		TRACE_RIO("[%ld]   I/O: offset: %lld, vecBase: %p, length: %lu\n",
			find_thread(NULL), offset, vecBase, vecLength);

		size_t transferred = vecLength;
		status = io.IO(offset, vecBase, &transferred);
		if (status != B_OK)
			break;

		offset += transferred;
		length -= transferred;

		if (transferred != vecLength)
			break;
	}

	TRACE_RIO("[%ld] synchronous_io() finished: %#lx\n",
		find_thread(NULL), status);

	buffer->FreeVirtualVecCookie(virtualVecCookie);
	request->SetTransferredBytes(length > 0, request->Length() - length);
	request->SetStatusAndNotify(status);
	return status;
}


// #pragma mark - kernel private API


/** @brief Submit an io_request to a vnode, falling back to synchronous I/O.
 *
 * Attempts to call the file system's io() hook.  If the hook is absent or
 * returns B_UNSUPPORTED, the request is satisfied synchronously via
 * synchronous_io() / VnodeIO.
 *
 * @param vnode    The vnode to issue the request against.
 * @param cookie   The open-file cookie for the vnode.
 * @param request  The io_request to submit.
 * @return B_OK on success, or an error code on failure.
 */
status_t
vfs_vnode_io(struct vnode* vnode, void* cookie, io_request* request)
{
	status_t result = B_ERROR;
	if (!HAS_FS_CALL(vnode, io)
		|| (result = FS_CALL(vnode, io, cookie, request)) == B_UNSUPPORTED) {
		// no io() call -- fall back to synchronous I/O
		VnodeIO io(request->IsWrite(), vnode, cookie);
		return synchronous_io(request, io);
	}

	return result;
}


/** @brief Satisfy an io_request synchronously using a caller-supplied callback.
 *
 * Wraps @p doIO in a CallbackIO object and drives it through synchronous_io().
 *
 * @param request  The io_request to satisfy.
 * @param doIO     Callback that performs the actual read or write for each
 *                 segment.
 * @param cookie   Opaque value forwarded as the first argument to @p doIO.
 * @return B_OK on success, or an error code on failure.
 */
status_t
vfs_synchronous_io(io_request* request,
	status_t (*doIO)(void* cookie, off_t offset, void* buffer, size_t* length),
	void* cookie)
{
	CallbackIO io(request->IsWrite(), doIO, cookie);
	return synchronous_io(request, io);
}


/** @brief Asynchronously read pages from a vnode into a generic I/O buffer.
 *
 * Allocates an IORequest, initialises it for a read at @p pos, attaches
 * @p callback as the completion notification, and submits the request to
 * vfs_vnode_io().  On any allocation or initialisation failure the callback
 * is invoked directly with the error before returning.
 *
 * @param vnode       The vnode to read from.
 * @param cookie      The open-file cookie for the vnode.
 * @param pos         Byte offset within the file at which to start reading.
 * @param vecs        Array of generic_io_vec destination buffers.
 * @param count       Number of elements in @p vecs.
 * @param numBytes    Total number of bytes to read.
 * @param flags       I/O flags (e.g. B_VIP_IO_REQUEST, B_DELETE_IO_REQUEST).
 * @param callback    Callback invoked when the request completes.
 * @return B_OK if the request was submitted successfully, B_NO_MEMORY if
 *         allocation failed, or another error code if initialisation failed.
 */
status_t
vfs_asynchronous_read_pages(struct vnode* vnode, void* cookie, off_t pos,
	const generic_io_vec* vecs, size_t count, generic_size_t numBytes,
	uint32 flags, AsyncIOCallback* callback)
{
	IORequest* request = IORequest::Create((flags & B_VIP_IO_REQUEST) != 0);
	if (request == NULL) {
		callback->IOFinished(B_NO_MEMORY, true, 0);
		return B_NO_MEMORY;
	}

	status_t status = request->Init(pos, vecs, count, numBytes, false,
		flags | B_DELETE_IO_REQUEST);
	if (status != B_OK) {
		delete request;
		callback->IOFinished(status, true, 0);
		return status;
	}

	request->SetFinishedCallback(&AsyncIOCallback::IORequestCallback,
		callback);

	return vfs_vnode_io(vnode, cookie, request);
}


/** @brief Asynchronously write pages from a generic I/O buffer to a vnode.
 *
 * Allocates an IORequest, initialises it for a write at @p pos, attaches
 * @p callback as the completion notification, and submits the request to
 * vfs_vnode_io().  On any allocation or initialisation failure the callback
 * is invoked directly with the error before returning.
 *
 * @param vnode       The vnode to write to.
 * @param cookie      The open-file cookie for the vnode.
 * @param pos         Byte offset within the file at which to start writing.
 * @param vecs        Array of generic_io_vec source buffers.
 * @param count       Number of elements in @p vecs.
 * @param numBytes    Total number of bytes to write.
 * @param flags       I/O flags (e.g. B_VIP_IO_REQUEST, B_DELETE_IO_REQUEST).
 * @param callback    Callback invoked when the request completes.
 * @return B_OK if the request was submitted successfully, B_NO_MEMORY if
 *         allocation failed, or another error code if initialisation failed.
 */
status_t
vfs_asynchronous_write_pages(struct vnode* vnode, void* cookie, off_t pos,
	const generic_io_vec* vecs, size_t count, generic_size_t numBytes,
	uint32 flags, AsyncIOCallback* callback)
{
	IORequest* request = IORequest::Create((flags & B_VIP_IO_REQUEST) != 0);
	if (request == NULL) {
		callback->IOFinished(B_NO_MEMORY, true, 0);
		return B_NO_MEMORY;
	}

	status_t status = request->Init(pos, vecs, count, numBytes, true,
		flags | B_DELETE_IO_REQUEST);
	if (status != B_OK) {
		delete request;
		callback->IOFinished(status, true, 0);
		return status;
	}

	request->SetFinishedCallback(&AsyncIOCallback::IORequestCallback,
		callback);

	return vfs_vnode_io(vnode, cookie, request);
}


// #pragma mark - public API


/** @brief Perform I/O on an open file descriptor using the iterative path.
 *
 * Convenience wrapper around do_iterative_fd_io() with no get_vecs callback,
 * no finished callback, and no cookie — suitable for simple, contiguous
 * file-descriptor I/O.
 *
 * @param fd       Open file descriptor to perform I/O on.
 * @param request  The io_request describing the operation.
 * @return B_OK on success, or B_FILE_ERROR / another error code on failure.
 */
status_t
do_fd_io(int fd, io_request* request)
{
	return do_iterative_fd_io(fd, request, NULL, NULL, NULL);
}


/** @brief Perform (optionally iterative) I/O on an open file descriptor.
 *
 * Resolves the file descriptor to a vnode, validates the access mode, and
 * either falls back to do_synchronous_iterative_vnode_io() (when the vnode
 * has no io() hook or memory is exhausted) or sets up an iterative_io_cookie
 * and drives the request asynchronously through the vnode's io() hook.
 *
 * @param fd        Open file descriptor to perform I/O on.
 * @param request   The io_request describing the operation.
 * @param getVecs   Callback that translates logical file offsets to
 *                  file_io_vec arrays; may be NULL for contiguous I/O.
 * @param finished  Optional callback invoked when the request completes.
 * @param cookie    Opaque value forwarded to @p getVecs and @p finished.
 * @return B_OK if the request was submitted or completed successfully,
 *         B_FILE_ERROR if the descriptor is invalid or the access mode is
 *         wrong, or another error code on failure.
 */
status_t
do_iterative_fd_io(int fd, io_request* request, iterative_io_get_vecs getVecs,
	iterative_io_finished finished, void* cookie)
{
	TRACE_RIO("[%" B_PRId32 "] do_iterative_fd_io(fd: %d, request: %p "
		"(offset: %" B_PRIdOFF ", length: %" B_PRIuGENADDR "))\n",
		find_thread(NULL), fd, request, request->Offset(), request->Length());

	struct vnode* vnode;
	file_descriptor* descriptor = get_fd_and_vnode(fd, &vnode, true);
	FileDescriptorPutter descriptorPutter(descriptor);
	if (descriptor != NULL && (request->IsWrite()
			? (descriptor->open_mode & O_RWMASK) == O_RDONLY
			: (descriptor->open_mode & O_RWMASK) == O_WRONLY)) {
		descriptor = NULL;
	}

	if (descriptor == NULL) {
		if (finished != NULL)
			finished(cookie, request, B_FILE_ERROR, true, 0);
		request->SetStatusAndNotify(B_FILE_ERROR);
		return B_FILE_ERROR;
	}

	if (!HAS_FS_CALL(vnode, io)) {
		// no io() call -- fall back to synchronous I/O
		return do_synchronous_iterative_vnode_io(vnode, descriptor->cookie,
			request, getVecs, finished, cookie);
	}

	iterative_io_cookie* iterationCookie
		= (request->Flags() & B_VIP_IO_REQUEST) != 0
			? new(malloc_flags(HEAP_PRIORITY_VIP)) iterative_io_cookie
			: new(std::nothrow) iterative_io_cookie;
	if (iterationCookie == NULL) {
		// no memory -- fall back to synchronous I/O
		return do_synchronous_iterative_vnode_io(vnode, descriptor->cookie,
			request, getVecs, finished, cookie);
	}

	iterationCookie->vnode = vnode;
	iterationCookie->descriptor = descriptor;
	iterationCookie->get_vecs = getVecs;
	iterationCookie->finished = finished;
	iterationCookie->cookie = cookie;
	iterationCookie->request_offset = request->Offset();
	iterationCookie->next_finished_callback = request->FinishedCallback(
		&iterationCookie->next_finished_cookie);

	request->SetFinishedCallback(&do_iterative_fd_io_finish, iterationCookie);
	if (getVecs != NULL)
		request->SetIterationCallback(&do_iterative_fd_io_iterate, iterationCookie);

	descriptorPutter.Detach();
		// From now on the descriptor is put by our finish callback.

	if (getVecs != NULL) {
		bool partialTransfer = false;
		status_t error = do_iterative_fd_io_iterate(iterationCookie, request,
			&partialTransfer);
		if (error != B_OK || partialTransfer) {
			if (partialTransfer) {
				request->SetTransferredBytes(partialTransfer,
					request->TransferredBytes());
			}

			request->SetStatusAndNotify(error);
			return error;
		}
	} else {
		return vfs_vnode_io(vnode, descriptor->cookie, request);
	}

	return B_OK;
}
