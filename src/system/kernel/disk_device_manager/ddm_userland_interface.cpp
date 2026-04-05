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
 *   Copyright 2003-2017, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold, ingo_weinhold@gmx.de
 *       Axel Dörfler, axeld@pinc-software.de
 */

/**
 * @file ddm_userland_interface.cpp
 * @brief Syscall interface between userland disk management tools and the kernel DDM.
 *
 * Implements all _kern_* syscalls exposed to user space for disk device
 * management: querying devices, partitions, and disk systems; creating,
 * deleting, and modifying partitions; mounting/unmounting; and managing
 * partition change notifications. Handles parameter validation and
 * copying data between kernel and user memory.
 *
 * @see KDiskDeviceManager.cpp, KPartition.cpp
 */

#include <ddm_userland_interface.h>

#include <stdlib.h>

#include <AutoDeleter.h>
#include <fs/KPath.h>
#include <KDiskDevice.h>
#include <KDiskDeviceManager.h>
#include <KDiskDeviceUtils.h>
#include <KDiskSystem.h>
#include <KFileDiskDevice.h>
#include <syscall_args.h>

#include "UserDataWriter.h"

using namespace BPrivate::DiskDevice;

// debugging
#define ERROR(x)


// TODO: Replace all instances, when it has been decided how to handle
// notifications during jobs.
#define DUMMY_JOB_ID	0


/**
 * @brief Safely copies a string from user memory into a kernel buffer.
 *
 * Wraps user_strlcpy() and converts its ssize_t result to a status_t,
 * optionally treating truncation as an error.
 *
 * @param to               Destination kernel buffer.
 * @param from             Source user-space string pointer.
 * @param size             Size of @a to in bytes (including NUL terminator).
 * @param allowTruncation  If @c true, silently truncate strings longer than
 *                         @a size. If @c false, return B_NAME_TOO_LONG when
 *                         the source is longer than the buffer.
 * @return B_OK on success.
 * @retval B_NAME_TOO_LONG  Source string is longer than @a size and
 *                          @a allowTruncation is @c false.
 * @retval negative         user_strlcpy() reported a fault.
 */
static status_t
ddm_strlcpy(char *to, const char *from, size_t size,
	bool allowTruncation = false)
{
	ssize_t fromLen = user_strlcpy(to, from, size);
	if (fromLen < 0)
		return fromLen;
	if ((size_t)fromLen >= size && !allowTruncation)
		return B_NAME_TOO_LONG;
	return B_OK;
}


/**
 * @brief Copies a single typed value from a user-space pointer into a kernel
 *        variable.
 *
 * @tparam Type         The plain-data type to copy.
 * @param  value        Kernel-side destination variable.
 * @param  userValue    User-space source pointer; must not be NULL and must
 *                      be a valid user address.
 * @return B_OK on success.
 * @retval B_BAD_VALUE    @a userValue is NULL.
 * @retval B_BAD_ADDRESS  @a userValue does not pass IS_USER_ADDRESS().
 * @retval other          user_memcpy() error.
 */
template<typename Type>
static inline status_t
copy_from_user_value(Type& value, const Type* userValue)
{
	if (userValue == NULL)
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userValue))
		return B_BAD_ADDRESS;

	return user_memcpy(&value, userValue, sizeof(Type));
}


/**
 * @brief Copies a single typed value from a kernel variable to a user-space
 *        pointer.
 *
 * @tparam Type         The plain-data type to copy.
 * @param  userValue    User-space destination pointer; must not be NULL and
 *                      must be a valid user address.
 * @param  value        Kernel-side source value.
 * @return B_OK on success.
 * @retval B_BAD_VALUE    @a userValue is NULL.
 * @retval B_BAD_ADDRESS  @a userValue does not pass IS_USER_ADDRESS().
 * @retval other          user_memcpy() error.
 */
template<typename Type>
static inline status_t
copy_to_user_value(Type* userValue, const Type& value)
{
	if (userValue == NULL)
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userValue))
		return B_BAD_ADDRESS;

	return user_memcpy(userValue, &value, sizeof(Type));
}


/**
 * @brief RAII helper that copies and owns a user-supplied string parameter.
 *
 * The template parameter @a kAllowsNull controls whether a NULL user pointer
 * is accepted. The copied string is heap-allocated and freed in the
 * destructor.
 *
 * @tparam kAllowsNull  If @c true, a NULL @a userValue is accepted and
 *                      leaves @c value as NULL. If @c false, NULL is
 *                      rejected with B_BAD_VALUE.
 */
template<bool kAllowsNull>
struct UserStringParameter {
	char*	value;

	inline UserStringParameter()
		: value(NULL)
	{
	}

	inline ~UserStringParameter()
	{
		free(value);
	}

	/**
	 * @brief Validates and copies the user string into a heap buffer.
	 *
	 * @param userValue  User-space pointer to the source string.
	 * @param maxSize    Maximum number of bytes to copy (buffer size).
	 * @return B_OK on success.
	 * @retval B_BAD_VALUE    NULL pointer when @a kAllowsNull is @c false.
	 * @retval B_BAD_ADDRESS  Pointer fails IS_USER_ADDRESS().
	 * @retval B_NO_MEMORY    malloc() returned NULL.
	 * @retval B_BUFFER_OVERFLOW  String is longer than @a maxSize - 1.
	 * @retval negative       user_strlcpy() reported a fault.
	 */
	inline status_t Init(const char* userValue, size_t maxSize)
	{
		if (userValue == NULL) {
			if (!kAllowsNull)
				return B_BAD_VALUE;

			return B_OK;
		}

		if (!IS_USER_ADDRESS(userValue))
			return B_BAD_ADDRESS;

		value = (char*)malloc(maxSize);
		if (value == NULL)
			return B_NO_MEMORY;

		ssize_t bytesCopied = user_strlcpy(value, userValue, maxSize);
		if (bytesCopied < 0)
			return bytesCopied;

		if ((size_t)bytesCopied >= maxSize)
			return B_BUFFER_OVERFLOW;

		return B_OK;
	}

	inline operator const char*()
	{
		return value;
	}

	inline operator char*()
	{
		return value;
	}
};


#if 0
static void
move_descendants(KPartition *partition, off_t moveBy)
{
	if (!partition)
		return;
	partition->SetOffset(partition->Offset() + moveBy);
	// move children
	for (int32 i = 0; KPartition *child = partition->ChildAt(i); i++)
		move_descendants(child, moveBy);
}


static status_t
move_descendants_contents(KPartition *partition)
{
	if (!partition)
		return B_BAD_VALUE;
	// implicit content disk system changes
	KDiskSystem *diskSystem = partition->DiskSystem();
	if (diskSystem || partition->AlgorithmData()) {
		status_t error = diskSystem->ShadowPartitionChanged(partition,
			NULL, B_PARTITION_MOVE);
		if (error != B_OK)
			return error;
	}
	// move children's contents
	for (int32 i = 0; KPartition *child = partition->ChildAt(i); i++) {
		status_t error = move_descendants_contents(child);
		if (error != B_OK)
			return error;
	}
	return B_OK;
}
#endif // 0


/**
 * @brief Iterates over registered disk devices, returning the next device ID.
 *
 * On each call the cookie is advanced so that successive calls enumerate all
 * devices. Optionally reports the buffer size needed to hold the full device
 * data (see _user_get_disk_device_data()).
 *
 * @param _cookie      User-space pointer to the iteration cookie (in/out).
 *                     Initialise to 0 before the first call.
 * @param neededSize   If not NULL, receives the byte count required for
 *                     _user_get_disk_device_data() to describe this device.
 * @return The partition_id of the next disk device on success.
 * @retval B_ENTRY_NOT_FOUND  No more devices.
 * @retval B_BAD_ADDRESS      @a _cookie is not a valid user address.
 * @retval other              Copy-in/copy-out error.
 */
