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
 *   Copyright 2009-2010, Axel Dörfler, axeld@pinc-software.de.
 *   Copyright 2008, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file socket.cpp
 * @brief Kernel socket VFS interface — exposes BSD sockets as VFS file descriptors.
 *
 * Bridges the BSD socket API (socket, bind, connect, send, recv, etc.) into
 * the VFS layer so that sockets can be treated as file descriptors. Delegates
 * to the net stack add-on loaded at runtime.
 *
 * @see fifo.cpp, vfs.cpp
 */


#include <sys/socket.h>

#include <errno.h>
#include <limits.h>

#include <module.h>

#include <AutoDeleter.h>
#include <AutoDeleterDrivers.h>

#include <syscall_utils.h>

#include <fd.h>
#include <kernel.h>
#include <lock.h>
#include <syscall_restart.h>
#include <util/AutoLock.h>
#include <util/iovec_support.h>
#include <vfs.h>

#include <net_stack_interface.h>
#include <net_stat.h>


#define MAX_SOCKET_ADDRESS_LENGTH	(sizeof(sockaddr_storage))
#define MAX_SOCKET_OPTION_LENGTH	128
#define MAX_ANCILLARY_DATA_LENGTH	1024

#define GET_SOCKET_FD_OR_RETURN(fd, kernel, descriptor)	\
	do {												\
		status_t getError = get_socket_descriptor(fd, kernel, descriptor); \
		if (getError != B_OK)							\
			return getError;							\
	} while (false)

#define FD_SOCKET(descriptor) ((net_socket*)descriptor->cookie)


static net_stack_interface_module_info* sStackInterface = NULL;
static int32 sStackInterfaceConsumers = 0;
static rw_lock sLock = RW_LOCK_INITIALIZER("stack interface");


/**
 * @brief Acquire a reference to the net stack interface module, loading it if needed.
 *
 * Atomically increments the consumer count, then returns the already-loaded
 * module under a read lock. If the module is not yet loaded, upgrades to a
 * write lock and loads NET_STACK_INTERFACE_MODULE_NAME.
 *
 * @return Pointer to the net_stack_interface_module_info on success, or NULL
 *         if the module could not be loaded (consumer count is decremented
 *         in that case).
 */
static net_stack_interface_module_info*
get_stack_interface_module()
{
	atomic_add(&sStackInterfaceConsumers, 1);

	ReadLocker readLocker(sLock);
	if (sStackInterface != NULL)
		return sStackInterface;

	readLocker.Unlock();
	WriteLocker writeLocker(sLock);
	if (sStackInterface != NULL)
		return sStackInterface;

	// load module
	net_stack_interface_module_info* module;
	// TODO: Add driver settings option to load the userland net stack.
	status_t error = get_module(NET_STACK_INTERFACE_MODULE_NAME,
		(module_info**)&module);
	if (error == B_OK)
		sStackInterface = module;

	if (sStackInterface == NULL)
		atomic_add(&sStackInterfaceConsumers, -1);

	return sStackInterface;
}


/**
 * @brief Release a previously acquired reference to the net stack interface module.
 *
 * Decrements the consumer count. On KDEBUG kernels, unloads the module when
 * the count reaches zero. On non-KDEBUG kernels the module is kept resident.
 */
static void
put_stack_interface_module()
{
	if (atomic_add(&sStackInterfaceConsumers, -1) != 1)
		return;

	// Keep the stack loaded on non-KDEBUG kernels.
#if KDEBUG
	WriteLocker _(sLock);
	if (atomic_get(&sStackInterfaceConsumers) > 0)
		return;
	if (sStackInterface == NULL)
		return;

	put_module(NET_STACK_INTERFACE_MODULE_NAME);
	sStackInterface = NULL;
#endif
}


/**
 * @brief Copy a socket address from userland into a kernel-side buffer.
 *
 * Validates that the address pointer is a legal user address and that the
 * length is within MAX_SOCKET_ADDRESS_LENGTH, then copies @p addressLength
 * bytes and sets @p address->ss_len to the given length.
 *
 * @param userAddress   The userland socket address pointer.
 * @param addressLength The number of bytes to copy.
 * @param address       Destination kernel-side sockaddr_storage buffer.
 * @retval B_OK          Address copied successfully.
 * @retval B_BAD_VALUE   @p userAddress is NULL or @p addressLength is out of range.
 * @retval B_BAD_ADDRESS @p userAddress is not a valid user address.
 */
static status_t
copy_address_from_userland(const sockaddr* userAddress, socklen_t addressLength,
	sockaddr_storage* address)
{
	if (userAddress == NULL || addressLength < 0 || addressLength > MAX_SOCKET_ADDRESS_LENGTH)
		return B_BAD_VALUE;

	*address = {};
	if (!IS_USER_ADDRESS(userAddress)
			|| user_memcpy(address, userAddress, addressLength) != B_OK) {
		return B_BAD_ADDRESS;
	}

	address->ss_len = addressLength;
		// make sure the sa_len field is set correctly

	return B_OK;
}


/**
 * @brief Validate and prepare the output address buffer for a userland accept/getpeername call.
 *
 * Checks that @p _addressLength is a valid user pointer, reads the buffer
 * size from userland, clamps it to MAX_SOCKET_ADDRESS_LENGTH, and validates
 * @p userAddress appropriately (required or optional, depending on
 * @p addressRequired).
 *
 * @param userAddress     The userland address buffer pointer (may be adjusted).
 * @param _addressLength  Userland pointer to the address length field.
 * @param addressLength   Output: the clamped address buffer size.
 * @param addressRequired If true, a NULL @p userAddress is an error.
 * @retval B_OK          Parameters are valid; @p addressLength is populated.
 * @retval B_BAD_VALUE   @p _addressLength is NULL or @p userAddress is required but NULL.
 * @retval B_BAD_ADDRESS A pointer failed the IS_USER_ADDRESS check.
 */
static status_t
prepare_userland_address_result(struct sockaddr*& userAddress,
	socklen_t* _addressLength, socklen_t& addressLength, bool addressRequired)
{
	// check parameters
	if (_addressLength == NULL)
		return B_BAD_VALUE;
	if (userAddress == NULL) {
		if (addressRequired)
			return B_BAD_VALUE;
	} else {
		if (!IS_USER_ADDRESS(_addressLength))
			return B_BAD_ADDRESS;
		if (!IS_USER_ADDRESS(userAddress)) {
			if (addressRequired)
				return B_BAD_ADDRESS;
			userAddress = (struct sockaddr*)(intptr_t)-1;
		}
	}

	// copy the buffer size from userland
	addressLength = 0;
	if (userAddress != NULL
			&& user_memcpy(&addressLength, _addressLength, sizeof(socklen_t))
				!= B_OK) {
		return B_BAD_ADDRESS;
	}

	if (addressLength > MAX_SOCKET_ADDRESS_LENGTH)
		addressLength = MAX_SOCKET_ADDRESS_LENGTH;

	return B_OK;
}


/**
 * @brief Copy a socket address and its length back to userland.
 *
 * Writes @p addressLength to @p userAddressLength, then copies up to
 * min(@p addressLength, @p userAddressBufferSize) bytes of @p address to
 * @p userAddress.
 *
 * @param address               Kernel-side address buffer.
 * @param addressLength         Actual length of the address.
 * @param userAddress           Destination userland address buffer (may be NULL).
 * @param userAddressBufferSize Size of @p userAddress in userland.
 * @param userAddressLength     Userland pointer to receive the address length.
 * @retval B_OK          Data copied successfully.
 * @retval B_BAD_ADDRESS A user_memcpy call failed.
 */
static status_t
copy_address_to_userland(const void* address, socklen_t addressLength,
	sockaddr* userAddress, socklen_t userAddressBufferSize,
	socklen_t* userAddressLength)
{
	// copy address size and address back to userland
	if (user_memcpy(userAddressLength, &addressLength,
			sizeof(socklen_t)) != B_OK
		|| (userAddress != NULL
			&& (!IS_USER_ADDRESS(userAddress) || user_memcpy(userAddress, address,
				min_c(addressLength, userAddressBufferSize)) != B_OK))) {
		return B_BAD_ADDRESS;
	}

	return B_OK;
}


/**
 * @brief Copy and validate a userland msghdr along with its iovec array and address.
 *
 * Copies the msghdr struct from userland, validates and copies the iovec
 * array (up to IOV_MAX entries), validates the msg_name pointer and clamps
 * msg_namelen, and sets msg_name to the kernel-side @p address buffer.
 *
 * @param userMessage  Userland pointer to the msghdr to copy.
 * @param message      Output: kernel-side copy of the msghdr.
 * @param userVecs     Output: original userland iovec pointer saved from the message.
 * @param vecsDeleter  Deleter that owns the heap-allocated kernel iovec copy.
 * @param userAddress  Output: original userland msg_name pointer.
 * @param address      Kernel-side buffer (MAX_SOCKET_ADDRESS_LENGTH bytes) for msg_name.
 * @retval B_OK        Message prepared successfully.
 * @retval B_BAD_VALUE @p userMessage is NULL or iovec count is out of range.
 * @retval B_BAD_ADDRESS A user address check failed.
 * @retval B_NO_MEMORY iovec array allocation failed.
 * @retval EMSGSIZE    msg_iovlen exceeds IOV_MAX.
 */
