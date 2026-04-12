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
 *   Copyright 2013-2017 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Adrien Destugues, pulkomandy@pulkomandy.tk
 */


/**
 * @file UrlResult.cpp
 * @brief Implementation of BUrlResult, the base URL response metadata class.
 *
 * Stores the content type and total byte length of a URL response. This base
 * class is extended by BHttpResult which adds HTTP-specific fields such as
 * status code and response headers.
 *
 * @see BHttpResult, BUrlRequest
 */


#include <UrlResult.h>


using namespace BPrivate::Network;


/**
 * @brief Default constructor — creates a BUrlResult with no content type and zero length.
 */
BUrlResult::BUrlResult()
	:
	fContentType(),
	fLength(0)
{
}


/**
 * @brief Destructor.
 */
BUrlResult::~BUrlResult()
{
}


/**
 * @brief Set the MIME content type of the response body.
 *
 * @param contentType  The content type string (e.g. "text/html;charset=UTF-8").
 */
void
BUrlResult::SetContentType(BString contentType)
{
	fContentType = contentType;
}


/**
 * @brief Set the total byte length of the response body.
 *
 * @param length  The number of bytes in the response body.
 */
void
BUrlResult::SetLength(off_t length)
{
	fLength = length;
}


/**
 * @brief Return the MIME content type of the response body.
 *
 * @return A BString containing the content type, or an empty string if not set.
 */
BString
BUrlResult::ContentType() const
{
	return fContentType;
}


/**
 * @brief Return the total byte length of the response body.
 *
 * @return The length in bytes, or 0 if not set.
 */
off_t
BUrlResult::Length() const
{
	return fLength;
}
