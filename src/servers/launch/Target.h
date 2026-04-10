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
 *   Copyright 2015, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file Target.h
 *  @brief Named launch target — a group of jobs and shared data driven as one unit. */

#ifndef TARGET_H
#define TARGET_H


#include "BaseJob.h"

#include <Message.h>


using namespace BSupportKit;


/** @brief A named launch target containing per-target data and a launched-once flag.
 *
 * Targets are launch_daemon's grouping concept: jobs declare which target
 * they belong to, and the daemon launches the entire target's jobs together
 * in dependency order. The target also carries a free-form BMessage of
 * settings shared with all of its jobs. */
class Target : public BaseJob {
public:
								Target(const char* name);

	/** @brief Stores @p data inside the target under @p name. */
			status_t			AddData(const char* name, BMessage& data);
	/** @brief Returns the target's stored data message. */
			const BMessage&		Data() const
									{ return fData; }

	/** @brief Returns true once the target's jobs have been started. */
			bool				HasLaunched() const
									{ return fLaunched; }
	/** @brief Records that the target has been started. */
			void				SetLaunched(bool launched);

protected:
	/** @brief Marks the target as launched (the actual jobs run separately). */
	virtual	status_t			Execute();

private:
			BMessage			fData;     /**< Per-target shared settings. */
			bool				fLaunched;  /**< True after SetLaunched(true). */
};


#endif // TARGET_H
