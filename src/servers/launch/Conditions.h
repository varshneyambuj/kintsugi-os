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

/** @file Conditions.h
 *  @brief Tree of boolean conditions guarding when a launch_daemon job may run. */

#ifndef CONDITIONS_H
#define CONDITIONS_H


#include <String.h>


class BMessage;


/** @brief Read-only context exposed to Condition subclasses during evaluation. */
class ConditionContext {
public:
	/** @brief Returns true if the system was booted in safe mode. */
	virtual	bool				IsSafeMode() const = 0;
	/** @brief Returns true if the boot volume is read-only. */
	virtual	bool				BootVolumeIsReadOnly() const = 0;
};


/** @brief Abstract boolean condition gating a launch job.
 *
 * Conditions form a tree (and/or/not + leaf predicates) parsed from the
 * configuration file. The daemon evaluates them whenever a job is about to
 * launch and skips jobs whose condition tree does not currently hold. */
class Condition {
public:
								Condition();
	virtual						~Condition();

	/** @brief Subclass hook: returns true if the condition currently holds. */
	virtual	bool				Test(ConditionContext& context) const = 0;

	/** @brief Returns true if this condition's result will never change at runtime. */
	virtual	bool				IsConstant(ConditionContext& context) const;

	/** @brief Returns a human-readable description of the condition. */
	virtual	BString				ToString() const = 0;
};


/** @brief Helper factory for parsing and combining Condition trees. */
class Conditions {
public:
	/** @brief Builds a Condition tree from a parsed configuration message. */
	static	Condition*			FromMessage(const BMessage& message);
	/** @brief Wraps @p condition in an AND with a "not safe mode" check. */
	static	Condition*			AddNotSafeMode(Condition* condition);
};


#endif // CONDITIONS_H
