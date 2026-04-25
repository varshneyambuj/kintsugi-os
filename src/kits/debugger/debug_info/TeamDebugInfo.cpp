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
 *   Copyright 2012-2016, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file TeamDebugInfo.cpp
 * @brief Implementation of TeamDebugInfo, the team-wide aggregate of
 *        per-image debug data, type cache and source-code bookkeeping.
 *
 * TeamDebugInfo orchestrates the entire debugger debug-info subsystem for
 * one target team. It owns the prioritized list of SpecificTeamDebugInfo
 * backends, materializes ImageDebugInfo objects per loaded image, merges
 * per-image FunctionInstance lists into shared Function objects keyed by
 * source location, and caches FileSourceCode for each LocatableFile that
 * functions resolve to. It also provides the type lookup entry point used
 * across the debugger.
 *
 * @see ImageDebugInfo, Function, FunctionInstance, GlobalTypeCache,
 *      DwarfTeamDebugInfo, DebuggerTeamDebugInfo
 */


#include "TeamDebugInfo.h"

#include <stdio.h>

#include <new>

#include <AutoDeleter.h>
#include <AutoLocker.h>

#include "Architecture.h"
#include "DebuggerInterface.h"
#include "DebuggerTeamDebugInfo.h"
#include "DisassembledCode.h"
#include "DwarfTeamDebugInfo.h"
#include "FileManager.h"
#include "FileSourceCode.h"
#include "Function.h"
#include "FunctionID.h"
#include "ImageDebugInfo.h"
#include "ImageDebugInfoLoadingState.h"
#include "LocatableFile.h"
#include "SourceFile.h"
#include "SourceLanguage.h"
#include "SpecificImageDebugInfo.h"
#include "Type.h"
#include "TypeLookupConstraints.h"


// #pragma mark - FunctionHashDefinition


/**
 * @brief Hash-table policy used to look up a Function by FunctionInstance.
 *
 * Instances backed by the same source file, name and source location are
 * considered to denote the same logical Function. Instances without a
 * source file degenerate to identity comparison so each one becomes its
 * own Function.
 */
struct TeamDebugInfo::FunctionHashDefinition {
	typedef const FunctionInstance*	KeyType;
	typedef	Function				ValueType;

	/** @brief Hashes a key built from the instance's name and source
	           location, or its address when no source file is known. */
	size_t HashKey(const FunctionInstance* key) const
	{
		// Instances without source file only equal themselves.
		if (key->SourceFile() == NULL)
			return (uint32)(addr_t)key;

		uint32 hash = key->Name().HashValue();
		hash = hash * 17 + (uint32)(addr_t)key->SourceFile();
		SourceLocation location = key->GetSourceLocation();
		hash = hash * 17 + location.Line();
		hash = hash * 17 + location.Column();

		return hash;
	}

	/** @brief Hashes a stored Function via its first instance. */
	size_t Hash(const Function* value) const
	{
		return HashKey(value->FirstInstance());
	}

	/** @brief Equality predicate used for hash-table collisions. */
	bool Compare(const FunctionInstance* key, const Function* value) const
	{
		// source file must be the same
		if (key->SourceFile() != value->SourceFile())
			return false;

		// Instances without source file only equal themselves.
		if (key->SourceFile() == NULL)
			return key == value->FirstInstance();

		// Source location and function name must also match.
		return key->GetSourceLocation() == value->GetSourceLocation()
			&& key->Name() == value->Name();
	}

	/** @brief Returns the chain pointer used by the hash table. */
	Function*& GetLink(Function* value) const
	{
		return value->fNext;
	}
};


// #pragma mark - SourceFileEntry


/**
 * @brief Per-source-file aggregate keeping the list of Functions known in a
 *        file and the cached FileSourceCode (when loaded).
 *
 * Functions are kept sorted by source location so a binary search can find
 * the function covering an arbitrary line/column.
 */
struct TeamDebugInfo::SourceFileEntry {
	/** @brief Constructs an empty entry and acquires a reference on the
	           source file. */
	SourceFileEntry(LocatableFile* sourceFile)
		:
		fSourceFile(sourceFile),
		fSourceCode(NULL)
	{
		fSourceFile->AcquireReference();
	}

	/** @brief Destroys the entry and releases all held references. */
	~SourceFileEntry()
	{
		SetSourceCode(NULL);
		fSourceFile->ReleaseReference();
	}

	/** @brief No-op initialization hook reserved for symmetry. */
	status_t Init()
	{
		return B_OK;
	}

	/** @brief Returns the LocatableFile this entry represents. */
	LocatableFile* SourceFile() const
	{
		return fSourceFile;
	}

