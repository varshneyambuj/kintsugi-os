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
 *       Ingo Weinhold, bonefish@cs.tu-berlin.de
 *       Lubos Kulic, lubos@radical.ed
 */

/**
 * @file KPartitioningSystem.cpp
 * @brief Kernel representation of a partitioning system add-on (e.g. Intel, GPT).
 *
 * KPartitioningSystem wraps a partition_module_info add-on and implements
 * operations like Identify, Scan, CreateChild, DeleteChild, and Repair that
 * the disk device manager invokes when working with partition tables.
 *
 * @see KDiskSystem.cpp, KDiskDeviceManager.cpp
 */

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <ddm_modules.h>
#include <KDiskDevice.h>
//#include <KDiskDeviceJob.h>
#include <KDiskDeviceManager.h>
#include <KDiskDeviceUtils.h>
#include <KPartition.h>
#include <KPartitioningSystem.h>


/**
 * @brief Constructs a KPartitioningSystem with the given module name.
 *
 * @param name  The full module name used to load the partitioning system
 *              add-on (e.g. "partitioning_systems/intel/v1").
 *
 * @note fModule is initialised to NULL; the actual module is loaded lazily
 *       via Load() / Init().
 */
// constructor
KPartitioningSystem::KPartitioningSystem(const char *name)
	: KDiskSystem(name),
	  fModule(NULL)
{
}


/**
 * @brief Destroys the KPartitioningSystem instance.
 *
 * @note The underlying module must already have been unloaded (via Unload())
 *       before the destructor runs.  Base-class destructor handles freeing
 *       the name strings.
 */
// destructor
KPartitioningSystem::~KPartitioningSystem()
{
}


/**
 * @brief Initialises the partitioning system object by loading the add-on
 *        once to read its metadata, then immediately unloading it.
 *
 * Calls KDiskSystem::Init(), loads the module, reads short_name,
 * pretty_name and flags from the module info structure (clearing the
 * B_DISK_SYSTEM_IS_FILE_SYSTEM bit), then calls Unload().
 *
 * @retval B_OK  Initialisation succeeded.
 * @return Any error code from KDiskSystem::Init(), Load(), SetShortName(),
 *         or SetPrettyName().
 */
// Init
status_t
KPartitioningSystem::Init()
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

	SetFlags(fModule->flags & ~(uint32)B_DISK_SYSTEM_IS_FILE_SYSTEM);
	Unload();
	return error;
}


/**
 * @brief Asks the underlying module whether it recognises the partition table.
 *
 * Opens the partition device read-only, verifies that the block size is
 * non-zero, then delegates to the module's identify_partition hook.
 *
 * @param partition  The partition to probe.  Must not be NULL.
 * @param cookie     Output parameter; receives an opaque identification
 *                   cookie that must later be passed to FreeIdentifyCookie().
 *                   Must not be NULL.
 *
 * @return A priority score in the range [0, 1] if the partition is
 *         recognised; -1 if not recognised or a precondition is not met
 *         (NULL arguments, zero block size, missing hook, or open failure).
 */
// Identify
//! Try to identify a given partition
float
KPartitioningSystem::Identify(KPartition *partition, void **cookie)
{
	if (!partition || !cookie || !fModule || !fModule->identify_partition)
		return -1;
	int fd = -1;
	if (partition->Open(O_RDONLY, &fd) != B_OK)
		return -1;
	if (partition->BlockSize() == 0) {
		close(fd);
		return -1;
	}

	float result = fModule->identify_partition(fd, partition->PartitionData(),
		cookie);
	close(fd);
	return result;
}


/**
 * @brief Scans the partition table and populates child partition information.
 *
 * Opens the partition device read-only and calls the module's
 * scan_partition hook.  The @p cookie must have been obtained from a
 * previous successful call to Identify().
 *
 * @param partition  The partition whose table is to be scanned.
 *                   Must not be NULL.
 * @param cookie     The opaque cookie returned by Identify().
 *
 * @retval B_OK    Scan succeeded.
 * @retval B_ERROR A precondition was not met (NULL pointer or missing hook).
 * @return Any error code propagated from partition->Open() or from the
 *         module's scan_partition hook.
 */
// Scan
//! Scan the partition
status_t
KPartitioningSystem::Scan(KPartition *partition, void *cookie)
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
KPartitioningSystem::FreeIdentifyCookie(KPartition *partition, void *cookie)
{
	if (!partition || !fModule || !fModule->free_identify_partition_cookie)
		return;
	fModule->free_identify_partition_cookie(partition->PartitionData(),
		cookie);
}


