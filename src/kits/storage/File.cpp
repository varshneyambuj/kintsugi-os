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
 *   Copyright 2002-2009, Haiku Inc.
 *   Authors: Tyler Dauwalder, Ingo Weinhold, bonefish@users.sf.net
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file File.cpp
 * @brief Implements the BFile class for reading and writing regular files.
 *
 * BFile extends BNode with random-access read and write operations, open-mode
 * tracking, and seek/position support. It can be initialized from an entry_ref,
 * a BEntry, a path string, or a path relative to a BDirectory. The class
 * provides both sequential (Read/Write) and positional (ReadAt/WriteAt) I/O,
 * as well as file-size truncation via SetSize().
 *
 * @see BNode
 * @see BEntry
 * @see BStatable
 */

#include <fcntl.h>
#include <unistd.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <fs_interface.h>
#include <NodeMonitor.h>
#include "storage_support.h"

#include <syscalls.h>
#include <umask.h>


/**
 * @brief Constructs a default, uninitialized BFile.
 *
 * InitCheck() will return B_NO_INIT until a SetTo() overload is called
 * successfully.
 */
BFile::BFile()
	:
	fMode(0)
{
}


/**
 * @brief Copy constructor; duplicates the file descriptor from \a file.
 *
 * @param file The BFile to copy.
 */
BFile::BFile(const BFile& file)
	:
	fMode(0)
{
	*this = file;
}


/**
 * @brief Constructs a BFile and opens the file identified by \a ref.
 *
 * @param ref       Pointer to the entry_ref identifying the file.
 * @param openMode  Combination of O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, etc.
 */
BFile::BFile(const entry_ref* ref, uint32 openMode)
	:
	fMode(0)
{
	SetTo(ref, openMode);
}


/**
 * @brief Constructs a BFile and opens the file referred to by \a entry.
 *
 * @param entry     Pointer to the BEntry identifying the file.
 * @param openMode  Combination of O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, etc.
 */
BFile::BFile(const BEntry* entry, uint32 openMode)
	:
	fMode(0)
{
	SetTo(entry, openMode);
}


/**
 * @brief Constructs a BFile and opens the file at the given path.
 *
 * @param path      Absolute or relative filesystem path.
 * @param openMode  Combination of O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, etc.
 */
BFile::BFile(const char* path, uint32 openMode)
	:
	fMode(0)
{
	SetTo(path, openMode);
}


/**
 * @brief Constructs a BFile and opens the file at \a path relative to \a dir.
 *
 * @param dir       The directory from which \a path is resolved.
 * @param path      Path relative to \a dir.
 * @param openMode  Combination of O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, etc.
 */
BFile::BFile(const BDirectory *dir, const char* path, uint32 openMode)
	:
	fMode(0)
{
	SetTo(dir, path, openMode);
}


/**
 * @brief Destructor; closes the file descriptor and releases all resources.
 *
 * Explicitly calls close_fd() rather than relying solely on BNode's destructor
 * to avoid virtual dispatch issues during destruction.
 */
BFile::~BFile()
{
	// Also called by the BNode destructor, but we rather try to avoid
	// problems with calling virtual functions in the base class destructor.
	// Depending on the compiler implementation an object may be degraded to
	// an object of the base class after the destructor of the derived class
	// has been executed.
	close_fd();
}


/**
 * @brief (Re-)initializes the BFile to the file identified by \a ref.
 *
 * If ref->name is an absolute path, the path-only overload is used instead.
 *
 * @param ref       Pointer to the entry_ref identifying the file.
 * @param openMode  Combination of open-mode flags (O_RDONLY, O_WRONLY, etc.).
 * @return B_OK on success, B_BAD_VALUE if \a ref is NULL, or another error code.
 */
status_t
BFile::SetTo(const entry_ref* ref, uint32 openMode)
{
	Unset();

	if (!ref)
		return (fCStatus = B_BAD_VALUE);

	// if ref->name is absolute, let the path-only SetTo() do the job
	if (BPrivate::Storage::is_absolute_path(ref->name))
		return SetTo(ref->name, openMode);

	openMode |= O_CLOEXEC;

	int fd = _kern_open_entry_ref(ref->device, ref->directory, ref->name,
		openMode, DEFFILEMODE & ~__gUmask);
	if (fd >= 0) {
		set_fd(fd);
		fMode = openMode;
		fCStatus = B_OK;
	} else
		fCStatus = fd;

	return fCStatus;
}


/**
 * @brief (Re-)initializes the BFile to the file referred to by \a entry.
 *
 * @param entry     Pointer to the BEntry identifying the file.
 * @param openMode  Combination of open-mode flags.
 * @return B_OK on success, B_BAD_VALUE if \a entry is NULL or uninitialized,
 *         or another error code.
 */