	/** @brief Returns the cached FileSourceCode or @c NULL if not loaded. */
	FileSourceCode* GetSourceCode() const
	{
		return fSourceCode;
	}

	/** @brief Replaces the cached source code, adjusting refcounts. */
	void SetSourceCode(FileSourceCode* sourceCode)
	{
		if (sourceCode == fSourceCode)
			return;

		if (fSourceCode != NULL)
			fSourceCode->ReleaseReference();

		fSourceCode = sourceCode;

		if (fSourceCode != NULL)
			fSourceCode->AcquireReference();
	}


	/** @brief Reports whether the function list is empty. */
	bool IsUnused() const
	{
		return fFunctions.IsEmpty();
	}

	/** @brief Adds a Function in sorted order keyed by source location. */
	status_t AddFunction(Function* function)
	{
		if (!fFunctions.BinaryInsert(function, &_CompareFunctions))
			return B_NO_MEMORY;

		return B_OK;
	}

	/** @brief Removes a previously added Function from the sorted list. */
	void RemoveFunction(Function* function)
	{
		int32 index = fFunctions.BinarySearchIndex(*function,
			&_CompareFunctions);
		if (index >= 0)
			fFunctions.RemoveItemAt(index);
	}

	/**
	 * @brief Looks up the function that covers a given source location.
	 *
	 * If no function declares its start at exactly @a location the previous
	 * function (which may still contain the line) is returned.
	 *
	 * @param location  Source line/column to query.
	 * @return Pointer to the covering Function, or @c NULL.
	 */
	Function* FunctionAtLocation(const SourceLocation& location) const
	{
		int32 index = fFunctions.BinarySearchIndexByKey(location,
			&_CompareLocationFunction);
		if (index >= 0)
			return fFunctions.ItemAt(index);

		// No exact match, so we return the previous function which might still
		// contain the location.
		index = -index - 1;

		if (index == 0)
			return NULL;

		return fFunctions.ItemAt(index - 1);
	}

	/** @brief Returns the function at the given list index. */
	Function* FunctionAt(int32 index) const
	{
		return fFunctions.ItemAt(index);
	}

	/** @brief Linear search for a function by exact name. */
	Function* FunctionByName(const BString& name) const
	{
		// TODO: That's not exactly optimal.
		for (int32 i = 0; Function* function = fFunctions.ItemAt(i); i++) {
			if (name == function->Name())
				return function;
		}
		return NULL;
	}

private:
	typedef BObjectList<Function> FunctionList;

private:
	/** @brief Compares two Functions by source location, breaking ties by
	           name (so distinct template instantiations differ). */
	static int _CompareFunctions(const Function* a, const Function* b)
	{
		SourceLocation locationA = a->GetSourceLocation();
		SourceLocation locationB = b->GetSourceLocation();

		if (locationA < locationB)
			return -1;

		if (locationA != locationB )
			return 1;

		// if the locations match we still need to compare by name to be
		// certain, since differently typed instantiations of template
		// functions will have the same source file and location
		return a->Name().Compare(b->Name());
	}

	/** @brief Compares a SourceLocation key against a Function. */
	static int _CompareLocationFunction(const SourceLocation* location,
		const Function* function)
	{
		SourceLocation functionLocation = function->GetSourceLocation();

		if (*location < functionLocation)
			return -1;

		return *location == functionLocation ? 0 : 1;
	}

private:
	LocatableFile*		fSourceFile;
	FileSourceCode*		fSourceCode;
	FunctionList		fFunctions;

public:
	SourceFileEntry*	fNext;
};


// #pragma mark - SourceFileHashDefinition


/**
 * @brief Hash-table policy keying SourceFileEntry by its LocatableFile
 *        pointer; the pointer is unique per source file in the team.
 */
struct TeamDebugInfo::SourceFileHashDefinition {
	typedef const LocatableFile*	KeyType;
	typedef	SourceFileEntry			ValueType;

	/** @brief Hashes the LocatableFile pointer directly. */
	size_t HashKey(const LocatableFile* key) const
	{
		return (size_t)(addr_t)key;
	}

	/** @brief Hashes a stored entry by its source-file pointer. */
	size_t Hash(const SourceFileEntry* value) const
	{
		return HashKey(value->SourceFile());
	}

	/** @brief Pointer-equality comparison. */
	bool Compare(const LocatableFile* key, const SourceFileEntry* value) const
	{
		return key == value->SourceFile();
	}

	/** @brief Returns the chain pointer used by the hash table. */
	SourceFileEntry*& GetLink(SourceFileEntry* value) const
	{
		return value->fNext;
	}
};


// #pragma mark - TeamDebugInfo


