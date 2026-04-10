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
 *   Copyright 2003-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file KDiskSystem.cpp
 * @brief Abstract base for disk system add-ons (file systems and partitioning systems).
 *
 * KDiskSystem manages the lifecycle of disk-system modules: loading the
 * add-on on demand, reference-counting usage, and unloading when no longer
 * needed. Both KFileSystem and KPartitioningSystem derive from this class.
 *
 * @see KFileSystem.cpp, KPartitioningSystem.cpp
 */

#include <stdio.h>
#include <stdlib.h>

#include <KernelExport.h>
#include <util/kernel_cpp.h>

#include "ddm_userland_interface.h"
#include "KDiskDeviceManager.h"
#include "KDiskDeviceUtils.h"
#include "KDiskSystem.h"


// debugging
//#define TRACE_KDISK_SYSTEM
#ifdef TRACE_KDISK_SYSTEM
#	define TRACE(x...) dprintf(x)
#else
#	define TRACE(x...)	do { } while (false)
#endif


/**
 * @brief Constructs a KDiskSystem and assigns it a unique identifier.
 *
 * Allocates a copy of @p name and stores it in fName.  The numeric ID is
 * generated atomically from the class-level counter fNextID.  fLoadCounter
 * is initialised to zero so the module is not loaded until the first
 * explicit call to Load().
 *
 * @param name  The full module name that identifies this disk system to the
 *              kernel add-on manager (e.g. "file_systems/bfs/v1").
 *
 * @note If the name allocation fails, fName will be NULL.  Check with Init().
 */
// constructor
KDiskSystem::KDiskSystem(const char *name)
	: fID(_NextID()),
	  fName(NULL),
	  fShortName(NULL),
	  fPrettyName(NULL),
	  fLoadCounter(0)
{
	set_string(fName, name);
}


/**
 * @brief Destroys the KDiskSystem and frees all allocated name strings.
 *
 * @note The caller is responsible for ensuring that the module has been
 *       fully unloaded (fLoadCounter == 0) before this destructor runs.
 */
// destructor
KDiskSystem::~KDiskSystem()
{
	free(fName);
	free(fShortName);
	free(fPrettyName);
}


/**
 * @brief Validates that the object was constructed successfully.
 *
 * Checks that the fName allocation in the constructor succeeded.  Derived
 * classes should call this first in their own Init() overrides.
 *
 * @retval B_OK        Initialisation check passed; fName is non-NULL.
 * @retval B_NO_MEMORY The name string allocation failed in the constructor.
 */
// Init
status_t
KDiskSystem::Init()
{
	return fName ? B_OK : B_NO_MEMORY;
}


// SetID
/*void
KDiskSystem::SetID(disk_system_id id)
{
	fID = id;
}*/


/**
 * @brief Returns the unique numeric identifier assigned to this disk system.
 *
 * @return The disk_system_id generated at construction time.
 */
// ID
disk_system_id
KDiskSystem::ID() const
{
	return fID;
}


/**
 * @brief Returns the full module name of this disk system.
 *
 * @return A pointer to the null-terminated module name string, or NULL if
 *         the constructor's allocation failed.
 */
// Name
const char *
KDiskSystem::Name() const
{
	return fName;
}


/**
 * @brief Returns the abbreviated display name of this disk system.
 *
 * @return A pointer to the null-terminated short name string, or NULL if
 *         SetShortName() has not yet been called successfully.
 */
// ShortName
const char *
KDiskSystem::ShortName() const
{
	return fShortName;
}


/**
 * @brief Returns the human-readable display name of this disk system.
 *
 * @return A pointer to the null-terminated pretty name string, or NULL if
 *         SetPrettyName() has not yet been called successfully.
 */
// PrettyName
const char *
KDiskSystem::PrettyName() const
{
	return fPrettyName;
}


