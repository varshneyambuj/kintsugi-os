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
 *   Copyright 2016, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ElfSymbolLookup.cpp
 * @brief Implementation of ElfSymbolLookup, the iterator that walks an
 *        ELF symbol table on top of an arbitrary read source.
 *
 * The reader is templated over @c ElfClass32 / @c ElfClass64 so the same
 * code resolves both address widths. A small two-window cache wrapper
 * (CachedSymbolLookupSource) batches the underlying byte reads when the
 * caller asks for caching, which is important when the source is a live
 * team's address space accessed via debug syscalls. The factory
 * @c ElfSymbolLookup::Create() picks the correct ElfClass instantiation
 * based on the bitness flag.
 */


#include "ElfSymbolLookup.h"

#include <algorithm>

#include <image.h>


/** @brief Maximum byte length of a symbol name read from the string table. */
static const size_t kMaxSymbolNameLength = 64 * 1024;
/** @brief Largest chunk read in a single I/O when scanning a string. */
static const size_t kMaxReadStringChunkSize = 1024;
/** @brief Per-window byte size of the symbol-source read cache. */
static const size_t kCacheBufferSize = 4 * 1024;


/**
 * @brief Two-window read cache wrapping an underlying ElfSymbolLookupSource.
 *
 * Maintains two equal-size windows so sequential symbol traversals can
 * straddle a window boundary without thrashing. When a window's reader
 * hit its final byte the next miss reuses that window; otherwise the
 * other window is reloaded.
 */
struct CachedSymbolLookupSource : public ElfSymbolLookupSource {
	/**
	 * @brief Constructs the cache around @a source and acquires a reference.
	 *
	 * @param source Underlying byte-read source.
	 */
	CachedSymbolLookupSource(ElfSymbolLookupSource* source)
		:
		fSource(source),
		fBufferSize(0)
	{
		for (int i = 0; i < 2; i++) {
			fBuffer[i] = 0;
			fAddress[i] = 0;
			fCachedSize[i] = 0;
			fHitEnd[i] = true;
		}

		fSource->AcquireReference();
	}

	/**
	 * @brief Frees the cache buffer and releases the source reference.
	 */
	~CachedSymbolLookupSource()
	{
		delete[] fBuffer[0];

		fSource->ReleaseReference();
	}

	/**
	 * @brief Allocates the two cache windows of @a bufferSize bytes each.
	 *
	 * @param bufferSize Bytes per cache window.
	 * @return         @c B_OK on success, @c B_NO_MEMORY on allocation failure.
	 */
	status_t Init(size_t bufferSize)
	{
		fBuffer[0] = new(std::nothrow) uint8[bufferSize * 2];
		if (fBuffer[0] == NULL)
			return B_NO_MEMORY;

		fBuffer[1] = fBuffer[0] + bufferSize;
		fBufferSize = bufferSize;
		return B_OK;
	}

	/**
	 * @brief Reads up to @a size bytes from @a address into @a _buffer.
	 *
	 * Loops over the cache, refilling on miss, until either the whole
	 * request is satisfied or the underlying source returns an error or
	 * end-of-data.
	 *
	 * @param address Source-space address to start reading from.
	 * @param _buffer Destination buffer.
	 * @param size    Maximum byte count to read.
	 * @return       Bytes actually read, or a negative error code.
	 */
	virtual ssize_t Read(uint64 address, void* _buffer, size_t size)
	{
		uint8* buffer = (uint8*)_buffer;
		size_t totalRead = 0;

		while (size > 0) {
			ssize_t bytesRead = _ReadPartial(address, buffer, size);
			if (bytesRead < 0)
				return totalRead == 0 ? bytesRead : totalRead;
			if (bytesRead == 0)
				return totalRead == 0 ? B_IO_ERROR : totalRead;

			totalRead += bytesRead;
			buffer += bytesRead;
			size -= bytesRead;
		}

		return totalRead;
	}

private:
	/**
	 * @brief Performs one read from the cache, refilling on miss.
	 *
	 * @param address Source-space address.
	 * @param buffer  Destination buffer.
	 * @param size    Maximum byte count.
	 * @return       Bytes copied (>= 0) on success, or negative status.
	 */
	ssize_t _ReadPartial(uint64 address, uint8* buffer, size_t size)
	{
		size_t bytesRead = _ReadCached(address, buffer, size);
		if (bytesRead > 0)
			return bytesRead;

		status_t error = _Cache(address, size);
		if (error != B_OK)
			return error;

		return (ssize_t)_ReadCached(address, buffer, size);
	}

