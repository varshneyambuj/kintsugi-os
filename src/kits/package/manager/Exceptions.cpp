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
 *   Copyright 2013-2014, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Exceptions.cpp
 * @brief Exception types used by the package manager to signal fatal and non-fatal conditions.
 *
 * The package manager uses C++ exceptions for control flow across deeply nested
 * call stacks. BFatalErrorException signals an unrecoverable error with an
 * optional status code and detail string; BAbortedByUserException signals user
 * cancellation; BNothingToDoException signals that a requested operation was
 * a no-op.
 *
 * @see BPackageManager
 */


#include <package/manager/Exceptions.h>

#include <stdarg.h>


namespace BPackageKit {

namespace BManager {

namespace BPrivate {


// #pragma mark - BException


/**
 * @brief Default-construct a BException with an empty message.
 */
BException::BException()
	:
	fMessage()
{
}


/**
 * @brief Construct a BException with an explicit message string.
 *
 * @param message  Human-readable error description.
 */
BException::BException(const BString& message)
	:
	fMessage(message)
{
}


// #pragma mark - BFatalErrorException


/**
 * @brief Default-construct a BFatalErrorException with no message and B_OK status.
 */
BFatalErrorException::BFatalErrorException()
	:
	BException(),
	fDetails(),
	fError(B_OK),
	fCommitTransactionResult(),
	fCommitTransactionFailed(false)
{
}


/**
 * @brief Construct a BFatalErrorException with a printf-style message.
 *
 * @param format  printf-style format string for the error message.
 * @param ...     Arguments for the format string.
 */
BFatalErrorException::BFatalErrorException(const char* format, ...)
	:
	BException(),
	fDetails(),
	fError(B_OK),
	fCommitTransactionResult(),
	fCommitTransactionFailed(false)
{
	va_list args;
	va_start(args, format);
	fMessage.SetToFormatVarArgs(format, args);
	va_end(args);
}


/**
 * @brief Construct a BFatalErrorException with a status code and a printf-style message.
 *
 * @param error   The status_t error code associated with this exception.
 * @param format  printf-style format string for the error message.
 * @param ...     Arguments for the format string.
 */
BFatalErrorException::BFatalErrorException(status_t error, const char* format,
	...)
	:
	BException(),
	fDetails(),
	fError(error),
	fCommitTransactionResult(),
	fCommitTransactionFailed(false)
{
	va_list args;
	va_start(args, format);
	fMessage.SetToFormatVarArgs(format, args);
	va_end(args);
}


/**
 * @brief Construct a BFatalErrorException from a failed commit-transaction result.
 *
 * Sets fCommitTransactionFailed to true and builds the message from the
 * result's full error description.
 *
 * @param result  The BCommitTransactionResult describing the failure.
 */
BFatalErrorException::BFatalErrorException(
	const BCommitTransactionResult& result)
	:
	BException(),
	fDetails(),
	fError(B_OK),
	fCommitTransactionResult(result),
	fCommitTransactionFailed(true)
{
	fMessage.SetToFormat("failed to commit transaction: %s",
		result.FullErrorMessage().String());
}


/**
 * @brief Attach supplementary detail text to this exception and return a self-reference.
 *
 * Intended for fluent use: throw BFatalErrorException(...).SetDetails("...").
 *
 * @param details  Additional context about the failure.
 * @return Reference to this exception object.
 */
BFatalErrorException&
BFatalErrorException::SetDetails(const BString& details)
{
	fDetails = details;
	return *this;
}


// #pragma mark - BAbortedByUserException


/**
 * @brief Construct a BAbortedByUserException.
 *
 * Thrown when the user explicitly cancels an operation.
 */
BAbortedByUserException::BAbortedByUserException()
	:
	BException()
{
}


// #pragma mark - BNothingToDoException


/**
 * @brief Construct a BNothingToDoException.
 *
 * Thrown when an operation is requested but no packages require
 * installation, removal, or update.
 */
BNothingToDoException::BNothingToDoException()
	:
	BException()
{
}


}	// namespace BPrivate

}	// namespace BManager

}	// namespace BPackageKit
