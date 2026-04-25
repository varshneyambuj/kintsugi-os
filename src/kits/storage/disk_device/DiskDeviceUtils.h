/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2007, Haiku.
 * Original author: Ingo Weinhold.
 */

/** @file DiskDeviceUtils.h
    @brief Small inline helpers for managing heap-allocated C strings used by
    disk-device job state. */

#ifndef _DISK_DEVICE_UTILS_H
#define _DISK_DEVICE_UTILS_H

#include <stdlib.h>
#include <string.h>

#include <SupportDefs.h>


namespace BPrivate {


// set_string
static inline status_t
set_string(char*& location, const char* newString)
{
	char* string = NULL;
	if (newString) {
		string = strdup(newString);
		if (!string)
			return B_NO_MEMORY;
	}

	free(location);
	location = string;

	return B_OK;
}


#define SET_STRING_RETURN_ON_ERROR(location, string)	\
{														\
	status_t error = set_string(location, string);		\
	if (error != B_OK)									\
		return error;									\
}


static inline int
compare_string(const char* a, const char* b)
{
	if (a == NULL)
		return (b == NULL ? 0 : -1);
	if (b == NULL)
		return 1;
	return strcmp(a, b);
}


}	// namespace BPrivate

using BPrivate::set_string;
using BPrivate::compare_string;

#endif	// _DISK_DEVICE_UTILS_H
