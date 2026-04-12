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
 *   Copyright 2022 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Niels Sascha Reedijk, niels.reedijk@gmail.com
 */


/**
 * @file HttpSession.cpp
 * @brief Implementation of BHttpSession, the async HTTP/1.1 client.
 *
 * BHttpSession manages a pool of HTTP connections through two internal threads:
 * a control thread that resolves hostnames, opens TCP/TLS connections, and
 * enforces per-host connection limits; and a data thread that multiplexes
 * I/O on all active sockets using wait_for_objects().  BHttpResult provides a
 * future-like interface that blocks until the status, headers, or body are ready.
 *
 * @see BHttpRequest, BHttpResult, BHttpFields
 */


#include <algorithm>
#include <atomic>
#include <deque>
#include <list>
#include <map>
#include <optional>
#include <vector>

#include <AutoLocker.h>
#include <DataIO.h>
#include <ErrorsExt.h>
#include <HttpFields.h>
#include <HttpRequest.h>
#include <HttpResult.h>
#include <HttpSession.h>
#include <Locker.h>
#include <Messenger.h>
#include <NetBuffer.h>
#include <NetServicesDefs.h>
#include <NetworkAddress.h>
#include <OS.h>
#include <SecureSocket.h>
#include <Socket.h>
#include <ZlibCompressionAlgorithm.h>

#include "HttpBuffer.h"
#include "HttpParser.h"
#include "HttpResultPrivate.h"
#include "HttpSerializer.h"
#include "NetServicesPrivate.h"

using namespace std::literals;
using namespace BPrivate::Network;


/**
 * @brief Maximum allowed HTTP header line size in bytes.
 *
 * Prevents unbounded buffer growth while waiting for CRLF on a line that
 * never terminates.
 */
static constexpr ssize_t kMaxHeaderLineSize = 64 * 1024;


struct CounterDeleter {
	void operator()(int32* counter) const noexcept { atomic_add(counter, -1); }
};


class BHttpSession::Request
{
public:
	Request(BHttpRequest&& request, BBorrow<BDataIO> target, BMessenger observer);

	Request(Request& original, const Redirect& redirect);

	// States
	enum RequestState { InitialState, Connected, RequestSent, ContentReceived };
	RequestState State() const noexcept { return fRequestStatus; }

	// Result Helpers
	std::shared_ptr<HttpResultPrivate> Result() { return fResult; }
	void SetError(std::exception_ptr e);

	// Helpers for maintaining the connection count
	std::pair<BString, int> GetHost() const;
	void SetCounter(int32* counter) noexcept;

	// Operational methods
	void ResolveHostName();
	void OpenConnection();
	void TransferRequest();
	bool ReceiveResult();
	void Disconnect() noexcept;

	// Object information
	int Socket() const noexcept { return fSocket->Socket(); }
	int32 Id() const noexcept { return fResult->id; }
	bool CanCancel() const noexcept { return fResult->CanCancel(); }

	// Message helper
	void SendMessage(uint32 what, std::function<void(BMessage&)> dataFunc = nullptr) const;

private:
	BHttpRequest fRequest;

	// Request state/events
	RequestState fRequestStatus = InitialState;

	// Communication
	BMessenger fObserver;
	std::shared_ptr<HttpResultPrivate> fResult;

	// Connection
	BNetworkAddress fRemoteAddress;
	std::unique_ptr<BSocket> fSocket;

	// Sending and receiving
	HttpBuffer fBuffer;
	HttpSerializer fSerializer;
	HttpParser fParser;

	// Receive state
	BHttpStatus fStatus;
	BHttpFields fFields;

	// Redirection
	bool fMightRedirect = false;
	int8 fRemainingRedirects;

	// Connection counter
	std::unique_ptr<int32, CounterDeleter> fConnectionCounter;
};


class BHttpSession::Impl
{
public:
	Impl();
	~Impl() noexcept;

	BHttpResult Execute(BHttpRequest&& request, BBorrow<BDataIO> target, BMessenger observer);
	void Cancel(int32 identifier);
	void SetMaxConnectionsPerHost(size_t maxConnections);
	void SetMaxHosts(size_t maxConnections);

private:
		// Thread functions
	static status_t ControlThreadFunc(void* arg);
	static status_t DataThreadFunc(void* arg);

	// Helper functions
	std::vector<BHttpSession::Request> GetRequestsForControlThread();

private:
		// constants (can be accessed unlocked)
	const sem_id fControlQueueSem;
	const sem_id fDataQueueSem;
	const thread_id fControlThread;
	const thread_id fDataThread;

	// locking mechanism
	BLocker fLock;
	std::atomic<bool> fQuitting = false;

	// queues & shared data
	std::list<BHttpSession::Request> fControlQueue;
	std::deque<BHttpSession::Request> fDataQueue;
	std::vector<int32> fCancelList;

	// data owned by the controlThread
	using Host = std::pair<BString, int>;
	std::map<Host, int32> fConnectionCount;

	// data that can only be accessed atomically
	std::atomic<size_t> fMaxConnectionsPerHost = 2;
	std::atomic<size_t> fMaxHosts = 10;

	// data owned by the dataThread
	std::map<int, BHttpSession::Request> connectionMap;
	std::vector<object_wait_info> objectList;
};


