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
 *   Copyright 2005-2014 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stefano Ceccherini, burton666@libero.it
 */


/**
 * @file DataIO.cpp
 * @brief Implementation of the four core data-stream classes.
 *
 * This file provides the implementations of:
 *   - BDataIO  — abstract base for sequential byte-stream I/O, with helpers
 *                ReadExactly() and WriteExactly() that loop until all bytes
 *                are transferred or an error occurs.
 *   - BPositionIO — extends BDataIO with absolute-position ReadAt()/WriteAt()
 *                   semantics, implementing sequential Read()/Write() on top
 *                   of them, plus ReadAtExactly(), WriteAtExactly(), GetSize().
 *   - BMemoryIO — concrete BPositionIO backed by a caller-supplied fixed-size
 *                 buffer (mutable or read-only).
 *   - BMallocIO — concrete BPositionIO that owns a heap-allocated, dynamically
 *                 resizable buffer allocated in multiples of a configurable
 *                 block size.
 *
 * @see BDataIO, BPositionIO, BMemoryIO, BMallocIO
 */


#include <DataIO.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <Errors.h>

#if defined(_KERNEL_MODE) && !defined(_BOOT_MODE)
// for user_memcpy() and IS_USER_ADDRESS()
#include <KernelExport.h>

#include <kernel.h>
#endif


/**
 * @brief Construct a BDataIO instance.
 *
 * The base class has no state to initialise; this constructor exists so that
 * subclasses can call it through their own constructors.
 */
BDataIO::BDataIO()
{
}


/**
 * @brief Destroy the BDataIO instance.
 *
 * Virtual destructor — ensures that deleting a BDataIO pointer correctly
 * calls the most-derived destructor.
 */
BDataIO::~BDataIO()
{
}


/**
 * @brief Read up to \a size bytes from the stream into \a buffer.
 *
 * The base-class implementation always returns B_NOT_SUPPORTED. Concrete
 * subclasses must override this method to provide actual I/O behaviour.
 *
 * @param buffer Destination buffer for the bytes read.
 * @param size   Maximum number of bytes to read.
 * @return The number of bytes actually read (>= 0), or a negative error code.
 *         B_NOT_SUPPORTED is returned by this base implementation.
 */
ssize_t
BDataIO::Read(void* buffer, size_t size)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Write up to \a size bytes from \a buffer into the stream.
 *
 * The base-class implementation always returns B_NOT_SUPPORTED. Concrete
 * subclasses must override this method to provide actual I/O behaviour.
 *
 * @param buffer Source buffer containing the bytes to write.
 * @param size   Number of bytes to write.
 * @return The number of bytes actually written (>= 0), or a negative error code.
 *         B_NOT_SUPPORTED is returned by this base implementation.
 */
ssize_t
BDataIO::Write(const void* buffer, size_t size)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Flush any buffered data to the underlying storage or device.
 *
 * The base-class implementation is a no-op that returns B_OK. Subclasses that
 * buffer data internally (e.g. BBufferIO) should override this to push
 * pending bytes downstream.
 *
 * @return B_OK on success, or an error code if the flush fails.
 */
status_t
BDataIO::Flush()
{
	return B_OK;
}


/**
 * @brief Read exactly \a size bytes, retrying partial reads until complete.
 *
 * Calls Read() in a loop until all \a size bytes have been transferred, an
 * error is returned, or a zero-byte read is detected (treated as end-of-stream
 * / B_PARTIAL_READ).
 *
 * @param buffer     Destination buffer; must be at least \a size bytes.
 * @param size       Exact number of bytes to read.
 * @param _bytesRead Optional output pointer that receives the total number of
 *                   bytes actually read (may be less than \a size on error).
 * @return B_OK if exactly \a size bytes were read.
 *         B_PARTIAL_READ if the stream ended before \a size bytes were read.
 *         Any negative error code propagated from Read().
 */
status_t
BDataIO::ReadExactly(void* buffer, size_t size, size_t* _bytesRead)
{
	uint8* out = (uint8*)buffer;
	size_t bytesRemaining = size;
	status_t error = B_OK;

	while (bytesRemaining > 0) {
		ssize_t bytesRead = Read(out, bytesRemaining);
		if (bytesRead < 0) {
			error = bytesRead;
			break;
		}

		if (bytesRead == 0) {
			error = B_PARTIAL_READ;
			break;
		}

		out += bytesRead;
		bytesRemaining -= bytesRead;
	}

	if (_bytesRead != NULL)
		*_bytesRead = size - bytesRemaining;

	return error;
}