/**
 * @brief Returns the capability and type flags for this disk system.
 *
 * The flags field is a bitmask combining B_DISK_SYSTEM_* constants.  The
 * most significant bit for callers is B_DISK_SYSTEM_IS_FILE_SYSTEM which
 * distinguishes file systems from partitioning systems.
 *
 * @return The current flags value.
 */
// Flags
uint32
KDiskSystem::Flags() const
{
	return fFlags;
}


/**
 * @brief Returns whether this disk system is a file system.
 *
 * @retval true   The B_DISK_SYSTEM_IS_FILE_SYSTEM flag is set.
 * @retval false  The flag is not set; this is a partitioning system.
 */
// IsFileSystem
bool
KDiskSystem::IsFileSystem() const
{
	return (fFlags & B_DISK_SYSTEM_IS_FILE_SYSTEM);
}


/**
 * @brief Returns whether this disk system is a partitioning system.
 *
 * @retval true   The B_DISK_SYSTEM_IS_FILE_SYSTEM flag is NOT set.
 * @retval false  The flag is set; this is a file system.
 */
// IsPartitioningSystem
bool
KDiskSystem::IsPartitioningSystem() const
{
	return !(fFlags & B_DISK_SYSTEM_IS_FILE_SYSTEM);
}


/**
 * @brief Populates a user_disk_system_info structure with this object's data.
 *
 * Copies the numeric ID, name strings, and flags into @p info so the
 * information can be passed to user-space callers.
 *
 * @param info  Output structure to fill.  If NULL the function returns
 *              immediately without writing anything.
 */
// GetInfo
void
KDiskSystem::GetInfo(user_disk_system_info *info)
{
	if (!info)
		return;

	info->id = ID();
	strlcpy(info->name, Name(), sizeof(info->name));
	strlcpy(info->short_name, ShortName(), sizeof(info->short_name));
	strlcpy(info->pretty_name, PrettyName(), sizeof(info->pretty_name));
	info->flags = Flags();
}


/**
 * @brief Increments the load reference count, loading the module if needed.
 *
 * Acquires the disk device manager lock and, if this is the first reference
 * (fLoadCounter transitions from 0 to 1), calls LoadModule().  On success
 * fLoadCounter is incremented.
 *
 * @retval B_OK  Module is loaded and the reference count has been incremented.
 * @return Any error code returned by LoadModule() if the module could not
 *         be loaded; in that case fLoadCounter is not incremented.
 */
// Load
status_t
KDiskSystem::Load()
{
	ManagerLocker locker(KDiskDeviceManager::Default());
TRACE("KDiskSystem::Load(): %s -> %ld\n", Name(), fLoadCounter + 1);
	status_t error = B_OK;
	if (fLoadCounter == 0)
		error = LoadModule();
	if (error == B_OK)
		fLoadCounter++;
	return error;
}


/**
 * @brief Decrements the load reference count, unloading the module if needed.
 *
 * Acquires the disk device manager lock and, if fLoadCounter reaches zero
 * after decrementing, calls UnloadModule().
 *
 * @note It is a programming error to call Unload() without a preceding
 *       successful Load().  The function guards against underflow by only
 *       decrementing when fLoadCounter > 0.
 */
// Unload
void
KDiskSystem::Unload()
{
	ManagerLocker locker(KDiskDeviceManager::Default());
TRACE("KDiskSystem::Unload(): %s -> %ld\n", Name(), fLoadCounter - 1);
	if (fLoadCounter > 0 && --fLoadCounter == 0)
		UnloadModule();
}


/**
 * @brief Returns whether the module is currently loaded.
 *
 * Acquires the disk device manager lock to read fLoadCounter atomically.
 *
 * @retval true   fLoadCounter > 0; the module is resident.
 * @retval false  fLoadCounter == 0; the module is not loaded.
 */
// IsLoaded
bool
KDiskSystem::IsLoaded() const
{
	ManagerLocker locker(KDiskDeviceManager::Default());
	return (fLoadCounter > 0);
}


