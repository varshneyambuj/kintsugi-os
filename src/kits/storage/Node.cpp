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
 *   Copyright 2002-2011 Haiku, Inc. All rights reserved.
 *   Authors: Tyler Dauwalder, Ingo Weinhold (bonefish@users.sf.net)
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file Node.cpp
 * @brief Implementation of node_ref and BNode for filesystem node access.
 *
 * This file implements node_ref, a lightweight structure identifying a
 * filesystem node by device and inode number, and BNode, the base class for
 * all filesystem objects (files, directories, symbolic links). BNode provides
 * attribute I/O, stat access, locking, and file-descriptor management used by
 * its subclasses.
 *
 * @see BNode
 */

#include <Node.h>

#include <errno.h>
#include <fcntl.h>
#include <new>
#include <string.h>
#include <unistd.h>

#include <compat/sys/stat.h>

#include <Directory.h>
#include <Entry.h>
#include <fs_attr.h>
#include <String.h>
#include <TypeConstants.h>

#include <syscalls.h>

#include "storage_support.h"


//	#pragma mark - node_ref


/**
 * @brief Default constructor; initializes device and node to invalid sentinel values.
 */
node_ref::node_ref()
	:
	device((dev_t)-1),
	node((ino_t)-1)
{
}


/**
 * @brief Constructs a node_ref with the given device and inode number.
 *
 * @param device The device ID of the filesystem.
 * @param node   The inode number of the node on that device.
 */
node_ref::node_ref(dev_t device, ino_t node)
	:
	device(device),
	node(node)
{
}


/**
 * @brief Copy constructor; initializes this node_ref as a copy of other.
 *
 * @param other The node_ref to copy.
 */
node_ref::node_ref(const node_ref& other)
	:
	device((dev_t)-1),
	node((ino_t)-1)
{
	*this = other;
}


/**
 * @brief Equality operator; compares device and node fields.
 *
 * @param other The node_ref to compare against.
 * @return true if both device and node match, false otherwise.
 */
bool
node_ref::operator==(const node_ref& other) const
{
	return (device == other.device && node == other.node);
}


/**
 * @brief Inequality operator.
 *
 * @param other The node_ref to compare against.
 * @return true if device or node differ, false otherwise.
 */
bool
node_ref::operator!=(const node_ref& other) const
{
	return !(*this == other);
}


/**
 * @brief Less-than operator for ordering; compares by device then by node.
 *
 * @param other The node_ref to compare against.
 * @return true if this node_ref is ordered before other.
 */
bool
node_ref::operator<(const node_ref& other) const
{
	if (this->device != other.device)
		return this->device < other.device;

	return this->node < other.node;
}


/**
 * @brief Assignment operator; copies device and node from other.
 *
 * @param other The node_ref to assign from.
 * @return A reference to this node_ref.
 */
node_ref&
node_ref::operator=(const node_ref& other)
{
	device = other.device;
	node = other.node;
	return *this;
}


//	#pragma mark - BNode


/**
 * @brief Default constructor; creates an uninitialized BNode.
 */
BNode::BNode()
	:
	fFd(-1),
	fAttrFd(-1),
	fCStatus(B_NO_INIT)
{
}


/**
 * @brief Constructs a BNode and initializes it to the entry identified by ref.
 *
 * @param ref An entry_ref identifying the node to open.
 */
BNode::BNode(const entry_ref* ref)
	:
	fFd(-1),
	fAttrFd(-1),
	fCStatus(B_NO_INIT)
{
	// fCStatus is set by SetTo(), ignore return value
	(void)SetTo(ref);
}


/**
 * @brief Constructs a BNode and initializes it to the node pointed to by entry.
 *
 * @param entry A BEntry identifying the node to open.
 */
BNode::BNode(const BEntry* entry)
	:
	fFd(-1),
	fAttrFd(-1),
	fCStatus(B_NO_INIT)
{
	// fCStatus is set by SetTo(), ignore return value
	(void)SetTo(entry);
}


/**
 * @brief Constructs a BNode and initializes it to the node at the given path.
 *
 * @param path A null-terminated string specifying the filesystem path.
 */
BNode::BNode(const char* path)
	:
	fFd(-1),
	fAttrFd(-1),
	fCStatus(B_NO_INIT)
{
	// fCStatus is set by SetTo(), ignore return value
	(void)SetTo(path);
}


