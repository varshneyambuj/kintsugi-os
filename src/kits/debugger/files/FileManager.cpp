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
 *   Copyright 2011-2017, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file FileManager.cpp
 * @brief Maps target-side and source-side paths to local on-disk locations.
 *
 * Maintains two LocatableEntry domains (target and source) plus a cache of
 * loaded SourceFile objects. When debugging a remote or relocated build
 * the user can supply explicit mappings; the FileManager attempts to
 * propagate those mappings down hierarchies of LocatableDirectory entries
 * so subsequently looked-up files are auto-located.
 *
 * @see LocatableEntry, LocatableDirectory, LocatableFile, SourceFile
 */

#include "FileManager.h"

#include <new>

#include <AutoDeleter.h>
#include <AutoLocker.h>

#include "LocatableDirectory.h"
#include "LocatableFile.h"
#include "SourceFile.h"
#include "TeamFileManagerSettings.h"


// #pragma mark - EntryPath


/**
 * @brief Lightweight (directory, name) pair used as a hash-table key.
 *
 * Stores raw pointers so it can wrap either a (BString, BString) pair or an
 * existing LocatableEntry without copying. Comparison is value-based.
 */
struct FileManager::EntryPath {
	const char*	directory;
	const char*	name;

	EntryPath(const char* directory, const char* name)
		:
		directory(directory),
		name(name)
	{
	}

	EntryPath(const BString& directory, const BString& name)
		:
		directory(directory.Length() > 0 ? directory.String() : NULL),
		name(name.String())
	{
	}

	EntryPath(const LocatableEntry* entry)
		:
		directory(NULL),
		name(entry->Name())
	{
		LocatableDirectory* parent = entry->Parent();
		if (parent != NULL && strlen(parent->Path()) > 0)
			directory = parent->Path();
	}

	EntryPath(const EntryPath& other)
		:
		directory(other.directory),
		name(other.name)
	{
	}

	size_t HashValue() const
	{
		return BString::HashValue(directory)
			^ BString::HashValue(name);
	}

	bool operator==(const EntryPath& other) const
	{
		if (directory != other.directory
			&& (directory == NULL || other.directory == NULL
				|| strcmp(directory, other.directory) != 0)) {
			return false;
		}

		return strcmp(name, other.name) == 0;
	}
};


// #pragma mark - EntryHashDefinition


/**
 * @brief Hash-table policy that lets the locatable-entry table key by EntryPath.
 *
 * Provides hash and comparison using the (directory, name) pair, plus
 * GetLink() so the BOpenHashTable can chain entries through fNext.
 */
struct FileManager::EntryHashDefinition {
	typedef EntryPath		KeyType;
	typedef	LocatableEntry	ValueType;

	size_t HashKey(const EntryPath& key) const
	{
		return key.HashValue();
	}

	size_t Hash(const LocatableEntry* value) const
	{
		return HashKey(EntryPath(value));
	}

	bool Compare(const EntryPath& key, const LocatableEntry* value) const
	{
		return EntryPath(value) == key;
	}

	LocatableEntry*& GetLink(LocatableEntry* value) const
	{
		return value->fNext;
	}
};


// #pragma mark - Domain


/**
 * @brief One side of a target/source path universe owned by the FileManager.
 *
 * Maintains a LocatableEntry table keyed by (directory, name) and
 * implements the locate-by-mapping logic (auto-locating sibling and
 * descendant entries when a path is resolved). Locking is delegated back
 * to the owning FileManager so callers can hold the lock across entire
 * path-locating operations.
 */
class FileManager::Domain : private LocatableEntryOwner {
public:
	Domain(FileManager* manager, bool isLocal)
		:
		fManager(manager),
		fIsLocal(isLocal)
	{
	}

	~Domain()
	{
		LocatableEntry* entry = fEntries.Clear(true);
		while (entry != NULL) {
			LocatableEntry* next = entry->fNext;
			entry->ReleaseReference();
			entry = next;
		}
	}

	status_t Init()
	{
		status_t error = fEntries.Init();
		if (error != B_OK)
			return error;

		return B_OK;
	}

	LocatableFile* GetFile(const BString& directoryPath,
		const BString& relativePath)
	{
		if (directoryPath.Length() == 0 || relativePath[0] == '/')
			return GetFile(relativePath);
		return GetFile(BString(directoryPath) << '/' << relativePath);
	}

