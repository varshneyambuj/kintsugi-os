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
 *   Copyright 2004-2009, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file safemode_settings.cpp
 * @brief Readers for the boot-menu safemode settings file.
 *
 * The boot loader menu lets the user toggle safe-mode options (disable SMP,
 * disable swap, skip user add-ons, and so on) and persists the selections
 * into a driver_settings-style file. This module exposes the lookup entry
 * points used across the kernel (`get_safemode_option`, the `_early`
 * variants, and the `_user_get_safemode_option` syscall) plus a hand-rolled
 * parser that works before the heap is functional.
 */


#include <safemode.h>

#define _DEFAULT_SOURCE
#include <ctype.h>
#include <string.h>
#include <strings.h>

#include <algorithm>

#include <KernelExport.h>

#include <boot/kernel_args.h>
#include <kernel.h>
#include <syscalls.h>


#ifndef _BOOT_MODE


/**
 * @brief Parse a driver_settings file embedded in kernel_args for one option.
 *
 * Used during early boot before the heap and driver_settings infrastructure
 * are available. Walks the raw text buffer for the matching settings file,
 * performs minimal line-oriented tokenisation, and returns the first value
 * whose key matches `parameter`.
 *
 * @param args Kernel argument block carrying the preloaded settings files.
 * @param settingsName Name of the settings file to search (e.g. "safemode").
 * @param parameter Key to locate within that file.
 * @param parameterLength Length of `parameter` in bytes (no NUL required).
 * @param buffer Output buffer that receives the NUL-terminated value.
 * @param _bufferSize [in/out] Size of `buffer` on entry; required length on
 *                    return (may exceed the supplied buffer).
 * @return B_OK on match, B_ENTRY_NOT_FOUND if the settings file is absent,
 *         or B_NAME_NOT_FOUND if the key is not present.
 */
static status_t
get_option_from_kernel_args(kernel_args* args, const char* settingsName,
	const char* parameter, size_t parameterLength, char* buffer,
	size_t* _bufferSize)
{
	// find the settings in the kernel args
	const char* settings = NULL;
	for (driver_settings_file* file = args->driver_settings;
			file != NULL; file = file->next) {
		if (strcmp(settingsName, file->name) == 0) {
			settings = file->buffer;
			break;
		}
	}

	if (settings == NULL)
		return B_ENTRY_NOT_FOUND;

	// Unfortunately we can't just use parse_driver_settings_string(), since
	// we might not have a working heap yet. So we do very limited parsing
	// ourselves.
	int32 parameterLevel = 0;

	while (*settings != '\0') {
		// find end of line
		const char* lineEnd = strchrnul(settings, '\n');
		const char* nextLine;
		if (*lineEnd != '\0')
			nextLine = lineEnd + 1;
		else
			nextLine = lineEnd;

		// ignore any trailing comments
		const char* commentStart = (const char*)memchr(settings, '#', lineEnd - settings);
		if (commentStart != NULL)
			lineEnd = commentStart;

		const char* nameStart = NULL;
		const char* nameEnd = NULL;
		const char* valueStart = NULL;
		const char* valueEnd = NULL;
		const char** elementEnd = NULL;
		bool sawSeparator = true;

		for (; settings < lineEnd; settings++) {
			switch (*settings) {
				case '{':
					parameterLevel++;
					sawSeparator = true;
					break;

				case '}':
					parameterLevel--;
					sawSeparator = true;
					break;

				case ';':
					// TODO: That's not correct. There should be another loop.
					sawSeparator = true;
					break;

				default:
					if (parameterLevel != 0)
						break;

					if (isspace(*settings)) {
						sawSeparator = true;
						break;
					}

					if (!sawSeparator)
						break;

					sawSeparator = false;

					if (nameStart == NULL) {
						nameStart = settings;
						elementEnd = &nameEnd;
					} else if (valueStart == NULL) {
						valueStart = settings;
						elementEnd = &valueEnd;
					}

					if (*settings == '"' || *settings == '\'') {
						// Just take everything until the end of the line, and let
						// the caller deal with the quotations.
						settings = lineEnd;
						sawSeparator = true;
					}
					break;
			}

			if (sawSeparator && elementEnd != NULL) {
				*elementEnd = settings;
				elementEnd = NULL;
			}
		}

		if (elementEnd != NULL)
			*elementEnd = settings;

		if (nameStart != NULL && size_t(nameEnd - nameStart) == parameterLength
			&& strncmp(parameter, nameStart, parameterLength) == 0) {
			if (valueStart == NULL)
				return B_NAME_NOT_FOUND;

			size_t length = valueEnd - valueStart;
			if (*_bufferSize > 0) {
				size_t toCopy = std::min(length, *_bufferSize - 1);
				memcpy(buffer, valueStart, toCopy);
				buffer[toCopy] = '\0';
			}

			*_bufferSize = length;
			return B_OK;
		}

		settings = nextLine;
	}

	return B_NAME_NOT_FOUND;
}


