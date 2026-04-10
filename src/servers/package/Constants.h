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

/** @file Constants.h
 *  @brief Defines daemon-wide constants for file names, timeouts, and message codes */

#ifndef CONSTANTS_H
#define CONSTANTS_H


#include <SupportDefs.h>


extern const char* const kPackageFileNameExtension;
extern const char* const kAdminDirectoryName;
extern const char* const kActivationFileName;
extern const char* const kTemporaryActivationFileName;
extern const char* const kFirstBootProcessingNeededFileName;
extern const char* const kWritableFilesDirectoryName;
extern const char* const kPackageFileAttribute;
extern const char* const kQueuedScriptsDirectoryName;

static const bigtime_t kHandleNodeMonitorEvents = 'nmon';

static const bigtime_t kNodeMonitorEventHandlingDelay = 500000;
static const bigtime_t kCommunicationTimeout = 1000000;

// sanity limit for activation file size
static const size_t kMaxActivationFileSize = 10 * 1024 * 1024;


#endif	// CONSTANTS_H