static status_t
prepare_userland_msghdr(const msghdr* userMessage, msghdr& message,
	iovec*& userVecs, MemoryDeleter& vecsDeleter, void*& userAddress,
	char* address)
{
	if (userMessage == NULL)
		return B_BAD_VALUE;

	// copy message from userland
	if (!IS_USER_ADDRESS(userMessage)
			|| user_memcpy(&message, userMessage, sizeof(msghdr)) != B_OK) {
		return B_BAD_ADDRESS;
	}

	userVecs = message.msg_iov;
	userAddress = message.msg_name;

	// copy iovecs from userland
	if (message.msg_iovlen < 0 || message.msg_iovlen > IOV_MAX)
		return EMSGSIZE;
	if (userVecs != NULL && message.msg_iovlen > 0) {
		iovec* vecs = (iovec*)malloc(sizeof(iovec) * message.msg_iovlen);
		if (vecs == NULL)
			return B_NO_MEMORY;
		vecsDeleter.SetTo(vecs);

		status_t error = get_iovecs_from_user(message.msg_iov, message.msg_iovlen, vecs);
		if (error != B_OK)
			return error;
		message.msg_iov = vecs;
	} else {
		message.msg_iov = NULL;
		message.msg_iovlen = 0;
	}

	// prepare the address field
	userAddress = message.msg_name;
	if (userAddress != NULL) {
		if (!IS_USER_ADDRESS(message.msg_name))
			return B_BAD_ADDRESS;
		if (message.msg_namelen > MAX_SOCKET_ADDRESS_LENGTH)
			message.msg_namelen = MAX_SOCKET_ADDRESS_LENGTH;

		memset(address, 0, MAX_SOCKET_ADDRESS_LENGTH);
		message.msg_name = address;
	}

	return B_OK;
}


// #pragma mark - socket file descriptor


/**
 * @brief Read data from a socket file descriptor.
 *
 * Delegates to the net stack's recv() with flags=0. Updates @p *_length
 * with the number of bytes actually read.
 *
 * @param descriptor The socket file descriptor.
 * @param pos        File position (ignored for sockets).
 * @param buffer     Destination buffer for received data.
 * @param _length    Input: buffer capacity; output: bytes received.
 * @retval B_OK On success.
 * @retval <0   Network-stack error code on failure.
 */
static status_t
socket_read(struct file_descriptor *descriptor, off_t pos, void *buffer,
	size_t *_length)
{
	ssize_t bytesRead = sStackInterface->recv(FD_SOCKET(descriptor), buffer,
		*_length, 0);
	*_length = bytesRead >= 0 ? bytesRead : 0;
	return bytesRead >= 0 ? B_OK : bytesRead;
}


/**
 * @brief Write data to a socket file descriptor.
 *
 * Delegates to the net stack's send() with flags=0. Updates @p *_length
 * with the number of bytes actually sent.
 *
 * @param descriptor The socket file descriptor.
 * @param pos        File position (ignored for sockets).
 * @param buffer     Source buffer containing data to send.
 * @param _length    Input: bytes to send; output: bytes sent.
 * @retval B_OK On success.
 * @retval <0   Network-stack error code on failure.
 */
static status_t
socket_write(struct file_descriptor *descriptor, off_t pos, const void *buffer,
	size_t *_length)
{
	ssize_t bytesWritten = sStackInterface->send(FD_SOCKET(descriptor), buffer,
		*_length, 0);
	*_length = bytesWritten >= 0 ? bytesWritten : 0;
	return bytesWritten >= 0 ? B_OK : bytesWritten;
}


/**
 * @brief Read data from a socket into a scatter-gather iovec array.
 *
 * Wraps the iovec array in a zero-initialised msghdr and delegates to
 * the net stack's recvmsg().
 *
 * @param descriptor The socket file descriptor.
 * @param pos        File position (ignored for sockets).
 * @param vecs       Array of iovec buffers to receive data into.
 * @param count      Number of elements in @p vecs.
 * @return Number of bytes received on success, or a negative error code.
 */
static ssize_t
socket_readv(struct file_descriptor *descriptor, off_t pos,
	const struct iovec *vecs, int count)
{
	struct msghdr message = {};
	message.msg_iov = (struct iovec*)vecs;
	message.msg_iovlen = count;
	return sStackInterface->recvmsg(FD_SOCKET(descriptor), &message, 0);
}


/**
 * @brief Write data from a scatter-gather iovec array to a socket.
 *
 * Wraps the iovec array in a zero-initialised msghdr and delegates to
 * the net stack's sendmsg().
 *
 * @param descriptor The socket file descriptor.
 * @param pos        File position (ignored for sockets).
 * @param vecs       Array of iovec buffers containing data to send.
 * @param count      Number of elements in @p vecs.
 * @return Number of bytes sent on success, or a negative error code.
 */
static ssize_t
socket_writev(struct file_descriptor *descriptor, off_t pos,
	const struct iovec *vecs, int count)
{
	struct msghdr message = {};
	message.msg_iov = (struct iovec*)vecs;
	message.msg_iovlen = count;
	return sStackInterface->sendmsg(FD_SOCKET(descriptor), &message, 0);
}


/**
 * @brief Perform an ioctl operation on a socket file descriptor.
 *
 * Forwards the request directly to the net stack's ioctl() handler.
 *
 * @param descriptor The socket file descriptor.
 * @param op         The ioctl request code.
 * @param buffer     The ioctl argument buffer.
 * @param length     Size of @p buffer in bytes.
 * @retval B_OK On success.
 * @retval <0   Network-stack error code on failure.
 */
static status_t
socket_ioctl(struct file_descriptor *descriptor, ulong op, void *buffer,
	size_t length)
{
	return sStackInterface->ioctl(FD_SOCKET(descriptor), op, buffer, length);
}


/**
 * @brief Set the blocking/non-blocking I/O flag on a socket file descriptor.
 *
 * Translates the VFS O_NONBLOCK flag into a B_SET_NONBLOCKING_IO or
 * B_SET_BLOCKING_IO ioctl directed at the net stack. O_APPEND is silently
 * ignored.
 *
 * @param descriptor The socket file descriptor.
 * @param flags      New open-mode flags; only O_NONBLOCK is acted upon.
 * @retval B_OK On success.
 * @retval <0   Network-stack error code on failure.
 */
static status_t
socket_set_flags(struct file_descriptor *descriptor, int flags)
{
	// we ignore O_APPEND, but O_NONBLOCK we need to translate
	uint32 op = (flags & O_NONBLOCK) != 0
		? B_SET_NONBLOCKING_IO : B_SET_BLOCKING_IO;

	return sStackInterface->ioctl(FD_SOCKET(descriptor), op, NULL, 0);
}


/**
 * @brief Register a select sync object for a socket event.
 *
 * Delegates to the net stack's select() to arm notification of the given
 * I/O event (B_SELECT_READ, B_SELECT_WRITE, etc.).
 *
 * @param descriptor The socket file descriptor.
 * @param event      The event type to monitor.
 * @param sync       The selectsync object to notify when the event fires.
 * @retval B_OK On success.
 * @retval <0   Network-stack error code on failure.
 */
static status_t
socket_select(struct file_descriptor *descriptor, uint8 event,
	struct selectsync *sync)
{
	return sStackInterface->select(FD_SOCKET(descriptor), event, sync);
}


/**
 * @brief Deregister a select sync object from a socket event.
 *
 * Delegates to the net stack's deselect() to disarm a previously armed
 * event notification.
 *
 * @param descriptor The socket file descriptor.
 * @param event      The event type to stop monitoring.
 * @param sync       The selectsync object to remove.
 * @retval B_OK On success.
 * @retval <0   Network-stack error code on failure.
 */
static status_t
socket_deselect(struct file_descriptor *descriptor, uint8 event,
	struct selectsync *sync)
{
	return sStackInterface->deselect(FD_SOCKET(descriptor), event, sync);
}


/**
 * @brief Fill in a struct stat for a socket file descriptor.
 *
 * Sockets do not map to real filesystem inodes, so synthetic values are
 * returned: the inode number is the kernel address of the net_socket object,
 * mode is S_IFSOCK|0666, and all timestamps are set to the current time.
 *
 * @param descriptor The socket file descriptor.
 * @param st         Output stat structure to populate.
 * @retval B_OK Always.
 */
