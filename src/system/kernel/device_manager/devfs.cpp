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
 *   Copyright 2002-2016, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file devfs.cpp
 * @brief Device filesystem (/dev) — VFS layer over kernel device nodes.
 *
 * devfs is the in-memory virtual filesystem mounted at /dev. It exposes
 * device nodes (created by the device manager and legacy driver layer) as
 * regular files in the VFS hierarchy. Implements the fs_vnode_ops and
 * fs_volume_ops interfaces so the VFS can open, read, write, and ioctl
 * device files using the standard file operations.
 *
 * @see device_manager.cpp, legacy_drivers.cpp
 */


#include <fs/devfs.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <Drivers.h>
#include <KernelExport.h>
#include <NodeMonitor.h>

#include <arch/cpu.h>
#include <AutoDeleter.h>
#include <boot/kernel_args.h>
#include <boot_device.h>
#include <debug.h>
#include <elf.h>
#include <FindDirectory.h>
#include <fs/devfs.h>
#include <fs/KPath.h>
#include <fs/node_monitor.h>
#include <kdevice_manager.h>
#include <lock.h>
#include <Notifications.h>
#include <util/AutoLock.h>
#include <util/fs_trim_support.h>
#include <vfs.h>
#include <vm/vm.h>
#include <wait_for_objects.h>

#include "BaseDevice.h"
#include "FileDevice.h"
#include "IORequest.h"
#include "legacy_drivers.h"


//#define TRACE_DEVFS
#ifdef TRACE_DEVFS
#	define TRACE(x) dprintf x
#else
#	define TRACE(x)
#endif


namespace {

struct devfs_partition {
	struct devfs_vnode*	raw_device;
	partition_info		info;
};

struct driver_entry;

enum {
	kNotScanned = 0,
	kBootScan,
	kNormalScan,
};

struct devfs_stream {
	mode_t				type;
	union {
		struct stream_dir {
			struct devfs_vnode*		dir_head;
			struct list				cookies;
			mutex					scan_lock;
			int32					scanned;
		} dir;
		struct stream_dev {
			BaseDevice*				device;
			struct devfs_partition*	partition;
		} dev;
		struct stream_symlink {
			const char*				path;
			size_t					length;
		} symlink;
	} u;
};

struct devfs_vnode {
	struct devfs_vnode*	all_next;
	ino_t				id;
	char*				name;
	timespec			modification_time;
	timespec			creation_time;
	uid_t				uid;
	gid_t				gid;
	struct devfs_vnode*	parent;
	struct devfs_vnode*	dir_next;
	struct devfs_stream	stream;
};

#define DEVFS_HASH_SIZE 16


struct NodeHash {
	typedef ino_t			KeyType;
	typedef	devfs_vnode		ValueType;

	size_t HashKey(KeyType key) const
	{
		return key ^ (key >> 32);
	}

	size_t Hash(ValueType* value) const
	{
		return HashKey(value->id);
	}

	bool Compare(KeyType key, ValueType* value) const
	{
		return value->id == key;
	}

	ValueType*& GetLink(ValueType* value) const
	{
		return value->all_next;
	}
};

typedef BOpenHashTable<NodeHash> NodeTable;

struct devfs {
	dev_t				id;
	fs_volume*			volume;
	recursive_lock		lock;
	int32				next_vnode_id;
	NodeTable*			vnode_hash;
	struct devfs_vnode*	root_vnode;
};

struct devfs_dir_cookie {
	struct list_link	link;
	struct devfs_vnode*	current;
	int32				state;	// iteration state
};

struct devfs_cookie {
	void*				device_cookie;
};

// directory iteration states
enum {
	ITERATION_STATE_DOT		= 0,
	ITERATION_STATE_DOT_DOT	= 1,
	ITERATION_STATE_OTHERS	= 2,
	ITERATION_STATE_BEGIN	= ITERATION_STATE_DOT,
};

// extern only to make forward declaration possible
extern fs_volume_ops kVolumeOps;
extern fs_vnode_ops kVnodeOps;

} // namespace


static status_t get_node_for_path(struct devfs* fs, const char* path,
	struct devfs_vnode** _node);
static void get_device_name(struct devfs_vnode* vnode, char* buffer,
	size_t size);
static status_t unpublish_node(struct devfs* fs, devfs_vnode* node,
	mode_t type);
static status_t publish_device(struct devfs* fs, const char* path,
	BaseDevice* device);


// The one and only allowed devfs instance
static struct devfs* sDeviceFileSystem = NULL;


//	#pragma mark - devfs private


/**
 * @brief Return the current wall-clock time as a timespec.
 *
 * @return A timespec populated from real_time_clock_usecs().
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
 * @brief Return the inode id of @p vnode's parent, or -1 if there is none.
 *
 * @param vnode The vnode whose parent id is queried.
 * @return Parent inode id, or -1 when @p vnode has no parent.
 */
static ino_t
get_parent_id(struct devfs_vnode* vnode)
{
	if (vnode->parent != NULL)
		return vnode->parent->id;
	return -1;
}


/**
 * @brief Return the current driver-scan mode (boot or normal).
 *
 * @return kNormalScan when a boot device is available, kBootScan otherwise.
 */
static int32
scan_mode(void)
{
	// We may scan every device twice:
	//  - once before there is a boot device,
	//  - and once when there is one

	return gBootDevice >= 0 ? kNormalScan : kBootScan;
}


/**
 * @brief Trigger a lazy driver scan for @p dir if it has not yet been scanned
 *        at the current scan level.
 *
 * @param dir  Directory vnode to scan; must be a directory stream.
 * @retval B_OK        Scan succeeded or was not needed.
 * @retval B_NO_MEMORY Path buffer allocation failed.
 */
static status_t
scan_for_drivers_if_needed(devfs_vnode* dir)
{
	ASSERT(S_ISDIR(dir->stream.type));

	MutexLocker _(dir->stream.u.dir.scan_lock);

	if (dir->stream.u.dir.scanned >= scan_mode())
		return B_OK;

	KPath path;
	if (path.InitCheck() != B_OK)
		return B_NO_MEMORY;

	get_device_name(dir, path.LockBuffer(), path.BufferSize());
	path.UnlockBuffer();

	TRACE(("scan_for_drivers_if_needed: mode %" B_PRId32 ": %s\n",
		scan_mode(), path.Path()));

	// scan for drivers at this path
	static int32 updateCycle = 1;
	device_manager_probe(path.Path(), updateCycle++);
	legacy_driver_probe(path.Path());

	dir->stream.u.dir.scanned = scan_mode();
	return B_OK;
}


/**
 * @brief Initialize @p vnode as a directory stream with the given permissions.
 *
 * @param vnode       Vnode to initialize.
 * @param permissions Permission bits (e.g. 0755) applied to S_IFDIR.
 */
static void
init_directory_vnode(struct devfs_vnode* vnode, int permissions)
{
	vnode->stream.type = S_IFDIR | permissions;
		mutex_init(&vnode->stream.u.dir.scan_lock, "devfs scan");
	vnode->stream.u.dir.dir_head = NULL;
	list_init(&vnode->stream.u.dir.cookies);
}


/**
 * @brief Allocate and minimally populate a new devfs vnode.
 *
 * The caller is responsible for further initializing the stream type and
 * inserting the vnode into the hash table and parent directory.
 *
 * @param fs     The devfs instance that owns the vnode.
 * @param parent Parent vnode used to inherit the group id; may be NULL.
 * @param name   Name of the new entry; will be duplicated internally.
 * @return Newly allocated vnode, or NULL on allocation failure.
 */
static struct devfs_vnode*
devfs_create_vnode(struct devfs* fs, devfs_vnode* parent, const char* name)
{
	struct devfs_vnode* vnode;

	vnode = (struct devfs_vnode*)malloc(sizeof(struct devfs_vnode));
	if (vnode == NULL)
		return NULL;

	memset(vnode, 0, sizeof(struct devfs_vnode));
	vnode->id = fs->next_vnode_id++;

	vnode->name = strdup(name);
	if (vnode->name == NULL) {
		free(vnode);
		return NULL;
	}

	vnode->creation_time = vnode->modification_time = current_timespec();
	vnode->uid = geteuid();
	vnode->gid = parent ? parent->gid : getegid();
		// inherit group from parent if possible

	return vnode;
}


/**
 * @brief Remove @p vnode from the hash table and free all associated memory.
 *
 * For character devices the underlying BaseDevice::Removed() is called (or the
 * raw-device reference is released for partitions). For directories the scan
 * mutex is destroyed.
 *
 * @param fs           The owning devfs instance.
 * @param vnode        Vnode to delete.
 * @param forceDelete  When false, refuses deletion if the vnode is still linked
 *                     in a directory or is a non-empty directory.
 * @retval B_OK          Vnode deleted successfully.
 * @retval B_NOT_ALLOWED @p forceDelete is false and the vnode is still in use.
 */
static status_t
devfs_delete_vnode(struct devfs* fs, struct devfs_vnode* vnode,
	bool forceDelete)
{
	// Can't delete it if it's in a directory or is a directory
	// and has children
	if (!forceDelete && ((S_ISDIR(vnode->stream.type)
				&& vnode->stream.u.dir.dir_head != NULL)
			|| vnode->dir_next != NULL))
		return B_NOT_ALLOWED;

	// remove it from the global hash table
	fs->vnode_hash->Remove(vnode);

	if (S_ISCHR(vnode->stream.type)) {
		if (vnode->stream.u.dev.partition == NULL) {
			// pass the call through to the underlying device
			vnode->stream.u.dev.device->Removed();
		} else {
			// for partitions, we have to release the raw device but must
			// not free the device info as it was inherited from the raw
			// device and is still in use there
			put_vnode(fs->volume, vnode->stream.u.dev.partition->raw_device->id);
		}
	} else if (S_ISDIR(vnode->stream.type)) {
		mutex_destroy(&vnode->stream.u.dir.scan_lock);
	}

	free(vnode->name);
	free(vnode);

	return B_OK;
}


/*! Makes sure none of the dircookies point to the vnode passed in */
/**
 * @brief Advance any directory cookies that currently point at @p vnode to its
 *        successor, so that removal does not leave dangling pointers.
 *
 * @param dir   Directory vnode whose cookie list is iterated.
 * @param vnode Vnode that is about to be removed from @p dir.
 */
static void
update_dir_cookies(struct devfs_vnode* dir, struct devfs_vnode* vnode)
{
	struct devfs_dir_cookie* cookie = NULL;

	while ((cookie = (devfs_dir_cookie*)list_get_next_item(
			&dir->stream.u.dir.cookies, cookie)) != NULL) {
		if (cookie->current == vnode)
			cookie->current = vnode->dir_next;
	}
}


/**
 * @brief Look up a child entry by name inside a directory vnode.
 *
 * Handles the special names "." and "..".
 *
 * @param dir   Directory vnode to search.
 * @param path  Name of the entry to find (single component, no slashes).
 * @return Matching child vnode, or NULL if not found.
 */
static struct devfs_vnode*
devfs_find_in_dir(struct devfs_vnode* dir, const char* path)
{
	struct devfs_vnode* vnode;

	if (!S_ISDIR(dir->stream.type))
		return NULL;

	if (!strcmp(path, "."))
		return dir;
	if (!strcmp(path, ".."))
		return dir->parent;

	for (vnode = dir->stream.u.dir.dir_head; vnode; vnode = vnode->dir_next) {
		//TRACE(("devfs_find_in_dir: looking at entry '%s'\n", vnode->name));
		if (strcmp(vnode->name, path) == 0) {
			//TRACE(("devfs_find_in_dir: found it at %p\n", vnode));
			return vnode;
		}
	}
	return NULL;
}


/**
 * @brief Insert @p vnode into @p dir, maintaining alphabetical order.
 *
 * Optionally sends a node-monitor entry-created notification.
 *
 * @param dir     Destination directory vnode.
 * @param vnode   Vnode to insert.
 * @param notify  When true (default) a VFS node-monitor notification is sent.
 * @retval B_OK        Inserted successfully.
 * @retval B_BAD_VALUE @p dir is not a directory.
 */