struct BHttpSession::Redirect {
	BUrl url;
	bool redirectToGet;
};


// #pragma mark -- BHttpSession::Impl


/**
 * @brief Construct the session implementation, creating threads and semaphores.
 *
 * Throws BRuntimeError if semaphore creation or thread launch fails.
 */
BHttpSession::Impl::Impl()
	:
	fControlQueueSem(create_sem(0, "http:control")),
	fDataQueueSem(create_sem(0, "http:data")),
	fControlThread(spawn_thread(ControlThreadFunc, "http:control", B_NORMAL_PRIORITY, this)),
	fDataThread(spawn_thread(DataThreadFunc, "http:data", B_NORMAL_PRIORITY, this))
{
	// check initialization of semaphores
	if (fControlQueueSem < 0)
		throw BRuntimeError(__PRETTY_FUNCTION__, "Cannot create control queue semaphore");
	if (fDataQueueSem < 0)
		throw BRuntimeError(__PRETTY_FUNCTION__, "Cannot create data queue semaphore");

	// set up internal threads
	if (fControlThread < 0)
		throw BRuntimeError(__PRETTY_FUNCTION__, "Cannot create control thread");
	if (resume_thread(fControlThread) != B_OK)
		throw BRuntimeError(__PRETTY_FUNCTION__, "Cannot resume control thread");

	if (fDataThread < 0)
		throw BRuntimeError(__PRETTY_FUNCTION__, "Cannot create data thread");
	if (resume_thread(fDataThread) != B_OK)
		throw BRuntimeError(__PRETTY_FUNCTION__, "Cannot resume data thread");
}


/**
 * @brief Destructor — signals the threads to quit and waits for them to exit.
 */
BHttpSession::Impl::~Impl() noexcept
{
	fQuitting.store(true);
	delete_sem(fControlQueueSem);
	delete_sem(fDataQueueSem);
	status_t threadResult;
	wait_for_thread(fControlThread, &threadResult);
		// The control thread waits for the data thread
}


/**
 * @brief Submit a new HTTP request for asynchronous execution.
 *
 * Wraps the request in an internal Request object, enqueues it on the
 * control queue, and returns a BHttpResult future to the caller.
 *
 * @param request   The BHttpRequest to execute (moved).
 * @param target    BBorrow<BDataIO> that receives the response body.
 * @param observer  Optional BMessenger for progress notifications.
 * @return A BHttpResult future that provides access to the response.
 */
BHttpResult
BHttpSession::Impl::Execute(BHttpRequest&& request, BBorrow<BDataIO> target, BMessenger observer)
{
	auto wRequest = Request(std::move(request), std::move(target), observer);

	auto retval = BHttpResult(wRequest.Result());
	auto lock = AutoLocker<BLocker>(fLock);
	fControlQueue.push_back(std::move(wRequest));
	release_sem(fControlQueueSem);
	return retval;
}


/**
 * @brief Cancel a pending or active request by its identifier.
 *
 * Removes the request from the control queue if it is still there, and
 * marks it as cancelled in the data queue so the data thread can clean up.
 *
 * @param identifier  The int32 request ID returned by BHttpResult::Identity().
 */
void
BHttpSession::Impl::Cancel(int32 identifier)
{
	auto lock = AutoLocker<BLocker>(fLock);
	// Check if the item is on the control queue
	fControlQueue.remove_if([&identifier](auto& request) {
		if (request.Id() == identifier) {
			try {
				throw BNetworkRequestError(__PRETTY_FUNCTION__, BNetworkRequestError::Canceled);
			} catch (...) {
				request.SetError(std::current_exception());
			}
			return true;
		}
		return false;
	});

	// Get it on the list for deletion in the data queue
	fCancelList.push_back(identifier);
	release_sem(fDataQueueSem);
}


/**
 * @brief Set the maximum number of simultaneous connections per remote host.
 *
 * @param maxConnections  New per-host connection limit (must be between 1 and INT32_MAX-1).
 */
void
BHttpSession::Impl::SetMaxConnectionsPerHost(size_t maxConnections)
{
	if (maxConnections <= 0 || maxConnections >= INT32_MAX) {
		throw BRuntimeError(
			__PRETTY_FUNCTION__, "MaxConnectionsPerHost must be between 1 and INT32_MAX");
	}
	fMaxConnectionsPerHost.store(maxConnections, std::memory_order_relaxed);
}


/**
 * @brief Set the maximum number of distinct remote hosts with active connections.
 *
 * @param maxConnections  New host limit (must be at least 1).
 */
void
BHttpSession::Impl::SetMaxHosts(size_t maxConnections)
{
	if (maxConnections <= 0)
		throw BRuntimeError(__PRETTY_FUNCTION__, "MaxHosts must be 1 or more");
	fMaxHosts.store(maxConnections, std::memory_order_relaxed);
}


/**
 * @brief Control thread entry point — dequeues requests, resolves hosts, opens sockets.
 *
 * Waits on fControlQueueSem, processes queued requests subject to per-host
 * connection limits, opens connections, and hands them to the data thread.
 * On shutdown it cancels all pending requests and waits for the data thread.
 *
 * @param arg  Pointer to the owning BHttpSession::Impl.
 * @return B_OK.
 */