	/**
	 * @brief Copies bytes into @a buffer if @a address falls in either window.
	 *
	 * Updates the per-window @c fHitEnd flag for the window that served the
	 * request so the cache eviction policy can prefer reusing exhausted
	 * windows.
	 *
	 * @param address Source-space address.
	 * @param buffer  Destination buffer.
	 * @param size    Maximum byte count.
	 * @return       Bytes copied (zero on miss).
	 */
	size_t _ReadCached(uint64 address, uint8* buffer, size_t size)
	{
		for (int i = 0; i < 2; i++) {
			if (address >= fAddress[i]
					&& address < fAddress[i] + fCachedSize[i]) {
				size_t toRead = std::min(size,
					size_t(fAddress[i] + fCachedSize[i] - address));
				memcpy(buffer, fBuffer[i] + (address - fAddress[i]), toRead);
				fHitEnd[i] = address + toRead == fAddress[i] + fCachedSize[i];
				return toRead;
			}
		}
		return 0;
	}

	/**
	 * @brief Refills one cache window starting at @a address.
	 *
	 * Picks the window that most recently hit end-of-data when possible.
	 *
	 * @param address Source-space address to begin caching at.
	 * @param size    Hint about the upcoming request size; advisory only.
	 * @return       @c B_OK on success, @c B_IO_ERROR on a zero-byte read,
	 *                or the propagated underlying error.
	 */
	status_t _Cache(uint64 address, size_t size)
	{
		int i = 0;
		if (!fHitEnd[i])
			i++;

		ssize_t bytesRead = fSource->Read(address, fBuffer[i], fBufferSize);
		if (bytesRead < 0)
			return bytesRead;
		if (bytesRead == 0)
			return B_IO_ERROR;

		fAddress[i] = address;
		fCachedSize[i] = bytesRead;
		fHitEnd[i] = false;
		return B_OK;
	}

private:
	ElfSymbolLookupSource*	fSource;
	uint8*					fBuffer[2];
	uint64					fAddress[2];
	size_t					fCachedSize[2];
	bool					fHitEnd[2];
	size_t					fBufferSize;
};


// #pragma mark - ElfSymbolLookupImpl


/**
 * @brief Class-templated ELF symbol-table walker.
 *
 * The template parameter @c ElfClass selects the 32-bit or 64-bit ELF
 * structure layout (in particular the @c Sym entry type). The walker
 * lazily resolves symbols by reading the symbol table sequentially and
 * skipping entries that are neither functions nor objects or have a
 * zero address.
 */
template<typename ElfClass>
class ElfSymbolLookupImpl : public ElfSymbolLookup {
public:
	typedef typename ElfClass::Sym ElfSym;

	/**
	 * @brief Constructs the walker; reference acquired on @a source.
	 *
	 * @param source               Underlying read source.
	 * @param symbolTable          Source-space address of the symbol table.
	 * @param symbolHash           Source-space address of the hash table.
	 * @param stringTable          Source-space address of the string table.
	 * @param symbolCount          Number of symbols, or @c kGetSymbolCountFromHash.
	 * @param symbolTableEntrySize Bytes per symbol-table entry.
	 * @param loadDelta            Address adjustment applied to each symbol.
	 * @param swappedByteOrder     True if the source uses opposite endianness.
	 */
	ElfSymbolLookupImpl(ElfSymbolLookupSource* source, uint64 symbolTable,
		uint64 symbolHash, uint64 stringTable, uint32 symbolCount,
		uint32 symbolTableEntrySize, uint64 loadDelta, bool swappedByteOrder)
		:
		fSource(NULL),
		fSymbolTable(symbolTable),
		fSymbolHash(symbolHash),
		fStringTable(stringTable),
		fSymbolCount(symbolCount),
		fSymbolTableEntrySize(symbolTableEntrySize),
		fLoadDelta(loadDelta),
		fSwappedByteOrder(swappedByteOrder)
	{
		SetSource(source);
	}

	/**
	 * @brief Releases the source reference via @c SetSource(NULL).
	 */
	~ElfSymbolLookupImpl()
	{
		SetSource(NULL);
	}

	/**
	 * @brief Endianness-aware byte-swap helper used to read raw ELF fields.
	 *
	 * @param value Raw value as found in the source.
	 * @return     Host-order representation of @a value.
	 */
	template<typename Value>
	Value Get(const Value& value) const
	{
		return ElfFile::StaticGet(value, fSwappedByteOrder);
	}

	/**
	 * @brief Replaces the underlying source, transferring the reference.
	 *
	 * @param source Replacement source, or NULL to detach.
	 */
	void SetSource(ElfSymbolLookupSource* source)
	{
		if (source == fSource)
			return;

		if (fSource != NULL)
			fSource->ReleaseReference();

		fSource = source;

		if (fSource != NULL)
			fSource->AcquireReference();
	}