/**
 * @brief Constructs an uninitialized TeamDebugInfo bound to the given
 *        debugger interface, architecture and file manager.
 *
 * Init() must be called before any other method to allocate hash tables,
 * the type cache and the per-backend SpecificTeamDebugInfo objects.
 *
 * @param debuggerInterface  Debugger interface; reference acquired.
 * @param architecture       Target architecture used by all backends.
 * @param fileManager        File manager used to resolve source files.
 */
TeamDebugInfo::TeamDebugInfo(DebuggerInterface* debuggerInterface,
	Architecture* architecture, FileManager* fileManager)
	:
	fLock("team debug info"),
	fDebuggerInterface(debuggerInterface),
	fArchitecture(architecture),
	fFileManager(fileManager),
	fSpecificInfos(10),
	fFunctions(NULL),
	fSourceFiles(NULL),
	fTypeCache(NULL),
	fMainFunction(NULL)
{
	fDebuggerInterface->AcquireReference();
}


/**
 * @brief Destroys the team debug info, releasing every owned table,
 *        cached entry and reference.
 */
TeamDebugInfo::~TeamDebugInfo()
{
	if (fTypeCache != NULL)
		fTypeCache->ReleaseReference();

	if (fSourceFiles != NULL) {
		SourceFileEntry* entry = fSourceFiles->Clear(true);
		while (entry != NULL) {
			SourceFileEntry* next = entry->fNext;
			delete entry;
			entry = next;
		}

		delete fSourceFiles;
	}

	if (fFunctions != NULL) {
		Function* function = fFunctions->Clear(true);
		while (function != NULL) {
			Function* next = function->fNext;
			function->ReleaseReference();
			function = next;
		}

		delete fFunctions;
	}

	fDebuggerInterface->ReleaseReference();
}


/**
 * @brief Initializes lock, hash tables, type cache and SpecificTeamDebugInfo
 *        backends in descending order of expressiveness.
 *
 * Two backends are registered: the DWARF-based info first, the live
 * debugger fallback second. Either failing to construct or initialize
 * aborts the whole call.
 *
 * @retval B_OK         The team debug info is ready to use.
 * @retval B_NO_MEMORY  Allocation failure.
 * @retval other        Errors propagated from a backend's Init().
 */
status_t
TeamDebugInfo::Init()
{
	// check the lock
	status_t error = fLock.InitCheck();
	if (error != B_OK)
		return error;

	// create function hash table
	fFunctions = new(std::nothrow) FunctionTable;
	if (fFunctions == NULL)
		return B_NO_MEMORY;

	error = fFunctions->Init();
	if (error != B_OK)
		return error;

	// create source file hash table
	fSourceFiles = new(std::nothrow) SourceFileTable;
	if (fSourceFiles == NULL)
		return B_NO_MEMORY;

	error = fSourceFiles->Init();
	if (error != B_OK)
		return error;

	// create a type cache
	fTypeCache = new(std::nothrow) GlobalTypeCache;
	if (fTypeCache == NULL)
		return B_NO_MEMORY;

	error = fTypeCache->Init();
	if (error != B_OK)
		return error;

	// Create specific infos for all types of debug info we support, in
	// descending order of expressiveness.

	// DWARF
	DwarfTeamDebugInfo* dwarfInfo = new(std::nothrow) DwarfTeamDebugInfo(
		fArchitecture, fDebuggerInterface, fFileManager, this, this,
		fTypeCache);
	if (dwarfInfo == NULL || !fSpecificInfos.AddItem(dwarfInfo)) {
		delete dwarfInfo;
		return B_NO_MEMORY;
	}

	error = dwarfInfo->Init();
	if (error != B_OK)
		return error;

	// debugger based info
	DebuggerTeamDebugInfo* debuggerInfo
		= new(std::nothrow) DebuggerTeamDebugInfo(fDebuggerInterface,
			fArchitecture);
	if (debuggerInfo == NULL || !fSpecificInfos.AddItem(debuggerInfo)) {
		delete debuggerInfo;
		return B_NO_MEMORY;
	}

	error = debuggerInfo->Init();
	if (error != B_OK)
		return error;

	return B_OK;
}


/**
 * @brief Convenience wrapper calling GetType() against the team's own type
 *        cache.
 *
 * @param name        Type name to look up.
 * @param constraints Optional kind/subkind constraints.
 * @param _type       Out parameter receiving the resolved Type.
 * @return Status from GetType().
 */
status_t
TeamDebugInfo::LookupTypeByName(const BString& name,
	const TypeLookupConstraints& constraints, Type*& _type)
{
	return GetType(fTypeCache, name, constraints, _type);
}


/**
 * @brief Reports whether a type by the given name exists in the team.
 *
 * @param name        Type name to test.
 * @param constraints Optional kind/subkind constraints.
 * @return @c true if any image debug info contains the type.
 */
