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
 *   Copyright 2009-2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ElfFile.cpp
 * @brief Implementation of ElfFile, ElfSection, and ElfSegment: the
 *        debugger's read-only ELF parser.
 *
 * @c ElfFile::Init() opens an ELF binary, reads the e_ident magic to
 * detect endianness and bitness, then dispatches to the templated
 * @c _LoadFile<ElfClass32|ElfClass64>() worker which decodes the
 * section and program-header tables into in-memory @c ElfSection and
 * @c ElfSegment objects. @c CreateSymbolLookup() builds an
 * @c ElfSymbolLookup walker over the file's symbol and string tables;
 * the walker reads via the embedded @c SymbolLookupSource which
 * @c pread()s from the open file descriptor on demand.
 */

#include "ElfFile.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <new>

#include <AutoDeleter.h>

#include "ElfSymbolLookup.h"
#include "Tracing.h"


// #pragma mark - ElfSection


/**
 * @brief Constructs an ElfSection descriptor referencing data inside the file.
 *
 * The data buffer is not loaded yet; callers must invoke @c Load() before
 * accessing @c Data().
 *
 * @param name        Section name (interned by the caller).
 * @param type        Section type (one of @c SHT_*).
 * @param fd          Open ELF file descriptor.
 * @param offset      File offset of the section's bytes.
 * @param size        Section size in bytes.
 * @param loadAddress Run-time virtual address of the section.
 * @param flags       Section flags (@c SHF_*).
 * @param linkIndex   Index of an associated section (e.g. string table).
 */
ElfSection::ElfSection(const char* name, uint32 type, int fd, uint64 offset,
	uint64 size, target_addr_t loadAddress, uint32 flags, uint32 linkIndex)
	:
	fName(name),
	fType(type),
	fFD(fd),
	fOffset(offset),
	fSize(size),
	fData(NULL),
	fLoadAddress(loadAddress),
	fFlags(flags),
	fLoadCount(0),
	fLinkIndex(linkIndex)
{
}


/**
 * @brief Frees the section's loaded byte buffer if any remains.
 */
ElfSection::~ElfSection()
{
	free(fData);
}


/**
 * @brief Pages the section's bytes into memory; reference-counted.
 *
 * Each call increments the load count; the first call performs the actual
 * read. Pair every successful call with @c Unload().
 *
 * @return @c B_OK on success, @c B_NO_MEMORY on allocation failure, an
 *          @c errno code on read failure, or @c B_ERROR on a short read.
 */
status_t
ElfSection::Load()
{
	if (fLoadCount > 0) {
		fLoadCount++;
		return B_OK;
	}

	fData = malloc(fSize);
	if (fData == NULL)
		return B_NO_MEMORY;

	ssize_t bytesRead = pread(fFD, fData, fSize, fOffset);
	if (bytesRead < 0 || (uint64)bytesRead != fSize) {
		free(fData);
		fData = NULL;
		return bytesRead < 0 ? errno : B_ERROR;
	}

	fLoadCount++;
	return B_OK;
}


/**
 * @brief Decrements the load count; frees the buffer when it reaches zero.
 */
void
ElfSection::Unload()
{
	if (fLoadCount == 0)
		return;

	if (--fLoadCount == 0) {
		free(fData);
		fData = NULL;
	}
}


// #pragma mark - ElfSegment


/**
 * @brief Constructs an ElfSegment describing one program-header entry.
 *
 * @param type        Segment type (@c PT_LOAD, @c PT_NOTE, etc.).
 * @param fileOffset  Offset of the segment's bytes inside the ELF file.
 * @param fileSize    Number of bytes the segment occupies in the file.
 * @param loadAddress Run-time virtual address of the segment.
 * @param loadSize    Number of bytes the segment occupies in memory.
 * @param flags       Segment flags (@c PF_*).
 */
ElfSegment::ElfSegment(uint32 type, uint64 fileOffset, uint64 fileSize,
	target_addr_t loadAddress, target_size_t loadSize, uint32 flags)
	:
	fFileOffset(fileOffset),
	fFileSize(fileSize),
	fLoadAddress(loadAddress),
	fLoadSize(loadSize),
	fType(type),
	fFlags(flags)
{
}


/**
 * @brief Destroys the ElfSegment.
 */
ElfSegment::~ElfSegment()
{
}


// #pragma mark - SymbolLookupSource