partition_id
_user_get_next_disk_device_id(int32 *_cookie, size_t *neededSize)
{
	int32 cookie;
	status_t error = copy_from_user_value(cookie, _cookie);
	if (error != B_OK)
		return error;

	partition_id id = B_ENTRY_NOT_FOUND;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the next device
	if (KDiskDevice *device = manager->RegisterNextDevice(&cookie)) {
		PartitionRegistrar _(device, true);
		id = device->ID();
		if (neededSize != NULL) {
			if (DeviceReadLocker locker = device) {
				// get the needed size
				UserDataWriter writer;
				device->WriteUserData(writer);
				status_t status = copy_to_user_value(neededSize,
					writer.AllocatedSize());
				if (status != B_OK)
					return status;
			} else
				id = B_ERROR;
		}
	}

	error = copy_to_user_value(_cookie, cookie);
	if (error != B_OK)
		return error;
	return id;
}


/**
 * @brief Finds a disk device by its device-node path.
 *
 * Looks up the device whose path matches @a _filename and returns its ID.
 * Optionally returns the size required to hold its user-data representation.
 *
 * @param _filename   User-space pointer to the device path string.
 * @param neededSize  If not NULL, receives the byte count required by
 *                    _user_get_disk_device_data() for this device.
 * @return The partition_id of the matching device on success.
 * @retval B_ENTRY_NOT_FOUND  No device with that path is registered.
 * @retval B_BAD_VALUE        @a _filename is NULL.
 * @retval other              String copy or user-memory error.
 */
partition_id
_user_find_disk_device(const char *_filename, size_t *neededSize)
{
	UserStringParameter<false> filename;
	status_t error = filename.Init(_filename, B_PATH_NAME_LENGTH);
	if (error != B_OK)
		return error;

	partition_id id = B_ENTRY_NOT_FOUND;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// find the device
	if (KDiskDevice *device = manager->RegisterDevice(filename)) {
		PartitionRegistrar _(device, true);
		id = device->ID();
		if (neededSize != NULL) {
			if (DeviceReadLocker locker = device) {
				// get the needed size
				UserDataWriter writer;
				device->WriteUserData(writer);
				error = copy_to_user_value(neededSize, writer.AllocatedSize());
				if (error != B_OK)
					return error;
			} else
				return B_ERROR;
		}
	}
	return id;
}


/**
 * @brief Finds a partition by its published device-node path.
 *
 * Locates the partition whose node path matches @a _filename. Optionally
 * returns the size needed for _user_get_disk_device_data() on the containing
 * device.
 *
 * @param _filename   User-space pointer to the partition path string.
 * @param neededSize  If not NULL, receives the size required for the
 *                    containing device's user data.
 * @return The partition_id of the matching partition on success.
 * @retval B_ENTRY_NOT_FOUND  No partition with that path exists.
 * @retval B_BAD_VALUE        @a _filename is NULL.
 * @retval other              String copy or user-memory error.
 */
partition_id
_user_find_partition(const char *_filename, size_t *neededSize)
{
	UserStringParameter<false> filename;
	status_t error = filename.Init(_filename, B_PATH_NAME_LENGTH);
	if (error != B_OK)
		return error;

	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// find the partition
	KPartition *partition = manager->RegisterPartition(filename);
	if (partition == NULL)
		return B_ENTRY_NOT_FOUND;
	PartitionRegistrar _(partition, true);
	partition_id id = partition->ID();
	if (neededSize != NULL) {
		// get and lock the partition's device
		KDiskDevice *device = manager->RegisterDevice(partition->ID(), false);
		if (device == NULL)
			return B_ENTRY_NOT_FOUND;
		PartitionRegistrar _2(device, true);
		if (DeviceReadLocker locker = device) {
			// get the needed size
			UserDataWriter writer;
			device->WriteUserData(writer);
			error = copy_to_user_value(neededSize, writer.AllocatedSize());
			if (error != B_OK)
				return error;
		} else
			return B_ERROR;
	}
	return id;
}


/**
 * @brief Finds a file-backed disk device by its backing file path.
 *
 * Normalises @a _filename, locates the matching KFileDiskDevice, and returns
 * its ID. Optionally reports the size needed for its user-data representation.
 *
 * @param _filename   User-space pointer to the backing file path.
 * @param neededSize  If not NULL, receives the byte count required by
 *                    _user_get_disk_device_data() for this device.
 * @return The partition_id of the file disk device on success.
 * @retval B_ENTRY_NOT_FOUND  No file device backed by that path is registered.
 * @retval B_BAD_VALUE        @a _filename is NULL.
 * @retval other              String copy or user-memory error.
 */
partition_id
_user_find_file_disk_device(const char *_filename, size_t *neededSize)
{
	UserStringParameter<false> filename;
	status_t error = filename.Init(_filename, B_PATH_NAME_LENGTH);
	if (error != B_OK)
		return error;

	KPath path(filename, KPath::NORMALIZE);

	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// find the device
	KFileDiskDevice* device = manager->RegisterFileDevice(path.Path());
	if (device == NULL)
		return B_ENTRY_NOT_FOUND;
	PartitionRegistrar _(device, true);
	partition_id id = device->ID();
	if (neededSize != NULL) {
		if (DeviceReadLocker locker = device) {
			// get the needed size
			UserDataWriter writer;
			device->WriteUserData(writer);
			error = copy_to_user_value(neededSize, writer.AllocatedSize());
			if (error != B_OK)
				return error;
		} else
			return B_ERROR;
	}
	return id;
}


/*!	\brief Writes data describing the disk device identified by ID and all
		   its partitions into the supplied buffer.

	The function passes the buffer size required to hold the data back
	through the \a _neededSize parameter, if the device could be found at
	least and no serious error occured. If fails with \c B_BUFFER_OVERFLOW,
	if the supplied buffer is too small or a \c NULL buffer is supplied
	(and \c bufferSize is 0).

	The device is identified by \a id. If \a deviceOnly is \c true, then
	it must be the ID of a disk device, otherwise the disk device is
	chosen, on which the partition \a id refers to resides.

	\param id The ID of an arbitrary partition on the disk device (including
		   the disk device itself), whose data shall be returned
		   (if \a deviceOnly is \c false), or the ID of the disk device
		   itself (if \a deviceOnly is true).
	\param deviceOnly Specifies whether only IDs of disk devices (\c true),
		   or also IDs of partitions (\c false) are accepted for \a id.
	\param buffer The buffer into which the disk device data shall be written.
		   May be \c NULL.
	\param bufferSize The size of \a buffer.
	\param _neededSize Pointer to a variable into which the actually needed
		   buffer size is written. May be \c NULL.
	\return
	- \c B_OK: Everything went fine. The device was found and, if not \c NULL,
	  in \a _neededSize the actually needed buffer size is returned. And
	  \a buffer will contain the disk device data.
	- \c B_BAD_VALUE: \c NULL \a buffer, but not 0 \a bufferSize.
	- \c B_BUFFER_OVERFLOW: The supplied buffer was too small. \a _neededSize,
	  if not \c NULL, will contain the required buffer size.
	- \c B_NO_MEMORY: Insufficient memory to complete the operation.
	- \c B_ENTRY_NOT_FOUND: \a id is no valid disk device ID (if \a deviceOnly
	  is \c true) or not even a valid partition ID (if \a deviceOnly is
	  \c false).
	- \c B_ERROR: An unexpected error occured.
	- another error code...
*/
/**
 * @brief Writes the full disk device data (including partition tree) for a
 *        given device or partition ID into a user-space buffer.
 *
 * Performs a dry run first to compute the required buffer size, which is
 * reported via @a _neededSize. If @a buffer is too small or NULL,
 * B_BUFFER_OVERFLOW is returned. Otherwise the data is written into a
 * temporary kernel buffer, relocated, and copied to user space.
 *
 * @param id          Partition or device ID identifying the target device.
 * @param deviceOnly  If @c true, @a id must be a device ID; if @c false,
 *                    any partition ID on the device is accepted.
 * @param buffer      User-space destination buffer; may be NULL.
 * @param bufferSize  Byte size of @a buffer.
 * @param _neededSize If not NULL, receives the required buffer size.
 * @return B_OK on success (buffer populated).
 * @retval B_BAD_VALUE      Non-zero @a bufferSize with NULL @a buffer.
 * @retval B_BUFFER_OVERFLOW @a buffer is too small; @a _neededSize is set.
 * @retval B_ENTRY_NOT_FOUND @a id does not identify a known device/partition.
 * @retval B_NO_MEMORY      Kernel-side allocation failed.
 * @retval B_BAD_ADDRESS    @a buffer is not a valid user address.
 * @retval B_ERROR          Unexpected internal error (size mismatch).
 */
