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
 * @file RemoveEngine.cpp
 * @brief Implementation of the BRemoveEngine file/directory removal engine.
 *
 * BRemoveEngine provides a flexible mechanism for recursively removing file
 * system entries. A BController callback interface allows callers to filter
 * entries, handle errors gracefully, and receive removal notifications.
 *
 * @see BRemoveEngine
 */

#include <RemoveEngine.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <Directory.h>
#include <Entry.h>
#include <Path.h>


namespace BPrivate {


// #pragma mark - BRemoveEngine


/**
 * @brief Constructs a BRemoveEngine with no controller installed.
 */
BRemoveEngine::BRemoveEngine()
	:
	fController(NULL)
{
}


/**
 * @brief Destructor.
 */
BRemoveEngine::~BRemoveEngine()
{
}


/**
 * @brief Returns the currently installed controller.
 *
 * @return Pointer to the BController, or NULL if none is set.
 */
BRemoveEngine::BController*
BRemoveEngine::Controller() const
{
	return fController;
}


/**
 * @brief Installs a controller that receives removal progress and error
 *        callbacks.
 *
 * @param controller Pointer to the BController to install (may be NULL).
 */
void
BRemoveEngine::SetController(BController* controller)
{
	fController = controller;
}


/**
 * @brief Removes the file system entry described by the given Entry object.
 *
 * Resolves the entry to a path and delegates to _RemoveEntry().
 *
 * @param entry The entry to remove.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BRemoveEngine::RemoveEntry(const Entry& entry)
{
	BPath pathBuffer;
	const char* path;
	status_t error = entry.GetPath(pathBuffer, path);
	if (error != B_OK)
		return error;

	return _RemoveEntry(path);
}


/**
 * @brief Internal recursive implementation that removes an entry by path.
 *
 * Applies the entry filter via the controller, stats the entry, recurses into
 * directory children if necessary, and then removes the entry itself using
 * rmdir(2) or unlink(2).
 *
 * @param path Absolute path of the entry to remove.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BRemoveEngine::_RemoveEntry(const char* path)
{
	// apply entry filter
	if (fController != NULL && !fController->EntryStarted(path))
		return B_OK;

	// stat entry
	struct stat st;
	if (lstat(path, &st) < 0) {
		return _HandleEntryError(path, errno, "Couldn't access \"%s\": %s\n",
			path, strerror(errno));
	}

	// recurse, if entry is a directory
	if (S_ISDIR(st.st_mode)) {
		// open directory
		BDirectory directory;
		status_t error = directory.SetTo(path);
		if (error != B_OK) {
			return _HandleEntryError(path, error,
				"Failed to open directory \"%s\": %s\n", path, strerror(error));
		}

		char buffer[offsetof(struct dirent, d_name) + B_FILE_NAME_LENGTH];
		dirent *entry = (dirent*)buffer;
		while (directory.GetNextDirents(entry, sizeof(buffer), 1) == 1) {
			if (strcmp(entry->d_name, ".") == 0
				|| strcmp(entry->d_name, "..") == 0) {
				continue;
			}

			// construct child entry path
			BPath childPath;
			error = childPath.SetTo(path, entry->d_name);
			if (error != B_OK) {
				return _HandleEntryError(path, error,
					"Failed to construct entry path from dir \"%s\" and name "
					"\"%s\": %s\n", path, entry->d_name, strerror(error));
			}

			// remove the entry
			error = _RemoveEntry(childPath.Path());
			if (error != B_OK) {
				if (fController != NULL
					&& fController->EntryFinished(path, error)) {
					return B_OK;
				}
				return error;
			}
		}
	}

	// remove entry
	if (S_ISDIR(st.st_mode)) {
		if (rmdir(path) < 0) {
			return _HandleEntryError(path, errno,
				"Failed to remove \"%s\": %s\n", path, strerror(errno));
		}
	} else {
		if (unlink(path) < 0) {
			return _HandleEntryError(path, errno,
				"Failed to unlink \"%s\": %s\n", path, strerror(errno));
		}
	}

	if (fController != NULL)
		fController->EntryFinished(path, B_OK);

	return B_OK;
}


/**
 * @brief Notifies the controller of an error using a va_list argument list.
 *
 * @param error  The error code to report.
 * @param format printf-style format string for the human-readable message.
 * @param args   Pre-initialized va_list of format arguments.
 */
void
BRemoveEngine::_NotifyErrorVarArgs(status_t error, const char* format,
	va_list args)
{
	if (fController != NULL) {
		BString message;
		message.SetToFormatVarArgs(format, args);
		fController->ErrorOccurred(message, error);
	}
}


/**
 * @brief Reports an entry-level error and queries the controller whether to
 *        continue or propagate the error.
 *
 * @param path   Path of the entry that caused the error.
 * @param error  The error code.
 * @param format printf-style format string for the human-readable message.
 * @param ...    Format arguments.
 * @return B_OK if the controller allows the operation to continue, otherwise
 *         the original error code.
 */
status_t
BRemoveEngine::_HandleEntryError(const char* path, status_t error,
	const char* format, ...)
{
	if (fController == NULL)
		return error;

	va_list args;
	va_start(args, format);
	_NotifyErrorVarArgs(error, format, args);
	va_end(args);

	if (fController->EntryFinished(path, error))
		return B_OK;
	return error;
}


// #pragma mark - BController


/**
 * @brief Default constructor for BController.
 */
BRemoveEngine::BController::BController()
{
}


/**
 * @brief Destructor for BController.
 */
BRemoveEngine::BController::~BController()
{
}


/**
 * @brief Called before an entry is removed; returns whether to remove it.
 *
 * @param path Path of the entry about to be removed.
 * @return true to proceed with removal, false to skip this entry.
 */
bool
BRemoveEngine::BController::EntryStarted(const char* path)
{
	return true;
}


/**
 * @brief Called after an entry has been processed; returns whether to continue.
 *
 * @param path  Path of the entry that was just processed.
 * @param error B_OK if the entry was removed successfully, or an error code.
 * @return true to continue removing remaining entries, false to abort.
 */
bool
BRemoveEngine::BController::EntryFinished(const char* path, status_t error)
{
	return error == B_OK;
}


/**
 * @brief Called when a non-fatal error occurs during the removal operation.
 *
 * @param message Human-readable description of the error.
 * @param error   The error code.
 */
void
BRemoveEngine::BController::ErrorOccurred(const char* message, status_t error)
{
}


} // namespace BPrivate
