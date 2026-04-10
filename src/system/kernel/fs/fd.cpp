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
 *   Copyright 2009-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2002-2018, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file fd.cpp
 * @brief File descriptor table management for kernel and user processes.
 *
 * Implements the per-team file descriptor table: allocating/freeing slots,
 * duplicating descriptors (dup/dup2), translating fd numbers to file_descriptor
 * objects, and the select()/poll() infrastructure. Also provides the kernel-side
 * fd operations that delegate to vfs.cpp.
 *
 * @see vfs.cpp, Vnode.cpp
 */


#include <fd.h>

#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include <OS.h>

#include <AutoDeleter.h>
#include <AutoDeleterDrivers.h>
#include <BytePointer.h>
#include <StackOrHeapArray.h>

#include <syscalls.h>
#include <syscall_restart.h>
#include <slab/Slab.h>
#include <util/AutoLock.h>
#include <util/iovec_support.h>
#include <vfs.h>
#include <wait_for_objects.h>

#include "vfs_tracing.h"


//#define TRACE_FD
#ifdef TRACE_FD
#	define TRACE(x) dprintf x
#else
#	define TRACE(x)
#endif


static const size_t kMaxReadDirBufferSize = B_PAGE_SIZE * 2;

extern object_cache* sFileDescriptorCache;


static struct file_descriptor* get_fd_locked(const struct io_context* context,
	int fd);
static struct file_descriptor* remove_fd(struct io_context* context, int fd);
static void deselect_select_infos(file_descriptor* descriptor,
	select_info* infos, bool putSyncObjects);


//	#pragma mark - General fd routines


#ifdef DEBUG
void dump_fd(int fd, struct file_descriptor* descriptor);

/** @brief Debug helper that prints the fields of a file_descriptor to the kernel log. */
void
dump_fd(int fd,struct file_descriptor* descriptor)
{
	dprintf("fd[%d] = %p: ref_count = %" B_PRId32 ", ops "
		"= %p, u.vnode = %p, u.mount = %p, cookie = %p, open_mode = %" B_PRIx32
		", pos = %" B_PRId64 "\n",
		fd, descriptor, descriptor->ref_count,
		descriptor->ops, descriptor->u.vnode, descriptor->u.mount,
		descriptor->cookie, descriptor->open_mode, descriptor->pos);
}
#endif


/**
 * @brief Allocates and zero-initialises a new file_descriptor from the slab cache.
 *
 * The returned descriptor has a reference count of 1 and an open count of 0.
 * The caller is responsible for eventually calling put_fd() to release it.
 *
 * @return Pointer to the new descriptor, or @c NULL on allocation failure.
 */
struct file_descriptor*
alloc_fd(void)
{
	file_descriptor* descriptor
		= (file_descriptor*)object_cache_alloc(sFileDescriptorCache, 0);
	if (descriptor == NULL)
		return NULL;

	descriptor->u.vnode = NULL;
	descriptor->cookie = NULL;
	descriptor->ref_count = 1;
	descriptor->open_count = 0;
	descriptor->open_mode = 0;
	descriptor->pos = -1;

	return descriptor;
}


/**
 * @brief Returns whether the close-on-exec flag is set for the given fd slot.
 *
 * The caller must hold at least a read lock on @a context->lock.
 */
bool
fd_close_on_exec(const struct io_context* context, int fd)
{
	ASSERT_READ_LOCKED_RW_LOCK(&context->lock);

	return CHECK_BIT(context->fds_close_on_exec[fd / 8], fd & 7) ? true : false;
}


/**
 * @brief Sets or clears the close-on-exec flag for the given fd slot.
 *
 * The caller must hold a write lock on @a context->lock.
 */
void
fd_set_close_on_exec(struct io_context* context, int fd, bool closeFD)
{
	ASSERT_WRITE_LOCKED_RW_LOCK(&context->lock);

	if (closeFD)
		context->fds_close_on_exec[fd / 8] |= (1 << (fd & 7));
	else
		context->fds_close_on_exec[fd / 8] &= ~(1 << (fd & 7));
}


/**
 * @brief Returns whether the close-on-fork flag is set for the given fd slot.
 *
 * The caller must hold at least a read lock on @a context->lock.
 */
bool
fd_close_on_fork(const struct io_context* context, int fd)
{
	ASSERT_READ_LOCKED_RW_LOCK(&context->lock);

	return CHECK_BIT(context->fds_close_on_fork[fd / 8], fd & 7) ? true : false;
}


/**
 * @brief Sets or clears the close-on-fork flag for the given fd slot.
 *
 * The caller must hold a write lock on @a context->lock.
 */
void
fd_set_close_on_fork(struct io_context* context, int fd, bool closeFD)
{
	ASSERT_WRITE_LOCKED_RW_LOCK(&context->lock);

	if (closeFD)
		context->fds_close_on_fork[fd / 8] |= (1 << (fd & 7));
	else
		context->fds_close_on_fork[fd / 8] &= ~(1 << (fd & 7));
}


/**
 * @brief Searches for a free slot in the FD table starting at @a firstIndex and
 *        inserts @a descriptor into it.
 *
 * Increments the descriptor's open_count atomically. The caller must not have
 * the context lock held; this function acquires a write lock internally.
 *
 * @param context     The I/O context whose table will be modified.
 * @param descriptor  The already-allocated file_descriptor to insert.
 * @param firstIndex  The lowest fd number to consider (typically 0 or 3).
 * @return The newly assigned fd number, or @c B_NO_MORE_FDS / @c B_BAD_VALUE.
 */
int
new_fd_etc(struct io_context* context, struct file_descriptor* descriptor,
	int firstIndex)
{
	int fd = -1;
	uint32 i;

	if (firstIndex < 0 || (uint32)firstIndex >= context->table_size)
		return B_BAD_VALUE;

	WriteLocker locker(context->lock);

	for (i = firstIndex; i < context->table_size; i++) {
		if (!context->fds[i]) {
			fd = i;
			break;
		}
	}
	if (fd < 0)
		return B_NO_MORE_FDS;

	TFD(NewFD(context, fd, descriptor));

	context->fds[fd] = descriptor;
	context->num_used_fds++;
	atomic_add(&descriptor->open_count, 1);

	return fd;
}