status_t
_user_get_disk_device_data(partition_id id, bool deviceOnly,
	user_disk_device_data *buffer, size_t bufferSize, size_t *_neededSize)
{
	if (buffer == NULL && bufferSize > 0)
		return B_BAD_VALUE;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the device
	KDiskDevice *device = manager->RegisterDevice(id, deviceOnly);
	if (device == NULL)
		return B_ENTRY_NOT_FOUND;

	PartitionRegistrar _(device, true);
	if (DeviceReadLocker locker = device) {
		// do a dry run first to get the needed size
		UserDataWriter writer;
		device->WriteUserData(writer);
		size_t neededSize = writer.AllocatedSize();
		if (_neededSize != NULL) {
			status_t error = copy_ref_var_to_user(neededSize, _neededSize);
			if (error != B_OK)
				return error;
		}
		// if no buffer has been supplied or the buffer is too small,
		// then we're done
		if (buffer == NULL || bufferSize < neededSize)
			return B_BUFFER_OVERFLOW;
		if (!IS_USER_ADDRESS(buffer))
			return B_BAD_ADDRESS;
		// otherwise allocate a kernel buffer
		user_disk_device_data *kernelBuffer
			= static_cast<user_disk_device_data*>(malloc(neededSize));
		if (kernelBuffer == NULL)
			return B_NO_MEMORY;
		MemoryDeleter deleter(kernelBuffer);
		// write the device data into the buffer
		writer.SetTo(kernelBuffer, bufferSize);
		device->WriteUserData(writer);
		// sanity check
		if (writer.AllocatedSize() != neededSize) {
			ERROR(("Size of written disk device user data changed from "
				   "%lu to %lu while device was locked!\n"));
			return B_ERROR;
		}
		// relocate
		status_t error = writer.Relocate(buffer);
		if (error != B_OK)
			return error;
		// copy out
		return user_memcpy(buffer, kernelBuffer, neededSize);
	} else
		return B_ERROR;
}


/**
 * @brief Registers a file as a virtual disk device (loop-device equivalent).
 *
 * If a file device backed by the same normalised path already exists, its ID
 * is returned without creating a duplicate. Otherwise a new KFileDiskDevice
 * is created and registered.
 *
 * @param _filename  User-space pointer to the backing file path.
 * @return The partition_id of the new or existing file device on success.
 * @retval B_BAD_VALUE  @a _filename is NULL.
 * @retval B_ERROR      Manager lock could not be acquired.
 * @retval other        CreateFileDevice() error or string-copy failure.
 */
partition_id
_user_register_file_device(const char *_filename)
{
	UserStringParameter<false> filename;
	status_t error = filename.Init(_filename, B_PATH_NAME_LENGTH);
	if (error != B_OK)
		return error;

	KPath path(filename, KPath::NORMALIZE);

	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	if (ManagerLocker locker = manager) {
		KFileDiskDevice *device = manager->FindFileDevice(path.Path());
		if (device != NULL)
			return device->ID();
		return manager->CreateFileDevice(path.Path());
	}
	return B_ERROR;
}


/**
 * @brief Unregisters a previously registered file disk device.
 *
 * The device to remove may be identified either by its numeric @a deviceID
 * or by the backing file path @a _filename (but not both as NULL).
 *
 * @param deviceID   ID of the file device to remove, or a negative value to
 *                   look up by path instead.
 * @param _filename  User-space pointer to the backing file path; used only
 *                   when @a deviceID is negative. May be NULL if @a deviceID
 *                   is valid.
 * @return B_OK on success.
 * @retval B_BAD_VALUE  Both @a deviceID is negative and @a _filename is NULL.
 * @retval other        DeleteFileDevice() or string-copy error.
 */
status_t
_user_unregister_file_device(partition_id deviceID, const char *_filename)
{
	if (deviceID < 0 && _filename == NULL)
		return B_BAD_VALUE;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	if (deviceID >= 0)
		return manager->DeleteFileDevice(deviceID);

	UserStringParameter<false> filename;
	status_t error = filename.Init(_filename, B_PATH_NAME_LENGTH);
	if (error != B_OK)
		return error;

	return manager->DeleteFileDevice(filename);
}


/**
 * @brief Copies the backing file path of a file disk device to user space.
 *
 * Looks up the device identified by @a id, verifies it is a KFileDiskDevice,
 * and copies its file path into the caller-supplied user buffer.
 *
 * @param id          ID of the file disk device.
 * @param buffer      User-space destination buffer for the path string.
 * @param bufferSize  Size of @a buffer in bytes.
 * @return B_OK on success.
 * @retval B_BAD_VALUE    @a id is negative, @a buffer is NULL, or
 *                        @a bufferSize is 0, or the device is not a file
 *                        device.
 * @retval B_BAD_ADDRESS  @a buffer is not a valid user address.
 * @retval B_BUFFER_OVERFLOW The path is longer than @a bufferSize.
 * @retval B_ERROR        Device not found or lock failed.
 */
status_t
_user_get_file_disk_device_path(partition_id id, char* buffer,
	size_t bufferSize)
{
	if (id < 0 || buffer == NULL || bufferSize == 0)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(buffer))
		return B_BAD_ADDRESS;

	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	KDiskDevice *device = manager->RegisterDevice(id, true);
	if (device != NULL) {
		PartitionRegistrar _(device, true);
		if (DeviceReadLocker locker = device) {
			KFileDiskDevice* fileDevice
				= dynamic_cast<KFileDiskDevice*>(device);
			if (fileDevice == NULL)
				return B_BAD_VALUE;

			ssize_t copied = user_strlcpy(buffer, fileDevice->FilePath(),
				bufferSize);
			if (copied < 0)
				return copied;
			return (size_t)copied < bufferSize ? B_OK : B_BUFFER_OVERFLOW;
		}
	}

	return B_ERROR;
}


/**
 * @brief Retrieves information about a disk system by its numeric ID.
 *
 * Fills a user_disk_system_info structure with the name, flags, and
 * capabilities of the disk system whose ID equals @a id.
 *
 * @param id     Numeric disk_system_id to look up.
 * @param _info  User-space pointer to receive the user_disk_system_info.
 * @return B_OK on success.
 * @retval B_BAD_VALUE    @a _info is NULL.
 * @retval B_ENTRY_NOT_FOUND No disk system with the given ID was found.
 * @retval other          User-memory copy error.
 */
