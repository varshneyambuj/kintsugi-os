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
 *   Copyright 2010, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Clemens Zeidler <haiku@clemens-zeidler.de>
 */

/** @file AnalyserDispatcher.h
 *  @brief Dispatcher that routes file-system events to registered FileAnalyser add-ons. */

#ifndef ANALYSER_DISPATCHER
#define ANALYSER_DISPATCHER


#include <Looper.h>
#include <String.h>

#include "IndexServerAddOn.h"


class FileAnalyser;


/** @brief Dispatches file-system change notifications to a list of FileAnalyser instances. */
class AnalyserDispatcher : public BLooper {
public:
								AnalyserDispatcher(const char* name);
								~AnalyserDispatcher();

	/** @brief Signal the dispatcher to stop processing. */
			void				Stop();
	/** @brief Return whether the dispatcher has been stopped. */
			bool				Stopped();

	/** @brief Return whether the dispatcher is currently busy processing. */
			bool				Busy();

	/** @brief Notify all analysers that a new entry was created or modified. */
			void				AnalyseEntry(const entry_ref& ref);
	/** @brief Notify all analysers that an entry was deleted. */
			void				DeleteEntry(const entry_ref& ref);
	/** @brief Notify all analysers that an entry was moved/renamed. */
			void				MoveEntry(const entry_ref& oldRef,
									const entry_ref& newRef);
	/** @brief Notify all analysers that the last entry in a batch has been reached. */
			void				LastEntry();

	/** @brief Thread-safe addition of a FileAnalyser to the dispatcher. */
			bool				AddAnalyser(FileAnalyser* analyser);
	/** @brief Remove a FileAnalyser by name. */
			bool				RemoveAnalyser(const BString& name);

	/** @brief Persist settings for all registered analysers. */
			void				WriteAnalyserSettings();
	/** @brief Set the sync position timestamp for all analysers. */
			void				SetSyncPosition(bigtime_t time);
	/** @brief Set the watching-start timestamp for all analysers. */
			void				SetWatchingStart(bigtime_t time);
	/** @brief Set the watching-position timestamp for all analysers. */
			void				SetWatchingPosition(bigtime_t time);

protected:
			FileAnalyserList	fFileAnalyserList; /**< List of active FileAnalyser instances. */

private:
			FileAnalyser*		_FindAnalyser(const BString& name);

			int32				fStopped; /**< Atomic flag indicating stop was requested. */
};

#endif // ANALYSER_DISPATCHER
