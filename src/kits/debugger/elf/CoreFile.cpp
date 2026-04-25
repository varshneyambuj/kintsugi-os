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
 * @file CoreFile.cpp
 * @brief Implementation of CoreFile, the parser for an ELF core dump
 *        produced by the kernel for a debugged team.
 *
 * The parser opens the core file via @c ElfFile, locates the @c PT_NOTE
 * program-header segments, and decodes the Haiku-specific note types
 * (@c NT_TEAM, @c NT_AREAS, @c NT_IMAGES, @c NT_SYMBOLS, @c NT_THREADS)
 * into the corresponding descriptor structs (CoreFileTeamInfo,
 * CoreFileAreaInfo, CoreFileImageInfo, CoreFileSymbolsInfo,
 * CoreFileThreadInfo). It also exposes a factory that wires an
 * @c ElfSymbolLookup walker over an image's symbol/string-table addresses
 * inside the core's text segment.
 */


#include "CoreFile.h"

#include <errno.h>

#include <algorithm>

#include <OS.h>

#include <AutoDeleter.h>

#include "ElfSymbolLookup.h"
#include "Tracing.h"


/** @brief Hard upper bound on the size of a single PT_NOTE segment. */
static const size_t kMaxNotesSize = 10 * 1024 * 1024;


// pragma mark - CoreFileTeamInfo


/**
 * @brief Constructs an empty CoreFileTeamInfo with invalid identifiers.
 */
CoreFileTeamInfo::CoreFileTeamInfo()
	:
	fId(-1),
	fUid(-1),
	fGid(-1),
	fArgs()
{
}


/**
 * @brief Populates the team info from the values in the @c NT_TEAM note.
 *
 * @param id   Team identifier.
 * @param uid  Owning user id.
 * @param gid  Owning group id.
 * @param args Command-line argument string.
 */
void
CoreFileTeamInfo::Init(int32 id, int32 uid, int32 gid, const BString& args)
{
	fId = id;
	fUid = uid;
	fGid = gid;
	fArgs = args;
}


// pragma mark - CoreFileAreaInfo


/**
 * @brief Constructs a CoreFileAreaInfo binding an area to its backing segment.
 *
 * @param segment    ElfSegment inside the core that backs the area's contents.
 * @param id         Area identifier.
 * @param baseAddress Base address of the area in the dumped team.
 * @param size       Virtual size of the area in bytes.
 * @param ramSize    Resident size in bytes.
 * @param locking    Locking flags.
 * @param protection Protection flags.
 * @param name       Display name (currently stored on the field but not used here).
 */
CoreFileAreaInfo::CoreFileAreaInfo(ElfSegment* segment, int32 id,
	uint64 baseAddress, uint64 size, uint64 ramSize, uint32 locking,
	uint32 protection, const BString& name)
	:
	fSegment(segment),
	fBaseAddress(baseAddress),
	fSize(size),
	fRamSize(ramSize),
	fLocking(locking),
	fProtection(protection),
	fId(-1)
{
}


// pragma mark - CoreFileImageInfo


/**
 * @brief Constructs a CoreFileImageInfo from one entry of the @c NT_IMAGES note.
 *
 * Stores back-pointers to the @c CoreFileAreaInfo objects that own the
 * text and data segments so symbol lookups can locate the actual bytes
 * inside the dump.
 *
 * @param id          Image identifier.
 * @param type        Image type.
 * @param initRoutine Init function address.
 * @param termRoutine Term function address.
 * @param textBase    Base address of the text segment.
 * @param textSize    Size of the text segment in bytes.
 * @param textDelta   Address adjustment applied to text-segment symbols.
 * @param dataBase    Base address of the data segment.
 * @param dataSize    Size of the data segment in bytes.
 * @param deviceId    Device id of the on-disk file.
 * @param nodeId      Inode number of the on-disk file.
 * @param symbolTable Source-space address of the image's symbol table.
 * @param symbolHash  Source-space address of the image's symbol-hash table.
 * @param stringTable Source-space address of the image's string table.
 * @param textArea    CoreFileAreaInfo for the text segment.
 * @param dataArea    CoreFileAreaInfo for the data segment.
 * @param name        Display name (path) of the image.
 */
CoreFileImageInfo::CoreFileImageInfo(int32 id, int32 type, uint64 initRoutine,
	uint64 termRoutine, uint64 textBase, uint64 textSize, int64 textDelta,
	uint64 dataBase, uint64 dataSize, int32 deviceId, int64 nodeId,
	uint64 symbolTable, uint64 symbolHash, uint64 stringTable,
	CoreFileAreaInfo* textArea, CoreFileAreaInfo* dataArea, const BString& name)
	:
	fId(id),
	fType(type),
	fInitRoutine(initRoutine),
	fTermRoutine(termRoutine),
	fTextBase(textBase),
	fTextSize(textSize),
	fTextDelta(textDelta),
	fDataBase(dataBase),
	fDataSize(dataSize),
	fDeviceId(deviceId),
	fNodeId(nodeId),
	fSymbolTable(symbolTable),
	fSymbolHash(symbolHash),
	fStringTable(stringTable),
	fTextArea(textArea),
	fDataArea(dataArea),
	fName(name),
	fSymbolsInfo(NULL)
{
}