/**
 * @brief In-file ElfSymbolLookupSource backed by a list of file segments.
 *
 * Each registered segment maps a (file-offset, length) range to a memory
 * address as seen by the symbol-table consumer. @c Read() finds the
 * segment covering the requested address and @c pread()s from the
 * underlying file descriptor.
 */
struct ElfFile::SymbolLookupSource : public ElfSymbolLookupSource {
	/**
	 * @brief Constructs the source over file descriptor @a fd.
	 *
	 * @param fd Open ELF file descriptor; SymbolLookupSource does not own it.
	 */
	SymbolLookupSource(int fd)
		:
		fFd(fd),
		fSegments(8)
	{
	}

	/**
	 * @brief Registers a (file-offset, length, memory-address) mapping.
	 *
	 * @param fileOffset    Offset inside the ELF file.
	 * @param fileLength    Length of the mapping in bytes.
	 * @param memoryAddress Address as seen by the symbol-table consumer.
	 * @return             True on success, false on allocation failure.
	 */
	bool AddSegment(uint64 fileOffset, uint64 fileLength, uint64 memoryAddress)
	{
		Segment* segment = new(std::nothrow) Segment(fileOffset, fileLength,
			memoryAddress);
		if (segment == NULL || !fSegments.AddItem(segment)) {
			delete segment;
			return false;
		}
		return true;
	}

	/**
	 * @brief Reads bytes from the file at @a address via the segment table.
	 *
	 * Locates the registered segment whose memory range covers @a address
	 * and translates the request into a @c pread() on the file descriptor.
	 *
	 * @param address Memory-space address of the data.
	 * @param buffer  Destination buffer.
	 * @param size    Maximum byte count to read.
	 * @return       Bytes read, or @c B_BAD_VALUE if no segment matches,
	 *                or an @c errno code on read failure.
	 */
	virtual ssize_t Read(uint64 address, void* buffer, size_t size)
	{
		for (int32 i = 0; Segment* segment = fSegments.ItemAt(i); i++) {
			if (address < segment->fMemoryAddress
					|| address - segment->fMemoryAddress
						> segment->fFileLength) {
				continue;
			}

			uint64 offset = address - segment->fMemoryAddress;
			size_t toRead = (size_t)std::min((uint64)size,
				segment->fFileLength - offset);
			if (toRead == 0)
				return 0;

			ssize_t bytesRead = pread(fFd, buffer, toRead,
				(off_t)(segment->fFileOffset + offset));
			if (bytesRead < 0)
				return errno;
			return bytesRead;
		}

		return B_BAD_VALUE;
	}

private:
	/**
	 * @brief One registered (file-offset, length, memory-address) mapping.
	 */
	struct Segment {
		uint64	fFileOffset;
		uint64	fFileLength;
		uint64	fMemoryAddress;

		/**
		 * @brief Constructs a Segment mapping.
		 *
		 * @param fileOffset    File offset.
		 * @param fileLength    Mapping length in bytes.
		 * @param memoryAddress Memory-space address of the mapping.
		 */
		Segment(uint64 fileOffset, uint64 fileLength, uint64 memoryAddress)
			:
			fFileOffset(fileOffset),
			fFileLength(fileLength),
			fMemoryAddress(memoryAddress)
		{
		}
	};

private:
	int							fFd;
	BObjectList<Segment, true>	fSegments;
};


// #pragma mark - ElfFile


/**
 * @brief Constructs an empty, uninitialised ElfFile.
 *
 * @c Init() must be called before any accessor returns meaningful data.
 */
ElfFile::ElfFile()
	:
	fFileSize(0),
	fFD(-1),
	fType(ET_NONE),
	fMachine(EM_NONE),
	f64Bit(false),
	fSwappedByteOrder(false),
	fSections(16),
	fSegments(16)
{
}


/**
 * @brief Closes the underlying file descriptor; sections are freed by the lists.
 */
ElfFile::~ElfFile()
{
	if (fFD >= 0)
		close(fFD);
}


/**
 * @brief Opens @a fileName, validates the ELF magic, and parses headers.
 *
 * Reads the ELF identification bytes to determine endianness and bitness,
 * then dispatches to either @c _LoadFile<ElfClass32>() or
 * @c _LoadFile<ElfClass64>() to populate the section and segment lists.
 *
 * @param fileName Path to the ELF file to open.
 * @return        @c B_OK on success, an @c errno code on open/stat
 *                 failure, @c B_ERROR on a short identification read,
 *                 @c B_BAD_DATA on missing magic or invalid class/data
 *                 fields, or a propagated load error.
 */
