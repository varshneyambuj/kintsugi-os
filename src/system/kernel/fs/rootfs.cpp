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
 *   Copyright 2002-2017, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file rootfs.cpp
 * @brief The root in-memory filesystem — backing the "/" mount point.
 *
 * rootfs is a minimal RAM-based file system that provides the "/" directory
 * before the real root partition is mounted. It stores directory entries and
 * symlinks in memory and is never unmounted. Also used for bind-mounts and
 * the /dev, /pipe, and other virtual mount points.
 *
 * @see vfs.cpp, vfs_boot.cpp
 */


#if FS_SHELL
#	include "fssh_api_wrapper.h"

#	include "OpenHashTable.h"
#	include "list.h"
#else
#	include <stdio.h>
#	include <stdlib.h>
#	include <string.h>
#	include <sys/stat.h>

#	include <fs_cache.h>
#	include <KernelExport.h>
#	include <NodeMonitor.h>

#	include <debug.h>
#	include <lock.h>
#	include <util/OpenHashTable.h>
#	include <util/AutoLock.h>
#	include <vfs.h>
#	include <vm/vm.h>
#endif

#include <fs_ops_support.h>


#if FS_SHELL
	using namespace FSShell;
#endif


//#define TRACE_ROOTFS
#ifdef TRACE_ROOTFS
#	define TRACE(x) dprintf x
#else
#	define TRACE(x)
#endif


namespace {

struct rootfs_stream {
	mode_t						type;
	struct stream_dir {
		struct rootfs_vnode*	dir_head;
		struct list				cookies;
		mutex					cookie_lock;
	} dir;
	struct stream_symlink {
		char*					path;
		size_t					length;
	} symlink;
};

struct rootfs_vnode {
	struct rootfs_vnode*		all_next;
	ino_t						id;
	char*						name;
	timespec					modification_time;
	timespec					creation_time;
	uid_t						uid;
	gid_t						gid;
	struct rootfs_vnode*		parent;
	struct rootfs_vnode*		dir_next;
	struct rootfs_stream		stream;
};

struct VnodeHash {
	typedef	ino_t			KeyType;
	typedef	rootfs_vnode	ValueType;

	size_t HashKey(KeyType key) const
	{
		return key;
	}

	size_t Hash(ValueType* vnode) const
	{
		return vnode->id;
	}

	bool Compare(KeyType key, ValueType* vnode) const
	{
		return vnode->id == key;
	}

	ValueType*& GetLink(ValueType* value) const
	{
		return value->all_next;
	}
};

typedef BOpenHashTable<VnodeHash> VnodeTable;

struct rootfs {
	fs_volume*					volume;
	dev_t						id;
	rw_lock						lock;
	ino_t						next_vnode_id;
	VnodeTable*					vnode_list_hash;
	struct rootfs_vnode*		root_vnode;
};

// dircookie, dirs are only types of streams supported by rootfs
struct rootfs_dir_cookie {
	struct list_link			link;
	mutex						lock;
	struct rootfs_vnode*		current;
	int32						iteration_state;
};

// directory iteration states
enum {
	ITERATION_STATE_DOT		= 0,
	ITERATION_STATE_DOT_DOT	= 1,
	ITERATION_STATE_OTHERS	= 2,
	ITERATION_STATE_BEGIN	= ITERATION_STATE_DOT,
};


// extern only to make forward declaration possible
extern fs_volume_ops sVolumeOps;
extern fs_vnode_ops sVnodeOps;

} // namespace


#define ROOTFS_HASH_SIZE 16


/**
 * @brief Check whether the calling process has the requested access to a vnode.
 *
 * @param dir        The vnode whose permission bits are tested.
 * @param accessMode POSIX access-mode flags (R_OK, W_OK, X_OK).
 * @retval B_OK          Access is permitted.
 * @retval B_NOT_ALLOWED Access is denied.
 */
inline static status_t
rootfs_check_permissions(struct rootfs_vnode* dir, int accessMode)
{
	return check_access_permissions(accessMode, dir->stream.type, (gid_t)dir->gid, (uid_t)dir->uid);
}


/**
 * @brief Return the current wall-clock time as a POSIX timespec.
 *
 * @return A timespec whose tv_sec and tv_nsec fields are populated from
 *         real_time_clock_usecs().
 */
static timespec
current_timespec()
{
	bigtime_t time = real_time_clock_usecs();

	timespec tv;
	tv.tv_sec = time / 1000000;
	tv.tv_nsec = (time % 1000000) * 1000;
	return tv;
}


/**
 * @brief Return the inode ID of a vnode's parent directory.
 *
 * @param vnode The vnode whose parent ID is requested.
 * @return The parent inode ID, or -1 if the vnode has no parent.
 */
static ino_t
get_parent_id(struct rootfs_vnode* vnode)
{
	if (vnode->parent != NULL)
		return vnode->parent->id;
	return -1;
}


/**
 * @brief Allocate and initialise a new rootfs vnode.
 *
 * Allocates heap memory for a vnode, copies the name, assigns a unique inode
 * ID, initialises timestamps and ownership, and (for directories) initialises
 * the cookie list and its mutex.
 *
 * @param fs     The rootfs volume that will own this vnode.
 * @param parent The parent directory vnode; may be NULL for the root.
 * @param name   The entry name; duplicated internally. May be NULL.
 * @param type   The mode bits (e.g. S_IFDIR | 0755).
 * @return Pointer to the new vnode on success, or NULL if allocation failed.
 */
static struct rootfs_vnode*
rootfs_create_vnode(struct rootfs* fs, struct rootfs_vnode* parent,
	const char* name, int type)
{
	struct rootfs_vnode* vnode;

	vnode = (rootfs_vnode*)malloc(sizeof(struct rootfs_vnode));
	if (vnode == NULL)
		return NULL;

	memset(vnode, 0, sizeof(struct rootfs_vnode));

	if (name != NULL) {
		vnode->name = strdup(name);
		if (vnode->name == NULL) {
			free(vnode);
			return NULL;
		}
	}

	vnode->id = fs->next_vnode_id++;
	vnode->stream.type = type;
	vnode->creation_time = vnode->modification_time = current_timespec();
	vnode->uid = geteuid();
	vnode->gid = parent ? parent->gid : getegid();
		// inherit group from parent if possible

	if (S_ISDIR(type)) {
		list_init(&vnode->stream.dir.cookies);
		mutex_init(&vnode->stream.dir.cookie_lock, "rootfs dir cookies");
	}

	return vnode;
}


/**
 * @brief Destroy a rootfs vnode and free all associated resources.
 *
 * Removes the vnode from the global hash table, destroys the directory
 * cookie mutex if applicable, and frees the name string and the vnode
 * structure itself.
 *
 * @param fs           The owning rootfs volume.
 * @param v            The vnode to destroy.
 * @param force_delete If false the call fails when the vnode is still linked
 *                     into a directory or has children.
 * @retval B_OK   Vnode deleted.
 * @retval EPERM  Vnode is still referenced and force_delete is false.
 */
static status_t
rootfs_delete_vnode(struct rootfs* fs, struct rootfs_vnode* v, bool force_delete)
{
	// cant delete it if it's in a directory or is a directory
	// and has children
	if (!force_delete && (v->stream.dir.dir_head != NULL || v->dir_next != NULL))
		return EPERM;

	// remove it from the global hash table
	fs->vnode_list_hash->Remove(v);

	if (S_ISDIR(v->stream.type))
		mutex_destroy(&v->stream.dir.cookie_lock);

	free(v->name);
	free(v);

	return 0;
}


/**
 * @brief Advance any open directory cookies that point at a vnode being removed.
 *
 * Iterates all open cookies on @p dir and, for each cookie whose current
 * pointer equals @p vnode, advances it to the next entry so that ongoing
 * readdir() calls skip the removed node cleanly.
 *
 * @param dir   The directory whose cookie list is scanned.
 * @param vnode The vnode that is about to be unlinked from @p dir.
 */
