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
 *   Copyright 2010-2017 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Christophe Huriaux, c.huriaux@gmail.com
 *       Adrien Destugues, pulkomandy@pulkomandy.tk
 */


/**
 * @file HttpResult.cpp
 * @brief Implementation of BHttpResult, the HTTP response metadata container.
 *
 * Stores the response URL (which may differ from the request URL after
 * redirects), the full set of response headers, the numeric status code, and
 * the status reason phrase. Convenience accessors parse the Content-Type and
 * Content-Length headers on demand.
 *
 * @see BHttpRequest, BHttpHeaders
 */

#include <HttpResult.h>

#include <errno.h>
#include <Debug.h>

using namespace BPrivate::Network;


/**
 * @brief Construct a BHttpResult for the given request URL.
 *
 * @param url  The URL that was requested; stored as the initial response URL.
 */
BHttpResult::BHttpResult(const BUrl& url)
	:
	fUrl(url),
	fHeaders(),
	fStatusCode(0)
{
}


/**
 * @brief Copy constructor — deep-copies URL, headers, status code, and text.
 *
 * @param other  The source BHttpResult to copy.
 */
BHttpResult::BHttpResult(const BHttpResult& other)
	:
	fUrl(other.fUrl),
	fHeaders(other.fHeaders),
	fStatusCode(other.fStatusCode),
	fStatusString(other.fStatusString)
{
}


/**
 * @brief Destructor.
 */
BHttpResult::~BHttpResult()
{
}


// #pragma mark Result parameters modifications


/**
 * @brief Update the result URL (used after a redirect is followed).
 *
 * @param url  The new URL reflecting the final response location.
 */
void
BHttpResult::SetUrl(const BUrl& url)
{
	fUrl = url;
}


// #pragma mark Result parameters access


/**
 * @brief Return the URL associated with this result.
 *
 * @return A const reference to the response BUrl.
 */
const BUrl&
BHttpResult::Url() const
{
	return fUrl;
}


/**
 * @brief Return the value of the Content-Type response header.
 *
 * @return A BString containing the Content-Type value, or an empty string
 *         if the header is not present.
 */
BString
BHttpResult::ContentType() const
{
	return Headers()["Content-Type"];
}


/**
 * @brief Parse and return the Content-Length response header value.
 *
 * Validates that the value contains only decimal digits (RFC 7230 compliance)
 * and converts it with strtoull(). Returns 0 if the header is absent, empty,
 * starts with a sign character, or overflows.
 *
 * @return The content length in bytes, or 0 if unknown or invalid.
 */
off_t
BHttpResult::Length() const
{
	const char* length = Headers()["Content-Length"];
	if (length == NULL)
		return 0;

	/* NOTE: Not RFC7230 compliant:
	 * - If Content-Length is a list, all values must be checked and verified
	 *   to be duplicates of each other, but this is currently not supported.
	 */
	off_t result = 0;
	/* strtoull() will ignore a prefixed sign, so we verify that there aren't
	 * any before continuing (RFC7230 only permits digits).
	 *
	 * We can check length[0] directly because header values are trimmed by
	 * HttpHeader beforehand. */
	if (length[0] != '-' && length[0] != '+') {
		errno = 0;
		char *endptr = NULL;
		result = strtoull(length, &endptr, 10);
		/* ERANGE will be signalled if the result is too large (which can
		 * happen), in that case, return 0. */
		if (errno != 0 || *endptr != '\0')
			result = 0;
	}
	return result;
}


/**
 * @brief Return the full set of response headers.
 *
 * @return A const reference to the BHttpHeaders collection.
 */
const BHttpHeaders&
BHttpResult::Headers() const
{
	return fHeaders;
}


/**
 * @brief Return the HTTP response status code.
 *
 * @return The three-digit status code (e.g. 200, 404), or 0 if not yet set.
 */
int32
BHttpResult::StatusCode() const
{
	return fStatusCode;
}


/**
 * @brief Return the HTTP response status reason phrase.
 *
 * @return A const reference to the status text BString (e.g. "OK", "Not Found").
 */
const BString&
BHttpResult::StatusText() const
{
	return fStatusString;
}


// #pragma mark Result tests


/**
 * @brief Return whether any response headers have been received.
 *
 * @return true if the header collection is non-empty, false otherwise.
 */
bool
BHttpResult::HasHeaders() const
{
	return fHeaders.CountHeaders() > 0;
}


// #pragma mark Overloaded members


/**
 * @brief Assignment operator — replaces all fields with those from \a other.
 *
 * @param other  The source BHttpResult to copy from.
 * @return A reference to this object.
 */
BHttpResult&
BHttpResult::operator=(const BHttpResult& other)
{
	if (this == &other)
		return *this;

	fUrl = other.fUrl;
	fHeaders = other.fHeaders;
	fStatusCode = other.fStatusCode;
	fStatusString = other.fStatusString;

	return *this;
}
