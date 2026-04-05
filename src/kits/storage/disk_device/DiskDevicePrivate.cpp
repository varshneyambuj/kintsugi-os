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
 *   Copyright 2003-2006, Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file DiskDevicePrivate.cpp
 * @brief Internal visitor and filter helpers for the disk device kit.
 *
 * Implements PartitionFilterVisitor, which wraps a BDiskDeviceVisitor and
 * a PartitionFilter to deliver only matching nodes to the underlying visitor,
 * and IDFinderVisitor, a lightweight visitor that searches for a partition or
 * device by its numeric ID.
 *
 * @see BDiskDeviceVisitor
 * @see BDiskDevice
 */


#include <DiskDevicePrivate.h>
#include <DiskDevice.h>
#include <Partition.h>


/**
 * @brief Destroys the PartitionFilter base object.
 */
PartitionFilter::~PartitionFilter()
{
}


//	#pragma mark - PartitionFilterVisitor

/**
 * @brief Constructs a PartitionFilterVisitor that gates visits through a filter.
 *
 * @param visitor The underlying visitor to forward matching nodes to.
 * @param filter  The filter used to decide whether a node should be visited.
 */
PartitionFilterVisitor::PartitionFilterVisitor(BDiskDeviceVisitor *visitor,
											   PartitionFilter *filter)
	: BDiskDeviceVisitor(),
	  fVisitor(visitor),
	  fFilter(filter)
{
}

/**
 * @brief Visits a BDiskDevice if it passes the filter.
 *
 * Delegates to the wrapped visitor only when the filter accepts the device
 * at depth level 0.
 *
 * @param device The disk device to evaluate.
 * @return The return value of the wrapped visitor if the filter accepts the
 *         device, otherwise false.
 */
bool
PartitionFilterVisitor::Visit(BDiskDevice *device)
{
	if (fFilter->Filter(device, 0))
		return fVisitor->Visit(device);
	return false;
}

/**
 * @brief Visits a BPartition at the given depth if it passes the filter.
 *
 * Delegates to the wrapped visitor only when the filter accepts the partition.
 *
 * @param partition The partition to evaluate.
 * @param level     The depth of the partition in the device hierarchy.
 * @return The return value of the wrapped visitor if the filter accepts the
 *         partition, otherwise false.
 */
bool
PartitionFilterVisitor::Visit(BPartition *partition, int32 level)
{
	if (fFilter->Filter(partition, level))
		return fVisitor->Visit(partition, level);
	return false;
}


// #pragma mark -

// IDFinderVisitor

/**
 * @brief Constructs an IDFinderVisitor that searches for the given ID.
 *
 * @param id The partition or device ID to search for.
 */
IDFinderVisitor::IDFinderVisitor(int32 id)
	: BDiskDeviceVisitor(),
	  fID(id)
{
}

/**
 * @brief Returns true if the device's ID matches the target ID.
 *
 * @param device The disk device to compare.
 * @return true if device->ID() equals the target ID, false otherwise.
 */
bool
IDFinderVisitor::Visit(BDiskDevice *device)
{
	return (device->ID() == fID);
}

/**
 * @brief Returns true if the partition's ID matches the target ID.
 *
 * @param partition The partition to compare.
 * @param level     The depth of the partition in the device hierarchy (unused).
 * @return true if partition->ID() equals the target ID, false otherwise.
 */
bool
IDFinderVisitor::Visit(BPartition *partition, int32 level)
{
	return (partition->ID() == fID);
}
