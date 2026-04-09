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
 *   Copyright 2009, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Scott T. Mansfield, thephantom@mac.com
 *       Bruno Albuquerque, bga@bug-br.org.br
 */

/** @file NetBuffer.cpp
 *  @brief Wire-format serialisation buffer providing append/remove primitives
 *         for typed values. Integer scalars are stored in big-endian (network)
 *         byte order, strings are stored NUL-terminated, and BMessages are
 *         flattened in place. */

#include <ByteOrder.h>
#include <Message.h>
#include <TypeConstants.h>

#include "DynamicBuffer.h"
#include "NetBuffer.h"

#include <new>
#include <string.h>

/** @brief Constructs a BNetBuffer backed by a DynamicBuffer of the given size.
 *  @param size Initial capacity, in bytes, of the underlying buffer. */
BNetBuffer::BNetBuffer(size_t size) :
	BArchivable(),
	fInit(B_NO_INIT)
{
	fImpl = new (std::nothrow) DynamicBuffer(size);
	if (fImpl != NULL)
		fInit = fImpl->InitCheck();
}


/** @brief Destructor. Deletes the owned DynamicBuffer. */
BNetBuffer::~BNetBuffer()
{
	delete fImpl;
}


/** @brief Copy constructor. Creates an independent deep copy of the buffer. */
BNetBuffer::BNetBuffer(const BNetBuffer& buffer) :
	BArchivable(),
	fInit(B_NO_INIT)
{
	fImpl = new (std::nothrow) DynamicBuffer(*buffer.GetImpl());
	if (fImpl != NULL)
		fInit = fImpl->InitCheck();
}


/** @brief Unarchiving constructor. Populates the buffer from a "buffer"
 *         B_RAW_TYPE field previously written by Archive().
 *  @param archive Source BMessage. */
BNetBuffer::BNetBuffer(BMessage* archive) :
	BArchivable(),
	fInit(B_NO_INIT)
{
	const unsigned char* bufferPtr;
	ssize_t bufferSize;

	if (archive->FindData("buffer", B_RAW_TYPE, (const void**)&bufferPtr,
		&bufferSize) == B_OK) {
		fImpl = new (std::nothrow) DynamicBuffer(bufferSize);
		if (fImpl != NULL) {
			ssize_t result = fImpl->Write(bufferPtr, bufferSize);
			if (result >= 0)
				fInit = fImpl->InitCheck();
			else
				fInit = result;
		}
	}
}

/** @brief Assignment operator. Replaces the current contents with a deep
 *         copy of @a buffer. */
BNetBuffer&
BNetBuffer::operator=(const BNetBuffer& buffer)
{
	if (&buffer != this) {
		delete fImpl;

		fImpl = new (std::nothrow) DynamicBuffer(*buffer.GetImpl());
		if (fImpl != NULL)
			fInit = fImpl->InitCheck();
	}
	return *this;
}


/** @brief Serialises the raw buffer bytes into @a into as a "buffer" field.
 *  @param into Destination BMessage.
 *  @param deep Ignored (kept for BArchivable compatibility).
 *  @return B_OK on success, B_NO_INIT if the buffer was not initialised,
 *          or an error from BMessage::AddData(). */
status_t
BNetBuffer::Archive(BMessage* into, bool deep) const
{
	if (fInit != B_OK)
		return B_NO_INIT;

	status_t result = into->AddData("buffer", B_RAW_TYPE, fImpl->Data(),
		fImpl->BytesRemaining());

	return result;
}


/** @brief BArchivable factory that reconstructs a BNetBuffer from a BMessage.
 *  @param archive Source BMessage produced by Archive().
 *  @return New BNetBuffer owned by the caller, or NULL on failure. */
BArchivable*
BNetBuffer::Instantiate(BMessage* archive)
{
    if (!validate_instantiation(archive, "BNetBuffer"))
        return NULL;

    BNetBuffer* buffer = new (std::nothrow) BNetBuffer(archive);
    if (buffer == NULL)
        return NULL;

    if (buffer->InitCheck() != B_OK) {
        delete buffer;
        return NULL;
    }

    return buffer;
}


/** @brief Returns the construction status of the underlying buffer. */
status_t
BNetBuffer::InitCheck()
{
	return fInit;
}


/** @brief Appends an 8-bit signed integer to the buffer. */
status_t
BNetBuffer::AppendInt8(int8 data)
{
	return AppendData((const void*)&data, sizeof(int8));
}


/** @brief Appends an 8-bit unsigned integer to the buffer. */
status_t
BNetBuffer::AppendUint8(uint8 data)
{
	return AppendData((const void*)&data, sizeof(int8));
}


/** @brief Appends a 16-bit signed integer in network byte order. */
status_t
BNetBuffer::AppendInt16(int16 data)
{
	int16 be_data = B_HOST_TO_BENDIAN_INT16(data);
	return AppendData((const void*)&be_data, sizeof(int16));
}