/**
 * @brief Inserts @a descriptor into the lowest available slot of @a context's
 *        FD table (convenience wrapper around new_fd_etc() with firstIndex = 0).
 */
int
new_fd(struct io_context* context, struct file_descriptor* descriptor)
{
	return new_fd_etc(context, descriptor, 0);
}


/**
 * @brief Decrements the reference count of @a descriptor and, when it reaches
 *        zero, invokes the fd_free hook and returns the object to the slab cache.
 *
 * Also handles the disconnected-descriptor case: if the descriptor has been
 * marked @c O_DISCONNECTED and the ref count drops to the open count, the
 * underlying fd_close and fd_free hooks are invoked so the underlying resource
 * is released even though the fd slot still exists until explicitly closed.
 */
void
put_fd(struct file_descriptor* descriptor)
{
	int32 previous = atomic_add(&descriptor->ref_count, -1);

	TFD(PutFD(descriptor));

	TRACE(("put_fd(descriptor = %p [ref = %" B_PRId32 ", cookie = %p])\n",
		descriptor, descriptor->ref_count, descriptor->cookie));

	// free the descriptor if we don't need it anymore
	if (previous == 1) {
		// free the underlying object
		if (descriptor->ops != NULL && descriptor->ops->fd_free != NULL)
			descriptor->ops->fd_free(descriptor);

		object_cache_free(sFileDescriptorCache, descriptor, 0);
	} else if ((descriptor->open_mode & O_DISCONNECTED) != 0
			&& previous - 1 == descriptor->open_count
			&& descriptor->ops != NULL) {
		// the descriptor has been disconnected - it cannot
		// be accessed anymore, let's close it (no one is
		// currently accessing this descriptor)

		if (descriptor->ops->fd_close)
			descriptor->ops->fd_close(descriptor);
		if (descriptor->ops->fd_free)
			descriptor->ops->fd_free(descriptor);

		// prevent this descriptor from being closed/freed again
		descriptor->ops = NULL;
		descriptor->u.vnode = NULL;

		// the file descriptor is kept intact, so that it's not
		// reused until someone explicitly closes it
	}
}


/**
 * @brief Decrements the open count of @a descriptor and, when it hits zero,
 *        invokes fd_close to release the underlying resource.
 *
 * Also releases any POSIX advisory locks held by the context on this file.
 * Must be paired with a subsequent put_fd() call to release the reference.
 */
void
close_fd(struct io_context* context, struct file_descriptor* descriptor)
{
	// POSIX advisory locks need to be released when any file descriptor closes
	if (fd_is_file(descriptor))
		vfs_release_posix_lock(context, descriptor);

	if (atomic_add(&descriptor->open_count, -1) == 1) {
		vfs_unlock_vnode_if_locked(descriptor);

		if (descriptor->ops != NULL && descriptor->ops->fd_close != NULL)
			descriptor->ops->fd_close(descriptor);
	}
}


/**
 * @brief Removes the file descriptor at slot @a fd from @a context's table,
 *        closes it, and drops the slot's reference.
 *
 * This is the common implementation of close(2) for both user and kernel callers.
 *
 * @return @c B_OK on success, @c B_FILE_ERROR if @a fd is not open.
 */
status_t
close_fd_index(struct io_context* context, int fd)
{
	struct file_descriptor* descriptor = remove_fd(context, fd);

	if (descriptor == NULL)
		return B_FILE_ERROR;

	close_fd(context, descriptor);
	put_fd(descriptor);
		// the reference associated with the slot

	return B_OK;
}


/**
 * @brief Marks @a descriptor as disconnected so that it can no longer be
 *        looked up by fd number.
 *
 * The underlying close/free hooks are deferred until the last reference is
 * dropped via put_fd(). Useful when the backing storage disappears
 * unexpectedly (e.g. a hot-unplugged volume).
 */
void
disconnect_fd(struct file_descriptor* descriptor)
{
	descriptor->open_mode |= O_DISCONNECTED;
}


/**
 * @brief Increments the reference count of @a descriptor by one.
 *
 * Used when passing a descriptor pointer to code that will later call put_fd().
 */
void
inc_fd_ref_count(struct file_descriptor* descriptor)
{
	atomic_add(&descriptor->ref_count, 1);
}


/**
 * @brief Increments the open count of @a descriptor by one.
 *
 * The open count tracks how many "active" operations (reads, writes, ioctls)
 * are currently in progress on the descriptor.
 */
void
inc_fd_open_count(struct file_descriptor* descriptor)
{
	atomic_add(&descriptor->open_count, 1);
}


/**
 * @brief Returns the file_descriptor for slot @a fd without acquiring the
 *        context lock (caller must already hold at least a read lock).
 *
 * Increments the reference count of the returned descriptor; the caller is
 * responsible for calling put_fd(). Disconnected descriptors are treated as
 * absent and @c NULL is returned.
 */
static struct file_descriptor*
get_fd_locked(const struct io_context* context, int fd)
{
	if (fd < 0 || (uint32)fd >= context->table_size)
		return NULL;

	struct file_descriptor* descriptor = context->fds[fd];

	if (descriptor != NULL) {
		// disconnected descriptors cannot be accessed anymore
		if (descriptor->open_mode & O_DISCONNECTED)
			return NULL;

		TFD(GetFD(context, fd, descriptor));
		inc_fd_ref_count(descriptor);
	}

	return descriptor;
}


/**
 * @brief Returns the file_descriptor for slot @a fd, acquiring a read lock
 *        internally.
 *
 * Increments the reference count of the returned descriptor; the caller must
 * call put_fd() when done.
 *
 * @return The descriptor, or @c NULL if @a fd is invalid or disconnected.
 */
struct file_descriptor*
get_fd(const struct io_context* context, int fd)
{
	ReadLocker locker(context->lock);
	return get_fd_locked(context, fd);
}


