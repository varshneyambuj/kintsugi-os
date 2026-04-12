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
 *   Copyright 2016 Dario Casalinuovo. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file AdapterIO.cpp
 * @brief Implementation of BAdapterIO and BInputAdapter for streaming media I/O.
 *
 * BAdapterIO provides a BPositionIO-compatible interface that wraps an internal
 * ring-style buffer (RelativePositionIO) so that media decoders can read from
 * a continuously-growing or seekable stream. BInputAdapter is the write-side
 * companion that pushes incoming data into the backing store via BackWrite().
 *
 * @see BMediaIO, BPositionIO
 */


#include "AdapterIO.h"

#include <MediaIO.h>

#include <string.h>

#include "MediaDebug.h"


#define TIMEOUT_QUANTA 100000


/**
 * @brief Internal BPositionIO wrapper that translates absolute stream positions
 *        to relative offsets within the backing buffer.
 *
 * RelativePositionIO maintains a start-offset so that flushed data regions
 * can be dropped from the buffer without affecting the logical position
 * seen by the consumer. All public methods are thread-safe via an RWLocker.
 */
class RelativePositionIO : public BPositionIO {
public:
	/**
	 * @brief Constructs a RelativePositionIO with an initial backing buffer.
	 *
	 * @param owner    The owning BAdapterIO used to query stream flags.
	 * @param buffer   The initial backing BPositionIO (takes ownership).
	 * @param timeout  Maximum time in microseconds to wait for data before
	 *                 returning B_TIMED_OUT, or B_INFINITE_TIMEOUT.
	 */
	RelativePositionIO(BAdapterIO* owner, BPositionIO* buffer,
		bigtime_t timeout)
		:
		BPositionIO(),
		fOwner(owner),
		fBackPosition(0),
		fStartOffset(0),
		fBuffer(buffer),
		fTimeout(timeout)
	{
	}

	/**
	 * @brief Destroys the RelativePositionIO and frees the backing buffer.
	 */
	virtual	~RelativePositionIO()
	{
		delete fBuffer;
	}

	/**
	 * @brief Truncates the backing buffer and resets the start-offset anchor.
	 *
	 * @param offset  The new absolute stream position that maps to offset 0
	 *                inside the backing buffer.
	 * @return B_OK on success, or an error code if SetSize fails.
	 */
	status_t ResetStartOffset(off_t offset)
	{
		status_t ret = fBuffer->SetSize(0);
		if (ret != B_OK)
			return ret;

		fBackPosition = 0;
		fStartOffset = offset;
		return B_OK;
	}

	/**
	 * @brief Flushes data before a given position into a new backing buffer.
	 *
	 * Copies the portion of oldBuffer that lies at or after \a position into
	 * \a buffer, then replaces the current backing store with \a buffer.
	 *
	 * @param position   Absolute stream position; data before this is discarded.
	 * @param buffer     New BPositionIO that receives the retained data.
	 * @param oldBuffer  Raw pointer to the old buffer's memory region.
	 * @param oldLength  Byte length of the old buffer's memory region.
	 * @return B_OK on success, B_BAD_VALUE if position is beyond oldLength,
	 *         or another error code on I/O failure.
	 */
	status_t FlushBefore(off_t position, BPositionIO* buffer, const void* oldBuffer,
		size_t oldLength)
	{
		AutoWriteLocker _(fLock);
		off_t relative = _PositionToRelative(position);
		if (relative < 0)
			return B_OK;
		if (relative > (off_t)oldLength)
			return B_BAD_VALUE;
		status_t status = buffer->WriteAt(0, (void*)((addr_t)oldBuffer + relative),
			oldLength - relative);
		if (status < B_OK)
			return status;
		status = buffer->Seek(fBuffer->Position() - relative, SEEK_SET);
		if (status < B_OK)
			return status;
		fBackPosition -= relative;
		fStartOffset += relative;
		SetBuffer(buffer);
		return B_OK;
	}

