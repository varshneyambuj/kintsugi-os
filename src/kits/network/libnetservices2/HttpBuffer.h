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

/** @file HttpBuffer.h
    @brief Resizable byte staging buffer used by the HTTP serializer and
           parser for incremental I/O between sockets and BDataIO sinks. */

#ifndef _B_HTTP_BUFFER_H_
#define _B_HTTP_BUFFER_H_

#include <functional>
#include <optional>
#include <string_view>
#include <vector>

class BDataIO;
class BString;


namespace BPrivate {

namespace Network {

/** @brief Callback signature used by HttpBuffer::WriteTo() and friends to
           push staged bytes into a downstream sink. */
using HttpTransferFunction = std::function<size_t(const std::byte*, size_t)>;


/** @brief Growable byte buffer with a tracked read offset, used to bridge
           the network stream and the HTTP parser/serializer state machines. */
class HttpBuffer
{
public:
								HttpBuffer(size_t capacity = 8 * 1024);

			ssize_t				ReadFrom(BDataIO* source,
									std::optional<size_t> maxSize = std::nullopt);
			size_t				WriteTo(HttpTransferFunction func,
									std::optional<size_t> maxSize = std::nullopt);
			void				WriteExactlyTo(HttpTransferFunction func,
									std::optional<size_t> maxSize = std::nullopt);
			std::optional<BString> GetNextLine();

			size_t				RemainingBytes() const noexcept;

			void				Flush() noexcept;
			void				Clear() noexcept;

			std::string_view	Data() const noexcept;

	// load data into the buffer
			HttpBuffer&			operator<<(const std::string_view& data);

private:
			std::vector<std::byte> fBuffer;
			size_t				fCurrentOffset = 0;
};


} // namespace Network

} // namespace BPrivate

#endif // _B_HTTP_BUFFER_H_