static status_t
devfs_insert_in_dir(struct devfs_vnode* dir, struct devfs_vnode* vnode,
	bool notify = true)
{
	if (!S_ISDIR(dir->stream.type))
		return B_BAD_VALUE;

	// make sure the directory stays sorted alphabetically

	devfs_vnode* node = dir->stream.u.dir.dir_head;
	devfs_vnode* last = NULL;
	while (node && strcmp(node->name, vnode->name) < 0) {
		last = node;
		node = node->dir_next;
	}
	if (last == NULL) {
		// the new vnode is the first entry in the list
		vnode->dir_next = dir->stream.u.dir.dir_head;
		dir->stream.u.dir.dir_head = vnode;
	} else {
		// insert after that node
		vnode->dir_next = last->dir_next;
		last->dir_next = vnode;
	}

	vnode->parent = dir;
	dir->modification_time = current_timespec();

	if (notify) {
		notify_entry_created(sDeviceFileSystem->id, dir->id, vnode->name,
			vnode->id);
		notify_stat_changed(sDeviceFileSystem->id, get_parent_id(dir), dir->id,
			B_STAT_MODIFICATION_TIME);
	}
	return B_OK;
}


/**
 * @brief Remove @p removeNode from @p dir's child list.
 *
 * Updates all open directory cookies and optionally sends node-monitor
 * notifications.
 *
 * @param dir        Directory vnode.
 * @param removeNode Child vnode to unlink.
 * @param notify     When true (default) VFS node-monitor notifications are sent.
 * @retval B_OK             Removed successfully.
 * @retval B_ENTRY_NOT_FOUND @p removeNode was not found in @p dir.
 */