status_t
_user_get_disk_system_info(disk_system_id id, user_disk_system_info *_info)
{
	if (_info == NULL)
		return B_BAD_VALUE;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	if (ManagerLocker locker = manager) {
		KDiskSystem *diskSystem = manager->FindDiskSystem(id);
		if (diskSystem != NULL) {
			user_disk_system_info info;
			diskSystem->GetInfo(&info);
			return copy_to_user_value(_info, info);
		}
	}
	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Iterates over registered disk systems, returning the next entry.
 *
 * Successive calls with the same cookie enumerate all loaded disk systems.
 * The cookie must be initialised to 0 before the first call.
 *
 * @param _cookie  User-space pointer to the iteration cookie (in/out).
 * @param _info    User-space pointer to receive the user_disk_system_info.
 * @return B_OK on success (a disk system was found and @a _info was filled).
 * @retval B_BAD_VALUE    @a _info is NULL.
 * @retval B_BAD_ADDRESS  @a _info is not a valid user address.
 * @retval B_ENTRY_NOT_FOUND No more disk systems.
 * @retval other          Copy-in/copy-out error.
 */
status_t
_user_get_next_disk_system_info(int32 *_cookie, user_disk_system_info *_info)
{
	if (_info == NULL)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(_info))
		return B_BAD_ADDRESS;
	int32 cookie;
	status_t result = copy_from_user_value(cookie, _cookie);
	if (result != B_OK)
		return result;
	result = B_ENTRY_NOT_FOUND;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	if (ManagerLocker locker = manager) {
		KDiskSystem *diskSystem = manager->NextDiskSystem(&cookie);
		if (diskSystem != NULL) {
			user_disk_system_info info;
			diskSystem->GetInfo(&info);
			result = copy_to_user_value(_info, info);
		}
	}
	status_t error = copy_to_user_value(_cookie, cookie);
	if (error != B_OK)
		result = error;
	return result;
}


/**
 * @brief Finds a disk system by its name string.
 *
 * Looks up the disk system whose name exactly matches @a _name and copies
 * its info to @a _info.
 *
 * @param _name  User-space pointer to the disk system name string.
 * @param _info  User-space pointer to receive the user_disk_system_info.
 * @return B_OK on success.
 * @retval B_BAD_VALUE    @a _name or @a _info is NULL.
 * @retval B_BAD_ADDRESS  Either pointer fails IS_USER_ADDRESS().
 * @retval B_ENTRY_NOT_FOUND No disk system with that name was found.
 * @retval other          String copy or user-memory error.
 */
status_t
_user_find_disk_system(const char *_name, user_disk_system_info *_info)
{
	if (_name == NULL || _info == NULL)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(_name) || !IS_USER_ADDRESS(_info))
		return B_BAD_ADDRESS;
	char name[B_DISK_SYSTEM_NAME_LENGTH];
	status_t error = ddm_strlcpy(name, _name, B_DISK_SYSTEM_NAME_LENGTH);
	if (error != B_OK)
		return error;
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	if (ManagerLocker locker = manager) {
		KDiskSystem *diskSystem = manager->FindDiskSystem(name);
		if (diskSystem != NULL) {
			user_disk_system_info info;
			diskSystem->GetInfo(&info);
			return copy_to_user_value(_info, info);
		}
	}
	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Asks the disk system managing a partition to defragment it.
 *
 * Validates the change counter, marks the partition busy, delegates to
 * KDiskSystem::Defragment(), then returns the updated change counter.
 *
 * @param partitionID     ID of the partition to defragment.
 * @param _changeCounter  User-space pointer to the current change counter
 *                        (in); receives the updated counter on success.
 * @return B_OK on success.
 * @retval B_ENTRY_NOT_FOUND  @a partitionID is unknown.
 * @retval B_BAD_VALUE        Change counter mismatch or no disk system set.
 * @retval B_BUSY             Partition is already busy.
 * @retval other              KDiskSystem::Defragment() error or copy error.
 */
status_t
_user_defragment_partition(partition_id partitionID, int32* _changeCounter)
{
	// copy parameters in
	int32 changeCounter;

	status_t error = copy_from_user_value(changeCounter, _changeCounter);
	if (error != B_OK)
		return error;

	// get the partition
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	KPartition* partition = manager->WriteLockPartition(partitionID);
	if (partition == NULL)
		return B_ENTRY_NOT_FOUND;

	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceWriteLocker locker(partition->Device(), true);

	// check change counter
	if (changeCounter != partition->ChangeCounter())
		return B_BAD_VALUE;

	// the partition must be initialized
	KDiskSystem* diskSystem = partition->DiskSystem();
	if (diskSystem == NULL)
		return B_BAD_VALUE;

	// mark the partition busy and unlock
	if (!partition->CheckAndMarkBusy(false))
		return B_BUSY;
	locker.Unlock();

	// defragment
	error = diskSystem->Defragment(partition, DUMMY_JOB_ID);

	// re-lock and unmark busy
	locker.Lock();
	partition->UnmarkBusy(false);

	if (error != B_OK)
		return error;

	// return change counter
	return copy_to_user_value(_changeCounter, partition->ChangeCounter());
}


/**
 * @brief Checks or repairs the file system on a partition.
 *
 * Validates the change counter, marks the partition busy, then calls
 * KDiskSystem::Repair(). If @a checkOnly is @c true only a consistency
 * check is performed without writing fixes.
 *
 * @param partitionID     ID of the partition to check or repair.
 * @param _changeCounter  User-space pointer to the current change counter
 *                        (in); receives the updated counter on success.
 * @param checkOnly       If @c true, perform a read-only check only.
 * @return B_OK on success.
 * @retval B_ENTRY_NOT_FOUND  @a partitionID is unknown.
 * @retval B_BAD_VALUE        Change counter mismatch or no disk system set.
 * @retval B_BUSY             Partition is already busy.
 * @retval other              KDiskSystem::Repair() error or copy error.
 */
status_t
_user_repair_partition(partition_id partitionID, int32* _changeCounter,
	bool checkOnly)
{
	// copy parameters in
	int32 changeCounter;

	status_t error = copy_from_user_value(changeCounter, _changeCounter);
	if (error != B_OK)
		return error;

	// get the partition
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	KPartition* partition = manager->WriteLockPartition(partitionID);
	if (partition == NULL)
		return B_ENTRY_NOT_FOUND;

	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceWriteLocker locker(partition->Device(), true);

	// check change counter
	if (changeCounter != partition->ChangeCounter())
		return B_BAD_VALUE;

	// the partition must be initialized
	KDiskSystem* diskSystem = partition->DiskSystem();
	if (diskSystem == NULL)
		return B_BAD_VALUE;

	// mark the partition busy and unlock
	if (!partition->CheckAndMarkBusy(false))
		return B_BUSY;
	locker.Unlock();

	// repair/check
	error = diskSystem->Repair(partition, checkOnly, DUMMY_JOB_ID);

	// re-lock and unmark busy
	locker.Lock();
	partition->UnmarkBusy(false);

	if (error != B_OK)
		return error;

	// return change counter
	return copy_to_user_value(_changeCounter, partition->ChangeCounter());
}


/**
 * @brief Resizes a child partition and optionally its content.
 *
 * Validates both the parent and child change counters, marks them busy,
 * then resizes the content (if shrinking), the partition entry, and the
 * content again (if growing). Updated change counters for both parent and
 * child are returned on success.
 *
 * @param partitionID         ID of the parent partition.
 * @param _changeCounter      User-space pointer to the parent change counter
 *                            (in/out).
 * @param childID             ID of the child partition to resize.
 * @param _childChangeCounter User-space pointer to the child change counter
 *                            (in/out).
 * @param size                New total size for the child partition in bytes.
 * @param contentSize         New size for the child's content in bytes.
 * @return B_OK on success.
 * @retval B_ENTRY_NOT_FOUND  @a partitionID or @a childID is unknown.
 * @retval B_BAD_VALUE        Change counter mismatch, wrong parent, or
 *                            invalid size combination.
 * @retval B_BUSY             Either partition is already busy.
 * @retval other              Resize operation or copy error.
 */