/**
 * @brief Like get_fd() but also increments the open count so the descriptor
 *        cannot be fully closed until the caller decrements it.
 *
 * Used when starting an I/O operation that may sleep; prevents the descriptor
 * from being freed mid-operation.
 */
struct file_descriptor*
get_open_fd(const struct io_context* context, int fd)
{
	ReadLocker locker(context->lock);

	file_descriptor* descriptor = get_fd_locked(context, fd);
	if (descriptor == NULL)
		return NULL;

	atomic_add(&descriptor->open_count, 1);

	return descriptor;
}


/**
 * @brief Removes and returns the file_descriptor at slot @a fd from @a context's
 *        table under the write lock.
 *
 * Also clears any pending select_info entries associated with the slot. The
 * returned descriptor retains the reference that belonged to the table slot;
 * the caller must call close_fd() and put_fd() on it.
 */
static struct file_descriptor*
remove_fd(struct io_context* context, int fd)
{
	struct file_descriptor* descriptor = NULL;

	if (fd < 0)
		return NULL;

	WriteLocker locker(context->lock);

	if ((uint32)fd < context->table_size)
		descriptor = context->fds[fd];

	select_info* selectInfos = NULL;

	if (descriptor != NULL)	{
		// fd is valid
		TFD(RemoveFD(context, fd, descriptor));

		context->fds[fd] = NULL;
		fd_set_close_on_exec(context, fd, false);
		fd_set_close_on_fork(context, fd, false);
		context->num_used_fds--;

		selectInfos = context->select_infos[fd];
		context->select_infos[fd] = NULL;
	}

	if (selectInfos != NULL)
		deselect_select_infos(descriptor, selectInfos, true);

	return descriptor;
}


/**
 * @brief Duplicates file descriptor @a fd in the current team's I/O context,
 *        assigning it the lowest available slot number (dup(2) semantics).
 *
 * @param fd      The source fd to duplicate.
 * @param kernel  @c true to operate on the kernel I/O context.
 * @return The new fd number, or a negative error code.
 */
static int
dup_fd(int fd, bool kernel)
{
	struct io_context* context = get_current_io_context(kernel);
	struct file_descriptor* descriptor;
	int status;

	TRACE(("dup_fd: fd = %d\n", fd));

	// Try to get the fd structure
	descriptor = get_fd(context, fd);
	if (descriptor == NULL)
		return B_FILE_ERROR;

	// now put the fd in place
	status = new_fd(context, descriptor);
	if (status < 0) {
		put_fd(descriptor);
	} else {
		WriteLocker locker(context->lock);
		fd_set_close_on_exec(context, status, false);
		fd_set_close_on_fork(context, status, false);
	}

	return status;
}


/**
 * @brief Implements dup2(2) / dup3(2): makes @a newfd refer to the same
 *        underlying object as @a oldfd, atomically closing @a newfd first.
 *
 * POSIX specifies dup2() as equivalent to close(newfd) followed by
 * fcntl(oldfd, F_DUPFD, newfd), but this implementation is thread-safe.
 * The @a flags argument may include @c O_CLOEXEC and/or @c O_CLOFORK.
 *
 * @param oldfd   The source fd.
 * @param newfd   The target fd slot.
 * @param flags   Optional @c O_CLOEXEC / @c O_CLOFORK flags for @a newfd.
 * @param kernel  @c true to operate on the kernel I/O context.
 * @return @a newfd on success, or a negative error code.
 */
static int
dup2_fd(int oldfd, int newfd, int flags, bool kernel)
{
	struct file_descriptor* evicted = NULL;
	struct io_context* context;

	TRACE(("dup2_fd: ofd = %d, nfd = %d\n", oldfd, newfd));

	// quick check
	if (oldfd < 0 || newfd < 0)
		return B_FILE_ERROR;
	if ((flags & ~(O_CLOEXEC | O_CLOFORK)) != 0)
		return B_BAD_VALUE;

	// Get current I/O context and lock it
	context = get_current_io_context(kernel);
	WriteLocker locker(context->lock);

	// Check if the fds are valid (mutex must be locked because
	// the table size could be changed)
	if ((uint32)oldfd >= context->table_size
		|| (uint32)newfd >= context->table_size
		|| context->fds[oldfd] == NULL
		|| (context->fds[oldfd]->open_mode & O_DISCONNECTED) != 0) {
		return B_FILE_ERROR;
	}

	// Check for identity, note that it cannot be made above
	// because we always want to return an error on invalid
	// handles
	if (oldfd != newfd) {
		// Now do the work
		TFD(Dup2FD(context, oldfd, newfd));

		evicted = context->fds[newfd];
		select_info* selectInfos = context->select_infos[newfd];
		context->select_infos[newfd] = NULL;
		atomic_add(&context->fds[oldfd]->ref_count, 1);
		atomic_add(&context->fds[oldfd]->open_count, 1);
		context->fds[newfd] = context->fds[oldfd];

		if (evicted == NULL)
			context->num_used_fds++;

		deselect_select_infos(evicted, selectInfos, true);
	}

	fd_set_close_on_exec(context, newfd, (flags & O_CLOEXEC) != 0);
	fd_set_close_on_fork(context, newfd, (flags & O_CLOFORK) != 0);

	locker.Unlock();

	// Say bye bye to the evicted fd
	if (evicted) {
		close_fd(context, evicted);
		put_fd(evicted);
	}

	return newfd;
}


/**
 * @brief Duplicates an fd from another team into the current (or kernel) team.
 *
 * @param fromTeam  The team that owns the source fd.
 * @param fd        The fd number within @a fromTeam to duplicate.
 * @param kernel    @c true to place the new fd in the kernel I/O context.
 * @return The new fd number on success, or @c B_BAD_TEAM_ID / @c B_FILE_ERROR.
 */
