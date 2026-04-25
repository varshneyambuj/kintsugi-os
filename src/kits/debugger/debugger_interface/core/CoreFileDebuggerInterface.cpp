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
 * @file CoreFileDebuggerInterface.cpp
 * @brief DebuggerInterface implementation backed by an ELF core dump.
 *
 * Exposes a frozen team to the debugger UI: thread infos, image infos, CPU
 * state and memory reads come from the dump; control operations (continue,
 * step, set breakpoint, write memory, ...) all return B_UNSUPPORTED because
 * the snapshot is read-only.
 */


#include "CoreFileDebuggerInterface.h"

#include <algorithm>
#include <new>

#include <errno.h>

#include <AutoDeleter.h>

#include "ArchitectureX86.h"
#include "ArchitectureX8664.h"
#include "CoreFile.h"
#include "ElfSymbolLookup.h"
#include "ImageInfo.h"
#include "TeamInfo.h"
#include "ThreadInfo.h"
#include "Tracing.h"


/**
 * @brief Constructs the interface around an already-parsed core file.
 *
 * @param coreFile  CoreFile instance whose ownership transfers to this object;
 *                  it is deleted in the destructor.
 */
CoreFileDebuggerInterface::CoreFileDebuggerInterface(CoreFile* coreFile)
	:
	fCoreFile(coreFile),
	fArchitecture(NULL)
{
}


/**
 * @brief Releases the architecture reference and deletes the underlying core file.
 */
CoreFileDebuggerInterface::~CoreFileDebuggerInterface()
{
	if (fArchitecture != NULL)
		fArchitecture->ReleaseReference();

	delete fCoreFile;
}


/**
 * @brief Selects an Architecture implementation based on the core file's ELF machine.
 *
 * @return B_OK on success, B_UNSUPPORTED if the machine type is not handled,
 *         B_NO_MEMORY on allocation failure, or any error from
 *         Architecture::Init().
 */
status_t
CoreFileDebuggerInterface::Init()
{
	// create the Architecture object
	uint16 machine = fCoreFile->GetElfFile().Machine();
	switch (machine) {
		case EM_386:
			fArchitecture = new(std::nothrow) ArchitectureX86(this);
			break;
		case EM_X86_64:
			fArchitecture = new(std::nothrow) ArchitectureX8664(this);
			break;
		default:
			WARNING("Unsupported core file machine (%u)\n", machine);
			return B_UNSUPPORTED;
	}

	if (fArchitecture == NULL)
		return B_NO_MEMORY;

	return fArchitecture->Init();
}


/**
 * @brief No-op close; nothing to release on a read-only core file.
 *
 * @param killTeam  Ignored; there is no live team to kill.
 */
void
CoreFileDebuggerInterface::Close(bool killTeam)
{
}


/**
 * @brief A core file is always considered "connected" once parsed.
 *
 * @return Always true.
 */
bool
CoreFileDebuggerInterface::Connected() const
{
	return true;
}


/**
 * @brief Identifies this interface as a post-mortem (snapshot) target.
 *
 * @return Always true.
 */
bool
CoreFileDebuggerInterface::IsPostMortem() const
{
	return true;
}


/**
 * @brief Returns the team id captured in the core file.
 *
 * @return Recorded team id.
 */
team_id
CoreFileDebuggerInterface::TeamID() const
{
	return fCoreFile->GetTeamInfo().Id();
}


/**
 * @brief Returns the Architecture object selected during Init().
 *
 * @return Borrowed pointer to the Architecture; valid for the interface lifetime.
 */
Architecture*
CoreFileDebuggerInterface::GetArchitecture() const
{
	return fArchitecture;
}


/**
 * @brief Core files don't produce a live event stream.
 *
 * @param _event  Unused output parameter.
 * @return Always @c B_UNSUPPORTED.
 */
status_t
CoreFileDebuggerInterface::GetNextDebugEvent(DebugEvent*& _event)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Cannot change team debugging flags on a snapshot.
 *
 * @param flags  Ignored.
 * @return Always @c B_UNSUPPORTED.
 */
status_t
CoreFileDebuggerInterface::SetTeamDebuggingFlags(uint32 flags)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Threads in a core file cannot be resumed.
 *
 * @param thread  Ignored.
 * @return Always @c B_UNSUPPORTED.
 */
status_t
CoreFileDebuggerInterface::ContinueThread(thread_id thread)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Threads in a core file are already stopped.
 *
 * @param thread  Ignored.
 * @return Always @c B_UNSUPPORTED.
 */