static void
update_dir_cookies(struct rootfs_vnode* dir, struct rootfs_vnode* vnode)
{
	struct rootfs_dir_cookie* cookie = NULL;

	while ((cookie = (rootfs_dir_cookie*)list_get_next_item(
			&dir->stream.dir.cookies, cookie)) != NULL) {
		MutexLocker cookieLocker(cookie->lock);
		if (cookie->current == vnode)
			cookie->current = vnode->dir_next;
	}
}


/**
 * @brief Look up a child entry by name inside a directory vnode.
 *
 * Handles the special "." and ".." names directly; for all other names it
 * performs a linear scan of the sorted sibling list.
 *
 * @param dir  The directory to search.
 * @param path The entry name to look for.
 * @return Pointer to the matching vnode, or NULL if not found.
 */
static struct rootfs_vnode*
rootfs_find_in_dir(struct rootfs_vnode* dir, const char* path)
{
	struct rootfs_vnode* vnode;

	if (!strcmp(path, "."))
		return dir;
	if (!strcmp(path, ".."))
		return dir->parent;

	for (vnode = dir->stream.dir.dir_head; vnode; vnode = vnode->dir_next) {
		if (!strcmp(vnode->name, path))
			return vnode;
	}
	return NULL;
}


/**
 * @brief Insert a vnode into a directory's sorted child list.
 *
 * Maintains alphabetical ordering of the sibling linked list, sets the
 * vnode's parent pointer, updates the directory's modification time, and
 * fires a stat-changed notification.
 *
 * @param fs    The owning rootfs volume (used for notifications).
 * @param dir   The directory that will receive the new entry.
 * @param vnode The vnode to insert.
 * @retval B_OK Always succeeds.
 */
static status_t
rootfs_insert_in_dir(struct rootfs* fs, struct rootfs_vnode* dir,
	struct rootfs_vnode* vnode)
{
	// make sure the directory stays sorted alphabetically

	struct rootfs_vnode* node = dir->stream.dir.dir_head;
	struct rootfs_vnode* last = NULL;
	while (node != NULL && strcmp(node->name, vnode->name) < 0) {
		last = node;
		node = node->dir_next;
	}
	if (last == NULL) {
		// the new vnode is the first entry in the list
		vnode->dir_next = dir->stream.dir.dir_head;
		dir->stream.dir.dir_head = vnode;
	} else {
		// insert after that node
		vnode->dir_next = last->dir_next;
		last->dir_next = vnode;
	}

	vnode->parent = dir;
	dir->modification_time = current_timespec();

	notify_stat_changed(fs->id, get_parent_id(dir), dir->id,
		B_STAT_MODIFICATION_TIME);
	return B_OK;
}


/**
 * @brief Remove a specific vnode from a directory's child list.
 *
 * Scans the sibling list for @p removeVnode, updates any open cookies via
 * update_dir_cookies(), patches the linked list, refreshes the directory's
 * modification time, and fires a stat-changed notification.
 *
 * @param fs          The owning rootfs volume.
 * @param dir         The directory from which the entry is removed.
 * @param removeVnode The vnode to unlink.
 * @retval B_OK             Entry removed.
 * @retval B_ENTRY_NOT_FOUND Entry was not found in the directory.
 */
static status_t
rootfs_remove_from_dir(struct rootfs* fs, struct rootfs_vnode* dir,
	struct rootfs_vnode* removeVnode)
{
	struct rootfs_vnode* vnode;
	struct rootfs_vnode* lastVnode;

	for (vnode = dir->stream.dir.dir_head, lastVnode = NULL; vnode != NULL;
			lastVnode = vnode, vnode = vnode->dir_next) {
		if (vnode == removeVnode) {
			// make sure all dircookies dont point to this vnode
			update_dir_cookies(dir, vnode);

			if (lastVnode)
				lastVnode->dir_next = vnode->dir_next;
			else
				dir->stream.dir.dir_head = vnode->dir_next;
			vnode->dir_next = NULL;

			dir->modification_time = current_timespec();
			notify_stat_changed(fs->id, get_parent_id(dir), dir->id,
				B_STAT_MODIFICATION_TIME);
			return B_OK;
		}
	}
	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Test whether a directory vnode contains no children.
 *
 * @param dir The directory vnode to test.
 * @return true if the directory has no child entries, false otherwise.
 */
static bool
rootfs_is_dir_empty(struct rootfs_vnode* dir)
{
	return !dir->stream.dir.dir_head;
}


/**
 * @brief Schedule a vnode for removal and unlink it from its directory.
 *
 * Acquires a temporary VFS reference, calls remove_vnode() to mark it
 * for deferred deletion, removes it from the parent directory, and emits
 * an entry-removed notification. Must be called with the FS write lock held.
 *
 * @param fs        The owning rootfs volume.
 * @param directory The directory that currently contains the vnode.
 * @param vnode     The vnode to remove.
 * @retval B_OK On success.
 */
static status_t
remove_node(struct rootfs* fs, struct rootfs_vnode* directory,
	struct rootfs_vnode* vnode)
{
	// schedule this vnode to be removed when it's ref goes to zero

	bool gotNode = (get_vnode(fs->volume, vnode->id, NULL) == B_OK);

	status_t status = B_OK;
	if (gotNode)
		status = remove_vnode(fs->volume, vnode->id);

	if (status == B_OK) {
		rootfs_remove_from_dir(fs, directory, vnode);
		notify_entry_removed(fs->id, directory->id, vnode->name, vnode->id);
	}

	if (gotNode)
		put_vnode(fs->volume, vnode->id);