/** @brief Appends a 16-bit unsigned integer in network byte order. */
status_t
BNetBuffer::AppendUint16(uint16 data)
{
	uint16 be_data = B_HOST_TO_BENDIAN_INT16(data);
	return AppendData((const void*)&be_data, sizeof(uint16));
}


/** @brief Appends a 32-bit signed integer in network byte order. */
status_t
BNetBuffer::AppendInt32(int32 data)
{
	int32 be_data = B_HOST_TO_BENDIAN_INT32(data);
	return AppendData((const void*)&be_data, sizeof(int32));
}


/** @brief Appends a 32-bit unsigned integer in network byte order. */
status_t
BNetBuffer::AppendUint32(uint32 data)
{
	uint32 be_data = B_HOST_TO_BENDIAN_INT32(data);
	return AppendData((const void*)&be_data, sizeof(uint32));
}


/** @brief Appends a single-precision float (byte order unchanged). */
status_t
BNetBuffer::AppendFloat(float data)
{
	return AppendData((const void*)&data, sizeof(float));
}


/** @brief Appends a double-precision float (byte order unchanged). */
status_t
BNetBuffer::AppendDouble(double data)
{
	return AppendData((const void*)&data, sizeof(double));
}


/** @brief Appends a NUL-terminated C string, including its terminator. */
status_t
BNetBuffer::AppendString(const char* data)
{
	return AppendData((const void*)data, strlen(data) + 1);
}


/** @brief Appends an arbitrary blob of bytes to the end of the buffer.
 *  @param data Pointer to the source bytes.
 *  @param size Number of bytes to append.
 *  @return B_OK on success, B_NO_INIT if the buffer is not constructed,
 *          or a buffer error code. */
status_t
BNetBuffer::AppendData(const void* data, size_t size)
{
	if (fInit != B_OK)
		return B_NO_INIT;

	ssize_t bytesWritten = fImpl->Write(data, size);
	if (bytesWritten < 0)
		return (status_t)bytesWritten;
	return (size_t)bytesWritten == size ? B_OK : B_ERROR;
}


#define STACK_BUFFER_SIZE 2048

/** @brief Flattens a BMessage and appends its bytes to the buffer.
 *         Small messages are flattened on the stack; larger ones on the heap.
 *  @param data The BMessage to flatten and append.
 *  @return B_OK on success, or an error code on flatten/append failure. */
status_t
BNetBuffer::AppendMessage(const BMessage& data)
{
	char stackFlattenedData[STACK_BUFFER_SIZE];

	ssize_t dataSize = data.FlattenedSize();

	if (dataSize < 0)
		return dataSize;

	if (dataSize == 0)
		return B_ERROR;

	status_t result = B_OK;

	if (dataSize > STACK_BUFFER_SIZE) {
		char* flattenedData = new (std::nothrow) char[dataSize];
		if (flattenedData == NULL)
			return B_NO_MEMORY;

		if (data.Flatten(flattenedData, dataSize) == B_OK)
			result = AppendData((const void*)&flattenedData, dataSize);

		delete[] flattenedData;
	} else {
		if (data.Flatten(stackFlattenedData, dataSize) == B_OK)
			result = AppendData((const void*)&stackFlattenedData, dataSize);
	}

	return result;
}


/** @brief Appends a 64-bit signed integer in network byte order. */
status_t
BNetBuffer::AppendInt64(int64 data)
{
	int64 be_data = B_HOST_TO_BENDIAN_INT64(data);
	return AppendData((const void*)&be_data, sizeof(int64));
}


/** @brief Appends a 64-bit unsigned integer in network byte order. */
status_t
BNetBuffer::AppendUint64(uint64 data)
{
	uint64 be_data = B_HOST_TO_BENDIAN_INT64(data);
	return AppendData((const void*)&be_data, sizeof(uint64));
}


/** @brief Reads an 8-bit signed integer from the head of the buffer. */
status_t
BNetBuffer::RemoveInt8(int8& data)
{
	return RemoveData((void*)&data, sizeof(int8));
}


/** @brief Reads an 8-bit unsigned integer from the head of the buffer. */
status_t
BNetBuffer::RemoveUint8(uint8& data)
{
	return RemoveData((void*)&data, sizeof(uint8));
}


/** @brief Reads a 16-bit signed integer and converts it to host byte order. */
status_t
BNetBuffer::RemoveInt16(int16& data)
{
	int16 be_data;
	status_t result = RemoveData((void*)&be_data, sizeof(int16));
	if (result != B_OK)
		return result;

	data = B_BENDIAN_TO_HOST_INT16(be_data);

	return B_OK;
}


/** @brief Reads a 16-bit unsigned integer and converts it to host byte order. */
status_t
BNetBuffer::RemoveUint16(uint16& data)
{
	uint16 be_data;
	status_t result = RemoveData((void*)&be_data, sizeof(uint16));
	if (result != B_OK)
		return result;

	data = B_BENDIAN_TO_HOST_INT16(be_data);

	return B_OK;
}