status_t
CoreFileDebuggerInterface::StopThread(thread_id thread)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Cannot single-step a snapshot.
 *
 * @param thread  Ignored.
 * @return Always @c B_UNSUPPORTED.
 */
status_t
CoreFileDebuggerInterface::SingleStepThread(thread_id thread)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Cannot install breakpoints on a snapshot.
 *
 * @param address  Ignored.
 * @return Always @c B_UNSUPPORTED.
 */
status_t
CoreFileDebuggerInterface::InstallBreakpoint(target_addr_t address)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Cannot uninstall breakpoints on a snapshot.
 *
 * @param address  Ignored.
 * @return Always @c B_UNSUPPORTED.
 */
status_t
CoreFileDebuggerInterface::UninstallBreakpoint(target_addr_t address)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Cannot install watchpoints on a snapshot.
 *
 * @param address  Ignored.
 * @param type     Ignored.
 * @param length   Ignored.
 * @return Always @c B_UNSUPPORTED.
 */
status_t
CoreFileDebuggerInterface::InstallWatchpoint(target_addr_t address, uint32 type,
	int32 length)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Cannot uninstall watchpoints on a snapshot.
 *
 * @param address  Ignored.
 * @return Always @c B_UNSUPPORTED.
 */
status_t
CoreFileDebuggerInterface::UninstallWatchpoint(target_addr_t address)
{
	return B_UNSUPPORTED;
}


/**
 * @brief System-level info is not retained in a core file.
 *
 * @param info  Output parameter; left untouched.
 * @return Always @c B_UNSUPPORTED.
 */
status_t
CoreFileDebuggerInterface::GetSystemInfo(SystemInfo& info)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Fills @a info with the team id and command-line arguments from the dump.
 *
 * @param info  Output parameter populated from the core file's team info.
 * @return Always @c B_OK.
 */
status_t
CoreFileDebuggerInterface::GetTeamInfo(TeamInfo& info)
{
	const CoreFileTeamInfo& coreInfo = fCoreFile->GetTeamInfo();
	info.SetTo(coreInfo.Id(), coreInfo.Arguments());
	return B_OK;
}


/**
 * @brief Builds a list of ThreadInfo entries from the core file's thread records.
 *
 * @param infos  Output list; ownership of the appended ThreadInfo objects
 *               transfers to the list.
 * @return B_OK on success, B_NO_MEMORY if any allocation fails.
 */
status_t
CoreFileDebuggerInterface::GetThreadInfos(BObjectList<ThreadInfo, true>& infos)
{
	int32 count = fCoreFile->CountThreadInfos();
	for (int32 i = 0; i < count; i++) {
		const CoreFileThreadInfo* coreInfo = fCoreFile->ThreadInfoAt(i);
		ThreadInfo* info = new(std::nothrow) ThreadInfo;
		if (info == NULL || !infos.AddItem(info)) {
			delete info;
			return B_NO_MEMORY;
		}

		_GetThreadInfo(*coreInfo, *info);
	}

	return B_OK;
}


/**
 * @brief Builds a list of ImageInfo entries from the core file's image records.
 *
 * @param infos  Output list; ownership of the appended ImageInfo objects
 *               transfers to the list.
 * @return B_OK on success, B_NO_MEMORY if any allocation fails.
 */
status_t
CoreFileDebuggerInterface::GetImageInfos(BObjectList<ImageInfo, true>& infos)
{
	int32 count = fCoreFile->CountImageInfos();
	for (int32 i = 0; i < count; i++) {
		const CoreFileImageInfo* coreInfo = fCoreFile->ImageInfoAt(i);
		ImageInfo* info = new(std::nothrow) ImageInfo;
		if (info == NULL || !infos.AddItem(info)) {
			delete info;
			return B_NO_MEMORY;
		}

		info->SetTo(TeamID(), coreInfo->Id(), coreInfo->Name(),
			(image_type)coreInfo->Type(), coreInfo->TextBase(),
			coreInfo->TextSize(), coreInfo->DataBase(), coreInfo->DataSize());
	}

	return B_OK;
}


/**
 * @brief Areas are not currently captured in core files.
 *
 * @param infos  Output list; left untouched.
 * @return Always @c B_UNSUPPORTED.
 */
status_t
CoreFileDebuggerInterface::GetAreaInfos(BObjectList<AreaInfo, true>& infos)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Semaphore state is not currently captured in core files.
 *
 * @param infos  Output list; left untouched.
 * @return Always @c B_UNSUPPORTED.
 */