/*static*/ status_t
BHttpSession::Impl::ControlThreadFunc(void* arg)
{
	BHttpSession::Impl* impl = static_cast<BHttpSession::Impl*>(arg);

	// Outer loop to use the fControlQueueSem when new items have entered the queue
	while (true) {
		if (auto status = acquire_sem(impl->fControlQueueSem); status == B_INTERRUPTED)
			continue;
		else if (status != B_OK) {
			// Most likely B_BAD_SEM_ID indicating that the sem was deleted; go to cleanup
			break;
		}

		// Check if we have woken up because we are quitting
		if (impl->fQuitting.load())
			break;

		// Get items to process (locking done by the helper)
		auto requests = impl->GetRequestsForControlThread();
		if (requests.size() == 0)
			continue;

		for (auto& request: requests) {
			bool hasError = false;
			try {
				request.ResolveHostName();
				request.OpenConnection();
			} catch (...) {
				request.SetError(std::current_exception());
				hasError = true;
			}

			if (hasError) {
				// Do not add the request back to the queue; release the sem to do another round
				// in case there is another item waiting because the limits of concurrent requests
				// were reached
				release_sem(impl->fControlQueueSem);
				continue;
			}

			impl->fLock.Lock();
			impl->fDataQueue.push_back(std::move(request));
			impl->fLock.Unlock();
			release_sem(impl->fDataQueueSem);
		}
	}

	// Clean up and make sure we are quitting
	if (impl->fQuitting.load()) {
		// First wait for the data thread to complete
		status_t threadResult;
		wait_for_thread(impl->fDataThread, &threadResult);
		// Cancel all requests
		for (auto& request: impl->fControlQueue) {
			try {
				throw BNetworkRequestError(__PRETTY_FUNCTION__, BNetworkRequestError::Canceled);
			} catch (...) {
				request.SetError(std::current_exception());
			}
		}
	} else {
		throw BRuntimeError(
			__PRETTY_FUNCTION__, "Unknown reason that the controlQueueSem is deleted");
	}

	// Cleanup: wait for data thread
	return B_OK;
}


/** @brief Internal event flag used to signal a cancelled connection to the data thread. */
static constexpr uint16 EVENT_CANCELLED = 0x4000;


/**
 * @brief Data thread entry point — multiplexes I/O on all active sockets.
 *
 * Uses wait_for_objects() to wait on all active socket file descriptors and
 * the data queue semaphore simultaneously.  Dispatches to TransferRequest()
 * (write phase) or ReceiveResult() (read phase) based on the event type, and
 * handles cancellation, disconnection, and redirection.
 *
 * @param arg  Pointer to the owning BHttpSession::Impl.
 * @return B_OK.
 */
