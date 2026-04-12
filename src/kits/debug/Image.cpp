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
 *   Copyright 2005-2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Image.cpp
 * @brief ELF image wrappers for the debug kit symbol-lookup subsystem.
 *
 * Implements the Image class hierarchy used by SymbolLookup to walk the symbol
 * tables of loaded and on-disk ELF images. Concrete subclasses cover user-land
 * image files (ImageFile), kernel images (KernelImage), and the commpage
 * shared image (CommPageImage). SymbolTableBasedImage provides the common
 * linear symbol-table scan shared by all ELF-based subclasses.
 *
 * @see SymbolLookup, debug_support.h
 */


#include "Image.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <new>

#include <runtime_loader.h>
#include <syscalls.h>


using namespace BPrivate::Debug;


// #pragma mark - Image


/**
 * @brief Default constructor — initialises an empty Image descriptor.
 */
Image::Image()
{
}


/**
 * @brief Destructor — subclasses are responsible for releasing their resources.
 */
Image::~Image()
{
}


/**
 * @brief Search the image's symbol table for a symbol matching the given name.
 *
 * Iterates over all symbols via NextSymbol() looking for one whose name equals
 * @a name and whose type matches @a symbolType (or B_SYMBOL_TYPE_ANY). On
 * success the optional output pointers are populated.
 *
 * @param name             Null-terminated symbol name to look up.
 * @param symbolType       Required symbol type (B_SYMBOL_TYPE_TEXT,
 *                         B_SYMBOL_TYPE_DATA, or B_SYMBOL_TYPE_ANY).
 * @param _symbolLocation  Output — receives the runtime address of the symbol,
 *                         or unchanged if NULL.
 * @param _symbolSize      Output — receives the size of the symbol in bytes,
 *                         or unchanged if NULL.
 * @param _symbolType      Output — receives the resolved symbol type,
 *                         or unchanged if NULL.
 * @return B_OK if the symbol is found, B_ENTRY_NOT_FOUND otherwise.
 */