	/**
	 * @brief Validates whether a requested stream position is currently available.
	 *
	 * @param position   Absolute stream position to evaluate.
	 * @param totalSize  Total known stream size, or 0 if unknown.
	 * @return B_OK if the position is readable, B_RESOURCE_UNAVAILABLE if it
	 *         precedes the current start offset, B_WOULD_BLOCK if the stream is
	 *         mutable and the position is beyond totalSize, or B_ERROR otherwise.
	 */
	status_t EvaluatePosition(off_t position, off_t totalSize)
	{
		if (position < 0)
			return B_ERROR;

		if (position < fStartOffset)
			return B_RESOURCE_UNAVAILABLE;

		if (totalSize > 0 && position > totalSize) {
			// This is an endless stream, we don't know
			// how much data will come and when, we could
			// block on that.
			if (IsMutable())
				return B_WOULD_BLOCK;
			else
				return B_ERROR;
		}

		return B_OK;
	}

	/**
	 * @brief Blocks until the buffer contains at least position + size bytes.
	 *
	 * Polls the buffer size at TIMEOUT_QUANTA intervals until the requested
	 * amount of data is available or the cumulative wait exceeds fTimeout.
	 *
	 * @param position  Starting absolute stream position.
	 * @param size      Number of bytes required past \a position.
	 * @return B_OK when data is available, B_NOT_SUPPORTED if the owner is no
	 *         longer running, or B_TIMED_OUT if the deadline is exceeded.
	 */
	status_t WaitForData(off_t position, off_t size)
	{
		off_t bufferSize = 0;
		status_t ret = GetSize(&bufferSize);
		if (ret != B_OK)
			return B_ERROR;

		bigtime_t totalTimeOut = 0;

		while (bufferSize < position + size) {
			// We are not running, no luck to receive
			// more data, let's return and avoid locking.
			if (!fOwner->IsRunning())
				return B_NOT_SUPPORTED;

			if (fTimeout != B_INFINITE_TIMEOUT && totalTimeOut >= fTimeout)
				return B_TIMED_OUT;

			snooze(TIMEOUT_QUANTA);

			totalTimeOut += TIMEOUT_QUANTA;
			GetSize(&bufferSize);
		}
		return B_OK;
	}

	/**
	 * @brief Reads bytes from a translated relative position in the backing buffer.
	 *
	 * @param position  Absolute stream position to read from.
	 * @param buffer    Destination buffer.
	 * @param size      Number of bytes to read.
	 * @return Number of bytes read, or a negative error code.
	 */
	virtual	ssize_t	ReadAt(off_t position, void* buffer,
		size_t size)
	{
		AutoReadLocker _(fLock);

		return fBuffer->ReadAt(
			_PositionToRelative(position), buffer, size);

	}

	/**
	 * @brief Writes bytes to a translated relative position in the backing buffer.
	 *
	 * @param position  Absolute stream position to write to.
	 * @param buffer    Source data.
	 * @param size      Number of bytes to write.
	 * @return Number of bytes written, or a negative error code.
	 */
	virtual	ssize_t	WriteAt(off_t position,
		const void* buffer, size_t size)
	{
		AutoWriteLocker _(fLock);

		return fBuffer->WriteAt(
			_PositionToRelative(position), buffer, size);
	}

	/**
	 * @brief Repositions the stream cursor with start-offset translation.
	 *
	 * SEEK_SET positions are translated to relative form; SEEK_CUR and
	 * SEEK_END are forwarded unchanged to the backing buffer.
	 *
	 * @param position  Offset operand for the seek.
	 * @param seekMode  One of SEEK_SET, SEEK_CUR, or SEEK_END.
	 * @return The new absolute stream position, or a negative error code.
	 */
	virtual	off_t Seek(off_t position, uint32 seekMode)
	{
		AutoWriteLocker _(fLock);

		if (seekMode == SEEK_SET)
			return fBuffer->Seek(_PositionToRelative(position), seekMode);
		return fBuffer->Seek(position, seekMode);
	}