#endif	// !_BOOT_MODE


/**
 * @brief Look up an option in a named settings file (early or runtime path).
 *
 * When called with a non-NULL `args` (and outside _BOOT_MODE) the raw kernel
 * args buffer is searched via get_option_from_kernel_args(). Otherwise the
 * driver_settings subsystem is used.
 *
 * @param args Early-boot kernel args, or NULL to use driver_settings.
 * @param settingsName Name of the settings file.
 * @param parameter Key to look up.
 * @param parameterLength Length of `parameter` in bytes.
 * @param buffer Output buffer for the value.
 * @param _bufferSize [in/out] Size of buffer; actual length on return.
 * @return B_OK on success, B_ENTRY_NOT_FOUND or B_NAME_NOT_FOUND on miss.
 */
static status_t
get_option(kernel_args* args, const char* settingsName, const char* parameter,
	size_t parameterLength, char* buffer, size_t* _bufferSize)
{
#ifndef _BOOT_MODE
	if (args != NULL) {
		return get_option_from_kernel_args(args, settingsName, parameter,
			parameterLength, buffer, _bufferSize);
	}
#endif

	void* handle = load_driver_settings(settingsName);
	if (handle == NULL)
		return B_ENTRY_NOT_FOUND;

	status_t status = B_NAME_NOT_FOUND;

	const char* value = get_driver_parameter(handle, parameter, NULL, NULL);
	if (value != NULL) {
		*_bufferSize = strlcpy(buffer, value, *_bufferSize);
		status = B_OK;
	}

	unload_driver_settings(handle);
	return status;
}


/**
 * @brief Look up `parameter` in the safemode file, falling back to "kernel".
 *
 * Convenience overload that hides the two fixed settings file names used by
 * the safemode mechanism: B_SAFEMODE_DRIVER_SETTINGS first, then "kernel".
 *
 * @param args Early-boot kernel args, or NULL for the driver_settings path.
 * @param parameter Key to look up.
 * @param buffer Output buffer for the value.
 * @param _bufferSize [in/out] Size of buffer; actual length on return.
 * @return B_OK on success or the last error code if neither file matched.
 */
static status_t
get_option(kernel_args* args, const char* parameter, char* buffer,
	size_t* _bufferSize)
{
	size_t parameterLength = strlen(parameter);
	status_t status = get_option(args, B_SAFEMODE_DRIVER_SETTINGS, parameter,
		parameterLength, buffer, _bufferSize);
	if (status != B_OK) {
		// Try kernel settings file as a fall back
		status = get_option(args, "kernel", parameter, parameterLength, buffer,
			_bufferSize);
	}

	return status;
}


/**
 * @brief Look up a boolean safemode option.
 *
 * Reads the raw string value via get_option() and accepts the usual
 * affirmative spellings (case-insensitive "on"/"true"/"yes"/"enabled" and
 * the literal "1"). Any other value resolves to `defaultValue`.
 *
 * @param args Early-boot kernel args, or NULL for the driver_settings path.
 * @param parameter Key to look up.
 * @param defaultValue Value returned when the option is missing.
 * @return Parsed boolean value.
 */
static bool
get_boolean(kernel_args* args, const char* parameter, bool defaultValue)
{
	char value[16];
	size_t length = sizeof(value);

	if (get_option(args, parameter, value, &length) != B_OK)
		return defaultValue;

	return !strcasecmp(value, "on") || !strcasecmp(value, "true")
		|| !strcmp(value, "1") || !strcasecmp(value, "yes")
		|| !strcasecmp(value, "enabled");
}


