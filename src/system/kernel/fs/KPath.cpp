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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2004-2008, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2008-2017, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file KPath.cpp
 * @brief Fixed-buffer path string helper for use in kernel VFS code.
 *
 * KPath wraps a fixed-size kernel buffer and provides path manipulation
 * helpers: appending components, getting/setting the leaf name, locking
 * the buffer for direct use, and querying the current path length.
 *
 * @see vfs.cpp
 */


#include <fs/KPath.h>

#include <stdlib.h>
#include <string.h>

#include <team.h>
#include <vfs.h>
#include <slab/Slab.h>


// debugging
#define TRACE(x) ;
//#define TRACE(x) dprintf x


#ifdef _KERNEL_MODE
extern object_cache* sPathNameCache;
#endif


/** @brief Constructs a KPath with an empty path and a caller-specified buffer size. */
KPath::KPath(size_t bufferSize)
	:
	fBuffer(NULL),
	fBufferSize(0),
	fPathLength(0),
	fLocked(false),
	fLazy(false),
	fFailed(false),
	fIsNull(false)
{
	SetTo(NULL, DEFAULT, bufferSize);
}


/** @brief Constructs a KPath initialised to @a path with the given flags and buffer size. */
KPath::KPath(const char* path, int32 flags, size_t bufferSize)
	:
	fBuffer(NULL),
	fBufferSize(0),
	fPathLength(0),
	fLocked(false),
	fLazy(false),
	fFailed(false),
	fIsNull(false)
{
	SetTo(path, flags, bufferSize);
}


/** @brief Copy-constructs a KPath as a deep copy of @a other. */
KPath::KPath(const KPath& other)
	:
	fBuffer(NULL),
	fBufferSize(0),
	fPathLength(0),
	fLocked(false),
	fLazy(false),
	fFailed(false),
	fIsNull(false)
{
	*this = other;
}


/** @brief Destructor — releases the internal path buffer. */
KPath::~KPath()
{
	_FreeBuffer();
}


/**
 * @brief Reinitialises this object to @a path with the specified flags and buffer size.
 *
 * If the buffer size differs from the current one the old buffer is freed and a new
 * one is allocated. Passing @c NULL for @a path clears the stored path.
 */
status_t
KPath::SetTo(const char* path, int32 flags, size_t bufferSize)
{
	if (bufferSize == 0)
		bufferSize = B_PATH_NAME_LENGTH;

	// free the previous buffer, if the buffer size differs
	if (fBuffer != NULL && fBufferSize != bufferSize) {
		_FreeBuffer();
		fBufferSize = 0;
	}

	fPathLength = 0;
	fLocked = false;
	fBufferSize = bufferSize;
	fLazy = (flags & LAZY_ALLOC) != 0;
	fIsNull = path == NULL;

	if (path != NULL || !fLazy) {
		status_t status = _AllocateBuffer();
		if (status != B_OK)
			return status;
	}

	return SetPath(path, flags);
}


/**
 * @brief Takes ownership of the buffer held by @a other, leaving @a other empty.
 *
 * More efficient than copying when the caller no longer needs the source object.
 */
void
KPath::Adopt(KPath& other)
{
	_FreeBuffer();

	fBuffer = other.fBuffer;
	fBufferSize = other.fBufferSize;
	fPathLength = other.fPathLength;
	fLazy = other.fLazy;
	fFailed = other.fFailed;
	fIsNull = other.fIsNull;

	other.fBuffer = NULL;
	if (!other.fLazy)
		other.fBufferSize = 0;
	other.fPathLength = 0;
	other.fFailed = false;
	other.fIsNull = other.fLazy;
}


/**
 * @brief Returns @c B_OK if the object is properly initialised, an error otherwise.
 *
 * Returns @c B_NO_MEMORY if a previous allocation failed, or @c B_NO_INIT if the
 * object has never been initialised.
 */