status_t
ElfFile::Init(const char* fileName)
{
	// open file
	fFD = open(fileName, O_RDONLY);
	if (fFD < 0) {
		WARNING("Failed to open \"%s\": %s\n", fileName, strerror(errno));
		return errno;
	}

	// stat() file to get its size
	struct stat st;
	if (fstat(fFD, &st) < 0) {
		WARNING("Failed to stat \"%s\": %s\n", fileName, strerror(errno));
		return errno;
	}
	fFileSize = st.st_size;

	// Read the identification information to determine whether this is an
	// ELF file at all and some relevant properties for reading it.
	uint8 elfIdent[EI_NIDENT];
	ssize_t bytesRead = pread(fFD, elfIdent, sizeof(elfIdent), 0);
	if (bytesRead != (ssize_t)sizeof(elfIdent))
		return bytesRead < 0 ? errno : B_ERROR;

	// magic
	if (!(memcmp(elfIdent, ELFMAG, 4) == 0))
		return B_ERROR;

	// endianess
	if (elfIdent[EI_DATA] == ELFDATA2LSB) {
		fSwappedByteOrder = B_HOST_IS_BENDIAN != 0;
	} else if (elfIdent[EI_DATA] == ELFDATA2MSB) {
		fSwappedByteOrder = B_HOST_IS_LENDIAN != 0;
	} else {
		WARNING("%s: Invalid ELF data byte order: %d\n", fileName,
			elfIdent[EI_DATA]);
		return B_BAD_DATA;
	}

	// determine class and load
	if(elfIdent[EI_CLASS] == ELFCLASS64) {
		f64Bit = true;
		return _LoadFile<ElfClass64>(fileName);
	}
	if(elfIdent[EI_CLASS] == ELFCLASS32) {
		f64Bit = false;
		return _LoadFile<ElfClass32>(fileName);
	}

	WARNING("%s: Invalid ELF class: %d\n", fileName, elfIdent[EI_CLASS]);
	return B_BAD_DATA;
}


/**
 * @brief Finds the section named @a name and pages it in via @c Load().
 *
 * Pair every successful call with @c PutSection().
 *
 * @param name Section name (e.g. ".symtab").
 * @return    The loaded section on success, NULL if missing or load failed.
 */
ElfSection*
ElfFile::GetSection(const char* name)
{
	ElfSection* section = FindSection(name);
	if (section != NULL && section->Load() == B_OK)
		return section;

	return NULL;
}


/**
 * @brief Releases a section previously returned from @c GetSection().
 *
 * @param section Section to release; NULL is tolerated.
 */
void
ElfFile::PutSection(ElfSection* section)
{
	if (section != NULL)
		section->Unload();
}


/**
 * @brief Looks up a section by name without loading it.
 *
 * @param name Section name to search for.
 * @return    The matching section, or NULL if absent.
 */
ElfSection*
ElfFile::FindSection(const char* name) const
{
	int32 count = fSections.CountItems();
	for (int32 i = 0; i < count; i++) {
		ElfSection* section = fSections.ItemAt(i);
		if (strcmp(section->Name(), name) == 0)
			return section;
	}

	return NULL;
}


/**
 * @brief Looks up the first section with the given type.
 *
 * @param type Section type (one of @c SHT_*).
 * @return    The first matching section, or NULL.
 */
ElfSection*
ElfFile::FindSection(uint32 type) const
{
	int32 count = fSections.CountItems();
	for (int32 i = 0; i < count; i++) {
		ElfSection* section = fSections.ItemAt(i);
		if (section->Type() == type)
			return section;
	}

	return NULL;
}


/**
 * @brief Returns the first non-writable @c PT_LOAD segment (the text segment).
 *
 * @return The text segment, or NULL if none found.
 */
ElfSegment*
ElfFile::TextSegment() const
{
	int32 count = fSegments.CountItems();
	for (int32 i = 0; i < count; i++) {
		ElfSegment* segment = fSegments.ItemAt(i);
		if (segment->Type() == PT_LOAD && !segment->IsWritable())
			return segment;
	}

	return NULL;
}


