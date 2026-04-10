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
 *   Copyright 2013, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file PathFinder.cpp
 * @brief Implementation of the BPathFinder class for locating standard
 *        filesystem directories.
 *
 * BPathFinder provides a convenient interface for resolving well-known base
 * directories (such as data, settings, or add-on directories) relative to a
 * given path, entry reference, or code pointer. It wraps the lower-level
 * find_path_etc() and find_paths_etc() C functions and returns results as
 * BPath or BStringList objects. The package-kit-specific portions of this
 * class (BResolvableExpression constructor and SetTo()) are implemented
 * separately in the package kit.
 *
 * @see BPath, BStringList
 */

#include <PathFinder.h>

#include <AutoDeleter.h>
#include <FindDirectory.h>
#include <Path.h>
#include <StringList.h>


// NOTE: The package kit specific part of BPathFinder (BResolvableExpression
// constructor and SetTo()) is implemented in the package kit.


/**
 * @brief Constructs a BPathFinder anchored to the image that contains the
 *        given code pointer.
 *
 * @param codePointer A pointer to a symbol within the image to use as anchor.
 * @param dependency  Optional dependency name used for package-relative
 *                    resolution; may be NULL.
 */
BPathFinder::BPathFinder(const void* codePointer, const char* dependency)
{
	_SetTo(codePointer, NULL, dependency);
}


/**
 * @brief Constructs a BPathFinder anchored to the given filesystem path.
 *
 * @param path       The filesystem path string used as the resolution anchor.
 * @param dependency Optional dependency name; may be NULL.
 */
BPathFinder::BPathFinder(const char* path, const char* dependency)
{
	_SetTo(NULL, path, dependency);
}


/**
 * @brief Constructs a BPathFinder anchored to the entry identified by the
 *        given entry_ref.
 *
 * @param ref        The entry_ref identifying the anchor filesystem entry.
 * @param dependency Optional dependency name; may be NULL.
 */
BPathFinder::BPathFinder(const entry_ref& ref, const char* dependency)
{
	SetTo(ref, dependency);
}


/**
 * @brief Reinitializes the object to use the image that contains the given
 *        code pointer as its resolution anchor.
 *
 * @param codePointer A pointer to a symbol within the image to use as anchor.
 * @param dependency  Optional dependency name; may be NULL.
 * @return B_OK on success, or B_NO_MEMORY if internal string allocation fails.
 */
status_t
BPathFinder::SetTo(const void* codePointer, const char* dependency)
{
	return _SetTo(codePointer, NULL, dependency);
}


/**
 * @brief Reinitializes the object to use the given filesystem path as its
 *        resolution anchor.
 *
 * @param path       The filesystem path string used as the resolution anchor.
 * @param dependency Optional dependency name; may be NULL.
 * @return B_OK on success, or B_NO_MEMORY if internal string allocation fails.
 */
status_t
BPathFinder::SetTo(const char* path, const char* dependency)
{
	return _SetTo(NULL, path, dependency);
}


/**
 * @brief Reinitializes the object to use the entry identified by the given
 *        entry_ref as its resolution anchor.
 *
 * @param ref        The entry_ref identifying the anchor filesystem entry.
 * @param dependency Optional dependency name; may be NULL.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPathFinder::SetTo(const entry_ref& ref, const char* dependency)
{
	BPath path;
	fInitStatus = path.SetTo(&ref);
	if (fInitStatus != B_OK)
		return fInitStatus;

	return _SetTo(NULL, path.Path(), dependency);
}


/**
 * @brief Finds a single path for the specified base directory, architecture,
 *        and optional sub-path.
 *
 * Uses find_path_for_path_etc() when an anchor path is set, or
 * find_path_etc() when anchored to a code pointer.
 *
 * @param architecture  The architecture string (e.g. "x86_64"), or NULL for
 *                      the current architecture.
 * @param baseDirectory The well-known base directory constant to resolve.
 * @param subPath       Optional sub-path to append to the resolved base; may
 *                      be NULL.
 * @param flags         Flags modifying the lookup behavior.
 * @param _path         Output BPath that receives the resolved path.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPathFinder::FindPath(const char* architecture,
	path_base_directory baseDirectory, const char* subPath, uint32 flags,
	BPath& _path)
{
	_path.Unset();

	if (fInitStatus != B_OK)
		return fInitStatus;

	const char* dependency = fDependency.IsEmpty()
		? NULL : fDependency.String();

	char pathBuffer[B_PATH_NAME_LENGTH];
	status_t error;

	if (!fPath.IsEmpty()) {
		error = find_path_for_path_etc(fPath, dependency, architecture,
			baseDirectory, subPath, flags, pathBuffer, sizeof(pathBuffer));
	} else {
		error = find_path_etc(fCodePointer, dependency, architecture,
			baseDirectory, subPath, flags, pathBuffer, sizeof(pathBuffer));
	}

	if (error != B_OK)
		return error;

	return _path.SetTo(pathBuffer);
}


/**
 * @brief Finds a single path for the specified base directory and optional
 *        sub-path, using the current architecture.
 *
 * @param baseDirectory The well-known base directory constant to resolve.
 * @param subPath       Optional sub-path to append; may be NULL.
 * @param flags         Flags modifying the lookup behavior.
 * @param _path         Output BPath that receives the resolved path.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPathFinder::FindPath(path_base_directory baseDirectory, const char* subPath,
	uint32 flags, BPath& _path)
{
	return FindPath(NULL, baseDirectory, subPath, flags, _path);
}


/**
 * @brief Finds a single path for the specified base directory and optional
 *        sub-path, with no flags and the current architecture.
 *
 * @param baseDirectory The well-known base directory constant to resolve.
 * @param subPath       Optional sub-path to append; may be NULL.
 * @param _path         Output BPath that receives the resolved path.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPathFinder::FindPath(path_base_directory baseDirectory, const char* subPath,
	BPath& _path)
{
	return FindPath(NULL, baseDirectory, subPath, 0, _path);
}


/**
 * @brief Finds a single path for the specified base directory with no
 *        sub-path, no flags, and the current architecture.
 *
 * @param baseDirectory The well-known base directory constant to resolve.
 * @param _path         Output BPath that receives the resolved path.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPathFinder::FindPath(path_base_directory baseDirectory, BPath& _path)
{
	return FindPath(NULL, baseDirectory, NULL, 0, _path);
}


/**
 * @brief Finds all paths for the specified base directory, architecture, and
 *        optional sub-path across all installation locations.
 *
 * @param architecture  The architecture string, or NULL for the current one.
 * @param baseDirectory The well-known base directory constant to resolve.
 * @param subPath       Optional sub-path to append; may be NULL.
 * @param flags         Flags modifying the lookup behavior.
 * @param _paths        Output BStringList that receives all resolved paths.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or another
 *         error code on failure.
 */