/**
 * @brief Releases any owned CoreFileSymbolsInfo via @c SetSymbolsInfo(NULL).
 */
CoreFileImageInfo::~CoreFileImageInfo()
{
	SetSymbolsInfo(NULL);
}


/**
 * @brief Replaces (and deletes) the owned CoreFileSymbolsInfo.
 *
 * @param symbolsInfo New symbols info to take ownership of, or NULL to clear.
 */
void
CoreFileImageInfo::SetSymbolsInfo(CoreFileSymbolsInfo* symbolsInfo)
{
	if (fSymbolsInfo != NULL)
		delete fSymbolsInfo;

	fSymbolsInfo = symbolsInfo;
}


// pragma mark - CoreFileSymbolsInfo

/**
 * @brief Constructs an empty CoreFileSymbolsInfo with NULL buffers.
 */
CoreFileSymbolsInfo::CoreFileSymbolsInfo()
	:
	fSymbolTable(NULL),
	fStringTable(NULL),
	fSymbolCount(0),
	fSymbolTableEntrySize(0),
	fStringTableSize(0)
{
}


/**
 * @brief Frees the owned symbol-table and string-table buffers.
 */
CoreFileSymbolsInfo::~CoreFileSymbolsInfo()
{
	free(fSymbolTable);
	free(fStringTable);
}


/**
 * @brief Allocates and copies the symbol- and string-table buffers from the note.
 *
 * @param symbolTable          Pointer to the source symbol-table bytes.
 * @param symbolCount          Number of symbol-table entries.
 * @param symbolTableEntrySize Size of one symbol-table entry in bytes.
 * @param stringTable          Pointer to the source string-table bytes.
 * @param stringTableSize      Length of the string table in bytes.
 * @return                    True on success, false on allocation failure.
 */
bool
CoreFileSymbolsInfo::Init(const void* symbolTable, uint32 symbolCount,
	uint32 symbolTableEntrySize, const char* stringTable,
	uint32 stringTableSize)
{
	fSymbolTable = malloc(symbolCount * symbolTableEntrySize);
	fStringTable = (char*)malloc(stringTableSize);

	if (fSymbolTable == NULL || fStringTable == NULL)
		return false;

	memcpy(fSymbolTable, symbolTable, symbolCount * symbolTableEntrySize);
	memcpy(fStringTable, stringTable, stringTableSize);

	fSymbolCount = symbolCount;
	fSymbolTableEntrySize = symbolTableEntrySize;
	fStringTableSize = stringTableSize;

	return true;
}


// pragma mark - CoreFileThreadInfo


/**
 * @brief Constructs a CoreFileThreadInfo from one entry of the @c NT_THREADS note.
 *
 * The CPU-state blob is filled later via @c SetCpuState().
 *
 * @param id        Thread identifier.
 * @param state     Scheduling state at dump time.
 * @param priority  Thread priority at dump time.
 * @param stackBase Lower bound of the thread's user stack.
 * @param stackEnd  Upper bound of the thread's user stack.
 * @param name      Thread display name.
 */
CoreFileThreadInfo::CoreFileThreadInfo(int32 id, int32 state, int32 priority,
	uint64 stackBase, uint64 stackEnd, const BString& name)
	:
	fId(id),
	fState(state),
	fPriority(priority),
	fStackBase(stackBase),
	fStackEnd(stackEnd),
	fName(name),
	fCpuState(NULL),
	fCpuStateSize(0)
{
}


/**
 * @brief Frees the captured CPU-state blob.
 */
CoreFileThreadInfo::~CoreFileThreadInfo()
{
	free(fCpuState);
}


/**
 * @brief Replaces the captured CPU-state blob.
 *
 * @param state Pointer to the source bytes; NULL clears the field.
 * @param size  Number of bytes to copy from @a state.
 * @return     True on success, false on allocation failure.
 */
bool
CoreFileThreadInfo::SetCpuState(const void* state, size_t size)
{
	free(fCpuState);
	fCpuState = NULL;
	fCpuStateSize = 0;

	if (state != NULL) {
		fCpuState = malloc(size);
		if (fCpuState == NULL)
			return false;
		memcpy(fCpuState, state, size);
		fCpuStateSize = size;
	}

	return true;
}


// pragma mark - CoreFile


