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
 *   Copyright 2003-2011, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold, ingo_weinhold@gmx.de
 */

/**
 * @file KFileSystem.cpp
 * @brief Kernel representation of a mountable file system add-on (e.g. BFS, FAT).
 *
 * KFileSystem wraps a file_system_module_info add-on and exposes it through
 * the disk device manager. Tracks loaded/unloaded state and delegates
 * mounting and unmounting to the underlying module.
 *
 * @see KDiskSystem.cpp, KPartitioningSystem.cpp
 */

#include "KFileSystem.h"

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <fs_interface.h>

#include "ddm_modules.h"
#include "KDiskDeviceUtils.h"
#include "KPartition.h"


/**
 * @brief Constructs a KFileSystem with the given module name.
 *
 * @param name  The full module name used to load the file system add-on
 *              (e.g. "file_systems/bfs/v1").
 *
 * @note fModule is initialised to NULL; the actual module is loaded lazily
 *       via Load() / Init().
 */
// constructor
KFileSystem::KFileSystem(const char *name)
	: KDiskSystem(name),
	fModule(NULL)
{
}


/**
 * @brief Destroys the KFileSystem instance.
 *
 * @note The underlying module must already have been unloaded (via Unload())
 *       before the destructor runs.  The base-class destructor handles
 *       freeing the name strings.
 */
// destructor
KFileSystem::~KFileSystem()
{
}


/**
 * @brief Initialises the file system object by loading the add-on once to
 *        read its metadata, then immediately unloading it.
 *
 * Calls KDiskSystem::Init(), loads the module, reads short_name,
 * pretty_name and flags from the module info structure, then calls Unload()
 * so the module is not kept resident unnecessarily.
 *
 * @return B_OK on success; a Haiku error code on failure (e.g. B_NO_MEMORY
 *         if a name string allocation fails, or an add-on load error).
 */
// Init
status_t
KFileSystem::Init()
{
	status_t error = KDiskSystem::Init();
	if (error != B_OK)
		return error;
	error = Load();
	if (error != B_OK)
		return error;
	error = SetShortName(fModule->short_name);
	if (error == B_OK)
		error = SetPrettyName(fModule->pretty_name);

	SetFlags(fModule->flags | B_DISK_SYSTEM_IS_FILE_SYSTEM);
	Unload();
	return error;
}


/**
 * @brief Asks the underlying module whether it recognises the partition.
 *
 * Opens the partition device read-only, delegates to the module's
 * identify_partition hook, then closes the device.
 *
 * @param partition  The partition to probe.  Must not be NULL.
 * @param cookie     Output parameter; receives an opaque identification
 *                   cookie that must later be passed to FreeIdentifyCookie().
 *                   Must not be NULL.
 *
 * @return A priority score in the range [0, 1] if the partition is
 *         recognised; -1 if the partition is not recognised or a
 *         precondition is not met (NULL arguments, missing hook, or a
 *         failure to open the device).
 */
// Identify
float
KFileSystem::Identify(KPartition *partition, void **cookie)
{
	if (!partition || !cookie || !fModule || !fModule->identify_partition)
		return -1;
	int fd = -1;
	if (partition->Open(O_RDONLY, &fd) != B_OK)
		return -1;
	float result = fModule->identify_partition(fd, partition->PartitionData(),
		cookie);
	close(fd);
	return result;
}


/**
 * @brief Scans the partition and populates its content information.
 *
 * Opens the partition device read-only and calls the module's
 * scan_partition hook.  The @p cookie must have been obtained from a
 * previous successful call to Identify().
 *
 * @param partition  The partition to scan.  Must not be NULL.
 * @param cookie     The opaque cookie previously returned by Identify().
 *
 * @retval B_OK      Scan succeeded.
 * @retval B_ERROR   A precondition was not met (NULL pointer or missing hook).
 * @return Any other error code propagated from partition->Open() or from
 *         the module's scan_partition hook.
 */
// Scan
status_t
KFileSystem::Scan(KPartition *partition, void *cookie)
{
	if (!partition || !fModule || !fModule->scan_partition)
		return B_ERROR;
	int fd = -1;
	status_t result = partition->Open(O_RDONLY, &fd);
	if (result != B_OK)
		return result;
	result = fModule->scan_partition(fd, partition->PartitionData(), cookie);
	close(fd);
	return result;
}


/**
 * @brief Releases the identification cookie previously allocated by Identify().
 *
 * Delegates to the module's free_identify_partition_cookie hook.
 * Safe to call with a NULL @p cookie if the module hook handles that case.
 *
 * @param partition  The partition for which Identify() was called.
 * @param cookie     The opaque cookie to free.  Ownership is transferred to
 *                   the module.
 *
 * @note If any precondition is not met (NULL partition, missing module, or
 *       missing hook) the function returns silently without freeing anything.
 */
