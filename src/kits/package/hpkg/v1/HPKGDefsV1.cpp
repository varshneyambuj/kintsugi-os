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
 *   Copyright 2013, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file HPKGDefsV1.cpp
 * @brief Global constant definitions for the HPKG v1 format.
 *
 * Provides the well-known string constants defined by the HPKG v1 format
 * specification. Currently this file exports the canonical name of the
 * embedded package-info file that is stored inside every v1 package.
 *
 * @see BPackageReader (V1), HPKGDefsPrivate (v1)
 */


#include <package/hpkg/v1/HPKGDefs.h>


namespace BPackageKit {

namespace BHPKG {

namespace V1 {


/** @brief The canonical filename of the embedded package info within a v1 HPKG. */
const char* const B_HPKG_PACKAGE_INFO_FILE_NAME = ".PackageInfo";


}	// namespace V1

}	// namespace BHPKG

}	// namespace BPackageKit