/**
 * @brief Base implementation of partition identification; always fails.
 *
 * Derived classes (KFileSystem, KPartitioningSystem) override this to
 * delegate to their respective module's identify_partition hook.
 *
 * @param partition  The partition to probe.
 * @param cookie     Output parameter for the identification cookie.
 *
 * @return -1 always; derived implementations return a score in [0, 1].
 */
// Identify
float
KDiskSystem::Identify(KPartition *partition, void **cookie)
{
	// to be implemented by derived classes
	return -1;
}


/**
 * @brief Base implementation of partition scanning; always fails.
 *
 * Derived classes override this to populate partition content information
 * after a successful Identify().
 *
 * @param partition  The partition to scan.
 * @param cookie     The identification cookie from Identify().
 *
 * @retval B_ERROR  Always; derived classes return the actual result.
 */
// Scan
status_t
KDiskSystem::Scan(KPartition *partition, void *cookie)
{
	// to be implemented by derived classes
	return B_ERROR;
}


/**
 * @brief Base implementation of identify-cookie release; does nothing.
 *
 * Derived classes override this to free the cookie allocated by their
 * Identify() implementation.
 *
 * @param partition  The partition for which Identify() was called.
 * @param cookie     The cookie to release.
 */
// FreeIdentifyCookie
void
KDiskSystem::FreeIdentifyCookie(KPartition *partition, void *cookie)
{
	// to be implemented by derived classes
}


/**
 * @brief Base implementation of per-partition cookie release; does nothing.
 *
 * Derived classes override this to free any partition-level private data
 * allocated during scanning.
 *
 * @param partition  The partition whose cookie is to be freed.
 */
// FreeCookie
void
KDiskSystem::FreeCookie(KPartition *partition)
{
	// to be implemented by derived classes
}


/**
 * @brief Base implementation of content-cookie release; does nothing.
 *
 * Derived classes override this to free any content-level private data
 * associated with the partition.
 *
 * @param partition  The partition whose content cookie is to be freed.
 */
// FreeContentCookie
void
KDiskSystem::FreeContentCookie(KPartition *partition)
{
	// to be implemented by derived classes
}


/**
 * @brief Base implementation of file system defragmentation; always fails.
 *
 * @param partition  The partition to defragment.
 * @param job        The disk job identifier.
 *
 * @retval B_ERROR  Always; override in a derived class to implement.
 */
// Defragment
status_t
KDiskSystem::Defragment(KPartition* partition, disk_job_id job)
{
	// to be implemented by derived classes
	return B_ERROR;
}


/**
 * @brief Base implementation of partition check/repair; always fails.
 *
 * @param partition  The partition to check or repair.
 * @param checkOnly  If true, perform a read-only check only.
 * @param job        The disk job identifier.
 *
 * @retval B_ERROR  Always; override in a derived class to implement.
 */
// Repair
status_t
KDiskSystem::Repair(KPartition* partition, bool checkOnly, disk_job_id job)
{
	// to be implemented by derived classes
	return B_ERROR;
}


/**
 * @brief Base implementation of partition resize; always fails.
 *
 * @param partition  The partition to resize.
 * @param size       The desired new size in bytes.
 * @param job        The disk job identifier.
 *
 * @retval B_ERROR  Always; override in a derived class to implement.
 */
// Resize
status_t
KDiskSystem::Resize(KPartition* partition, off_t size, disk_job_id job)
{
	// to be implemented by derived classes
	return B_ERROR;
}


/**
 * @brief Base implementation of child partition resize; always fails.
 *
 * @param child  The child partition to resize.
 * @param size   The desired new size in bytes.
 * @param job    The disk job identifier.
 *
 * @retval B_ERROR  Always; override in a derived class to implement.
 */
// ResizeChild
status_t
KDiskSystem::ResizeChild(KPartition* child, off_t size, disk_job_id job)
{
	// to be implemented by derived classes
	return B_ERROR;
}