	return status;
}


/**
 * @brief Remove an entry from a directory by name, with type validation.
 *
 * Acquires the FS write lock, looks up the entry, validates that its type
 * matches @p isDirectory, checks that a directory target is empty, removes
 * it from the entry cache, and calls remove_node().
 *
 * @param fs          The owning rootfs volume.
 * @param dir         The directory from which to remove the entry.
 * @param name        The entry name to remove.
 * @param isDirectory true if the caller expects to remove a directory.
 * @retval B_OK                  Entry removed.
 * @retval B_ENTRY_NOT_FOUND     No entry with that name exists.
 * @retval B_NOT_A_DIRECTORY     Entry exists but is not a directory.
 * @retval B_IS_A_DIRECTORY      Entry is a directory but isDirectory is false.
 * @retval B_DIRECTORY_NOT_EMPTY Directory is not empty.
 */
static status_t
rootfs_remove(struct rootfs* fs, struct rootfs_vnode* dir, const char* name,
	bool isDirectory)
{
	struct rootfs_vnode* vnode;
	status_t status = B_OK;

	WriteLocker locker(fs->lock);

	vnode = rootfs_find_in_dir(dir, name);
	if (!vnode)
		status = B_ENTRY_NOT_FOUND;
	else if (isDirectory && !S_ISDIR(vnode->stream.type))
		status = B_NOT_A_DIRECTORY;
	else if (!isDirectory && S_ISDIR(vnode->stream.type))
		status = B_IS_A_DIRECTORY;
	else if (isDirectory && !rootfs_is_dir_empty(vnode))
		status = B_DIRECTORY_NOT_EMPTY;

	if (status != B_OK)
		return status;

	entry_cache_remove(fs->volume->id, dir->id, name);

	return remove_node(fs, dir, vnode);
}


//	#pragma mark -


/**
 * @brief Mount the rootfs volume and publish the root vnode.
 *
 * Allocates the in-memory rootfs state, initialises the vnode hash table,
 * creates the root directory vnode, publishes it to the VFS, and returns
 * its inode ID in @p _rootID.
 *
 * @param volume   The VFS volume descriptor to initialise.
 * @param device   Ignored (rootfs has no backing device).
 * @param flags    Mount flags (unused).
 * @param args     Mount arguments (unused).
 * @param _rootID  Output: inode ID of the root vnode.
 * @retval B_OK        Volume mounted successfully.
 * @retval B_NO_MEMORY Allocation failure.
 */
static status_t
rootfs_mount(fs_volume* volume, const char* device, uint32 flags,
	const char* args, ino_t* _rootID)
{
	struct rootfs* fs;
	struct rootfs_vnode* vnode;
	status_t err;

	TRACE(("rootfs_mount: entry\n"));

	fs = (rootfs*)malloc(sizeof(struct rootfs));
	if (fs == NULL)
		return B_NO_MEMORY;

	volume->private_volume = fs;
	volume->ops = &sVolumeOps;
	fs->volume = volume;
	fs->id = volume->id;
	fs->next_vnode_id = 1;

	rw_lock_init(&fs->lock, "rootfs");

	fs->vnode_list_hash = new(std::nothrow) VnodeTable();
	if (fs->vnode_list_hash == NULL
			|| fs->vnode_list_hash->Init(ROOTFS_HASH_SIZE) != B_OK) {
		err = B_NO_MEMORY;
		goto err2;
	}

	// create the root vnode
	vnode = rootfs_create_vnode(fs, NULL, ".", S_IFDIR | 0755);
	if (vnode == NULL) {
		err = B_NO_MEMORY;
		goto err3;
	}
	vnode->parent = vnode;

	fs->root_vnode = vnode;
	fs->vnode_list_hash->Insert(vnode);
	publish_vnode(volume, vnode->id, vnode, &sVnodeOps, vnode->stream.type, 0);

	*_rootID = vnode->id;

	return B_OK;

err3:
	delete fs->vnode_list_hash;
err2:
	rw_lock_destroy(&fs->lock);
	free(fs);

	return err;
}


/**
 * @brief Unmount the rootfs volume and release all resources.
 *
 * Drops the reference to the root vnode, iterates the vnode hash table
 * force-deleting every remaining vnode, destroys the read-write lock, and
 * frees the rootfs structure.
 *
 * @param _volume The VFS volume descriptor for the rootfs instance.
 * @retval B_OK Always succeeds.
 */
static status_t
rootfs_unmount(fs_volume* _volume)
{
	struct rootfs* fs = (struct rootfs*)_volume->private_volume;

	TRACE(("rootfs_unmount: entry fs = %p\n", fs));

	// release the reference to the root
	put_vnode(fs->volume, fs->root_vnode->id);

	// delete all of the vnodes
	VnodeTable::Iterator i(fs->vnode_list_hash);

	while (i.HasNext()) {
		struct rootfs_vnode* vnode = i.Next();
		rootfs_delete_vnode(fs, vnode, true);
	}

	delete fs->vnode_list_hash;
	rw_lock_destroy(&fs->lock);
	free(fs);

	return B_OK;
}


/**
 * @brief Sync the rootfs volume to its backing store (no-op).
 *
 * rootfs is entirely in memory; this function exists only to satisfy the
 * VFS sync interface.
 *
 * @param _volume The VFS volume descriptor (unused).
 * @retval B_OK Always.
 */
static status_t
rootfs_sync(fs_volume* _volume)
{
	TRACE(("rootfs_sync: entry\n"));

	return B_OK;
}


/**
 * @brief Look up a named entry in a directory and return its inode ID.
 *
 * Checks that @p _dir is a directory, verifies execute permission, acquires
 * the FS read lock, calls rootfs_find_in_dir(), obtains a VFS reference to
 * the found vnode, populates the entry cache, and writes the inode ID to
 * @p _id.
 *
 * @param _volume The rootfs volume.
 * @param _dir    The directory vnode in which to search.
 * @param name    The entry name to look up.
 * @param _id     Output: inode ID of the located vnode.
 * @retval B_OK              Entry found and referenced.
 * @retval B_NOT_A_DIRECTORY @p _dir is not a directory.
 * @retval B_ENTRY_NOT_FOUND No entry with that name exists.
 * @retval B_NOT_ALLOWED     Caller lacks execute permission on the directory.
 */
static status_t
rootfs_lookup(fs_volume* _volume, fs_vnode* _dir, const char* name, ino_t* _id)
{
	struct rootfs* fs = (struct rootfs*)_volume->private_volume;
	struct rootfs_vnode* dir = (struct rootfs_vnode*)_dir->private_node;
	struct rootfs_vnode* vnode;

	TRACE(("rootfs_lookup: entry dir %p, name '%s'\n", dir, name));
	if (!S_ISDIR(dir->stream.type))
		return B_NOT_A_DIRECTORY;

	status_t status = rootfs_check_permissions(dir, X_OK);
	if (status != B_OK)
		return status;

	ReadLocker locker(fs->lock);

	// look it up
	vnode = rootfs_find_in_dir(dir, name);
	if (!vnode)
		return B_ENTRY_NOT_FOUND;

	status = get_vnode(fs->volume, vnode->id, NULL);
	if (status != B_OK)
		return status;

	entry_cache_add(fs->volume->id, dir->id, name, vnode->id);

	*_id = vnode->id;
	return B_OK;
}


/**
 * @brief Copy a vnode's name into the caller-supplied buffer.
 *
 * @param _volume    The rootfs volume (unused).
 * @param _vnode     The vnode whose name is requested.
 * @param buffer     Destination buffer for the name string.
 * @param bufferSize Size of @p buffer in bytes.
 * @retval B_OK Always succeeds (name is always present).
 */
static status_t
rootfs_get_vnode_name(fs_volume* _volume, fs_vnode* _vnode, char* buffer,
	size_t bufferSize)
{
	struct rootfs_vnode* vnode = (struct rootfs_vnode*)_vnode->private_node;

	TRACE(("rootfs_get_vnode_name: vnode = %p (name = %s)\n", vnode,
		vnode->name));

	strlcpy(buffer, vnode->name, bufferSize);
	return B_OK;
}


/**
 * @brief Retrieve a vnode object by inode ID (called by the VFS pager).
 *
 * Looks up the inode ID in the vnode hash table (under the FS read lock
 * unless re-entering), and fills in the fs_vnode structure with the private
 * node pointer, operations table, type, and flags.
 *
 * @param _volume The rootfs volume.
 * @param id      The inode ID to look up.
 * @param _vnode  Output: filled with private_node pointer and ops.
 * @param _type   Output: the vnode's mode bits.
 * @param _flags  Output: vnode publish flags (always 0).
 * @param reenter true if the VFS is re-entering (skips lock acquisition).
 * @retval B_OK             Vnode found and populated.
 * @retval B_ENTRY_NOT_FOUND No vnode with that ID.
 */
static status_t
rootfs_get_vnode(fs_volume* _volume, ino_t id, fs_vnode* _vnode, int* _type,
	uint32* _flags, bool reenter)
{
	struct rootfs* fs = (struct rootfs*)_volume->private_volume;
	struct rootfs_vnode* vnode;

	TRACE(("rootfs_getvnode: asking for vnode %lld, r %d\n", id, reenter));

	if (!reenter)
		rw_lock_read_lock(&fs->lock);

	vnode = fs->vnode_list_hash->Lookup(id);

	if (!reenter)
		rw_lock_read_unlock(&fs->lock);

	TRACE(("rootfs_getnvnode: looked it up at %p\n", vnode));

	if (vnode == NULL)
		return B_ENTRY_NOT_FOUND;

	_vnode->private_node = vnode;
	_vnode->ops = &sVnodeOps;
	*_type = vnode->stream.type;
	*_flags = 0;

	return B_OK;
}


/**
 * @brief Release a VFS reference to a rootfs vnode (no-op).
 *
 * rootfs vnodes are never evicted from memory, so this function performs
 * no action beyond satisfying the VFS interface contract.
 *
 * @param _volume The rootfs volume (unused).
 * @param _vnode  The vnode being released (unused).
 * @param reenter true if called from a re-entrant VFS path.
 * @retval B_OK Always.
 */
static status_t
rootfs_put_vnode(fs_volume* _volume, fs_vnode* _vnode, bool reenter)
{
#ifdef TRACE_ROOTFS
	struct rootfs_vnode* vnode = (struct rootfs_vnode*)_vnode->private_node;

	TRACE(("rootfs_putvnode: entry on vnode 0x%Lx, r %d\n", vnode->id, reenter));
#endif
	return B_OK; // whatever
}


/**
 * @brief Physically delete a rootfs vnode whose last reference has been dropped.
 *
 * Called by the VFS when the vnode's reference count reaches zero after a
 * prior remove_vnode() call. Acquires the FS write lock (unless re-entering)
 * and calls rootfs_delete_vnode().
 *
 * @param _volume The rootfs volume.
 * @param _vnode  The vnode to permanently remove.
 * @param reenter true if called from a re-entrant VFS path.
 * @retval B_OK Always.
 * @note Panics if the vnode is still linked into its parent directory.
 */
static status_t
rootfs_remove_vnode(fs_volume* _volume, fs_vnode* _vnode, bool reenter)
{
	struct rootfs* fs = (struct rootfs*)_volume->private_volume;
	struct rootfs_vnode* vnode = (struct rootfs_vnode*)_vnode->private_node;

	TRACE(("rootfs_remove_vnode: remove %p (0x%Lx), r %d\n", vnode, vnode->id,
		reenter));

	if (!reenter)
		rw_lock_write_lock(&fs->lock);

	if (vnode->dir_next) {
		// can't remove node if it's linked to the dir
		panic("rootfs_remove_vnode: vnode %p asked to be removed is present in "
			"dir\n", vnode);
	}

	rootfs_delete_vnode(fs, vnode, false);

	if (!reenter)
		rw_lock_write_unlock(&fs->lock);

	return B_OK;
}


/**
 * @brief Stub for regular-file creation — not supported by rootfs.
 *
 * rootfs only holds directories and symlinks; creating regular files is
 * not permitted.
 *
 * @param _volume  The rootfs volume (unused).
 * @param _dir     The target directory (unused).
 * @param name     The desired file name (unused).
 * @param omode    Open-mode flags (unused).
 * @param perms    Permission bits (unused).
 * @param _cookie  Output cookie (unused).
 * @param _newID   Output inode ID (unused).
 * @retval B_BAD_VALUE Always — operation not supported.
 */
static status_t
rootfs_create(fs_volume* _volume, fs_vnode* _dir, const char* name, int omode,
	int perms, void** _cookie, ino_t* _newID)
{
	return B_BAD_VALUE;
}


/**
 * @brief Open a rootfs vnode for reading (or directory iteration).
 *
 * Directories may only be opened read-only. Checks access permissions and
 * returns a NULL cookie, since rootfs files carry no data.
 *
 * @param _volume  The rootfs volume.
 * @param _v       The vnode to open.
 * @param openMode O_* flags describing the desired access mode.
 * @param _cookie  Output: always set to NULL on success.
 * @retval B_OK          Vnode opened.
 * @retval B_IS_A_DIRECTORY Directory opened with write flags.
 * @retval B_NOT_ALLOWED Permission denied.
 */
static status_t
rootfs_open(fs_volume* _volume, fs_vnode* _v, int openMode, void** _cookie)
{
	struct rootfs_vnode* vnode = (rootfs_vnode*)_v->private_node;

	if (S_ISDIR(vnode->stream.type) && (openMode & O_RWMASK) != O_RDONLY)
		return B_IS_A_DIRECTORY;

	status_t status = rootfs_check_permissions(vnode, open_mode_to_access(openMode));
	if (status != B_OK)
		return status;

	// allow to open the file, but nothing can be done with it

	*_cookie = NULL;
	return B_OK;
}


/**
 * @brief Close a previously opened rootfs vnode (no-op).
 *
 * @param _volume The rootfs volume (unused).
 * @param _vnode  The vnode being closed (unused).
 * @param _cookie The per-open cookie (unused).
 * @retval B_OK Always.
 */
static status_t
rootfs_close(fs_volume* _volume, fs_vnode* _vnode, void* _cookie)
{
	TRACE(("rootfs_close: entry vnode %p, cookie %p\n", _vnode->private_node,
		_cookie));
	return B_OK;
}


/**
 * @brief Free the per-open cookie for a rootfs vnode (no-op).
 *
 * Since rootfs_open() always stores NULL in the cookie, there is nothing
 * to free here.
 *
 * @param _volume The rootfs volume (unused).
 * @param _v      The vnode (unused).
 * @param _cookie The cookie to free — always NULL for rootfs.
 * @retval B_OK Always.
 */
static status_t
rootfs_free_cookie(fs_volume* _volume, fs_vnode* _v, void* _cookie)
{
	return B_OK;
}


/**
 * @brief Flush a rootfs vnode to its backing store (no-op).
 *
 * rootfs is entirely in memory; this call is a no-op.
 *
 * @param _volume  The rootfs volume (unused).
 * @param _v       The vnode (unused).
 * @param dataOnly If true only data (not metadata) should be flushed (unused).
 * @retval B_OK Always.
 */
static status_t
rootfs_fsync(fs_volume* _volume, fs_vnode* _v, bool dataOnly)
{
	return B_OK;
}


/**
 * @brief Read data from a rootfs vnode — not supported.
 *
 * rootfs holds only directories and symlinks; data reads are not meaningful.
 *
 * @param _volume The rootfs volume (unused).
 * @param _vnode  The vnode (unused).
 * @param _cookie The per-open cookie (unused).
 * @param pos     File offset (unused).
 * @param buffer  Destination buffer (unused).
 * @param _length Input/output byte count (unused).
 * @retval EINVAL Always.
 */
static status_t
rootfs_read(fs_volume* _volume, fs_vnode* _vnode, void* _cookie,
	off_t pos, void* buffer, size_t* _length)
{
	return EINVAL;
}


/**
 * @brief Write data to a rootfs vnode — not permitted.
 *
 * Writes are never allowed on rootfs nodes.
 *
 * @param _volume The rootfs volume (unused).
 * @param vnode   The vnode (unused).
 * @param cookie  The per-open cookie (unused).
 * @param pos     File offset (unused).
 * @param buffer  Source data buffer (unused).
 * @param _length Input/output byte count (unused).
 * @retval EPERM Always.
 */
static status_t
rootfs_write(fs_volume* _volume, fs_vnode* vnode, void* cookie,
	off_t pos, const void* buffer, size_t* _length)
{
	TRACE(("rootfs_write: vnode %p, cookie %p, pos 0x%Lx , len %#x\n",
		vnode, cookie, pos, (int)*_length));

	return EPERM;
}


/**
 * @brief Create a new directory entry inside a rootfs directory.
 *
 * Verifies write permission on the parent, checks for name collisions,
 * allocates a new directory vnode, inserts it into the parent's sorted
 * child list, registers it in the vnode hash, populates the entry cache,
 * and fires an entry-created notification.
 *
 * @param _volume The rootfs volume.
 * @param _dir    The parent directory vnode.
 * @param name    The name for the new directory.
 * @param mode    Permission bits (masked with S_IUMSK).
 * @retval B_OK          Directory created.
 * @retval B_FILE_EXISTS An entry with that name already exists.
 * @retval B_NO_MEMORY   Vnode allocation failed.
 * @retval B_NOT_ALLOWED Caller lacks write permission.
 */
static status_t
rootfs_create_dir(fs_volume* _volume, fs_vnode* _dir, const char* name,
	int mode)
{
	struct rootfs* fs = (rootfs*)_volume->private_volume;
	struct rootfs_vnode* dir = (rootfs_vnode*)_dir->private_node;
	struct rootfs_vnode* vnode;

	TRACE(("rootfs_create_dir: dir %p, name = '%s', perms = %d\n", dir, name,
		mode));

	status_t status = rootfs_check_permissions(dir, W_OK);
	if (status != B_OK)
		return status;

	WriteLocker locker(fs->lock);

	vnode = rootfs_find_in_dir(dir, name);
	if (vnode != NULL)
		return B_FILE_EXISTS;

	TRACE(("rootfs_create: creating new vnode\n"));
	vnode = rootfs_create_vnode(fs, dir, name, S_IFDIR | (mode & S_IUMSK));
	if (vnode == NULL)
		return B_NO_MEMORY;

	rootfs_insert_in_dir(fs, dir, vnode);
	fs->vnode_list_hash->Insert(vnode);

	entry_cache_add(fs->volume->id, dir->id, name, vnode->id);
	notify_entry_created(fs->id, dir->id, name, vnode->id);

	return B_OK;
}


/**
 * @brief Remove a directory entry from a rootfs directory.
 *
 * Verifies write permission on the parent, then delegates to rootfs_remove()
 * with @p isDirectory=true which additionally checks that the target is an
 * empty directory.
 *
 * @param _volume The rootfs volume.
 * @param _dir    The parent directory vnode.
 * @param name    The name of the directory to remove.
 * @retval B_OK                  Directory removed.
 * @retval B_ENTRY_NOT_FOUND     No entry with that name.
 * @retval B_NOT_A_DIRECTORY     Entry exists but is not a directory.
 * @retval B_DIRECTORY_NOT_EMPTY Directory is not empty.
 * @retval B_NOT_ALLOWED         Caller lacks write permission.
 */
static status_t
rootfs_remove_dir(fs_volume* _volume, fs_vnode* _dir, const char* name)
{
	struct rootfs* fs = (rootfs*)_volume->private_volume;
	struct rootfs_vnode* dir = (rootfs_vnode*)_dir->private_node;

	status_t status = rootfs_check_permissions(dir, W_OK);
	if (status != B_OK)
		return status;

	TRACE(("rootfs_remove_dir: dir %p (0x%Lx), name '%s'\n", dir, dir->id,
		name));

	return rootfs_remove(fs, dir, name, true);
}


/**
 * @brief Open a directory vnode for iteration and allocate a cookie.
 *
 * Verifies read permission, allocates a rootfs_dir_cookie, initialises its
 * mutex and iteration state, registers it in the directory's cookie list,
 * and returns it via @p _cookie.
 *
 * @param _volume  The rootfs volume.
 * @param _vnode   The directory vnode to open.
 * @param _cookie  Output: the newly allocated directory cookie.
 * @retval B_OK          Directory opened.
 * @retval B_BAD_VALUE   @p _vnode is not a directory.
 * @retval B_NO_MEMORY   Cookie allocation failed.
 * @retval B_NOT_ALLOWED Caller lacks read permission.
 */
static status_t
rootfs_open_dir(fs_volume* _volume, fs_vnode* _vnode, void** _cookie)
{
	struct rootfs* fs = (struct rootfs*)_volume->private_volume;
	struct rootfs_vnode* vnode = (struct rootfs_vnode*)_vnode->private_node;
	struct rootfs_dir_cookie* cookie;

	status_t status = rootfs_check_permissions(vnode, R_OK);
	if (status < B_OK)
		return status;

	TRACE(("rootfs_open: vnode %p\n", vnode));

	if (!S_ISDIR(vnode->stream.type))
		return B_BAD_VALUE;

	cookie = (rootfs_dir_cookie*)malloc(sizeof(struct rootfs_dir_cookie));
	if (cookie == NULL)
		return B_NO_MEMORY;

	mutex_init(&cookie->lock, "rootfs dir cookie");

	ReadLocker locker(fs->lock);

	cookie->current = vnode->stream.dir.dir_head;
	cookie->iteration_state = ITERATION_STATE_BEGIN;

	mutex_lock(&vnode->stream.dir.cookie_lock);
	list_add_item(&vnode->stream.dir.cookies, cookie);
	mutex_unlock(&vnode->stream.dir.cookie_lock);

	*_cookie = cookie;

	return B_OK;
}


/**
 * @brief Release and free a directory iteration cookie.
 *
 * Removes the cookie from the directory's cookie list, destroys its mutex,
 * and frees the cookie memory.
 *
 * @param _volume  The rootfs volume.
 * @param _vnode   The directory vnode that owns the cookie.
 * @param _cookie  The cookie to free (cast to rootfs_dir_cookie*).
 * @retval B_OK Always.
 */
static status_t
rootfs_free_dir_cookie(fs_volume* _volume, fs_vnode* _vnode, void* _cookie)
{
	struct rootfs_dir_cookie* cookie = (rootfs_dir_cookie*)_cookie;
	struct rootfs_vnode* vnode = (rootfs_vnode*)_vnode->private_node;
	struct rootfs* fs = (rootfs*)_volume->private_volume;

	ReadLocker locker(fs->lock);

	mutex_lock(&vnode->stream.dir.cookie_lock);
	list_remove_item(&vnode->stream.dir.cookies, cookie);
	mutex_unlock(&vnode->stream.dir.cookie_lock);

	locker.Unlock();

	mutex_destroy(&cookie->lock);

	free(cookie);
	return B_OK;
}


/**
 * @brief Read the next directory entry from an open directory cookie.
 *
 * Returns one dirent at a time. Synthesises "." and ".." from the vnode's own
 * ID and its parent's ID respectively, then iterates through the sorted child
 * list. Advances the cookie's current pointer and iteration state on success.
 *
 * @param _volume    The rootfs volume.
 * @param _vnode     The directory vnode being iterated.
 * @param _cookie    The per-open directory cookie.
 * @param dirent     Output buffer for the dirent structure and name.
 * @param bufferSize Size of @p dirent in bytes.
 * @param _num       Input: max entries requested; output: entries written (0 or 1).
 * @retval B_OK    Entry written or end of directory (@p *_num == 0).
 * @retval ENOBUFS Buffer too small for the current entry.
 */
static status_t
rootfs_read_dir(fs_volume* _volume, fs_vnode* _vnode, void* _cookie,
	struct dirent* dirent, size_t bufferSize, uint32* _num)
{
	struct rootfs_vnode* vnode = (struct rootfs_vnode*)_vnode->private_node;
	struct rootfs_dir_cookie* cookie = (rootfs_dir_cookie*)_cookie;
	struct rootfs* fs = (rootfs*)_volume->private_volume;
	struct rootfs_vnode* childNode = NULL;
	const char* name = NULL;
	struct rootfs_vnode* nextChildNode = NULL;