/**
 * @brief Write exactly \a size bytes, retrying partial writes until complete.
 *
 * Calls Write() in a loop until all \a size bytes have been transferred, an
 * error is returned, or a zero-byte write is detected (treated as
 * B_PARTIAL_WRITE).
 *
 * @param buffer        Source buffer; must contain at least \a size bytes.
 * @param size          Exact number of bytes to write.
 * @param _bytesWritten Optional output pointer that receives the total number
 *                      of bytes actually written (may be less than \a size on
 *                      error).
 * @return B_OK if exactly \a size bytes were written.
 *         B_PARTIAL_WRITE if the stream could not accept further bytes.
 *         Any negative error code propagated from Write().
 */
status_t
BDataIO::WriteExactly(const void* buffer, size_t size, size_t* _bytesWritten)
{
	const uint8* in = (const uint8*)buffer;
	size_t bytesRemaining = size;
	status_t error = B_OK;

	while (bytesRemaining > 0) {
		ssize_t bytesWritten = Write(in, bytesRemaining);
		if (bytesWritten < 0) {
			error = bytesWritten;
			break;
		}

		if (bytesWritten == 0) {
			error = B_PARTIAL_WRITE;
			break;
		}

		in += bytesWritten;
		bytesRemaining -= bytesWritten;
	}

	if (_bytesWritten != NULL)
		*_bytesWritten = size - bytesRemaining;

	return error;
}


// Private or Reserved

/** @brief Copying a BDataIO is not allowed. */
BDataIO::BDataIO(const BDataIO &)
{
	// Copying not allowed
}


/** @brief Copy-assigning a BDataIO is not allowed. */
BDataIO &
BDataIO::operator=(const BDataIO &)
{
	// Copying not allowed
	return *this;
}


#if __GNUC__ == 2


extern "C" status_t
_ReservedDataIO1__7BDataIO(BDataIO* self)
{
	return self->BDataIO::Flush();
}


#else


// TODO: RELEASE: Remove!

extern "C" status_t
_ZN7BDataIO16_ReservedDataIO1Ev(BDataIO* self)
{
	return self->BDataIO::Flush();
}


#endif


// FBC
void BDataIO::_ReservedDataIO2(){}
void BDataIO::_ReservedDataIO3(){}
void BDataIO::_ReservedDataIO4(){}
void BDataIO::_ReservedDataIO5(){}
void BDataIO::_ReservedDataIO6(){}
void BDataIO::_ReservedDataIO7(){}
void BDataIO::_ReservedDataIO8(){}
void BDataIO::_ReservedDataIO9(){}
void BDataIO::_ReservedDataIO10(){}
void BDataIO::_ReservedDataIO11(){}
void BDataIO::_ReservedDataIO12(){}


//	#pragma mark -


/**
 * @brief Construct a BPositionIO instance.
 *
 * No state is initialised by the base class itself; subclasses initialise
 * their own position and buffer fields.
 */
BPositionIO::BPositionIO()
{
}


/**
 * @brief Destroy the BPositionIO instance.
 *
 * Virtual destructor — ensures proper cleanup in derived classes.
 */
BPositionIO::~BPositionIO()
{
}


/**
 * @brief Read up to \a size bytes sequentially from the current position.
 *
 * Delegates to ReadAt() at the current Position(), then advances the position
 * by the number of bytes actually read.
 *
 * @param buffer Destination buffer for the bytes read.
 * @param size   Maximum number of bytes to read.
 * @return The number of bytes read (>= 0), or a negative error code.
 */
ssize_t
BPositionIO::Read(void* buffer, size_t size)
{
	off_t curPos = Position();
	ssize_t result = ReadAt(curPos, buffer, size);
	if (result > 0)
		Seek(result, SEEK_CUR);

	return result;
}


/**
 * @brief Write up to \a size bytes sequentially at the current position.
 *
 * Delegates to WriteAt() at the current Position(), then advances the position
 * by the number of bytes actually written.
 *
 * @param buffer Source buffer containing the bytes to write.
 * @param size   Number of bytes to write.
 * @return The number of bytes written (>= 0), or a negative error code.
 */
ssize_t
BPositionIO::Write(const void* buffer, size_t size)
{
	off_t curPos = Position();
	ssize_t result = WriteAt(curPos, buffer, size);
	if (result > 0)
		Seek(result, SEEK_CUR);

	return result;
}