status_t
KPath::InitCheck() const
{
	if (fBuffer != NULL || (fLazy && !fFailed && fBufferSize != 0))
		return B_OK;

	return fFailed ? B_NO_MEMORY : B_NO_INIT;
}


/**
 * @brief Sets the stored path string to @a path, optionally normalising it.
 *
 * @param path   The path to store, or @c NULL to clear the buffer.
 * @param flags  Bitfield; understands @c NORMALIZE and @c TRAVERSE_LEAF_LINK.
 * @return @c B_OK on success, @c B_BUFFER_OVERFLOW if the path is too long, or
 *         another error code if normalisation fails.
 */
status_t
KPath::SetPath(const char* path, int32 flags)
{
	if (path == NULL && fLazy && fBuffer == NULL) {
		fIsNull = true;
		return B_OK;
	}

	if (fBuffer == NULL) {
		if (fLazy) {
			status_t status = _AllocateBuffer();
			if (status != B_OK)
				return B_NO_MEMORY;
		} else
			return B_NO_INIT;
	}

	fIsNull = false;

	if (path != NULL) {
		if ((flags & NORMALIZE) != 0) {
			// normalize path
			status_t status = _Normalize(path,
				(flags & TRAVERSE_LEAF_LINK) != 0);
			if (status != B_OK)
				return status;
		} else {
			// don't normalize path
			size_t length = strlen(path);
			if (length >= fBufferSize)
				return B_BUFFER_OVERFLOW;

			memcpy(fBuffer, path, length + 1);
			fPathLength = length;
			_ChopTrailingSlashes();
		}
	} else {
		fBuffer[0] = '\0';
		fPathLength = 0;
		if (fLazy)
			fIsNull = true;
	}
	return B_OK;
}


/**
 * @brief Returns a pointer to the stored path string, or @c NULL if the object
 *        was initialised with a null path.
 */
const char*
KPath::Path() const
{
	return fIsNull ? NULL : fBuffer;
}


/**
 * @brief Locks the internal buffer for direct external modification.
 *
 * The caller may write directly into the returned pointer up to BufferSize()
 * bytes. Call UnlockBuffer() once the modifications are complete so the stored
 * path length is recalculated.
 *
 * @param force  When @c true and the object is in lazy mode, allocate a buffer
 *               even if the path is currently null.
 * @return Pointer to the writable buffer, or @c NULL if already locked or not
 *         initialised.
 */
char*
KPath::LockBuffer(bool force)
{
	if (fBuffer == NULL && fLazy) {
		if (fIsNull && !force)
			return NULL;

		_AllocateBuffer();
	}

	if (fBuffer == NULL || fLocked)
		return NULL;

	fLocked = true;
	fIsNull = false;

	return fBuffer;
}


/**
 * @brief Unlocks the buffer after direct external modification, recomputing the
 *        stored path length and trimming any trailing slashes.
 *
 * Panics (in kernel mode) if the buffer was not previously locked.
 */
void
KPath::UnlockBuffer()
{
	if (!fLocked) {
#ifdef _KERNEL_MODE
		panic("KPath::UnlockBuffer(): Buffer not locked!");
#endif
		return;
	}

	fLocked = false;

	if (fBuffer == NULL)
		return;

	fPathLength = strnlen(fBuffer, fBufferSize);
	if (fPathLength == fBufferSize) {
		TRACE(("KPath::UnlockBuffer(): WARNING: Unterminated buffer!\n"));
		fPathLength--;
		fBuffer[fPathLength] = '\0';
	}
	_ChopTrailingSlashes();
}


/**
 * @brief Detaches and returns the underlying buffer, transferring ownership to
 *        the caller.
 *
 * In kernel mode the slab-allocated buffer is copied into a plain malloc block
 * first so the caller can free() it safely.
 */
char*
KPath::DetachBuffer()
{
	char* buffer = fBuffer;

#ifdef _KERNEL_MODE
	if (fBufferSize == B_PATH_NAME_LENGTH) {
		buffer = (char*)malloc(fBufferSize);
		memcpy(buffer, fBuffer, fBufferSize);
		_FreeBuffer();
	}
#endif

	if (fBuffer != NULL) {
		fBuffer = NULL;
		fPathLength = 0;
		fLocked = false;
	}

	return buffer;
}


