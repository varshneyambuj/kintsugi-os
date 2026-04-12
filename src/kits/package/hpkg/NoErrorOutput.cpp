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
 *   Copyright 2013, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 */


/**
 * @file NoErrorOutput.cpp
 * @brief A silent BErrorOutput implementation that discards all error messages.
 *
 * BNoErrorOutput provides a concrete BErrorOutput whose PrintErrorVarArgs()
 * is a no-op, effectively suppressing all diagnostic output.  It is useful
 * when HPKG operations are performed programmatically and the caller wishes
 * to handle errors exclusively through return codes rather than printed text.
 *
 * @see BErrorOutput
 */


#include <package/hpkg/NoErrorOutput.h>


namespace BPackageKit {

namespace BHPKG {


/**
 * @brief Silently discard a formatted error message.
 *
 * This implementation intentionally performs no action, suppressing all
 * diagnostic output from the HPKG subsystem.
 *
 * @param format Unused printf-style format string.
 * @param args   Unused variadic argument list.
 */
void
BNoErrorOutput::PrintErrorVarArgs(const char* format, va_list args)
{
}


}	// namespace BHPKG

}	// namespace BPackageKit