status_t
BFile::SetTo(const BEntry* entry, uint32 openMode)
{
	Unset();

	if (!entry)
		return (fCStatus = B_BAD_VALUE);
	if (entry->InitCheck() != B_OK)
		return (fCStatus = entry->InitCheck());

	openMode |= O_CLOEXEC;

	int fd = _kern_open(entry->fDirFd, entry->fName, openMode | O_CLOEXEC,
		DEFFILEMODE & ~__gUmask);
	if (fd >= 0) {
		set_fd(fd);
		fMode = openMode;
		fCStatus = B_OK;
	} else
		fCStatus = fd;

	return fCStatus;
}


/**
 * @brief (Re-)initializes the BFile to the file at the given path.
 *
 * @param path      Absolute or relative filesystem path.
 * @param openMode  Combination of open-mode flags.
 * @return B_OK on success, B_BAD_VALUE if \a path is NULL, or another error code.
 */
status_t
BFile::SetTo(const char* path, uint32 openMode)
{
	Unset();

	if (!path)
		return (fCStatus = B_BAD_VALUE);

	openMode |= O_CLOEXEC;

	int fd = _kern_open(AT_FDCWD, path, openMode, DEFFILEMODE & ~__gUmask);
	if (fd >= 0) {
		set_fd(fd);
		fMode = openMode;
		fCStatus = B_OK;
	} else
		fCStatus = fd;

	return fCStatus;
}


/**
 * @brief (Re-)initializes the BFile to the file at \a path relative to \a dir.
 *
 * @param dir       The directory from which \a path is resolved.
 * @param path      Path relative to \a dir.
 * @param openMode  Combination of open-mode flags.
 * @return B_OK on success, B_BAD_VALUE if \a dir is NULL, or another error code.
 */
status_t
BFile::SetTo(const BDirectory* dir, const char* path, uint32 openMode)
{
	Unset();

	if (!dir)
		return (fCStatus = B_BAD_VALUE);

	openMode |= O_CLOEXEC;

	int fd = _kern_open(dir->fDirFd, path, openMode, DEFFILEMODE & ~__gUmask);
	if (fd >= 0) {
		set_fd(fd);
		fMode = openMode;
		fCStatus = B_OK;
	} else
		fCStatus = fd;

	return fCStatus;
}


/**
 * @brief Returns whether the file was opened with read access.
 *
 * @return \c true if the file is properly initialized and was opened with
 *         O_RDONLY or O_RDWR, \c false otherwise.
 */
bool
BFile::IsReadable() const
{
	return InitCheck() == B_OK
		&& ((fMode & O_RWMASK) == O_RDONLY || (fMode & O_RWMASK) == O_RDWR);
}


/**
 * @brief Returns whether the file was opened with write access.
 *
 * @return \c true if the file is properly initialized and was opened with
 *         O_WRONLY or O_RDWR, \c false otherwise.
 */
bool
BFile::IsWritable() const
{
	return InitCheck() == B_OK
		&& ((fMode & O_RWMASK) == O_WRONLY || (fMode & O_RWMASK) == O_RDWR);
}


/**
 * @brief Reads up to \a size bytes from the current file position into \a buffer.
 *
 * Advances the file position by the number of bytes read.
 *
 * @param buffer  Destination buffer for the data.
 * @param size    Maximum number of bytes to read.
 * @return The number of bytes actually read on success, or a negative error
 *         code (including the InitCheck() error) on failure.
 */
ssize_t
BFile::Read(void* buffer, size_t size)
{
	if (InitCheck() != B_OK)
		return InitCheck();
	return _kern_read(get_fd(), -1, buffer, size);
}


/**
 * @brief Reads up to \a size bytes from \a location within the file into \a buffer.
 *
 * The file's current position is not modified.
 *
 * @param location  Byte offset from the start of the file.
 * @param buffer    Destination buffer for the data.
 * @param size      Maximum number of bytes to read.
 * @return The number of bytes actually read on success, B_BAD_VALUE if
 *         \a location is negative, or a negative error code on failure.
 */
ssize_t
BFile::ReadAt(off_t location, void* buffer, size_t size)
{
	if (InitCheck() != B_OK)
		return InitCheck();
	if (location < 0)
		return B_BAD_VALUE;

	return _kern_read(get_fd(), location, buffer, size);
}


/**
 * @brief Writes \a size bytes from \a buffer at the current file position.
 *
 * Advances the file position by the number of bytes written.
 *
 * @param buffer  Source buffer containing the data to write.
 * @param size    Number of bytes to write.
 * @return The number of bytes actually written on success, or a negative
 *         error code on failure.
 */
ssize_t
BFile::Write(const void* buffer, size_t size)
{
	if (InitCheck() != B_OK)
		return InitCheck();
	return _kern_write(get_fd(), -1, buffer, size);
}


