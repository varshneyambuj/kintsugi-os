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
   /*
    * Copyright 2014, Ingo Weinhold, ingo_weinhold@gmx.de.
    * Distributed under the terms of the MIT License.
    */
 */

/** @file Constants.cpp
 *  @brief Provides definitions for package daemon string constants */



#include "Constants.h"

#include <package/PackagesDirectoryDefs.h>


const char* const kPackageFileNameExtension = ".hpkg";
const char* const kAdminDirectoryName = PACKAGES_DIRECTORY_ADMIN_DIRECTORY;
const char* const kActivationFileName = PACKAGES_DIRECTORY_ACTIVATION_FILE;
const char* const kTemporaryActivationFileName
	= PACKAGES_DIRECTORY_ACTIVATION_FILE ".tmp";
const char* const kFirstBootProcessingNeededFileName
	= "FirstBootProcessingNeeded";
const char* const kWritableFilesDirectoryName = "writable-files";
const char* const kPackageFileAttribute = "SYS:PACKAGE";
const char* const kQueuedScriptsDirectoryName = "queued-scripts";