status_t
Image::GetSymbol(const char* name, int32 symbolType, void** _symbolLocation,
	size_t* _symbolSize, int32* _symbolType) const
{
	// TODO: At least for ImageFile we could do hash lookups!
	int32 iterator = 0;
	const char* foundName;
	size_t foundNameLen;
	addr_t foundAddress;
	size_t foundSize;
	int32 foundType;
	while (NextSymbol(iterator, &foundName, &foundNameLen, &foundAddress,
			&foundSize, &foundType) == B_OK) {
		if ((symbolType == B_SYMBOL_TYPE_ANY || symbolType == foundType)
			&& strcmp(name, foundName) == 0) {
			if (_symbolLocation != NULL)
				*_symbolLocation = (void*)foundAddress;
			if (_symbolSize != NULL)
				*_symbolSize = foundSize;
			if (_symbolType != NULL)
				*_symbolType = foundType;
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


// #pragma mark - SymbolTableBasedImage


/**
 * @brief Default constructor — zeroes all ELF symbol-table pointers.
 */
SymbolTableBasedImage::SymbolTableBasedImage()
	:
	fLoadDelta(0),
	fSymbolTable(NULL),
	fStringTable(NULL),
	fSymbolCount(0),
	fStringTableSize(0)
{
}


/**
 * @brief Destructor — subclasses own the table storage and must free it.
 */
SymbolTableBasedImage::~SymbolTableBasedImage()
{
}


/**
 * @brief Find the symbol that best covers the given runtime address.
 *
 * Performs a linear scan of the ELF symbol table. An exact match (address
 * falls within [symbol_start, symbol_start+size)) terminates the search
 * immediately; otherwise the closest preceding symbol is returned.
 *
 * @param address       Runtime address to resolve.
 * @param _baseAddress  Output — receives the load address of the found symbol.
 * @param _symbolName   Output — receives a pointer into the string table for
 *                      the symbol's name.
 * @param _symbolNameLen Output — receives the length of the symbol name string.
 * @param _exactMatch   Output — set to true when the address falls within the
 *                      symbol's size range.
 * @return A pointer to the matching elf_sym entry, or NULL if no symbol covers
 *         the address.
 */
const elf_sym*
SymbolTableBasedImage::LookupSymbol(addr_t address, addr_t* _baseAddress,
	const char** _symbolName, size_t *_symbolNameLen, bool *_exactMatch) const
{
	const elf_sym* symbolFound = NULL;
	const char* symbolName = NULL;
	bool exactMatch = false;
	addr_t deltaFound = ~(addr_t)0;

	for (int32 i = 0; i < fSymbolCount; i++) {
		const elf_sym* symbol = &fSymbolTable[i];

		if (symbol->st_value == 0
			|| symbol->st_size >= (size_t)fInfo.text_size + fInfo.data_size) {
			continue;
		}

		addr_t symbolAddress = symbol->st_value + fLoadDelta;
		if (symbolAddress > address)
			continue;

		addr_t symbolDelta = address - symbolAddress;
		if (symbolDelta >= 0 && symbolDelta < symbol->st_size)
			exactMatch = true;

		if (exactMatch || symbolDelta < deltaFound) {
			deltaFound = symbolDelta;
			symbolFound = symbol;
			symbolName = fStringTable + symbol->st_name;

			if (exactMatch)
				break;
		}
	}

	if (symbolFound != NULL) {
		if (_baseAddress != NULL)
			*_baseAddress = symbolFound->st_value + fLoadDelta;
		if (_symbolName != NULL)
			*_symbolName = symbolName;
		if (_exactMatch != NULL)
			*_exactMatch = exactMatch;
		if (_symbolNameLen != NULL)
			*_symbolNameLen = _SymbolNameLen(symbolName);
	}

	return symbolFound;
}


/**
 * @brief Advance the symbol iterator to the next function or data symbol.
 *
 * Increments @a iterator and skips entries that are neither STT_FUNC nor
 * STT_OBJECT, or whose st_value is zero, until a usable symbol is found or
 * the table is exhausted.
 *
 * @param iterator      Iteration state — pass -1 on the first call; updated
 *                      to the index of the returned symbol on success.
 * @param _symbolName   Output — pointer into the string table for the symbol name.
 * @param _symbolNameLen Output — length of the symbol name.
 * @param _symbolAddress Output — runtime address of the symbol.
 * @param _symbolSize    Output — size of the symbol in bytes.
 * @param _symbolType    Output — B_SYMBOL_TYPE_TEXT or B_SYMBOL_TYPE_DATA.
 * @return B_OK when a symbol is returned, B_ENTRY_NOT_FOUND at end-of-table.
 */
status_t
SymbolTableBasedImage::NextSymbol(int32& iterator, const char** _symbolName,
	size_t* _symbolNameLen, addr_t* _symbolAddress, size_t* _symbolSize,
	int32* _symbolType) const
{
	while (true) {
		if (++iterator >= fSymbolCount)
			return B_ENTRY_NOT_FOUND;

		const elf_sym* symbol = &fSymbolTable[iterator];

		if ((symbol->Type() != STT_FUNC && symbol->Type() != STT_OBJECT)
			|| symbol->st_value == 0) {
			continue;
		}

		*_symbolName = fStringTable + symbol->st_name;
		*_symbolNameLen = _SymbolNameLen(*_symbolName);
		*_symbolAddress = symbol->st_value + fLoadDelta;
		*_symbolSize = symbol->st_size;
		*_symbolType = symbol->Type() == STT_FUNC ? B_SYMBOL_TYPE_TEXT
			: B_SYMBOL_TYPE_DATA;

		return B_OK;
	}
}


/**
 * @brief Return the safe length of a symbol name stored in the string table.
 *
 * Guards against out-of-bounds string pointers by clamping the strnlen search
 * to the end of the mapped string table buffer.
 *
 * @param symbolName  Pointer into the string table.
 * @return Number of bytes in the name excluding the NUL terminator, or 0 if
 *         the pointer is NULL or outside the string table range.
 */
size_t
SymbolTableBasedImage::_SymbolNameLen(const char* symbolName) const
{
	if (symbolName == NULL || (addr_t)symbolName < (addr_t)fStringTable
		|| (addr_t)symbolName >= (addr_t)fStringTable + fStringTableSize) {
		return 0;
	}

	return strnlen(symbolName,
		(addr_t)fStringTable + fStringTableSize - (addr_t)symbolName);
}


// #pragma mark - ImageFile


/**
 * @brief Constructor — initialises an unmapped ImageFile descriptor.
 */
ImageFile::ImageFile()
	:
	fFD(-1),
	fFileSize(0),
	fMappedFile((uint8*)MAP_FAILED)
{
}


/**
 * @brief Destructor — unmaps the file and closes the file descriptor.
 */
ImageFile::~ImageFile()
{
	if (fMappedFile != MAP_FAILED)
		munmap(fMappedFile, fFileSize);

	if (fFD >= 0)
		close(fFD);
}


/**
 * @brief Initialise the ImageFile from a running image's image_info record.
 *
 * Copies the provided info, loads the ELF file from disk, and computes the
 * load delta as the difference between the mapped text address and the
 * recorded runtime text address.
 *
 * @param info  The image_info record obtained from get_image_info() or the
 *              image-created debug event for the target team.
 * @return B_OK on success, or a file/parse error code on failure.
 */
status_t
ImageFile::Init(const image_info& info)
{
	// just copy the image info
	fInfo = info;

	// load the file
	addr_t textAddress;
	size_t textSize;
	addr_t dataAddress;
	size_t dataSize;
	status_t error = _LoadFile(info.name, &textAddress, &textSize, &dataAddress,
		&dataSize);
	if (error != B_OK)
		return error;

	// compute the load delta
	fLoadDelta = (addr_t)fInfo.text - textAddress;

	return B_OK;
}


/**
 * @brief Initialise the ImageFile directly from a file path.
 *
 * Loads the ELF file from @a path without any running-image context. The
 * synthesised image_info is marked with id=-1 and load delta 0.
 *
 * @param path  Absolute path to the ELF image file on disk.
 * @return B_OK on success, or a file/parse error code on failure.
 */
status_t
ImageFile::Init(const char* path)
{
	// load the file
	addr_t textAddress;
	size_t textSize;
	addr_t dataAddress;
	size_t dataSize;
	status_t error = _LoadFile(path, &textAddress, &textSize, &dataAddress,
		&dataSize);
	if (error != B_OK)
		return error;

	// init the image info
	fInfo.id = -1;
	fInfo.type = B_LIBRARY_IMAGE;
	fInfo.sequence = 0;
	fInfo.init_order = 0;
	fInfo.init_routine = 0;
	fInfo.term_routine = 0;
	fInfo.device = -1;
	fInfo.node = -1;
	strlcpy(fInfo.name, path, sizeof(fInfo.name));
	fInfo.text = (void*)textAddress;
	fInfo.data = (void*)dataAddress;
	fInfo.text_size = textSize;
	fInfo.data_size = dataSize;

	// the image isn't loaded, so no delta
	fLoadDelta = 0;

	return B_OK;
}


/**
 * @brief Open, map, and parse the ELF headers of the image file at @a path.
 *
 * Opens the file, memory-maps it read-only, validates the ELF magic and class,
 * locates the text and data PT_LOAD segments, and then delegates to
 * _FindTableInSection() to populate the symbol and string table pointers.
 * Tries SHT_SYMTAB first; falls back to SHT_DYNSYM if unavailable.
 *
 * @param path          Path to the ELF file on disk.
 * @param _textAddress  Output — virtual address of the first read-only segment.
 * @param _textSize     Output — byte size of the combined text region.
 * @param _dataAddress  Output — virtual address of the first writable segment.
 * @param _dataSize     Output — byte size of the combined data region.
 * @return B_OK on success; B_NOT_AN_EXECUTABLE, B_BAD_DATA, or an errno value
 *         on failure.
 */
status_t
ImageFile::_LoadFile(const char* path, addr_t* _textAddress, size_t* _textSize,
	addr_t* _dataAddress, size_t* _dataSize)
{
	// open and stat() the file
	fFD = open(path, O_RDONLY);
	if (fFD < 0)
		return errno;

	struct stat st;
	if (fstat(fFD, &st) < 0)
		return errno;

	fFileSize = st.st_size;
	if (fFileSize < (off_t)sizeof(elf_ehdr))
		return B_NOT_AN_EXECUTABLE;

	// map it
	fMappedFile = (uint8*)mmap(NULL, fFileSize, PROT_READ, MAP_PRIVATE, fFD, 0);
	if (fMappedFile == MAP_FAILED)
		return errno;

	// examine the elf header
	elf_ehdr* elfHeader = (elf_ehdr*)fMappedFile;
	if (memcmp(elfHeader->e_ident, ELFMAG, 4) != 0)
		return B_NOT_AN_EXECUTABLE;

	if (elfHeader->e_ident[4] != ELF_CLASS)
		return B_NOT_AN_EXECUTABLE;

	// verify the location of the program headers
	int32 programHeaderCount = elfHeader->e_phnum;
	if (elfHeader->e_phoff < sizeof(elf_ehdr)
		|| elfHeader->e_phentsize < sizeof(elf_phdr)
		|| (off_t)(elfHeader->e_phoff + programHeaderCount
				* elfHeader->e_phentsize)
			> fFileSize) {
		return B_NOT_AN_EXECUTABLE;
	}

	elf_phdr* programHeaders
		= (elf_phdr*)(fMappedFile + elfHeader->e_phoff);

	// verify the location of the section headers
	int32 sectionCount = elfHeader->e_shnum;
	if (elfHeader->e_shoff < sizeof(elf_ehdr)
		|| elfHeader->e_shentsize < sizeof(elf_shdr)
		|| (off_t)(elfHeader->e_shoff + sectionCount * elfHeader->e_shentsize)
			> fFileSize) {
		return B_NOT_AN_EXECUTABLE;
	}

	// find the text and data segment -- we need load address and size
	// in case of multiple segments of the same type, combine them
	addr_t textBase = 0;
	addr_t textEnd = 0;
	addr_t dataBase = 0;
	addr_t dataEnd = 0;
	for (int32 i = 0; i < programHeaderCount; i++) {
		elf_phdr* header = (elf_phdr*)
			((uint8*)programHeaders + i * elfHeader->e_phentsize);
		if (header->p_type != PT_LOAD)
			continue;

		addr_t base = header->p_vaddr;
		addr_t end = base + header->p_memsz;
		if ((header->p_flags & PF_WRITE) == 0) {
			if (textEnd == 0) {
				textBase = base;
				textEnd = end;
			} else {
				textBase = min_c(textBase, base);
				textEnd = max_c(textEnd, end);
			}
		} else {
			if (dataEnd == 0) {
				dataBase = base;
				dataEnd = end;
			} else {
				dataBase = min_c(dataBase, base);
				dataEnd = max_c(dataEnd, end);
			}
		}
	}

	*_textAddress = textBase;
	*_textSize = textEnd - textBase;
	*_dataAddress = dataBase;
	*_dataSize = dataEnd - dataBase;

	status_t error = _FindTableInSection(elfHeader, SHT_SYMTAB);
	if (error != B_OK)
		error = _FindTableInSection(elfHeader, SHT_DYNSYM);

	return error;
}


/**
 * @brief Locate the ELF symbol table section of the given type and cache it.
 *
 * Iterates the section header table looking for a section whose sh_type equals
 * @a sectionType. When found, validates its associated SHT_STRTAB section and
 * sets fSymbolTable, fStringTable, fSymbolCount, and fStringTableSize.
 *
 * @param elfHeader    Pointer to the start of the mapped ELF file.
 * @param sectionType  Section type to search for (SHT_SYMTAB or SHT_DYNSYM).
 * @return B_OK when the table is found and validated, B_BAD_DATA otherwise.
 */
status_t
ImageFile::_FindTableInSection(elf_ehdr* elfHeader, uint16 sectionType)
{
	elf_shdr* sectionHeaders
		= (elf_shdr*)(fMappedFile + elfHeader->e_shoff);

	// find the symbol table
	for (int32 i = 0; i < elfHeader->e_shnum; i++) {
		elf_shdr* sectionHeader = (elf_shdr*)
			((uint8*)sectionHeaders + i * elfHeader->e_shentsize);

		if (sectionHeader->sh_type == sectionType) {
			elf_shdr& stringHeader = *(elf_shdr*)
				((uint8*)sectionHeaders
					+ sectionHeader->sh_link * elfHeader->e_shentsize);

			if (stringHeader.sh_type != SHT_STRTAB)
				return B_BAD_DATA;

			if ((off_t)(sectionHeader->sh_offset + sectionHeader->sh_size)
					> fFileSize
				|| (off_t)(stringHeader.sh_offset + stringHeader.sh_size)
					> fFileSize) {
				return B_BAD_DATA;
			}

			fSymbolTable = (elf_sym*)(fMappedFile + sectionHeader->sh_offset);
			fStringTable = (char*)(fMappedFile + stringHeader.sh_offset);
			fSymbolCount = sectionHeader->sh_size / sizeof(elf_sym);
			fStringTableSize = stringHeader.sh_size;

			return B_OK;
		}
	}

	return B_BAD_DATA;
}


// #pragma mark - KernelImage


/**
 * @brief Constructor — initialises an empty KernelImage descriptor.
 */
KernelImage::KernelImage()
{
}


/**
 * @brief Destructor — frees the heap-allocated symbol and string tables.
 */
KernelImage::~KernelImage()
{
	delete[] fSymbolTable;
	delete[] fStringTable;
}


/**
 * @brief Initialise the KernelImage from the running kernel image.
 *
 * Uses _kern_read_kernel_image_symbols() to allocate and populate the symbol
 * and string tables directly from kernel memory. Also retrieves the load delta
 * used to translate symbol values to runtime addresses.
 *
 * @param info  The image_info record for the kernel image (B_SYSTEM_TEAM).
 * @return B_OK on success, B_NO_MEMORY if allocation fails, or a syscall error.
 */
status_t
KernelImage::Init(const image_info& info)
{
	fInfo = info;

	// get the table sizes
	fSymbolCount = 0;
	fStringTableSize = 0;
	status_t error = _kern_read_kernel_image_symbols(fInfo.id,
		NULL, &fSymbolCount, NULL, &fStringTableSize, NULL);
	if (error != B_OK)
		return error;

	// allocate the tables
	fSymbolTable = new(std::nothrow) elf_sym[fSymbolCount];
	fStringTable = new(std::nothrow) char[fStringTableSize];
	if (fSymbolTable == NULL || fStringTable == NULL)
		return B_NO_MEMORY;

	// get the info
	return _kern_read_kernel_image_symbols(fInfo.id,
		fSymbolTable, &fSymbolCount, fStringTable, &fStringTableSize,
		&fLoadDelta);
}


/**
 * @brief Constructor — initialises an empty CommPageImage descriptor.
 */
CommPageImage::CommPageImage()
{
}


/**
 * @brief Destructor — frees the heap-allocated symbol and string tables.
 */
CommPageImage::~CommPageImage()
{
	delete[] fSymbolTable;
	delete[] fStringTable;
}


/**
 * @brief Initialise the CommPageImage for the commpage shared mapping.
 *
 * Locates the "commpage" kernel image by scanning the system team's image
 * list, loads its symbol and string tables via _kern_read_kernel_image_symbols(),
 * and sets fLoadDelta to the runtime text address supplied in @a info.
 *
 * @param info  The image_info record for the commpage mapping in the target team.
 * @return B_OK on success; B_ENTRY_NOT_FOUND if no "commpage" image is found;
 *         B_NO_MEMORY on allocation failure; or a syscall error code.
 */
status_t
CommPageImage::Init(const image_info& info)
{
	// find kernel image for commpage
	image_id commPageID = -1;
	image_info commPageInfo;

	int32 cookie = 0;
	while (_kern_get_next_image_info(B_SYSTEM_TEAM, &cookie, &commPageInfo,
			sizeof(image_info)) == B_OK) {
		if (!strcmp("commpage", commPageInfo.name)) {
			commPageID = commPageInfo.id;
			break;
		}
	}
	if (commPageID < 0)
		return B_ENTRY_NOT_FOUND;

	fInfo = commPageInfo;
	fInfo.text = info.text;

	// get the table sizes
	fSymbolCount = 0;
	fStringTableSize = 0;
	status_t error = _kern_read_kernel_image_symbols(commPageID, NULL,
		&fSymbolCount, NULL, &fStringTableSize, NULL);
	if (error != B_OK)
		return error;

	// allocate the tables
	fSymbolTable = new(std::nothrow) elf_sym[fSymbolCount];
	fStringTable = new(std::nothrow) char[fStringTableSize];
	if (fSymbolTable == NULL || fStringTable == NULL)
		return B_NO_MEMORY;

	// get the info
	error = _kern_read_kernel_image_symbols(commPageID,
		fSymbolTable, &fSymbolCount, fStringTable, &fStringTableSize, NULL);
	if (error != B_OK) {
		delete[] fSymbolTable;
		delete[] fStringTable;
		return error;
	}

	fLoadDelta = (addr_t)info.text;

	return B_OK;
}