	/**
	 * @brief Returns the current absolute stream position.
	 *
	 * @return The current position in absolute stream coordinates.
	 */
	virtual off_t Position() const
	{
		AutoReadLocker _(fLock);

		return _RelativeToPosition(fBuffer->Position());
	}

	/**
	 * @brief Sets the logical size of the stream by translating to a relative size.
	 *
	 * @param size  New absolute stream size.
	 * @return B_OK on success, or an error code from the backing buffer.
	 */
	virtual	status_t SetSize(off_t size)
	{
		AutoWriteLocker _(fLock);

		return fBuffer->SetSize(_PositionToRelative(size));
	}

	/**
	 * @brief Returns the apparent stream size based on the back-write cursor.
	 *
	 * The reported size is derived from fBackPosition rather than the backing
	 * buffer's own size, keeping it independent of seek position.
	 *
	 * @param size  Output pointer for the absolute stream size.
	 * @return B_OK always.
	 */
	virtual	status_t GetSize(off_t* size) const
	{
		AutoReadLocker _(fLock);

		// We use the backend position to make our buffer
		// independant of that.
		*size = _RelativeToPosition(fBackPosition);

		return B_OK;
	}

	/**
	 * @brief Appends data to the back of the stream (producer side).
	 *
	 * Writes to the current fBackPosition and advances it by the number of
	 * bytes written.
	 *
	 * @param buffer  Source data.
	 * @param size    Number of bytes to append.
	 * @return Number of bytes written, or a negative error code.
	 */
	ssize_t BackWrite(const void* buffer, size_t size)
	{
		AutoWriteLocker _(fLock);

		off_t ret = fBuffer->WriteAt(fBackPosition, buffer, size);
		fBackPosition += ret;
		return ret;
	}

	/**
	 * @brief Replaces the backing BPositionIO with a new one, freeing the old one.
	 *
	 * @param buffer  New backing store (takes ownership).
	 */
	void SetBuffer(BPositionIO* buffer)
	{
		delete fBuffer;
		fBuffer = buffer;
	}

	/**
	 * @brief Returns whether the owning stream has the B_MEDIA_STREAMING flag set.
	 *
	 * @return true if the stream is a live/streaming source.
	 */
	bool IsStreaming() const
	{
		int32 flags = 0;
		fOwner->GetFlags(&flags);
		return ((flags & B_MEDIA_STREAMING) == B_MEDIA_STREAMING);
	}

	/**
	 * @brief Returns whether the owning stream has the B_MEDIA_MUTABLE_SIZE flag set.
	 *
	 * @return true if the stream size can grow over time.
	 */
	bool IsMutable() const
	{
		int32 flags = 0;
		fOwner->GetFlags(&flags);
		return ((flags & B_MEDIA_MUTABLE_SIZE) == B_MEDIA_MUTABLE_SIZE);
	}

	/**
	 * @brief Returns whether the owning stream has the B_MEDIA_SEEKABLE flag set.
	 *
	 * @return true if random-access seeking is supported.
	 */
	bool IsSeekable() const
	{
		int32 flags = 0;
		fOwner->GetFlags(&flags);
		return ((flags & B_MEDIA_SEEKABLE) == B_MEDIA_SEEKABLE);
	}

	/**
	 * @brief Returns a const pointer to the current backing BPositionIO.
	 *
	 * @return Pointer to the backing buffer; ownership is not transferred.
	 */
	const BPositionIO* Buffer() const
	{
		return fBuffer;
	}

private:

	/**
	 * @brief Converts an absolute stream position to a relative buffer position.
	 *
	 * @param position  Absolute stream position.
	 * @return Relative offset within the backing buffer.
	 */
	off_t _PositionToRelative(off_t position) const
	{
		return position - fStartOffset;
	}