// FreeIdentifyCookie
void
KFileSystem::FreeIdentifyCookie(KPartition *partition, void *cookie)
{
	if (!partition || !fModule || !fModule->free_identify_partition_cookie)
		return;
	fModule->free_identify_partition_cookie(partition->PartitionData(), cookie);
}


/**
 * @brief Releases the content cookie stored inside the partition.
 *
 * Delegates to the module's free_partition_content_cookie hook.
 *
 * @param partition  The partition whose content cookie is to be freed.
 *
 * @note If any precondition is not met (NULL partition, missing module, or
 *       missing hook) the function returns silently without freeing anything.
 */
// FreeContentCookie
void
KFileSystem::FreeContentCookie(KPartition *partition)
{
	if (!partition || !fModule || !fModule->free_partition_content_cookie)
		return;
	fModule->free_partition_content_cookie(partition->PartitionData());
}


/**
 * @brief Defragments the file system on the given partition.
 *
 * @param partition  The partition whose file system is to be defragmented.
 * @param job        The disk job identifier for progress tracking.
 *
 * @retval B_ERROR  Not yet implemented.
 *
 * @note This function is a stub and will always return B_ERROR until
 *       a concrete implementation is provided.
 */
// Defragment
status_t
KFileSystem::Defragment(KPartition* partition, disk_job_id job)
{
	// to be implemented
	return B_ERROR;
}


/**
 * @brief Checks or repairs the file system on the given partition.
 *
 * @param partition  The partition whose file system is to be checked or
 *                   repaired.
 * @param checkOnly  If true, only perform a read-only integrity check
 *                   without writing any corrections.
 * @param job        The disk job identifier for progress tracking.
 *
 * @retval B_ERROR  Not yet implemented.
 *
 * @note This function is a stub and will always return B_ERROR until
 *       a concrete implementation is provided.
 */
// Repair
status_t
KFileSystem::Repair(KPartition* partition, bool checkOnly, disk_job_id job)
{
	// to be implemented
	return B_ERROR;
}


/**
 * @brief Resizes the file system on the given partition to a new byte size.
 *
 * Opens the partition device read/write and calls the module's resize hook.
 *
 * @param partition  The partition to resize.  Must not be NULL.
 * @param size       The desired new size in bytes.
 * @param job        The disk job identifier for progress tracking.
 *
 * @retval B_OK           Resize succeeded.
 * @retval B_ERROR        @p partition or fModule is NULL.
 * @retval B_NOT_SUPPORTED The module does not provide a resize hook.
 * @return Any error code propagated from partition->Open() or from the
 *         module's resize hook.
 */
// Resize
status_t
KFileSystem::Resize(KPartition* partition, off_t size, disk_job_id job)
{
	if (partition == NULL || fModule == NULL)
		return B_ERROR;
	if (fModule->resize == NULL)
		return B_NOT_SUPPORTED;

	int fd = -1;
	status_t result = partition->Open(O_RDWR, &fd);
	if (result != B_OK)
		return result;

	result = fModule->resize(fd, partition->ID(), size, job);

	close(fd);
	return result;
}


/**
 * @brief Moves the file system content to a new offset within the device.
 *
 * @param partition  The partition to move.
 * @param offset     The new byte offset on the device.
 * @param job        The disk job identifier for progress tracking.
 *
 * @retval B_ERROR  Not yet implemented.
 *
 * @note This function is a stub and will always return B_ERROR until
 *       a concrete implementation is provided.
 */
// Move
status_t
KFileSystem::Move(KPartition* partition, off_t offset, disk_job_id job)
{
	// to be implemented
	return B_ERROR;
}


/**
 * @brief Sets the volume name of the file system on the given partition.
 *
 * Opens the partition device read/write and delegates to the module's
 * set_content_name hook.
 *
 * @param partition  The partition whose content name is to be changed.
 *                   Must not be NULL.
 * @param name       The new volume name string.
 * @param job        The disk job identifier for progress tracking.
 *
 * @retval B_OK           Name change succeeded.
 * @retval B_BAD_VALUE    @p partition or fModule is NULL.
 * @retval B_NOT_SUPPORTED The module does not provide a set_content_name hook.
 * @return Any error code propagated from partition->Open() or from the
 *         module's set_content_name hook.
 */
// SetContentName
status_t
KFileSystem::SetContentName(KPartition* partition, const char* name,
	disk_job_id job)
{
	// check parameters
	if (!partition || !fModule)
		return B_BAD_VALUE;
	if (!fModule->set_content_name)
		return B_NOT_SUPPORTED;

	// open partition device
	int fd = -1;
	status_t result = partition->Open(O_RDWR, &fd);
	if (result != B_OK)
		return result;

	// let the module do its job
	result = fModule->set_content_name(fd, partition->ID(), name, job);

	// cleanup and return
	close(fd);
	return result;
}


