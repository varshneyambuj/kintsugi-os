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
 * @file HttpRequest.cpp
 * @brief Implementation of BHttpMethod and BHttpRequest.
 *
 * BHttpMethod wraps either a standard HTTP verb enum or a custom method string,
 * validating custom strings against RFC 7230 token rules.  BHttpRequest holds
 * all parameters required to issue an HTTP/1.1 request — URL, method, headers,
 * authentication, body, and timing — and can serialise the request header
 * section into an HttpBuffer for transmission by HttpSerializer.
 *
 * @see BHttpSession, HttpSerializer, BHttpFields
 */


#include <HttpRequest.h>

#include <algorithm>
#include <ctype.h>
#include <sstream>
#include <utility>

#include <DataIO.h>
#include <HttpFields.h>
#include <MimeType.h>
#include <NetServicesDefs.h>
#include <Url.h>

#include "HttpBuffer.h"
#include "HttpPrivate.h"

using namespace std::literals;
using namespace BPrivate::Network;


// #pragma mark -- BHttpMethod::InvalidMethod


/**
 * @brief Construct an InvalidMethod exception for a bad custom verb string.
 *
 * @param origin  Null-terminated origin identifier string.
 * @param input   The method string that failed validation.
 */
BHttpMethod::InvalidMethod::InvalidMethod(const char* origin, BString input)
	:
	BError(origin),
	input(std::move(input))
{
}


/**
 * @brief Return a human-readable description of the validation failure.
 *
 * @return "The HTTP method cannot be empty" if input is empty, or
 *         "Unsupported characters in the HTTP method" otherwise.
 */
const char*
BHttpMethod::InvalidMethod::Message() const noexcept
{
	if (input.IsEmpty())
		return "The HTTP method cannot be empty";
	else
		return "Unsupported characters in the HTTP method";
}


/**
 * @brief Build a debug message that includes the offending method string.
 *
 * @return BString with origin, message, and the invalid input.
 */
BString
BHttpMethod::InvalidMethod::DebugMessage() const
{
	BString output = BError::DebugMessage();
	if (!input.IsEmpty())
		output << ":\t " << input << "\n";
	return output;
}


// #pragma mark -- BHttpMethod


/**
 * @brief Construct a BHttpMethod from a standard verb enum value.
 *
 * @param verb  One of the predefined BHttpMethod::Verb constants.
 */
BHttpMethod::BHttpMethod(Verb verb) noexcept
	:
	fMethod(verb)
{
}


/**
 * @brief Construct a BHttpMethod from a custom method string_view.
 *
 * Validates that \a verb is a non-empty, RFC 7230-compliant token.
 *
 * @param verb  Custom HTTP method string (e.g. "PATCH", "PROPFIND").
 */
BHttpMethod::BHttpMethod(const std::string_view& verb)
	:
	fMethod(BString(verb.data(), verb.length()))
{
	if (verb.size() == 0 || !validate_http_token_string(verb))
		throw BHttpMethod::InvalidMethod(
			__PRETTY_FUNCTION__, std::move(std::get<BString>(fMethod)));
}


/**
 * @brief Copy constructor.
 *
 * @param other  Source BHttpMethod to copy.
 */
BHttpMethod::BHttpMethod(const BHttpMethod& other) = default;


/**
 * @brief Move constructor — resets the source to the Get verb.
 *
 * @param other  Source BHttpMethod to move.
 */
BHttpMethod::BHttpMethod(BHttpMethod&& other) noexcept
	:
	fMethod(std::move(other.fMethod))
{
	other.fMethod = Get;
}


/**
 * @brief Destructor.
 */
BHttpMethod::~BHttpMethod() = default;


/**
 * @brief Copy assignment operator.
 *
 * @param other  Source BHttpMethod to copy.
 * @return Reference to this object.
 */
BHttpMethod& BHttpMethod::operator=(const BHttpMethod& other) = default;


/**
 * @brief Move assignment operator — resets the source to the Get verb.
 *
 * @param other  Source BHttpMethod to move.
 * @return Reference to this object.
 */
