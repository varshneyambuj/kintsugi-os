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
 * @file HttpResult.cpp
 * @brief Implementation of BHttpStatus and BHttpResult.
 *
 * BHttpStatus provides helpers to classify a raw HTTP status code into a
 * BHttpStatusClass and to map it to the BHttpStatusCode enum.  BHttpResult
 * is a move-only future-like object that blocks its accessor methods until
 * the corresponding data (status, headers, body) has been produced by the
 * background HTTP session worker.
 *
 * @see BHttpSession, BHttpFields, HttpResultPrivate
 */


#include <ErrorsExt.h>
#include <HttpFields.h>
#include <HttpResult.h>

#include "HttpResultPrivate.h"

using namespace BPrivate::Network;


// #pragma mark -- BHttpStatus


/**
 * @brief Classify the numeric status code into a broad BHttpStatusClass.
 *
 * Maps the hundreds digit (1xx–5xx) to Informational, Success,
 * Redirection, ClientError, or ServerError.
 *
 * @return BHttpStatusClass enum value for this status code.
 */
BHttpStatusClass
BHttpStatus::StatusClass() const noexcept
{
	switch (code / 100) {
		case 1:
			return BHttpStatusClass::Informational;
		case 2:
			return BHttpStatusClass::Success;
		case 3:
			return BHttpStatusClass::Redirection;
		case 4:
			return BHttpStatusClass::ClientError;
		case 5:
			return BHttpStatusClass::ServerError;
		default:
			break;
	}
	return BHttpStatusClass::Invalid;
}


/**
 * @brief Map the numeric status code to the BHttpStatusCode enum.
 *
 * Returns BHttpStatusCode::Unknown for any code not explicitly listed in
 * the enum.
 *
 * @return The matching BHttpStatusCode, or BHttpStatusCode::Unknown.
 */
BHttpStatusCode
BHttpStatus::StatusCode() const noexcept
{
	switch (static_cast<BHttpStatusCode>(code)) {
		// 1xx
		case BHttpStatusCode::Continue:
			[[fallthrough]];
		case BHttpStatusCode::SwitchingProtocols:
			[[fallthrough]];

		// 2xx
		case BHttpStatusCode::Ok:
			[[fallthrough]];
		case BHttpStatusCode::Created:
			[[fallthrough]];
		case BHttpStatusCode::Accepted:
			[[fallthrough]];
		case BHttpStatusCode::NonAuthoritativeInformation:
			[[fallthrough]];
		case BHttpStatusCode::NoContent:
			[[fallthrough]];
		case BHttpStatusCode::ResetContent:
			[[fallthrough]];
		case BHttpStatusCode::PartialContent:
			[[fallthrough]];

		// 3xx
		case BHttpStatusCode::MultipleChoice:
			[[fallthrough]];
		case BHttpStatusCode::MovedPermanently:
			[[fallthrough]];
		case BHttpStatusCode::Found:
			[[fallthrough]];
		case BHttpStatusCode::SeeOther:
			[[fallthrough]];
		case BHttpStatusCode::NotModified:
			[[fallthrough]];
		case BHttpStatusCode::UseProxy:
			[[fallthrough]];
		case BHttpStatusCode::TemporaryRedirect:
			[[fallthrough]];
		case BHttpStatusCode::PermanentRedirect:
			[[fallthrough]];

		// 4xx
		case BHttpStatusCode::BadRequest:
			[[fallthrough]];
		case BHttpStatusCode::Unauthorized:
			[[fallthrough]];
		case BHttpStatusCode::PaymentRequired:
			[[fallthrough]];
		case BHttpStatusCode::Forbidden:
			[[fallthrough]];
		case BHttpStatusCode::NotFound:
			[[fallthrough]];
		case BHttpStatusCode::MethodNotAllowed:
			[[fallthrough]];
		case BHttpStatusCode::NotAcceptable:
			[[fallthrough]];
		case BHttpStatusCode::ProxyAuthenticationRequired:
			[[fallthrough]];
		case BHttpStatusCode::RequestTimeout:
			[[fallthrough]];
		case BHttpStatusCode::Conflict:
			[[fallthrough]];
		case BHttpStatusCode::Gone:
			[[fallthrough]];
		case BHttpStatusCode::LengthRequired:
			[[fallthrough]];
		case BHttpStatusCode::PreconditionFailed:
			[[fallthrough]];
		case BHttpStatusCode::RequestEntityTooLarge:
			[[fallthrough]];
		case BHttpStatusCode::RequestUriTooLarge:
			[[fallthrough]];
		case BHttpStatusCode::UnsupportedMediaType:
			[[fallthrough]];
		case BHttpStatusCode::RequestedRangeNotSatisfiable:
			[[fallthrough]];
		case BHttpStatusCode::ExpectationFailed:
			[[fallthrough]];

		// 5xx
		case BHttpStatusCode::InternalServerError:
			[[fallthrough]];
		case BHttpStatusCode::NotImplemented:
			[[fallthrough]];
		case BHttpStatusCode::BadGateway:
			[[fallthrough]];
		case BHttpStatusCode::ServiceUnavailable:
			[[fallthrough]];
		case BHttpStatusCode::GatewayTimeout:
			return static_cast<BHttpStatusCode>(code);

		default:
			break;
	}

	return BHttpStatusCode::Unknown;
}