/**
 * @brief Base implementation of partition move; always fails.
 *
 * @param partition  The partition to move.
 * @param offset     The new byte offset on the device.
 * @param job        The disk job identifier.
 *
 * @retval B_ERROR  Always; override in a derived class to implement.
 */
// Move
status_t
KDiskSystem::Move(KPartition* partition, off_t offset, disk_job_id job)
{
	// to be implemented by derived classes
	return B_ERROR;
}


/**
 * @brief Base implementation of child partition move; always fails.
 *
 * @param child   The child partition to move.
 * @param offset  The new byte offset on the parent device.
 * @param job     The disk job identifier.
 *
 * @retval B_ERROR  Always; override in a derived class to implement.
 */
// MoveChild
status_t
KDiskSystem::MoveChild(KPartition* child, off_t offset, disk_job_id job)
{
	// to be implemented by derived classes
	return B_ERROR;
}


/**
 * @brief Base implementation of partition name change; always fails.
 *
 * @param partition  The partition whose name is to be set.
 * @param name       The new name string.
 * @param job        The disk job identifier.
 *
 * @retval B_ERROR  Always; override in a derived class to implement.
 */
// SetName
status_t
KDiskSystem::SetName(KPartition* partition, const char* name, disk_job_id job)
{
	// to be implemented by derived classes
	return B_ERROR;
}


/**
 * @brief Base implementation of content name change; always fails.
 *
 * @param partition  The partition whose content name is to be set.
 * @param name       The new content name string.
 * @param job        The disk job identifier.
 *
 * @retval B_ERROR  Always; override in a derived class to implement.
 */
// SetContentName
status_t
KDiskSystem::SetContentName(KPartition* partition, const char* name,
	disk_job_id job)
{
	// to be implemented by derived classes
	return B_ERROR;
}


/**
 * @brief Base implementation of partition type change; always fails.
 *
 * @param partition  The partition whose type is to be set.
 * @param type       The new type string (e.g. a GUID or type constant).
 * @param job        The disk job identifier.
 *
 * @retval B_ERROR  Always; override in a derived class to implement.
 */
// SetType
status_t
KDiskSystem::SetType(KPartition* partition, const char *type, disk_job_id job)
{
	// to be implemented by derived classes
	return B_ERROR;
}


/**
 * @brief Base implementation of partition parameter change; always fails.
 *
 * @param partition   The partition whose parameters are to be set.
 * @param parameters  The new parameter string.
 * @param job         The disk job identifier.
 *
 * @retval B_ERROR  Always; override in a derived class to implement.
 */
// SetParameters
status_t
KDiskSystem::SetParameters(KPartition* partition, const char* parameters,
	disk_job_id job)
{
	// to be implemented by derived classes
	return B_ERROR;
}


/**
 * @brief Base implementation of content parameter change; always fails.
 *
 * @param partition   The partition whose content parameters are to be set.
 * @param parameters  The new parameter string.
 * @param job         The disk job identifier.
 *
 * @retval B_ERROR  Always; override in a derived class to implement.
 */
// SetContentParameters
status_t
KDiskSystem::SetContentParameters(KPartition* partition,
	const char* parameters, disk_job_id job)
{
	// to be implemented by derived classes
	return B_ERROR;
}


/**
 * @brief Base implementation of partition initialization; always fails.
 *
 * @param partition   The partition to format or initialize.
 * @param name        The initial volume or partition name.
 * @param parameters  Optional creation parameters string.
 * @param job         The disk job identifier.
 *
 * @retval B_ERROR  Always; override in a derived class to implement.
 */
// Initialize
status_t
KDiskSystem::Initialize(KPartition* partition, const char* name,
	const char* parameters, disk_job_id job)
{
	// to be implemented by derived classes
	return B_ERROR;
}


/**
 * @brief Base implementation of partition uninitialization; reports unsupported.
 *
 * @param partition  The partition to uninitialize.
 * @param job        The disk job identifier.
 *
 * @retval B_NOT_SUPPORTED  Always; override in a derived class to implement.
 */