/**
 * @brief Returns a pointer to the leaf (final component) of the stored path.
 *
 * Scans the buffer backwards for the last '/' separator. If none is found the
 * entire path is the leaf. Returns @c NULL when the buffer is uninitialised.
 */
const char*
KPath::Leaf() const
{
	if (fBuffer == NULL)
		return NULL;

	for (int32 i = fPathLength - 1; i >= 0; i--) {
		if (fBuffer[i] == '/')
			return fBuffer + i + 1;
	}

	return fBuffer;
}


/**
 * @brief Replaces the leaf component of the stored path with @a newLeaf.
 *
 * Strips the existing leaf from the buffer and, if @a newLeaf is non-null,
 * appends it via Append(). Passing @c NULL just removes the current leaf.
 */
status_t
KPath::ReplaceLeaf(const char* newLeaf)
{
	const char* leaf = Leaf();
	if (leaf == NULL)
		return B_NO_INIT;

	int32 leafIndex = leaf - fBuffer;
	// chop off the current leaf (don't replace "/", though)
	if (leafIndex != 0 || fBuffer[leafIndex - 1]) {
		fBuffer[leafIndex] = '\0';
		fPathLength = leafIndex;
		_ChopTrailingSlashes();
	}

	// if a leaf was given, append it
	if (newLeaf != NULL)
		return Append(newLeaf);
	return B_OK;
}


/**
 * @brief Removes the leaf component from the stored path, exposing the parent
 *        directory.
 *
 * @return @c true on success, @c false when the object is uninitialised or the
 *         path cannot be shortened further (e.g. already at the root).
 */
bool
KPath::RemoveLeaf()
{
	// get the leaf -- bail out, if not initialized or only the "/" is left
	const char* leaf = Leaf();
	if (leaf == NULL || leaf == fBuffer || leaf[0] == '\0')
		return false;

	// chop off the leaf
	int32 leafIndex = leaf - fBuffer;
	fBuffer[leafIndex] = '\0';
	fPathLength = leafIndex;
	_ChopTrailingSlashes();

	return true;
}


/**
 * @brief Appends @a component to the stored path.
 *
 * When @a isComponent is @c true a '/' separator is inserted automatically if
 * neither the current path ends with one nor @a component begins with one.
 *
 * @return @c B_OK on success, @c B_BUFFER_OVERFLOW if the result would exceed
 *         the buffer, or @c B_BAD_VALUE / @c B_NO_INIT on invalid state.
 */
status_t
KPath::Append(const char* component, bool isComponent)
{
	// check initialization and parameter
	if (fBuffer == NULL)
		return B_NO_INIT;
	if (component == NULL)
		return B_BAD_VALUE;
	if (fPathLength == 0)
		return SetPath(component);

	// get component length
	size_t componentLength = strlen(component);
	if (componentLength < 1)
		return B_OK;

	// if our current path is empty, we just copy the supplied one
	// compute the result path len
	bool insertSlash = isComponent && fBuffer[fPathLength - 1] != '/'
		&& component[0] != '/';
	size_t resultPathLength = fPathLength + componentLength
		+ (insertSlash ? 1 : 0);
	if (resultPathLength >= fBufferSize)
		return B_BUFFER_OVERFLOW;

	// compose the result path
	if (insertSlash)
		fBuffer[fPathLength++] = '/';
	memcpy(fBuffer + fPathLength, component, componentLength + 1);
	fPathLength = resultPathLength;
	return B_OK;
}


/**
 * @brief Normalises the stored path in-place by resolving "." and ".." components
 *        and, optionally, traversing a leaf symlink.
 *
 * @param traverseLeafLink  When @c true, a symlink at the final path component
 *                          is resolved as well.
 */