/**
 * @brief Releases the per-partition cookie and clears it on the partition.
 *
 * Delegates to the module's free_partition_cookie hook only when @p partition
 * is owned by this disk system (ParentDiskSystem() == this).  Clears the
 * cookie pointer on the partition after freeing.
 *
 * @param partition  The partition whose private cookie is to be freed.
 *
 * @note If any precondition is not met (NULL partition, missing module,
 *       missing hook, or wrong owning disk system) the function returns
 *       silently without freeing anything.
 */
// FreeCookie
void
KPartitioningSystem::FreeCookie(KPartition *partition)
{
	if (!partition || !fModule || !fModule->free_partition_cookie
		|| partition->ParentDiskSystem() != this) {
		return;
	}
	fModule->free_partition_cookie(partition->PartitionData());
	partition->SetCookie(NULL);
}


/**
 * @brief Releases the content cookie stored on the partition and clears it.
 *
 * Delegates to the module's free_partition_content_cookie hook only when
 * @p partition's content is managed by this disk system (DiskSystem() == this).
 * Clears the content cookie pointer on the partition after freeing.
 *
 * @param partition  The partition whose content cookie is to be freed.
 *
 * @note If any precondition is not met (NULL partition, missing module,
 *       missing hook, or wrong owning disk system) the function returns
 *       silently without freeing anything.
 */
// FreeContentCookie
void
KPartitioningSystem::FreeContentCookie(KPartition *partition)
{
	if (!partition || !fModule || !fModule->free_partition_content_cookie
		|| partition->DiskSystem() != this) {
		return;
	}
	fModule->free_partition_content_cookie(partition->PartitionData());
	partition->SetContentCookie(NULL);
}


/**
 * @brief Checks or repairs the partition table.
 *
 * @param partition  The partition whose table is to be checked or repaired.
 * @param checkOnly  If true, perform a read-only integrity check only.
 * @param job        The disk job identifier for progress tracking.
 *
 * @retval B_ERROR  Not yet implemented.
 *
 * @note This function is a stub and will always return B_ERROR until
 *       a concrete implementation is provided.
 */
// Repair
//! Repairs a partition
status_t
KPartitioningSystem::Repair(KPartition* partition, bool checkOnly,
	disk_job_id job)
{
	// to be implemented
	return B_ERROR;
}


/**
 * @brief Resizes a partition entry in the partition table.
 *
 * Opens the partition device read/write and calls the module's resize hook.
 *
 * @param partition  The partition to resize.  Must not be NULL; size must
 *                   be >= 0.
 * @param size       The desired new size in bytes.
 * @param job        The disk job identifier for progress tracking.
 *
 * @retval B_OK           Resize succeeded.
 * @retval B_BAD_VALUE    @p partition is NULL, @p size is negative, or
 *                        fModule is NULL.
 * @retval B_NOT_SUPPORTED The module does not provide a resize hook.
 * @return Any error code propagated from partition->Open() or from the
 *         module's resize hook.
 */
// Resize
//! Resizes a partition
status_t
KPartitioningSystem::Resize(KPartition* partition, off_t size, disk_job_id job)
{
	// check parameters
	if (!partition || size < 0 || !fModule)
		return B_BAD_VALUE;
	if (!fModule->resize)
		return B_NOT_SUPPORTED;

	// open partition device
	int fd = -1;
	status_t result = partition->Open(O_RDWR, &fd);
	if (result != B_OK)
		return result;

	// let the module do its job
	result = fModule->resize(fd, partition->ID(), size, job);

	// cleanup and return
	close(fd);
	return result;
}


/**
 * @brief Resizes a child partition entry within its parent's partition table.
 *
 * Opens the parent partition device read/write and calls the module's
 * resize_child hook.
 *
 * @param child  The child partition to resize.  Must not be NULL and must
 *               have a valid parent; @p size must be >= 0.
 * @param size   The desired new size in bytes.
 * @param job    The disk job identifier for progress tracking.
 *
 * @retval B_OK           Resize succeeded.
 * @retval B_BAD_VALUE    @p child or its parent is NULL, @p size is negative,
 *                        or fModule is NULL.
 * @retval B_NOT_SUPPORTED The module does not provide a resize_child hook.
 * @return Any error code propagated from the parent's Open() call or from
 *         the module's resize_child hook.
 */
// ResizeChild
//! Resizes child of a partition
status_t
KPartitioningSystem::ResizeChild(KPartition* child, off_t size, disk_job_id job)
{
	// check parameters
	if (!child || !child->Parent() || size < 0 || !fModule)
		return B_BAD_VALUE;
	if (!fModule->resize_child)
		return B_NOT_SUPPORTED;

	// open partition device
	int fd = -1;
	status_t result = child->Parent()->Open(O_RDWR, &fd);
	if (result != B_OK)
		return result;

	// let the module do its job
	result = fModule->resize_child(fd, child->ID(), size, job);

	// cleanup and return
	close(fd);
	return result;
}


