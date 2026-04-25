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
 * @file FileSourceCode.cpp
 * @brief Implementation of FileSourceCode, a SourceCode realisation backed
 *        by an on-disk source file.
 *
 * FileSourceCode wraps a LocatableFile (the path), a SourceFile (the
 * tokenised line buffer), and a SourceLanguage (syntax descriptor). It
 * also maintains a sorted index of statement-start SourceLocations so the
 * debugger can map statements back to text ranges in the source view.
 */


#include "FileSourceCode.h"

#include <string.h>

#include "LocatableFile.h"
#include "SourceFile.h"
#include "SourceLanguage.h"
#include "SourceLocation.h"


/**
 * @brief Constructs a FileSourceCode and acquires references to its parts.
 *
 * @param file       Locatable on-disk file path; reference acquired.
 * @param sourceFile Tokenised source-line buffer; reference acquired.
 * @param language   Source-language descriptor; reference acquired.
 */
FileSourceCode::FileSourceCode(LocatableFile* file, SourceFile* sourceFile,
	SourceLanguage* language)
	:
	fLock("source code"),
	fFile(file),
	fSourceFile(sourceFile),
	fLanguage(language)
{
	fFile->AcquireReference();
	fSourceFile->AcquireReference();
	fLanguage->AcquireReference();
}


/**
 * @brief Releases references on the language, source file, and locatable file.
 */
FileSourceCode::~FileSourceCode()
{
	fLanguage->ReleaseReference();
	fSourceFile->ReleaseReference();
	fFile->ReleaseReference();
}


/**
 * @brief Performs deferred initialisation by checking the lock construction.
 *
 * @return The result of @c BLocker::InitCheck().
 */
status_t
FileSourceCode::Init()
{
	return fLock.InitCheck();
}


/**
 * @brief Inserts a statement-start source location into the sorted index.
 *
 * Inserts only when no equal location is already present.
 *
 * @param location Source location to remember as a statement start.
 * @return        @c B_OK on success or duplicate; @c B_NO_MEMORY on insert
 *                 failure.
 */
status_t
FileSourceCode::AddSourceLocation(const SourceLocation& location)
{
	// Find the insertion index; don't insert twice.
	bool foundMatch;
	int32 index = _FindSourceLocationIndex(location, foundMatch);
	if (foundMatch)
		return B_OK;

	return fSourceLocations.Insert(location, index) ? B_OK : B_NO_MEMORY;
}


/**
 * @brief Acquires the per-instance source-code lock.
 *
 * @return True if the lock was acquired.
 */
bool
FileSourceCode::Lock()
{
	return fLock.Lock();
}


/**
 * @brief Releases the per-instance source-code lock.
 */
void
FileSourceCode::Unlock()
{
	fLock.Unlock();
}


/**
 * @brief Returns the SourceLanguage that describes the file's syntax.
 *
 * @return Pointer to the language descriptor; the FileSourceCode keeps a
 *          reference, the caller must not release.
 */
SourceLanguage*
FileSourceCode::GetSourceLanguage() const
{
	return fLanguage;
}


/**
 * @brief Returns the number of source lines in the file.
 *
 * @return Line count from the underlying SourceFile.
 */
int32
FileSourceCode::CountLines() const
{
	return fSourceFile->CountLines();
}


/**
 * @brief Returns the text of the @a index'th source line.
 *
 * @param index Zero-based line index.
 * @return     Pointer to the NUL-terminated line text, or NULL if out of range.
 */
const char*
FileSourceCode::LineAt(int32 index) const
{
	return fSourceFile->LineAt(index);
}


/**
 * @brief Returns the byte length of the @a index'th source line.
 *
 * @param index Zero-based line index.
 * @return     Length in bytes excluding the line terminator.
 */
int32
FileSourceCode::LineLengthAt(int32 index) const
{
	return fSourceFile->LineLengthAt(index);
}


/**
 * @brief Computes the source location range covering the statement at @a location.
 *
 * Looks up the largest stored statement-start location that is less than
 * or equal to @a location and reports the span up to the next start (or
 * end-of-file).
 *
 * @param location Query location inside the file.
 * @param _start   Receives the statement start.
 * @param _end     Receives the statement end (exclusive).
 * @return        True if a covering range was found, false if the location
 *                 lies beyond the file or before the first known statement.
 */
bool
FileSourceCode::GetStatementLocationRange(const SourceLocation& location,
	SourceLocation& _start, SourceLocation& _end) const
{
	int32 lineCount = CountLines();
	if (location.Line() >= lineCount)
		return false;

	bool foundMatch;
	int32 index = _FindSourceLocationIndex(location, foundMatch);

	if (!foundMatch) {
		if (index == 0)
			return false;
		index--;
	}

	_start = fSourceLocations[index];
	_end = index + 1 < lineCount
		? fSourceLocations[index + 1] : SourceLocation(lineCount);
	return true;
}


/**
 * @brief Returns the LocatableFile carrying this source code's path.
 *
 * @return Pointer to the file; FileSourceCode retains a reference.
 */
LocatableFile*
FileSourceCode::GetSourceFile() const
{
	return fFile;
}


/**
 * @brief Binary-searches the sorted statement-start index for @a location.
 *
 * @param location    Query location.
 * @param _foundMatch Set true if an exactly equal stored location exists.
 * @return           Index of the matching entry, or the insertion index
 *                    when @a _foundMatch is false.
 */
int32
FileSourceCode::_FindSourceLocationIndex(const SourceLocation& location,
	bool& _foundMatch) const
{
	int32 lower = 0;
	int32 upper = fSourceLocations.Size();
	while (lower < upper) {
		int32 mid = (lower + upper) / 2;
		if (location <= fSourceLocations[mid])
			upper = mid;
		else
			lower = mid + 1;
	}

	_foundMatch = lower < fSourceLocations.Size()
		&& location == fSourceLocations[lower];
	return lower;
}