status_t
CoreFileDebuggerInterface::GetSemaphoreInfos(BObjectList<SemaphoreInfo, true>& infos)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Resolves the symbol table for an image, drawing on whichever source
 *        is available (in-core symbols, on-disk ELF, or core-derived lookup).
 *
 * Strategy: prefer the in-core symbol table when present; if not, fall back
 * to symbols from the on-disk ELF file; if that fails, attempt to build a
 * lookup directly from data captured in the dump.
 *
 * @param team   Team id; used only for error context.
 * @param image  Image identifier inside the core file.
 * @param infos  Output list; ownership of appended SymbolInfo objects transfers.
 * @return B_OK on success, B_BAD_IMAGE_ID if @a image is not in the dump,
 *         or any error from the underlying ELF/symbol-lookup path.
 */
status_t
CoreFileDebuggerInterface::GetSymbolInfos(team_id team, image_id image,
	BObjectList<SymbolInfo, true>& infos)
{
	// get the image info
	const CoreFileImageInfo* imageInfo = fCoreFile->ImageInfoForId(image);
	if (imageInfo == NULL)
		return B_BAD_IMAGE_ID;

	if (const CoreFileSymbolsInfo* symbolsInfo = imageInfo->SymbolsInfo()) {
		return GetElfSymbols(symbolsInfo->SymbolTable(),
			symbolsInfo->SymbolCount(), symbolsInfo->SymbolTableEntrySize(),
			symbolsInfo->StringTable(), symbolsInfo->StringTableSize(),
			fCoreFile->GetElfFile().Is64Bit(),
			fCoreFile->GetElfFile().IsByteOrderSwapped(),
			imageInfo->TextDelta(), infos);
	}

	// get the symbols from the ELF file, if possible
	status_t error = GetElfSymbols(imageInfo->Name(), imageInfo->TextDelta(),
		infos);
	if (error == B_OK)
		return error;

	// get the symbols from the core file, if possible
	ElfSymbolLookup* symbolLookup;
	error = fCoreFile->CreateSymbolLookup(imageInfo, symbolLookup);
	if (error != B_OK) {
		WARNING("Failed to create symbol lookup for image (%" B_PRId32
			"): %s\n", image, strerror(error));
		return error;
	}

	ObjectDeleter<ElfSymbolLookup> symbolLookupDeleter(symbolLookup);

	return GetElfSymbols(symbolLookup, infos);
}


/**
 * @brief Single-symbol lookup is not yet implemented for core files.
 *
 * @param team        Ignored.
 * @param image       Ignored.
 * @param name        Ignored.
 * @param symbolType  Ignored.
 * @param info        Output parameter; left untouched.
 * @return Always @c B_UNSUPPORTED.
 * @todo Implement targeted symbol lookup against the ELF symbol cache.
 */
status_t
CoreFileDebuggerInterface::GetSymbolInfo(team_id team, image_id image,
	const char* name, int32 symbolType, SymbolInfo& info)
{
	// TODO:...
	return B_UNSUPPORTED;
}


/**
 * @brief Returns a single ThreadInfo by thread id from the core file.
 *
 * @param thread  Thread id to look up.
 * @param info    Output parameter populated on success.
 * @return B_OK on success, B_BAD_THREAD_ID if the dump has no such thread.
 */
status_t
CoreFileDebuggerInterface::GetThreadInfo(thread_id thread, ThreadInfo& info)
{
	const CoreFileThreadInfo* coreInfo = fCoreFile->ThreadInfoForId(thread);
	if (coreInfo == NULL)
		return B_BAD_THREAD_ID;

	_GetThreadInfo(*coreInfo, info);
	return B_OK;
}


/**
 * @brief Reconstructs a CpuState object from the per-thread state in the dump.
 *
 * @param thread  Thread id whose CPU state is requested.
 * @param _state  On success, set to a freshly-allocated CpuState owned by the caller.
 * @return B_OK on success, B_BAD_THREAD_ID if @a thread is not in the dump,
 *         or any error from Architecture::CreateCpuState().
 */
status_t
CoreFileDebuggerInterface::GetCpuState(thread_id thread, CpuState*& _state)
{
	const CoreFileThreadInfo* coreInfo = fCoreFile->ThreadInfoForId(thread);
	if (coreInfo == NULL)
		return B_BAD_THREAD_ID;

	return fArchitecture->CreateCpuState(coreInfo->GetCpuState(),
		coreInfo->CpuStateSize(), _state);
}


