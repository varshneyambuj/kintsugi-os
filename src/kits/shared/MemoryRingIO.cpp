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
 *   Copyright 2022 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Leorize, leorize+oss@disroot.org
 */

/** @file MemoryRingIO.cpp
 *  @brief Implementation of BMemoryRingIO, a thread-safe in-memory ring
 *         (circular) buffer that implements the BDataIO interface.
 *
 *  BMemoryRingIO provides blocking read/write semantics backed by a
 *  power-of-two-sized ring buffer, coordinated via POSIX mutex and
 *  condition variable primitives.
 */

#include <MemoryRingIO.h>

#include <AutoLocker.h>

#include <algorithm>

#include <stdlib.h>
#include <string.h>


/** @brief POSIX mutex locking policy for use with AutoLocker.
 *
 *  Wraps pthread_mutex_lock / pthread_mutex_unlock so that AutoLocker can
 *  manage a pthread_mutex_t automatically.
 */
class PThreadLocking {
public:
	/** @brief Acquires the mutex.
	 *  @param mutex  The POSIX mutex to lock.
	 *  @return true if locking succeeded, false otherwise.
	 */
	inline bool Lock(pthread_mutex_t* mutex)
	{
		return pthread_mutex_lock(mutex) == 0;
	}

	/** @brief Releases the mutex.
	 *  @param mutex  The POSIX mutex to unlock.
	 */
	inline void Unlock(pthread_mutex_t* mutex)
	{
		pthread_mutex_unlock(mutex);
	}
};


/** @brief Convenience typedef for an AutoLocker over a pthread_mutex_t. */
typedef AutoLocker<pthread_mutex_t, PThreadLocking> PThreadAutoLocker;


/** @brief Condition functor that is satisfied when data is available to read.
 *
 *  Used with _WaitForCondition<ReadCondition> to block until at least one
 *  byte can be consumed from the ring buffer.
 */
struct ReadCondition {
	/** @brief Returns true when the ring buffer has bytes available.
	 *  @param ring  The ring buffer to query.
	 *  @return true if BytesAvailable() > 0.
	 */
	inline bool operator()(BMemoryRingIO &ring) {
		return ring.BytesAvailable() != 0;
	}
};


/** @brief Condition functor that is satisfied when space is available to write.
 *
 *  Used with _WaitForCondition<WriteCondition> to block until at least one
 *  byte of free space exists in the ring buffer.
 */
struct WriteCondition {
	/** @brief Returns true when the ring buffer has space available.
	 *  @param ring  The ring buffer to query.
	 *  @return true if SpaceAvailable() > 0.
	 */
	inline bool operator()(BMemoryRingIO &ring) {
		return ring.SpaceAvailable() != 0;
	}
};


/** @brief Wraps an index into the ring buffer using a power-of-two mask. */
#define RING_MASK(x) ((x) & (fBufferSize - 1))


/** @brief Rounds @p value up to the next power of two.
 *
 *  Uses a bit-spreading technique to fill all lower bits then adds one.
 *  Returns 0 when @p value is 0.
 *
 *  @param value  The value to round up.
 *  @return The smallest power of two >= @p value.
 */
static size_t
next_power_of_two(size_t value)
{
	value--;
	value |= value >> 1;
	value |= value >> 2;
	value |= value >> 4;
	value |= value >> 8;
	value |= value >> 16;
#if SIZE_MAX >= UINT64_MAX
	value |= value >> 32;
#endif
	value++;

	return value;
}


/** @brief Constructs a BMemoryRingIO with an initial buffer capacity.
 *
 *  The actual allocation size is rounded up to the next power of two.
 *  Initializes the POSIX mutex and condition variable used for thread
 *  synchronization.
 *
 *  @param size  Requested initial capacity in bytes.
 */
BMemoryRingIO::BMemoryRingIO(size_t size)
	:
	fBuffer(NULL),
	fBufferSize(0),
	fWriteAtNext(0),
	fReadAtNext(0),
	fBufferFull(false),
	fWriteDisabled(false)
{
	// We avoid the use of pthread_mutexattr as it can possibly fail.
	//
	// The only Haiku-specific behavior that we depend on is that
	// PTHREAD_MUTEX_DEFAULT mutexes check for double-locks.
	pthread_mutex_init(&fLock, NULL);
	pthread_cond_init(&fEvent, NULL);

	SetSize(size);
}


/** @brief Destructor. Releases the ring buffer memory and destroys the
 *         POSIX synchronization primitives.
 */
BMemoryRingIO::~BMemoryRingIO()
{
	SetSize(0);

	pthread_mutex_destroy(&fLock);
	pthread_cond_destroy(&fEvent);
}


/** @brief Returns whether the object was successfully initialized.
 *
 *  @return B_OK if the buffer is allocated and ready, or B_NO_INIT if
 *          the buffer size is zero (allocation failed or not yet set).
 */
