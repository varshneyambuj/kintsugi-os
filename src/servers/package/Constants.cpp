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
 * Copyright 2014, Ingo Weinhold, ingo_weinhold@gmx.de.
  * Distributed under the terms of the MIT License.
 */

/** @file Constants.cpp
 *  @brief Provides definitions for package daemon string constants */



#include "Constants.h"

#include <package/PackagesDirectoryDefs.h>


/** @brief File extension used by all package archive files. */
const char* const kPackageFileNameExtension = ".hpkg";

/** @brief Name of the administrative directory inside the packages directory. */
const char* const kAdminDirectoryName = PACKAGES_DIRECTORY_ADMIN_DIRECTORY;

/** @brief Name of the file listing currently activated packages. */
const char* const kActivationFileName = PACKAGES_DIRECTORY_ACTIVATION_FILE;

/** @brief Temporary activation file name used during atomic updates. */
const char* const kTemporaryActivationFileName
	= PACKAGES_DIRECTORY_ACTIVATION_FILE ".tmp";

/** @brief Flag file whose presence requests first-boot package processing. */
const char* const kFirstBootProcessingNeededFileName
	= "FirstBootProcessingNeeded";

/** @brief Name of the directory storing extracted writable files from packages. */
const char* const kWritableFilesDirectoryName = "writable-files";

/** @brief Extended attribute name used to tag files with their originating package. */
const char* const kPackageFileAttribute = "SYS:PACKAGE";

/** @brief Name of the directory holding symlinks to queued post-install scripts. */
const char* const kQueuedScriptsDirectoryName = "queued-scripts";