/*static*/ status_t
BHttpSession::Impl::DataThreadFunc(void* arg)
{
	BHttpSession::Impl* data = static_cast<BHttpSession::Impl*>(arg);

	// initial initialization of wait list
	data->objectList.push_back(
		object_wait_info{data->fDataQueueSem, B_OBJECT_TYPE_SEMAPHORE, B_EVENT_ACQUIRE_SEMAPHORE});

	while (true) {
		if (auto status = wait_for_objects(data->objectList.data(), data->objectList.size());
			status == B_INTERRUPTED)
			continue;
		else if (status < 0) {
			// Something went inexplicably wrong
			throw BSystemError("wait_for_objects()", status);
		}

		// First check if the change is in acquiring the sem, meaning that
		// there are new requests to be scheduled
		if (data->objectList[0].events == B_EVENT_ACQUIRE_SEMAPHORE) {
			if (auto status = acquire_sem(data->fDataQueueSem); status == B_INTERRUPTED)
				continue;
			else if (status != B_OK) {
				// Most likely B_BAD_SEM_ID indicating that the sem was deleted
				break;
			}

			// Process the cancelList and dataQueue. Note that there might
			// be a situation where a request is cancelled and added in the
			// same iteration, but that is taken care by this algorithm.
			data->fLock.Lock();
			while (!data->fDataQueue.empty()) {
				auto request = std::move(data->fDataQueue.front());
				data->fDataQueue.pop_front();
				auto socket = request.Socket();

				data->connectionMap.insert(std::make_pair(socket, std::move(request)));

				// Add to objectList
				data->objectList.push_back(
					object_wait_info{socket, B_OBJECT_TYPE_FD, B_EVENT_WRITE});
			}

			for (auto id: data->fCancelList) {
				// To cancel, we set a special event status on the
				// object_wait_info list so that we can handle it below.
				// Also: the first item in the waitlist is always the semaphore
				// so the fun starts at offset 1.
				size_t offset = 0;
				for (auto it = data->connectionMap.cbegin(); it != data->connectionMap.cend();
					it++) {
					offset++;
					if (it->second.Id() == id) {
						data->objectList[offset].events = EVENT_CANCELLED;
						break;
					}
				}
			}
			data->fCancelList.clear();
			data->fLock.Unlock();
		} else if ((data->objectList[0].events & B_EVENT_INVALID) == B_EVENT_INVALID) {
			// The semaphore has been deleted. Start the cleanup
			break;
		}

		// Process all objects that are ready
		bool resizeObjectList = false;
		for (auto& item: data->objectList) {
			if (item.type != B_OBJECT_TYPE_FD)
				continue;
			if ((item.events & B_EVENT_WRITE) == B_EVENT_WRITE) {
				auto& request = data->connectionMap.find(item.object)->second;
				auto error = false;
				try {
					request.TransferRequest();
				} catch (...) {
					request.SetError(std::current_exception());
					error = true;
				}

				// End failed writes
				if (error) {
					request.Disconnect();
					data->connectionMap.erase(item.object);
					release_sem(data->fControlQueueSem);
						// wake up control thread; there may queued requests unblocked.
					resizeObjectList = true;
				}
			} else if ((item.events & B_EVENT_READ) == B_EVENT_READ) {
				auto& request = data->connectionMap.find(item.object)->second;
				auto finished = false;
				try {
					if (request.CanCancel())
						finished = true;
					else
						finished = request.ReceiveResult();
				} catch (const Redirect& r) {
					// Request is redirected, send back to the controlThread
					// Move existing request into a new request and hand over to the control queue
					auto lock = AutoLocker<BLocker>(data->fLock);
					data->fControlQueue.emplace_back(request, r);
					release_sem(data->fControlQueueSem);

					finished = true;
				} catch (...) {
					request.SetError(std::current_exception());
					finished = true;
				}

				if (finished) {
					// Clean up finished requests; including redirected requests
					request.Disconnect();
					data->connectionMap.erase(item.object);
					release_sem(data->fControlQueueSem);
						// wake up control thread; there may queued requests unblocked.
					resizeObjectList = true;
				}
			} else if ((item.events & B_EVENT_DISCONNECTED) == B_EVENT_DISCONNECTED) {
				auto& request = data->connectionMap.find(item.object)->second;
				try {
					throw BNetworkRequestError(
						__PRETTY_FUNCTION__, BNetworkRequestError::NetworkError);
				} catch (...) {
					request.SetError(std::current_exception());
				}
				data->connectionMap.erase(item.object);
				resizeObjectList = true;
			} else if ((item.events & EVENT_CANCELLED) == EVENT_CANCELLED) {
				auto& request = data->connectionMap.find(item.object)->second;
				request.Disconnect();
				try {
					throw BNetworkRequestError(__PRETTY_FUNCTION__, BNetworkRequestError::Canceled);
				} catch (...) {
					request.SetError(std::current_exception());
				}
				data->connectionMap.erase(item.object);
				release_sem(data->fControlQueueSem);
					// wake up control thread; there may queued requests unblocked.
				resizeObjectList = true;
			} else if (item.events == 0) {
				// No events for this item, skip
				continue;
			} else {
				// Likely to be B_EVENT_INVALID. This should not happen
				auto& request = data->connectionMap.find(item.object)->second;
				request.SendMessage(UrlEvent::DebugMessage, [](BMessage& msg) {
					msg.AddUInt32(UrlEventData::DebugType, UrlEventData::DebugError);
					msg.AddString(UrlEventData::DebugMessage, "Unexpected event; socket deleted?");
				});
				throw BRuntimeError(
					__PRETTY_FUNCTION__, "Socket was deleted at an unexpected time");
			}
		}

		// Reset objectList
		data->objectList[0].events = B_EVENT_ACQUIRE_SEMAPHORE;
		if (resizeObjectList)
			data->objectList.resize(data->connectionMap.size() + 1);

		auto i = 1;
		for (auto it = data->connectionMap.cbegin(); it != data->connectionMap.cend(); it++) {
			data->objectList[i].object = it->first;
			if (it->second.State() == Request::InitialState)
				throw BRuntimeError(__PRETTY_FUNCTION__, "Invalid state of request");
			else if (it->second.State() == Request::Connected)
				data->objectList[i].events = B_EVENT_WRITE | B_EVENT_DISCONNECTED;
			else
				data->objectList[i].events = B_EVENT_READ | B_EVENT_DISCONNECTED;
			i++;
		}
	}
	// Clean up and make sure we are quitting
	if (data->fQuitting.load()) {
		// Cancel all requests
		for (auto it = data->connectionMap.begin(); it != data->connectionMap.end(); it++) {
			try {
				throw BNetworkRequestError(__PRETTY_FUNCTION__, BNetworkRequestError::Canceled);
			} catch (...) {
				it->second.SetError(std::current_exception());
			}
		}
	} else {
		throw BRuntimeError(__PRETTY_FUNCTION__, "Unknown reason that the dataQueueSem is deleted");
	}

	return B_OK;
}


/**
 * @brief Collect requests from the control queue that may be started immediately.
 *
 * Enforces the per-host connection limit and the total host limit by skipping
 * requests that would exceed either bound.  Increments the connection counter
 * for each request that is returned.
 *
 * @return Vector of Request objects ready for hostname resolution and connection.
 */