status_t
_user_resize_partition(partition_id partitionID, int32* _changeCounter,
	partition_id childID, int32* _childChangeCounter, off_t size,
	off_t contentSize)
{
	// copy parameters in
	int32 changeCounter;
	int32 childChangeCounter;

	status_t error = copy_from_user_value(changeCounter, _changeCounter);
	if (error == B_OK)
		error = copy_from_user_value(childChangeCounter, _childChangeCounter);
	if (error != B_OK)
		return error;

	// get the partition
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	KPartition* partition = manager->WriteLockPartition(partitionID);
	if (partition == NULL)
		return B_ENTRY_NOT_FOUND;

	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceWriteLocker locker(partition->Device(), true);

	// register child
	KPartition* child = manager->RegisterPartition(childID);
	if (child == NULL)
		return B_ENTRY_NOT_FOUND;

	PartitionRegistrar registrar3(child, true);

	// check change counters
	if (changeCounter != partition->ChangeCounter()
		|| childChangeCounter != child->ChangeCounter()) {
		return B_BAD_VALUE;
	}

	// the partition must be initialized
	KDiskSystem* diskSystem = partition->DiskSystem();
	if (diskSystem == NULL)
		return B_BAD_VALUE;

	// child must indeed be a child of partition
	if (child->Parent() != partition)
		return B_BAD_VALUE;

	// check sizes
	if (size < 0 || contentSize < 0 || size < contentSize
		|| size > partition->ContentSize()) {
		return B_BAD_VALUE;
	}

	// mark the partitions busy and unlock
	if (partition->IsBusy() || child->IsBusy())
		return B_BUSY;
	partition->SetBusy(true);
	child->SetBusy(true);
	locker.Unlock();

	// resize contents first, if shrinking
	if (child->DiskSystem() && contentSize < child->ContentSize())
		error = child->DiskSystem()->Resize(child, contentSize, DUMMY_JOB_ID);

	// resize the partition
	if (error == B_OK && size != child->Size())
		error = diskSystem->ResizeChild(child, size, DUMMY_JOB_ID);

	// resize contents last, if growing
	if (error == B_OK && child->DiskSystem()
		&& contentSize > child->ContentSize()) {
		error = child->DiskSystem()->Resize(child, contentSize, DUMMY_JOB_ID);
	}

	// re-lock and unmark busy
	locker.Lock();
	partition->SetBusy(false);
	child->SetBusy(false);

	if (error != B_OK)
		return error;

	// return change counters
	error = copy_to_user_value(_changeCounter, partition->ChangeCounter());
	if (error == B_OK)
		error = copy_to_user_value(_childChangeCounter, child->ChangeCounter());
	return error;
}


/**
 * @brief Moves a child partition to a new offset on the device.
 *
 * Currently unimplemented (the body is compiled out via @c #if 0). Always
 * returns B_BAD_VALUE.
 *
 * @param partitionID            ID of the parent partition.
 * @param changeCounter          User-space pointer to the parent change counter.
 * @param childID                ID of the child partition to move.
 * @param childChangeCounter     User-space pointer to the child change counter.
 * @param newOffset              Desired new byte offset for the child.
 * @param descendantIDs          Array of descendant partition IDs to update.
 * @param descendantChangeCounters Array of change counters for descendants.
 * @param descendantCount        Number of entries in the descendant arrays.
 * @return B_BAD_VALUE always (not yet implemented).
 */
status_t
_user_move_partition(partition_id partitionID, int32* changeCounter,
	partition_id childID, int32* childChangeCounter, off_t newOffset,
	partition_id* descendantIDs, int32* descendantChangeCounters,
	int32 descendantCount)
{
#if 0
	KDiskDeviceManager *manager = KDiskDeviceManager::Default();
	// get the partition
	KPartition *partition = manager->WriteLockPartition(partitionID);
	if (!partition)
		return B_ENTRY_NOT_FOUND;
	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceWriteLocker locker(partition->Device(), true);
	// check the new offset
	if (newOffset == partition->Offset())
		return B_OK;
	off_t proposedOffset = newOffset;
	status_t error = validate_move_partition(partition, changeCounter,
		&proposedOffset, true);
	if (error != B_OK)
		return error;
	if (proposedOffset != newOffset)
		return B_BAD_VALUE;
	// new offset is fine -- move the thing
	off_t moveBy = newOffset - partition->Offset();
	move_descendants(partition, moveBy);
	partition->Changed(B_PARTITION_CHANGED_OFFSET);
	// implicit partitioning system changes
	error = partition->Parent()->DiskSystem()->ShadowPartitionChanged(
		partition->Parent(), partition, B_PARTITION_MOVE_CHILD);
	if (error != B_OK)
		return error;
	// implicit descendants' content disk system changes
	return move_descendants_contents(partition);
#endif
return B_BAD_VALUE;
}


/**
 * @brief Sets the name of a child partition via its parent's disk system.
 *
 * Validates change counters, marks parent and child busy, calls
 * KDiskSystem::SetName(), then returns updated change counters.
 *
 * @param partitionID         ID of the parent partition.
 * @param _changeCounter      User-space pointer to parent change counter
 *                            (in/out).
 * @param childID             ID of the child partition to rename.
 * @param _childChangeCounter User-space pointer to child change counter
 *                            (in/out).
 * @param _name               User-space pointer to the new name string.
 * @return B_OK on success.
 * @retval B_ENTRY_NOT_FOUND  @a partitionID or @a childID is unknown.
 * @retval B_BAD_VALUE        Change counter mismatch, wrong parent, or
 *                            no disk system set.
 * @retval B_BUSY             Either partition is already busy.
 * @retval other              SetName() operation or copy error.
 */
status_t
_user_set_partition_name(partition_id partitionID, int32* _changeCounter,
	partition_id childID, int32* _childChangeCounter, const char* _name)
{
	// copy parameters in
	UserStringParameter<false> name;
	int32 changeCounter;
	int32 childChangeCounter;

	status_t error = name.Init(_name, B_DISK_DEVICE_NAME_LENGTH);
	if (error == B_OK)
		error = copy_from_user_value(changeCounter, _changeCounter);
	if (error == B_OK)
		error = copy_from_user_value(childChangeCounter, _childChangeCounter);
	if (error != B_OK)
		return error;

	// get the partition
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	KPartition* partition = manager->WriteLockPartition(partitionID);
	if (partition == NULL)
		return B_ENTRY_NOT_FOUND;

	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceWriteLocker locker(partition->Device(), true);

	// register child
	KPartition* child = manager->RegisterPartition(childID);
	if (child == NULL)
		return B_ENTRY_NOT_FOUND;

	PartitionRegistrar registrar3(child, true);

	// check change counters
	if (changeCounter != partition->ChangeCounter()
		|| childChangeCounter != child->ChangeCounter()) {
		return B_BAD_VALUE;
	}

	// the partition must be initialized
	KDiskSystem* diskSystem = partition->DiskSystem();
	if (diskSystem == NULL)
		return B_BAD_VALUE;

	// child must indeed be a child of partition
	if (child->Parent() != partition)
		return B_BAD_VALUE;

	// mark the partitions busy and unlock
	if (partition->IsBusy() || child->IsBusy())
		return B_BUSY;
	partition->SetBusy(true);
	child->SetBusy(true);
	locker.Unlock();

	// set the child name
	error = diskSystem->SetName(child, name.value, DUMMY_JOB_ID);

	// re-lock and unmark busy
	locker.Lock();
	partition->SetBusy(false);
	child->SetBusy(false);

	if (error != B_OK)
		return error;

	// return change counters
	error = copy_to_user_value(_changeCounter, partition->ChangeCounter());
	if (error == B_OK)
		error = copy_to_user_value(_childChangeCounter, child->ChangeCounter());
	return error;
}


/**
 * @brief Sets the content (volume) name of an already-initialised partition.
 *
 * Validates the change counter, marks the partition busy, delegates to
 * KDiskSystem::SetContentName(), and returns the updated change counter.
 * A NULL name is allowed (treated as an empty/default label by the disk
 * system).
 *
 * @param partitionID     ID of the partition whose content name to change.
 * @param _changeCounter  User-space pointer to the change counter (in/out).
 * @param _name           User-space pointer to the new content name string;
 *                        may be NULL.
 * @return B_OK on success.
 * @retval B_ENTRY_NOT_FOUND  @a partitionID is unknown.
 * @retval B_BAD_VALUE        Change counter mismatch or no disk system set.
 * @retval B_BUSY             Partition is already busy.
 * @retval other              SetContentName() or copy error.
 */
