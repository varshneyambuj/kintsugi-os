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
 *   Copyright 2002-2014 Haiku, Inc. All rights reserved.
 *   Authors: Tyler Dauwalder, Ingo Weinhold, bonefish@users.sf.net
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file Statable.cpp
 * @brief Implements the BStatable abstract mixin for querying and modifying filesystem node metadata.
 *
 * BStatable provides a uniform interface over the POSIX stat structure for any
 * node that can be stat'd (BNode, BEntry, BFile, etc.). It exposes type tests
 * (IsFile, IsDirectory, IsSymLink), ownership and permission accessors, size
 * and timestamp getters/setters, and volume lookup. The class relies on the
 * pure-virtual GetStat() and set_stat() methods provided by each concrete
 * subclass. A Private inner class bridges legacy BeOS stat_beos ABI calls to
 * the current stat layout.
 *
 * @see BNode
 * @see BEntry
 * @see BVolume
 */

#include <Statable.h>

#include <sys/stat.h>

#include <compat/sys/stat.h>

#include <Node.h>
#include <NodeMonitor.h>
#include <Volume.h>


/**
 * @brief Private helper class that provides access to the BeOS-compatible GetStat() path.
 *
 * Used internally by the _OhSoStatable1 binary-compatibility shim to obtain
 * a stat_beos structure from any BStatable subclass without exposing the
 * internal _GetStat(struct stat_beos*) method publicly.
 */
class BStatable::Private {
public:
	/**
	 * @brief Constructs a Private wrapper around the given BStatable object.
	 *
	 * @param object  The BStatable instance to wrap; must not be NULL.
	 */
	Private(const BStatable* object)
		:
		fObject(object)
	{
	}

	/**
	 * @brief Retrieves the BeOS-compatible stat structure from the wrapped object.
	 *
	 * @param stat  Pointer to a stat_beos structure to be filled in.
	 * @return B_OK on success, or an error code forwarded from _GetStat().
	 */
	status_t GetStatBeOS(struct stat_beos* stat)
	{
		return fObject->_GetStat(stat);
	}

private:
	const BStatable*	fObject;
};


#if __GNUC__ > 3
/**
 * @brief Virtual destructor; required for correct polymorphic destruction on GCC > 3.
 */
BStatable::~BStatable()
{
}
#endif


/**
 * @brief Returns whether the node is a regular file.
 *
 * Performs a GetStat() call and tests the mode with S_ISREG().
 *
 * @return \c true if the node is a regular file, \c false otherwise or on error.
 */
bool
BStatable::IsFile() const
{
	struct stat stat;
	if (GetStat(&stat) == B_OK)
		return S_ISREG(stat.st_mode);
	else
		return false;
}


/**
 * @brief Returns whether the node is a directory.
 *
 * Performs a GetStat() call and tests the mode with S_ISDIR().
 *
 * @return \c true if the node is a directory, \c false otherwise or on error.
 */
bool
BStatable::IsDirectory() const
{
	struct stat stat;
	if (GetStat(&stat) == B_OK)
		return S_ISDIR(stat.st_mode);
	else
		return false;
}


/**
 * @brief Returns whether the node is a symbolic link.
 *
 * Performs a GetStat() call and tests the mode with S_ISLNK().
 *
 * @return \c true if the node is a symbolic link, \c false otherwise or on error.
 */
bool
BStatable::IsSymLink() const
{
	struct stat stat;
	if (GetStat(&stat) == B_OK)
		return S_ISLNK(stat.st_mode);
	else
		return false;
}


/**
 * @brief Fills out \a ref with the node_ref (device + inode) of this node.
 *
 * @param ref  Pointer to the node_ref structure to fill in.
 * @return B_OK on success, B_BAD_VALUE if \a ref is NULL, or an error code
 *         from GetStat().
 */
status_t
BStatable::GetNodeRef(node_ref* ref) const
{
	status_t result = (ref ? B_OK : B_BAD_VALUE);
	struct stat stat = {};

	if (result == B_OK)
		result = GetStat(&stat);

	if (result == B_OK) {
		ref->device  = stat.st_dev;
		ref->node = stat.st_ino;
	}

	return result;
}


