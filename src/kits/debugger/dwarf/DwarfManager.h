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
 * MIT License. Copyright 2009, Ingo Weinhold; Copyright 2014, Rene Gollent.
 */

/** @file DwarfManager.h
    @brief Locking owner of multiple DwarfFile loaders for a debug session. */

#ifndef DWARF_MANAGER_H
#define DWARF_MANAGER_H

#include <Locker.h>

#include <util/DoublyLinkedList.h>


class DwarfFile;
struct DwarfFileLoadingState;


/**
 * @brief Coordinates loading and post-load fixups of multiple DwarfFiles.
 *
 * The manager remembers the target ABI's address size and endianness so
 * each file is parsed consistently, and serialises load operations under
 * an internal BLocker to allow concurrent access from multiple threads.
 */
class DwarfManager {
public:
								DwarfManager(uint8 addressSize, bool isBigEndian);
								~DwarfManager();

			status_t			Init();

			bool				Lock()		{ return fLock.Lock(); }
			void				Unlock()	{ fLock.Unlock(); }

			status_t			LoadFile(const char* fileName,
									DwarfFileLoadingState& _loadingState);
									// _loadingState receives a reference
									// to the corresponding DwarfFile.

			status_t			FinishLoading();

private:
			typedef DoublyLinkedList<DwarfFile> FileList;

private:
			uint8				fAddressSize;
			bool				fIsBigEndian;
			BLocker				fLock;
			FileList			fFiles;
};



#endif	// DWARF_MANAGER_H