/**
 * @brief Constructs a BNode and initializes it to a path relative to a directory.
 *
 * @param dir  The base directory for the relative path.
 * @param path A relative path within dir.
 */
BNode::BNode(const BDirectory* dir, const char* path)
	:
	fFd(-1),
	fAttrFd(-1),
	fCStatus(B_NO_INIT)
{
	// fCStatus is set by SetTo(), ignore return value
	(void)SetTo(dir, path);
}


/**
 * @brief Copy constructor; duplicates the file descriptor of node.
 *
 * @param node The BNode to copy.
 */
BNode::BNode(const BNode& node)
	:
	fFd(-1),
	fAttrFd(-1),
	fCStatus(B_NO_INIT)
{
	*this = node;
}


/**
 * @brief Destructor; releases all resources held by this BNode.
 */
BNode::~BNode()
{
	Unset();
}


/**
 * @brief Returns the initialization status of this BNode.
 *
 * @return B_OK if initialized, B_NO_INIT or another error otherwise.
 */
status_t
BNode::InitCheck() const
{
	return fCStatus;
}


/**
 * @brief Initializes the BNode to the entry identified by ref.
 *
 * @param ref An entry_ref identifying the node to open.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BNode::SetTo(const entry_ref* ref)
{
	return _SetTo(ref, false);
}


/**
 * @brief Initializes the BNode to the node pointed to by entry.
 *
 * @param entry A BEntry identifying the node; must not be NULL.
 * @return B_OK on success, B_BAD_VALUE if entry is NULL, or another error code.
 */
status_t
BNode::SetTo(const BEntry* entry)
{
	if (entry == NULL) {
		Unset();
		return (fCStatus = B_BAD_VALUE);
	}

	return _SetTo(entry->fDirFd, entry->fName, false);
}


/**
 * @brief Initializes the BNode to the node at the given path.
 *
 * @param path A null-terminated string specifying the filesystem path.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BNode::SetTo(const char* path)
{
	return _SetTo(-1, path, false);
}


/**
 * @brief Initializes the BNode to a path relative to a directory.
 *
 * @param dir  The base directory; must not be NULL.
 * @param path A relative path within dir; must not be NULL or absolute.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BNode::SetTo(const BDirectory* dir, const char* path)
{
	if (dir == NULL || path == NULL
		|| BPrivate::Storage::is_absolute_path(path)) {
		Unset();
		return (fCStatus = B_BAD_VALUE);
	}

	return _SetTo(dir->fDirFd, path, false);
}


/**
 * @brief Closes all file descriptors and resets the node to an uninitialized state.
 */
void
BNode::Unset()
{
	close_fd();
	fCStatus = B_NO_INIT;
}


/**
 * @brief Acquires an advisory lock on the node.
 *
 * @return B_OK on success, or an error code if the node is not initialized or locking fails.
 */
status_t
BNode::Lock()
{
	if (fCStatus != B_OK)
		return fCStatus;

	return _kern_lock_node(fFd);
}


/**
 * @brief Releases a previously acquired advisory lock on the node.
 *
 * @return B_OK on success, or an error code if the node is not initialized or unlocking fails.
 */
status_t
BNode::Unlock()
{
	if (fCStatus != B_OK)
		return fCStatus;

	return _kern_unlock_node(fFd);
}


/**
 * @brief Flushes any pending writes to the node to persistent storage.
 *
 * @return B_OK on success, or B_FILE_ERROR if the node is not initialized.
 */
status_t
BNode::Sync()
{
	return (fCStatus != B_OK) ? B_FILE_ERROR : _kern_fsync(fFd, false);
}


/**
 * @brief Writes data to the named attribute of this node.
 *
 * @param attr   The attribute name.
 * @param type   The type code of the attribute data.
 * @param offset Byte offset within the attribute at which to begin writing.
 * @param buffer Pointer to the data to write.
 * @param length Number of bytes to write.
 * @return The number of bytes written on success, or a negative error code.
 */
ssize_t
BNode::WriteAttr(const char* attr, type_code type, off_t offset,
	const void* buffer, size_t length)
{
	if (fCStatus != B_OK)
		return B_FILE_ERROR;

	if (attr == NULL || buffer == NULL)
		return B_BAD_VALUE;

	ssize_t result = fs_write_attr(fFd, attr, type, offset, buffer, length);

	return result < 0 ? errno : result;
}