	LocatableFile* GetFile(const BString& path)
	{
		BString directoryPath;
		BString name;
		_SplitPath(path, directoryPath, name);
		LocatableFile* file = _GetFile(directoryPath, name);
		if (file == NULL)
			return NULL;

		// try to auto-locate the file
		if (LocatableDirectory* directory = file->Parent()) {
			if (directory->State() == LOCATABLE_ENTRY_UNLOCATED) {
				// parent not yet located -- try locate with the entry's path
				BString path;
				file->GetPath(path);
				_LocateEntry(file, path, true, true);
			} else {
				// parent already located -- locate the entry in the parent
				BString locatedDirectoryPath;
				if (directory->GetLocatedPath(locatedDirectoryPath))
					_LocateEntryInParentDir(file, locatedDirectoryPath, true);
			}
		}

		return file;
	}

	void EntryLocated(const BString& path, const BString& locatedPath)
	{
		BString directory;
		BString name;
		_SplitPath(path, directory, name);

		LocatableEntry* entry = _LookupEntry(EntryPath(directory, name));
		if (entry == NULL)
			return;

		_LocateEntry(entry, locatedPath, false, true);
	}

private:
	virtual bool Lock()
	{
		return fManager->Lock();
	}

	virtual void Unlock()
	{
		fManager->Unlock();
	}

	virtual void LocatableEntryUnused(LocatableEntry* entry)
	{
		AutoLocker<FileManager> lock(fManager);
		if (fEntries.Lookup(EntryPath(entry)) == entry)
			fEntries.Remove(entry);

		LocatableDirectory* parent = entry->Parent();
		if (parent != NULL)
			parent->RemoveEntry(entry);
	}

	bool _LocateDirectory(LocatableDirectory* directory,
		const BString& locatedPath, bool implicit)
	{
		if (directory == NULL
			|| directory->State() != LOCATABLE_ENTRY_UNLOCATED) {
			return false;
		}

		if (!_LocateEntry(directory, locatedPath, implicit, true))
			return false;

		_LocateEntries(directory, locatedPath, implicit);

		return true;
	}

	bool _LocateEntry(LocatableEntry* entry, const BString& locatedPath,
		bool implicit, bool locateAncestors)
	{
		if (implicit && entry->State() == LOCATABLE_ENTRY_LOCATED_EXPLICITLY)
			return false;

		struct stat st;
		if (stat(locatedPath, &st) != 0)
			return false;

		if (S_ISDIR(st.st_mode)) {
			LocatableDirectory* directory
				= dynamic_cast<LocatableDirectory*>(entry);
			if (directory == NULL)
				return false;
			entry->SetLocatedPath(locatedPath, implicit);
		} else if (S_ISREG(st.st_mode)) {
			LocatableFile* file = dynamic_cast<LocatableFile*>(entry);
			if (file == NULL)
				return false;
			entry->SetLocatedPath(locatedPath, implicit);
		}

		// locate the ancestor directories, if requested
		if (locateAncestors) {
			BString locatedDirectory;
			BString locatedName;
			_SplitPath(locatedPath, locatedDirectory, locatedName);
			if (locatedName == entry->Name())
				_LocateDirectory(entry->Parent(), locatedDirectory, implicit);
		}

		return true;
	}

	bool _LocateEntryInParentDir(LocatableEntry* entry,
		const BString& locatedDirectoryPath, bool implicit)
	{
		// construct the located entry path
		BString locatedEntryPath(locatedDirectoryPath);
		int32 pathLength = locatedEntryPath.Length();
		if (pathLength >= 1 && locatedEntryPath[pathLength - 1] != '/')
			locatedEntryPath << '/';
		locatedEntryPath << entry->Name();

		return _LocateEntry(entry, locatedEntryPath, implicit, false);
	}

	void _LocateEntries(LocatableDirectory* directory,
		const BString& locatedPath, bool implicit)
	{
		for (LocatableEntryList::ConstIterator it
				= directory->Entries().GetIterator();
			LocatableEntry* entry = it.Next();) {
			if (entry->State() == LOCATABLE_ENTRY_LOCATED_EXPLICITLY)
				continue;

			 if (_LocateEntryInParentDir(entry, locatedPath, implicit)) {
				// recurse for directories
				if (LocatableDirectory* subDir
						= dynamic_cast<LocatableDirectory*>(entry)) {
					BString locatedEntryPath;
					if (subDir->GetLocatedPath(locatedEntryPath))
						_LocateEntries(subDir, locatedEntryPath, implicit);
				}
			}
		}
	}