status_t
KPath::Normalize(bool traverseLeafLink)
{
	if (fBuffer == NULL)
		return B_NO_INIT;
	if (fPathLength == 0)
		return B_BAD_VALUE;

	return _Normalize(fBuffer, traverseLeafLink);
}


/**
 * @brief Copy-assignment operator — performs a deep copy of @a other into this
 *        object, reallocating the buffer if necessary.
 */
KPath&
KPath::operator=(const KPath& other)
{
	if (other.fBuffer == fBuffer)
		return *this;

	SetTo(other.fBuffer, fLazy ? KPath::LAZY_ALLOC : KPath::DEFAULT,
		other.fBufferSize);
	return *this;
}


/**
 * @brief Assigns a plain C string to this object by calling SetPath().
 */
KPath&
KPath::operator=(const char* path)
{
	SetPath(path);
	return *this;
}


/**
 * @brief Returns @c true when this object's path is equal to @a other's path.
 */
bool
KPath::operator==(const KPath& other) const
{
	if (fBuffer == NULL)
		return !other.fBuffer;

	return other.fBuffer != NULL
		&& fPathLength == other.fPathLength
		&& strcmp(fBuffer, other.fBuffer) == 0;
}


/**
 * @brief Returns @c true when the stored path equals the plain C string @a path.
 */
bool
KPath::operator==(const char* path) const
{
	if (fBuffer == NULL)
		return path == NULL;

	return path != NULL && strcmp(fBuffer, path) == 0;
}


/**
 * @brief Returns @c true when this object's path differs from @a other's path.
 */
bool
KPath::operator!=(const KPath& other) const
{
	return !(*this == other);
}


/**
 * @brief Returns @c true when the stored path differs from the plain C string
 *        @a path.
 */
bool
KPath::operator!=(const char* path) const
{
	return !(*this == path);
}


/** @brief Allocates the internal path buffer, using the slab allocator in kernel
 *         mode when the buffer size equals @c B_PATH_NAME_LENGTH. */
status_t
KPath::_AllocateBuffer()
{
	if (fBuffer == NULL && fBufferSize != 0) {
#ifdef _KERNEL_MODE
		if (fBufferSize == B_PATH_NAME_LENGTH)
			fBuffer = (char*)object_cache_alloc(sPathNameCache, 0);
		else
#endif
			fBuffer = (char*)malloc(fBufferSize);
	}
	if (fBuffer == NULL) {
		fFailed = true;
		return B_NO_MEMORY;
	}

	fBuffer[0] = '\0';
	fFailed = false;
	return B_OK;
}


/** @brief Releases the internal path buffer back to the slab or heap as
 *         appropriate, and sets fBuffer to @c NULL. */
void
KPath::_FreeBuffer()
{
#ifdef _KERNEL_MODE
	if (fBufferSize == B_PATH_NAME_LENGTH)
		object_cache_free(sPathNameCache, fBuffer, 0);
	else
#endif
		free(fBuffer);
	fBuffer = NULL;
}


/** @brief Calls vfs_normalize_path() to resolve the given @a path into the
 *         internal buffer, updating fPathLength on success. */
status_t
KPath::_Normalize(const char* path, bool traverseLeafLink)
{
	status_t error = vfs_normalize_path(path, fBuffer, fBufferSize,
		traverseLeafLink,
		team_get_kernel_team_id() == team_get_current_team_id());
	if (error != B_OK) {
		// vfs_normalize_path() might have screwed up the previous
		// path -- unset it completely to avoid weird problems.
		fBuffer[0] = '\0';
		fPathLength = 0;
		return error;
	}

	fPathLength = strlen(fBuffer);
	return B_OK;
}


/** @brief Removes any trailing '/' characters from the buffer, leaving at most
 *         a single '/' for the root path. */
void
KPath::_ChopTrailingSlashes()
{
	if (fBuffer != NULL) {
		while (fPathLength > 1 && fBuffer[fPathLength - 1] == '/')
			fBuffer[--fPathLength] = '\0';
	}
}