bool
TeamDebugInfo::TypeExistsByName(const BString& name,
	const TypeLookupConstraints& constraints)
{
	return HasType(fTypeCache, name, constraints);
}


/**
 * @brief Resolves a type by name, consulting a given GlobalTypeCache first
 *        and falling back to per-image queries.
 *
 * The cache is checked under its own lock; on cache miss the team lock is
 * taken briefly to clone the image list (with references), and queries are
 * issued without any lock held to avoid contention.
 *
 * @param cache       Cache to consult and (via the backends) populate.
 * @param name        Type name to look up.
 * @param constraints Optional kind/subkind constraints.
 * @param _type       Out parameter receiving the resolved Type.
 * @retval B_OK              A type was returned.
 * @retval B_ENTRY_NOT_FOUND No backend resolved @a name.
 * @retval other             Errors from a backend (e.g. @c B_NO_MEMORY).
 */
status_t
TeamDebugInfo::GetType(GlobalTypeCache* cache, const BString& name,
	const TypeLookupConstraints& constraints, Type*& _type)
{
	// maybe the type is already cached
	AutoLocker<GlobalTypeCache> cacheLocker(cache);

	Type* type = cache->GetType(name, constraints);
	if (type != NULL) {
		type->AcquireReference();
		_type = type;
		return B_OK;
	}

	cacheLocker.Unlock();

	// Clone the image list and get references to the images, so we can iterate
	// through them without locking.
	AutoLocker<BLocker> locker(fLock);

	ImageList images;
	for (int32 i = 0; ImageDebugInfo* imageDebugInfo = fImages.ItemAt(i); i++) {
		if (images.AddItem(imageDebugInfo))
			imageDebugInfo->AcquireReference();
	}

	locker.Unlock();

	// get the type
	status_t error = B_ENTRY_NOT_FOUND;
	for (int32 i = 0; ImageDebugInfo* imageDebugInfo = images.ItemAt(i); i++) {
		error = imageDebugInfo->GetType(cache, name, constraints, type);
		if (error == B_OK) {
			_type = type;
			break;
		}
	}

	// release the references
	for (int32 i = 0; ImageDebugInfo* imageDebugInfo = images.ItemAt(i); i++)
		imageDebugInfo->ReleaseReference();

	return error;
}


/**
 * @brief Reports whether the type identified by @a name is known.
 *
 * Mirrors GetType() but stops at the first match without resolving the
 * concrete Type object.
 *
 * @param cache       Cache to consult.
 * @param name        Type name to test.
 * @param constraints Optional kind/subkind constraints.
 * @return @c true if any cache entry or image backend reports the type.
 */
bool
TeamDebugInfo::HasType(GlobalTypeCache* cache, const BString& name,
	const TypeLookupConstraints& constraints)
{
	// maybe the type is already cached
	AutoLocker<GlobalTypeCache> cacheLocker(cache);

	Type* type = cache->GetType(name, constraints);
	if (type != NULL)
		return true;

	cacheLocker.Unlock();

	// Clone the image list and get references to the images, so we can iterate
	// through them without locking.
	AutoLocker<BLocker> locker(fLock);

	ImageList images;
	for (int32 i = 0; ImageDebugInfo* imageDebugInfo = fImages.ItemAt(i); i++) {
		if (images.AddItem(imageDebugInfo))
			imageDebugInfo->AcquireReference();
	}

	locker.Unlock();

	bool found = false;
	for (int32 i = 0; ImageDebugInfo* imageDebugInfo = images.ItemAt(i); i++) {
		if (imageDebugInfo->HasType(name, constraints)) {
			found = true;
			break;
		}
	}

	// release the references
	for (int32 i = 0; ImageDebugInfo* imageDebugInfo = images.ItemAt(i); i++)
		imageDebugInfo->ReleaseReference();

	return found;
}


/**
 * @brief Returns the currently loaded SourceCode for a given function info.
 *
 * Tries the source-file path first: if the Function at @a info's start
 * location holds loaded source code, that is returned. Falls through to a
 * lazy attach if a SourceFileEntry already has the FileSourceCode but the
 * Function does not. As a last resort, scans the image list and returns
 * the disassembled code attached to a matching FunctionInstance.
 *
 * @param info   FunctionDebugInfo whose code is being requested.
 * @param _code  Out parameter receiving an additional reference to the
 *               source code.
 * @retval B_OK              A SourceCode was returned.
 * @retval B_ENTRY_NOT_FOUND No loaded code was found for @a info.
 */
