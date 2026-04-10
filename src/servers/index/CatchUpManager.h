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

/** @file CatchUpManager.h
 *  @brief Declarations for the catch-up indexing manager and its analyser thread. */

#ifndef CATCH_UP_MANAGER_H
#define CATCH_UP_MANAGER_H


#include "AnalyserDispatcher.h"


#define DEBUG_CATCH_UP
#ifdef DEBUG_CATCH_UP
#include <stdio.h>
#	define STRACE(x...) printf(x)
#else
#	define STRACE(x...) ;
#endif


/** @brief Analyses files modified in a specific time range to catch up after an offline period. */
class CatchUpAnalyser : public AnalyserDispatcher {
public:
								CatchUpAnalyser(const BVolume& volume,
									time_t start, time_t end,
									BHandler* manager);

	/** @brief Handle incoming BMessages (dispatches kCatchUp). */
			void				MessageReceived(BMessage *message);
	/** @brief Begin the asynchronous catch-up analysis. */
			void				StartAnalysing();

	/** @brief Analyse a single entry_ref against registered analysers whose time window matches. */
			void				AnalyseEntry(const entry_ref& ref);

	/** @brief Return the volume being analysed. */
			const BVolume&		Volume() { return fVolume; }

private:
			void				_CatchUp();
			void				_WriteSyncSatus(bigtime_t syncTime);

			BVolume				fVolume;   /**< Volume being caught up. */
			time_t				fStart;    /**< Start of the catch-up time window. */
			time_t				fEnd;      /**< End of the catch-up time window. */

			BHandler*			fCatchUpManager; /**< Back-pointer to the owning CatchUpManager. */
};


typedef BObjectList<CatchUpAnalyser> CatchUpAnalyserList;


/** @brief Queues FileAnalysers and spawns CatchUpAnalyser threads to process missed changes. */
class CatchUpManager : public BHandler {
public:
								CatchUpManager(const BVolume& volume);
								~CatchUpManager();

	/** @brief Handle incoming BMessages (processes kCatchUpDone). */
			void				MessageReceived(BMessage *message);

	/** @brief Add an analyser to the pending queue. */
			bool				AddAnalyser(const FileAnalyser* analyser);
	/** @brief Remove an analyser by name from the queue and active catch-up threads. */
			void				RemoveAnalyser(const BString& name);

	/** @brief Spawn a CatchUpAnalyser and fill it with the analysers in the queue. */
			bool				CatchUp();
	/** @brief Stop all active catch-up threads. */
			void				Stop();

private:
			BVolume				fVolume; /**< Volume this manager operates on. */

			FileAnalyserList	fFileAnalyserQueue;     /**< Pending analysers waiting for catch-up. */
			CatchUpAnalyserList	fCatchUpAnalyserList;   /**< Currently running catch-up analysers. */
};

#endif