/**
 * @brief Read exactly \a size bytes starting at absolute position \a position.
 *
 * Calls ReadAt() in a loop, advancing \a position on each partial read, until
 * all requested bytes have been transferred or an error occurs.
 *
 * @param position   Absolute byte offset in the stream from which to start.
 * @param buffer     Destination buffer; must be at least \a size bytes.
 * @param size       Exact number of bytes to read.
 * @param _bytesRead Optional output pointer that receives the total bytes read.
 * @return B_OK on full read, B_PARTIAL_READ on early end-of-stream, or a
 *         negative error code from ReadAt().
 */
status_t
BPositionIO::ReadAtExactly(off_t position, void* buffer, size_t size,
	size_t* _bytesRead)
{
	uint8* out = (uint8*)buffer;
	size_t bytesRemaining = size;
	status_t error = B_OK;

	while (bytesRemaining > 0) {
		ssize_t bytesRead = ReadAt(position, out, bytesRemaining);
		if (bytesRead < 0) {
			error = bytesRead;
			break;
		}

		if (bytesRead == 0) {
			error = B_PARTIAL_READ;
			break;
		}

		out += bytesRead;
		bytesRemaining -= bytesRead;
		position += bytesRead;
	}

	if (_bytesRead != NULL)
		*_bytesRead = size - bytesRemaining;

	return error;
}


/**
 * @brief Write exactly \a size bytes starting at absolute position \a position.
 *
 * Calls WriteAt() in a loop, advancing \a position on each partial write,
 * until all requested bytes have been transferred or an error occurs.
 *
 * @param position      Absolute byte offset in the stream at which to start.
 * @param buffer        Source buffer; must contain at least \a size bytes.
 * @param size          Exact number of bytes to write.
 * @param _bytesWritten Optional output pointer that receives the total bytes
 *                      written.
 * @return B_OK on full write, B_PARTIAL_WRITE if the stream is full, or a
 *         negative error code from WriteAt().
 */
status_t
BPositionIO::WriteAtExactly(off_t position, const void* buffer, size_t size,
	size_t* _bytesWritten)
{
	const uint8* in = (const uint8*)buffer;
	size_t bytesRemaining = size;
	status_t error = B_OK;

	while (bytesRemaining > 0) {
		ssize_t bytesWritten = WriteAt(position, in, bytesRemaining);
		if (bytesWritten < 0) {
			error = bytesWritten;
			break;
		}

		if (bytesWritten == 0) {
			error = B_PARTIAL_WRITE;
			break;
		}

		in += bytesWritten;
		bytesRemaining -= bytesWritten;
		position += bytesWritten;
	}

	if (_bytesWritten != NULL)
		*_bytesWritten = size - bytesRemaining;

	return error;
}


/**
 * @brief Set the logical size of the stream to \a size bytes.
 *
 * The base-class implementation always returns B_ERROR. Subclasses that
 * support resizing (e.g. BMallocIO) must override this.
 *
 * @param size The desired new size in bytes.
 * @return B_OK on success, or B_ERROR if resizing is not supported.
 */
status_t
BPositionIO::SetSize(off_t size)
{
	return B_ERROR;
}


/**
 * @brief Return the total size of the stream in bytes.
 *
 * Seeks to the end of the stream to determine its size, then restores the
 * original position. This is a const method but requires non-const Seek()
 * calls, so it uses const_cast internally.
 *
 * @param size Output pointer that receives the stream size.
 * @return B_OK on success, B_BAD_VALUE if \a size is NULL, or a negative
 *         error code if Position() or Seek() fails.
 */
status_t
BPositionIO::GetSize(off_t* size) const
{
	if (!size)
		return B_BAD_VALUE;

	off_t currentPos = Position();
	if (currentPos < 0)
		return (status_t)currentPos;

	*size = const_cast<BPositionIO*>(this)->Seek(0, SEEK_END);
	if (*size < 0)
		return (status_t)*size;

	off_t pos = const_cast<BPositionIO*>(this)->Seek(currentPos, SEEK_SET);

	if (pos != currentPos)
		return pos < 0 ? (status_t)pos : B_ERROR;

	return B_OK;
}