BHttpMethod&
BHttpMethod::operator=(BHttpMethod&& other) noexcept
{
	fMethod = std::move(other.fMethod);
	other.fMethod = Get;
	return *this;
}


/**
 * @brief Compare this method to a standard Verb enum value.
 *
 * @param other  Verb constant to compare against.
 * @return true if the methods are equivalent.
 */
bool
BHttpMethod::operator==(const BHttpMethod::Verb& other) const noexcept
{
	if (std::holds_alternative<Verb>(fMethod)) {
		return std::get<Verb>(fMethod) == other;
	} else {
		BHttpMethod otherMethod(other);
		auto otherMethodSv = otherMethod.Method();
		return std::get<BString>(fMethod).Compare(otherMethodSv.data(), otherMethodSv.size()) == 0;
	}
}


/**
 * @brief Compare this method to a standard Verb enum value for inequality.
 *
 * @param other  Verb constant to compare against.
 * @return true if the methods differ.
 */
bool
BHttpMethod::operator!=(const BHttpMethod::Verb& other) const noexcept
{
	return !operator==(other);
}


/**
 * @brief Return the HTTP method name as a string_view.
 *
 * Maps verb enum values to their canonical ASCII names ("GET", "POST", etc.)
 * or returns the custom string for non-standard methods.
 *
 * @return string_view representing the HTTP method token.
 */
const std::string_view
BHttpMethod::Method() const noexcept
{
	if (std::holds_alternative<Verb>(fMethod)) {
		switch (std::get<Verb>(fMethod)) {
			case Get:
				return "GET"sv;
			case Head:
				return "HEAD"sv;
			case Post:
				return "POST"sv;
			case Put:
				return "PUT"sv;
			case Delete:
				return "DELETE"sv;
			case Connect:
				return "CONNECT"sv;
			case Options:
				return "OPTIONS"sv;
			case Trace:
				return "TRACE"sv;
			default:
				// should never be reached
				std::abort();
		}
	} else {
		const auto& methodString = std::get<BString>(fMethod);
		// the following constructor is not noexcept, but we know we pass in valid data
		return std::string_view(methodString.String());
	}
}


// #pragma mark -- BHttpRequest::Data
/** @brief Default URL sentinel used when no URL has been set. */
static const BUrl kDefaultUrl = BUrl();

/** @brief Default HTTP method (GET). */
static const BHttpMethod kDefaultMethod = BHttpMethod::Get;

/** @brief Empty optional-fields sentinel. */
static const BHttpFields kDefaultOptionalFields = BHttpFields();

struct BHttpRequest::Data {
	BUrl url = kDefaultUrl;
	BHttpMethod method = kDefaultMethod;
	uint8 maxRedirections = 8;
	BHttpFields optionalFields;
	std::optional<BHttpAuthentication> authentication;
	bool stopOnError = false;
	bigtime_t timeout = B_INFINITE_TIMEOUT;
	std::optional<Body> requestBody;
};


// #pragma mark -- BHttpRequest helper functions


/**
 * @brief Build a Basic HTTP Authorization header value.
 *
 * Encodes "username:password" in URL-safe base-64 and prepends "Basic ".
 *
 * @param username  The account username.
 * @param password  The account password.
 * @return BString formatted as "Basic <base64>".
 */
static inline BString
build_basic_http_header(const BString& username, const BString& password)
{
	BString basicEncode, result;
	basicEncode << username << ":" << password;
	result << "Basic " << encode_to_base64(basicEncode);
	return result;
}


// #pragma mark -- BHttpRequest


/**
 * @brief Construct a default BHttpRequest with no URL set.
 */
BHttpRequest::BHttpRequest()
	:
	fData(std::make_unique<Data>())
{
}


/**
 * @brief Construct a BHttpRequest targeting \a url.
 *
 * @param url  The request URL; must be valid and use http or https scheme.
 */
BHttpRequest::BHttpRequest(const BUrl& url)
	:
	fData(std::make_unique<Data>())
{
	SetUrl(url);
}


/**
 * @brief Move constructor.
 *
 * @param other  Source BHttpRequest to move.
 */
BHttpRequest::BHttpRequest(BHttpRequest&& other) noexcept = default;