static status_t
socket_read_stat(struct file_descriptor *descriptor, struct stat *st)
{
	st->st_dev = 0;
	st->st_ino = (addr_t)descriptor->cookie;
	st->st_mode = S_IFSOCK | 0666;
	st->st_nlink = 1;
	st->st_uid = 0;
	st->st_gid = 0;
	st->st_size = 0;
	st->st_rdev = 0;
	st->st_blksize = 1024;	// use MTU for datagram sockets?
	st->st_type = 0;

	timespec now;
	now.tv_sec = time(NULL);
	now.tv_nsec = 0;

	st->st_atim = now;
	st->st_mtim = now;
	st->st_ctim = now;
	st->st_crtim = now;

	return B_OK;
}


/**
 * @brief Close a socket file descriptor.
 *
 * Delegates to the net stack's close() to initiate teardown of the socket.
 * The socket object is freed later in socket_free().
 *
 * @param descriptor The socket file descriptor.
 * @retval B_OK On success.
 * @retval <0   Network-stack error code on failure.
 */
static status_t
socket_close(struct file_descriptor *descriptor)
{
	return sStackInterface->close(FD_SOCKET(descriptor));
}


/**
 * @brief Free the socket object associated with a file descriptor.
 *
 * Called after the last reference to the descriptor is dropped. Invokes
 * the net stack's free() to release the net_socket, then releases the
 * module reference acquired when the socket was created.
 *
 * @param descriptor The socket file descriptor whose resources are to be freed.
 */
static void
socket_free(struct file_descriptor *descriptor)
{
	sStackInterface->free(FD_SOCKET(descriptor));
	put_stack_interface_module();
}


static struct fd_ops sSocketFDOps = {
	&socket_close,
	&socket_free,
	&socket_read,
	&socket_write,
	&socket_readv,
	&socket_writev,
	NULL,	// fd_seek
	&socket_ioctl,
	&socket_set_flags,
	&socket_select,
	&socket_deselect,
	NULL,	// fd_read_dir
	NULL,	// fd_rewind_dir
	&socket_read_stat,
	NULL,	// fd_write_stat
};


/**
 * @brief Look up and validate a socket file descriptor from the current I/O context.
 *
 * Retrieves the file_descriptor for @p fd and verifies that its ops pointer
 * is sSocketFDOps. Puts the descriptor and returns ENOTSOCK if it is not a
 * socket.
 *
 * @param fd         The file descriptor number to look up.
 * @param kernel     If true use the kernel I/O context; otherwise user context.
 * @param descriptor Output: the located file_descriptor pointer.
 * @retval B_OK     Descriptor retrieved and is a socket.
 * @retval EBADF    @p fd is negative or not found in the I/O context.
 * @retval ENOTSOCK @p fd refers to a non-socket file descriptor.
 */
static status_t
get_socket_descriptor(int fd, bool kernel, file_descriptor*& descriptor)
{
	if (fd < 0)
		return EBADF;

	descriptor = get_fd(get_current_io_context(kernel), fd);
	if (descriptor == NULL)
		return EBADF;

	if (descriptor->ops != &sSocketFDOps) {
		put_fd(descriptor);
		return ENOTSOCK;
	}

	return B_OK;
}


/**
 * @brief Allocate a VFS file descriptor for an already-created net_socket.
 *
 * Queries the socket's non-blocking state, builds the open-mode flags from
 * @p flags (SOCK_CLOEXEC, SOCK_CLOFORK, SOCK_NONBLOCK), allocates a
 * file_descriptor via alloc_fd(), and publishes it with new_fd().
 *
 * @param socket The net_socket object to wrap.
 * @param flags  Socket creation flags (SOCK_CLOEXEC | SOCK_NONBLOCK | SOCK_CLOFORK).
 * @param kernel If true publish into the kernel I/O context.
 * @return The new file descriptor number on success, or a negative error code.
 */
static int
create_socket_fd(net_socket* socket, int flags, bool kernel)
{
	// Get the socket's non-blocking flag, so we can set the respective
	// open mode flag.
	int32 nonBlock;
	socklen_t nonBlockLen = sizeof(int32);
	status_t error = sStackInterface->getsockopt(socket, SOL_SOCKET,
		SO_NONBLOCK, &nonBlock, &nonBlockLen);
	if (error != B_OK)
		return error;
	int oflags = 0;
	if ((flags & SOCK_CLOEXEC) != 0)
		oflags |= O_CLOEXEC;
	if ((flags & SOCK_CLOFORK) != 0)
		oflags |= O_CLOFORK;
	if ((flags & SOCK_NONBLOCK) != 0 || nonBlock)
		oflags |= O_NONBLOCK;

	// allocate a file descriptor
	file_descriptor* descriptor = alloc_fd();
	if (descriptor == NULL)
		return B_NO_MEMORY;

	// init it
	descriptor->ops = &sSocketFDOps;
	descriptor->cookie = socket;
	descriptor->open_mode = O_RDWR | oflags;

	// publish it
	io_context* context = get_current_io_context(kernel);
	int fd = new_fd(context, descriptor);
	if (fd < 0) {
		descriptor->ops = NULL;
		put_fd(descriptor);
		return fd;
	}

	rw_lock_write_lock(&context->lock);
	fd_set_close_on_exec(context, fd, (oflags & O_CLOEXEC) != 0);
	fd_set_close_on_fork(context, fd, (oflags & O_CLOFORK) != 0);
	rw_lock_write_unlock(&context->lock);

	return fd;
}


// #pragma mark - common sockets API implementation


/**
 * @brief Common implementation for socket(2) — create a network socket.
 *
 * Loads the net stack module, strips creation flags from @p type, calls
 * the stack's open(), wraps the result in a VFS file descriptor, and
 * releases the module reference if FD allocation fails.
 *
 * @param family   Address family (AF_INET, AF_UNIX, etc.).
 * @param type     Socket type (SOCK_STREAM, etc.); may include SOCK_CLOEXEC,
 *                 SOCK_NONBLOCK, SOCK_CLOFORK.
 * @param protocol Protocol number (0 for default).
 * @param kernel   If true, publish into the kernel I/O context.
 * @return New file descriptor on success, or a negative error/status code.
 */
static int
common_socket(int family, int type, int protocol, bool kernel)
{
	if (!get_stack_interface_module())
		return B_UNSUPPORTED;

	int sflags = type & (SOCK_CLOEXEC | SOCK_NONBLOCK | SOCK_CLOFORK);
	type &= ~(SOCK_CLOEXEC | SOCK_NONBLOCK | SOCK_CLOFORK);

	// create the socket
	net_socket* socket;
	status_t error = sStackInterface->open(family, type, protocol, &socket);
	if (error != B_OK) {
		put_stack_interface_module();
		return error;
	}

	// allocate the FD
	int fd = create_socket_fd(socket, sflags, kernel);
	if (fd < 0) {
		sStackInterface->close(socket);
		sStackInterface->free(socket);
		put_stack_interface_module();
	}

	return fd;
}


/**
 * @brief Common implementation for bind(2).
 *
 * Retrieves the socket descriptor for @p fd and forwards the call to the
 * net stack's bind().
 *
 * @param fd            File descriptor referring to the socket.
 * @param address       Local address to bind to.
 * @param addressLength Size of @p address.
 * @param kernel        If true, resolve @p fd in the kernel I/O context.
 * @retval B_OK On success.
 * @retval EBADF / ENOTSOCK Invalid socket descriptor.
 */
static status_t
common_bind(int fd, const struct sockaddr *address, socklen_t addressLength,
	bool kernel)
{
	file_descriptor* descriptor;
	GET_SOCKET_FD_OR_RETURN(fd, kernel, descriptor);
	FileDescriptorPutter _(descriptor);

	return sStackInterface->bind(FD_SOCKET(descriptor), address, addressLength);
}


/**
 * @brief Common implementation for shutdown(2).
 *
 * Validates @p how is in [SHUT_RD, SHUT_RDWR], then delegates to the
 * net stack's shutdown().
 *
 * @param fd     File descriptor referring to the socket.
 * @param how    SHUT_RD, SHUT_WR, or SHUT_RDWR.
 * @param kernel If true, resolve @p fd in the kernel I/O context.
 * @retval B_OK        On success.
 * @retval B_BAD_VALUE @p how is out of range.
 */
static status_t
common_shutdown(int fd, int how, bool kernel)
{
	file_descriptor* descriptor;
	GET_SOCKET_FD_OR_RETURN(fd, kernel, descriptor);
	FileDescriptorPutter _(descriptor);

	if (how < SHUT_RD || how > SHUT_RDWR)
		return B_BAD_VALUE;

	return sStackInterface->shutdown(FD_SOCKET(descriptor), how);
}


/**
 * @brief Common implementation for connect(2).
 *
 * Retrieves the socket descriptor for @p fd and forwards the call to the
 * net stack's connect().
 *
 * @param fd            File descriptor referring to the socket.
 * @param address       Remote address to connect to.
 * @param addressLength Size of @p address.
 * @param kernel        If true, resolve @p fd in the kernel I/O context.
 * @retval B_OK On success.
 */