int
dup_foreign_fd(team_id fromTeam, int fd, bool kernel)
{
	// get the I/O context for the team in question
	Team* team = Team::Get(fromTeam);
	if (team == NULL)
		return B_BAD_TEAM_ID;
	BReference<Team> teamReference(team, true);

	io_context* fromContext = team->io_context;

	// get the file descriptor
	file_descriptor* descriptor = get_fd(fromContext, fd);
	if (descriptor == NULL)
		return B_FILE_ERROR;
	FileDescriptorPutter descriptorPutter(descriptor);

	// create a new FD in the target I/O context
	int result = new_fd(get_current_io_context(kernel), descriptor);
	if (result >= 0) {
		// the descriptor reference belongs to the slot, now
		descriptorPutter.Detach();
	}

	return result;
}


/**
 * @brief Issues an ioctl on the file descriptor @a fd.
 *
 * Translates @c FIONBIO into an @c F_SETFL fcntl call for portability.
 * Returns @c ENOTTY (mapped from @c B_DEV_INVALID_IOCTL) when the operation
 * is not supported by the underlying driver.
 */
static status_t
fd_ioctl(bool kernelFD, int fd, uint32 op, void* buffer, size_t length)
{
	FileDescriptorPutter descriptor(get_fd(get_current_io_context(kernelFD), fd));
	if (!descriptor.IsSet())
		return B_FILE_ERROR;

	// Special case: translate FIONBIO into fcntl(F_SETFL).
	if (op == FIONBIO) {
		if (buffer == NULL)
			return B_BAD_VALUE;

		int value;
		if (is_called_via_syscall()) {
			if (!IS_USER_ADDRESS(buffer)
				|| user_memcpy(&value, buffer, sizeof(int)) != B_OK) {
				return B_BAD_ADDRESS;
			}
		} else
			value = *(int*)buffer;

		size_t argument = descriptor->open_mode & ~O_NONBLOCK;
		argument |= (value ? O_NONBLOCK : 0);

		return (kernelFD ? _kern_fcntl : _user_fcntl)(fd, F_SETFL, argument);
	}

	status_t status;
	if (descriptor->ops->fd_ioctl)
		status = descriptor->ops->fd_ioctl(descriptor.Get(), op, buffer, length);
	else
		status = B_DEV_INVALID_IOCTL;

	if (status == B_DEV_INVALID_IOCTL)
		status = ENOTTY;

	return status;
}


/**
 * @brief Iterates over a linked list of select_info structures and deselects
 *        each registered event, then notifies them with @c B_EVENT_INVALID.
 *
 * If @a putSyncObjects is @c true, the reference to each select_sync object is
 * released via put_select_sync().
 */
static void
deselect_select_infos(file_descriptor* descriptor, select_info* infos,
	bool putSyncObjects)
{
	TRACE(("deselect_select_infos(%p, %p)\n", descriptor, infos));

	select_info* info = infos;
	while (info != NULL) {
		select_sync* sync = info->sync;

		// deselect the selected events
		uint16 eventsToDeselect = info->selected_events & ~B_EVENT_INVALID;
		if (descriptor->ops->fd_deselect != NULL && eventsToDeselect != 0) {
			for (uint16 event = 1; event < 16; event++) {
				if ((eventsToDeselect & SELECT_FLAG(event)) != 0) {
					descriptor->ops->fd_deselect(descriptor, event,
						(selectsync*)info);
				}
			}
		}

		select_info* next = info->next;
		notify_select_events(info, B_EVENT_INVALID);
		info = next;

		if (putSyncObjects)
			put_select_sync(sync);
	}
}


/**
 * @brief Registers a select_info structure against fd @a fd, subscribing to
 *        the events specified in @a info->selected_events.
 *
 * For each requested event the fd_select hook is called. The info is then
 * appended to @a context->select_infos[@a fd] so that future notifications
 * reach the caller. If the fd does not support select(), any non-output-only
 * events are immediately notified and @c B_UNSUPPORTED is returned.
 *
 * @return @c B_OK, @c B_FILE_ERROR if the fd is invalid, or @c B_UNSUPPORTED.
 */
status_t
select_fd(int32 fd, struct select_info* info, bool kernel)
{
	TRACE(("select_fd(fd = %" B_PRId32 ", info = %p (%p), 0x%x)\n", fd, info,
		info->sync, info->selected_events));

	FileDescriptorPutter descriptor;
		// define before the context locker, so it will be destroyed after it

	io_context* context = get_current_io_context(kernel);
	ReadLocker readLocker(context->lock);

	descriptor.SetTo(get_fd_locked(context, fd));
	if (!descriptor.IsSet())
		return B_FILE_ERROR;

	uint16 eventsToSelect = info->selected_events & ~B_EVENT_INVALID;

	if (descriptor->ops->fd_select == NULL) {
		// if the I/O subsystem doesn't support select(), we will
		// immediately notify the select call
		eventsToSelect &= ~SELECT_OUTPUT_ONLY_FLAGS;
		if (eventsToSelect != 0)
			notify_select_events(info, eventsToSelect);

		info->selected_events = 0;
		return B_UNSUPPORTED;
	}

	// We need the FD to stay open while we're doing this, so no select()/
	// deselect() will be called on it after it is closed.
	atomic_add(&descriptor->open_count, 1);

	readLocker.Unlock();

	// select any events asked for
	uint32 selectedEvents = 0;

	for (uint16 event = 1; event < 16; event++) {
		if ((eventsToSelect & SELECT_FLAG(event)) != 0
			&& descriptor->ops->fd_select(descriptor.Get(), event,
				(selectsync*)info) == B_OK) {
			selectedEvents |= SELECT_FLAG(event);
		}
	}
	info->selected_events = selectedEvents
		| (info->selected_events & B_EVENT_INVALID);

	// Add the info to the IO context. Even if nothing has been selected -- we
	// always support B_EVENT_INVALID.
	WriteLocker writeLocker(context->lock);
	if (context->fds[fd] != descriptor.Get()) {
		// Someone close()d the index in the meantime. deselect() all
		// events.
		info->next = NULL;
		deselect_select_infos(descriptor.Get(), info, false);

		// Release our open reference of the descriptor.
		close_fd(context, descriptor.Get());
		return B_FILE_ERROR;
	}

	// The FD index hasn't changed, so we add the select info to the table.

	info->next = context->select_infos[fd];
	context->select_infos[fd] = info;

	// As long as the info is in the list, we keep a reference to the sync
	// object.
	acquire_select_sync(info->sync);

	// Finally release our open reference. It is safe just to decrement,
	// since as long as the descriptor is associated with the slot,
	// someone else still has it open.
	atomic_add(&descriptor->open_count, -1);

	return B_OK;
}