/**
 * @brief Constructs an empty CoreFile; @c Init() must be called before use.
 */
CoreFile::CoreFile()
	:
	fElfFile(),
	fTeamInfo(),
	fAreaInfos(32),
	fImageInfos(32),
	fThreadInfos(32)
{
}


/**
 * @brief Destroys the CoreFile and lets the BObjectLists release their items.
 */
CoreFile::~CoreFile()
{
}


/**
 * @brief Opens @a fileName and parses every PT_NOTE segment it contains.
 *
 * @param fileName Path to the ELF core dump.
 * @return        @c B_OK on success, or the propagated error from
 *                 @c ElfFile::Init() or the templated note-decoding worker.
 */
status_t
CoreFile::Init(const char* fileName)
{
	status_t error = fElfFile.Init(fileName);
	if (error != B_OK)
		return error;

	if (fElfFile.Is64Bit())
		return _Init<ElfClass64>();
	return _Init<ElfClass32>();
}


/**
 * @brief Looks up a thread descriptor by id via linear search.
 *
 * @param id Thread identifier.
 * @return  The matching CoreFileThreadInfo, or NULL.
 */
const CoreFileThreadInfo*
CoreFile::ThreadInfoForId(int32 id) const
{
	int32 count = fThreadInfos.CountItems();
	for (int32 i = 0; i < count; i++) {
		CoreFileThreadInfo* info = fThreadInfos.ItemAt(i);
		if (info->Id() == id)
			return info;
	}

	return NULL;
}


/**
 * @brief Builds an ElfSymbolLookup walker over an image's symbol/string tables.
 *
 * The walker reads through a fresh @c ElfSymbolLookupSource pointed at
 * the file-resident bytes of the text segment, allowing symbol lookup
 * without requiring the on-disk image to still exist.
 *
 * @param imageInfo Image whose symbols to expose.
 * @param _lookup   On success, receives the walker; ownership transfers.
 * @return         @c B_OK on success; @c B_UNSUPPORTED when the image
 *                  lacks symbol information; @c B_NO_MEMORY on
 *                  allocation failure; or a propagated walker init error.
 *
 * @todo Read DT_SYMENT to determine the actual symbol-table entry size
 *       rather than assuming the canonical layout.
 */
status_t
CoreFile::CreateSymbolLookup(const CoreFileImageInfo* imageInfo,
	ElfSymbolLookup*& _lookup)
{
	// get the needed data
	uint64 textDelta = imageInfo->TextDelta();
	uint64 symbolTable = imageInfo->SymbolTable();
	uint64 symbolHash = imageInfo->SymbolHash();
	uint64 stringTable = imageInfo->StringTable();
	CoreFileAreaInfo* textArea = imageInfo->TextArea();
	ElfSegment* textSegment = textArea != NULL ? textArea->Segment() : NULL;

	if (symbolTable == 0 || symbolHash == 0 || stringTable == 0
			|| textSegment == NULL) {
		return B_UNSUPPORTED;
	}

	// create a data source for the text segment
	ElfSymbolLookupSource* source = fElfFile.CreateSymbolLookupSource(
		textSegment->FileOffset(), textSegment->FileSize(),
		textSegment->LoadAddress());
	if (source == NULL)
		return B_NO_MEMORY;

	// get the symbol table entry size
	// TODO: This is not actually correct, since at least theoretically the
	// entry size may differ (cf. DT_SYMENT in the dynamic segment).
	size_t symbolTableEntrySize = fElfFile.Is64Bit()
		? sizeof(ElfClass64::Sym) : sizeof(ElfClass32::Sym);

	// create the symbol lookup
	return ElfSymbolLookup::Create(source, symbolTable, symbolHash, stringTable,
		ElfSymbolLookup::kGetSymbolCountFromHash, symbolTableEntrySize,
		textDelta, fElfFile.Is64Bit(), fElfFile.IsByteOrderSwapped(), true,
		_lookup);
}


/**
 * @brief Templated stage-two initialiser dispatching note parsing.
 *
 * Templated on @c ElfClass32 / @c ElfClass64 so the same code resolves
 * 32-bit and 64-bit dumps. Currently delegates to @c _ReadNotes().
 *
 * @return @c B_OK on success or the propagated note-parser error.
 *
 * @todo Verify that the dump produced at least one usable record.
 */
template<typename ElfClass>
status_t
CoreFile::_Init()
{
	status_t error = _ReadNotes<ElfClass>();
	if (error != B_OK)
		return error;
printf("CoreFile::_Init(): got %" B_PRId32 " areas, %" B_PRId32 " images, %"
B_PRId32 " threads\n", CountAreaInfos(), CountImageInfos(), CountThreadInfos());
	// TODO: Verify that we actually read something!
	return B_OK;
}


