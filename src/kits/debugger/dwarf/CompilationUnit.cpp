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
 *   Copyright 2013, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file CompilationUnit.cpp
 * @brief Implementation of CompilationUnit, a single .debug_info CU.
 *
 * Adds the CU-specific state on top of @ref BaseUnit: the line number
 * program (.debug_line entry decoded for this CU), the directory and
 * file tables it references, the address-range list it covers, and the
 * top-level DIECompileUnitBase DIE.
 */


#include "CompilationUnit.h"

#include <new>

#include "DebugInfoEntries.h"
#include "TargetAddressRangeList.h"


/**
 * @brief Internal record pairing a file name with its directory entry.
 */
struct CompilationUnit::File {
	BString		fileName;
	const char*	dirName;


	File(const char* fileName, const char* dirName)
		:
		fileName(fileName),
		dirName(dirName)
	{
	}
};


/**
 * @brief Constructs a compilation unit with empty file/directory tables.
 *
 * @param headerOffset       Section offset of the unit header.
 * @param contentOffset      Offset of the first DIE following the header.
 * @param totalSize          Total size of the unit in bytes.
 * @param abbreviationOffset Offset into .debug_abbrev for the unit's table.
 * @param addressSize        Target address width in bytes.
 * @param isBigEndian        @c true for big-endian targets.
 * @param isDwarf64          @c true for DWARF-64 length encoding.
 */
CompilationUnit::CompilationUnit(off_t headerOffset, off_t contentOffset,
	off_t totalSize, off_t abbreviationOffset, uint8 addressSize,
	bool isBigEndian, bool isDwarf64)
	:
	BaseUnit(headerOffset, contentOffset, totalSize, abbreviationOffset,
		addressSize, isBigEndian, isDwarf64),
	fUnitEntry(NULL),
	fAddressRanges(NULL),
	fDirectories(10),
	fFiles(10),
	fLineNumberProgram(addressSize, isBigEndian)
{
}


/**
 * @brief Destroys the compilation unit and releases its address-range list.
 */
CompilationUnit::~CompilationUnit()
{
	SetAddressRanges(NULL);
}


/**
 * @brief Records the top-level DIE describing this compilation unit.
 *
 * @param entry DW_TAG_compile_unit / DW_TAG_partial_unit DIE.
 */
void
CompilationUnit::SetUnitEntry(DIECompileUnitBase* entry)
{
	fUnitEntry = entry;
}


/**
 * @brief Replaces the unit's address-range list, adjusting reference counts.
 *
 * @param ranges New range list (may be NULL); the existing list is released.
 */
void
CompilationUnit::SetAddressRanges(TargetAddressRangeList* ranges)
{
	if (fAddressRanges != NULL)
		fAddressRanges->ReleaseReference();

	fAddressRanges = ranges;

	if (fAddressRanges != NULL)
		fAddressRanges->AcquireReference();
}


/**
 * @brief Returns the base address used to relocate range-list entries.
 *
 * Defaults to the unit's DW_AT_low_pc value.
 *
 * @return The base PC, or 0 if no unit entry has been recorded.
 */
target_addr_t
CompilationUnit::AddressRangeBase() const
{
	return fUnitEntry != NULL ? fUnitEntry->LowPC() : 0;
}


/**
 * @brief Appends a directory to the line-program directory table.
 *
 * Empty directory strings are rejected so that diagnostic output never
 * shows blank entries.
 *
 * @param directory Directory path string from the line program prologue.
 * @return @c true on success, @c false on allocation failure or empty input.
 */
bool
CompilationUnit::AddDirectory(const char* directory)
{
	BString* directoryString = new(std::nothrow) BString(directory);
	if (directoryString == NULL || directoryString->Length() == 0
		|| !fDirectories.AddItem(directoryString)) {
		delete directoryString;
		return false;
	}

	return true;
}


/**
 * @brief Returns the number of directories registered for the line program.
 */
int32
CompilationUnit::CountDirectories() const
{
	return fDirectories.CountItems();
}


/**
 * @brief Returns the directory string at @a index, or NULL if out of range.
 */
const char*
CompilationUnit::DirectoryAt(int32 index) const
{
	BString* directory = fDirectories.ItemAt(index);
	return directory != NULL ? directory->String() : NULL;
}


/**
 * @brief Appends a source file to the line-program file table.
 *
 * @param fileName File name as encoded in the line program prologue.
 * @param dirIndex Directory-table index providing the file's directory.
 * @return @c true on success, @c false on allocation failure or empty name.
 */
bool
CompilationUnit::AddFile(const char* fileName, int32 dirIndex)
{
	File* file = new(std::nothrow) File(fileName, DirectoryAt(dirIndex));
	if (file == NULL || file->fileName.Length() == 0 || !fFiles.AddItem(file)) {
		delete file;
		return false;
	}

	return true;
}


/**
 * @brief Returns the number of source files registered for the line program.
 */
int32
CompilationUnit::CountFiles() const
{
	return fFiles.CountItems();
}


/**
 * @brief Returns the file name (and optionally directory) at @a index.
 *
 * @param index      File-table index.
 * @param _directory Optional output pointer receiving the directory string.
 * @return The file name, or NULL if @a index is out of range.
 */
const char*
CompilationUnit::FileAt(int32 index, const char** _directory) const
{
	if (File* file = fFiles.ItemAt(index)) {
		if (_directory != NULL)
			*_directory = file->dirName;
		return file->fileName.String();
	}

	return NULL;
}


/**
 * @brief Reports the unit kind to upstream code.
 *
 * @return Always @c dwarf_unit_kind_compilation.
 */
dwarf_unit_kind
CompilationUnit::Kind() const
{
	return dwarf_unit_kind_compilation;
}