	TRACE(("rootfs_read_dir: vnode %p, cookie %p, buffer = %p, bufferSize = %d, "
		"num = %p\n", _vnode, cookie, dirent, (int)bufferSize, _num));

	ReadLocker locker(fs->lock);

	MutexLocker cookieLocker(cookie->lock);
	int nextState = cookie->iteration_state;

	switch (cookie->iteration_state) {
		case ITERATION_STATE_DOT:
			childNode = vnode;
			name = ".";
			nextChildNode = vnode->stream.dir.dir_head;
			nextState = cookie->iteration_state + 1;
			break;
		case ITERATION_STATE_DOT_DOT:
			childNode = vnode->parent;
			name = "..";
			nextChildNode = vnode->stream.dir.dir_head;
			nextState = cookie->iteration_state + 1;
			break;
		default:
			childNode = cookie->current;
			if (childNode) {
				name = childNode->name;
				nextChildNode = childNode->dir_next;
			}
			break;
	}

	if (!childNode) {
		// we're at the end of the directory
		*_num = 0;
		return B_OK;
	}

	dirent->d_dev = fs->id;
	dirent->d_ino = childNode->id;
	dirent->d_reclen = offsetof(struct dirent, d_name) + strlen(name) + 1;

	if (dirent->d_reclen > bufferSize)
		return ENOBUFS;