/**
 * @brief Walks every PT_NOTE segment of the core file.
 *
 * @return @c B_OK on success, or the first error from a sub-call.
 */
template<typename ElfClass>
status_t
CoreFile::_ReadNotes()
{
	int32 count = fElfFile.CountSegments();
	for (int32 i = 0; i < count; i++) {
		ElfSegment* segment = fElfFile.SegmentAt(i);
		if (segment->Type() == PT_NOTE) {
			status_t error = _ReadNotes<ElfClass>(segment);
			if (error != B_OK)
				return error;
		}
	}

	return B_OK;
}


/**
 * @brief Reads one PT_NOTE segment fully into memory and iterates its notes.
 *
 * Each note has a header, a name, and a data area, each padded to a
 * 4-byte boundary. Names are NUL-validated; data is dispatched to
 * @c _ReadNote().
 *
 * @param segment PT_NOTE segment to consume.
 * @return       @c B_OK on success, @c B_UNSUPPORTED if the segment
 *                exceeds @c kMaxNotesSize, an @c errno code on read
 *                failure, @c B_IO_ERROR on a short read, or
 *                @c B_BAD_DATA on validation failures.
 */
template<typename ElfClass>
status_t
CoreFile::_ReadNotes(ElfSegment* segment)
{
	// read the whole segment into memory
	if ((uint64)segment->FileSize() > kMaxNotesSize) {
		WARNING("Notes segment too large (%" B_PRIdOFF ")\n",
			segment->FileSize());
		return B_UNSUPPORTED;
	}

	size_t notesSize = (size_t)segment->FileSize();
	uint8* notes = (uint8*)malloc(notesSize);
	if (notes == NULL)
		return B_NO_MEMORY;
	MemoryDeleter notesDeleter(notes);

	ssize_t bytesRead = pread(fElfFile.FD(), notes, notesSize,
		(off_t)segment->FileOffset());
	if (bytesRead < 0) {
		WARNING("Failed to read notes segment: %s\n", strerror(errno));
		return errno;
	}
	if ((size_t)bytesRead != notesSize) {
		WARNING("Failed to read whole notes segment\n");
		return B_IO_ERROR;
	}

	// iterate through notes
	typedef typename ElfClass::Nhdr Nhdr;
	while (notesSize > 0) {
		if (notesSize < sizeof(Nhdr)) {
			WARNING("Remaining bytes in notes segment too short for header\n");
			return B_BAD_DATA;
		}

		const Nhdr* header = (const Nhdr*)notes;
		uint32 nameSize = Get(header->n_namesz);
		uint32 dataSize = Get(header->n_descsz);
		uint32 type = Get(header->n_type);

		notes += sizeof(Nhdr);
		notesSize -= sizeof(Nhdr);

		size_t alignedNameSize = (nameSize + 3) / 4 * 4;
		if (alignedNameSize > notesSize) {
			WARNING("Not enough bytes remaining in notes segment for note "
				"name (%zu / %zu)\n", notesSize, alignedNameSize);
			return B_BAD_DATA;
		}

		const char* name = (const char*)notes;
		size_t nameLen = strnlen(name, nameSize);
		if (nameLen == nameSize) {
			WARNING("Unterminated note name\n");
			return B_BAD_DATA;
		}

		notes += alignedNameSize;
		notesSize -= alignedNameSize;

		size_t alignedDataSize = (dataSize + 3) / 4 * 4;
		if (alignedDataSize > notesSize) {
			WARNING("Not enough bytes remaining in notes segment for note "
				"data\n");
			return B_BAD_DATA;
		}

		_ReadNote<ElfClass>(name, type, notes, dataSize);

		notes += alignedDataSize;
		notesSize -= alignedDataSize;
	}

	return B_OK;
}


/**
 * @brief Dispatches one note to its type-specific decoder.
 *
 * Recognises @c ELF_NOTE_HAIKU notes carrying team, areas, images,
 * symbols, or threads payloads. @c ELF_NOTE_CORE / @c NT_FILE notes are
 * accepted but currently ignored.
 *
 * @param name     Note name (NUL-terminated).
 * @param type     Note type identifier.
 * @param data     Pointer to the note data.
 * @param dataSize Length of the note data in bytes.
 * @return        @c B_OK on success or the propagated decoder error;
 *                 unknown notes are logged and skipped.
 */
