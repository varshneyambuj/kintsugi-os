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
 */

/** @file HttpSerializer.h
    @brief State-machine wrapper that emits the bytes of a BHttpRequest —
           header first, then body — through an HttpBuffer staging area into
           a target BDataIO. */

#ifndef _B_HTTP_SERIALIZER_H_
#define _B_HTTP_SERIALIZER_H_


#include <functional>
#include <optional>

class BDataIO;

namespace BPrivate {

namespace Network {

class BHttpRequest;
class HttpBuffer;

/** @brief Callback used by HttpBuffer to ship bytes to a downstream sink. */
using HttpTransferFunction = std::function<size_t(const std::byte*, size_t)>;


/** @brief States of the HttpSerializer transmission machine. */
enum class HttpSerializerState { Uninitialized, Header, ChunkHeader, Body, Done };


/** @brief Drives the byte-level transmission of a BHttpRequest, switching
           between header and body phases and tracking how much body has
           been pushed downstream. */
class HttpSerializer
{
public:
	/** @brief Construct an uninitialised serializer; call SetTo() before use. */
								HttpSerializer(){};

			void				SetTo(HttpBuffer& buffer, const BHttpRequest& request);
			bool				IsInitialized() const noexcept;

			size_t				Serialize(HttpBuffer& buffer, BDataIO* target);

			std::optional<off_t> BodyBytesTotal() const noexcept;
			off_t				BodyBytesTransferred() const noexcept;
			bool				Complete() const noexcept;

private:
			bool				_IsChunked() const noexcept;
			size_t				_WriteToTarget(HttpBuffer& buffer, BDataIO* target) const;

private:
			HttpSerializerState	fState = HttpSerializerState::Uninitialized;
			BDataIO*			fBody = nullptr;
			off_t				fTransferredBodySize = 0;
			std::optional<off_t> fBodySize;
};


/** @brief Whether SetTo() has been called and the serializer is ready to run. */
inline bool
HttpSerializer::IsInitialized() const noexcept
{
	return fState != HttpSerializerState::Uninitialized;
}


/** @brief Total body byte count if known, or std::nullopt for chunked transfers. */
inline std::optional<off_t>
HttpSerializer::BodyBytesTotal() const noexcept
{
	return fBodySize;
}


/** @brief Number of body bytes pushed to the target so far. */
inline off_t
HttpSerializer::BodyBytesTransferred() const noexcept
{
	return fTransferredBodySize;
}


/** @brief Whether the serializer has finished transmitting the entire request. */
inline bool
HttpSerializer::Complete() const noexcept
{
	return fState == HttpSerializerState::Done;
}


} // namespace Network

} // namespace BPrivate

#endif // _B_HTTP_SERIALIZER_H_