	int nameLength = user_strlcpy(dirent->d_name, name,
		bufferSize - offsetof(struct dirent, d_name));
	if (nameLength < B_OK)
		return nameLength;

	cookie->current = nextChildNode;
	cookie->iteration_state = nextState;
	*_num = 1;
	return B_OK;
}


/**
 * @brief Rewind a directory cookie back to the start of the entry list.
 *
 * Resets the cookie's current pointer to the first child and the iteration
 * state to ITERATION_STATE_BEGIN so the next readdir() returns ".".
 *
 * @param _volume  The rootfs volume.
 * @param _vnode   The directory vnode.
 * @param _cookie  The cookie to rewind.
 * @retval B_OK Always.
 */
static status_t
rootfs_rewind_dir(fs_volume* _volume, fs_vnode* _vnode, void* _cookie)
{
	struct rootfs_dir_cookie* cookie = (rootfs_dir_cookie*)_cookie;
	struct rootfs_vnode* vnode = (rootfs_vnode*)_vnode->private_node;
	struct rootfs* fs = (rootfs*)_volume->private_volume;

	ReadLocker locker(fs->lock);
	MutexLocker cookieLocker(cookie->lock);

	cookie->current = vnode->stream.dir.dir_head;
	cookie->iteration_state = ITERATION_STATE_BEGIN;

	return B_OK;
}


/**
 * @brief Perform an ioctl operation on a rootfs vnode — unsupported.
 *
 * rootfs does not support any ioctl operations.
 *
 * @param _volume The rootfs volume (unused).
 * @param _v      The vnode (unused).
 * @param _cookie The per-open cookie (unused).
 * @param op      The ioctl request code (unused).
 * @param buffer  The ioctl argument buffer (unused).
 * @param length  Size of @p buffer (unused).
 * @retval B_BAD_VALUE Always.
 */
static status_t
rootfs_ioctl(fs_volume* _volume, fs_vnode* _v, void* _cookie, uint32 op,
	void* buffer, size_t length)
{
	TRACE(("rootfs_ioctl: vnode %p, cookie %p, op %d, buf %p, length %d\n",
		_volume, _cookie, (int)op, buffer, (int)length));

	return B_BAD_VALUE;
}


/**
 * @brief Report whether a rootfs vnode supports paging — always false.
 *
 * @param _volume The rootfs volume (unused).
 * @param _v      The vnode (unused).
 * @param cookie  The per-open cookie (unused).
 * @return false Always.
 */
static bool
rootfs_can_page(fs_volume* _volume, fs_vnode* _v, void* cookie)
{
	return false;
}


/**
 * @brief Read pages from a rootfs vnode — not allowed.
 *
 * @param _volume   The rootfs volume (unused).
 * @param _v        The vnode (unused).
 * @param cookie    The per-open cookie (unused).
 * @param pos       Page offset (unused).
 * @param vecs      I/O vector array (unused).
 * @param count     Number of vectors (unused).
 * @param _numBytes Input/output byte count (unused).
 * @retval B_NOT_ALLOWED Always.
 */
static status_t
rootfs_read_pages(fs_volume* _volume, fs_vnode* _v, void* cookie, off_t pos,
	const iovec* vecs, size_t count, size_t* _numBytes)
{
	return B_NOT_ALLOWED;
}


/**
 * @brief Write pages to a rootfs vnode — not allowed.
 *
 * @param _volume   The rootfs volume (unused).
 * @param _v        The vnode (unused).
 * @param cookie    The per-open cookie (unused).
 * @param pos       Page offset (unused).
 * @param vecs      I/O vector array (unused).
 * @param count     Number of vectors (unused).
 * @param _numBytes Input/output byte count (unused).
 * @retval B_NOT_ALLOWED Always.
 */
static status_t
rootfs_write_pages(fs_volume* _volume, fs_vnode* _v, void* cookie, off_t pos,
	const iovec* vecs, size_t count, size_t* _numBytes)
{
	return B_NOT_ALLOWED;
}


/**
 * @brief Read the target path of a symbolic link vnode.
 *
 * Copies the symlink's stored path into @p buffer (up to @p *_bufferSize
 * bytes) and updates @p *_bufferSize with the full length of the target.
 *
 * @param _volume     The rootfs volume (unused).
 * @param _link       The symlink vnode.
 * @param buffer      Destination buffer for the link target string.
 * @param _bufferSize Input: capacity of @p buffer; output: actual target length.
 * @retval B_OK        Target copied successfully.
 * @retval B_BAD_VALUE @p _link is not a symbolic link.
 */
static status_t
rootfs_read_link(fs_volume* _volume, fs_vnode* _link, char* buffer,
	size_t* _bufferSize)
{
	struct rootfs_vnode* link = (rootfs_vnode*)_link->private_node;

	if (!S_ISLNK(link->stream.type))
		return B_BAD_VALUE;

	memcpy(buffer, link->stream.symlink.path, min_c(*_bufferSize,
		link->stream.symlink.length));

	*_bufferSize = link->stream.symlink.length;

	return B_OK;
}


/**
 * @brief Create a symbolic link entry in a rootfs directory.
 *
 * Checks write permission, acquires the FS write lock, verifies the name
 * is free, creates a new S_IFLNK vnode, inserts it into the parent,
 * duplicates the target path string into the vnode's symlink stream, and
 * fires an entry-created notification.
 *
 * @param _volume The rootfs volume.
 * @param _dir    The parent directory vnode.
 * @param name    The name of the new symlink entry.
 * @param path    The target path string the symlink should point to.
 * @param mode    Permission bits (masked with S_IUMSK).
 * @retval B_OK          Symlink created.
 * @retval B_FILE_EXISTS An entry with that name already exists.
 * @retval B_NO_MEMORY   Allocation failure.
 * @retval B_NOT_ALLOWED Caller lacks write permission.
 */
static status_t
rootfs_symlink(fs_volume* _volume, fs_vnode* _dir, const char* name,
	const char* path, int mode)
{
	struct rootfs* fs = (rootfs*)_volume->private_volume;
	struct rootfs_vnode* dir = (rootfs_vnode*)_dir->private_node;
	struct rootfs_vnode* vnode;

	TRACE(("rootfs_symlink: dir %p, name = '%s', path = %s\n", dir, name, path));

	status_t status = rootfs_check_permissions(dir, W_OK);
	if (status != B_OK)
		return status;

	WriteLocker locker(fs->lock);

	vnode = rootfs_find_in_dir(dir, name);
	if (vnode != NULL)
		return B_FILE_EXISTS;

	TRACE(("rootfs_create: creating new symlink\n"));
	vnode = rootfs_create_vnode(fs, dir, name, S_IFLNK | (mode & S_IUMSK));
	if (vnode == NULL)
		return B_NO_MEMORY;

	rootfs_insert_in_dir(fs, dir, vnode);
	fs->vnode_list_hash->Insert(vnode);

	vnode->stream.symlink.path = strdup(path);
	if (vnode->stream.symlink.path == NULL) {
		rootfs_delete_vnode(fs, vnode, false);
		return B_NO_MEMORY;
	}
	vnode->stream.symlink.length = strlen(path);

	entry_cache_add(fs->volume->id, dir->id, name, vnode->id);

	notify_entry_created(fs->id, dir->id, name, vnode->id);

	return B_OK;
}


/**
 * @brief Unlink a non-directory entry from a rootfs directory.
 *
 * Verifies write permission and delegates to rootfs_remove() with
 * @p isDirectory=false.
 *
 * @param _volume The rootfs volume.
 * @param _dir    The directory containing the entry to unlink.
 * @param name    The name of the entry to remove.
 * @retval B_OK             Entry removed.
 * @retval B_ENTRY_NOT_FOUND Entry not found.
 * @retval B_IS_A_DIRECTORY  Entry is a directory; use rootfs_remove_dir().
 * @retval B_NOT_ALLOWED     Caller lacks write permission.
 */
static status_t
rootfs_unlink(fs_volume* _volume, fs_vnode* _dir, const char* name)
{
	struct rootfs* fs = (rootfs*)_volume->private_volume;
	struct rootfs_vnode* dir = (rootfs_vnode*)_dir->private_node;

	TRACE(("rootfs_unlink: dir %p (0x%Lx), name '%s'\n", dir, dir->id, name));

	status_t status = rootfs_check_permissions(dir, W_OK);
	if (status != B_OK)
		return status;

	return rootfs_remove(fs, dir, name, false);
}


/**
 * @brief Rename (move) an entry between rootfs directories.
 *
 * Acquires the FS write lock, looks up the source entry, verifies that the
 * destination directory is not a subdirectory of the source vnode, removes
 * any existing entry at the destination (if it is an empty directory),
 * renames the vnode in place, and fires an entry-moved notification.
 *
 * @note Renaming "/boot" from the root is explicitly forbidden.
 *
 * @param _volume   The rootfs volume.
 * @param _fromDir  The source directory vnode.
 * @param fromName  The current entry name.
 * @param _toDir    The destination directory vnode.
 * @param toName    The new entry name.
 * @retval B_OK             Entry renamed.
 * @retval B_ENTRY_NOT_FOUND Source entry not found.
 * @retval B_BAD_VALUE      Destination is a subdirectory of the source.
 * @retval B_NAME_IN_USE    Destination is a non-empty directory.
 * @retval B_NO_MEMORY      Buffer allocation for the new name failed.
 * @retval B_NOT_ALLOWED    Caller lacks write permission on source or dest.
 * @retval EPERM            Attempted to rename "/boot".
 */
static status_t
rootfs_rename(fs_volume* _volume, fs_vnode* _fromDir, const char* fromName,
	fs_vnode* _toDir, const char* toName)
{
	struct rootfs* fs = (rootfs*)_volume->private_volume;
	struct rootfs_vnode* fromDirectory = (rootfs_vnode*)_fromDir->private_node;
	struct rootfs_vnode* toDirectory = (rootfs_vnode*)_toDir->private_node;

	TRACE(("rootfs_rename: from %p (0x%Lx, %s), fromName '%s', to %p "
		"(0x%Lx, %s), toName '%s'\n", fromDirectory, fromDirectory->id,
		fromDirectory->name != NULL ? fromDirectory->name : "NULL",
		fromName, toDirectory, toDirectory->id,
		toDirectory->name != NULL ? toDirectory->name : "NULL",
		toName));

	// Prevent renaming /boot, since that will stop everything from working.
	// TODO: This should be solved differently. Either root should still be
	// able to do this or a mechanism should be introduced that does this
	// at the VFS level, for example by checking nodes for a specific
	// attribute.
	if (fromDirectory->id == 1 && strcmp(fromName, "boot") == 0)
		return EPERM;

	status_t status = rootfs_check_permissions(fromDirectory, W_OK);
	if (status == B_OK)
		status = rootfs_check_permissions(toDirectory, W_OK);
	if (status != B_OK)
		return status;

	WriteLocker locker(fs->lock);

	struct rootfs_vnode* vnode = rootfs_find_in_dir(fromDirectory, fromName);
	if (vnode == NULL)
		return B_ENTRY_NOT_FOUND;

	// make sure the target is not a subdirectory of us
	struct rootfs_vnode* parent = toDirectory->parent;
	while (parent != NULL && parent != parent->parent) {
		if (parent == vnode)
			return B_BAD_VALUE;

		parent = parent->parent;
	}

	struct rootfs_vnode* targetVnode = rootfs_find_in_dir(toDirectory, toName);
	if (targetVnode != NULL) {
		// target node exists, let's see if it is an empty directory
		if (S_ISDIR(targetVnode->stream.type)
			&& !rootfs_is_dir_empty(targetVnode))
			return B_NAME_IN_USE;

		// so we can cleanly remove it
		entry_cache_remove(fs->volume->id, toDirectory->id, toName);
		remove_node(fs, toDirectory, targetVnode);
	}

	// we try to reuse the existing name buffer if possible
	if (strlen(fromName) < strlen(toName)) {
		char* nameBuffer = strdup(toName);
		if (nameBuffer == NULL)
			return B_NO_MEMORY;

		free(vnode->name);
		vnode->name = nameBuffer;
	} else {
		// we can just copy it
		strcpy(vnode->name, toName);
	}

	// remove it from the dir
	entry_cache_remove(fs->volume->id, fromDirectory->id, fromName);
	rootfs_remove_from_dir(fs, fromDirectory, vnode);

	// Add it back to the dir with the new name.
	// We need to do this even in the same directory,
	// so that it keeps sorted correctly.
	rootfs_insert_in_dir(fs, toDirectory, vnode);

	entry_cache_add(fs->volume->id, toDirectory->id, toName, vnode->id);

	notify_entry_moved(fs->id, fromDirectory->id, fromName, toDirectory->id,
		toName, vnode->id);

	return B_OK;
}


/**
 * @brief Read the stat(2) metadata of a rootfs vnode.
 *
 * Fills a struct stat with the vnode's inode ID, mode, ownership,
 * timestamps, and size. The size is non-zero only for symlinks (target
 * path length); directories always report size 0.
 *
 * @param _volume The rootfs volume.
 * @param _v      The vnode whose metadata is requested.
 * @param stat    Output buffer to fill.
 * @retval B_OK Always.
 */
static status_t
rootfs_read_stat(fs_volume* _volume, fs_vnode* _v, struct stat* stat)
{
	struct rootfs* fs = (rootfs*)_volume->private_volume;
	struct rootfs_vnode* vnode = (rootfs_vnode*)_v->private_node;

	TRACE(("rootfs_read_stat: vnode %p (0x%Lx), stat %p\n", vnode, vnode->id,
		stat));

	// stream exists, but we know to return size 0, since we can only hold
	// directories
	stat->st_dev = fs->id;
	stat->st_ino = vnode->id;
	if (S_ISLNK(vnode->stream.type))
		stat->st_size = vnode->stream.symlink.length;
	else
		stat->st_size = 0;
	stat->st_mode = vnode->stream.type;

	stat->st_nlink = 1;
	stat->st_blksize = 65536;
	stat->st_blocks = 0;

	stat->st_uid = vnode->uid;
	stat->st_gid = vnode->gid;

	stat->st_atim.tv_sec = real_time_clock();
	stat->st_atim.tv_nsec = 0;
	stat->st_mtim = stat->st_ctim = vnode->modification_time;
	stat->st_crtim = vnode->creation_time;

	return B_OK;
}


/**
 * @brief Write (update) the stat(2) metadata of a rootfs vnode.
 *
 * Applies a subset of stat fields controlled by @p statMask. Mode, UID, GID,
 * modification time, and creation time may all be updated subject to
 * permission checks. Resizing is never allowed.
 *
 * @param _volume  The rootfs volume.
 * @param _vnode   The vnode whose metadata is to be updated.
 * @param stat     The new metadata values.
 * @param statMask Bitmask of B_STAT_* flags indicating which fields to apply.
 * @retval B_OK         Metadata updated; stat-changed notification sent.
 * @retval B_BAD_VALUE  B_STAT_SIZE was set in @p statMask.
 * @retval B_NOT_ALLOWED Caller lacks the privilege for the requested change.
 */
static status_t
rootfs_write_stat(fs_volume* _volume, fs_vnode* _vnode, const struct stat* stat,
	uint32 statMask)
{
	struct rootfs* fs = (rootfs*)_volume->private_volume;
	struct rootfs_vnode* vnode = (rootfs_vnode*)_vnode->private_node;

	const uid_t uid = geteuid();
	const bool isOwnerOrRoot = uid == 0 || uid == (uid_t)vnode->uid;
	const bool hasWriteAccess = rootfs_check_permissions(vnode, W_OK) == B_OK;

	TRACE(("rootfs_write_stat: vnode %p (0x%Lx), stat %p\n", vnode, vnode->id,
		stat));

	// we cannot change the size of anything
	if (statMask & B_STAT_SIZE)
		return B_BAD_VALUE;

	WriteLocker locker(fs->lock);

	if ((statMask & B_STAT_MODE) != 0) {
		// only the user or root can do that
		if (!isOwnerOrRoot)
			return B_NOT_ALLOWED;

		vnode->stream.type = (vnode->stream.type & ~S_IUMSK)
			| (stat->st_mode & S_IUMSK);
	}

	if ((statMask & B_STAT_UID) != 0) {
		// only root should be allowed
		if (uid != 0)
			return B_NOT_ALLOWED;
		vnode->uid = stat->st_uid;
	}

	if ((statMask & B_STAT_GID) != 0) {
		// only user or root can do that
		if (!isOwnerOrRoot)
			return B_NOT_ALLOWED;
		vnode->gid = stat->st_gid;
	}

	if ((statMask & B_STAT_MODIFICATION_TIME) != 0) {
		if (!isOwnerOrRoot && !hasWriteAccess)
			return B_NOT_ALLOWED;
		vnode->modification_time = stat->st_mtim;
	}

	if ((statMask & B_STAT_CREATION_TIME) != 0) {
		if (!isOwnerOrRoot && !hasWriteAccess)
			return B_NOT_ALLOWED;
		vnode->creation_time = stat->st_crtim;
	}

	locker.Unlock();

	notify_stat_changed(fs->id, get_parent_id(vnode), vnode->id, statMask);
	return B_OK;
}


/**
 * @brief Create a special (device, pipe, etc.) node inside a rootfs directory.
 *
 * Allocates a new vnode with the given mode, optionally inserts it into the
 * parent directory (if @p name is non-NULL), publishes it to the VFS using
 * the provided sub-vnode operations, and notifies the node monitor.
 *
 * @param _volume    The rootfs volume.
 * @param _dir       The parent directory vnode.
 * @param name       The entry name; if NULL the node is published as removed.
 * @param subVnode   The sub-vnode supplied by the caller; if NULL the super
 *                   vnode is used as both super and sub.
 * @param mode       The mode and type bits for the new node.
 * @param flags      VFS publish flags; B_VNODE_PUBLISH_REMOVED is added when
 *                   @p name is NULL.
 * @param _superVnode Output: filled with the rootfs-level vnode info.
 * @param _nodeID    Output: inode ID of the created node.
 * @retval B_OK          Node created and published.
 * @retval B_FILE_EXISTS An entry with that name already exists.
 * @retval B_NO_MEMORY   Vnode allocation failed.
 */
static status_t
rootfs_create_special_node(fs_volume* _volume, fs_vnode* _dir, const char* name,
	fs_vnode* subVnode, mode_t mode, uint32 flags, fs_vnode* _superVnode,
	ino_t* _nodeID)
{
	struct rootfs* fs = (rootfs*)_volume->private_volume;
	struct rootfs_vnode* dir = (rootfs_vnode*)_dir->private_node;
	struct rootfs_vnode* vnode;

	WriteLocker locker(fs->lock);

	if (name != NULL) {
		vnode = rootfs_find_in_dir(dir, name);
		if (vnode != NULL)
			return B_FILE_EXISTS;
	}

	vnode = rootfs_create_vnode(fs, dir, name, mode);
	if (vnode == NULL)
		return B_NO_MEMORY;

	if (name != NULL)
		rootfs_insert_in_dir(fs, dir, vnode);
	else
		flags |= B_VNODE_PUBLISH_REMOVED;

	fs->vnode_list_hash->Insert(vnode);

	_superVnode->private_node = vnode;
	_superVnode->ops = &sVnodeOps;
	*_nodeID = vnode->id;

	if (subVnode == NULL)
		subVnode = _superVnode;

	status_t status = publish_vnode(fs->volume, vnode->id,
		subVnode->private_node, subVnode->ops, mode, flags);
	if (status != B_OK) {
		if (name != NULL)
			rootfs_remove_from_dir(fs, dir, vnode);
		rootfs_delete_vnode(fs, vnode, false);
		return status;
	}

	if (name != NULL) {
		entry_cache_add(fs->volume->id, dir->id, name, vnode->id);
		notify_entry_created(fs->id, dir->id, name, vnode->id);
	}

	return B_OK;
}


/**
 * @brief Handle module init/uninit operations for the rootfs module.
 *
 * Called by the module subsystem with B_MODULE_INIT or B_MODULE_UNINIT.
 * rootfs requires no per-module initialisation beyond what the mount
 * call performs.
 *
 * @param op  The module operation code (B_MODULE_INIT or B_MODULE_UNINIT).
 * @param ... Additional arguments (unused).
 * @retval B_OK    For B_MODULE_INIT and B_MODULE_UNINIT.
 * @retval B_ERROR For any unrecognised operation code.
 */
static status_t
rootfs_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			return B_OK;