template<typename ElfClass>
status_t
CoreFile::_ReadNote(const char* name, uint32 type, const void* data,
	uint32 dataSize)
{
	if (strcmp(name, ELF_NOTE_CORE) == 0) {
		switch (type) {
			case NT_FILE:
				// not needed
				return B_OK;
		}
	} else if (strcmp(name, ELF_NOTE_HAIKU) == 0) {
		switch (type) {
			case NT_TEAM:
				return _ReadTeamNote<ElfClass>(data, dataSize);
			case NT_AREAS:
				return _ReadAreasNote<ElfClass>(data, dataSize);
			case NT_IMAGES:
				return _ReadImagesNote<ElfClass>(data, dataSize);
			case NT_SYMBOLS:
				return _ReadSymbolsNote<ElfClass>(data, dataSize);
			case NT_THREADS:
				return _ReadThreadsNote<ElfClass>(data, dataSize);
			break;
		}
	}

	WARNING("Unsupported note type %s/%#" B_PRIx32 "\n", name, type);
	return B_OK;
}


/**
 * @brief Decodes the @c NT_TEAM note into @c fTeamInfo.
 *
 * Reads the leading entry-size word, the @c NoteTeam structure, and the
 * trailing NUL-terminated arguments string.
 *
 * @param data     Pointer to the note data.
 * @param dataSize Length of the note data in bytes.
 * @return        @c B_OK on success; @c B_BAD_DATA on malformed input;
 *                 @c B_NO_MEMORY on string allocation failure.
 */
template<typename ElfClass>
status_t
CoreFile::_ReadTeamNote(const void* data, uint32 dataSize)
{
	typedef typename ElfClass::NoteTeam NoteTeam;

	if (dataSize < sizeof(uint32)) {
		WARNING("Team note too short\n");
		return B_BAD_DATA;
	}
	uint32 entrySize = Get(*(const uint32*)data);
	data = (const uint32*)data + 1;
	dataSize -= sizeof(uint32);

	if (entrySize == 0 || dataSize == 0 || dataSize - 1 < entrySize) {
		WARNING("Team note: too short or invalid entry size (%" B_PRIu32 ")\n",
			entrySize);
		return B_BAD_DATA;
	}

	NoteTeam note = {};
	_ReadEntry(data, dataSize, note, entrySize);

	// check, if args are null-terminated
	const char* args = (const char*)data;
	size_t argsSize = dataSize;
	if (args[argsSize - 1] != '\0') {
		WARNING("Team note args not terminated\n");
		return B_BAD_DATA;
	}

	int32 id = Get(note.nt_id);
	int32 uid = Get(note.nt_uid);
	int32 gid = Get(note.nt_gid);

	BString copiedArgs(args);
	if (args[0] != '\0' && copiedArgs.Length() == 0)
		return B_NO_MEMORY;

	fTeamInfo.Init(id, uid, gid, copiedArgs);
	return B_OK;
}


/**
 * @brief Decodes the @c NT_AREAS note into @c fAreaInfos.
 *
 * Reads the area count and per-entry size, then walks the entries
 * followed by a NUL-separated string table of area names. Each entry
 * is paired with its backing PT_LOAD segment via @c _FindAreaSegment().
 *
 * @param data     Pointer to the note data.
 * @param dataSize Length of the note data in bytes.
 * @return        @c B_OK on success, @c B_BAD_DATA on validation
 *                 failure, @c B_NO_MEMORY on allocation failure.
 */
