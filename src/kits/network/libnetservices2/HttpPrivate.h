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

/** @file HttpPrivate.h
    @brief Internal helpers shared across the libnetservices2 HTTP
           implementation, including token-syntax validation per RFC 7230. */

#ifndef _B_HTTP_PRIVATE_H_
#define _B_HTTP_PRIVATE_H_

#include <string_view>

#include <HttpRequest.h>
#include <Url.h>


namespace BPrivate {

namespace Network {

/**
 * @brief Validate whether @a string conforms to an HTTP token value.
 *
 * RFC 7230 section 3.2.6 determines that valid tokens for header names are:
 * !#$%&'*+=.^_`|~, any digits or alpha.
 *
 * @param string  Candidate header-name token.
 * @return true if @a string is a valid HTTP token, false otherwise.
 */
static inline bool
validate_http_token_string(const std::string_view& string)
{
	for (auto it = string.cbegin(); it < string.cend(); it++) {
		if (*it <= 31 || *it == 127 || *it == '(' || *it == ')' || *it == '<' || *it == '>'
			|| *it == '@' || *it == ',' || *it == ';' || *it == '\\' || *it == '"' || *it == '/'
			|| *it == '[' || *it == ']' || *it == '?' || *it == '=' || *it == '{' || *it == '}'
			|| *it == ' ')
			return false;
	}
	return true;
}


} // namespace Network

} // namespace BPrivate

#endif // _B_HTTP_PRIVATE_H_