/**
 * @brief Returns the user ID (UID) of the node's owner.
 *
 * @param owner  Pointer to a uid_t to receive the owner's user ID.
 * @return B_OK on success, B_BAD_VALUE if \a owner is NULL, or an error code
 *         from GetStat().
 */
status_t
BStatable::GetOwner(uid_t* owner) const
{
	status_t result = (owner ? B_OK : B_BAD_VALUE);
	struct stat stat = {};

	if (result == B_OK)
		result = GetStat(&stat);

	if (result == B_OK)
		*owner = stat.st_uid;

	return result;
}


/**
 * @brief Sets the user ID (UID) of the node's owner.
 *
 * @param owner  The new user ID to assign to the node.
 * @return B_OK on success, or an error code from set_stat().
 */
status_t
BStatable::SetOwner(uid_t owner)
{
	struct stat stat = {};
	stat.st_uid = owner;

	return set_stat(stat, B_STAT_UID);
}


/**
 * @brief Returns the group ID (GID) of the node.
 *
 * @param group  Pointer to a gid_t to receive the node's group ID.
 * @return B_OK on success, B_BAD_VALUE if \a group is NULL, or an error code
 *         from GetStat().
 */
status_t
BStatable::GetGroup(gid_t* group) const
{
	status_t result = (group ? B_OK : B_BAD_VALUE);
	struct stat stat = {};

	if (result == B_OK)
		result = GetStat(&stat);

	if (result == B_OK)
		*group = stat.st_gid;

	return result;
}


/**
 * @brief Sets the group ID (GID) of the node.
 *
 * @param group  The new group ID to assign to the node.
 * @return B_OK on success, or an error code from set_stat().
 */
status_t
BStatable::SetGroup(gid_t group)
{
	struct stat stat = {};
	stat.st_gid = group;

	return set_stat(stat, B_STAT_GID);
}


/**
 * @brief Returns the permission bits of the node.
 *
 * Only the S_IUMSK portion of st_mode is returned.
 *
 * @param permissions  Pointer to a mode_t to receive the permission bits.
 * @return B_OK on success, B_BAD_VALUE if \a permissions is NULL, or an error
 *         code from GetStat().
 */
status_t
BStatable::GetPermissions(mode_t* permissions) const
{
	status_t result = (permissions ? B_OK : B_BAD_VALUE);
	struct stat stat = {};

	if (result == B_OK)
		result = GetStat(&stat);

	if (result == B_OK)
		*permissions = (stat.st_mode & S_IUMSK);

	return result;
}


/**
 * @brief Sets the permission bits of the node.
 *
 * The filesystem is responsible for masking the supplied value to the
 * applicable S_IUMSK bits.
 *
 * @param permissions  The new permission mode bits.
 * @return B_OK on success, or an error code from set_stat().
 */
status_t
BStatable::SetPermissions(mode_t permissions)
{
	struct stat stat = {};
	// the FS should do the correct masking -- only the S_IUMSK part is
	// modifiable
	stat.st_mode = permissions;

	return set_stat(stat, B_STAT_MODE);
}


/**
 * @brief Returns the size of the node's data in bytes, not counting attributes.
 *
 * @param size  Pointer to an off_t to receive the data size.
 * @return B_OK on success, B_BAD_VALUE if \a size is NULL, or an error code
 *         from GetStat().
 */
status_t
BStatable::GetSize(off_t* size) const
{
	status_t result = (size ? B_OK : B_BAD_VALUE);
	struct stat stat = {};

	if (result == B_OK)
		result = GetStat(&stat);

	if (result == B_OK)
		*size = stat.st_size;

	return result;
}


/**
 * @brief Returns the last modification time of the node.
 *
 * @param mtime  Pointer to a time_t to receive the modification timestamp.
 * @return B_OK on success, B_BAD_VALUE if \a mtime is NULL, or an error code
 *         from GetStat().
 */
status_t
BStatable::GetModificationTime(time_t* mtime) const
{
	status_t result = (mtime ? B_OK : B_BAD_VALUE);
	struct stat stat = {};

	if (result == B_OK)
		result = GetStat(&stat);

	if (result == B_OK)
		*mtime = stat.st_mtime;

	return result;
}


/**
 * @brief Sets the last modification time of the node.
 *
 * @param mtime  The new modification timestamp (seconds since epoch).
 * @return B_OK on success, or an error code from set_stat().
 */
