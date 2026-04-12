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
 * @file HPKGDefs.cpp
 * @brief Global constant definitions for the HPKG package format.
 *
 * Defines string and numeric constants that are shared across the HPKG
 * reader and writer.  Currently this translation unit provides the
 * well-known name of the embedded package-info file that every HPKG
 * archive must contain.
 *
 * @see BPackageReader, BPackageWriter
 */


#include <package/hpkg/HPKGDefs.h>


namespace BPackageKit {

namespace BHPKG {


/** @brief Name of the embedded package metadata file inside every HPKG archive. */
const char* const B_HPKG_PACKAGE_INFO_FILE_NAME = ".PackageInfo";


}	// namespace BHPKG

}	// namespace BPackageKit