static status_t
devfs_remove_from_dir(struct devfs_vnode* dir, struct devfs_vnode* removeNode,
	bool notify = true)
{
	struct devfs_vnode* vnode = dir->stream.u.dir.dir_head;
	struct devfs_vnode* lastNode = NULL;

	for (; vnode != NULL; lastNode = vnode, vnode = vnode->dir_next) {
		if (vnode == removeNode) {
			// make sure no dircookies point to this vnode
			update_dir_cookies(dir, vnode);

			if (lastNode)
				lastNode->dir_next = vnode->dir_next;
			else
				dir->stream.u.dir.dir_head = vnode->dir_next;
			vnode->dir_next = NULL;
			dir->modification_time = current_timespec();

			if (notify) {
				notify_entry_removed(sDeviceFileSystem->id, dir->id, vnode->name,
					vnode->id);
				notify_stat_changed(sDeviceFileSystem->id, get_parent_id(dir),
					dir->id, B_STAT_MODIFICATION_TIME);
			}
			return B_OK;
		}
	}
	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Create a partition vnode that maps a slice of @p device.
 *
 * Allocates a devfs_partition descriptor, takes a reference on the raw
 * device vnode, creates a new child vnode in the same directory as
 * @p device, and inserts it into the hash table.
 *
 * @param fs      The owning devfs instance (must be locked by caller).
 * @param device  Raw device vnode that the partition lives on.
 * @param name    Name for the new partition entry.
 * @param info    Partition geometry (offset, size, block size, …).
 * @retval B_OK        Partition created and published.
 * @retval B_BAD_VALUE @p device is not a character device, is already a
 *                     partition, @p info.size is negative, or @p name is
 *                     already in use.
 * @retval B_NO_MEMORY Allocation failure.
 */
static status_t
add_partition(struct devfs* fs, struct devfs_vnode* device, const char* name,
	const partition_info& info)
{
	struct devfs_vnode* partitionNode;
	status_t status;

	if (!S_ISCHR(device->stream.type))
		return B_BAD_VALUE;

	// we don't support nested partitions
	if (device->stream.u.dev.partition != NULL)
		return B_BAD_VALUE;

	// reduce checks to a minimum - things like negative offsets could be useful
	if (info.size < 0)
		return B_BAD_VALUE;

	// create partition
	struct devfs_partition* partition = (struct devfs_partition*)malloc(
		sizeof(struct devfs_partition));
	if (partition == NULL)
		return B_NO_MEMORY;

	memcpy(&partition->info, &info, sizeof(partition_info));

	RecursiveLocker locker(fs->lock);

	// you cannot change a partition once set
	if (devfs_find_in_dir(device->parent, name)) {
		status = B_BAD_VALUE;
		goto err1;
	}

	// increase reference count of raw device -
	// the partition device really needs it
	status = get_vnode(fs->volume, device->id, (void**)&partition->raw_device);
	if (status < B_OK)
		goto err1;

	// now create the partition vnode
	partitionNode = devfs_create_vnode(fs, device->parent, name);
	if (partitionNode == NULL) {
		status = B_NO_MEMORY;
		goto err2;
	}

	partitionNode->stream.type = device->stream.type;
	partitionNode->stream.u.dev.device = device->stream.u.dev.device;
	partitionNode->stream.u.dev.partition = partition;

	fs->vnode_hash->Insert(partitionNode);
	devfs_insert_in_dir(device->parent, partitionNode);

	TRACE(("add_partition(name = %s, offset = %" B_PRIdOFF
		", size = %" B_PRIdOFF ")\n",
		name, info.offset, info.size));
	return B_OK;

err2:
	put_vnode(fs->volume, device->id);
err1:
	free(partition);
	return status;
}


/**
 * @brief Clamp and translate a (offset, size) byte range into partition space.
 *
 * @param partition  Partition descriptor containing offset and size limits.
 * @param offset     In: offset relative to partition start.  Out: absolute
 *                   device offset.
 * @param size       In/Out: request size, clamped to the partition boundary.
 * @note Asserts that @p offset is non-negative and within the partition.
 */
static inline void
translate_partition_access(devfs_partition* partition, off_t& offset,
	size_t& size)
{
	ASSERT(offset >= 0);
	ASSERT(offset < partition->info.size);

	size = (size_t)min_c((off_t)size, partition->info.size - offset);
	offset += partition->info.offset;
}


/**
 * @brief Translate a (offset, size) range expressed as uint64 values into
 *        absolute device coordinates, with overflow and bounds checking.
 *
 * @param partition  Partition descriptor.
 * @param offset     In: partition-relative offset.  Out: device-absolute offset.
 * @param size       In/Out: request size, clamped to the partition boundary.
 * @return true if the translation is valid and the range is within the
 *         partition; false if the range is out of bounds or would overflow.
 */
static bool
translate_partition_access(devfs_partition* partition, uint64& offset,
	uint64& size)
{
	const off_t partitionSize = partition->info.size;
	const off_t partitionOffset = partition->info.offset;

	// Check that off_t values can be cast to uint64,
	// partition offset can theoretically be negative
	ASSERT(partitionSize >= 0);
	STATIC_ASSERT(sizeof(partitionSize) <= sizeof(uint64));
	STATIC_ASSERT(sizeof(partitionOffset) <= sizeof(uint64));

	// Check that calculations give expected results
	if (offset >= (uint64)partitionSize)
		return false;
	if (partitionOffset >= 0 && offset > UINT64_MAX - (uint64)partitionOffset)
		return false;
	if (partitionOffset < 0 && offset < (uint64)-partitionOffset)
		return false;

	size = min_c(size, (uint64)partitionSize - offset);
	if (partitionOffset >= 0)
		offset += (uint64)partitionOffset;
	else
		offset -= (uint64)-partitionOffset;

	return true;
}


/**
 * @brief Translate the offset stored in an io_request into absolute device
 *        coordinates for a partition.
 *
 * @param partition  Partition descriptor.
 * @param request    IO request whose offset is adjusted in place.
 * @note Asserts that the request fits entirely within the partition.
 */
static inline void
translate_partition_access(devfs_partition* partition, io_request* request)
{
	off_t offset = request->Offset();

	ASSERT(offset >= 0);
	ASSERT(offset + (off_t)request->Length() <= partition->info.size);

	request->SetOffset(offset + partition->info.offset);
}


/**
 * @brief Resolve a devfs-relative path string to its vnode.
 *
 * The returned vnode has its reference count incremented; the caller must
 * eventually call put_vnode().
 *
 * @param fs    The devfs instance to search.
 * @param path  Path relative to the devfs root (no leading slash).
 * @param _node Out: the resolved vnode on success.
 * @retval B_OK             Node found and reference acquired.
 * @retval B_ENTRY_NOT_FOUND Path does not exist.
 */
static status_t
get_node_for_path(struct devfs* fs, const char* path,
	struct devfs_vnode** _node)
{
	return vfs_get_fs_node_from_path(fs->volume, path, false, true,
		(void**)_node);
}


/**
 * @brief Remove a published node from its parent directory and mark it for
 *        removal by the VFS.
 *
 * @param fs    The owning devfs instance.
 * @param node  Node to unpublish; must still be in its parent directory.
 * @param type  Expected stream type mask (e.g. S_IFCHR); returns B_BAD_TYPE if
 *              the node's type does not match.
 * @retval B_OK       Unpublished successfully.
 * @retval B_BAD_TYPE Node type does not match @p type.
 */
static status_t
unpublish_node(struct devfs* fs, devfs_vnode* node, mode_t type)
{
	if ((node->stream.type & S_IFMT) != type)
		return B_BAD_TYPE;

	recursive_lock_lock(&fs->lock);

	status_t status = devfs_remove_from_dir(node->parent, node);
	if (status < B_OK)
		goto out;

	status = remove_vnode(fs->volume, node->id);

out:
	recursive_lock_unlock(&fs->lock);
	return status;
}


/**
 * @brief Insert @p node into the hash table and its parent directory atomically.
 *
 * @param fs       The owning devfs instance.
 * @param dirNode  Directory into which @p node is inserted.
 * @param node     Fully initialized vnode to publish.
 */
static void
publish_node(devfs* fs, devfs_vnode* dirNode, struct devfs_vnode* node)
{
	fs->vnode_hash->Insert(node);
	devfs_insert_in_dir(dirNode, node);
}


/**
 * @brief Create all intermediate directory components for @p path under the
 *        devfs root, if they do not already exist.
 *
 * @param fs    The owning devfs instance (caller must hold fs->lock).
 * @param path  Slash-separated directory path relative to the devfs root.
 * @retval B_OK        All directories exist or were created.
 * @retval B_NO_MEMORY Vnode or path buffer allocation failed.
 * @retval B_FILE_EXISTS A non-directory entry already exists along the path.
 * @note Caller must hold @c fs->lock (recursive).
 */
static status_t
publish_directory(struct devfs* fs, const char* path)
{
	ASSERT_LOCKED_RECURSIVE(&fs->lock);

	// copy the path over to a temp buffer so we can munge it
	KPath tempPath(path);
	if (tempPath.InitCheck() != B_OK)
		return B_NO_MEMORY;

	TRACE(("devfs: publish directory \"%s\"\n", path));
	char* temp = tempPath.LockBuffer();

	// create the path leading to the device
	// parse the path passed in, stripping out '/'

	struct devfs_vnode* dir = fs->root_vnode;
	struct devfs_vnode* vnode = NULL;
	status_t status = B_OK;
	int32 i = 0, last = 0;

	while (temp[last]) {
		if (temp[i] == '/') {
			temp[i] = '\0';
			i++;
		} else if (temp[i] != '\0') {
			i++;
			continue;
		}

		//TRACE(("\tpath component '%s'\n", &temp[last]));

		// we have a path component
		vnode = devfs_find_in_dir(dir, &temp[last]);
		if (vnode) {
			if (S_ISDIR(vnode->stream.type)) {
				last = i;
				dir = vnode;
				continue;
			}

			// we hit something on our path that's not a directory
			status = B_FILE_EXISTS;
			goto out;
		} else {
			vnode = devfs_create_vnode(fs, dir, &temp[last]);
			if (!vnode) {
				status = B_NO_MEMORY;
				goto out;
			}
		}

		// set up the new directory
		init_directory_vnode(vnode, 0755);
		publish_node(sDeviceFileSystem, dir, vnode);

		last = i;
		dir = vnode;
	}

out:
	return status;
}


/**
 * @brief Walk @p path, creating intermediate directories as needed, and return
 *        the leaf vnode and its parent without publishing the leaf.
 *
 * The leaf vnode is created but intentionally NOT inserted into the directory;
 * the caller must call publish_node() after finishing initialization. This
 * avoids sending a premature creation notification for a partially initialized
 * node.
 *
 * @param fs    The owning devfs instance (caller must hold fs->lock).
 * @param path  Slash-separated path relative to the devfs root.
 * @param _node Out: newly allocated leaf vnode (unlinked).
 * @param _dir  Out: parent directory vnode for the leaf.
 * @retval B_OK        Leaf vnode allocated and returned.
 * @retval B_NO_MEMORY Vnode or path buffer allocation failed.
 * @retval B_FILE_EXISTS A node already exists at the target path.
 * @note Caller must hold @c fs->lock (recursive).
 */
static status_t
new_node(struct devfs* fs, const char* path, struct devfs_vnode** _node,
	struct devfs_vnode** _dir)
{
	ASSERT_LOCKED_RECURSIVE(&fs->lock);

	// copy the path over to a temp buffer so we can munge it
	KPath tempPath(path);
	if (tempPath.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char* temp = tempPath.LockBuffer();

	// create the path leading to the device
	// parse the path passed in, stripping out '/'

	struct devfs_vnode* dir = fs->root_vnode;
	struct devfs_vnode* vnode = NULL;
	status_t status = B_OK;
	int32 i = 0, last = 0;
	bool atLeaf = false;

	for (;;) {
		if (temp[i] == '\0') {
			atLeaf = true; // we'll be done after this one
		} else if (temp[i] == '/') {
			temp[i] = '\0';
			i++;
		} else {
			i++;
			continue;
		}

		//TRACE(("\tpath component '%s'\n", &temp[last]));

		// we have a path component
		vnode = devfs_find_in_dir(dir, &temp[last]);
		if (vnode) {
			if (!atLeaf) {
				// we are not at the leaf of the path, so as long as
				// this is a dir we're okay
				if (S_ISDIR(vnode->stream.type)) {
					last = i;
					dir = vnode;
					continue;
				}
			}
			// we are at the leaf and hit another node
			// or we aren't but hit a non-dir node.
			// we're screwed
			status = B_FILE_EXISTS;
			goto out;
		} else {
			vnode = devfs_create_vnode(fs, dir, &temp[last]);
			if (!vnode) {
				status = B_NO_MEMORY;
				goto out;
			}
		}

		// set up the new vnode
		if (!atLeaf) {
			// this is a dir
			init_directory_vnode(vnode, 0755);
			publish_node(fs, dir, vnode);
		} else {
			// this is the last component
			// Note: We do not yet insert the node into the directory, as it
			// is not yet fully initialized. Instead we return the directory
			// vnode so that the calling function can insert it after all
			// initialization is done. This ensures that no create notification
			// is sent out for a vnode that is not yet fully valid.
			*_node = vnode;
			*_dir = dir;
			break;
		}

		last = i;
		dir = vnode;
	}

out:
	return status;
}


/**
 * @brief Create a new character-device vnode at @p path and associate it with
 *        @p device.
 *
 * Intermediate path components are created as directories if necessary. The
 * device node is assigned mode S_IFCHR|0644.
 *
 * @param fs      The owning devfs instance.
 * @param path    Destination path relative to the devfs root (no leading slash).
 * @param device  Fully initialized BaseDevice to expose at @p path.
 * @retval B_OK        Device published successfully.
 * @retval B_BAD_VALUE @p device or @p path is NULL, path is empty, or starts
 *                     with '/'.
 * @retval B_NO_MEMORY Vnode allocation failed.
 * @retval B_FILE_EXISTS A node already exists at @p path.
 */
static status_t
publish_device(struct devfs* fs, const char* path, BaseDevice* device)
{
	TRACE(("publish_device(path = \"%s\", device = %p)\n", path, device));

	if (sDeviceFileSystem == NULL) {
		panic("publish_device() called before devfs mounted\n");
		return B_ERROR;
	}

	if (device == NULL || path == NULL || path[0] == '\0' || path[0] == '/')
		return B_BAD_VALUE;

// TODO: this has to be done in the BaseDevice sub classes!
#if 0
	// are the provided device hooks okay?
	if (info->device_open == NULL || info->device_close == NULL
		|| info->device_free == NULL
		|| ((info->device_read == NULL || info->device_write == NULL)
			&& info->device_io == NULL))
		return B_BAD_VALUE;
#endif

	struct devfs_vnode* node;
	struct devfs_vnode* dirNode;
	status_t status;

	RecursiveLocker locker(&fs->lock);

	status = new_node(fs, path, &node, &dirNode);
	if (status != B_OK)
		return status;

	// all went fine, let's initialize the node
	node->stream.type = S_IFCHR | 0644;
	node->stream.u.dev.device = device;
	device->SetID(node->id);

	// the node is now fully valid and we may insert it into the dir
	publish_node(fs, dirNode, node);
	return B_OK;
}


/*!	Construct complete device name (as used for device_open()).
	This is safe to use only when the device is in use (and therefore
	cannot be unpublished during the iteration).
*/
/**
 * @brief Build the full devfs-relative path of @p vnode into @p buffer.
 *
 * Walks the parent chain to determine the depth, then fills the buffer
 * back-to-front. Safe to call only while the device is in use (i.e. while
 * a reference is held, preventing unpublish).
 *
 * @param vnode  Vnode whose path is to be constructed.
 * @param buffer Caller-provided output buffer.
 * @param size   Size of @p buffer in bytes.
 */
static void
get_device_name(struct devfs_vnode* vnode, char* buffer, size_t size)
{
	RecursiveLocker _(sDeviceFileSystem->lock);

	struct devfs_vnode* leaf = vnode;
	size_t offset = 0;

	// count levels

	for (; vnode->parent && vnode->parent != vnode; vnode = vnode->parent) {
		offset += strlen(vnode->name) + 1;
	}

	// construct full path name

	for (vnode = leaf; vnode->parent && vnode->parent != vnode;
			vnode = vnode->parent) {
		size_t length = strlen(vnode->name);
		size_t start = offset - length - 1;

		if (size >= offset) {
			strcpy(buffer + start, vnode->name);
			if (vnode != leaf)
				buffer[offset - 1] = '/';
		}

		offset = start;
	}
}


/**
 * @brief Kernel debugger command — dump a devfs_vnode at the given address.
 *
 * @param argc  Argument count; must be 2.
 * @param argv  argv[1] is the hex address of the vnode to dump.
 * @return 0 always (debugger command convention).
 */
static int
dump_node(int argc, char** argv)
{
	if (argc != 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	struct devfs_vnode* vnode = (struct devfs_vnode*)parse_expression(argv[1]);
	if (vnode == NULL) {
		kprintf("invalid node address\n");
		return 0;
	}

	kprintf("DEVFS NODE: %p\n", vnode);
	kprintf(" id:          %" B_PRIdINO "\n", vnode->id);
	kprintf(" name:        \"%s\"\n", vnode->name);
	kprintf(" type:        %x\n", vnode->stream.type);
	kprintf(" parent:      %p\n", vnode->parent);
	kprintf(" dir next:    %p\n", vnode->dir_next);

	if (S_ISDIR(vnode->stream.type)) {
		kprintf(" dir scanned: %" B_PRId32 "\n", vnode->stream.u.dir.scanned);
		kprintf(" contents:\n");

		devfs_vnode* children = vnode->stream.u.dir.dir_head;
		while (children != NULL) {
			kprintf("   %p, id %" B_PRIdINO "\n", children, children->id);
			children = children->dir_next;
		}
	} else if (S_ISLNK(vnode->stream.type)) {
		kprintf(" symlink to:  %s\n", vnode->stream.u.symlink.path);
	} else {
		kprintf(" device:      %p\n", vnode->stream.u.dev.device);
		kprintf(" partition:   %p\n", vnode->stream.u.dev.partition);
		if (vnode->stream.u.dev.partition != NULL) {
			partition_info& info = vnode->stream.u.dev.partition->info;
			kprintf("  raw device node: %p\n",
				vnode->stream.u.dev.partition->raw_device);
			kprintf("  offset:          %" B_PRIdOFF "\n", info.offset);
			kprintf("  size:            %" B_PRIdOFF "\n", info.size);
			kprintf("  block size:      %" B_PRId32 "\n", info.logical_block_size);
			kprintf("  session:         %" B_PRId32 "\n", info.session);
			kprintf("  partition:       %" B_PRId32 "\n", info.partition);
			kprintf("  device:          %s\n", info.device);
			set_debug_variable("_raw",
				(addr_t)vnode->stream.u.dev.partition->raw_device);
		}
	}

	return 0;
}


/**
 * @brief Kernel debugger command — dump a devfs_cookie at the given address.
 *
 * @param argc  Argument count; must be 2.
 * @param argv  argv[1] is the hex address of the cookie to dump.
 * @return 0 always (debugger command convention).
 */
static int
dump_cookie(int argc, char** argv)
{
	if (argc != 2) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	uint64 address;
	if (!evaluate_debug_expression(argv[1], &address, false))
		return 0;

	struct devfs_cookie* cookie = (devfs_cookie*)(addr_t)address;

	kprintf("DEVFS COOKIE: %p\n", cookie);
	kprintf(" device_cookie: %p\n", cookie->device_cookie);

	return 0;
}


//	#pragma mark - file system interface


/**
 * @brief Mount the device filesystem and initialize the devfs root vnode.
 *
 * Only one devfs instance is permitted per boot. Creates the root directory
 * vnode, initializes the vnode hash table, and publishes the root via the VFS.
 *
 * @param volume       VFS volume descriptor to populate.
 * @param devfs        Unused mount arguments (always NULL for devfs).
 * @param flags        Mount flags (unused).
 * @param args         Mount arguments string (unused).
 * @param _rootNodeID  Out: inode id of the newly created root vnode.
 * @retval B_OK        Filesystem mounted successfully.
 * @retval B_ERROR     A devfs instance is already mounted.
 * @retval B_NO_MEMORY Allocation failure.
 */
static status_t
devfs_mount(fs_volume* volume, const char* devfs, uint32 flags,
	const char* args, ino_t* _rootNodeID)
{
	struct devfs_vnode* vnode;
	struct devfs* fs;
	status_t err;

	TRACE(("devfs_mount: entry\n"));

	if (sDeviceFileSystem) {
		TRACE(("double mount of devfs attempted\n"));
		err = B_ERROR;
		goto err;
	}

	fs = (struct devfs*)malloc(sizeof(struct devfs));
	if (fs == NULL) {
		err = B_NO_MEMORY;
		goto err;
	}

	volume->private_volume = fs;
	volume->ops = &kVolumeOps;
	fs->volume = volume;
	fs->id = volume->id;
	fs->next_vnode_id = 0;

	recursive_lock_init(&fs->lock, "devfs lock");

	fs->vnode_hash = new(std::nothrow) NodeTable();
	if (fs->vnode_hash == NULL || fs->vnode_hash->Init(DEVFS_HASH_SIZE) != B_OK) {
		err = B_NO_MEMORY;
		goto err2;
	}

	// create a vnode
	vnode = devfs_create_vnode(fs, NULL, "");
	if (vnode == NULL) {
		err = B_NO_MEMORY;
		goto err3;
	}

	// set it up
	vnode->parent = vnode;

	// create a dir stream for it to hold
	init_directory_vnode(vnode, 0755);
	fs->root_vnode = vnode;

	fs->vnode_hash->Insert(vnode);
	publish_vnode(volume, vnode->id, vnode, &kVnodeOps, vnode->stream.type, 0);

	*_rootNodeID = vnode->id;
	sDeviceFileSystem = fs;
	return B_OK;

err3:
	delete fs->vnode_hash;
err2:
	recursive_lock_destroy(&fs->lock);
	free(fs);
err:
	return err;
}


/**
 * @brief Unmount the devfs volume, releasing all vnodes and filesystem state.
 *
 * Releases the root vnode reference, iterates the vnode hash and force-deletes
 * every vnode, then destroys the lock and frees the fs structure.
 *
 * @param _volume  VFS volume descriptor for the devfs instance to unmount.
 * @retval B_OK Always succeeds.
 */
static status_t
devfs_unmount(fs_volume* _volume)
{
	struct devfs* fs = (struct devfs*)_volume->private_volume;
	struct devfs_vnode* vnode;

	TRACE(("devfs_unmount: entry fs = %p\n", fs));

	recursive_lock_lock(&fs->lock);

	// release the reference to the root
	put_vnode(fs->volume, fs->root_vnode->id);

	// delete all of the vnodes
	NodeTable::Iterator i(fs->vnode_hash);
	while (i.HasNext()) {
		vnode = i.Next();
		devfs_delete_vnode(fs, vnode, true);
	}
	delete fs->vnode_hash;

	recursive_lock_destroy(&fs->lock);
	free(fs);

	return B_OK;
}


/**
 * @brief Sync the devfs volume — no-op because devfs is entirely in memory.
 *
 * @param _volume  VFS volume descriptor (unused).
 * @retval B_OK Always.
 */
static status_t
devfs_sync(fs_volume* _volume)
{
	TRACE(("devfs_sync: entry\n"));

	return B_OK;
}


/**
 * @brief Look up a directory entry by name and return its inode id.
 *
 * Triggers a lazy driver scan on @p _dir before searching, so that entries
 * added by device_manager or legacy drivers become visible.
 *
 * @param _volume  VFS volume.
 * @param _dir     Directory vnode in which to search.
 * @param name     Entry name to look up (single path component).
 * @param _id      Out: inode id of the found vnode.
 * @retval B_OK               Found; *_id set and reference acquired.
 * @retval B_NOT_A_DIRECTORY  @p _dir is not a directory.
 * @retval B_ENTRY_NOT_FOUND  No entry with @p name exists.
 */
static status_t
devfs_lookup(fs_volume* _volume, fs_vnode* _dir, const char* name, ino_t* _id)
{
	struct devfs* fs = (struct devfs*)_volume->private_volume;
	struct devfs_vnode* dir = (struct devfs_vnode*)_dir->private_node;
	struct devfs_vnode* vnode;
	status_t status;

	TRACE(("devfs_lookup: entry dir %p, name '%s'\n", dir, name));

	if (!S_ISDIR(dir->stream.type))
		return B_NOT_A_DIRECTORY;

	// Make sure the directory contents are up to date
	scan_for_drivers_if_needed(dir);

	RecursiveLocker locker(&fs->lock);

	// look it up
	vnode = devfs_find_in_dir(dir, name);
	if (vnode == NULL) {
		// We don't have to rescan here, because thanks to node monitoring
		// we already know it does not exist
		return B_ENTRY_NOT_FOUND;
	}

	status = get_vnode(fs->volume, vnode->id, NULL);
	if (status < B_OK)
		return status;

	*_id = vnode->id;

	return B_OK;
}


/**
 * @brief Copy the name of @p _vnode into @p buffer.
 *
 * @param _volume     VFS volume (unused).
 * @param _vnode      Vnode whose name is requested.
 * @param buffer      Caller-supplied output buffer.
 * @param bufferSize  Size of @p buffer in bytes.
 * @retval B_OK Always.
 */
static status_t
devfs_get_vnode_name(fs_volume* _volume, fs_vnode* _vnode, char* buffer,
	size_t bufferSize)
{
	struct devfs_vnode* vnode = (struct devfs_vnode*)_vnode->private_node;

	TRACE(("devfs_get_vnode_name: vnode = %p\n", vnode));

	strlcpy(buffer, vnode->name, bufferSize);
	return B_OK;
}


/**
 * @brief Resolve an inode id to a vnode pointer (VFS get_vnode callback).
 *
 * @param _volume  VFS volume.
 * @param id       Inode id to look up.
 * @param _vnode   Out: populated fs_vnode on success.
 * @param _type    Out: stream type of the found vnode.
 * @param _flags   Out: always set to 0.
 * @param reenter  True if the call is made from within the VFS layer.
 * @retval B_OK              Vnode found and @p _vnode populated.
 * @retval B_ENTRY_NOT_FOUND No vnode with @p id exists.
 */
static status_t
devfs_get_vnode(fs_volume* _volume, ino_t id, fs_vnode* _vnode, int* _type,
	uint32* _flags, bool reenter)
{
	struct devfs* fs = (struct devfs*)_volume->private_volume;

	TRACE(("devfs_get_vnode: asking for vnode id = %" B_PRIdINO
		", vnode = %p, r %d\n", id, _vnode, reenter));

	RecursiveLocker _(fs->lock);

	struct devfs_vnode* vnode = fs->vnode_hash->Lookup(id);
	if (vnode == NULL)
		return B_ENTRY_NOT_FOUND;

	TRACE(("devfs_get_vnode: looked it up at %p\n", vnode));

	_vnode->private_node = vnode;
	_vnode->ops = &kVnodeOps;
	*_type = vnode->stream.type;
	*_flags = 0;
	return B_OK;
}


/**
 * @brief Release a VFS reference to a vnode — no-op for devfs.
 *
 * @param _volume  VFS volume (unused).
 * @param _vnode   Vnode being released (unused except in trace builds).
 * @param reenter  Reentrant flag (unused).
 * @retval B_OK Always.
 */
static status_t
devfs_put_vnode(fs_volume* _volume, fs_vnode* _vnode, bool reenter)
{
#ifdef TRACE_DEVFS
	struct devfs_vnode* vnode = (struct devfs_vnode*)_vnode->private_node;

	TRACE(("devfs_put_vnode: entry on vnode %p, id = %" B_PRIdINO
		", reenter %d\n", vnode, vnode->id, reenter));
#endif

	return B_OK;
}


/**
 * @brief Remove a vnode from the devfs after the VFS has dropped all
 *        references to it.
 *
 * Panics if the vnode is still linked into a directory (should never happen
 * because unpublish_node removes it first). Calls devfs_delete_vnode() with
 * forceDelete = false.
 *
 * @param _volume  VFS volume.
 * @param _v       Vnode to remove.
 * @param reenter  Reentrant flag (unused).
 * @retval B_OK Always.
 */
static status_t
devfs_remove_vnode(fs_volume* _volume, fs_vnode* _v, bool reenter)
{
	struct devfs* fs = (struct devfs*)_volume->private_volume;
	struct devfs_vnode* vnode = (struct devfs_vnode*)_v->private_node;

	TRACE(("devfs_removevnode: remove %p (%" B_PRIdINO "), reenter %d\n",
		vnode, vnode->id, reenter));

	RecursiveLocker locker(&fs->lock);

	if (vnode->dir_next) {
		// can't remove node if it's linked to the dir
		panic("devfs_removevnode: vnode %p asked to be removed is present in dir\n", vnode);
	}

	devfs_delete_vnode(fs, vnode, false);

	return B_OK;
}


/**
 * @brief Open a devfs entry and allocate a per-open cookie.
 *
 * For character devices the underlying device's Open() hook is invoked.
 * Directories may only be opened read-only.
 *
 * @param _volume   VFS volume.
 * @param _vnode    Vnode to open.
 * @param openMode  Open flags (O_RDONLY, O_WRONLY, …).
 * @param _cookie   Out: newly allocated devfs_cookie on success.
 * @retval B_OK            Opened successfully.
 * @retval B_IS_A_DIRECTORY Directory opened with write access requested.
 * @retval B_NO_MEMORY     Cookie allocation failed.
 * @retval other           Error from the underlying device's Open() hook.
 */
static status_t
devfs_open(fs_volume* _volume, fs_vnode* _vnode, int openMode,
	void** _cookie)
{
	struct devfs_vnode* vnode = (struct devfs_vnode*)_vnode->private_node;
	struct devfs_cookie* cookie;
	status_t status = B_OK;

	if (S_ISDIR(vnode->stream.type) && (openMode & O_RWMASK) != O_RDONLY)
		return B_IS_A_DIRECTORY;

	cookie = (struct devfs_cookie*)malloc(sizeof(struct devfs_cookie));
	if (cookie == NULL)
		return B_NO_MEMORY;

	TRACE(("devfs_open: vnode %p, openMode 0x%x, cookie %p\n", vnode, openMode,
		cookie));

	cookie->device_cookie = NULL;

	if (S_ISCHR(vnode->stream.type)) {
		BaseDevice* device = vnode->stream.u.dev.device;
		status = device->InitDevice();
		if (status != B_OK) {
			free(cookie);
			return status;
		}

		char path[B_FILE_NAME_LENGTH];
		get_device_name(vnode, path, sizeof(path));

		status = device->Open(path, openMode, &cookie->device_cookie);
		if (status != B_OK)
			device->UninitDevice();
	}

	if (status != B_OK)
		free(cookie);
	else
		*_cookie = cookie;

	return status;
}


/**
 * @brief Close an open devfs file descriptor.
 *
 * For character devices, forwards the close to the underlying device's
 * Close() hook. Does nothing for directories.
 *
 * @param _volume  VFS volume.
 * @param _vnode   Vnode being closed.
 * @param _cookie  Per-open cookie created by devfs_open().
 * @retval B_OK   Always for directories; device's Close() result for devices.
 */
static status_t
devfs_close(fs_volume* _volume, fs_vnode* _vnode, void* _cookie)
{
	struct devfs_vnode* vnode = (struct devfs_vnode*)_vnode->private_node;
	struct devfs_cookie* cookie = (struct devfs_cookie*)_cookie;

	TRACE(("devfs_close: entry vnode %p, cookie %p\n", vnode, cookie));

	if (S_ISCHR(vnode->stream.type)) {
		// pass the call through to the underlying device
		return vnode->stream.u.dev.device->Close(cookie->device_cookie);
	}

	return B_OK;
}


/**
 * @brief Release the per-open cookie and uninitialize the device if needed.
 *
 * Calls the underlying device's Free() and UninitDevice() for character
 * devices, then frees the cookie memory.
 *
 * @param _volume  VFS volume.
 * @param _vnode   Vnode whose cookie is being freed.
 * @param _cookie  Per-open cookie to release.
 * @retval B_OK Always.
 */
static status_t
devfs_free_cookie(fs_volume* _volume, fs_vnode* _vnode, void* _cookie)
{
	struct devfs_vnode* vnode = (struct devfs_vnode*)_vnode->private_node;
	struct devfs_cookie* cookie = (struct devfs_cookie*)_cookie;

	TRACE(("devfs_freecookie: entry vnode %p, cookie %p\n", vnode, cookie));

	if (S_ISCHR(vnode->stream.type)) {
		// pass the call through to the underlying device
		vnode->stream.u.dev.device->Free(cookie->device_cookie);
		vnode->stream.u.dev.device->UninitDevice();
	}

	free(cookie);
	return B_OK;
}


/**
 * @brief fsync a devfs vnode — no-op because devfs is in-memory.
 *
 * @param _volume   VFS volume (unused).
 * @param _v        Vnode to sync (unused).
 * @param dataOnly  Unused.
 * @retval B_OK Always.
 */
static status_t
devfs_fsync(fs_volume* _volume, fs_vnode* _v, bool dataOnly)
{
	return B_OK;
}


/**
 * @brief Read the target of a symlink vnode.
 *
 * @param _volume      VFS volume (unused).
 * @param _link        Symlink vnode to read.
 * @param buffer       Caller-supplied output buffer.
 * @param _bufferSize  In: buffer size; Out: actual symlink length.
 * @retval B_OK        Target copied into @p buffer.
 * @retval B_BAD_VALUE @p _link is not a symlink.
 */
static status_t
devfs_read_link(fs_volume* _volume, fs_vnode* _link, char* buffer,
	size_t* _bufferSize)
{
	struct devfs_vnode* link = (struct devfs_vnode*)_link->private_node;

	if (!S_ISLNK(link->stream.type))
		return B_BAD_VALUE;

	memcpy(buffer, link->stream.u.symlink.path, min_c(*_bufferSize,
		link->stream.u.symlink.length));

	*_bufferSize = link->stream.u.symlink.length;

	return B_OK;
}


/**
 * @brief Read data from a character device vnode.
 *
 * Translates partition-relative offsets to device-absolute before forwarding
 * to the underlying device's Read() hook.
 *
 * @param _volume  VFS volume.
 * @param _vnode   Character device vnode.
 * @param _cookie  Per-open cookie.
 * @param pos      Byte offset to read from (partition-relative if applicable).
 * @param buffer   Caller-supplied output buffer.
 * @param _length  In: requested byte count; Out: bytes actually read.
 * @retval B_OK        Data read successfully.
 * @retval B_BAD_VALUE Not a character device, negative @p pos, or @p pos
 *                     beyond the partition end.
 */
static status_t
devfs_read(fs_volume* _volume, fs_vnode* _vnode, void* _cookie, off_t pos,
	void* buffer, size_t* _length)
{
	struct devfs_vnode* vnode = (struct devfs_vnode*)_vnode->private_node;
	struct devfs_cookie* cookie = (struct devfs_cookie*)_cookie;

	//TRACE(("devfs_read: vnode %p, cookie %p, pos %lld, len %p\n",
	//	vnode, cookie, pos, _length));

	if (!S_ISCHR(vnode->stream.type))
		return B_BAD_VALUE;

	if (pos < 0)
		return B_BAD_VALUE;

	if (vnode->stream.u.dev.partition != NULL) {
		if (pos >= vnode->stream.u.dev.partition->info.size)
			return B_BAD_VALUE;

		translate_partition_access(vnode->stream.u.dev.partition, pos,
			*_length);
	}

	if (*_length == 0)
		return B_OK;

	// pass the call through to the device
	return vnode->stream.u.dev.device->Read(cookie->device_cookie, pos, buffer,
		_length);
}


/**
 * @brief Write data to a character device vnode.
 *
 * Translates partition-relative offsets to device-absolute before forwarding
 * to the underlying device's Write() hook.
 *
 * @param _volume  VFS volume.
 * @param _vnode   Character device vnode.
 * @param _cookie  Per-open cookie.
 * @param pos      Byte offset to write at (partition-relative if applicable).
 * @param buffer   Data to write.
 * @param _length  In: byte count to write; Out: bytes actually written.
 * @retval B_OK        Data written successfully.
 * @retval B_BAD_VALUE Not a character device, negative @p pos, or @p pos
 *                     beyond the partition end.
 */
static status_t
devfs_write(fs_volume* _volume, fs_vnode* _vnode, void* _cookie, off_t pos,
	const void* buffer, size_t* _length)
{
	struct devfs_vnode* vnode = (struct devfs_vnode*)_vnode->private_node;
	struct devfs_cookie* cookie = (struct devfs_cookie*)_cookie;

	//TRACE(("devfs_write: vnode %p, cookie %p, pos %lld, len %p\n",
	//	vnode, cookie, pos, _length));

	if (!S_ISCHR(vnode->stream.type))
		return B_BAD_VALUE;

	if (pos < 0)
		return B_BAD_VALUE;

	if (vnode->stream.u.dev.partition != NULL) {
		if (pos >= vnode->stream.u.dev.partition->info.size)
			return B_BAD_VALUE;

		translate_partition_access(vnode->stream.u.dev.partition, pos,
			*_length);
	}

	if (*_length == 0)
		return B_OK;

	return vnode->stream.u.dev.device->Write(cookie->device_cookie, pos, buffer,
		_length);
}


/**
 * @brief Create a new subdirectory inside a devfs directory.
 *
 * @param _volume  VFS volume.
 * @param _dir     Parent directory vnode.
 * @param name     Name of the new subdirectory.
 * @param perms    Permission bits for the new directory.
 * @retval B_OK        Directory created.
 * @retval EEXIST      An entry with @p name already exists.
 * @retval B_NO_MEMORY Vnode allocation failed.
 */
static status_t
devfs_create_dir(fs_volume* _volume, fs_vnode* _dir, const char* name,
	int perms)
{
	struct devfs* fs = (struct devfs*)_volume->private_volume;
	struct devfs_vnode* dir = (struct devfs_vnode*)_dir->private_node;

	struct devfs_vnode* vnode = devfs_find_in_dir(dir, name);
	if (vnode != NULL) {
		return EEXIST;
	}

	vnode = devfs_create_vnode(fs, dir, name);
	if (vnode == NULL) {
		return B_NO_MEMORY;
	}

	// set up the new directory
	init_directory_vnode(vnode, perms);
	publish_node(sDeviceFileSystem, dir, vnode);

	return B_OK;
}


/**
 * @brief Open a directory vnode for iteration and allocate a dir cookie.
 *
 * Triggers a lazy driver scan on the directory before creating the cookie.
 *
 * @param _volume  VFS volume.
 * @param _vnode   Directory vnode to open.
 * @param _cookie  Out: newly allocated devfs_dir_cookie.
 * @retval B_OK        Directory opened.
 * @retval B_BAD_VALUE @p _vnode is not a directory.
 * @retval B_NO_MEMORY Cookie allocation failed.
 */
static status_t
devfs_open_dir(fs_volume* _volume, fs_vnode* _vnode, void** _cookie)
{
	struct devfs* fs = (struct devfs*)_volume->private_volume;
	struct devfs_vnode* vnode = (struct devfs_vnode*)_vnode->private_node;
	struct devfs_dir_cookie* cookie;

	TRACE(("devfs_open_dir: vnode %p\n", vnode));

	if (!S_ISDIR(vnode->stream.type))
		return B_BAD_VALUE;

	cookie = (devfs_dir_cookie*)malloc(sizeof(devfs_dir_cookie));
	if (cookie == NULL)
		return B_NO_MEMORY;

	// make sure the directory has up-to-date contents
	scan_for_drivers_if_needed(vnode);

	RecursiveLocker locker(&fs->lock);

	cookie->current = vnode->stream.u.dir.dir_head;
	cookie->state = ITERATION_STATE_BEGIN;

	list_add_item(&vnode->stream.u.dir.cookies, cookie);
	*_cookie = cookie;

	return B_OK;
}


/**
 * @brief Close a directory cookie and remove it from the directory's cookie list.
 *
 * @param _volume  VFS volume.
 * @param _vnode   Directory vnode.
 * @param _cookie  Cookie to free.
 * @retval B_OK Always.
 */
static status_t
devfs_free_dir_cookie(fs_volume* _volume, fs_vnode* _vnode, void* _cookie)
{
	struct devfs_vnode* vnode = (struct devfs_vnode*)_vnode->private_node;
	struct devfs_dir_cookie* cookie = (devfs_dir_cookie*)_cookie;
	struct devfs* fs = (struct devfs*)_volume->private_volume;

	TRACE(("devfs_free_dir_cookie: entry vnode %p, cookie %p\n", vnode, cookie));

	RecursiveLocker locker(&fs->lock);

	list_remove_item(&vnode->stream.u.dir.cookies, cookie);
	free(cookie);
	return B_OK;
}


/**
 * @brief Read the next entry from an open directory.
 *
 * Handles "." and ".." synthetic entries, then iterates real children.
 * Advances the cookie's position for the next call.
 *
 * @param _volume     VFS volume.
 * @param _vnode      Directory vnode.
 * @param _cookie     Directory iteration cookie.
 * @param dirent      Caller-supplied buffer for the result.
 * @param bufferSize  Size of @p dirent buffer.
 * @param _num        Out: number of entries returned (0 or 1).
 * @retval B_OK        Entry written to @p dirent (or 0 entries at end).
 * @retval B_BAD_VALUE @p _vnode is not a directory.
 * @retval ENOBUFS    Buffer too small for the next entry.
 */
static status_t
devfs_read_dir(fs_volume* _volume, fs_vnode* _vnode, void* _cookie,
	struct dirent* dirent, size_t bufferSize, uint32* _num)
{
	struct devfs_vnode* vnode = (devfs_vnode*)_vnode->private_node;
	struct devfs_dir_cookie* cookie = (devfs_dir_cookie*)_cookie;
	struct devfs* fs = (struct devfs*)_volume->private_volume;
	status_t status = B_OK;
	struct devfs_vnode* childNode = NULL;
	const char* name = NULL;
	struct devfs_vnode* nextChildNode = NULL;
	int32 nextState = cookie->state;

	TRACE(("devfs_read_dir: vnode %p, cookie %p, buffer %p, size %ld\n",
		_vnode, cookie, dirent, bufferSize));

	if (!S_ISDIR(vnode->stream.type))
		return B_BAD_VALUE;

	RecursiveLocker locker(&fs->lock);

	switch (cookie->state) {
		case ITERATION_STATE_DOT:
			childNode = vnode;
			name = ".";
			nextChildNode = vnode->stream.u.dir.dir_head;
			nextState = cookie->state + 1;
			break;
		case ITERATION_STATE_DOT_DOT:
			childNode = vnode->parent;
			name = "..";
			nextChildNode = vnode->stream.u.dir.dir_head;
			nextState = cookie->state + 1;
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
		*_num = 0;
		return B_OK;
	}

	dirent->d_dev = fs->id;
	dirent->d_ino = childNode->id;
	dirent->d_reclen = offsetof(struct dirent, d_name) + strlen(name) + 1;

	if (dirent->d_reclen > bufferSize)
		return ENOBUFS;

	status = user_strlcpy(dirent->d_name, name,
		bufferSize - offsetof(struct dirent, d_name));
	if (status < B_OK)
		return status;

	cookie->current = nextChildNode;
	cookie->state = nextState;
	*_num = 1;

	return B_OK;
}


/**
 * @brief Rewind a directory cookie back to the first entry.
 *
 * @param _volume  VFS volume.
 * @param _vnode   Directory vnode.
 * @param _cookie  Cookie to reset.
 * @retval B_OK        Reset successfully.
 * @retval B_BAD_VALUE @p _vnode is not a directory.
 */
static status_t
devfs_rewind_dir(fs_volume* _volume, fs_vnode* _vnode, void* _cookie)
{
	struct devfs_vnode* vnode = (struct devfs_vnode*)_vnode->private_node;
	struct devfs_dir_cookie* cookie = (devfs_dir_cookie*)_cookie;
	struct devfs* fs = (struct devfs*)_volume->private_volume;

	TRACE(("devfs_rewind_dir: vnode %p, cookie %p\n", vnode, cookie));

	if (!S_ISDIR(vnode->stream.type))
		return B_BAD_VALUE;

	RecursiveLocker locker(&fs->lock);

	cookie->current = vnode->stream.u.dir.dir_head;
	cookie->state = ITERATION_STATE_BEGIN;

	return B_OK;
}


/*!	Forwards the opcode to the device driver, but also handles some devfs
	specific functionality, like partitions.
*/
/**
 * @brief Handle an ioctl request on a devfs character device.
 *
 * Intercepts devfs-specific opcodes (B_GET_GEOMETRY, B_TRIM_DEVICE,
 * B_GET_PARTITION_INFO, B_GET_PATH_FOR_DEVICE) before forwarding unknown
 * opcodes to the underlying device's Control() hook.
 *
 * @param _volume  VFS volume.
 * @param _vnode   Device vnode.
 * @param _cookie  Per-open cookie.
 * @param op       Ioctl opcode.
 * @param buffer   User-space argument buffer.
 * @param length   Size of @p buffer in bytes.
 * @retval B_OK          Operation completed successfully.
 * @retval B_BAD_VALUE   Not a character device, or invalid arguments.
 * @retval B_NOT_ALLOWED B_SET_PARTITION attempted.
 * @retval B_UNSUPPORTED Unsupported legacy R5 ioctl.
 * @retval other         Error from the underlying device Control() hook.
 */
static status_t
devfs_ioctl(fs_volume* _volume, fs_vnode* _vnode, void* _cookie, uint32 op,
	void* buffer, size_t length)
{
	struct devfs_vnode* vnode = (struct devfs_vnode*)_vnode->private_node;
	struct devfs_cookie* cookie = (struct devfs_cookie*)_cookie;

	TRACE(("devfs_ioctl: vnode %p, cookie %p, op %" B_PRIu32
		", buf %p, len %" B_PRIuSIZE "\n",
		vnode, cookie, op, buffer, length));

	// we are actually checking for a *device* here, we don't make the
	// distinction between char and block devices
	if (S_ISCHR(vnode->stream.type)) {
		switch (op) {
			case B_GET_GEOMETRY:
			{
				struct devfs_partition* partition
					= vnode->stream.u.dev.partition;
				if (partition == NULL)
					break;

				device_geometry geometry;
				status_t status = vnode->stream.u.dev.device->Control(
					cookie->device_cookie, op, &geometry, length);
				if (status != B_OK)
					return status;

				// patch values to match partition size
				if (geometry.bytes_per_sector == 0)
					geometry.bytes_per_sector = 512;

				devfs_compute_geometry_size(&geometry,
					partition->info.size / geometry.bytes_per_sector,
					geometry.bytes_per_sector);

				return user_memcpy(buffer, &geometry, sizeof(device_geometry));
			}

			case B_TRIM_DEVICE:
			{
				struct devfs_partition* partition
					= vnode->stream.u.dev.partition;

				fs_trim_data* trimData;
				MemoryDeleter deleter;
				status_t status = get_trim_data_from_user(buffer, length,
					deleter, trimData);
				if (status != B_OK)
					return status;

#ifdef DEBUG_TRIM
				dprintf("TRIM: devfs: received TRIM ranges (bytes):\n");
				for (uint32 i = 0; i < trimData->range_count; i++) {
					dprintf("[%3" B_PRIu32 "] %" B_PRIu64 " : %"
						B_PRIu64 "\n", i,
						trimData->ranges[i].offset,
						trimData->ranges[i].size);
				}
#endif

				if (partition != NULL) {
					// If there is a partition, offset all ranges according
					// to the partition start.
					// Range size may be reduced to fit the partition size.
					for (uint32 i = 0; i < trimData->range_count; i++) {
						if (!translate_partition_access(partition,
							trimData->ranges[i].offset,
							trimData->ranges[i].size)) {
							return B_BAD_VALUE;
						}
					}

#ifdef DEBUG_TRIM
					dprintf("TRIM: devfs: TRIM ranges after partition"
						" translation (bytes):\n");
					for (uint32 i = 0; i < trimData->range_count; i++) {
						dprintf("[%3" B_PRIu32 "] %" B_PRIu64 " : %"
							B_PRIu64 "\n", i,
							trimData->ranges[i].offset,
							trimData->ranges[i].size);
					}
#endif
				}

				status = vnode->stream.u.dev.device->Control(
					cookie->device_cookie, op, trimData, length);

				// Copy the data back to userland (it contains the number of
				// trimmed bytes)
				if (status == B_OK)
					status = copy_trim_data_to_user(buffer, trimData);

				return status;
			}

			case B_GET_PARTITION_INFO:
			{
				struct devfs_partition* partition
					= vnode->stream.u.dev.partition;
				if (!S_ISCHR(vnode->stream.type)
					|| partition == NULL
					|| length != sizeof(partition_info))
					return B_BAD_VALUE;

				return user_memcpy(buffer, &partition->info,
					sizeof(partition_info));
			}

			case B_SET_PARTITION:
				return B_NOT_ALLOWED;

			case B_GET_PATH_FOR_DEVICE:
			{
				char path[256];
				// TODO: we might want to actually find the mountpoint
				// of that instance of devfs...
				// but for now we assume it's mounted on /dev
				strcpy(path, "/dev/");
				get_device_name(vnode, path + 5, sizeof(path) - 5);
				if (length && (length <= strlen(path)))
					return ERANGE;
				return user_strlcpy((char*)buffer, path, sizeof(path));
			}

			// old unsupported R5 private stuff

			case B_GET_NEXT_OPEN_DEVICE:
				dprintf("devfs: unsupported legacy ioctl B_GET_NEXT_OPEN_DEVICE\n");
				return B_UNSUPPORTED;
			case B_ADD_FIXED_DRIVER:
				dprintf("devfs: unsupported legacy ioctl B_ADD_FIXED_DRIVER\n");
				return B_UNSUPPORTED;
			case B_REMOVE_FIXED_DRIVER:
				dprintf("devfs: unsupported legacy ioctl B_REMOVE_FIXED_DRIVER\n");
				return B_UNSUPPORTED;

		}

		return vnode->stream.u.dev.device->Control(cookie->device_cookie,
			op, buffer, length);
	}

	return B_BAD_VALUE;
}


/**
 * @brief Set O_NONBLOCK / blocking-IO flag on an open device.
 *
 * Forwards B_SET_NONBLOCKING_IO or B_SET_BLOCKING_IO to the device's
 * Control() hook based on whether O_NONBLOCK is set in @p flags.
 *
 * @param _volume  VFS volume.
 * @param _vnode   Character device vnode.
 * @param _cookie  Per-open cookie.
 * @param flags    New file-status flags; only O_NONBLOCK is examined.
 * @retval B_OK          Flag set successfully.
 * @retval B_NOT_ALLOWED @p _vnode is not a character device.
 */
static status_t
devfs_set_flags(fs_volume* _volume, fs_vnode* _vnode, void* _cookie,
	int flags)
{
	struct devfs_vnode* vnode = (struct devfs_vnode*)_vnode->private_node;
	struct devfs_cookie* cookie = (struct devfs_cookie*)_cookie;

	// we need to pass the O_NONBLOCK flag to the underlying device

	if (!S_ISCHR(vnode->stream.type))
		return B_NOT_ALLOWED;

	return vnode->stream.u.dev.device->Control(cookie->device_cookie,
		flags & O_NONBLOCK ? B_SET_NONBLOCKING_IO : B_SET_BLOCKING_IO, NULL, 0);
}


/**
 * @brief Register interest in an I/O event for a character device.
 *
 * If the device does not implement Select(), notifies the select subsystem
 * immediately for non-output-only events and returns B_UNSUPPORTED.
 *
 * @param _volume  VFS volume.
 * @param _vnode   Character device vnode.
 * @param _cookie  Per-open cookie.
 * @param event    Select event type (B_SELECT_READ, B_SELECT_WRITE, …).
 * @param sync     Select synchronization object.
 * @retval B_OK          Interest registered with the device.
 * @retval B_NOT_ALLOWED @p _vnode is not a character device.
 * @retval B_UNSUPPORTED Device has no Select() hook.
 */
static status_t
devfs_select(fs_volume* _volume, fs_vnode* _vnode, void* _cookie,
	uint8 event, selectsync* sync)
{
	struct devfs_vnode* vnode = (struct devfs_vnode*)_vnode->private_node;
	struct devfs_cookie* cookie = (struct devfs_cookie*)_cookie;

	if (!S_ISCHR(vnode->stream.type))
		return B_NOT_ALLOWED;

	// If the device has no select() hook, notify select() now.
	if (!vnode->stream.u.dev.device->HasSelect()) {
		if (!SELECT_TYPE_IS_OUTPUT_ONLY(event))
			notify_select_event((selectsync*)sync, event);
		return B_UNSUPPORTED;
	}

	return vnode->stream.u.dev.device->Select(cookie->device_cookie, event,
		(selectsync*)sync);
}


/**
 * @brief Cancel a previously registered select event interest.
 *
 * @param _volume  VFS volume.
 * @param _vnode   Character device vnode.
 * @param _cookie  Per-open cookie.
 * @param event    Select event type to deregister.
 * @param sync     Select synchronization object.
 * @retval B_OK          Deselected successfully or device has no Deselect hook.
 * @retval B_NOT_ALLOWED @p _vnode is not a character device.
 */
static status_t
devfs_deselect(fs_volume* _volume, fs_vnode* _vnode, void* _cookie,
	uint8 event, selectsync* sync)
{
	struct devfs_vnode* vnode = (struct devfs_vnode*)_vnode->private_node;
	struct devfs_cookie* cookie = (struct devfs_cookie*)_cookie;

	if (!S_ISCHR(vnode->stream.type))
		return B_NOT_ALLOWED;

	if (!vnode->stream.u.dev.device->HasDeselect())
		return B_OK;

	return vnode->stream.u.dev.device->Deselect(cookie->device_cookie, event,
		(selectsync*)sync);
}


/**
 * @brief Report whether a character device vnode supports page I/O.
 *
 * Currently always returns false because the paging hook is obsolete.
 *
 * @param _volume  VFS volume (unused).
 * @param _vnode   Vnode to query (unused).
 * @param cookie   Per-open cookie (unused).
 * @return false always.
 */
static bool
devfs_can_page(fs_volume* _volume, fs_vnode* _vnode, void* cookie)
{
#if 0
	struct devfs_vnode* vnode = (devfs_vnode*)_vnode->private_node;

	//TRACE(("devfs_canpage: vnode %p\n", vnode));

	if (!S_ISCHR(vnode->stream.type)
		|| vnode->stream.u.dev.device->Node() == NULL
		|| cookie == NULL)
		return false;

	return vnode->stream.u.dev.device->HasRead()
		|| vnode->stream.u.dev.device->HasIO();
#endif
	// TODO: Obsolete hook!
	return false;
}


/**
 * @brief Read a scatter-gather list of pages from a character device.
 *
 * Emulates read_pages() by calling the device's Read() hook for each iovec.
 * Translates partition-relative offsets before issuing reads.
 *
 * @param _volume    VFS volume.
 * @param _vnode     Character device vnode.
 * @param _cookie    Per-open cookie.
 * @param pos        Starting offset (partition-relative if applicable).
 * @param vecs       Array of iovec descriptors for the target buffers.
 * @param count      Number of entries in @p vecs.
 * @param _numBytes  In: total bytes requested; Out: total bytes transferred.
 * @retval B_OK          At least one byte was read successfully.
 * @retval B_NOT_ALLOWED Not a character device, device lacks read/io support,
 *                       or @p cookie is NULL.
 * @retval B_BAD_VALUE   Negative @p pos or @p pos beyond partition end.
 */
static status_t
devfs_read_pages(fs_volume* _volume, fs_vnode* _vnode, void* _cookie,
	off_t pos, const iovec* vecs, size_t count, size_t* _numBytes)
{
	struct devfs_vnode* vnode = (devfs_vnode*)_vnode->private_node;
	struct devfs_cookie* cookie = (struct devfs_cookie*)_cookie;

	//TRACE(("devfs_read_pages: vnode %p, vecs %p, count = %lu, pos = %lld, size = %lu\n", vnode, vecs, count, pos, *_numBytes));

	if (!S_ISCHR(vnode->stream.type)
		|| (!vnode->stream.u.dev.device->HasRead()
			&& !vnode->stream.u.dev.device->HasIO())
		|| cookie == NULL)
		return B_NOT_ALLOWED;

	if (pos < 0)
		return B_BAD_VALUE;

	if (vnode->stream.u.dev.partition != NULL) {
		if (pos >= vnode->stream.u.dev.partition->info.size)
			return B_BAD_VALUE;

		translate_partition_access(vnode->stream.u.dev.partition, pos,
			*_numBytes);
	}

	if (vnode->stream.u.dev.device->HasIO()) {
		// TODO: use io_requests for this!
	}

	// emulate read_pages() using read()

	status_t error = B_OK;
	size_t bytesTransferred = 0;

	size_t remainingBytes = *_numBytes;
	for (size_t i = 0; i < count && remainingBytes > 0; i++) {
		size_t toRead = min_c(vecs[i].iov_len, remainingBytes);
		size_t length = toRead;

		error = vnode->stream.u.dev.device->Read(cookie->device_cookie, pos,
			vecs[i].iov_base, &length);
		if (error != B_OK)
			break;

		pos += length;
		bytesTransferred += length;
		remainingBytes -= length;

		if (length < toRead)
			break;
	}

	*_numBytes = bytesTransferred;

	return bytesTransferred > 0 ? B_OK : error;
}


/**
 * @brief Write a scatter-gather list of pages to a character device.
 *
 * Emulates write_pages() by calling the device's Write() hook for each iovec.
 * Translates partition-relative offsets before issuing writes.
 *
 * @param _volume    VFS volume.
 * @param _vnode     Character device vnode.
 * @param _cookie    Per-open cookie.
 * @param pos        Starting offset (partition-relative if applicable).
 * @param vecs       Array of iovec descriptors for the source buffers.
 * @param count      Number of entries in @p vecs.
 * @param _numBytes  In: total bytes requested; Out: total bytes transferred.
 * @retval B_OK          At least one byte was written successfully.
 * @retval B_NOT_ALLOWED Not a character device, device lacks write/io support,
 *                       or @p cookie is NULL.
 * @retval B_BAD_VALUE   Negative @p pos or @p pos beyond partition end.
 */
static status_t
devfs_write_pages(fs_volume* _volume, fs_vnode* _vnode, void* _cookie,
	off_t pos, const iovec* vecs, size_t count, size_t* _numBytes)
{
	struct devfs_vnode* vnode = (devfs_vnode*)_vnode->private_node;
	struct devfs_cookie* cookie = (struct devfs_cookie*)_cookie;

	//TRACE(("devfs_write_pages: vnode %p, vecs %p, count = %lu, pos = %lld, size = %lu\n", vnode, vecs, count, pos, *_numBytes));

	if (!S_ISCHR(vnode->stream.type)
		|| (!vnode->stream.u.dev.device->HasWrite()
			&& !vnode->stream.u.dev.device->HasIO())
		|| cookie == NULL)
		return B_NOT_ALLOWED;

	if (pos < 0)
		return B_BAD_VALUE;

	if (vnode->stream.u.dev.partition != NULL) {
		if (pos >= vnode->stream.u.dev.partition->info.size)
			return B_BAD_VALUE;

		translate_partition_access(vnode->stream.u.dev.partition, pos,
			*_numBytes);
	}

	if (vnode->stream.u.dev.device->HasIO()) {
		// TODO: use io_requests for this!
	}

	// emulate write_pages() using write()

	status_t error = B_OK;
	size_t bytesTransferred = 0;

	size_t remainingBytes = *_numBytes;
	for (size_t i = 0; i < count && remainingBytes > 0; i++) {
		size_t toWrite = min_c(vecs[i].iov_len, remainingBytes);
		size_t length = toWrite;

		error = vnode->stream.u.dev.device->Write(cookie->device_cookie, pos,
			vecs[i].iov_base, &length);
		if (error != B_OK)
			break;

		pos += length;
		bytesTransferred += length;
		remainingBytes -= length;

		if (length < toWrite)
			break;
	}

	*_numBytes = bytesTransferred;

	return bytesTransferred > 0 ? B_OK : error;
}


/**
 * @brief Submit an asynchronous io_request to the underlying character device.
 *
 * Validates that the request fits within any partition boundary, adjusts the
 * offset for partitions, and forwards to the device's IO() hook.
 *
 * @param volume   VFS volume.
 * @param _vnode   Character device vnode.
 * @param _cookie  Per-open cookie.
 * @param request  IO request descriptor (offset and length are adjusted for
 *                 partitions in place before forwarding).
 * @retval B_OK          IO request accepted by the device.
 * @retval B_NOT_ALLOWED Not a character device or @p cookie is NULL.
 * @retval B_UNSUPPORTED Device does not implement the IO() hook.
 * @retval B_BAD_VALUE   Request extends beyond the partition boundary.
 */
static status_t
devfs_io(fs_volume* volume, fs_vnode* _vnode, void* _cookie,
	io_request* request)
{
	TRACE(("[%d] devfs_io(request: %p)\n", find_thread(NULL), request));

	devfs_vnode* vnode = (devfs_vnode*)_vnode->private_node;
	devfs_cookie* cookie = (devfs_cookie*)_cookie;

	if (!S_ISCHR(vnode->stream.type) || cookie == NULL) {
		request->SetStatusAndNotify(B_NOT_ALLOWED);
		return B_NOT_ALLOWED;
	}

	if (!vnode->stream.u.dev.device->HasIO())
		return B_UNSUPPORTED;

	if (vnode->stream.u.dev.partition != NULL) {
		if (request->Offset() + (off_t)request->Length()
				> vnode->stream.u.dev.partition->info.size) {
			request->SetStatusAndNotify(B_BAD_VALUE);
			return B_BAD_VALUE;
		}
		translate_partition_access(vnode->stream.u.dev.partition, request);
	}

	return vnode->stream.u.dev.device->IO(cookie->device_cookie, request);
}


/**
 * @brief Populate a stat structure for a devfs vnode.
 *
 * Reports size information for partition vnodes (making them appear as block
 * devices) and symlink length for symlink vnodes.
 *
 * @param _volume  VFS volume.
 * @param _vnode   Vnode to stat.
 * @param stat     Caller-supplied stat buffer to populate.
 * @retval B_OK Always.
 */
static status_t
devfs_read_stat(fs_volume* _volume, fs_vnode* _vnode, struct stat* stat)
{
	struct devfs_vnode* vnode = (struct devfs_vnode*)_vnode->private_node;

	TRACE(("devfs_read_stat: vnode %p (%" B_PRIdINO "), stat %p\n",
		vnode, vnode->id, stat));

	stat->st_ino = vnode->id;
	stat->st_rdev = vnode->id;
	stat->st_size = 0;
	stat->st_mode = vnode->stream.type;

	stat->st_nlink = 1;
	stat->st_blksize = 65536;
	stat->st_blocks = 0;

	stat->st_uid = vnode->uid;
	stat->st_gid = vnode->gid;

	stat->st_atim = current_timespec();
	stat->st_mtim = stat->st_ctim = vnode->modification_time;
	stat->st_crtim = vnode->creation_time;

	// TODO: this only works for partitions right now - if we should decide
	//	to keep this feature, we should have a better solution
	if (S_ISCHR(vnode->stream.type)) {
		//device_geometry geometry;

		// if it's a real block device, then let's report a useful size
		if (vnode->stream.u.dev.partition != NULL) {
			stat->st_size = vnode->stream.u.dev.partition->info.size;
#if 0
		} else if (vnode->stream.u.dev.info->control(cookie->device_cookie,
					B_GET_GEOMETRY, &geometry, sizeof(struct device_geometry)) >= B_OK) {
			stat->st_size = 1LL * geometry.head_count * geometry.cylinder_count
				* geometry.sectors_per_track * geometry.bytes_per_sector;
#endif
		}

		// is this a real block device? then let's have it reported like that
		if (stat->st_size != 0)
			stat->st_mode = S_IFBLK | (vnode->stream.type & S_IUMSK);
	} else if (S_ISLNK(vnode->stream.type)) {
		stat->st_size = vnode->stream.u.symlink.length;
	}

	return B_OK;
}


/**
 * @brief Update writable stat fields (mode, uid, gid, timestamps) for a vnode.
 *
 * Changing the size field is explicitly rejected. Sends a stat-changed
 * node-monitor notification on success.
 *
 * @param _volume   VFS volume.
 * @param _vnode    Vnode to update.
 * @param stat      Source stat values.
 * @param statMask  Bitmask of B_STAT_* flags indicating which fields to update.
 * @retval B_OK        Fields updated.
 * @retval B_BAD_VALUE B_STAT_SIZE was set in @p statMask.
 */
static status_t
devfs_write_stat(fs_volume* _volume, fs_vnode* _vnode, const struct stat* stat,
	uint32 statMask)
{
	struct devfs* fs = (struct devfs*)_volume->private_volume;
	struct devfs_vnode* vnode = (struct devfs_vnode*)_vnode->private_node;

	TRACE(("devfs_write_stat: vnode %p (0x%" B_PRIdINO "), stat %p\n",
		vnode, vnode->id, stat));

	// we cannot change the size of anything
	if (statMask & B_STAT_SIZE)
		return B_BAD_VALUE;

	RecursiveLocker locker(&fs->lock);

	if (statMask & B_STAT_MODE) {
		vnode->stream.type = (vnode->stream.type & ~S_IUMSK)
			| (stat->st_mode & S_IUMSK);
	}

	if (statMask & B_STAT_UID)
		vnode->uid = stat->st_uid;
	if (statMask & B_STAT_GID)
		vnode->gid = stat->st_gid;

	if (statMask & B_STAT_MODIFICATION_TIME)
		vnode->modification_time = stat->st_mtim;
	if (statMask & B_STAT_CREATION_TIME)
		vnode->creation_time = stat->st_crtim;

	notify_stat_changed(fs->id, get_parent_id(vnode), vnode->id, statMask);
	return B_OK;
}


/**
 * @brief Module init/uninit handler for the devfs filesystem module.
 *
 * On B_MODULE_INIT registers the devfs_node and devfs_cookie debugger
 * commands and initializes the legacy driver layer. On B_MODULE_UNINIT
 * removes those commands.
 *
 * @param op  B_MODULE_INIT or B_MODULE_UNINIT.
 * @retval B_OK    Operation completed.
 * @retval B_ERROR Unknown operation code.
 */
static status_t
devfs_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			add_debugger_command_etc("devfs_node", &dump_node,
				"Print info on a private devfs node",
				"<address>\n"
				"Prints information on a devfs node given by <address>.\n",
				0);
			add_debugger_command_etc("devfs_cookie", &dump_cookie,
				"Print info on a private devfs cookie",
				"<address>\n"
				"Prints information on a devfs cookie given by <address>.\n",
				0);

			legacy_driver_init();
			return B_OK;

		case B_MODULE_UNINIT:
			remove_debugger_command("devfs_node", &dump_node);
			remove_debugger_command("devfs_cookie", &dump_cookie);
			return B_OK;

		default:
			return B_ERROR;
	}
}

namespace {

fs_volume_ops kVolumeOps = {
	&devfs_unmount,
	NULL,
	NULL,
	&devfs_sync,
	&devfs_get_vnode,

	// the other operations are not supported (attributes, indices, queries)
	NULL,
};

fs_vnode_ops kVnodeOps = {
	&devfs_lookup,
	&devfs_get_vnode_name,

	&devfs_put_vnode,
	&devfs_remove_vnode,

	&devfs_can_page,
	&devfs_read_pages,
	&devfs_write_pages,

	&devfs_io,
	NULL,	// cancel_io()

	NULL,	// get_file_map

	/* common */
	&devfs_ioctl,
	&devfs_set_flags,
	&devfs_select,
	&devfs_deselect,
	&devfs_fsync,

	&devfs_read_link,
	NULL,	// symlink
	NULL,	// link
	NULL,	// unlink
	NULL,	// rename

	NULL,	// access
	&devfs_read_stat,
	&devfs_write_stat,
	NULL,

	/* file */
	NULL,	// create
	&devfs_open,
	&devfs_close,
	&devfs_free_cookie,
	&devfs_read,
	&devfs_write,

	/* directory */
	&devfs_create_dir,
	NULL,	// remove_dir
	&devfs_open_dir,
	&devfs_close,
		// same as for files - it does nothing for directories, anyway
	&devfs_free_dir_cookie,
	&devfs_read_dir,
	&devfs_rewind_dir,

	// attributes operations are not supported
	NULL,
};

}	// namespace

file_system_module_info gDeviceFileSystem = {
	{
		"file_systems/devfs" B_CURRENT_FS_API_VERSION,
		0,
		devfs_std_ops,
	},

	"devfs",					// short_name
	"Device File System",		// pretty_name
	0,							// DDM flags

	NULL,	// identify_partition()
	NULL,	// scan_partition()
	NULL,	// free_identify_partition_cookie()
	NULL,	// free_partition_content_cookie()

	&devfs_mount,
};


//	#pragma mark - kernel private API


/**
 * @brief Unpublish a file-backed virtual device from devfs by path.
 *
 * Looks up the node, verifies it is a FileDevice, removes it from its
 * parent directory, and schedules VFS vnode removal.
 *
 * @param path  devfs-relative path of the file device to remove.
 * @retval B_OK        Device unpublished.
 * @retval B_BAD_VALUE Path does not refer to a character device or the
 *                     device is not a FileDevice instance.
 */
extern "C" status_t
devfs_unpublish_file_device(const char* path)
{
	// get the device node
	devfs_vnode* node;
	status_t status = get_node_for_path(sDeviceFileSystem, path, &node);
	if (status != B_OK)
		return status;

	if (!S_ISCHR(node->stream.type)) {
		put_vnode(sDeviceFileSystem->volume, node->id);
		return B_BAD_VALUE;
	}

	// if it is indeed a file device, unpublish it
	FileDevice* device = dynamic_cast<FileDevice*>(node->stream.u.dev.device);
	if (device == NULL) {
		put_vnode(sDeviceFileSystem->volume, node->id);
		return B_BAD_VALUE;
	}

	status = unpublish_node(sDeviceFileSystem, node, S_IFCHR);

	put_vnode(sDeviceFileSystem->volume, node->id);
	return status;
}


/**
 * @brief Publish a file as a virtual block device at @p path in devfs.
 *
 * Creates a FileDevice backed by @p filePath and publishes it at @p path
 * within the devfs hierarchy.
 *
 * @param path      devfs-relative destination path (e.g. "disk/virtual/0/raw").
 * @param filePath  Absolute path to the backing file.
 * @retval B_OK        Device published.
 * @retval B_NO_MEMORY FileDevice allocation failed.
 * @retval other       Error from FileDevice::Init() or publish_device().
 */
extern "C" status_t
devfs_publish_file_device(const char* path, const char* filePath)
{
	// create a FileDevice for the file
	FileDevice* device = new(std::nothrow) FileDevice;
	if (device == NULL)
		return B_NO_MEMORY;
	ObjectDeleter<FileDevice> deviceDeleter(device);

	status_t error = device->Init(filePath);
	if (error != B_OK)
		return error;

	// publish the device
	error = publish_device(sDeviceFileSystem, path, device);
	if (error != B_OK)
		return error;

	deviceDeleter.Detach();
	return B_OK;
}


/**
 * @brief Remove a partition vnode from devfs by path.
 *
 * @param path  devfs-relative path of the partition node to unpublish.
 * @retval B_OK   Partition unpublished.
 * @retval other  Error from get_node_for_path() or unpublish_node().
 */
extern "C" status_t
devfs_unpublish_partition(const char* path)
{
	devfs_vnode* node;
	status_t status = get_node_for_path(sDeviceFileSystem, path, &node);
	if (status != B_OK)
		return status;

	status = unpublish_node(sDeviceFileSystem, node, S_IFCHR);
	put_vnode(sDeviceFileSystem->volume, node->id);
	return status;
}


/**
 * @brief Publish a partition as a character device derived from an existing
 *        raw device.
 *
 * Locates the raw device named in @p info->device, then calls add_partition()
 * to create a child vnode that maps the given slice.
 *
 * @param name  Name for the new partition entry (leaf name only).
 * @param info  Partition descriptor; info->device must be the devfs-relative
 *              path of the parent raw device.
 * @retval B_OK        Partition published.
 * @retval B_BAD_VALUE @p name or @p info is NULL.
 * @retval other       Error from get_node_for_path() or add_partition().
 */
extern "C" status_t
devfs_publish_partition(const char* name, const partition_info* info)
{
	if (name == NULL || info == NULL)
		return B_BAD_VALUE;
	TRACE(("publish partition: %s (device \"%s\", offset %" B_PRIdOFF
		", size %" B_PRIdOFF ")\n",
		name, info->device, info->offset, info->size));

	devfs_vnode* device;
	status_t status = get_node_for_path(sDeviceFileSystem, info->device,
		&device);
	if (status != B_OK)
		return status;

	status = add_partition(sDeviceFileSystem, device, name, *info);

	put_vnode(sDeviceFileSystem->volume, device->id);
	return status;
}


/**
 * @brief Rename a partition vnode within its parent directory.
 *
 * Removes the vnode from the directory under @p oldName and re-inserts it
 * under @p newName, sending a rename notification to the node monitor.
 *
 * @param devicePath  devfs-relative path of the raw device whose parent
 *                    directory contains the partition entries.
 * @param oldName     Current leaf name of the partition.
 * @param newName     New leaf name; must not already exist in the directory.
 * @retval B_OK             Renamed.
 * @retval B_BAD_VALUE      @p oldName or @p newName is NULL, or @p newName
 *                          already exists.
 * @retval B_ENTRY_NOT_FOUND No entry named @p oldName found.
 * @retval B_NO_MEMORY      strdup() failed.
 */
extern "C" status_t
devfs_rename_partition(const char* devicePath, const char* oldName,
	const char* newName)
{
	if (oldName == NULL || newName == NULL)
		return B_BAD_VALUE;

	devfs_vnode* device;
	status_t status = get_node_for_path(sDeviceFileSystem, devicePath, &device);
	if (status != B_OK)
		return status;

	RecursiveLocker locker(sDeviceFileSystem->lock);
	devfs_vnode* node = devfs_find_in_dir(device->parent, oldName);
	if (node == NULL)
		return B_ENTRY_NOT_FOUND;

	// check if the new path already exists
	if (devfs_find_in_dir(device->parent, newName))
		return B_BAD_VALUE;

	char* name = strdup(newName);
	if (name == NULL)
		return B_NO_MEMORY;

	devfs_remove_from_dir(device->parent, node, false);

	free(node->name);
	node->name = name;

	devfs_insert_in_dir(device->parent, node, false);

	notify_entry_moved(sDeviceFileSystem->id, device->parent->id, oldName,
		device->parent->id, newName, node->id);
	notify_stat_changed(sDeviceFileSystem->id, get_parent_id(device->parent),
		device->parent->id, B_STAT_MODIFICATION_TIME);

	return B_OK;
}


/**
 * @brief Create a directory hierarchy in devfs for the given path.
 *
 * Acquires the filesystem lock and delegates to publish_directory().
 *
 * @param path  devfs-relative directory path to create (e.g. "bus/usb").
 * @retval B_OK        All path components exist or were created.
 * @retval B_NO_MEMORY Allocation failure.
 * @retval B_FILE_EXISTS A non-directory component exists along the path.
 */
extern "C" status_t
devfs_publish_directory(const char* path)
{
	RecursiveLocker locker(&sDeviceFileSystem->lock);

	return publish_directory(sDeviceFileSystem, path);
}


/**
 * @brief Unpublish a device node from devfs by path and optionally disconnect
 *        all open file descriptors.
 *
 * @param path        devfs-relative path of the device to remove.
 * @param disconnect  When true, vfs_disconnect_vnode() is called to force-close
 *                    any open file descriptors referencing this vnode.
 * @retval B_OK   Device unpublished.
 * @retval other  Error from get_node_for_path() or unpublish_node().
 */
extern "C" status_t
devfs_unpublish_device(const char* path, bool disconnect)
{
	devfs_vnode* node;
	status_t status = get_node_for_path(sDeviceFileSystem, path, &node);
	if (status != B_OK)
		return status;

	status = unpublish_node(sDeviceFileSystem, node, S_IFCHR);

	if (status == B_OK && disconnect)
		vfs_disconnect_vnode(sDeviceFileSystem->id, node->id);

	put_vnode(sDeviceFileSystem->volume, node->id);
	return status;
}


//	#pragma mark - device_manager private API


/**
 * @brief Publish a BaseDevice at the given devfs-relative path.
 *
 * Thin wrapper over the internal publish_device() function used by the
 * device manager layer.
 *
 * @param path    devfs-relative destination path.
 * @param device  Initialized BaseDevice to publish.
 * @retval B_OK   Device published.
 * @retval other  Error from publish_device().
 */
status_t
devfs_publish_device(const char* path, BaseDevice* device)
{
	return publish_device(sDeviceFileSystem, path, device);
}


/**
 * @brief Unpublish a BaseDevice identified by its device object pointer.
 *
 * Looks up the vnode by the device's stored inode id, unpublishes it, and
 * optionally disconnects open file descriptors.
 *
 * @param device     The device to unpublish; must have a valid ID set.
 * @param disconnect When true, vfs_disconnect_vnode() is called on success.
 * @retval B_OK   Unpublished.
 * @retval other  Error from get_vnode() or unpublish_node().
 */
status_t
devfs_unpublish_device(BaseDevice* device, bool disconnect)
{
	devfs_vnode* node;
	status_t status = get_vnode(sDeviceFileSystem->volume, device->ID(),
		(void**)&node);
	if (status != B_OK)
		return status;

	status = unpublish_node(sDeviceFileSystem, node, S_IFCHR);

	if (status == B_OK && disconnect)
		vfs_disconnect_vnode(sDeviceFileSystem->id, node->id);

	put_vnode(sDeviceFileSystem->volume, node->id);
	return status;
}


/*!	Gets the device for a given devfs relative path.
	If successful the call must be balanced with a call to devfs_put_device().
*/
/**
 * @brief Retrieve the BaseDevice pointer for a devfs-relative path.
 *
 * The returned device has a reference held on its vnode; the caller must
 * release it by calling devfs_put_device().
 *
 * @param path     devfs-relative path of the device node.
 * @param _device  Out: BaseDevice pointer on success.
 * @retval B_OK        Device found and reference held.
 * @retval B_BAD_VALUE Node is not a bare character device (e.g. it is a
 *                     partition or not a character device).
 * @retval other       Error from get_node_for_path().
 */
status_t
devfs_get_device(const char* path, BaseDevice*& _device)
{
	devfs_vnode* node;
	status_t status = get_node_for_path(sDeviceFileSystem, path, &node);
	if (status != B_OK)
		return status;

	if (!S_ISCHR(node->stream.type) || node->stream.u.dev.partition != NULL) {
		put_vnode(sDeviceFileSystem->volume, node->id);
		return B_BAD_VALUE;
	}

	_device = node->stream.u.dev.device;
	return B_OK;
}


/**
 * @brief Release the VFS reference acquired by devfs_get_device().
 *
 * @param device  Device whose vnode reference is to be released.
 */
void
devfs_put_device(BaseDevice* device)
{
	put_vnode(sDeviceFileSystem->volume, device->ID());
}


/**
 * @brief Compute a device_geometry that encodes a given block count and size.
 *
 * Adjusts head_count upward (doubling) until sectors_per_track fits in a
 * uint32, keeping the total logical block count accurate.
 *
 * @param geometry    Out: geometry structure to populate.
 * @param blockCount  Total number of logical blocks on the device.
 * @param blockSize   Size of each logical block in bytes.
 */
void
devfs_compute_geometry_size(device_geometry* geometry, uint64 blockCount,
	uint32 blockSize)
{
	geometry->head_count = 1;
	while (blockCount > UINT32_MAX) {
		geometry->head_count <<= 1;
		blockCount >>= 1;
	}

	geometry->cylinder_count = 1;
	geometry->sectors_per_track = blockCount;
	geometry->bytes_per_sector = blockSize;
}


//	#pragma mark - support API for legacy drivers


/**
 * @brief Ask the legacy driver layer to rescan drivers for @p driverName.
 *
 * @param driverName  Name of the legacy driver module to rescan.
 * @retval B_OK   Rescan triggered.
 * @retval other  Error from legacy_driver_rescan().
 */
extern "C" status_t
devfs_rescan_driver(const char* driverName)
{
	TRACE(("devfs_rescan_driver: %s\n", driverName));

	return legacy_driver_rescan(driverName);
}


/**
 * @brief Publish a legacy (R5-style) device using a device_hooks table.
 *
 * Delegates to legacy_driver_publish() which wraps the hooks in a
 * LegacyDevice and calls publish_device() internally.
 *
 * @param path   devfs-relative path at which to publish the device.
 * @param hooks  Legacy device hook table provided by the driver.
 * @retval B_OK   Device published.
 * @retval other  Error from legacy_driver_publish().
 */
extern "C" status_t
devfs_publish_device(const char* path, device_hooks* hooks)
{
	return legacy_driver_publish(path, hooks);
}