	/**
	 * @brief Validates the entry size, optionally wraps the source in a
	 *        cache, and resolves the symbol count from the hash table when
	 *        the caller passed @c kGetSymbolCountFromHash.
	 *
	 * @param cacheSource True to install the read cache.
	 * @return           @c B_OK on success; @c B_BAD_DATA on invalid
	 *                    entry size; @c B_NO_MEMORY on cache allocation
	 *                    failure; @c B_IO_ERROR on a short hash-table read;
	 *                    or the propagated source error.
	 */
	virtual status_t Init(bool cacheSource)
	{
		if (fSymbolTableEntrySize < sizeof(ElfSym))
			return B_BAD_DATA;

		// Create a cached source, if requested.
		if (cacheSource) {
			CachedSymbolLookupSource* cachedSource
				= new(std::nothrow) CachedSymbolLookupSource(fSource);
			if (cachedSource == NULL)
				return B_NO_MEMORY;

			SetSource(cachedSource);

			status_t error = cachedSource->Init(kCacheBufferSize);
			if (error != B_OK)
				return error;
		}

		if (fSymbolCount == kGetSymbolCountFromHash) {
			// Read the number of symbols in the symbol table from the hash
			// table entry 1.
			uint32 symbolCount;
			ssize_t bytesRead = fSource->Read(fSymbolHash + 4, &symbolCount, 4);
			if (bytesRead < 0)
				return bytesRead;
			if (bytesRead != 4)
				return B_IO_ERROR;

			fSymbolCount = Get(symbolCount);
		}

		return B_OK;
	}

	/**
	 * @brief Returns the next defined function or object symbol from the table.
	 *
	 * Skips undefined entries and entries that are not functions or
	 * objects. Applies @c fLoadDelta to the symbol address before
	 * returning it.
	 *
	 * @param index On entry: starting symbol index. On exit: position
	 *               immediately after the matched symbol.
	 * @param _info Receives the matched symbol's address, size, type, and name.
	 * @return     @c B_OK on success, @c B_ENTRY_NOT_FOUND when the table
	 *              is exhausted, or a propagated I/O error.
	 */
	virtual status_t NextSymbolInfo(uint32& index, SymbolInfo& _info)
	{
		uint64 symbolAddress = fSymbolTable + index * fSymbolTableEntrySize;
		for (; index < fSymbolCount;
				index++, symbolAddress += fSymbolTableEntrySize) {
			// read the symbol structure
			ElfSym symbol;
			ssize_t bytesRead = fSource->Read(symbolAddress, &symbol,
				sizeof(symbol));
			if (bytesRead < 0)
				return bytesRead;
			if ((size_t)bytesRead != sizeof(symbol))
				return B_IO_ERROR;

			// check, if it is a function or a data object and defined
			// Note: Type() operates on a uint8, so byte order is irrelevant.
			if ((symbol.Type() != STT_FUNC && symbol.Type() != STT_OBJECT)
				|| symbol.st_value == 0) {
				continue;
			}

			// get the values
			target_addr_t address = Get(symbol.st_value) + fLoadDelta;
			target_size_t size = Get(symbol.st_size);
			uint32 type = symbol.Type() == STT_FUNC
				? B_SYMBOL_TYPE_TEXT : B_SYMBOL_TYPE_DATA;

			// get the symbol name
			uint64 nameAddress = fStringTable + Get(symbol.st_name);
			BString name;
			status_t error = _ReadString(nameAddress, kMaxSymbolNameLength,
				name);
			if (error != B_OK)
				return error;

			_info.SetTo(address, size, type, name);
			index++;
			return B_OK;
		}

		return B_ENTRY_NOT_FOUND;
	}