// #pragma mark -- BHttpResult


/**
 * @brief Private constructor — creates a BHttpResult backed by shared private state.
 *
 * @param data  Shared pointer to the HttpResultPrivate data produced by BHttpSession.
 */
/*private*/
BHttpResult::BHttpResult(std::shared_ptr<HttpResultPrivate> data)
	:
	fData(data)
{
}


/**
 * @brief Move constructor.
 *
 * @param other  BHttpResult to move from; \a other becomes invalid.
 */
BHttpResult::BHttpResult(BHttpResult&& other) noexcept = default;


/**
 * @brief Destructor — cancels the pending request if it is not yet complete.
 */
BHttpResult::~BHttpResult()
{
	if (fData)
		fData->SetCancel();
}


/**
 * @brief Move assignment operator.
 *
 * @param other  BHttpResult to move from.
 * @return Reference to this object.
 */
BHttpResult& BHttpResult::operator=(BHttpResult&& other) noexcept = default;


/**
 * @brief Block until the HTTP status line is available and return it.
 *
 * @return Const reference to the BHttpStatus populated by the session worker.
 */
const BHttpStatus&
BHttpResult::Status() const
{
	if (!fData)
		throw BRuntimeError(__PRETTY_FUNCTION__, "The BHttpResult object is no longer valid");
	status_t status = B_OK;
	while (status == B_INTERRUPTED || status == B_OK) {
		auto dataStatus = fData->GetStatusAtomic();
		if (dataStatus == HttpResultPrivate::kError)
			std::rethrow_exception(*(fData->error));

		if (dataStatus >= HttpResultPrivate::kStatusReady)
			return fData->status.value();

		status = acquire_sem(fData->data_wait);
	}
	throw BRuntimeError(__PRETTY_FUNCTION__, "Unexpected error waiting for status!");
}


/**
 * @brief Block until the HTTP header fields are available and return them.
 *
 * @return Const reference to the BHttpFields populated by the session worker.
 */
const BHttpFields&
BHttpResult::Fields() const
{
	if (!fData)
		throw BRuntimeError(__PRETTY_FUNCTION__, "The BHttpResult object is no longer valid");
	status_t status = B_OK;
	while (status == B_INTERRUPTED || status == B_OK) {
		auto dataStatus = fData->GetStatusAtomic();
		if (dataStatus == HttpResultPrivate::kError)
			std::rethrow_exception(*(fData->error));

		if (dataStatus >= HttpResultPrivate::kHeadersReady)
			return *(fData->fields);

		status = acquire_sem(fData->data_wait);
	}
	throw BRuntimeError(__PRETTY_FUNCTION__, "Unexpected error waiting for fields!");
}


/**
 * @brief Block until the HTTP body is available and return a reference to it.
 *
 * @return Reference to the BHttpBody populated by the session worker.
 */
BHttpBody&
BHttpResult::Body() const
{
	if (!fData)
		throw BRuntimeError(__PRETTY_FUNCTION__, "The BHttpResult object is no longer valid");
	status_t status = B_OK;
	while (status == B_INTERRUPTED || status == B_OK) {
		auto dataStatus = fData->GetStatusAtomic();
		if (dataStatus == HttpResultPrivate::kError)
			std::rethrow_exception(*(fData->error));

		if (dataStatus >= HttpResultPrivate::kBodyReady)
			return *(fData->body);

		status = acquire_sem(fData->data_wait);
	}
	throw BRuntimeError(__PRETTY_FUNCTION__, "Unexpected error waiting for the body!");
}


/**
 * @brief Return whether the HTTP status line has been received.
 *
 * @return true if Status() would not block.
 */
bool
BHttpResult::HasStatus() const
{
	if (!fData)
		throw BRuntimeError(__PRETTY_FUNCTION__, "The BHttpResult object is no longer valid");
	return fData->GetStatusAtomic() >= HttpResultPrivate::kStatusReady;
}


/**
 * @brief Return whether the HTTP header fields have been received.
 *
 * @return true if Fields() would not block.
 */
bool
BHttpResult::HasFields() const
{
	if (!fData)
		throw BRuntimeError(__PRETTY_FUNCTION__, "The BHttpResult object is no longer valid");
	return fData->GetStatusAtomic() >= HttpResultPrivate::kHeadersReady;
}


/**
 * @brief Return whether the HTTP body has been fully received.
 *
 * @return true if Body() would not block.
 */
bool
BHttpResult::HasBody() const
{
	if (!fData)
		throw BRuntimeError(__PRETTY_FUNCTION__, "The BHttpResult object is no longer valid");
	return fData->GetStatusAtomic() >= HttpResultPrivate::kBodyReady;
}


/**
 * @brief Return whether the entire HTTP response has been received.
 *
 * Equivalent to HasBody().
 *
 * @return true if the response is fully complete.
 */
bool
BHttpResult::IsCompleted() const
{
	if (!fData)
		throw BRuntimeError(__PRETTY_FUNCTION__, "The BHttpResult object is no longer valid");
	return HasBody();
}


/**
 * @brief Return the unique identifier for the request that produced this result.
 *
 * @return int32 identifier assigned by the session at request submission time.
 */
int32
BHttpResult::Identity() const
{
	if (!fData)
		throw BRuntimeError(__PRETTY_FUNCTION__, "The BHttpResult object is no longer valid");
	return fData->id;
}
