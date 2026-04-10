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
 *
 * Copyright 2013, Haiku, Inc. All Rights Reserved.
  * Distributed under the terms of the MIT License.
  *
  * Authors:
  *		Ingo Weinhold <ingo_weinhold@gmx.de>
 */

/** @file Job.h
 *  @brief Base class for asynchronous jobs executed by the package daemon */

#ifndef JOB_H
#define JOB_H


#include <Referenceable.h>
#include <util/DoublyLinkedList.h>


/** @brief Abstract base class for reference-counted jobs dispatched through the daemon's job queue */
class Job : public BReferenceable, public DoublyLinkedListLinkImpl<Job> {
public:
	/** @brief Construct a new Job */
								Job();
	/** @brief Destructor */
	virtual						~Job();

	/** @brief Execute the job's work; subclasses must implement */
	virtual	void				Do() = 0;
};


#endif	// JOB_H