	/**
	 * @brief Looks up a symbol by name via a linear scan of the table.
	 *
	 * @param name       Null-terminated symbol name to match.
	 * @param symbolType Symbol-type filter (currently unused).
	 * @param _info      Receives the matching symbol on success.
	 * @return          @c B_OK on success, @c B_ENTRY_NOT_FOUND if no
	 *                   matching symbol is found, or a propagated I/O error.
	 *
	 * @todo Use the hash table for direct lookup instead of scanning.
	 */
	virtual status_t GetSymbolInfo(const char* name, uint32 symbolType,
		SymbolInfo& _info)
	{
		// TODO: Optimize this by using the hash table.
		uint32 index = 0;
		SymbolInfo info;
		while (NextSymbolInfo(index, info) == B_OK) {
			if (strcmp(name, info.Name()) == 0) {
				_info = info;
				return B_OK;
			}
		}

		return B_ENTRY_NOT_FOUND;
	}

private:
	/**
	 * @brief Reads a NUL-terminated string from the source into @a _string.
	 *
	 * Reads in chunks of up to @c kMaxReadStringChunkSize bytes and stops
	 * at the first NUL byte or when @a size bytes have been consumed.
	 *
	 * @param address Source-space address of the string.
	 * @param size    Maximum byte count to read.
	 * @param _string Receives the bytes read; cleared first.
	 * @return       @c B_OK on success, @c B_NO_MEMORY when the BString
	 *                cannot grow, @c B_BAD_DATA if no NUL was found within
	 *                @a size bytes, @c B_IO_ERROR on a zero-byte read, or
	 *                a propagated I/O error.
	 */
	status_t _ReadString(uint64 address, size_t size, BString& _string)
	{
		_string.Truncate(0);

		char buffer[kMaxReadStringChunkSize];
		while (size > 0) {
			size_t toRead = std::min(size, sizeof(buffer));
			ssize_t bytesRead = fSource->Read(address, buffer, toRead);
			if (bytesRead < 0)
				return bytesRead;
			if (bytesRead == 0)
				return B_IO_ERROR;

			size_t chunkSize = strnlen(buffer, bytesRead);
			int32 oldLength = _string.Length();
			_string.Append(buffer, chunkSize);
			if (_string.Length() <= oldLength)
				return B_NO_MEMORY;

			if (chunkSize < (size_t)bytesRead) {
				// we found a terminating null
				return B_OK;
			}

			address += bytesRead;
			size -= bytesRead;
		}

		return B_BAD_DATA;
	}

private:
	ElfSymbolLookupSource*	fSource;
	uint64					fSymbolTable;
	uint64					fSymbolHash;
	uint64					fStringTable;
	uint32					fSymbolCount;
	uint32					fSymbolTableEntrySize;
	uint64					fLoadDelta;
	bool					fSwappedByteOrder;
};


// #pragma mark - ElfSymbolLookup


/**
 * @brief Virtual destructor anchor for the ElfSymbolLookup interface.
 */
ElfSymbolLookup::~ElfSymbolLookup()
{
}


/**
 * @brief Factory constructing the appropriate concrete walker for an ELF source.
 *
 * Picks a 32-bit or 64-bit @c ElfSymbolLookupImpl based on @a is64Bit, then
 * runs @c Init() to validate parameters and resolve the symbol count from
 * the hash table when requested.
 *
 * @param source               Underlying read source for the ELF data.
 * @param symbolTable          Source-space address of the symbol table.
 * @param symbolHash           Source-space address of the hash table.
 * @param stringTable          Source-space address of the string table.
 * @param symbolCount          Number of symbols, or @c kGetSymbolCountFromHash
 *                              to read it from the hash table.
 * @param symbolTableEntrySize Bytes per symbol-table entry.
 * @param loadDelta            Address adjustment applied to each symbol.
 * @param is64Bit              True to instantiate the 64-bit walker.
 * @param swappedByteOrder     True if the source uses opposite endianness.
 * @param cacheSource          True to wrap the source in a read cache.
 * @param _lookup              On success, receives the created walker;
 *                              ownership transfers to the caller.
 * @return                    @c B_OK on success, @c B_NO_MEMORY on
 *                              allocation failure, or a propagated init error.
 */
/*static*/ status_t
ElfSymbolLookup::Create(ElfSymbolLookupSource* source, uint64 symbolTable,
	uint64 symbolHash, uint64 stringTable, uint32 symbolCount,
	uint32 symbolTableEntrySize, uint64 loadDelta, bool is64Bit,
	bool swappedByteOrder, bool cacheSource, ElfSymbolLookup*& _lookup)
{
	// create
	ElfSymbolLookup* lookup;
	if (is64Bit) {
		lookup = new(std::nothrow) ElfSymbolLookupImpl<ElfClass64>(source,
			symbolTable, symbolHash, stringTable, symbolCount,
			symbolTableEntrySize, loadDelta, swappedByteOrder);
	} else {
		lookup = new(std::nothrow) ElfSymbolLookupImpl<ElfClass32>(source,
			symbolTable, symbolHash, stringTable, symbolCount,
			symbolTableEntrySize, loadDelta, swappedByteOrder);
	}

	if (lookup == NULL)
		return B_NO_MEMORY;

	// init
	status_t error = lookup->Init(cacheSource);
	if (error == B_OK)
		_lookup = lookup;
	else
		delete lookup;

	return error;
}