/*static*/ status_t
BPathFinder::FindPaths(const char* architecture,
	path_base_directory baseDirectory, const char* subPath, uint32 flags,
	BStringList& _paths)
{
	_paths.MakeEmpty();

	// get the paths
	char** pathArray;
	size_t pathCount;
	status_t error = find_paths_etc(architecture, baseDirectory, subPath, flags,
		&pathArray, &pathCount);
	if (error != B_OK)
		return error;

	MemoryDeleter pathArrayDeleter(pathArray);

	// add them to BStringList
	for (size_t i = 0; i < pathCount; i++) {
		BString path(pathArray[i]);
		if (path.IsEmpty() || !_paths.Add(path)) {
			_paths.MakeEmpty();
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


/**
 * @brief Finds all paths for the specified base directory and optional
 *        sub-path, using the current architecture.
 *
 * @param baseDirectory The well-known base directory constant to resolve.
 * @param subPath       Optional sub-path to append; may be NULL.
 * @param flags         Flags modifying the lookup behavior.
 * @param _paths        Output BStringList that receives all resolved paths.
 * @return B_OK on success, or an error code on failure.
 */
/*static*/ status_t
BPathFinder::FindPaths(path_base_directory baseDirectory, const char* subPath,
	uint32 flags, BStringList& _paths)
{
	return FindPaths(NULL, baseDirectory, subPath, flags, _paths);
}


/**
 * @brief Finds all paths for the specified base directory and optional
 *        sub-path, with no flags and the current architecture.
 *
 * @param baseDirectory The well-known base directory constant to resolve.
 * @param subPath       Optional sub-path to append; may be NULL.
 * @param _paths        Output BStringList that receives all resolved paths.
 * @return B_OK on success, or an error code on failure.
 */
/*static*/ status_t
BPathFinder::FindPaths(path_base_directory baseDirectory, const char* subPath,
	BStringList& _paths)
{
	return FindPaths(NULL, baseDirectory, subPath, 0, _paths);
}


/**
 * @brief Finds all paths for the specified base directory with no sub-path,
 *        no flags, and the current architecture.
 *
 * @param baseDirectory The well-known base directory constant to resolve.
 * @param _paths        Output BStringList that receives all resolved paths.
 * @return B_OK on success, or an error code on failure.
 */
/*static*/ status_t
BPathFinder::FindPaths(path_base_directory baseDirectory, BStringList& _paths)
{
	return FindPaths(NULL, baseDirectory, NULL, 0, _paths);
}


/**
 * @brief Internal initialization helper that sets the code pointer, path,
 *        and dependency strings.
 *
 * @param codePointer A pointer to a symbol within the anchor image, or NULL.
 * @param path        The anchor filesystem path string, or NULL.
 * @param dependency  The dependency name string, or NULL.
 * @return B_OK on success, B_NO_MEMORY if string allocation fails.
 */
status_t
BPathFinder::_SetTo(const void* codePointer, const char* path,
	const char* dependency)
{
	fCodePointer = codePointer;
	fPath = path;
	fDependency = dependency;

	if ((path != NULL && path[0] != '\0' && fPath.IsEmpty())
		|| (dependency != NULL && dependency[0] != '\0'
			&& fDependency.IsEmpty())) {
		fInitStatus = B_NO_MEMORY;
	} else
		fInitStatus = B_OK;

	return fInitStatus;
}