/**
 * @brief CPU state cannot be modified on a snapshot.
 *
 * @param thread  Ignored.
 * @param state   Ignored.
 * @return Always @c B_UNSUPPORTED.
 */
status_t
CoreFileDebuggerInterface::SetCpuState(thread_id thread, const CpuState* state)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Reports CPU feature flags as discovered by the Architecture object.
 *
 * @param flags  Output parameter populated by the Architecture.
 * @return Whatever Architecture::GetCpuFeatures() returns.
 */
status_t
CoreFileDebuggerInterface::GetCpuFeatures(uint32& flags)
{
	return fArchitecture->GetCpuFeatures(flags);
}


/**
 * @brief Writing a new core file from a core file is not supported.
 *
 * @param path  Ignored.
 * @return Always @c B_NOT_SUPPORTED.
 */
status_t
CoreFileDebuggerInterface::WriteCoreFile(const char* path)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Returns the protection and locking flags for the area covering @a address.
 *
 * @note Write protection is masked off because this interface cannot honor
 *       writes anyway.
 *
 * @param address     Target-side address to look up.
 * @param protection  Output parameter; receives the masked protection flags.
 * @param locking     Output parameter; receives the area's locking flags.
 * @return B_OK on success, B_BAD_ADDRESS if no captured area covers @a address.
 */
status_t
CoreFileDebuggerInterface::GetMemoryProperties(target_addr_t address,
	uint32& protection, uint32& locking)
{
	const CoreFileAreaInfo* info = fCoreFile->AreaInfoForAddress(address);
	if (info == NULL)
		return B_BAD_ADDRESS;

	protection = info->Protection() & ~(uint32)B_WRITE_AREA;
		// Filter out write protection, since we don't support writing memory.
	locking = info->Locking();
	return B_OK;
}


/**
 * @brief Reads bytes from the captured memory regions, splicing across segments
 *        as needed.
 *
 * Walks the area list and pread()s the appropriate ELF segment's file offset.
 * Returns whatever it could copy if a partial read crosses an unmapped region.
 *
 * @param address  Target-side starting address.
 * @param _buffer  Destination buffer; must hold @a size bytes.
 * @param size     Number of bytes to read.
 * @return Number of bytes actually read, B_BAD_ADDRESS if no segment covers
 *         the very first byte, or a negative errno-derived status on I/O error.
 */
ssize_t
CoreFileDebuggerInterface::ReadMemory(target_addr_t address, void* _buffer,
	size_t size)
{
	if (size == 0)
		return B_OK;

	ssize_t totalRead = 0;
	uint8* buffer = (uint8*)_buffer;

	while (size > 0) {
		const CoreFileAreaInfo* info = fCoreFile->AreaInfoForAddress(address);
		if (info == NULL)
			return totalRead > 0 ? totalRead : B_BAD_ADDRESS;

		ElfSegment* segment = info->Segment();
		uint64 offset = address - segment->LoadAddress();
		if (offset >= segment->FileSize())
			return totalRead > 0 ? totalRead : B_BAD_ADDRESS;

		size_t toRead = (size_t)std::min((uint64)size,
			segment->FileSize() - offset);
		ssize_t bytesRead = pread(fCoreFile->GetElfFile().FD(), buffer, toRead,
			segment->FileOffset() + offset);
		if (bytesRead <= 0) {
			status_t error = bytesRead == 0 ? B_IO_ERROR : errno;
			return totalRead > 0 ? totalRead : error;
		}

		buffer += bytesRead;
		size -= bytesRead;
		totalRead += bytesRead;
	}

	return totalRead;
}


/**
 * @brief Memory cannot be written through a core file interface.
 *
 * @param address  Ignored.
 * @param buffer   Ignored.
 * @param size     Ignored.
 * @return Always @c B_UNSUPPORTED.
 */
ssize_t
CoreFileDebuggerInterface::WriteMemory(target_addr_t address, void* buffer,
	size_t size)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Copies the relevant fields of a CoreFileThreadInfo into a ThreadInfo.
 *
 * @param coreInfo  Source thread record from the core file.
 * @param info      Destination ThreadInfo populated with team id, thread id, and name.
 */
void
CoreFileDebuggerInterface::_GetThreadInfo(const CoreFileThreadInfo& coreInfo,
	ThreadInfo& info)
{
	info.SetTo(TeamID(), coreInfo.Id(), coreInfo.Name());
}