/**
 * @brief Reads data from the named attribute of this node.
 *
 * @param attr   The attribute name.
 * @param type   The expected type code of the attribute data.
 * @param offset Byte offset within the attribute at which to begin reading.
 * @param buffer Buffer to receive the data.
 * @param length Maximum number of bytes to read.
 * @return The number of bytes read on success, or a negative error code.
 */
ssize_t
BNode::ReadAttr(const char* attr, type_code type, off_t offset,
	void* buffer, size_t length) const
{
	if (fCStatus != B_OK)
		return B_FILE_ERROR;

	if (attr == NULL || buffer == NULL)
		return B_BAD_VALUE;

	ssize_t result = fs_read_attr(fFd, attr, type, offset, buffer, length);

	return result == -1 ? errno : result;
}


/**
 * @brief Removes the named attribute from this node.
 *
 * @param name The name of the attribute to remove.
 * @return B_OK on success, B_FILE_ERROR if not initialized, or another error code.
 */
status_t
BNode::RemoveAttr(const char* name)
{
	return fCStatus != B_OK ? B_FILE_ERROR : _kern_remove_attr(fFd, name);
}


/**
 * @brief Renames an attribute on this node.
 *
 * @param oldName The current name of the attribute.
 * @param newName The new name for the attribute.
 * @return B_OK on success, B_FILE_ERROR if not initialized, or another error code.
 */
status_t
BNode::RenameAttr(const char* oldName, const char* newName)
{
	if (fCStatus != B_OK)
		return B_FILE_ERROR;

	return _kern_rename_attr(fFd, oldName, fFd, newName);
}


/**
 * @brief Retrieves metadata about the named attribute.
 *
 * @param name The attribute name to query.
 * @param info Pointer to an attr_info structure to be filled in.
 * @return B_OK on success, B_FILE_ERROR if not initialized, or another error code.
 */
status_t
BNode::GetAttrInfo(const char* name, struct attr_info* info) const
{
	if (fCStatus != B_OK)
		return B_FILE_ERROR;

	if (name == NULL || info == NULL)
		return B_BAD_VALUE;

	return fs_stat_attr(fFd, name, info) < 0 ? errno : B_OK ;
}


/**
 * @brief Retrieves the name of the next attribute in the iteration sequence.
 *
 * @param buffer A buffer of at least B_ATTR_NAME_LENGTH bytes to receive the name.
 * @return B_OK on success, B_ENTRY_NOT_FOUND when all attributes have been iterated,
 *         or an error code on failure.
 */
status_t
BNode::GetNextAttrName(char* buffer)
{
	// We're allowed to assume buffer is at least
	// B_ATTR_NAME_LENGTH chars long, but NULLs
	// are not acceptable.

	// BeOS R5 crashed when passed NULL
	if (buffer == NULL)
		return B_BAD_VALUE;

	if (InitAttrDir() != B_OK)
		return B_FILE_ERROR;

	BPrivate::Storage::LongDirEntry longEntry;
	struct dirent* entry = longEntry.dirent();
	ssize_t result = _kern_read_dir(fAttrFd, entry, sizeof(longEntry), 1);
	if (result < 0)
		return result;

	if (result == 0)
		return B_ENTRY_NOT_FOUND;

	strlcpy(buffer, entry->d_name, B_ATTR_NAME_LENGTH);

	return B_OK;
}


/**
 * @brief Rewinds the attribute iterator to the first attribute.
 *
 * @return B_OK on success, or B_FILE_ERROR if not initialized.
 */
status_t
BNode::RewindAttrs()
{
	if (InitAttrDir() != B_OK)
		return B_FILE_ERROR;

	return _kern_rewind_dir(fAttrFd);
}


