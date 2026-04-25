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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file SourceFile.cpp
 * @brief Reads and indexes a source file so the debugger can render lines on demand.
 *
 * Loads the entire source file into a single buffer, splits it on '\n',
 * and stores line-start offsets for O(1) line lookup. Capped at
 * @c kMaxSourceFileSize to avoid blowing memory on accidentally huge files.
 */

#include "SourceFile.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <new>


/** @brief Hard upper bound on the size of a source file the debugger will load (10 MiB). */
static const int32 kMaxSourceFileSize = 10 * 1024 * 1024;


// #pragma mark - SourceFileOwner


/** @brief Virtual destructor anchor for the SourceFileOwner interface. */
SourceFileOwner::~SourceFileOwner()
{
}


// #pragma mark - SourceFile


/**
 * @brief Construct an empty SourceFile bound to @a owner.
 *
 * Init() must be called before any line accessors are valid.
 *
 * @param owner  Owner that receives unused/deleted callbacks.
 */
SourceFile::SourceFile(SourceFileOwner* owner)
	:
	fOwner(owner),
	fFileContent(NULL),
	fLineOffsets(NULL),
	fLineCount(0)
{
}


/** @brief Free the file buffer and notify the owner of deletion. */
SourceFile::~SourceFile()
{
	free(fFileContent);
	delete[] fLineOffsets;
	fOwner->SourceFileDeleted(this);
}


/**
 * @brief Read @a path into memory and build the line index.
 *
 * Replaces newline characters with NULs in place so each line can be
 * returned as a C string. Allocates a parallel line-offset array.
 *
 * @param path  Absolute or relative path to the source file.
 * @retval B_OK              Loaded and indexed.
 * @retval B_FILE_TOO_LARGE  File exceeds @c kMaxSourceFileSize.
 * @retval B_BAD_VALUE       File is empty.
 * @retval B_FILE_ERROR      Short read.
 * @retval B_NO_MEMORY       Out of memory.
 * @return Other status codes propagated from open()/fstat()/read().
 */
status_t
SourceFile::Init(const char* path)
{
	// open the file
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return errno;

	// stat the file to get its size
	struct stat st;
	if (fstat(fd, &st) < 0) {
		close(fd);
		return errno;
	}

	if (st.st_size > kMaxSourceFileSize) {
		close(fd);
		return B_FILE_TOO_LARGE;
	}
	size_t fileSize = st.st_size;

	if (fileSize == 0) {
		close(fd);
		return B_BAD_VALUE;
	}

	// allocate the content buffer
	fFileContent = (char*)malloc(fileSize + 1);
		// one more byte for a terminating null
	if (fFileContent == NULL) {
		close(fd);
		return B_NO_MEMORY;
	}

	// read the file
	ssize_t bytesRead = read(fd, fFileContent, fileSize);
	close(fd);
	if (bytesRead < 0 || (size_t)bytesRead != fileSize)
		return bytesRead < 0 ? errno : B_FILE_ERROR;

	// null-terminate
	fFileContent[fileSize] = '\0';

	// count lines
	fLineCount = 1;
	for (size_t i = 0; i < fileSize; i++) {
		if (fFileContent[i] == '\n')
			fLineCount++;
	}

	// allocate line offset array
	fLineOffsets = new(std::nothrow) int32[fLineCount + 1];
	if (fLineOffsets == NULL)
		return B_NO_MEMORY;

	// get the line offsets and null-terminate the lines
	int32 lineIndex = 0;
	fLineOffsets[lineIndex++] = 0;
	for (size_t i = 0; i < fileSize; i++) {
		if (fFileContent[i] == '\n') {
			fFileContent[i] = '\0';
			fLineOffsets[lineIndex++] = i + 1;
		}
	}
	fLineOffsets[fLineCount] = fileSize + 1;

	return B_OK;
}


/** @brief Number of lines in the file (always at least 1 for non-empty files). */
int32
SourceFile::CountLines() const
{
	return fLineCount;
}


/**
 * @brief Pointer to the NUL-terminated text of line @a index.
 *
 * @param index  Zero-based line number.
 * @return Pointer to the line content, or NULL if @a index is out of range.
 */
const char*
SourceFile::LineAt(int32 index) const
{
	return index >= 0 && index < fLineCount
		? fFileContent + fLineOffsets[index] : NULL;
}


/**
 * @brief Length of line @a index in bytes, excluding the terminating NUL.
 *
 * @param index  Zero-based line number.
 * @return Length in bytes, or 0 if @a index is out of range.
 */
int32
SourceFile::LineLengthAt(int32 index) const
{
	return index >= 0 && index < fLineCount
		? fLineOffsets[index + 1] - fLineOffsets[index] - 1: 0;
}

/**
 * @brief BReferenceable hook invoked when the last reference is released.
 *
 * Notifies the owner that the file is unused, then suicides.
 */
void
SourceFile::LastReferenceReleased()
{
	fOwner->SourceFileUnused(this);
	delete this;
}