static status_t
common_connect(int fd, const struct sockaddr *address,
	socklen_t addressLength, bool kernel)
{
	file_descriptor* descriptor;
	GET_SOCKET_FD_OR_RETURN(fd, kernel, descriptor);
	FileDescriptorPutter _(descriptor);

	return sStackInterface->connect(FD_SOCKET(descriptor), address,
		addressLength);
}


/**
 * @brief Common implementation for listen(2).
 *
 * Retrieves the socket descriptor for @p fd and calls the net stack's
 * listen() with the given backlog.
 *
 * @param fd      File descriptor referring to the socket.
 * @param backlog Maximum pending connection queue length.
 * @param kernel  If true, resolve @p fd in the kernel I/O context.
 * @retval B_OK On success.
 */
static status_t
common_listen(int fd, int backlog, bool kernel)
{
	file_descriptor* descriptor;
	GET_SOCKET_FD_OR_RETURN(fd, kernel, descriptor);
	FileDescriptorPutter _(descriptor);

	return sStackInterface->listen(FD_SOCKET(descriptor), backlog);
}


/**
 * @brief Common implementation for accept(2) / accept4(2).
 *
 * Calls the net stack's accept(), wraps the accepted socket in a new VFS
 * file descriptor (honouring @p flags for SOCK_CLOEXEC etc.), and acquires
 * an additional module reference for the new FD.
 *
 * @param fd             Listening socket file descriptor.
 * @param address        Output buffer for the peer address (may be NULL).
 * @param _addressLength Input/output address buffer size.
 * @param flags          Socket flags for the new descriptor (SOCK_CLOEXEC, etc.).
 * @param kernel         If true, resolve @p fd in the kernel I/O context.
 * @return New file descriptor on success, or a negative error code.
 */
static int
common_accept(int fd, struct sockaddr *address, socklen_t *_addressLength, int flags,
	bool kernel)
{
	file_descriptor* descriptor;
	GET_SOCKET_FD_OR_RETURN(fd, kernel, descriptor);
	FileDescriptorPutter _(descriptor);

	if ((flags & ~(SOCK_CLOEXEC | SOCK_NONBLOCK | SOCK_CLOFORK)) != 0)
		RETURN_AND_SET_ERRNO(B_BAD_VALUE);

	net_socket* acceptedSocket;
	status_t error = sStackInterface->accept(FD_SOCKET(descriptor), address,
		_addressLength, &acceptedSocket);
	if (error != B_OK)
		return error;

	// allocate the FD
	int acceptedFD = create_socket_fd(acceptedSocket, flags, kernel);
	if (acceptedFD < 0) {
		sStackInterface->close(acceptedSocket);
		sStackInterface->free(acceptedSocket);
	} else {
		// we need a reference for the new FD
		get_stack_interface_module();
	}

	return acceptedFD;
}


/**
 * @brief Common implementation for recv(2).
 *
 * @param fd     Socket file descriptor.
 * @param data   Destination buffer.
 * @param length Buffer capacity in bytes.
 * @param flags  Receive flags (MSG_PEEK, MSG_WAITALL, etc.).
 * @param kernel If true, resolve @p fd in the kernel I/O context.
 * @return Bytes received on success, or a negative error code.
 */
static ssize_t
common_recv(int fd, void *data, size_t length, int flags, bool kernel)
{
	file_descriptor* descriptor;
	GET_SOCKET_FD_OR_RETURN(fd, kernel, descriptor);
	FileDescriptorPutter _(descriptor);

	return sStackInterface->recv(FD_SOCKET(descriptor), data, length, flags);
}


/**
 * @brief Common implementation for recvfrom(2).
 *
 * @param fd             Socket file descriptor.
 * @param data           Destination buffer.
 * @param length         Buffer capacity in bytes.
 * @param flags          Receive flags.
 * @param address        Output buffer for the sender's address (may be NULL).
 * @param _addressLength Input/output size of @p address.
 * @param kernel         If true, resolve @p fd in the kernel I/O context.
 * @return Bytes received on success, or a negative error code.
 */
static ssize_t
common_recvfrom(int fd, void *data, size_t length, int flags,
	struct sockaddr *address, socklen_t *_addressLength, bool kernel)
{
	file_descriptor* descriptor;
	GET_SOCKET_FD_OR_RETURN(fd, kernel, descriptor);
	FileDescriptorPutter _(descriptor);

	return sStackInterface->recvfrom(FD_SOCKET(descriptor), data, length,
		flags, address, _addressLength);
}


/**
 * @brief Common implementation for recvmsg(2).
 *
 * @param fd      Socket file descriptor.
 * @param message Message header describing receive buffers and optional address.
 * @param flags   Receive flags.
 * @param kernel  If true, resolve @p fd in the kernel I/O context.
 * @return Bytes received on success, or a negative error code.
 */
static ssize_t
common_recvmsg(int fd, struct msghdr *message, int flags, bool kernel)
{
	file_descriptor* descriptor;
	GET_SOCKET_FD_OR_RETURN(fd, kernel, descriptor);
	FileDescriptorPutter _(descriptor);

	return sStackInterface->recvmsg(FD_SOCKET(descriptor), message, flags);
}


/**
 * @brief Common implementation for send(2).
 *
 * @param fd     Socket file descriptor.
 * @param data   Source buffer.
 * @param length Number of bytes to send.
 * @param flags  Send flags (MSG_OOB, MSG_NOSIGNAL, etc.).
 * @param kernel If true, resolve @p fd in the kernel I/O context.
 * @return Bytes sent on success, or a negative error code.
 */
static ssize_t
common_send(int fd, const void *data, size_t length, int flags, bool kernel)
{
	file_descriptor* descriptor;
	GET_SOCKET_FD_OR_RETURN(fd, kernel, descriptor);
	FileDescriptorPutter _(descriptor);

	return sStackInterface->send(FD_SOCKET(descriptor), data, length, flags);
}


/**
 * @brief Common implementation for sendto(2).
 *
 * @param fd            Socket file descriptor.
 * @param data          Source buffer.
 * @param length        Number of bytes to send.
 * @param flags         Send flags.
 * @param address       Destination address (may be NULL for connected sockets).
 * @param addressLength Size of @p address.
 * @param kernel        If true, resolve @p fd in the kernel I/O context.
 * @return Bytes sent on success, or a negative error code.
 */
static ssize_t
common_sendto(int fd, const void *data, size_t length, int flags,
	const struct sockaddr *address, socklen_t addressLength, bool kernel)
{
	file_descriptor* descriptor;
	GET_SOCKET_FD_OR_RETURN(fd, kernel, descriptor);
	FileDescriptorPutter _(descriptor);

	return sStackInterface->sendto(FD_SOCKET(descriptor), data, length, flags,
		address, addressLength);
}


/**
 * @brief Common implementation for sendmsg(2).
 *
 * @param fd      Socket file descriptor.
 * @param message Message header describing send buffers and optional address.
 * @param flags   Send flags.
 * @param kernel  If true, resolve @p fd in the kernel I/O context.
 * @return Bytes sent on success, or a negative error code.
 */
static ssize_t
common_sendmsg(int fd, const struct msghdr *message, int flags, bool kernel)
{
	file_descriptor* descriptor;
	GET_SOCKET_FD_OR_RETURN(fd, kernel, descriptor);
	FileDescriptorPutter _(descriptor);

	return sStackInterface->sendmsg(FD_SOCKET(descriptor), message, flags);
}


/**
 * @brief Common implementation for getsockopt(2).
 *
 * @param fd      Socket file descriptor.
 * @param level   Protocol level (SOL_SOCKET, IPPROTO_TCP, etc.).
 * @param option  Option name.
 * @param value   Output buffer for the option value.
 * @param _length Input/output: size of @p value.
 * @param kernel  If true, resolve @p fd in the kernel I/O context.
 * @retval B_OK On success.
 */
static status_t
common_getsockopt(int fd, int level, int option, void *value,
	socklen_t *_length, bool kernel)
{
	file_descriptor* descriptor;
	GET_SOCKET_FD_OR_RETURN(fd, kernel, descriptor);
	FileDescriptorPutter _(descriptor);

	return sStackInterface->getsockopt(FD_SOCKET(descriptor), level, option,
		value, _length);
}


/**
 * @brief Common implementation for setsockopt(2).
 *
 * @param fd      Socket file descriptor.
 * @param level   Protocol level (SOL_SOCKET, IPPROTO_TCP, etc.).
 * @param option  Option name.
 * @param value   Buffer containing the new option value.
 * @param length  Size of @p value.
 * @param kernel  If true, resolve @p fd in the kernel I/O context.
 * @retval B_OK On success.
 */
