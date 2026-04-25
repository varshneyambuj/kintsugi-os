/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2016, Ingo Weinhold.
 */

/** @file ElfSymbolLookup.h
    @brief Interfaces for iterating an ELF symbol table on top of a byte-read source. */

#ifndef ELF_SYMBOL_LOOKUP_H
#define ELF_SYMBOL_LOOKUP_H


#include <Referenceable.h>

#include "ElfFile.h"
#include "SymbolInfo.h"


/**
 * @brief Reference-counted abstract source of ELF bytes addressed by source-space offset.
 *
 * Concrete subclasses can wrap an on-disk file, an in-memory buffer, or
 * a live team's address space accessed via debug syscalls.
 */
class ElfSymbolLookupSource : public BReferenceable {
public:
	virtual	ssize_t				Read(uint64 address, void* buffer,
									size_t size) = 0;
};


/**
 * @brief Abstract iterator over the symbols of one ELF image.
 *
 * Use @c Create() to construct an instance bound to a specific ELF
 * source; concrete implementations specialise on 32-bit or 64-bit ELF
 * layout. Symbols can be enumerated with @c NextSymbolInfo() or looked
 * up by name with @c GetSymbolInfo().
 */
class ElfSymbolLookup {
public:
	/** @brief Sentinel passed for @c symbolCount to ask @c Init() to read
	    the count from the hash table. */
	static	const uint32		kGetSymbolCountFromHash = ~(uint32)0;

public:
	virtual						~ElfSymbolLookup();

	static	status_t			Create(ElfSymbolLookupSource* source,
									uint64 symbolTable, uint64 symbolHash,
									uint64 stringTable, uint32 symbolCount,
									uint32 symbolTableEntrySize,
									uint64 loadDelta, bool is64Bit,
									bool swappedByteOrder, bool cacheSource,
									ElfSymbolLookup*& _lookup);

	virtual	status_t			Init(bool cacheSource) = 0;
	virtual	status_t			NextSymbolInfo(uint32& index,
									SymbolInfo& _info) = 0;
	virtual	status_t			GetSymbolInfo(const char* name,
									uint32 symbolType, SymbolInfo& _info) = 0;
};


#endif	// ELF_SYMBOL_LOOKUP_H
