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
 */

/**
 * @file KPartitionListener.cpp
 * @brief Abstract listener interface for partition change notifications.
 *
 * Provides default (no-op) implementations for all virtual notification
 * methods declared in KPartitionListener. Concrete subclasses override
 * only the events they care about.
 */

#include <KPartitionListener.h>
#include <util/kernel_cpp.h>


/** @brief Default constructor. */
KPartitionListener::KPartitionListener()
{
}

/** @brief Destructor. */
KPartitionListener::~KPartitionListener()
{
}

/**
 * @brief Called when the byte offset of @a partition has changed.
 * @param partition The partition whose offset changed.
 * @param offset    The new byte offset.
 */
void
KPartitionListener::OffsetChanged(KPartition *partition, off_t offset)
{
}

/**
 * @brief Called when the byte size of @a partition has changed.
 * @param partition The partition whose size changed.
 * @param size      The new size in bytes.
 */
void
KPartitionListener::SizeChanged(KPartition *partition, off_t size)
{
}

/**
 * @brief Called when the content size of @a partition has changed.
 * @param partition The partition whose content size changed.
 * @param size      The new content size in bytes.
 */
void
KPartitionListener::ContentSizeChanged(KPartition *partition, off_t size)
{
}

/**
 * @brief Called when the block size of @a partition has changed.
 * @param partition The partition whose block size changed.
 * @param blockSize The new block size in bytes.
 */
void
KPartitionListener::BlockSizeChanged(KPartition *partition, uint32 blockSize)
{
}

/**
 * @brief Called when the child index of @a partition within its parent has changed.
 * @param partition The partition whose index changed.
 * @param index     The new child index.
 */
void
KPartitionListener::IndexChanged(KPartition *partition, int32 index)
{
}

/**
 * @brief Called when the status of @a partition has changed.
 * @param partition The partition whose status changed.
 * @param status    The new status value.
 */
void
KPartitionListener::StatusChanged(KPartition *partition, uint32 status)
{
}

/**
 * @brief Called when the flags of @a partition have changed.
 * @param partition The partition whose flags changed.
 * @param flags     The new flags bitmask.
 */
void
KPartitionListener::FlagsChanged(KPartition *partition, uint32 flags)
{
}

/**
 * @brief Called when the name of @a partition has changed.
 * @param partition The partition whose name changed.
 * @param name      The new partition name string.
 */
void
KPartitionListener::NameChanged(KPartition *partition, const char *name)
{
}

/**
 * @brief Called when the content name of @a partition has changed.
 * @param partition The partition whose content name changed.
 * @param name      The new content name string.
 */
void
KPartitionListener::ContentNameChanged(KPartition *partition, const char *name)
{
}

/**
 * @brief Called when the partition type string of @a partition has changed.
 * @param partition The partition whose type changed.
 * @param type      The new type string.
 */
void
KPartitionListener::TypeChanged(KPartition *partition, const char *type)
{
}

/**
 * @brief Called when the numeric identifier of @a partition has changed.
 * @param partition The partition whose ID changed.
 * @param id        The new partition_id value.
 */
void
KPartitionListener::IDChanged(KPartition *partition, partition_id id)
{
}

/**
 * @brief Called when the volume ID associated with @a partition has changed.
 * @param partition The partition whose volume ID changed.
 * @param volumeID  The new volume device ID.
 */
void
KPartitionListener::VolumeIDChanged(KPartition *partition, dev_t volumeID)
{
}

/**
 * @brief Called when the raw parameters string of @a partition has changed.
 * @param partition  The partition whose parameters changed.
 * @param parameters The new parameters string.
 */
void
KPartitionListener::ParametersChanged(KPartition *partition,
									  const char *parameters)
{
}

/**
 * @brief Called when the content parameters string of @a partition has changed.
 * @param partition  The partition whose content parameters changed.
 * @param parameters The new content parameters string.
 */
void
KPartitionListener::ContentParametersChanged(KPartition *partition,
											 const char *parameters)
{
}

/**
 * @brief Called when a new child partition has been added to @a partition.
 * @param partition The parent partition.
 * @param child     The newly added child partition.
 * @param index     The index at which the child was inserted.
 */
void
KPartitionListener::ChildAdded(KPartition *partition, KPartition *child,
							   int32 index)
{
}

/**
 * @brief Called when a child partition has been removed from @a partition.
 * @param partition The parent partition.
 * @param child     The child partition that was removed.
 * @param index     The index from which the child was removed.
 */
void
KPartitionListener::ChildRemoved(KPartition *partition, KPartition *child,
								 int32 index)
{
}

/**
 * @brief Called when the disk system associated with @a partition has changed.
 * @param partition  The partition whose disk system changed.
 * @param diskSystem The new KDiskSystem, or NULL if none.
 */
void
KPartitionListener::DiskSystemChanged(KPartition *partition,
									  KDiskSystem *diskSystem)
{
}

/**
 * @brief Called when the opaque cookie stored on @a partition has changed.
 * @param partition The partition whose cookie changed.
 * @param cookie    The new cookie pointer.
 */
void
KPartitionListener::CookieChanged(KPartition *partition, void *cookie)
{
}

/**
 * @brief Called when the content cookie stored on @a partition has changed.
 * @param partition The partition whose content cookie changed.
 * @param cookie    The new content cookie pointer.
 */
void
KPartitionListener::ContentCookieChanged(KPartition *partition, void *cookie)
{
}