std::vector<BHttpSession::Request>
BHttpSession::Impl::GetRequestsForControlThread()
{
	std::vector<BHttpSession::Request> requests;

	// Clean up connection list if it is at the max number of hosts
	if (fConnectionCount.size() >= fMaxHosts.load()) {
		for (auto it = fConnectionCount.begin(); it != fConnectionCount.end();) {
			if (atomic_get(std::addressof(it->second)) == 0) {
				it = fConnectionCount.erase(it);
			} else {
				it++;
			}
		}
	}

	// Process the list of pending requests and review if they can be started.
	auto lock = AutoLocker<BLocker>(fLock);
	fControlQueue.remove_if([this, &requests](auto& request) {
		auto host = request.GetHost();
		auto it = fConnectionCount.find(host);
		if (it != fConnectionCount.end()) {
			if (static_cast<size_t>(atomic_get(std::addressof(it->second)))
				>= fMaxConnectionsPerHost.load(std::memory_order_relaxed)) {
				request.SendMessage(UrlEvent::DebugMessage, [](BMessage& msg) {
					msg.AddUInt32(UrlEventData::DebugType, UrlEventData::DebugWarning);
					msg.AddString(UrlEventData::DebugMessage,
						"Request is queued: too many active connections for host");
				});
				return false;
			} else {
				atomic_add(std::addressof(it->second), 1);
				request.SetCounter(std::addressof(it->second));
			}
		} else {
			if (fConnectionCount.size() == fMaxHosts.load()) {
				request.SendMessage(UrlEvent::DebugMessage, [](BMessage& msg) {
					msg.AddUInt32(UrlEventData::DebugType, UrlEventData::DebugWarning);
					msg.AddString(UrlEventData::DebugMessage,
						"Request is queued: maximum number of concurrent hosts");
				});
				return false;
			}
			auto [newIt, success] = fConnectionCount.insert({host, 1});
			if (!success) {
				throw BRuntimeError(__PRETTY_FUNCTION__, "Cannot insert into fConnectionCount");
			}
			request.SetCounter(std::addressof(newIt->second));
		}
		requests.emplace_back(std::move(request));
		return true;
	});
	return requests;
}


// #pragma mark -- BHttpSession (public interface)


/**
 * @brief Construct a BHttpSession, creating the internal Impl and worker threads.
 */
BHttpSession::BHttpSession()
{
	fImpl = std::make_shared<BHttpSession::Impl>();
}


/**
 * @brief Destructor.
 */
BHttpSession::~BHttpSession() = default;


/**
 * @brief Copy constructor — shares the underlying Impl.
 *
 * Multiple BHttpSession objects sharing an Impl all submit to the same thread pool.
 *
 * @param other  Source BHttpSession to share with.
 */
BHttpSession::BHttpSession(const BHttpSession&) noexcept = default;


/**
 * @brief Copy assignment operator — shares the underlying Impl.
 *
 * @param other  Source BHttpSession to share with.
 * @return Reference to this object.
 */
BHttpSession& BHttpSession::operator=(const BHttpSession&) noexcept = default;


/**
 * @brief Submit an HTTP request and return a future BHttpResult.
 *
 * @param request   The BHttpRequest to execute (moved).
 * @param target    BBorrow<BDataIO> that receives the response body bytes.
 * @param observer  Optional BMessenger for progress and status notifications.
 * @return BHttpResult that provides blocking access to status, fields, and body.
 */
BHttpResult
BHttpSession::Execute(BHttpRequest&& request, BBorrow<BDataIO> target, BMessenger observer)
{
	return fImpl->Execute(std::move(request), std::move(target), observer);
}


/**
 * @brief Cancel a pending or active request by its numeric identifier.
 *
 * @param identifier  The int32 ID returned by BHttpResult::Identity().
 */
void
BHttpSession::Cancel(int32 identifier)
{
	fImpl->Cancel(identifier);
}


/**
 * @brief Cancel the request associated with a BHttpResult future.
 *
 * @param request  The BHttpResult whose underlying request should be cancelled.
 */
void
BHttpSession::Cancel(const BHttpResult& request)
{
	fImpl->Cancel(request.Identity());
}


/**
 * @brief Set the maximum number of simultaneous connections per remote host.
 *
 * @param maxConnections  New per-host limit.
 */
void
BHttpSession::SetMaxConnectionsPerHost(size_t maxConnections)
{
	fImpl->SetMaxConnectionsPerHost(maxConnections);
}


/**
 * @brief Set the maximum number of remote hosts with simultaneous connections.
 *
 * @param maxConnections  New host count limit.
 */
void
BHttpSession::SetMaxHosts(size_t maxConnections)
{
	fImpl->SetMaxHosts(maxConnections);
}


// #pragma mark -- BHttpSession::Request (helpers)

/**
 * @brief Construct a new Request wrapping a BHttpRequest, body target, and observer.
 *
 * Assigns a unique identifier, creates the shared HttpResultPrivate, and
 * notifies the parser if the method is HEAD (no content expected).
 *
 * @param request   The BHttpRequest to execute (moved).
 * @param target    BBorrow<BDataIO> for the response body.
 * @param observer  BMessenger for event notifications.
 */
BHttpSession::Request::Request(BHttpRequest&& request, BBorrow<BDataIO> target, BMessenger observer)
	:
	fRequest(std::move(request)),
	fObserver(observer)
{
	auto identifier = get_netservices_request_identifier();

	// interpret the remaining redirects
	fRemainingRedirects = fRequest.MaxRedirections();

	// create shared data
	fResult = std::make_shared<HttpResultPrivate>(identifier);

	// check if there is a target
	if (target.HasValue())
		fResult->bodyTarget = std::move(target);

	// inform the parser when we do a HEAD request, so not to expect content
	if (fRequest.Method() == BHttpMethod::Head)
		fParser.SetNoContent();
}