template<typename ElfClass>
status_t
CoreFile::_ReadAreasNote(const void* data, uint32 dataSize)
{
	if (dataSize < 2 * sizeof(uint32)) {
		WARNING("Areas note too short\n");
		return B_BAD_DATA;
	}
	uint32 areaCount = _ReadValue<uint32>(data, dataSize);
	uint32 entrySize = _ReadValue<uint32>(data, dataSize);

	typedef typename ElfClass::NoteAreaEntry Entry;

	if (areaCount == 0)
		return B_OK;

	// check entry size and area count
	if (entrySize == 0 || dataSize == 0 || areaCount > dataSize
			|| dataSize - 1 < entrySize || areaCount * entrySize >= dataSize) {
		WARNING("Areas note: too short or invalid entry size (%" B_PRIu32 ")\n",
			entrySize);
		return B_BAD_DATA;
	}

	// check, if strings are null-terminated
	const char* strings = (const char*)data + areaCount * entrySize;
	size_t stringsSize = dataSize - areaCount * entrySize;
	if (stringsSize == 0 || strings[stringsSize - 1] != '\0') {
		WARNING("Areas note strings not terminated\n");
		return B_BAD_DATA;
	}

	for (uint64 i = 0; i < areaCount; i++) {
		// get entry values
		Entry entry = {};
		_ReadEntry(data, dataSize, entry, entrySize);

		int32 id = Get(entry.na_id);
		uint64 baseAddress = Get(entry.na_base);
		uint64 size = Get(entry.na_size);
		uint64 ramSize = Get(entry.na_ram_size);
		uint32 lock = Get(entry.na_lock);
		uint32 protection = Get(entry.na_protection);

		// get name
		if (stringsSize == 0) {
			WARNING("Area %" B_PRIu64 " (ID %#" B_PRIx32 " @ %#" B_PRIx64
				") has no name\n", i, id, baseAddress);
			continue;
		}
		const char* name = strings;
		size_t nameSize = strlen(name) + 1;
		strings += nameSize;
		stringsSize -= nameSize;

		BString copiedName(name);
		if (name[0] != '\0' && copiedName.Length() == 0)
			return B_NO_MEMORY;

		// create and add area
		ElfSegment* segment = _FindAreaSegment(baseAddress);
		if (segment == NULL) {
			WARNING("No matching segment found for area %" B_PRIu64 " (ID %#"
				B_PRIx32 " @ %#" B_PRIx64 ", name: '%s')", i, id, baseAddress,
				name);
			continue;
		}

		CoreFileAreaInfo* area = new(std::nothrow) CoreFileAreaInfo(segment, id,
			baseAddress, size, ramSize, lock, protection, copiedName);
		if (area == NULL || !fAreaInfos.AddItem(area)) {
			delete area;
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


/**
 * @brief Decodes the @c NT_IMAGES note into @c fImageInfos.
 *
 * Each image entry is paired with its text and data CoreFileAreaInfo
 * via @c _FindArea() so subsequent symbol lookups can locate the bytes
 * inside the dump.
 *
 * @param data     Pointer to the note data.
 * @param dataSize Length of the note data in bytes.
 * @return        @c B_OK on success, @c B_BAD_DATA on validation
 *                 failure, @c B_NO_MEMORY on allocation failure.
 */
template<typename ElfClass>
status_t
CoreFile::_ReadImagesNote(const void* data, uint32 dataSize)
{
	if (dataSize < 2 * sizeof(uint32)) {
		WARNING("Images note too short\n");
		return B_BAD_DATA;
	}
	uint32 imageCount = _ReadValue<uint32>(data, dataSize);
	uint32 entrySize = _ReadValue<uint32>(data, dataSize);

	typedef typename ElfClass::NoteImageEntry Entry;

	if (imageCount == 0)
		return B_OK;

	// check entry size and image count
	if (entrySize == 0 || dataSize == 0 || imageCount > dataSize
			|| dataSize - 1 < entrySize || imageCount * entrySize >= dataSize) {
		WARNING("Images note: too short or invalid entry size (%" B_PRIu32
			")\n", entrySize);
		return B_BAD_DATA;
	}

	// check, if strings are null-terminated
	const char* strings = (const char*)data + imageCount * entrySize;
	size_t stringsSize = dataSize - imageCount * entrySize;
	if (stringsSize == 0 || strings[stringsSize - 1] != '\0') {
		WARNING("Images note strings not terminated\n");
		return B_BAD_DATA;
	}

	for (uint64 i = 0; i < imageCount; i++) {
		// get entry values
		Entry entry = {};
		_ReadEntry(data, dataSize, entry, entrySize);

		int32 id = Get(entry.ni_id);
		int32 type = Get(entry.ni_type);
		uint64 initRoutine = Get(entry.ni_init_routine);
		uint64 termRoutine = Get(entry.ni_term_routine);
		uint64 textBase = Get(entry.ni_text_base);
		uint64 textSize = Get(entry.ni_text_size);
		int64 textDelta = Get(entry.ni_text_delta);
		uint64 dataBase = Get(entry.ni_data_base);
		uint64 dataSize = Get(entry.ni_data_size);
		int32 deviceId = Get(entry.ni_device);
		int64 nodeId = Get(entry.ni_node);
		uint64 symbolTable = Get(entry.ni_symbol_table);
		uint64 symbolHash = Get(entry.ni_symbol_hash);
		uint64 stringTable = Get(entry.ni_string_table);

		// get name
		if (stringsSize == 0) {
			WARNING("Image %" B_PRIu64 " (ID %#" B_PRIx32 ") has no name\n",
				i, id);
			continue;
		}
		const char* name = strings;
		size_t nameSize = strlen(name) + 1;
		strings += nameSize;
		stringsSize -= nameSize;

		BString copiedName(name);
		if (name[0] != '\0' && copiedName.Length() == 0)
			return B_NO_MEMORY;

		// create and add image
		CoreFileAreaInfo* textArea = _FindArea(textBase);
		CoreFileAreaInfo* dataArea = _FindArea(dataBase);
		CoreFileImageInfo* image = new(std::nothrow) CoreFileImageInfo(id, type,
			initRoutine, termRoutine, textBase, textSize, textDelta, dataBase,
			dataSize, deviceId, nodeId, symbolTable, symbolHash, stringTable,
			textArea, dataArea, copiedName);
		if (image == NULL || !fImageInfos.AddItem(image)) {
			delete image;
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


/**
 * @brief Decodes one @c NT_SYMBOLS note and attaches it to the matching image.
 *
 * The note contains the image id followed by a symbol-table block and a
 * trailing string-table block. The decoded data is wrapped in a
 * CoreFileSymbolsInfo and installed via @c CoreFileImageInfo::SetSymbolsInfo().
 *
 * @param data     Pointer to the note data.
 * @param dataSize Length of the note data in bytes.
 * @return        @c B_OK on success; @c B_BAD_DATA on validation
 *                 failure; @c B_NO_MEMORY on allocation failure.
 */
template<typename ElfClass>
status_t
CoreFile::_ReadSymbolsNote(const void* data, uint32 dataSize)
{
	if (dataSize < 3 * sizeof(uint32)) {
		WARNING("Symbols note too short\n");
		return B_BAD_DATA;
	}
	int32 imageId = _ReadValue<int32>(data, dataSize);
	uint32 symbolCount = _ReadValue<uint32>(data, dataSize);
	uint32 entrySize = _ReadValue<uint32>(data, dataSize);

	typedef typename ElfClass::Sym Sym;

	if (symbolCount == 0)
		return B_OK;

	// get the corresponding image
	CoreFileImageInfo* imageInfo = _ImageInfoForId(imageId);
	if (imageInfo == NULL) {
		WARNING("Symbols note: image (ID %" B_PRId32 ") not found\n",
			entrySize);
		return B_BAD_DATA;
	}

	// check entry size and symbol count
	if (entrySize < sizeof(Sym) || symbolCount > dataSize
			|| dataSize - 1 < entrySize
			|| symbolCount * entrySize >= dataSize - 1) {
		WARNING("Symbols note: too short or invalid entry size (%" B_PRIu32
			")\n", entrySize);
		return B_BAD_DATA;
	}

	uint32 symbolTableSize = symbolCount * entrySize;
	uint32 stringTableSize = dataSize - symbolTableSize;

	// check, if the string table is null-terminated
	const char* stringTable = (const char*)data + symbolTableSize;
	if (stringTableSize == 0 || stringTable[stringTableSize - 1] != '\0') {
		WARNING("Symbols note string table not terminated\n");
		return B_BAD_DATA;
	}

	CoreFileSymbolsInfo* symbolsInfo = new(std::nothrow) CoreFileSymbolsInfo;
	if (symbolsInfo == NULL
			|| !symbolsInfo->Init(data, symbolCount, entrySize, stringTable,
					stringTableSize)) {
		delete symbolsInfo;
		return B_NO_MEMORY;
	}

	imageInfo->SetSymbolsInfo(symbolsInfo);

	return B_OK;
}


/**
 * @brief Decodes the @c NT_THREADS note into @c fThreadInfos.
 *
 * Reads thread count, per-entry size, and per-CPU-state size, then
 * iterates entries pairing each one with the matching CPU-state blob
 * and trailing thread name from the string table.
 *
 * @param data     Pointer to the note data.
 * @param dataSize Length of the note data in bytes.
 * @return        @c B_OK on success; @c B_BAD_DATA on validation
 *                 failure; @c B_NO_MEMORY on allocation failure.
 */
template<typename ElfClass>
status_t
CoreFile::_ReadThreadsNote(const void* data, uint32 dataSize)
{
	if (dataSize < 3 * sizeof(uint32)) {
		WARNING("Threads note too short\n");
		return B_BAD_DATA;
	}
	uint32 threadCount = _ReadValue<uint32>(data, dataSize);
	uint32 entrySize = _ReadValue<uint32>(data, dataSize);
	uint32 cpuStateSize = _ReadValue<uint32>(data, dataSize);

	if (cpuStateSize > 1024 * 1024) {
		WARNING("Threads note: unreasonable CPU state size: %" B_PRIu32 "\n",
			cpuStateSize);
		return B_BAD_DATA;
	}

	typedef typename ElfClass::NoteThreadEntry Entry;

	if (threadCount == 0)
		return B_OK;

	size_t totalEntrySize = entrySize + cpuStateSize;

	// check entry size and thread count
	if (entrySize == 0 || dataSize == 0 || threadCount > dataSize
			|| entrySize > dataSize || cpuStateSize > dataSize
			|| dataSize - 1 < totalEntrySize
			|| threadCount * totalEntrySize >= dataSize) {
		WARNING("Threads note: too short or invalid entry size (%" B_PRIu32
			")\n", entrySize);
		return B_BAD_DATA;
	}

	// check, if strings are null-terminated
	const char* strings = (const char*)data + threadCount * totalEntrySize;
	size_t stringsSize = dataSize - threadCount * totalEntrySize;
	if (stringsSize == 0 || strings[stringsSize - 1] != '\0') {
		WARNING("Threads note strings not terminated\n");
		return B_BAD_DATA;
	}

	for (uint64 i = 0; i < threadCount; i++) {
		// get entry values
		Entry entry = {};
		_ReadEntry(data, dataSize, entry, entrySize);

		int32 id = Get(entry.nth_id);
		int32 state = Get(entry.nth_state);
		int32 priority = Get(entry.nth_priority);
		uint64 stackBase = Get(entry.nth_stack_base);
		uint64 stackEnd = Get(entry.nth_stack_end);

		// get name
		if (stringsSize == 0) {
			WARNING("Thread %" B_PRIu64 " (ID %#" B_PRIx32 ") has no name\n",
				i, id);
			continue;
		}
		const char* name = strings;
		size_t nameSize = strlen(name) + 1;
		strings += nameSize;
		stringsSize -= nameSize;

		BString copiedName(name);
		if (name[0] != '\0' && copiedName.Length() == 0)
			return B_NO_MEMORY;

		// create and add thread
		CoreFileThreadInfo* thread = new(std::nothrow) CoreFileThreadInfo(id,
			state, priority, stackBase, stackEnd, copiedName);
		if (thread == NULL || !fThreadInfos.AddItem(thread)) {
			delete thread;
			return B_NO_MEMORY;
		}

		// get CPU state
		if (!thread->SetCpuState(data, cpuStateSize))
			return B_NO_MEMORY;
		_Advance(data, dataSize, cpuStateSize);
	}

	return B_OK;
}


/**
 * @brief Looks up the CoreFileAreaInfo whose virtual range contains @a address.
 *
 * @param address Target-space address.
 * @return       The matching area, or NULL if @a address is unmapped.
 */
CoreFileAreaInfo*
CoreFile::_FindArea(uint64 address) const
{
	int32 count = fAreaInfos.CountItems();
	for (int32 i = 0; i < count; i++) {
		CoreFileAreaInfo* area = fAreaInfos.ItemAt(i);
		if (address >= area->BaseAddress()
				&& address < area->EndAddress()) {
			return area;
		}
	}

	return NULL;
}


/**
 * @brief Looks up the PT_LOAD segment whose load address equals @a address.
 *
 * @param address Target-space load address.
 * @return       The matching segment, or NULL if no segment matches.
 */
ElfSegment*
CoreFile::_FindAreaSegment(uint64 address) const
{
	int32 count = fElfFile.CountSegments();
	for (int32 i = 0; i < count; i++) {
		ElfSegment* segment = fElfFile.SegmentAt(i);
		if (segment->Type() == PT_LOAD && segment->LoadAddress() == address)
			return segment;
	}

	return NULL;
}


/**
 * @brief Looks up the CoreFileImageInfo for an image id via linear search.
 *
 * @param id Image identifier.
 * @return  The matching image info, or NULL if absent.
 */
CoreFileImageInfo*
CoreFile::_ImageInfoForId(int32 id) const
{
	int32 count = fImageInfos.CountItems();
	for (int32 i = 0; i < count; i++) {
		CoreFileImageInfo* info = fImageInfos.ItemAt(i);
		if (info->Id() == id)
			return info;
	}

	return NULL;
}


/**
 * @brief Reads one endianness-corrected primitive @c Type from @a data.
 *
 * Advances @a data and decrements @a dataSize by @c sizeof(Type).
 *
 * @param data     In/out cursor into the note data.
 * @param dataSize In/out remaining byte count.
 * @return        Host-order value of the read primitive.
 */
template<typename Type>
Type
CoreFile::_ReadValue(const void*& data, uint32& dataSize)
{
	Type value = Get(*(const Type*)data);
	_Advance(data, dataSize, sizeof(Type));
	return value;
}


/**
 * @brief Copies one variable-size note entry into @a entry.
 *
 * Copies the smaller of the on-disk entry size and the in-memory struct
 * size to tolerate the kernel writing more (or fewer) fields than this
 * build understands. Always advances by @a entrySize.
 *
 * @param data      In/out cursor into the note data.
 * @param dataSize  In/out remaining byte count.
 * @param entry     Destination struct.
 * @param entrySize On-disk byte size of one entry.
 */
template<typename Entry>
void
CoreFile::_ReadEntry(const void*& data, uint32& dataSize, Entry& entry,
	size_t entrySize)
{
	memcpy(&entry, data, std::min(sizeof(entry), entrySize));
	_Advance(data, dataSize, entrySize);
}


/**
 * @brief Advances the (data, dataSize) cursor by @a by bytes.
 *
 * @param data     In/out cursor.
 * @param dataSize In/out remaining byte count.
 * @param by       Bytes to consume.
 */
void
CoreFile::_Advance(const void*& data, uint32& dataSize, size_t by)
{
	data = (const uint8*)data + by;
	dataSize -= by;
}