		case B_MODULE_UNINIT:
			return B_OK;

		default:
			return B_ERROR;
	}
}


namespace {

fs_volume_ops sVolumeOps = {
	&rootfs_unmount,
	NULL,
	NULL,
	&rootfs_sync,
	&rootfs_get_vnode,

	// the other operations are not supported (indices, queries)
	NULL,
};

fs_vnode_ops sVnodeOps = {
	&rootfs_lookup,
	&rootfs_get_vnode_name,

	&rootfs_put_vnode,
	&rootfs_remove_vnode,

	&rootfs_can_page,
	&rootfs_read_pages,
	&rootfs_write_pages,

	NULL,	// io()
	NULL,	// cancel_io()

	NULL,	// get_file_map()

	/* common */
	&rootfs_ioctl,
	NULL,	// fs_set_flags()
	NULL,	// select
	NULL,	// deselect
	&rootfs_fsync,

	&rootfs_read_link,
	&rootfs_symlink,
	NULL,	// fs_link()
	&rootfs_unlink,
	&rootfs_rename,

	NULL,	// fs_access()
	&rootfs_read_stat,
	&rootfs_write_stat,
	NULL,

	/* file */
	&rootfs_create,
	&rootfs_open,
	&rootfs_close,
	&rootfs_free_cookie,
	&rootfs_read,
	&rootfs_write,

	/* directory */
	&rootfs_create_dir,
	&rootfs_remove_dir,
	&rootfs_open_dir,
	&rootfs_close,			// same as for files - it does nothing, anyway
	&rootfs_free_dir_cookie,
	&rootfs_read_dir,
	&rootfs_rewind_dir,

	/* attribute directory operations */
	NULL,	// open_attr_dir
	NULL,	// close_attr_dir
	NULL,	// free_attr_dir_cookie
	NULL,	// read_attr_dir
	NULL,	// rewind_attr_dir

	/* attribute operations */
	NULL,	// create_attr
	NULL,	// open_attr
	NULL,	// close_attr
	NULL,	// free_attr_cookie
	NULL,	// read_attr
	NULL,	// write_attr

	NULL,	// read_attr_stat
	NULL,	// write_attr_stat
	NULL,	// rename_attr
	NULL,	// remove_attr

	/* support for node and FS layers */
	&rootfs_create_special_node,
	NULL,	// get_super_vnode,
};

}	// namespace

file_system_module_info gRootFileSystem = {
	{
		"file_systems/rootfs" B_CURRENT_FS_API_VERSION,
		0,
		rootfs_std_ops,
	},

	"rootfs",				// short_name
	"Root File System",		// pretty_name
	0,						// DDM flags

	NULL,	// identify_partition()
	NULL,	// scan_partition()
	NULL,	// free_identify_partition_cookie()
	NULL,	// free_partition_content_cookie()

	&rootfs_mount,
};