/**
 * @brief Construct a redirect Request by copying state from \a original with a new URL.
 *
 * Applies the redirect URL, optionally converts the method to GET, and
 * decrements the remaining redirect counter.
 *
 * @param original  The Request being redirected.
 * @param redirect  Struct containing the new URL and whether to convert to GET.
 */
BHttpSession::Request::Request(Request& original, const BHttpSession::Redirect& redirect)
	:
	fRequest(std::move(original.fRequest)),
	fObserver(original.fObserver),
	fResult(original.fResult)
{
	// update the original request with the new location
	fRequest.SetUrl(redirect.url);

	if (redirect.redirectToGet
		&& (fRequest.Method() != BHttpMethod::Head && fRequest.Method() != BHttpMethod::Get)) {
		fRequest.SetMethod(BHttpMethod::Get);
		fRequest.ClearRequestBody();
	}

	fRemainingRedirects = original.fRemainingRedirects--;

	// inform the parser when we do a HEAD request, so not to expect content
	if (fRequest.Method() == BHttpMethod::Head)
		fParser.SetNoContent();
}


/**
 * @brief Store the error in the result and send error notifications to the observer.
 *
 * @param e  Exception pointer to store and report.
 */
void
BHttpSession::Request::SetError(std::exception_ptr e)
{
	fResult->SetError(e);
	SendMessage(UrlEvent::DebugMessage, [&e](BMessage& msg) {
		msg.AddUInt32(UrlEventData::DebugType, UrlEventData::DebugError);
		try {
			std::rethrow_exception(e);
		} catch (BError& error) {
			msg.AddString(UrlEventData::DebugMessage, error.DebugMessage());
		} catch (std::exception& error) {
			msg.AddString(UrlEventData::DebugMessage, error.what());
		} catch (...) {
			msg.AddString(UrlEventData::DebugMessage, "Unknown exception");
		}
	});
	SendMessage(UrlEvent::RequestCompleted,
		[](BMessage& msg) { msg.AddBool(UrlEventData::Success, false); });
}


/**
 * @brief Return the (hostname, port) pair for this request's target host.
 *
 * @return std::pair<BString, int> of hostname and port number.
 */
std::pair<BString, int>
BHttpSession::Request::GetHost() const
{
	return {fRequest.Url().Host(), fRequest.Url().Port()};
}


/**
 * @brief Associate a connection counter with this request.
 *
 * The counter is decremented via CounterDeleter when the request is destroyed.
 *
 * @param counter  Pointer to the per-host connection count to decrement on destruction.
 */
void
BHttpSession::Request::SetCounter(int32* counter) noexcept
{
	fConnectionCounter = std::unique_ptr<int32, CounterDeleter>(counter);
}


/**
 * @brief Resolve the target hostname to a BNetworkAddress.
 *
 * Determines the port from the URL or defaults to 80 (http) or 443 (https),
 * then performs a synchronous DNS lookup.
 */
void
BHttpSession::Request::ResolveHostName()
{
	int port;
	if (fRequest.Url().HasPort())
		port = fRequest.Url().Port();
	else if (fRequest.Url().Protocol() == "https")
		port = 443;
	else
		port = 80;

	// TODO: proxy
	if (auto status = fRemoteAddress.SetTo(fRequest.Url().Host(), port); status != B_OK) {
		throw BNetworkRequestError(
			"BNetworkAddress::SetTo()", BNetworkRequestError::HostnameError, status);
	}

	SendMessage(UrlEvent::HostNameResolved,
		[this](BMessage& msg) { msg.AddString(UrlEventData::HostName, fRequest.Url().Host()); });
}


/**
 * @brief Open the TCP/TLS connection and switch the socket to non-blocking mode.
 *
 * Creates either a BSecureSocket (https) or BSocket (http), applies the
 * configured timeout, connects, and sets O_NONBLOCK via fcntl.
 */
void
BHttpSession::Request::OpenConnection()
{
	// Set up the socket
	if (fRequest.Url().Protocol() == "https") {
		// To do: secure socket with callbacks to check certificates
		fSocket = std::make_unique<BSecureSocket>();
	} else {
		fSocket = std::make_unique<BSocket>();
	}

	// Set timeout
	fSocket->SetTimeout(fRequest.Timeout());

	// Open connection
	if (auto status = fSocket->Connect(fRemoteAddress); status != B_OK) {
		// TODO: inform listeners that the connection failed
		throw BNetworkRequestError(
			"BSocket::Connect()", BNetworkRequestError::NetworkError, status);
	}

	// Make the rest of the interaction non-blocking
	auto flags = fcntl(fSocket->Socket(), F_GETFL, 0);
	if (flags == -1)
		throw BRuntimeError("fcntl()", "Error getting socket flags");
	if (fcntl(fSocket->Socket(), F_SETFL, flags | O_NONBLOCK) != 0)
		throw BRuntimeError("fcntl()", "Error setting non-blocking flag on socket");

	SendMessage(UrlEvent::ConnectionOpened);

	fRequestStatus = Connected;
}