/**
 * @brief Removes a previously registered select_info from fd @a fd and
 *        deselects all events it had subscribed to.
 *
 * If the info is not found in the list (e.g. because the fd was closed in the
 * meantime) the function returns @c B_OK silently.
 *
 * @return @c B_OK always; @c B_FILE_ERROR if the fd itself is no longer valid.
 */
status_t
deselect_fd(int32 fd, struct select_info* info, bool kernel)
{
	TRACE(("deselect_fd(fd = %" B_PRId32 ", info = %p (%p), 0x%x)\n", fd, info,
		info->sync, info->selected_events));

	FileDescriptorPutter descriptor;
		// define before the context locker, so it will be destroyed after it

	io_context* context = get_current_io_context(kernel);
	WriteLocker locker(context->lock);

	descriptor.SetTo(get_fd_locked(context, fd));
	if (!descriptor.IsSet())
		return B_FILE_ERROR;

	// remove the info from the IO context

	select_info** infoLocation = &context->select_infos[fd];
	while (*infoLocation != NULL && *infoLocation != info)
		infoLocation = &(*infoLocation)->next;

	// If not found, someone else beat us to it.
	if (*infoLocation != info)
		return B_OK;

	*infoLocation = info->next;

	locker.Unlock();

	// deselect the selected events
	uint16 eventsToDeselect = info->selected_events & ~B_EVENT_INVALID;
	if (descriptor->ops->fd_deselect != NULL && eventsToDeselect != 0) {
		for (uint16 event = 1; event < 16; event++) {
			if ((eventsToDeselect & SELECT_FLAG(event)) != 0) {
				descriptor->ops->fd_deselect(descriptor.Get(), event,
					(selectsync*)info);
			}
		}
	}

	put_select_sync(info->sync);

	return B_OK;
}


/**
 * @brief Quick validity check for fd @a fd in the current I/O context.
 *
 * Performs get_fd() / put_fd() without holding any additional lock. Because the
 * descriptor may be closed immediately after this returns, the result should
 * only be used as a hint.
 *
 * @return @c true if the fd is currently open and not disconnected.
 */
bool
fd_is_valid(int fd, bool kernel)
{
	struct file_descriptor* descriptor
		= get_fd(get_current_io_context(kernel), fd);
	if (descriptor == NULL)
		return false;

	put_fd(descriptor);
	return true;
}


/**
 * @brief Internal scatter/gather I/O implementation used by both kernel and
 *        user vector read/write paths.
 *
 * Iterates over the @a vecs array, calling the appropriate fd_read or fd_write
 * hook for each vector. Falls back to per-vector loops when the fd_readv /
 * fd_writev hooks are absent or unsupported.
 *
 * @param fd      File descriptor number.
 * @param pos     File offset, or -1 to use/advance the descriptor's current position.
 * @param vecs    Array of I/O vectors.
 * @param count   Number of vectors.
 * @param write   @c true for write, @c false for read.
 * @param kernel  @c true to use the kernel I/O context.
 * @return Total bytes transferred, or a negative error code.
 */
static ssize_t
common_vector_io(int fd, off_t pos, const iovec* vecs, size_t count, bool write, bool kernel)
{
	if (pos < -1)
		return B_BAD_VALUE;

	FileDescriptorPutter descriptor(get_fd(get_current_io_context(kernel), fd));
	if (!descriptor.IsSet())
		return B_FILE_ERROR;

	if (write ? (descriptor->open_mode & O_RWMASK) == O_RDONLY
			: (descriptor->open_mode & O_RWMASK) == O_WRONLY) {
		return B_FILE_ERROR;
	}

	bool movePosition = false;
	if (pos == -1 && descriptor->pos != -1) {
		pos = descriptor->pos;
		movePosition = true;
	}

	if (write ? descriptor->ops->fd_write == NULL
			: descriptor->ops->fd_read == NULL) {
		return B_BAD_VALUE;
	}

	if (!movePosition && count > 1 && (write ? descriptor->ops->fd_writev != NULL
			: descriptor->ops->fd_readv != NULL)) {
		ssize_t result;
		if (write) {
			result = descriptor->ops->fd_writev(descriptor.Get(), pos,
				vecs, count);
		} else {
			result = descriptor->ops->fd_readv(descriptor.Get(), pos,
				vecs, count);
		}
		if (result != B_UNSUPPORTED)
			return result;
		// If not supported, just fall back to the loop.
	}

	status_t status = B_OK;
	ssize_t bytesTransferred = 0;
	for (size_t i = 0; i < count; i++) {
		if (vecs[i].iov_base == NULL)
			continue;

		size_t length = vecs[i].iov_len;
		if (write) {
			status = descriptor->ops->fd_write(descriptor.Get(), pos,
				vecs[i].iov_base, &length);
		} else {
			status = descriptor->ops->fd_read(descriptor.Get(), pos,
				vecs[i].iov_base, &length);
		}

		if (status != B_OK) {
			if (bytesTransferred == 0)
				return status;
			break;
		}

		if ((uint64)bytesTransferred + length > SSIZE_MAX)
			bytesTransferred = SSIZE_MAX;
		else
			bytesTransferred += (ssize_t)length;

		if (pos != -1)
			pos += length;

		if (length < vecs[i].iov_len)
			break;
	}

	if (movePosition) {
		descriptor->pos = write && (descriptor->open_mode & O_APPEND) != 0
			? descriptor->ops->fd_seek(descriptor.Get(), 0, SEEK_END) : pos;
	}

	return bytesTransferred;
}