	LocatableFile* _GetFile(const BString& directoryPath, const BString& name)
	{
		BString normalizedDirPath;
		_NormalizePath(directoryPath, normalizedDirPath);

		// if already known return the file
		LocatableEntry* entry = _LookupEntry(EntryPath(normalizedDirPath, name));
		if (entry != NULL) {
			LocatableFile* file = dynamic_cast<LocatableFile*>(entry);
			if (file == NULL)
				return NULL;

			if (file->AcquireReference() == 0)
				fEntries.Remove(file);
			else
				return file;
		}

		// no such file yet -- create it
		LocatableDirectory* directory = _GetDirectory(normalizedDirPath);
		if (directory == NULL)
			return NULL;

		LocatableFile* file = new(std::nothrow) LocatableFile(this, directory,
			name);
		if (file == NULL) {
			directory->ReleaseReference();
			return NULL;
		}

		directory->AddEntry(file);

		fEntries.Insert(file);

		return file;
	}

	LocatableDirectory* _GetDirectory(const BString& path)
	{
		BString directoryPath;
		BString fileName;
		_SplitNormalizedPath(path, directoryPath, fileName);

		// if already know return the directory
		LocatableEntry* entry
			= _LookupEntry(EntryPath(directoryPath, fileName));
		if (entry != NULL) {
			LocatableDirectory* directory
				= dynamic_cast<LocatableDirectory*>(entry);
			if (directory == NULL)
				return NULL;
			directory->AcquireReference();
			return directory;
		}

		// get the parent directory
		LocatableDirectory* parentDirectory = NULL;
		if (directoryPath.Length() > 0) {
			parentDirectory = _GetDirectory(directoryPath);
			if (parentDirectory == NULL)
				return NULL;
		}

		// create a new directory
		LocatableDirectory* directory = new(std::nothrow) LocatableDirectory(
			this, parentDirectory, path);
		if (directory == NULL) {
			parentDirectory->ReleaseReference();
			return NULL;
		}

		// auto-locate, if possible
		if (fIsLocal) {
			BString dirPath;
			directory->GetPath(dirPath);
			directory->SetLocatedPath(dirPath, false);
		} else if (parentDirectory != NULL
			&& parentDirectory->State() != LOCATABLE_ENTRY_UNLOCATED) {
			BString locatedDirectoryPath;
			if (parentDirectory->GetLocatedPath(locatedDirectoryPath))
				_LocateEntryInParentDir(directory, locatedDirectoryPath, true);
		}

		if (parentDirectory != NULL)
			parentDirectory->AddEntry(directory);

		fEntries.Insert(directory);
		return directory;
	}

	LocatableEntry* _LookupEntry(const EntryPath& entryPath)
	{
		LocatableEntry* entry = fEntries.Lookup(entryPath);
		if (entry == NULL)
			return NULL;

		// if already unreferenced, remove it
		if (entry->CountReferences() == 0) {
			fEntries.Remove(entry);
			return NULL;
		}

		return entry;
	}

	void _NormalizePath(const BString& path, BString& _normalizedPath)
	{
		BString normalizedPath;
		char* buffer = normalizedPath.LockBuffer(path.Length());
		int32 outIndex = 0;
		const char* remaining = path.String();

		while (*remaining != '\0') {
			// collapse repeated slashes
			if (*remaining == '/') {
				buffer[outIndex++] = '/';
				remaining++;
				while (*remaining == '/')
					remaining++;
			}

			if (*remaining == '\0') {
				// remove trailing slash (unless it's "/" only)
				if (outIndex > 1)
					outIndex--;
				break;
			}

			// skip "." components
			if (*remaining == '.') {
				if (remaining[1] == '\0')
					break;

				if (remaining[1] == '/') {
					remaining += 2;
					while (*remaining == '/')
						remaining++;
					continue;
				}
			}

			// copy path component
			while (*remaining != '\0' && *remaining != '/')
				buffer[outIndex++] = *(remaining++);
		}

		// If the path didn't change, use the original path (BString's copy on
		// write mechanism) rather than the new string.
		if (outIndex == path.Length()) {
			_normalizedPath = path;
		} else {
			normalizedPath.UnlockBuffer(outIndex);
			_normalizedPath = normalizedPath;
		}
	}