/**
 * @brief Moves a partition entry to a new offset within the device.
 *
 * Opens the partition device read/write and calls the module's move hook.
 *
 * @param partition  The partition to move.  Must not be NULL.
 * @param offset     The new byte offset on the device.
 * @param job        The disk job identifier for progress tracking.
 *
 * @retval B_OK           Move succeeded.
 * @retval B_BAD_VALUE    @p partition is NULL.
 * @retval B_NOT_SUPPORTED The module does not provide a move hook.
 * @return Any error code propagated from partition->Open() or from the
 *         module's move hook.
 */
// Move
//! Moves a partition
status_t
KPartitioningSystem::Move(KPartition* partition, off_t offset, disk_job_id job)
{
	// check parameters
	if (!partition)
		return B_BAD_VALUE;
	if (!fModule->move)
		return B_NOT_SUPPORTED;

	// open partition device
	int fd = -1;
	status_t result = partition->Open(O_RDWR, &fd);
	if (result != B_OK)
		return result;

	// let the module do its job
	result = fModule->move(fd, partition->ID(), offset, job);

	// cleanup and return
	close(fd);
	return result;
}


/**
 * @brief Moves a child partition entry to a new offset within the parent.
 *
 * Opens the parent partition device read/write and calls the module's
 * move_child hook.
 *
 * @param child   The child partition to move.  Must not be NULL and must
 *                have a valid parent.
 * @param offset  The new byte offset within the parent device.
 * @param job     The disk job identifier for progress tracking.
 *
 * @retval B_OK           Move succeeded.
 * @retval B_BAD_VALUE    @p child, its parent, or fModule is NULL.
 * @retval B_NOT_SUPPORTED The module does not provide a move_child hook.
 * @return Any error code propagated from the parent's Open() call or from
 *         the module's move_child hook.
 */
// MoveChild
//! Moves child of a partition
status_t
KPartitioningSystem::MoveChild(KPartition* child, off_t offset, disk_job_id job)
{
	// check parameters
	if (!child || !child->Parent() || !fModule)
		return B_BAD_VALUE;
	if (!fModule->move_child)
		return B_NOT_SUPPORTED;

	// open partition device
	int fd = -1;
	status_t result = child->Parent()->Open(O_RDWR, &fd);
	if (result != B_OK)
		return result;

	// let the module do its job
	result = fModule->move_child(fd, child->Parent()->ID(), child->ID(), offset,
		job);

	// cleanup and return
	close(fd);
	return result;
}


/**
 * @brief Sets the name of a child partition entry in the partition table.
 *
 * Opens the parent partition device read/write and delegates to the module's
 * set_name hook.
 *
 * @param child  The child partition whose name is to be changed.  Must not
 *               be NULL and must have a valid parent.
 * @param name   The new name string.
 * @param job    The disk job identifier for progress tracking.
 *
 * @retval B_OK           Name change succeeded.
 * @retval B_BAD_VALUE    @p child, its parent, or fModule is NULL.
 * @retval B_NOT_SUPPORTED The module does not provide a set_name hook.
 * @return Any error code propagated from the parent's Open() call or from
 *         the module's set_name hook.
 */
// SetName
//! Sets name of a partition
status_t
KPartitioningSystem::SetName(KPartition* child, const char* name,
	disk_job_id job)
{
	// check parameters
	if (!child || !child->Parent() || !fModule)
		return B_BAD_VALUE;
	if (!fModule->set_name)
		return B_NOT_SUPPORTED;

	// open partition device
	int fd = -1;
	status_t result = child->Parent()->Open(O_RDWR, &fd);
	if (result != B_OK)
		return result;

	// let the module do its job
	result = fModule->set_name(fd, child->ID(), name, job);
// TODO: Change hook interface!

	// cleanup and return
	close(fd);
	return result;
}


/**
 * @brief Sets the content name stored in the partition table entry.
 *
 * Opens the partition device read/write and delegates to the module's
 * set_content_name hook.
 *
 * @param partition  The partition whose content name is to be changed.
 *                   Must not be NULL.
 * @param name       The new content name string.
 * @param job        The disk job identifier for progress tracking.
 *
 * @retval B_OK           Name change succeeded.
 * @retval B_BAD_VALUE    @p partition or fModule is NULL.
 * @retval B_NOT_SUPPORTED The module does not provide a set_content_name hook.
 * @return Any error code propagated from partition->Open() or from the
 *         module's set_content_name hook.
 */