	/**
	 * @brief Converts a relative buffer position to an absolute stream position.
	 *
	 * @param position  Relative offset within the backing buffer.
	 * @return Absolute stream position.
	 */
	off_t _RelativeToPosition(off_t position) const
	{
		return position + fStartOffset;
	}

	BAdapterIO*			fOwner;
	off_t				fBackPosition;
	off_t				fStartOffset;

	BPositionIO*		fBuffer;

	mutable	RWLocker	fLock;

	bigtime_t			fTimeout;
};


/**
 * @brief Constructs a BAdapterIO with the given stream flags and read timeout.
 *
 * Allocates an internal RelativePositionIO backed by a BMallocIO.
 *
 * @param flags    Combination of B_MEDIA_STREAMING, B_MEDIA_SEEKABLE, and
 *                 B_MEDIA_MUTABLE_SIZE flags describing stream capabilities.
 * @param timeout  Maximum time in microseconds to wait for data in read/seek
 *                 operations, or B_INFINITE_TIMEOUT.
 */
BAdapterIO::BAdapterIO(int32 flags, bigtime_t timeout)
	:
	fFlags(flags),
	fBuffer(NULL),
	fTotalSize(0),
	fOpened(false),
	fSeekSem(-1),
	fInputAdapter(NULL)
{
	CALLED();

	fBuffer = new RelativePositionIO(this, new BMallocIO(), timeout);
}


/**
 * @brief Copy constructor — copying is not allowed and does nothing.
 */
BAdapterIO::BAdapterIO(const BAdapterIO &)
{
	// copying not allowed...
}


/**
 * @brief Destroys the BAdapterIO and frees the input adapter and backing buffer.
 */
BAdapterIO::~BAdapterIO()
{
	CALLED();

	delete fInputAdapter;
	delete fBuffer;
}


/**
 * @brief Returns the stream capability flags set at construction time.
 *
 * @param flags  Output pointer that receives the flags value.
 */
void
BAdapterIO::GetFlags(int32* flags) const
{
	CALLED();

	*flags = fFlags;
}


/**
 * @brief Reads bytes from the stream, waiting for data if necessary.
 *
 * Calls _EvaluateWait() to block until the requested region is available,
 * then delegates to the backing RelativePositionIO.
 *
 * @param position  Absolute stream position to read from.
 * @param buffer    Destination buffer.
 * @param size      Number of bytes to read.
 * @return Number of bytes read, or a negative error code.
 */
ssize_t
BAdapterIO::ReadAt(off_t position, void* buffer, size_t size)
{
	CALLED();

	status_t ret = _EvaluateWait(position, size);
	if (ret != B_OK)
		return ret;

	return fBuffer->ReadAt(position, buffer, size);
}


/**
 * @brief Writes bytes directly to the stream at an absolute position.
 *
 * @param position  Absolute stream position to write to.
 * @param buffer    Source data.
 * @param size      Number of bytes to write.
 * @return Number of bytes written, or a negative error code.
 */
ssize_t
BAdapterIO::WriteAt(off_t position, const void* buffer, size_t size)
{
	CALLED();

	return fBuffer->WriteAt(position, buffer, size);
}


/**
 * @brief Repositions the read cursor, blocking or requesting a backend seek
 *        when the target position is not yet buffered.
 *
 * For seekable streaming sources where the target position has already been
 * flushed, SeekRequested() is called and the method blocks on fSeekSem until
 * SeekCompleted() signals that the backend has repositioned.
 *
 * @param position  Offset operand for the seek.
 * @param seekMode  One of SEEK_SET, SEEK_CUR, or SEEK_END.
 * @return New absolute stream position, or B_NOT_SUPPORTED on failure.
 */