status_t
_user_set_partition_content_name(partition_id partitionID,
	int32* _changeCounter, const char* _name)
{
	// copy parameters in
	UserStringParameter<true> name;
	int32 changeCounter;

	status_t error = name.Init(_name, B_DISK_DEVICE_NAME_LENGTH);
	if (error == B_OK)
		error = copy_from_user_value(changeCounter, _changeCounter);
	if (error != B_OK)
		return error;

	// get the partition
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	KPartition* partition = manager->WriteLockPartition(partitionID);
	if (partition == NULL)
		return B_ENTRY_NOT_FOUND;

	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceWriteLocker locker(partition->Device(), true);

	// check change counter
	if (changeCounter != partition->ChangeCounter())
		return B_BAD_VALUE;

	// the partition must be initialized
	KDiskSystem* diskSystem = partition->DiskSystem();
	if (diskSystem == NULL)
		return B_BAD_VALUE;

	// mark the partition busy and unlock
	if (!partition->CheckAndMarkBusy(false))
		return B_BUSY;
	locker.Unlock();

	// set content parameters
	error = diskSystem->SetContentName(partition, name.value, DUMMY_JOB_ID);

	// re-lock and unmark busy
	locker.Lock();
	partition->UnmarkBusy(false);

	if (error != B_OK)
		return error;

	// return change counter
	return copy_to_user_value(_changeCounter, partition->ChangeCounter());
}


/**
 * @brief Sets the type string of a child partition.
 *
 * Validates change counters, marks parent and child busy, delegates to
 * KDiskSystem::SetType(), then returns updated change counters.
 *
 * @param partitionID         ID of the parent partition.
 * @param _changeCounter      User-space pointer to parent change counter
 *                            (in/out).
 * @param childID             ID of the child partition to retype.
 * @param _childChangeCounter User-space pointer to child change counter
 *                            (in/out).
 * @param _type               User-space pointer to the new type string.
 * @return B_OK on success.
 * @retval B_ENTRY_NOT_FOUND  @a partitionID or @a childID is unknown.
 * @retval B_BAD_VALUE        Change counter mismatch, wrong parent, or no
 *                            disk system set.
 * @retval B_BUSY             Either partition is already busy.
 * @retval other              SetType() or copy error.
 */
status_t
_user_set_partition_type(partition_id partitionID, int32* _changeCounter,
	partition_id childID, int32* _childChangeCounter, const char* _type)
{
	// copy parameters in
	UserStringParameter<false> type;
	int32 changeCounter;
	int32 childChangeCounter;

	status_t error = type.Init(_type, B_DISK_DEVICE_TYPE_LENGTH);
	if (error == B_OK)
		error = copy_from_user_value(changeCounter, _changeCounter);
	if (error == B_OK)
		error = copy_from_user_value(childChangeCounter, _childChangeCounter);
	if (error != B_OK)
		return error;


	// get the partition
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	KPartition* partition = manager->WriteLockPartition(partitionID);
	if (partition == NULL)
		return B_ENTRY_NOT_FOUND;

	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceWriteLocker locker(partition->Device(), true);

	// register child
	KPartition* child = manager->RegisterPartition(childID);
	if (child == NULL)
		return B_ENTRY_NOT_FOUND;

	PartitionRegistrar registrar3(child, true);

	// check change counters
	if (changeCounter != partition->ChangeCounter()
		|| childChangeCounter != child->ChangeCounter()) {
		return B_BAD_VALUE;
	}

	// the partition must be initialized
	KDiskSystem* diskSystem = partition->DiskSystem();
	if (diskSystem == NULL)
		return B_BAD_VALUE;

	// child must indeed be a child of partition
	if (child->Parent() != partition)
		return B_BAD_VALUE;

	// mark the partition busy and unlock
	if (partition->IsBusy() || child->IsBusy())
		return B_BUSY;
	partition->SetBusy(true);
	child->SetBusy(true);
	locker.Unlock();

	// set the child type
	error = diskSystem->SetType(child, type.value, DUMMY_JOB_ID);

	// re-lock and unmark busy
	locker.Lock();
	partition->SetBusy(false);
	child->SetBusy(false);

	if (error != B_OK)
		return error;

	// return change counters
	error = copy_to_user_value(_changeCounter, partition->ChangeCounter());
	if (error == B_OK)
		error = copy_to_user_value(_childChangeCounter, child->ChangeCounter());
	return error;
}


/**
 * @brief Sets the partitioning-system parameters string for a child partition.
 *
 * Validates change counters, marks parent and child busy, delegates to
 * KDiskSystem::SetParameters(), and returns updated change counters.
 * A NULL parameters string is accepted (clears current parameters).
 *
 * @param partitionID         ID of the parent partition.
 * @param _changeCounter      User-space pointer to parent change counter
 *                            (in/out).
 * @param childID             ID of the child partition to update.
 * @param _childChangeCounter User-space pointer to child change counter
 *                            (in/out).
 * @param _parameters         User-space pointer to the new parameters string;
 *                            may be NULL.
 * @return B_OK on success.
 * @retval B_ENTRY_NOT_FOUND  @a partitionID or @a childID is unknown.
 * @retval B_BAD_VALUE        Change counter mismatch, wrong parent, or no
 *                            disk system set.
 * @retval B_BUSY             Either partition is already busy.
 * @retval other              SetParameters() or copy error.
 */
status_t
_user_set_partition_parameters(partition_id partitionID, int32* _changeCounter,
	partition_id childID, int32* _childChangeCounter, const char* _parameters)
{
	// copy parameters in
	UserStringParameter<true> parameters;
	int32 changeCounter;
	int32 childChangeCounter;

	status_t error
		= parameters.Init(_parameters, B_DISK_DEVICE_MAX_PARAMETER_SIZE);
	if (error == B_OK)
		error = copy_from_user_value(changeCounter, _changeCounter);
	if (error == B_OK)
		error = copy_from_user_value(childChangeCounter, _childChangeCounter);
	if (error != B_OK)
		return error;

	// get the partition
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	KPartition* partition = manager->WriteLockPartition(partitionID);
	if (partition == NULL)
		return B_ENTRY_NOT_FOUND;

	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceWriteLocker locker(partition->Device(), true);

	// register child
	KPartition* child = manager->RegisterPartition(childID);
	if (child == NULL)
		return B_ENTRY_NOT_FOUND;

	PartitionRegistrar registrar3(child, true);

	// check change counters
	if (changeCounter != partition->ChangeCounter()
		|| childChangeCounter != child->ChangeCounter()) {
		return B_BAD_VALUE;
	}

	// the partition must be initialized
	KDiskSystem* diskSystem = partition->DiskSystem();
	if (diskSystem == NULL)
		return B_BAD_VALUE;

	// child must indeed be a child of partition
	if (child->Parent() != partition)
		return B_BAD_VALUE;

	// mark the partition busy and unlock
	if (partition->IsBusy() || child->IsBusy())
		return B_BUSY;
	partition->SetBusy(true);
	child->SetBusy(true);
	locker.Unlock();

	// set the child parameters
	error = diskSystem->SetParameters(child, parameters.value, DUMMY_JOB_ID);

	// re-lock and unmark busy
	locker.Lock();
	partition->SetBusy(false);
	child->SetBusy(false);

	if (error != B_OK)
		return error;

	// return change counters
	error = copy_to_user_value(_changeCounter, partition->ChangeCounter());
	if (error == B_OK)
		error = copy_to_user_value(_childChangeCounter, child->ChangeCounter());
	return error;
}


/**
 * @brief Sets the content-level parameters string of an initialised partition.
 *
 * Validates the change counter, marks the partition and all its descendants
 * busy (via CheckAndMarkBusy(true)), delegates to
 * KDiskSystem::SetContentParameters(), then returns the updated change
 * counter. A NULL parameters string is accepted.
 *
 * @param partitionID     ID of the partition to update.
 * @param _changeCounter  User-space pointer to the change counter (in/out).
 * @param _parameters     User-space pointer to the new parameters string;
 *                        may be NULL.
 * @return B_OK on success.
 * @retval B_ENTRY_NOT_FOUND  @a partitionID is unknown.
 * @retval B_BAD_VALUE        Change counter mismatch or no disk system set.
 * @retval B_BUSY             Partition or a descendant is already busy.
 * @retval other              SetContentParameters() or copy error.
 */
