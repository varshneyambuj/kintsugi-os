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
 *   Copyright 2009-2016, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2010-2016, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DebuggerInterface.cpp
 * @brief Shared base behavior for the debugger back-end interface.
 *
 * Concrete subclasses (local, network, core file) implement the per-target
 * control and event surface; this file provides the lookup helpers that turn
 * raw ELF symbol tables (either from a file path or an in-memory blob) into
 * SymbolInfo lists used by the rest of the debugger.
 */


#include "DebuggerInterface.h"

#include <algorithm>

#include <AutoDeleter.h>

#include "ElfSymbolLookup.h"


// #pragma mark - SymbolTableLookupSource


/**
 * @brief Adapter that exposes a symbol table plus its string table to ElfSymbolLookup
 *        as if they were a single contiguous, addressable region.
 *
 * Reads at addresses inside the symbol table region copy from the symbol-table
 * buffer; reads past it copy from the string-table buffer. Used so that we can
 * feed in-memory symbol blobs (e.g. from the runtime loader) through the same
 * file-backed code path.
 */
struct DebuggerInterface::SymbolTableLookupSource : ElfSymbolLookupSource {
public:
	/**
	 * @brief Captures the addresses and sizes of the two buffers to splice together.
	 *
	 * @param symbolTable      Pointer to the ELF symbol table bytes.
	 * @param symbolTableSize  Size of the symbol table in bytes.
	 * @param stringTable      Pointer to the associated string table bytes.
	 * @param stringTableSize  Size of the string table in bytes.
	 */
	SymbolTableLookupSource(const void* symbolTable, size_t symbolTableSize,
		const char* stringTable, size_t stringTableSize)
		:
		fSymbolTable((const uint8*)symbolTable),
		fStringTable(stringTable),
		fSymbolTableSize(symbolTableSize),
		fStringTableEnd(symbolTableSize + stringTableSize)
	{
	}

	/**
	 * @brief Reads @a size bytes starting at the virtual address @a address.
	 *
	 * Addresses [0, fSymbolTableSize) map to the symbol table; addresses
	 * [fSymbolTableSize, fStringTableEnd) map to the string table.
	 *
	 * @param address  Virtual offset within the spliced region.
	 * @param buffer   Destination buffer; must hold @a size bytes.
	 * @param size     Maximum number of bytes to copy.
	 * @return Number of bytes copied, or B_BAD_VALUE if @a address is past the end.
	 */
	virtual ssize_t Read(uint64 address, void* buffer, size_t size)
	{
		ssize_t copied = 0;

		if (address > fStringTableEnd)
			return B_BAD_VALUE;

		if (address < fSymbolTableSize) {
			size_t toCopy = std::min(size, size_t(fSymbolTableSize - address));
			memcpy(buffer, fSymbolTable + address, toCopy);
			address -= toCopy;
			size -= toCopy;
			copied += toCopy;
		}

		if (address < fStringTableEnd) {
			size_t toCopy = std::min(size, size_t(fStringTableEnd - address));
			memcpy(buffer, fStringTable + address - fSymbolTableSize, toCopy);
			address -= toCopy;
			size -= toCopy;
			copied += toCopy;
		}

		return copied;
	}

private:
	const uint8*	fSymbolTable;
	const char*		fStringTable;
	size_t			fSymbolTableSize;
	size_t			fStringTableEnd;
};


// #pragma mark - DebuggerInterface


/**
 * @brief Virtual destructor; concrete subclasses release their own resources.
 */
DebuggerInterface::~DebuggerInterface()
{
}


/**
 * @brief Reports whether this interface represents a snapshot (core file) target.
 *
 * @return Always false at this layer; the core-file subclass overrides to true.
 */
bool
DebuggerInterface::IsPostMortem() const
{
	// only true for core file interfaces
	return false;
}