off_t
BAdapterIO::Seek(off_t position, uint32 seekMode)
{
	CALLED();

	off_t absolutePosition = 0;
	off_t size = 0;

	if (seekMode == SEEK_CUR)
		absolutePosition = Position()+position;
	else if (seekMode == SEEK_END) {
		if (GetSize(&size) != B_OK)
			return B_NOT_SUPPORTED;

		absolutePosition = size-position;
	}

	status_t ret = _EvaluateWait(absolutePosition, 0);

	if (ret == B_RESOURCE_UNAVAILABLE && fBuffer->IsStreaming()
			&& fBuffer->IsSeekable()) {

		fSeekSem = create_sem(0, "BAdapterIO seek sem");

		if (SeekRequested(absolutePosition) != B_OK)
			return B_NOT_SUPPORTED;

		TRACE("BAdapterIO::Seek: Locking on backend seek\n");
		acquire_sem(fSeekSem);
		TRACE("BAdapterIO::Seek: Seek completed!\n");
		fBuffer->ResetStartOffset(absolutePosition);
	} else if (ret != B_OK)
		return B_NOT_SUPPORTED;

	return fBuffer->Seek(position, seekMode);
}


/**
 * @brief Returns the current read cursor position in absolute stream coordinates.
 *
 * @return Current absolute stream position.
 */
off_t
BAdapterIO::Position() const
{
	CALLED();

	return fBuffer->Position();
}


/**
 * @brief Sets the total known size of the stream.
 *
 * For immutable streams, stores the size in fTotalSize; for mutable streams,
 * delegates to the backing buffer.
 *
 * @param size  New stream size in bytes.
 * @return B_OK on success, or an error code from the backing buffer.
 */
status_t
BAdapterIO::SetSize(off_t size)
{
	CALLED();

	if (!fBuffer->IsMutable()) {
		fTotalSize = size;
		return B_OK;
	}

	return fBuffer->SetSize(size);
}


/**
 * @brief Returns the current stream size.
 *
 * For immutable streams, returns fTotalSize; for mutable streams, queries the
 * backing buffer.
 *
 * @param size  Output pointer that receives the stream size.
 * @return B_OK on success, or an error code from the backing buffer.
 */
status_t
BAdapterIO::GetSize(off_t* size) const
{
	CALLED();

	if (!fBuffer->IsMutable()) {
		*size = fTotalSize;
		return B_OK;
	}

	return fBuffer->GetSize(size);
}


/**
 * @brief Marks the stream as open and ready for reading.
 *
 * @return B_OK always.
 */
status_t
BAdapterIO::Open()
{
	CALLED();

	fOpened = true;
	return B_OK;
}


/**
 * @brief Returns whether the stream has been opened via Open().
 *
 * @return true if Open() has been called and the stream is running.
 */
bool
BAdapterIO::IsRunning() const
{
	return fOpened;
}


/**
 * @brief Signals that the backend has completed a previously requested seek.
 *
 * Releases and destroys fSeekSem to unblock any Seek() call waiting on it.
 */
void
BAdapterIO::SeekCompleted()
{
	CALLED();
	release_sem(fSeekSem);
	delete_sem(fSeekSem);
	fSeekSem = -1;
}


/**
 * @brief Replaces the backing BPositionIO before the stream is opened.
 *
 * @param buffer  New backing store (ownership transferred to the internal
 *                RelativePositionIO).
 * @return B_OK on success, B_ERROR if the stream has already been opened.
 */
status_t
BAdapterIO::SetBuffer(BPositionIO* buffer)
{
	// We can't change the buffer while we
	// are running.
	if (fOpened)
		return B_ERROR;

	fBuffer->SetBuffer(buffer);
	return B_OK;
}


/**
 * @brief Discards buffered stream data that precedes \a position.
 *
 * Copies the retained portion of the current BMallocIO into a new BMallocIO
 * and replaces the backing store, freeing memory no longer needed.
 *
 * @param position  Absolute stream position; data before this is freed.
 * @return B_OK always.
 */
status_t
BAdapterIO::FlushBefore(off_t position)
{
	BMallocIO* buffer = new BMallocIO();
	BMallocIO* oldBuffer = (BMallocIO*)fBuffer->Buffer();
	fBuffer->FlushBefore(position, buffer, oldBuffer->Buffer(), oldBuffer->BufferLength());
	return B_OK;
}


