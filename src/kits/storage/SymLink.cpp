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
 *   Copyright 2002-2009 Haiku, Inc. All rights reserved.
 *   Authors: Tyler Dauwalder, Ingo Weinhold, ingo_weinhold@gmx.de
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file SymLink.cpp
 * @brief Implements the BSymLink class for accessing and resolving symbolic links.
 *
 * BSymLink wraps a kernel file descriptor for a symbolic-link node and provides
 * operations to read the raw link target, construct an absolute resolved path
 * from a reference directory, and test whether the link target is absolute.
 * All constructors mirror those of BNode and accept entry_ref, BEntry, path
 * string, or directory-relative path forms.
 *
 * @see BNode
 * @see BEntry
 * @see BPath
 */

#include <new>
#include <string.h>

#include <SymLink.h>
#include <Directory.h>
#include <Entry.h>
#include <Path.h>

#include <syscalls.h>

#include "storage_support.h"


using namespace std;


/**
 * @brief Constructs a default, uninitialized BSymLink object.
 *
 * InitCheck() will return B_NO_INIT until a SetTo() overload is called
 * successfully.
 */
BSymLink::BSymLink()
{
}


/**
 * @brief Copy constructor; duplicates the node from \a other.
 *
 * @param other The BSymLink to copy.
 */
BSymLink::BSymLink(const BSymLink& other)
	:
	BNode(other)
{
}


/**
 * @brief Constructs a BSymLink and opens the symbolic link identified by \a ref.
 *
 * @param ref  Pointer to the entry_ref identifying the symbolic link.
 */
BSymLink::BSymLink(const entry_ref* ref)
	:
	BNode(ref)
{
}


/**
 * @brief Constructs a BSymLink and opens the symbolic link referred to by \a entry.
 *
 * @param entry  Pointer to the BEntry identifying the symbolic link.
 */
BSymLink::BSymLink(const BEntry* entry)
		: BNode(entry)
{
}


/**
 * @brief Constructs a BSymLink and opens the symbolic link at the given path.
 *
 * @param path  Absolute or relative filesystem path of the symbolic link.
 */
BSymLink::BSymLink(const char* path)
	:
	BNode(path)
{
}


/**
 * @brief Constructs a BSymLink and opens the symbolic link at \a path relative to \a dir.
 *
 * @param dir   The directory from which \a path is resolved.
 * @param path  Path relative to \a dir identifying the symbolic link.
 */
BSymLink::BSymLink(const BDirectory* dir, const char* path)
	:
	BNode(dir, path)
{
}


/**
 * @brief Destructor; closes the node file descriptor and releases all resources.
 */
BSymLink::~BSymLink()
{
}


/**
 * @brief Reads the raw contents (target path) of the symbolic link into \a buffer.
 *
 * If the link target fits in \a size bytes, a NUL terminator is appended.
 * Otherwise the last byte of \a buffer is set to NUL and the target is
 * truncated. The return value is the actual link length, not counting any NUL.
 *
 * @param buffer  Buffer to receive the link target string.
 * @param size    Size of \a buffer in bytes.
 * @return The length of the link target in bytes on success (may be larger
 *         than \a size if truncated), B_BAD_VALUE if \a buffer is NULL,
 *         B_FILE_ERROR if the object is uninitialized, or another error code.
 */
ssize_t
BSymLink::ReadLink(char* buffer, size_t size)
{
	if (buffer == NULL)
		return B_BAD_VALUE;

	if (InitCheck() != B_OK)
		return B_FILE_ERROR;

	size_t linkLen = size;
	status_t result = _kern_read_link(get_fd(), NULL, buffer, &linkLen);
	if (result < B_OK)
		return result;

	if (linkLen < size)
		buffer[linkLen] = '\0';
	else if (size > 0)
		buffer[size - 1] = '\0';

	return linkLen;
}


/**
 * @brief Resolves the symbolic link against a directory path to form an absolute path.
 *
 * \a dirPath is first converted to a BDirectory (resolving any symlinks in it),
 * then the link contents are combined with that directory. The directory must
 * exist.
 *
 * @param dirPath  Absolute or relative path of the reference directory.
 * @param path     BPath object to receive the resolved absolute path.
 * @return The length of the resolved path string on success, B_BAD_VALUE if
 *         either argument is NULL, or a negative error code on failure.
 */
ssize_t
BSymLink::MakeLinkedPath(const char* dirPath, BPath* path)
{
	// BeOS seems to convert the dirPath to a BDirectory, which causes links
	// to be resolved. This means that the dirPath must exist!
	if (dirPath == NULL || path == NULL)
		return B_BAD_VALUE;

	BDirectory dir(dirPath);
	ssize_t result = dir.InitCheck();
	if (result == B_OK)
		result = MakeLinkedPath(&dir, path);

	return result;
}


/**
 * @brief Resolves the symbolic link against a BDirectory to form an absolute path.
 *
 * If the link target is absolute it is used directly; otherwise it is
 * appended to the directory's path. The result is written to \a path.
 *
 * @param dir   Pointer to the BDirectory to use as the base for relative links.
 * @param path  BPath object to receive the resolved absolute path.
 * @return The length of the resolved path string on success, B_BAD_VALUE if
 *         either argument is NULL, or a negative error code on failure.
 */
ssize_t
BSymLink::MakeLinkedPath(const BDirectory* dir, BPath* path)
{
	if (dir == NULL || path == NULL)
		return B_BAD_VALUE;

	char contents[B_PATH_NAME_LENGTH];
	ssize_t result = ReadLink(contents, sizeof(contents));
	if (result >= 0) {
		if (BPrivate::Storage::is_absolute_path(contents))
			result = path->SetTo(contents);
		else
			result = path->SetTo(dir, contents);

		if (result == B_OK)
			result = strlen(path->Path());
	}

	return result;
}


/**
 * @brief Returns whether the symbolic link target is an absolute path.
 *
 * Reads the link contents and checks whether they start with '/'.
 *
 * @return \c true if the link target begins with '/', \c false if the link
 *         is relative or cannot be read.
 */
bool
BSymLink::IsAbsolute()
{
	char contents[B_PATH_NAME_LENGTH];
	bool result = (ReadLink(contents, sizeof(contents)) >= 0);
	if (result)
		result = BPrivate::Storage::is_absolute_path(contents);

	return result;
}


/** @brief Reserved virtual slot 1 (binary compatibility padding). */
void BSymLink::_MissingSymLink1() {}
/** @brief Reserved virtual slot 2 (binary compatibility padding). */
void BSymLink::_MissingSymLink2() {}
/** @brief Reserved virtual slot 3 (binary compatibility padding). */
void BSymLink::_MissingSymLink3() {}
/** @brief Reserved virtual slot 4 (binary compatibility padding). */
void BSymLink::_MissingSymLink4() {}
/** @brief Reserved virtual slot 5 (binary compatibility padding). */
void BSymLink::_MissingSymLink5() {}
/** @brief Reserved virtual slot 6 (binary compatibility padding). */
void BSymLink::_MissingSymLink6() {}


/*!	Returns the file descriptor of the BSymLink.

	This method should be used instead of accessing the private \c fFd member
	of the BNode directly.

	\return The object's file descriptor, or -1 if not properly initialized.
*/
/**
 * @brief Returns the underlying file descriptor for this symbolic link node.
 *
 * Use this accessor rather than reading the BNode private member fFd directly.
 *
 * @return The open file descriptor, or -1 if the BSymLink is not properly
 *         initialized.
 */
int
BSymLink::get_fd() const
{
	return fFd;
}