/**
 * @brief Writes a BString value as a string attribute on this node.
 *
 * @param name The attribute name.
 * @param data Pointer to the BString to write; must not be NULL.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BNode::WriteAttrString(const char* name, const BString* data)
{
	status_t error = (!name || !data)  ? B_BAD_VALUE : B_OK;
	if (error == B_OK) {
		int32 length = data->Length() + 1;
		ssize_t sizeWritten = WriteAttr(name, B_STRING_TYPE, 0, data->String(),
			length);
		if (sizeWritten != length)
			error = sizeWritten;
	}

	return error;
}


/**
 * @brief Reads a string attribute from this node into a BString.
 *
 * @param name   The attribute name.
 * @param result Pointer to a BString to receive the attribute value.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BNode::ReadAttrString(const char* name, BString* result) const
{
	if (name == NULL || result == NULL)
		return B_BAD_VALUE;

	attr_info info;
	status_t error;

	error = GetAttrInfo(name, &info);
	if (error != B_OK)
		return error;

	// Lock the string's buffer so we can meddle with it
	char* data = result->LockBuffer(info.size + 1);
	if (data == NULL)
		return B_NO_MEMORY;

	// Read the attribute
	ssize_t bytes = ReadAttr(name, B_STRING_TYPE, 0, data, info.size);
	// Check for failure
	if (bytes < 0) {
		error = bytes;
		bytes = 0;
			// In this instance, we simply clear the string
	} else
		error = B_OK;

	// Null terminate the new string just to be sure (since it *is*
	// possible to read and write non-NULL-terminated strings)
	data[bytes] = 0;
	result->UnlockBuffer();

	return error;
}


/**
 * @brief Assignment operator; duplicates the file descriptor of node.
 *
 * @param node The BNode to assign from.
 * @return A reference to this BNode.
 */
BNode&
BNode::operator=(const BNode& node)
{
	// No need to do any assignment if already equal
	if (*this == node)
		return *this;

	// Close down out current state
	Unset();
	// We have to manually dup the node, because R5::BNode::Dup()
	// is not declared to be const (which IMO is retarded).
	fFd = _kern_dup(node.fFd);
	fCStatus = (fFd < 0) ? B_NO_INIT : B_OK ;

	return *this;
}


/**
 * @brief Equality operator; returns true if both nodes refer to the same filesystem node.
 *
 * @param node The BNode to compare against.
 * @return true if both are uninitialized or both refer to the same node_ref.
 */
bool
BNode::operator==(const BNode& node) const
{
	if (fCStatus == B_NO_INIT && node.InitCheck() == B_NO_INIT)
		return true;

	if (fCStatus == B_OK && node.InitCheck() == B_OK) {
		// compare the node_refs
		node_ref ref1, ref2;
		if (GetNodeRef(&ref1) != B_OK)
			return false;

		if (node.GetNodeRef(&ref2) != B_OK)
			return false;

		return (ref1 == ref2);
	}

	return false;
}


/**
 * @brief Inequality operator.
 *
 * @param node The BNode to compare against.
 * @return true if the nodes are not equal.
 */
bool
BNode::operator!=(const BNode& node) const
{
	return !(*this == node);
}


/**
 * @brief Duplicates the underlying file descriptor.
 *
 * @return A new file descriptor on success, or -1 on failure.
 */
int
BNode::Dup()
{
	int fd = _kern_dup(fFd);

	return (fd >= 0 ? fd : -1);
		// comply with R5 return value
}


/*! (currently unused) */
void BNode::_RudeNode1() { }
void BNode::_RudeNode2() { }
void BNode::_RudeNode3() { }
void BNode::_RudeNode4() { }
void BNode::_RudeNode5() { }
void BNode::_RudeNode6() { }


/*!	Sets the node's file descriptor.

	Used by each implementation (i.e. BNode, BFile, BDirectory, etc.) to set
	the node's file descriptor. This allows each subclass to use the various
	file-type specific system calls for opening file descriptors.

	\note This method calls close_fd() to close previously opened FDs. Thus
		derived classes should take care to first call set_fd() and set
		class specific resources freed in their close_fd() version
		thereafter.

	\param fd the file descriptor this BNode should be set to (may be -1).

	\returns \c B_OK if everything went fine, or an error code if something
		went wrong.
*/
/**
 * @brief Sets the node's file descriptor, closing any previously open descriptor.
 *
 * @param fd The new file descriptor; may be -1 to indicate no open descriptor.
 * @return B_OK on success.
 */
status_t
BNode::set_fd(int fd)
{
	if (fFd != -1)
		close_fd();

	fFd = fd;

	return B_OK;
}


/*!	Closes the node's file descriptor(s).

	To be implemented by subclasses to close the file descriptor using the
	proper system call for the given file-type. This implementation calls
	_kern_close(fFd) and also _kern_close(fAttrDir) if necessary.
*/
/**
 * @brief Closes the node and attribute file descriptors.
 */
