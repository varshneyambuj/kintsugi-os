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
 * @file HttpSerializer.cpp
 * @brief Implementation of HttpSerializer, the HTTP request sender state machine.
 *
 * HttpSerializer serialises a BHttpRequest in stages: first the header bytes
 * buffered by BHttpRequest::SerializeHeaderTo(), then the body bytes read
 * incrementally from the BDataIO body source.  It drives the transmission
 * loop in BHttpSession and tracks how many body bytes have been sent.
 *
 * @see BHttpRequest, BHttpSession, HttpBuffer
 */


#include "HttpSerializer.h"

#include <DataIO.h>
#include <HttpRequest.h>
#include <NetServicesDefs.h>

#include "HttpBuffer.h"

using namespace std::literals;
using namespace BPrivate::Network;


/**
 * @brief Prepare the serialiser with \a request and load the header into \a buffer.
 *
 * Clears \a buffer, calls BHttpRequest::SerializeHeaderTo() to write the
 * header section, and stores a reference to the request body source (if any).
 *
 * @param buffer   HttpBuffer to receive the serialised header bytes.
 * @param request  The BHttpRequest to serialise.
 */
void
HttpSerializer::SetTo(HttpBuffer& buffer, const BHttpRequest& request)
{
	buffer.Clear();
	request.SerializeHeaderTo(buffer);
	fState = HttpSerializerState::Header;

	if (auto requestBody = request.RequestBody()) {
		fBody = requestBody->input.get();
		if (requestBody->size) {
			fBodySize = *(requestBody->size);
		}
	}
}


/**
 * @brief Transmit the next chunk of request data to \a target.
 *
 * Drives the serialiser state machine: first flushes any buffered header bytes,
 * then reads body bytes from the source BDataIO into \a buffer and forwards
 * them to \a target.  Each call may write a partial amount; the caller should
 * repeat until the state reaches Done.
 *
 * @param buffer  HttpBuffer used for intermediate staging of data.
 * @param target  BDataIO to write the serialised bytes to.
 * @return Number of body bytes written during this call (header bytes not counted).
 */
size_t
HttpSerializer::Serialize(HttpBuffer& buffer, BDataIO* target)
{
	bool finishing = false;
	size_t bodyBytesWritten = 0;
	while (!finishing) {
		switch (fState) {
			case HttpSerializerState::Uninitialized:
				throw BRuntimeError(__PRETTY_FUNCTION__, "Invalid state: Uninitialized");

			case HttpSerializerState::Header:
				_WriteToTarget(buffer, target);
				if (buffer.RemainingBytes() > 0) {
					// There are more bytes to be processed; wait for the next iteration
					return 0;
				}

				if (fBody == nullptr) {
					fState = HttpSerializerState::Done;
					return 0;
				} else if (_IsChunked())
					// fState = HttpSerializerState::ChunkHeader;
					throw BRuntimeError(
						__PRETTY_FUNCTION__, "Chunked serialization not implemented");
				else
					fState = HttpSerializerState::Body;
				break;

			case HttpSerializerState::Body:
			{
				auto bytesWritten = _WriteToTarget(buffer, target);
				bodyBytesWritten += bytesWritten;
				fTransferredBodySize += bytesWritten;
				if (buffer.RemainingBytes() > 0) {
					// did not manage to write all the bytes in the buffer; continue in the next
					// round
					finishing = true;
					break;
				}

				if (fBodySize && fBodySize.value() == fTransferredBodySize) {
					fState = HttpSerializerState::Done;
					finishing = true;
				}
				break;
			}

			case HttpSerializerState::Done:
			default:
				finishing = true;
				continue;
		}

		// Load more data into the buffer
		std::optional<size_t> maxReadSize = std::nullopt;
		if (fBodySize)
			maxReadSize = fBodySize.value() - fTransferredBodySize;
		buffer.ReadFrom(fBody, maxReadSize);
	}

	return bodyBytesWritten;
}


/**
 * @brief Return whether this request uses chunked transfer encoding.
 *
 * Chunked encoding is indicated by the absence of a known body size.
 *
 * @return true if the body size is unknown (chunked), false if it is fixed.
 */
bool
HttpSerializer::_IsChunked() const noexcept
{
	return fBodySize == std::nullopt;
}


/**
 * @brief Write buffered bytes from \a buffer to \a target, retrying on EINTR.
 *
 * Uses an HttpBuffer::WriteTo() lambda that calls target->Write() in a
 * B_INTERRUPTED retry loop.  Throws BNetworkRequestError on any error other
 * than B_WOULD_BLOCK.
 *
 * @param buffer  HttpBuffer whose current data is to be written.
 * @param target  BDataIO to write to.
 * @return Total number of bytes successfully written.
 */
size_t
HttpSerializer::_WriteToTarget(HttpBuffer& buffer, BDataIO* target) const
{
	size_t bytesWritten = 0;
	buffer.WriteTo([target, &bytesWritten](const std::byte* buffer, size_t size) {
		ssize_t result = B_INTERRUPTED;
		while (result == B_INTERRUPTED) {
			result = target->Write(buffer, size);
		}

		if (result <= 0 && result != B_WOULD_BLOCK) {
			throw BNetworkRequestError(
				__PRETTY_FUNCTION__, BNetworkRequestError::NetworkError, result);
		} else if (result > 0) {
			bytesWritten += result;
			return size_t(result);
		} else {
			return size_t(0);
		}
	});

	return bytesWritten;
}
