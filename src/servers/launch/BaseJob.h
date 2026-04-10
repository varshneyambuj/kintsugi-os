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

/** @file BaseJob.h
 *  @brief Common base for launch_daemon BJobs (condition, event, environment). */

#ifndef BASE_JOB_H
#define BASE_JOB_H


#include <Job.h>
#include <StringList.h>


using namespace BSupportKit;

class BMessage;
class Condition;
class ConditionContext;
class Event;


/** @brief BJob subclass adding condition gating, event triggering, and environment management.
 *
 * Both Job and Target inherit from BaseJob so they share the same
 * configuration handling: an optional Condition that gates execution, an
 * optional Event that schedules execution, and a captured environment
 * (variables plus source files to evaluate at launch time). */
class BaseJob : public BJob {
public:
								BaseJob(const char* name);
								~BaseJob();

	/** @brief Returns the job's display name. */
			const char*			Name() const;

	/** @brief Returns the gating condition, if any. */
			const ::Condition*	Condition() const;
	/** @brief Mutable variant of Condition(). */
			::Condition*		Condition();
	/** @brief Replaces the gating condition (taking ownership). */
			void				SetCondition(::Condition* condition);
	/** @brief Returns true if the job is currently allowed to run. */
	virtual	bool				CheckCondition(ConditionContext& context) const;

	/** @brief Returns the triggering event, if any. */
			const ::Event*		Event() const;
	/** @brief Mutable variant of Event(). */
			::Event*			Event();
	/** @brief Replaces the triggering event (taking ownership). */
			void				SetEvent(::Event* event);
	/** @brief Returns true if the configured event has fired. */
			bool				EventHasTriggered() const;

	/** @brief Returns the captured environment variable list. */
			const BStringList&	Environment() const;
			BStringList&		Environment();
	/** @brief Returns the list of shell source files to evaluate at launch time. */
			const BStringList&	EnvironmentSourceFiles() const;
			BStringList&		EnvironmentSourceFiles();
	/** @brief Replaces the captured environment from a settings message. */
			void				SetEnvironment(const BMessage& message);

	/** @brief Walks the source-file list and produces an evaluated environment. */
			void				GetSourceFilesEnvironment(
									BStringList& environment);
	/** @brief Resolves wildcards in the source file list. */
			void				ResolveSourceFiles();

private:
			void				_GetSourceFileEnvironment(const char* script,
									BStringList& environment);
			void				_ParseExportVariable(BStringList& environment,
									const BString& line);

protected:
			::Condition*		fCondition;       /**< Gating condition (owned), or NULL. */
			::Event*			fEvent;           /**< Triggering event (owned), or NULL. */
			BStringList			fEnvironment;     /**< Captured KEY=VALUE environment entries. */
			BStringList			fSourceFiles;     /**< Shell scripts evaluated at launch time. */
};


#endif // BASE_JOB_H