/**
 * @brief Loads ELF symbols from a file on disk into @a infos.
 *
 * @param filePath   Path to the ELF file.
 * @param textDelta  Runtime offset added to symbol addresses (relocation slide).
 * @param infos      Output list to which SymbolInfo entries are appended.
 * @return B_OK on success, otherwise the first failing status from ElfFile or
 *         the per-symbol allocation path.
 */
status_t
DebuggerInterface::GetElfSymbols(const char* filePath, int64 textDelta,
	BObjectList<SymbolInfo, true>& infos)
{
	// open the ELF file
	ElfFile elfFile;
	status_t error = elfFile.Init(filePath);
	if (error != B_OK)
		return error;

	// create the symbol lookup
	ElfSymbolLookup* symbolLookup;
	error = elfFile.CreateSymbolLookup(textDelta, symbolLookup);
	if (error != B_OK)
		return error;

	ObjectDeleter<ElfSymbolLookup> symbolLookupDeleter(symbolLookup);

	// get the symbols
	return GetElfSymbols(symbolLookup, infos);
}


/**
 * @brief Loads ELF symbols from in-memory symbol/string tables into @a infos.
 *
 * Useful for sources that already have the table data resident (such as the
 * runtime loader) and need symbol resolution without re-reading the on-disk
 * ELF file.
 *
 * @param symbolTable           Pointer to the symbol table bytes.
 * @param symbolCount           Number of symbol entries.
 * @param symbolTableEntrySize  Size in bytes of a single symbol entry.
 * @param stringTable           Pointer to the string table bytes.
 * @param stringTableSize       Size of the string table in bytes.
 * @param is64Bit               True if the ELF file is 64-bit.
 * @param swappedByteOrder      True if the host byte order differs from the file.
 * @param textDelta             Runtime offset added to symbol addresses.
 * @param infos                 Output list to which SymbolInfo entries are appended.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or any error
 *         returned by ElfSymbolLookup::Create().
 */
status_t
DebuggerInterface::GetElfSymbols(const void* symbolTable, uint32 symbolCount,
	uint32 symbolTableEntrySize, const char* stringTable,
	uint32 stringTableSize, bool is64Bit, bool swappedByteOrder,
	int64 textDelta, BObjectList<SymbolInfo, true>& infos)
{
	size_t symbolTableSize = symbolCount * symbolTableEntrySize;
	SymbolTableLookupSource* source = new(std::nothrow) SymbolTableLookupSource(
		symbolTable, symbolTableSize, stringTable, stringTableSize);
	if (source == NULL)
		return B_NO_MEMORY;
	BReference<SymbolTableLookupSource> sourceReference(source, true);

	ElfSymbolLookup* symbolLookup;
	status_t error = ElfSymbolLookup::Create(
		source, 0, 0, symbolTableSize, symbolCount, symbolTableEntrySize,
		textDelta, is64Bit, swappedByteOrder, false, symbolLookup);
	if (error != B_OK)
		return error;

	ObjectDeleter<ElfSymbolLookup> symbolLookupDeleter(symbolLookup);

	// get the symbols
	return GetElfSymbols(symbolLookup, infos);
}


/**
 * @brief Drains @a symbolLookup, copying each symbol into a fresh SymbolInfo
 *        appended to @a infos.
 *
 * @param symbolLookup  Iterator-like helper that yields SymbolInfo entries.
 * @param infos         Output list; ownership of the appended SymbolInfo
 *                      objects transfers to the list.
 * @return B_OK on success, B_NO_MEMORY if any allocation fails midway through.
 */
status_t
DebuggerInterface::GetElfSymbols(ElfSymbolLookup* symbolLookup,
	BObjectList<SymbolInfo, true>& infos)
{
	SymbolInfo symbolInfo;
	uint32 index = 0;
	while (symbolLookup->NextSymbolInfo(index, symbolInfo) == B_OK) {
		SymbolInfo* info = new(std::nothrow) SymbolInfo(symbolInfo);
		if (info == NULL || !infos.AddItem(info)) {
			delete info;
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}