// #pragma mark -


/**
 * @brief Public accessor for a safemode option string.
 *
 * Thin wrapper around get_option() that always uses the runtime
 * driver_settings path.
 *
 * @param parameter Key to look up.
 * @param buffer Output buffer that receives the value string.
 * @param _bufferSize [in/out] Size of buffer; actual length on return.
 * @return B_OK on success or an error status on miss.
 */
status_t
get_safemode_option(const char* parameter, char* buffer, size_t* _bufferSize)
{
	return get_option(NULL, parameter, buffer, _bufferSize);
}


/**
 * @brief Public accessor for a safemode option interpreted as a boolean.
 *
 * @param parameter Key to look up.
 * @param defaultValue Value returned if the option is missing or invalid.
 * @return Parsed boolean value.
 */
bool
get_safemode_boolean(const char* parameter, bool defaultValue)
{
	return get_boolean(NULL, parameter, defaultValue);
}


#ifndef _BOOT_MODE


/**
 * @brief Early-boot safemode option lookup using raw kernel_args.
 *
 * Callable before the heap and driver_settings subsystem are available;
 * parses the settings text preloaded in `args` directly.
 *
 * @param args Kernel arguments carrying the preloaded settings buffers.
 * @param parameter Key to look up.
 * @param buffer Output buffer.
 * @param _bufferSize [in/out] Size of buffer; actual length on return.
 * @return B_OK on success or an error status on miss.
 */
status_t
get_safemode_option_early(kernel_args* args, const char* parameter,
	char* buffer, size_t* _bufferSize)
{
	return get_option(args, parameter, buffer, _bufferSize);
}


/**
 * @brief Early-boot boolean safemode lookup using raw kernel_args.
 *
 * @param args Kernel arguments carrying the preloaded settings buffers.
 * @param parameter Key to look up.
 * @param defaultValue Value returned if the option is missing or invalid.
 * @return Parsed boolean value.
 */
bool
get_safemode_boolean_early(kernel_args* args, const char* parameter,
	bool defaultValue)
{
	return get_boolean(args, parameter, defaultValue);
}


#endif	// _BOOT_MODE


//	#pragma mark - syscalls


#ifndef _BOOT_MODE


/**
 * @brief Syscall implementation of get_safemode_option() for userland.
 *
 * Validates user pointers, copies the parameter name and buffer size in,
 * invokes get_safemode_option(), and then copies the value and updated size
 * back out. All user pointer accesses go through user_memcpy/user_strlcpy
 * so that faults are caught rather than crashing the kernel.
 *
 * @param userParameter User-space pointer to the NUL-terminated key name.
 * @param userBuffer User-space output buffer for the value.
 * @param _userBufferSize User-space [in/out] size word.
 * @return B_BAD_ADDRESS if any user pointer fails validation, otherwise the
 *         status returned by get_safemode_option().
 */
extern "C" status_t
_user_get_safemode_option(const char* userParameter, char* userBuffer,
	size_t* _userBufferSize)
{
	char parameter[B_FILE_NAME_LENGTH];
	char buffer[B_PATH_NAME_LENGTH];
	size_t bufferSize, originalBufferSize;

	if (!IS_USER_ADDRESS(userParameter) || !IS_USER_ADDRESS(userBuffer)
		|| !IS_USER_ADDRESS(_userBufferSize)
		|| user_memcpy(&bufferSize, _userBufferSize, sizeof(size_t)) != B_OK
		|| user_strlcpy(parameter, userParameter, B_FILE_NAME_LENGTH) < B_OK)
		return B_BAD_ADDRESS;

	if (bufferSize > B_PATH_NAME_LENGTH)
		bufferSize = B_PATH_NAME_LENGTH;

	originalBufferSize = bufferSize;
	status_t status = get_safemode_option(parameter, buffer, &bufferSize);

	if (status == B_OK
		&& (user_strlcpy(userBuffer, buffer, originalBufferSize) < B_OK
			|| user_memcpy(_userBufferSize, &bufferSize, sizeof(size_t))
				!= B_OK))
		return B_BAD_ADDRESS;

	return status;
}


#endif	// !_BOOT_MODE
