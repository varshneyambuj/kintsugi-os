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
 *   Copyright 2007, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file PartitionReference.cpp
 * @brief Reference-counted handle to a partition identity.
 *
 * PartitionReference pairs a partition_id with a change counter so that
 * disk device jobs can safely track which partition they target even across
 * structural modifications. It extends BReferenceable so that multiple jobs
 * can share a single handle through AcquireReference / ReleaseReference.
 *
 * @see DiskDeviceJob
 */

#include "PartitionReference.h"


/**
 * @brief Constructs a PartitionReference with the given partition ID and
 *        change counter.
 *
 * @param id            The unique identifier of the target partition.
 * @param changeCounter The change counter value at the time of creation.
 */
PartitionReference::PartitionReference(partition_id id, int32 changeCounter)
	:
	BReferenceable(),
	fID(id),
	fChangeCounter(changeCounter)
{
}


/**
 * @brief Destroys the PartitionReference.
 */
PartitionReference::~PartitionReference()
{
}


/**
 * @brief Updates the partition ID and change counter in one call.
 *
 * @param id            The new partition identifier.
 * @param changeCounter The new change counter value.
 */
void
PartitionReference::SetTo(partition_id id, int32 changeCounter)
{
	fID = id;
	fChangeCounter = changeCounter;
}


/**
 * @brief Returns the stored partition ID.
 *
 * @return The partition_id this reference tracks.
 */
partition_id
PartitionReference::PartitionID() const
{
	return fID;
}


/**
 * @brief Sets the partition ID without changing the change counter.
 *
 * @param id The new partition identifier.
 */
void
PartitionReference::SetPartitionID(partition_id id)
{
	fID = id;
}


/**
 * @brief Returns the stored change counter.
 *
 * @return The change counter value associated with the referenced partition.
 */
int32
PartitionReference::ChangeCounter() const
{
	return fChangeCounter;
}


/**
 * @brief Sets the change counter without altering the partition ID.
 *
 * @param counter The new change counter value.
 */
void
PartitionReference::SetChangeCounter(int32 counter)
{
	fChangeCounter = counter;
}