/**
 * @brief Creates (or returns the existing) BInputAdapter for pushing data in.
 *
 * Only one BInputAdapter per BAdapterIO is allowed; subsequent calls return
 * the same instance.
 *
 * @return Pointer to the BInputAdapter; ownership is retained by this object.
 */
BInputAdapter*
BAdapterIO::BuildInputAdapter()
{
	if (fInputAdapter != NULL)
		return fInputAdapter;

	fInputAdapter = new BInputAdapter(this);
	return fInputAdapter;
}


/**
 * @brief Override hook called when the consumer requests an out-of-buffer seek.
 *
 * The default implementation returns B_ERROR. Subclasses should initiate the
 * backend reposition and call SeekCompleted() when done.
 *
 * @param position  The absolute stream position being sought to.
 * @return B_OK if the seek was initiated, or an error code.
 */
status_t
BAdapterIO::SeekRequested(off_t position)
{
	CALLED();

	return B_ERROR;
}


/**
 * @brief Appends data to the stream from the producer side.
 *
 * Forwards the write to the internal RelativePositionIO's BackWrite().
 *
 * @param buffer  Source data.
 * @param size    Number of bytes to write.
 * @return Number of bytes written, or a negative error code.
 */
ssize_t
BAdapterIO::BackWrite(const void* buffer, size_t size)
{
	return fBuffer->BackWrite(buffer, size);
}


/**
 * @brief Evaluates readiness and waits for data at the given stream position.
 *
 * Queries the total stream size, evaluates whether the position is valid, and
 * if needed blocks via WaitForData() until the required bytes are present.
 *
 * @param pos   Absolute stream position to check.
 * @param size  Number of bytes required starting at \a pos.
 * @return B_OK when data is ready, or an appropriate error code.
 */
status_t
BAdapterIO::_EvaluateWait(off_t pos, off_t size)
{
	CALLED();

	off_t totalSize = 0;
	if (GetSize(&totalSize) != B_OK)
		TRACE("BAdapterIO::ReadAt: Can't get our size!\n");

	TRACE("BAdapterIO::_EvaluateWait TS %" B_PRId64 " P %" B_PRId64
		" S %" B_PRId64 "\n", totalSize, pos, size);

	status_t err = fBuffer->EvaluatePosition(pos, totalSize);

	TRACE("BAdapterIO::_EvaluateWait: %s\n", strerror(err));

	if (err != B_OK && err != B_WOULD_BLOCK)
		return err;

	TRACE("BAdapterIO::_EvaluateWait: waiting for data\n");

	return fBuffer->WaitForData(pos, size);
}


/**
 * @brief Constructs a BInputAdapter bound to the given BAdapterIO.
 *
 * @param io  The owning BAdapterIO that receives data via BackWrite().
 */
BInputAdapter::BInputAdapter(BAdapterIO* io)
	:
	fIO(io)
{
}


/**
 * @brief Destroys the BInputAdapter.
 */
BInputAdapter::~BInputAdapter()
{
}


/**
 * @brief Pushes data into the owning BAdapterIO's backing stream.
 *
 * Delegates to BAdapterIO::BackWrite(), which appends the data to the
 * producer end of the internal buffer.
 *
 * @param buffer  Source data to write.
 * @param size    Number of bytes to write.
 * @return Number of bytes written, or a negative error code.
 */
ssize_t
BInputAdapter::Write(const void* buffer, size_t size)
{
	return fIO->BackWrite(buffer, size);
}


// FBC
void BAdapterIO::_ReservedAdapterIO1() {}
void BAdapterIO::_ReservedAdapterIO2() {}
void BAdapterIO::_ReservedAdapterIO3() {}
void BAdapterIO::_ReservedAdapterIO4() {}
void BAdapterIO::_ReservedAdapterIO5() {}

void BInputAdapter::_ReservedInputAdapter1() {}
void BInputAdapter::_ReservedInputAdapter2() {}