/**
 * @brief Updates the content parameters of the file system on a partition.
 *
 * Opens the partition device read/write and delegates to the module's
 * set_content_parameters hook.
 *
 * @param partition   The partition whose content parameters are to be changed.
 *                    Must not be NULL.
 * @param parameters  A null-terminated string encoding the new parameters.
 * @param job         The disk job identifier for progress tracking.
 *
 * @retval B_OK           Parameter update succeeded.
 * @retval B_BAD_VALUE    @p partition or fModule is NULL.
 * @retval B_NOT_SUPPORTED The module does not provide a
 *                         set_content_parameters hook.
 * @return Any error code propagated from partition->Open() or from the
 *         module's set_content_parameters hook.
 */
// SetContentParameters
status_t
KFileSystem::SetContentParameters(KPartition* partition,
	const char* parameters, disk_job_id job)
{
	// check parameters
	if (!partition || !fModule)
		return B_BAD_VALUE;
	if (!fModule->set_content_parameters)
		return B_NOT_SUPPORTED;

	// open partition device
	int fd = -1;
	status_t result = partition->Open(O_RDWR, &fd);
	if (result != B_OK)
		return result;

	// let the module do its job
	result = fModule->set_content_parameters(fd, partition->ID(), parameters,
		job);

	// cleanup and return
	close(fd);
	return result;
}


/**
 * @brief Formats a partition with this file system.
 *
 * Opens the partition device read/write and calls the module's initialize
 * hook to write the initial file system structures.
 *
 * @param partition   The partition to format.  Must not be NULL.
 * @param name        The initial volume name, or NULL for an unnamed volume.
 * @param parameters  A null-terminated string encoding optional creation
 *                    parameters, or NULL.
 * @param job         The disk job identifier for progress tracking.
 *
 * @retval B_OK           Formatting succeeded.
 * @retval B_BAD_VALUE    @p partition or fModule is NULL.
 * @retval B_NOT_SUPPORTED The module does not provide an initialize hook.
 * @return Any error code propagated from partition->Open() or from the
 *         module's initialize hook.
 */
// Initialize
status_t
KFileSystem::Initialize(KPartition* partition, const char* name,
	const char* parameters, disk_job_id job)
{
	// check parameters
	if (!partition || !fModule)
		return B_BAD_VALUE;
	if (!fModule->initialize)
		return B_NOT_SUPPORTED;

	// open partition device
	int fd = -1;
	status_t result = partition->Open(O_RDWR, &fd);
	if (result != B_OK)
		return result;

	// let the module do its job
	result = fModule->initialize(fd, partition->ID(), name, parameters,
		partition->Size(), job);

	// cleanup and return
	close(fd);
	return result;
}


/**
 * @brief Removes all file system structures from the partition.
 *
 * Opens the partition device read/write and calls the module's uninitialize
 * hook to erase the on-disk file system metadata.
 *
 * @param partition  The partition to uninitialize.  Must not be NULL.
 * @param job        The disk job identifier for progress tracking.
 *
 * @retval B_OK           Uninitialization succeeded.
 * @retval B_BAD_VALUE    @p partition or fModule is NULL.
 * @retval B_NOT_SUPPORTED The module does not provide an uninitialize hook.
 * @return Any error code propagated from partition->Open() or from the
 *         module's uninitialize hook.
 */
status_t
KFileSystem::Uninitialize(KPartition* partition, disk_job_id job)
{
	// check parameters
	if (partition == NULL || fModule == NULL)
		return B_BAD_VALUE;
	if (fModule->uninitialize == NULL)
		return B_NOT_SUPPORTED;

	// open partition device
	int fd = -1;
	status_t result = partition->Open(O_RDWR, &fd);
	if (result != B_OK)
		return result;

	// let the module do its job
	result = fModule->uninitialize(fd, partition->ID(), partition->Size(),
		partition->BlockSize(), job);

	// cleanup and return
	close(fd);
	return result;
}


/**
 * @brief Loads the underlying file_system_module_info add-on into memory.
 *
 * Uses get_module() to look up and load the module identified by Name().
 * If the module is already loaded (fModule != NULL) the function returns
 * immediately with B_OK.
 *
 * @retval B_OK   Module loaded successfully; fModule now points to the
 *                module info structure.
 * @return Any error code returned by get_module() on failure.
 *
 * @note Called by KDiskSystem::Load() the first time the reference count
 *       transitions from 0 to 1.
 */
// LoadModule
status_t
KFileSystem::LoadModule()
{
	if (fModule)		// shouldn't happen
		return B_OK;

	return get_module(Name(), (module_info **)&fModule);
}


/**
 * @brief Unloads the underlying file_system_module_info add-on.
 *
 * Calls put_module() to decrement the kernel module reference count and,
 * if it reaches zero, unload the add-on.  Sets fModule to NULL afterwards.
 *
 * @note Called by KDiskSystem::Unload() when the last user releases the
 *       disk system.  Safe to call when fModule is already NULL.
 */
// UnloadModule
void
KFileSystem::UnloadModule()
{
	if (fModule) {
		put_module(fModule->info.name);
		fModule = NULL;
	}
}