/**
 * @brief Returns the first writable @c PT_LOAD segment (the data segment).
 *
 * @return The data segment, or NULL if none found.
 */
ElfSegment*
ElfFile::DataSegment() const
{
	int32 count = fSegments.CountItems();
	for (int32 i = 0; i < count; i++) {
		ElfSegment* segment = fSegments.ItemAt(i);
		if (segment->Type() == PT_LOAD && segment->IsWritable())
			return segment;
	}

	return NULL;
}


/**
 * @brief Creates a SymbolLookupSource over a single file-offset range.
 *
 * @param fileOffset    File offset of the data.
 * @param fileLength    Length of the data in bytes.
 * @param memoryAddress Memory-space address as seen by the consumer.
 * @return             Newly allocated source on success, NULL on failure.
 *                      Caller owns one reference.
 */
ElfSymbolLookupSource*
ElfFile::CreateSymbolLookupSource(uint64 fileOffset, uint64 fileLength,
	uint64 memoryAddress) const
{
	SymbolLookupSource* source = new(std::nothrow) SymbolLookupSource(fFD);
	if (source == NULL
			|| !source->AddSegment(fileOffset, fileLength, memoryAddress)) {
		delete source;
		return NULL;
	}

	return source;
}


/**
 * @brief Builds an ElfSymbolLookup over the file's symbol and string sections.
 *
 * Prefers the non-dynamic @c .symtab section when present; falls back to
 * the dynamic @c .dynsym. The returned walker reads from a freshly
 * allocated SymbolLookupSource that knows both section ranges.
 *
 * @param textDelta Address adjustment applied to each symbol address.
 * @param _lookup   On success, receives the new walker; ownership transfers
 *                   to the caller.
 * @return         @c B_OK on success, @c B_ENTRY_NOT_FOUND when neither
 *                  symbol section is present, @c B_NO_MEMORY on
 *                  allocation failure, or a propagated init error.
 */
status_t
ElfFile::CreateSymbolLookup(uint64 textDelta, ElfSymbolLookup*& _lookup) const
{
	// Get the symbol table + corresponding string section. There may be two
	// symbol tables: the dynamic and the non-dynamic one. The former contains
	// only the symbols needed at run-time. The latter, if existing, is likely
	// more complete. So try to find and use the latter one, falling back to the
	// former.
	ElfSection* symbolSection;
	ElfSection* stringSection;
	if (!_FindSymbolSections(symbolSection, stringSection, SHT_SYMTAB)
		&& !_FindSymbolSections(symbolSection, stringSection, SHT_DYNSYM)) {
		return B_ENTRY_NOT_FOUND;
	}

	// create a source with a segment for each section
	SymbolLookupSource* source = new(std::nothrow) SymbolLookupSource(fFD);
	if (source == NULL)
		return B_NO_MEMORY;
	BReference<SymbolLookupSource> sourceReference(source, true);

	if (!source->AddSegment(symbolSection->Offset(), symbolSection->Size(),
				symbolSection->Offset())
		|| !source->AddSegment(stringSection->Offset(), stringSection->Size(),
				stringSection->Offset())) {
		return B_NO_MEMORY;
	}

	// create the lookup
	size_t symbolTableEntrySize = Is64Bit()
		? sizeof(ElfClass64::Sym) : sizeof(ElfClass32::Sym);
	uint32 symbolCount = uint32(symbolSection->Size() / symbolTableEntrySize);

	return ElfSymbolLookup::Create(source, symbolSection->Offset(), 0,
		stringSection->Offset(), symbolCount, symbolTableEntrySize, textDelta,
		f64Bit, fSwappedByteOrder, true, _lookup);
}


/**
 * @brief Templated worker that decodes the ELF section and program tables.
 *
 * Templated on @c ElfClass32 or @c ElfClass64 to handle both bitnesses
 * with the same code. Validates header offsets, allocates @c ElfSection
 * objects for each section header, then walks the program-header table
 * to allocate @c ElfSegment objects.
 *
 * @param fileName File name used for diagnostic output.
 * @return        @c B_OK on success, @c B_BAD_DATA on header validation
 *                 failure, @c B_NO_MEMORY on allocation failure, or an
 *                 @c errno code on read failure.
 */