// SetContentName
//! Sets name of the content of a partition
status_t
KPartitioningSystem::SetContentName(KPartition* partition, const char* name,
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
 * @brief Sets the type string of a child partition entry.
 *
 * Opens the parent partition device read/write and delegates to the module's
 * set_type hook.
 *
 * @param child  The child partition whose type is to be changed.  Must not
 *               be NULL, must have a valid parent, and @p type must not be
 *               NULL.
 * @param type   The new partition type string (e.g. a GUID or type constant).
 * @param job    The disk job identifier for progress tracking.
 *
 * @retval B_OK           Type change succeeded.
 * @retval B_BAD_VALUE    @p child, its parent, @p type, or fModule is NULL.
 * @retval B_NOT_SUPPORTED The module does not provide a set_type hook.
 * @return Any error code propagated from the parent's Open() call or from
 *         the module's set_type hook.
 */
// SetType
//! Sets type of a partition
status_t
KPartitioningSystem::SetType(KPartition* child, const char* type,
	disk_job_id job)
{
	// check parameters
	if (!child || !child->Parent() || !type || !fModule)
		return B_BAD_VALUE;
	if (!fModule->set_type)
		return B_NOT_SUPPORTED;

	// open partition device
	int fd = -1;
	status_t result = child->Parent()->Open(O_RDWR, &fd);
	if (result != B_OK)
		return result;

	// let the module do its job
	result = fModule->set_type(fd, child->ID(), type, job);
// TODO: Change hook interface!

	// cleanup and return
	close(fd);
	return result;
}


/**
 * @brief Updates the parameters stored for a child partition entry.
 *
 * Opens the parent partition device read/write and delegates to the module's
 * set_parameters hook.
 *
 * @param child       The child partition whose parameters are to be changed.
 *                    Must not be NULL and must have a valid parent.
 * @param parameters  The new parameter string.
 * @param job         The disk job identifier for progress tracking.
 *
 * @retval B_OK           Parameter update succeeded.
 * @retval B_BAD_VALUE    @p child, its parent, or fModule is NULL.
 * @retval B_NOT_SUPPORTED The module does not provide a set_parameters hook.
 * @return Any error code propagated from the parent's Open() call or from
 *         the module's set_parameters hook.
 */
// SetParameters
//! Sets parameters of a partition
status_t
KPartitioningSystem::SetParameters(KPartition* child, const char* parameters,
	disk_job_id job)
{
	// check parameters
	if (!child || !child->Parent() || !fModule)
		return B_BAD_VALUE;
	if (!fModule->set_parameters)
		return B_NOT_SUPPORTED;

	// open partition device
	int fd = -1;
	status_t result = child->Parent()->Open(O_RDWR, &fd);
	if (result != B_OK)
		return result;

	// let the module do its job
	result = fModule->set_parameters(fd, child->ID(), parameters, job);
// TODO: Change hook interface!

	// cleanup and return
	close(fd);
	return result;
}


/**
 * @brief Updates the content parameters of a partition table entry.
 *
 * Opens the partition device read/write and delegates to the module's
 * set_content_parameters hook.
 *
 * @param partition   The partition whose content parameters are to be changed.
 *                    Must not be NULL.
 * @param parameters  The new parameter string.
 * @param job         The disk job identifier for progress tracking.
 *
 * @retval B_OK           Parameter update succeeded.
 * @retval B_BAD_VALUE    @p partition or fModule is NULL.
 * @retval B_NOT_SUPPORTED The module does not provide a set_content_parameters
 *                         hook.
 * @return Any error code propagated from partition->Open() or from the
 *         module's set_content_parameters hook.
 */
// SetContentParameters
//! Sets parameters of the content of a partition
status_t
KPartitioningSystem::SetContentParameters(KPartition* partition,
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
 * @brief Writes an initial partition table to the partition.
 *
 * Opens the partition device read/write and calls the module's initialize
 * hook to write the on-disk partition table structures.
 *
 * @param partition   The partition to initialise.  Must not be NULL.
 * @param name        An optional name for the partition table, or NULL.
 * @param parameters  Optional creation parameter string, or NULL.
 * @param job         The disk job identifier for progress tracking.
 *
 * @retval B_OK           Initialization succeeded.
 * @retval B_BAD_VALUE    @p partition or fModule is NULL.
 * @retval B_NOT_SUPPORTED The module does not provide an initialize hook.
 * @return Any error code propagated from partition->Open() or from the
 *         module's initialize hook.
 */
// Initialize
//! Initializes a partition with this partitioning system
status_t
KPartitioningSystem::Initialize(KPartition* partition, const char* name,
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
 * @brief Removes all partitioning system structures from the partition.
 *
 * Opens the partition device read/write and calls the module's uninitialize
 * hook to erase the on-disk partition table metadata.
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
KPartitioningSystem::Uninitialize(KPartition* partition, disk_job_id job)
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
 * @brief Creates a new child partition entry in the parent's partition table.
 *
 * Opens the parent partition device read/write, calls the module's
 * create_child hook, then looks up the newly created child partition in the
 * disk device manager by the returned child ID.
 *
 * @param partition   The parent partition.  Must not be NULL.
 * @param offset      Byte offset of the new child within the parent.
 * @param size        Size in bytes of the new child partition.
 * @param type        Partition type string (e.g. GUID).  Must not be NULL.
 * @param name        Partition name, or NULL for an unnamed partition.
 * @param parameters  Creation parameter string.  Must not be NULL.
 * @param job         The disk job identifier for progress tracking.
 * @param child       Output pointer; receives the newly created KPartition
 *                    object.  Must not be NULL.
 * @param childID     Desired partition_id for the new child, or
 *                    INVALID_PARTITION_ID to let the system choose.
 *
 * @retval B_OK           Child partition created successfully; @p *child is
 *                        set to the new KPartition object.
 * @retval B_BAD_VALUE    Any of @p partition, @p type, @p parameters,
 *                        @p child, or fModule is NULL.
 * @retval B_NOT_SUPPORTED The module does not provide a create_child hook.
 * @return Any error code propagated from partition->Open() or from the
 *         module's create_child hook.
 *
 * @note Even on success @p *child may be NULL if the manager cannot find
 *       the partition by the returned childID.
 */
// CreateChild
//! Creates a child partition
status_t
KPartitioningSystem::CreateChild(KPartition* partition, off_t offset,
	off_t size, const char* type, const char* name, const char* parameters,
	disk_job_id job, KPartition** child, partition_id childID)
{
	// check parameters
	if (!partition || !type || !parameters || !child || !fModule)
		return B_BAD_VALUE;
	if (!fModule->create_child)
		return B_NOT_SUPPORTED;

	// open partition device
	int fd = -1;
	status_t result = partition->Open(O_RDWR, &fd);
	if (result != B_OK)
		return result;

	// let the module do its job
	result = fModule->create_child(fd, partition->ID(), offset, size,
		type, name, parameters, job, &childID);

	// find and return the child
	*child = KDiskDeviceManager::Default()->FindPartition(childID);

	// cleanup and return
	close(fd);
	return result;
}


/**
 * @brief Removes a child partition entry from the parent's partition table.
 *
 * Opens the parent partition device read/write and calls the module's
 * delete_child hook.
 *
 * @param child  The child partition to delete.  Must not be NULL and must
 *               have a valid parent.
 * @param job    The disk job identifier for progress tracking.
 *
 * @retval B_OK           Deletion succeeded.
 * @retval B_BAD_VALUE    @p child or its parent is NULL.
 * @retval B_NOT_SUPPORTED The module does not provide a delete_child hook.
 * @return Any error code propagated from the parent's Open() call or from
 *         the module's delete_child hook.
 */
// DeleteChild
//! Deletes a child partition
status_t
KPartitioningSystem::DeleteChild(KPartition* child, disk_job_id job)
{
	if (!child || !child->Parent())
		return B_BAD_VALUE;
	if (!fModule->delete_child)
		return B_NOT_SUPPORTED;

	int fd = -1;
	KPartition* parent = child->Parent();
	status_t result = parent->Open(O_RDWR, &fd);
	if (result != B_OK)
		return result;

	result = fModule->delete_child(fd, parent->ID(), child->ID(), job);
	close(fd);
	return result;
}


/**
 * @brief Loads the underlying partition_module_info add-on into memory.
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
KPartitioningSystem::LoadModule()
{
	if (fModule)		// shouldn't happen
		return B_OK;
	return get_module(Name(), (module_info**)&fModule);
}


/**
 * @brief Unloads the underlying partition_module_info add-on.
 *
 * Calls put_module() to decrement the kernel module reference count and,
 * if it reaches zero, unload the add-on.  Sets fModule to NULL afterwards.
 *
 * @note Called by KDiskSystem::Unload() when the last user releases the
 *       disk system.  Safe to call when fModule is already NULL.
 */
// UnloadModule
void
KPartitioningSystem::UnloadModule()
{
	if (fModule) {
		put_module(fModule->module.name);
		fModule = NULL;
	}
}
