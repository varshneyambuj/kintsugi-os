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
 *   Copyright 2021 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Niels Sascha Reedijk, niels.reedijk@gmail.com
 */


/**
 * @file NetServicesMisc.cpp
 * @brief Miscellaneous error types, utility functions, and message constants for the Network
 *        Services kit.
 *
 * Defines BUnsupportedProtocol, BInvalidUrl, and BNetworkRequestError exception
 * classes, the base64 encoding helper used for HTTP authentication, the
 * UrlEventData string constants for BMessage-based URL event notifications,
 * and the atomic request-identifier counter.
 *
 * @see BHttpSession, BHttpRequest, ErrorsExt.h
 */


#include <NetServicesDefs.h>


namespace BPrivate {

namespace Network {


// #pragma mark -- BUnsupportedProtocol


/**
 * @brief Construct a BUnsupportedProtocol error with a C-string origin.
 *
 * @param origin              Null-terminated origin identifier string.
 * @param url                 The URL whose protocol is not supported.
 * @param supportedProtocols  List of protocol strings that are accepted.
 */
BUnsupportedProtocol::BUnsupportedProtocol(
	const char* origin, BUrl url, BStringList supportedProtocols)
	:
	BError(origin),
	fUrl(std::move(url)),
	fSupportedProtocols(std::move(supportedProtocols))
{
}


/**
 * @brief Construct a BUnsupportedProtocol error with a BString origin.
 *
 * @param origin              BString origin identifier.
 * @param url                 The URL whose protocol is not supported.
 * @param supportedProtocols  List of protocol strings that are accepted.
 */
BUnsupportedProtocol::BUnsupportedProtocol(BString origin, BUrl url, BStringList supportedProtocols)
	:
	BError(std::move(origin)),
	fUrl(std::move(url)),
	fSupportedProtocols(std::move(supportedProtocols))
{
}


/**
 * @brief Return the human-readable error message.
 *
 * @return "Unsupported Protocol".
 */
const char*
BUnsupportedProtocol::Message() const noexcept
{
	return "Unsupported Protocol";
}


/**
 * @brief Return a const reference to the URL that triggered this error.
 *
 * @return Const reference to the stored BUrl.
 */
const BUrl&
BUnsupportedProtocol::Url() const
{
	return fUrl;
}


/**
 * @brief Return the list of protocol strings supported by the caller.
 *
 * @return Const reference to the BStringList of supported protocol names.
 */
const BStringList&
BUnsupportedProtocol::SupportedProtocols() const
{
	return fSupportedProtocols;
}


// #pragma mark -- BInvalidUrl


/**
 * @brief Construct a BInvalidUrl error with a C-string origin.
 *
 * @param origin  Null-terminated origin identifier string.
 * @param url     The malformed or otherwise invalid URL.
 */
BInvalidUrl::BInvalidUrl(const char* origin, BUrl url)
	:
	BError(origin),
	fUrl(std::move(url))
{
}


/**
 * @brief Construct a BInvalidUrl error with a BString origin.
 *
 * @param origin  BString origin identifier.
 * @param url     The malformed or otherwise invalid URL.
 */
BInvalidUrl::BInvalidUrl(BString origin, BUrl url)
	:
	BError(std::move(origin)),
	fUrl(std::move(url))
{
}


/**
 * @brief Return the human-readable error message.
 *
 * @return "Invalid URL".
 */
const char*
BInvalidUrl::Message() const noexcept
{
	return "Invalid URL";
}


/**
 * @brief Return a const reference to the invalid URL.
 *
 * @return Const reference to the stored BUrl.
 */
const BUrl&
BInvalidUrl::Url() const
{
	return fUrl;
}


// #pragma mark -- BNetworkRequestError


/**
 * @brief Construct a BNetworkRequestError with a status code and optional custom message.
 *
 * @param origin        Null-terminated origin identifier string.
 * @param type          Error category (HostnameError, NetworkError, ProtocolError, etc.).
 * @param errorCode     Underlying Haiku status_t code; use B_OK if not applicable.
 * @param customMessage Additional diagnostic text appended to DebugMessage().
 */
BNetworkRequestError::BNetworkRequestError(
	const char* origin, ErrorType type, status_t errorCode, const BString& customMessage)
	:
	BError(origin),
	fErrorType(type),
	fErrorCode(errorCode),
	fCustomMessage(customMessage)
{
}


/**
 * @brief Construct a BNetworkRequestError without a status code.
 *
 * @param origin        Null-terminated origin identifier string.
 * @param type          Error category.
 * @param customMessage Additional diagnostic text.
 */
BNetworkRequestError::BNetworkRequestError(
	const char* origin, ErrorType type, const BString& customMessage)
	:
	BError(origin),
	fErrorType(type),
	fCustomMessage(customMessage)
{
}


/**
 * @brief Return a short description of the error category.
 *
 * @return Null-terminated string describing the error type.
 */
const char*
BNetworkRequestError::Message() const noexcept
{
	switch (fErrorType) {
		case HostnameError:
			return "Cannot resolving hostname";
		case NetworkError:
			return "Network error during operation";
		case ProtocolError:
			return "Protocol error";
		case SystemError:
			return "System error";
		case Canceled:
			return "Network request was canceled";
	}
	// Unreachable
	return "Network request error";
}


/**
 * @brief Build a verbose debug message including error code and custom text.
 *
 * @return BString combining the base debug message with system error code
 *         (if non-OK) and the custom message (if non-empty).
 */
BString
BNetworkRequestError::DebugMessage() const
{
	BString debugMessage;
	debugMessage << "[" << Origin() << "] " << Message();
	if (fErrorCode != B_OK) {
		debugMessage << "\n\tUnderlying System Error: " << fErrorCode << " ("
					 << strerror(fErrorCode) << ")";
	}
	if (fCustomMessage.Length() > 0) {
		debugMessage << "\n\tAdditional Info: " << fCustomMessage;
	}
	return debugMessage;
}


/**
 * @brief Return the error category enum value.
 *
 * @return The ErrorType passed at construction time.
 */
BNetworkRequestError::ErrorType
BNetworkRequestError::Type() const noexcept
{
	return fErrorType;
}


/**
 * @brief Return the underlying Haiku status_t error code.
 *
 * @return The status_t value, or B_OK if none was provided.
 */
status_t
BNetworkRequestError::ErrorCode() const noexcept
{
	return fErrorCode;
}


/**
 * @brief Return the custom diagnostic message.
 *
 * @return Null-terminated custom message string, or an empty string if none.
 */
const char*
BNetworkRequestError::CustomMessage() const noexcept
{
	return fCustomMessage.String();
}


// #pragma mark -- Public functions


/** @brief URL-safe base-64 symbol table (RFC 4648 §5). */
static const char* kBase64Symbols
	= "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";


/**
 * @brief Encode a BString to URL-safe base-64.
 *
 * Processes the input three bytes at a time and appends '=' padding when
 * the length is not a multiple of three.
 *
 * @param string  The raw input string to encode.
 * @return Base-64 encoded BString.
 */
BString
encode_to_base64(const BString& string)
{
	BString result;
	BString tmpString = string;

	while (tmpString.Length()) {
		char in[3] = {0, 0, 0};
		char out[4] = {0, 0, 0, 0};
		int8 remaining = tmpString.Length();

		tmpString.MoveInto(in, 0, 3);

		out[0] = (in[0] & 0xFC) >> 2;
		out[1] = ((in[0] & 0x03) << 4) | ((in[1] & 0xF0) >> 4);
		out[2] = ((in[1] & 0x0F) << 2) | ((in[2] & 0xC0) >> 6);
		out[3] = in[2] & 0x3F;

		for (int i = 0; i < 4; i++)
			out[i] = kBase64Symbols[(int) out[i]];

		//  Add padding if the input length is not a multiple
		// of 3
		switch (remaining) {
			case 1:
				out[2] = '=';
				// Fall through
			case 2:
				out[3] = '=';
				break;
		}

		result.Append(out, 4);
	}

	return result;
}


// #pragma mark -- message constants
namespace UrlEventData {
/** @brief BMessage field name for the request identifier. */
const char* Id = "url:identifier";

/** @brief BMessage field name for the resolved hostname. */
const char* HostName = "url:hostname";

/** @brief BMessage field name for the number of bytes transferred so far. */
const char* NumBytes = "url:numbytes";

/** @brief BMessage field name for the total expected byte count. */
const char* TotalBytes = "url:totalbytes";

/** @brief BMessage field name for the success flag. */
const char* Success = "url:success";

/** @brief BMessage field name for the debug event type. */
const char* DebugType = "url:debugtype";

/** @brief BMessage field name for the debug event message text. */
const char* DebugMessage = "url:debugmessage";
} // namespace UrlEventData


// #pragma mark -- Private functions and data


/** @brief Atomic counter used to assign unique request identifiers. */
static int32 gRequestIdentifier = 1;


/**
 * @brief Return the next globally unique network-request identifier.
 *
 * Uses atomic_add to ensure uniqueness across threads without locking.
 *
 * @return A monotonically increasing int32 identifier starting from 1.
 */
int32
get_netservices_request_identifier()
{
	return atomic_add(&gRequestIdentifier, 1);
}


} // namespace Network

} // namespace BPrivate
