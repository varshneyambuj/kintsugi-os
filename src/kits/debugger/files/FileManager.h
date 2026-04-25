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
 * MIT License. Copyright 2009-2013, Haiku.
 * Original authors: Ingo Weinhold, Rene Gollent.
 */

/** @file FileManager.h
    @brief Maps target/source paths to local on-disk locations and caches loaded source files. */

#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include <map>

#include <Locker.h>
#include <Message.h>
#include <String.h>

#include <util/DoublyLinkedList.h>
#include <util/OpenHashTable.h>


class LocatableEntry;
class LocatableFile;
class SourceFile;
class TeamFileManagerSettings;


/** @brief Owns the target and source path-resolution domains and a cache of loaded source files. */
class FileManager {
public:
								FileManager();
								~FileManager();

			status_t			Init(bool targetIsLocal);

			/** @brief Acquire the FileManager's internal lock. */
			bool				Lock()		{ return fLock.Lock(); }
			/** @brief Release the FileManager's internal lock. */
			void				Unlock()	{ fLock.Unlock(); }

			LocatableFile*		GetTargetFile(const BString& directory,
									const BString& relativePath);
										// returns a reference
			LocatableFile*		GetTargetFile(const BString& path);
										// returns a reference
			void				TargetEntryLocated(const BString& path,
									const BString& locatedPath);

			LocatableFile*		GetSourceFile(const BString& directory,
									const BString& relativePath);
										// returns a reference
			LocatableFile*		GetSourceFile(const BString& path);
										// returns a reference
			status_t			SourceEntryLocated(const BString& path,
									const BString& locatedPath);

			status_t			LoadSourceFile(LocatableFile* file,
									SourceFile*& _sourceFile);
										// returns a reference

			status_t			LoadLocationMappings(TeamFileManagerSettings*
									settings);
			status_t			SaveLocationMappings(TeamFileManagerSettings*
									settings);

private:
			struct EntryPath;
			struct EntryHashDefinition;
			class Domain;
			struct SourceFileEntry;
			struct SourceFileHashDefinition;

			typedef BOpenHashTable<EntryHashDefinition> LocatableEntryTable;
			typedef BOpenHashTable<SourceFileHashDefinition> SourceFileTable;
			typedef std::map<BString, BString> LocatedFileMap;

			friend struct SourceFileEntry;
				// for gcc 2

private:
			SourceFileEntry*	_LookupSourceFile(const BString& path);
			void				_SourceFileUnused(SourceFileEntry* entry);
			bool				_LocateFileIfMapped(const BString& sourcePath,
									LocatableFile* file);

private:
			BLocker				fLock;
			Domain*				fTargetDomain;
			Domain*				fSourceDomain;
			SourceFileTable*	fSourceFiles;
			LocatedFileMap		fSourceLocationMappings;
};



#endif	// FILE_MANAGER_H