status_t
BStatable::SetModificationTime(time_t mtime)
{
	struct stat stat = {};
	stat.st_mtime = mtime;

	return set_stat(stat, B_STAT_MODIFICATION_TIME);
}


/**
 * @brief Returns the creation time of the node.
 *
 * @note Uses st_crtime, the Haiku-specific creation-time field.
 *
 * @param ctime  Pointer to a time_t to receive the creation timestamp.
 * @return B_OK on success, B_BAD_VALUE if \a ctime is NULL, or an error code
 *         from GetStat().
 */
status_t
BStatable::GetCreationTime(time_t* ctime) const
{
	status_t result = (ctime ? B_OK : B_BAD_VALUE);
	struct stat stat = {};

	if (result == B_OK)
		result = GetStat(&stat);

	if (result == B_OK)
		*ctime = stat.st_crtime;

	return result;
}


/**
 * @brief Sets the creation time of the node.
 *
 * @note Writes to st_crtime, the Haiku-specific creation-time field.
 *
 * @param ctime  The new creation timestamp (seconds since epoch).
 * @return B_OK on success, or an error code from set_stat().
 */
status_t
BStatable::SetCreationTime(time_t ctime)
{
	struct stat stat = {};
	stat.st_crtime = ctime;

	return set_stat(stat, B_STAT_CREATION_TIME);
}


/**
 * @brief Returns the last access time of the node.
 *
 * @param atime  Pointer to a time_t to receive the access timestamp.
 * @return B_OK on success, B_BAD_VALUE if \a atime is NULL, or an error code
 *         from GetStat().
 */
status_t
BStatable::GetAccessTime(time_t* atime) const
{
	status_t result = (atime ? B_OK : B_BAD_VALUE);
	struct stat stat = {};

	if (result == B_OK)
		result = GetStat(&stat);

	if (result == B_OK)
		*atime = stat.st_atime;

	return result;
}


/**
 * @brief Sets the last access time of the node.
 *
 * @param atime  The new access timestamp (seconds since epoch).
 * @return B_OK on success, or an error code from set_stat().
 */
status_t
BStatable::SetAccessTime(time_t atime)
{
	struct stat stat = {};
	stat.st_atime = atime;

	return set_stat(stat, B_STAT_ACCESS_TIME);
}


/**
 * @brief Returns the BVolume on which this node resides.
 *
 * @param volume  Pointer to a BVolume to be initialized with the node's volume.
 * @return B_OK on success, B_BAD_VALUE if \a volume is NULL, or an error code
 *         from GetStat() or BVolume::SetTo().
 */
status_t
BStatable::GetVolume(BVolume* volume) const
{
	status_t result = (volume ? B_OK : B_BAD_VALUE);
	struct stat stat = {};
	if (result == B_OK)
		result = GetStat(&stat);

	if (result == B_OK)
		result = volume->SetTo(stat.st_dev);

	return result;
}


/**
 * @brief Binary-compatibility shim: implements the old GetStat() ABI via _OhSoStatable1.
 *
 * This extern "C" function is mapped to the GetStat() symbol for the LIBBE_BASE
 * version. It obtains a stat_beos from the concrete subclass and converts it to
 * the current stat layout, allowing old binaries compiled against BeOS R5 to
 * call GetStat() on any BStatable subclass.
 *
 * @param self  The BStatable object to query.
 * @param stat  Pointer to a stat structure to be filled with the converted data.
 * @return B_OK on success, or an error code from the underlying _GetStat().
 */
// _OhSoStatable1() -> GetStat()
extern "C" status_t
#if __GNUC__ == 2
_OhSoStatable1__9BStatable(const BStatable* self, struct stat* stat)
#else
_ZN9BStatable14_OhSoStatable1Ev(const BStatable* self, struct stat* stat)
#endif
{
	// No Perform() method -- we have to use the old GetStat() method instead.
	struct stat_beos oldStat = {};
	status_t result = BStatable::Private(self).GetStatBeOS(&oldStat);
	if (result != B_OK)
		return result;

	convert_from_stat_beos(&oldStat, stat);

	return B_OK;
}


/** @brief Reserved virtual slot 2 (binary compatibility padding). */
void BStatable::_OhSoStatable2() {}
/** @brief Reserved virtual slot 3 (binary compatibility padding). */
void BStatable::_OhSoStatable3() {}
