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
 *   Copyright 2021 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Niels Sascha Reedijk, niels.reedijk@gmail.com
 */


/**
 * @file ErrorsExt.cpp
 * @brief Implementation of BError, BRuntimeError, and BSystemError exception classes.
 *
 * Provides an extensible exception hierarchy for the Kintsugi network services
 * subsystem.  BError is the abstract base; BRuntimeError carries a free-form
 * message string; BSystemError wraps a Haiku status_t alongside its strerror()
 * text.
 *
 * @see BNetworkRequestError, BHttpSession
 */


#include <ErrorsExt.h>

#include <iostream>
#include <sstream>

#include <DataIO.h>

using namespace BPrivate::Network;


/**
 * @brief Construct a BError with a C-string origin identifier.
 *
 * @param origin  Null-terminated string identifying the source of the error
 *                (typically __PRETTY_FUNCTION__).
 */
BError::BError(const char* origin)
	:
	fOrigin(BString(origin))
{
}


/**
 * @brief Construct a BError with a BString origin identifier (move).
 *
 * @param origin  BString identifying the source of the error.
 */
BError::BError(BString origin)
	:
	fOrigin(std::move(origin))
{
}


/**
 * @brief Virtual destructor.
 */
BError::~BError() noexcept = default;


/**
 * @brief Copy constructor.
 *
 * @param error  Source BError to copy.
 */
BError::BError(const BError& error) = default;


/**
 * @brief Move constructor.
 *
 * @param error  Source BError to move from.
 */
BError::BError(BError&& error) noexcept = default;


/**
 * @brief Copy assignment operator.
 *
 * @param error  Source BError to copy from.
 * @return Reference to this object.
 */
BError& BError::operator=(const BError& error) = default;


/**
 * @brief Move assignment operator.
 *
 * @param error  Source BError to move from.
 * @return Reference to this object.
 */
BError& BError::operator=(BError&& error) noexcept = default;


/**
 * @brief Return the origin identifier for this error.
 *
 * @return Null-terminated string that was passed to the constructor.
 */
const char*
BError::Origin() const noexcept
{
	return fOrigin.String();
}


/**
 * @brief Build a human-readable debug message combining origin and description.
 *
 * @return BString formatted as "[origin] message".
 */
BString
BError::DebugMessage() const
{
	BString debugMessage;
	debugMessage << "[" << Origin() << "] " << Message();
	return debugMessage;
}


/**
 * @brief Write the debug message to a C++ output stream.
 *
 * @param stream  The output stream to write to.
 */
void
BError::WriteToStream(std::ostream& stream) const
{
	stream << DebugMessage().String() << std::endl;
}


/**
 * @brief Write the debug message to a BDataIO output.
 *
 * @param output  The BDataIO target to write to.
 * @return Number of bytes written (including the null terminator).
 */
size_t
BError::WriteToOutput(BDataIO* output) const
{
	std::stringstream stream;
	WriteToStream(stream);
	ssize_t result = output->Write(stream.str().c_str(), stream.str().length() + 1);
	if (result < 0)
		throw BSystemError("BDataIO::Write()", result);
	return static_cast<size_t>(result);
}


/**
 * @brief Reserved virtual slot 1 — do not override.
 */
void
BError::_ReservedError1()
{
}


/**
 * @brief Reserved virtual slot 2 — do not override.
 */
void
BError::_ReservedError2()
{
}


/**
 * @brief Reserved virtual slot 3 — do not override.
 */
void
BError::_ReservedError3()
{
}


/**
 * @brief Reserved virtual slot 4 — do not override.
 */
void
BError::_ReservedError4()
{
}


/* BRuntimeError */
/**
 * @brief Construct a BRuntimeError with C-string origin and message.
 *
 * @param origin   Null-terminated origin identifier string.
 * @param message  Null-terminated human-readable error description.
 */
BRuntimeError::BRuntimeError(const char* origin, const char* message)
	:
	BError(origin),
	fMessage(BString(message))
{
}