/**
 * @brief Destructor.
 */
BHttpRequest::~BHttpRequest() = default;


/**
 * @brief Move assignment operator.
 *
 * @param other  Source BHttpRequest to move.
 * @return Reference to this object (unused).
 */
BHttpRequest& BHttpRequest::operator=(BHttpRequest&&) noexcept = default;


/**
 * @brief Return whether this request has no valid URL set.
 *
 * @return true if fData is null or the URL is invalid.
 */
bool
BHttpRequest::IsEmpty() const noexcept
{
	return (!fData || !fData->url.IsValid());
}


/**
 * @brief Return the configured HTTP authentication credentials, if any.
 *
 * @return Pointer to the BHttpAuthentication, or nullptr if none is set.
 */
const BHttpAuthentication*
BHttpRequest::Authentication() const noexcept
{
	if (fData && fData->authentication)
		return std::addressof(*fData->authentication);
	return nullptr;
}


/**
 * @brief Return the optional request-specific HTTP header fields.
 *
 * @return Const reference to the BHttpFields set by SetFields().
 */
const BHttpFields&
BHttpRequest::Fields() const noexcept
{
	if (!fData)
		return kDefaultOptionalFields;
	return fData->optionalFields;
}


/**
 * @brief Return the maximum number of redirections the session should follow.
 *
 * @return Maximum redirect count (default 8).
 */
uint8
BHttpRequest::MaxRedirections() const noexcept
{
	if (!fData)
		return 8;
	return fData->maxRedirections;
}


/**
 * @brief Return the HTTP method for this request.
 *
 * @return Const reference to the BHttpMethod (default GET).
 */
const BHttpMethod&
BHttpRequest::Method() const noexcept
{
	if (!fData)
		return kDefaultMethod;
	return fData->method;
}


/**
 * @brief Return the optional request body descriptor.
 *
 * @return Pointer to the Body struct, or nullptr if no body is set.
 */
const BHttpRequest::Body*
BHttpRequest::RequestBody() const noexcept
{
	if (fData && fData->requestBody)
		return std::addressof(*fData->requestBody);
	return nullptr;
}


/**
 * @brief Return whether the request should stop on HTTP error responses (4xx/5xx).
 *
 * @return true if the session should treat error responses as failures.
 */
bool
BHttpRequest::StopOnError() const noexcept
{
	if (!fData)
		return false;
	return fData->stopOnError;
}


/**
 * @brief Return the connection timeout for this request.
 *
 * @return Timeout in microseconds, or B_INFINITE_TIMEOUT if not set.
 */
bigtime_t
BHttpRequest::Timeout() const noexcept
{
	if (!fData)
		return B_INFINITE_TIMEOUT;
	return fData->timeout;
}


/**
 * @brief Return the target URL for this request.
 *
 * @return Const reference to the BUrl (default-constructed if fData is null).
 */
const BUrl&
BHttpRequest::Url() const noexcept
{
	if (!fData)
		return kDefaultUrl;
	return fData->url;
}


/**
 * @brief Set HTTP Basic authentication credentials for this request.
 *
 * @param authentication  Struct containing username and password.
 */
void
BHttpRequest::SetAuthentication(const BHttpAuthentication& authentication)
{
	if (!fData)
		fData = std::make_unique<Data>();

	fData->authentication = authentication;
}

/** @brief Reserved field names that may not be set via SetFields(). */
static constexpr std::array<std::string_view, 6> fReservedOptionalFieldNames
	= {"Host"sv, "Accept-Encoding"sv, "Connection"sv, "Content-Type"sv, "Content-Length"sv};


/**
 * @brief Replace the optional request-specific header fields.
 *
 * Validates that none of the reserved field names (Host, Accept-Encoding,
 * Connection, Content-Type, Content-Length) appear in \a fields.
 *
 * @param fields  BHttpFields to use as the optional fields.
 */
