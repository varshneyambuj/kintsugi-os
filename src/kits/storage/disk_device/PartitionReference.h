/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2007, Haiku.
 * Original author: Ingo Weinhold.
 */

/** @file PartitionReference.h
    @brief Reference-counted handle to a partition identified by id and
    change counter. */

#ifndef _PARTITION_REFERENCE_H
#define _PARTITION_REFERENCE_H

#include <DiskDeviceDefs.h>

#include <Referenceable.h>


namespace BPrivate {


/** @brief Shared, refcounted partition identity used by disk-device jobs. */
class PartitionReference : public BReferenceable {
public:
								PartitionReference(partition_id id = -1,
									int32 changeCounter = 0);
								~PartitionReference();

			void				SetTo(partition_id id, int32 changeCounter);

			partition_id		PartitionID() const;
			void				SetPartitionID(partition_id id);

			int32				ChangeCounter() const;
			void				SetChangeCounter(int32 counter);

private:
			partition_id		fID;
			int32				fChangeCounter;
};


}	// namespace BPrivate

using BPrivate::PartitionReference;

#endif	// _PARTITION_REFERENCE_H