/**
 * @brief Writes \a size bytes from \a buffer at the specified \a location.
 *
 * The file's current position is not modified.
 *
 * @param location  Byte offset from the start of the file.
 * @param buffer    Source buffer containing the data to write.
 * @param size      Number of bytes to write.
 * @return The number of bytes actually written on success, B_BAD_VALUE if
 *         \a location is negative, or a negative error code on failure.
 */
ssize_t
BFile::WriteAt(off_t location, const void* buffer, size_t size)
{
	if (InitCheck() != B_OK)
		return InitCheck();
	if (location < 0)
		return B_BAD_VALUE;

	return _kern_write(get_fd(), location, buffer, size);
}


/**
 * @brief Moves the read/write position to a new location within the file.
 *
 * @param offset    Byte offset to apply relative to \a seekMode.
 * @param seekMode  One of SEEK_SET, SEEK_CUR, or SEEK_END.
 * @return The new file position on success, or B_FILE_ERROR if not initialized.
 */
off_t
BFile::Seek(off_t offset, uint32 seekMode)
{
	if (InitCheck() != B_OK)
		return B_FILE_ERROR;
	return _kern_seek(get_fd(), offset, seekMode);
}


/**
 * @brief Returns the current read/write position within the file.
 *
 * @return The current byte offset from the beginning of the file, or
 *         B_FILE_ERROR if the BFile is not properly initialized.
 */
off_t
BFile::Position() const
{
	if (InitCheck() != B_OK)
		return B_FILE_ERROR;
	return _kern_seek(get_fd(), 0, SEEK_CUR);
}


/**
 * @brief Truncates or extends the file to exactly \a size bytes.
 *
 * If the file is extended, the new bytes read as zero. The file must have
 * been opened with write access.
 *
 * @param size  The desired file size in bytes; must be non-negative.
 * @return B_OK on success, B_BAD_VALUE if \a size is negative, or another
 *         error code if the operation fails.
 */
status_t
BFile::SetSize(off_t size)
{
	if (InitCheck() != B_OK)
		return InitCheck();
	if (size < 0)
		return B_BAD_VALUE;
	struct stat statData;
	statData.st_size = size;
	return set_stat(statData, B_STAT_SIZE | B_STAT_SIZE_INSECURE);
}


/**
 * @brief Returns the current size of the file's data.
 *
 * Delegates to BStatable::GetSize().
 *
 * @param size  Pointer to an off_t that receives the file size in bytes.
 * @return B_OK on success, or an error code from BStatable::GetSize().
 */
status_t
BFile::GetSize(off_t* size) const
{
	return BStatable::GetSize(size);
}


/**
 * @brief Assignment operator; duplicates the file descriptor from \a file.
 *
 * If \a file is not initialized or its FD cannot be duplicated, this object
 * is also left in an error state.
 *
 * @param file The BFile to assign from.
 * @return A reference to this BFile.
 */
BFile&
BFile::operator=(const BFile &file)
{
	if (&file != this) {
		// no need to assign us to ourselves
		Unset();
		if (file.InitCheck() == B_OK) {
			// duplicate the file descriptor
			int fd = _kern_dup(file.get_fd());
			// set it
			if (fd >= 0) {
				fFd = fd;
				fMode = file.fMode;
				fCStatus = B_OK;
			} else
				fCStatus = fd;
		}
	}
	return *this;
}


/** @brief Reserved virtual slot 1 (binary compatibility padding). */
void BFile::_PhiloFile1() {}
/** @brief Reserved virtual slot 2 (binary compatibility padding). */
void BFile::_PhiloFile2() {}
/** @brief Reserved virtual slot 3 (binary compatibility padding). */
void BFile::_PhiloFile3() {}
/** @brief Reserved virtual slot 4 (binary compatibility padding). */
void BFile::_PhiloFile4() {}
/** @brief Reserved virtual slot 5 (binary compatibility padding). */
void BFile::_PhiloFile5() {}
/** @brief Reserved virtual slot 6 (binary compatibility padding). */
void BFile::_PhiloFile6() {}


/*!	Gets the file descriptor of the BFile.

	To be used instead of accessing the BNode's private \c fFd member directly.

	\returns The file descriptor, or -1 if not properly initialized.
*/
/**
 * @brief Returns the underlying file descriptor.
 *
 * Use this accessor rather than reading the BNode private member fFd directly.
 *
 * @return The open file descriptor, or -1 if the BFile is not properly initialized.
 */
int
BFile::get_fd() const
{
	return fFd;
}


/**
 * @brief Overrides BNode::close_fd() for binary compatibility with BeOS R5.
 *
 * Delegates immediately to BNode::close_fd(); the override exists solely to
 * preserve the correct vtable slot on legacy ABIs.
 */
void
BFile::close_fd()
{
	BNode::close_fd();
}