/**
 * @brief Validates and performs a single-buffer userland read or write.
 *
 * Verifies that @a buffer is a valid user-space address range before invoking
 * the underlying fd_read / fd_write hook. Advances the descriptor's current
 * position when @a pos is -1.
 *
 * @param fd      File descriptor number (user I/O context).
 * @param pos     File offset, or -1 to use the current position.
 * @param buffer  User-space buffer pointer.
 * @param length  Number of bytes to transfer.
 * @param write   @c true for write, @c false for read.
 * @return Bytes transferred on success, or a negative error/status code.
 */
static ssize_t
common_user_io(int fd, off_t pos, void* buffer, size_t length, bool write)
{
	if (pos < -1)
		return B_BAD_VALUE;

	FileDescriptorPutter descriptor(get_fd(get_current_io_context(false), fd));
	if (!descriptor.IsSet())
		return B_FILE_ERROR;

	if (write ? (descriptor->open_mode & O_RWMASK) == O_RDONLY
			: (descriptor->open_mode & O_RWMASK) == O_WRONLY) {
		return B_FILE_ERROR;
	}

	bool movePosition = false;
	if (pos == -1 && descriptor->pos != -1) {
		pos = descriptor->pos;
		movePosition = true;
	}

	if (write ? descriptor->ops->fd_write == NULL
			: descriptor->ops->fd_read == NULL) {
		return B_BAD_VALUE;
	}

	if (length == 0)
		return 0;

	if (!is_user_address_range(buffer, length))
		return B_BAD_ADDRESS;

	SyscallRestartWrapper<status_t> status;

	if (write)
		status = descriptor->ops->fd_write(descriptor.Get(), pos, buffer, &length);
	else
		status = descriptor->ops->fd_read(descriptor.Get(), pos, buffer, &length);

	if (status != B_OK)
		return status;

	if (movePosition) {
		descriptor->pos = write && (descriptor->open_mode & O_APPEND) != 0
			? descriptor->ops->fd_seek(descriptor.Get(), 0, SEEK_END) : pos + length;
	}

	return length <= SSIZE_MAX ? (ssize_t)length : SSIZE_MAX;
}


/**
 * @brief Copies the userland iovec array into a kernel buffer and delegates to
 *        common_vector_io() for the actual transfer (user vector read/write).
 *
 * Enforces @c IOV_MAX and validates the iovec array via get_iovecs_from_user().
 *
 * @param fd        File descriptor number.
 * @param pos       File offset, or -1 to use the current position.
 * @param userVecs  User-space pointer to the iovec array.
 * @param count     Number of vectors.
 * @param write     @c true for write, @c false for read.
 * @return Bytes transferred on success, or a negative error code.
 */
static ssize_t
common_user_vector_io(int fd, off_t pos, const iovec* userVecs, size_t count,
	bool write)
{
	if (count > IOV_MAX)
		return B_BAD_VALUE;

	BStackOrHeapArray<iovec, 16> vecs(count);
	if (!vecs.IsValid())
		return B_NO_MEMORY;

	status_t error = get_iovecs_from_user(userVecs, count, vecs, true);
	if (error != B_OK)
		return error;

	SyscallRestartWrapper<ssize_t> result;
	result = common_vector_io(fd, pos, vecs, count, write, false);

	return result;
}


/**
 * @brief Closes a single fd in the given context (common implementation for
 *        both _user_close() and _kern_close()).
 */
static status_t
common_close(int fd, bool kernel)
{
	return close_fd_index(get_current_io_context(kernel), fd);
}


/**
 * @brief Closes all open file descriptors in the range [@a minFd, @a maxFd]
 *        inclusive.
 *
 * If @c CLOSE_RANGE_CLOEXEC is set in @a flags the descriptors are not closed
 * immediately; instead their close-on-exec flags are set so they will be closed
 * across the next exec().
 *
 * @return @c B_OK, or @c B_BAD_VALUE if @a maxFd < @a minFd.
 */
static status_t
common_close_range(u_int minFd, u_int maxFd, int flags, bool kernel)
{
	if (maxFd < minFd)
		return B_BAD_VALUE;
	struct io_context* context = get_current_io_context(kernel);
	maxFd = min_c(maxFd, context->table_size - 1);
	if ((flags & CLOSE_RANGE_CLOEXEC) == 0) {
		for (u_int fd = minFd; fd <= maxFd; fd++)
			close_fd_index(context, fd);
	} else {
		WriteLocker locker(context->lock);
		for (u_int fd = minFd; fd <= maxFd; fd++) {
			if (context->fds[fd] != NULL)
				fd_set_close_on_exec(context, fd, true);
		}
	}
	return B_OK;
}


/**
 * @brief Issues an ioctl on a user fd from kernel code (e.g. a driver calling
 *        back into the VFS layer on behalf of a user request).
 */
status_t
user_fd_kernel_ioctl(int fd, uint32 op, void* buffer, size_t length)
{
	TRACE(("user_fd_kernel_ioctl: fd %d\n", fd));

	return fd_ioctl(false, fd, op, buffer, length);
}


//	#pragma mark - User syscalls


/** @brief Syscall entry point for read(2): reads up to @a length bytes from @a fd
 *         at offset @a pos into the user-space @a buffer. */
ssize_t
_user_read(int fd, off_t pos, void* buffer, size_t length)
{
	return common_user_io(fd, pos, buffer, length, false);
}


/** @brief Syscall entry point for readv(2): scattered read using a user-space
 *         iovec array. */
ssize_t
_user_readv(int fd, off_t pos, const iovec* userVecs, size_t count)
{
	return common_user_vector_io(fd, pos, userVecs, count, false);
}


/** @brief Syscall entry point for write(2): writes up to @a length bytes from
 *         the user-space @a buffer to @a fd at offset @a pos. */
ssize_t
_user_write(int fd, off_t pos, const void* buffer, size_t length)
{
	return common_user_io(fd, pos, (void*)buffer, length, true);
}


/** @brief Syscall entry point for writev(2): gathered write using a user-space
 *         iovec array. */
ssize_t
_user_writev(int fd, off_t pos, const iovec* userVecs, size_t count)
{
	return common_user_vector_io(fd, pos, userVecs, count, true);
}