status_t
_user_set_partition_content_parameters(partition_id partitionID,
	int32* _changeCounter, const char* _parameters)
{
	// copy parameters in
	UserStringParameter<true> parameters;
	int32 changeCounter;

	status_t error
		= parameters.Init(_parameters, B_DISK_DEVICE_MAX_PARAMETER_SIZE);
	if (error == B_OK)
		error = copy_from_user_value(changeCounter, _changeCounter);
	if (error != B_OK)
		return error;

	// get the partition
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	KPartition* partition = manager->WriteLockPartition(partitionID);
	if (partition == NULL)
		return B_ENTRY_NOT_FOUND;

	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceWriteLocker locker(partition->Device(), true);

	// check change counter
	if (changeCounter != partition->ChangeCounter())
		return B_BAD_VALUE;

	// the partition must be initialized
	KDiskSystem* diskSystem = partition->DiskSystem();
	if (diskSystem == NULL)
		return B_BAD_VALUE;

	// mark the partition busy and unlock
	if (!partition->CheckAndMarkBusy(true))
		return B_BUSY;
	locker.Unlock();

	// set content parameters
	error = diskSystem->SetContentParameters(partition, parameters.value,
		DUMMY_JOB_ID);

	// re-lock and unmark busy
	locker.Lock();
	partition->UnmarkBusy(true);

	if (error != B_OK)
		return error;

	// return change counter
	return copy_to_user_value(_changeCounter, partition->ChangeCounter());
}


/**
 * @brief Initialises a partition with a new disk (file) system.
 *
 * The partition must currently be uninitialised (no disk system). Loads the
 * named disk system, marks the partition busy, calls KDiskSystem::Initialize(),
 * then assigns the disk system to the partition object if it was not already
 * set by the underlying driver. Returns the updated change counter.
 *
 * @param partitionID      ID of the partition to initialise.
 * @param _changeCounter   User-space pointer to the change counter (in/out).
 * @param _diskSystemName  User-space pointer to the disk system name string.
 * @param _name            User-space pointer to the volume name; may be NULL.
 * @param _parameters      User-space pointer to creation parameters; may be
 *                         NULL.
 * @return B_OK on success.
 * @retval B_ENTRY_NOT_FOUND  @a partitionID is unknown or the disk system
 *                             name is not registered.
 * @retval B_BAD_VALUE        Change counter mismatch or partition already
 *                             has a disk system.
 * @retval B_BUSY             Partition is already busy.
 * @retval other              Initialize() or copy error.
 */
status_t
_user_initialize_partition(partition_id partitionID, int32* _changeCounter,
	const char* _diskSystemName, const char* _name, const char* _parameters)
{
	// copy parameters in
	UserStringParameter<false> diskSystemName;
	UserStringParameter<true> name;
	UserStringParameter<true> parameters;
	int32 changeCounter;

	status_t error
		= diskSystemName.Init(_diskSystemName, B_DISK_SYSTEM_NAME_LENGTH);
	if (error == B_OK)
		error = name.Init(_name, B_DISK_DEVICE_NAME_LENGTH);
	if (error == B_OK)
		error = parameters.Init(_parameters, B_DISK_DEVICE_MAX_PARAMETER_SIZE);
	if (error == B_OK)
		error = copy_from_user_value(changeCounter, _changeCounter);
	if (error != B_OK)
		return error;

	// get the partition
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	KPartition* partition = manager->WriteLockPartition(partitionID);
	if (partition == NULL)
		return B_ENTRY_NOT_FOUND;

	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceWriteLocker locker(partition->Device(), true);

	// check change counter
	if (changeCounter != partition->ChangeCounter())
		return B_BAD_VALUE;

	// the partition must be uninitialized
	if (partition->DiskSystem() != NULL)
		return B_BAD_VALUE;

	// load the new disk system
	KDiskSystem *diskSystem = manager->LoadDiskSystem(diskSystemName.value,
		true);
	if (diskSystem == NULL)
		return B_ENTRY_NOT_FOUND;
	DiskSystemLoader loader(diskSystem, true);

	// mark the partition busy and unlock
	if (!partition->CheckAndMarkBusy(true))
		return B_BUSY;
	locker.Unlock();

	// let the disk system initialize the partition
	error = diskSystem->Initialize(partition, name.value, parameters.value,
		DUMMY_JOB_ID);

	// re-lock and unmark busy
	locker.Lock();
	partition->UnmarkBusy(true);

	if (error != B_OK)
		return error;

	// Set the disk system. Re-check whether a disk system is already set on the
	// partition. Some disk systems just write the on-disk structures and let
	// the DDM rescan the partition, in which case the disk system will already
	// be set. In very unfortunate cases the on-disk structure of the previous
	// disk system has not been destroyed and the previous disk system has a
	// higher priority than the new one. The old disk system will thus prevail.
	// Not setting the new disk system will at least prevent that the partition
	// object gets into an inconsistent state.
	if (partition->DiskSystem() == NULL)
		partition->SetDiskSystem(diskSystem);

	// return change counter
	return copy_to_user_value(_changeCounter, partition->ChangeCounter());
}


/**
 * @brief Removes the disk system association from a partition.
 *
 * Validates the change counter (and optionally the parent change counter),
 * ensures the partition and none of its children are mounted, then calls
 * KDiskSystem::Uninitialize() to destroy the on-disk structures, followed
 * by KPartition::UninitializeContents() to reset the in-kernel state.
 *
 * @param partitionID          ID of the partition to uninitialise.
 * @param _changeCounter       User-space pointer to the partition change
 *                             counter (in/out).
 * @param parentID             ID of the parent partition, or a negative value
 *                             if there is no relevant parent.
 * @param _parentChangeCounter User-space pointer to the parent change counter
 *                             (in/out); ignored when @a parentID is negative.
 * @return B_OK on success.
 * @retval B_ENTRY_NOT_FOUND  @a partitionID is unknown.
 * @retval B_BAD_VALUE        Change counter mismatch, no disk system set, or
 *                             partition/child is mounted.
 * @retval B_BUSY             Partition is already busy.
 * @retval other              UninitializeContents() or copy error.
 */
status_t
_user_uninitialize_partition(partition_id partitionID, int32* _changeCounter,
	partition_id parentID, int32* _parentChangeCounter)
{
	// copy parameters in
	int32 changeCounter;
	int32 parentChangeCounter;
	bool haveParent = parentID >= 0;

	status_t error = copy_from_user_value(changeCounter, _changeCounter);
	if (haveParent && error == B_OK)
		error = copy_from_user_value(parentChangeCounter, _parentChangeCounter);
	if (error != B_OK)
		return error;

	// get the partition
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	KPartition* partition = manager->WriteLockPartition(partitionID);
	if (partition == NULL)
		return B_ENTRY_NOT_FOUND;

	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceWriteLocker locker(partition->Device(), true);

	// register parent
	KPartition* parent = NULL;
	if (haveParent)
		parent = manager->RegisterPartition(parentID);

	PartitionRegistrar registrar3(parent, true);

	// check change counter
	if (changeCounter != partition->ChangeCounter())
		return B_BAD_VALUE;
	if (haveParent && parentChangeCounter != parent->ChangeCounter())
		return B_BAD_VALUE;

	// the partition must be initialized
	if (partition->DiskSystem() == NULL)
		return B_BAD_VALUE;

	// check busy
	if (!partition->CheckAndMarkBusy(true))
		return B_BUSY;

	if (partition->IsMounted() || partition->IsChildMounted())
		return B_BAD_VALUE;

	KDiskSystem* diskSystem = partition->DiskSystem();

	locker.Unlock();

	// Let the disk system uninitialize the partition. This operation is not
	// mandatory. If implemented, it will destroy the on-disk structures, so
	// that the disk system cannot accidentally identify the partition later on.
	if (diskSystem != NULL)
		diskSystem->Uninitialize(partition, DUMMY_JOB_ID);

	// re-lock and uninitialize the partition object
	locker.Lock();
	error = partition->UninitializeContents(true);

	partition->UnmarkBusy(true);

	if (error != B_OK)
		return error;

	// return change counter
	error = copy_to_user_value(_changeCounter, partition->ChangeCounter());
	if (haveParent && error == B_OK)
		error = copy_to_user_value(_parentChangeCounter, parent->ChangeCounter());
	return error;
}