	void _SplitPath(const BString& path, BString& _directory, BString& _name)
	{
		BString normalized;
		_NormalizePath(path, normalized);
		_SplitNormalizedPath(normalized, _directory, _name);
	}

	void _SplitNormalizedPath(const BString& path, BString& _directory,
		BString& _name)
	{
		// handle single component (including root dir) cases
		int32 lastSlash = path.FindLast('/');
		if (lastSlash < 0 || path.Length() == 1) {
			_directory = (const char*)NULL;
			_name = path;
			return;
		}

		// handle root dir + one component and multi component cases
		if (lastSlash == 0)
			_directory = "/";
		else
			_directory.SetTo(path, lastSlash);
		_name = path.String() + (lastSlash + 1);
	}

private:
	FileManager*		fManager;
	LocatableEntryTable	fEntries;
	bool				fIsLocal;
};


// #pragma mark - SourceFileEntry


/**
 * @brief Cache entry tying a source path to a loaded SourceFile.
 *
 * Implements SourceFileOwner so the loaded SourceFile can call back when
 * its reference count drops to zero or it is destroyed.
 */
struct FileManager::SourceFileEntry : public SourceFileOwner {

	FileManager*		manager;
	BString				path;
	SourceFile*			file;
	SourceFileEntry*	next;

	SourceFileEntry(FileManager* manager, const BString& path)
		:
		manager(manager),
		path(path),
		file(NULL)
	{
	}

	virtual void SourceFileUnused(SourceFile* sourceFile)
	{
		manager->_SourceFileUnused(this);
	}

	virtual void SourceFileDeleted(SourceFile* sourceFile)
	{
		// We have already been removed from the table, so commit suicide.
		delete this;
	}
};


// #pragma mark - SourceFileHashDefinition


/**
 * @brief Hash-table policy that lets the source-file cache key by source path.
 */
struct FileManager::SourceFileHashDefinition {
	typedef BString			KeyType;
	typedef	SourceFileEntry	ValueType;

	size_t HashKey(const BString& key) const
	{
		return key.HashValue();
	}

	size_t Hash(const SourceFileEntry* value) const
	{
		return HashKey(value->path);
	}

	bool Compare(const BString& key, const SourceFileEntry* value) const
	{
		return value->path == key;
	}

	SourceFileEntry*& GetLink(SourceFileEntry* value) const
	{
		return value->next;
	}
};


// #pragma mark - FileManager


/** @brief Construct an unconfigured FileManager; call Init() before use. */
FileManager::FileManager()
	:
	fLock("file manager"),
	fTargetDomain(NULL),
	fSourceDomain(NULL),
	fSourceFiles(NULL)
{
}


/** @brief Destroy both domains and the source-file cache. */
FileManager::~FileManager()
{
	delete fTargetDomain;
	delete fSourceDomain;

	SourceFileEntry* entry = fSourceFiles->Clear();
	while (entry != NULL) {
		SourceFileEntry* next = entry->next;
		delete entry;
		entry = next;
	}
	delete fSourceFiles;
}


/**
 * @brief Bring the FileManager online: lock, target/source domains, file cache.
 *
 * @param targetIsLocal  When true, target paths are auto-located against the
 *                       host filesystem (typical for local debugging).
 * @retval B_OK         All sub-objects were initialized.
 * @retval B_NO_MEMORY  Allocation of a domain or table failed.
 * @return Other status codes propagated from BLocker::InitCheck() or
 *         BOpenHashTable::Init().
 */
status_t
FileManager::Init(bool targetIsLocal)
{
	status_t error = fLock.InitCheck();
	if (error != B_OK)
		return error;

	// create target domain
	fTargetDomain = new(std::nothrow) Domain(this, targetIsLocal);
	if (fTargetDomain == NULL)
		return B_NO_MEMORY;

	error = fTargetDomain->Init();
	if (error != B_OK)
		return error;

	// create source domain
	fSourceDomain = new(std::nothrow) Domain(this, false);
	if (fSourceDomain == NULL)
		return B_NO_MEMORY;

	error = fSourceDomain->Init();
	if (error != B_OK)
		return error;

	// create source file table
	fSourceFiles = new(std::nothrow) SourceFileTable;
	if (fSourceFiles == NULL)
		return B_NO_MEMORY;

	error = fSourceFiles->Init();
	if (error != B_OK)
		return error;

	return B_OK;
}