/**
 * @brief Construct a BRuntimeError with C-string origin and BString message (move).
 *
 * @param origin   Null-terminated origin identifier string.
 * @param message  BString human-readable error description.
 */
BRuntimeError::BRuntimeError(const char* origin, BString message)
	:
	BError(origin),
	fMessage(std::move(message))
{
}


/**
 * @brief Construct a BRuntimeError with BString origin and BString message (move).
 *
 * @param origin   BString origin identifier.
 * @param message  BString error description.
 */
BRuntimeError::BRuntimeError(BString origin, BString message)
	:
	BError(std::move(origin)),
	fMessage(std::move(message))
{
}


/**
 * @brief Copy constructor.
 *
 * @param other  Source BRuntimeError to copy.
 */
BRuntimeError::BRuntimeError(const BRuntimeError& other) = default;


/**
 * @brief Move constructor.
 *
 * @param other  Source BRuntimeError to move from.
 */
BRuntimeError::BRuntimeError(BRuntimeError&& other) noexcept = default;


/**
 * @brief Copy assignment operator.
 *
 * @param other  Source BRuntimeError to copy from.
 * @return Reference to this object.
 */
BRuntimeError& BRuntimeError::operator=(const BRuntimeError& other) = default;


/**
 * @brief Move assignment operator.
 *
 * @param other  Source BRuntimeError to move from.
 * @return Reference to this object.
 */
BRuntimeError& BRuntimeError::operator=(BRuntimeError&& other) noexcept = default;


/**
 * @brief Return the human-readable error message.
 *
 * @return Null-terminated message string passed at construction.
 */
const char*
BRuntimeError::Message() const noexcept
{
	return fMessage.String();
}


/* BSystemError */
/**
 * @brief Construct a BSystemError wrapping a Haiku status_t with a C-string origin.
 *
 * @param origin  Null-terminated origin identifier string.
 * @param error   The status_t error code from the failed system call.
 */
BSystemError::BSystemError(const char* origin, status_t error)
	:
	BError(origin),
	fErrorCode(error)
{
}


/**
 * @brief Construct a BSystemError wrapping a Haiku status_t with a BString origin.
 *
 * @param origin  BString origin identifier.
 * @param error   The status_t error code from the failed system call.
 */
BSystemError::BSystemError(BString origin, status_t error)
	:
	BError(std::move(origin)),
	fErrorCode(error)
{
}


/**
 * @brief Copy constructor.
 *
 * @param other  Source BSystemError to copy.
 */
BSystemError::BSystemError(const BSystemError& other) = default;


/**
 * @brief Move constructor.
 *
 * @param other  Source BSystemError to move from.
 */
BSystemError::BSystemError(BSystemError&& other) noexcept = default;


/**
 * @brief Copy assignment operator.
 *
 * @param other  Source BSystemError to copy from.
 * @return Reference to this object.
 */
BSystemError& BSystemError::operator=(const BSystemError& other) = default;


/**
 * @brief Move assignment operator.
 *
 * @param other  Source BSystemError to move from.
 * @return Reference to this object.
 */
BSystemError& BSystemError::operator=(BSystemError&& other) noexcept = default;


/**
 * @brief Return the strerror() description of the wrapped error code.
 *
 * @return Null-terminated system error description string.
 */
const char*
BSystemError::Message() const noexcept
{
	return strerror(fErrorCode);
}


/**
 * @brief Build a debug message including the numeric error code.
 *
 * @return BString formatted as "[origin] message (code)".
 */
BString
BSystemError::DebugMessage() const
{
	BString debugMessage;
	debugMessage << "[" << Origin() << "] " << Message() << " (" << fErrorCode << ")";
	return debugMessage;
}


/**
 * @brief Return the raw Haiku status_t error code.
 *
 * @return The status_t value stored at construction time.
 */
status_t
BSystemError::Error() noexcept
{
	return fErrorCode;
}