static status_t
common_setsockopt(int fd, int level, int option, const void *value,
	socklen_t length, bool kernel)
{
	file_descriptor* descriptor;
	GET_SOCKET_FD_OR_RETURN(fd, kernel, descriptor);
	FileDescriptorPutter _(descriptor);

	return sStackInterface->setsockopt(FD_SOCKET(descriptor), level, option,
		value, length);
}


/**
 * @brief Common implementation for getpeername(2).
 *
 * @param fd             Socket file descriptor.
 * @param address        Output buffer for the remote peer address.
 * @param _addressLength Input/output size of @p address.
 * @param kernel         If true, resolve @p fd in the kernel I/O context.
 * @retval B_OK On success.
 */
static status_t
common_getpeername(int fd, struct sockaddr *address,
	socklen_t *_addressLength, bool kernel)
{
	file_descriptor* descriptor;
	GET_SOCKET_FD_OR_RETURN(fd, kernel, descriptor);
	FileDescriptorPutter _(descriptor);

	return sStackInterface->getpeername(FD_SOCKET(descriptor), address,
		_addressLength);
}


/**
 * @brief Common implementation for getsockname(2).
 *
 * @param fd             Socket file descriptor.
 * @param address        Output buffer for the local bound address.
 * @param _addressLength Input/output size of @p address.
 * @param kernel         If true, resolve @p fd in the kernel I/O context.
 * @retval B_OK On success.
 */
static status_t
common_getsockname(int fd, struct sockaddr *address,
	socklen_t *_addressLength, bool kernel)
{
	file_descriptor* descriptor;
	GET_SOCKET_FD_OR_RETURN(fd, kernel, descriptor);
	FileDescriptorPutter _(descriptor);

	return sStackInterface->getsockname(FD_SOCKET(descriptor), address,
		_addressLength);
}


/**
 * @brief Common implementation for sockatmark(3).
 *
 * Tests whether the read pointer is currently positioned at an out-of-band
 * mark on the socket.
 *
 * @param fd     Socket file descriptor.
 * @param kernel If true, resolve @p fd in the kernel I/O context.
 * @return 1 if at OOB mark, 0 if not, or a negative error code.
 */
static int
common_sockatmark(int fd, bool kernel)
{
	file_descriptor* descriptor;
	GET_SOCKET_FD_OR_RETURN(fd, kernel, descriptor);
	FileDescriptorPutter _(descriptor);

	return sStackInterface->sockatmark(FD_SOCKET(descriptor));
}


/**
 * @brief Common implementation for socketpair(2).
 *
 * Loads the net stack module, strips creation flags from @p type, calls
 * the stack's socketpair(), wraps each socket in a VFS file descriptor,
 * and acquires an extra module reference for the second FD.
 *
 * @param family   Address family (typically AF_UNIX).
 * @param type     Socket type (SOCK_STREAM, etc.) plus optional SOCK_CLOEXEC etc.
 * @param protocol Protocol number.
 * @param fds      Output array of two file descriptor numbers.
 * @param kernel   If true, publish FDs in the kernel I/O context.
 * @retval B_OK         Both FDs created successfully.
 * @retval B_UNSUPPORTED Net stack could not be loaded.
 */
static status_t
common_socketpair(int family, int type, int protocol, int fds[2], bool kernel)
{
	if (!get_stack_interface_module())
		return B_UNSUPPORTED;

	int sflags = type & (SOCK_CLOEXEC | SOCK_NONBLOCK | SOCK_CLOFORK);
	type &= ~(SOCK_CLOEXEC | SOCK_NONBLOCK | SOCK_CLOFORK);

	net_socket* sockets[2];
	status_t error = sStackInterface->socketpair(family, type, protocol,
		sockets);
	if (error != B_OK) {
		put_stack_interface_module();
		return error;
	}

	// allocate the FDs
	for (int i = 0; i < 2; i++) {
		fds[i] = create_socket_fd(sockets[i], sflags, kernel);
		if (fds[i] < 0) {
			sStackInterface->close(sockets[i]);
			sStackInterface->free(sockets[i]);
			put_stack_interface_module();
			return fds[i];
		}
	}

	// We need another reference for the second socket
	get_stack_interface_module();
	return B_OK;
}


/**
 * @brief Retrieve the next socket statistics entry for a given address family.
 *
 * Loads the net stack module (if not already loaded), delegates to
 * get_next_socket_stat(), and releases the module reference.
 *
 * @param family  Address family to enumerate (0 for all).
 * @param cookie  Opaque iteration cookie; updated on each call.
 * @param stat    Output: net_stat structure populated with socket info.
 * @retval B_OK          Entry returned.
 * @retval B_UNSUPPORTED Net stack could not be loaded.
 */
static status_t
common_get_next_socket_stat(int family, uint32 *cookie, struct net_stat *stat)
{
	if (!get_stack_interface_module())
		return B_UNSUPPORTED;

	status_t status = sStackInterface->get_next_socket_stat(family, cookie,
		stat);

	put_stack_interface_module();
	return status;
}


// #pragma mark - kernel sockets API


/**
 * @brief Kernel-internal socket(2) — create a socket from kernel code.
 *
 * Clears the syscall-restart flag and delegates to common_socket() with
 * kernel=true.
 *
 * @param family   Address family.
 * @param type     Socket type (SOCK_STREAM, etc.).
 * @param protocol Protocol number.
 * @return New file descriptor or a negated errno value.
 */
int
socket(int family, int type, int protocol)
{
	SyscallFlagUnsetter _;
	RETURN_AND_SET_ERRNO(common_socket(family, type, protocol, true));
}


/**
 * @brief Kernel-internal bind(2).
 *
 * @param socket        File descriptor referring to the socket.
 * @param address       Local address to bind.
 * @param addressLength Size of @p address.
 * @return 0 on success or a negated errno value.
 */
int
bind(int socket, const struct sockaddr *address, socklen_t addressLength)
{
	SyscallFlagUnsetter _;
	RETURN_AND_SET_ERRNO(common_bind(socket, address, addressLength, true));
}


/**
 * @brief Kernel-internal shutdown(2).
 *
 * @param socket File descriptor referring to the socket.
 * @param how    SHUT_RD, SHUT_WR, or SHUT_RDWR.
 * @return 0 on success or a negated errno value.
 */
int
shutdown(int socket, int how)
{
	SyscallFlagUnsetter _;
	RETURN_AND_SET_ERRNO(common_shutdown(socket, how, true));
}


/**
 * @brief Kernel-internal connect(2).
 *
 * @param socket        File descriptor referring to the socket.
 * @param address       Remote address to connect to.
 * @param addressLength Size of @p address.
 * @return 0 on success or a negated errno value.
 */
int
connect(int socket, const struct sockaddr *address, socklen_t addressLength)
{
	SyscallFlagUnsetter _;
	RETURN_AND_SET_ERRNO(common_connect(socket, address, addressLength, true));
}


/**
 * @brief Kernel-internal listen(2).
 *
 * @param socket  File descriptor referring to the socket.
 * @param backlog Maximum pending connection queue length.
 * @return 0 on success or a negated errno value.
 */
int
listen(int socket, int backlog)
{
	SyscallFlagUnsetter _;
	RETURN_AND_SET_ERRNO(common_listen(socket, backlog, true));
}


/**
 * @brief Kernel-internal accept(2).
 *
 * @param socket         Listening socket file descriptor.
 * @param address        Output buffer for the peer address (may be NULL).
 * @param _addressLength Input/output size of @p address.
 * @return New file descriptor on success or a negated errno value.
 */
int
accept(int socket, struct sockaddr *address, socklen_t *_addressLength)
{
	SyscallFlagUnsetter _;
	RETURN_AND_SET_ERRNO(common_accept(socket, address, _addressLength, 0, true));
}


/**
 * @brief Kernel-internal accept4(2).
 *
 * Like accept() but allows specifying SOCK_CLOEXEC / SOCK_NONBLOCK / SOCK_CLOFORK
 * flags for the new descriptor.
 *
 * @param socket         Listening socket file descriptor.
 * @param address        Output buffer for the peer address (may be NULL).
 * @param _addressLength Input/output size of @p address.
 * @param flags          Socket flags for the accepted descriptor.
 * @return New file descriptor on success or a negated errno value.
 */
int
accept4(int socket, struct sockaddr *address, socklen_t *_addressLength, int flags)
{
	SyscallFlagUnsetter _;
	RETURN_AND_SET_ERRNO(common_accept(socket, address, _addressLength, flags, true));
}


/**
 * @brief Kernel-internal recv(2).
 *
 * @param socket File descriptor referring to the socket.
 * @param data   Destination buffer.
 * @param length Buffer capacity.
 * @param flags  Receive flags.
 * @return Bytes received or a negated errno value.
 */
ssize_t
recv(int socket, void *data, size_t length, int flags)
{
	SyscallFlagUnsetter _;
	RETURN_AND_SET_ERRNO(common_recv(socket, data, length, flags, true));
}


