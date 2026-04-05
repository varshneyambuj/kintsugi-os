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
 *   Copyright 2013, Haiku, Inc. All Rights Reserved.
 *   Authors: Ingo Weinhold <ingo_weinhold@gmx.de>
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file CopyEngine.cpp
 * @brief Implementation of the BCopyEngine file/directory copy engine.
 *
 * BCopyEngine provides a flexible mechanism for copying file system entries,
 * including regular files, directories (recursively), symbolic links, and
 * extended attributes. A BController callback interface allows callers to
 * filter entries, handle errors gracefully, and receive progress notifications.
 *
 * @see BCopyEngine
 */

#include <CopyEngine.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <fs_attr.h>
#include <Path.h>
#include <SymLink.h>
#include <TypeConstants.h>


namespace BPrivate {


static const size_t kDefaultBufferSize = 1024 * 1024;
static const size_t kSmallBufferSize = 64 * 1024;


// #pragma mark - BCopyEngine


/**
 * @brief Constructs a BCopyEngine with the given operational flags.
 *
 * @param flags Combination of BCopyEngine flag constants controlling copy
 *              behaviour (e.g. COPY_RECURSIVELY, UNLINK_DESTINATION).
 */
BCopyEngine::BCopyEngine(uint32 flags)
	:
	fController(NULL),
	fFlags(flags),
	fBuffer(NULL),
	fBufferSize(0)
{
}


/**
 * @brief Destructor. Releases the internal I/O buffer.
 */
BCopyEngine::~BCopyEngine()
{
	delete[] fBuffer;
}


/**
 * @brief Returns the currently installed controller.
 *
 * @return Pointer to the BController, or NULL if none is set.
 */
BCopyEngine::BController*
BCopyEngine::Controller() const
{
	return fController;
}


/**
 * @brief Installs a controller that receives copy progress and error callbacks.
 *
 * @param controller Pointer to the BController to install (may be NULL).
 */
void
BCopyEngine::SetController(BController* controller)
{
	fController = controller;
}


/**
 * @brief Returns the current copy flags.
 *
 * @return The flags bitmask currently in effect.
 */
uint32
BCopyEngine::Flags() const
{
	return fFlags;
}


/**
 * @brief Replaces all current flags with the supplied value.
 *
 * @param flags New flags bitmask.
 * @return Reference to this engine (for chaining).
 */
BCopyEngine&
BCopyEngine::SetFlags(uint32 flags)
{
	fFlags = flags;
	return *this;
}


/**
 * @brief Adds the given flags to the current flags bitmask.
 *
 * @param flags Flags to add.
 * @return Reference to this engine (for chaining).
 */
BCopyEngine&
BCopyEngine::AddFlags(uint32 flags)
{
	fFlags |= flags;
	return *this;
}


/**
 * @brief Removes the given flags from the current flags bitmask.
 *
 * @param flags Flags to remove.
 * @return Reference to this engine (for chaining).
 */
BCopyEngine&
BCopyEngine::RemoveFlags(uint32 flags)
{
	fFlags &= ~flags;
	return *this;
}


/**
 * @brief Copies a single file system entry from sourceEntry to destEntry.
 *
 * Allocates the I/O buffer on first use and resolves both Entry objects to
 * path strings before delegating to _CopyEntry().
 *
 * @param sourceEntry The entry to copy from.
 * @param destEntry   The entry to copy to.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BCopyEngine::CopyEntry(const Entry& sourceEntry, const Entry& destEntry)
{
	if (fBuffer == NULL) {
		fBuffer = new(std::nothrow) char[kDefaultBufferSize];
		if (fBuffer == NULL) {
			fBuffer = new(std::nothrow) char[kSmallBufferSize];
			if (fBuffer == NULL) {
				_NotifyError(B_NO_MEMORY, "Failed to allocate buffer");
				return B_NO_MEMORY;
			}
			fBufferSize = kSmallBufferSize;
		} else
			fBufferSize = kDefaultBufferSize;
	}

	BPath sourcePathBuffer;
	const char* sourcePath;
	status_t error = sourceEntry.GetPath(sourcePathBuffer, sourcePath);
	if (error != B_OK)
		return error;

	BPath destPathBuffer;
	const char* destPath;
	error = destEntry.GetPath(destPathBuffer, destPath);
	if (error != B_OK)
		return error;

	return _CopyEntry(sourcePath, destPath);
}


/**
 * @brief Internal recursive implementation that copies a single entry by path.
 *
 * Handles entry filtering via the controller, stat inspection, destination
 * unlinking, node creation, file data copying, attribute copying, permission
 * propagation, and optional recursive descent into directories.
 *
 * @param sourcePath Absolute path of the source entry.
 * @param destPath   Absolute path of the destination entry.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BCopyEngine::_CopyEntry(const char* sourcePath, const char* destPath)
{
	// apply entry filter
	if (fController != NULL && !fController->EntryStarted(sourcePath))
		return B_OK;

	// stat source
	struct stat sourceStat;
	if (lstat(sourcePath, &sourceStat) < 0) {
		return _HandleEntryError(sourcePath, errno,
			"Couldn't access \"%s\": %s\n", sourcePath, strerror(errno));
	}

	// stat destination
	struct stat destStat;
	bool destExists = lstat(destPath, &destStat) == 0;

	// check whether to delete/create the destination
	bool unlinkDest = destExists;
	bool createDest = true;
	if (destExists) {
		if (S_ISDIR(destStat.st_mode)) {
			if (!S_ISDIR(sourceStat.st_mode)
				|| (fFlags & MERGE_EXISTING_DIRECTORIES) == 0) {
				return _HandleEntryError(sourcePath, B_FILE_EXISTS,
					"Can't copy \"%s\", since directory \"%s\" is in the "
					"way.\n", sourcePath, destPath);
			}

			if (S_ISDIR(sourceStat.st_mode)) {
				// both are dirs; nothing to do
				unlinkDest = false;
				destExists = false;
			}
		} else if ((fFlags & UNLINK_DESTINATION) == 0) {
			return _HandleEntryError(sourcePath, B_FILE_EXISTS,
				"Can't copy \"%s\", since entry \"%s\" is in the way.\n",
				sourcePath, destPath);
		}
	}

	// unlink the destination
	if (unlinkDest) {
		if (unlink(destPath) < 0) {
			return _HandleEntryError(sourcePath, errno,
				"Failed to unlink \"%s\": %s\n", destPath, strerror(errno));
		}
	}

	// open source node
	BNode _sourceNode;
	BFile sourceFile;
	BDirectory sourceDir;
	BNode* sourceNode = NULL;
	status_t error;

	if (S_ISDIR(sourceStat.st_mode)) {
		error = sourceDir.SetTo(sourcePath);
		sourceNode = &sourceDir;
	} else if (S_ISREG(sourceStat.st_mode)) {
		error = sourceFile.SetTo(sourcePath, B_READ_ONLY);
		sourceNode = &sourceFile;
	} else {
		error = _sourceNode.SetTo(sourcePath);
		sourceNode = &_sourceNode;
	}

	if (error != B_OK) {
		return _HandleEntryError(sourcePath, error,
			"Failed to open \"%s\": %s\n", sourcePath, strerror(error));
	}

	// create the destination
	BNode _destNode;
	BDirectory destDir;
	BFile destFile;
	BSymLink destSymLink;
	BNode* destNode = NULL;

	if (createDest) {
		if (S_ISDIR(sourceStat.st_mode)) {
			// create dir
			error = BDirectory().CreateDirectory(destPath, &destDir);
			if (error != B_OK) {
				return _HandleEntryError(sourcePath, error,
					"Failed to make directory \"%s\": %s\n", destPath,
					strerror(error));
			}

			destNode = &destDir;
		} else if (S_ISREG(sourceStat.st_mode)) {
			// create file
			error = BDirectory().CreateFile(destPath, &destFile);
			if (error != B_OK) {
				return _HandleEntryError(sourcePath, error,
					"Failed to create file \"%s\": %s\n", destPath,
					strerror(error));
			}

			destNode = &destFile;

			// copy file contents
			error = _CopyFileData(sourcePath, sourceFile, destPath, destFile);
			if (error != B_OK) {
				if (fController != NULL
					&& fController->EntryFinished(sourcePath, error)) {
					return B_OK;
				}
				return error;
			}
		} else if (S_ISLNK(sourceStat.st_mode)) {
			// read symlink
			char* linkTo = fBuffer;
			ssize_t bytesRead = readlink(sourcePath, linkTo, fBufferSize - 1);
			if (bytesRead < 0) {
				return _HandleEntryError(sourcePath, errno,
					"Failed to read symlink \"%s\": %s\n", sourcePath,
					strerror(errno));
			}

			// null terminate the link contents
			linkTo[bytesRead] = '\0';

			// create symlink
			error = BDirectory().CreateSymLink(destPath, linkTo, &destSymLink);
			if (error != B_OK) {
				return _HandleEntryError(sourcePath, error,
					"Failed to create symlink \"%s\": %s\n", destPath,
					strerror(error));
			}

			destNode = &destSymLink;

		} else {
			return _HandleEntryError(sourcePath, B_NOT_SUPPORTED,
				"Source file \"%s\" has unsupported type.\n", sourcePath);
		}

		// copy attributes (before setting the permissions!)
		error = _CopyAttributes(sourcePath, *sourceNode, destPath, *destNode);
		if (error != B_OK) {
			if (fController != NULL
				&& fController->EntryFinished(sourcePath, error)) {
				return B_OK;
			}
			return error;
		}

		// set file owner, group, permissions, times
		destNode->SetOwner(sourceStat.st_uid);
		destNode->SetGroup(sourceStat.st_gid);
		destNode->SetPermissions(sourceStat.st_mode);
		#ifdef HAIKU_TARGET_PLATFORM_HAIKU
			destNode->SetCreationTime(sourceStat.st_crtime);
		#endif
		destNode->SetModificationTime(sourceStat.st_mtime);
	}

	// the destination node is no longer needed
	destNode->Unset();

	// recurse
	if ((fFlags & COPY_RECURSIVELY) != 0 && S_ISDIR(sourceStat.st_mode)) {
		char buffer[offsetof(struct dirent, d_name) + B_FILE_NAME_LENGTH];
		dirent *entry = (dirent*)buffer;
		while (sourceDir.GetNextDirents(entry, sizeof(buffer), 1) == 1) {
			if (strcmp(entry->d_name, ".") == 0
				|| strcmp(entry->d_name, "..") == 0) {
				continue;
			}

			// construct new entry paths
			BPath sourceEntryPath;
			error = sourceEntryPath.SetTo(sourcePath, entry->d_name);
			if (error != B_OK) {
				return _HandleEntryError(sourcePath, error,
					"Failed to construct entry path from dir \"%s\" and name "
					"\"%s\": %s\n", sourcePath, entry->d_name, strerror(error));
			}

			BPath destEntryPath;
			error = destEntryPath.SetTo(destPath, entry->d_name);
			if (error != B_OK) {
				return _HandleEntryError(sourcePath, error,
					"Failed to construct entry path from dir \"%s\" and name "
					"\"%s\": %s\n", destPath, entry->d_name, strerror(error));
			}

			// copy the entry
			error = _CopyEntry(sourceEntryPath.Path(), destEntryPath.Path());
			if (error != B_OK) {
				if (fController != NULL
					&& fController->EntryFinished(sourcePath, error)) {
					return B_OK;
				}
				return error;
			}
		}
	}

	if (fController != NULL)
		fController->EntryFinished(sourcePath, B_OK);
	return B_OK;
}


/**
 * @brief Copies the raw byte data from one open BFile to another.
 *
 * Reads the source file in chunks using the engine's I/O buffer and writes
 * each chunk to the destination file at the same offset.
 *
 * @param sourcePath  Path of the source file (used in error messages).
 * @param source      Open source BFile.
 * @param destPath    Path of the destination file (used in error messages).
 * @param destination Open destination BFile.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BCopyEngine::_CopyFileData(const char* sourcePath, BFile& source,
	const char* destPath, BFile& destination)
{
	off_t offset = 0;
	while (true) {
		// read
		ssize_t bytesRead = source.ReadAt(offset, fBuffer, fBufferSize);
		if (bytesRead < 0) {
			_NotifyError(bytesRead, "Failed to read from file \"%s\": %s\n",
				sourcePath, strerror(bytesRead));
			return bytesRead;
		}

		if (bytesRead == 0)
			return B_OK;

		// write
		ssize_t bytesWritten = destination.WriteAt(offset, fBuffer, bytesRead);
		if (bytesWritten < 0) {
			_NotifyError(bytesWritten, "Failed to write to file \"%s\": %s\n",
				destPath, strerror(bytesWritten));
			return bytesWritten;
		}

		if (bytesWritten != bytesRead) {
			_NotifyError(B_ERROR, "Failed to write all data to file \"%s\"\n",
				destPath);
			return B_ERROR;
		}

		offset += bytesRead;
	}
}


/**
 * @brief Copies all extended attributes from one node to another.
 *
 * Iterates over every attribute on the source node, optionally filters via
 * the controller, reads the attribute data in chunks, and writes it to the
 * destination node.
 *
 * @param sourcePath  Path of the source node (used in error messages).
 * @param source      Source BNode whose attributes are read.
 * @param destPath    Path of the destination node (used in error messages).
 * @param destination Destination BNode that receives the attributes.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BCopyEngine::_CopyAttributes(const char* sourcePath, BNode& source,
	const char* destPath, BNode& destination)
{
	char attrName[B_ATTR_NAME_LENGTH];
	while (source.GetNextAttrName(attrName) == B_OK) {
		// get attr info
		attr_info attrInfo;
		status_t error = source.GetAttrInfo(attrName, &attrInfo);
		if (error != B_OK) {
			// Delay reporting/handling the error until the controller has been
			// asked whether it is interested.
			attrInfo.type = B_ANY_TYPE;
		}

		// filter
		if (fController != NULL
			&& !fController->AttributeStarted(sourcePath, attrName,
				attrInfo.type)) {
			if (error != B_OK) {
				_NotifyError(error, "Failed to get info of attribute \"%s\" "
					"of file \"%s\": %s\n", attrName, sourcePath,
					strerror(error));
			}
			continue;
		}

		if (error != B_OK) {
			error = _HandleAttributeError(sourcePath, attrName, attrInfo.type,
				error, "Failed to get info of attribute \"%s\" of file \"%s\": "
				"%s\n", attrName, sourcePath, strerror(error));
			if (error != B_OK)
				return error;
			continue;
		}

		// copy the attribute
		off_t offset = 0;
		off_t bytesLeft = attrInfo.size;
		// go at least once through the loop, so that an empty attribute will be
		// created as well
		do {
			size_t toRead = fBufferSize;
			if ((off_t)toRead > bytesLeft)
				toRead = bytesLeft;

			// read
			ssize_t bytesRead = source.ReadAttr(attrName, attrInfo.type,
				offset, fBuffer, toRead);
			if (bytesRead < 0) {
				error = _HandleAttributeError(sourcePath, attrName,
					attrInfo.type, bytesRead, "Failed to read attribute \"%s\" "
					"of file \"%s\": %s\n", attrName, sourcePath,
					strerror(bytesRead));
				if (error != B_OK)
					return error;
				break;
			}

			if (bytesRead == 0 && offset > 0)
				break;

			// write
			ssize_t bytesWritten = destination.WriteAttr(attrName,
				attrInfo.type, offset, fBuffer, bytesRead);
			if (bytesWritten < 0) {
				error = _HandleAttributeError(sourcePath, attrName,
					attrInfo.type, bytesWritten, "Failed to write attribute "
					"\"%s\" of file \"%s\": %s\n", attrName, destPath,
					strerror(bytesWritten));
				if (error != B_OK)
					return error;
				break;
			}

			bytesLeft -= bytesRead;
			offset += bytesRead;
		} while (bytesLeft > 0);

		if (fController != NULL) {
			fController->AttributeFinished(sourcePath, attrName, attrInfo.type,
				B_OK);
		}
	}

	return B_OK;
}


/**
 * @brief Notifies the controller of an error using a printf-style format string.
 *
 * @param error  The error code to report.
 * @param format printf-style format string for the human-readable message.
 * @param ...    Format arguments.
 */
void
BCopyEngine::_NotifyError(status_t error, const char* format, ...)
{
	if (fController != NULL) {
		va_list args;
		va_start(args, format);
		_NotifyErrorVarArgs(error, format, args);
		va_end(args);
	}
}


/**
 * @brief Notifies the controller of an error using a va_list argument list.
 *
 * @param error  The error code to report.
 * @param format printf-style format string for the human-readable message.
 * @param args   Pre-initialized va_list of format arguments.
 */
void
BCopyEngine::_NotifyErrorVarArgs(status_t error, const char* format,
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
BCopyEngine::_HandleEntryError(const char* path, status_t error,
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


/**
 * @brief Reports an attribute-level error and queries the controller whether
 *        to continue or propagate the error.
 *
 * @param path          Path of the file whose attribute caused the error.
 * @param attribute     Name of the attribute that caused the error.
 * @param attributeType Type code of the attribute.
 * @param error         The error code.
 * @param format        printf-style format string for the human-readable message.
 * @param ...           Format arguments.
 * @return B_OK if the controller allows the operation to continue, otherwise
 *         the original error code.
 */
status_t
BCopyEngine::_HandleAttributeError(const char* path, const char* attribute,
	uint32 attributeType, status_t error, const char* format, ...)
{
	if (fController == NULL)
		return error;

	va_list args;
	va_start(args, format);
	_NotifyErrorVarArgs(error, format, args);
	va_end(args);

	if (fController->AttributeFinished(path, attribute, attributeType, error))
		return B_OK;
	return error;
}


// #pragma mark - BController


/**
 * @brief Default constructor for BController.
 */
BCopyEngine::BController::BController()
{
}


/**
 * @brief Destructor for BController.
 */
BCopyEngine::BController::~BController()
{
}


/**
 * @brief Called before an entry is processed; returns whether to copy it.
 *
 * @param path Path of the entry about to be copied.
 * @return true to proceed with the copy, false to skip this entry.
 */
bool
BCopyEngine::BController::EntryStarted(const char* path)
{
	return true;
}


/**
 * @brief Called after an entry has been processed; returns whether to continue.
 *
 * @param path  Path of the entry that was just processed.
 * @param error B_OK if the entry was copied successfully, or an error code.
 * @return true to continue copying remaining entries, false to abort.
 */
bool
BCopyEngine::BController::EntryFinished(const char* path, status_t error)
{
	return error == B_OK;
}


/**
 * @brief Called before an attribute is copied; returns whether to copy it.
 *
 * @param path          Path of the file whose attribute is about to be copied.
 * @param attribute     Name of the attribute.
 * @param attributeType Type code of the attribute.
 * @return true to proceed with the attribute copy, false to skip it.
 */
bool
BCopyEngine::BController::AttributeStarted(const char* path,
	const char* attribute, uint32 attributeType)
{
	return true;
}


/**
 * @brief Called after an attribute has been processed; returns whether to
 *        continue.
 *
 * @param path          Path of the file whose attribute was processed.
 * @param attribute     Name of the attribute.
 * @param attributeType Type code of the attribute.
 * @param error         B_OK on success, or an error code.
 * @return true to continue with remaining attributes, false to abort.
 */
bool
BCopyEngine::BController::AttributeFinished(const char* path,
	const char* attribute, uint32 attributeType, status_t error)
{
	return error == B_OK;
}


/**
 * @brief Called when a non-fatal error occurs during the copy operation.
 *
 * @param message Human-readable description of the error.
 * @param error   The error code.
 */
void
BCopyEngine::BController::ErrorOccurred(const char* message, status_t error)
{
}


} // namespace BPrivate
