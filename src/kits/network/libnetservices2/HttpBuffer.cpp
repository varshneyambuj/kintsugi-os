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
 * @file HttpBuffer.cpp
 * @brief Implementation of HttpBuffer, an internal byte-vector for HTTP I/O.
 *
 * HttpBuffer manages a fixed-capacity std::vector<std::byte> with a current
 * read offset, supporting line-oriented parsing for HTTP headers, chunked
 * writes to an arbitrary transfer function, and streaming reads from a
 * BDataIO source.
 *
 * @see HttpParser, HttpSerializer
 */


#include "HttpBuffer.h"

#include <DataIO.h>
#include <NetServicesDefs.h>
#include <String.h>

using namespace BPrivate::Network;


/**
 * @brief Newline sequence as per HTTP RFC — CR LF.
 */
static constexpr std::array<std::byte, 2> kNewLine = {std::byte('\r'), std::byte('\n')};


/**
 * @brief Construct a new HttpBuffer with the given \a capacity.
 *
 * @param capacity  Initial reserved capacity in bytes for the internal vector.
 */
HttpBuffer::HttpBuffer(size_t capacity)
{
	fBuffer.reserve(capacity);
};


/**
 * @brief Read available data from \a source into the buffer's spare capacity.
 *
 * Calls Flush() first to remove consumed bytes, then reads up to the
 * remaining capacity (or \a maxSize if smaller) from \a source.
 *
 * @param source   BDataIO to read from.
 * @param maxSize  Optional cap on the number of bytes to read in one call.
 * @return Number of bytes read (>=0), B_WOULD_BLOCK if source would block,
 *         or throws BNetworkRequestError on other read errors.
 */
ssize_t
HttpBuffer::ReadFrom(BDataIO* source, std::optional<size_t> maxSize)
{
	// Remove any unused bytes at the beginning of the buffer
	Flush();

	auto currentSize = fBuffer.size();
	auto remainingBufferSize = fBuffer.capacity() - currentSize;

	if (maxSize && maxSize.value() < remainingBufferSize)
		remainingBufferSize = maxSize.value();

	// Adjust the buffer to the maximum size
	fBuffer.resize(fBuffer.capacity());

	ssize_t bytesRead = B_INTERRUPTED;
	while (bytesRead == B_INTERRUPTED)
		bytesRead = source->Read(fBuffer.data() + currentSize, remainingBufferSize);

	if (bytesRead == B_WOULD_BLOCK || bytesRead == 0) {
		fBuffer.resize(currentSize);
		return bytesRead;
	} else if (bytesRead < 0) {
		throw BNetworkRequestError(
			"BDataIO::Read()", BNetworkRequestError::NetworkError, bytesRead);
	}

	// Adjust the buffer to the current size
	fBuffer.resize(currentSize + bytesRead);

	return bytesRead;
}


/**
 * @brief Write buffered data through the transfer function \a func.
 *
 * Passes a pointer to the unread data and its size to \a func; advances
 * the internal offset by the number of bytes \a func reports as written.
 *
 * @param func     Callable that performs the actual write; receives
 *                 (data pointer, byte count) and returns bytes written.
 * @param maxSize  Optional cap on the number of bytes offered to \a func.
 * @return Number of bytes consumed by \a func.
 */
size_t
HttpBuffer::WriteTo(HttpTransferFunction func, std::optional<size_t> maxSize)
{
	if (RemainingBytes() == 0)
		return 0;

	auto size = RemainingBytes();
	if (maxSize.has_value() && *maxSize < size)
		size = *maxSize;

	auto bytesWritten = func(fBuffer.data() + fCurrentOffset, size);
	if (bytesWritten > size)
		throw BRuntimeError(__PRETTY_FUNCTION__, "More bytes written than were made available");

	fCurrentOffset += bytesWritten;

	return bytesWritten;
}


/**
 * @brief Extract the next CRLF-terminated line from the buffer.
 *
 * Searches from the current offset for the first \r\n sequence and
 * returns everything up to (but not including) the newline.  The offset
 * is advanced past the newline so the next call returns the following line.
 * Call Flush() after processing all needed lines to reclaim memory.
 *
 * @return A BString containing the line (without CRLF), or std::nullopt if
 *         no complete line is available yet.
 */
std::optional<BString>
HttpBuffer::GetNextLine()
{
	auto offset = fBuffer.cbegin() + fCurrentOffset;
	auto result = std::search(offset, fBuffer.cend(), kNewLine.cbegin(), kNewLine.cend());
	if (result == fBuffer.cend())
		return std::nullopt;

	BString line(
		reinterpret_cast<const char*>(std::addressof(*offset)), std::distance(offset, result));
	fCurrentOffset = std::distance(fBuffer.cbegin(), result) + 2;
	return line;
}


/**
 * @brief Return the number of unread bytes remaining in the buffer.
 *
 * @return Byte count from the current read offset to the end of valid data.
 */
size_t
HttpBuffer::RemainingBytes() const noexcept
{
	return fBuffer.size() - fCurrentOffset;
}


/**
 * @brief Compact the buffer by erasing already-consumed bytes at the front.
 *
 * Moves remaining data to the beginning of the internal vector and resets
 * the read offset to zero, freeing capacity for further reads.
 */
void
HttpBuffer::Flush() noexcept
{
	if (fCurrentOffset > 0) {
		auto end = fBuffer.cbegin() + fCurrentOffset;
		fBuffer.erase(fBuffer.cbegin(), end);
		fCurrentOffset = 0;
	}
}


/**
 * @brief Discard all data in the buffer and reset the read offset.
 */
void
HttpBuffer::Clear() noexcept
{
	fBuffer.clear();
	fCurrentOffset = 0;
}


/**
 * @brief Return a string_view over the current unread data.
 *
 * @return std::string_view over valid bytes from the current offset, or an
 *         empty view if no data is available.
 */
std::string_view
HttpBuffer::Data() const noexcept
{
	if (RemainingBytes() > 0) {
		return std::string_view(
			reinterpret_cast<const char*>(fBuffer.data()) + fCurrentOffset, RemainingBytes());
	} else
		return std::string_view();
}


/**
 * @brief Append string_view data to the buffer, throwing on overflow.
 *
 * @param data  The string_view to append.
 * @return Reference to this buffer (for chaining).
 */
HttpBuffer&
HttpBuffer::operator<<(const std::string_view& data)
{
	if (data.size() > (fBuffer.capacity() - fBuffer.size())) {
		throw BNetworkRequestError(__PRETTY_FUNCTION__, BNetworkRequestError::ProtocolError,
			"No capacity left in buffer to append data.");
	}

	for (const auto& character: data)
		fBuffer.push_back(static_cast<const std::byte>(character));

	return *this;
}