// FBC
extern "C" void _ReservedPositionIO1__11BPositionIO() {}
void BPositionIO::_ReservedPositionIO2(){}
void BPositionIO::_ReservedPositionIO3(){}
void BPositionIO::_ReservedPositionIO4(){}
void BPositionIO::_ReservedPositionIO5(){}
void BPositionIO::_ReservedPositionIO6(){}
void BPositionIO::_ReservedPositionIO7(){}
void BPositionIO::_ReservedPositionIO8(){}
void BPositionIO::_ReservedPositionIO9(){}
void BPositionIO::_ReservedPositionIO10(){}
void BPositionIO::_ReservedPositionIO11(){}
void BPositionIO::_ReservedPositionIO12(){}


//	#pragma mark -


/**
 * @brief Construct a mutable BMemoryIO over a caller-supplied buffer.
 *
 * The object does not take ownership of \a buffer; the caller is responsible
 * for ensuring the buffer remains valid for the lifetime of this object.
 * ReadAt(), WriteAt(), and SetSize() are all permitted.
 *
 * @param buffer Pointer to the mutable backing buffer.
 * @param length Size of the buffer in bytes; both fLength and fBufferSize are
 *               initialised to this value.
 */
BMemoryIO::BMemoryIO(void* buffer, size_t length)
	:
	fReadOnly(false),
	fBuffer(static_cast<char*>(buffer)),
	fLength(length),
	fBufferSize(length),
	fPosition(0)
{
}


/**
 * @brief Construct a read-only BMemoryIO over a caller-supplied const buffer.
 *
 * WriteAt() and SetSize() will return B_NOT_ALLOWED on this instance.
 * The caller retains ownership of the buffer.
 *
 * @param buffer Pointer to the read-only backing buffer.
 * @param length Size of the buffer in bytes.
 */
BMemoryIO::BMemoryIO(const void* buffer, size_t length)
	:
	fReadOnly(true),
	fBuffer(const_cast<char*>(static_cast<const char*>(buffer))),
	fLength(length),
	fBufferSize(length),
	fPosition(0)
{
}


/**
 * @brief Destroy the BMemoryIO instance.
 *
 * The backing buffer is owned by the caller and is not freed here.
 */
BMemoryIO::~BMemoryIO()
{
}


/**
 * @brief Read up to \a size bytes from the buffer at absolute position \a pos.
 *
 * Clamps the read to the valid data range [0, fLength). Does not modify the
 * current stream position.
 *
 * @param pos    Absolute byte offset within the buffer to read from.
 * @param buffer Destination for the bytes read.
 * @param size   Maximum number of bytes to read.
 * @return The number of bytes actually read, or B_BAD_VALUE if \a buffer is
 *         NULL or \a pos is negative.
 */
ssize_t
BMemoryIO::ReadAt(off_t pos, void* buffer, size_t size)
{
	if (buffer == NULL || pos < 0)
		return B_BAD_VALUE;

	ssize_t sizeRead = 0;
	if (pos < (off_t)fLength) {
		sizeRead = min_c((off_t)size, (off_t)fLength - pos);
		memcpy(buffer, fBuffer + pos, sizeRead);
	}

	return sizeRead;
}


/**
 * @brief Write up to \a size bytes into the buffer at absolute position \a pos.
 *
 * Writes are clamped to the allocated buffer size (fBufferSize). If the write
 * extends beyond the current logical length (fLength), fLength is updated.
 * Returns B_NOT_ALLOWED if the object was constructed with a const buffer.
 *
 * @param pos    Absolute byte offset within the buffer to write at.
 * @param buffer Source of the bytes to write.
 * @param size   Number of bytes to write.
 * @return The number of bytes actually written, B_NOT_ALLOWED for read-only
 *         instances, or B_BAD_VALUE if \a buffer is NULL or \a pos is negative.
 */
ssize_t
BMemoryIO::WriteAt(off_t pos, const void* buffer, size_t size)
{
	if (fReadOnly)
		return B_NOT_ALLOWED;

	if (buffer == NULL || pos < 0)
		return B_BAD_VALUE;

	ssize_t sizeWritten = 0;
	if (pos < (off_t)fBufferSize) {
		sizeWritten = min_c((off_t)size, (off_t)fBufferSize - pos);
#if defined(_KERNEL_MODE) && !defined(_BOOT_MODE)
		if (IS_USER_ADDRESS(fBuffer)) {
			if (user_memcpy(fBuffer + pos, buffer, sizeWritten) != B_OK)
				return B_BAD_ADDRESS;
		} else
#endif
		memcpy(fBuffer + pos, buffer, sizeWritten);
	}

	if (pos + sizeWritten > (off_t)fLength)
		fLength = pos + sizeWritten;

	return sizeWritten;
}