/**
 * @brief Send the serialised HTTP request over the socket.
 *
 * Lazily initialises the HttpSerializer on the first call and drives the
 * serialisation loop until either the socket would block or all data is sent.
 * Sends upload-progress notifications and advances the state to RequestSent.
 */
void
BHttpSession::Request::TransferRequest()
{
	// Assert that we are in the right state
	if (fRequestStatus != Connected)
		throw BRuntimeError(
			__PRETTY_FUNCTION__, "Write request for object that is not in the Connected state");

	if (!fSerializer.IsInitialized())
		fSerializer.SetTo(fBuffer, fRequest);

	auto currentBytesWritten = fSerializer.Serialize(fBuffer, fSocket.get());

	if (currentBytesWritten > 0) {
		SendMessage(UrlEvent::UploadProgress, [this](BMessage& msg) {
			msg.AddInt64(UrlEventData::NumBytes, fSerializer.BodyBytesTransferred());
			if (auto totalSize = fSerializer.BodyBytesTotal())
				msg.AddInt64(UrlEventData::TotalBytes, totalSize.value());
		});
	}

	if (fSerializer.Complete())
		fRequestStatus = RequestSent;
}


/**
 * @brief Read and parse the next chunk of the HTTP response.
 *
 * Reads from the socket into the buffer, then drives the HttpParser through
 * the status, fields, and body states.  Handles redirects by throwing a
 * Redirect struct to be caught by the data thread.  Sends download-progress
 * and completion notifications.
 *
 * @return true if the response is fully received and processing is complete.
 */