/**
 * @brief Look up or create a target-domain file from a directory plus relative path.
 *
 * @param directory     Directory portion (or empty for an absolute @a relativePath).
 * @param relativePath  File path relative to @a directory or absolute.
 * @return A referenced LocatableFile, or NULL on allocation failure.
 */
LocatableFile*
FileManager::GetTargetFile(const BString& directory,
	const BString& relativePath)
{
	AutoLocker<FileManager> locker(this);
	return fTargetDomain->GetFile(directory, relativePath);
}


/**
 * @brief Look up or create a target-domain file from a single path.
 *
 * @param path  Absolute or relative path.
 * @return A referenced LocatableFile, or NULL on allocation failure.
 */
LocatableFile*
FileManager::GetTargetFile(const BString& path)
{
	AutoLocker<FileManager> locker(this);
	return fTargetDomain->GetFile(path);
}


/**
 * @brief Tell the target domain that @a path now resolves to @a locatedPath.
 *
 * @param path         Original (target-side) path.
 * @param locatedPath  Local path the user has supplied for it.
 */
void
FileManager::TargetEntryLocated(const BString& path,
	const BString& locatedPath)
{
	AutoLocker<FileManager> locker(this);
	fTargetDomain->EntryLocated(path, locatedPath);
}


/**
 * @brief Look up or create a source-domain file from a directory plus relative path.
 *
 * @param directory     Directory portion (or empty for an absolute @a relativePath).
 * @param relativePath  File path relative to @a directory or absolute.
 * @return A referenced LocatableFile, or NULL on allocation failure.
 */
LocatableFile*
FileManager::GetSourceFile(const BString& directory,
	const BString& relativePath)
{
	AutoLocker<FileManager> locker(this);
	LocatableFile* file = fSourceDomain->GetFile(directory, relativePath);

	return file;
}


/**
 * @brief Look up or create a source-domain file from a single path.
 *
 * @param path  Absolute or relative path.
 * @return A referenced LocatableFile, or NULL on allocation failure.
 */
LocatableFile*
FileManager::GetSourceFile(const BString& path)
{
	AutoLocker<FileManager> locker(this);
	LocatableFile* file = fSourceDomain->GetFile(path);

	return file;
}


/**
 * @brief Record a source-path mapping and invalidate any prior loaded copy.
 *
 * Clears any cached SourceFileEntry for @a path so a subsequent
 * LoadSourceFile() reads from the new location, then forwards the mapping
 * to the source domain and stores it in fSourceLocationMappings for
 * persistence across sessions.
 *
 * @param path         Original source path.
 * @param locatedPath  Local path the user has supplied for it.
 * @retval B_OK         Mapping recorded.
 * @retval B_NO_MEMORY  Out of memory while updating the map.
 */