status_t
BMemoryRingIO::InitCheck() const
{
	if (fBufferSize == 0)
		return B_NO_INIT;

	return B_OK;
}


/** @brief Reads up to @p size bytes from the ring buffer into @p _buffer.
 *
 *  Blocks until data is available unless write has been disabled, in which
 *  case it returns immediately with however many bytes are available
 *  (possibly 0).  Signals writers after consuming data.
 *
 *  @param _buffer  Destination buffer. Must not be NULL.
 *  @param size     Maximum number of bytes to read.
 *  @return The number of bytes actually read (>= 0), or B_BAD_VALUE if
 *          @p _buffer is NULL.
 */
ssize_t
BMemoryRingIO::Read(void* _buffer, size_t size)
{
	if (_buffer == NULL)
		return B_BAD_VALUE;
	if (size == 0)
		return 0;

	PThreadAutoLocker _(fLock);

	if (!fWriteDisabled)
		WaitForRead();

	size = std::min(size, BytesAvailable());
	uint8* buffer = reinterpret_cast<uint8*>(_buffer);
	if (fReadAtNext + size < fBufferSize)
		memcpy(buffer, fBuffer + fReadAtNext, size);
	else {
		const size_t upper = fBufferSize - fReadAtNext;
		const size_t lower = size - upper;
		memcpy(buffer, fBuffer + fReadAtNext, upper);
		memcpy(buffer + upper, fBuffer, lower);
	}
	fReadAtNext = RING_MASK(fReadAtNext + size);
	fBufferFull = false;

	pthread_cond_signal(&fEvent);

	return size;
}


/** @brief Writes up to @p size bytes from @p _buffer into the ring buffer.
 *
 *  Blocks until space is available unless write has been disabled.
 *  Signals readers after producing data.
 *
 *  @param _buffer  Source buffer. Must not be NULL.
 *  @param size     Number of bytes to write.
 *  @return The number of bytes actually written (>= 0), B_BAD_VALUE if
 *          @p _buffer is NULL, or B_READ_ONLY_DEVICE if writing is
 *          disabled.
 */
ssize_t
BMemoryRingIO::Write(const void* _buffer, size_t size)
{
	if (_buffer == NULL)
		return B_BAD_VALUE;
	if (size == 0)
		return 0;

	PThreadAutoLocker locker(fLock);

	if (!fWriteDisabled)
		WaitForWrite();

	// We separate this check from WaitForWrite() as the boolean
	// might have been toggled during our wait on the conditional.
	if (fWriteDisabled)
		return B_READ_ONLY_DEVICE;

	const uint8* buffer = reinterpret_cast<const uint8*>(_buffer);
	size = std::min(size, SpaceAvailable());
	if (fWriteAtNext + size < fBufferSize)
		memcpy(fBuffer + fWriteAtNext, buffer, size);
	else {
		const size_t upper = fBufferSize - fWriteAtNext;
		const size_t lower = size - upper;
		memcpy(fBuffer + fWriteAtNext, buffer, size);
		memcpy(fBuffer, buffer + upper, lower);
	}
	fWriteAtNext = RING_MASK(fWriteAtNext + size);
	fBufferFull = fReadAtNext == fWriteAtNext;

	pthread_cond_signal(&fEvent);

	return size;
}


/** @brief Resizes the ring buffer to at least @p _size bytes.
 *
 *  The new capacity is rounded up to the next power of two.  Any data
 *  currently in the buffer is preserved.  The new size must be large
 *  enough to hold all bytes currently available for reading.
 *
 *  @param _size  Requested new capacity in bytes. Pass 0 to free the
 *                buffer entirely.
 *  @return B_OK on success, B_BAD_VALUE if @p _size is smaller than the
 *          number of bytes currently buffered, or B_NO_MEMORY if allocation
 *          fails.
 */
status_t
BMemoryRingIO::SetSize(size_t _size)
{
	PThreadAutoLocker locker(fLock);

	const size_t size = next_power_of_two(_size);

	const size_t availableBytes = BytesAvailable();
	if (size < availableBytes)
		return B_BAD_VALUE;

	if (size == 0) {
		free(fBuffer);
		fBuffer = NULL;
		fBufferSize = 0;
		Clear(); // resets other internal counters
		return B_OK;
	}

	uint8* newBuffer = reinterpret_cast<uint8*>(malloc(size));
	if (newBuffer == NULL)
		return B_NO_MEMORY;

	Read(newBuffer, availableBytes);
	free(fBuffer);

	fBuffer = newBuffer;
	fBufferSize = size;
	fReadAtNext = 0;
	fWriteAtNext = RING_MASK(availableBytes);
	fBufferFull = fBufferSize == availableBytes;

	pthread_cond_signal(&fEvent);

	return B_OK;
}


/** @brief Discards all data currently held in the ring buffer.
 *
 *  Resets the read and write positions and the full flag. Does not
 *  deallocate the backing buffer.
 */