template<typename ElfClass>
status_t
ElfFile::_LoadFile(const char* fileName)
{
	typedef typename ElfClass::Ehdr Ehdr;
	typedef typename ElfClass::Phdr Phdr;
	typedef typename ElfClass::Shdr Shdr;

	// read the elf header
	Ehdr elfHeader;
	ssize_t bytesRead = pread(fFD, &elfHeader, sizeof(elfHeader), 0);
	if (bytesRead != (ssize_t)sizeof(elfHeader))
		return bytesRead < 0 ? errno : B_ERROR;

	// check the ELF header
	if (!_CheckRange(0, sizeof(elfHeader))
		|| !_CheckElfHeader<ElfClass>(elfHeader)) {
		WARNING("\"%s\": Not a valid ELF file\n", fileName);
		return B_BAD_DATA;
	}

	fType = Get(elfHeader.e_type);
	fMachine = Get(elfHeader.e_machine);

	if (Get(elfHeader.e_shnum) > 0) {
		// check section header table values
		uint64 sectionHeadersOffset = Get(elfHeader.e_shoff);
		size_t sectionHeaderSize = Get(elfHeader.e_shentsize);
		int sectionCount = Get(elfHeader.e_shnum);
		size_t sectionHeaderTableSize = sectionHeaderSize * sectionCount;
		if (!_CheckRange(sectionHeadersOffset, sectionHeaderTableSize)) {
			WARNING("\"%s\": Invalid ELF header\n", fileName);
			return B_BAD_DATA;
		}

		// read the section header table
		uint8* sectionHeaderTable = (uint8*)malloc(sectionHeaderTableSize);
		if (sectionHeaderTable == NULL)
			return B_NO_MEMORY;
		MemoryDeleter sectionHeaderTableDeleter(sectionHeaderTable);

		bytesRead = pread(fFD, sectionHeaderTable, sectionHeaderTableSize,
			sectionHeadersOffset);
		if (bytesRead != (ssize_t)sectionHeaderTableSize)
			return bytesRead < 0 ? errno : B_ERROR;

		// check and get the section header string section
		Shdr* stringSectionHeader = (Shdr*)(sectionHeaderTable
			+ Get(elfHeader.e_shstrndx) * sectionHeaderSize);
		if (!_CheckRange(Get(stringSectionHeader->sh_offset),
				Get(stringSectionHeader->sh_size))) {
			WARNING("\"%s\": Invalid string section header\n", fileName);
			return B_BAD_DATA;
		}
		size_t sectionStringSize = Get(stringSectionHeader->sh_size);

		ElfSection* sectionStringSection = new(std::nothrow) ElfSection(
			".shstrtab", Get(stringSectionHeader->sh_type),fFD,
			Get(stringSectionHeader->sh_offset), sectionStringSize,
			Get(stringSectionHeader->sh_addr),
			Get(stringSectionHeader->sh_flags),
			Get(stringSectionHeader->sh_link));
		if (sectionStringSection == NULL)
			return B_NO_MEMORY;
		if (!fSections.AddItem(sectionStringSection)) {
			delete sectionStringSection;
			return B_NO_MEMORY;
		}

		status_t error = sectionStringSection->Load();
		if (error != B_OK)
			return error;

		const char* sectionStrings = (const char*)sectionStringSection->Data();

		// read the other sections
		for (int i = 0; i < sectionCount; i++) {
			Shdr* sectionHeader = (Shdr*)(sectionHeaderTable + i
				* sectionHeaderSize);
			// skip invalid sections and the section header string section
			const char* name = sectionStrings + Get(sectionHeader->sh_name);
			if (Get(sectionHeader->sh_name) >= sectionStringSize
				|| !_CheckRange(Get(sectionHeader->sh_offset),
					Get(sectionHeader->sh_size))
				|| i == Get(elfHeader.e_shstrndx)) {
				continue;
			}

			// create an ElfSection
			ElfSection* section = new(std::nothrow) ElfSection(name,
				Get(sectionHeader->sh_type), fFD, Get(sectionHeader->sh_offset),
				Get(sectionHeader->sh_size), Get(sectionHeader->sh_addr),
				Get(sectionHeader->sh_flags), Get(sectionHeader->sh_link));
			if (section == NULL)
				return B_NO_MEMORY;
			if (!fSections.AddItem(section)) {
				delete section;
				return B_NO_MEMORY;
			}
		}
	}

	if (Get(elfHeader.e_phnum) > 0) {
		// check program header table values
		uint64 programHeadersOffset = Get(elfHeader.e_phoff);
		size_t programHeaderSize = Get(elfHeader.e_phentsize);
		int segmentCount = Get(elfHeader.e_phnum);
		size_t programHeaderTableSize = programHeaderSize * segmentCount;
		if (!_CheckRange(programHeadersOffset, programHeaderTableSize)) {
			WARNING("\"%s\": Invalid ELF header\n", fileName);
			return B_BAD_DATA;
		}

		// read the program header table
		uint8* programHeaderTable = (uint8*)malloc(programHeaderTableSize);
		if (programHeaderTable == NULL)
			return B_NO_MEMORY;
		MemoryDeleter programHeaderTableDeleter(programHeaderTable);

		bytesRead = pread(fFD, programHeaderTable, programHeaderTableSize,
			programHeadersOffset);
		if (bytesRead != (ssize_t)programHeaderTableSize)
			return bytesRead < 0 ? errno : B_ERROR;

		// read the program headers and create ElfSegment objects
		for (int i = 0; i < segmentCount; i++) {
			Phdr* programHeader = (Phdr*)(programHeaderTable + i
				* programHeaderSize);
			// skip invalid program headers
			if (Get(programHeader->p_filesz) > 0
				&& !_CheckRange(Get(programHeader->p_offset),
					Get(programHeader->p_filesz))) {
				continue;
			}

			// create an ElfSegment
			ElfSegment* segment = new(std::nothrow) ElfSegment(
				Get(programHeader->p_type), Get(programHeader->p_offset),
				Get(programHeader->p_filesz), Get(programHeader->p_vaddr),
				Get(programHeader->p_memsz), Get(programHeader->p_flags));
			if (segment == NULL)
				return B_NO_MEMORY;
			if (!fSegments.AddItem(segment)) {
				delete segment;
				return B_NO_MEMORY;
			}
		}
	}

	return B_OK;
}