status_t
TeamDebugInfo::GetActiveSourceCode(FunctionDebugInfo* info, SourceCode*& _code)
{
	AutoLocker<BLocker> locker(fLock);

	LocatableFile* file = info->SourceFile();
	if (file != NULL) {
		Function* function = FunctionAtSourceLocation(file,
			info->SourceStartLocation());
		if (function != NULL) {
			function_source_state state = function->SourceCodeState();
			if (function->SourceCodeState() == FUNCTION_SOURCE_LOADED) {
				_code = function->GetSourceCode();
				_code->AcquireReference();
				return B_OK;
			} else if (state == FUNCTION_SOURCE_NOT_LOADED) {
				// if the function's source state is not loaded, check
				// if we already know the file anyways. Currently, when
				// a source code job runs, it does so on behalf of a specific
				// function, and consequently only sets the loaded source code
				// on that particular function at that point in time, rather
				// than all others sharing that same file. Consequently,
				// set it lazily here.
				SourceFileEntry* entry = fSourceFiles->Lookup(file);
				if (entry != NULL) {
					FileSourceCode* sourceCode = entry->GetSourceCode();
					if (sourceCode != NULL) {
						function->SetSourceCode(sourceCode,
							FUNCTION_SOURCE_LOADED);
						_code = sourceCode;
						_code->AcquireReference();
						return B_OK;
					}
				}
			}
		}
	}

	for (int32 i = 0; i < fImages.CountItems(); i++) {
		ImageDebugInfo* imageInfo = fImages.ItemAt(i);
		FunctionInstance* instance = imageInfo->FunctionAtAddress(
			info->Address());
		if (instance != NULL && instance->SourceCodeState()
				== FUNCTION_SOURCE_LOADED) {
			_code = instance->GetSourceCode();
			_code->AcquireReference();
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Drives every registered backend to materialize per-image debug
 *        info for a newly loaded image.
 *
 * Iterates SpecificTeamDebugInfo backends in order. Each backend that
 * succeeds appends a SpecificImageDebugInfo to the new ImageDebugInfo. A
 * backend that requires user input causes the call to return early with
 * the corresponding error so the orchestrator can interact with the user
 * and resume. After all backends have been queried the aggregated
 * ImageDebugInfo is finalized via FinishInit() and the team's main
 * function is recorded if a backend identified one.
 *
 * @param imageInfo         Image identity passed to each backend.
 * @param imageFile         Located image file (may be @c NULL for purely
 *                          symbol-driven backends).
 * @param _state            Loading state used to convey backend-specific
 *                          progress and user prompts.
 * @param _imageDebugInfo   Out parameter receiving the new ImageDebugInfo
 *                          on success; reference transferred to caller.
 * @retval B_OK         The image is fully described.
 * @retval B_NO_MEMORY  Allocation failure inside a backend.
 * @retval other        Backend errors propagated as-is, including the
 *                      "user input required" indication.
 */
status_t
TeamDebugInfo::LoadImageDebugInfo(const ImageInfo& imageInfo,
	LocatableFile* imageFile, ImageDebugInfoLoadingState& _state,
	ImageDebugInfo*& _imageDebugInfo)
{
	ImageDebugInfo* imageDebugInfo = new(std::nothrow) ImageDebugInfo(
		imageInfo);
	if (imageDebugInfo == NULL)
		return B_NO_MEMORY;
	BReference<ImageDebugInfo> imageDebugInfoReference(imageDebugInfo, true);

	for (int32 i = 0; SpecificTeamDebugInfo* specificTeamInfo
			= fSpecificInfos.ItemAt(i); i++) {
		SpecificImageDebugInfo* specificImageInfo;
		status_t error = specificTeamInfo->CreateImageDebugInfo(imageInfo,
			imageFile, _state, specificImageInfo);
		if (error == B_OK) {
			if (!imageDebugInfo->AddSpecificInfo(specificImageInfo)) {
				delete specificImageInfo;
				return B_NO_MEMORY;
			}
		} else if (_state.UserInputRequired()) {
			_state.SetSpecificInfoIndex(i);
			return error;
		} else if (error == B_NO_MEMORY)
			return error;
				// fail only when out of memory

		_state.ClearSpecificDebugInfoLoadingState();
			// if we made it this far, then we're done with current specific
			// info, and its corresponding state object, if any, is no longer
			// needed
	}

	status_t error = imageDebugInfo->FinishInit(fDebuggerInterface);
	if (error != B_OK)
		return error;

	if (fMainFunction == NULL) {
		FunctionInstance* instance = imageDebugInfo->MainFunction();
		if (instance != NULL)
			fMainFunction = instance;
	}

	_imageDebugInfo = imageDebugInfoReference.Detach();
	return B_OK;
}


/**
 * @brief Loads source-code lines for a known source file and annotates
 *        them with statement information from every covering image.
 *
 * Reuses a previously cached FileSourceCode when present. Otherwise,
 * picks the source language from one of the file's functions, asks the
 * file manager to read the on-disk file, builds a FileSourceCode and
 * calls AddSourceCodeInfo() on every image that knows the file.
 *
 * @param file         Source file to load.
 * @param _sourceCode  Out parameter receiving the (newly cached) source
 *                     code; reference transferred to caller.
 * @retval B_OK              Source code is available and returned.
 * @retval B_ENTRY_NOT_FOUND The file is unknown or no image contributed
 *                           statement info.
 * @retval B_NO_MEMORY       Allocation failure.
 * @retval other             Errors from the file manager or backends.
 */
status_t
TeamDebugInfo::LoadSourceCode(LocatableFile* file, FileSourceCode*& _sourceCode)
{
	AutoLocker<BLocker> locker(fLock);

	// If we don't know the source file, there's nothing we can do.
	SourceFileEntry* entry = fSourceFiles->Lookup(file);
	if (entry == NULL)
		return B_ENTRY_NOT_FOUND;

	// the source might already be loaded
	FileSourceCode* sourceCode = entry->GetSourceCode();
	if (sourceCode != NULL) {
		sourceCode->AcquireReference();
		_sourceCode = sourceCode;
		return B_OK;
	}

	// get the source language from some function's image debug info
	Function* function = entry->FunctionAt(0);
	if (function == NULL)
		return B_ENTRY_NOT_FOUND;

	FunctionDebugInfo* functionDebugInfo
		= function->FirstInstance()->GetFunctionDebugInfo();
	SourceLanguage* language;
	status_t error = functionDebugInfo->GetSpecificImageDebugInfo()
		->GetSourceLanguage(functionDebugInfo, language);
	if (error != B_OK)
		return error;
	BReference<SourceLanguage> languageReference(language, true);

	// no source code yet
//	locker.Unlock();
	// TODO: It would be nice to unlock here, but we need to iterate through
	// the images below. We could clone the list, acquire references, and
	// unlock. Then we have to compare the list with the then current list when
	// we're done loading.

	// load the source file
	SourceFile* sourceFile;
	error = fFileManager->LoadSourceFile(file, sourceFile);
	if (error != B_OK)
		return error;

	// create the source code
	sourceCode = new(std::nothrow) FileSourceCode(file, sourceFile, language);
	sourceFile->ReleaseReference();
	if (sourceCode == NULL)
		return B_NO_MEMORY;
	BReference<FileSourceCode> sourceCodeReference(sourceCode, true);

	error = sourceCode->Init();
	if (error != B_OK)
		return error;

	// Iterate through all images that know the source file and ask them to add
	// information.
	bool anyInfo = false;
	for (int32 i = 0; ImageDebugInfo* imageDebugInfo = fImages.ItemAt(i); i++)
		anyInfo |= imageDebugInfo->AddSourceCodeInfo(file, sourceCode) == B_OK;

	if (!anyInfo)
		return B_ENTRY_NOT_FOUND;

	entry->SetSourceCode(sourceCode);

	_sourceCode = sourceCodeReference.Detach();
	return B_OK;
}


/**
 * @brief Drops the cached source code for a file (e.g. after the file
 *        moves on disk).
 *
 * @param sourceFile  Source file whose cached FileSourceCode should be
 *                    invalidated; no-op if unknown.
 */
void
TeamDebugInfo::ClearSourceCode(LocatableFile* sourceFile)
{
	AutoLocker<BLocker> locker(fLock);

	SourceFileEntry* entry = fSourceFiles->Lookup(sourceFile);
	if (entry != NULL)
		entry->SetSourceCode(NULL);
}


/**
 * @brief Reads the function's instructions from the target and returns
 *        DisassembledCode produced by the architecture.
 *
 * Reads up to 64 KiB or the function size, whichever is smaller. The
 * caller-provided allocation is freed automatically.
 *
 * @param functionInstance  Function instance to disassemble.
 * @param _sourceCode       Out parameter receiving the new
 *                          DisassembledCode on success.
 * @retval B_OK         Disassembly succeeded.
 * @retval B_NO_MEMORY  Allocation failure.
 * @retval other        Negative ssize_t errors from ReadCode() or codes
 *                      from the architecture's disassembler.
 */
status_t
TeamDebugInfo::DisassembleFunction(FunctionInstance* functionInstance,
	DisassembledCode*& _sourceCode)
{
	// allocate a buffer for the function code
	static const target_size_t kMaxBufferSize = 64 * 1024;
	target_size_t bufferSize = std::min(functionInstance->Size(),
		kMaxBufferSize);
	void* buffer = malloc(bufferSize);
	if (buffer == NULL)
		return B_NO_MEMORY;
	MemoryDeleter bufferDeleter(buffer);

	// read the function code
	FunctionDebugInfo* functionDebugInfo
		= functionInstance->GetFunctionDebugInfo();
	ssize_t bytesRead = functionDebugInfo->GetSpecificImageDebugInfo()
		->ReadCode(functionInstance->Address(), buffer, bufferSize);
	if (bytesRead < 0)
		return bytesRead;

	return fArchitecture->DisassembleCode(functionDebugInfo, buffer, bytesRead,
		_sourceCode);
}


/**
 * @brief Inserts a fully-populated ImageDebugInfo into the team and merges
 *        its FunctionInstance objects with existing Functions.
 *
 * For every instance, a Function with the same source identity is reused;
 * otherwise a new Function is created and registered. After merging, every
 * SourceFileEntry that has cached source code is given a chance to
 * absorb additional statement information contributed by the new image.
 *
 * @param imageDebugInfo  Newly produced per-image info to take ownership
 *                        of.
 * @retval B_OK         Image successfully integrated.
 * @retval B_NO_MEMORY  Allocation failure; partial work is rolled back via
 *                      RemoveImageDebugInfo().
 */
status_t
TeamDebugInfo::AddImageDebugInfo(ImageDebugInfo* imageDebugInfo)
{
	AutoLocker<BLocker> locker(fLock);
		// We have both locks now, so that for read-only access either lock
		// suffices.

	if (!fImages.AddItem(imageDebugInfo))
		return B_NO_MEMORY;

	// Match all of the image debug info's functions instances with functions.
	BObjectList<SourceFileEntry> sourceFileEntries;
	for (int32 i = 0;
		FunctionInstance* instance = imageDebugInfo->FunctionAt(i); i++) {
		// lookup the function or create it, if it doesn't exist yet
		Function* function = fFunctions->Lookup(instance);
		if (function != NULL) {
// TODO: Also update possible user breakpoints in this function!
			function->AddInstance(instance);
			instance->SetFunction(function);

			// The new image debug info might have additional information about
			// the source file of the function, so remember the source file
			// entry.
			if (LocatableFile* sourceFile = function->SourceFile()) {
				SourceFileEntry* entry = fSourceFiles->Lookup(sourceFile);
				if (entry != NULL && entry->GetSourceCode() != NULL)
					sourceFileEntries.AddItem(entry);
			}
		} else {
			function = new(std::nothrow) Function;
			if (function == NULL) {
				RemoveImageDebugInfo(imageDebugInfo);
				return B_NO_MEMORY;
			}
			function->AddInstance(instance);
			instance->SetFunction(function);

			status_t error = _AddFunction(function);
				// Insert after adding the instance. Otherwise the function
				// wouldn't be hashable/comparable.
			if (error != B_OK) {
				function->RemoveInstance(instance);
				instance->SetFunction(NULL);
				RemoveImageDebugInfo(imageDebugInfo);
				return error;
			}
		}
	}

	// update the source files the image debug info knows about
	for (int32 i = 0; SourceFileEntry* entry = sourceFileEntries.ItemAt(i);
			i++) {
		FileSourceCode* sourceCode = entry->GetSourceCode();
		sourceCode->Lock();
		if (imageDebugInfo->AddSourceCodeInfo(entry->SourceFile(),
				sourceCode) == B_OK) {
			// TODO: Notify interesting parties! Iterate through all functions
			// for this source file?
		}
		sourceCode->Unlock();
	}

	return B_OK;
}


/**
 * @brief Detaches an ImageDebugInfo from the team and unwinds the
 *        Function/FunctionInstance bookkeeping.
 *
 * Also evicts any types that originated from this image from the global
 * type cache so subsequent lookups do not return stale entries.
 *
 * @param imageDebugInfo  Image debug info to remove.
 */
void
TeamDebugInfo::RemoveImageDebugInfo(ImageDebugInfo* imageDebugInfo)
{
	AutoLocker<BLocker> locker(fLock);
		// We have both locks now, so that for read-only access either lock
		// suffices.

	// Remove the functions from all of the image debug info's functions
	// instances.
	for (int32 i = 0;
		FunctionInstance* instance = imageDebugInfo->FunctionAt(i); i++) {
		if (Function* function = instance->GetFunction()) {
// TODO: Also update possible user breakpoints in this function!
			if (function->FirstInstance() == function->LastInstance()) {
				// function unused -- remove it
				// Note, that we have to remove it from the hash before removing
				// the instance, since otherwise the function cannot be compared
				// anymore.
				_RemoveFunction(function);
				function->ReleaseReference();
					// The instance still has a reference.
			}

			function->RemoveInstance(instance);
			instance->SetFunction(NULL);
				// If this was the last instance, it will remove the last
				// reference to the function.
		}
	}

	// remove cached types from that image
	fTypeCache->RemoveTypes(imageDebugInfo->GetImageInfo().ImageID());

	fImages.RemoveItem(imageDebugInfo);
}


/**
 * @brief Looks up an ImageDebugInfo by image name.
 *
 * @param name  Image name to find.
 * @return Pointer to the matching ImageDebugInfo, or @c NULL.
 */
ImageDebugInfo*
TeamDebugInfo::ImageDebugInfoByName(const char* name) const
{
	for (int32 i = 0; ImageDebugInfo* imageDebugInfo = fImages.ItemAt(i); i++) {
		if (imageDebugInfo->GetImageInfo().Name() == name)
			return imageDebugInfo;
	}

	return NULL;
}


/**
 * @brief Finds the Function whose declaration covers the given source
 *        location.
 *
 * @param file      Source file.
 * @param location  Line/column in @a file.
 * @return Pointer to the Function, or @c NULL when @a file is unknown.
 */
Function*
TeamDebugInfo::FunctionAtSourceLocation(LocatableFile* file,
	const SourceLocation& location) const
{
	if (SourceFileEntry* entry = fSourceFiles->Lookup(file))
		return entry->FunctionAtLocation(location);
	return NULL;
}


/**
 * @brief Resolves a FunctionID to the corresponding Function.
 *
 * Handles both SourceFunctionID (looked up via SourceFileEntry) and
 * ImageFunctionID (looked up via ImageDebugInfoByName + FunctionByName).
 *
 * @param functionID  Identifier to resolve.
 * @return Pointer to the Function, or @c NULL when not found or when
 *         @a functionID is of an unsupported subtype.
 */
Function*
TeamDebugInfo::FunctionByID(FunctionID* functionID) const
{
	if (SourceFunctionID* sourceFunctionID
			= dynamic_cast<SourceFunctionID*>(functionID)) {
		// get the source file
		LocatableFile* file = fFileManager->GetSourceFile(
			sourceFunctionID->SourceFilePath());
		if (file == NULL)
			return NULL;
		BReference<LocatableFile> fileReference(file, true);

		if (SourceFileEntry* entry = fSourceFiles->Lookup(file))
			return entry->FunctionByName(functionID->FunctionName());
		return NULL;
	}

	ImageFunctionID* imageFunctionID
		= dynamic_cast<ImageFunctionID*>(functionID);
	if (imageFunctionID == NULL)
		return NULL;

	ImageDebugInfo* imageDebugInfo
		= ImageDebugInfoByName(imageFunctionID->ImageName());
	if (imageDebugInfo == NULL)
		return NULL;

	FunctionInstance* functionInstance = imageDebugInfo->FunctionByName(
		functionID->FunctionName());
	return functionInstance != NULL ? functionInstance->GetFunction() : NULL;
}


/**
 * @brief Adds a Function to the team-wide hash and to its source file's
 *        per-file list (creating a SourceFileEntry on demand).
 *
 * @param function  Function to register.
 * @retval B_OK         Function added to all relevant indexes.
 * @retval B_NO_MEMORY  Allocation failure; any partial work is undone.
 */
status_t
TeamDebugInfo::_AddFunction(Function* function)
{
	// If the function refers to a source file, add it to the respective entry.
	if (LocatableFile* sourceFile = function->SourceFile()) {
		SourceFileEntry* entry = fSourceFiles->Lookup(sourceFile);
		if (entry == NULL) {
			// no entry for the source file yet -- create on
			entry = new(std::nothrow) SourceFileEntry(sourceFile);
			if (entry == NULL)
				return B_NO_MEMORY;

			status_t error = entry->Init();
			if (error != B_OK) {
				delete entry;
				return error;
			}

			fSourceFiles->Insert(entry);
		}

		// add the function
		status_t error = entry->AddFunction(function);
		if (error != B_OK) {
			if (entry->IsUnused()) {
				fSourceFiles->Remove(entry);
				delete entry;
			}
			return error;
		}
	}

	fFunctions->Insert(function);

	return B_OK;
}


/**
 * @brief Removes a Function from the team-wide hash and its
 *        SourceFileEntry, if one exists.
 *
 * @param function  Function to remove.
 */
void
TeamDebugInfo::_RemoveFunction(Function* function)
{
	fFunctions->Remove(function);

	// If the function refers to a source file, remove it from the respective
	// entry.
	if (LocatableFile* sourceFile = function->SourceFile()) {
		if (SourceFileEntry* entry = fSourceFiles->Lookup(sourceFile))
			entry->RemoveFunction(function);
	}
}