void
BHttpRequest::SetFields(const BHttpFields& fields)
{
	if (!fData)
		fData = std::make_unique<Data>();

	for (auto& field: fields) {
		if (std::find(fReservedOptionalFieldNames.begin(), fReservedOptionalFieldNames.end(),
				field.Name())
			!= fReservedOptionalFieldNames.end()) {
			std::string_view fieldName = field.Name();
			throw BHttpFields::InvalidInput(
				__PRETTY_FUNCTION__, BString(fieldName.data(), fieldName.size()));
		}
	}
	fData->optionalFields = fields;
}


/**
 * @brief Set the maximum number of HTTP redirections to follow.
 *
 * @param maxRedirections  Maximum redirect count.
 */
void
BHttpRequest::SetMaxRedirections(uint8 maxRedirections)
{
	if (!fData)
		fData = std::make_unique<Data>();
	fData->maxRedirections = maxRedirections;
}


/**
 * @brief Set the HTTP method for this request.
 *
 * @param method  The BHttpMethod to use.
 */
void
BHttpRequest::SetMethod(const BHttpMethod& method)
{
	if (!fData)
		fData = std::make_unique<Data>();
	fData->method = method;
}


/**
 * @brief Set the request body with a BDataIO input, MIME type, and optional size.
 *
 * @param input     Non-null BDataIO providing the body bytes.
 * @param mimeType  Valid MIME type string for the Content-Type header.
 * @param size      Optional body size; if absent, chunked transfer is required
 *                  (which is not yet supported and will throw at serialisation time).
 */
void
BHttpRequest::SetRequestBody(
	std::unique_ptr<BDataIO> input, BString mimeType, std::optional<off_t> size)
{
	if (input == nullptr)
		throw std::invalid_argument("input cannot be null");

	// TODO: support optional mimetype arguments like type/subtype;parameter=value
	if (!BMimeType::IsValid(mimeType.String()))
		throw std::invalid_argument("mimeType must be a valid mimetype");

	// TODO: review if there should be complex validation between the method and whether or not
	// there is a request body. The current implementation does the validation at the request
	// generation stage, where GET, HEAD, OPTIONS, CONNECT and TRACE will not submit a body.

	if (!fData)
		fData = std::make_unique<Data>();
	fData->requestBody = {std::move(input), std::move(mimeType), size};

	// Check if the input is a BPositionIO, and if so, store the current position, so that it can
	// be rewinded in case of a redirect.
	auto inputPositionIO = dynamic_cast<BPositionIO*>(fData->requestBody->input.get());
	if (inputPositionIO != nullptr)
		fData->requestBody->startPosition = inputPositionIO->Position();
}


/**
 * @brief Configure whether to treat HTTP error responses as request failures.
 *
 * @param stopOnError  true to fail on 4xx/5xx responses.
 */
void
BHttpRequest::SetStopOnError(bool stopOnError)
{
	if (!fData)
		fData = std::make_unique<Data>();
	fData->stopOnError = stopOnError;
}


/**
 * @brief Set the connection timeout for this request.
 *
 * @param timeout  Timeout in microseconds; pass B_INFINITE_TIMEOUT for no limit.
 */
void
BHttpRequest::SetTimeout(bigtime_t timeout)
{
	if (!fData)
		fData = std::make_unique<Data>();
	fData->timeout = timeout;
}


/**
 * @brief Set the target URL, validating scheme and URL validity.
 *
 * @param url  Target URL; must be valid and use the http or https scheme.
 */
void
BHttpRequest::SetUrl(const BUrl& url)
{
	if (!fData)
		fData = std::make_unique<Data>();

	if (!url.IsValid())
		throw BInvalidUrl(__PRETTY_FUNCTION__, BUrl(url));
	if (url.Protocol() != "http" && url.Protocol() != "https") {
		// TODO: optimize BStringList with modern language features
		BStringList list;
		list.Add("http");
		list.Add("https");
		throw BUnsupportedProtocol(__PRETTY_FUNCTION__, BUrl(url), list);
	}
	fData->url = url;
}


/**
 * @brief Remove any previously configured HTTP authentication credentials.
 */
void
BHttpRequest::ClearAuthentication() noexcept
{
	if (fData)
		fData->authentication = std::nullopt;
}


/**
 * @brief Remove the request body and return ownership of the BDataIO.
 *
 * @return The previously set BDataIO, or nullptr if no body was set.
 */
