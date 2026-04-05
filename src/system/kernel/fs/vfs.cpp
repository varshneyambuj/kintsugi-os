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
 *   Copyright 2005-2013, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2002-2018, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file vfs.cpp
 * @brief Virtual File System layer — the central hub for all file I/O.
 *
 * vfs.cpp implements the kernel VFS: vnode lifecycle (get_vnode, put_vnode),
 * namespace operations (lookup, open, read, write, mkdir, rename, unlink),
 * mount/unmount, file descriptor management, and all VFS-related syscalls.
 * All file system add-ons are called exclusively through the vnode operation
 * vectors stored in fs_vnode_ops and fs_volume_ops.
 *
 * @see fd.cpp, Vnode.cpp, vfs_boot.cpp
 */

/*! Virtual File System and File System Interface Layer */


#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fs_attr.h>
#include <fs_info.h>
#include <fs_interface.h>
#include <fs_volume.h>
#include <NodeMonitor.h>
#include <OS.h>
#include <StorageDefs.h>

#include <AutoDeleter.h>
#include <AutoDeleterDrivers.h>
#include <block_cache.h>
#include <boot/kernel_args.h>
#include <debug_heap.h>
#include <disk_device_manager/KDiskDevice.h>
#include <disk_device_manager/KDiskDeviceManager.h>
#include <disk_device_manager/KDiskDeviceUtils.h>
#include <disk_device_manager/KDiskSystem.h>
#include <fd.h>
#include <file_cache.h>
#include <fs/node_monitor.h>
#include <KPath.h>
#include <lock.h>
#include <low_resource_manager.h>
#include <slab/Slab.h>
#include <StackOrHeapArray.h>
#include <syscalls.h>
#include <syscall_restart.h>
#include <tracing.h>
#include <usergroup.h>
#include <util/atomic.h>
#include <util/AutoLock.h>
#include <util/ThreadAutoLock.h>
#include <util/DoublyLinkedList.h>
#include <vfs.h>
#include <vm/vm.h>
#include <vm/VMCache.h>
#include <wait_for_objects.h>

#include "EntryCache.h"
#include "fifo.h"
#include "IORequest.h"
#include "unused_vnodes.h"
#include "vfs_tracing.h"
#include "Vnode.h"
#include "../cache/vnode_store.h"


//#define TRACE_VFS
#ifdef TRACE_VFS
#	define TRACE(x) dprintf x
#	define FUNCTION(x) dprintf x
#else
#	define TRACE(x) ;
#	define FUNCTION(x) ;
#endif

#define ADD_DEBUGGER_COMMANDS


#define HAS_FS_CALL(vnode, op)			(vnode->ops->op != NULL)
#define HAS_FS_MOUNT_CALL(mount, op)	(mount->volume->ops->op != NULL)

#if KDEBUG
#	define FS_CALL(vnode, op, params...) \
		( HAS_FS_CALL(vnode, op) ? \
			vnode->ops->op(vnode->mount->volume, vnode, params) \
			: (panic("FS_CALL: vnode %p op " #op " is NULL", vnode), 0))
#	define FS_CALL_NO_PARAMS(vnode, op) \
		( HAS_FS_CALL(vnode, op) ? \
			vnode->ops->op(vnode->mount->volume, vnode) \
			: (panic("FS_CALL_NO_PARAMS: vnode %p op " #op " is NULL", vnode), 0))
#	define FS_MOUNT_CALL(mount, op, params...) \
		( HAS_FS_MOUNT_CALL(mount, op) ? \
			mount->volume->ops->op(mount->volume, params) \
			: (panic("FS_MOUNT_CALL: mount %p op " #op " is NULL", mount), 0))
#	define FS_MOUNT_CALL_NO_PARAMS(mount, op) \
		( HAS_FS_MOUNT_CALL(mount, op) ? \
			mount->volume->ops->op(mount->volume) \
			: (panic("FS_MOUNT_CALL_NO_PARAMS: mount %p op " #op " is NULL", mount), 0))
#else
#	define FS_CALL(vnode, op, params...) \
			vnode->ops->op(vnode->mount->volume, vnode, params)
#	define FS_CALL_NO_PARAMS(vnode, op) \
			vnode->ops->op(vnode->mount->volume, vnode)
#	define FS_MOUNT_CALL(mount, op, params...) \
			mount->volume->ops->op(mount->volume, params)
#	define FS_MOUNT_CALL_NO_PARAMS(mount, op) \
			mount->volume->ops->op(mount->volume)
#endif


const static size_t kMaxPathLength = 65536;
	// The absolute maximum path length (for getcwd() - this is not depending
	// on PATH_MAX


typedef DoublyLinkedList<vnode> VnodeList;

/*!	\brief Structure to manage a mounted file system

	Note: The root_vnode and root_vnode->covers fields (what others?) are
	initialized in fs_mount() and not changed afterwards. That is as soon
	as the mount is mounted and it is made sure it won't be unmounted
	(e.g. by holding a reference to a vnode of that mount) (read) access
	to those fields is always safe, even without additional locking. Morever
	while mounted the mount holds a reference to the root_vnode->covers vnode,
	and thus making the access path vnode->mount->root_vnode->covers->mount->...
	safe if a reference to vnode is held (note that for the root mount
	root_vnode->covers is NULL, though).
*/
struct fs_mount {
	fs_mount()
		:
		volume(NULL),
		device_name(NULL)
	{
		mutex_init(&lock, "mount lock");
	}

	~fs_mount()
	{
		ASSERT(vnodes.IsEmpty());

		mutex_destroy(&lock);
		free(device_name);

		while (volume) {
			fs_volume* superVolume = volume->super_volume;

			if (volume->file_system != NULL)
				put_module(volume->file_system->info.name);

			free(volume->file_system_name);
			free(volume);
			volume = superVolume;
		}
	}

	struct fs_mount* next;
	dev_t			id;
	fs_volume*		volume;
	char*			device_name;
	mutex			lock;	// guards the vnodes list
	struct vnode*	root_vnode;
	struct vnode*	covers_vnode;	// immutable
	KPartition*		partition;
	VnodeList		vnodes;
	EntryCache		entry_cache;
	bool			unmounting;
	bool			owns_file_device;
};


namespace {

struct advisory_lock : public DoublyLinkedListLinkImpl<advisory_lock> {
	void*			bound_to;
	team_id			team;
	pid_t			session;
	off_t			start;
	off_t			end;
	bool			shared;
};

typedef DoublyLinkedList<advisory_lock> LockList;

} // namespace


struct advisory_locking {
	sem_id			lock;
	sem_id			wait_sem;
	LockList		locks;

	advisory_locking()
		:
		lock(-1),
		wait_sem(-1)
	{
	}

	~advisory_locking()
	{
		if (lock >= 0)
			delete_sem(lock);
		if (wait_sem >= 0)
			delete_sem(wait_sem);
	}
};

/*!	\brief Guards sMountsTable.

	The holder is allowed to read/write access the sMountsTable.
	Manipulation of the fs_mount structures themselves
	(and their destruction) requires different locks though.
*/
static rw_lock sMountLock = RW_LOCK_INITIALIZER("vfs_mount_lock");

/*!	\brief Guards mount/unmount operations.

	The fs_mount() and fs_unmount() hold the lock during their whole operation.
	That is locking the lock ensures that no FS is mounted/unmounted. In
	particular this means that
	- sMountsTable will not be modified,
	- the fields immutable after initialization of the fs_mount structures in
	  sMountsTable will not be modified,

	The thread trying to lock the lock must not hold sVnodeLock or
	sMountLock.
*/
static recursive_lock sMountOpLock;

/*!	\brief Guards sVnodeTable.

	The holder is allowed read/write access to sVnodeTable and to
	any unbusy vnode in that table, save to the immutable fields (device, id,
	private_node, mount) to which only read-only access is allowed.
	The mutable fields advisory_locking, mandatory_locked_by, and ref_count, as
	well as the busy, removed, unused flags, and the vnode's type can also be
	write accessed when holding a read lock to sVnodeLock *and* having the vnode
	locked. Write access to covered_by and covers requires to write lock
	sVnodeLock.

	The thread trying to acquire the lock must not hold sMountLock.
	You must not hold this lock when calling create_sem(), as this might call
	vfs_free_unused_vnodes() and thus cause a deadlock.
*/
static rw_lock sVnodeLock = RW_LOCK_INITIALIZER("vfs_vnode_lock");

/*!	\brief Guards io_context::root.

	Must be held when setting or getting the io_context::root field.
	The only operation allowed while holding this lock besides getting or
	setting the field is inc_vnode_ref_count() on io_context::root.
*/
static rw_lock sIOContextRootLock = RW_LOCK_INITIALIZER("io_context::root lock");


namespace {

struct vnode_hash_key {
	dev_t	device;
	ino_t	vnode;
};

struct VnodeHash {
	typedef vnode_hash_key	KeyType;
	typedef	struct vnode	ValueType;

#define VHASH(mountid, vnodeid) \
	(((uint32)((vnodeid) >> 32) + (uint32)(vnodeid)) ^ (uint32)(mountid))

	size_t HashKey(KeyType key) const
	{
		return VHASH(key.device, key.vnode);
	}

	size_t Hash(ValueType* vnode) const
	{
		return VHASH(vnode->device, vnode->id);
	}

#undef VHASH

	bool Compare(KeyType key, ValueType* vnode) const
	{
		return vnode->device == key.device && vnode->id == key.vnode;
	}

	ValueType*& GetLink(ValueType* value) const
	{
		return value->hash_next;
	}
};

typedef BOpenHashTable<VnodeHash> VnodeTable;


struct MountHash {
	typedef dev_t			KeyType;
	typedef	struct fs_mount	ValueType;

	size_t HashKey(KeyType key) const
	{
		return key;
	}

	size_t Hash(ValueType* mount) const
	{
		return mount->id;
	}

	bool Compare(KeyType key, ValueType* mount) const
	{
		return mount->id == key;
	}

	ValueType*& GetLink(ValueType* value) const
	{
		return value->next;
	}
};

typedef BOpenHashTable<MountHash> MountTable;

} // namespace


object_cache* sPathNameCache;
object_cache* sVnodeCache;
object_cache* sFileDescriptorCache;

#define VNODE_HASH_TABLE_SIZE 1024
static VnodeTable* sVnodeTable;
static struct vnode* sRoot;

#define MOUNTS_HASH_TABLE_SIZE 16
static MountTable* sMountsTable;
static dev_t sNextMountID = 1;

#define MAX_TEMP_IO_VECS 8

// How long to wait for busy vnodes (10s)
#define BUSY_VNODE_RETRIES 2000
#define BUSY_VNODE_DELAY 5000

mode_t __gUmask = 022;

/* function declarations */

static void free_unused_vnodes();

// file descriptor operation prototypes
static status_t file_read(struct file_descriptor* descriptor, off_t pos,
	void* buffer, size_t* _bytes);
static status_t file_write(struct file_descriptor* descriptor, off_t pos,
	const void* buffer, size_t* _bytes);
static ssize_t file_readv(struct file_descriptor* descriptor, off_t pos,
	const struct iovec *vecs, int count);
static ssize_t file_writev(struct file_descriptor* descriptor, off_t pos,
	const struct iovec *vecs, int count);
static off_t file_seek(struct file_descriptor* descriptor, off_t pos,
	int seekType);
static void file_free_fd(struct file_descriptor* descriptor);
static status_t file_close(struct file_descriptor* descriptor);
static status_t file_select(struct file_descriptor* descriptor, uint8 event,
	struct selectsync* sync);
static status_t file_deselect(struct file_descriptor* descriptor, uint8 event,
	struct selectsync* sync);
static status_t dir_read(struct io_context* context,
	struct file_descriptor* descriptor, struct dirent* buffer,
	size_t bufferSize, uint32* _count);
static status_t dir_read(struct io_context* ioContext, struct vnode* vnode,
	void* cookie, struct dirent* buffer, size_t bufferSize, uint32* _count);
static status_t dir_rewind(struct file_descriptor* descriptor);
static void dir_free_fd(struct file_descriptor* descriptor);
static status_t dir_close(struct file_descriptor* descriptor);
static status_t attr_dir_read(struct io_context* context,
	struct file_descriptor* descriptor, struct dirent* buffer,
	size_t bufferSize, uint32* _count);
static status_t attr_dir_rewind(struct file_descriptor* descriptor);
static void attr_dir_free_fd(struct file_descriptor* descriptor);
static status_t attr_dir_close(struct file_descriptor* descriptor);
static status_t attr_read(struct file_descriptor* descriptor, off_t pos,
	void* buffer, size_t* _bytes);
static status_t attr_write(struct file_descriptor* descriptor, off_t pos,
	const void* buffer, size_t* _bytes);
static off_t attr_seek(struct file_descriptor* descriptor, off_t pos,
	int seekType);
static void attr_free_fd(struct file_descriptor* descriptor);
static status_t attr_close(struct file_descriptor* descriptor);
static status_t attr_read_stat(struct file_descriptor* descriptor,
	struct stat* statData);
static status_t attr_write_stat(struct file_descriptor* descriptor,
	const struct stat* stat, int statMask);
static status_t index_dir_read(struct io_context* context,
	struct file_descriptor* descriptor, struct dirent* buffer,
	size_t bufferSize, uint32* _count);
static status_t index_dir_rewind(struct file_descriptor* descriptor);
static void index_dir_free_fd(struct file_descriptor* descriptor);
static status_t index_dir_close(struct file_descriptor* descriptor);
static status_t query_read(struct io_context* context,
	struct file_descriptor* descriptor, struct dirent* buffer,
	size_t bufferSize, uint32* _count);
static status_t query_rewind(struct file_descriptor* descriptor);
static void query_free_fd(struct file_descriptor* descriptor);
static status_t query_close(struct file_descriptor* descriptor);

static status_t common_ioctl(struct file_descriptor* descriptor, ulong op,
	void* buffer, size_t length);
static status_t common_read_stat(struct file_descriptor* descriptor,
	struct stat* statData);
static status_t common_write_stat(struct file_descriptor* descriptor,
	const struct stat* statData, int statMask);
static status_t common_path_read_stat(int fd, char* path, bool traverseLeafLink,
	struct stat* stat, bool kernel);

static status_t vnode_path_to_vnode(struct vnode* vnode, char* path,
	bool traverseLeafLink, bool kernel,
	VnodePutter& _vnode, ino_t* _parentID, char* leafName = NULL);
static status_t dir_vnode_to_path(struct vnode* vnode, char* buffer,
	size_t bufferSize, bool kernel);
static status_t fd_and_path_to_vnode(int fd, char* path, bool traverseLeafLink,
	VnodePutter& _vnode, ino_t* _parentID, bool kernel);
static int32 inc_vnode_ref_count(struct vnode* vnode);
static status_t dec_vnode_ref_count(struct vnode* vnode, bool alwaysFree,
	bool reenter);
static inline void put_vnode(struct vnode* vnode);
static status_t fs_unmount(char* path, dev_t mountID, uint32 flags,
	bool kernel);
static int open_vnode(struct vnode* vnode, int openMode, bool kernel);


static struct fd_ops sFileOps = {
	file_close,
	file_free_fd,
	file_read,
	file_write,
	file_readv,
	file_writev,
	file_seek,
	common_ioctl,
	NULL,		// set_flags()
	file_select,
	file_deselect,
	NULL,		// read_dir()
	NULL,		// rewind_dir()
	common_read_stat,
	common_write_stat,
};

static struct fd_ops sDirectoryOps = {
	dir_close,
	dir_free_fd,
	NULL, NULL,	// read(), write()
	NULL, NULL,	// readv(), writev()
	NULL,		// seek()
	common_ioctl,
	NULL,		// set_flags
	NULL,		// select()
	NULL,		// deselect()
	dir_read,
	dir_rewind,
	common_read_stat,
	common_write_stat,
};

static struct fd_ops sAttributeDirectoryOps = {
	attr_dir_close,
	attr_dir_free_fd,
	NULL, NULL,	// read(), write()
	NULL, NULL,	// readv(), writev()
	NULL,		// seek()
	common_ioctl,
	NULL,		// set_flags
	NULL,		// select()
	NULL,		// deselect()
	attr_dir_read,
	attr_dir_rewind,
	common_read_stat,
	common_write_stat,
};

static struct fd_ops sAttributeOps = {
	attr_close,
	attr_free_fd,
	attr_read,
	attr_write,
	NULL,		// readv()
	NULL,		// writev()
	attr_seek,
	common_ioctl,
	NULL,		// set_flags()
	NULL,		// select()
	NULL,		// deselect()
	NULL,		// read_dir()
	NULL,		// rewind_dir()
	attr_read_stat,
	attr_write_stat,
};

static struct fd_ops sIndexDirectoryOps = {
	index_dir_close,
	index_dir_free_fd,
	NULL, NULL,	// read(), write()
	NULL, NULL,	// readv(), writev()
	NULL,		// seek()
	NULL,		// ioctl()
	NULL,		// set_flags()
	NULL,		// select()
	NULL,		// deselect()
	index_dir_read,
	index_dir_rewind,
	NULL,		// read_stat()
	NULL,		// write_stat()
};

#if 0
static struct fd_ops sIndexOps = {
	NULL,		// dir_close()
	NULL,		// free_fd()
	NULL, NULL,	// read(), write()
	NULL, NULL,	// readv(), writev()
	NULL,		// seek()
	NULL,		// ioctl()
	NULL,		// set_flags
	NULL,		// select()
	NULL,		// deselect()
	NULL,		// dir_read()
	NULL,		// dir_rewind()
	index_read_stat,	// read_stat()
	NULL,		// write_stat()
};
#endif

static struct fd_ops sQueryOps = {
	query_close,
	query_free_fd,
	NULL, NULL,	// read(), write()
	NULL, NULL,	// readv(), writev()
	NULL,		// seek()
	NULL,		// ioctl()
	NULL,		// set_flags()
	NULL,		// select()
	NULL,		// deselect()
	query_read,
	query_rewind,
	NULL,		// read_stat()
	NULL,		// write_stat()
};


namespace {

class FDCloser {
public:
	FDCloser() : fFD(-1), fKernel(true) {}

	FDCloser(int fd, bool kernel) : fFD(fd), fKernel(kernel) {}

	~FDCloser()
	{
		Close();
	}

	void SetTo(int fd, bool kernel)
	{
		Close();
		fFD = fd;
		fKernel = kernel;
	}

	void Close()
	{
		if (fFD >= 0) {
			if (fKernel)
				_kern_close(fFD);
			else
				_user_close(fFD);
			fFD = -1;
		}
	}

	int Detach()
	{
		int fd = fFD;
		fFD = -1;
		return fd;
	}

private:
	int		fFD;
	bool	fKernel;
};

} // namespace


#if VFS_PAGES_IO_TRACING

namespace VFSPagesIOTracing {

class PagesIOTraceEntry : public AbstractTraceEntry {
protected:
	PagesIOTraceEntry(struct vnode* vnode, void* cookie, off_t pos,
		const generic_io_vec* vecs, uint32 count, uint32 flags,
		generic_size_t bytesRequested, status_t status,
		generic_size_t bytesTransferred)
		:
		fVnode(vnode),
		fMountID(vnode->mount->id),
		fNodeID(vnode->id),
		fCookie(cookie),
		fPos(pos),
		fCount(count),
		fFlags(flags),
		fBytesRequested(bytesRequested),
		fStatus(status),
		fBytesTransferred(bytesTransferred)
	{
		fVecs = (generic_io_vec*)alloc_tracing_buffer_memcpy(vecs,
			sizeof(generic_io_vec) * count, false);
	}

	void AddDump(TraceOutput& out, const char* mode)
	{
		out.Print("vfs pages io %5s: vnode: %p (%" B_PRId32 ", %" B_PRId64 "), "
			"cookie: %p, pos: %" B_PRIdOFF ", size: %" B_PRIu64 ", vecs: {",
			mode, fVnode, fMountID, fNodeID, fCookie, fPos,
			(uint64)fBytesRequested);

		if (fVecs != NULL) {
			for (uint32 i = 0; i < fCount; i++) {
				if (i > 0)
					out.Print(", ");
				out.Print("(%" B_PRIx64 ", %" B_PRIu64 ")", (uint64)fVecs[i].base,
					(uint64)fVecs[i].length);
			}
		}

		out.Print("}, flags: %#" B_PRIx32 " -> status: %#" B_PRIx32 ", "
			"transferred: %" B_PRIu64, fFlags, fStatus,
			(uint64)fBytesTransferred);
	}

protected:
	struct vnode*	fVnode;
	dev_t			fMountID;
	ino_t			fNodeID;
	void*			fCookie;
	off_t			fPos;
	generic_io_vec*	fVecs;
	uint32			fCount;
	uint32			fFlags;
	generic_size_t	fBytesRequested;
	status_t		fStatus;
	generic_size_t	fBytesTransferred;
};


class ReadPages : public PagesIOTraceEntry {
public:
	ReadPages(struct vnode* vnode, void* cookie, off_t pos,
		const generic_io_vec* vecs, uint32 count, uint32 flags,
		generic_size_t bytesRequested, status_t status,
		generic_size_t bytesTransferred)
		:
		PagesIOTraceEntry(vnode, cookie, pos, vecs, count, flags,
			bytesRequested, status, bytesTransferred)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		PagesIOTraceEntry::AddDump(out, "read");
	}
};


class WritePages : public PagesIOTraceEntry {
public:
	WritePages(struct vnode* vnode, void* cookie, off_t pos,
		const generic_io_vec* vecs, uint32 count, uint32 flags,
		generic_size_t bytesRequested, status_t status,
		generic_size_t bytesTransferred)
		:
		PagesIOTraceEntry(vnode, cookie, pos, vecs, count, flags,
			bytesRequested, status, bytesTransferred)
	{
		Initialized();
	}

	virtual void AddDump(TraceOutput& out)
	{
		PagesIOTraceEntry::AddDump(out, "write");
	}
};

}	// namespace VFSPagesIOTracing

#	define TPIO(x) new(std::nothrow) VFSPagesIOTracing::x;
#else
#	define TPIO(x) ;
#endif	// VFS_PAGES_IO_TRACING


/*! Finds the mounted device (the fs_mount structure) with the given ID.
	Note, you must hold the sMountLock lock when you call this function.
*/
/**
 * @brief Looks up a mounted file system by its device ID.
 *
 * @param id The device (mount) ID to find.
 * @return Pointer to the fs_mount structure, or NULL if not found.
 * @note Caller must hold sMountLock (read lock at minimum) before calling.
 */
static struct fs_mount*
find_mount(dev_t id)
{
	ASSERT_READ_LOCKED_RW_LOCK(&sMountLock);

	return sMountsTable->Lookup(id);
}


/**
 * @brief Retrieves a mount by device ID and acquires a reference to its root vnode.
 *
 * @param id       The device (mount) ID.
 * @param _mount   Output pointer set to the located fs_mount on success.
 * @retval B_OK        Success; root vnode reference incremented.
 * @retval B_BAD_VALUE No mount found for the given ID.
 * @retval B_BUSY      Mount is in the process of being mounted or unmounted.
 * @note The caller must call put_mount() when done to release the reference.
 */
static status_t
get_mount(dev_t id, struct fs_mount** _mount)
{
	struct fs_mount* mount;

	ReadLocker nodeLocker(sVnodeLock);
	ReadLocker mountLocker(sMountLock);

	mount = find_mount(id);
	if (mount == NULL)
		return B_BAD_VALUE;

	struct vnode* rootNode = mount->root_vnode;
	if (mount->unmounting || rootNode == NULL || rootNode->IsBusy()
			|| rootNode->ref_count == 0) {
		// might have been called during a mount/unmount operation
		return B_BUSY;
	}

	inc_vnode_ref_count(rootNode);
	*_mount = mount;
	return B_OK;
}


/**
 * @brief Releases the reference on a mount acquired by get_mount().
 *
 * @param mount The mount whose root vnode reference shall be released.
 *              May be NULL (no-op).
 * @note Internally calls put_vnode() on mount->root_vnode.
 */
static void
put_mount(struct fs_mount* mount)
{
	if (mount)
		put_vnode(mount->root_vnode);
}


/*!	Tries to open the specified file system module.
	Accepts a file system name of the form "bfs" or "file_systems/bfs/v1".
	Returns a pointer to file system module interface, or NULL if it
	could not open the module.
*/
/**
 * @brief Opens the kernel module for a named file system.
 *
 * Accepts a short name such as "bfs" or a full module path like
 * "file_systems/bfs/v1".
 *
 * @param fsName Name (short or full) of the file system module to load.
 * @return Pointer to the file_system_module_info on success, or NULL if the
 *         module could not be found or opened.
 */
static file_system_module_info*
get_file_system(const char* fsName)
{
	char name[B_FILE_NAME_LENGTH];
	if (strncmp(fsName, "file_systems/", strlen("file_systems/"))) {
		// construct module name if we didn't get one
		// (we currently support only one API)
		snprintf(name, sizeof(name), "file_systems/%s/v1", fsName);
		fsName = NULL;
	}

	file_system_module_info* info;
	if (get_module(fsName ? fsName : name, (module_info**)&info) != B_OK)
		return NULL;

	return info;
}


/*!	Accepts a file system name of the form "bfs" or "file_systems/bfs/v1"
	and returns a compatible fs_info.fsh_name name ("bfs" in both cases).
	The name is allocated for you, and you have to free() it when you're
	done with it.
	Returns NULL if the required memory is not available.
*/
/**
 * @brief Converts a file system module path to a canonical short name.
 *
 * Given "file_systems/bfs/v1" or "bfs", returns a heap-allocated "bfs"
 * suitable for use in fs_info::fsh_name.
 *
 * @param fsName Full module path or short file system name.
 * @return Newly allocated canonical name string, or NULL on allocation
 *         failure. Caller must free().
 */
static char*
get_file_system_name(const char* fsName)
{
	const size_t length = strlen("file_systems/");

	if (strncmp(fsName, "file_systems/", length)) {
		// the name already seems to be the module's file name
		return strdup(fsName);
	}

	fsName += length;
	const char* end = strchr(fsName, '/');
	if (end == NULL) {
		// this doesn't seem to be a valid name, but well...
		return strdup(fsName);
	}

	// cut off the trailing /v1

	char* name = (char*)malloc(end + 1 - fsName);
	if (name == NULL)
		return NULL;

	strlcpy(name, fsName, end + 1 - fsName);
	return name;
}


/*!	Accepts a list of file system names separated by a colon, one for each
	layer and returns the file system name for the specified layer.
	The name is allocated for you, and you have to free() it when you're
	done with it.
	Returns NULL if the required memory is not available or if there is no
	name for the specified layer.
*/
/**
 * @brief Extracts the file system name for a specific stacking layer.
 *
 * Parses a colon-separated list of FS names (e.g. "overlay:bfs") and
 * returns the name at position \a layer (0-based).
 *
 * @param fsNames Colon-separated list of file system names.
 * @param layer   Zero-based layer index to extract.
 * @return Newly allocated name string for the requested layer, or NULL if
 *         the layer does not exist or memory could not be allocated. Caller
 *         must free().
 */
static char*
get_file_system_name_for_layer(const char* fsNames, int32 layer)
{
	while (layer >= 0) {
		const char* end = strchr(fsNames, ':');
		if (end == NULL) {
			if (layer == 0)
				return strdup(fsNames);
			return NULL;
		}

		if (layer == 0) {
			size_t length = end - fsNames + 1;
			char* result = (char*)malloc(length);
			strlcpy(result, fsNames, length);
			return result;
		}

		fsNames = end + 1;
		layer--;
	}

	return NULL;
}


/**
 * @brief Adds a vnode to the mount's vnode list.
 *
 * @param vnode  The vnode to add.
 * @param mount  The mount the vnode belongs to.
 * @note Acquires and releases mount->lock internally.
 */
static void
add_vnode_to_mount_list(struct vnode* vnode, struct fs_mount* mount)
{
	MutexLocker _(mount->lock);
	mount->vnodes.Add(vnode);
}


/**
 * @brief Removes a vnode from the mount's vnode list.
 *
 * @param vnode  The vnode to remove.
 * @param mount  The mount the vnode belongs to.
 * @note Acquires and releases mount->lock internally.
 */
static void
remove_vnode_from_mount_list(struct vnode* vnode, struct fs_mount* mount)
{
	MutexLocker _(mount->lock);
	mount->vnodes.Remove(vnode);
}


/*!	\brief Looks up a vnode by mount and node ID in the sVnodeTable.

	The caller must hold the sVnodeLock (read lock at least).

	\param mountID the mount ID.
	\param vnodeID the node ID.

	\return The vnode structure, if it was found in the hash table, \c NULL
			otherwise.
*/
static struct vnode*
lookup_vnode(dev_t mountID, ino_t vnodeID)
{
	ASSERT_READ_LOCKED_RW_LOCK(&sVnodeLock);

	struct vnode_hash_key key;

	key.device = mountID;
	key.vnode = vnodeID;

	return sVnodeTable->Lookup(key);
}


/*!	\brief Checks whether or not a busy vnode should be waited for (again).

	This will also wait for BUSY_VNODE_DELAY before returning if one should
	still wait for the vnode becoming unbusy.

	\return \c true if one should retry, \c false if not.
*/
/**
 * @brief Decides whether to retry waiting for a busy vnode and sleeps briefly.
 *
 * Decrements the retry counter and delays for BUSY_VNODE_DELAY microseconds
 * before returning.
 *
 * @param tries   In/out retry counter; decremented on each call.
 * @param mountID Device ID used for the diagnostic message on exhaustion.
 * @param vnodeID Vnode ID used for the diagnostic message on exhaustion.
 * @return true if the caller should retry; false when retries are exhausted.
 */
static bool
retry_busy_vnode(int32& tries, dev_t mountID, ino_t vnodeID)
{
	if (--tries < 0) {
		// vnode doesn't seem to become unbusy
		dprintf("vnode %" B_PRIdDEV ":%" B_PRIdINO
			" is not becoming unbusy!\n", mountID, vnodeID);
		return false;
	}
	snooze(BUSY_VNODE_DELAY);
	return true;
}


/*!	Creates a new vnode with the given mount and node ID.
	If the node already exists, it is returned instead and no new node is
	created. In either case -- but not, if an error occurs -- the function write
	locks \c sVnodeLock and keeps it locked for the caller when returning. On
	error the lock is not held on return.

	\param mountID The mount ID.
	\param vnodeID The vnode ID.
	\param _vnode Will be set to the new vnode on success.
	\param _nodeCreated Will be set to \c true when the returned vnode has
		been newly created, \c false when it already existed. Will not be
		changed on error.
	\return \c B_OK, when the vnode was successfully created and inserted or
		a node with the given ID was found, \c B_NO_MEMORY or
		\c B_ENTRY_NOT_FOUND on error.
*/
/**
 * @brief Creates a new vnode for the given mount/node ID pair and write-locks sVnodeLock.
 *
 * Allocates a fresh vnode, inserts it into sVnodeTable and the mount's vnode
 * list. If a vnode with the same ID already exists it is returned instead and
 * no new node is created.  On success (both new and existing) sVnodeLock is
 * held write-locked for the caller. On error sVnodeLock is NOT held.
 *
 * @param mountID      The mount (device) ID.
 * @param vnodeID      The file system node ID.
 * @param _vnode       Set to the new or existing vnode on success.
 * @param _nodeCreated Set to true when a new vnode was created, false when an
 *                     existing one was returned.  Unchanged on error.
 * @retval B_OK            Success.
 * @retval B_NO_MEMORY     Allocation failure.
 * @retval B_ENTRY_NOT_FOUND Mount not found or is being unmounted.
 * @note Caller must not hold sVnodeLock or sMountLock on entry.
 */
static status_t
create_new_vnode_and_lock(dev_t mountID, ino_t vnodeID, struct vnode*& _vnode,
	bool& _nodeCreated)
{
	FUNCTION(("create_new_vnode_and_lock()\n"));

	struct vnode* vnode = (struct vnode*)object_cache_alloc(sVnodeCache, 0);
	if (vnode == NULL)
		return B_NO_MEMORY;

	// initialize basic values
	memset(vnode, 0, sizeof(struct vnode));
	vnode->device = mountID;
	vnode->id = vnodeID;
	vnode->ref_count = 1;
	vnode->SetBusy(true);

	// look up the node -- it might have been added by someone else in the
	// meantime
	rw_lock_write_lock(&sVnodeLock);
	struct vnode* existingVnode = lookup_vnode(mountID, vnodeID);
	if (existingVnode != NULL) {
		object_cache_free(sVnodeCache, vnode, 0);
		_vnode = existingVnode;
		_nodeCreated = false;
		return B_OK;
	}

	// get the mount structure
	rw_lock_read_lock(&sMountLock);
	vnode->mount = find_mount(mountID);
	if (!vnode->mount || vnode->mount->unmounting) {
		rw_lock_read_unlock(&sMountLock);
		rw_lock_write_unlock(&sVnodeLock);
		object_cache_free(sVnodeCache, vnode, 0);
		return B_ENTRY_NOT_FOUND;
	}

	// add the vnode to the mount's node list and the hash table
	sVnodeTable->Insert(vnode);
	add_vnode_to_mount_list(vnode, vnode->mount);

	rw_lock_read_unlock(&sMountLock);

	_vnode = vnode;
	_nodeCreated = true;

	// keep the vnode lock locked
	return B_OK;
}


/*!	Frees the vnode and all resources it has acquired, and removes
	it from the vnode hash as well as from its mount structure.
	Will also make sure that any cache modifications are written back.
*/
/**
 * @brief Frees all resources held by a vnode and removes it from global tables.
 *
 * Writes back cached pages (if not removed), calls the FS put_vnode or
 * remove_vnode hook, notifies the VM cache, removes the vnode from
 * sVnodeTable and the mount list, and returns it to sVnodeCache.
 *
 * @param vnode   The vnode to free.  Must have ref_count == 0 and be busy.
 * @param reenter true if called (indirectly) from within a file system hook.
 * @note Caller must not hold sVnodeLock or sMountLock.
 */
static void
free_vnode(struct vnode* vnode, bool reenter)
{
	ASSERT_PRINT(vnode->ref_count == 0 && vnode->IsBusy(), "vnode: %p\n",
		vnode);
	ASSERT_PRINT(vnode->advisory_locking == NULL, "vnode: %p\n", vnode);

	// write back any changes in this vnode's cache -- but only
	// if the vnode won't be deleted, in which case the changes
	// will be discarded

	if (!vnode->IsRemoved() && HAS_FS_CALL(vnode, fsync))
		FS_CALL(vnode, fsync, false);

	// Note: If this vnode has a cache attached, there will still be two
	// references to that cache at this point. The last one belongs to the vnode
	// itself (cf. vfs_get_vnode_cache()) and one belongs to the node's file
	// cache. Each but the last reference to a cache also includes a reference
	// to the vnode. The file cache, however, released its reference (cf.
	// file_cache_create()), so that this vnode's ref count has the chance to
	// ever drop to 0. Deleting the file cache now, will cause the next to last
	// cache reference to be released, which will also release a (no longer
	// existing) vnode reference. To ensure that will be ignored, and that no
	// other consumers will acquire this vnode in the meantime, we make the
	// vnode's ref count negative.
	vnode->ref_count = -1;

	if (!vnode->IsUnpublished()) {
		if (vnode->IsRemoved())
			FS_CALL(vnode, remove_vnode, reenter);
		else
			FS_CALL(vnode, put_vnode, reenter);
	}

	// If the vnode has a VMCache attached, make sure that it won't try to get
	// another reference via VMVnodeCache::AcquireUnreferencedStoreRef(). As
	// long as the vnode is busy and in the hash, that won't happen, but as
	// soon as we've removed it from the hash, it could reload the vnode -- with
	// a new cache attached!
	if (vnode->cache != NULL && vnode->cache->type == CACHE_TYPE_VNODE)
		((VMVnodeCache*)vnode->cache)->VnodeDeleted();

	// The file system has removed the resources of the vnode now, so we can
	// make it available again (by removing the busy vnode from the hash).
	rw_lock_write_lock(&sVnodeLock);
	sVnodeTable->Remove(vnode);
	rw_lock_write_unlock(&sVnodeLock);

	// if we have a VMCache attached, remove it
	if (vnode->cache)
		vnode->cache->ReleaseRef();

	vnode->cache = NULL;

	remove_vnode_from_mount_list(vnode, vnode->mount);

	object_cache_free(sVnodeCache, vnode, 0);
}


/*!	\brief Decrements the reference counter of the given vnode and deletes it,
	if the counter dropped to 0.

	The caller must, of course, own a reference to the vnode to call this
	function.
	The caller must not hold the sVnodeLock or the sMountLock.

	\param vnode the vnode.
	\param alwaysFree don't move this vnode into the unused list, but really
		   delete it if possible.
	\param reenter \c true, if this function is called (indirectly) from within
		   a file system. This will be passed to file system hooks only.
	\return \c B_OK, if everything went fine, an error code otherwise.
*/
/**
 * @brief Decrements a vnode's reference count and frees it when it reaches zero.
 *
 * When the count drops to zero the vnode is either placed on the unused list
 * (for later eviction) or freed immediately depending on \a alwaysFree and the
 * vnode's removed flag.
 *
 * @param vnode      The vnode whose reference count is to be decremented.
 * @param alwaysFree If true, skip the unused list and free the vnode directly.
 * @param reenter    true if called from within a file system hook.
 * @retval B_OK Always.
 * @note Caller must own a reference and must NOT hold sVnodeLock or sMountLock.
 */
static status_t
dec_vnode_ref_count(struct vnode* vnode, bool alwaysFree, bool reenter)
{
	ReadLocker locker(sVnodeLock);
	AutoLocker<Vnode> nodeLocker(vnode);

	const int32 oldRefCount = atomic_add(&vnode->ref_count, -1);
	ASSERT_PRINT(oldRefCount > 0, "vnode %p\n", vnode);

	TRACE(("dec_vnode_ref_count: vnode %p, ref now %" B_PRId32 "\n", vnode,
		vnode->ref_count));

	if (oldRefCount != 1)
		return B_OK;

	if (vnode->IsBusy())
		panic("dec_vnode_ref_count: called on busy vnode %p\n", vnode);

	if (vnode->mount->unmounting)
		alwaysFree = true;

	bool freeNode = false;
	bool freeUnusedNodes = false;

	// Just insert the vnode into an unused list if we don't need
	// to delete it
	if (vnode->IsRemoved() || alwaysFree) {
		vnode_to_be_freed(vnode);
		vnode->SetBusy(true);
		freeNode = true;
	} else
		freeUnusedNodes = vnode_unused(vnode);

	nodeLocker.Unlock();
	locker.Unlock();

	if (freeNode)
		free_vnode(vnode, reenter);
	else if (freeUnusedNodes)
		free_unused_vnodes();

	return B_OK;
}


/*!	\brief Increments the reference counter of the given vnode.

	The caller must make sure that the node isn't deleted while this function
	is called. This can be done either:
	- by ensuring that a reference to the node exists and remains in existence,
	  or
	- by holding the vnode's lock (which also requires read locking sVnodeLock)
	  or by holding sVnodeLock write locked.

	In the second case the caller is responsible for dealing with the ref count
	0 -> 1 transition. That is 1. this function must not be invoked when the
	node is busy in the first place and 2. vnode_used() must be called for the
	node.

	\param vnode the vnode.
	\returns the old reference count.
*/
/**
 * @brief Atomically increments a vnode's reference count.
 *
 * @param vnode The vnode to reference.
 * @return The reference count value before the increment.
 * @note Caller must ensure the vnode cannot be freed concurrently
 *       (e.g. already holds a reference, or holds sVnodeLock read-locked
 *       with the vnode locked, or holds sVnodeLock write-locked).
 */
static int32
inc_vnode_ref_count(struct vnode* vnode)
{
	const int32 oldCount = atomic_add(&vnode->ref_count, 1);
	TRACE(("inc_vnode_ref_count: vnode %p, ref now %" B_PRId32 "\n", vnode,
		oldCount + 1));
	ASSERT(oldCount >= 0);
	return oldCount;
}


/**
 * @brief Tests whether a node type requires a special sub-node (e.g. FIFO).
 *
 * @param type POSIX mode type bits (e.g. S_IFIFO).
 * @return true if the type needs a special sub-node, false otherwise.
 */
static bool
is_special_node_type(int type)
{
	// at the moment only FIFOs are supported
	return S_ISFIFO(type);
}


/**
 * @brief Creates and attaches a special sub-node (e.g. FIFO) to an existing vnode.
 *
 * @param vnode The vnode for which the sub-node should be created.
 * @param flags Flags forwarded from the FS publish call.
 * @retval B_OK        Sub-node created successfully.
 * @retval B_BAD_VALUE The vnode type does not need a special sub-node.
 */
static status_t
create_special_sub_node(struct vnode* vnode, uint32 flags)
{
	if (S_ISFIFO(vnode->Type()))
		return create_fifo_vnode(vnode->mount->volume, vnode);

	return B_BAD_VALUE;
}


/*!	\brief Retrieves a vnode for a given mount ID, node ID pair.

	If the node is not yet in memory, it will be loaded.

	The caller must not hold the sVnodeLock or the sMountLock.

	\param mountID the mount ID.
	\param vnodeID the node ID.
	\param _vnode Pointer to a vnode* variable into which the pointer to the
		   retrieved vnode structure shall be written.
	\param reenter \c true, if this function is called (indirectly) from within
		   a file system.
	\return \c B_OK, if everything when fine, an error code otherwise.
*/
/**
 * @brief Retrieves (and if necessary loads) a vnode by mount and node ID.
 *
 * If the vnode is already cached, its reference count is incremented and it is
 * returned immediately.  Otherwise a new vnode is created, the FS get_vnode()
 * hook is called to populate it, and the vnode is published.
 *
 * @param mountID  The device (mount) ID of the desired vnode.
 * @param vnodeID  The file system node ID.
 * @param _vnode   Output: set to the located vnode on success.
 * @param canWait  If true, sleep briefly and retry when the vnode is busy.
 * @param reenter  true when called from within a file system hook.
 * @retval B_OK          Success; caller owns one reference to *_vnode.
 * @retval B_BUSY        Vnode is busy and canWait is false, or retries exhausted.
 * @retval B_NO_MEMORY   Allocation failure.
 * @retval B_ENTRY_NOT_FOUND Mount is being unmounted.
 * @note Caller must NOT hold sVnodeLock or sMountLock.
 */
static status_t
get_vnode(dev_t mountID, ino_t vnodeID, struct vnode** _vnode, bool canWait,
	int reenter)
{
	FUNCTION(("get_vnode: mountid %" B_PRId32 " vnid 0x%" B_PRIx64 " %p\n",
		mountID, vnodeID, _vnode));

	rw_lock_read_lock(&sVnodeLock);

	int32 tries = BUSY_VNODE_RETRIES;
restart:
	struct vnode* vnode = lookup_vnode(mountID, vnodeID);

	if (vnode != NULL && !vnode->IsBusy()) {
		// Try to increment the vnode's reference count without locking.
		// (We can't use atomic_add here, as if the vnode is unused,
		// we need to hold its lock to mark it used again.)
		const int32 oldRefCount = atomic_get(&vnode->ref_count);
		if (oldRefCount > 0 && atomic_test_and_set(&vnode->ref_count,
				oldRefCount + 1, oldRefCount) == oldRefCount) {
			rw_lock_read_unlock(&sVnodeLock);
			*_vnode = vnode;
			return B_OK;
		}
	}

	AutoLocker<Vnode> nodeLocker(vnode);

	if (vnode != NULL && vnode->IsBusy()) {
		// vnodes in the Removed state (except ones still Unpublished)
		// which are also Busy will disappear soon, so we do not wait for them.
		const bool doNotWait = vnode->IsRemoved() && !vnode->IsUnpublished();

		nodeLocker.Unlock();
		rw_lock_read_unlock(&sVnodeLock);
		if (!canWait) {
			dprintf("vnode %" B_PRIdDEV ":%" B_PRIdINO " is busy!\n",
				mountID, vnodeID);
			return B_BUSY;
		}
		if (doNotWait || !retry_busy_vnode(tries, mountID, vnodeID))
			return B_BUSY;

		rw_lock_read_lock(&sVnodeLock);
		goto restart;
	}

	TRACE(("get_vnode: tried to lookup vnode, got %p\n", vnode));

	if (vnode != NULL) {
		if (inc_vnode_ref_count(vnode) == 0) {
			// this vnode has been unused before
			vnode_used(vnode);
		}

		nodeLocker.Unlock();
		rw_lock_read_unlock(&sVnodeLock);
	} else {
		// we need to create a new vnode and read it in
		rw_lock_read_unlock(&sVnodeLock);
			// unlock -- create_new_vnode_and_lock() write-locks on success
		bool nodeCreated;
		status_t status = create_new_vnode_and_lock(mountID, vnodeID, vnode,
			nodeCreated);
		if (status != B_OK)
			return status;

		if (!nodeCreated) {
			rw_lock_read_lock(&sVnodeLock);
			rw_lock_write_unlock(&sVnodeLock);
			goto restart;
		}

		rw_lock_write_unlock(&sVnodeLock);

		int type = 0;
		uint32 flags = 0;
		status = FS_MOUNT_CALL(vnode->mount, get_vnode, vnodeID, vnode, &type,
			&flags, reenter);
		if (status == B_OK && (vnode->private_node == NULL || vnode->ops == NULL)) {
			KDEBUG_ONLY(panic("filesystem get_vnode returned 0 with unset fields"));
			status = B_BAD_VALUE;
		}

		bool gotNode = status == B_OK;
		bool publishSpecialSubNode = false;
		if (gotNode) {
			vnode->SetType(type);
			publishSpecialSubNode = is_special_node_type(type)
				&& (flags & B_VNODE_DONT_CREATE_SPECIAL_SUB_NODE) == 0;
		}

		if (gotNode && publishSpecialSubNode)
			status = create_special_sub_node(vnode, flags);

		if (status != B_OK) {
			if (gotNode)
				FS_CALL(vnode, put_vnode, reenter);

			rw_lock_write_lock(&sVnodeLock);
			sVnodeTable->Remove(vnode);
			remove_vnode_from_mount_list(vnode, vnode->mount);
			rw_lock_write_unlock(&sVnodeLock);

			object_cache_free(sVnodeCache, vnode, 0);
			return status;
		}

		rw_lock_read_lock(&sVnodeLock);
		vnode->Lock();

		vnode->SetRemoved((flags & B_VNODE_PUBLISH_REMOVED) != 0);
		vnode->SetBusy(false);

		vnode->Unlock();
		rw_lock_read_unlock(&sVnodeLock);
	}

	TRACE(("get_vnode: returning %p\n", vnode));

	*_vnode = vnode;
	return B_OK;
}


/*!	\brief Decrements the reference counter of the given vnode and deletes it,
	if the counter dropped to 0.

	The caller must, of course, own a reference to the vnode to call this
	function.
	The caller must not hold the sVnodeLock or the sMountLock.

	\param vnode the vnode.
*/
/**
 * @brief Releases one reference to a vnode, potentially freeing it.
 *
 * Thin wrapper around dec_vnode_ref_count() with alwaysFree=false, reenter=false.
 *
 * @param vnode The vnode to dereference.
 * @note Caller must own a reference and must NOT hold sVnodeLock or sMountLock.
 */
static inline void
put_vnode(struct vnode* vnode)
{
	dec_vnode_ref_count(vnode, false, false);
}


/**
 * @brief Frees a number of unused (zero-ref) vnodes in response to memory pressure.
 *
 * The number of vnodes freed is proportional to the \a level of the low-resource
 * notification: note, warning, or critical.
 *
 * @param level Low-resource level (B_NO_LOW_RESOURCE, B_LOW_RESOURCE_NOTE,
 *              B_LOW_RESOURCE_WARNING, or B_LOW_RESOURCE_CRITICAL).
 */
static void
free_unused_vnodes(int32 level)
{
	unused_vnodes_check_started();

	if (level == B_NO_LOW_RESOURCE) {
		unused_vnodes_check_done();
		return;
	}

	flush_hot_vnodes();

	// determine how many nodes to free
	uint32 count = 1;
	{
		ReadLocker hotVnodesReadLocker(sHotVnodesLock);
		InterruptsSpinLocker unusedVnodesLocker(sUnusedVnodesLock);

		switch (level) {
			case B_LOW_RESOURCE_NOTE:
				count = sUnusedVnodes / 100;
				break;
			case B_LOW_RESOURCE_WARNING:
				count = sUnusedVnodes / 10;
				break;
			case B_LOW_RESOURCE_CRITICAL:
				count = sUnusedVnodes;
				break;
		}

		if (count > sUnusedVnodes)
			count = sUnusedVnodes;
	}

	// Write back the modified pages of some unused vnodes and free them.

	for (uint32 i = 0; i < count; i++) {
		ReadLocker vnodesReadLocker(sVnodeLock);

		// get the first node
		rw_lock_read_lock(&sHotVnodesLock);
		InterruptsSpinLocker unusedVnodesLocker(sUnusedVnodesLock);
		struct vnode* vnode = sUnusedVnodeList.First();
		unusedVnodesLocker.Unlock();
		rw_lock_read_unlock(&sHotVnodesLock);

		if (vnode == NULL)
			break;

		// lock the node
		AutoLocker<Vnode> nodeLocker(vnode);

		// Check whether the node is still unused -- since we only append to the
		// tail of the unused queue, the vnode should still be at its head.
		//
		// Alternatively we could check its ref count for 0 and its busy flag,
		// but if the node is no longer at the head of the queue, it means it
		// has been touched in the meantime, i.e. it is no longer the least
		// recently used unused vnode and we rather don't free it.
		//
		// (We skip acquiring the unused lock here, since the vnode can't be
		// removed from the unused list without its lock being held.)
		if (vnode != sUnusedVnodeList.First())
			continue;

		ASSERT(!vnode->IsBusy() && vnode->ref_count == 0);

		// grab a reference
		inc_vnode_ref_count(vnode);
		vnode_used(vnode);

		// write back changes and free the node
		nodeLocker.Unlock();
		vnodesReadLocker.Unlock();

		if (vnode->cache != NULL)
			vnode->cache->WriteModified();

		dec_vnode_ref_count(vnode, true, false);
			// this should free the vnode when it's still unused
	}

	unused_vnodes_check_done();
}


/*!	Gets the vnode the given vnode is covering.

	The caller must have \c sVnodeLock read-locked at least.

	The function returns a reference to the retrieved vnode (if any), the caller
	is responsible to free.

	\param vnode The vnode whose covered node shall be returned.
	\return The covered vnode, or \c NULL if the given vnode doesn't cover any
		vnode.
*/
/**
 * @brief Returns the deepest vnode that the given vnode covers (sVnodeLock held).
 *
 * Walks the covers chain to find the bottom-most covered vnode and increments
 * its reference count.
 *
 * @param vnode The covering vnode to resolve.
 * @return The covered vnode with an acquired reference, or NULL if \a vnode
 *         does not cover any other vnode.
 * @note Caller must hold sVnodeLock (read lock at minimum).
 */
static inline Vnode*
get_covered_vnode_locked(Vnode* vnode)
{
	if (Vnode* coveredNode = vnode->covers) {
		while (coveredNode->covers != NULL)
			coveredNode = coveredNode->covers;

		inc_vnode_ref_count(coveredNode);
		return coveredNode;
	}

	return NULL;
}


/*!	Gets the vnode the given vnode is covering.

	The caller must not hold \c sVnodeLock. Note that this implies a race
	condition, since the situation can change at any time.

	The function returns a reference to the retrieved vnode (if any), the caller
	is responsible to free.

	\param vnode The vnode whose covered node shall be returned.
	\return The covered vnode, or \c NULL if the given vnode doesn't cover any
		vnode.
*/
/**
 * @brief Returns the deepest vnode that the given vnode covers (no lock held).
 *
 * Acquires sVnodeLock internally for the lookup.  Note that the result may
 * be stale by the time it is used.
 *
 * @param vnode The covering vnode to resolve.
 * @return The covered vnode with an acquired reference, or NULL.
 * @note Caller must NOT hold sVnodeLock.
 */
static inline Vnode*
get_covered_vnode(Vnode* vnode)
{
	if (!vnode->IsCovering())
		return NULL;

	ReadLocker vnodeReadLocker(sVnodeLock);
	return get_covered_vnode_locked(vnode);
}


/*!	Gets the vnode the given vnode is covered by.

	The caller must have \c sVnodeLock read-locked at least.

	The function returns a reference to the retrieved vnode (if any), the caller
	is responsible to free.

	\param vnode The vnode whose covering node shall be returned.
	\return The covering vnode, or \c NULL if the given vnode isn't covered by
		any vnode.
*/
/**
 * @brief Returns the topmost vnode covering the given vnode (sVnodeLock held).
 *
 * Walks the covered_by chain to the top-most covering vnode and increments
 * its reference count.
 *
 * @param vnode The vnode that may be covered.
 * @return The covering vnode with an acquired reference, or NULL if the vnode
 *         is not covered.
 * @note Caller must hold sVnodeLock (read lock at minimum).
 */
static Vnode*
get_covering_vnode_locked(Vnode* vnode)
{
	if (Vnode* coveringNode = vnode->covered_by) {
		while (coveringNode->covered_by != NULL)
			coveringNode = coveringNode->covered_by;

		inc_vnode_ref_count(coveringNode);
		return coveringNode;
	}

	return NULL;
}


/*!	Gets the vnode the given vnode is covered by.

	The caller must not hold \c sVnodeLock. Note that this implies a race
	condition, since the situation can change at any time.

	The function returns a reference to the retrieved vnode (if any), the caller
	is responsible to free.

	\param vnode The vnode whose covering node shall be returned.
	\return The covering vnode, or \c NULL if the given vnode isn't covered by
		any vnode.
*/
/**
 * @brief Returns the topmost vnode covering the given vnode (no lock held).
 *
 * Acquires sVnodeLock internally for the lookup.
 *
 * @param vnode The vnode that may be covered.
 * @return The covering vnode with an acquired reference, or NULL.
 * @note Caller must NOT hold sVnodeLock.
 */
static inline Vnode*
get_covering_vnode(Vnode* vnode)
{
	if (!vnode->IsCovered())
		return NULL;

	ReadLocker vnodeReadLocker(sVnodeLock);
	return get_covering_vnode_locked(vnode);
}


/**
 * @brief Frees unused vnodes based on the current low-resource state.
 *
 * Queries the current low-resource level and delegates to the parameterised
 * overload of free_unused_vnodes().
 */
static void
free_unused_vnodes()
{
	free_unused_vnodes(
		low_resource_state(B_KERNEL_RESOURCE_PAGES | B_KERNEL_RESOURCE_MEMORY
			| B_KERNEL_RESOURCE_ADDRESS_SPACE));
}


/**
 * @brief Low-resource callback that triggers unused-vnode eviction.
 *
 * Registered with the low_resource_manager to free cached vnodes when memory
 * or address-space pressure is detected.
 *
 * @param data      Unused opaque data pointer (always NULL).
 * @param resources Bitmask of the resources that are low.
 * @param level     Severity level of the low-resource condition.
 */
static void
vnode_low_resource_handler(void* /*data*/, uint32 resources, int32 level)
{
	TRACE(("vnode_low_resource_handler(level = %" B_PRId32 ")\n", level));

	free_unused_vnodes(level);
}


/**
 * @brief Releases the lock on an advisory_locking object acquired via get_advisory_locking().
 *
 * @param locking The advisory_locking object to release.
 */
static inline void
put_advisory_locking(struct advisory_locking* locking)
{
	release_sem(locking->lock);
}


/*!	Returns the advisory_locking object of the \a vnode in case it
	has one, and locks it.
	You have to call put_advisory_locking() when you're done with
	it.
	Note, you must not have the vnode mutex locked when calling
	this function.
*/
/**
 * @brief Returns the locked advisory_locking object for a vnode.
 *
 * If the vnode has an advisory_locking attached, acquires its semaphore so that
 * the caller can safely inspect and modify the lock list.  Call
 * put_advisory_locking() when done.
 *
 * @param vnode The vnode whose advisory_locking shall be retrieved.
 * @return Pointer to the locked advisory_locking, or NULL if none exists.
 * @note Caller must NOT hold the vnode mutex when calling this function.
 */
static struct advisory_locking*
get_advisory_locking(struct vnode* vnode)
{
	rw_lock_read_lock(&sVnodeLock);
	vnode->Lock();

	struct advisory_locking* locking = vnode->advisory_locking;
	sem_id lock = locking != NULL ? locking->lock : B_ERROR;

	vnode->Unlock();
	rw_lock_read_unlock(&sVnodeLock);

	if (lock >= 0)
		lock = acquire_sem(lock);
	if (lock < 0) {
		// This means the locking has been deleted in the mean time
		// or had never existed in the first place - otherwise, we
		// would get the lock at some point.
		return NULL;
	}

	return locking;
}


/*!	Creates a locked advisory_locking object, and attaches it to the
	given \a vnode.
	Returns B_OK in case of success - also if the vnode got such an
	object from someone else in the mean time, you'll still get this
	one locked then.
*/
/**
 * @brief Ensures an advisory_locking object is attached to a vnode.
 *
 * Creates a new advisory_locking with its semaphore locked and attaches it to
 * the vnode if none exists.  If another thread wins the race, the newly created
 * locking is discarded and the existing one is returned locked.
 *
 * @param vnode The vnode to attach locking to.
 * @retval B_OK        An advisory_locking is now attached and locked.
 * @retval B_FILE_ERROR vnode is NULL.
 * @retval B_NO_MEMORY Allocation failure.
 */
static status_t
create_advisory_locking(struct vnode* vnode)
{
	if (vnode == NULL)
		return B_FILE_ERROR;

	ObjectDeleter<advisory_locking> lockingDeleter;
	struct advisory_locking* locking = NULL;

	while (get_advisory_locking(vnode) == NULL) {
		// no locking object set on the vnode yet, create one
		if (locking == NULL) {
			locking = new(std::nothrow) advisory_locking;
			if (locking == NULL)
				return B_NO_MEMORY;
			lockingDeleter.SetTo(locking);

			locking->wait_sem = create_sem(0, "advisory lock");
			if (locking->wait_sem < 0)
				return locking->wait_sem;

			locking->lock = create_sem(0, "advisory locking");
			if (locking->lock < 0)
				return locking->lock;
		}

		// set our newly created locking object
		ReadLocker _(sVnodeLock);
		AutoLocker<Vnode> nodeLocker(vnode);
		if (vnode->advisory_locking == NULL) {
			vnode->advisory_locking = locking;
			lockingDeleter.Detach();
			return B_OK;
		}
	}

	// The vnode already had a locking object. That's just as well.

	return B_OK;
}


/*! Returns \c true when either \a flock is \c NULL or the \a flock intersects
	with the advisory_lock \a lock.
*/
/**
 * @brief Tests whether an advisory lock intersects with a given flock range.
 *
 * @param lock  The existing advisory_lock to check.
 * @param flock The flock range to compare against.  If NULL, always returns true.
 * @return true if the ranges overlap, false otherwise.
 */
static bool
advisory_lock_intersects(struct advisory_lock* lock, struct flock* flock)
{
	if (flock == NULL)
		return true;

	return lock->start <= flock->l_start - 1 + flock->l_len
		&& lock->end >= flock->l_start;
}


/*!	Tests whether acquiring a lock would block.
*/
/**
 * @brief Tests whether acquiring an advisory lock would block.
 *
 * Sets flock->l_type to F_UNLCK if no conflicting lock exists, or fills in the
 * conflicting lock's details otherwise.
 *
 * @param vnode The vnode on which to test the lock.
 * @param flock Input: desired lock range and type.  Output: conflicting lock
 *              info or type set to F_UNLCK.
 * @retval B_OK Always (conflict information is returned via \a flock).
 */
static status_t
test_advisory_lock(struct vnode* vnode, struct flock* flock)
{
	flock->l_type = F_UNLCK;

	struct advisory_locking* locking = get_advisory_locking(vnode);
	if (locking == NULL)
		return B_OK;

	team_id team = team_get_current_team_id();

	LockList::Iterator iterator = locking->locks.GetIterator();
	while (iterator.HasNext()) {
		struct advisory_lock* lock = iterator.Next();

		 if (lock->team != team && advisory_lock_intersects(lock, flock)) {
			// locks do overlap
			if (flock->l_type != F_RDLCK || !lock->shared) {
				// collision
				flock->l_type = lock->shared ? F_RDLCK : F_WRLCK;
				flock->l_whence = SEEK_SET;
				flock->l_start = lock->start;
				flock->l_len = lock->end - lock->start + 1;
				flock->l_pid = lock->team;
				break;
			}
		}
	}

	put_advisory_locking(locking);
	return B_OK;
}


/*!	Removes the specified lock, or all locks of the calling team
	if \a flock is NULL.
*/
/**
 * @brief Releases one or more advisory locks on a vnode.
 *
 * Removes locks matching the descriptor (flock() semantics) or the io_context
 * plus range (POSIX semantics).  If flock is NULL, all matching locks for the
 * team are removed.
 *
 * @param vnode      The vnode on which to release locks.
 * @param context    The io_context of the calling team (POSIX locks).
 * @param descriptor If non-NULL, removes flock()-style locks bound to this FD.
 * @param flock      The range to release (POSIX), or NULL to release all.
 * @retval B_OK        Success.
 * @retval B_NO_MEMORY Failed to split a lock range (rare).
 */
static status_t
release_advisory_lock(struct vnode* vnode, struct io_context* context,
	struct file_descriptor* descriptor, struct flock* flock)
{
	FUNCTION(("release_advisory_lock(vnode = %p, flock = %p)\n", vnode, flock));

	struct advisory_locking* locking = get_advisory_locking(vnode);
	if (locking == NULL)
		return B_OK;

	// find matching lock entries

	LockList::Iterator iterator = locking->locks.GetIterator();
	while (iterator.HasNext()) {
		struct advisory_lock* lock = iterator.Next();
		bool removeLock = false;

		if (descriptor != NULL && lock->bound_to == descriptor) {
			// Remove flock() locks
			removeLock = true;
		} else if (lock->bound_to == context
				&& advisory_lock_intersects(lock, flock)) {
			// Remove POSIX locks
			bool endsBeyond = false;
			bool startsBefore = false;
			if (flock != NULL) {
				startsBefore = lock->start < flock->l_start;
				endsBeyond = lock->end > flock->l_start - 1 + flock->l_len;
			}

			if (!startsBefore && !endsBeyond) {
				// lock is completely contained in flock
				removeLock = true;
			} else if (startsBefore && !endsBeyond) {
				// cut the end of the lock
				lock->end = flock->l_start - 1;
			} else if (!startsBefore && endsBeyond) {
				// cut the start of the lock
				lock->start = flock->l_start + flock->l_len;
			} else {
				// divide the lock into two locks
				struct advisory_lock* secondLock = new advisory_lock;
				if (secondLock == NULL) {
					// TODO: we should probably revert the locks we already
					// changed... (ie. allocate upfront)
					put_advisory_locking(locking);
					return B_NO_MEMORY;
				}

				lock->end = flock->l_start - 1;

				secondLock->bound_to = context;
				secondLock->team = lock->team;
				secondLock->session = lock->session;
				// values must already be normalized when getting here
				secondLock->start = flock->l_start + flock->l_len;
				secondLock->end = lock->end;
				secondLock->shared = lock->shared;

				locking->locks.Add(secondLock);
			}
		}

		if (removeLock) {
			// this lock is no longer used
			iterator.Remove();
			delete lock;
		}
	}

	bool removeLocking = locking->locks.IsEmpty();
	release_sem_etc(locking->wait_sem, 1, B_RELEASE_ALL);

	put_advisory_locking(locking);

	if (removeLocking) {
		// We can remove the whole advisory locking structure; it's no
		// longer used
		locking = get_advisory_locking(vnode);
		if (locking != NULL) {
			ReadLocker locker(sVnodeLock);
			AutoLocker<Vnode> nodeLocker(vnode);

			// the locking could have been changed in the mean time
			if (locking->locks.IsEmpty()) {
				vnode->advisory_locking = NULL;
				nodeLocker.Unlock();
				locker.Unlock();

				// we've detached the locking from the vnode, so we can
				// safely delete it
				delete locking;
			} else {
				// the locking is in use again
				nodeLocker.Unlock();
				locker.Unlock();
				release_sem_etc(locking->lock, 1, B_DO_NOT_RESCHEDULE);
			}
		}
	}

	return B_OK;
}


/*!	Acquires an advisory lock for the \a vnode. If \a wait is \c true, it
	will wait for the lock to become available, if there are any collisions
	(it will return B_PERMISSION_DENIED in this case if \a wait is \c false).

	If \a descriptor is NULL, POSIX semantics are used for this lock. Otherwise,
	BSD flock() semantics are used, that is, all children can unlock the file
	in question (we even allow parents to remove the lock, though, but that
	seems to be in line to what the BSD's are doing).
*/
/**
 * @brief Acquires an advisory (POSIX or flock()) lock on a vnode.
 *
 * Checks for conflicting locks and either waits for them to be released or
 * returns B_WOULD_BLOCK / B_PERMISSION_DENIED depending on \a wait.
 *
 * @param vnode      The vnode to lock.
 * @param context    The calling team's io_context (used for POSIX lock binding).
 * @param descriptor If non-NULL, flock() semantics; otherwise POSIX semantics.
 * @param flock      Specifies the lock type (F_RDLCK/F_WRLCK) and byte range.
 * @param wait       If true, block until the lock can be acquired.
 * @retval B_OK              Lock acquired successfully.
 * @retval B_WOULD_BLOCK     Conflicting lock exists and wait is false (flock).
 * @retval B_PERMISSION_DENIED Conflicting lock exists and wait is false (POSIX).
 * @retval B_NO_MEMORY       Allocation failure.
 * @note TODO: deadlock detection is not yet implemented.
 */
static status_t
acquire_advisory_lock(struct vnode* vnode, io_context* context,
	struct file_descriptor* descriptor, struct flock* flock, bool wait)
{
	FUNCTION(("acquire_advisory_lock(vnode = %p, flock = %p, wait = %s)\n",
		vnode, flock, wait ? "yes" : "no"));

	bool shared = flock->l_type == F_RDLCK;
	void* boundTo = descriptor != NULL ? (void*)descriptor : (void*)context;
	status_t status = B_OK;

	// TODO: do deadlock detection!

	struct advisory_locking* locking;

	while (true) {
		// if this vnode has an advisory_locking structure attached,
		// lock that one and search for any colliding file lock
		status = create_advisory_locking(vnode);
		if (status != B_OK)
			return status;

		locking = vnode->advisory_locking;
		team_id team = team_get_current_team_id();
		sem_id waitForLock = -1;

		// test for collisions
		LockList::Iterator iterator = locking->locks.GetIterator();
		while (iterator.HasNext()) {
			struct advisory_lock* lock = iterator.Next();

			// TODO: locks from the same team might be joinable!
			if ((lock->team != team || lock->bound_to != boundTo)
					&& advisory_lock_intersects(lock, flock)) {
				// locks do overlap
				if (!shared || !lock->shared) {
					// we need to wait
					waitForLock = locking->wait_sem;
					break;
				}
			}
		}

		if (waitForLock < 0)
			break;

		// We need to wait. Do that or fail now, if we've been asked not to.

		if (!wait) {
			put_advisory_locking(locking);
			return descriptor != NULL ? B_WOULD_BLOCK : B_PERMISSION_DENIED;
		}

		status = switch_sem_etc(locking->lock, waitForLock, 1,
			B_CAN_INTERRUPT, 0);
		if (status != B_OK && status != B_BAD_SEM_ID)
			return status;

		// We have been notified, but we need to re-lock the locking object. So
		// go another round...
	}

	// install new lock

	struct advisory_lock* lock = new(std::nothrow) advisory_lock;
	if (lock == NULL) {
		put_advisory_locking(locking);
		return B_NO_MEMORY;
	}

	lock->bound_to = boundTo;
	lock->team = team_get_current_team_id();
	lock->session = thread_get_current_thread()->team->session_id;
	// values must already be normalized when getting here
	lock->start = flock->l_start;
	lock->end = flock->l_start - 1 + flock->l_len;
	lock->shared = shared;

	locking->locks.Add(lock);
	put_advisory_locking(locking);

	return status;
}


/*!	Normalizes the \a flock structure to make it easier to compare the
	structure with others. The l_start and l_len fields are set to absolute
	values according to the l_whence field.
*/
/**
 * @brief Normalizes flock l_start/l_len to absolute byte offsets.
 *
 * Converts SEEK_CUR and SEEK_END offsets to SEEK_SET-relative values so that
 * lock range comparisons are straightforward.
 *
 * @param descriptor The open file descriptor (used for current position and stat).
 * @param flock      The flock structure to normalize in place.
 * @retval B_OK         Normalization succeeded.
 * @retval B_UNSUPPORTED The vnode does not support read_stat (needed for SEEK_END).
 * @retval B_BAD_VALUE  Unknown l_whence value.
 */
static status_t
normalize_flock(struct file_descriptor* descriptor, struct flock* flock)
{
	switch (flock->l_whence) {
		case SEEK_SET:
			break;
		case SEEK_CUR:
			flock->l_start += descriptor->pos;
			break;
		case SEEK_END:
		{
			struct vnode* vnode = descriptor->u.vnode;
			struct stat stat;
			status_t status;

			if (!HAS_FS_CALL(vnode, read_stat))
				return B_UNSUPPORTED;

			status = FS_CALL(vnode, read_stat, &stat);
			if (status != B_OK)
				return status;

			flock->l_start += stat.st_size;
			break;
		}
		default:
			return B_BAD_VALUE;
	}

	if (flock->l_start < 0)
		flock->l_start = 0;
	if (flock->l_len == 0)
		flock->l_len = OFF_MAX;

	// don't let the offset and length overflow
	if (flock->l_start > 0 && OFF_MAX - flock->l_start < flock->l_len)
		flock->l_len = OFF_MAX - flock->l_start;

	if (flock->l_len < 0) {
		// a negative length reverses the region
		flock->l_start += flock->l_len;
		flock->l_len = -flock->l_len;
	}

	return B_OK;
}


/**
 * @brief Replaces a vnode pointer with a fallback if it belongs to a being-unmounted mount.
 *
 * Used during unmount to redirect root and CWD references away from the
 * unmounting mount to a safe fallback.
 *
 * @param mount              The mount being unmounted.
 * @param vnodeToDisconnect  Specific vnode to disconnect, or NULL for all vnodes on mount.
 * @param vnode              Reference to the vnode pointer to potentially replace.
 * @param fallBack           Replacement vnode if disconnection occurs.
 * @param lockRootLock       If true, acquires sIOContextRootLock while redirecting.
 */
static void
replace_vnode_if_disconnected(struct fs_mount* mount,
	struct vnode* vnodeToDisconnect, struct vnode*& vnode,
	struct vnode* fallBack, bool lockRootLock)
{
	struct vnode* givenVnode = vnode;
	bool vnodeReplaced = false;

	ReadLocker vnodeReadLocker(sVnodeLock);

	if (lockRootLock)
		rw_lock_write_lock(&sIOContextRootLock);

	while (vnode != NULL && vnode->mount == mount
		&& (vnodeToDisconnect == NULL || vnodeToDisconnect == vnode)) {
		if (vnode->covers != NULL) {
			// redirect the vnode to the covered vnode
			vnode = vnode->covers;
		} else
			vnode = fallBack;

		vnodeReplaced = true;
	}

	// If we've replaced the node, grab a reference for the new one.
	if (vnodeReplaced && vnode != NULL)
		inc_vnode_ref_count(vnode);

	if (lockRootLock)
		rw_lock_write_unlock(&sIOContextRootLock);

	vnodeReadLocker.Unlock();

	if (vnodeReplaced)
		put_vnode(givenVnode);
}


/*!	Disconnects all file descriptors that are associated with the
	\a vnodeToDisconnect, or if this is NULL, all vnodes of the specified
	\a mount object.

	Note, after you've called this function, there might still be ongoing
	accesses - they won't be interrupted if they already happened before.
	However, any subsequent access will fail.

	This is not a cheap function and should be used with care and rarely.
	TODO: there is currently no means to stop a blocking read/write!
*/
/**
 * @brief Disconnects all open file descriptors associated with a mount or vnode.
 *
 * Iterates all teams and closes/marks-disconnected any FD whose vnode belongs to
 * the specified mount (or matches vnodeToDisconnect if non-NULL).  Also
 * redirects root and CWD references.
 *
 * @param mount             The mount whose FDs should be disconnected.
 * @param vnodeToDisconnect Specific vnode to disconnect, or NULL for all on mount.
 * @note This is a slow operation and should be used sparingly (typically during
 *       forced unmount).  Already-in-progress I/O is NOT interrupted.
 */
static void
disconnect_mount_or_vnode_fds(struct fs_mount* mount,
	struct vnode* vnodeToDisconnect)
{
	// iterate over all teams and peek into their file descriptors
	TeamListIterator teamIterator;
	while (Team* team = teamIterator.Next()) {
		BReference<Team> teamReference(team, true);
		TeamLocker teamLocker(team);

		// lock the I/O context
		io_context* context = team->io_context;
		if (context == NULL)
			continue;
		WriteLocker contextLocker(context->lock);

		teamLocker.Unlock();

		replace_vnode_if_disconnected(mount, vnodeToDisconnect, context->root,
			sRoot, true);
		replace_vnode_if_disconnected(mount, vnodeToDisconnect, context->cwd,
			sRoot, false);

		for (uint32 i = 0; i < context->table_size; i++) {
			struct file_descriptor* descriptor = context->fds[i];
			if (descriptor == NULL || (descriptor->open_mode & O_DISCONNECTED) != 0)
				continue;

			inc_fd_ref_count(descriptor);

			// if this descriptor points at this mount, we
			// need to disconnect it to be able to unmount
			struct vnode* vnode = fd_vnode(descriptor);
			if (vnodeToDisconnect != NULL) {
				if (vnode == vnodeToDisconnect)
					disconnect_fd(descriptor);
			} else if ((vnode != NULL && vnode->mount == mount)
				|| (vnode == NULL && descriptor->u.mount == mount))
				disconnect_fd(descriptor);

			put_fd(descriptor);
		}
	}
}


/*!	\brief Gets the root node of the current IO context.
	If \a kernel is \c true, the kernel IO context will be used.
	The caller obtains a reference to the returned node.
*/
/**
 * @brief Returns the root vnode of the current I/O context, incrementing its ref count.
 *
 * If \a kernel is false, the process-specific root (chroot) is returned; if it
 * has not been set, falls back to the global sRoot.
 *
 * @param kernel true to use the kernel I/O context (global root).
 * @return The root vnode with one additional reference held by the caller.
 */
struct vnode*
get_root_vnode(bool kernel)
{
	if (!kernel) {
		// Get current working directory from io context
		struct io_context* context = get_current_io_context(kernel);

		rw_lock_read_lock(&sIOContextRootLock);

		struct vnode* root = context->root;
		if (root != NULL)
			inc_vnode_ref_count(root);

		rw_lock_read_unlock(&sIOContextRootLock);

		if (root != NULL)
			return root;

		// That should never happen.
		dprintf("get_root_vnode(): IO context for team %" B_PRId32 " doesn't "
			"have a root\n", team_get_current_team_id());
	}

	inc_vnode_ref_count(sRoot);
	return sRoot;
}


/*!	\brief Gets the directory path and leaf name for a given path.

	The supplied \a path is transformed to refer to the directory part of
	the entry identified by the original path, and into the buffer \a filename
	the leaf name of the original entry is written.
	Neither the returned path nor the leaf name can be expected to be
	canonical.

	\param path The path to be analyzed. Must be able to store at least one
		   additional character.
	\param filename The buffer into which the leaf name will be written.
		   Must be of size B_FILE_NAME_LENGTH at least.
	\return \c B_OK, if everything went fine, \c B_NAME_TOO_LONG, if the leaf
		   name is longer than \c B_FILE_NAME_LENGTH, or \c B_ENTRY_NOT_FOUND,
		   if the given path name is empty.
*/
/**
 * @brief Splits a path into its directory part and leaf name.
 *
 * Modifies \a path in place so that it refers to the parent directory, and
 * writes the leaf name into \a filename.  Paths ending in "/" have "." as the
 * leaf name.
 *
 * @param path     In/out path buffer (must have room for one extra character).
 * @param filename Output buffer for the leaf name (B_FILE_NAME_LENGTH bytes).
 * @retval B_OK           Success.
 * @retval B_NAME_TOO_LONG The leaf name exceeds B_FILE_NAME_LENGTH.
 * @retval B_ENTRY_NOT_FOUND The path is empty.
 */
static status_t
get_dir_path_and_leaf(char* path, char* filename)
{
	if (*path == '\0')
		return B_ENTRY_NOT_FOUND;

	char* last = strrchr(path, '/');
		// '/' are not allowed in file names!

	FUNCTION(("get_dir_path_and_leaf(path = %s)\n", path));

	if (last == NULL) {
		// this path is single segment with no '/' in it
		// ex. "foo"
		if (strlcpy(filename, path, B_FILE_NAME_LENGTH) >= B_FILE_NAME_LENGTH)
			return B_NAME_TOO_LONG;

		strcpy(path, ".");
	} else {
		last++;
		if (last[0] == '\0') {
			// special case: the path ends in one or more '/' - remove them
			while (*--last == '/' && last != path);
			last[1] = '\0';

			// pathnames that end with one or more trailing slash characters
			// must refer to directory paths
			strcpy(filename, ".");
			return B_OK;
		}

		// normal leaf: replace the leaf portion of the path with a '.'
		if (strlcpy(filename, last, B_FILE_NAME_LENGTH) >= B_FILE_NAME_LENGTH)
			return B_NAME_TOO_LONG;

		last[0] = '.';
		last[1] = '\0';
	}
	return B_OK;
}


/**
 * @brief Resolves an entry_ref (directory node + name) to a vnode.
 *
 * @param mountID     Device ID of the directory.
 * @param directoryID Node ID of the parent directory.
 * @param name        Name of the entry within the directory.
 * @param traverse    If true, traverse the final symlink component.
 * @param kernel      true if the kernel I/O context should be used.
 * @param _vnode      Output: the resolved vnode with a reference held.
 * @retval B_OK           Success.
 * @retval B_NAME_TOO_LONG Name exceeds B_FILE_NAME_LENGTH.
 * @note Delegates to vnode_path_to_vnode() after looking up the directory vnode.
 */
static status_t
entry_ref_to_vnode(dev_t mountID, ino_t directoryID, const char* name,
	bool traverse, bool kernel, VnodePutter& _vnode)
{
	char clonedName[B_FILE_NAME_LENGTH + 1];
	if (strlcpy(clonedName, name, B_FILE_NAME_LENGTH) >= B_FILE_NAME_LENGTH)
		return B_NAME_TOO_LONG;

	// get the directory vnode and let vnode_path_to_vnode() do the rest
	struct vnode* directory;

	status_t status = get_vnode(mountID, directoryID, &directory, true, false);
	if (status < 0)
		return status;

	return vnode_path_to_vnode(directory, clonedName, traverse, kernel,
		_vnode, NULL);
}


/*!	Looks up the entry with name \a name in the directory represented by \a dir
	and returns the respective vnode.
	On success a reference to the vnode is acquired for the caller.
*/
/**
 * @brief Looks up a directory entry and returns the resulting vnode with a reference.
 *
 * Checks the entry cache first; on a miss calls the FS lookup() hook.
 *
 * @param dir    The directory vnode to search.
 * @param name   The entry name to look up.
 * @param _vnode Output: vnode for the found entry.
 * @retval B_OK           Entry found; caller owns one reference to *_vnode.
 * @retval B_ENTRY_NOT_FOUND Entry does not exist.
 * @note The caller must put_vnode() the returned vnode when done.
 */
static status_t
lookup_dir_entry(struct vnode* dir, const char* name, struct vnode** _vnode)
{
	ino_t id;
	bool missing;

	if (dir->mount->entry_cache.Lookup(dir->id, name, id, missing)) {
		return missing ? B_ENTRY_NOT_FOUND
			: get_vnode(dir->device, id, _vnode, true, false);
	}

	status_t status = FS_CALL(dir, lookup, name, &id);
	if (status != B_OK)
		return status;

	// The lookup() hook calls get_vnode() or publish_vnode(), so we do already
	// have a reference and just need to look the node up.
	rw_lock_read_lock(&sVnodeLock);
	*_vnode = lookup_vnode(dir->device, id);
	rw_lock_read_unlock(&sVnodeLock);

	if (*_vnode == NULL) {
		panic("lookup_dir_entry(): could not lookup vnode (mountid 0x%" B_PRIx32
			" vnid 0x%" B_PRIx64 ")\n", dir->device, id);
		return B_ENTRY_NOT_FOUND;
	}

//	ktrace_printf("lookup_dir_entry(): dir: %p (%ld, %lld), name: \"%s\" -> "
//		"%p (%ld, %lld)", dir, dir->mount->id, dir->id, name, *_vnode,
//		(*_vnode)->mount->id, (*_vnode)->id);

	return B_OK;
}


/*!	Returns the vnode for the relative \a path starting at the specified \a vnode.

	\param[in,out] path The relative path being searched. Must not be NULL.
	If the function returns successfully, \a path contains the name of the last path
	component. This function clobbers the buffer pointed to by \a path only
	if it does contain more than one component.

	If the function fails and leafName is not NULL, \a _vnode contains the last directory,
	the caller has the responsibility to call put_vnode() on it.

	Note, this reduces the ref_count of the starting \a vnode, no matter if
	it is successful or not!

	\param[out] _vnode If the function returns B_OK, points to the found node.
	\param[out] _vnode If the function returns something else and leafname is not NULL: set to the
		last existing directory in the path. The caller has responsibility to release it using
		put_vnode().
	\param[out] _vnode If the function returns something else and leafname is NULL: not used.
*/
/**
 * @brief Walks a relative path from a starting vnode to the target vnode.
 *
 * Consumes one reference to \a start unconditionally.  Resolves "..", symlinks
 * (up to B_MAX_SYMLINKS deep), and mount-point crossings.  On success sets
 * \a _vnode to the resolved vnode.  On failure with \a leafName non-NULL,
 * \a _vnode is set to the last successfully resolved directory.
 *
 * @param start           Starting vnode (reference consumed).
 * @param path            Null-terminated relative path (modified in place).
 * @param traverseLeafLink If true, follow the final symlink component.
 * @param count           Current symlink recursion depth (pass 0 externally).
 * @param ioContext       I/O context used for root/prison checks.
 * @param _vnode          Output: target vnode on success, or last directory on failure.
 * @param _parentID       Optional output: node ID of the parent directory.
 * @param leafName        Optional output: leaf name when returning an intermediate dir.
 * @retval B_OK           Path resolved; caller owns reference to _vnode.
 * @retval B_ENTRY_NOT_FOUND Component not found.
 * @retval B_NOT_A_DIRECTORY A non-directory component is used as a directory.
 * @retval B_LINK_LIMIT   Symlink depth exceeded.
 */
static status_t
vnode_path_to_vnode(struct vnode* start, char* path, bool traverseLeafLink,
	int count, struct io_context* ioContext, VnodePutter& _vnode,
	ino_t* _parentID, char* leafName)
{
	FUNCTION(("vnode_path_to_vnode(vnode = %p, path = %s)\n", start, path));
	ASSERT(!_vnode.IsSet());

	VnodePutter vnode(start);

	if (path == NULL)
		return B_BAD_VALUE;
	if (*path == '\0')
		return B_ENTRY_NOT_FOUND;

	status_t status = B_OK;
	ino_t lastParentID = vnode->id;
	while (true) {
		TRACE(("vnode_path_to_vnode: top of loop. p = %p, p = '%s'\n", path,
			path));

		// done?
		if (path[0] == '\0')
			break;

		// walk to find the next path component ("path" will point to a single
		// path component), and filter out multiple slashes
		char* nextPath = path + 1;
		while (*nextPath != '\0' && *nextPath != '/')
			nextPath++;

		bool directoryFound = false;
		if (*nextPath == '/') {
			directoryFound = true;
			*nextPath = '\0';
			do {
				nextPath++;
			} while (*nextPath == '/');
		}

		// See if the '..' is at a covering vnode move to the covered
		// vnode so we pass the '..' path to the underlying filesystem.
		// Also prevent breaking the root of the IO context.
		if (strcmp("..", path) == 0) {
			if (vnode.Get() == ioContext->root) {
				// Attempted prison break! Keep it contained.
				path = nextPath;
				continue;
			}

			if (Vnode* coveredVnode = get_covered_vnode(vnode.Get()))
				vnode.SetTo(coveredVnode);
		}

		// check if vnode is really a directory
		if (status == B_OK && !S_ISDIR(vnode->Type()))
			status = B_NOT_A_DIRECTORY;

		// Check if we have the right to search the current directory vnode.
		// If a file system doesn't have the access() function, we assume that
		// searching a directory is always allowed
		if (status == B_OK && HAS_FS_CALL(vnode, access))
			status = FS_CALL(vnode.Get(), access, X_OK);

		// Tell the filesystem to get the vnode of this path component (if we
		// got the permission from the call above)
		VnodePutter nextVnode;
		if (status == B_OK) {
			struct vnode* temp = NULL;
			status = lookup_dir_entry(vnode.Get(), path, &temp);
			nextVnode.SetTo(temp);
		}

		if (status != B_OK) {
			if (leafName != NULL && !directoryFound) {
				strlcpy(leafName, path, B_FILE_NAME_LENGTH);
				_vnode.SetTo(vnode.Detach());
			}
			return status;
		}

		// If the new node is a symbolic link, resolve it (if we've been told to)
		if (S_ISLNK(nextVnode->Type()) && (traverseLeafLink || directoryFound)) {
			TRACE(("traverse link\n"));

			if ((count + 1) > B_MAX_SYMLINKS)
				return B_LINK_LIMIT;

			size_t bufferSize = B_PATH_NAME_LENGTH;
			char* buffer = (char*)object_cache_alloc(sPathNameCache, 0);
			if (buffer == NULL)
				return B_NO_MEMORY;

			if (HAS_FS_CALL(nextVnode, read_symlink)) {
				bufferSize--;
				status = FS_CALL(nextVnode.Get(), read_symlink, buffer, &bufferSize);
				// null-terminate
				if (status >= 0 && bufferSize < B_PATH_NAME_LENGTH)
					buffer[bufferSize] = '\0';
			} else
				status = B_BAD_VALUE;

			if (status != B_OK) {
				object_cache_free(sPathNameCache, buffer, 0);
				return status;
			}
			nextVnode.Unset();

			// Check if we start from the root directory or the current
			// directory ("vnode" still points to that one).
			// Cut off all leading slashes if it's the root directory
			path = buffer;
			bool absoluteSymlink = false;
			if (path[0] == '/') {
				// we don't need the old directory anymore
				vnode.Unset();

				while (*++path == '/')
					;

				rw_lock_read_lock(&sIOContextRootLock);
				vnode.SetTo(ioContext->root);
				inc_vnode_ref_count(vnode.Get());
				rw_lock_read_unlock(&sIOContextRootLock);

				absoluteSymlink = true;
			}

			inc_vnode_ref_count(vnode.Get());
				// balance the next recursion - we will decrement the
				// ref_count of the vnode, no matter if we succeeded or not

			if (absoluteSymlink && *path == '\0') {
				// symlink was just "/"
				nextVnode.SetTo(vnode.Get());
			} else {
				status = vnode_path_to_vnode(vnode.Get(), path, true, count + 1,
					ioContext, nextVnode, &lastParentID, leafName);
			}

			object_cache_free(sPathNameCache, buffer, 0);

			if (status != B_OK) {
				if (leafName != NULL)
					_vnode.SetTo(nextVnode.Detach());
				return status;
			}
		} else {
			lastParentID = vnode->id;
		}

		// if next path component is empty, check next vnode is actually a directory
		if (directoryFound && *nextPath == '\0' && !S_ISDIR(nextVnode->Type()))
			return B_NOT_A_DIRECTORY;

		// decrease the ref count on the old dir we just looked up into
		vnode.Unset();

		path = nextPath;
		vnode.SetTo(nextVnode.Detach());

		// see if we hit a covered node
		if (Vnode* coveringNode = get_covering_vnode(vnode.Get()))
			vnode.SetTo(coveringNode);
	}

	_vnode.SetTo(vnode.Detach());
	if (_parentID != NULL)
		*_parentID = lastParentID;

	return B_OK;
}


/**
 * @brief Convenience overload of vnode_path_to_vnode() using the current I/O context.
 *
 * @param vnode           Starting vnode (reference consumed).
 * @param path            Relative path to resolve (modified in place).
 * @param traverseLeafLink If true, follow the final symlink.
 * @param kernel          true to use the kernel I/O context.
 * @param _vnode          Output: resolved vnode.
 * @param _parentID       Optional output: parent node ID.
 * @param leafName        Optional output: leaf name on partial resolution.
 * @retval B_OK  Success.
 */
static status_t
vnode_path_to_vnode(struct vnode* vnode, char* path, bool traverseLeafLink,
	bool kernel, VnodePutter& _vnode, ino_t* _parentID, char* leafName)
{
	return vnode_path_to_vnode(vnode, path, traverseLeafLink, 0,
		get_current_io_context(kernel), _vnode, _parentID, leafName);
}


/**
 * @brief Resolves an absolute or CWD-relative path to a vnode.
 *
 * Determines the starting vnode (global root for absolute paths, CWD for
 * relative) and delegates to vnode_path_to_vnode().
 *
 * @param path         The path to resolve (modified in place by vnode_path_to_vnode).
 * @param traverseLink If true, follow the final symlink component.
 * @param _vnode       Output: resolved vnode.
 * @param _parentID    Optional output: parent node ID.
 * @param kernel       true to use the kernel I/O context and root.
 * @retval B_OK           Success.
 * @retval B_BAD_VALUE    path is NULL.
 * @retval B_ENTRY_NOT_FOUND path is empty.
 * @retval B_ERROR        No root (called too early) or no CWD.
 */
static status_t
path_to_vnode(char* path, bool traverseLink, VnodePutter& _vnode,
	ino_t* _parentID, bool kernel)
{
	struct vnode* start = NULL;

	FUNCTION(("path_to_vnode(path = \"%s\")\n", path));

	if (!path)
		return B_BAD_VALUE;

	if (*path == '\0')
		return B_ENTRY_NOT_FOUND;

	// figure out if we need to start at root or at cwd
	if (*path == '/') {
		if (sRoot == NULL) {
			// we're a bit early, aren't we?
			return B_ERROR;
		}

		while (*++path == '/')
			;
		start = get_root_vnode(kernel);

		if (*path == '\0') {
			_vnode.SetTo(start);
			return B_OK;
		}
	} else {
		const struct io_context* context = get_current_io_context(kernel);

		rw_lock_read_lock(&context->lock);
		start = context->cwd;
		if (start != NULL)
			inc_vnode_ref_count(start);
		rw_lock_read_unlock(&context->lock);

		if (start == NULL)
			return B_ERROR;
	}

	return vnode_path_to_vnode(start, path, traverseLink, kernel, _vnode,
		_parentID);
}


/*! Returns the vnode in the next to last segment of the path, and returns
	the last portion in filename.
	The path buffer must be able to store at least one additional character.
*/
/**
 * @brief Resolves a path to the parent directory vnode and leaf name.
 *
 * @param path     Path to split and resolve (modified in place).
 * @param _vnode   Output: parent directory vnode.
 * @param filename Output buffer for the leaf name (B_FILE_NAME_LENGTH).
 * @param kernel   true to use the kernel I/O context.
 * @retval B_OK    Success.
 */
static status_t
path_to_dir_vnode(char* path, VnodePutter& _vnode, char* filename,
	bool kernel)
{
	status_t status = get_dir_path_and_leaf(path, filename);
	if (status != B_OK)
		return status;

	return path_to_vnode(path, true, _vnode, NULL, kernel);
}


/*!	\brief Retrieves the directory vnode and the leaf name of an entry referred
		   to by a FD + path pair.

	\a path must be given in either case. \a fd might be omitted, in which
	case \a path is either an absolute path or one relative to the current
	directory. If both a supplied and \a path is relative it is reckoned off
	of the directory referred to by \a fd. If \a path is absolute \a fd is
	ignored.

	The caller has the responsibility to call put_vnode() on the returned
	directory vnode.

	\param fd The FD. May be < 0.
	\param path The absolute or relative path. Must not be \c NULL. The buffer
	       is modified by this function. It must have at least room for a
	       string one character longer than the path it contains.
	\param _vnode A pointer to a variable the directory vnode shall be written
		   into.
	\param filename A buffer of size B_FILE_NAME_LENGTH or larger into which
		   the leaf name of the specified entry will be written.
	\param kernel \c true, if invoked from inside the kernel, \c false if
		   invoked from userland.
	\return \c B_OK, if everything went fine, another error code otherwise.
*/
/**
 * @brief Resolves an fd+path pair to the parent directory vnode and leaf name.
 *
 * @param fd       File descriptor of the base directory (AT_FDCWD or -1 for CWD).
 * @param path     Absolute or relative path (modified in place).
 * @param _vnode   Output: parent directory vnode (caller must put_vnode()).
 * @param filename Output buffer for the leaf name.
 * @param kernel   true to use the kernel I/O context.
 * @retval B_OK          Success.
 * @retval B_BAD_VALUE   path is NULL.
 * @retval B_ENTRY_NOT_FOUND path is empty.
 */
static status_t
fd_and_path_to_dir_vnode(int fd, char* path, VnodePutter& _vnode,
	char* filename, bool kernel)
{
	if (!path)
		return B_BAD_VALUE;
	if (*path == '\0')
		return B_ENTRY_NOT_FOUND;
	if (fd == AT_FDCWD || fd == -1 || *path == '/')
		return path_to_dir_vnode(path, _vnode, filename, kernel);

	status_t status = get_dir_path_and_leaf(path, filename);
	if (status != B_OK)
		return status;

	return fd_and_path_to_vnode(fd, path, true, _vnode, NULL, kernel);
}


/*!	\brief Retrieves the directory vnode and the leaf name of an entry referred
		   to by a vnode + path pair.

	\a path must be given in either case. \a vnode might be omitted, in which
	case \a path is either an absolute path or one relative to the current
	directory. If both a supplied and \a path is relative it is reckoned off
	of the directory referred to by \a vnode. If \a path is absolute \a vnode is
	ignored.

	The caller has the responsibility to call put_vnode() on the returned
	directory vnode.

	Note, this reduces the ref_count of the starting \a vnode, no matter if
	it is successful or not.

	\param vnode The vnode. May be \c NULL.
	\param path The absolute or relative path. Must not be \c NULL. The buffer
	       is modified by this function. It must have at least room for a
	       string one character longer than the path it contains.
	\param _vnode A pointer to a variable the directory vnode shall be written
		   into.
	\param filename A buffer of size B_FILE_NAME_LENGTH or larger into which
		   the leaf name of the specified entry will be written.
	\param kernel \c true, if invoked from inside the kernel, \c false if
		   invoked from userland.
	\return \c B_OK, if everything went fine, another error code otherwise.
*/
/**
 * @brief Resolves a vnode+path pair to the parent directory vnode and leaf name.
 *
 * Consumes one reference to \a vnode.  If vnode is NULL or path is absolute,
 * delegates to path_to_dir_vnode().
 *
 * @param vnode    Base directory vnode (reference consumed, may be NULL).
 * @param path     Relative or absolute path (modified in place).
 * @param _vnode   Output: parent directory vnode.
 * @param filename Output buffer for the leaf name.
 * @param kernel   true to use the kernel I/O context.
 * @retval B_OK    Success.
 */
static status_t
vnode_and_path_to_dir_vnode(struct vnode* vnode, char* path,
	VnodePutter& _vnode, char* filename, bool kernel)
{
	VnodePutter vnodePutter(vnode);

	if (!path)
		return B_BAD_VALUE;
	if (*path == '\0')
		return B_ENTRY_NOT_FOUND;
	if (vnode == NULL || path[0] == '/')
		return path_to_dir_vnode(path, _vnode, filename, kernel);

	status_t status = get_dir_path_and_leaf(path, filename);
	if (status != B_OK)
		return status;

	vnodePutter.Detach();
	return vnode_path_to_vnode(vnode, path, true, kernel, _vnode, NULL);
}


/*! Returns a vnode's name in the d_name field of a supplied dirent buffer.
*/
/**
 * @brief Retrieves the name of a vnode within its parent directory.
 *
 * Uses the FS get_vnode_name() hook if available, otherwise scans the parent
 * directory for a matching inode number.
 *
 * @param vnode      The vnode whose name is to be found.
 * @param parent     The parent directory vnode (used for fallback search).
 * @param buffer     dirent buffer where d_name is written.
 * @param bufferSize Total size of \a buffer.
 * @param ioContext  The current I/O context.
 * @retval B_OK         Name found and written to buffer->d_name.
 * @retval B_BAD_VALUE  Buffer too small.
 * @retval B_UNSUPPORTED Neither hook is available and parent is NULL.
 * @retval B_ENTRY_NOT_FOUND Name not found by directory scan.
 */
static status_t
get_vnode_name(struct vnode* vnode, struct vnode* parent, struct dirent* buffer,
	size_t bufferSize, struct io_context* ioContext)
{
	if (bufferSize < sizeof(struct dirent))
		return B_BAD_VALUE;

	// See if the vnode is covering another vnode and move to the covered
	// vnode so we get the underlying file system
	VnodePutter vnodePutter;
	if (Vnode* coveredVnode = get_covered_vnode(vnode)) {
		vnode = coveredVnode;
		vnodePutter.SetTo(vnode);
	}

	if (HAS_FS_CALL(vnode, get_vnode_name)) {
		// The FS supports getting the name of a vnode.
		if (FS_CALL(vnode, get_vnode_name, buffer->d_name,
			(char*)buffer + bufferSize - buffer->d_name) == B_OK)
			return B_OK;
	}

	// The FS doesn't support getting the name of a vnode. So we search the
	// parent directory for the vnode, if the caller let us.

	if (parent == NULL || !HAS_FS_CALL(parent, read_dir))
		return B_UNSUPPORTED;

	void* cookie;

	status_t status = FS_CALL(parent, open_dir, &cookie);
	if (status >= B_OK) {
		while (true) {
			uint32 num = 1;
			// We use the FS hook directly instead of dir_read(), since we don't
			// want the entries to be fixed. We have already resolved vnode to
			// the covered node.
			status = FS_CALL(parent, read_dir, cookie, buffer, bufferSize,
				&num);
			if (status != B_OK)
				break;
			if (num == 0) {
				status = B_ENTRY_NOT_FOUND;
				break;
			}

			if (vnode->id == buffer->d_ino) {
				// found correct entry!
				break;
			}
		}

		FS_CALL(parent, close_dir, cookie);
		FS_CALL(parent, free_dir_cookie, cookie);
	}
	return status;
}


/**
 * @brief Convenience overload of get_vnode_name() returning the name in a char buffer.
 *
 * @param vnode    The vnode whose name is to be retrieved.
 * @param parent   The parent directory vnode.
 * @param name     Output name buffer.
 * @param nameSize Size of \a name in bytes.
 * @param kernel   true to use the kernel I/O context.
 * @retval B_OK            Name written to \a name.
 * @retval B_BUFFER_OVERFLOW Name does not fit in \a name.
 */
static status_t
get_vnode_name(struct vnode* vnode, struct vnode* parent, char* name,
	size_t nameSize, bool kernel)
{
	char buffer[offsetof(struct dirent, d_name) + B_FILE_NAME_LENGTH + 1];
	struct dirent* dirent = (struct dirent*)buffer;

	status_t status = get_vnode_name(vnode, parent, dirent, sizeof(buffer),
		get_current_io_context(kernel));
	if (status != B_OK)
		return status;

	if (strlcpy(name, dirent->d_name, nameSize) >= nameSize)
		return B_BUFFER_OVERFLOW;

	return B_OK;
}


/*!	Gets the full path to a given directory vnode.
	It uses the fs_get_vnode_name() call to get the name of a vnode; if a
	file system doesn't support this call, it will fall back to iterating
	through the parent directory to get the name of the child.

	To protect against circular loops, it supports a maximum tree depth
	of 256 levels.

	Note that the path may not be correct the time this function returns!
	It doesn't use any locking to prevent returning the correct path, as
	paths aren't safe anyway: the path to a file can change at any time.

	It might be a good idea, though, to check if the returned path exists
	in the calling function (it's not done here because of efficiency)
*/
/**
 * @brief Constructs the full absolute path of a directory vnode.
 *
 * Walks up the directory tree (up to 256 levels) using ".." lookups, building
 * the path right-to-left into \a buffer.
 *
 * @param vnode      A directory vnode to resolve.
 * @param buffer     Output buffer for the path string.
 * @param bufferSize Size of \a buffer in bytes.
 * @param kernel     true to use the kernel I/O context.
 * @retval B_OK                    Path written to \a buffer.
 * @retval B_BAD_VALUE             vnode, buffer, or bufferSize invalid.
 * @retval B_NOT_A_DIRECTORY       vnode is not a directory.
 * @retval B_RESULT_NOT_REPRESENTABLE Path is too long for the buffer.
 * @retval B_LINK_LIMIT            Tree depth exceeds maximum (cycle detection).
 * @note The returned path may be stale; no locking prevents concurrent renames.
 */
static status_t
dir_vnode_to_path(struct vnode* vnode, char* buffer, size_t bufferSize,
	bool kernel)
{
	FUNCTION(("dir_vnode_to_path(%p, %p, %lu)\n", vnode, buffer, bufferSize));

	if (vnode == NULL || buffer == NULL || bufferSize == 0)
		return B_BAD_VALUE;

	if (!S_ISDIR(vnode->Type()))
		return B_NOT_A_DIRECTORY;

	char* path = buffer;
	int32 insert = bufferSize;
	int32 maxLevel = 256;
	int32 length;
	status_t status = B_OK;
	struct io_context* ioContext = get_current_io_context(kernel);

	// we don't use get_vnode() here because this call is more
	// efficient and does all we need from get_vnode()
	inc_vnode_ref_count(vnode);

	path[--insert] = '\0';
		// the path is filled right to left

	while (true) {
		// If the node is the context's root, bail out. Otherwise resolve mount
		// points.
		if (vnode == ioContext->root)
			break;

		if (Vnode* coveredVnode = get_covered_vnode(vnode)) {
			put_vnode(vnode);
			vnode = coveredVnode;
		}

		// lookup the parent vnode
		struct vnode* parentVnode;
		status = lookup_dir_entry(vnode, "..", &parentVnode);
		if (status != B_OK)
			goto out;

		if (parentVnode == vnode) {
			// The caller apparently got their hands on a node outside of their
			// context's root. Now we've hit the global root.
			put_vnode(parentVnode);
			break;
		}

		// get the node's name
		char nameBuffer[offsetof(struct dirent, d_name) + B_FILE_NAME_LENGTH + 1];
			// also used for fs_read_dir()
		char* name = &((struct dirent*)nameBuffer)->d_name[0];
		status = get_vnode_name(vnode, parentVnode, (struct dirent*)nameBuffer,
			sizeof(nameBuffer), ioContext);

		// release the current vnode, we only need its parent from now on
		put_vnode(vnode);
		vnode = parentVnode;

		if (status != B_OK)
			goto out;

		// TODO: add an explicit check for loops in about 10 levels to do
		// real loop detection

		// don't go deeper as 'maxLevel' to prevent circular loops
		if (maxLevel-- < 0) {
			status = B_LINK_LIMIT;
			goto out;
		}

		// add the name in front of the current path
		name[B_FILE_NAME_LENGTH - 1] = '\0';
		length = strlen(name);
		insert -= length;
		if (insert <= 0) {
			status = B_RESULT_NOT_REPRESENTABLE;
			goto out;
		}
		memcpy(path + insert, name, length);
		path[--insert] = '/';
	}

	// the root dir will result in an empty path: fix it
	if (path[insert] == '\0')
		path[--insert] = '/';

	TRACE(("  path is: %s\n", path + insert));

	// move the path to the start of the buffer
	length = bufferSize - insert;
	memmove(buffer, path + insert, length);

out:
	put_vnode(vnode);
	return status;
}


/*!	Checks the length of every path component, and adds a '.'
	if the path ends in a slash.
	The given path buffer must be able to store at least one
	additional character.
*/
/**
 * @brief Validates each path component length and appends "." for trailing slashes.
 *
 * @param to Path string to validate and optionally extend (must have room for
 *           one extra character).
 * @retval B_OK           Path is valid.
 * @retval B_NAME_TOO_LONG A component or the completed path is too long.
 * @retval B_ENTRY_NOT_FOUND Path is empty.
 */
static status_t
check_path(char* to)
{
	int32 length = 0;

	// check length of every path component

	while (*to) {
		char* begin;
		if (*to == '/')
			to++, length++;

		begin = to;
		while (*to != '/' && *to)
			to++, length++;

		if (to - begin > B_FILE_NAME_LENGTH)
			return B_NAME_TOO_LONG;
	}

	if (length == 0)
		return B_ENTRY_NOT_FOUND;

	// complete path if there is a slash at the end

	if (*(to - 1) == '/') {
		if (length > B_PATH_NAME_LENGTH - 2)
			return B_NAME_TOO_LONG;

		to[0] = '.';
		to[1] = '\0';
	}

	return B_OK;
}


/**
 * @brief Retrieves a file descriptor and its associated vnode.
 *
 * @param fd      The file descriptor number.
 * @param _vnode  Output: the vnode associated with the descriptor.
 * @param kernel  true to look up in the kernel I/O context.
 * @return The file_descriptor with its ref count incremented, or NULL if the FD
 *         is invalid or does not have an associated vnode.
 * @note Caller must call put_fd() when done.
 */
static struct file_descriptor*
get_fd_and_vnode(int fd, struct vnode** _vnode, bool kernel)
{
	struct file_descriptor* descriptor
		= get_fd(get_current_io_context(kernel), fd);
	if (descriptor == NULL)
		return NULL;

	struct vnode* vnode = fd_vnode(descriptor);
	if (vnode == NULL) {
		put_fd(descriptor);
		return NULL;
	}

	// ToDo: when we can close a file descriptor at any point, investigate
	//	if this is still valid to do (accessing the vnode without ref_count
	//	or locking)
	*_vnode = vnode;
	return descriptor;
}


/**
 * @brief Returns the vnode associated with a file descriptor, with a reference.
 *
 * @param fd     The file descriptor number.
 * @param kernel true to look up in the kernel I/O context.
 * @return The vnode with an incremented reference count, or NULL if the FD is
 *         invalid or has no vnode.
 * @note Caller must call put_vnode() when done.
 */
static struct vnode*
get_vnode_from_fd(int fd, bool kernel)
{
	struct file_descriptor* descriptor;
	struct vnode* vnode;

	descriptor = get_fd(get_current_io_context(kernel), fd);
	if (descriptor == NULL)
		return NULL;

	vnode = fd_vnode(descriptor);
	if (vnode != NULL)
		inc_vnode_ref_count(vnode);

	put_fd(descriptor);
	return vnode;
}


/*!	Gets the vnode from an FD + path combination. If \a fd is lower than zero,
	only the path will be considered. In this case, the \a path must not be
	NULL.
	If \a fd is a valid file descriptor, \a path may be NULL for directories,
	and should be NULL for files.
*/
/**
 * @brief Resolves an fd+path pair to a vnode.
 *
 * If path is absolute, fd is ignored.  If fd is AT_FDCWD/-1, the CWD is used
 * as the base.  Otherwise the vnode of fd is used as the starting point.
 *
 * @param fd               Base file descriptor (AT_FDCWD, -1, or a valid FD).
 * @param path             Absolute or relative path (may be NULL for FD-only).
 * @param traverseLeafLink If true, follow the final symlink component.
 * @param _vnode           Output: resolved vnode (caller must put_vnode()).
 * @param _parentID        Optional output: parent node ID.
 * @param kernel           true to use the kernel I/O context.
 * @retval B_OK           Resolved; caller owns a reference to _vnode.
 * @retval B_BAD_VALUE    Both fd < 0 and path is NULL.
 * @retval B_FILE_ERROR   fd is invalid.
 */
static status_t
fd_and_path_to_vnode(int fd, char* path, bool traverseLeafLink,
	VnodePutter& _vnode, ino_t* _parentID, bool kernel)
{
	if (fd < 0 && !path)
		return B_BAD_VALUE;

	if (path != NULL && *path == '\0')
		return B_ENTRY_NOT_FOUND;

	if ((fd == AT_FDCWD || fd == -1) || (path != NULL && path[0] == '/')) {
		// no FD or absolute path
		return path_to_vnode(path, traverseLeafLink, _vnode, _parentID, kernel);
	}

	// FD only, or FD + relative path
	struct vnode* vnode = get_vnode_from_fd(fd, kernel);
	if (vnode == NULL)
		return B_FILE_ERROR;

	if (path != NULL) {
		return vnode_path_to_vnode(vnode, path, traverseLeafLink, kernel,
			_vnode, _parentID);
	}

	// there is no relative path to take into account

	_vnode.SetTo(vnode);
	if (_parentID)
		*_parentID = -1;

	return B_OK;
}


struct vnode*
fd_vnode(struct file_descriptor* descriptor)
{
	if (descriptor->ops == &sFileOps
			|| descriptor->ops == &sDirectoryOps
			|| descriptor->ops == &sAttributeOps
			|| descriptor->ops == &sAttributeDirectoryOps)
		return descriptor->u.vnode;

	return NULL;
}


bool
fd_is_file(struct file_descriptor* descriptor)
{
	return descriptor->ops == &sFileOps;
}


/**
 * @brief Allocates a new file descriptor and associates it with a vnode or mount.
 *
 * @param ops      The fd_ops table to assign to the descriptor.
 * @param mount    Mount to associate (used when vnode is NULL, e.g. index dirs).
 * @param vnode    Vnode to associate (may be NULL for mount-only FDs).
 * @param cookie   FS-private open cookie.
 * @param openMode Flags from the open() call (O_RDONLY, O_WRONLY, etc.).
 * @param kernel   true to allocate in the kernel I/O context.
 * @return The new file descriptor number (>= 0) on success, or a negative
 *         error code (B_BUSY, B_NO_MEMORY, B_NO_MORE_FDS).
 * @note Sets O_CLOEXEC and O_CLOFORK on the descriptor when requested.
 */
static int
get_new_fd(struct fd_ops* ops, struct fs_mount* mount, struct vnode* vnode,
	void* cookie, int openMode, bool kernel)
{
	struct file_descriptor* descriptor;
	int fd;

	// If the vnode is locked, we don't allow creating a new file/directory
	// file_descriptor for it
	if (vnode != NULL && vnode->mandatory_locked_by != NULL
			&& (ops == &sFileOps || ops == &sDirectoryOps))
		return B_BUSY;

	descriptor = alloc_fd();
	if (!descriptor)
		return B_NO_MEMORY;

	if (vnode)
		descriptor->u.vnode = vnode;
	else
		descriptor->u.mount = mount;
	descriptor->cookie = cookie;

	descriptor->ops = ops;
	descriptor->open_mode = openMode;

	if (descriptor->ops->fd_seek != NULL) {
		// some kinds of files are not seekable
		switch (vnode->Type() & S_IFMT) {
			case S_IFIFO:
			case S_IFSOCK:
				ASSERT(descriptor->pos == -1);
				break;

			// The Open Group Base Specs don't mention any file types besides pipes,
			// FIFOs, and sockets specially, so we allow seeking all others.
			default:
				descriptor->pos = 0;
				break;
		}
	}

	io_context* context = get_current_io_context(kernel);
	fd = new_fd(context, descriptor);
	if (fd < 0) {
		descriptor->ops = NULL;
		put_fd(descriptor);
		return B_NO_MORE_FDS;
	}

	rw_lock_write_lock(&context->lock);
	fd_set_close_on_exec(context, fd, (openMode & O_CLOEXEC) != 0);
	fd_set_close_on_fork(context, fd, (openMode & O_CLOFORK) != 0);
	rw_lock_write_unlock(&context->lock);

	return fd;
}


/*!	In-place normalizes \a path. It's otherwise semantically equivalent to
	vfs_normalize_path(). See there for more documentation.
*/
/**
 * @brief Normalizes a path in place, resolving "." ".." and symlinks.
 *
 * @param path         Path buffer to normalize in place.
 * @param pathSize     Capacity of \a path in bytes.
 * @param traverseLink If true, also resolve the final symlink component.
 * @param kernel       true to use the kernel I/O context.
 * @retval B_OK           Path normalized successfully.
 * @retval B_NAME_TOO_LONG Normalized path does not fit in \a path.
 * @retval B_LINK_LIMIT   Symlink recursion limit reached.
 */
static status_t
normalize_path(char* path, size_t pathSize, bool traverseLink, bool kernel)
{
	VnodePutter dir;
	status_t error;

	for (int i = 0; i < B_MAX_SYMLINKS; i++) {
		// get dir vnode + leaf name
		char leaf[B_FILE_NAME_LENGTH];
		error = vnode_and_path_to_dir_vnode(dir.Detach(), path, dir, leaf, kernel);
		if (error != B_OK)
			return error;
		strcpy(path, leaf);

		// get file vnode, if we shall resolve links
		bool fileExists = false;
		VnodePutter fileVnode;
		if (traverseLink) {
			inc_vnode_ref_count(dir.Get());
			if (vnode_path_to_vnode(dir.Get(), path, false, kernel, fileVnode,
					NULL) == B_OK) {
				fileExists = true;
			}
		}

		if (!fileExists || !traverseLink || !S_ISLNK(fileVnode->Type())) {
			// we're done -- construct the path
			bool hasLeaf = true;
			if (strcmp(leaf, ".") == 0 || strcmp(leaf, "..") == 0) {
				// special cases "." and ".." -- get the dir, forget the leaf
				error = vnode_path_to_vnode(dir.Detach(), leaf, false, kernel,
					dir, NULL);
				if (error != B_OK)
					return error;
				hasLeaf = false;
			}

			// get the directory path
			error = dir_vnode_to_path(dir.Get(), path, B_PATH_NAME_LENGTH, kernel);
			if (error != B_OK)
				return error;

			// append the leaf name
			if (hasLeaf) {
				// insert a directory separator if this is not the file system
				// root
				if ((strcmp(path, "/") != 0
					&& strlcat(path, "/", pathSize) >= pathSize)
					|| strlcat(path, leaf, pathSize) >= pathSize) {
					return B_NAME_TOO_LONG;
				}
			}

			return B_OK;
		}

		// read link
		if (HAS_FS_CALL(fileVnode, read_symlink)) {
			size_t bufferSize = B_PATH_NAME_LENGTH - 1;
			error = FS_CALL(fileVnode.Get(), read_symlink, path, &bufferSize);
			if (error != B_OK)
				return error;
			if (bufferSize < B_PATH_NAME_LENGTH)
				path[bufferSize] = '\0';
		} else
			return B_BAD_VALUE;
	}

	return B_LINK_LIMIT;
}


/**
 * @brief Resolves a directory's ".." entry, honouring mount-point crossings.
 *
 * If \a parent is the I/O context root, returns its own device/node IDs.
 * Otherwise traverses ".." through the covering vnode chain.
 *
 * @param parent    The directory whose parent should be resolved.
 * @param _device   Output: device ID of the parent.
 * @param _node     Output: node ID of the parent.
 * @param ioContext The current I/O context (prison root check).
 * @retval B_OK    Parent resolved successfully.
 */
static status_t
resolve_covered_parent(struct vnode* parent, dev_t* _device, ino_t* _node,
	struct io_context* ioContext)
{
	// Make sure the IO context root is not bypassed.
	if (parent == ioContext->root) {
		*_device = parent->device;
		*_node = parent->id;
		return B_OK;
	}

	inc_vnode_ref_count(parent);
		// vnode_path_to_vnode() puts the node

	// ".." is guaranteed not to be clobbered by this call
	VnodePutter vnode;
	status_t status = vnode_path_to_vnode(parent, (char*)"..", false,
		ioContext, vnode, NULL);
	if (status == B_OK) {
		*_device = vnode->device;
		*_node = vnode->id;
	}

	return status;
}


#ifdef ADD_DEBUGGER_COMMANDS


/**
 * @brief Kernel debugger helper: prints advisory_locking details.
 *
 * @param locking Pointer to the advisory_locking to dump, or NULL (no-op).
 */
static void
_dump_advisory_locking(advisory_locking* locking)
{
	if (locking == NULL)
		return;

	kprintf("   lock:        %" B_PRId32, locking->lock);
	kprintf("   wait_sem:    %" B_PRId32, locking->wait_sem);

	int32 index = 0;
	LockList::Iterator iterator = locking->locks.GetIterator();
	while (iterator.HasNext()) {
		struct advisory_lock* lock = iterator.Next();

		kprintf("   [%2" B_PRId32 "] team:   %" B_PRId32 "\n", index++, lock->team);
		kprintf("        start:  %" B_PRIdOFF "\n", lock->start);
		kprintf("        end:    %" B_PRIdOFF "\n", lock->end);
		kprintf("        shared? %s\n", lock->shared ? "yes" : "no");
	}
}


/**
 * @brief Kernel debugger helper: prints all fields of an fs_mount structure.
 *
 * @param mount The mount to dump.
 */
static void
_dump_mount(struct fs_mount* mount)
{
	kprintf("MOUNT: %p\n", mount);
	kprintf(" id:            %" B_PRIdDEV "\n", mount->id);
	kprintf(" device_name:   %s\n", mount->device_name);
	kprintf(" root_vnode:    %p\n", mount->root_vnode);
	kprintf(" covers:        %p\n", mount->root_vnode->covers);
	kprintf(" partition:     %p\n", mount->partition);
	kprintf(" lock:          %p\n", &mount->lock);
	kprintf(" flags:        %s%s\n", mount->unmounting ? " unmounting" : "",
		mount->owns_file_device ? " owns_file_device" : "");

	fs_volume* volume = mount->volume;
	while (volume != NULL) {
		kprintf(" volume %p:\n", volume);
		kprintf("  layer:            %" B_PRId32 "\n", volume->layer);
		kprintf("  private_volume:   %p\n", volume->private_volume);
		kprintf("  ops:              %p\n", volume->ops);
		kprintf("  file_system:      %p\n", volume->file_system);
		kprintf("  file_system_name: %s\n", volume->file_system_name);
		volume = volume->super_volume;
	}

	set_debug_variable("_volume", (addr_t)mount->volume->private_volume);
	set_debug_variable("_root", (addr_t)mount->root_vnode);
	set_debug_variable("_covers", (addr_t)mount->root_vnode->covers);
	set_debug_variable("_partition", (addr_t)mount->partition);
}


/**
 * @brief Kernel debugger helper: prepends a name component to a path buffer.
 *
 * @param buffer     The path buffer (filled right-to-left).
 * @param bufferSize In/out: current write offset into buffer.
 * @param name       Component name to prepend.
 * @return true if the name fit, false if the buffer was too small.
 */
static bool
debug_prepend_vnode_name_to_path(char* buffer, size_t& bufferSize,
	const char* name)
{
	bool insertSlash = buffer[bufferSize] != '\0';
	size_t nameLength = strlen(name);

	if (bufferSize < nameLength + (insertSlash ? 1 : 0))
		return false;

	if (insertSlash)
		buffer[--bufferSize] = '/';

	bufferSize -= nameLength;
	memcpy(buffer + bufferSize, name, nameLength);

	return true;
}


/**
 * @brief Kernel debugger helper: prepends a "<dev,ino>" token to a path buffer.
 *
 * Used when the real name of a vnode cannot be resolved in the debugger.
 *
 * @param buffer     The path buffer (filled right-to-left).
 * @param bufferSize In/out: current write offset.
 * @param devID      Device ID to encode.
 * @param nodeID     Inode number to encode.
 * @return true if the token fit, false if the buffer was too small.
 */
static bool
debug_prepend_vnode_id_to_path(char* buffer, size_t& bufferSize, dev_t devID,
	ino_t nodeID)
{
	if (bufferSize == 0)
		return false;

	bool insertSlash = buffer[bufferSize] != '\0';
	if (insertSlash)
		buffer[--bufferSize] = '/';

	size_t size = snprintf(buffer, bufferSize,
		"<%" B_PRIdDEV ",%" B_PRIdINO ">", devID, nodeID);
	if (size > bufferSize) {
		if (insertSlash)
			bufferSize++;
		return false;
	}

	if (size < bufferSize)
		memmove(buffer + bufferSize - size, buffer, size);

	bufferSize -= size;
	return true;
}


/**
 * @brief Kernel debugger helper: reconstructs the path of a vnode using the entry cache.
 *
 * Safe to call from the kernel debugger (no locking, uses debug-safe memory).
 *
 * @param vnode       The vnode whose path should be reconstructed.
 * @param buffer      Work/output buffer.
 * @param bufferSize  Size of \a buffer.
 * @param _truncated  Set to true if the path did not fit and was truncated.
 * @return Pointer into \a buffer where the (possibly truncated) path begins.
 */
static char*
debug_resolve_vnode_path(struct vnode* vnode, char* buffer, size_t bufferSize,
	bool& _truncated)
{
	// null-terminate the path
	buffer[--bufferSize] = '\0';

	while (true) {
		while (vnode->covers != NULL)
			vnode = vnode->covers;

		if (vnode == sRoot) {
			_truncated = bufferSize == 0;
			if (!_truncated)
				buffer[--bufferSize] = '/';
			return buffer + bufferSize;
		}

		// resolve the name
		ino_t dirID;
		const char* name = vnode->mount->entry_cache.DebugReverseLookup(
			vnode->id, dirID);
		if (name == NULL) {
			// Failed to resolve the name -- prepend "<dev,node>/".
			_truncated = !debug_prepend_vnode_id_to_path(buffer, bufferSize,
				vnode->mount->id, vnode->id);
			return buffer + bufferSize;
		}

		// prepend the name
		if (!debug_prepend_vnode_name_to_path(buffer, bufferSize, name)) {
			_truncated = true;
			return buffer + bufferSize;
		}

		// resolve the directory node
		struct vnode* nextVnode = lookup_vnode(vnode->mount->id, dirID);
		if (nextVnode == NULL) {
			_truncated = !debug_prepend_vnode_id_to_path(buffer, bufferSize,
				vnode->mount->id, dirID);
			return buffer + bufferSize;
		}

		vnode = nextVnode;
	}
}


/**
 * @brief Kernel debugger helper: prints all fields of a vnode structure.
 *
 * @param vnode     The vnode to dump.
 * @param printPath If true, attempt to reconstruct and print the vnode's path.
 */
static void
_dump_vnode(struct vnode* vnode, bool printPath)
{
	kprintf("VNODE: %p\n", vnode);
	kprintf(" device:        %" B_PRIdDEV "\n", vnode->device);
	kprintf(" id:            %" B_PRIdINO "\n", vnode->id);
	kprintf(" ref_count:     %" B_PRId32 "\n", vnode->ref_count);
	kprintf(" private_node:  %p\n", vnode->private_node);
	kprintf(" mount:         %p\n", vnode->mount);
	kprintf(" covered_by:    %p\n", vnode->covered_by);
	kprintf(" covers:        %p\n", vnode->covers);
	kprintf(" cache:         %p\n", vnode->cache);
	kprintf(" type:          %#" B_PRIx32 "\n", vnode->Type());
	kprintf(" flags:         %s%s%s\n", vnode->IsRemoved() ? "r" : "-",
		vnode->IsBusy() ? "b" : "-", vnode->IsUnpublished() ? "u" : "-");
	kprintf(" advisory_lock: %p\n", vnode->advisory_locking);

	_dump_advisory_locking(vnode->advisory_locking);

	if (printPath) {
		void* buffer = debug_malloc(B_PATH_NAME_LENGTH);
		if (buffer != NULL) {
			bool truncated;
			char* path = debug_resolve_vnode_path(vnode, (char*)buffer,
				B_PATH_NAME_LENGTH, truncated);
			if (path != NULL) {
				kprintf(" path:          ");
				if (truncated)
					kputs("<truncated>/");
				kputs(path);
				kputs("\n");
			} else
				kprintf("Failed to resolve vnode path.\n");

			debug_free(buffer);
		} else
			kprintf("Failed to allocate memory for constructing the path.\n");
	}

	set_debug_variable("_node", (addr_t)vnode->private_node);
	set_debug_variable("_mount", (addr_t)vnode->mount);
	set_debug_variable("_covered_by", (addr_t)vnode->covered_by);
	set_debug_variable("_covers", (addr_t)vnode->covers);
	set_debug_variable("_adv_lock", (addr_t)vnode->advisory_locking);
}


/**
 * @brief Kernel debugger command: print info about a specific fs_mount.
 *
 * @param argc Argument count.
 * @param argv Argument vector: argv[1] is the mount ID or address.
 * @return 0 always (debugger convention).
 */
static int
dump_mount(int argc, char** argv)
{
	if (argc != 2 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s [id|address]\n", argv[0]);
		return 0;
	}

	ulong val = parse_expression(argv[1]);
	uint32 id = val;

	struct fs_mount* mount = sMountsTable->Lookup(id);
	if (mount == NULL) {
		if (IS_USER_ADDRESS(id)) {
			kprintf("fs_mount not found\n");
			return 0;
		}
		mount = (fs_mount*)val;
	}

	_dump_mount(mount);
	return 0;
}


/**
 * @brief Kernel debugger command: list all mounted file systems.
 *
 * @param argc Argument count (must be 1).
 * @param argv Argument vector.
 * @return 0 always.
 */
static int
dump_mounts(int argc, char** argv)
{
	if (argc != 1) {
		kprintf("usage: %s\n", argv[0]);
		return 0;
	}

	kprintf("%-*s    id %-*s   %-*s   %-*s   fs_name\n",
		B_PRINTF_POINTER_WIDTH, "address", B_PRINTF_POINTER_WIDTH, "root",
		B_PRINTF_POINTER_WIDTH, "covers", B_PRINTF_POINTER_WIDTH, "cookie");

	struct fs_mount* mount;

	MountTable::Iterator iterator(sMountsTable);
	while (iterator.HasNext()) {
		mount = iterator.Next();
		kprintf("%p%4" B_PRIdDEV " %p %p %p %s\n", mount, mount->id, mount->root_vnode,
			mount->root_vnode->covers, mount->volume->private_volume,
			mount->volume->file_system_name);

		fs_volume* volume = mount->volume;
		while (volume->super_volume != NULL) {
			volume = volume->super_volume;
			kprintf("                                     %p %s\n",
				volume->private_volume, volume->file_system_name);
		}
	}

	return 0;
}


/**
 * @brief Kernel debugger command: print info about a specific vnode.
 *
 * Accepts either a vnode pointer or a (devID, nodeID) pair.  With "-p" also
 * reconstructs and prints the path.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 always.
 */
static int
dump_vnode(int argc, char** argv)
{
	bool printPath = false;
	int argi = 1;
	if (argc >= 2 && strcmp(argv[argi], "-p") == 0) {
		printPath = true;
		argi++;
	}

	if (argi >= argc || argi + 2 < argc || strcmp(argv[argi], "--help") == 0) {
		print_debugger_command_usage(argv[0]);
		return 0;
	}

	struct vnode* vnode = NULL;

	if (argi + 1 == argc) {
		vnode = (struct vnode*)parse_expression(argv[argi]);
		if (IS_USER_ADDRESS(vnode)) {
			kprintf("invalid vnode address\n");
			return 0;
		}
		_dump_vnode(vnode, printPath);
		return 0;
	}

	dev_t device = parse_expression(argv[argi]);
	ino_t id = parse_expression(argv[argi + 1]);

	VnodeTable::Iterator iterator(sVnodeTable);
	while (iterator.HasNext()) {
		vnode = iterator.Next();
		if (vnode->id != id || vnode->device != device)
			continue;

		_dump_vnode(vnode, printPath);
	}

	return 0;
}


/**
 * @brief Kernel debugger command: list all vnodes on a given device.
 *
 * @param argc Argument count.
 * @param argv argv[1] is the device ID to filter by.
 * @return 0 always.
 */
static int
dump_vnodes(int argc, char** argv)
{
	if (argc != 2 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s [device]\n", argv[0]);
		return 0;
	}

	// restrict dumped nodes to a certain device if requested
	dev_t device = parse_expression(argv[1]);

	struct vnode* vnode;

	kprintf("%-*s   dev     inode  ref %-*s   %-*s   %-*s   flags\n",
		B_PRINTF_POINTER_WIDTH, "address", B_PRINTF_POINTER_WIDTH, "cache",
		B_PRINTF_POINTER_WIDTH, "fs-node", B_PRINTF_POINTER_WIDTH, "locking");

	VnodeTable::Iterator iterator(sVnodeTable);
	while (iterator.HasNext()) {
		vnode = iterator.Next();
		if (vnode->device != device)
			continue;

		kprintf("%p%4" B_PRIdDEV "%10" B_PRIdINO "%5" B_PRId32 " %p %p %p %s%s%s\n",
			vnode, vnode->device, vnode->id, vnode->ref_count, vnode->cache,
			vnode->private_node, vnode->advisory_locking,
			vnode->IsRemoved() ? "r" : "-", vnode->IsBusy() ? "b" : "-",
			vnode->IsUnpublished() ? "u" : "-");
	}

	return 0;
}


/**
 * @brief Kernel debugger command: list vnodes that have a VMCache attached.
 *
 * @param argc Argument count.
 * @param argv Optional argv[1] to filter by device ID.
 * @return 0 always.
 */
static int
dump_vnode_caches(int argc, char** argv)
{
	struct vnode* vnode;

	if (argc > 2 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s [device]\n", argv[0]);
		return 0;
	}

	// restrict dumped nodes to a certain device if requested
	dev_t device = -1;
	if (argc > 1)
		device = parse_expression(argv[1]);

	kprintf("%-*s   dev     inode %-*s       size   pages\n",
		B_PRINTF_POINTER_WIDTH, "address", B_PRINTF_POINTER_WIDTH, "cache");

	VnodeTable::Iterator iterator(sVnodeTable);
	while (iterator.HasNext()) {
		vnode = iterator.Next();
		if (vnode->cache == NULL)
			continue;
		if (device != -1 && vnode->device != device)
			continue;

		kprintf("%p%4" B_PRIdDEV "%10" B_PRIdINO " %p %8" B_PRIdOFF "%8" B_PRId32 "\n",
			vnode, vnode->device, vnode->id, vnode->cache,
			(vnode->cache->virtual_end + B_PAGE_SIZE - 1) / B_PAGE_SIZE,
			vnode->cache->page_count);
	}

	return 0;
}


/**
 * @brief Kernel debugger command: dump the I/O context of a team or address.
 *
 * @param argc Argument count.
 * @param argv Optional argv[1]: team ID or io_context address.
 * @return 0 always.
 */
int
dump_io_context(int argc, char** argv)
{
	if (argc > 2 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s [team-id|address]\n", argv[0]);
		return 0;
	}

	struct io_context* context = NULL;

	if (argc > 1) {
		ulong num = parse_expression(argv[1]);
		if (IS_KERNEL_ADDRESS(num))
			context = (struct io_context*)num;
		else {
			Team* team = team_get_team_struct_locked(num);
			if (team == NULL) {
				kprintf("could not find team with ID %lu\n", num);
				return 0;
			}
			context = (struct io_context*)team->io_context;
		}
	} else
		context = get_current_io_context(true);

	kprintf("I/O CONTEXT: %p\n", context);
	kprintf(" root vnode:\t%p\n", context->root);
	kprintf(" cwd vnode:\t%p\n", context->cwd);
	kprintf(" used fds:\t%" B_PRIu32 "\n", context->num_used_fds);
	kprintf(" max fds:\t%" B_PRIu32 "\n", context->table_size);

	if (context->num_used_fds) {
		kprintf("   no.    %*s  ref  open  mode         pos    %*s\n",
			B_PRINTF_POINTER_WIDTH, "ops", B_PRINTF_POINTER_WIDTH, "cookie");
	}

	for (uint32 i = 0; i < context->table_size; i++) {
		struct file_descriptor* fd = context->fds[i];
		if (fd == NULL)
			continue;

		kprintf("  %3" B_PRIu32 ":  %p  %3" B_PRId32 "  %4"
			B_PRIu32 "  %4" B_PRIx32 "  %10" B_PRIdOFF "  %p  %s %p\n", i,
			fd->ops, fd->ref_count, fd->open_count, fd->open_mode,
			fd->pos, fd->cookie,
			(fd_vnode(fd) != NULL) ? "vnode" : "mount",
			fd->u.vnode);
	}

	kprintf(" used monitors:\t%" B_PRIu32 "\n", context->num_monitors);
	kprintf(" max monitors:\t%" B_PRIu32 "\n", context->max_monitors);

	set_debug_variable("_cwd", (addr_t)context->cwd);

	return 0;
}


/**
 * @brief Kernel debugger command: print vnode cache usage statistics.
 *
 * @param argc Argument count (must be 1).
 * @param argv Argument vector.
 * @return 0 always.
 */
int
dump_vnode_usage(int argc, char** argv)
{
	if (argc != 1) {
		kprintf("usage: %s\n", argv[0]);
		return 0;
	}

	kprintf("Unused vnodes: %" B_PRIu32 " (max unused %" B_PRIu32 ")\n",
		sUnusedVnodes, kMaxUnusedVnodes);

	uint32 count = sVnodeTable->CountElements();

	kprintf("%" B_PRIu32 " vnodes total (%" B_PRIu32 " in use).\n", count,
		count - sUnusedVnodes);
	return 0;
}

#endif	// ADD_DEBUGGER_COMMANDS


/*!	Clears memory specified by an iovec array.
*/
/**
 * @brief Zeroes up to \a bytes bytes across an iovec array.
 *
 * @param vecs     Array of iovec descriptors.
 * @param vecCount Number of entries in \a vecs.
 * @param bytes    Maximum number of bytes to zero.
 */
static void
zero_iovecs(const iovec* vecs, size_t vecCount, size_t bytes)
{
	for (size_t i = 0; i < vecCount && bytes > 0; i++) {
		size_t length = std::min(vecs[i].iov_len, bytes);
		memset(vecs[i].iov_base, 0, length);
		bytes -= length;
	}
}


/*!	Does the dirty work of combining the file_io_vecs with the iovecs
	and calls the file system hooks to read/write the request to disk.
*/
/**
 * @brief Performs scatter/gather page I/O combining file_io_vecs and memory iovecs.
 *
 * Drives the FS read_pages / write_pages hooks by matching file extents to
 * memory buffers, handling sparse regions (offset -1) and partial completions.
 *
 * @param vnode        The vnode on which to perform I/O.
 * @param cookie       The open file cookie.
 * @param fileVecs     Array of file extents (offset, length pairs).
 * @param fileVecCount Number of entries in \a fileVecs.
 * @param vecs         Memory iovec array.
 * @param vecCount     Number of entries in \a vecs.
 * @param _vecIndex    In/out: current iovec index.
 * @param _vecOffset   In/out: current byte offset within the current iovec.
 * @param _numBytes    In: bytes requested; out: bytes actually transferred.
 * @param doWrite      true for write, false for read.
 * @retval B_OK     I/O completed (check *_numBytes for actual transfer size).
 * @retval B_BAD_VALUE fileVecCount is zero (out-of-bounds access).
 */
static status_t
common_file_io_vec_pages(struct vnode* vnode, void* cookie,
	const file_io_vec* fileVecs, size_t fileVecCount, const iovec* vecs,
	size_t vecCount, uint32* _vecIndex, size_t* _vecOffset, size_t* _numBytes,
	bool doWrite)
{
	if (fileVecCount == 0) {
		// There are no file vecs at this offset, so we're obviously trying
		// to access the file outside of its bounds
		return B_BAD_VALUE;
	}

	size_t numBytes = *_numBytes;
	uint32 fileVecIndex;
	size_t vecOffset = *_vecOffset;
	uint32 vecIndex = *_vecIndex;
	status_t status;
	size_t size;

	if (!doWrite && vecOffset == 0) {
		// now directly read the data from the device
		// the first file_io_vec can be read directly
		// TODO: we could also write directly

		if (fileVecs[0].length < (off_t)numBytes)
			size = fileVecs[0].length;
		else
			size = numBytes;

		if (fileVecs[0].offset >= 0) {
			status = FS_CALL(vnode, read_pages, cookie, fileVecs[0].offset,
				&vecs[vecIndex], vecCount - vecIndex, &size);
		} else {
			// sparse read
			zero_iovecs(&vecs[vecIndex], vecCount - vecIndex, size);
			status = B_OK;
		}
		if (status != B_OK)
			return status;

		ASSERT((off_t)size <= fileVecs[0].length);

		// If the file portion was contiguous, we're already done now
		if (size == numBytes)
			return B_OK;

		// if we reached the end of the file, we can return as well
		if ((off_t)size != fileVecs[0].length) {
			*_numBytes = size;
			return B_OK;
		}

		fileVecIndex = 1;

		// first, find out where we have to continue in our iovecs
		for (; vecIndex < vecCount; vecIndex++) {
			if (size < vecs[vecIndex].iov_len)
				break;

			size -= vecs[vecIndex].iov_len;
		}

		vecOffset = size;
	} else {
		fileVecIndex = 0;
		size = 0;
	}

	// Too bad, let's process the rest of the file_io_vecs

	size_t totalSize = size;
	size_t bytesLeft = numBytes - size;

	for (; fileVecIndex < fileVecCount; fileVecIndex++) {
		const file_io_vec &fileVec = fileVecs[fileVecIndex];
		off_t fileOffset = fileVec.offset;
		off_t fileLeft = min_c(fileVec.length, (off_t)bytesLeft);

		TRACE(("FILE VEC [%" B_PRIu32 "] length %" B_PRIdOFF "\n", fileVecIndex,
			fileLeft));

		// process the complete fileVec
		while (fileLeft > 0) {
			iovec tempVecs[MAX_TEMP_IO_VECS];
			uint32 tempCount = 0;

			// size tracks how much of what is left of the current fileVec
			// (fileLeft) has been assigned to tempVecs
			size = 0;

			// assign what is left of the current fileVec to the tempVecs
			for (size = 0; (off_t)size < fileLeft && vecIndex < vecCount
					&& tempCount < MAX_TEMP_IO_VECS;) {
				// try to satisfy one iovec per iteration (or as much as
				// possible)

				// bytes left of the current iovec
				size_t vecLeft = vecs[vecIndex].iov_len - vecOffset;
				if (vecLeft == 0) {
					vecOffset = 0;
					vecIndex++;
					continue;
				}

				TRACE(("fill vec %" B_PRIu32 ", offset = %lu, size = %lu\n",
					vecIndex, vecOffset, size));

				// actually available bytes
				size_t tempVecSize = min_c(vecLeft, fileLeft - size);

				tempVecs[tempCount].iov_base
					= (void*)((addr_t)vecs[vecIndex].iov_base + vecOffset);
				tempVecs[tempCount].iov_len = tempVecSize;
				tempCount++;

				size += tempVecSize;
				vecOffset += tempVecSize;
			}

			size_t bytes = size;

			if (fileOffset == -1) {
				if (doWrite) {
					panic("sparse write attempt: vnode %p", vnode);
					status = B_IO_ERROR;
				} else {
					// sparse read
					zero_iovecs(tempVecs, tempCount, bytes);
					status = B_OK;
				}
			} else if (doWrite) {
				status = FS_CALL(vnode, write_pages, cookie, fileOffset,
					tempVecs, tempCount, &bytes);
			} else {
				status = FS_CALL(vnode, read_pages, cookie, fileOffset,
					tempVecs, tempCount, &bytes);
			}
			if (status != B_OK)
				return status;

			totalSize += bytes;
			bytesLeft -= size;
			if (fileOffset >= 0)
				fileOffset += size;
			fileLeft -= size;
			//dprintf("-> file left = %Lu\n", fileLeft);

			if (size != bytes || vecIndex >= vecCount) {
				// there are no more bytes or iovecs, let's bail out
				*_numBytes = totalSize;
				return B_OK;
			}
		}
	}

	*_vecIndex = vecIndex;
	*_vecOffset = vecOffset;
	*_numBytes = totalSize;
	return B_OK;
}


/**
 * @brief Releases all resources held by an io_context and frees it.
 *
 * Closes all open file descriptors, releases root and CWD vnode references,
 * removes all node monitors, and frees the context's memory.
 *
 * @param context The io_context to free.
 * @retval B_OK Always.
 */
static status_t
free_io_context(io_context* context)
{
	TIOC(FreeIOContext(context));

	if (context->root)
		put_vnode(context->root);

	if (context->cwd)
		put_vnode(context->cwd);

	rw_lock_write_lock(&context->lock);

	for (uint32 i = 0; i < context->table_size; i++) {
		if (struct file_descriptor* descriptor = context->fds[i]) {
			close_fd(context, descriptor);
			put_fd(descriptor);
		}
	}

	rw_lock_destroy(&context->lock);

	remove_node_monitors(context);

	free(context->fds_close_on_exec);
	free(context->fds_close_on_fork);
	free(context->select_infos);
	free(context->fds);
	free(context);

	return B_OK;
}


/**
 * @brief Resizes the node-monitor quota of an io_context.
 *
 * @param context The io_context to modify.
 * @param newSize The new maximum number of node monitors (1..MAX_NODE_MONITORS).
 * @retval B_OK        Quota updated.
 * @retval B_BAD_VALUE newSize is out of the allowed range.
 * @retval B_BUSY      newSize is smaller than the number of currently active monitors.
 */
static status_t
resize_monitor_table(struct io_context* context, const int newSize)
{
	if (newSize <= 0 || newSize > MAX_NODE_MONITORS)
		return B_BAD_VALUE;

	WriteLocker locker(context->lock);

	if ((size_t)newSize < context->num_monitors)
		return B_BUSY;

	context->max_monitors = newSize;
	return B_OK;
}


//	#pragma mark - public API for file systems


/**
 * @brief Creates a new, unpublished vnode for use by a file system.
 *
 * Allocates a vnode, marks it busy and unpublished, and associates it with the
 * given FS-private data and operation vector.  The vnode must subsequently be
 * published by calling publish_vnode().
 *
 * @param volume      The volume on which to create the vnode.
 * @param vnodeID     The file system node ID for the new vnode.
 * @param privateNode FS-private data pointer (must not be NULL).
 * @param ops         The vnode operation vector.
 * @retval B_OK        Vnode created and marked busy/unpublished.
 * @retval B_BAD_VALUE privateNode is NULL.
 * @retval B_NO_MEMORY Allocation failure.
 * @retval B_BUSY      An existing vnode with the same ID is permanently busy.
 * @note Called from FS get_vnode() hooks; must not be called for already-known nodes.
 */
extern "C" status_t
new_vnode(fs_volume* volume, ino_t vnodeID, void* privateNode,
	fs_vnode_ops* ops)
{
	FUNCTION(("new_vnode(volume = %p (%" B_PRId32 "), vnodeID = %" B_PRId64
		", node = %p)\n", volume, volume->id, vnodeID, privateNode));

	if (privateNode == NULL)
		return B_BAD_VALUE;

	int32 tries = BUSY_VNODE_RETRIES;
restart:
	// create the node
	bool nodeCreated;
	struct vnode* vnode;
	status_t status = create_new_vnode_and_lock(volume->id, vnodeID, vnode,
		nodeCreated);
	if (status != B_OK)
		return status;

	WriteLocker nodeLocker(sVnodeLock, true);
		// create_new_vnode_and_lock() has locked for us

	if (!nodeCreated && vnode->IsBusy()) {
		nodeLocker.Unlock();
		if (!retry_busy_vnode(tries, volume->id, vnodeID))
			return B_BUSY;
		goto restart;
	}

	// file system integrity check:
	// test if the vnode already exists and bail out if this is the case!
	if (!nodeCreated) {
		panic("vnode %" B_PRIdDEV ":%" B_PRIdINO " already exists (node = %p, "
			"vnode->node = %p)!", volume->id, vnodeID, privateNode,
			vnode->private_node);
		return B_ERROR;
	}

	vnode->private_node = privateNode;
	vnode->ops = ops;
	vnode->SetUnpublished(true);

	TRACE(("returns: %s\n", strerror(status)));

	return status;
}


/**
 * @brief Publishes a vnode, making it visible to the rest of the kernel.
 *
 * If the vnode was previously created with new_vnode(), this call clears its
 * busy and unpublished flags.  It also creates sub-vnodes for stacked file
 * systems and special node types (FIFOs etc.) as required.
 *
 * @param volume      The volume owning the vnode.
 * @param vnodeID     The node ID.
 * @param privateNode FS-private data (may be NULL if the vnode already exists).
 * @param ops         The vnode operation vector.
 * @param type        POSIX mode type bits (S_IFDIR, S_IFREG, etc.).
 * @param flags       B_VNODE_PUBLISH_REMOVED, B_VNODE_DONT_CREATE_SPECIAL_SUB_NODE.
 * @retval B_OK        Vnode published.
 * @retval B_BAD_VALUE The vnode exists in an unexpected state.
 * @retval B_BUSY      A conflicting busy vnode exists and retries were exhausted.
 * @note Called from FS lookup() and create() hooks.
 */
extern "C" status_t
publish_vnode(fs_volume* volume, ino_t vnodeID, void* privateNode,
	fs_vnode_ops* ops, int type, uint32 flags)
{
	FUNCTION(("publish_vnode()\n"));

	int32 tries = BUSY_VNODE_RETRIES;
restart:
	WriteLocker locker(sVnodeLock);

	struct vnode* vnode = lookup_vnode(volume->id, vnodeID);

	bool nodeCreated = false;
	if (vnode == NULL) {
		if (privateNode == NULL)
			return B_BAD_VALUE;

		// create the node
		locker.Unlock();
			// create_new_vnode_and_lock() will re-lock for us on success
		status_t status = create_new_vnode_and_lock(volume->id, vnodeID, vnode,
			nodeCreated);
		if (status != B_OK)
			return status;

		locker.SetTo(sVnodeLock, true);
	}

	if (nodeCreated) {
		vnode->private_node = privateNode;
		vnode->ops = ops;
		vnode->SetUnpublished(true);
	} else if (vnode->IsBusy() && vnode->IsUnpublished()
		&& vnode->private_node == privateNode && vnode->ops == ops) {
		// already known, but not published
	} else if (vnode->IsBusy()) {
		locker.Unlock();
		if (!retry_busy_vnode(tries, volume->id, vnodeID))
			return B_BUSY;
		goto restart;
	} else
		return B_BAD_VALUE;

	bool publishSpecialSubNode = false;

	vnode->SetType(type);
	vnode->SetRemoved((flags & B_VNODE_PUBLISH_REMOVED) != 0);
	publishSpecialSubNode = is_special_node_type(type)
		&& (flags & B_VNODE_DONT_CREATE_SPECIAL_SUB_NODE) == 0;

	status_t status = B_OK;

	// create sub vnodes, if necessary
	if (volume->sub_volume != NULL || publishSpecialSubNode) {
		locker.Unlock();

		fs_volume* subVolume = volume;
		if (volume->sub_volume != NULL) {
			while (status == B_OK && subVolume->sub_volume != NULL) {
				subVolume = subVolume->sub_volume;
				status = subVolume->ops->create_sub_vnode(subVolume, vnodeID,
					vnode);
			}
		}

		if (status == B_OK && publishSpecialSubNode)
			status = create_special_sub_node(vnode, flags);

		if (status != B_OK) {
			// error -- clean up the created sub vnodes
			while (subVolume->super_volume != volume) {
				subVolume = subVolume->super_volume;
				subVolume->ops->delete_sub_vnode(subVolume, vnode);
			}
		}

		if (status == B_OK) {
			ReadLocker vnodesReadLocker(sVnodeLock);
			AutoLocker<Vnode> nodeLocker(vnode);
			vnode->SetBusy(false);
			vnode->SetUnpublished(false);
		} else {
			locker.Lock();
			sVnodeTable->Remove(vnode);
			remove_vnode_from_mount_list(vnode, vnode->mount);
			object_cache_free(sVnodeCache, vnode, 0);
		}
	} else {
		// we still hold the write lock -- mark the node unbusy and published
		vnode->SetBusy(false);
		vnode->SetUnpublished(false);
	}

	TRACE(("returns: %s\n", strerror(status)));

	return status;
}


/**
 * @brief Public FS API: retrieves a vnode and returns its private node data.
 *
 * Increments the vnode's reference count.  For layered file systems, resolves
 * the private node for the correct layer using get_super_vnode().
 *
 * @param volume       The volume owning the vnode.
 * @param vnodeID      The node ID to retrieve.
 * @param _privateNode Optional output: FS-private data for the requested layer.
 * @retval B_OK        Vnode found; caller must call put_vnode() when done.
 * @retval B_BAD_VALUE volume is NULL.
 */
extern "C" status_t
get_vnode(fs_volume* volume, ino_t vnodeID, void** _privateNode)
{
	struct vnode* vnode;

	if (volume == NULL)
		return B_BAD_VALUE;

	status_t status = get_vnode(volume->id, vnodeID, &vnode, true, true);
	if (status != B_OK)
		return status;

	// If this is a layered FS, we need to get the node cookie for the requested
	// layer.
	if (HAS_FS_CALL(vnode, get_super_vnode)) {
		fs_vnode resolvedNode;
		status_t status = FS_CALL(vnode, get_super_vnode, volume,
			&resolvedNode);
		if (status != B_OK) {
			panic("get_vnode(): Failed to get super node for vnode %p, "
				"volume: %p", vnode, volume);
			put_vnode(vnode);
			return status;
		}

		if (_privateNode != NULL)
			*_privateNode = resolvedNode.private_node;
	} else if (_privateNode != NULL)
		*_privateNode = vnode->private_node;

	return B_OK;
}


/**
 * @brief Public FS API: adds an extra reference to an already-referenced vnode.
 *
 * The caller must already hold a reference; this function is used to pass
 * references between layers without calling the full get_vnode() path.
 *
 * @param volume  The volume owning the vnode.
 * @param vnodeID The node ID.
 * @retval B_OK        Reference added.
 * @retval B_BAD_VALUE Vnode not found in the hash table.
 * @note Panics in debug builds if the vnode was unused (ref_count was 0).
 */
extern "C" status_t
acquire_vnode(fs_volume* volume, ino_t vnodeID)
{
	ReadLocker nodeLocker(sVnodeLock);

	struct vnode* vnode = lookup_vnode(volume->id, vnodeID);
	if (vnode == NULL) {
		KDEBUG_ONLY(panic("acquire_vnode(%p, %" B_PRIdINO "): not found!", volume, vnodeID));
		return B_BAD_VALUE;
	}

	if (inc_vnode_ref_count(vnode) == 0) {
		// It isn't valid to acquire another reference to a vnode that
		// you don't already have a reference to, so this should never happen.
		panic("acquire_vnode(%p, %" B_PRIdINO "): node wasn't used!", volume, vnodeID);
	}

	return B_OK;
}


/**
 * @brief Public FS API: releases a reference to a vnode identified by volume+ID.
 *
 * @param volume  The volume owning the vnode.
 * @param vnodeID The node ID.
 * @retval B_OK        Reference released.
 * @retval B_BAD_VALUE Vnode not found.
 * @note Calls dec_vnode_ref_count() with reenter=true.
 */
extern "C" status_t
put_vnode(fs_volume* volume, ino_t vnodeID)
{
	struct vnode* vnode;

	rw_lock_read_lock(&sVnodeLock);
	vnode = lookup_vnode(volume->id, vnodeID);
	rw_lock_read_unlock(&sVnodeLock);

	if (vnode == NULL) {
		KDEBUG_ONLY(panic("put_vnode(%p, %" B_PRIdINO "): not found!", volume, vnodeID));
		return B_BAD_VALUE;
	}

	dec_vnode_ref_count(vnode, false, true);
	return B_OK;
}


/**
 * @brief Public FS API: marks a vnode as removed (to be deleted when last ref drops).
 *
 * If the vnode is still unpublished it is freed immediately.
 *
 * @param volume  The volume owning the vnode.
 * @param vnodeID The node ID to mark for removal.
 * @retval B_OK        Vnode marked removed.
 * @retval B_ENTRY_NOT_FOUND Vnode not found.
 * @retval B_BUSY      Vnode is a mount point and cannot be removed.
 */
extern "C" status_t
remove_vnode(fs_volume* volume, ino_t vnodeID)
{
	ReadLocker locker(sVnodeLock);

	struct vnode* vnode = lookup_vnode(volume->id, vnodeID);
	if (vnode == NULL)
		return B_ENTRY_NOT_FOUND;

	if (vnode->covered_by != NULL || vnode->covers != NULL) {
		// this vnode is in use
		return B_BUSY;
	}

	vnode->Lock();

	vnode->SetRemoved(true);
	bool removeUnpublished = false;

	if (vnode->IsUnpublished()) {
		// prepare the vnode for deletion
		removeUnpublished = true;
		vnode->SetBusy(true);
	}

	vnode->Unlock();
	locker.Unlock();

	if (removeUnpublished) {
		// If the vnode hasn't been published yet, we delete it here
		atomic_add(&vnode->ref_count, -1);
		free_vnode(vnode, true);
	}

	return B_OK;
}


/**
 * @brief Public FS API: cancels a previous remove_vnode() call on a vnode.
 *
 * @param volume  The volume owning the vnode.
 * @param vnodeID The node ID to un-mark.
 * @retval B_OK Always (silently ignores missing vnodes).
 */
extern "C" status_t
unremove_vnode(fs_volume* volume, ino_t vnodeID)
{
	struct vnode* vnode;

	rw_lock_read_lock(&sVnodeLock);

	vnode = lookup_vnode(volume->id, vnodeID);
	if (vnode) {
		AutoLocker<Vnode> nodeLocker(vnode);
		vnode->SetRemoved(false);
	}

	rw_lock_read_unlock(&sVnodeLock);
	return B_OK;
}


/**
 * @brief Public FS API: queries whether a vnode is marked for removal.
 *
 * @param volume   The volume owning the vnode.
 * @param vnodeID  The node ID to query.
 * @param _removed Output: true if the vnode is removed, false otherwise.
 * @retval B_OK        Query successful.
 * @retval B_BAD_VALUE Vnode not found.
 */
extern "C" status_t
get_vnode_removed(fs_volume* volume, ino_t vnodeID, bool* _removed)
{
	ReadLocker _(sVnodeLock);

	if (struct vnode* vnode = lookup_vnode(volume->id, vnodeID)) {
		if (_removed != NULL)
			*_removed = vnode->IsRemoved();
		return B_OK;
	}

	return B_BAD_VALUE;
}


/**
 * @brief Public FS API: returns the fs_volume that owns a given fs_vnode.
 *
 * @param _vnode The fs_vnode (cast-compatible with struct vnode).
 * @return The owning fs_volume, or NULL if _vnode is NULL.
 */
extern "C" fs_volume*
volume_for_vnode(fs_vnode* _vnode)
{
	if (_vnode == NULL)
		return NULL;

	struct vnode* vnode = static_cast<struct vnode*>(_vnode);
	return vnode->mount->volume;
}


/**
 * @brief Checks whether the calling thread has the requested access permissions.
 *
 * Evaluates POSIX user/group/other permission bits against the current euid/egid.
 * Root always has read/write; execute requires at least one X bit.
 *
 * @param accessMode Requested permission bits (R_OK, W_OK, X_OK).
 * @param mode       Node permission bits (st_mode & 0777 plus type bits).
 * @param nodeGroupID Owner GID of the node.
 * @param nodeUserID  Owner UID of the node.
 * @retval B_OK               Access is permitted.
 * @retval B_PERMISSION_DENIED Access is denied.
 */
extern "C" status_t
check_access_permissions(int accessMode, mode_t mode, gid_t nodeGroupID,
	uid_t nodeUserID)
{
	// get node permissions
	int userPermissions = (mode & S_IRWXU) >> 6;
	int groupPermissions = (mode & S_IRWXG) >> 3;
	int otherPermissions = mode & S_IRWXO;

	// get the node permissions for this uid/gid
	int permissions = 0;
	uid_t uid = geteuid();

	if (uid == 0) {
		// user is root
		// root has always read/write permission, but at least one of the
		// X bits must be set for execute permission
		permissions = userPermissions | groupPermissions | otherPermissions
			| S_IROTH | S_IWOTH;
		if (S_ISDIR(mode))
			permissions |= S_IXOTH;
	} else if (uid == nodeUserID) {
		// user is node owner
		permissions = userPermissions;
	} else if (is_in_group(thread_get_current_thread()->team, nodeGroupID)) {
		// user is in owning group
		permissions = groupPermissions;
	} else {
		// user is one of the others
		permissions = otherPermissions;
	}

	return (accessMode & ~permissions) == 0 ? B_OK : B_PERMISSION_DENIED;
}


/**
 * @brief Validates write_stat permission for each requested stat field.
 *
 * Enforces POSIX rules: only the owner or root may change mode/UID/GID/times;
 * truncation requires write permission.
 *
 * @param nodeGroupID GID of the node being stat'd.
 * @param nodeUserID  UID of the node being stat'd.
 * @param nodeMode    Current mode bits of the node.
 * @param mask        B_STAT_* bitmask of fields to change.
 * @param stat        New stat values requested by the caller.
 * @retval B_OK         All requested changes are permitted.
 * @retval B_NOT_ALLOWED At least one change is not permitted.
 */
extern "C" status_t
check_write_stat_permissions(gid_t nodeGroupID, uid_t nodeUserID, mode_t nodeMode,
	uint32 mask, const struct stat* stat)
{
	uid_t uid = geteuid();

	// root has all permissions
	if (uid == 0)
		return B_OK;

	const bool hasWriteAccess = check_access_permissions(W_OK,
		nodeMode, nodeGroupID, nodeUserID) == B_OK;

	if ((mask & B_STAT_SIZE) != 0) {
		if (!hasWriteAccess)
			return B_NOT_ALLOWED;
	}

	if ((mask & B_STAT_UID) != 0) {
		if (nodeUserID == uid && stat->st_uid == uid) {
			// No change.
		} else
			return B_NOT_ALLOWED;
	}

	if ((mask & B_STAT_GID) != 0) {
		if (nodeUserID != uid)
			return B_NOT_ALLOWED;

		if (!is_in_group(thread_get_current_thread()->team, stat->st_gid))
			return B_NOT_ALLOWED;
	}

	if ((mask & B_STAT_MODE) != 0) {
		if (nodeUserID != uid)
			return B_NOT_ALLOWED;
	}

	if ((mask & (B_STAT_CREATION_TIME | B_STAT_MODIFICATION_TIME | B_STAT_CHANGE_TIME)) != 0) {
		if (!hasWriteAccess && nodeUserID != uid)
			return B_NOT_ALLOWED;
	}

	return B_OK;
}


#if 0
extern "C" status_t
read_pages(int fd, off_t pos, const iovec* vecs, size_t count,
	size_t* _numBytes)
{
	struct file_descriptor* descriptor;
	struct vnode* vnode;

	descriptor = get_fd_and_vnode(fd, &vnode, true);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	status_t status = vfs_read_pages(vnode, descriptor->cookie, pos, vecs,
		count, 0, _numBytes);

	put_fd(descriptor);
	return status;
}


extern "C" status_t
write_pages(int fd, off_t pos, const iovec* vecs, size_t count,
	size_t* _numBytes)
{
	struct file_descriptor* descriptor;
	struct vnode* vnode;

	descriptor = get_fd_and_vnode(fd, &vnode, true);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	status_t status = vfs_write_pages(vnode, descriptor->cookie, pos, vecs,
		count, 0, _numBytes);

	put_fd(descriptor);
	return status;
}
#endif


/**
 * @brief Reads data from a file using scatter/gather page I/O.
 *
 * @param fd           The file descriptor to read from.
 * @param fileVecs     File extent array describing on-disk locations.
 * @param fileVecCount Number of file extents.
 * @param vecs         Memory iovec array.
 * @param vecCount     Number of memory iovecs.
 * @param _vecIndex    In/out: current iovec index.
 * @param _vecOffset   In/out: byte offset within the current iovec.
 * @param _bytes       In: bytes to read; out: bytes actually read.
 * @retval B_OK        Read succeeded.
 * @retval B_FILE_ERROR FD is invalid or write-only.
 */
extern "C" status_t
read_file_io_vec_pages(int fd, const file_io_vec* fileVecs, size_t fileVecCount,
	const iovec* vecs, size_t vecCount, uint32* _vecIndex, size_t* _vecOffset,
	size_t* _bytes)
{
	struct vnode* vnode;
	FileDescriptorPutter descriptor(get_fd_and_vnode(fd, &vnode, true));
	if (!descriptor.IsSet())
		return B_FILE_ERROR;
	if ((descriptor->open_mode & O_RWMASK) == O_WRONLY)
		return B_FILE_ERROR;

	status_t status = common_file_io_vec_pages(vnode, descriptor->cookie,
		fileVecs, fileVecCount, vecs, vecCount, _vecIndex, _vecOffset, _bytes,
		false);

	return status;
}


/**
 * @brief Writes data to a file using scatter/gather page I/O.
 *
 * @param fd           The file descriptor to write to.
 * @param fileVecs     File extent array describing on-disk locations.
 * @param fileVecCount Number of file extents.
 * @param vecs         Memory iovec array.
 * @param vecCount     Number of memory iovecs.
 * @param _vecIndex    In/out: current iovec index.
 * @param _vecOffset   In/out: byte offset within the current iovec.
 * @param _bytes       In: bytes to write; out: bytes actually written.
 * @retval B_OK        Write succeeded.
 * @retval B_FILE_ERROR FD is invalid or read-only.
 */
extern "C" status_t
write_file_io_vec_pages(int fd, const file_io_vec* fileVecs, size_t fileVecCount,
	const iovec* vecs, size_t vecCount, uint32* _vecIndex, size_t* _vecOffset,
	size_t* _bytes)
{
	struct vnode* vnode;
	FileDescriptorPutter descriptor(get_fd_and_vnode(fd, &vnode, true));
	if (!descriptor.IsSet())
		return B_FILE_ERROR;
	if ((descriptor->open_mode & O_RWMASK) == O_RDONLY)
		return B_FILE_ERROR;

	status_t status = common_file_io_vec_pages(vnode, descriptor->cookie,
		fileVecs, fileVecCount, vecs, vecCount, _vecIndex, _vecOffset, _bytes,
		true);

	return status;
}


/**
 * @brief Adds a positive entry to the VFS entry cache.
 *
 * @param mountID Device ID of the directory's mount.
 * @param dirID   Node ID of the parent directory.
 * @param name    Entry name to cache.
 * @param nodeID  Node ID that the entry resolves to.
 * @retval B_OK        Entry cached.
 * @retval B_BAD_VALUE Mount not found.
 */
extern "C" status_t
entry_cache_add(dev_t mountID, ino_t dirID, const char* name, ino_t nodeID)
{
	// lookup mount -- the caller is required to make sure that the mount
	// won't go away
	ReadLocker locker(sMountLock);
	struct fs_mount* mount = find_mount(mountID);
	if (mount == NULL)
		return B_BAD_VALUE;
	locker.Unlock();

	return mount->entry_cache.Add(dirID, name, nodeID, false);
}


/**
 * @brief Adds a negative (missing) entry to the VFS entry cache.
 *
 * @param mountID Device ID of the directory's mount.
 * @param dirID   Node ID of the parent directory.
 * @param name    Entry name to mark as missing.
 * @retval B_OK        Negative entry cached.
 * @retval B_BAD_VALUE Mount not found.
 */
extern "C" status_t
entry_cache_add_missing(dev_t mountID, ino_t dirID, const char* name)
{
	// lookup mount -- the caller is required to make sure that the mount
	// won't go away
	ReadLocker locker(sMountLock);
	struct fs_mount* mount = find_mount(mountID);
	if (mount == NULL)
		return B_BAD_VALUE;
	locker.Unlock();

	return mount->entry_cache.Add(dirID, name, -1, true);
}


/**
 * @brief Removes an entry from the VFS entry cache.
 *
 * @param mountID Device ID of the directory's mount.
 * @param dirID   Node ID of the parent directory.
 * @param name    Entry name to remove.
 * @retval B_OK        Entry removed (or was not present).
 * @retval B_BAD_VALUE Mount not found.
 */
extern "C" status_t
entry_cache_remove(dev_t mountID, ino_t dirID, const char* name)
{
	// lookup mount -- the caller is required to make sure that the mount
	// won't go away
	ReadLocker locker(sMountLock);
	struct fs_mount* mount = find_mount(mountID);
	if (mount == NULL)
		return B_BAD_VALUE;
	locker.Unlock();

	return mount->entry_cache.Remove(dirID, name);
}


//	#pragma mark - private VFS API
//	Functions the VFS exports for other parts of the kernel


/*! Acquires another reference to the vnode that has to be released
	by calling vfs_put_vnode().
*/
/**
 * @brief Increments a vnode's reference count (private kernel VFS API).
 *
 * @param vnode The vnode to acquire an additional reference to.
 * @note Caller must already hold a valid reference to prevent concurrent freeing.
 */
void
vfs_acquire_vnode(struct vnode* vnode)
{
	inc_vnode_ref_count(vnode);
}


/*! This is currently called from file_cache_create() only.
	It's probably a temporary solution as long as devfs requires that
	fs_read_pages()/fs_write_pages() are called with the standard
	open cookie and not with a device cookie.
	If that's done differently, remove this call; it has no other
	purpose.
*/
/**
 * @brief Retrieves the FS-private open cookie for a file descriptor.
 *
 * Currently used only by file_cache_create() to obtain the device cookie.
 *
 * @param fd      The kernel file descriptor.
 * @param _cookie Output: the FS cookie associated with the FD.
 * @retval B_OK        Cookie retrieved.
 * @retval B_FILE_ERROR FD is invalid.
 */
extern "C" status_t
vfs_get_cookie_from_fd(int fd, void** _cookie)
{
	struct file_descriptor* descriptor;

	descriptor = get_fd(get_current_io_context(true), fd);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	*_cookie = descriptor->cookie;
	return B_OK;
}


/**
 * @brief Returns the vnode associated with a file descriptor, with a reference.
 *
 * @param fd     The file descriptor number.
 * @param kernel true to use the kernel I/O context.
 * @param vnode  Output: the vnode.
 * @retval B_OK        Vnode returned; caller must call vfs_put_vnode() when done.
 * @retval B_FILE_ERROR FD is invalid or has no associated vnode.
 */
extern "C" status_t
vfs_get_vnode_from_fd(int fd, bool kernel, struct vnode** vnode)
{
	*vnode = get_vnode_from_fd(fd, kernel);

	if (*vnode == NULL)
		return B_FILE_ERROR;

	return B_NO_ERROR;
}


/**
 * @brief Resolves a path to a vnode with a reference.
 *
 * @param path   The absolute or relative path to resolve.
 * @param kernel true to use the kernel I/O context.
 * @param _vnode Output: the resolved vnode; caller must call vfs_put_vnode().
 * @retval B_OK    Vnode resolved.
 * @retval B_NO_MEMORY Path buffer allocation failed.
 */
extern "C" status_t
vfs_get_vnode_from_path(const char* path, bool kernel, struct vnode** _vnode)
{
	TRACE(("vfs_get_vnode_from_path: entry. path = '%s', kernel %d\n",
		path, kernel));

	KPath pathBuffer;
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char* buffer = pathBuffer.LockBuffer();
	strlcpy(buffer, path, pathBuffer.BufferSize());

	VnodePutter vnode;
	status_t status = path_to_vnode(buffer, true, vnode, NULL, kernel);
	if (status != B_OK)
		return status;

	*_vnode = vnode.Detach();
	return B_OK;
}


/**
 * @brief Retrieves a vnode by mount/node ID with an optional wait.
 *
 * @param mountID  Device (mount) ID.
 * @param vnodeID  Node ID.
 * @param canWait  If true, retry if the vnode is temporarily busy.
 * @param _vnode   Output: the vnode; caller must call vfs_put_vnode().
 * @retval B_OK    Vnode found.
 */
extern "C" status_t
vfs_get_vnode(dev_t mountID, ino_t vnodeID, bool canWait, struct vnode** _vnode)
{
	struct vnode* vnode = NULL;

	status_t status = get_vnode(mountID, vnodeID, &vnode, canWait, false);
	if (status != B_OK)
		return status;

	*_vnode = vnode;
	return B_OK;
}


/**
 * @brief Resolves an entry_ref to a vnode without following the leaf symlink.
 *
 * @param mountID     Device ID of the parent directory.
 * @param directoryID Node ID of the parent directory.
 * @param name        Entry name.
 * @param _vnode      Output: resolved vnode; caller must call vfs_put_vnode().
 * @retval B_OK    Vnode found.
 */
extern "C" status_t
vfs_entry_ref_to_vnode(dev_t mountID, ino_t directoryID,
	const char* name, struct vnode** _vnode)
{
	VnodePutter vnode;
	status_t status = entry_ref_to_vnode(mountID, directoryID, name, false, true, vnode);
	*_vnode = vnode.Detach();
	return status;
}


/**
 * @brief Returns the device and node IDs stored in a vnode.
 *
 * @param vnode    The vnode to query.
 * @param _mountID Output: the device (mount) ID.
 * @param _vnodeID Output: the node ID.
 */
extern "C" void
vfs_vnode_to_node_ref(struct vnode* vnode, dev_t* _mountID, ino_t* _vnodeID)
{
	*_mountID = vnode->device;
	*_vnodeID = vnode->id;
}


/*!
	Helper function abstracting the process of "converting" a given
	vnode-pointer to a fs_vnode-pointer.
	Currently only used in bindfs.
*/
extern "C" fs_vnode*
vfs_fsnode_for_vnode(struct vnode* vnode)
{
	return vnode;
}


/*!
	Calls fs_open() on the given vnode and returns a new
	file descriptor for it
*/
/**
 * @brief Opens a vnode and returns a new file descriptor for it.
 *
 * @param vnode    The vnode to open (reference NOT consumed on failure).
 * @param openMode Open flags (O_RDONLY, O_WRONLY, O_RDWR, etc.).
 * @param kernel   true to create the FD in the kernel I/O context.
 * @return A non-negative FD on success, or a negative error code.
 */
int
vfs_open_vnode(struct vnode* vnode, int openMode, bool kernel)
{
	return open_vnode(vnode, openMode, kernel);
}


/*!	Looks up a vnode with the given mount and vnode ID.
	Must only be used with "in-use" vnodes as it doesn't grab a reference
	to the node.
	It's currently only be used by file_cache_create().
*/
/**
 * @brief Looks up a vnode in the hash table without acquiring a reference.
 *
 * For use by subsystems (e.g. file_cache_create()) that already hold a
 * reference via other means.
 *
 * @param mountID  Device ID.
 * @param vnodeID  Node ID.
 * @param _vnode   Output: the vnode pointer (no reference added).
 * @retval B_OK    Vnode found.
 * @retval B_ERROR Vnode not in the hash table.
 * @note Must only be used with "in-use" vnodes to avoid use-after-free.
 */
extern "C" status_t
vfs_lookup_vnode(dev_t mountID, ino_t vnodeID, struct vnode** _vnode)
{
	rw_lock_read_lock(&sVnodeLock);
	struct vnode* vnode = lookup_vnode(mountID, vnodeID);
	rw_lock_read_unlock(&sVnodeLock);

	if (vnode == NULL)
		return B_ERROR;

	*_vnode = vnode;
	return B_OK;
}


/**
 * @brief Resolves a path within a volume and returns the FS-private node data.
 *
 * Relative paths are resolved from the volume root; absolute paths use the
 * global namespace.  The node must reside on the given volume.
 *
 * @param volume          The volume whose nodes are accessible.
 * @param path            Absolute or volume-relative path.
 * @param traverseLeafLink If true, follow the final symlink.
 * @param kernel          true to use the kernel I/O context.
 * @param _node           Output: FS-private node pointer for the resolved vnode.
 * @retval B_OK        Node found on the specified volume.
 * @retval B_BAD_VALUE Node found but it belongs to a different volume.
 */
extern "C" status_t
vfs_get_fs_node_from_path(fs_volume* volume, const char* path,
	bool traverseLeafLink, bool kernel, void** _node)
{
	TRACE(("vfs_get_fs_node_from_path(volume = %p, path = \"%s\", kernel %d)\n",
		volume, path, kernel));

	KPath pathBuffer;
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	fs_mount* mount;
	status_t status = get_mount(volume->id, &mount);
	if (status != B_OK)
		return status;

	char* buffer = pathBuffer.LockBuffer();
	strlcpy(buffer, path, pathBuffer.BufferSize());

	VnodePutter vnode;

	if (buffer[0] == '/')
		status = path_to_vnode(buffer, traverseLeafLink, vnode, NULL, kernel);
	else {
		inc_vnode_ref_count(mount->root_vnode);
			// vnode_path_to_vnode() releases a reference to the starting vnode
		status = vnode_path_to_vnode(mount->root_vnode, buffer, traverseLeafLink,
			kernel, vnode, NULL);
	}

	put_mount(mount);

	if (status != B_OK)
		return status;

	if (vnode->device != volume->id) {
		// wrong mount ID - must not gain access on foreign file system nodes
		return B_BAD_VALUE;
	}

	// Use get_vnode() to resolve the cookie for the right layer.
	status = get_vnode(volume, vnode->id, _node);

	return status;
}


/**
 * @brief Retrieves stat information for a file descriptor or path.
 *
 * If @p path is non-NULL, resolves the path relative to @p fd and calls the
 * common_path_read_stat() helper.  If @p path is NULL the fd_read_stat
 * operation on the file descriptor is called directly.
 *
 * @param fd               File descriptor; used as directory base when path
 *                         is relative.
 * @param path             Optional path relative to @p fd, or NULL to stat
 *                         the FD directly.
 * @param traverseLeafLink If true, follow the leaf symlink.
 * @param stat             Output buffer to fill with stat data.
 * @param kernel           true to use the kernel I/O context.
 * @retval B_OK         Stat filled successfully.
 * @retval B_FILE_ERROR @p fd is invalid (when @p path is NULL).
 * @retval B_UNSUPPORTED FD type does not support fd_read_stat.
 */
status_t
vfs_read_stat(int fd, const char* path, bool traverseLeafLink,
	struct stat* stat, bool kernel)
{
	status_t status;

	if (path != NULL) {
		// path given: get the stat of the node referred to by (fd, path)
		KPath pathBuffer(path);
		if (pathBuffer.InitCheck() != B_OK)
			return B_NO_MEMORY;

		status = common_path_read_stat(fd, pathBuffer.LockBuffer(),
			traverseLeafLink, stat, kernel);
	} else {
		// no path given: get the FD and use the FD operation
		FileDescriptorPutter descriptor
			(get_fd(get_current_io_context(kernel), fd));
		if (!descriptor.IsSet())
			return B_FILE_ERROR;

		if (descriptor->ops->fd_read_stat)
			status = descriptor->ops->fd_read_stat(descriptor.Get(), stat);
		else
			status = B_UNSUPPORTED;
	}

	return status;
}


/*!	Finds the full path to the file that contains the module \a moduleName,
	puts it into \a pathBuffer, and returns B_OK for success.
	If \a pathBuffer was too small, it returns \c B_BUFFER_OVERFLOW,
	\c B_ENTRY_NOT_FOUNT if no file could be found.
	\a pathBuffer is clobbered in any case and must not be relied on if this
	functions returns unsuccessfully.
	\a basePath and \a pathBuffer must not point to the same space.
*/
/**
 * @brief Searches for a module file under @p basePath and constructs its full path.
 *
 * Walks the directory hierarchy under @p basePath following each component of
 * @p moduleName (slash-separated).  Stops when a regular file matching the
 * final component is found and returns its full path in @p pathBuffer.
 *
 * @param basePath   Root search directory (must already exist).
 * @param moduleName Module sub-path to locate (e.g. "bus_managers/usb/driver").
 * @param pathBuffer Output buffer for the constructed absolute path.
 * @param bufferSize Size of @p pathBuffer.
 * @retval B_OK             Module file found; @p pathBuffer contains the path.
 * @retval B_BUFFER_OVERFLOW @p pathBuffer is too small.
 * @retval B_ENTRY_NOT_FOUND No matching regular file found; path refers to
 *                           a directory or the traversal exhausted all
 *                           components.
 */
status_t
vfs_get_module_path(const char* basePath, const char* moduleName,
	char* pathBuffer, size_t bufferSize)
{
	status_t status;
	size_t length;
	char* path;

	if (bufferSize == 0
		|| strlcpy(pathBuffer, basePath, bufferSize) >= bufferSize)
		return B_BUFFER_OVERFLOW;

	VnodePutter dir;
	status = path_to_vnode(pathBuffer, true, dir, NULL, true);
	if (status != B_OK)
		return status;

	// the path buffer had been clobbered by the above call
	length = strlcpy(pathBuffer, basePath, bufferSize);
	if (pathBuffer[length - 1] != '/')
		pathBuffer[length++] = '/';

	path = pathBuffer + length;
	bufferSize -= length;

	VnodePutter file;
	while (moduleName) {
		char* nextPath = strchr(moduleName, '/');
		if (nextPath == NULL)
			length = strlen(moduleName);
		else {
			length = nextPath - moduleName;
			nextPath++;
		}

		if (length + 1 >= bufferSize)
			return B_BUFFER_OVERFLOW;

		memcpy(path, moduleName, length);
		path[length] = '\0';
		moduleName = nextPath;

		// vnode_path_to_vnode() assumes ownership of the passed dir
		status = vnode_path_to_vnode(dir.Detach(), path, true, true, file, NULL);
		if (status != B_OK)
			return status;

		if (S_ISDIR(file->Type())) {
			// goto the next directory
			path[length] = '/';
			path[length + 1] = '\0';
			path += length + 1;
			bufferSize -= length + 1;

			dir.SetTo(file.Detach());
		} else if (S_ISREG(file->Type())) {
			// it's a file so it should be what we've searched for
			return B_OK;
		} else {
			TRACE(("vfs_get_module_path(): something is strange here: "
				"0x%08" B_PRIx32 "...\n", file->Type()));
			return B_ERROR;
		}
	}

	// if we got here, the moduleName just pointed to a directory, not to
	// a real module - what should we do in this case?
	return B_ENTRY_NOT_FOUND;
}


/*!	\brief Normalizes a given path.

	The path must refer to an existing or non-existing entry in an existing
	directory, that is chopping off the leaf component the remaining path must
	refer to an existing directory.

	The returned will be canonical in that it will be absolute, will not
	contain any "." or ".." components or duplicate occurrences of '/'s,
	and none of the directory components will by symbolic links.

	Any two paths referring to the same entry, will result in the same
	normalized path (well, that is pretty much the definition of `normalized',
	isn't it :-).

	\param path The path to be normalized.
	\param buffer The buffer into which the normalized path will be written.
		   May be the same one as \a path.
	\param bufferSize The size of \a buffer.
	\param traverseLink If \c true, the function also resolves leaf symlinks.
	\param kernel \c true, if the IO context of the kernel shall be used,
		   otherwise that of the team this thread belongs to. Only relevant,
		   if the path is relative (to get the CWD).
	\return \c B_OK if everything went fine, another error code otherwise.
*/
/**
 * @brief Normalizes a path to its canonical absolute form.
 *
 * The resulting path is absolute, contains no "." or ".." components, has no
 * duplicate slashes, and none of the directory components are symbolic links
 * (leaf symlinks are followed only when @p traverseLink is true).
 *
 * @param path          The path to normalize (may equal @p buffer).
 * @param buffer        Output buffer for the normalized path; may be the same
 *                      pointer as @p path.
 * @param bufferSize    Size of @p buffer.
 * @param traverseLink  If true, also resolves a leaf symlink.
 * @param kernel        true to use the kernel I/O context for CWD resolution.
 * @retval B_OK             Path normalized.
 * @retval B_BAD_VALUE      @p path or @p buffer is NULL, or @p bufferSize < 1.
 * @retval B_BUFFER_OVERFLOW Normalized path does not fit in @p buffer.
 */
status_t
vfs_normalize_path(const char* path, char* buffer, size_t bufferSize,
	bool traverseLink, bool kernel)
{
	if (!path || !buffer || bufferSize < 1)
		return B_BAD_VALUE;

	if (path != buffer) {
		if (strlcpy(buffer, path, bufferSize) >= bufferSize)
			return B_BUFFER_OVERFLOW;
	}

	return normalize_path(buffer, bufferSize, traverseLink, kernel);
}


/*!	\brief Gets the parent of the passed in node.

	Gets the parent of the passed in node, and correctly resolves covered
	nodes.
*/
/**
 * @brief Returns the parent vnode's device/node IDs, crossing mount points.
 *
 * If @p parent is covered by another vnode (i.e. it is a mount point root),
 * the covering vnode's parent is returned.
 *
 * @param parent  The vnode whose parent is requested.
 * @param device  Output: device (mount) ID of the parent.
 * @param node    Output: node ID of the parent.
 * @retval B_OK         Parent found.
 * @retval B_ENTRY_NOT_FOUND @p parent has no parent (it is the root).
 */
extern "C" status_t
vfs_resolve_parent(struct vnode* parent, dev_t* device, ino_t* node)
{
	return resolve_covered_parent(parent, device, node,
		get_current_io_context(true));
}


/*!	\brief Creates a special node in the file system.

	The caller gets a reference to the newly created node (which is passed
	back through \a _createdVnode) and is responsible for releasing it.

	\param path The path where to create the entry for the node. Can be \c NULL,
		in which case the node is created without an entry in the root FS -- it
		will automatically be deleted when the last reference has been released.
	\param subVnode The definition of the subnode. Can be \c NULL, in which case
		the target file system will just create the node with its standard
		operations. Depending on the type of the node a subnode might be created
		automatically, though.
	\param mode The type and permissions for the node to be created.
	\param flags Flags to be passed to the creating FS.
	\param kernel \c true, if called in the kernel context (relevant only if
		\a path is not \c NULL and not absolute).
	\param _superVnode Pointer to a pre-allocated structure to be filled by the
		file system creating the node, with the private data pointer and
		operations for the super node. Can be \c NULL.
	\param _createVnode Pointer to pre-allocated storage where to store the
		pointer to the newly created node.
	\return \c B_OK, if everything went fine, another error code otherwise.
*/
/**
 * @brief Creates a special node (FIFO, device, etc.) in the file system.
 *
 * If @p path is non-NULL the node is anchored at that path; otherwise it is
 * created in the root FS without a directory entry and will be automatically
 * deleted when the last reference is dropped.
 *
 * @param path          Optional path for the new node's directory entry.
 * @param subVnode      Optional sub-vnode definition for layered FS support.
 * @param mode          Type and permission bits for the new node.
 * @param flags         FS-specific creation flags.
 * @param kernel        true when called from kernel context.
 * @param _superVnode   Optional output: super-vnode filled by the FS.
 * @param _createdVnode Output: pointer to the newly created vnode (caller
 *                      holds a reference and must call vfs_put_vnode()).
 * @retval B_OK          Node created.
 * @retval B_UNSUPPORTED The target FS does not support create_special_node.
 * @retval B_NO_MEMORY   Path buffer allocation failed.
 */
status_t
vfs_create_special_node(const char* path, fs_vnode* subVnode, mode_t mode,
	uint32 flags, bool kernel, fs_vnode* _superVnode,
	struct vnode** _createdVnode)
{
	VnodePutter dirNode;
	char _leaf[B_FILE_NAME_LENGTH];
	char* leaf = NULL;

	if (path) {
		// We've got a path. Get the dir vnode and the leaf name.
		KPath tmpPathBuffer;
		if (tmpPathBuffer.InitCheck() != B_OK)
			return B_NO_MEMORY;

		char* tmpPath = tmpPathBuffer.LockBuffer();
		if (strlcpy(tmpPath, path, B_PATH_NAME_LENGTH) >= B_PATH_NAME_LENGTH)
			return B_NAME_TOO_LONG;

		// get the dir vnode and the leaf name
		leaf = _leaf;
		status_t error = path_to_dir_vnode(tmpPath, dirNode, leaf, kernel);
		if (error != B_OK)
			return error;
	} else {
		// No path. Create the node in the root FS.
		dirNode.SetTo(sRoot);
		inc_vnode_ref_count(dirNode.Get());
	}

	// check support for creating special nodes
	if (!HAS_FS_CALL(dirNode, create_special_node))
		return B_UNSUPPORTED;

	// create the node
	fs_vnode superVnode;
	ino_t nodeID;
	status_t status = FS_CALL(dirNode.Get(), create_special_node, leaf, subVnode,
		mode, flags, _superVnode != NULL ? _superVnode : &superVnode, &nodeID);
	if (status != B_OK)
		return status;

	// lookup the node
	rw_lock_read_lock(&sVnodeLock);
	*_createdVnode = lookup_vnode(dirNode->mount->id, nodeID);
	rw_lock_read_unlock(&sVnodeLock);

	if (*_createdVnode == NULL) {
		panic("vfs_create_special_node(): lookup of node failed");
		return B_ERROR;
	}

	return B_OK;
}


/**
 * @brief Releases a reference to a vnode (private kernel VFS API).
 *
 * Decrements the vnode's reference count.  When the count reaches zero and
 * the vnode is marked removed, it is freed.
 *
 * @param vnode The vnode to release.
 */
extern "C" void
vfs_put_vnode(struct vnode* vnode)
{
	put_vnode(vnode);
}


/**
 * @brief Returns the device and node IDs of the current working directory.
 *
 * Reads the CWD from the calling thread's I/O context (user context).
 *
 * @param _mountID  Output: device ID of the CWD vnode.
 * @param _vnodeID  Output: node ID of the CWD vnode.
 * @retval B_OK     CWD retrieved.
 * @retval B_ERROR  The I/O context has no CWD set.
 */
extern "C" status_t
vfs_get_cwd(dev_t* _mountID, ino_t* _vnodeID)
{
	// Get current working directory from io context
	const struct io_context* context = get_current_io_context(false);

	ReadLocker locker(context->lock);
	if (context->cwd == NULL)
		return B_ERROR;

	*_mountID = context->cwd->device;
	*_vnodeID = context->cwd->id;
	return B_OK;
}


/**
 * @brief Unmounts a volume from the kernel side.
 *
 * Delegates to fs_unmount() using the kernel I/O context.
 *
 * @param mountID Device ID of the volume to unmount.
 * @param flags   Unmount flags (e.g. B_FORCE_UNMOUNT).
 * @retval B_OK        Volume unmounted.
 * @retval B_BUSY      Volume is in use and B_FORCE_UNMOUNT was not specified.
 */
status_t
vfs_unmount(dev_t mountID, uint32 flags)
{
	return fs_unmount(NULL, mountID, flags, true);
}


/**
 * @brief Forces all open file descriptors on a vnode to be disconnected.
 *
 * Acquires a temporary reference to the vnode and calls
 * disconnect_mount_or_vnode_fds() to close all FDs that reference it.
 *
 * @param mountID Device ID of the vnode.
 * @param vnodeID Node ID of the vnode.
 * @retval B_OK        Vnode disconnected.
 * @retval B_ENTRY_NOT_FOUND Vnode not found.
 */
extern "C" status_t
vfs_disconnect_vnode(dev_t mountID, ino_t vnodeID)
{
	struct vnode* vnode;

	status_t status = get_vnode(mountID, vnodeID, &vnode, true, true);
	if (status != B_OK)
		return status;

	disconnect_mount_or_vnode_fds(vnode->mount, vnode);
	put_vnode(vnode);
	return B_OK;
}


/**
 * @brief Releases unused vnodes in response to a low-resource notification.
 *
 * Calls the vnode low-resource handler to trim cached vnodes from the
 * inactive list.
 *
 * @param level Low-resource severity level passed to the handler.
 */
extern "C" void
vfs_free_unused_vnodes(int32 level)
{
	vnode_low_resource_handler(NULL,
		B_KERNEL_RESOURCE_PAGES | B_KERNEL_RESOURCE_MEMORY
			| B_KERNEL_RESOURCE_ADDRESS_SPACE,
		level);
}


/**
 * @brief Queries whether a vnode supports page-level I/O (mmap/page cache).
 *
 * @param vnode  The vnode to query.
 * @param cookie The open cookie.
 * @return true if the FS reports the vnode as pageable, false otherwise.
 */
extern "C" bool
vfs_can_page(struct vnode* vnode, void* cookie)
{
	FUNCTION(("vfs_canpage: vnode %p\n", vnode));

	if (HAS_FS_CALL(vnode, can_page))
		return FS_CALL(vnode, can_page, cookie);
	return false;
}


/**
 * @brief Reads pages from a vnode into caller-supplied generic I/O vectors.
 *
 * Builds an IORequest from the supplied vectors and dispatches it through the
 * vnode I/O path.
 *
 * @param vnode     The vnode to read from.
 * @param cookie    The open cookie for the vnode.
 * @param pos       Byte offset within the file.
 * @param vecs      Array of generic I/O vectors.
 * @param count     Number of entries in @p vecs.
 * @param flags     I/O flags (e.g. B_PHYSICAL_IO_REQUEST).
 * @param _numBytes In: total bytes to read; out: bytes actually transferred.
 * @retval B_OK     Read completed.
 */
extern "C" status_t
vfs_read_pages(struct vnode* vnode, void* cookie, off_t pos,
	const generic_io_vec* vecs, size_t count, uint32 flags,
	generic_size_t* _numBytes)
{
	FUNCTION(("vfs_read_pages: vnode %p, vecs %p, pos %" B_PRIdOFF "\n", vnode,
		vecs, pos));

#if VFS_PAGES_IO_TRACING
	generic_size_t bytesRequested = *_numBytes;
#endif

	IORequest request;
	status_t status = request.Init(pos, vecs, count, *_numBytes, false, flags);
	if (status == B_OK) {
		status = vfs_vnode_io(vnode, cookie, &request);
		if (status == B_OK)
			status = request.Wait();
		*_numBytes = request.TransferredBytes();
	}

	TPIO(ReadPages(vnode, cookie, pos, vecs, count, flags, bytesRequested,
		status, *_numBytes));

	return status;
}


/**
 * @brief Writes pages from caller-supplied generic I/O vectors to a vnode.
 *
 * Builds an IORequest from the supplied vectors and dispatches it through the
 * vnode I/O path.
 *
 * @param vnode     The vnode to write to.
 * @param cookie    The open cookie for the vnode.
 * @param pos       Byte offset within the file.
 * @param vecs      Array of generic I/O vectors.
 * @param count     Number of entries in @p vecs.
 * @param flags     I/O flags (e.g. B_PHYSICAL_IO_REQUEST).
 * @param _numBytes In: total bytes to write; out: bytes actually transferred.
 * @retval B_OK     Write completed.
 */
extern "C" status_t
vfs_write_pages(struct vnode* vnode, void* cookie, off_t pos,
	const generic_io_vec* vecs, size_t count, uint32 flags,
	generic_size_t* _numBytes)
{
	FUNCTION(("vfs_write_pages: vnode %p, vecs %p, pos %" B_PRIdOFF "\n", vnode,
		vecs, pos));

#if VFS_PAGES_IO_TRACING
	generic_size_t bytesRequested = *_numBytes;
#endif

	IORequest request;
	status_t status = request.Init(pos, vecs, count, *_numBytes, true, flags);
	if (status == B_OK) {
		status = vfs_vnode_io(vnode, cookie, &request);
		if (status == B_OK)
			status = request.Wait();
		*_numBytes = request.TransferredBytes();
	}

	TPIO(WritePages(vnode, cookie, pos, vecs, count, flags, bytesRequested,
		status, *_numBytes));

	return status;
}


/*!	Gets the vnode's VMCache object. If it didn't have one, it will be
	created if \a allocate is \c true.
	In case it's successful, it will also grab a reference to the cache
	it returns.
*/
/**
 * @brief Returns the VMCache for a vnode, optionally creating it.
 *
 * If the vnode already has a VMCache the function acquires an extra reference
 * and returns it.  If not and @p allocate is true, vm_create_vnode_cache() is
 * called.
 *
 * @param vnode    The vnode whose cache is requested.
 * @param _cache   Output: the VMCache with one added reference.
 * @param allocate If true, create the cache when it does not exist yet.
 * @retval B_OK        Cache returned.
 * @retval B_BAD_VALUE @p allocate is false and the vnode has no cache.
 * @note Acquires and releases sVnodeLock (read) and the vnode's own lock.
 */
extern "C" status_t
vfs_get_vnode_cache(struct vnode* vnode, VMCache** _cache, bool allocate)
{
	if (vnode->cache != NULL) {
		vnode->cache->AcquireRef();
		*_cache = vnode->cache;
		return B_OK;
	}

	rw_lock_read_lock(&sVnodeLock);
	vnode->Lock();

	status_t status = B_OK;

	// The cache could have been created in the meantime
	if (vnode->cache == NULL) {
		if (allocate) {
			// TODO: actually the vnode needs to be busy already here, or
			//	else this won't work...
			bool wasBusy = vnode->IsBusy();
			vnode->SetBusy(true);

			vnode->Unlock();
			rw_lock_read_unlock(&sVnodeLock);

			status = vm_create_vnode_cache(vnode, &vnode->cache);

			rw_lock_read_lock(&sVnodeLock);
			vnode->Lock();
			vnode->SetBusy(wasBusy);
		} else
			status = B_BAD_VALUE;
	}

	vnode->Unlock();
	rw_lock_read_unlock(&sVnodeLock);

	if (status == B_OK) {
		vnode->cache->AcquireRef();
		*_cache = vnode->cache;
	}

	return status;
}


/*!	Sets the vnode's VMCache object, for subsystems that want to manage
	their own.
	In case it's successful, it will also grab a reference to the cache
	it returns.
*/
/**
 * @brief Associates a caller-managed VMCache with a vnode.
 *
 * Used by subsystems (e.g. devfs) that manage their own page cache.  The
 * function acquires a reference to the supplied cache.
 *
 * @param vnode  The vnode to attach the cache to.
 * @param _cache The VMCache to attach (reference will be incremented).
 * @retval B_OK         Cache attached.
 * @retval B_NOT_ALLOWED The vnode already has a VMCache.
 * @note Acquires and releases sVnodeLock (read) and the vnode's own lock.
 */
extern "C" status_t
vfs_set_vnode_cache(struct vnode* vnode, VMCache* _cache)
{
	rw_lock_read_lock(&sVnodeLock);
	vnode->Lock();

	status_t status = B_OK;
	if (vnode->cache != NULL) {
		status = B_NOT_ALLOWED;
	} else {
		vnode->cache = _cache;
		_cache->AcquireRef();
	}

	vnode->Unlock();
	rw_lock_read_unlock(&sVnodeLock);
	return status;
}


/**
 * @brief Retrieves the physical file extents for a byte range of a vnode.
 *
 * Delegates to the FS get_file_map() hook.  Used by the block cache and I/O
 * scheduler to perform direct block I/O.
 *
 * @param vnode   The vnode to query.
 * @param offset  Start byte offset within the file.
 * @param size    Number of bytes requested.
 * @param vecs    Output array of file_io_vec extents.
 * @param _count  In: capacity of @p vecs; out: number of entries filled.
 * @retval B_OK   Map filled.
 */
status_t
vfs_get_file_map(struct vnode* vnode, off_t offset, size_t size,
	file_io_vec* vecs, size_t* _count)
{
	FUNCTION(("vfs_get_file_map: vnode %p, vecs %p, offset %" B_PRIdOFF
		", size = %" B_PRIuSIZE "\n", vnode, vecs, offset, size));

	return FS_CALL(vnode, get_file_map, offset, size, vecs, _count);
}


/**
 * @brief Retrieves stat information for a vnode directly.
 *
 * Calls the FS read_stat() hook and fills in st_dev, st_ino, and st_rdev.
 *
 * @param vnode The vnode to stat.
 * @param stat  Output buffer for stat data.
 * @retval B_OK Stat filled.
 */
status_t
vfs_stat_vnode(struct vnode* vnode, struct stat* stat)
{
	status_t status = FS_CALL(vnode, read_stat, stat);

	// fill in the st_dev and st_ino fields
	if (status == B_OK) {
		stat->st_dev = vnode->device;
		stat->st_ino = vnode->id;
		// the rdev field must stay unset for non-special files
		if (!S_ISBLK(stat->st_mode) && !S_ISCHR(stat->st_mode))
			stat->st_rdev = -1;
	}

	return status;
}


/**
 * @brief Retrieves stat information for a node identified by device+inode.
 *
 * Acquires a reference to the vnode, calls vfs_stat_vnode(), then releases it.
 *
 * @param device Device (mount) ID.
 * @param inode  Node ID.
 * @param stat   Output buffer for stat data.
 * @retval B_OK         Stat filled.
 * @retval B_ENTRY_NOT_FOUND Vnode not found.
 */
status_t
vfs_stat_node_ref(dev_t device, ino_t inode, struct stat* stat)
{
	struct vnode* vnode;
	status_t status = get_vnode(device, inode, &vnode, true, false);
	if (status != B_OK)
		return status;

	status = vfs_stat_vnode(vnode, stat);

	put_vnode(vnode);
	return status;
}


/**
 * @brief Retrieves the leaf name of a vnode (private kernel VFS API).
 *
 * Delegates to get_vnode_name() using the kernel I/O context.
 *
 * @param vnode    The vnode whose name is requested.
 * @param name     Output buffer for the entry name.
 * @param nameSize Size of @p name.
 * @retval B_OK            Name filled.
 * @retval B_BUFFER_OVERFLOW Name does not fit in @p name.
 */
status_t
vfs_get_vnode_name(struct vnode* vnode, char* name, size_t nameSize)
{
	return get_vnode_name(vnode, NULL, name, nameSize, true);
}


/**
 * @brief Converts a device/inode/leaf tuple into a full absolute path.
 *
 * Resolves the directory identified by (@p device, @p inode) to a path and
 * appends the optional @p leaf component.  The special leaf values "." and
 * ".." are handled directly.
 *
 * @param device     Device ID of the parent directory.
 * @param inode      Node ID of the parent directory.
 * @param leaf       Optional leaf entry name (may be NULL, ".", "..").
 * @param kernel     true to use the kernel I/O context.
 * @param path       Output buffer for the absolute path.
 * @param pathLength Size of @p path.
 * @retval B_OK             Path constructed.
 * @retval B_BAD_VALUE      @p leaf contains a slash or is empty.
 * @retval B_NAME_TOO_LONG  Resulting path exceeds @p pathLength.
 */
status_t
vfs_entry_ref_to_path(dev_t device, ino_t inode, const char* leaf,
	bool kernel, char* path, size_t pathLength)
{
	VnodePutter vnode;
	status_t status;

	// filter invalid leaf names
	if (leaf != NULL && (leaf[0] == '\0' || strchr(leaf, '/')))
		return B_BAD_VALUE;

	// get the vnode matching the dir's node_ref
	if (leaf && (strcmp(leaf, ".") == 0 || strcmp(leaf, "..") == 0)) {
		// special cases "." and "..": we can directly get the vnode of the
		// referenced directory
		status = entry_ref_to_vnode(device, inode, leaf, false, kernel, vnode);
		leaf = NULL;
	} else {
		struct vnode* temp = NULL;
		status = get_vnode(device, inode, &temp, true, false);
		vnode.SetTo(temp);
	}
	if (status != B_OK)
		return status;

	// get the directory path
	status = dir_vnode_to_path(vnode.Get(), path, pathLength, kernel);
	vnode.Unset();
		// we don't need the vnode anymore
	if (status != B_OK)
		return status;

	// append the leaf name
	if (leaf) {
		// insert a directory separator if this is not the file system root
		if ((strcmp(path, "/") && strlcat(path, "/", pathLength)
				>= pathLength)
			|| strlcat(path, leaf, pathLength) >= pathLength) {
			return B_NAME_TOO_LONG;
		}
	}

	return B_OK;
}


/*!	If the given descriptor locked its vnode, that lock will be released. */
/**
 * @brief Releases a mandatory lock held by a file descriptor on its vnode.
 *
 * If the vnode's mandatory_locked_by field points to @p descriptor, it is
 * cleared.  No-op if the descriptor does not hold the lock.
 *
 * @param descriptor The file descriptor that may hold the vnode lock.
 */
void
vfs_unlock_vnode_if_locked(struct file_descriptor* descriptor)
{
	struct vnode* vnode = fd_vnode(descriptor);

	if (vnode != NULL && vnode->mandatory_locked_by == descriptor)
		vnode->mandatory_locked_by = NULL;
}


/*!	Releases any POSIX locks on the file descriptor. */
/**
 * @brief Releases all POSIX locks held by a file descriptor on its vnode.
 *
 * If the FS provides a release_lock hook it is called; otherwise the generic
 * advisory-lock release path is used.
 *
 * @param context    The I/O context owning the file descriptor.
 * @param descriptor The file descriptor whose POSIX locks should be released.
 * @retval B_OK Always (failures from the FS are propagated as-is).
 */
status_t
vfs_release_posix_lock(io_context* context, struct file_descriptor* descriptor)
{
	struct vnode* vnode = descriptor->u.vnode;
	if (vnode == NULL)
		return B_OK;

	if (HAS_FS_CALL(vnode, release_lock))
		return FS_CALL(vnode, release_lock, descriptor->cookie, NULL);

	return release_advisory_lock(vnode, context, NULL, NULL);
}


/*!	Closes all file descriptors of the specified I/O context that
	have the O_CLOEXEC flag set.
*/
/**
 * @brief Closes all O_CLOEXEC file descriptors in an I/O context (exec path).
 *
 * Called after exec() to close file descriptors that have the close-on-exec
 * flag set.
 *
 * @param context The I/O context to process.
 */
void
vfs_exec_io_context(io_context* context)
{
	for (uint32 i = 0; i < context->table_size; i++) {
		rw_lock_write_lock(&context->lock);

		struct file_descriptor* descriptor = context->fds[i];
		bool remove = false;

		if (descriptor != NULL && fd_close_on_exec(context, i)) {
			context->fds[i] = NULL;
			context->num_used_fds--;

			remove = true;
		}

		rw_lock_write_unlock(&context->lock);

		if (remove) {
			close_fd(context, descriptor);
			put_fd(descriptor);
		}
	}
}


/*! Sets up a new io_control structure, and inherits the properties
	of the parent io_control if it is given.
*/
/**
 * @brief Allocates and initializes a new I/O context.
 *
 * If @p parentContext is non-NULL, the new context inherits open file
 * descriptors (excluding O_DISCONNECTED FDs and those filtered by
 * @p purgeCloseOnExec / close-on-fork flags).  Root and CWD vnodes are
 * inherited with an additional reference.  If @p parentContext is NULL, the
 * context starts with the global root and no CWD.
 *
 * @param parentContext    The parent I/O context to inherit from, or NULL.
 * @param purgeCloseOnExec If true, skip FDs with O_CLOEXEC (exec path);
 *                         if false, skip close-on-fork FDs (fork path).
 * @return A pointer to the new io_context on success, or NULL on allocation
 *         failure.
 */
io_context*
vfs_new_io_context(const io_context* parentContext, bool purgeCloseOnExec)
{
	io_context* context = (io_context*)malloc(sizeof(io_context));
	if (context == NULL)
		return NULL;

	TIOC(NewIOContext(context, parentContext));

	memset(context, 0, sizeof(io_context));
	context->ref_count = 1;
	rw_lock_init(&context->lock, "I/O context");

	WriteLocker contextLocker(context->lock);
	ReadLocker parentLocker;

	size_t tableSize;
	if (parentContext != NULL) {
		parentLocker.SetTo(parentContext->lock, false);
		tableSize = parentContext->table_size;
	} else
		tableSize = DEFAULT_FD_TABLE_SIZE;

	if (vfs_resize_fd_table(context, tableSize) != B_OK) {
		free(context);
		return NULL;
	}

	// Copy all parent file descriptors

	if (parentContext != NULL) {
		rw_lock_read_lock(&sIOContextRootLock);
		context->root = parentContext->root;
		if (context->root != NULL)
			inc_vnode_ref_count(context->root);
		rw_lock_read_unlock(&sIOContextRootLock);

		context->cwd = parentContext->cwd;
		if (context->cwd != NULL)
			inc_vnode_ref_count(context->cwd);

		for (size_t i = 0; i < tableSize; i++) {
			struct file_descriptor* descriptor = parentContext->fds[i];
			if (descriptor == NULL
					|| (descriptor->open_mode & O_DISCONNECTED) != 0) {
				continue;
			}

			const bool closeOnExec = fd_close_on_exec(parentContext, i);
			const bool closeOnFork = fd_close_on_fork(parentContext, i);
			if ((closeOnExec && purgeCloseOnExec) || (closeOnFork && !purgeCloseOnExec))
				continue;

			TFD(InheritFD(context, i, descriptor, parentContext));

			context->fds[i] = descriptor;
			context->num_used_fds++;
			atomic_add(&descriptor->ref_count, 1);
			atomic_add(&descriptor->open_count, 1);

			if (closeOnExec)
				fd_set_close_on_exec(context, i, true);
			if (closeOnFork)
				fd_set_close_on_fork(context, i, true);
		}

		parentLocker.Unlock();
	} else {
		context->root = sRoot;
		context->cwd = sRoot;

		if (context->root)
			inc_vnode_ref_count(context->root);

		if (context->cwd)
			inc_vnode_ref_count(context->cwd);
	}

	list_init(&context->node_monitors);
	context->max_monitors = DEFAULT_NODE_MONITORS;

	return context;
}


/**
 * @brief Increments the reference count of an I/O context.
 *
 * @param context The I/O context to acquire a reference to.
 */
void
vfs_get_io_context(io_context* context)
{
	atomic_add(&context->ref_count, 1);
}


/**
 * @brief Decrements the reference count of an I/O context and frees it if zero.
 *
 * When the last reference is dropped, free_io_context() closes all open FDs
 * and releases associated resources.
 *
 * @param context The I/O context to release.
 */
void
vfs_put_io_context(io_context* context)
{
	if (atomic_add(&context->ref_count, -1) == 1)
		free_io_context(context);
}


/**
 * @brief Resizes the file descriptor table of an I/O context.
 *
 * Allocates new FD, select_info, close-on-exec, and close-on-fork arrays of
 * the requested size and copies existing entries.  Entries beyond the new
 * size must not be in use when shrinking.
 *
 * @param context The I/O context whose table should be resized.
 * @param newSize The new table capacity (1..MAX_FD_TABLE_SIZE).
 * @retval B_OK        Table resized.
 * @retval B_BAD_VALUE @p newSize is out of range.
 * @retval B_BUSY      Shrinking would drop in-use file descriptors.
 * @retval B_NO_MEMORY Allocation failure.
 * @note Acquires context->lock (write) internally.
 */
status_t
vfs_resize_fd_table(struct io_context* context, uint32 newSize)
{
	if (newSize == 0 || newSize > MAX_FD_TABLE_SIZE)
		return B_BAD_VALUE;

	TIOC(ResizeIOContext(context, newSize));

	WriteLocker locker(context->lock);

	uint32 oldSize = context->table_size;
	int oldCloseOnExitBitmapSize = (oldSize + 7) / 8;
	int newCloseOnExitBitmapSize = (newSize + 7) / 8;

	// If the tables shrink, make sure none of the fds being dropped are in use.
	if (newSize < oldSize) {
		for (uint32 i = oldSize; i-- > newSize;) {
			if (context->fds[i] != NULL)
				return B_BUSY;
		}
	}

	// store pointers to the old tables
	file_descriptor** oldFDs = context->fds;
	select_info** oldSelectInfos = context->select_infos;
	uint8* oldCloseOnExecTable = context->fds_close_on_exec;
	uint8* oldCloseOnForkTable = context->fds_close_on_fork;

	// allocate new tables (separately to reduce the chances of needing a raw allocation)
	file_descriptor** newFDs = (file_descriptor**)malloc(
		sizeof(struct file_descriptor*) * newSize);
	select_info** newSelectInfos = (select_info**)malloc(
		+ sizeof(select_info**) * newSize);
	uint8* newCloseOnExecTable = (uint8*)malloc(newCloseOnExitBitmapSize);
	uint8* newCloseOnForkTable = (uint8*)malloc(newCloseOnExitBitmapSize);
	if (newFDs == NULL || newSelectInfos == NULL || newCloseOnExecTable == NULL
		|| newCloseOnForkTable == NULL) {
		free(newFDs);
		free(newSelectInfos);
		free(newCloseOnExecTable);
		free(newCloseOnForkTable);
		return B_NO_MEMORY;
	}

	context->fds = newFDs;
	context->select_infos = newSelectInfos;
	context->fds_close_on_exec = newCloseOnExecTable;
	context->fds_close_on_fork = newCloseOnForkTable;
	context->table_size = newSize;

	if (oldSize != 0) {
		// copy entries from old tables
		uint32 toCopy = min_c(oldSize, newSize);

		memcpy(context->fds, oldFDs, sizeof(void*) * toCopy);
		memcpy(context->select_infos, oldSelectInfos, sizeof(void*) * toCopy);
		memcpy(context->fds_close_on_exec, oldCloseOnExecTable,
			min_c(oldCloseOnExitBitmapSize, newCloseOnExitBitmapSize));
		memcpy(context->fds_close_on_fork, oldCloseOnForkTable,
			min_c(oldCloseOnExitBitmapSize, newCloseOnExitBitmapSize));
	}

	// clear additional entries, if the tables grow
	if (newSize > oldSize) {
		memset(context->fds + oldSize, 0, sizeof(void*) * (newSize - oldSize));
		memset(context->select_infos + oldSize, 0,
			sizeof(void*) * (newSize - oldSize));
		memset(context->fds_close_on_exec + oldCloseOnExitBitmapSize, 0,
			newCloseOnExitBitmapSize - oldCloseOnExitBitmapSize);
		memset(context->fds_close_on_fork + oldCloseOnExitBitmapSize, 0,
			newCloseOnExitBitmapSize - oldCloseOnExitBitmapSize);
	}

	free(oldFDs);
	free(oldSelectInfos);
	free(oldCloseOnExecTable);
	free(oldCloseOnForkTable);

	return B_OK;
}


/*!	\brief Resolves a vnode to the vnode it is covered by, if any.

	Given an arbitrary vnode (identified by mount and node ID), the function
	checks, whether the vnode is covered by another vnode. If it is, the
	function returns the mount and node ID of the covering vnode. Otherwise
	it simply returns the supplied mount and node ID.

	In case of error (e.g. the supplied node could not be found) the variables
	for storing the resolved mount and node ID remain untouched and an error
	code is returned.

	\param mountID The mount ID of the vnode in question.
	\param nodeID The node ID of the vnode in question.
	\param resolvedMountID Pointer to storage for the resolved mount ID.
	\param resolvedNodeID Pointer to storage for the resolved node ID.
	\return
	- \c B_OK, if everything went fine,
	- another error code, if something went wrong.
*/
/**
 * @brief Resolves a vnode to the vnode that covers it (if any).
 *
 * If the vnode identified by (@p mountID, @p nodeID) is covered by another
 * vnode (i.e. it is a mount point), the device and node IDs of the covering
 * vnode are returned; otherwise the original IDs are returned unchanged.
 *
 * @param mountID         Device ID of the vnode to resolve.
 * @param nodeID          Node ID of the vnode to resolve.
 * @param resolvedMountID Output: device ID of the covering vnode (or original).
 * @param resolvedNodeID  Output: node ID of the covering vnode (or original).
 * @retval B_OK           Resolution successful.
 * @retval B_ENTRY_NOT_FOUND Vnode not found.
 */
status_t
vfs_resolve_vnode_to_covering_vnode(dev_t mountID, ino_t nodeID,
	dev_t* resolvedMountID, ino_t* resolvedNodeID)
{
	// get the node
	struct vnode* node;
	status_t error = get_vnode(mountID, nodeID, &node, true, false);
	if (error != B_OK)
		return error;

	// resolve the node
	if (Vnode* coveringNode = get_covering_vnode(node)) {
		put_vnode(node);
		node = coveringNode;
	}

	// set the return values
	*resolvedMountID = node->device;
	*resolvedNodeID = node->id;

	put_vnode(node);

	return B_OK;
}


/**
 * @brief Returns the device and node IDs of a volume's mount-point vnode.
 *
 * @param mountID             Device ID of the mounted volume.
 * @param _mountPointMountID  Output: device ID of the mount-point vnode.
 * @param _mountPointNodeID   Output: node ID of the mount-point vnode.
 * @retval B_OK        Mount point found.
 * @retval B_BAD_VALUE @p mountID does not identify a known mount.
 * @note Acquires sVnodeLock (read) and sMountLock (read).
 */
status_t
vfs_get_mount_point(dev_t mountID, dev_t* _mountPointMountID,
	ino_t* _mountPointNodeID)
{
	ReadLocker nodeLocker(sVnodeLock);
	ReadLocker mountLocker(sMountLock);

	struct fs_mount* mount = find_mount(mountID);
	if (mount == NULL)
		return B_BAD_VALUE;

	Vnode* mountPoint = mount->covers_vnode;

	*_mountPointMountID = mountPoint->device;
	*_mountPointNodeID = mountPoint->id;

	return B_OK;
}


/**
 * @brief Establishes a covering/covered relationship between two vnodes.
 *
 * Used by overlay/bindfs mounts to link a covering vnode on one mount to a
 * covered vnode on another mount without going through the normal mount path.
 *
 * @param mountID        Device ID of the covering vnode.
 * @param nodeID         Node ID of the covering vnode.
 * @param coveredMountID Device ID of the covered (mount-point) vnode.
 * @param coveredNodeID  Node ID of the covered (mount-point) vnode.
 * @retval B_OK     Link established.
 * @retval B_BAD_VALUE Either vnode not found.
 * @retval B_BUSY   One of the vnodes already participates in a cover link,
 *                  or one of the mounts is being unmounted.
 * @note Acquires sVnodeLock (write).
 */
status_t
vfs_bind_mount_directory(dev_t mountID, ino_t nodeID, dev_t coveredMountID,
	ino_t coveredNodeID)
{
	// get the vnodes
	Vnode* vnode;
	status_t error = get_vnode(mountID, nodeID, &vnode, true, false);
	if (error != B_OK)
		return B_BAD_VALUE;
	VnodePutter vnodePutter(vnode);

	Vnode* coveredVnode;
	error = get_vnode(coveredMountID, coveredNodeID, &coveredVnode, true,
		false);
	if (error != B_OK)
		return B_BAD_VALUE;
	VnodePutter coveredVnodePutter(coveredVnode);

	// establish the covered/covering links
	WriteLocker locker(sVnodeLock);

	if (vnode->covers != NULL || coveredVnode->covered_by != NULL
		|| vnode->mount->unmounting || coveredVnode->mount->unmounting) {
		return B_BUSY;
	}

	vnode->covers = coveredVnode;
	vnode->SetCovering(true);

	coveredVnode->covered_by = vnode;
	coveredVnode->SetCovered(true);

	// the vnodes do now reference each other
	inc_vnode_ref_count(vnode);
	inc_vnode_ref_count(coveredVnode);

	return B_OK;
}


/**
 * @brief Retrieves a VFS-related resource limit for the calling process.
 *
 * Supports RLIMIT_NOFILE (maximum open file descriptors) and RLIMIT_NOVMON
 * (maximum node monitors).
 *
 * @param resource The RLIMIT_* constant to query.
 * @param rlp      Output buffer for the current and maximum limits.
 * @return 0 on success, B_BAD_VALUE for unsupported resources, or
 *         B_BAD_ADDRESS if @p rlp is NULL.
 */
int
vfs_getrlimit(int resource, struct rlimit* rlp)
{
	if (!rlp)
		return B_BAD_ADDRESS;

	switch (resource) {
		case RLIMIT_NOFILE:
		{
			struct io_context* context = get_current_io_context(false);
			ReadLocker _(context->lock);

			rlp->rlim_cur = context->table_size;
			rlp->rlim_max = MAX_FD_TABLE_SIZE;
			return 0;
		}

		case RLIMIT_NOVMON:
		{
			struct io_context* context = get_current_io_context(false);
			ReadLocker _(context->lock);

			rlp->rlim_cur = context->max_monitors;
			rlp->rlim_max = MAX_NODE_MONITORS;
			return 0;
		}

		default:
			return B_BAD_VALUE;
	}
}


/**
 * @brief Sets a VFS-related resource limit for the calling process.
 *
 * Supports RLIMIT_NOFILE (resizes the FD table) and RLIMIT_NOVMON (adjusts
 * node-monitor quota).  The hard limit must match the compile-time maximum.
 *
 * @param resource The RLIMIT_* constant to set.
 * @param rlp      New limits (rlim_cur is the new soft limit).
 * @return 0 on success, B_BAD_VALUE for unsupported resources,
 *         B_NOT_ALLOWED if the hard-limit constraint is violated, or
 *         B_BAD_ADDRESS if @p rlp is NULL.
 */
int
vfs_setrlimit(int resource, const struct rlimit* rlp)
{
	if (!rlp)
		return B_BAD_ADDRESS;

	switch (resource) {
		case RLIMIT_NOFILE:
			/* TODO: check getuid() */
			if (rlp->rlim_max != RLIM_SAVED_MAX
				&& rlp->rlim_max != MAX_FD_TABLE_SIZE)
				return B_NOT_ALLOWED;

			return vfs_resize_fd_table(get_current_io_context(false),
				rlp->rlim_cur);

		case RLIMIT_NOVMON:
			/* TODO: check getuid() */
			if (rlp->rlim_max != RLIM_SAVED_MAX
				&& rlp->rlim_max != MAX_NODE_MONITORS)
				return B_NOT_ALLOWED;

			return resize_monitor_table(get_current_io_context(false),
				rlp->rlim_cur);

		default:
			return B_BAD_VALUE;
	}
}


/**
 * @brief Initializes the VFS subsystem during kernel boot.
 *
 * Creates the vnode and mount hash tables, object caches for vnodes/FDs/path
 * names, initializes locking primitives, node monitors, block cache, FIFO
 * subsystem, file map, file cache, and registers debugger commands and the
 * low-resource vnode handler.
 *
 * @param args Kernel boot arguments (currently unused by VFS itself).
 * @retval B_OK  VFS initialized.
 * @retval B_ERROR block_cache_init() or file_cache_init() failed (panics for
 *                 most other failures).
 */
status_t
vfs_init(kernel_args* args)
{
	vnode::StaticInit();

	sVnodeTable = new(std::nothrow) VnodeTable();
	if (sVnodeTable == NULL || sVnodeTable->Init(VNODE_HASH_TABLE_SIZE) != B_OK)
		panic("vfs_init: error creating vnode hash table\n");

	sMountsTable = new(std::nothrow) MountTable();
	if (sMountsTable == NULL
			|| sMountsTable->Init(MOUNTS_HASH_TABLE_SIZE) != B_OK)
		panic("vfs_init: error creating mounts hash table\n");

	sPathNameCache = create_object_cache("vfs path names",
		B_PATH_NAME_LENGTH, 0);
	if (sPathNameCache == NULL)
		panic("vfs_init: error creating path name object_cache\n");
	object_cache_set_minimum_reserve(sPathNameCache, 1);

	sVnodeCache = create_object_cache("vfs vnodes",
		sizeof(struct vnode), 0);
	if (sVnodeCache == NULL)
		panic("vfs_init: error creating vnode object_cache\n");

	sFileDescriptorCache = create_object_cache("vfs fds",
		sizeof(file_descriptor), 0);
	if (sFileDescriptorCache == NULL)
		panic("vfs_init: error creating file descriptor object_cache\n");

	node_monitor_init();

	sRoot = NULL;

	recursive_lock_init(&sMountOpLock, "vfs_mount_op_lock");

	if (block_cache_init() != B_OK)
		return B_ERROR;

#ifdef ADD_DEBUGGER_COMMANDS
	// add some debugger commands
	add_debugger_command_etc("vnode", &dump_vnode,
		"Print info about the specified vnode",
		"[ \"-p\" ] ( <vnode> | <devID> <nodeID> )\n"
		"Prints information about the vnode specified by address <vnode> or\n"
		"<devID>, <vnodeID> pair. If \"-p\" is given, a path of the vnode is\n"
		"constructed and printed. It might not be possible to construct a\n"
		"complete path, though.\n",
		0);
	add_debugger_command("vnodes", &dump_vnodes,
		"list all vnodes (from the specified device)");
	add_debugger_command("vnode_caches", &dump_vnode_caches,
		"list all vnode caches");
	add_debugger_command("mount", &dump_mount,
		"info about the specified fs_mount");
	add_debugger_command("mounts", &dump_mounts, "list all fs_mounts");
	add_debugger_command("io_context", &dump_io_context,
		"info about the I/O context");
	add_debugger_command("vnode_usage", &dump_vnode_usage,
		"info about vnode usage");
#endif

	register_low_resource_handler(&vnode_low_resource_handler, NULL,
		B_KERNEL_RESOURCE_PAGES | B_KERNEL_RESOURCE_MEMORY
			| B_KERNEL_RESOURCE_ADDRESS_SPACE,
		0);

	fifo_init();
	file_map_init();

	return file_cache_init();
}


//	#pragma mark - fd_ops implementations


/**
 * @brief Validates open mode flags against a vnode's type.
 *
 * @param vnode    The vnode to be opened.
 * @param openMode Open flags (O_RDONLY, O_WRONLY, O_RDWR, O_DIRECTORY, etc.).
 * @retval B_OK            Flags are valid for this vnode type.
 * @retval B_BAD_VALUE     Conflicting flags (O_RDWR|O_WRONLY, or O_RDONLY|O_TRUNC).
 * @retval B_LINK_LIMIT    O_NOFOLLOW specified and the vnode is a symlink.
 * @retval B_NOT_A_DIRECTORY O_DIRECTORY specified but the vnode is not a directory.
 */
static status_t
check_open_mode(struct vnode* vnode, int openMode)
{
	if ((openMode & O_RDWR) != 0 && (openMode & O_WRONLY) != 0)
		return B_BAD_VALUE;
	if ((openMode & O_RWMASK) == O_RDONLY && (openMode & O_TRUNC) != 0)
		return B_BAD_VALUE;

	if ((openMode & O_NOFOLLOW) != 0 && S_ISLNK(vnode->Type()))
		return B_LINK_LIMIT;
	if ((openMode & O_DIRECTORY) != 0 && !S_ISDIR(vnode->Type()))
		return B_NOT_A_DIRECTORY;

	return B_OK;
}


/*!
	Calls fs_open() on the given vnode and returns a new
	file descriptor for it
*/
/**
 * @brief Calls the FS open() hook and returns a new file descriptor.
 *
 * @param vnode    The vnode to open.
 * @param openMode Open flags.
 * @param kernel   true to allocate the FD in the kernel I/O context.
 * @return A non-negative file descriptor on success, or a negative error code.
 */
static int
open_vnode(struct vnode* vnode, int openMode, bool kernel)
{
	status_t status = check_open_mode(vnode, openMode);
	if (status != B_OK)
		return status;

	void* cookie;
	status = FS_CALL(vnode, open, openMode, &cookie);
	if (status != B_OK)
		return status;

	int fd = get_new_fd(&sFileOps, NULL, vnode, cookie, openMode, kernel);
	if (fd < 0) {
		FS_CALL(vnode, close, cookie);
		FS_CALL(vnode, free_cookie, cookie);
	}
	return fd;
}


/*!
	Creates a new regular file and returns a new file descriptor for it.

	If O_EXCL is not specified and an entry already exists at the path,
	then that entry will be opened and returned instead.
*/
/**
 * @brief Creates a new file in @p directory or opens an existing one.
 *
 * If the entry already exists and O_EXCL is not set, the existing node is
 * opened.  The function retries up to 3 times to handle the race between
 * lookup and creation.
 *
 * @param directory The directory vnode in which to create the file.
 * @param name      Leaf name for the new file.
 * @param openMode  Open flags (O_CREAT implied, O_EXCL optional).
 * @param perms     Permission bits for the new file.
 * @param kernel    true to allocate the FD in the kernel I/O context.
 * @return A non-negative file descriptor, or a negative error code.
 */
static int
file_create_vnode(struct vnode* directory, const char* name, int openMode,
	int perms, bool kernel)
{
	bool traverse = ((openMode & (O_NOTRAVERSE | O_NOFOLLOW)) == 0);
	status_t status = B_ERROR;
	VnodePutter vnode, dirPutter;
	void* cookie;
	ino_t newID;
	char clonedName[B_FILE_NAME_LENGTH + 1];

	if (strcmp(name, ".") == 0)
		return B_IS_A_DIRECTORY;

	// This is somewhat tricky: If the entry already exists, the FS responsible
	// for the directory might not necessarily also be the one responsible for
	// the node the entry refers to (e.g. in case of mount points or FIFOs). So
	// we can actually never call the create() hook without O_EXCL. Instead we
	// try to look the entry up first. If it already exists, we just open the
	// node (unless O_EXCL), otherwise we call create() with O_EXCL. This
	// introduces a race condition, since someone else might have created the
	// entry in the meantime. We hope the respective FS returns the correct
	// error code and retry (up to 3 times) again.

	for (int i = 0; i < 3 && status != B_OK; i++) {
		bool create = false;

		// look the node up
		{
			struct vnode* entry = NULL;
			status = lookup_dir_entry(directory, name, &entry);
			vnode.SetTo(entry);
		}
		if (status == B_OK) {
			if ((openMode & O_EXCL) != 0)
				return B_FILE_EXISTS;

			// If the node is a symlink, we have to follow it, unless
			// O_NOTRAVERSE is set.
			if (S_ISLNK(vnode->Type()) && traverse) {
				vnode.Unset();
				if (strlcpy(clonedName, name, B_FILE_NAME_LENGTH)
						>= B_FILE_NAME_LENGTH) {
					return B_NAME_TOO_LONG;
				}

				inc_vnode_ref_count(directory);
				dirPutter.Unset();
				status = vnode_path_to_vnode(directory, clonedName, true,
					kernel, vnode, NULL, clonedName);
				if (status != B_OK) {
					if (status != B_ENTRY_NOT_FOUND || !vnode.IsSet())
						return status;

					// vnode is not found, but maybe it has a parent and we can create it from
					// there. In that case, vnode_path_to_vnode has set vnode to the latest
					// directory found in the path.
					directory = vnode.Detach();
					dirPutter.SetTo(directory);
					name = clonedName;
					create = true;
				}
			}

			if (!create) {
				if (S_ISDIR(vnode->Type()))
					return B_IS_A_DIRECTORY;

				int fd = open_vnode(vnode.Get(), openMode & ~O_CREAT, kernel);
				// on success keep the vnode reference for the FD
				if (fd >= 0)
					vnode.Detach();

				return fd;
			}
		}

		// it doesn't exist yet -- try to create it

		if (!HAS_FS_CALL(directory, create))
			return B_READ_ONLY_DEVICE;

		status = FS_CALL(directory, create, name, openMode | O_EXCL, perms,
			&cookie, &newID);
		if (status != B_OK && ((openMode & O_EXCL) != 0 || status != B_FILE_EXISTS))
			return status;
	}

	if (status != B_OK)
		return status;

	// the node has been created successfully

	rw_lock_read_lock(&sVnodeLock);
	vnode.SetTo(lookup_vnode(directory->device, newID));
	rw_lock_read_unlock(&sVnodeLock);

	if (!vnode.IsSet()) {
		panic("vfs: fs_create() returned success but there is no vnode, "
			"mount ID %" B_PRIdDEV "!\n", directory->device);
		return B_BAD_VALUE;
	}

	int fd = get_new_fd(&sFileOps, NULL, vnode.Get(), cookie, openMode, kernel);
	if (fd >= 0) {
		vnode.Detach();
		return fd;
	}

	status = fd;

	// something went wrong, clean up

	FS_CALL(vnode.Get(), close, cookie);
	FS_CALL(vnode.Get(), free_cookie, cookie);

	FS_CALL(directory, unlink, name);

	return status;
}


/*! Calls fs open_dir() on the given vnode and returns a new
	file descriptor for it
*/
/**
 * @brief Calls the FS open_dir() hook and returns a new directory FD.
 *
 * @param vnode  The directory vnode to open.
 * @param kernel true to allocate the FD in the kernel I/O context.
 * @return A non-negative FD on success, or a negative error code.
 * @retval B_UNSUPPORTED The vnode does not implement open_dir.
 */
static int
open_dir_vnode(struct vnode* vnode, bool kernel)
{
	if (!HAS_FS_CALL(vnode, open_dir))
		return B_UNSUPPORTED;

	void* cookie;
	status_t status = FS_CALL(vnode, open_dir, &cookie);
	if (status != B_OK)
		return status;

	// directory is opened, create a fd
	status = get_new_fd(&sDirectoryOps, NULL, vnode, cookie, O_CLOEXEC, kernel);
	if (status >= 0)
		return status;

	FS_CALL(vnode, close_dir, cookie);
	FS_CALL(vnode, free_dir_cookie, cookie);

	return status;
}


/*! Calls fs open_attr_dir() on the given vnode and returns a new
	file descriptor for it.
	Used by attr_dir_open(), and attr_dir_open_fd().
*/
/**
 * @brief Calls the FS open_attr_dir() hook and returns a new FD.
 *
 * @param vnode  The vnode whose attribute directory to open.
 * @param kernel true to allocate the FD in the kernel I/O context.
 * @return A non-negative FD on success, or a negative error code.
 * @retval B_UNSUPPORTED The vnode does not implement open_attr_dir.
 */
static int
open_attr_dir_vnode(struct vnode* vnode, bool kernel)
{
	if (!HAS_FS_CALL(vnode, open_attr_dir))
		return B_UNSUPPORTED;

	void* cookie;
	status_t status = FS_CALL(vnode, open_attr_dir, &cookie);
	if (status != B_OK)
		return status;

	// directory is opened, create a fd
	status = get_new_fd(&sAttributeDirectoryOps, NULL, vnode, cookie, O_CLOEXEC,
		kernel);
	if (status >= 0)
		return status;

	FS_CALL(vnode, close_attr_dir, cookie);
	FS_CALL(vnode, free_attr_dir_cookie, cookie);

	return status;
}


/**
 * @brief Creates a new file identified by entry_ref and returns an FD.
 *
 * Looks up the directory vnode by (@p mountID, @p directoryID) and delegates
 * to file_create_vnode().
 *
 * @param mountID     Device ID of the parent directory.
 * @param directoryID Node ID of the parent directory.
 * @param name        Entry name for the new file.
 * @param openMode    Open flags.
 * @param perms       Permission bits.
 * @param kernel      true to use the kernel I/O context.
 * @return A non-negative FD, or a negative error code.
 */
static int
file_create_entry_ref(dev_t mountID, ino_t directoryID, const char* name,
	int openMode, int perms, bool kernel)
{
	FUNCTION(("file_create_entry_ref: name = '%s', omode %x, perms %d, "
		"kernel %d\n", name, openMode, perms, kernel));

	// get directory to put the new file in
	struct vnode* directory;
	status_t status = get_vnode(mountID, directoryID, &directory, true, false);
	if (status != B_OK)
		return status;

	status = file_create_vnode(directory, name, openMode, perms, kernel);
	put_vnode(directory);

	return status;
}


/**
 * @brief Creates a new file identified by a path and returns an FD.
 *
 * Resolves the directory component of @p path relative to @p fd and delegates
 * to file_create_vnode().
 *
 * @param fd       Base directory FD for relative paths (or AT_FDCWD).
 * @param path     Path of the file to create (leaf is extracted).
 * @param openMode Open flags.
 * @param perms    Permission bits.
 * @param kernel   true to use the kernel I/O context.
 * @return A non-negative FD, or a negative error code.
 */
static int
file_create(int fd, char* path, int openMode, int perms, bool kernel)
{
	FUNCTION(("file_create: path '%s', omode %x, perms %d, kernel %d\n", path,
		openMode, perms, kernel));

	// get directory to put the new file in
	char name[B_FILE_NAME_LENGTH];
	VnodePutter directory;
	status_t status = fd_and_path_to_dir_vnode(fd, path, directory, name,
		kernel);
	if (status < 0)
		return status;

	return file_create_vnode(directory.Get(), name, openMode, perms, kernel);
}


/**
 * @brief Opens a file identified by entry_ref and returns an FD.
 *
 * @param mountID     Device ID of the parent directory.
 * @param directoryID Node ID of the parent directory.
 * @param name        Entry name.
 * @param openMode    Open flags.
 * @param kernel      true to use the kernel I/O context.
 * @return A non-negative FD, or a negative error code.
 * @retval B_BAD_VALUE @p name is NULL or empty.
 */
static int
file_open_entry_ref(dev_t mountID, ino_t directoryID, const char* name,
	int openMode, bool kernel)
{
	if (name == NULL || *name == '\0')
		return B_BAD_VALUE;

	FUNCTION(("file_open_entry_ref(ref = (%" B_PRId32 ", %" B_PRId64 ", %s), "
		"openMode = %d)\n", mountID, directoryID, name, openMode));

	bool traverse = (openMode & (O_NOTRAVERSE | O_NOFOLLOW)) == 0;

	// get the vnode matching the entry_ref
	VnodePutter vnode;
	status_t status = entry_ref_to_vnode(mountID, directoryID, name, traverse,
		kernel, vnode);
	if (status != B_OK)
		return status;

	int newFD = open_vnode(vnode.Get(), openMode, kernel);
	if (newFD >= 0) {
		cache_node_opened(vnode.Get(), vnode->cache, mountID,
			directoryID, vnode->id, name);

		// The vnode reference has been transferred to the FD
		vnode.Detach();
	}

	return newFD;
}


/**
 * @brief Opens a file identified by (fd, path) and returns an FD.
 *
 * Resolves the path relative to @p fd using fd_and_path_to_vnode() and then
 * calls open_vnode().
 *
 * @param fd       Base FD for relative paths (or AT_FDCWD).
 * @param path     File path to open.
 * @param openMode Open flags.
 * @param kernel   true to use the kernel I/O context.
 * @return A non-negative FD, or a negative error code.
 */
static int
file_open(int fd, char* path, int openMode, bool kernel)
{
	bool traverse = (openMode & (O_NOTRAVERSE | O_NOFOLLOW)) == 0;

	FUNCTION(("file_open: fd: %d, entry path = '%s', omode %d, kernel %d\n",
		fd, path, openMode, kernel));

	// get the vnode matching the vnode + path combination
	VnodePutter vnode;
	ino_t parentID;
	status_t status = fd_and_path_to_vnode(fd, path, traverse, vnode,
		&parentID, kernel);
	if (status != B_OK)
		return status;

	// open the vnode
	int newFD = open_vnode(vnode.Get(), openMode, kernel);
	if (newFD >= 0) {
		cache_node_opened(vnode.Get(), vnode->cache,
			vnode->device, parentID, vnode->id, NULL);

		// The vnode reference has been transferred to the FD
		vnode.Detach();
	}

	return newFD;
}


/**
 * @brief fd_ops close handler for regular files.
 *
 * Calls the FS close() hook and releases all advisory/POSIX locks held by
 * this descriptor.
 *
 * @param descriptor The file descriptor being closed.
 * @retval B_OK Close succeeded.
 */
static status_t
file_close(struct file_descriptor* descriptor)
{
	struct vnode* vnode = descriptor->u.vnode;
	status_t status = B_OK;

	FUNCTION(("file_close(descriptor = %p)\n", descriptor));

	cache_node_closed(vnode, vnode->cache, vnode->device,
		vnode->id);
	if (HAS_FS_CALL(vnode, close)) {
		status = FS_CALL(vnode, close, descriptor->cookie);
	}

	if (status == B_OK) {
		// remove all outstanding locks for this team
		if (HAS_FS_CALL(vnode, release_lock))
			status = FS_CALL(vnode, release_lock, descriptor->cookie, NULL);
		else
			status = release_advisory_lock(vnode, NULL, descriptor, NULL);
	}
	return status;
}


/**
 * @brief fd_ops free_fd handler for regular files.
 *
 * Calls the FS free_cookie() hook and drops the vnode reference held by the
 * file descriptor.
 *
 * @param descriptor The file descriptor being freed.
 */
static void
file_free_fd(struct file_descriptor* descriptor)
{
	struct vnode* vnode = descriptor->u.vnode;

	if (vnode != NULL) {
		FS_CALL(vnode, free_cookie, descriptor->cookie);
		put_vnode(vnode);
	}
}


/**
 * @brief fd_ops read handler for regular files.
 *
 * @param descriptor The open file descriptor.
 * @param pos        File offset; -1 means use the FD's current position.
 * @param buffer     Destination buffer.
 * @param length     In: bytes to read; out: bytes actually read.
 * @retval B_IS_A_DIRECTORY If the vnode is a directory.
 * @retval ESPIPE           @p pos is not -1 but the file is non-seekable.
 */
static status_t
file_read(struct file_descriptor* descriptor, off_t pos, void* buffer,
	size_t* length)
{
	struct vnode* vnode = descriptor->u.vnode;
	FUNCTION(("file_read: buf %p, pos %" B_PRIdOFF ", len %p = %ld\n", buffer,
		pos, length, *length));

	if (S_ISDIR(vnode->Type()))
		return B_IS_A_DIRECTORY;
	if (pos != -1 && descriptor->pos == -1)
		return ESPIPE;

	return FS_CALL(vnode, read, descriptor->cookie, pos, buffer, length);
}


/**
 * @brief fd_ops write handler for regular files.
 *
 * @param descriptor The open file descriptor.
 * @param pos        File offset; -1 means use the FD's current position.
 * @param buffer     Source buffer.
 * @param length     In: bytes to write; out: bytes actually written.
 * @retval B_IS_A_DIRECTORY If the vnode is a directory.
 * @retval ESPIPE           @p pos is not -1 but the file is non-seekable.
 * @retval B_READ_ONLY_DEVICE FS has no write hook.
 */
static status_t
file_write(struct file_descriptor* descriptor, off_t pos, const void* buffer,
	size_t* length)
{
	struct vnode* vnode = descriptor->u.vnode;
	FUNCTION(("file_write: buf %p, pos %" B_PRIdOFF ", len %p\n", buffer, pos,
		length));

	if (S_ISDIR(vnode->Type()))
		return B_IS_A_DIRECTORY;
	if (pos != -1 && descriptor->pos == -1)
		return ESPIPE;

	if (!HAS_FS_CALL(vnode, write))
		return B_READ_ONLY_DEVICE;

	return FS_CALL(vnode, write, descriptor->cookie, pos, buffer, length);
}


/**
 * @brief Performs scatter/gather I/O on a regular file FD.
 *
 * Only supported for vnodes without a page cache (raw I/O via the io hook).
 *
 * @param descriptor The open file descriptor.
 * @param pos        File offset (must not be -1).
 * @param vecs       Array of iovec structures.
 * @param count      Number of iovecs.
 * @param write      true to write, false to read.
 * @return Total bytes transferred on success, or a negative error code.
 * @retval ESPIPE       The file is non-seekable.
 * @retval B_IS_A_DIRECTORY The vnode is a directory.
 * @retval B_UNSUPPORTED    pos == -1, no io hook, or vnode has a cache.
 */
static ssize_t
file_vector_io(struct file_descriptor* descriptor, off_t pos,
	const struct iovec *vecs, int count, bool write)
{
	struct vnode* vnode = descriptor->u.vnode;
	if (pos != -1 && descriptor->pos == -1)
		return ESPIPE;
	if (S_ISDIR(vnode->Type()))
		return B_IS_A_DIRECTORY;

	if (pos == -1)
		return B_UNSUPPORTED;
	if (!HAS_FS_CALL(vnode, io))
		return B_UNSUPPORTED;

	// We can only perform real vectored I/O for vnodes that have no cache,
	// because the I/O hook bypasses the cache entirely.
	if (vnode->cache != NULL)
		return B_UNSUPPORTED;

	BStackOrHeapArray<generic_io_vec, 8> iovecs(count);
	if (!iovecs.IsValid())
		return B_NO_MEMORY;

	generic_size_t length = 0;
	for (int i = 0; i < count; i++) {
		iovecs[i].base = (generic_addr_t)vecs[i].iov_base;
		iovecs[i].length = vecs[i].iov_len;
		length += vecs[i].iov_len;
	}

	status_t status = (write ? vfs_write_pages : vfs_read_pages)(vnode,
		descriptor->cookie, pos, iovecs, count, 0, &length);
	if (length > 0)
		return length;
	return status;
}


/**
 * @brief fd_ops readv handler — scatter-read from a regular file.
 *
 * @param descriptor The open file descriptor.
 * @param pos        File offset.
 * @param vecs       iovec array.
 * @param count      Number of iovecs.
 * @return Bytes read, or a negative error code.
 */
static ssize_t
file_readv(struct file_descriptor* descriptor, off_t pos,
	const struct iovec *vecs, int count)
{
	FUNCTION(("file_readv: pos %" B_PRIdOFF "\n", pos));
	return file_vector_io(descriptor, pos, vecs, count, false);
}


/**
 * @brief fd_ops writev handler — gather-write to a regular file.
 *
 * @param descriptor The open file descriptor.
 * @param pos        File offset.
 * @param vecs       iovec array.
 * @param count      Number of iovecs.
 * @return Bytes written, or a negative error code.
 */
static ssize_t
file_writev(struct file_descriptor* descriptor, off_t pos,
	const struct iovec *vecs, int count)
{
	FUNCTION(("file_writev: pos %" B_PRIdOFF "\n", pos));
	return file_vector_io(descriptor, pos, vecs, count, true);
}


/**
 * @brief fd_ops seek handler for regular files.
 *
 * Computes the new position from @p pos and @p seekType (SEEK_SET, SEEK_CUR,
 * SEEK_END, SEEK_DATA, SEEK_HOLE).  For block/char devices the end-of-file is
 * determined via the geometry ioctl.
 *
 * @param descriptor The open file descriptor.
 * @param pos        Seek offset.
 * @param seekType   SEEK_SET, SEEK_CUR, SEEK_END, SEEK_DATA, or SEEK_HOLE.
 * @return New file position on success, or ESPIPE / negative error code.
 */
static off_t
file_seek(struct file_descriptor* descriptor, off_t pos, int seekType)
{
	struct vnode* vnode = descriptor->u.vnode;
	off_t offset;
	bool isDevice = false;

	FUNCTION(("file_seek(pos = %" B_PRIdOFF ", seekType = %d)\n", pos,
		seekType));

	if (descriptor->pos == -1)
		return ESPIPE;

	switch (vnode->Type() & S_IFMT) {
		// drivers publish block devices as chr, so pick both
		case S_IFBLK:
		case S_IFCHR:
			isDevice = true;
			break;
	}

	switch (seekType) {
		case SEEK_SET:
			offset = 0;
			break;
		case SEEK_CUR:
			offset = descriptor->pos;
			break;
		case SEEK_END:
		{
			// stat() the node
			if (!HAS_FS_CALL(vnode, read_stat))
				return B_UNSUPPORTED;

			struct stat stat;
			status_t status = FS_CALL(vnode, read_stat, &stat);
			if (status != B_OK)
				return status;

			offset = stat.st_size;

			if (offset == 0 && isDevice) {
				// stat() on regular drivers doesn't report size
				device_geometry geometry;

				if (HAS_FS_CALL(vnode, ioctl)) {
					status = FS_CALL(vnode, ioctl, descriptor->cookie,
						B_GET_GEOMETRY, &geometry, sizeof(geometry));
					if (status == B_OK)
						offset = (off_t)geometry.bytes_per_sector
							* geometry.sectors_per_track
							* geometry.cylinder_count
							* geometry.head_count;
				}
			}

			break;
		}
		case SEEK_DATA:
		case SEEK_HOLE:
		{
			status_t status = B_BAD_VALUE;
			if (HAS_FS_CALL(vnode, ioctl)) {
				offset = pos;
				status = FS_CALL(vnode, ioctl, descriptor->cookie,
					seekType == SEEK_DATA ? FIOSEEKDATA : FIOSEEKHOLE,
					&offset, sizeof(offset));
				if (status == B_OK) {
					if (offset > pos)
						offset -= pos;
					break;
				}
			}
			if (status != B_BAD_VALUE && status != B_DEV_INVALID_IOCTL && status != B_UNSUPPORTED)
				return status;

			// basic implementation with stat() the node
			if (!HAS_FS_CALL(vnode, read_stat) || isDevice)
				return B_BAD_VALUE;

			struct stat stat;
			status = FS_CALL(vnode, read_stat, &stat);
			if (status != B_OK)
				return status;

			off_t end = stat.st_size;
			if (pos >= end)
				return ENXIO;
			offset = seekType == SEEK_HOLE ? end - pos : 0;
			break;
		}
		default:
			return B_BAD_VALUE;
	}

	// assumes off_t is 64 bits wide
	if (offset > 0 && LONGLONG_MAX - offset < pos)
		return B_BUFFER_OVERFLOW;

	pos += offset;
	if (pos < 0)
		return B_BAD_VALUE;

	return descriptor->pos = pos;
}


/**
 * @brief fd_ops select handler for regular files.
 *
 * Calls the FS select() hook.  If the FS does not support select, the event is
 * immediately notified for non-output-only event types.
 *
 * @param descriptor The file descriptor to monitor.
 * @param event      The select event (B_SELECT_READ, B_SELECT_WRITE, etc.).
 * @param sync       The selectsync object to notify.
 * @retval B_OK        Event registered.
 * @retval B_UNSUPPORTED FS has no select hook (event was auto-notified).
 */
static status_t
file_select(struct file_descriptor* descriptor, uint8 event,
	struct selectsync* sync)
{
	FUNCTION(("file_select(%p, %u, %p)\n", descriptor, event, sync));

	struct vnode* vnode = descriptor->u.vnode;

	// If the FS has no select() hook, notify select() now.
	if (!HAS_FS_CALL(vnode, select)) {
		if (!SELECT_TYPE_IS_OUTPUT_ONLY(event))
			notify_select_event(sync, event);
		return B_UNSUPPORTED;
	}

	status_t result = FS_CALL(vnode, select, descriptor->cookie, event, sync);

	// A FS may have a select() hook but may not support it in some cases (e.g. write_overlay)
	if (result == B_UNSUPPORTED && !SELECT_TYPE_IS_OUTPUT_ONLY(event))
		notify_select_event(sync, event);

	return result;
}


/**
 * @brief fd_ops deselect handler for regular files.
 *
 * Cancels a previously registered select event.
 *
 * @param descriptor The file descriptor being deselected.
 * @param event      The select event to cancel.
 * @param sync       The selectsync object.
 * @retval B_OK Always.
 */
static status_t
file_deselect(struct file_descriptor* descriptor, uint8 event,
	struct selectsync* sync)
{
	struct vnode* vnode = descriptor->u.vnode;

	if (!HAS_FS_CALL(vnode, deselect))
		return B_OK;

	return FS_CALL(vnode, deselect, descriptor->cookie, event, sync);
}


/**
 * @brief Creates a subdirectory named @p name inside the given directory vnode.
 *
 * @param vnode The parent directory vnode.
 * @param name  Name for the new subdirectory.
 * @param perms Permission bits.
 * @retval B_OK         Directory created.
 * @retval B_FILE_EXISTS An entry with this name already exists.
 * @retval B_READ_ONLY_DEVICE FS has no create_dir hook.
 */
static status_t
dir_create_vnode(struct vnode* vnode, const char* name, int perms)
{
	status_t status;
	if (HAS_FS_CALL(vnode, create_dir)) {
		status = FS_CALL(vnode, create_dir, name, perms);
	} else {
		struct vnode* entry = NULL;
		if (lookup_dir_entry(vnode, name, &entry) == B_OK) {
			status = B_FILE_EXISTS;
			put_vnode(entry);
		} else
			status = B_READ_ONLY_DEVICE;
	}
	return status;
}


/**
 * @brief Creates a directory identified by entry_ref.
 *
 * @param mountID  Device ID of the parent directory.
 * @param parentID Node ID of the parent directory.
 * @param name     Name for the new directory.
 * @param perms    Permission bits.
 * @param kernel   true to use the kernel I/O context.
 * @retval B_OK        Directory created.
 * @retval B_BAD_VALUE @p name is NULL or empty.
 */
static status_t
dir_create_entry_ref(dev_t mountID, ino_t parentID, const char* name, int perms,
	bool kernel)
{
	struct vnode* vnode;
	status_t status;

	if (name == NULL || *name == '\0')
		return B_BAD_VALUE;

	FUNCTION(("dir_create_entry_ref(dev = %" B_PRId32 ", ino = %" B_PRId64 ", "
		"name = '%s', perms = %d)\n", mountID, parentID, name, perms));

	status = get_vnode(mountID, parentID, &vnode, true, false);
	if (status != B_OK)
		return status;

	status = dir_create_vnode(vnode, name, perms);

	put_vnode(vnode);
	return status;
}


/**
 * @brief Creates a directory identified by (fd, path).
 *
 * Resolves the parent directory from @p path relative to @p fd and calls
 * dir_create_vnode().
 *
 * @param fd     Base FD for relative paths (or AT_FDCWD).
 * @param path   Path of the directory to create.
 * @param perms  Permission bits.
 * @param kernel true to use the kernel I/O context.
 * @retval B_OK Directory created.
 */
static status_t
dir_create(int fd, char* path, int perms, bool kernel)
{
	char filename[B_FILE_NAME_LENGTH];
	status_t status;

	FUNCTION(("dir_create: path '%s', perms %d, kernel %d\n", path, perms,
		kernel));

	char* p = strrchr(path, '/');
	if (p != NULL) {
		p++;
		if (p[0] == '\0') {
			// special case: the path ends in one or more '/' - remove them
			while (*--p == '/' && p != path)
				;
			p[1] = '\0';
		}
	}

	VnodePutter vnode;
	status = fd_and_path_to_dir_vnode(fd, path, vnode, filename, kernel);
	if (status < 0)
		return status;

	return dir_create_vnode(vnode.Get(), filename, perms);
}


/**
 * @brief Opens a directory identified by entry_ref and returns an FD.
 *
 * If @p name is NULL, the vnode at (@p mountID, @p parentID) is opened
 * directly; otherwise the entry is looked up first.
 *
 * @param mountID  Device ID.
 * @param parentID Node ID of the parent (or the directory itself if name is NULL).
 * @param name     Entry name (may be NULL).
 * @param kernel   true to use the kernel I/O context.
 * @return A non-negative FD, or a negative error code.
 * @retval B_BAD_VALUE @p name is an empty string.
 */
static int
dir_open_entry_ref(dev_t mountID, ino_t parentID, const char* name, bool kernel)
{
	FUNCTION(("dir_open_entry_ref()\n"));

	if (name != NULL && name[0] == '\0')
		return B_BAD_VALUE;

	// get the vnode matching the entry_ref/node_ref
	VnodePutter vnode;
	status_t status;
	if (name != NULL) {
		status = entry_ref_to_vnode(mountID, parentID, name, true, kernel,
			vnode);
	} else {
		struct vnode* temp = NULL;
		status = get_vnode(mountID, parentID, &temp, true, false);
		vnode.SetTo(temp);
	}
	if (status != B_OK)
		return status;

	int newFD = open_dir_vnode(vnode.Get(), kernel);
	if (newFD >= 0) {
		cache_node_opened(vnode.Get(), vnode->cache, mountID, parentID,
			vnode->id, name);

		// The vnode reference has been transferred to the FD
		vnode.Detach();
	}

	return newFD;
}


/**
 * @brief Opens a directory identified by (fd, path) and returns an FD.
 *
 * @param fd     Base FD for relative paths (or AT_FDCWD).
 * @param path   Directory path to open.
 * @param kernel true to use the kernel I/O context.
 * @return A non-negative FD, or a negative error code.
 */
static int
dir_open(int fd, char* path, bool kernel)
{
	FUNCTION(("dir_open: fd: %d, entry path = '%s', kernel %d\n", fd, path,
		kernel));

	// get the vnode matching the vnode + path combination
	VnodePutter vnode;
	ino_t parentID;
	status_t status = fd_and_path_to_vnode(fd, path, true, vnode, &parentID,
		kernel);
	if (status != B_OK)
		return status;

	// open the dir
	int newFD = open_dir_vnode(vnode.Get(), kernel);
	if (newFD >= 0) {
		cache_node_opened(vnode.Get(), vnode->cache, vnode->device,
			parentID, vnode->id, NULL);

		// The vnode reference has been transferred to the FD
		vnode.Detach();
	}

	return newFD;
}


/**
 * @brief fd_ops close handler for directory file descriptors.
 *
 * @param descriptor The directory FD being closed.
 * @retval B_OK Always.
 */
static status_t
dir_close(struct file_descriptor* descriptor)
{
	struct vnode* vnode = descriptor->u.vnode;

	FUNCTION(("dir_close(descriptor = %p)\n", descriptor));

	cache_node_closed(vnode, vnode->cache, vnode->device,
		vnode->id);
	if (HAS_FS_CALL(vnode, close_dir))
		return FS_CALL(vnode, close_dir, descriptor->cookie);

	return B_OK;
}


/**
 * @brief fd_ops free_fd handler for directory file descriptors.
 *
 * Calls the FS free_dir_cookie() hook and drops the vnode reference.
 *
 * @param descriptor The directory FD being freed.
 */
static void
dir_free_fd(struct file_descriptor* descriptor)
{
	struct vnode* vnode = descriptor->u.vnode;

	if (vnode != NULL) {
		FS_CALL(vnode, free_dir_cookie, descriptor->cookie);
		put_vnode(vnode);
	}
}


/**
 * @brief fd_ops read_dir handler — reads entries from a directory FD.
 *
 * Delegates to the vnode-based dir_read() overload.
 *
 * @param ioContext  The I/O context of the caller.
 * @param descriptor The directory FD.
 * @param buffer     Output buffer for dirent structures.
 * @param bufferSize Size of @p buffer.
 * @param _count     In: max entries to read; out: entries actually read.
 * @retval B_OK Entries read (check *_count for the actual count).
 */
static status_t
dir_read(struct io_context* ioContext, struct file_descriptor* descriptor,
	struct dirent* buffer, size_t bufferSize, uint32* _count)
{
	return dir_read(ioContext, descriptor->u.vnode, descriptor->cookie, buffer,
		bufferSize, _count);
}


/**
 * @brief Adjusts device/inode fields of a dirent for mount-point crossing.
 *
 * Sets d_pdev/d_pino from the parent vnode.  For ".." entries whose parent is
 * a covering vnode, resolves to the covered parent.  For all other entries,
 * follows covered_by links so the consumer sees the covering FS node IDs.
 *
 * @param parent    The directory vnode that owns the entry.
 * @param entry     The dirent to fix up.
 * @param ioContext I/O context for CWD/root resolution.
 * @retval B_OK Fixup applied.
 */
static status_t
fix_dirent(struct vnode* parent, struct dirent* entry,
	struct io_context* ioContext)
{
	// set d_pdev and d_pino
	entry->d_pdev = parent->device;
	entry->d_pino = parent->id;

	// If this is the ".." entry and the directory covering another vnode,
	// we need to replace d_dev and d_ino with the actual values.
	if (strcmp(entry->d_name, "..") == 0 && parent->IsCovering()) {
		return resolve_covered_parent(parent, &entry->d_dev, &entry->d_ino,
			ioContext);
	}

	// resolve covered vnodes
	ReadLocker _(&sVnodeLock);

	struct vnode* vnode = lookup_vnode(entry->d_dev, entry->d_ino);
	if (vnode != NULL && vnode->covered_by != NULL) {
		do {
			vnode = vnode->covered_by;
		} while (vnode->covered_by != NULL);

		entry->d_dev = vnode->device;
		entry->d_ino = vnode->id;
	}

	return B_OK;
}


/**
 * @brief Reads directory entries from a vnode and applies mount-point fixups.
 *
 * Calls the FS read_dir() hook and then calls fix_dirent() on each returned
 * entry to adjust device/inode IDs for mount-point crossing.
 *
 * @param ioContext  The I/O context of the caller.
 * @param vnode      The directory vnode.
 * @param cookie     The open directory cookie.
 * @param buffer     Output buffer for dirent structures.
 * @param bufferSize Size of @p buffer.
 * @param _count     In: max entries; out: entries actually read.
 * @retval B_OK         Entries read.
 * @retval B_UNSUPPORTED FS has no read_dir hook.
 */
static status_t
dir_read(struct io_context* ioContext, struct vnode* vnode, void* cookie,
	struct dirent* buffer, size_t bufferSize, uint32* _count)
{
	if (!HAS_FS_CALL(vnode, read_dir))
		return B_UNSUPPORTED;

	status_t error = FS_CALL(vnode, read_dir, cookie, buffer, bufferSize,
		_count);
	if (error != B_OK)
		return error;

	// we need to adjust the read dirents
	uint32 count = *_count;
	for (uint32 i = 0; i < count; i++) {
		error = fix_dirent(vnode, buffer, ioContext);
		if (error != B_OK)
			return error;

		buffer = (struct dirent*)((uint8*)buffer + buffer->d_reclen);
	}

	return error;
}


/**
 * @brief fd_ops rewind_dir handler — resets directory enumeration to the start.
 *
 * @param descriptor The directory FD.
 * @retval B_OK         Position reset.
 * @retval B_UNSUPPORTED FS has no rewind_dir hook.
 */
static status_t
dir_rewind(struct file_descriptor* descriptor)
{
	struct vnode* vnode = descriptor->u.vnode;

	if (HAS_FS_CALL(vnode, rewind_dir)) {
		return FS_CALL(vnode, rewind_dir, descriptor->cookie);
	}

	return B_UNSUPPORTED;
}


/**
 * @brief Removes a directory identified by (fd, path).
 *
 * Rejects paths ending in ".", "..", or containing only slashes.  Resolves
 * the parent directory and calls the FS remove_dir() hook.
 *
 * @param fd     Base FD for relative paths (or AT_FDCWD).
 * @param path   Path of the directory to remove.
 * @param kernel true to use the kernel I/O context.
 * @retval B_OK             Directory removed.
 * @retval B_NOT_ALLOWED    Path ends in ".." or ".".
 * @retval B_READ_ONLY_DEVICE FS has no remove_dir hook.
 */
static status_t
dir_remove(int fd, char* path, bool kernel)
{
	char name[B_FILE_NAME_LENGTH];
	status_t status;

	if (path != NULL) {
		// we need to make sure our path name doesn't stop with "/", ".",
		// or ".."
		char* lastSlash;
		while ((lastSlash = strrchr(path, '/')) != NULL) {
			char* leaf = lastSlash + 1;
			if (!strcmp(leaf, ".."))
				return B_NOT_ALLOWED;

			// omit multiple slashes
			while (lastSlash > path && lastSlash[-1] == '/')
				lastSlash--;

			if (leaf[0]
				&& strcmp(leaf, ".")) {
				break;
			}
			// "name/" -> "name", or "name/." -> "name"
			lastSlash[0] = '\0';
		}

		if (!strcmp(path, ".") || !strcmp(path, ".."))
			return B_NOT_ALLOWED;
	}

	VnodePutter directory;
	status = fd_and_path_to_dir_vnode(fd, path, directory, name, kernel);
	if (status != B_OK)
		return status;

	if (HAS_FS_CALL(directory, remove_dir))
		status = FS_CALL(directory.Get(), remove_dir, name);
	else
		status = B_READ_ONLY_DEVICE;

	return status;
}


/**
 * @brief fd_ops ioctl handler — dispatches device-control requests to the FS.
 *
 * @param descriptor The file descriptor.
 * @param op         The ioctl operation code.
 * @param buffer     Input/output buffer for the operation.
 * @param length     Size of @p buffer.
 * @retval B_DEV_INVALID_IOCTL FS has no ioctl hook.
 */
static status_t
common_ioctl(struct file_descriptor* descriptor, ulong op, void* buffer,
	size_t length)
{
	struct vnode* vnode = descriptor->u.vnode;

	if (HAS_FS_CALL(vnode, ioctl))
		return FS_CALL(vnode, ioctl, descriptor->cookie, op, buffer, length);

	return B_DEV_INVALID_IOCTL;
}


/**
 * @brief Implements the fcntl() system call for both kernel and user callers.
 *
 * Handles F_GETFD/F_SETFD (close-on-exec/fork flags), F_GETFL/F_SETFL
 * (open-mode flags), F_DUPFD/F_DUPFD_CLOEXEC/F_DUPFD_CLOFORK (FD duplication),
 * and F_GETLK/F_SETLK/F_SETLKW (POSIX advisory record locks).
 *
 * @param fd       The file descriptor to operate on.
 * @param op       The F_* fcntl command.
 * @param argument Command-specific argument (flag value, new FD base, or
 *                 pointer to a struct flock).
 * @param kernel   true to use the kernel I/O context.
 * @retval B_OK        Operation succeeded.
 * @retval B_FILE_ERROR @p fd is invalid.
 * @retval B_BAD_VALUE  Unsupported command or invalid arguments.
 */
static status_t
common_fcntl(int fd, int op, size_t argument, bool kernel)
{
	struct flock flock;

	FUNCTION(("common_fcntl(fd = %d, op = %d, argument = %lx, %s)\n",
		fd, op, argument, kernel ? "kernel" : "user"));

	struct io_context* context = get_current_io_context(kernel);

	FileDescriptorPutter descriptor(get_fd(context, fd));
	if (!descriptor.IsSet())
		return B_FILE_ERROR;

	struct vnode* vnode = fd_vnode(descriptor.Get());

	status_t status = B_OK;

	if (op == F_SETLK || op == F_SETLKW || op == F_GETLK) {
		if (descriptor->ops != &sFileOps)
			status = B_BAD_VALUE;
		else if (kernel)
			memcpy(&flock, (struct flock*)argument, sizeof(struct flock));
		else if (user_memcpy(&flock, (struct flock*)argument,
				sizeof(struct flock)) != B_OK)
			status = B_BAD_ADDRESS;
		if (status != B_OK)
			return status;
	}

	switch (op) {
		case F_SETFD:
		{
			// Set file descriptor flags

			// O_CLOEXEC and O_CLOFORK are the only flags available at this time
			rw_lock_write_lock(&context->lock);
			fd_set_close_on_exec(context, fd, (argument & FD_CLOEXEC) != 0);
			fd_set_close_on_fork(context, fd, (argument & FD_CLOFORK) != 0);
			rw_lock_write_unlock(&context->lock);

			status = B_OK;
			break;
		}

		case F_GETFD:
		{
			// Get file descriptor flags
			rw_lock_read_lock(&context->lock);
			status = fd_close_on_exec(context, fd) ? FD_CLOEXEC : 0;
			status |= fd_close_on_fork(context, fd) ? FD_CLOFORK : 0;
			rw_lock_read_unlock(&context->lock);
			break;
		}

		case F_SETFL:
		{
			// Set file descriptor open mode

			// we only accept changes to certain flags
			const int32 modifiableFlags = O_APPEND | O_NONBLOCK;
			argument &= modifiableFlags;

			if (descriptor->ops->fd_set_flags != NULL) {
				status = descriptor->ops->fd_set_flags(descriptor.Get(), argument);
			} else if (vnode != NULL && HAS_FS_CALL(vnode, set_flags)) {
				status = FS_CALL(vnode, set_flags, descriptor->cookie,
					(int)argument);
			} else
				status = B_UNSUPPORTED;

			if (status == B_OK) {
				// update this descriptor's open_mode field
				descriptor->open_mode = (descriptor->open_mode
					& ~modifiableFlags) | argument;
			}

			break;
		}

		case F_GETFL:
			// Get file descriptor open mode
			status = descriptor->open_mode;
			break;

		case F_DUPFD:
		case F_DUPFD_CLOEXEC:
		case F_DUPFD_CLOFORK:
		{
			status = new_fd_etc(context, descriptor.Get(), (int)argument);
			if (status >= 0) {
				rw_lock_write_lock(&context->lock);
				if (op == F_DUPFD_CLOEXEC)
					fd_set_close_on_exec(context, status, true);
				else if (op == F_DUPFD_CLOFORK)
					fd_set_close_on_fork(context, status, true);
				rw_lock_write_unlock(&context->lock);

				atomic_add(&descriptor->ref_count, 1);
			}
			break;
		}

		case F_GETLK:
			if (vnode != NULL) {
				struct flock normalizedLock;

				memcpy(&normalizedLock, &flock, sizeof(struct flock));
				status = normalize_flock(descriptor.Get(), &normalizedLock);
				if (status != B_OK)
					break;

				if (HAS_FS_CALL(vnode, test_lock)) {
					status = FS_CALL(vnode, test_lock, descriptor->cookie,
						&normalizedLock);
				} else
					status = test_advisory_lock(vnode, &normalizedLock);
				if (status == B_OK) {
					if (normalizedLock.l_type == F_UNLCK) {
						// no conflicting lock found, copy back the same struct
						// we were given except change type to F_UNLCK
						flock.l_type = F_UNLCK;
						if (kernel) {
							memcpy((struct flock*)argument, &flock,
								sizeof(struct flock));
						} else {
							status = user_memcpy((struct flock*)argument,
								&flock, sizeof(struct flock));
						}
					} else {
						// a conflicting lock was found, copy back its range and
						// type
						if (normalizedLock.l_len == OFF_MAX)
							normalizedLock.l_len = 0;

						if (kernel) {
							memcpy((struct flock*)argument,
								&normalizedLock, sizeof(struct flock));
						} else {
							status = user_memcpy((struct flock*)argument,
								&normalizedLock, sizeof(struct flock));
						}
					}
				}
			} else
				status = B_BAD_VALUE;
			break;

		case F_SETLK:
		case F_SETLKW:
			status = normalize_flock(descriptor.Get(), &flock);
			if (status != B_OK)
				break;

			if (vnode == NULL) {
				status = B_BAD_VALUE;
			} else if (flock.l_type == F_UNLCK) {
				if (HAS_FS_CALL(vnode, release_lock)) {
					status = FS_CALL(vnode, release_lock, descriptor->cookie,
						&flock);
				} else {
					status = release_advisory_lock(vnode, context, NULL,
						&flock);
				}
			} else {
				// the open mode must match the lock type
				if (((descriptor->open_mode & O_RWMASK) == O_RDONLY
						&& flock.l_type == F_WRLCK)
					|| ((descriptor->open_mode & O_RWMASK) == O_WRONLY
						&& flock.l_type == F_RDLCK))
					status = B_FILE_ERROR;
				else {
					if (HAS_FS_CALL(vnode, acquire_lock)) {
						status = FS_CALL(vnode, acquire_lock,
							descriptor->cookie, &flock, op == F_SETLKW);
					} else {
						status = acquire_advisory_lock(vnode, context, NULL,
							&flock, op == F_SETLKW);
					}
				}
			}
			break;

		// ToDo: add support for more ops?

		default:
			status = B_BAD_VALUE;
	}

	return status;
}


/**
 * @brief Flushes cached data and/or metadata for a file descriptor to storage.
 *
 * @param fd       The file descriptor to sync.
 * @param dataOnly If true, only data is flushed (fdatasync semantics).
 * @param kernel   true to use the kernel I/O context.
 * @retval B_OK         Sync completed.
 * @retval B_FILE_ERROR @p fd is invalid.
 * @retval B_UNSUPPORTED FS has no fsync hook.
 */
static status_t
common_sync(int fd, bool dataOnly, bool kernel)
{
	FUNCTION(("common_sync: entry. fd %d kernel %d, data only %d\n", fd, kernel, dataOnly));

	struct vnode* vnode;
	FileDescriptorPutter descriptor(get_fd_and_vnode(fd, &vnode, kernel));
	if (!descriptor.IsSet())
		return B_FILE_ERROR;

	status_t status;
	if (HAS_FS_CALL(vnode, fsync))
		status = FS_CALL(vnode, fsync, dataOnly);
	else
		status = B_UNSUPPORTED;

	return status;
}


/**
 * @brief Sets a mandatory lock on the vnode backing a file descriptor.
 *
 * Uses an atomic compare-and-swap so that only one FD can hold the lock.
 *
 * @param fd     The file descriptor that will hold the lock.
 * @param kernel true to use the kernel I/O context.
 * @retval B_OK   Lock acquired.
 * @retval B_BUSY Another descriptor already holds the mandatory lock.
 * @retval B_FILE_ERROR @p fd is invalid.
 */
static status_t
common_lock_node(int fd, bool kernel)
{
	struct vnode* vnode;
	FileDescriptorPutter descriptor(get_fd_and_vnode(fd, &vnode, kernel));
	if (!descriptor.IsSet())
		return B_FILE_ERROR;

	status_t status = B_OK;

	// We need to set the locking atomically - someone
	// else might set one at the same time
	if (atomic_pointer_test_and_set(&vnode->mandatory_locked_by,
			descriptor.Get(), (file_descriptor*)NULL) != NULL)
		status = B_BUSY;

	return status;
}


/**
 * @brief Releases the mandatory lock on the vnode backing a file descriptor.
 *
 * Uses an atomic compare-and-swap to ensure only the holder can release it.
 *
 * @param fd     The file descriptor that holds the lock.
 * @param kernel true to use the kernel I/O context.
 * @retval B_OK        Lock released.
 * @retval B_BAD_VALUE The descriptor does not hold the mandatory lock.
 * @retval B_FILE_ERROR @p fd is invalid.
 */
static status_t
common_unlock_node(int fd, bool kernel)
{
	struct vnode* vnode;
	FileDescriptorPutter descriptor(get_fd_and_vnode(fd, &vnode, kernel));
	if (!descriptor.IsSet())
		return B_FILE_ERROR;

	status_t status = B_OK;

	// We need to set the locking atomically - someone
	// else might set one at the same time
	if (atomic_pointer_test_and_set(&vnode->mandatory_locked_by,
			(file_descriptor*)NULL, descriptor.Get()) != descriptor.Get())
		status = B_BAD_VALUE;

	return status;
}


/**
 * @brief Pre-allocates disk space for a regular file.
 *
 * Delegates to the FS preallocate() hook.  Not supported for non-regular
 * files (pipes, sockets, block/char devices, directories, symlinks).
 *
 * @param fd     The file descriptor (must be open for writing).
 * @param offset Start byte offset for pre-allocation.
 * @param length Number of bytes to pre-allocate (must be > 0).
 * @param kernel true to use the kernel I/O context.
 * @retval B_OK          Space reserved.
 * @retval B_BAD_VALUE   @p offset < 0 or @p length == 0.
 * @retval B_FILE_TOO_LARGE  offset + length would overflow off_t.
 * @retval B_FILE_ERROR  @p fd is invalid or read-only.
 * @retval ESPIPE        @p fd refers to a pipe or socket.
 * @retval B_DEVICE_NOT_FOUND Non-regular file type.
 * @retval B_UNSUPPORTED FS has write but no preallocate hook.
 * @retval B_READ_ONLY_DEVICE FS has neither preallocate nor write hook.
 */
static status_t
common_preallocate(int fd, off_t offset, off_t length, bool kernel)
{
	if (offset < 0 || length == 0)
		return B_BAD_VALUE;
	if (offset > OFF_MAX - length)
		return B_FILE_TOO_LARGE;

	struct vnode* vnode;
	FileDescriptorPutter descriptor(get_fd_and_vnode(fd, &vnode, kernel));
	if (!descriptor.IsSet() || (descriptor->open_mode & O_RWMASK) == O_RDONLY)
		return B_FILE_ERROR;

	switch (vnode->Type() & S_IFMT) {
		case S_IFIFO:
		case S_IFSOCK:
			return ESPIPE;

		case S_IFBLK:
		case S_IFCHR:
		case S_IFDIR:
		case S_IFLNK:
			return B_DEVICE_NOT_FOUND;

		case S_IFREG:
			break;
	}

	status_t status = B_OK;
	if (HAS_FS_CALL(vnode, preallocate)) {
		status = FS_CALL(vnode, preallocate, offset, length);
	} else {
		status = HAS_FS_CALL(vnode, write)
			? B_UNSUPPORTED : B_READ_ONLY_DEVICE;
	}

	return status;
}


/**
 * @brief Reads the target of a symbolic link.
 *
 * @param fd          Base FD for relative paths (or AT_FDCWD).
 * @param path        Path to the symlink (leaf is NOT followed).
 * @param buffer      Output buffer for the link target.
 * @param _bufferSize In: buffer capacity; out: bytes written (without NUL).
 * @param kernel      true to use the kernel I/O context.
 * @retval B_OK         Link target read.
 * @retval B_BAD_VALUE  The vnode is not a symlink or has no read_symlink hook.
 */
static status_t
common_read_link(int fd, char* path, char* buffer, size_t* _bufferSize,
	bool kernel)
{
	VnodePutter vnode;
	status_t status;

	status = fd_and_path_to_vnode(fd, path, false, vnode, NULL, kernel);
	if (status != B_OK)
		return status;

	if (HAS_FS_CALL(vnode, read_symlink)) {
		status = FS_CALL(vnode.Get(), read_symlink, buffer, _bufferSize);
	} else
		status = B_BAD_VALUE;

	return status;
}


/**
 * @brief Creates a symbolic link.
 *
 * @param fd     Base FD for resolving @p path (or AT_FDCWD).
 * @param path   Path where the symlink will be created.
 * @param toPath Link target string (stored verbatim in the symlink).
 * @param mode   Permission bits (often ignored by FS).
 * @param kernel true to use the kernel I/O context.
 * @retval B_OK             Symlink created.
 * @retval B_UNSUPPORTED    FS supports write but no create_symlink.
 * @retval B_READ_ONLY_DEVICE FS has no write hook.
 */
static status_t
common_create_symlink(int fd, char* path, const char* toPath, int mode,
	bool kernel)
{
	// path validity checks have to be in the calling function!
	char name[B_FILE_NAME_LENGTH];
	status_t status;

	FUNCTION(("common_create_symlink(fd = %d, path = %s, toPath = %s, "
		"mode = %d, kernel = %d)\n", fd, path, toPath, mode, kernel));

	VnodePutter vnode;
	status = fd_and_path_to_dir_vnode(fd, path, vnode, name, kernel);
	if (status != B_OK)
		return status;

	if (HAS_FS_CALL(vnode, create_symlink))
		status = FS_CALL(vnode.Get(), create_symlink, name, toPath, mode);
	else {
		status = HAS_FS_CALL(vnode, write)
			? B_UNSUPPORTED : B_READ_ONLY_DEVICE;
	}

	return status;
}


/**
 * @brief Creates a hard link.
 *
 * Both the new entry and the target must reside on the same mount.
 *
 * @param pathFD          FD for the directory in which to create the link.
 * @param path            Path for the new hard link.
 * @param toFD            FD for the directory containing the link target.
 * @param toPath          Path of the existing node to link to.
 * @param traverseLeafLink If true, follow the final symlink in @p toPath.
 * @param kernel          true to use the kernel I/O context.
 * @retval B_OK                Link created.
 * @retval B_CROSS_DEVICE_LINK @p path and @p toPath are on different mounts.
 * @retval B_READ_ONLY_DEVICE  FS has no link hook.
 */
static status_t
common_create_link(int pathFD, char* path, int toFD, char* toPath,
	bool traverseLeafLink, bool kernel)
{
	// path validity checks have to be in the calling function!

	FUNCTION(("common_create_link(path = %s, toPath = %s, kernel = %d)\n", path,
		toPath, kernel));

	char name[B_FILE_NAME_LENGTH];
	VnodePutter directory;
	status_t status = fd_and_path_to_dir_vnode(pathFD, path, directory, name,
		kernel);
	if (status != B_OK)
		return status;

	VnodePutter vnode;
	status = fd_and_path_to_vnode(toFD, toPath, traverseLeafLink, vnode, NULL,
		kernel);
	if (status != B_OK)
		return status;

	if (directory->mount != vnode->mount)
		return B_CROSS_DEVICE_LINK;

	if (HAS_FS_CALL(directory, link))
		status = FS_CALL(directory.Get(), link, name, vnode.Get());
	else
		status = B_READ_ONLY_DEVICE;

	return status;
}


/**
 * @brief Removes a directory entry (file or symlink).
 *
 * @param fd     Base FD for relative paths (or AT_FDCWD).
 * @param path   Path of the entry to remove.
 * @param kernel true to use the kernel I/O context.
 * @retval B_OK             Entry removed.
 * @retval B_READ_ONLY_DEVICE FS has no unlink hook.
 */
static status_t
common_unlink(int fd, char* path, bool kernel)
{
	char filename[B_FILE_NAME_LENGTH];
	status_t status;

	FUNCTION(("common_unlink: fd: %d, path '%s', kernel %d\n", fd, path,
		kernel));

	VnodePutter vnode;
	status = fd_and_path_to_dir_vnode(fd, path, vnode, filename, kernel);
	if (status < 0)
		return status;

	if (HAS_FS_CALL(vnode, unlink))
		status = FS_CALL(vnode.Get(), unlink, filename);
	else
		status = B_READ_ONLY_DEVICE;

	return status;
}


/**
 * @brief Checks whether the calling process has the specified access to a path.
 *
 * Delegates to the FS access() hook if available, otherwise returns B_OK.
 *
 * @param fd                Base FD for relative @p path.
 * @param path              Path to check.
 * @param mode              Access mode bits (R_OK, W_OK, X_OK, F_OK).
 * @param effectiveUserGroup If true use effective UID/GID (currently unused).
 * @param kernel            true to use the kernel I/O context.
 * @retval B_OK               Access permitted.
 * @retval B_PERMISSION_DENIED Access denied.
 */
static status_t
common_access(int fd, char* path, int mode, bool effectiveUserGroup, bool kernel)
{
	status_t status;

	// TODO: honor effectiveUserGroup argument

	VnodePutter vnode;
	status = fd_and_path_to_vnode(fd, path, true, vnode, NULL, kernel);
	if (status != B_OK)
		return status;

	if (HAS_FS_CALL(vnode, access))
		status = FS_CALL(vnode.Get(), access, mode);
	else
		status = B_OK;

	return status;
}


/**
 * @brief Renames a directory entry (both must be on the same device).
 *
 * @param fd      FD for the source directory.
 * @param path    Source path.
 * @param newFD   FD for the destination directory.
 * @param newPath Destination path.
 * @param kernel  true to use the kernel I/O context.
 * @retval B_OK                Rename succeeded.
 * @retval B_CROSS_DEVICE_LINK Source and destination are on different mounts.
 * @retval B_READ_ONLY_DEVICE  FS has no rename hook.
 */
static status_t
common_rename(int fd, char* path, int newFD, char* newPath, bool kernel)
{
	status_t status;

	FUNCTION(("common_rename(fd = %d, path = %s, newFD = %d, newPath = %s, "
		"kernel = %d)\n", fd, path, newFD, newPath, kernel));

	VnodePutter fromVnode;
	char fromName[B_FILE_NAME_LENGTH];
	status = fd_and_path_to_dir_vnode(fd, path, fromVnode, fromName, kernel);
	if (status != B_OK)
		return status;

	VnodePutter toVnode;
	char toName[B_FILE_NAME_LENGTH];
	status = fd_and_path_to_dir_vnode(newFD, newPath, toVnode, toName, kernel);
	if (status != B_OK)
		return status;

	if (fromVnode->device != toVnode->device)
		return B_CROSS_DEVICE_LINK;

	if (fromVnode.Get() == toVnode.Get() && !strcmp(fromName, toName))
		return B_OK;

	if (fromName[0] == '\0' || toName[0] == '\0'
		|| !strcmp(fromName, ".") || !strcmp(fromName, "..")
		|| !strcmp(toName, ".") || !strcmp(toName, "..")) {
		return B_BAD_VALUE;
	}

	if (HAS_FS_CALL(fromVnode, rename))
		status = FS_CALL(fromVnode.Get(), rename, fromName, toVnode.Get(), toName);
	else
		status = B_READ_ONLY_DEVICE;

	return status;
}


/**
 * @brief fd_ops read_stat handler — reads stat for an open file descriptor.
 *
 * Zeroes the nanosecond fields before calling vfs_stat_vnode() so that file
 * systems that don't initialize them still return a defined value.
 *
 * @param descriptor The open file descriptor.
 * @param stat       Output stat buffer.
 * @retval B_OK Stat filled.
 */
static status_t
common_read_stat(struct file_descriptor* descriptor, struct stat* stat)
{
	struct vnode* vnode = descriptor->u.vnode;

	FUNCTION(("common_read_stat: stat %p\n", stat));

	// TODO: remove this once all file systems properly set them!
	stat->st_crtim.tv_nsec = 0;
	stat->st_ctim.tv_nsec = 0;
	stat->st_mtim.tv_nsec = 0;
	stat->st_atim.tv_nsec = 0;

	return vfs_stat_vnode(vnode, stat);
}


/**
 * @brief fd_ops write_stat handler — updates stat fields for an open FD.
 *
 * Rejects B_STAT_SIZE changes on read-only FDs.
 *
 * @param descriptor The open file descriptor.
 * @param stat       New stat values.
 * @param statMask   B_STAT_* bitmask of fields to update.
 * @retval B_OK              Stat updated.
 * @retval B_BAD_VALUE       B_STAT_SIZE requested on a read-only FD.
 * @retval B_READ_ONLY_DEVICE FS has no write_stat hook.
 */
static status_t
common_write_stat(struct file_descriptor* descriptor, const struct stat* stat,
	int statMask)
{
	struct vnode* vnode = descriptor->u.vnode;

	FUNCTION(("common_write_stat(vnode = %p, stat = %p, statMask = %d)\n",
		vnode, stat, statMask));

	if ((descriptor->open_mode & O_RWMASK) == O_RDONLY
		&& (statMask & B_STAT_SIZE) != 0) {
		return B_BAD_VALUE;
	}

	if (!HAS_FS_CALL(vnode, write_stat))
		return B_READ_ONLY_DEVICE;

	return FS_CALL(vnode, write_stat, stat, statMask);
}


/**
 * @brief Reads stat for a node identified by (fd, path).
 *
 * @param fd               Base FD for relative paths (or AT_FDCWD).
 * @param path             Path to the node.
 * @param traverseLeafLink If true, follow a final symlink.
 * @param stat             Output stat buffer.
 * @param kernel           true to use the kernel I/O context.
 * @retval B_OK Stat filled.
 */
static status_t
common_path_read_stat(int fd, char* path, bool traverseLeafLink,
	struct stat* stat, bool kernel)
{
	FUNCTION(("common_path_read_stat: fd: %d, path '%s', stat %p,\n", fd, path,
		stat));

	VnodePutter vnode;
	status_t status = fd_and_path_to_vnode(fd, path, traverseLeafLink, vnode,
		NULL, kernel);
	if (status != B_OK)
		return status;

	status = vfs_stat_vnode(vnode.Get(), stat);

	return status;
}


/**
 * @brief Updates stat fields for a node identified by (fd, path).
 *
 * @param fd               Base FD for relative paths (or AT_FDCWD).
 * @param path             Path to the node.
 * @param traverseLeafLink If true, follow a final symlink.
 * @param stat             New stat values.
 * @param statMask         B_STAT_* bitmask of fields to update.
 * @param kernel           true to use the kernel I/O context.
 * @retval B_OK              Stat updated.
 * @retval B_READ_ONLY_DEVICE FS has no write_stat hook.
 */
static status_t
common_path_write_stat(int fd, char* path, bool traverseLeafLink,
	const struct stat* stat, int statMask, bool kernel)
{
	FUNCTION(("common_write_stat: fd: %d, path '%s', stat %p, stat_mask %d, "
		"kernel %d\n", fd, path, stat, statMask, kernel));

	VnodePutter vnode;
	status_t status = fd_and_path_to_vnode(fd, path, traverseLeafLink, vnode,
		NULL, kernel);
	if (status != B_OK)
		return status;

	if (HAS_FS_CALL(vnode, write_stat))
		status = FS_CALL(vnode.Get(), write_stat, stat, statMask);
	else
		status = B_READ_ONLY_DEVICE;

	return status;
}


/**
 * @brief Opens the attribute directory of a node identified by (fd, path).
 *
 * @param fd               Base FD for relative paths.
 * @param path             Path to the node.
 * @param traverseLeafLink If true, follow a final symlink.
 * @param kernel           true to use the kernel I/O context.
 * @return A non-negative FD on success, or a negative error code.
 */
static int
attr_dir_open(int fd, char* path, bool traverseLeafLink, bool kernel)
{
	FUNCTION(("attr_dir_open(fd = %d, path = '%s', kernel = %d)\n", fd, path,
		kernel));

	VnodePutter vnode;
	status_t status = fd_and_path_to_vnode(fd, path, traverseLeafLink, vnode,
		NULL, kernel);
	if (status != B_OK)
		return status;

	status = open_attr_dir_vnode(vnode.Get(), kernel);
	if (status >= 0)
		vnode.Detach();

	return status;
}


/**
 * @brief fd_ops close handler for attribute directory file descriptors.
 *
 * @param descriptor The attribute directory FD.
 * @retval B_OK Always.
 */
static status_t
attr_dir_close(struct file_descriptor* descriptor)
{
	struct vnode* vnode = descriptor->u.vnode;

	FUNCTION(("attr_dir_close(descriptor = %p)\n", descriptor));

	if (HAS_FS_CALL(vnode, close_attr_dir))
		return FS_CALL(vnode, close_attr_dir, descriptor->cookie);

	return B_OK;
}


/**
 * @brief fd_ops free_fd handler for attribute directory file descriptors.
 *
 * Calls free_attr_dir_cookie() and drops the vnode reference.
 *
 * @param descriptor The attribute directory FD being freed.
 */
static void
attr_dir_free_fd(struct file_descriptor* descriptor)
{
	struct vnode* vnode = descriptor->u.vnode;

	if (vnode != NULL) {
		FS_CALL(vnode, free_attr_dir_cookie, descriptor->cookie);
		put_vnode(vnode);
	}
}


/**
 * @brief fd_ops read_dir handler for attribute directory FDs.
 *
 * @param ioContext  Caller's I/O context.
 * @param descriptor The attribute directory FD.
 * @param buffer     Output buffer for dirent structures.
 * @param bufferSize Size of @p buffer.
 * @param _count     In: max entries; out: entries read.
 * @retval B_UNSUPPORTED FS has no read_attr_dir hook.
 */
static status_t
attr_dir_read(struct io_context* ioContext, struct file_descriptor* descriptor,
	struct dirent* buffer, size_t bufferSize, uint32* _count)
{
	struct vnode* vnode = descriptor->u.vnode;

	FUNCTION(("attr_dir_read(descriptor = %p)\n", descriptor));

	if (HAS_FS_CALL(vnode, read_attr_dir))
		return FS_CALL(vnode, read_attr_dir, descriptor->cookie, buffer,
			bufferSize, _count);

	return B_UNSUPPORTED;
}


/**
 * @brief fd_ops rewind_dir handler for attribute directory FDs.
 *
 * @param descriptor The attribute directory FD.
 * @retval B_UNSUPPORTED FS has no rewind_attr_dir hook.
 */
static status_t
attr_dir_rewind(struct file_descriptor* descriptor)
{
	struct vnode* vnode = descriptor->u.vnode;

	FUNCTION(("attr_dir_rewind(descriptor = %p)\n", descriptor));

	if (HAS_FS_CALL(vnode, rewind_attr_dir))
		return FS_CALL(vnode, rewind_attr_dir, descriptor->cookie);

	return B_UNSUPPORTED;
}


/**
 * @brief Creates a new extended attribute on a node.
 *
 * @param fd      Base FD for resolving @p path.
 * @param path    Path to the node that will own the attribute.
 * @param name    Name of the attribute to create.
 * @param type    Attribute type code (B_STRING_TYPE etc.).
 * @param openMode Open flags.
 * @param kernel  true to use the kernel I/O context.
 * @return A non-negative attribute FD, or a negative error code.
 * @retval B_BAD_VALUE @p name is NULL or empty.
 * @retval B_READ_ONLY_DEVICE FS has no create_attr hook.
 */
static int
attr_create(int fd, char* path, const char* name, uint32 type,
	int openMode, bool kernel)
{
	if (name == NULL || *name == '\0')
		return B_BAD_VALUE;

	bool traverse = (openMode & (O_NOTRAVERSE | O_NOFOLLOW)) == 0;
	VnodePutter vnode;
	status_t status = fd_and_path_to_vnode(fd, path, traverse, vnode, NULL,
		kernel);
	if (status != B_OK)
		return status;

	status = check_open_mode(vnode.Get(), openMode);
	if (status != B_OK)
		return status;

	if (!HAS_FS_CALL(vnode, create_attr))
		return B_READ_ONLY_DEVICE;

	void* cookie;
	status = FS_CALL(vnode.Get(), create_attr, name, type, openMode, &cookie);
	if (status != B_OK)
		return status;

	fd = get_new_fd(&sAttributeOps, NULL, vnode.Get(), cookie, openMode, kernel);
	if (fd >= 0) {
		vnode.Detach();
		return fd;
	}

	status = fd;

	FS_CALL(vnode.Get(), close_attr, cookie);
	FS_CALL(vnode.Get(), free_attr_cookie, cookie);

	FS_CALL(vnode.Get(), remove_attr, name);

	return status;
}


/**
 * @brief Opens an existing extended attribute on a node.
 *
 * @param fd      Base FD for resolving @p path.
 * @param path    Path to the node.
 * @param name    Attribute name.
 * @param openMode Open flags.
 * @param kernel  true to use the kernel I/O context.
 * @return A non-negative attribute FD, or a negative error code.
 * @retval B_BAD_VALUE    @p name is NULL or empty.
 * @retval B_UNSUPPORTED  FS has no open_attr hook.
 */
static int
attr_open(int fd, char* path, const char* name, int openMode, bool kernel)
{
	if (name == NULL || *name == '\0')
		return B_BAD_VALUE;

	bool traverse = (openMode & (O_NOTRAVERSE | O_NOFOLLOW)) == 0;
	VnodePutter vnode;
	status_t status = fd_and_path_to_vnode(fd, path, traverse, vnode, NULL,
		kernel);
	if (status != B_OK)
		return status;

	status = check_open_mode(vnode.Get(), openMode);
	if (status != B_OK)
		return status;

	if (!HAS_FS_CALL(vnode, open_attr))
		return B_UNSUPPORTED;

	void* cookie;
	status = FS_CALL(vnode.Get(), open_attr, name, openMode, &cookie);
	if (status != B_OK)
		return status;

	// now we only need a file descriptor for this attribute and we're done
	fd = get_new_fd(&sAttributeOps, NULL, vnode.Get(), cookie, openMode, kernel);
	if (fd >= 0) {
		vnode.Detach();
		return fd;
	}

	status = fd;

	FS_CALL(vnode.Get(), close_attr, cookie);
	FS_CALL(vnode.Get(), free_attr_cookie, cookie);

	return status;
}


/**
 * @brief fd_ops close handler for attribute file descriptors.
 *
 * @param descriptor The attribute FD.
 * @retval B_OK Always.
 */
static status_t
attr_close(struct file_descriptor* descriptor)
{
	struct vnode* vnode = descriptor->u.vnode;

	FUNCTION(("attr_close(descriptor = %p)\n", descriptor));

	if (HAS_FS_CALL(vnode, close_attr))
		return FS_CALL(vnode, close_attr, descriptor->cookie);

	return B_OK;
}


/**
 * @brief fd_ops free_fd handler for attribute file descriptors.
 *
 * @param descriptor The attribute FD being freed.
 */
static void
attr_free_fd(struct file_descriptor* descriptor)
{
	struct vnode* vnode = descriptor->u.vnode;

	if (vnode != NULL) {
		FS_CALL(vnode, free_attr_cookie, descriptor->cookie);
		put_vnode(vnode);
	}
}


/**
 * @brief fd_ops read handler for attribute file descriptors.
 *
 * @param descriptor The attribute FD.
 * @param pos        Byte offset within the attribute.
 * @param buffer     Destination buffer.
 * @param length     In: bytes to read; out: bytes read.
 * @retval B_UNSUPPORTED FS has no read_attr hook.
 */
static status_t
attr_read(struct file_descriptor* descriptor, off_t pos, void* buffer,
	size_t* length)
{
	struct vnode* vnode = descriptor->u.vnode;

	FUNCTION(("attr_read: buf %p, pos %" B_PRIdOFF ", len %p = %ld\n", buffer,
		pos, length, *length));

	if (!HAS_FS_CALL(vnode, read_attr))
		return B_UNSUPPORTED;

	return FS_CALL(vnode, read_attr, descriptor->cookie, pos, buffer, length);
}


/**
 * @brief fd_ops write handler for attribute file descriptors.
 *
 * @param descriptor The attribute FD.
 * @param pos        Byte offset within the attribute.
 * @param buffer     Source buffer.
 * @param length     In: bytes to write; out: bytes written.
 * @retval B_UNSUPPORTED FS has no write_attr hook.
 */
static status_t
attr_write(struct file_descriptor* descriptor, off_t pos, const void* buffer,
	size_t* length)
{
	struct vnode* vnode = descriptor->u.vnode;

	FUNCTION(("attr_write: buf %p, pos %" B_PRIdOFF ", len %p\n", buffer, pos,
		length));

	if (!HAS_FS_CALL(vnode, write_attr))
		return B_UNSUPPORTED;

	return FS_CALL(vnode, write_attr, descriptor->cookie, pos, buffer, length);
}


/**
 * @brief fd_ops seek handler for attribute file descriptors.
 *
 * Supports SEEK_SET, SEEK_CUR, SEEK_END (requires read_attr_stat).
 *
 * @param descriptor The attribute FD.
 * @param pos        Seek offset.
 * @param seekType   SEEK_SET, SEEK_CUR, or SEEK_END.
 * @return New position on success, or a negative error code.
 */
static off_t
attr_seek(struct file_descriptor* descriptor, off_t pos, int seekType)
{
	off_t offset;

	switch (seekType) {
		case SEEK_SET:
			offset = 0;
			break;
		case SEEK_CUR:
			offset = descriptor->pos;
			break;
		case SEEK_END:
		{
			struct vnode* vnode = descriptor->u.vnode;
			if (!HAS_FS_CALL(vnode, read_stat))
				return B_UNSUPPORTED;

			struct stat stat;
			status_t status = FS_CALL(vnode, read_attr_stat, descriptor->cookie,
				&stat);
			if (status != B_OK)
				return status;

			offset = stat.st_size;
			break;
		}
		default:
			return B_BAD_VALUE;
	}

	// assumes off_t is 64 bits wide
	if (offset > 0 && LONGLONG_MAX - offset < pos)
		return B_BUFFER_OVERFLOW;

	pos += offset;
	if (pos < 0)
		return B_BAD_VALUE;

	return descriptor->pos = pos;
}


/**
 * @brief fd_ops read_stat handler for attribute file descriptors.
 *
 * @param descriptor The attribute FD.
 * @param stat       Output stat buffer.
 * @retval B_UNSUPPORTED FS has no read_attr_stat hook.
 */
static status_t
attr_read_stat(struct file_descriptor* descriptor, struct stat* stat)
{
	struct vnode* vnode = descriptor->u.vnode;

	FUNCTION(("attr_read_stat: stat 0x%p\n", stat));

	if (!HAS_FS_CALL(vnode, read_attr_stat))
		return B_UNSUPPORTED;

	return FS_CALL(vnode, read_attr_stat, descriptor->cookie, stat);
}


/**
 * @brief fd_ops write_stat handler for attribute file descriptors.
 *
 * @param descriptor The attribute FD.
 * @param stat       New stat values.
 * @param statMask   B_STAT_* bitmask of fields to update.
 * @retval B_READ_ONLY_DEVICE FS has no write_attr_stat hook.
 */
static status_t
attr_write_stat(struct file_descriptor* descriptor, const struct stat* stat,
	int statMask)
{
	struct vnode* vnode = descriptor->u.vnode;

	FUNCTION(("attr_write_stat: stat = %p, statMask %d\n", stat, statMask));

	if (!HAS_FS_CALL(vnode, write_attr_stat))
		return B_READ_ONLY_DEVICE;

	return FS_CALL(vnode, write_attr_stat, descriptor->cookie, stat, statMask);
}


/**
 * @brief Removes a named extended attribute from a node.
 *
 * @param fd     File descriptor for the node owning the attribute.
 * @param name   Attribute name to remove.
 * @param kernel true to use the kernel I/O context.
 * @retval B_OK        Attribute removed.
 * @retval B_BAD_VALUE @p name is NULL or empty.
 * @retval B_FILE_ERROR @p fd is invalid.
 * @retval B_READ_ONLY_DEVICE FS has no remove_attr hook.
 */
static status_t
attr_remove(int fd, const char* name, bool kernel)
{
	if (name == NULL || *name == '\0')
		return B_BAD_VALUE;

	FUNCTION(("attr_remove: fd = %d, name = \"%s\", kernel %d\n", fd, name,
		kernel));

	struct vnode* vnode;
	FileDescriptorPutter descriptor(get_fd_and_vnode(fd, &vnode, kernel));
	if (!descriptor.IsSet())
		return B_FILE_ERROR;

	status_t status;
	if (HAS_FS_CALL(vnode, remove_attr))
		status = FS_CALL(vnode, remove_attr, name);
	else
		status = B_READ_ONLY_DEVICE;

	return status;
}


/**
 * @brief Renames an extended attribute (possibly across nodes on the same mount).
 *
 * @param fromFD   FD of the source node.
 * @param fromName Source attribute name.
 * @param toFD     FD of the destination node.
 * @param toName   Destination attribute name.
 * @param kernel   true to use the kernel I/O context.
 * @retval B_OK                Attribute renamed.
 * @retval B_BAD_VALUE         Any name pointer is NULL or empty.
 * @retval B_FILE_ERROR        Either FD is invalid.
 * @retval B_CROSS_DEVICE_LINK Nodes are on different mounts.
 * @retval B_READ_ONLY_DEVICE  FS has no rename_attr hook.
 */
static status_t
attr_rename(int fromFD, const char* fromName, int toFD, const char* toName,
	bool kernel)
{
	if (fromName == NULL || *fromName == '\0' || toName == NULL
		|| *toName == '\0')
		return B_BAD_VALUE;

	FUNCTION(("attr_rename: from fd = %d, from name = \"%s\", to fd = %d, to "
		"name = \"%s\", kernel %d\n", fromFD, fromName, toFD, toName, kernel));

	struct vnode* fromVnode;
	FileDescriptorPutter fromDescriptor(get_fd_and_vnode(fromFD, &fromVnode, kernel));
	if (!fromDescriptor.IsSet())
		return B_FILE_ERROR;

	struct vnode* toVnode;
	FileDescriptorPutter toDescriptor(get_fd_and_vnode(toFD, &toVnode, kernel));
	if (!toDescriptor.IsSet())
		return B_FILE_ERROR;

	// are the files on the same volume?
	if (fromVnode->device != toVnode->device)
		return B_CROSS_DEVICE_LINK;

	status_t status;
	if (HAS_FS_CALL(fromVnode, rename_attr)) {
		status = FS_CALL(fromVnode, rename_attr, fromName, toVnode, toName);
	} else
		status = B_READ_ONLY_DEVICE;

	return status;
}


/**
 * @brief Opens the index directory of a volume and returns an FD.
 *
 * @param mountID Device ID of the volume.
 * @param kernel  true to use the kernel I/O context.
 * @return A non-negative FD, or a negative error code.
 * @retval B_UNSUPPORTED FS has no open_index_dir hook.
 */
static int
index_dir_open(dev_t mountID, bool kernel)
{
	struct fs_mount* mount;
	void* cookie;

	FUNCTION(("index_dir_open(mountID = %" B_PRId32 ", kernel = %d)\n", mountID,
		kernel));

	status_t status = get_mount(mountID, &mount);
	if (status != B_OK)
		return status;

	if (!HAS_FS_MOUNT_CALL(mount, open_index_dir)) {
		status = B_UNSUPPORTED;
		goto error;
	}

	status = FS_MOUNT_CALL(mount, open_index_dir, &cookie);
	if (status != B_OK)
		goto error;

	// get fd for the index directory
	int fd;
	fd = get_new_fd(&sIndexDirectoryOps, mount, NULL, cookie, O_CLOEXEC, kernel);
	if (fd >= 0)
		return fd;

	// something went wrong
	FS_MOUNT_CALL(mount, close_index_dir, cookie);
	FS_MOUNT_CALL(mount, free_index_dir_cookie, cookie);

	status = fd;

error:
	put_mount(mount);
	return status;
}


/**
 * @brief fd_ops close handler for index directory file descriptors.
 *
 * @param descriptor The index directory FD.
 * @retval B_OK Always.
 */
static status_t
index_dir_close(struct file_descriptor* descriptor)
{
	struct fs_mount* mount = descriptor->u.mount;

	FUNCTION(("index_dir_close(descriptor = %p)\n", descriptor));

	if (HAS_FS_MOUNT_CALL(mount, close_index_dir))
		return FS_MOUNT_CALL(mount, close_index_dir, descriptor->cookie);

	return B_OK;
}


/**
 * @brief fd_ops free_fd handler for index directory file descriptors.
 *
 * @param descriptor The index directory FD being freed.
 */
static void
index_dir_free_fd(struct file_descriptor* descriptor)
{
	struct fs_mount* mount = descriptor->u.mount;

	if (mount != NULL) {
		FS_MOUNT_CALL(mount, free_index_dir_cookie, descriptor->cookie);
		put_mount(mount);
	}
}


/**
 * @brief fd_ops read_dir handler for index directory file descriptors.
 *
 * @param ioContext  Caller's I/O context (unused here).
 * @param descriptor The index directory FD.
 * @param buffer     Output buffer for dirent structures.
 * @param bufferSize Size of @p buffer.
 * @param _count     In: max entries; out: entries read.
 * @retval B_UNSUPPORTED FS has no read_index_dir hook.
 */
static status_t
index_dir_read(struct io_context* ioContext, struct file_descriptor* descriptor,
	struct dirent* buffer, size_t bufferSize, uint32* _count)
{
	struct fs_mount* mount = descriptor->u.mount;

	if (HAS_FS_MOUNT_CALL(mount, read_index_dir)) {
		return FS_MOUNT_CALL(mount, read_index_dir, descriptor->cookie, buffer,
			bufferSize, _count);
	}

	return B_UNSUPPORTED;
}


/**
 * @brief fd_ops rewind_dir handler for index directory file descriptors.
 *
 * @param descriptor The index directory FD.
 * @retval B_UNSUPPORTED FS has no rewind_index_dir hook.
 */
static status_t
index_dir_rewind(struct file_descriptor* descriptor)
{
	struct fs_mount* mount = descriptor->u.mount;

	if (HAS_FS_MOUNT_CALL(mount, rewind_index_dir))
		return FS_MOUNT_CALL(mount, rewind_index_dir, descriptor->cookie);

	return B_UNSUPPORTED;
}


/**
 * @brief Creates a new file-system index on a volume.
 *
 * @param mountID Device ID of the volume.
 * @param name    Index name (e.g. "name", "size").
 * @param type    Index type (B_INT32_TYPE, B_STRING_TYPE, etc.).
 * @param flags   FS-specific flags.
 * @param kernel  Currently unused.
 * @retval B_OK              Index created.
 * @retval B_READ_ONLY_DEVICE FS has no create_index hook.
 */
static status_t
index_create(dev_t mountID, const char* name, uint32 type, uint32 flags,
	bool kernel)
{
	FUNCTION(("index_create(mountID = %" B_PRId32 ", name = %s, kernel = %d)\n",
		mountID, name, kernel));

	struct fs_mount* mount;
	status_t status = get_mount(mountID, &mount);
	if (status != B_OK)
		return status;

	if (!HAS_FS_MOUNT_CALL(mount, create_index)) {
		status = B_READ_ONLY_DEVICE;
		goto out;
	}

	status = FS_MOUNT_CALL(mount, create_index, name, type, flags);

out:
	put_mount(mount);
	return status;
}


#if 0
static status_t
index_read_stat(struct file_descriptor* descriptor, struct stat* stat)
{
	struct vnode* vnode = descriptor->u.vnode;

	// ToDo: currently unused!
	FUNCTION(("index_read_stat: stat 0x%p\n", stat));
	if (!HAS_FS_CALL(vnode, read_index_stat))
		return B_UNSUPPORTED;

	return B_UNSUPPORTED;
	//return FS_CALL(vnode, read_index_stat, descriptor->cookie, stat);
}


static void
index_free_fd(struct file_descriptor* descriptor)
{
	struct vnode* vnode = descriptor->u.vnode;

	if (vnode != NULL) {
		FS_CALL(vnode, free_index_cookie, descriptor->cookie);
		put_vnode(vnode);
	}
}
#endif


/**
 * @brief Reads stat information for a named index on a volume.
 *
 * @param mountID Device ID of the volume.
 * @param name    Index name.
 * @param stat    Output stat buffer.
 * @param kernel  Currently unused.
 * @retval B_OK         Stat filled.
 * @retval B_UNSUPPORTED FS has no read_index_stat hook.
 */
static status_t
index_name_read_stat(dev_t mountID, const char* name, struct stat* stat,
	bool kernel)
{
	FUNCTION(("index_remove(mountID = %" B_PRId32 ", name = %s, kernel = %d)\n",
		mountID, name, kernel));

	struct fs_mount* mount;
	status_t status = get_mount(mountID, &mount);
	if (status != B_OK)
		return status;

	if (!HAS_FS_MOUNT_CALL(mount, read_index_stat)) {
		status = B_UNSUPPORTED;
		goto out;
	}

	status = FS_MOUNT_CALL(mount, read_index_stat, name, stat);

out:
	put_mount(mount);
	return status;
}


/**
 * @brief Removes a named index from a volume.
 *
 * @param mountID Device ID of the volume.
 * @param name    Index name to remove.
 * @param kernel  Currently unused.
 * @retval B_OK              Index removed.
 * @retval B_READ_ONLY_DEVICE FS has no remove_index hook.
 */
static status_t
index_remove(dev_t mountID, const char* name, bool kernel)
{
	FUNCTION(("index_remove(mountID = %" B_PRId32 ", name = %s, kernel = %d)\n",
		mountID, name, kernel));

	struct fs_mount* mount;
	status_t status = get_mount(mountID, &mount);
	if (status != B_OK)
		return status;

	if (!HAS_FS_MOUNT_CALL(mount, remove_index)) {
		status = B_READ_ONLY_DEVICE;
		goto out;
	}

	status = FS_MOUNT_CALL(mount, remove_index, name);

out:
	put_mount(mount);
	return status;
}


/*!	TODO: the query FS API is still the pretty much the same as in R5.
		It would be nice if the FS would find some more kernel support
		for them.
		For example, query parsing should be moved into the kernel.
*/
/**
 * @brief Opens a live query on a volume and returns an FD.
 *
 * @param device  Device ID of the volume to query.
 * @param query   Query expression string.
 * @param flags   Query flags (e.g. B_LIVE_QUERY).
 * @param port    Port to receive live-query notifications (or -1).
 * @param token   Token sent with notifications.
 * @param kernel  true to use the kernel I/O context.
 * @return A non-negative query FD, or a negative error code.
 * @retval B_UNSUPPORTED FS has no open_query hook.
 */
static int
query_open(dev_t device, const char* query, uint32 flags, port_id port,
	int32 token, bool kernel)
{
	struct fs_mount* mount;
	void* cookie;

	FUNCTION(("query_open(device = %" B_PRId32 ", query = \"%s\", kernel = %d)\n",
		device, query, kernel));

	status_t status = get_mount(device, &mount);
	if (status != B_OK)
		return status;

	if (!HAS_FS_MOUNT_CALL(mount, open_query)) {
		status = B_UNSUPPORTED;
		goto error;
	}

	status = FS_MOUNT_CALL(mount, open_query, query, flags, port, token,
		&cookie);
	if (status != B_OK)
		goto error;

	// get fd for the index directory
	int fd;
	fd = get_new_fd(&sQueryOps, mount, NULL, cookie, O_CLOEXEC, kernel);
	if (fd >= 0)
		return fd;

	status = fd;

	// something went wrong
	FS_MOUNT_CALL(mount, close_query, cookie);
	FS_MOUNT_CALL(mount, free_query_cookie, cookie);

error:
	put_mount(mount);
	return status;
}


/**
 * @brief fd_ops close handler for query file descriptors.
 *
 * @param descriptor The query FD.
 * @retval B_OK Always.
 */
static status_t
query_close(struct file_descriptor* descriptor)
{
	struct fs_mount* mount = descriptor->u.mount;

	FUNCTION(("query_close(descriptor = %p)\n", descriptor));

	if (HAS_FS_MOUNT_CALL(mount, close_query))
		return FS_MOUNT_CALL(mount, close_query, descriptor->cookie);

	return B_OK;
}


/**
 * @brief fd_ops free_fd handler for query file descriptors.
 *
 * @param descriptor The query FD being freed.
 */
static void
query_free_fd(struct file_descriptor* descriptor)
{
	struct fs_mount* mount = descriptor->u.mount;

	if (mount != NULL) {
		FS_MOUNT_CALL(mount, free_query_cookie, descriptor->cookie);
		put_mount(mount);
	}
}


/**
 * @brief fd_ops read_dir handler for query file descriptors.
 *
 * @param ioContext  Caller's I/O context (unused here).
 * @param descriptor The query FD.
 * @param buffer     Output buffer for dirent structures.
 * @param bufferSize Size of @p buffer.
 * @param _count     In: max entries; out: entries read.
 * @retval B_UNSUPPORTED FS has no read_query hook.
 */
static status_t
query_read(struct io_context* ioContext, struct file_descriptor* descriptor,
	struct dirent* buffer, size_t bufferSize, uint32* _count)
{
	struct fs_mount* mount = descriptor->u.mount;

	if (HAS_FS_MOUNT_CALL(mount, read_query)) {
		return FS_MOUNT_CALL(mount, read_query, descriptor->cookie, buffer,
			bufferSize, _count);
	}

	return B_UNSUPPORTED;
}


/**
 * @brief fd_ops rewind_dir handler for query file descriptors.
 *
 * @param descriptor The query FD.
 * @retval B_UNSUPPORTED FS has no rewind_query hook.
 */
static status_t
query_rewind(struct file_descriptor* descriptor)
{
	struct fs_mount* mount = descriptor->u.mount;

	if (HAS_FS_MOUNT_CALL(mount, rewind_query))
		return FS_MOUNT_CALL(mount, rewind_query, descriptor->cookie);

	return B_UNSUPPORTED;
}


//	#pragma mark - General File System functions


/**
 * @brief Mounts a file system and links it into the namespace.
 *
 * Creates a new fs_mount structure, loads the FS add-on, calls its
 * mount() hook, acquires the root vnode, and connects the mount to the
 * covering vnode at @p path.
 *
 * @param path    The mount point path (must be an existing directory).
 * @param device  Optional block device or image file path (may be NULL).
 * @param fsName  File system type name (e.g. "bfs").  May be NULL when
 *                @p device is given and B_MOUNT_VIRTUAL_DEVICE is not set.
 * @param flags   Mount flags (e.g. B_MOUNT_READ_ONLY, B_MOUNT_VIRTUAL_DEVICE).
 * @param args    FS-specific mount arguments (may be NULL).
 * @param kernel  true when called from the kernel (e.g. boot sequence).
 * @return The new device ID (dev_t) on success, or a negative error code.
 * @note Acquires sMountOpLock (recursive) for the duration of the call.
 */
static dev_t
fs_mount(char* path, const char* device, const char* fsName, uint32 flags,
	const char* args, bool kernel)
{
	struct ::fs_mount* mount;
	status_t status = B_OK;
	fs_volume* volume = NULL;
	int32 layer = 0;
	Vnode* coveredNode = NULL;

	FUNCTION(("fs_mount: path = '%s', device = '%s', fs_name = '%s', flags = %#"
		B_PRIx32 ", args = '%s'\n", path, device, fsName, flags, args));

	// The path is always safe, we just have to make sure that fsName is
	// almost valid - we can't make any assumptions about args, though.
	// A NULL fsName is OK, if a device was given and the FS is not virtual.
	// We'll get it from the DDM later.
	if (fsName == NULL) {
		if (!device || flags & B_MOUNT_VIRTUAL_DEVICE)
			return B_BAD_VALUE;
	} else if (fsName[0] == '\0')
		return B_BAD_VALUE;

	RecursiveLocker mountOpLocker(sMountOpLock);

	// Helper to delete a newly created file device on failure.
	// Not exactly beautiful, but helps to keep the code below cleaner.
	struct FileDeviceDeleter {
		FileDeviceDeleter() : id(-1) {}
		~FileDeviceDeleter()
		{
			KDiskDeviceManager::Default()->DeleteFileDevice(id);
		}

		partition_id id;
	} fileDeviceDeleter;

	// If the file system is not a "virtual" one, the device argument should
	// point to a real file/device (if given at all).
	// get the partition
	KDiskDeviceManager* ddm = KDiskDeviceManager::Default();
	KPartition* partition = NULL;
	KPath normalizedDevice;
	bool newlyCreatedFileDevice = false;

	if ((flags & B_MOUNT_VIRTUAL_DEVICE) == 0 && device != NULL) {
		// normalize the device path
		status = normalizedDevice.SetTo(device, true);
		if (status != B_OK)
			return status;

		// get a corresponding partition from the DDM
		partition = ddm->RegisterPartition(normalizedDevice.Path());
		if (partition == NULL) {
			// Partition not found: This either means, the user supplied
			// an invalid path, or the path refers to an image file. We try
			// to let the DDM create a file device for the path.
			partition_id deviceID = ddm->CreateFileDevice(
				normalizedDevice.Path(), &newlyCreatedFileDevice);
			if (deviceID >= 0) {
				partition = ddm->RegisterPartition(deviceID);
				if (newlyCreatedFileDevice)
					fileDeviceDeleter.id = deviceID;
			}
		}

		if (partition == NULL) {
			TRACE(("fs_mount(): Partition `%s' not found.\n",
				normalizedDevice.Path()));
			return B_ENTRY_NOT_FOUND;
		}

		device = normalizedDevice.Path();
			// correct path to file device
	}
	PartitionRegistrar partitionRegistrar(partition, true);

	// Write lock the partition's device. For the time being, we keep the lock
	// until we're done mounting -- not nice, but ensure, that no-one is
	// interfering.
	// TODO: Just mark the partition busy while mounting!
	KDiskDevice* diskDevice = NULL;
	if (partition != NULL) {
		diskDevice = ddm->WriteLockDevice(partition->Device()->ID());
		if (diskDevice == NULL) {
			TRACE(("fs_mount(): Failed to lock disk device!\n"));
			return B_ERROR;
		}
	}
	DeviceWriteLocker writeLocker(diskDevice, true);

	if (partition != NULL) {
		// make sure, that the partition is not busy
		if (partition->IsBusy()) {
			TRACE(("fs_mount(): Partition is busy.\n"));
			return B_BUSY;
		}

		if (partition->IsMounted()) {
			TRACE(("fs_mount(): Partition is already mounted.\n"));
			return B_BUSY;
		}

		// if no FS name had been supplied, we get it from the partition
		if (fsName == NULL) {
			KDiskSystem* diskSystem = partition->DiskSystem();
			if (diskSystem == NULL) {
				TRACE(("fs_mount(): No FS name was given, and the DDM didn't "
					"recognize it.\n"));
				return B_BAD_VALUE;
			}

			if (!diskSystem->IsFileSystem()) {
				TRACE(("fs_mount(): No FS name was given, and the DDM found a "
					"partitioning system.\n"));
				return B_BAD_VALUE;
			}

			// The disk system name will not change, and the KDiskSystem
			// object will not go away while the disk device is locked (and
			// the partition has a reference to it), so this is safe.
			fsName = diskSystem->Name();
		}
	}

	mount = new(std::nothrow) (struct ::fs_mount);
	if (mount == NULL)
		return B_NO_MEMORY;

	mount->device_name = strdup(device);
		// "device" can be NULL

	status = mount->entry_cache.Init();
	if (status != B_OK)
		goto err1;

	// initialize structure
	mount->id = sNextMountID++;
	mount->partition = NULL;
	mount->root_vnode = NULL;
	mount->covers_vnode = NULL;
	mount->unmounting = false;
	mount->owns_file_device = false;
	mount->volume = NULL;

	// build up the volume(s)
	while (true) {
		char* layerFSName = get_file_system_name_for_layer(fsName, layer);
		if (layerFSName == NULL) {
			if (layer == 0) {
				status = B_NO_MEMORY;
				goto err1;
			}

			break;
		}
		MemoryDeleter layerFSNameDeleter(layerFSName);

		volume = (fs_volume*)malloc(sizeof(fs_volume));
		if (volume == NULL) {
			status = B_NO_MEMORY;
			goto err1;
		}

		volume->id = mount->id;
		volume->partition = partition != NULL ? partition->ID() : -1;
		volume->layer = layer++;
		volume->private_volume = NULL;
		volume->ops = NULL;
		volume->sub_volume = NULL;
		volume->super_volume = NULL;
		volume->file_system = NULL;
		volume->file_system_name = NULL;

		volume->file_system_name = get_file_system_name(layerFSName);
		if (volume->file_system_name == NULL) {
			status = B_NO_MEMORY;
			free(volume);
			goto err1;
		}

		volume->file_system = get_file_system(layerFSName);
		if (volume->file_system == NULL) {
			status = B_DEVICE_NOT_FOUND;
			free(volume->file_system_name);
			free(volume);
			goto err1;
		}

		if (mount->volume == NULL)
			mount->volume = volume;
		else {
			volume->super_volume = mount->volume;
			mount->volume->sub_volume = volume;
			mount->volume = volume;
		}
	}

	// insert mount struct into list before we call FS's mount() function
	// so that vnodes can be created for this mount
	rw_lock_write_lock(&sMountLock);
	sMountsTable->Insert(mount);
	rw_lock_write_unlock(&sMountLock);

	ino_t rootID;

	if (!sRoot) {
		// we haven't mounted anything yet
		if (strcmp(path, "/") != 0) {
			status = B_ERROR;
			goto err2;
		}

		status = mount->volume->file_system->mount(mount->volume, device, flags,
			args, &rootID);
		if (status != B_OK || mount->volume->ops == NULL)
			goto err2;
	} else {
		{
			VnodePutter temp;
			status = path_to_vnode(path, true, temp, NULL, kernel);
			coveredNode = temp.Detach();
		}
		if (status != B_OK)
			goto err2;

		mount->covers_vnode = coveredNode;

		// make sure covered_vnode is a directory
		if (!S_ISDIR(coveredNode->Type())) {
			status = B_NOT_A_DIRECTORY;
			goto err3;
		}

		if (coveredNode->IsCovered()) {
			// this is already a covered vnode
			status = B_BUSY;
			goto err3;
		}

		// mount it/them
		fs_volume* volume = mount->volume;
		while (volume) {
			status = volume->file_system->mount(volume, device, flags, args,
				&rootID);
			if (status != B_OK || volume->ops == NULL) {
				if (status == B_OK && volume->ops == NULL)
					panic("fs_mount: mount() succeeded but ops is NULL!");
				if (volume->sub_volume)
					goto err4;
				goto err3;
			}

			volume = volume->super_volume;
		}

		volume = mount->volume;
		while (volume) {
			if (volume->ops->all_layers_mounted != NULL)
				volume->ops->all_layers_mounted(volume);
			volume = volume->super_volume;
		}
	}

	// the root node is supposed to be owned by the file system - it must
	// exist at this point
	rw_lock_write_lock(&sVnodeLock);
	mount->root_vnode = lookup_vnode(mount->id, rootID);
	if (mount->root_vnode == NULL || mount->root_vnode->ref_count != 1) {
		panic("fs_mount: file system does not own its root node!\n");
		status = B_ERROR;
		rw_lock_write_unlock(&sVnodeLock);
		goto err4;
	}

	// set up the links between the root vnode and the vnode it covers
	if (coveredNode != NULL) {
		if (coveredNode->IsCovered()) {
			// the vnode is covered now
			status = B_BUSY;
			rw_lock_write_unlock(&sVnodeLock);
			goto err4;
		}

		mount->root_vnode->covers = coveredNode;
		mount->root_vnode->SetCovering(true);

		coveredNode->covered_by = mount->root_vnode;
		coveredNode->SetCovered(true);
		inc_vnode_ref_count(mount->root_vnode);
	}
	rw_lock_write_unlock(&sVnodeLock);

	if (sRoot == NULL) {
		sRoot = mount->root_vnode;
		rw_lock_write_lock(&sIOContextRootLock);
		get_current_io_context(true)->root = sRoot;
		rw_lock_write_unlock(&sIOContextRootLock);
		inc_vnode_ref_count(sRoot);
	}

	// supply the partition (if any) with the mount ID and mark it mounted
	if (partition) {
		partition->SetVolumeID(mount->id);

		// keep a partition reference as long as the partition is mounted
		partitionRegistrar.Detach();
		mount->partition = partition;
		mount->owns_file_device = newlyCreatedFileDevice;
		fileDeviceDeleter.id = -1;
	}

	notify_mount(mount->id,
		coveredNode != NULL ? coveredNode->device : -1,
		coveredNode ? coveredNode->id : -1);

	return mount->id;

err4:
	FS_MOUNT_CALL_NO_PARAMS(mount, unmount);
err3:
	if (coveredNode != NULL)
		put_vnode(coveredNode);
err2:
	rw_lock_write_lock(&sMountLock);
	sMountsTable->Remove(mount);
	rw_lock_write_unlock(&sMountLock);
err1:
	delete mount;

	return status;
}


/**
 * @brief Unmounts a file system from the namespace.
 *
 * Detaches the mount from the VFS namespace, disconnects all open FDs,
 * frees all cached vnodes, calls the FS unmount() hook, and releases the
 * partition reference.
 *
 * @param path    Optional path to the mount point (used to find the mount).
 *                Exactly one of @p path and @p mountID must be specified.
 * @param mountID Device ID of the mount to unmount (used when @p path is NULL).
 * @param flags   Unmount flags (e.g. B_FORCE_UNMOUNT).
 * @param kernel  true when called from the kernel.
 * @retval B_OK        Volume unmounted.
 * @retval B_BAD_VALUE @p path does not refer to a mount root.
 * @retval B_BUSY      Volume is in use and B_FORCE_UNMOUNT was not set.
 * @note Acquires sMountOpLock (recursive) for the duration of the call.
 */
static status_t
fs_unmount(char* path, dev_t mountID, uint32 flags, bool kernel)
{
	struct fs_mount* mount;
	status_t err;

	FUNCTION(("fs_unmount(path '%s', dev %" B_PRId32 ", kernel %d\n", path,
		mountID, kernel));

	VnodePutter pathVnode;
	if (path != NULL) {
		err = path_to_vnode(path, true, pathVnode, NULL, kernel);
		if (err != B_OK)
			return B_ENTRY_NOT_FOUND;
	}

	RecursiveLocker mountOpLocker(sMountOpLock);
	ReadLocker mountLocker(sMountLock);

	mount = find_mount(path != NULL ? pathVnode->device : mountID);
	if (mount == NULL) {
		panic("fs_unmount: find_mount() failed on root vnode @%p of mount\n",
			pathVnode.Get());
	}

	mountLocker.Unlock();

	if (path != NULL) {
		if (mount->root_vnode != pathVnode.Get()) {
			// not mountpoint
			return B_BAD_VALUE;
		}

		pathVnode.Unset();
	}

	// if the volume is associated with a partition, lock the device of the
	// partition as long as we are unmounting
	KDiskDeviceManager* ddm = KDiskDeviceManager::Default();
	KPartition* partition = mount->partition;
	KDiskDevice* diskDevice = NULL;
	if (partition != NULL) {
		if (partition->Device() == NULL) {
			dprintf("fs_unmount(): There is no device!\n");
			return B_ERROR;
		}
		diskDevice = ddm->WriteLockDevice(partition->Device()->ID());
		if (!diskDevice) {
			TRACE(("fs_unmount(): Failed to lock disk device!\n"));
			return B_ERROR;
		}
	}
	DeviceWriteLocker writeLocker(diskDevice, true);

	// make sure, that the partition is not busy
	if (partition != NULL) {
		if ((flags & B_UNMOUNT_BUSY_PARTITION) == 0 && partition->IsBusy()) {
			dprintf("fs_unmount(): Partition is busy.\n");
			return B_BUSY;
		}
	}

	// grab the vnode master mutex to keep someone from creating
	// a vnode while we're figuring out if we can continue
	WriteLocker vnodesWriteLocker(&sVnodeLock);

	bool disconnectedDescriptors = false;

	while (true) {
		bool busy = false;

		// cycle through the list of vnodes associated with this mount and
		// make sure all of them are not busy or have refs on them
		VnodeList::Iterator iterator = mount->vnodes.GetIterator();
		while (struct vnode* vnode = iterator.Next()) {
			if (vnode->IsBusy()) {
				dprintf("fs_unmount(): inode %" B_PRIdINO " is busy\n", vnode->id);
				busy = true;
				break;
			}

			// check the vnode's ref count -- subtract additional references for
			// covering
			int32 refCount = vnode->ref_count;
			if (vnode->covers != NULL)
				refCount--;
			if (vnode->covered_by != NULL)
				refCount--;
			if (vnode == mount->root_vnode)
				refCount--;

			if (refCount != 0) {
				dprintf("fs_unmount(): inode %" B_PRIdINO " is still referenced\n", vnode->id);
				// there are still vnodes in use on this mount, so we cannot
				// unmount yet
				busy = true;
				break;
			}
		}

		if (!busy)
			break;

		if ((flags & B_FORCE_UNMOUNT) == 0)
			return B_BUSY;

		if (disconnectedDescriptors) {
			// wait a bit until the last access is finished, and then try again
			vnodesWriteLocker.Unlock();
			snooze(100000);
			// TODO: if there is some kind of bug that prevents the ref counts
			// from getting back to zero, this will fall into an endless loop...
			vnodesWriteLocker.Lock();
			continue;
		}

		// the file system is still busy - but we're forced to unmount it,
		// so let's disconnect all open file descriptors

		mount->unmounting = true;
			// prevent new vnodes from being created

		vnodesWriteLocker.Unlock();

		disconnect_mount_or_vnode_fds(mount, NULL);
		disconnectedDescriptors = true;

		vnodesWriteLocker.Lock();
	}

	// We can safely continue. Mark all of the vnodes busy and this mount
	// structure in unmounting state. Also undo the vnode covers/covered_by
	// links.
	mount->unmounting = true;

	VnodeList::Iterator iterator = mount->vnodes.GetIterator();
	while (struct vnode* vnode = iterator.Next()) {
		// Remove all covers/covered_by links from other mounts' nodes to this
		// vnode and adjust the node ref count accordingly. We will release the
		// references to the external vnodes below.
		if (Vnode* coveredNode = vnode->covers) {
			if (Vnode* coveringNode = vnode->covered_by) {
				// We have both covered and covering vnodes, so just remove us
				// from the chain.
				coveredNode->covered_by = coveringNode;
				coveringNode->covers = coveredNode;
				vnode->ref_count -= 2;

				vnode->covered_by = NULL;
				vnode->covers = NULL;
				vnode->SetCovering(false);
				vnode->SetCovered(false);
			} else {
				// We only have a covered vnode. Remove its link to us.
				coveredNode->covered_by = NULL;
				coveredNode->SetCovered(false);
				vnode->ref_count--;

				// If the other node is an external vnode, we keep its link
				// link around so we can put the reference later on. Otherwise
				// we get rid of it right now.
				if (coveredNode->mount == mount) {
					vnode->covers = NULL;
					coveredNode->ref_count--;
				}
			}
		} else if (Vnode* coveringNode = vnode->covered_by) {
			// We only have a covering vnode. Remove its link to us.
			coveringNode->covers = NULL;
			coveringNode->SetCovering(false);
			vnode->ref_count--;

			// If the other node is an external vnode, we keep its link
			// link around so we can put the reference later on. Otherwise
			// we get rid of it right now.
			if (coveringNode->mount == mount) {
				vnode->covered_by = NULL;
				coveringNode->ref_count--;
			}
		}

		if (vnode == mount->root_vnode)
			continue;

		vnode->SetBusy(true);
		vnode_to_be_freed(vnode);
	}

	vnodesWriteLocker.Unlock();

	// Free all vnodes associated with this mount.
	while (struct vnode* vnode = mount->vnodes.Head()) {
		// Put the references to external covered/covering vnodes we kept above.
		if (Vnode* coveredNode = vnode->covers)
			put_vnode(coveredNode);
		if (Vnode* coveringNode = vnode->covered_by)
			put_vnode(coveringNode);

		// free_vnode() removes nodes from the mount list. However, the root
		// will still be referenced by the FS, so we can't free it yet.
		if (vnode == mount->root_vnode)
			remove_vnode_from_mount_list(vnode, mount);
		else
			free_vnode(vnode, false);
	}

	// Re-add the root to the mount list, so it can be freed.
	add_vnode_to_mount_list(mount->root_vnode, mount);
	mount->root_vnode = NULL;

	// remove the mount structure from the hash table
	rw_lock_write_lock(&sMountLock);
	sMountsTable->Remove(mount);
	rw_lock_write_unlock(&sMountLock);

	mountOpLocker.Unlock();

	FS_MOUNT_CALL_NO_PARAMS(mount, unmount);
	notify_unmount(mount->id);

	// dereference the partition and mark it unmounted
	if (partition) {
		partition->SetVolumeID(-1);

		if (mount->owns_file_device)
			KDiskDeviceManager::Default()->DeleteFileDevice(partition->ID());
		partition->Unregister();
	}

	delete mount;
	return B_OK;
}


/**
 * @brief Synchronizes all dirty data and metadata for a mounted volume.
 *
 * Iterates over all vnodes with a VMCache, flushes their modified pages, then
 * calls the FS sync() hook and flushes the underlying device's write cache.
 *
 * @param device Device ID of the volume to sync.
 * @retval B_OK Volume synchronized.
 */
static status_t
fs_sync(dev_t device)
{
	struct fs_mount* mount;
	status_t status = get_mount(device, &mount);
	if (status != B_OK)
		return status;

	struct vnode marker;
	memset(&marker, 0, sizeof(marker));
	marker.SetBusy(true);
	marker.SetRemoved(true);

	// First, synchronize all file caches

	while (true) {
		WriteLocker locker(sVnodeLock);
			// Note: That's the easy way. Which is probably OK for sync(),
			// since it's a relatively rare call and doesn't need to allow for
			// a lot of concurrency. Using a read lock would be possible, but
			// also more involved, since we had to lock the individual nodes
			// and take care of the locking order, which we might not want to
			// do while holding fs_mount::lock.

		// synchronize access to vnode list
		mutex_lock(&mount->lock);

		struct vnode* vnode;
		if (!marker.IsRemoved()) {
			vnode = mount->vnodes.GetNext(&marker);
			mount->vnodes.Remove(&marker);
			marker.SetRemoved(true);
		} else
			vnode = mount->vnodes.First();

		while (vnode != NULL && (vnode->cache == NULL
			|| vnode->IsRemoved() || vnode->IsBusy())) {
			// TODO: we could track writes (and writable mapped vnodes)
			//	and have a simple flag that we could test for here
			vnode = mount->vnodes.GetNext(vnode);
		}

		if (vnode != NULL) {
			// insert marker vnode again
			mount->vnodes.InsertBefore(mount->vnodes.GetNext(vnode), &marker);
			marker.SetRemoved(false);
		}

		mutex_unlock(&mount->lock);

		if (vnode == NULL)
			break;

		vnode = lookup_vnode(mount->id, vnode->id);
		if (vnode == NULL || vnode->IsBusy())
			continue;

		if (inc_vnode_ref_count(vnode) == 0) {
			// this vnode has been unused before
			vnode_used(vnode);
		}

		locker.Unlock();

		if (vnode->cache != NULL && !vnode->IsRemoved())
			vnode->cache->WriteModified();

		put_vnode(vnode);
	}

	// Let the file systems do their synchronizing work
	if (HAS_FS_MOUNT_CALL(mount, sync))
		status = FS_MOUNT_CALL_NO_PARAMS(mount, sync);

	// Finally, flush the underlying device's write cache (if possible.)
	if (mount->partition != NULL && mount->partition->Device() != NULL)
		ioctl(mount->partition->Device()->FD(), B_FLUSH_DRIVE_CACHE);

	put_mount(mount);
	return status;
}


/**
 * @brief Retrieves file system information for a mounted volume.
 *
 * Calls the FS read_fs_info() hook and then fills in dev, root, fsh_name,
 * and device_name fields that the FS does not need to provide.
 *
 * @param device Device ID of the volume.
 * @param info   Output fs_info buffer.
 * @retval B_OK Info filled.
 */
static status_t
fs_read_info(dev_t device, struct fs_info* info)
{
	struct fs_mount* mount;
	status_t status = get_mount(device, &mount);
	if (status != B_OK)
		return status;

	memset(info, 0, sizeof(struct fs_info));

	if (HAS_FS_MOUNT_CALL(mount, read_fs_info))
		status = FS_MOUNT_CALL(mount, read_fs_info, info);

	// fill in info the file system doesn't (have to) know about
	if (status == B_OK) {
		info->dev = mount->id;
		info->root = mount->root_vnode->id;

		fs_volume* volume = mount->volume;
		while (volume->super_volume != NULL)
			volume = volume->super_volume;

		strlcpy(info->fsh_name, volume->file_system_name,
			sizeof(info->fsh_name));
		if (mount->device_name != NULL) {
			strlcpy(info->device_name, mount->device_name,
				sizeof(info->device_name));
		}
	}

	// if the call is not supported by the file system, there are still
	// the parts that we filled out ourselves

	put_mount(mount);
	return status;
}


/**
 * @brief Updates writable file system information fields for a mounted volume.
 *
 * @param device Device ID of the volume.
 * @param info   New fs_info values.
 * @param mask   Bitmask of fields to update (e.g. FS_WRITE_FSINFO_NAME).
 * @retval B_OK              Fields updated.
 * @retval B_READ_ONLY_DEVICE FS has no write_fs_info hook.
 */
static status_t
fs_write_info(dev_t device, const struct fs_info* info, int mask)
{
	struct fs_mount* mount;
	status_t status = get_mount(device, &mount);
	if (status != B_OK)
		return status;

	if (HAS_FS_MOUNT_CALL(mount, write_fs_info))
		status = FS_MOUNT_CALL(mount, write_fs_info, info, mask);
	else
		status = B_READ_ONLY_DEVICE;

	put_mount(mount);
	return status;
}


/**
 * @brief Iterates over mounted volumes by device ID.
 *
 * Finds the next mounted volume whose ID is >= *_cookie and returns its ID.
 * Updates *_cookie so that successive calls enumerate all mounts.
 *
 * @param _cookie In/out: cookie holding the next candidate device ID.
 * @return The device ID of the next mounted volume, or B_BAD_VALUE when
 *         there are no more mounts.
 */
static dev_t
fs_next_device(int32* _cookie)
{
	struct fs_mount* mount = NULL;
	dev_t device = *_cookie;

	rw_lock_read_lock(&sMountLock);

	// Since device IDs are assigned sequentially, this algorithm
	// does work good enough. It makes sure that the device list
	// returned is sorted, and that no device is skipped when an
	// already visited device got unmounted.

	while (device < sNextMountID) {
		mount = find_mount(device++);
		if (mount != NULL && mount->volume->private_volume != NULL)
			break;
	}

	*_cookie = device;

	if (mount != NULL)
		device = mount->id;
	else
		device = B_BAD_VALUE;

	rw_lock_read_unlock(&sMountLock);

	return device;
}


/**
 * @brief Reads an extended attribute value using the kernel I/O context.
 *
 * Opens the attribute identified by @p fd + @p attribute, reads @p readBytes
 * bytes at @p pos, and closes the attribute FD.
 *
 * @param fd         File descriptor of the node owning the attribute.
 * @param attribute  Attribute name.
 * @param type       Expected attribute type (passed to attr_open, ignored here).
 * @param pos        Byte offset within the attribute.
 * @param buffer     Destination buffer.
 * @param readBytes  Number of bytes to read.
 * @return Bytes read on success, or a negative error code.
 */
ssize_t
fs_read_attr(int fd, const char *attribute, uint32 type, off_t pos,
	void *buffer, size_t readBytes)
{
	int attrFD = attr_open(fd, NULL, attribute, O_RDONLY, true);
	if (attrFD < 0)
		return attrFD;

	ssize_t bytesRead = _kern_read(attrFD, pos, buffer, readBytes);

	_kern_close(attrFD);

	return bytesRead;
}


/**
 * @brief Retrieves the absolute path of the current working directory.
 *
 * @param buffer Output buffer for the path string.
 * @param size   Size of @p buffer.
 * @param kernel true to use the kernel I/O context.
 * @retval B_OK       CWD path written to @p buffer.
 * @retval B_ERROR    No CWD is set in the I/O context.
 */
static status_t
get_cwd(char* buffer, size_t size, bool kernel)
{
	FUNCTION(("vfs_get_cwd: buf %p, size %ld\n", buffer, size));

	// Get current working directory from io context
	const struct io_context* context = get_current_io_context(kernel);
	rw_lock_read_lock(&context->lock);

	struct vnode* vnode = context->cwd;
	if (vnode != NULL)
		inc_vnode_ref_count(vnode);

	rw_lock_read_unlock(&context->lock);

	if (vnode == NULL)
		return B_ERROR;

	status_t status = dir_vnode_to_path(vnode, buffer, size, kernel);
	put_vnode(vnode);

	return status;
}


/**
 * @brief Changes the current working directory to the given path.
 *
 * Resolves @p path relative to @p fd, verifies it is a directory and that
 * the caller has execute permission, then atomically swaps the CWD in the
 * I/O context.
 *
 * @param fd     Base FD for relative paths (or AT_FDCWD).
 * @param path   New working directory path.
 * @param kernel true to use the kernel I/O context.
 * @retval B_OK             CWD changed.
 * @retval B_NOT_A_DIRECTORY @p path does not refer to a directory.
 * @retval B_PERMISSION_DENIED Caller lacks execute permission on the directory.
 */
static status_t
set_cwd(int fd, char* path, bool kernel)
{
	struct io_context* context;
	struct vnode* oldDirectory;

	FUNCTION(("set_cwd: path = \'%s\'\n", path));

	// Get vnode for passed path, and bail if it failed
	VnodePutter vnode;
	status_t status = fd_and_path_to_vnode(fd, path, true, vnode, NULL, kernel);
	if (status < 0)
		return status;

	if (!S_ISDIR(vnode->Type())) {
		// nope, can't cwd to here
		return B_NOT_A_DIRECTORY;
	}

	// We need to have the permission to enter the directory, too
	if (HAS_FS_CALL(vnode, access)) {
		status = FS_CALL(vnode.Get(), access, X_OK);
		if (status != B_OK)
			return status;
	}

	// Get current io context and lock
	context = get_current_io_context(kernel);
	rw_lock_write_lock(&context->lock);

	// save the old current working directory first
	oldDirectory = context->cwd;
	context->cwd = vnode.Detach();

	rw_lock_write_unlock(&context->lock);

	if (oldDirectory)
		put_vnode(oldDirectory);

	return B_NO_ERROR;
}


/**
 * @brief Safely copies a name string from user space with length validation.
 *
 * @param to     Kernel-space destination buffer.
 * @param from   User-space source pointer.
 * @param length Size of @p to (including the NUL terminator).
 * @retval B_OK            Name copied.
 * @retval B_NAME_TOO_LONG String from user space exceeds @p length - 1.
 * @return Negative error code if user_strlcpy() fails (e.g. bad address).
 */
static status_t
user_copy_name(char* to, const char* from, size_t length)
{
	ssize_t len = user_strlcpy(to, from, length);
	if (len < 0)
		return len;
	if (len >= (ssize_t)length)
		return B_NAME_TOO_LONG;
	return B_OK;
}


//	#pragma mark - kernel mirrored syscalls


/**
 * @brief Kernel syscall: mount a file system.
 *
 * @param path       Mount point path.
 * @param device     Optional block device or image.
 * @param fsName     File system type name.
 * @param flags      Mount flags.
 * @param args       FS-specific arguments.
 * @param argsLength Length of @p args (unused; present for ABI symmetry).
 * @return New device ID on success, or a negative error code.
 */
dev_t
_kern_mount(const char* path, const char* device, const char* fsName,
	uint32 flags, const char* args, size_t argsLength)
{
	KPath pathBuffer(path);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	return fs_mount(pathBuffer.LockBuffer(), device, fsName, flags, args, true);
}


/**
 * @brief Kernel syscall: unmount a file system by path.
 *
 * @param path  Mount point path.
 * @param flags Unmount flags (e.g. B_FORCE_UNMOUNT).
 * @retval B_OK Volume unmounted.
 */
status_t
_kern_unmount(const char* path, uint32 flags)
{
	KPath pathBuffer(path);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	return fs_unmount(pathBuffer.LockBuffer(), -1, flags, true);
}


/**
 * @brief Kernel syscall: read file system information.
 *
 * @param device Device ID.
 * @param info   Output fs_info buffer.
 * @retval B_OK Info filled.
 * @retval B_BAD_VALUE @p info is NULL.
 */
status_t
_kern_read_fs_info(dev_t device, struct fs_info* info)
{
	if (info == NULL)
		return B_BAD_VALUE;

	return fs_read_info(device, info);
}


/**
 * @brief Kernel syscall: write file system information.
 *
 * @param device Device ID.
 * @param info   New fs_info values.
 * @param mask   Fields to update.
 * @retval B_OK Updated.
 * @retval B_BAD_VALUE @p info is NULL.
 */
status_t
_kern_write_fs_info(dev_t device, const struct fs_info* info, int mask)
{
	if (info == NULL)
		return B_BAD_VALUE;

	return fs_write_info(device, info, mask);
}


/**
 * @brief Kernel syscall: synchronize all mounted file systems.
 *
 * Iterates over all mounts and calls fs_sync() on each.  Errors from
 * individual mounts are logged but do not abort the iteration.
 *
 * @retval B_OK Always.
 */
status_t
_kern_sync(void)
{
	// Note: _kern_sync() is also called from _user_sync()
	int32 cookie = 0;
	dev_t device;
	while ((device = next_dev(&cookie)) >= 0) {
		status_t status = fs_sync(device);
		if (status != B_OK && status != B_BAD_VALUE) {
			dprintf("sync: device %" B_PRIdDEV " couldn't sync: %s\n", device,
				strerror(status));
		}
	}

	return B_OK;
}


/**
 * @brief Kernel syscall: iterate over mounted devices.
 *
 * @param _cookie In/out: enumeration cookie.
 * @return Next device ID, or B_BAD_VALUE when exhausted.
 */
dev_t
_kern_next_device(int32* _cookie)
{
	return fs_next_device(_cookie);
}


/**
 * @brief Kernel syscall: enumerate open file descriptors of a team.
 *
 * @param teamID   Team to inspect.
 * @param _cookie  In/out: FD slot index (start at 0).
 * @param info     Output fd_info structure.
 * @param infoSize Must equal sizeof(fd_info).
 * @retval B_OK            Info filled for the next open FD.
 * @retval B_ENTRY_NOT_FOUND No more open FDs.
 * @retval B_BAD_TEAM_ID   Team not found.
 * @retval B_BAD_VALUE     @p infoSize is wrong.
 */
status_t
_kern_get_next_fd_info(team_id teamID, uint32* _cookie, fd_info* info,
	size_t infoSize)
{
	if (infoSize != sizeof(fd_info))
		return B_BAD_VALUE;

	// get the team
	Team* team = Team::Get(teamID);
	if (team == NULL)
		return B_BAD_TEAM_ID;
	BReference<Team> teamReference(team, true);

	// now that we have a team reference, its I/O context won't go away
	const io_context* context = team->io_context;
	ReadLocker contextLocker(context->lock);

	uint32 slot = *_cookie;

	struct file_descriptor* descriptor;
	while (slot < context->table_size
			&& (descriptor = context->fds[slot]) == NULL) {
		slot++;
	}

	if (slot >= context->table_size)
		return B_ENTRY_NOT_FOUND;

	info->number = slot;
	info->open_mode = descriptor->open_mode;

	struct vnode* vnode = fd_vnode(descriptor);
	if (vnode != NULL) {
		info->device = vnode->device;
		info->node = vnode->id;
	} else if (descriptor->u.mount != NULL) {
		info->device = descriptor->u.mount->id;
		info->node = -1;
	}

	*_cookie = slot + 1;
	return B_OK;
}


/**
 * @brief Kernel syscall: open or create a file by entry_ref.
 *
 * @param device   Device ID of the parent directory.
 * @param inode    Node ID of the parent directory.
 * @param name     Entry name.
 * @param openMode Open flags.
 * @param perms    Permission bits (used only when O_CREAT is set).
 * @return A non-negative FD, or a negative error code.
 */
int
_kern_open_entry_ref(dev_t device, ino_t inode, const char* name, int openMode,
	int perms)
{
	if ((openMode & O_CREAT) != 0) {
		return file_create_entry_ref(device, inode, name, openMode, perms,
			true);
	}

	return file_open_entry_ref(device, inode, name, openMode, true);
}


/*!	\brief Opens a node specified by a FD + path pair.

	At least one of \a fd and \a path must be specified.
	If only \a fd is given, the function opens the node identified by this
	FD. If only a path is given, this path is opened. If both are given and
	the path is absolute, \a fd is ignored; a relative path is reckoned off
	of the directory (!) identified by \a fd.

	\param fd The FD. May be < 0.
	\param path The absolute or relative path. May be \c NULL.
	\param openMode The open mode.
	\return A FD referring to the newly opened node, or an error code,
			if an error occurs.
*/
/**
 * @brief Kernel syscall: open or create a file by (fd, path).
 *
 * @param fd       Base FD (AT_FDCWD or a directory FD).
 * @param path     Absolute or relative path.
 * @param openMode Open flags.
 * @param perms    Permissions (used when O_CREAT is set).
 * @return A non-negative FD, or a negative error code.
 */
int
_kern_open(int fd, const char* path, int openMode, int perms)
{
	KPath pathBuffer(path, KPath::LAZY_ALLOC);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	if ((openMode & O_CREAT) != 0)
		return file_create(fd, pathBuffer.LockBuffer(), openMode, perms, true);

	return file_open(fd, pathBuffer.LockBuffer(), openMode, true);
}


/*!	\brief Opens a directory specified by entry_ref or node_ref.

	The supplied name may be \c NULL, in which case directory identified
	by \a device and \a inode will be opened. Otherwise \a device and
	\a inode identify the parent directory of the directory to be opened
	and \a name its entry name.

	\param device If \a name is specified the ID of the device the parent
		   directory of the directory to be opened resides on, otherwise
		   the device of the directory itself.
	\param inode If \a name is specified the node ID of the parent
		   directory of the directory to be opened, otherwise node ID of the
		   directory itself.
	\param name The entry name of the directory to be opened. If \c NULL,
		   the \a device + \a inode pair identify the node to be opened.
	\return The FD of the newly opened directory or an error code, if
			something went wrong.
*/
/**
 * @brief Kernel syscall: open a directory by entry_ref or node_ref.
 *
 * @param device Device ID (parent directory if @p name != NULL, else target).
 * @param inode  Node ID (parent directory if @p name != NULL, else target).
 * @param name   Entry name, or NULL to open the node at (@p device, @p inode).
 * @return A non-negative FD, or a negative error code.
 */
int
_kern_open_dir_entry_ref(dev_t device, ino_t inode, const char* name)
{
	return dir_open_entry_ref(device, inode, name, true);
}


/*!	\brief Opens a directory specified by a FD + path pair.

	At least one of \a fd and \a path must be specified.
	If only \a fd is given, the function opens the directory identified by this
	FD. If only a path is given, this path is opened. If both are given and
	the path is absolute, \a fd is ignored; a relative path is reckoned off
	of the directory (!) identified by \a fd.

	\param fd The FD. May be < 0.
	\param path The absolute or relative path. May be \c NULL.
	\return A FD referring to the newly opened directory, or an error code,
			if an error occurs.
*/
/**
 * @brief Kernel syscall: open a directory by (fd, path).
 *
 * @param fd   Base FD (AT_FDCWD or a directory FD).
 * @param path Absolute or relative path to the directory.
 * @return A non-negative FD, or a negative error code.
 */
int
_kern_open_dir(int fd, const char* path)
{
	KPath pathBuffer(path, KPath::LAZY_ALLOC);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	return dir_open(fd, pathBuffer.LockBuffer(), true);
}


/**
 * @brief Kernel syscall: file control (fcntl).
 *
 * @param fd       File descriptor.
 * @param op       F_* command.
 * @param argument Command-specific argument.
 * @retval B_OK Operation succeeded.
 */
status_t
_kern_fcntl(int fd, int op, size_t argument)
{
	return common_fcntl(fd, op, argument, true);
}


/**
 * @brief Kernel syscall: flush file data/metadata to storage.
 *
 * @param fd       File descriptor.
 * @param dataOnly true for fdatasync semantics.
 * @retval B_OK Flushed.
 */
status_t
_kern_fsync(int fd, bool dataOnly)
{
	return common_sync(fd, dataOnly, true);
}


/**
 * @brief Kernel syscall: acquire a mandatory lock on a file's vnode.
 *
 * @param fd File descriptor.
 * @retval B_OK Lock acquired.
 */
status_t
_kern_lock_node(int fd)
{
	return common_lock_node(fd, true);
}


/**
 * @brief Kernel syscall: release a mandatory lock on a file's vnode.
 *
 * @param fd File descriptor.
 * @retval B_OK Lock released.
 */
status_t
_kern_unlock_node(int fd)
{
	return common_unlock_node(fd, true);
}


/**
 * @brief Kernel syscall: pre-allocate disk space for a file.
 *
 * @param fd     File descriptor.
 * @param offset Start offset.
 * @param length Bytes to pre-allocate.
 * @retval B_OK Space reserved.
 */
status_t
_kern_preallocate(int fd, off_t offset, off_t length)
{
	return common_preallocate(fd, offset, length, true);
}


/**
 * @brief Kernel syscall: create a directory by entry_ref.
 *
 * @param device Device ID of the parent.
 * @param inode  Node ID of the parent.
 * @param name   New directory name.
 * @param perms  Permission bits.
 * @retval B_OK Directory created.
 */
status_t
_kern_create_dir_entry_ref(dev_t device, ino_t inode, const char* name,
	int perms)
{
	return dir_create_entry_ref(device, inode, name, perms, true);
}


/*!	\brief Creates a directory specified by a FD + path pair.

	\a path must always be specified (it contains the name of the new directory
	at least). If only a path is given, this path identifies the location at
	which the directory shall be created. If both \a fd and \a path are given
	and the path is absolute, \a fd is ignored; a relative path is reckoned off
	of the directory (!) identified by \a fd.

	\param fd The FD. May be < 0.
	\param path The absolute or relative path. Must not be \c NULL.
	\param perms The access permissions the new directory shall have.
	\return \c B_OK, if the directory has been created successfully, another
			error code otherwise.
*/
/**
 * @brief Kernel syscall: create a directory by (fd, path).
 *
 * @param fd    Base FD for relative paths.
 * @param path  Path of the directory to create.
 * @param perms Permission bits.
 * @retval B_OK Directory created.
 */
status_t
_kern_create_dir(int fd, const char* path, int perms)
{
	KPath pathBuffer(path, KPath::DEFAULT);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	return dir_create(fd, pathBuffer.LockBuffer(), perms, true);
}


/**
 * @brief Kernel syscall: remove a directory by (fd, path).
 *
 * @param fd   Base FD for relative paths.
 * @param path Path of the directory to remove.
 * @retval B_OK Directory removed.
 */
status_t
_kern_remove_dir(int fd, const char* path)
{
	KPath pathBuffer(path, KPath::LAZY_ALLOC);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	return dir_remove(fd, pathBuffer.LockBuffer(), true);
}


/*!	\brief Reads the contents of a symlink referred to by a FD + path pair.

	At least one of \a fd and \a path must be specified.
	If only \a fd is given, the function the symlink to be read is the node
	identified by this FD. If only a path is given, this path identifies the
	symlink to be read. If both are given and the path is absolute, \a fd is
	ignored; a relative path is reckoned off of the directory (!) identified
	by \a fd.
	If this function fails with B_BUFFER_OVERFLOW, the \a _bufferSize pointer
	will still be updated to reflect the required buffer size.

	\param fd The FD. May be < 0.
	\param path The absolute or relative path. May be \c NULL.
	\param buffer The buffer into which the contents of the symlink shall be
		   written.
	\param _bufferSize A pointer to the size of the supplied buffer.
	\return The length of the link on success or an appropriate error code
*/
/**
 * @brief Kernel syscall: read the target of a symbolic link.
 *
 * @param fd          Base FD for relative paths.
 * @param path        Path to the symlink (leaf NOT followed).
 * @param buffer      Destination buffer.
 * @param _bufferSize In: capacity; out: required size.
 * @retval B_OK Link target read.
 */
status_t
_kern_read_link(int fd, const char* path, char* buffer, size_t* _bufferSize)
{
	KPath pathBuffer(path, KPath::LAZY_ALLOC);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	return common_read_link(fd, pathBuffer.LockBuffer(),
		buffer, _bufferSize, true);
}


/*!	\brief Creates a symlink specified by a FD + path pair.

	\a path must always be specified (it contains the name of the new symlink
	at least). If only a path is given, this path identifies the location at
	which the symlink shall be created. If both \a fd and \a path are given and
	the path is absolute, \a fd is ignored; a relative path is reckoned off
	of the directory (!) identified by \a fd.

	\param fd The FD. May be < 0.
	\param toPath The absolute or relative path. Must not be \c NULL.
	\param mode The access permissions the new symlink shall have.
	\return \c B_OK, if the symlink has been created successfully, another
			error code otherwise.
*/
/**
 * @brief Kernel syscall: create a symbolic link.
 *
 * @param fd     Base FD for relative @p path.
 * @param path   Path where the symlink is created.
 * @param toPath Link target.
 * @param mode   Permission bits.
 * @retval B_OK Symlink created.
 */
status_t
_kern_create_symlink(int fd, const char* path, const char* toPath, int mode)
{
	KPath pathBuffer(path);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	return common_create_symlink(fd, pathBuffer.LockBuffer(),
		toPath, mode, true);
}


/**
 * @brief Kernel syscall: create a hard link.
 *
 * @param pathFD          FD for the directory in which to create the link.
 * @param path            Path for the new hard link.
 * @param toFD            FD for the directory of the target.
 * @param toPath          Path of the existing node to link.
 * @param traverseLeafLink If true, follow the final symlink in @p toPath.
 * @retval B_OK Link created.
 */
status_t
_kern_create_link(int pathFD, const char* path, int toFD, const char* toPath,
	bool traverseLeafLink)
{
	KPath pathBuffer(path);
	KPath toPathBuffer(toPath);
	if (pathBuffer.InitCheck() != B_OK || toPathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	return common_create_link(pathFD, pathBuffer.LockBuffer(), toFD,
		toPathBuffer.LockBuffer(), traverseLeafLink, true);
}


/*!	\brief Removes an entry specified by a FD + path pair from its directory.

	\a path must always be specified (it contains at least the name of the entry
	to be deleted). If only a path is given, this path identifies the entry
	directly. If both \a fd and \a path are given and the path is absolute,
	\a fd is ignored; a relative path is reckoned off of the directory (!)
	identified by \a fd.

	\param fd The FD. May be < 0.
	\param path The absolute or relative path. Must not be \c NULL.
	\return \c B_OK, if the entry has been removed successfully, another
			error code otherwise.
*/
/**
 * @brief Kernel syscall: remove a directory entry by (fd, path).
 *
 * @param fd   Base FD for relative paths.
 * @param path Path of the entry to remove.
 * @retval B_OK Entry removed.
 */
status_t
_kern_unlink(int fd, const char* path)
{
	KPath pathBuffer(path);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	return common_unlink(fd, pathBuffer.LockBuffer(), true);
}


/*!	\brief Moves an entry specified by a FD + path pair to a an entry specified
		   by another FD + path pair.

	\a oldPath and \a newPath must always be specified (they contain at least
	the name of the entry). If only a path is given, this path identifies the
	entry directly. If both a FD and a path are given and the path is absolute,
	the FD is ignored; a relative path is reckoned off of the directory (!)
	identified by the respective FD.

	\param oldFD The FD of the old location. May be < 0.
	\param oldPath The absolute or relative path of the old location. Must not
		   be \c NULL.
	\param newFD The FD of the new location. May be < 0.
	\param newPath The absolute or relative path of the new location. Must not
		   be \c NULL.
	\return \c B_OK, if the entry has been moved successfully, another
			error code otherwise.
*/
/**
 * @brief Kernel syscall: rename a directory entry.
 *
 * @param oldFD   FD for the source directory.
 * @param oldPath Source path.
 * @param newFD   FD for the destination directory.
 * @param newPath Destination path.
 * @retval B_OK Entry renamed.
 */
status_t
_kern_rename(int oldFD, const char* oldPath, int newFD, const char* newPath)
{
	KPath oldPathBuffer(oldPath);
	KPath newPathBuffer(newPath);
	if (oldPathBuffer.InitCheck() != B_OK || newPathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	return common_rename(oldFD, oldPathBuffer.LockBuffer(),
		newFD, newPathBuffer.LockBuffer(), true);
}


/**
 * @brief Kernel syscall: check access permissions for a path.
 *
 * @param fd                Base FD for relative paths.
 * @param path              Path to check.
 * @param mode              R_OK, W_OK, X_OK, F_OK.
 * @param effectiveUserGroup If true, use effective UID/GID.
 * @retval B_OK Access permitted.
 */
status_t
_kern_access(int fd, const char* path, int mode, bool effectiveUserGroup)
{
	KPath pathBuffer(path, KPath::LAZY_ALLOC);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	return common_access(fd, pathBuffer.LockBuffer(), mode, effectiveUserGroup,
		true);
}


/*!	\brief Reads stat data of an entity specified by a FD + path pair.

	If only \a fd is given, the stat operation associated with the type
	of the FD (node, attr, attr dir etc.) is performed. If only \a path is
	given, this path identifies the entry for whose node to retrieve the
	stat data. If both \a fd and \a path are given and the path is absolute,
	\a fd is ignored; a relative path is reckoned off of the directory (!)
	identified by \a fd and specifies the entry whose stat data shall be
	retrieved.

	\param fd The FD. May be < 0.
	\param path The absolute or relative path. Must not be \c NULL.
	\param traverseLeafLink If \a path is given, \c true specifies that the
		   function shall not stick to symlinks, but traverse them.
	\param stat The buffer the stat data shall be written into.
	\param statSize The size of the supplied stat buffer.
	\return \c B_OK, if the the stat data have been read successfully, another
			error code otherwise.
*/
/**
 * @brief Kernel syscall: read stat data by (fd, path).
 *
 * Supports a smaller @p statSize for future ABI extension.
 *
 * @param fd               FD or base FD for relative paths.
 * @param path             Optional path.
 * @param traverseLeafLink If true, follow the final symlink.
 * @param stat             Output stat buffer.
 * @param statSize         sizeof(*stat) or smaller.
 * @retval B_OK Stat filled.
 */
status_t
_kern_read_stat(int fd, const char* path, bool traverseLeafLink,
	struct stat* stat, size_t statSize)
{
	struct stat completeStat;
	struct stat* originalStat = NULL;
	status_t status;

	if (statSize > sizeof(struct stat))
		return B_BAD_VALUE;

	// this supports different stat extensions
	if (statSize < sizeof(struct stat)) {
		originalStat = stat;
		stat = &completeStat;
	}

	status = vfs_read_stat(fd, path, traverseLeafLink, stat, true);

	if (status == B_OK && originalStat != NULL)
		memcpy(originalStat, stat, statSize);

	return status;
}


/*!	\brief Writes stat data of an entity specified by a FD + path pair.

	If only \a fd is given, the stat operation associated with the type
	of the FD (node, attr, attr dir etc.) is performed. If only \a path is
	given, this path identifies the entry for whose node to write the
	stat data. If both \a fd and \a path are given and the path is absolute,
	\a fd is ignored; a relative path is reckoned off of the directory (!)
	identified by \a fd and specifies the entry whose stat data shall be
	written.

	\param fd The FD. May be < 0.
	\param path The absolute or relative path. May be \c NULL.
	\param traverseLeafLink If \a path is given, \c true specifies that the
		   function shall not stick to symlinks, but traverse them.
	\param stat The buffer containing the stat data to be written.
	\param statSize The size of the supplied stat buffer.
	\param statMask A mask specifying which parts of the stat data shall be
		   written.
	\return \c B_OK, if the the stat data have been written successfully,
			another error code otherwise.
*/
/**
 * @brief Kernel syscall: write stat data by (fd, path).
 *
 * Supports a smaller @p statSize by zero-extending to sizeof(struct stat).
 *
 * @param fd               FD or base FD for relative paths.
 * @param path             Optional path.
 * @param traverseLeafLink If true, follow the final symlink.
 * @param stat             New stat values.
 * @param statSize         sizeof(*stat) or smaller.
 * @param statMask         B_STAT_* bitmask.
 * @retval B_OK Updated.
 */
status_t
_kern_write_stat(int fd, const char* path, bool traverseLeafLink,
	const struct stat* stat, size_t statSize, int statMask)
{
	struct stat completeStat;

	if (statSize > sizeof(struct stat))
		return B_BAD_VALUE;

	// this supports different stat extensions
	if (statSize < sizeof(struct stat)) {
		memset((uint8*)&completeStat + statSize, 0,
			sizeof(struct stat) - statSize);
		memcpy(&completeStat, stat, statSize);
		stat = &completeStat;
	}

	status_t status;

	if (path != NULL) {
		// path given: write the stat of the node referred to by (fd, path)
		KPath pathBuffer(path);
		if (pathBuffer.InitCheck() != B_OK)
			return B_NO_MEMORY;

		status = common_path_write_stat(fd, pathBuffer.LockBuffer(),
			traverseLeafLink, stat, statMask, true);
	} else {
		// no path given: get the FD and use the FD operation
		FileDescriptorPutter descriptor
			(get_fd(get_current_io_context(true), fd));
		if (!descriptor.IsSet())
			return B_FILE_ERROR;

		if (descriptor->ops->fd_write_stat)
			status = descriptor->ops->fd_write_stat(descriptor.Get(), stat, statMask);
		else
			status = B_UNSUPPORTED;
	}

	return status;
}


/**
 * @brief Kernel syscall: open the attribute directory of a node.
 *
 * @param fd               Base FD.
 * @param path             Path to the node.
 * @param traverseLeafLink Follow final symlink if true.
 * @return A non-negative FD, or a negative error code.
 */
int
_kern_open_attr_dir(int fd, const char* path, bool traverseLeafLink)
{
	KPath pathBuffer(path, KPath::LAZY_ALLOC);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	return attr_dir_open(fd, pathBuffer.LockBuffer(), traverseLeafLink, true);
}


/**
 * @brief Kernel syscall: open or create an extended attribute.
 *
 * @param fd      Base FD.
 * @param path    Path to the node.
 * @param name    Attribute name.
 * @param type    Attribute type (when creating).
 * @param openMode Open flags (O_CREAT to create).
 * @return A non-negative attribute FD, or a negative error code.
 */
int
_kern_open_attr(int fd, const char* path, const char* name, uint32 type,
	int openMode)
{
	KPath pathBuffer(path, KPath::LAZY_ALLOC);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	if ((openMode & O_CREAT) != 0) {
		return attr_create(fd, pathBuffer.LockBuffer(), name, type, openMode,
			true);
	}

	return attr_open(fd, pathBuffer.LockBuffer(), name, openMode, true);
}


/**
 * @brief Kernel syscall: remove an extended attribute by name.
 *
 * @param fd   FD of the owning node.
 * @param name Attribute name.
 * @retval B_OK Attribute removed.
 */
status_t
_kern_remove_attr(int fd, const char* name)
{
	return attr_remove(fd, name, true);
}


/**
 * @brief Kernel syscall: rename an extended attribute.
 *
 * @param fromFile FD of the source node.
 * @param fromName Source attribute name.
 * @param toFile   FD of the destination node.
 * @param toName   Destination attribute name.
 * @retval B_OK Attribute renamed.
 */
status_t
_kern_rename_attr(int fromFile, const char* fromName, int toFile,
	const char* toName)
{
	return attr_rename(fromFile, fromName, toFile, toName, true);
}


/**
 * @brief Kernel syscall: open the index directory of a volume.
 *
 * @param device Device ID of the volume.
 * @return A non-negative FD, or a negative error code.
 */
int
_kern_open_index_dir(dev_t device)
{
	return index_dir_open(device, true);
}


/**
 * @brief Kernel syscall: create a file-system index.
 *
 * @param device Device ID.
 * @param name   Index name.
 * @param type   Index type.
 * @param flags  FS-specific flags.
 * @retval B_OK Index created.
 */
status_t
_kern_create_index(dev_t device, const char* name, uint32 type, uint32 flags)
{
	return index_create(device, name, type, flags, true);
}


/**
 * @brief Kernel syscall: read stat information for a named index.
 *
 * @param device Device ID.
 * @param name   Index name.
 * @param stat   Output stat buffer.
 * @retval B_OK Stat filled.
 */
status_t
_kern_read_index_stat(dev_t device, const char* name, struct stat* stat)
{
	return index_name_read_stat(device, name, stat, true);
}


/**
 * @brief Kernel syscall: remove a file-system index by name.
 *
 * @param device Device ID.
 * @param name   Index name.
 * @retval B_OK Index removed.
 */
status_t
_kern_remove_index(dev_t device, const char* name)
{
	return index_remove(device, name, true);
}


/**
 * @brief Kernel syscall: get the current working directory path.
 *
 * @param buffer Output buffer.
 * @param size   Size of @p buffer.
 * @retval B_OK Path written.
 */
status_t
_kern_getcwd(char* buffer, size_t size)
{
	TRACE(("_kern_getcwd: buf %p, %ld\n", buffer, size));

	// Call vfs to get current working directory
	return get_cwd(buffer, size, true);
}


/**
 * @brief Kernel syscall: set the current working directory.
 *
 * @param fd   Base FD for relative paths (or AT_FDCWD).
 * @param path New working directory path.
 * @retval B_OK CWD changed.
 */
status_t
_kern_setcwd(int fd, const char* path)
{
	KPath pathBuffer(path, KPath::LAZY_ALLOC);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	return set_cwd(fd, pathBuffer.LockBuffer(), true);
}


//	#pragma mark - userland syscalls


/**
 * @brief User syscall: mount a file system.
 *
 * Copies all string arguments from user space, validates pointers, and
 * delegates to fs_mount().
 *
 * @param userPath       User-space mount point path.
 * @param userDevice     User-space block device path (may be NULL).
 * @param userFileSystem User-space FS type name (may be NULL).
 * @param flags          Mount flags.
 * @param userArgs       User-space FS-specific argument string (may be NULL).
 * @param argsLength     Length of @p userArgs (capped at 65535).
 * @return New device ID on success, or a negative error code.
 */
dev_t
_user_mount(const char* userPath, const char* userDevice,
	const char* userFileSystem, uint32 flags, const char* userArgs,
	size_t argsLength)
{
	char fileSystem[B_FILE_NAME_LENGTH];
	KPath path, device;
	char* args = NULL;
	status_t status;

	if (!IS_USER_ADDRESS(userPath))
		return B_BAD_ADDRESS;

	if (path.InitCheck() != B_OK || device.InitCheck() != B_OK)
		return B_NO_MEMORY;

	status = user_copy_name(path.LockBuffer(), userPath,
		B_PATH_NAME_LENGTH);
	if (status != B_OK)
		return status;
	path.UnlockBuffer();

	if (userFileSystem != NULL) {
		if (!IS_USER_ADDRESS(userFileSystem))
			return B_BAD_ADDRESS;

		status = user_copy_name(fileSystem, userFileSystem, sizeof(fileSystem));
		if (status != B_OK)
			return status;
	}

	if (userDevice != NULL) {
		if (!IS_USER_ADDRESS(userDevice))
			return B_BAD_ADDRESS;

		status = user_copy_name(device.LockBuffer(), userDevice,
			B_PATH_NAME_LENGTH);
		if (status != B_OK)
			return status;
		device.UnlockBuffer();
	}

	if (userArgs != NULL && argsLength > 0) {
		if (!IS_USER_ADDRESS(userArgs))
			return B_BAD_ADDRESS;

		// this is a safety restriction
		if (argsLength >= 65536)
			return B_NAME_TOO_LONG;

		args = (char*)malloc(argsLength + 1);
		if (args == NULL)
			return B_NO_MEMORY;

		status = user_copy_name(args, userArgs, argsLength + 1);
		if (status != B_OK) {
			free(args);
			return status;
		}
	}

	status = fs_mount(path.LockBuffer(),
		userDevice != NULL ? device.Path() : NULL,
		userFileSystem ? fileSystem : NULL, flags, args, false);

	free(args);
	return status;
}


/**
 * @brief User syscall: unmount a file system by path.
 *
 * @param userPath User-space mount point path.
 * @param flags    Unmount flags (B_UNMOUNT_BUSY_PARTITION is stripped).
 * @retval B_OK Volume unmounted.
 */
status_t
_user_unmount(const char* userPath, uint32 flags)
{
	if (!IS_USER_ADDRESS(userPath))
		return B_BAD_ADDRESS;

	KPath pathBuffer;
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char* path = pathBuffer.LockBuffer();

	status_t status = user_copy_name(path, userPath, B_PATH_NAME_LENGTH);
	if (status != B_OK)
		return status;

	return fs_unmount(path, -1, flags & ~B_UNMOUNT_BUSY_PARTITION, false);
}


/**
 * @brief User syscall: read file system information.
 *
 * Fills a kernel fs_info buffer and copies it to @p userInfo.
 *
 * @param device   Device ID.
 * @param userInfo User-space output buffer.
 * @retval B_OK Info copied to user space.
 */
status_t
_user_read_fs_info(dev_t device, struct fs_info* userInfo)
{
	struct fs_info info;
	status_t status;

	if (userInfo == NULL)
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userInfo))
		return B_BAD_ADDRESS;

	status = fs_read_info(device, &info);
	if (status != B_OK)
		return status;

	if (user_memcpy(userInfo, &info, sizeof(struct fs_info)) != B_OK)
		return B_BAD_ADDRESS;

	return B_OK;
}


/**
 * @brief User syscall: write file system information.
 *
 * @param device   Device ID.
 * @param userInfo User-space fs_info buffer with new values.
 * @param mask     Fields to update.
 * @retval B_OK Updated.
 */
status_t
_user_write_fs_info(dev_t device, const struct fs_info* userInfo, int mask)
{
	struct fs_info info;

	if (userInfo == NULL)
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userInfo)
		|| user_memcpy(&info, userInfo, sizeof(struct fs_info)) != B_OK)
		return B_BAD_ADDRESS;

	return fs_write_info(device, &info, mask);
}


/**
 * @brief User syscall: iterate over mounted devices.
 *
 * @param _userCookie User-space in/out enumeration cookie.
 * @return Next device ID, or B_BAD_VALUE when exhausted.
 */
dev_t
_user_next_device(int32* _userCookie)
{
	int32 cookie;
	dev_t device;

	if (!IS_USER_ADDRESS(_userCookie)
		|| user_memcpy(&cookie, _userCookie, sizeof(int32)) != B_OK)
		return B_BAD_ADDRESS;

	device = fs_next_device(&cookie);

	if (device >= B_OK) {
		// update user cookie
		if (user_memcpy(_userCookie, &cookie, sizeof(int32)) != B_OK)
			return B_BAD_ADDRESS;
	}

	return device;
}


/**
 * @brief User syscall: synchronize all mounted file systems.
 *
 * @retval B_OK Always.
 */
status_t
_user_sync(void)
{
	return _kern_sync();
}


/**
 * @brief User syscall: enumerate open file descriptors of a team (root only).
 *
 * @param team      Team to inspect.
 * @param userCookie User-space in/out FD slot index.
 * @param userInfo  User-space fd_info output.
 * @param infoSize  Must equal sizeof(fd_info).
 * @retval B_OK         Info written.
 * @retval B_NOT_ALLOWED Caller is not root.
 */
status_t
_user_get_next_fd_info(team_id team, uint32* userCookie, fd_info* userInfo,
	size_t infoSize)
{
	struct fd_info info;
	uint32 cookie;

	// only root can do this
	if (geteuid() != 0)
		return B_NOT_ALLOWED;

	if (infoSize != sizeof(fd_info))
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userCookie) || !IS_USER_ADDRESS(userInfo)
		|| user_memcpy(&cookie, userCookie, sizeof(uint32)) != B_OK)
		return B_BAD_ADDRESS;

	status_t status = _kern_get_next_fd_info(team, &cookie, &info, infoSize);
	if (status != B_OK)
		return status;

	if (user_memcpy(userCookie, &cookie, sizeof(uint32)) != B_OK
		|| user_memcpy(userInfo, &info, infoSize) != B_OK)
		return B_BAD_ADDRESS;

	return status;
}


/**
 * @brief User syscall: convert a device/inode/leaf tuple to an absolute path.
 *
 * @param device     Device ID.
 * @param inode      Node ID.
 * @param leaf       Optional leaf entry name (user space, may be NULL).
 * @param userPath   User-space output buffer.
 * @param pathLength Size of @p userPath.
 * @retval B_OK             Path written to user space.
 * @retval B_BUFFER_OVERFLOW Path exceeds @p pathLength.
 */
status_t
_user_entry_ref_to_path(dev_t device, ino_t inode, const char* leaf,
	char* userPath, size_t pathLength)
{
	if (!IS_USER_ADDRESS(userPath))
		return B_BAD_ADDRESS;

	KPath path;
	if (path.InitCheck() != B_OK)
		return B_NO_MEMORY;

	// copy the leaf name onto the stack
	char stackLeaf[B_FILE_NAME_LENGTH];
	if (leaf != NULL) {
		if (!IS_USER_ADDRESS(leaf))
			return B_BAD_ADDRESS;

		int status = user_copy_name(stackLeaf, leaf, B_FILE_NAME_LENGTH);
		if (status != B_OK)
			return status;

		leaf = stackLeaf;
	}

	status_t status = vfs_entry_ref_to_path(device, inode, leaf,
		false, path.LockBuffer(), path.BufferSize());
	if (status != B_OK)
		return status;

	path.UnlockBuffer();

	int length = user_strlcpy(userPath, path.Path(), pathLength);
	if (length < 0)
		return length;
	if (length >= (int)pathLength)
		return B_BUFFER_OVERFLOW;

	return B_OK;
}


/**
 * @brief User syscall: normalize a path to its canonical form.
 *
 * @param userPath     User-space input path.
 * @param traverseLink If true, follow the final symlink.
 * @param buffer       User-space output buffer (B_PATH_NAME_LENGTH).
 * @retval B_OK Normalized path written.
 */
status_t
_user_normalize_path(const char* userPath, bool traverseLink, char* buffer)
{
	if (userPath == NULL || buffer == NULL)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(userPath) || !IS_USER_ADDRESS(buffer))
		return B_BAD_ADDRESS;

	// copy path from userland
	KPath pathBuffer;
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;
	char* path = pathBuffer.LockBuffer();

	status_t status = user_copy_name(path, userPath, B_PATH_NAME_LENGTH);
	if (status != B_OK)
		return status;

	status_t error = normalize_path(path, pathBuffer.BufferSize(), traverseLink,
		false);
	if (error != B_OK)
		return error;

	// copy back to userland
	int len = user_strlcpy(buffer, path, B_PATH_NAME_LENGTH);
	if (len < 0)
		return len;
	if (len >= B_PATH_NAME_LENGTH)
		return B_BUFFER_OVERFLOW;

	return B_OK;
}


/**
 * @brief User syscall: open or create a file by entry_ref.
 *
 * @param device   Device ID of the parent directory.
 * @param inode    Node ID of the parent directory.
 * @param userName User-space entry name string.
 * @param openMode Open flags.
 * @param perms    Permissions (used when O_CREAT is set).
 * @return A non-negative FD, or a negative error code.
 */
int
_user_open_entry_ref(dev_t device, ino_t inode, const char* userName,
	int openMode, int perms)
{
	char name[B_FILE_NAME_LENGTH];

	if (userName == NULL || device < 0 || inode < 0)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(userName))
		return B_BAD_ADDRESS;
	status_t status = user_copy_name(name, userName, sizeof(name));
	if (status != B_OK)
		return status;

	if ((openMode & O_CREAT) != 0) {
		return file_create_entry_ref(device, inode, name, openMode, perms,
			false);
	}

	return file_open_entry_ref(device, inode, name, openMode, false);
}


/**
 * @brief User syscall: open or create a file by (fd, path).
 *
 * @param fd       Base FD for relative paths.
 * @param userPath User-space path.
 * @param openMode Open flags.
 * @param perms    Permissions (when O_CREAT is set).
 * @return A non-negative FD, or a negative error code.
 */
int
_user_open(int fd, const char* userPath, int openMode, int perms)
{
	KPath path;
	if (path.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char* buffer = path.LockBuffer();

	if (!IS_USER_ADDRESS(userPath))
		return B_BAD_ADDRESS;
	status_t status = user_copy_name(buffer, userPath, B_PATH_NAME_LENGTH);
	if (status != B_OK)
		return status;

	if ((openMode & O_CREAT) != 0)
		return file_create(fd, buffer, openMode, perms, false);

	return file_open(fd, buffer, openMode, false);
}


/**
 * @brief User syscall: open a directory by entry_ref or node_ref.
 *
 * @param device   Device ID.
 * @param inode    Node ID.
 * @param userName User-space entry name (may be NULL for node_ref).
 * @return A non-negative FD, or a negative error code.
 */
int
_user_open_dir_entry_ref(dev_t device, ino_t inode, const char* userName)
{
	if (userName != NULL) {
		char name[B_FILE_NAME_LENGTH];

		if (!IS_USER_ADDRESS(userName))
			return B_BAD_ADDRESS;
		status_t status = user_copy_name(name, userName, sizeof(name));
		if (status != B_OK)
			return status;

		return dir_open_entry_ref(device, inode, name, false);
	}
	return dir_open_entry_ref(device, inode, NULL, false);
}


/**
 * @brief User syscall: open a directory by (fd, path).
 *
 * @param fd       Base FD for relative paths.
 * @param userPath User-space path (may be NULL to open @p fd itself).
 * @return A non-negative FD, or a negative error code.
 */
int
_user_open_dir(int fd, const char* userPath)
{
	if (userPath == NULL)
		return dir_open(fd, NULL, false);

	KPath path;
	if (path.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char* buffer = path.LockBuffer();

	if (!IS_USER_ADDRESS(userPath))
		return B_BAD_ADDRESS;
	status_t status = user_copy_name(buffer, userPath, B_PATH_NAME_LENGTH);
	if (status != B_OK)
		return status;

	return dir_open(fd, buffer, false);
}


/*!	\brief Opens a directory's parent directory and returns the entry name
		   of the former.

	Aside from that it returns the directory's entry name, this method is
	equivalent to \code _user_open_dir(fd, "..") \endcode. It really is
	equivalent, if \a userName is \c NULL.

	If a name buffer is supplied and the name does not fit the buffer, the
	function fails. A buffer of size \c B_FILE_NAME_LENGTH should be safe.

	\param fd A FD referring to a directory.
	\param userName Buffer the directory's entry name shall be written into.
		   May be \c NULL.
	\param nameLength Size of the name buffer.
	\return The file descriptor of the opened parent directory, if everything
			went fine, an error code otherwise.
*/
/**
 * @brief User syscall: open a directory's parent and return the entry name.
 *
 * Opens the ".." directory relative to @p fd and optionally writes the
 * original directory's entry name to @p userName.
 *
 * @param fd         FD of the current directory.
 * @param userName   User-space buffer for the entry name (may be NULL).
 * @param nameLength Size of @p userName.
 * @return FD of the parent directory, or a negative error code.
 */
int
_user_open_parent_dir(int fd, char* userName, size_t nameLength)
{
	bool kernel = false;

	if (userName && !IS_USER_ADDRESS(userName))
		return B_BAD_ADDRESS;

	// open the parent dir
	int parentFD = dir_open(fd, (char*)"..", kernel);
	if (parentFD < 0)
		return parentFD;
	FDCloser fdCloser(parentFD, kernel);

	if (userName) {
		// get the vnodes
		struct vnode* parentVNode = get_vnode_from_fd(parentFD, kernel);
		struct vnode* dirVNode = get_vnode_from_fd(fd, kernel);
		VnodePutter parentVNodePutter(parentVNode);
		VnodePutter dirVNodePutter(dirVNode);
		if (!parentVNode || !dirVNode)
			return B_FILE_ERROR;

		// get the vnode name
		char _buffer[offsetof(struct dirent, d_name) + B_FILE_NAME_LENGTH + 1];
		struct dirent* buffer = (struct dirent*)_buffer;
		status_t status = get_vnode_name(dirVNode, parentVNode, buffer,
			sizeof(_buffer), get_current_io_context(false));
		if (status != B_OK)
			return status;

		// copy the name to the userland buffer
		int len = user_strlcpy(userName, buffer->d_name, nameLength);
		if (len < 0)
			return len;
		if (len >= (int)nameLength)
			return B_BUFFER_OVERFLOW;
	}

	return fdCloser.Detach();
}


/**
 * @brief User syscall: file control (fcntl).
 *
 * Handles F_SETLKW with syscall restart support.
 *
 * @param fd       File descriptor.
 * @param op       F_* command.
 * @param argument Command-specific argument.
 * @retval B_OK Operation succeeded.
 */
status_t
_user_fcntl(int fd, int op, size_t argument)
{
	status_t status = common_fcntl(fd, op, argument, false);
	if (op == F_SETLKW)
		syscall_restart_handle_post(status);

	return status;
}


/**
 * @brief User syscall: flush file data/metadata to storage.
 *
 * @param fd       File descriptor.
 * @param dataOnly true for fdatasync semantics.
 * @retval B_OK Flushed.
 */
status_t
_user_fsync(int fd, bool dataOnly)
{
	return common_sync(fd, dataOnly, false);
}


/**
 * @brief User syscall: BSD-style file lock (flock).
 *
 * Translates the BSD flock() operation (LOCK_SH, LOCK_EX, LOCK_UN, LOCK_NB)
 * into a POSIX-style flock advisory-lock call or an FS acquire/release_lock
 * hook call.
 *
 * @param fd        File descriptor.
 * @param operation LOCK_SH, LOCK_EX, LOCK_UN, optionally OR'd with LOCK_NB.
 * @retval B_OK        Lock acquired or released.
 * @retval B_BAD_VALUE @p fd is not a regular file FD, or invalid operation.
 */
status_t
_user_flock(int fd, int operation)
{
	FUNCTION(("_user_fcntl(fd = %d, op = %d)\n", fd, operation));

	// Check if the operation is valid
	switch (operation & ~LOCK_NB) {
		case LOCK_UN:
		case LOCK_SH:
		case LOCK_EX:
			break;

		default:
			return B_BAD_VALUE;
	}

	struct vnode* vnode;
	FileDescriptorPutter descriptor(get_fd_and_vnode(fd, &vnode, false));
	if (!descriptor.IsSet())
		return B_FILE_ERROR;

	if (descriptor->ops != &sFileOps)
		return B_BAD_VALUE;

	struct flock flock;
	flock.l_start = 0;
	flock.l_len = OFF_MAX;
	flock.l_whence = 0;
	flock.l_type = (operation & LOCK_SH) != 0 ? F_RDLCK : F_WRLCK;

	status_t status;
	if ((operation & LOCK_UN) != 0) {
		if (HAS_FS_CALL(vnode, release_lock))
			status = FS_CALL(vnode, release_lock, descriptor->cookie, &flock);
		else
			status = release_advisory_lock(vnode, NULL, descriptor.Get(), &flock);
	} else {
		if (HAS_FS_CALL(vnode, acquire_lock)) {
			status = FS_CALL(vnode, acquire_lock, descriptor->cookie, &flock,
				(operation & LOCK_NB) == 0);
		} else {
			status = acquire_advisory_lock(vnode, NULL, descriptor.Get(), &flock,
				(operation & LOCK_NB) == 0);
		}
	}

	syscall_restart_handle_post(status);

	return status;
}


/**
 * @brief User syscall: acquire a mandatory lock on a file's vnode.
 *
 * @param fd File descriptor.
 * @retval B_OK Lock acquired.
 */
status_t
_user_lock_node(int fd)
{
	return common_lock_node(fd, false);
}


/**
 * @brief User syscall: release a mandatory lock on a file's vnode.
 *
 * @param fd File descriptor.
 * @retval B_OK Lock released.
 */
status_t
_user_unlock_node(int fd)
{
	return common_unlock_node(fd, false);
}


/**
 * @brief User syscall: pre-allocate disk space for a file.
 *
 * @param fd     File descriptor.
 * @param offset Start offset.
 * @param length Bytes to pre-allocate.
 * @retval B_OK Space reserved.
 */
status_t
_user_preallocate(int fd, off_t offset, off_t length)
{
	return common_preallocate(fd, offset, length, false);
}


/**
 * @brief User syscall: create a directory by entry_ref.
 *
 * @param device   Device ID of the parent.
 * @param inode    Node ID of the parent.
 * @param userName User-space directory name.
 * @param perms    Permission bits.
 * @retval B_OK Directory created.
 */
status_t
_user_create_dir_entry_ref(dev_t device, ino_t inode, const char* userName,
	int perms)
{
	char name[B_FILE_NAME_LENGTH];
	status_t status;

	if (!IS_USER_ADDRESS(userName))
		return B_BAD_ADDRESS;

	status = user_copy_name(name, userName, sizeof(name));
	if (status != B_OK)
		return status;

	return dir_create_entry_ref(device, inode, name, perms, false);
}


/**
 * @brief User syscall: create a directory by (fd, path).
 *
 * @param fd       Base FD for relative paths.
 * @param userPath User-space directory path.
 * @param perms    Permission bits.
 * @retval B_OK Directory created.
 */
status_t
_user_create_dir(int fd, const char* userPath, int perms)
{
	KPath pathBuffer;
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char* path = pathBuffer.LockBuffer();

	if (!IS_USER_ADDRESS(userPath))
		return B_BAD_ADDRESS;
	status_t status = user_copy_name(path, userPath, B_PATH_NAME_LENGTH);
	if (status != B_OK)
		return status;

	return dir_create(fd, path, perms, false);
}


/**
 * @brief User syscall: remove a directory by (fd, path).
 *
 * @param fd       Base FD for relative paths.
 * @param userPath User-space path (may be NULL to use @p fd directly).
 * @retval B_OK Directory removed.
 */
status_t
_user_remove_dir(int fd, const char* userPath)
{
	KPath pathBuffer;
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char* path = pathBuffer.LockBuffer();

	if (userPath != NULL) {
		if (!IS_USER_ADDRESS(userPath))
			return B_BAD_ADDRESS;
		status_t status = user_copy_name(path, userPath, B_PATH_NAME_LENGTH);
		if (status != B_OK)
			return status;
	}

	return dir_remove(fd, userPath ? path : NULL, false);
}


/**
 * @brief User syscall: read the target of a symbolic link.
 *
 * @param fd             Base FD for relative paths.
 * @param userPath       User-space path to the symlink (may be NULL).
 * @param userBuffer     User-space destination buffer.
 * @param userBufferSize User-space in/out buffer size.
 * @retval B_OK Link target written to user space.
 */
status_t
_user_read_link(int fd, const char* userPath, char* userBuffer,
	size_t* userBufferSize)
{
	KPath pathBuffer, linkBuffer;
	if (pathBuffer.InitCheck() != B_OK || linkBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	size_t bufferSize;

	if (!IS_USER_ADDRESS(userBuffer) || !IS_USER_ADDRESS(userBufferSize)
		|| user_memcpy(&bufferSize, userBufferSize, sizeof(size_t)) != B_OK)
		return B_BAD_ADDRESS;

	char* path = pathBuffer.LockBuffer();
	char* buffer = linkBuffer.LockBuffer();

	if (userPath) {
		if (!IS_USER_ADDRESS(userPath))
			return B_BAD_ADDRESS;
		status_t status = user_copy_name(path, userPath, B_PATH_NAME_LENGTH);
		if (status != B_OK)
			return status;

		if (bufferSize > B_PATH_NAME_LENGTH)
			bufferSize = B_PATH_NAME_LENGTH;
	}

	size_t newBufferSize = bufferSize;
	status_t status = common_read_link(fd, userPath ? path : NULL, buffer,
		&newBufferSize, false);

	// we also update the bufferSize in case of errors
	// (the real length will be returned in case of B_BUFFER_OVERFLOW)
	if (user_memcpy(userBufferSize, &newBufferSize, sizeof(size_t)) != B_OK)
		return B_BAD_ADDRESS;

	if (status != B_OK)
		return status;

	bufferSize = min_c(newBufferSize, bufferSize);
	if (user_memcpy(userBuffer, buffer, bufferSize) != B_OK)
		return B_BAD_ADDRESS;

	return B_OK;
}


/**
 * @brief User syscall: create a symbolic link.
 *
 * @param fd         Base FD for relative @p userPath.
 * @param userPath   User-space path where the symlink is created.
 * @param userToPath User-space link target string.
 * @param mode       Permission bits.
 * @retval B_OK Symlink created.
 */
status_t
_user_create_symlink(int fd, const char* userPath, const char* userToPath,
	int mode)
{
	KPath pathBuffer;
	KPath toPathBuffer;
	if (pathBuffer.InitCheck() != B_OK || toPathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char* path = pathBuffer.LockBuffer();
	char* toPath = toPathBuffer.LockBuffer();

	if (!IS_USER_ADDRESS(userPath) || !IS_USER_ADDRESS(userToPath))
		return B_BAD_ADDRESS;
	status_t status = user_copy_name(path, userPath, B_PATH_NAME_LENGTH);
	if (status != B_OK)
		return status;
	status = user_copy_name(toPath, userToPath, B_PATH_NAME_LENGTH);
	if (status != B_OK)
		return status;

	return common_create_symlink(fd, path, toPath, mode, false);
}


/**
 * @brief User syscall: create a hard link.
 *
 * @param pathFD          FD for the directory of the new link.
 * @param userPath        User-space path for the new link.
 * @param toFD            FD for the directory of the target.
 * @param userToPath      User-space path of the existing node.
 * @param traverseLeafLink Follow final symlink in @p userToPath if true.
 * @retval B_OK Link created.
 */
status_t
_user_create_link(int pathFD, const char* userPath, int toFD,
	const char* userToPath, bool traverseLeafLink)
{
	KPath pathBuffer;
	KPath toPathBuffer;
	if (pathBuffer.InitCheck() != B_OK || toPathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char* path = pathBuffer.LockBuffer();
	char* toPath = toPathBuffer.LockBuffer();

	if (!IS_USER_ADDRESS(userPath) || !IS_USER_ADDRESS(userToPath))
		return B_BAD_ADDRESS;
	status_t status = user_copy_name(path, userPath, B_PATH_NAME_LENGTH);
	if (status != B_OK)
		return status;
	status = user_copy_name(toPath, userToPath, B_PATH_NAME_LENGTH);
	if (status != B_OK)
		return status;

	status = check_path(toPath);
	if (status != B_OK)
		return status;

	return common_create_link(pathFD, path, toFD, toPath, traverseLeafLink,
		false);
}


/**
 * @brief User syscall: remove a directory entry by (fd, path).
 *
 * @param fd       Base FD for relative paths.
 * @param userPath User-space path of the entry.
 * @retval B_OK Entry removed.
 */
status_t
_user_unlink(int fd, const char* userPath)
{
	KPath pathBuffer;
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char* path = pathBuffer.LockBuffer();

	if (!IS_USER_ADDRESS(userPath))
		return B_BAD_ADDRESS;
	status_t status = user_copy_name(path, userPath, B_PATH_NAME_LENGTH);
	if (status != B_OK)
		return status;

	return common_unlink(fd, path, false);
}


/**
 * @brief User syscall: rename a directory entry.
 *
 * @param oldFD       FD for the source directory.
 * @param userOldPath User-space source path.
 * @param newFD       FD for the destination directory.
 * @param userNewPath User-space destination path.
 * @retval B_OK Entry renamed.
 */
status_t
_user_rename(int oldFD, const char* userOldPath, int newFD,
	const char* userNewPath)
{
	KPath oldPathBuffer;
	KPath newPathBuffer;
	if (oldPathBuffer.InitCheck() != B_OK || newPathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char* oldPath = oldPathBuffer.LockBuffer();
	char* newPath = newPathBuffer.LockBuffer();

	if (!IS_USER_ADDRESS(userOldPath) || !IS_USER_ADDRESS(userNewPath))
		return B_BAD_ADDRESS;
	status_t status = user_copy_name(oldPath, userOldPath, B_PATH_NAME_LENGTH);
	if (status != B_OK)
		return status;
	status = user_copy_name(newPath, userNewPath, B_PATH_NAME_LENGTH);
	if (status != B_OK)
		return status;

	return common_rename(oldFD, oldPath, newFD, newPath, false);
}


/**
 * @brief User syscall: create a named pipe (FIFO) in the file system.
 *
 * @param fd       Base FD for relative @p userPath.
 * @param userPath User-space path for the new FIFO.
 * @param perms    Permission bits (S_IUMSK masked).
 * @retval B_OK         FIFO created.
 * @retval B_UNSUPPORTED FS has no create_special_node hook.
 */
status_t
_user_create_fifo(int fd, const char* userPath, mode_t perms)
{
	KPath pathBuffer;
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char* path = pathBuffer.LockBuffer();

	if (!IS_USER_ADDRESS(userPath))
		return B_BAD_ADDRESS;
	status_t status = user_copy_name(path, userPath, B_PATH_NAME_LENGTH);
	if (status != B_OK)
		return status;

	// split into directory vnode and filename path
	char filename[B_FILE_NAME_LENGTH];
	VnodePutter dir;
	status = fd_and_path_to_dir_vnode(fd, path, dir, filename, false);
	if (status != B_OK)
		return status;

	// the underlying FS needs to support creating FIFOs
	if (!HAS_FS_CALL(dir, create_special_node))
		return B_UNSUPPORTED;

	// create the entry	-- the FIFO sub node is set up automatically
	fs_vnode superVnode;
	ino_t nodeID;
	status = FS_CALL(dir.Get(), create_special_node, filename, NULL,
		S_IFIFO | (perms & S_IUMSK), 0, &superVnode, &nodeID);

	// create_special_node() acquired a reference for us that we don't need.
	if (status == B_OK)
		put_vnode(dir->mount->volume, nodeID);

	return status;
}


/**
 * @brief User syscall: create an anonymous pipe.
 *
 * Creates an unnamed FIFO node in the root FS and opens it for reading
 * (fds[0]) and writing (fds[1]).
 *
 * @param userFDs User-space int[2] output for the read/write FDs.
 * @param flags   O_NONBLOCK, O_CLOEXEC, O_CLOFORK (other flags rejected).
 * @retval B_OK         Pipe created; FDs written to @p userFDs.
 * @retval B_BAD_VALUE  Unsupported flags.
 * @retval B_UNSUPPORTED Root FS has no create_special_node hook.
 */
status_t
_user_create_pipe(int* userFDs, int flags)
{
	// check acceptable flags
	if ((flags & ~(O_NONBLOCK | O_CLOEXEC | O_CLOFORK)) != 0)
		return B_BAD_VALUE;

	// rootfs should support creating FIFOs, but let's be sure
	if (!HAS_FS_CALL(sRoot, create_special_node))
		return B_UNSUPPORTED;

	// create the node	-- the FIFO sub node is set up automatically
	fs_vnode superVnode;
	ino_t nodeID;
	status_t status = FS_CALL(sRoot, create_special_node, NULL, NULL,
		S_IFIFO | S_IRUSR | S_IWUSR, 0, &superVnode, &nodeID);
	if (status != B_OK)
		return status;

	// We've got one reference to the node and need another one.
	struct vnode* vnode;
	status = get_vnode(sRoot->mount->id, nodeID, &vnode, true, false);
	if (status != B_OK) {
		// that should not happen
		dprintf("_user_create_pipe(): Failed to lookup vnode (%" B_PRIdDEV ", "
			"%" B_PRIdINO ")\n", sRoot->mount->id, sRoot->id);
		return status;
	}

	// Everything looks good so far. Open two FDs for reading respectively
	// writing, O_NONBLOCK to avoid blocking on open with O_RDONLY
	int fds[2];
	fds[0] = open_vnode(vnode, O_RDONLY | O_NONBLOCK | flags, false);
	fds[1] = open_vnode(vnode, O_WRONLY | flags, false);
	// Reset O_NONBLOCK if requested
	if ((flags & O_NONBLOCK) == 0)
		common_fcntl(fds[0], F_SETFL, flags & O_NONBLOCK, false);

	FDCloser closer0(fds[0], false);
	FDCloser closer1(fds[1], false);

	status = (fds[0] >= 0 ? (fds[1] >= 0 ? B_OK : fds[1]) : fds[0]);

	// copy FDs to userland
	if (status == B_OK) {
		if (!IS_USER_ADDRESS(userFDs)
			|| user_memcpy(userFDs, fds, sizeof(fds)) != B_OK) {
			status = B_BAD_ADDRESS;
		}
	}

	// keep FDs, if everything went fine
	if (status == B_OK) {
		closer0.Detach();
		closer1.Detach();
	}

	return status;
}


/**
 * @brief User syscall: check access permissions for a path.
 *
 * @param fd                Base FD for relative paths.
 * @param userPath          User-space path.
 * @param mode              R_OK, W_OK, X_OK, F_OK.
 * @param effectiveUserGroup If true, use effective UID/GID.
 * @retval B_OK Access permitted.
 */
status_t
_user_access(int fd, const char* userPath, int mode, bool effectiveUserGroup)
{
	KPath pathBuffer;
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char* path = pathBuffer.LockBuffer();

	if (!IS_USER_ADDRESS(userPath))
		return B_BAD_ADDRESS;
	status_t status = user_copy_name(path, userPath, B_PATH_NAME_LENGTH);
	if (status != B_OK)
		return status;

	return common_access(fd, path, mode, effectiveUserGroup, false);
}


/**
 * @brief User syscall: read stat data by (fd, path) from user space.
 *
 * @param fd           FD or base FD for relative paths.
 * @param userPath     User-space path (may be NULL).
 * @param traverseLink Follow final symlink if true.
 * @param userStat     User-space output stat buffer.
 * @param statSize     sizeof(*userStat) or smaller.
 * @retval B_OK Stat written to user space.
 */
status_t
_user_read_stat(int fd, const char* userPath, bool traverseLink,
	struct stat* userStat, size_t statSize)
{
	struct stat stat = {0};
	status_t status;

	if (statSize > sizeof(struct stat))
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userStat))
		return B_BAD_ADDRESS;

	if (userPath != NULL) {
		// path given: get the stat of the node referred to by (fd, path)
		if (!IS_USER_ADDRESS(userPath))
			return B_BAD_ADDRESS;

		KPath pathBuffer;
		if (pathBuffer.InitCheck() != B_OK)
			return B_NO_MEMORY;

		char* path = pathBuffer.LockBuffer();

		status = user_copy_name(path, userPath, B_PATH_NAME_LENGTH);
		if (status != B_OK)
			return status;

		status = common_path_read_stat(fd, path, traverseLink, &stat, false);
	} else {
		// no path given: get the FD and use the FD operation
		FileDescriptorPutter descriptor
			(get_fd(get_current_io_context(false), fd));
		if (!descriptor.IsSet())
			return B_FILE_ERROR;

		if (descriptor->ops->fd_read_stat)
			status = descriptor->ops->fd_read_stat(descriptor.Get(), &stat);
		else
			status = B_UNSUPPORTED;
	}

	if (status != B_OK)
		return status;

	return user_memcpy(userStat, &stat, statSize);
}


/**
 * @brief User syscall: write stat data by (fd, path) from user space.
 *
 * @param fd              FD or base FD for relative paths.
 * @param userPath        User-space path (may be NULL).
 * @param traverseLeafLink Follow final symlink if true.
 * @param userStat        User-space input stat buffer.
 * @param statSize        sizeof(*userStat) or smaller.
 * @param statMask        B_STAT_* bitmask.
 * @retval B_OK Updated.
 */
status_t
_user_write_stat(int fd, const char* userPath, bool traverseLeafLink,
	const struct stat* userStat, size_t statSize, int statMask)
{
	if (statSize > sizeof(struct stat))
		return B_BAD_VALUE;

	struct stat stat;

	if (!IS_USER_ADDRESS(userStat)
		|| user_memcpy(&stat, userStat, statSize) < B_OK)
		return B_BAD_ADDRESS;

	// clear additional stat fields
	if (statSize < sizeof(struct stat))
		memset((uint8*)&stat + statSize, 0, sizeof(struct stat) - statSize);

	status_t status;

	if (userPath != NULL) {
		// path given: write the stat of the node referred to by (fd, path)
		if (!IS_USER_ADDRESS(userPath))
			return B_BAD_ADDRESS;

		KPath pathBuffer;
		if (pathBuffer.InitCheck() != B_OK)
			return B_NO_MEMORY;

		char* path = pathBuffer.LockBuffer();

		status = user_copy_name(path, userPath, B_PATH_NAME_LENGTH);
		if (status != B_OK)
			return status;

		status = common_path_write_stat(fd, path, traverseLeafLink, &stat,
			statMask, false);
	} else {
		// no path given: get the FD and use the FD operation
		FileDescriptorPutter descriptor
			(get_fd(get_current_io_context(false), fd));
		if (!descriptor.IsSet())
			return B_FILE_ERROR;

		if (descriptor->ops->fd_write_stat) {
			status = descriptor->ops->fd_write_stat(descriptor.Get(), &stat,
				statMask);
		} else
			status = B_UNSUPPORTED;
	}

	return status;
}


/**
 * @brief User syscall: open an attribute directory by (fd, path).
 *
 * @param fd               Base FD for relative paths.
 * @param userPath         User-space path (may be NULL to use @p fd).
 * @param traverseLeafLink Follow final symlink if true.
 * @return A non-negative FD, or a negative error code.
 */
int
_user_open_attr_dir(int fd, const char* userPath, bool traverseLeafLink)
{
	KPath pathBuffer;
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char* path = pathBuffer.LockBuffer();

	if (userPath != NULL) {
		if (!IS_USER_ADDRESS(userPath))
			return B_BAD_ADDRESS;
		status_t status = user_copy_name(path, userPath, B_PATH_NAME_LENGTH);
		if (status != B_OK)
			return status;
	}

	return attr_dir_open(fd, userPath ? path : NULL, traverseLeafLink, false);
}


/**
 * @brief User syscall: read an extended attribute value into user space.
 *
 * Opens the attribute FD, reads @p readBytes at @p pos, closes it.
 *
 * @param fd            Node FD.
 * @param userAttribute User-space attribute name.
 * @param pos           Byte offset within the attribute.
 * @param userBuffer    User-space destination buffer.
 * @param readBytes     Bytes to read.
 * @return Bytes read, or a negative error code.
 */
ssize_t
_user_read_attr(int fd, const char* userAttribute, off_t pos, void* userBuffer,
	size_t readBytes)
{
	char attribute[B_FILE_NAME_LENGTH];

	if (userAttribute == NULL)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(userAttribute))
		return B_BAD_ADDRESS;
	status_t status = user_copy_name(attribute, userAttribute, sizeof(attribute));
	if (status != B_OK)
		return status;

	int attr = attr_open(fd, NULL, attribute, O_RDONLY, false);
	if (attr < 0)
		return attr;

	ssize_t bytes = _user_read(attr, pos, userBuffer, readBytes);
	_user_close(attr);

	return bytes;
}


/**
 * @brief User syscall: write an extended attribute value from user space.
 *
 * Creates or opens the attribute (O_CREAT | O_WRONLY; O_TRUNC when pos==0),
 * writes @p writeBytes at @p pos, closes it.
 *
 * @param fd            Node FD.
 * @param userAttribute User-space attribute name.
 * @param type          Attribute type code.
 * @param pos           Byte offset within the attribute.
 * @param buffer        User-space source buffer.
 * @param writeBytes    Bytes to write.
 * @return Bytes written, or a negative error code.
 */
ssize_t
_user_write_attr(int fd, const char* userAttribute, uint32 type, off_t pos,
	const void* buffer, size_t writeBytes)
{
	char attribute[B_FILE_NAME_LENGTH];

	if (userAttribute == NULL)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(userAttribute))
		return B_BAD_ADDRESS;
	status_t status = user_copy_name(attribute, userAttribute, sizeof(attribute));
	if (status != B_OK)
		return status;

	// Try to support the BeOS typical truncation as well as the position
	// argument
	int attr = attr_create(fd, NULL, attribute, type,
		O_CREAT | O_WRONLY | (pos != 0 ? 0 : O_TRUNC), false);
	if (attr < 0)
		return attr;

	ssize_t bytes = _user_write(attr, pos, buffer, writeBytes);
	_user_close(attr);

	return bytes;
}


/**
 * @brief User syscall: read stat (type/size) for a named attribute.
 *
 * @param fd            Node FD.
 * @param userAttribute User-space attribute name.
 * @param userAttrInfo  User-space output attr_info buffer.
 * @retval B_OK Attribute info written to user space.
 */
status_t
_user_stat_attr(int fd, const char* userAttribute,
	struct attr_info* userAttrInfo)
{
	char attribute[B_FILE_NAME_LENGTH];

	if (userAttribute == NULL || userAttrInfo == NULL)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(userAttribute) || !IS_USER_ADDRESS(userAttrInfo))
		return B_BAD_ADDRESS;
	status_t status = user_copy_name(attribute, userAttribute,
		sizeof(attribute));
	if (status != B_OK)
		return status;

	int attr = attr_open(fd, NULL, attribute, O_RDONLY, false);
	if (attr < 0)
		return attr;

	struct file_descriptor* descriptor
		= get_fd(get_current_io_context(false), attr);
	if (descriptor == NULL) {
		_user_close(attr);
		return B_FILE_ERROR;
	}

	struct stat stat;
	if (descriptor->ops->fd_read_stat)
		status = descriptor->ops->fd_read_stat(descriptor, &stat);
	else
		status = B_UNSUPPORTED;

	put_fd(descriptor);
	_user_close(attr);

	if (status == B_OK) {
		attr_info info;
		info.type = stat.st_type;
		info.size = stat.st_size;

		if (user_memcpy(userAttrInfo, &info, sizeof(struct attr_info)) != B_OK)
			return B_BAD_ADDRESS;
	}

	return status;
}


/**
 * @brief User syscall: open or create an extended attribute from user space.
 *
 * @param fd      Base FD for relative paths.
 * @param userPath User-space path to the node (may be NULL).
 * @param userName User-space attribute name.
 * @param type    Attribute type (when creating).
 * @param openMode Open flags (O_CREAT to create).
 * @return A non-negative attribute FD, or a negative error code.
 */
int
_user_open_attr(int fd, const char* userPath, const char* userName,
	uint32 type, int openMode)
{
	char name[B_FILE_NAME_LENGTH];

	if (!IS_USER_ADDRESS(userName))
		return B_BAD_ADDRESS;
	status_t status = user_copy_name(name, userName, B_FILE_NAME_LENGTH);
	if (status != B_OK)
		return status;

	KPath pathBuffer;
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char* path = pathBuffer.LockBuffer();

	if (userPath != NULL) {
		if (!IS_USER_ADDRESS(userPath))
			return B_BAD_ADDRESS;
		status = user_copy_name(path, userPath, B_PATH_NAME_LENGTH);
		if (status != B_OK)
			return status;
	}

	if ((openMode & O_CREAT) != 0) {
		return attr_create(fd, userPath ? path : NULL, name, type, openMode,
			false);
	}

	return attr_open(fd, userPath ? path : NULL, name, openMode, false);
}


/**
 * @brief User syscall: remove an extended attribute by name.
 *
 * @param fd       FD of the owning node.
 * @param userName User-space attribute name.
 * @retval B_OK Attribute removed.
 */
status_t
_user_remove_attr(int fd, const char* userName)
{
	char name[B_FILE_NAME_LENGTH];

	if (!IS_USER_ADDRESS(userName))
		return B_BAD_ADDRESS;
	status_t status = user_copy_name(name, userName, B_FILE_NAME_LENGTH);
	if (status != B_OK)
		return status;

	return attr_remove(fd, name, false);
}


/**
 * @brief User syscall: rename an extended attribute.
 *
 * @param fromFile     FD of the source node.
 * @param userFromName User-space source attribute name.
 * @param toFile       FD of the destination node.
 * @param userToName   User-space destination attribute name.
 * @retval B_OK Attribute renamed.
 */
status_t
_user_rename_attr(int fromFile, const char* userFromName, int toFile,
	const char* userToName)
{
	if (!IS_USER_ADDRESS(userFromName)
		|| !IS_USER_ADDRESS(userToName))
		return B_BAD_ADDRESS;

	KPath fromNameBuffer(B_FILE_NAME_LENGTH);
	KPath toNameBuffer(B_FILE_NAME_LENGTH);
	if (fromNameBuffer.InitCheck() != B_OK || toNameBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char* fromName = fromNameBuffer.LockBuffer();
	char* toName = toNameBuffer.LockBuffer();

	status_t status = user_copy_name(fromName, userFromName, B_FILE_NAME_LENGTH);
	if (status != B_OK)
		return status;
	status = user_copy_name(toName, userToName, B_FILE_NAME_LENGTH);
	if (status != B_OK)
		return status;

	return attr_rename(fromFile, fromName, toFile, toName, false);
}


/**
 * @brief User syscall: open the index directory of a volume.
 *
 * @param device Device ID.
 * @return A non-negative FD, or a negative error code.
 */
int
_user_open_index_dir(dev_t device)
{
	return index_dir_open(device, false);
}


/**
 * @brief User syscall: create a file-system index.
 *
 * @param device   Device ID.
 * @param userName User-space index name.
 * @param type     Index type.
 * @param flags    FS-specific flags.
 * @retval B_OK Index created.
 */
status_t
_user_create_index(dev_t device, const char* userName, uint32 type,
	uint32 flags)
{
	char name[B_FILE_NAME_LENGTH];

	if (!IS_USER_ADDRESS(userName))
		return B_BAD_ADDRESS;
	status_t status = user_copy_name(name, userName, B_FILE_NAME_LENGTH);
	if (status != B_OK)
		return status;

	return index_create(device, name, type, flags, false);
}


/**
 * @brief User syscall: read stat information for a named index.
 *
 * @param device   Device ID.
 * @param userName User-space index name.
 * @param userStat User-space output stat buffer.
 * @retval B_OK Stat copied to user space.
 */
status_t
_user_read_index_stat(dev_t device, const char* userName, struct stat* userStat)
{
	char name[B_FILE_NAME_LENGTH];
	struct stat stat = {0};
	status_t status;

	if (!IS_USER_ADDRESS(userName) || !IS_USER_ADDRESS(userStat))
		return B_BAD_ADDRESS;
	status = user_copy_name(name, userName, B_FILE_NAME_LENGTH);
	if (status != B_OK)
		return status;

	status = index_name_read_stat(device, name, &stat, false);
	if (status == B_OK) {
		if (user_memcpy(userStat, &stat, sizeof(stat)) != B_OK)
			return B_BAD_ADDRESS;
	}

	return status;
}


/**
 * @brief User syscall: remove a file-system index by name.
 *
 * @param device   Device ID.
 * @param userName User-space index name.
 * @retval B_OK Index removed.
 */
status_t
_user_remove_index(dev_t device, const char* userName)
{
	char name[B_FILE_NAME_LENGTH];

	if (!IS_USER_ADDRESS(userName))
		return B_BAD_ADDRESS;
	status_t status = user_copy_name(name, userName, B_FILE_NAME_LENGTH);
	if (status != B_OK)
		return status;

	return index_remove(device, name, false);
}


/**
 * @brief User syscall: get the current working directory path.
 *
 * @param userBuffer User-space output buffer.
 * @param size       Size of @p userBuffer (capped at kMaxPathLength).
 * @retval B_OK Path written to user space.
 */
status_t
_user_getcwd(char* userBuffer, size_t size)
{
	if (size == 0)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(userBuffer))
		return B_BAD_ADDRESS;

	if (size > kMaxPathLength)
		size = kMaxPathLength;

	KPath pathBuffer(size);
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	TRACE(("user_getcwd: buf %p, %ld\n", userBuffer, size));

	char* path = pathBuffer.LockBuffer();

	status_t status = get_cwd(path, size, false);
	if (status != B_OK)
		return status;

	// Copy back the result
	if (user_strlcpy(userBuffer, path, size) < B_OK)
		return B_BAD_ADDRESS;

	return status;
}


/**
 * @brief User syscall: set the current working directory.
 *
 * @param fd       Base FD for relative paths (or AT_FDCWD).
 * @param userPath User-space path of the new CWD.
 * @retval B_OK CWD changed.
 */
status_t
_user_setcwd(int fd, const char* userPath)
{
	TRACE(("user_setcwd: path = %p\n", userPath));

	KPath pathBuffer;
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	char* path = pathBuffer.LockBuffer();

	if (userPath != NULL) {
		if (!IS_USER_ADDRESS(userPath))
			return B_BAD_ADDRESS;
		status_t status = user_copy_name(path, userPath, B_PATH_NAME_LENGTH);
		if (status != B_OK)
			return status;
	}

	return set_cwd(fd, userPath != NULL ? path : NULL, false);
}


/**
 * @brief User syscall: change the root directory for the calling team (chroot).
 *
 * Only root (euid == 0) is permitted.
 *
 * @param userPath User-space path for the new root directory.
 * @retval B_OK         Root changed.
 * @retval B_NOT_ALLOWED Caller is not root.
 */
status_t
_user_change_root(const char* userPath)
{
	// only root is allowed to chroot()
	if (geteuid() != 0)
		return B_NOT_ALLOWED;

	// alloc path buffer
	KPath pathBuffer;
	if (pathBuffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	// copy userland path to kernel
	char* path = pathBuffer.LockBuffer();
	if (userPath != NULL) {
		if (!IS_USER_ADDRESS(userPath))
			return B_BAD_ADDRESS;
		status_t status = user_copy_name(path, userPath, B_PATH_NAME_LENGTH);
		if (status != B_OK)
			return status;
	}

	// get the vnode
	VnodePutter vnode;
	status_t status = path_to_vnode(path, true, vnode, NULL, false);
	if (status != B_OK)
		return status;

	// set the new root
	struct io_context* context = get_current_io_context(false);
	rw_lock_write_lock(&sIOContextRootLock);
	struct vnode* oldRoot = context->root;
	context->root = vnode.Detach();
	rw_lock_write_unlock(&sIOContextRootLock);

	put_vnode(oldRoot);

	return B_OK;
}


/**
 * @brief User syscall: open a live query on a volume.
 *
 * @param device      Device ID of the volume to query.
 * @param userQuery   User-space query expression string.
 * @param queryLength Length of @p userQuery (must be < 65536).
 * @param flags       Query flags (e.g. B_LIVE_QUERY).
 * @param port        Port for live-query notifications (-1 if none).
 * @param token       Token sent with notifications.
 * @return A non-negative query FD, or a negative error code.
 */
int
_user_open_query(dev_t device, const char* userQuery, size_t queryLength,
	uint32 flags, port_id port, int32 token)
{
	if (device < 0 || userQuery == NULL || queryLength == 0)
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userQuery))
		return B_BAD_ADDRESS;

	// this is a safety restriction
	if (queryLength >= 65536)
		return B_NAME_TOO_LONG;

	BStackOrHeapArray<char, 128> query(queryLength + 1);
	if (!query.IsValid())
		return B_NO_MEMORY;

	if (user_strlcpy(query, userQuery, queryLength + 1) < B_OK)
		return B_BAD_ADDRESS;

	return query_open(device, query, flags, port, token, false);
}


#include "vfs_request_io.cpp"
