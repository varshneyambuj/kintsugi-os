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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file storage_support.cpp
 * @brief Implementations of miscellaneous internal Storage Kit support
 *        functions.
 *
 * This file provides a collection of low-level utility functions used
 * internally by the Storage Kit. It includes path parsing and splitting
 * routines, entry and path name validation, case-folding helpers, shell
 * path escaping, and a device root detection helper. The FDCloser RAII
 * helper for managing file descriptors is also implemented here.
 *
 * @see storage_support.h
 */

#include <new>
#include <ctype.h>
#include <string.h>

#include <StorageDefs.h>
#include <SupportDefs.h>

#include <syscalls.h>

#include "storage_support.h"

using std::nothrow;

namespace BPrivate {
namespace Storage {

/**
 * @brief Checks whether the given path is an absolute path.
 *
 * @param path The path string to test.
 * @return true if \a path is non-NULL and begins with '/', false otherwise.
 */
bool
is_absolute_path(const char *path)
{
	return (path && path[0] == '/');
}

// parse_path
/**
 * @brief Parses the supplied path and returns the position of the leaf name
 *        part and the length of its directory path part.
 *
 * The value written to \a dirEnd is guaranteed to be > 0, so the function
 * always returns a non-empty directory path part. The leaf name part may be
 * empty (i.e. \a leafStart == \a leafEnd) when the path has only one
 * component.
 *
 * @param fullPath  The path to be parsed.
 * @param dirEnd    Reference filled with the exclusive end index of the
 *                  directory part.
 * @param leafStart Reference filled with the inclusive start index of the
 *                  leaf name part.
 * @param leafEnd   Reference filled with the exclusive end index of the
 *                  leaf name part.
 * @return B_OK on success, B_BAD_VALUE if \a fullPath is NULL or empty.
 */
status_t
parse_path(const char *fullPath, int &dirEnd, int &leafStart, int &leafEnd)
{
	// check path and get length
	if (!fullPath)
		return B_BAD_VALUE;
	int pathLen = strlen(fullPath);
	if (pathLen == 0)
		return B_BAD_VALUE;
	// find then end of the leaf name (skip trailing '/')
	int i = pathLen - 1;
	while (i >= 0 && fullPath[i] == '/')
		i--;
	leafEnd = i + 1;
	if (leafEnd == 0) {
		// fullPath consists of slashes only
		dirEnd = leafStart = leafEnd = 1;
		return B_OK;
	}
	// find the start of the leaf name
	while (i >= 0 && fullPath[i] != '/')
		i--;
	leafStart = i + 1;
	if (leafStart == 0) {
		// fullPath contains only one component
		dirEnd = leafStart = leafEnd;
		return B_OK;
	}
	// find the end of the dir path
	while (i >= 0 && fullPath[i] == '/')
		i--;
	dirEnd = i + 1;
	if (dirEnd == 0)	// => fullPath[0] == '/' (an absolute path)
		dirEnd = 1;
	return B_OK;
}

// parse_path
/**
 * @brief Parses the supplied path and returns the leaf name part and its
 *        directory path part as strings.
 *
 * The directory path returned is guaranteed to be non-empty. The leaf name
 * may be empty when the path consists of only one component.
 *
 * @param fullPath The path to be parsed.
 * @param dirPath  Pointer to a buffer of at least B_PATH_NAME_LENGTH bytes
 *                 to receive the directory part; may be NULL.
 * @param leaf     Pointer to a buffer of at least B_FILE_NAME_LENGTH bytes
 *                 to receive the leaf name; may be NULL.
 * @return B_OK on success, B_BAD_VALUE if \a fullPath is invalid,
 *         B_NAME_TOO_LONG if any component exceeds the size limits.
 */
status_t
parse_path(const char *fullPath, char *dirPath, char *leaf)
{
	// parse the path and check the lengths
	int leafStart, leafEnd, dirEnd;
	status_t error = parse_path(fullPath, dirEnd, leafStart, leafEnd);
	if (error != B_OK)
		return error;
	if (dirEnd >= B_PATH_NAME_LENGTH
		|| leafEnd - leafStart >= B_FILE_NAME_LENGTH) {
		return B_NAME_TOO_LONG;
	}
	// copy the result strings
	if (dirPath)
		strlcpy(dirPath, fullPath, dirEnd + 1);
	if (leaf)
		strlcpy(leaf, fullPath + leafStart, leafEnd - leafStart + 1);
	return B_OK;
}

// internal_parse_path
/**
 * @brief Internal helper that parses a path and returns raw index positions
 *        for the leaf name and path end.
 *
 * @param fullPath  The path to parse; if NULL the function returns immediately.
 * @param leafStart Set to the start index of the leaf name, or -1 if none.
 * @param leafEnd   Set to the end index of the leaf name, or -1 if none.
 * @param pathEnd   Set to the end index of the directory portion, or -2 if
 *                  the path ran out of characters before a '/' was found.
 */
static
void
internal_parse_path(const char *fullPath, int &leafStart, int &leafEnd,
	int &pathEnd)
{
	if (fullPath == NULL)
		return;

	enum PathParserState { PPS_START, PPS_LEAF } state = PPS_START;

	int len = strlen(fullPath);

	leafStart = -1;
	leafEnd = -1;
	pathEnd = -2;

	bool loop = true;
	for (int pos = len-1; ; pos--) {
		if (pos < 0)
			break;

		switch (state) {
			case PPS_START:
				// Skip all trailing '/' chars, then move on to
				// reading the leaf name
				if (fullPath[pos] != '/') {
					leafEnd = pos;
					state = PPS_LEAF;
				}
				break;

			case PPS_LEAF:
				// Read leaf name chars until we hit a '/' char
				if (fullPath[pos] == '/') {
					leafStart = pos+1;
					pathEnd = pos-1;
					loop = false;
				}
				break;
		}

		if (!loop)
			break;
	}
}

/**
 * @brief Splits a path into a newly allocated directory path and leaf name
 *        (reference overload).
 *
 * The caller is responsible for deleting the returned strings with delete[].
 *
 * @param fullPath The path name to split.
 * @param path     Reference to a pointer that will be set to the newly
 *                 allocated directory path string; may be left as NULL.
 * @param leaf     Reference to a pointer that will be set to the newly
 *                 allocated leaf name string; may be left as NULL.
 * @return B_OK on success, B_BAD_VALUE if \a fullPath is NULL,
 *         B_NO_MEMORY on allocation failure.
 */
status_t
split_path(const char *fullPath, char *&path, char *&leaf)
{
	return split_path(fullPath, &path, &leaf);
}

/**
 * @brief Splits a path into a newly allocated directory path and leaf name
 *        (pointer overload).
 *
 * The caller is responsible for deleting the returned strings with delete[].
 * Special cases handled: a path of "/" yields path="/" and leaf="."; an
 * empty path "" yields path="" and leaf=".".
 *
 * @param fullPath The path name to split.
 * @param path     Pointer to a pointer that will be set to the newly
 *                 allocated directory path string; may be NULL.
 * @param leaf     Pointer to a pointer that will be set to the newly
 *                 allocated leaf name string; may be NULL.
 * @return B_OK on success, B_BAD_VALUE if \a fullPath is NULL,
 *         B_NO_MEMORY on allocation failure.
 */
status_t
split_path(const char *fullPath, char **path, char **leaf)
{
	if (path)
		*path = NULL;
	if (leaf)
		*leaf = NULL;

	if (fullPath == NULL)
		return B_BAD_VALUE;

	int leafStart, leafEnd, pathEnd, len;
	internal_parse_path(fullPath, leafStart, leafEnd, pathEnd);

	try {
		// Tidy up/handle special cases
		if (leafEnd == -1) {

			// Handle special cases
			if (fullPath[0] == '/') {
				// Handle "/"
				if (path) {
					*path = new char[2];
					(*path)[0] = '/';
					(*path)[1] = 0;
				}
				if (leaf) {
					*leaf = new char[2];
					(*leaf)[0] = '.';
					(*leaf)[1] = 0;
				}
				return B_OK;
			} else if (fullPath[0] == 0) {
				// Handle "", which we'll treat as "./"
				if (path) {
					*path = new char[1];
					(*path)[0] = 0;
				}
				if (leaf) {
					*leaf = new char[2];
					(*leaf)[0] = '.';
					(*leaf)[1] = 0;
				}
				return B_OK;
			}

		} else if (leafStart == -1) {
			// fullPath is just an entry name, no parent directories specified
			leafStart = 0;
		} else if (pathEnd == -1) {
			// The path is '/' (since pathEnd would be -2 if we had
			// run out of characters before hitting a '/')
			pathEnd = 0;
		}

		// Alloc new strings and copy the path and leaf over
		if (path) {
			if (pathEnd == -2) {
				// empty path
				*path = new char[2];
				(*path)[0] = '.';
				(*path)[1] = 0;
			} else {
				// non-empty path
				len = pathEnd + 1;
				*path = new char[len+1];
				memcpy(*path, fullPath, len);
				(*path)[len] = 0;
			}
		}
		if (leaf) {
			len = leafEnd - leafStart + 1;
			*leaf = new char[len+1];
			memcpy(*leaf, fullPath + leafStart, len);
			(*leaf)[len] = 0;
		}
	} catch (std::bad_alloc& exception) {
		if (path)
			delete[] *path;
		if (leaf)
			delete[] *leaf;
		return B_NO_MEMORY;
	}
	return B_OK;
}

/**
 * @brief Parses the first component of a path and returns its length and the
 *        index at which the next component starts.
 *
 * @param path          The path to parse; must not be NULL.
 * @param length        Set to the character length of the first component.
 * @param nextComponent Set to the index of the next component, or 0 if there
 *                      is no next component.
 * @return B_OK on success, B_BAD_VALUE if \a path is NULL.
 */
status_t
parse_first_path_component(const char *path, int32& length,
						   int32& nextComponent)
{
	status_t error = (path ? B_OK : B_BAD_VALUE);
	if (error == B_OK) {
		int32 i = 0;
		// find first '/' or end of name
		for (; path[i] != '/' && path[i] != '\0'; i++);
		// handle special case "/..." (absolute path)
		if (i == 0 && path[i] != '\0')
			i = 1;
		length = i;
		// find last '/' or end of name
		for (; path[i] == '/' && path[i] != '\0'; i++);
		if (path[i] == '\0')	// this covers "" as well
			nextComponent = 0;
		else
			nextComponent = i;
	}
	return error;
}

/**
 * @brief Parses the first component of a path and returns it as a newly
 *        allocated string along with the index of the next component.
 *
 * The caller is responsible for freeing the returned \a component string
 * with delete[].
 *
 * @param path          The path to parse; must not be NULL.
 * @param component     Set to a newly allocated string holding the first
 *                      path component.
 * @param nextComponent Set to the index of the next component, or 0 if there
 *                      is no next component.
 * @return B_OK on success, B_BAD_VALUE if \a path is NULL,
 *         B_NO_MEMORY on allocation failure.
 */
status_t
parse_first_path_component(const char *path, char *&component,
						   int32& nextComponent)
{
	int32 length;
	status_t error = parse_first_path_component(path, length, nextComponent);
	if (error == B_OK) {
		component = new(nothrow) char[length + 1];
		if (component) {
			strncpy(component, path, length);
			component[length] = '\0';
		} else
			error = B_NO_MEMORY;
	}
	return error;
}

/**
 * @brief Checks whether an entry name is valid.
 *
 * An entry name is considered valid if its length does not exceed
 * B_FILE_NAME_LENGTH (including the null terminator) and it contains no
 * '/' characters. An empty string "" is considered valid.
 *
 * @param entry The entry name to validate.
 * @return B_OK if valid, B_BAD_VALUE if \a entry is NULL or contains a '/',
 *         B_NAME_TOO_LONG if the name is too long.
 */
status_t
check_entry_name(const char *entry)
{
	status_t error = (entry ? B_OK : B_BAD_VALUE);
	if (error == B_OK) {
		if (strlen(entry) >= B_FILE_NAME_LENGTH)
			error = B_NAME_TOO_LONG;
	}
	if (error == B_OK) {
		for (int32 i = 0; error == B_OK && entry[i] != '\0'; i++) {
			if (entry[i] == '/')
				error = B_BAD_VALUE;
		}
	}
	return error;
}

/**
 * @brief Checks whether a path name is valid.
 *
 * A path name is considered valid if its total length does not exceed
 * B_PATH_NAME_LENGTH and each of its components is a valid entry name. An
 * empty string "" is considered valid.
 *
 * @param path The path name to validate.
 * @return B_OK if valid, B_BAD_VALUE if \a path is NULL,
 *         B_NAME_TOO_LONG if the path or any component is too long.
 */
status_t
check_path_name(const char *path)
{
	// check the path is not NULL
	status_t error = (path ? B_OK : B_BAD_VALUE);
	if (error == B_BAD_VALUE)
		return error;
	// check the path components
	const char *remainder = path;
	int32 length, nextComponent;
	do {
		error = parse_first_path_component(remainder, length, nextComponent);
		if (error == B_OK) {
			if (length >= B_FILE_NAME_LENGTH)
				error = B_NAME_TOO_LONG;
			remainder += nextComponent;
		}
	} while (error == B_OK && nextComponent != 0);
	// check the length of the path
	if (error == B_OK && strlen(path) >= B_PATH_NAME_LENGTH)
		error = B_NAME_TOO_LONG;
	return error;
}

/**
 * @brief Returns a lowercase copy of the given string as an std::string.
 *
 * @param str The input C string; if NULL, returns "(null)".
 * @return An std::string containing the lowercased version of \a str.
 */
std::string
to_lower(const char *str)
{
	std::string result;
	to_lower(str, result);
	return result;
}

/**
 * @brief Converts the given string to lowercase and stores the result in
 *        an std::string.
 *
 * @param str    The input C string; if NULL, \a result is set to "(null)".
 * @param result The std::string that receives the lowercased output.
 */
void
to_lower(const char *str, std::string &result)
{
	if (str) {
		result = "";
		for (int i = 0; i < (int)strlen(str); i++)
			result += tolower((unsigned char)str[i]);
	} else
		result = "(null)";
}

/**
 * @brief Converts the given string to lowercase and writes the result into
 *        a caller-supplied C string buffer.
 *
 * @param str    The input C string.
 * @param result The output buffer that receives the lowercased string; must
 *               be at least as large as \a str including the null terminator.
 */
void
to_lower(const char *str, char *result)
{
	if (str && result) {
		int i;
		for (i = 0; i < (int)strlen(str); i++)
			result[i] = tolower((unsigned char)str[i]);
		result[i] = 0;
	}
}

/**
 * @brief Converts a string to lowercase in place.
 *
 * @param str The string to convert; modified in place.
 */
void
to_lower(char *str)
{
	to_lower(str, str);
}

/**
 * @brief Escapes shell-special characters in \a str and writes the result
 *        into \a result.
 *
 * Characters that are escaped with a preceding backslash include spaces,
 * single quotes, double quotes, question marks, backslashes, parentheses,
 * brackets, asterisks, and carets.
 *
 * @param str    The input path string.
 * @param result The output buffer; must be large enough to hold the escaped
 *               string (at most twice the length of \a str plus one byte).
 */
void escape_path(const char *str, char *result)
{
	if (str && result) {
		int32 len = strlen(str);

		for (int32 i = 0; i < len; i++) {
			char ch = str[i];
			char escapeChar = 0;

			switch (ch) {
				case ' ':
				case '\'':
				case '"':
				case '?':
				case '\\':
				case '(':
				case ')':
				case '[':
				case ']':
				case '*':
				case '^':
					escapeChar = ch;
					break;
			}

			if (escapeChar) {
				*(result++) = '\\';
				*(result++) = escapeChar;
			} else {
				*(result++) = ch;
			}
		}

		*result = 0;
	}
}

/**
 * @brief Escapes shell-special characters in the given string in place.
 *
 * Allocates a temporary copy, escapes it into \a str. If memory allocation
 * fails, \a str is left unchanged.
 *
 * @param str The string to escape in place; must point to a buffer large
 *            enough to hold the fully escaped result.
 */
void escape_path(char *str)
{
	if (str) {
		char *copy = new(nothrow) char[strlen(str)+1];
		if (copy) {
			strcpy(copy, str);
			escape_path(copy, str);
		}
		delete [] copy;
	}
}

// device_is_root_device
/**
 * @brief Checks whether the given device is the root device.
 *
 * @param device The device identifier to test.
 * @return true if \a device equals 1 (the root device), false otherwise.
 */
bool
device_is_root_device(dev_t device)
{
	return device == 1;
}

// Close
/**
 * @brief Closes the managed file descriptor, if open, and resets it to -1.
 *
 * This method is safe to call multiple times; subsequent calls after the
 * descriptor has been closed are no-ops.
 */
void
FDCloser::Close()
{
	if (fFD >= 0)
		_kern_close(fFD);
	fFD = -1;
}

};	// namespace Storage
};	// namespace BPrivate
