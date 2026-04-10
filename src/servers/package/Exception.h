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

/** @file Exception.h
 *  @brief Represents errors that occur during package transaction processing */

#ifndef EXCEPTION_H
#define EXCEPTION_H


#include <String.h>

#include <package/CommitTransactionResult.h>


using BPackageKit::BCommitTransactionResult;
using BPackageKit::BTransactionError;


/** @brief Captures transaction error details for propagation to the commit result */
class Exception {
public:
	/** @brief Create an exception for the given transaction error code */
								Exception(BTransactionError error);

	/** @brief Return the transaction error code */
			BTransactionError	Error() const
									{ return fError; }

	/** @brief Return the underlying system error, if any */
			status_t			SystemError() const
									{ return fSystemError; }
	/** @brief Set the system-level error code and return this exception */
			Exception&			SetSystemError(status_t error);

	/** @brief Return the name of the package that caused the error */
			const BString&		PackageName() const
									{ return fPackageName; }
	/** @brief Set the offending package name and return this exception */
			Exception&			SetPackageName(const BString& packageName);

	/** @brief Return the first associated filesystem path */
			const BString&		Path1() const
									{ return fPath1; }
	/** @brief Set the first path and return this exception */
			Exception&			SetPath1(const BString& path);

	/** @brief Return the second associated filesystem path */
			const BString&		Path2() const
									{ return fPath2; }
	/** @brief Set the second path and return this exception */
			Exception&			SetPath2(const BString& path);

	/** @brief Return the first auxiliary string */
			const BString&		String1() const
									{ return fString1; }
	/** @brief Set the first auxiliary string and return this exception */
			Exception&			SetString1(const BString& string);

	/** @brief Return the second auxiliary string */
			const BString&		String2() const
									{ return fString2; }
	/** @brief Set the second auxiliary string and return this exception */
			Exception&			SetString2(const BString& string);

	/** @brief Copy all error fields into a BCommitTransactionResult */
			void				SetOnResult(BCommitTransactionResult& result);

private:
			BTransactionError	fError;       /**< High-level transaction error code */
			status_t			fSystemError; /**< Low-level system error */
			BString				fPackageName; /**< Package that triggered the error */
			BString				fPath1;       /**< First related path */
			BString				fPath2;       /**< Second related path */
			BString				fString1;     /**< First auxiliary detail string */
			BString				fString2;     /**< Second auxiliary detail string */
};


#endif	// EXCEPTION_H