/**
 * @brief Move the stream position to the byte specified by \a position and
 *        \a seek_mode.
 *
 * Supports SEEK_SET, SEEK_CUR, and SEEK_END. No bounds checking is performed
 * on the resulting position — callers may seek past the end of the buffer.
 *
 * @param position  Byte offset, interpreted relative to \a seek_mode.
 * @param seek_mode One of SEEK_SET, SEEK_CUR, or SEEK_END.
 * @return The new absolute stream position.
 */
off_t
BMemoryIO::Seek(off_t position, uint32 seek_mode)
{
	switch (seek_mode) {
		case SEEK_SET:
			fPosition = position;
			break;
		case SEEK_CUR:
			fPosition += position;
			break;
		case SEEK_END:
			fPosition = fLength + position;
			break;
		default:
			break;
	}

	return fPosition;
}


/**
 * @brief Return the current stream position.
 *
 * @return The current byte offset within the buffer.
 */
off_t
BMemoryIO::Position() const
{
	return fPosition;
}


/**
 * @brief Set the logical data length of the buffer to \a size bytes.
 *
 * Only reduces or expands the logical view of the data within the fixed-size
 * backing buffer. The requested \a size must not exceed fBufferSize.
 * Returns B_NOT_ALLOWED for read-only instances.
 *
 * @param size The new logical length in bytes.
 * @return B_OK on success, B_NOT_ALLOWED for read-only instances, or B_ERROR
 *         if \a size exceeds the buffer capacity.
 */
status_t
BMemoryIO::SetSize(off_t size)
{
	if (fReadOnly)
		return B_NOT_ALLOWED;

	if (size > (off_t)fBufferSize)
		return B_ERROR;

	fLength = size;

	return B_OK;
}


// Private or Reserved

/** @brief Copying a BMemoryIO is not allowed. */
BMemoryIO::BMemoryIO(const BMemoryIO &)
{
	//Copying not allowed
}


/** @brief Copy-assigning a BMemoryIO is not allowed. */
BMemoryIO &
BMemoryIO::operator=(const BMemoryIO &)
{
	//Copying not allowed
	return *this;
}


// FBC
void BMemoryIO::_ReservedMemoryIO1(){}
void BMemoryIO::_ReservedMemoryIO2(){}


//	#pragma mark -


/**
 * @brief Construct an empty BMallocIO with a default block size of 256 bytes.
 *
 * No heap memory is allocated until the first write or an explicit SetSize()
 * call. The block size controls the granularity of heap allocations.
 */
BMallocIO::BMallocIO()
	:
	fBlockSize(256),
	fMallocSize(0),
	fLength(0),
	fData(NULL),
	fPosition(0)
{
}


/**
 * @brief Destroy the BMallocIO and free the heap buffer.
 *
 * The internally managed buffer is freed; any pointers previously returned
 * by Buffer() become invalid after this call.
 */
BMallocIO::~BMallocIO()
{
	free(fData);
}


/**
 * @brief Read up to \a size bytes from the heap buffer at absolute position
 *        \a pos.
 *
 * Does not modify the current stream position.
 *
 * @param pos    Absolute byte offset within the buffer to read from.
 * @param buffer Destination for the bytes read.
 * @param size   Maximum number of bytes to read.
 * @return The number of bytes actually read, or B_BAD_VALUE if \a buffer is
 *         NULL.
 */
ssize_t
BMallocIO::ReadAt(off_t pos, void* buffer, size_t size)
{
	if (buffer == NULL)
		return B_BAD_VALUE;

	ssize_t sizeRead = 0;
	if (pos < (off_t)fLength) {
		sizeRead = min_c((off_t)size, (off_t)fLength - pos);
		memcpy(buffer, fData + pos, sizeRead);
	}

	return sizeRead;
}


/**
 * @brief Write \a size bytes from \a buffer into the heap buffer at absolute
 *        position \a pos.
 *
 * Automatically grows the heap allocation via SetSize() if necessary. Any
 * newly allocated bytes beyond the previous fLength are zero-initialised by
 * SetSize(). Updates fLength if the write extends past the current end.
 *
 * @param pos    Absolute byte offset within the buffer to write at.
 * @param buffer Source of the bytes to write.
 * @param size   Number of bytes to write.
 * @return \a size on success, B_BAD_VALUE if \a buffer is NULL, or a negative
 *         error code if the heap allocation fails.
 */