bool
BHttpSession::Request::ReceiveResult()
{
	// First: stream data from the socket
	auto bytesRead = fBuffer.ReadFrom(fSocket.get());

	if (bytesRead == B_WOULD_BLOCK || bytesRead == B_INTERRUPTED)
		return false;

	auto readEnd = bytesRead == 0;

	// Parse the content in the buffer
	switch (fParser.State()) {
		case HttpInputStreamState::StatusLine:
		{
			if (fBuffer.RemainingBytes() == static_cast<size_t>(bytesRead)) {
				// In the initial run, the bytes in the buffer will match the bytes read to indicate
				// the response has started.
				SendMessage(UrlEvent::ResponseStarted);
			}

			if (fParser.ParseStatus(fBuffer, fStatus)) {
				// the status headers are now received, decide what to do next

				// Determine if we can handle redirects; else notify of receiving status
				if (fRemainingRedirects > 0) {
					switch (fStatus.StatusCode()) {
						case BHttpStatusCode::MovedPermanently:
						case BHttpStatusCode::TemporaryRedirect:
						case BHttpStatusCode::PermanentRedirect:
							// These redirects require the request body to be sent again. It this is
							// possible, BHttpRequest::RewindBody() will return true in which case
							// we can handle the redirect.
							if (!fRequest.RewindBody())
								break;
							[[fallthrough]];
						case BHttpStatusCode::Found:
						case BHttpStatusCode::SeeOther:
							// These redirects redirect to GET, so we don't care if we can rewind
							// the body; in this case redirect
							fMightRedirect = true;
							break;
						default:
							break;
					}
				}

				if ((fStatus.StatusClass() == BHttpStatusClass::ClientError
						|| fStatus.StatusClass() == BHttpStatusClass::ServerError)
					&& fRequest.StopOnError()) {
					fRequestStatus = ContentReceived;
					fResult->SetStatus(std::move(fStatus));
					fResult->SetFields(BHttpFields());
					fResult->SetBody();
					SendMessage(UrlEvent::RequestCompleted,
						[](BMessage& msg) { msg.AddBool(UrlEventData::Success, true); });
					return true;
				}

				if (!fMightRedirect) {
					// we are not redirecting and there is no error, so inform listeners
					SendMessage(UrlEvent::HttpStatus, [this](BMessage& msg) {
						msg.AddInt16(UrlEventData::HttpStatusCode, fStatus.code);
					});
					fResult->SetStatus(BHttpStatus{fStatus.code, std::move(fStatus.text)});
				}
			} else {
				// We do not have enough data for the status line yet
				if (readEnd) {
					throw BNetworkRequestError(__PRETTY_FUNCTION__,
						BNetworkRequestError::ProtocolError,
						"Response did not include a complete status line");
				}
				return false;
			}
			[[fallthrough]];
		}
		case HttpInputStreamState::Fields:
		{
			if (!fParser.ParseFields(fBuffer, fFields)) {
				// there may be more headers to receive, throw an error if there will be no more
				if (readEnd) {
					throw BNetworkRequestError(__PRETTY_FUNCTION__,
						BNetworkRequestError::ProtocolError,
						"Response did not include a complete header section");
				}
				break;
			}

			// The headers have been received, now set up the rest of the response handling

			// Handle redirects
			if (fMightRedirect) {
				auto redirectToGet = false;
				switch (fStatus.StatusCode()) {
					case BHttpStatusCode::Found:
					case BHttpStatusCode::SeeOther:
						// 302 and 303 redirections convert all requests to GET request, except for
						// HEAD
						redirectToGet = true;
						[[fallthrough]];
					case BHttpStatusCode::MovedPermanently:
					case BHttpStatusCode::TemporaryRedirect:
					case BHttpStatusCode::PermanentRedirect:
					{
						auto locationField = fFields.FindField("Location");
						if (locationField == fFields.end()) {
							throw BNetworkRequestError(__PRETTY_FUNCTION__,
								BNetworkRequestError::ProtocolError,
								"Redirect; the Location field must be present and cannot be found");
						}
						auto locationString = BString(
							(*locationField).Value().data(), (*locationField).Value().size());
						auto redirect = BHttpSession::Redirect{
							BUrl(fRequest.Url(), locationString), redirectToGet};
						if (!redirect.url.IsValid()) {
							throw BNetworkRequestError(__PRETTY_FUNCTION__,
								BNetworkRequestError::ProtocolError,
								"Redirect; invalid URL in the Location field");
						}

						// Notify of redirect
						SendMessage(UrlEvent::HttpRedirect, [&locationString](BMessage& msg) {
							msg.AddString(UrlEventData::HttpRedirectUrl, locationString);
						});
						throw redirect;
					}
					default:
						// ignore other status codes and continue regular processing
						SendMessage(UrlEvent::HttpStatus, [this](BMessage& msg) {
							msg.AddInt16(UrlEventData::HttpStatusCode, fStatus.code);
						});
						fResult->SetStatus(BHttpStatus{fStatus.code, std::move(fStatus.text)});
						break;
				}
			}

			// TODO: Parse received cookies

			// Move headers to the result and inform listener
			fResult->SetFields(std::move(fFields));
			SendMessage(UrlEvent::HttpFields);

			if (!fParser.HasContent()) {
				// Any requests with not content are finished
				fResult->SetBody();
				SendMessage(UrlEvent::RequestCompleted,
					[](BMessage& msg) { msg.AddBool(UrlEventData::Success, true); });
				fRequestStatus = ContentReceived;
				return true;
			}
			[[fallthrough]];
		}
		case HttpInputStreamState::Body:
		{
			size_t bytesWrittenToBody;
				// The bytesWrittenToBody may differ from the bytes parsed from the buffer when
				// there is compression on the incoming stream.
			bytesRead = fParser.ParseBody(
				fBuffer,
				[this, &bytesWrittenToBody](const std::byte* buffer, size_t size) {
					bytesWrittenToBody = fResult->WriteToBody(buffer, size);
					return bytesWrittenToBody;
				},
				readEnd);

			SendMessage(UrlEvent::DownloadProgress, [this, bytesRead](BMessage& msg) {
				msg.AddInt64(UrlEventData::NumBytes, bytesRead);
				if (fParser.BodyBytesTotal())
					msg.AddInt64(UrlEventData::TotalBytes, fParser.BodyBytesTotal().value());
			});

			if (bytesWrittenToBody > 0) {
				SendMessage(UrlEvent::BytesWritten, [bytesWrittenToBody](BMessage& msg) {
					msg.AddInt64(UrlEventData::NumBytes, bytesWrittenToBody);
				});
			}

			if (fParser.Complete()) {
				fResult->SetBody();
				SendMessage(UrlEvent::RequestCompleted,
					[](BMessage& msg) { msg.AddBool(UrlEventData::Success, true); });
				fRequestStatus = ContentReceived;
				return true;
			} else if (readEnd) {
				// the parsing of the body is not complete but we are at the end of the data
				throw BNetworkRequestError(__PRETTY_FUNCTION__, BNetworkRequestError::ProtocolError,
					"Unexpected end of data: more data was expected");
			}

			break;
		}
		default:
			throw BRuntimeError(__PRETTY_FUNCTION__, "Not reachable");
	}

	// There is more to receive
	return false;
}


/**
 * @brief Disconnect the socket, ignoring any errors.
 */
void
BHttpSession::Request::Disconnect() noexcept
{
	fSocket->Disconnect();
}


/**
 * @brief Send a BMessage notification to the observer, if one is registered.
 *
 * Always adds the request identifier field before calling the optional
 * \a dataFunc so callers can identify which request the event relates to.
 *
 * @param what      Message what code (UrlEvent::* constant).
 * @param dataFunc  Optional function that populates additional message fields.
 */
void
BHttpSession::Request::SendMessage(uint32 what, std::function<void(BMessage&)> dataFunc) const
{
	if (fObserver.IsValid()) {
		BMessage msg(what);
		msg.AddInt32(UrlEventData::Id, fResult->id);
		if (dataFunc)
			dataFunc(msg);
		fObserver.SendMessage(&msg);
	}
}


// #pragma mark -- Message constants


namespace BPrivate::Network::UrlEventData {
/** @brief BMessage field name for the HTTP status code in UrlEvent::HttpStatus messages. */
const char* HttpStatusCode = "url:httpstatuscode";

/** @brief BMessage field name for the SSL certificate in TLS error events. */
const char* SSLCertificate = "url:sslcertificate";

/** @brief BMessage field name for the SSL error description string. */
const char* SSLMessage = "url:sslmessage";

/** @brief BMessage field name for the redirect target URL string. */
const char* HttpRedirectUrl = "url:httpredirecturl";
} // namespace BPrivate::Network::UrlEventData