void
BMemoryRingIO::Clear()
{
	PThreadAutoLocker locker(fLock);

	fReadAtNext = 0;
	fWriteAtNext = 0;
	fBufferFull = false;
}


/** @brief Returns the number of bytes available for reading.
 *
 *  @return Number of bytes that can be read without blocking.
 */
size_t
BMemoryRingIO::BytesAvailable()
{
	PThreadAutoLocker locker(fLock);

	if (fWriteAtNext == fReadAtNext) {
		if (fBufferFull)
			return fBufferSize;
		return 0;
	}
	return RING_MASK(fWriteAtNext - fReadAtNext);
}


/** @brief Returns the number of bytes of free space available for writing.
 *
 *  @return Number of bytes that can be written without blocking.
 */
size_t
BMemoryRingIO::SpaceAvailable()
{
	PThreadAutoLocker locker(fLock);

	return fBufferSize - BytesAvailable();
}


/** @brief Returns the total allocated capacity of the ring buffer.
 *
 *  @return The buffer size in bytes (always a power of two, or 0 if
 *          uninitialized).
 */
size_t
BMemoryRingIO::BufferSize()
{
	PThreadAutoLocker locker(fLock);

	return fBufferSize;
}


/** @brief Internal helper that waits for a condition to become true,
 *         with optional timeout support.
 *
 *  Blocks the calling thread on fEvent until the templated Condition
 *  functor returns true, or until the timeout elapses, or until an error
 *  occurs.
 *
 *  @tparam Condition  A functor type with operator()(BMemoryRingIO&) -> bool.
 *  @param timeout     How long to wait in microseconds.
 *                     Pass B_INFINITE_TIMEOUT to wait indefinitely.
 *  @return B_OK when the condition becomes true, B_TIMED_OUT on timeout,
 *          B_READ_ONLY_DEVICE if writing was disabled during the wait,
 *          or a POSIX error code on unexpected failure.
 */
template<typename Condition>
status_t
BMemoryRingIO::_WaitForCondition(bigtime_t timeout)
{
	PThreadAutoLocker autoLocker;

	struct timespec absTimeout;
	if (timeout == B_INFINITE_TIMEOUT) {
		autoLocker.SetTo(fLock, false);
	} else {
		memset(&absTimeout, 0, sizeof(absTimeout));
		bigtime_t target = system_time() + timeout;
		absTimeout.tv_sec = target / 100000;
		absTimeout.tv_nsec = (target % 100000) * 1000L;
		int err = pthread_mutex_timedlock(&fLock, &absTimeout);
		if (err == ETIMEDOUT)
			return B_TIMED_OUT;
		if (err != EDEADLK)
			autoLocker.SetTo(fLock, true);
	}

	Condition cond;
	while (!cond(*this)) {
		if (fWriteDisabled)
			return B_READ_ONLY_DEVICE;

		int err = 0;

		if (timeout == B_INFINITE_TIMEOUT)
			err = pthread_cond_wait(&fEvent, &fLock);
		else
			err = pthread_cond_timedwait(&fEvent, &fLock, &absTimeout);

		if (err != 0)
			return err;
	}

	return B_OK;
}


/** @brief Blocks until at least one byte is available for reading.
 *
 *  @param timeout  Maximum wait time in microseconds.
 *                  Use B_INFINITE_TIMEOUT (default) to wait indefinitely.
 *  @return B_OK when data is available, B_TIMED_OUT on timeout, or another
 *          error code on failure.
 */
status_t
BMemoryRingIO::WaitForRead(bigtime_t timeout)
{
	return _WaitForCondition<ReadCondition>(timeout);
}


/** @brief Blocks until at least one byte of free space is available for
 *         writing.
 *
 *  @param timeout  Maximum wait time in microseconds.
 *                  Use B_INFINITE_TIMEOUT (default) to wait indefinitely.
 *  @return B_OK when space is available, B_TIMED_OUT on timeout,
 *          B_READ_ONLY_DEVICE if writing is disabled, or another error
 *          code on failure.
 */
status_t
BMemoryRingIO::WaitForWrite(bigtime_t timeout)
{
	return _WaitForCondition<WriteCondition>(timeout);
}


/** @brief Enables or disables the write side of the ring buffer.
 *
 *  When disabled, Write() returns B_READ_ONLY_DEVICE and any threads
 *  blocked in WaitForWrite() are woken up.
 *
 *  @param disabled  Pass true to disable writing; false to re-enable it.
 */
void
BMemoryRingIO::SetWriteDisabled(bool disabled)
{
	PThreadAutoLocker autoLocker(fLock);

	fWriteDisabled = disabled;

	pthread_cond_broadcast(&fEvent);
}


/** @brief Returns whether the write side of the ring buffer is disabled.
 *
 *  @return true if writing has been disabled via SetWriteDisabled(true).
 */
bool
BMemoryRingIO::WriteDisabled()
{
	PThreadAutoLocker autoLocker(fLock);

	return fWriteDisabled;
}
