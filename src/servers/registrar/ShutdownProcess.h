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
   Copyright 2005, Ingo Weinhold, bonefish@users.sf.net.
   Copyright 2021, Jacob Secunda.

   Distributed under the terms of the MIT License.

   Manages the shutdown process.
 */

/** @file ShutdownProcess.h
 *  @brief Manages the orderly system shutdown or reboot sequence for all running applications. */
#ifndef SHUTDOWN_PROCESS_H
#define SHUTDOWN_PROCESS_H

#include <HashMap.h>
#include <HashSet.h>
#include <Locker.h>
#include <Looper.h>

#include "AppInfoList.h"
#include "EventMaskWatcher.h"
#include "RosterAppInfo.h"

class EventQueue;
class TRoster;

// Note: EventMaskWatcher is inherited public due to a gcc bug/C++ feature:
// Once cast into a Watcher dynamic_cast<>()ing it back into an
// EventMaskWatcher fails otherwise.
/** @brief Orchestrates the phased shutdown of user, system, and background applications. */
class ShutdownProcess : public BLooper, public EventMaskWatcher {
public:
								ShutdownProcess(TRoster* roster,
									EventQueue* eventQueue);
	virtual						~ShutdownProcess();

			/** @brief Initializes the shutdown process from the original request message. */
			status_t			Init(BMessage* request);

	/** @brief Handles shutdown-related events during the shutdown sequence. */
	virtual	void				MessageReceived(BMessage* message);

	/** @brief Sends a status reply to the shutdown request originator. */
	static	void				SendReply(BMessage* request, status_t error);

private:
			void				_SendReply(status_t error);

			void				_NegativeQuitRequestReply(thread_id thread);

			void				_PrepareShutdownMessage(BMessage& message) const;
			status_t			_ShutDown();

			status_t			_PushEvent(uint32 eventType, team_id team,
									int32 phase);
			status_t			_GetNextEvent(uint32& eventType, team_id& team,
									int32& phase, bool block);

			void				_SetPhase(int32 phase);
			void				_ScheduleTimeoutEvent(bigtime_t timeout,
									team_id team = -1);

			bool				_LockAppLists();
			void				_UnlockAppLists();

			void				_InitShutdownWindow();
			void				_SetShowShutdownWindow(bool show);
			void				_AddShutdownWindowApps(AppInfoList& infos);
			void				_RemoveShutdownWindowApp(team_id team);
			void				_SetShutdownWindowCurrentApp(team_id team);
			void				_SetShutdownWindowText(const char* text);
			void				_SetShutdownWindowCancelButtonEnabled(
									bool enabled);
			void				_SetShutdownWindowKillButtonEnabled(
									bool enabled);
			void				_SetShutdownWindowWaitAnimationEnabled(
									bool enabled);
			void				_SetShutdownWindowWaitForShutdown();
			void				_SetShutdownWindowWaitForAbortedOK();

	static	status_t			_WorkerEntry(void* data);
			status_t			_Worker();

			void				_WorkerDoShutdown();
			bool				_WaitForApp(team_id team, AppInfoList* list,
									bool systemApps);
			void				_QuitApps(AppInfoList& list, bool systemApps);
			void				_QuitBackgroundApps();
			void				_WaitForBackgroundApps();
			void				_KillBackgroundApps();
			void				_QuitNonApps();
			void				_QuitBlockingApp(AppInfoList& list, team_id team,
									const char* appName, bool cancelAllowed);
			void				_DisplayAbortingApp(team_id team);
			void				_WaitForDebuggedTeams();

private:
	class TimeoutEvent;
	class InternalEvent;
	struct InternalEventList;
	class QuitRequestReplyHandler;
	class ShutdownWindow;

	typedef HashSet<HashKey32<team_id> > TeamHash;

	friend class QuitRequestReplyHandler;

			BLocker				fWorkerLock;
									// protects fields shared by looper
									// and worker
			BMessage*			fRequest;
			TRoster*			fRoster;
			EventQueue*			fEventQueue;
			TeamHash			fVitalSystemApps;
			AppInfoList			fSystemApps;
			AppInfoList			fUserApps;
			AppInfoList			fBackgroundApps;
			TeamHash			fDebuggedTeams;
			TimeoutEvent*		fTimeoutEvent;
			InternalEventList*	fInternalEvents;
			sem_id				fInternalEventSemaphore;
			QuitRequestReplyHandler* fQuitRequestReplyHandler;
			thread_id			fWorker;
			int32				fCurrentPhase;
			status_t			fShutdownError;
			bool				fHasGUI;
			bool				fReboot;
			bool				fRequestReplySent;
			ShutdownWindow*		fWindow;
};

#endif	// SHUTDOWN_PROCESS_H
