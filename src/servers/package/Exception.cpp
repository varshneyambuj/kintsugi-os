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
 *
 * Copyright 2013-2014, Ingo Weinhold, ingo_weinhold@gmx.de.
  * Distributed under the terms of the MIT License.
 */

/** @file Exception.cpp
 *  @brief Implements Exception methods for propagating transaction errors to results */



#include "Exception.h"


using namespace BPackageKit;


/**
 * @brief Constructs an Exception with the given transaction error code.
 *
 * All optional fields (system error, package name, paths, strings) are
 * initialized to empty/default values.
 *
 * @param error The transaction-level error that triggered this exception.
 */
Exception::Exception(BTransactionError error)
	:
	fError(error),
	fSystemError(B_ERROR),
	fPackageName(),
	fPath1(),
	fPath2(),
	fString1(),
	fString2()
{
}

/**
 * @brief Sets the underlying system error code on this exception.
 *
 * @param error The POSIX/BeOS status_t code.
 * @return Reference to this Exception for method chaining.
 */
Exception&
Exception::SetSystemError(status_t error)
{
	fSystemError = error;
	return *this;
}


/**
 * @brief Sets the name of the package associated with this error.
 *
 * @param packageName Human-readable package file name.
 * @return Reference to this Exception for method chaining.
 */
Exception&
Exception::SetPackageName(const BString& packageName)
{
	fPackageName = packageName;
	return *this;
}


/**
 * @brief Sets the first filesystem path relevant to this error.
 *
 * @param path A filesystem path (e.g., source or target).
 * @return Reference to this Exception for method chaining.
 */
Exception&
Exception::SetPath1(const BString& path)
{
	fPath1 = path;
	return *this;
}


/**
 * @brief Sets the second filesystem path relevant to this error.
 *
 * @param path A filesystem path (e.g., destination or backup).
 * @return Reference to this Exception for method chaining.
 */
Exception&
Exception::SetPath2(const BString& path)
{
	fPath2 = path;
	return *this;
}


/**
 * @brief Sets the first auxiliary string for this error.
 *
 * @param string An additional descriptive string.
 * @return Reference to this Exception for method chaining.
 */
Exception&
Exception::SetString1(const BString& string)
{
	fString1 = string;
	return *this;
}


/**
 * @brief Sets the second auxiliary string for this error.
 *
 * @param string An additional descriptive string.
 * @return Reference to this Exception for method chaining.
 */
Exception&
Exception::SetString2(const BString& string)
{
	fString2 = string;
	return *this;
}


/**
 * @brief Transfers all stored error details into a BCommitTransactionResult.
 *
 * Copies the error code, system error, package name, paths, and auxiliary
 * strings into the supplied result object so it can be sent back to the
 * requesting client.
 *
 * @param result The result object to populate with this exception's details.
 */
void
Exception::SetOnResult(BCommitTransactionResult& result)
{
	result.SetError(fError);
	result.SetSystemError(fSystemError);
	result.SetErrorPackage(fPackageName);
	result.SetPath1(fPath1);
	result.SetPath2(fPath2);
	result.SetString1(fString1);
	result.SetString2(fString2);
}