status_t
FileManager::SourceEntryLocated(const BString& path,
	const BString& locatedPath)
{
	AutoLocker<FileManager> locker(this);

	// check if we already have this path mapped. If so,
	// first clear the mapping, as the user may be attempting
	// to correct an existing entry.
	SourceFileEntry* entry = _LookupSourceFile(path);
	if (entry != NULL)
		_SourceFileUnused(entry);

	fSourceDomain->EntryLocated(path, locatedPath);

	try {
		fSourceLocationMappings[path] = locatedPath;
	} catch (...) {
		return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Load (or fetch from cache) the SourceFile contents for @a file.
 *
 * Resolves the file's located path, applying any lazy mapping, then either
 * acquires a reference to a cached SourceFile or constructs a new one and
 * inserts it into the table.
 *
 * @param file         File whose contents are to be loaded.
 * @param _sourceFile  Output that receives a referenced SourceFile.
 * @retval B_OK               Loaded successfully.
 * @retval B_ENTRY_NOT_FOUND  No located path is known for @a file.
 * @retval B_NO_MEMORY        Allocation failed.
 * @return Other status codes propagated from SourceFile::Init().
 */
status_t
FileManager::LoadSourceFile(LocatableFile* file, SourceFile*& _sourceFile)
{
	AutoLocker<FileManager> locker(this);

	// get the path
	BString path;
	BString originalPath;
	file->GetPath(originalPath);
	if (!file->GetLocatedPath(path)) {
		// see if this is a file we have a lazy mapping for.
		if (!_LocateFileIfMapped(originalPath, file)
			|| !file->GetLocatedPath(path)) {
			return B_ENTRY_NOT_FOUND;
		}
	}

	// we might already know the source file
	SourceFileEntry* entry = _LookupSourceFile(originalPath);
	if (entry != NULL) {
		entry->file->AcquireReference();
		_sourceFile = entry->file;
		return B_OK;
	}

	// create the hash table entry
	entry = new(std::nothrow) SourceFileEntry(this, originalPath);
	if (entry == NULL)
		return B_NO_MEMORY;

	// load the file
	SourceFile* sourceFile = new(std::nothrow) SourceFile(entry);
	if (sourceFile == NULL) {
		delete entry;
		return B_NO_MEMORY;
	}
	ObjectDeleter<SourceFile> sourceFileDeleter(sourceFile);

	entry->file = sourceFile;

	status_t error = sourceFile->Init(path);
	if (error != B_OK)
		return error;

	fSourceFiles->Insert(entry);

	_sourceFile = sourceFileDeleter.Detach();
	return B_OK;
}


/**
 * @brief Restore source-path mappings from a TeamFileManagerSettings instance.
 *
 * @param settings  Settings record to read from.
 * @retval B_OK         All mappings restored.
 * @retval B_NO_MEMORY  Out of memory while updating the map.
 */
status_t
FileManager::LoadLocationMappings(TeamFileManagerSettings* settings)
{
	AutoLocker<FileManager> locker(this);
	for (int32 i = 0; i < settings->CountSourceMappings(); i++) {
		BString sourcePath;
		BString locatedPath;

		if (settings->GetSourceMappingAt(i, sourcePath, locatedPath) != B_OK)
			return B_NO_MEMORY;

		try {
			fSourceLocationMappings[sourcePath] = locatedPath;
		} catch (...) {
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


/**
 * @brief Persist the in-memory source-path mappings to @a settings.
 *
 * @param settings  Settings record to write to.
 * @return The first non-OK status from TeamFileManagerSettings::AddSourceMapping(),
 *         or B_OK on success.
 */
status_t
FileManager::SaveLocationMappings(TeamFileManagerSettings* settings)
{
	AutoLocker<FileManager> locker(this);

	for (LocatedFileMap::const_iterator it = fSourceLocationMappings.begin();
		it != fSourceLocationMappings.end(); ++it) {
		status_t error = settings->AddSourceMapping(it->first, it->second);
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


/**
 * @brief Find an existing source-file cache entry for @a path.
 *
 * Removes the entry from the table when its underlying SourceFile has
 * already lost all references.
 *
 * @param path  Original source-side path.
 * @return The matching entry, or NULL if none exists or it was stale.
 */
FileManager::SourceFileEntry*
FileManager::_LookupSourceFile(const BString& path)
{
	SourceFileEntry* entry = fSourceFiles->Lookup(path);
	if (entry == NULL)
		return NULL;

	// the entry might be unused already -- in that case remove it
	if (entry->file->CountReferences() == 0) {
		fSourceFiles->Remove(entry);
		return NULL;
	}

	return entry;
}


/**
 * @brief Callback fired when a cached SourceFile is no longer referenced.
 *
 * Removes the entry from the table so the next load reads a fresh copy.
 *
 * @param entry  Entry whose underlying SourceFile is unused.
 */
void
FileManager::_SourceFileUnused(SourceFileEntry* entry)
{
	AutoLocker<FileManager> locker(this);

	SourceFileEntry* otherEntry = fSourceFiles->Lookup(entry->path);
	if (otherEntry == entry)
		fSourceFiles->Remove(entry);
}


/**
 * @brief Apply a stored source-path mapping to @a file when one matches.
 *
 * Used by LoadSourceFile() so users can leave mappings registered without
 * having to explicitly call SourceEntryLocated() up front.
 *
 * @param sourcePath  Original source path being looked up.
 * @param file        Locatable file to update if a mapping is found.
 * @return true when a mapping existed and was applied, false otherwise.
 * @note Callers must hold the FileManager lock.
 */
bool
FileManager::_LocateFileIfMapped(const BString& sourcePath,
	LocatableFile* file)
{
	// called with lock held

	LocatedFileMap::const_iterator it = fSourceLocationMappings.find(
		sourcePath);
	if (it != fSourceLocationMappings.end()
		&& file->State() != LOCATABLE_ENTRY_LOCATED_EXPLICITLY
		&& file->State() != LOCATABLE_ENTRY_LOCATED_IMPLICITLY) {
		fSourceDomain->EntryLocated(it->first, it->second);
		return true;
	}

	return false;
}
