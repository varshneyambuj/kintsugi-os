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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ErrorOutput.cpp
 * @brief Abstract error-reporting interface used throughout the HPKG subsystem.
 *
 * BErrorOutput defines a polymorphic channel through which HPKG readers and
 * writers emit diagnostic messages.  The printf-style PrintError() convenience
 * method formats a va_list and delegates to the pure-virtual
 * PrintErrorVarArgs(), which concrete subclasses implement to direct output
 * to stderr, a log buffer, or nowhere at all.
 *
 * @see BNoErrorOutput
 */


#include <package/hpkg/ErrorOutput.h>

#include <stdio.h>


namespace BPackageKit {

namespace BHPKG {


/**
 * @brief Virtual destructor for BErrorOutput.
 *
 * Allows derived error-output objects to be deleted through a base-class
 * pointer without leaking resources.
 */
BErrorOutput::~BErrorOutput()
{
}


/**
 * @brief Emit a formatted error message using printf-style arguments.
 *
 * Constructs a va_list from the variadic arguments and forwards it to
 * PrintErrorVarArgs(), which must be implemented by subclasses.
 *
 * @param format printf-style format string for the error message.
 * @param ...    Additional arguments matching the format string.
 */
void
BErrorOutput::PrintError(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	PrintErrorVarArgs(format, args);
	va_end(args);
}


}	// namespace BHPKG

}	// namespace BPackageKit