/**
 * @brief Kernel-internal recvfrom(2).
 *
 * @param socket         File descriptor referring to the socket.
 * @param data           Destination buffer.
 * @param length         Buffer capacity.
 * @param flags          Receive flags.
 * @param address        Output buffer for the sender address (may be NULL).
 * @param _addressLength Input/output size of @p address.
 * @return Bytes received or a negated errno value.
 */
ssize_t
recvfrom(int socket, void *data, size_t length, int flags,
	struct sockaddr *address, socklen_t *_addressLength)
{
	SyscallFlagUnsetter _;
	RETURN_AND_SET_ERRNO(common_recvfrom(socket, data, length, flags, address,
		_addressLength, true));
}


/**
 * @brief Kernel-internal recvmsg(2).
 *
 * @param socket  File descriptor referring to the socket.
 * @param message Message header for the receive operation.
 * @param flags   Receive flags.
 * @return Bytes received or a negated errno value.
 */
ssize_t
recvmsg(int socket, struct msghdr *message, int flags)
{
	SyscallFlagUnsetter _;
	RETURN_AND_SET_ERRNO(common_recvmsg(socket, message, flags, true));
}


/**
 * @brief Kernel-internal send(2).
 *
 * @param socket File descriptor referring to the socket.
 * @param data   Source buffer.
 * @param length Number of bytes to send.
 * @param flags  Send flags.
 * @return Bytes sent or a negated errno value.
 */
ssize_t
send(int socket, const void *data, size_t length, int flags)
{
	SyscallFlagUnsetter _;
	RETURN_AND_SET_ERRNO(common_send(socket, data, length, flags, true));
}


/**
 * @brief Kernel-internal sendto(2).
 *
 * @param socket        File descriptor referring to the socket.
 * @param data          Source buffer.
 * @param length        Number of bytes to send.
 * @param flags         Send flags.
 * @param address       Destination address (may be NULL for connected sockets).
 * @param addressLength Size of @p address.
 * @return Bytes sent or a negated errno value.
 */
ssize_t
sendto(int socket, const void *data, size_t length, int flags,
	const struct sockaddr *address, socklen_t addressLength)
{
	SyscallFlagUnsetter _;
	RETURN_AND_SET_ERRNO(common_sendto(socket, data, length, flags, address,
		addressLength, true));
}


/**
 * @brief Kernel-internal sendmsg(2).
 *
 * @param socket  File descriptor referring to the socket.
 * @param message Message header for the send operation.
 * @param flags   Send flags.
 * @return Bytes sent or a negated errno value.
 */
ssize_t
sendmsg(int socket, const struct msghdr *message, int flags)
{
	SyscallFlagUnsetter _;
	RETURN_AND_SET_ERRNO(common_sendmsg(socket, message, flags, true));
}


/**
 * @brief Kernel-internal getsockopt(2).
 *
 * @param socket  File descriptor referring to the socket.
 * @param level   Protocol level.
 * @param option  Option name.
 * @param value   Output buffer for the option value.
 * @param _length Input/output size of @p value.
 * @return 0 on success or a negated errno value.
 */
int
getsockopt(int socket, int level, int option, void *value, socklen_t *_length)
{
	SyscallFlagUnsetter _;
	RETURN_AND_SET_ERRNO(common_getsockopt(socket, level, option, value,
		_length, true));
}


/**
 * @brief Kernel-internal setsockopt(2).
 *
 * @param socket File descriptor referring to the socket.
 * @param level  Protocol level.
 * @param option Option name.
 * @param value  Buffer with the new option value.
 * @param length Size of @p value.
 * @return 0 on success or a negated errno value.
 */
int
setsockopt(int socket, int level, int option, const void *value,
	socklen_t length)
{
	SyscallFlagUnsetter _;
	RETURN_AND_SET_ERRNO(common_setsockopt(socket, level, option, value,
		length, true));
}


/**
 * @brief Kernel-internal getpeername(2).
 *
 * @param socket         File descriptor referring to the socket.
 * @param address        Output buffer for the peer address.
 * @param _addressLength Input/output size of @p address.
 * @return 0 on success or a negated errno value.
 */
int
getpeername(int socket, struct sockaddr *address, socklen_t *_addressLength)
{
	SyscallFlagUnsetter _;
	RETURN_AND_SET_ERRNO(common_getpeername(socket, address, _addressLength,
		true));
}


/**
 * @brief Kernel-internal getsockname(2).
 *
 * @param socket         File descriptor referring to the socket.
 * @param address        Output buffer for the local bound address.
 * @param _addressLength Input/output size of @p address.
 * @return 0 on success or a negated errno value.
 */
int
getsockname(int socket, struct sockaddr *address, socklen_t *_addressLength)
{
	SyscallFlagUnsetter _;
	RETURN_AND_SET_ERRNO(common_getsockname(socket, address, _addressLength,
		true));
}


/**
 * @brief Kernel-internal sockatmark(3).
 *
 * @param socket File descriptor referring to the socket.
 * @return 1 if at OOB mark, 0 if not, or a negated errno value.
 */
int
sockatmark(int socket)
{
	SyscallFlagUnsetter _;
	RETURN_AND_SET_ERRNO(common_sockatmark(socket, true));
}


/**
 * @brief Kernel-internal socketpair(2).
 *
 * @param family        Address family (typically AF_UNIX).
 * @param type          Socket type.
 * @param protocol      Protocol number.
 * @param socketVector  Output array for the two connected file descriptors.
 * @return 0 on success or a negated errno value.
 */
int
socketpair(int family, int type, int protocol, int socketVector[2])
{
	SyscallFlagUnsetter _;
	RETURN_AND_SET_ERRNO(common_socketpair(family, type, protocol,
		socketVector, true));
}


// #pragma mark - syscalls


/**
 * @brief Userland syscall entry for socket(2).
 *
 * @param family   Address family.
 * @param type     Socket type plus optional SOCK_CLOEXEC / SOCK_NONBLOCK flags.
 * @param protocol Protocol number.
 * @return New file descriptor, or a negated errno value.
 */
int
_user_socket(int family, int type, int protocol)
{
	SyscallRestartWrapper<int> result;
	return result = common_socket(family, type, protocol, false);
}


/**
 * @brief Userland syscall entry for bind(2).
 *
 * Copies the socket address from userland before forwarding to common_bind().
 *
 * @param socket        File descriptor referring to the socket.
 * @param userAddress   Userland pointer to the socket address.
 * @param addressLength Size of the address structure.
 * @retval B_OK On success.
 * @retval B_BAD_ADDRESS / B_BAD_VALUE Address validation failed.
 */
status_t
_user_bind(int socket, const struct sockaddr *userAddress,
	socklen_t addressLength)
{
	sockaddr_storage address;
	status_t error = copy_address_from_userland(userAddress, addressLength, &address);
	if (error != B_OK)
		return error;

	SyscallRestartWrapper<status_t> result;
	return result = common_bind(socket, (sockaddr*)&address, addressLength,
		false);
}


/**
 * @brief Userland syscall entry for shutdown(2).
 *
 * @param socket File descriptor referring to the socket.
 * @param how    SHUT_RD, SHUT_WR, or SHUT_RDWR.
 * @retval B_OK On success.
 */
status_t
_user_shutdown_socket(int socket, int how)
{
	SyscallRestartWrapper<status_t> error;
	return error = common_shutdown(socket, how, false);
}


/**
 * @brief Userland syscall entry for connect(2).
 *
 * Copies the remote address from userland before forwarding to
 * common_connect().
 *
 * @param socket        File descriptor referring to the socket.
 * @param userAddress   Userland pointer to the remote address.
 * @param addressLength Size of the address structure.
 * @retval B_OK On success.
 */
status_t
_user_connect(int socket, const struct sockaddr *userAddress,
	socklen_t addressLength)
{
	sockaddr_storage address;
	status_t error = copy_address_from_userland(userAddress, addressLength, &address);
	if (error != B_OK)
		return error;

	SyscallRestartWrapper<status_t> result;
	return result = common_connect(socket, (sockaddr*)&address, addressLength,
		false);
}


/**
 * @brief Userland syscall entry for listen(2).
 *
 * @param socket  File descriptor referring to the socket.
 * @param backlog Maximum pending connection queue length.
 * @retval B_OK On success.
 */
status_t
_user_listen(int socket, int backlog)
{
	SyscallRestartWrapper<status_t> error;
	return error = common_listen(socket, backlog, false);
}


/**
 * @brief Userland syscall entry for accept4(2).
 *
 * Validates and prepares the userland address output buffer, calls
 * common_accept(), and copies the peer address back to userland. Closes the
 * new FD if the address copy fails.
 *
 * @param socket         Listening socket file descriptor.
 * @param userAddress    Userland output buffer for the peer address (may be NULL).
 * @param _addressLength Userland pointer to the address length field.
 * @param flags          Flags for the new descriptor (SOCK_CLOEXEC, etc.).
 * @return New file descriptor on success, or a negated errno / status code.
 */