/** @brief Reads a 32-bit signed integer and converts it to host byte order. */
status_t
BNetBuffer::RemoveInt32(int32& data)
{
	int32 be_data;
	status_t result = RemoveData((void*)&be_data, sizeof(int32));
	if (result != B_OK)
		return result;

	data = B_BENDIAN_TO_HOST_INT32(be_data);

	return B_OK;
}


/** @brief Reads a 32-bit unsigned integer and converts it to host byte order. */
status_t
BNetBuffer::RemoveUint32(uint32& data)
{
	uint32 be_data;
	status_t result = RemoveData((void*)&be_data, sizeof(uint32));
	if (result != B_OK)
		return result;

	data = B_BENDIAN_TO_HOST_INT32(be_data);

	return B_OK;
}


/** @brief Reads a single-precision float from the head of the buffer. */
status_t
BNetBuffer::RemoveFloat(float& data)
{
	return RemoveData((void*)&data, sizeof(float));
}


/** @brief Reads a double-precision float from the head of the buffer. */
status_t
BNetBuffer::RemoveDouble(double& data)
{
	return RemoveData((void*)&data, sizeof(double));
}


/** @brief Reads up to @a size bytes as a string payload.
 *  @param data Destination buffer.
 *  @param size Number of bytes to read (including any terminator). */
status_t
BNetBuffer::RemoveString(char* data, size_t size)
{
	// TODO(bga): Should we do anything specific to handle the terminating
	// NULL byte?
	return RemoveData((void*)data, size);
}


/** @brief Reads a raw byte blob from the head of the buffer.
 *  @param data Destination buffer.
 *  @param size Exact number of bytes to read.
 *  @return B_OK on success, B_BUFFER_OVERFLOW if fewer bytes are available,
 *          or B_NO_INIT if the buffer is not constructed. */
status_t
BNetBuffer::RemoveData(void* data, size_t size)
{
	if (fInit != B_OK)
		return B_NO_INIT;

	ssize_t bytesRead = fImpl->Read(data, size);
	if (bytesRead < 0)
		return (status_t)bytesRead;
	return (size_t)bytesRead == size ? B_OK : B_BUFFER_OVERFLOW;
}


/** @brief Unflattens a BMessage previously written with AppendMessage().
 *  @param data On success, receives the unflattened BMessage.
 *  @return B_OK on success, B_ERROR if the header type is not B_MESSAGE_TYPE,
 *          or another error code from the underlying read. */
status_t
BNetBuffer::RemoveMessage(BMessage& data)
{
	if (fInit != B_OK)
		return B_NO_INIT;

	unsigned char* bufferPtr = fImpl->Data();

	if (*(int32*)bufferPtr != B_MESSAGE_TYPE)
		return B_ERROR;

	bufferPtr += sizeof(int32);
	int32 dataSize = *(int32*)bufferPtr;

	char* flattenedData = new (std::nothrow) char[dataSize];
	if (flattenedData == NULL)
		return B_NO_MEMORY;

	status_t result = RemoveData(flattenedData, dataSize);
	if (result == B_OK)
		result = data.Unflatten(flattenedData);

	delete[] flattenedData;

	return result;
}


/** @brief Reads a 64-bit signed integer and converts it to host byte order. */
status_t
BNetBuffer::RemoveInt64(int64& data)
{
	int64 be_data;
	status_t result = RemoveData((void*)&be_data, sizeof(int64));
	if (result != B_OK)
		return result;

	data = B_BENDIAN_TO_HOST_INT64(be_data);

	return B_OK;
}


/** @brief Reads a 64-bit unsigned integer and converts it to host byte order. */
status_t
BNetBuffer::RemoveUint64(uint64& data)
{
	uint64 be_data;
	status_t result = RemoveData((void*)&be_data, sizeof(uint64));
	if (result != B_OK)
		return result;

	data = B_BENDIAN_TO_HOST_INT64(be_data);

	return B_OK;
}


/** @brief Returns a raw pointer to the first unread byte, or NULL. */
unsigned char*
BNetBuffer::Data() const
{
	if (fInit != B_OK)
		return NULL;

	return fImpl->Data();
}


/** @brief Returns the number of bytes currently buffered. */
size_t
BNetBuffer::Size() const
{
	if (fInit != B_OK)
		return 0;

	return fImpl->Size();
}


/** @brief Returns the number of bytes of free space after the write cursor. */
size_t
BNetBuffer::BytesRemaining() const
{
	if (fInit != B_OK)
		return 0;

	return fImpl->BytesRemaining();
}


void
BNetBuffer::_ReservedBNetBufferFBCCruft1()
{
}


void
BNetBuffer::_ReservedBNetBufferFBCCruft2()
{
}


void
BNetBuffer::_ReservedBNetBufferFBCCruft3()
{
}


void
BNetBuffer::_ReservedBNetBufferFBCCruft4()
{
}


void
BNetBuffer::_ReservedBNetBufferFBCCruft5()
{
}


void
BNetBuffer::_ReservedBNetBufferFBCCruft6()
{
}