/** @brief Syscall entry point for lseek(2): repositions the file offset of @a fd. */
off_t
_user_seek(int fd, off_t pos, int seekType)
{
	syscall_64_bit_return_value();

	FileDescriptorPutter descriptor(get_fd(get_current_io_context(false), fd));
	if (!descriptor.IsSet())
		return B_FILE_ERROR;

	TRACE(("user_seek(descriptor = %p)\n", descriptor));

	if (descriptor->ops->fd_seek)
		pos = descriptor->ops->fd_seek(descriptor.Get(), pos, seekType);
	else
		pos = ESPIPE;

	return pos;
}


/** @brief Syscall entry point for ioctl(2): dispatches a device-specific control
 *         operation on @a fd. */
status_t
_user_ioctl(int fd, uint32 op, void* buffer, size_t length)
{
	TRACE(("user_ioctl: fd %d\n", fd));

	// "buffer" is not always a pointer depending on "op", so we cannot
	// check that it is a userland buffer here. Instead we check that
	// it is at least not within the bounds of kernel memory; as in
	// the cases where it is a numeric constant it is usually a low one.
	if (IS_KERNEL_ADDRESS(buffer))
		return B_BAD_ADDRESS;

	SyscallRestartWrapper<status_t> status;

	return status = fd_ioctl(false, fd, op, buffer, length);
}


/**
 * @brief Syscall entry point for getdents(2): reads directory entries from a
 *        directory fd into the user-space @a userBuffer.
 *
 * Allocates a temporary kernel buffer, reads via the fd_read_dir hook, then
 * copies the result to user space.
 */
ssize_t
_user_read_dir(int fd, struct dirent* userBuffer, size_t bufferSize,
	uint32 maxCount)
{
	TRACE(("user_read_dir(fd = %d, userBuffer = %p, bufferSize = %ld, count = "
		"%" B_PRIu32 ")\n", fd, userBuffer, bufferSize, maxCount));

	if (maxCount == 0)
		return 0;

	if (userBuffer == NULL || !IS_USER_ADDRESS(userBuffer))
		return B_BAD_ADDRESS;

	// get I/O context and FD
	io_context* ioContext = get_current_io_context(false);
	FileDescriptorPutter descriptor(get_fd(ioContext, fd));
	if (!descriptor.IsSet())
		return B_FILE_ERROR;

	if (descriptor->ops->fd_read_dir == NULL)
		return B_UNSUPPORTED;

	// restrict buffer size and allocate a heap buffer
	if (bufferSize > kMaxReadDirBufferSize)
		bufferSize = kMaxReadDirBufferSize;
	struct dirent* buffer = (struct dirent*)malloc(bufferSize);
	if (buffer == NULL)
		return B_NO_MEMORY;
	MemoryDeleter bufferDeleter(buffer);

	// read the directory
	uint32 count = maxCount;
	status_t status = descriptor->ops->fd_read_dir(ioContext, descriptor.Get(),
		buffer, bufferSize, &count);
	if (status != B_OK)
		return status;

	ASSERT(count <= maxCount);

	// copy the buffer back -- determine the total buffer size first
	size_t sizeToCopy = 0;
	BytePointer<struct dirent> entry = buffer;
	for (uint32 i = 0; i < count; i++) {
		size_t length = entry->d_reclen;
		sizeToCopy += length;
		entry += length;
	}

	ASSERT(sizeToCopy <= bufferSize);

	if (user_memcpy(userBuffer, buffer, sizeToCopy) != B_OK)
		return B_BAD_ADDRESS;

	return count;
}


/** @brief Syscall entry point for rewinddir(2): resets the directory read
 *         position of @a fd back to the start. */
status_t
_user_rewind_dir(int fd)
{
	TRACE(("user_rewind_dir(fd = %d)\n", fd));

	FileDescriptorPutter descriptor(get_fd(get_current_io_context(false), fd));
	if (!descriptor.IsSet())
		return B_FILE_ERROR;

	status_t status;
	if (descriptor->ops->fd_rewind_dir)
		status = descriptor->ops->fd_rewind_dir(descriptor.Get());
	else
		status = B_UNSUPPORTED;

	return status;
}


/** @brief Syscall entry point for close(2): closes the file descriptor @a fd in
 *         the current user process. */
status_t
_user_close(int fd)
{
	return common_close(fd, false);
}


/**
 * @brief Syscall entry point for close_range(2): closes or sets CLOEXEC on a
 *        contiguous range of file descriptors [@a minFd, @a maxFd].
 *
 * Only @c CLOSE_RANGE_CLOEXEC is accepted in @a flags; other bits return
 * @c B_BAD_VALUE.
 */
status_t
_user_close_range(u_int minFd, u_int maxFd, int flags)
{
	if ((flags & ~(CLOSE_RANGE_CLOEXEC)) != 0)
		return B_BAD_VALUE;
	return common_close_range(minFd, maxFd, flags, false);
}


/** @brief Syscall entry point for dup(2): duplicates @a fd to the lowest
 *         available slot in the current user process. */
int
_user_dup(int fd)
{
	return dup_fd(fd, false);
}


/** @brief Syscall entry point for dup2(2) / dup3(2): duplicates @a ofd to
 *         exactly slot @a nfd in the current user process, honouring @a flags. */
int
_user_dup2(int ofd, int nfd, int flags)
{
	return dup2_fd(ofd, nfd, flags, false);
}


//	#pragma mark - Kernel calls


/**
 * @brief Kernel-internal read: reads up to @a length bytes from @a fd into the
 *        kernel-space @a buffer at offset @a pos.
 *
 * Does not go through the syscall restart machinery; intended for use by kernel
 * subsystems.
 */