/**
 * @brief Finds a symbol section of the given type and its linked string section.
 *
 * @param _symbolSection On success, receives the symbol section.
 * @param _stringSection On success, receives the linked string section.
 * @param type           Section type (@c SHT_SYMTAB or @c SHT_DYNSYM).
 * @return              True on success, false if either section is missing
 *                       or the link target is not a string table.
 */
bool
ElfFile::_FindSymbolSections(ElfSection*& _symbolSection,
	ElfSection*& _stringSection, uint32 type) const
{
	// get the symbol table section
	ElfSection* symbolSection = FindSection(type);
	if (symbolSection == NULL)
		return false;

	// The symbol table section is linked to the corresponding string section.
	ElfSection* stringSection = SectionAt(symbolSection->LinkIndex());
	if (stringSection == NULL || stringSection->Type() != SHT_STRTAB)
		return false;

	_symbolSection = symbolSection;
	_stringSection = stringSection;
	return true;
}


/**
 * @brief Tests whether @c [offset, offset + size) lies inside the file.
 *
 * @param offset Byte offset.
 * @param size   Range size in bytes.
 * @return      True if the range fits inside the file.
 */
bool
ElfFile::_CheckRange(uint64 offset, uint64 size) const
{
	return offset < fFileSize && offset + size <= fFileSize;
}


/**
 * @brief Validates section/program header table fields in @a elfHeader.
 *
 * Templated worker shared by 32-bit and 64-bit headers.
 *
 * @param elfHeader ELF header to validate.
 * @return         True if the header is internally consistent.
 */
template<typename ElfClass>
bool
ElfFile::_CheckElfHeader(typename ElfClass::Ehdr& elfHeader)
{
	if (Get(elfHeader.e_shnum) > 0) {
		if (Get(elfHeader.e_shoff) == 0
			|| Get(elfHeader.e_shentsize) < sizeof(typename ElfClass::Shdr)
			|| Get(elfHeader.e_shstrndx) == SHN_UNDEF
			|| Get(elfHeader.e_shstrndx) >= Get(elfHeader.e_shnum)) {
			return false;
		}
	}

	if (Get(elfHeader.e_phnum) > 0) {
		if (Get(elfHeader.e_phoff) == 0
			|| Get(elfHeader.e_phentsize) < sizeof(typename ElfClass::Phdr)) {
			return false;
		}
	}

	return true;
}