std::unique_ptr<BDataIO>
BHttpRequest::ClearRequestBody() noexcept
{
	if (fData && fData->requestBody) {
		auto body = std::move(fData->requestBody->input);
		fData->requestBody = std::nullopt;
		return body;
	}
	return nullptr;
}


/**
 * @brief Serialise the HTTP request header section to a BString.
 *
 * Convenience wrapper around SerializeHeaderTo() that returns the result
 * as a BString.
 *
 * @return BString containing the full HTTP request header (including CRLF terminator).
 */
BString
BHttpRequest::HeaderToString() const
{
	HttpBuffer buffer;
	SerializeHeaderTo(buffer);

	return BString(static_cast<const char*>(buffer.Data().data()), buffer.RemainingBytes());
}


/**
 * @brief Rewind the request body BPositionIO to its original position.
 *
 * Called by BHttpSession::Request when following a redirect that requires
 * retransmission of the body.
 *
 * @return true if the body was successfully rewound or there is no body;
 *         false if the body cannot be repositioned.
 */
bool
BHttpRequest::RewindBody() noexcept
{
	if (fData && fData->requestBody && fData->requestBody->startPosition) {
		auto inputData = dynamic_cast<BPositionIO*>(fData->requestBody->input.get());
		return *fData->requestBody->startPosition
			== inputData->Seek(*fData->requestBody->startPosition, SEEK_SET);
	}
	return true;
}


/**
 * @brief Write the HTTP request header section into \a buffer.
 *
 * Builds the request line, mandatory headers (Host, Accept-Encoding,
 * Connection), optional authentication and body headers, and then the
 * caller-supplied optional fields.  Used internally by HttpSerializer.
 *
 * @param buffer  HttpBuffer to receive the serialised header bytes.
 */
void
BHttpRequest::SerializeHeaderTo(HttpBuffer& buffer) const
{
	// Method & URL
	//	TODO: proxy
	buffer << fData->method.Method() << " "sv;
	if (fData->url.HasPath() && fData->url.Path().Length() > 0)
		buffer << std::string_view(fData->url.Path().String());
	else
		buffer << "/"sv;

	if (fData->url.HasRequest())
		buffer << "?"sv << Url().Request().String();

	// TODO: switch between HTTP 1.0 and 1.1 based on configuration
	buffer << " HTTP/1.1\r\n"sv;

	BHttpFields outputFields;
	if (true /* http == 1.1 */) {
		BString host = fData->url.Host();
		int defaultPort = fData->url.Protocol() == "http" ? 80 : 443;
		if (fData->url.HasPort() && fData->url.Port() != defaultPort)
			host << ':' << fData->url.Port();

		outputFields.AddFields({
			{"Host"sv, std::string_view(host.String())}, {"Accept-Encoding"sv, "gzip"sv},
			// Allows the server to compress data using the "gzip" format.
			// "deflate" is not supported, because there are two interpretations
			// of what it means (the RFC and Microsoft products), and we don't
			// want to handle this. Very few websites support only deflate,
			// and most of them will send gzip, or at worst, uncompressed data.
			{"Connection"sv, "close"sv}
			// Let the remote server close the connection after response since
			// we don't handle multiple request on a single connection
		});
	}

	if (fData->authentication) {
		// This request will add a Basic authorization header
		BString authorization = build_basic_http_header(
			fData->authentication->username, fData->authentication->password);
		outputFields.AddField("Authorization"sv, std::string_view(authorization.String()));
	}

	if (fData->requestBody) {
		outputFields.AddField(
			"Content-Type"sv, std::string_view(fData->requestBody->mimeType.String()));
		if (fData->requestBody->size)
			outputFields.AddField("Content-Length"sv, std::to_string(*fData->requestBody->size));
		else
			throw BRuntimeError(__PRETTY_FUNCTION__,
				"Transfer body with unknown content length; chunked transfer not supported");
	}

	for (const auto& field: outputFields)
		buffer << field.RawField() << "\r\n"sv;

	for (const auto& field: fData->optionalFields)
		buffer << field.RawField() << "\r\n"sv;

	buffer << "\r\n"sv;
}