int
_user_accept(int socket, struct sockaddr *userAddress,
	socklen_t *_addressLength, int flags)
{
	// check parameters
	socklen_t addressLength = 0;
	status_t error = prepare_userland_address_result(userAddress,
		_addressLength, addressLength, false);
	if (error != B_OK)
		return error;
	const socklen_t userAddressBufferSize = addressLength;

	// accept()
	SyscallRestartWrapper<int> result;
	char address[MAX_SOCKET_ADDRESS_LENGTH] = {};
	result = common_accept(socket,
		userAddress != NULL ? (sockaddr*)address : NULL, &addressLength, flags, false);

	// copy address size and address back to userland
	if (copy_address_to_userland(address, addressLength, userAddress,
			userAddressBufferSize, _addressLength) != B_OK) {
		_user_close(result);
		return B_BAD_ADDRESS;
	}

	return result;
}


/**
 * @brief Userland syscall entry for recv(2).
 *
 * Validates the user data buffer before delegating to common_recv().
 *
 * @param socket File descriptor referring to the socket.
 * @param data   Userland destination buffer.
 * @param length Buffer capacity.
 * @param flags  Receive flags.
 * @return Bytes received or a negated errno value.
 */
ssize_t
_user_recv(int socket, void *data, size_t length, int flags)
{
	if (length > 0 && (data == NULL || !is_user_address_range(data, length)))
		return B_BAD_ADDRESS;

	SyscallRestartWrapper<ssize_t> result;
	return result = common_recv(socket, data, length, flags, false);
}


/**
 * @brief Userland syscall entry for recvfrom(2).
 *
 * Validates data and address buffers, calls common_recvfrom() with a
 * kernel-side address buffer, then copies the peer address back to userland.
 *
 * @param socket         File descriptor referring to the socket.
 * @param data           Userland destination buffer.
 * @param length         Buffer capacity.
 * @param flags          Receive flags.
 * @param userAddress    Userland output buffer for the sender address (may be NULL).
 * @param _addressLength Userland pointer to the address length.
 * @return Bytes received or a negated errno value.
 */
ssize_t
_user_recvfrom(int socket, void *data, size_t length, int flags,
	struct sockaddr *userAddress, socklen_t *_addressLength)
{
	if (length > 0 && (data == NULL || !is_user_address_range(data, length)))
		return B_BAD_ADDRESS;

	// check parameters
	socklen_t addressLength = 0;
	status_t error = prepare_userland_address_result(userAddress,
		_addressLength, addressLength, false);
	if (error != B_OK)
		return error;
	const socklen_t userAddressBufferSize = addressLength;

	// recvfrom()
	SyscallRestartWrapper<ssize_t> result;
	char address[MAX_SOCKET_ADDRESS_LENGTH] = {};
	result = common_recvfrom(socket, data, length, flags,
		userAddress != NULL ? (sockaddr*)address : NULL, &addressLength, false);
	if (result < 0)
		return result;

	// copy address size and address back to userland
	if (copy_address_to_userland(address, addressLength, userAddress,
			userAddressBufferSize, _addressLength) != B_OK) {
		return B_BAD_ADDRESS;
	}

	return result;
}


/**
 * @brief Userland syscall entry for recvmsg(2).
 *
 * Copies and validates the msghdr, iovec array, and ancillary data from
 * userland, calls common_recvmsg(), and copies the address, ancillary data,
 * and updated message header back to userland.
 *
 * @param socket      File descriptor referring to the socket.
 * @param userMessage Userland pointer to the msghdr structure.
 * @param flags       Receive flags.
 * @return Bytes received or a negated errno value.
 */
ssize_t
_user_recvmsg(int socket, struct msghdr *userMessage, int flags)
{
	// copy message from userland
	msghdr message;
	iovec* userVecs;
	MemoryDeleter vecsDeleter;
	void* userAddress;
	char address[MAX_SOCKET_ADDRESS_LENGTH];

	status_t error = prepare_userland_msghdr(userMessage, message, userVecs,
		vecsDeleter, userAddress, address);
	if (error != B_OK)
		return error;

	// prepare a buffer for ancillary data
	MemoryDeleter ancillaryDeleter;
	void* ancillary = NULL;
	void* userAncillary = message.msg_control;
	if (userAncillary != NULL) {
		if (!IS_USER_ADDRESS(userAncillary))
			return B_BAD_ADDRESS;
		if (message.msg_controllen < 0)
			return B_BAD_VALUE;
		if (message.msg_controllen > MAX_ANCILLARY_DATA_LENGTH)
			message.msg_controllen = MAX_ANCILLARY_DATA_LENGTH;

		message.msg_control = ancillary = malloc(message.msg_controllen);
		if (message.msg_control == NULL)
			return B_NO_MEMORY;

		ancillaryDeleter.SetTo(ancillary);
	}

	// recvmsg()
	SyscallRestartWrapper<ssize_t> result;
	result = common_recvmsg(socket, &message, flags, false);
	if (result < 0)
		return result;

	// copy the address, the ancillary data, and the message header back to
	// userland
	message.msg_name = userAddress;
	message.msg_iov = userVecs;
	message.msg_control = userAncillary;
	if ((userAddress != NULL && user_memcpy(userAddress, address,
				message.msg_namelen) != B_OK)
		|| (userAncillary != NULL && user_memcpy(userAncillary, ancillary,
				message.msg_controllen) != B_OK)
		|| user_memcpy(userMessage, &message, sizeof(msghdr)) != B_OK) {
		return B_BAD_ADDRESS;
	}

	return result;
}


/**
 * @brief Userland syscall entry for send(2).
 *
 * Validates the data buffer before delegating to common_send().
 *
 * @param socket File descriptor referring to the socket.
 * @param data   Userland source buffer.
 * @param length Number of bytes to send.
 * @param flags  Send flags.
 * @return Bytes sent or a negated errno value.
 */
ssize_t
_user_send(int socket, const void *data, size_t length, int flags)
{
	if (length > 0 && (data == NULL || !is_user_address_range(data, length)))
		return B_BAD_ADDRESS;

	SyscallRestartWrapper<ssize_t> result;
	return result = common_send(socket, data, length, flags, false);
}


/**
 * @brief Userland syscall entry for sendto(2).
 *
 * Validates data and optionally copies the destination address from userland
 * before calling common_sendto().
 *
 * @param socket        File descriptor referring to the socket.
 * @param data          Userland source buffer.
 * @param length        Number of bytes to send.
 * @param flags         Send flags.
 * @param userAddress   Userland destination address (may be NULL).
 * @param addressLength Size of @p userAddress.
 * @return Bytes sent or a negated errno value.
 */
ssize_t
_user_sendto(int socket, const void *data, size_t length, int flags,
	const struct sockaddr *userAddress, socklen_t addressLength)
{
	if (length > 0 && (data == NULL || !is_user_address_range(data, length)))
		return B_BAD_ADDRESS;

	// copy address from userland
	sockaddr_storage address;
	if (userAddress != NULL) {
		status_t error = copy_address_from_userland(userAddress, addressLength, &address);
		if (error != B_OK)
			return error;
	} else {
		addressLength = 0;
	}

	// sendto()
	SyscallRestartWrapper<ssize_t> result;
	return result = common_sendto(socket, data, length, flags,
		userAddress != NULL ? (sockaddr*)&address : NULL, addressLength, false);
}


/**
 * @brief Userland syscall entry for sendmsg(2).
 *
 * Copies and validates the msghdr, iovec array, destination address, and
 * ancillary data from userland, then calls common_sendmsg().
 *
 * @param socket      File descriptor referring to the socket.
 * @param userMessage Userland pointer to the msghdr structure.
 * @param flags       Send flags.
 * @return Bytes sent or a negated errno value.
 */
ssize_t
_user_sendmsg(int socket, const struct msghdr *userMessage, int flags)
{
	// copy message from userland
	msghdr message;
	iovec* userVecs;
	MemoryDeleter vecsDeleter;
	void* userAddress;
	char address[MAX_SOCKET_ADDRESS_LENGTH];

	status_t error = prepare_userland_msghdr(userMessage, message, userVecs,
		vecsDeleter, userAddress, address);
	if (error != B_OK)
		return error;

	// copy the address from userland
	if (userAddress != NULL
			&& user_memcpy(address, userAddress, message.msg_namelen) != B_OK) {
		return B_BAD_ADDRESS;
	}

	// copy ancillary data from userland
	MemoryDeleter ancillaryDeleter;
	void* userAncillary = message.msg_control;
	if (userAncillary != NULL) {
		if (!IS_USER_ADDRESS(userAncillary))
			return B_BAD_ADDRESS;
		if (message.msg_controllen < 0
				|| message.msg_controllen > MAX_ANCILLARY_DATA_LENGTH) {
			return B_BAD_VALUE;
		}

		message.msg_control = malloc(message.msg_controllen);
		if (message.msg_control == NULL)
			return B_NO_MEMORY;
		ancillaryDeleter.SetTo(message.msg_control);

		if (user_memcpy(message.msg_control, userAncillary,
				message.msg_controllen) != B_OK) {
			return B_BAD_ADDRESS;
		}
	}

	// sendmsg()
	SyscallRestartWrapper<ssize_t> result;
	return result = common_sendmsg(socket, &message, flags, false);
}