/**
 * @brief Creates a new child partition inside an initialised parent partition.
 *
 * Validates the change counter, marks the parent busy, delegates to
 * KDiskSystem::CreateChild(), unmarks the new child busy, and returns the
 * updated parent change counter along with the new child's ID.
 *
 * @param partitionID      ID of the parent partition.
 * @param _changeCounter   User-space pointer to the parent change counter
 *                         (in/out).
 * @param offset           Byte offset of the new partition within the parent.
 * @param size             Size of the new partition in bytes.
 * @param _type            User-space pointer to the partition type string.
 * @param _name            User-space pointer to the partition name; may be
 *                         NULL.
 * @param _parameters      User-space pointer to disk-system-specific
 *                         parameters; may be NULL.
 * @param childID          User-space pointer to receive the new child's
 *                         partition_id.
 * @param childChangeCounter (Reserved; currently unused.)
 * @return B_OK on success.
 * @retval B_ENTRY_NOT_FOUND  @a partitionID is unknown.
 * @retval B_BAD_VALUE        Change counter mismatch or no disk system set.
 * @retval B_BUSY             Parent partition is already busy.
 * @retval B_ERROR            CreateChild() succeeded but returned NULL child.
 * @retval other              CreateChild() or copy error.
 */
status_t
_user_create_child_partition(partition_id partitionID, int32* _changeCounter,
	off_t offset, off_t size, const char* _type, const char* _name,
	const char* _parameters, partition_id* childID, int32* childChangeCounter)
{
	// copy parameters in
	UserStringParameter<false> type;
	UserStringParameter<true> name;
	UserStringParameter<true> parameters;
	int32 changeCounter;

	status_t error = type.Init(_type, B_DISK_DEVICE_TYPE_LENGTH);
	if (error == B_OK)
		error = name.Init(_name, B_DISK_DEVICE_NAME_LENGTH);
	if (error == B_OK)
		error = parameters.Init(_parameters, B_DISK_DEVICE_MAX_PARAMETER_SIZE);
	if (error == B_OK)
		error = copy_from_user_value(changeCounter, _changeCounter);
	if (error != B_OK)
		return error;

	// get the partition
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	KPartition* partition = manager->WriteLockPartition(partitionID);
	if (partition == NULL)
		return B_ENTRY_NOT_FOUND;

	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceWriteLocker locker(partition->Device(), true);

	// check change counter
	if (changeCounter != partition->ChangeCounter())
		return B_BAD_VALUE;

	// the partition must be initialized
	KDiskSystem* diskSystem = partition->DiskSystem();
	if (diskSystem == NULL)
		return B_BAD_VALUE;

	// mark the partition busy and unlock
	if (!partition->CheckAndMarkBusy(false))
		return B_BUSY;
	locker.Unlock();

	// create the child
	KPartition *child = NULL;
	error = diskSystem->CreateChild(partition, offset, size, type.value,
		name.value, parameters.value, DUMMY_JOB_ID, &child, -1);

	// re-lock and unmark busy
	locker.Lock();
	partition->UnmarkBusy(false);

	if (error != B_OK)
		return error;

	if (child == NULL)
		return B_ERROR;

	child->UnmarkBusy(true);

	// return change counter and child ID
	error = copy_to_user_value(_changeCounter, partition->ChangeCounter());
	if (error == B_OK)
		error = copy_to_user_value(childID, child->ID());
	return error;
}


/**
 * @brief Deletes a child partition from an initialised parent partition.
 *
 * Validates both change counters, marks the parent and child busy, delegates
 * to KDiskSystem::DeleteChild(), then returns the updated parent change
 * counter.
 *
 * @param partitionID        ID of the parent partition.
 * @param _changeCounter     User-space pointer to the parent change counter
 *                           (in/out).
 * @param childID            ID of the child partition to delete.
 * @param childChangeCounter The expected child change counter value (passed
 *                           by value; validated before deletion).
 * @return B_OK on success.
 * @retval B_ENTRY_NOT_FOUND  @a partitionID or @a childID is unknown.
 * @retval B_BAD_VALUE        Change counter mismatch, wrong parent, or no
 *                            disk system set.
 * @retval B_BUSY             Parent or child is already busy.
 * @retval other              DeleteChild() or copy error.
 */
status_t
_user_delete_child_partition(partition_id partitionID, int32* _changeCounter,
	partition_id childID, int32 childChangeCounter)
{
	// copy parameters in
	int32 changeCounter;

	status_t error;
	if ((error = copy_from_user_value(changeCounter, _changeCounter)) != B_OK)
		return error;

	// get the partition
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	KPartition* partition = manager->WriteLockPartition(partitionID);
	if (partition == NULL)
		return B_ENTRY_NOT_FOUND;

	PartitionRegistrar registrar1(partition, true);
	PartitionRegistrar registrar2(partition->Device(), true);
	DeviceWriteLocker locker(partition->Device(), true);

	// register child
	KPartition* child = manager->RegisterPartition(childID);
	if (child == NULL)
		return B_ENTRY_NOT_FOUND;

	PartitionRegistrar registrar3(child, true);

	// check change counters
	if (changeCounter != partition->ChangeCounter()
		|| childChangeCounter != child->ChangeCounter()) {
		return B_BAD_VALUE;
	}

	// the partition must be initialized
	KDiskSystem* diskSystem = partition->DiskSystem();
	if (diskSystem == NULL)
		return B_BAD_VALUE;

	// child must indeed be a child of partition
	if (child->Parent() != partition)
		return B_BAD_VALUE;

	// mark the partition and child busy and unlock
	if (partition->IsBusy() || !child->CheckAndMarkBusy(true))
		return B_BUSY;
	partition->SetBusy(true);
	locker.Unlock();

	// delete the child
	error = diskSystem->DeleteChild(child, DUMMY_JOB_ID);

	// re-lock and unmark busy
	locker.Lock();
	partition->SetBusy(false);
	child->UnmarkBusy(true);

	if (error != B_OK)
		return error;

	// return change counter
	return copy_to_user_value(_changeCounter, partition->ChangeCounter());
}


/**
 * @brief Registers a userland listener for disk device change events.
 *
 * The calling thread will receive notifications on @a port (with @a token
 * as the reply token) whenever one of the events in @a eventMask occurs.
 * Replaces any existing listener registered for the same (port, token) pair.
 *
 * @param eventMask  Bitmask of disk-device events to watch for.
 * @param port       The port to which notifications are delivered.
 * @param token      An application-defined token included in each message.
 * @return B_OK on success.
 * @retval other  UpdateUserListener() error.
 */
status_t
_user_start_watching_disks(uint32 eventMask, port_id port, int32 token)
{
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	return manager->Notifications().UpdateUserListener(eventMask, port, token);
}


/**
 * @brief Unregisters a userland listener for disk device change events.
 *
 * Removes all listeners associated with the given (port, token) pair so
 * that no further notifications are delivered to that target.
 *
 * @param port   The port that was supplied to _user_start_watching_disks().
 * @param token  The token that was supplied to _user_start_watching_disks().
 * @return B_OK on success.
 * @retval other  RemoveUserListeners() error.
 */
status_t
_user_stop_watching_disks(port_id port, int32 token)
{
	KDiskDeviceManager* manager = KDiskDeviceManager::Default();
	return manager->Notifications().RemoveUserListeners(port, token);
}