ssize_t
_kern_read(int fd, off_t pos, void* buffer, size_t length)
{
	if (pos < -1)
		return B_BAD_VALUE;

	FileDescriptorPutter descriptor(get_fd(get_current_io_context(true), fd));

	if (!descriptor.IsSet())
		return B_FILE_ERROR;
	if ((descriptor->open_mode & O_RWMASK) == O_WRONLY)
		return B_FILE_ERROR;

	bool movePosition = false;
	if (pos == -1 && descriptor->pos != -1) {
		pos = descriptor->pos;
		movePosition = true;
	}

	SyscallFlagUnsetter _;

	if (descriptor->ops->fd_read == NULL)
		return B_BAD_VALUE;

	ssize_t bytesRead = descriptor->ops->fd_read(descriptor.Get(), pos, buffer,
		&length);
	if (bytesRead >= B_OK) {
		if (length > SSIZE_MAX)
			bytesRead = SSIZE_MAX;
		else
			bytesRead = (ssize_t)length;

		if (movePosition)
			descriptor->pos = pos + length;
	}

	return bytesRead;
}


/**
 * @brief Kernel-internal write: writes up to @a length bytes from the
 *        kernel-space @a buffer to @a fd at offset @a pos.
 */
ssize_t
_kern_write(int fd, off_t pos, const void* buffer, size_t length)
{
	if (pos < -1)
		return B_BAD_VALUE;

	FileDescriptorPutter descriptor(get_fd(get_current_io_context(true), fd));

	if (!descriptor.IsSet())
		return B_FILE_ERROR;
	if ((descriptor->open_mode & O_RWMASK) == O_RDONLY)
		return B_FILE_ERROR;

	bool movePosition = false;
	if (pos == -1 && descriptor->pos != -1) {
		pos = descriptor->pos;
		movePosition = true;
	}

	if (descriptor->ops->fd_write == NULL)
		return B_BAD_VALUE;

	SyscallFlagUnsetter _;

	ssize_t bytesWritten = descriptor->ops->fd_write(descriptor.Get(), pos,
		buffer,	&length);
	if (bytesWritten >= B_OK) {
		if (length > SSIZE_MAX)
			bytesWritten = SSIZE_MAX;
		else
			bytesWritten = (ssize_t)length;

		if (movePosition)
			descriptor->pos = pos + length;
	}

	return bytesWritten;
}


/** @brief Kernel-internal scatter read: reads from @a fd using an array of
 *         kernel-space iovecs. */
ssize_t
_kern_readv(int fd, off_t pos, const iovec* vecs, size_t count)
{
	SyscallFlagUnsetter _;
	return common_vector_io(fd, pos, vecs, count, false, true);
}


/** @brief Kernel-internal gather write: writes to @a fd using an array of
 *         kernel-space iovecs. */
ssize_t
_kern_writev(int fd, off_t pos, const iovec* vecs, size_t count)
{
	SyscallFlagUnsetter _;
	return common_vector_io(fd, pos, vecs, count, true, true);
}


/** @brief Kernel-internal lseek: repositions the file offset of kernel fd @a fd. */
off_t
_kern_seek(int fd, off_t pos, int seekType)
{
	FileDescriptorPutter descriptor(get_fd(get_current_io_context(true), fd));
	if (!descriptor.IsSet())
		return B_FILE_ERROR;

	if (descriptor->ops->fd_seek)
		pos = descriptor->ops->fd_seek(descriptor.Get(), pos, seekType);
	else
		pos = ESPIPE;

	return pos;
}


/** @brief Kernel-internal ioctl: issues a device-specific control operation on
 *         kernel fd @a fd. */
status_t
_kern_ioctl(int fd, uint32 op, void* buffer, size_t length)
{
	TRACE(("kern_ioctl: fd %d\n", fd));

	SyscallFlagUnsetter _;

	return fd_ioctl(true, fd, op, buffer, length);
}


/**
 * @brief Kernel-internal getdents: reads directory entries from kernel directory
 *        fd @a fd into the kernel-space @a buffer.
 *
 * Returns the number of entries read, or a negative error code.
 */
ssize_t
_kern_read_dir(int fd, struct dirent* buffer, size_t bufferSize,
	uint32 maxCount)
{
	TRACE(("sys_read_dir(fd = %d, buffer = %p, bufferSize = %ld, count = "
		"%" B_PRIu32 ")\n",fd, buffer, bufferSize, maxCount));

	struct io_context* ioContext = get_current_io_context(true);
	FileDescriptorPutter descriptor(get_fd(ioContext, fd));
	if (!descriptor.IsSet())
		return B_FILE_ERROR;

	ssize_t retval;
	if (descriptor->ops->fd_read_dir) {
		uint32 count = maxCount;
		retval = descriptor->ops->fd_read_dir(ioContext, descriptor.Get(), buffer,
			bufferSize, &count);
		if (retval >= 0)
			retval = count;
	} else
		retval = B_UNSUPPORTED;

	return retval;
}


/** @brief Kernel-internal rewinddir: resets the directory read position of
 *         kernel fd @a fd back to the start. */
status_t
_kern_rewind_dir(int fd)
{
	TRACE(("sys_rewind_dir(fd = %d)\n",fd));

	FileDescriptorPutter descriptor(get_fd(get_current_io_context(true), fd));
	if (!descriptor.IsSet())
		return B_FILE_ERROR;

	status_t status;
	if (descriptor->ops->fd_rewind_dir)
		status = descriptor->ops->fd_rewind_dir(descriptor.Get());
	else
		status = B_UNSUPPORTED;

	return status;
}


/** @brief Kernel-internal close: closes kernel fd @a fd. */
status_t
_kern_close(int fd)
{
	return common_close(fd, true);
}


/**
 * @brief Kernel-internal close_range: closes or sets CLOEXEC on a contiguous
 *        range of kernel file descriptors [@a minFd, @a maxFd].
 */
status_t
_kern_close_range(u_int minFd, u_int maxFd, int flags)
{
	return common_close_range(minFd, maxFd, flags, true);
}


/** @brief Kernel-internal dup: duplicates kernel fd @a fd to the lowest
 *         available slot in the kernel I/O context. */
int
_kern_dup(int fd)
{
	return dup_fd(fd, true);
}


/** @brief Kernel-internal dup2: duplicates kernel fd @a ofd to exactly slot
 *         @a nfd in the kernel I/O context, honouring @a flags. */
int
_kern_dup2(int ofd, int nfd, int flags)
{
	return dup2_fd(ofd, nfd, flags, true);
}