status_t
KDiskSystem::Uninitialize(KPartition* partition, disk_job_id job)
{
	// to be implemented by derived classes
	return B_NOT_SUPPORTED;
}


/**
 * @brief Base implementation of child partition creation; always fails.
 *
 * @param partition   The parent partition in which to create the child.
 * @param offset      Byte offset of the new child within the parent.
 * @param size        Size in bytes of the new child partition.
 * @param type        Partition type string.
 * @param name        Partition name string, or NULL.
 * @param parameters  Optional parameters string.
 * @param job         The disk job identifier.
 * @param child       Output pointer; receives the newly created KPartition.
 * @param childID     The desired partition_id for the new child, or
 *                    INVALID_PARTITION_ID to let the system choose.
 *
 * @retval B_ERROR  Always; override in a derived class to implement.
 */
// CreateChild
status_t
KDiskSystem::CreateChild(KPartition* partition, off_t offset, off_t size,
	const char* type, const char* name, const char* parameters, disk_job_id job,
	KPartition **child, partition_id childID)
{
	// to be implemented by derived classes
	return B_ERROR;
}


/**
 * @brief Base implementation of child partition deletion; always fails.
 *
 * @param child  The child partition to delete.
 * @param job    The disk job identifier.
 *
 * @retval B_ERROR  Always; override in a derived class to implement.
 */
// DeleteChild
status_t
KDiskSystem::DeleteChild(KPartition* child, disk_job_id job)
{
	// to be implemented by derived classes
	return B_ERROR;
}


/**
 * @brief Base implementation of module loading; always fails.
 *
 * Derived classes must override this to load their specific add-on module
 * via get_module() and store the result in their fModule pointer.
 *
 * @retval B_ERROR  Always; derived classes return the actual result.
 */
// LoadModule
status_t
KDiskSystem::LoadModule()
{
	// to be implemented by derived classes
	return B_ERROR;
}


/**
 * @brief Base implementation of module unloading; does nothing.
 *
 * Derived classes must override this to call put_module() and clear their
 * fModule pointer.
 */
// UnloadModule
void
KDiskSystem::UnloadModule()
{
	// to be implemented by derived classes
}


/**
 * @brief Stores a copy of @p name as this disk system's abbreviated name.
 *
 * @param name  The short name string to copy and store.
 *
 * @retval B_OK        The short name was stored successfully.
 * @retval B_NO_MEMORY Allocation of the copy failed.
 */
// SetShortName
status_t
KDiskSystem::SetShortName(const char *name)
{
	return set_string(fShortName, name);
}


/**
 * @brief Stores a copy of @p name as this disk system's human-readable name.
 *
 * @param name  The pretty name string to copy and store.
 *
 * @retval B_OK        The pretty name was stored successfully.
 * @retval B_NO_MEMORY Allocation of the copy failed.
 */
// SetPrettyName
status_t
KDiskSystem::SetPrettyName(const char *name)
{
	return set_string(fPrettyName, name);
}


/**
 * @brief Replaces the capability/type flags for this disk system.
 *
 * @param flags  A bitmask of B_DISK_SYSTEM_* flag constants.  Typically set
 *               once during Init() from the loaded module's flags field.
 */
// SetFlags
void
KDiskSystem::SetFlags(uint32 flags)
{
	fFlags = flags;
}


/**
 * @brief Atomically generates and returns the next unique disk system ID.
 *
 * Uses atomic_add() on the class-level counter fNextID to ensure each
 * KDiskSystem instance receives a distinct identifier even when multiple
 * objects are constructed concurrently.
 *
 * @return A unique, monotonically increasing int32 identifier.
 */
// _NextID
int32
KDiskSystem::_NextID()
{
	return atomic_add(&fNextID, 1);
}


// fNextID
int32 KDiskSystem::fNextID = 0;
