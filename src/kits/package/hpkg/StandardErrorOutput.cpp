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
 *   Copyright 2009-2013, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file StandardErrorOutput.cpp
 * @brief BErrorOutput implementation that writes diagnostics to stderr.
 *
 * BStandardErrorOutput is the simplest concrete implementation of the
 * BErrorOutput interface. It forwards all formatted error messages directly
 * to the standard error stream (stderr) via vfprintf, making it suitable for
 * command-line tools and unit tests that do not need custom error routing.
 *
 * @see BErrorOutput, BPackageWriter, BPackageReader
 */


#include <package/hpkg/StandardErrorOutput.h>

#include <stdio.h>


namespace BPackageKit {

namespace BHPKG {


/**
 * @brief Prints a formatted error message to stderr.
 *
 * This is the single virtual method that concrete BErrorOutput subclasses must
 * override. BStandardErrorOutput simply delegates to vfprintf(stderr, ...).
 *
 * @param format printf-style format string.
 * @param args   Variable argument list corresponding to @a format.
 */
void
BStandardErrorOutput::PrintErrorVarArgs(const char* format, va_list args)
{
	vfprintf(stderr, format, args);
}


}	// namespace BHPKG

}	// namespace BPackageKit
