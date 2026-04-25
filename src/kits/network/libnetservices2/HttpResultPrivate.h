/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2022, Haiku Inc.
 * Original author: Niels Sascha Reedijk.
 */

/** @file HttpResultPrivate.h
    @brief Shared state block backing BHttpResult — atomically tracks request
           status, holds the parsed response, and synchronises the producer
           BHttpSession thread with consumer threads waiting on the result. */

#ifndef _HTTP_RESULT_PRIVATE_H_
#define _HTTP_RESULT_PRIVATE_H_


#include <memory>
#include <optional>
#include <string>

#include <DataIO.h>
#include <ExclusiveBorrow.h>
#include <OS.h>
#include <String.h>


namespace BPrivate {

namespace Network {

/** @brief Shared producer/consumer state for an in-flight HTTP request,
           guarded by atomic status word and a data-wait semaphore. */
struct HttpResultPrivate {
	// Read-only properties (multi-thread safe)
	const	int32				id;

	// Locking and atomic variables
	enum { kNoData = 0, kStatusReady, kHeadersReady, kBodyReady, kError };
			int32				requestStatus = kNoData;
			int32				canCancel = 0;
			sem_id				data_wait;

	// Data
			std::optional<BHttpStatus> status;
			std::optional<BHttpFields> fields;
			std::optional<BHttpBody> body;
			std::optional<std::exception_ptr> error;

	// Interim body storage (used while the request is running)
			BString				bodyString;
			BBorrow<BDataIO>	bodyTarget;

	// Utility functions
								HttpResultPrivate(int32 identifier);
			int32				GetStatusAtomic();
			bool				CanCancel();
			void				SetCancel();
			void				SetError(std::exception_ptr e);
			void				SetStatus(BHttpStatus&& s);
			void				SetFields(BHttpFields&& f);
			void				SetBody();
			size_t				WriteToBody(const void* buffer, size_t size);
};


/**
 * @brief Initialise the shared state with a unique @a identifier and create
 *        the data-wait semaphore used to signal consumers.
 *
 * @param identifier  Stable id used to name the semaphore for debugging.
 */
inline HttpResultPrivate::HttpResultPrivate(int32 identifier)
	:
	id(identifier)
{
	std::string name = "httpresult:" + std::to_string(identifier);
	data_wait = create_sem(1, name.c_str());
	if (data_wait < B_OK)
		throw BRuntimeError(__PRETTY_FUNCTION__, "Cannot create internal sem for httpresult");
}


/**
 * @brief Return the current request status without acquiring any lock.
 *
 * @return One of the kNoData, kStatusReady, kHeadersReady, kBodyReady, or
 *         kError sentinels.
 */
inline int32
HttpResultPrivate::GetStatusAtomic()
{
	return atomic_get(&requestStatus);
}


/**
 * @brief Whether the request is in a state where a cancel signal will be
 *        honoured by the session thread.
 *
 * @return true if cancellation is currently safe.
 */
inline bool
HttpResultPrivate::CanCancel()
{
	return atomic_get(&canCancel) == 1;
}


/** @brief Atomically set the cancel-allowed flag. */
inline void
HttpResultPrivate::SetCancel()
{
	atomic_set(&canCancel, 1);
}


/**
 * @brief Record a fatal error, release any borrowed body target, and wake
 *        any thread blocked on the data-wait semaphore.
 *
 * @param e  Exception pointer carrying the failure reason.
 */
inline void
HttpResultPrivate::SetError(std::exception_ptr e)
{
	// Release any held body target borrow
	bodyTarget.Return();

	error = e;
	atomic_set(&requestStatus, kError);
	release_sem(data_wait);
}


/**
 * @brief Publish the parsed response status line and signal waiters.
 *
 * @param s  Parsed BHttpStatus moved into the result.
 */
inline void
HttpResultPrivate::SetStatus(BHttpStatus&& s)
{
	status = std::move(s);
	atomic_set(&requestStatus, kStatusReady);
	release_sem(data_wait);
}


/**
 * @brief Publish the parsed response header fields and signal waiters.
 *
 * @param f  Parsed BHttpFields moved into the result.
 */
inline void
HttpResultPrivate::SetFields(BHttpFields&& f)
{
	fields = std::move(f);
	atomic_set(&requestStatus, kHeadersReady);
	release_sem(data_wait);
}


/**
 * @brief Finalise the response body — either swap in the in-memory string
 *        or release the borrowed BDataIO target — and signal waiters.
 */
inline void
HttpResultPrivate::SetBody()
{
	if (bodyTarget.HasValue()) {
		body = BHttpBody{};
		bodyTarget.Return();
	} else
		body = BHttpBody{std::move(bodyString)};

	atomic_set(&requestStatus, kBodyReady);
	release_sem(data_wait);
}


/**
 * @brief Append @a size bytes from @a buffer to the response body sink.
 *
 * Routes to the borrowed BDataIO target when one was supplied, otherwise
 * appends to the internal bodyString.
 *
 * @param buffer  Pointer to the bytes received from the network.
 * @param size    Byte count.
 * @return The number of bytes accepted by the sink.
 */
inline size_t
HttpResultPrivate::WriteToBody(const void* buffer, size_t size)
{
	// TODO: when the support for a shared BMemoryRingIO is here, choose
	// between one or the other depending on which one is available.
	if (bodyTarget.HasValue()) {
		auto result = bodyTarget->Write(buffer, size);
		if (result < 0)
			throw BSystemError("BDataIO::Write()", result);
		return result;
	} else {
		bodyString.Append(reinterpret_cast<const char*>(buffer), size);
		return size;
	}
}


} // namespace Network

} // namespace BPrivate

#endif // _HTTP_RESULT_PRIVATE_H_