void
BNode::close_fd()
{
	if (fAttrFd >= 0) {
		_kern_close(fAttrFd);
		fAttrFd = -1;
	}
	if (fFd >= 0) {
		_kern_close(fFd);
		fFd = -1;
	}
}


/*!	Sets the BNode's status.

	To be used by derived classes instead of accessing the BNode's private
	\c fCStatus member directly.

	\param newStatus the new value for the status variable.
*/
/**
 * @brief Sets the BNode's initialization status variable.
 *
 * @param newStatus The new status value to store in fCStatus.
 */
void
BNode::set_status(status_t newStatus)
{
	fCStatus = newStatus;
}


/*!	Initializes the BNode's file descriptor to the node referred to
	by the given FD and path combo.

	\a path must either be \c NULL, an absolute or a relative path.
	In the first case, \a fd must not be \c NULL; the node it refers to will
	be opened. If absolute, \a fd is ignored. If relative and \a fd is >= 0,
	it will be reckoned off the directory identified by \a fd, otherwise off
	the current working directory.

	The method will first try to open the node with read and write permission.
	If that fails due to a read-only FS or because the user has no write
	permission for the node, it will re-try opening the node read-only.

	The \a fCStatus member will be set to the return value of this method.

	\param fd Either a directory FD or a value < 0. In the latter case \a path
	       must be specified.
	\param path Either \a NULL in which case \a fd must be given, absolute, or
	       relative to the directory specified by \a fd (if given) or to the
	       current working directory.
	\param traverse If the node identified by \a fd and \a path is a symlink
	       and \a traverse is \c true, the symlink will be resolved recursively.

	\returns \c B_OK if everything went fine, or an error code otherwise.
*/
/**
 * @brief Opens the node identified by a directory FD and path, trying read-write then read-only.
 *
 * @param fd       A directory file descriptor, or a value < 0 (requires path).
 * @param path     NULL, absolute, or relative path; relative is resolved against fd.
 * @param traverse If true, symlinks are followed recursively.
 * @return B_OK on success, or an error code otherwise; also sets fCStatus.
 */
status_t
BNode::_SetTo(int fd, const char* path, bool traverse)
{
	Unset();

	status_t error = (fd >= 0 || path ? B_OK : B_BAD_VALUE);
	if (error == B_OK) {
		int traverseFlag = (traverse ? 0 : O_NOTRAVERSE);
		fFd = _kern_open(fd, path, O_RDWR | O_CLOEXEC | traverseFlag, 0);
		if (fFd < B_OK && fFd != B_ENTRY_NOT_FOUND) {
			// opening read-write failed, re-try read-only
			fFd = _kern_open(fd, path, O_RDONLY | O_CLOEXEC | traverseFlag, 0);
		}
		if (fFd < 0)
			error = fFd;
	}

	return fCStatus = error;
}


/*!	Initializes the BNode's file descriptor to the node referred to
	by the given entry_ref.

	The method will first try to open the node with read and write permission.
	If that fails due to a read-only FS or because the user has no write
	permission for the node, it will re-try opening the node read-only.

	The \a fCStatus member will be set to the return value of this method.

	\param ref An entry_ref identifying the node to be opened.
	\param traverse If the node identified by \a ref is a symlink and
	       \a traverse is \c true, the symlink will be resolved recursively.

	\returns \c B_OK if everything went fine, or an error code otherwise.
*/
/**
 * @brief Opens the node identified by an entry_ref, trying read-write then read-only.
 *
 * @param ref      An entry_ref identifying the node to open.
 * @param traverse If true, symlinks are followed recursively.
 * @return B_OK on success, or an error code otherwise; also sets fCStatus.
 */
status_t
BNode::_SetTo(const entry_ref* ref, bool traverse)
{
	Unset();

	status_t result = (ref ? B_OK : B_BAD_VALUE);
	if (result == B_OK) {
		int traverseFlag = (traverse ? 0 : O_NOTRAVERSE);
		fFd = _kern_open_entry_ref(ref->device, ref->directory, ref->name,
			O_RDWR | O_CLOEXEC | traverseFlag, 0);
		if (fFd < B_OK && fFd != B_ENTRY_NOT_FOUND) {
			// opening read-write failed, re-try read-only
			fFd = _kern_open_entry_ref(ref->device, ref->directory, ref->name,
				O_RDONLY | O_CLOEXEC | traverseFlag, 0);
		}
		if (fFd < 0)
			result = fFd;
	}

	return fCStatus = result;
}