ssize_t
BMallocIO::WriteAt(off_t pos, const void* buffer, size_t size)
{
	if (buffer == NULL)
		return B_BAD_VALUE;

	size_t newSize = max_c(pos + (off_t)size, (off_t)fLength);
	status_t error = B_OK;

	if (newSize > fMallocSize)
		error = SetSize(newSize);

	if (error == B_OK) {
		memcpy(fData + pos, buffer, size);
		if (pos + size > fLength)
			fLength = pos + size;
	}

	return error != B_OK ? error : size;
}


/**
 * @brief Move the stream position to the byte specified by \a position and
 *        \a seekMode.
 *
 * Supports SEEK_SET, SEEK_CUR, and SEEK_END. No bounds checking is performed
 * on the resulting position.
 *
 * @param position Byte offset, interpreted relative to \a seekMode.
 * @param seekMode One of SEEK_SET, SEEK_CUR, or SEEK_END.
 * @return The new absolute stream position.
 */
off_t
BMallocIO::Seek(off_t position, uint32 seekMode)
{
	switch (seekMode) {
		case SEEK_SET:
			fPosition = position;
			break;
		case SEEK_END:
			fPosition = fLength + position;
			break;
		case SEEK_CUR:
			fPosition += position;
			break;
		default:
			break;
	}
	return fPosition;
}


/**
 * @brief Return the current stream position.
 *
 * @return The current byte offset within the heap buffer.
 */
off_t
BMallocIO::Position() const
{
	return fPosition;
}


/**
 * @brief Resize the heap buffer to exactly \a size logical bytes.
 *
 * The physical allocation is rounded up to a multiple of fBlockSize. Newly
 * allocated bytes are zero-initialised. If \a size is zero the buffer is
 * freed entirely.
 *
 * @param size The desired new logical size in bytes.
 * @return B_OK on success, or B_NO_MEMORY if the reallocation fails.
 */
status_t
BMallocIO::SetSize(off_t size)
{
	status_t error = B_OK;
	if (size == 0) {
		// size == 0, free the memory
		free(fData);
		fData = NULL;
		fMallocSize = 0;
	} else {
		// size != 0, see, if necessary to resize
		size_t newSize = (size + fBlockSize - 1) / fBlockSize * fBlockSize;
		if (size != (off_t)fMallocSize) {
			// we need to resize
			if (char* newData = static_cast<char*>(realloc(fData, newSize))) {
				// set the new area to 0
				if (newSize > fMallocSize)
					memset(newData + fMallocSize, 0, newSize - fMallocSize);
				fData = newData;
				fMallocSize = newSize;
			} else	// couldn't alloc the memory
				error = B_NO_MEMORY;
		}
	}

	if (error == B_OK)
		fLength = size;

	return error;
}


/**
 * @brief Set the allocation block size used when growing the heap buffer.
 *
 * All future heap allocations will be rounded up to a multiple of
 * \a blockSize. A value of zero is silently promoted to 1.
 *
 * @param blockSize The new block size in bytes (must be > 0; 0 is treated as 1).
 */
void
BMallocIO::SetBlockSize(size_t blockSize)
{
	if (blockSize == 0)
		blockSize = 1;

	if (blockSize != fBlockSize)
		fBlockSize = blockSize;
}


/**
 * @brief Return a read-only pointer to the internal heap buffer.
 *
 * The returned pointer is valid only as long as the BMallocIO object is alive
 * and no write or resize operation is performed.
 *
 * @return A const pointer to the heap buffer, or NULL if no data has been
 *         written yet.
 */
const void*
BMallocIO::Buffer() const
{
	return fData;
}


/**
 * @brief Return the current logical length of the data in the buffer.
 *
 * This reflects the highest byte position written, not the physical allocation
 * size (which is always a multiple of fBlockSize).
 *
 * @return The number of valid data bytes in the buffer.
 */
size_t
BMallocIO::BufferLength() const
{
	return fLength;
}


// Private or Reserved

/** @brief Copying a BMallocIO is not allowed. */
BMallocIO::BMallocIO(const BMallocIO &)
{
	// copying not allowed...
}


/** @brief Copy-assigning a BMallocIO is not allowed. */
BMallocIO &
BMallocIO::operator=(const BMallocIO &)
{
	// copying not allowed...
	return *this;
}


// FBC
void BMallocIO::_ReservedMallocIO1() {}
void BMallocIO::_ReservedMallocIO2() {}