/**
 * @brief Userland syscall entry for getsockopt(2).
 *
 * Validates and copies the length from userland, calls common_getsockopt()
 * with a kernel-side value buffer, then copies the value and length back.
 *
 * @param socket    File descriptor referring to the socket.
 * @param level     Protocol level.
 * @param option    Option name.
 * @param userValue Userland output buffer for the option value.
 * @param _length   Userland pointer to the option length.
 * @retval B_OK On success.
 * @retval B_BAD_VALUE / B_BAD_ADDRESS Parameter validation failed.
 */
status_t
_user_getsockopt(int socket, int level, int option, void *userValue,
	socklen_t *_length)
{
	// check params
	if (userValue == NULL || _length == NULL)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(userValue) || !IS_USER_ADDRESS(_length))
		return B_BAD_ADDRESS;

	// copy length from userland
	socklen_t length;
	if (user_memcpy(&length, _length, sizeof(socklen_t)) != B_OK)
		return B_BAD_ADDRESS;

	if (length > MAX_SOCKET_OPTION_LENGTH)
		return B_BAD_VALUE;

	// getsockopt()
	char value[MAX_SOCKET_OPTION_LENGTH];
	SyscallRestartWrapper<status_t> error;
	error = common_getsockopt(socket, level, option, value, &length,
		false);
	if (error != B_OK)
		return error;

	// copy value back to userland
	if (user_memcpy(userValue, value, length) != B_OK)
		return B_BAD_ADDRESS;

	return B_OK;
}


/**
 * @brief Userland syscall entry for setsockopt(2).
 *
 * Validates and copies the option value from userland before calling
 * common_setsockopt().
 *
 * @param socket    File descriptor referring to the socket.
 * @param level     Protocol level.
 * @param option    Option name.
 * @param userValue Userland buffer containing the new option value.
 * @param length    Size of @p userValue.
 * @retval B_OK On success.
 * @retval B_BAD_VALUE / B_BAD_ADDRESS Parameter validation failed.
 */
status_t
_user_setsockopt(int socket, int level, int option, const void *userValue,
	socklen_t length)
{
	// check params
	if (userValue == NULL || length > MAX_SOCKET_OPTION_LENGTH)
		return B_BAD_VALUE;

	// copy value from userland
	char value[MAX_SOCKET_OPTION_LENGTH];
	if (!IS_USER_ADDRESS(userValue)
			|| user_memcpy(value, userValue, length) != B_OK) {
		return B_BAD_ADDRESS;
	}

	// setsockopt();
	SyscallRestartWrapper<status_t> error;
	return error = common_setsockopt(socket, level, option, value, length,
		false);
}


/**
 * @brief Userland syscall entry for getpeername(2).
 *
 * Validates and prepares the userland address output buffer, calls
 * common_getpeername() with a kernel buffer, and copies the result back.
 *
 * @param socket         File descriptor referring to the socket.
 * @param userAddress    Userland output buffer for the peer address.
 * @param _addressLength Userland pointer to the address length.
 * @retval B_OK On success.
 * @retval B_BAD_ADDRESS Copy to or from userland failed.
 */
status_t
_user_getpeername(int socket, struct sockaddr *userAddress,
	socklen_t *_addressLength)
{
	// check parameters
	socklen_t addressLength = 0;
	SyscallRestartWrapper<status_t> error;
	error = prepare_userland_address_result(userAddress, _addressLength,
		addressLength, true);
	if (error != B_OK)
		return error;
	const socklen_t userAddressBufferSize = addressLength;

	// getpeername()
	char address[MAX_SOCKET_ADDRESS_LENGTH] = {};
	error = common_getpeername(socket, (sockaddr*)address, &addressLength,
		false);
	if (error != B_OK)
		return error;

	// copy address size and address back to userland
	if (copy_address_to_userland(address, addressLength, userAddress,
			userAddressBufferSize, _addressLength) != B_OK) {
		return B_BAD_ADDRESS;
	}

	return B_OK;
}


/**
 * @brief Userland syscall entry for getsockname(2).
 *
 * Validates and prepares the userland address output buffer, calls
 * common_getsockname() with a kernel buffer, and copies the result back.
 *
 * @param socket         File descriptor referring to the socket.
 * @param userAddress    Userland output buffer for the local bound address.
 * @param _addressLength Userland pointer to the address length.
 * @retval B_OK On success.
 * @retval B_BAD_ADDRESS Copy to or from userland failed.
 */
status_t
_user_getsockname(int socket, struct sockaddr *userAddress,
	socklen_t *_addressLength)
{
	// check parameters
	socklen_t addressLength = 0;
	SyscallRestartWrapper<status_t> error;
	error = prepare_userland_address_result(userAddress, _addressLength,
		addressLength, true);
	if (error != B_OK)
		return error;
	const socklen_t userAddressBufferSize = addressLength;

	// getsockname()
	char address[MAX_SOCKET_ADDRESS_LENGTH] = {};
	error = common_getsockname(socket, (sockaddr*)address, &addressLength,
		false);
	if (error != B_OK)
		return error;

	// copy address size and address back to userland
	if (copy_address_to_userland(address, addressLength, userAddress,
			userAddressBufferSize, _addressLength) != B_OK) {
		return B_BAD_ADDRESS;
	}

	return B_OK;
}


/**
 * @brief Userland syscall entry for sockatmark(3).
 *
 * @param socket File descriptor referring to the socket.
 * @return 1 if at OOB mark, 0 if not, or a negated errno value.
 */
int
_user_sockatmark(int socket)
{
	SyscallRestartWrapper<status_t> error;
	return error = common_sockatmark(socket, false);
}


/**
 * @brief Userland syscall entry for socketpair(2).
 *
 * Validates the userland output vector pointer, calls common_socketpair()
 * with a kernel-side array, and copies both file descriptor numbers back.
 * Closes both FDs if the copy fails.
 *
 * @param family           Address family.
 * @param type             Socket type.
 * @param protocol         Protocol number.
 * @param userSocketVector Userland output array for two file descriptors.
 * @retval B_OK On success.
 * @retval B_BAD_VALUE / B_BAD_ADDRESS Parameter validation failed.
 */
status_t
_user_socketpair(int family, int type, int protocol, int *userSocketVector)
{
	// check parameters
	if (userSocketVector == NULL)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(userSocketVector))
		return B_BAD_ADDRESS;

	// socketpair()
	int socketVector[2];
	SyscallRestartWrapper<status_t> error;
	error = common_socketpair(family, type, protocol, socketVector, false);
	if (error != B_OK)
		return error;

	// copy FDs back to userland
	if (user_memcpy(userSocketVector, socketVector,
			sizeof(socketVector)) != B_OK) {
		_user_close(socketVector[0]);
		_user_close(socketVector[1]);
		return B_BAD_ADDRESS;
	}

	return B_OK;
}


/**
 * @brief Userland syscall entry to iterate socket statistics.
 *
 * Validates and copies the iteration cookie from userland, calls
 * common_get_next_socket_stat(), and copies the cookie and net_stat back.
 *
 * @param family  Address family filter (0 for all).
 * @param _cookie Userland pointer to the opaque iteration cookie.
 * @param _stat   Userland output pointer for the net_stat structure.
 * @retval B_OK On success.
 * @retval B_BAD_VALUE / B_BAD_ADDRESS Parameter validation failed.
 */
status_t
_user_get_next_socket_stat(int family, uint32 *_cookie, struct net_stat *_stat)
{
	// check parameters and copy cookie from userland
	if (_cookie == NULL || _stat == NULL)
		return B_BAD_VALUE;

	uint32 cookie;
	if (!IS_USER_ADDRESS(_stat) || !IS_USER_ADDRESS(_cookie)
		|| user_memcpy(&cookie, _cookie, sizeof(cookie)) != B_OK) {
		return B_BAD_ADDRESS;
	}

	net_stat stat;
	SyscallRestartWrapper<status_t> error;
	error = common_get_next_socket_stat(family, &cookie, &stat);
	if (error != B_OK)
		return error;

	// copy cookie and data back to userland
	if (user_memcpy(_cookie, &cookie, sizeof(cookie)) != B_OK
		|| user_memcpy(_stat, &stat, sizeof(net_stat)) != B_OK) {
		return B_BAD_ADDRESS;
	}

	return B_OK;
}