/*!	Modifies a certain setting for this node based on \a what and the
	corresponding value in \a st.

	Inherited from and called by BStatable.

	\param st a stat structure containing the value to be set.
	\param what specifies what setting to be modified.

	\returns \c B_OK if everything went fine, or an error code otherwise.
*/
/**
 * @brief Applies a stat-based modification to this node (used by BStatable).
 *
 * @param stat The stat structure containing the new value to apply.
 * @param what A flag specifying which stat field to modify.
 * @return B_OK on success, B_FILE_ERROR if not initialized, or another error code.
 */
status_t
BNode::set_stat(struct stat& stat, uint32 what)
{
	if (fCStatus != B_OK)
		return B_FILE_ERROR;

	return _kern_write_stat(fFd, NULL, false, &stat, sizeof(struct stat),
		what);
}



/*!	Verifies that the BNode has been properly initialized, and then
	(if necessary) opens the attribute directory on the node's file
	descriptor, storing it in fAttrDir.

	\returns \c B_OK if everything went fine, or an error code otherwise.
*/
/**
 * @brief Ensures the attribute directory FD is open, opening it if necessary.
 *
 * @return B_OK if the attribute directory is ready, or an error code on failure.
 */
status_t
BNode::InitAttrDir()
{
	if (fCStatus == B_OK && fAttrFd < 0) {
		fAttrFd = _kern_open_attr_dir(fFd, NULL, false);
		if (fAttrFd < 0)
			return fAttrFd;

		// set close on exec flag
		fcntl(fAttrFd, F_SETFD, FD_CLOEXEC);
	}

	return fCStatus;
}


/**
 * @brief Reads the stat structure for this node.
 *
 * @param stat Pointer to a stat structure to be filled in.
 * @return B_OK on success, or fCStatus if not initialized.
 */
status_t
BNode::_GetStat(struct stat* stat) const
{
	return fCStatus != B_OK
		? fCStatus
		: _kern_read_stat(fFd, NULL, false, stat, sizeof(struct stat));
}


/**
 * @brief Reads the BeOS-compatible stat structure for this node.
 *
 * @param stat Pointer to a stat_beos structure to be filled in.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BNode::_GetStat(struct stat_beos* stat) const
{
	struct stat newStat;
	status_t error = _GetStat(&newStat);
	if (error != B_OK)
		return error;

	convert_to_stat_beos(&newStat, stat);

	return B_OK;
}


//	#pragma mark - symbol versions


#ifdef HAIKU_TARGET_PLATFORM_LIBBE_TEST
#	if __GNUC__ == 2	// gcc 2

	B_DEFINE_SYMBOL_VERSION("_GetStat__C5BNodeP4stat",
		"GetStat__C5BNodeP4stat@@LIBBE_TEST");

#	else	// gcc 4

	B_DEFINE_SYMBOL_VERSION("_ZNK5BNode8_GetStatEP4stat",
		"_ZNK5BNode7GetStatEP4stat@@LIBBE_TEST");

#	endif	// gcc 4
#else	// !HAIKU_TARGET_PLATFORM_LIBBE_TEST
#	if __GNUC__ == 2	// gcc 2

	// BeOS compatible GetStat()
	B_DEFINE_SYMBOL_VERSION("_GetStat__C5BNodeP9stat_beos",
		"GetStat__C5BNodeP4stat@LIBBE_BASE");

	// Haiku GetStat()
	B_DEFINE_SYMBOL_VERSION("_GetStat__C5BNodeP4stat",
		"GetStat__C5BNodeP4stat@@LIBBE_1_ALPHA1");

#	else	// gcc 4

	// BeOS compatible GetStat()
	B_DEFINE_SYMBOL_VERSION("_ZNK5BNode8_GetStatEP9stat_beos",
		"_ZNK5BNode7GetStatEP4stat@LIBBE_BASE");

	// Haiku GetStat()
	B_DEFINE_SYMBOL_VERSION("_ZNK5BNode8_GetStatEP4stat",
		"_ZNK5BNode7GetStatEP4stat@@LIBBE_1_ALPHA1");

#	endif	// gcc 4
#endif	// !HAIKU_TARGET_PLATFORM_LIBBE_TEST
