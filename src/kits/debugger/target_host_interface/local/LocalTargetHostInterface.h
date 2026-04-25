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
 * MIT License. Copyright 2016, Rene Gollent, rene@gollent.com.
 */

/** @file LocalTargetHostInterface.h
    @brief Local-host implementation of TargetHostInterface. */

#ifndef LOCAL_TARGET_HOST_INTERFACE_H
#define LOCAL_TARGET_HOST_INTERFACE_H

#include "TargetHostInterface.h"


/** @brief Target host interface that drives debugging of teams running on the
           same machine via the local kernel debugger ports. */
class LocalTargetHostInterface : public TargetHostInterface {
public:
								LocalTargetHostInterface();
	virtual						~LocalTargetHostInterface();

	virtual	status_t			Init(Settings* settings);
	virtual	void				Close();

	virtual	bool				IsLocal() const;
	virtual	bool				Connected() const;

	virtual	TargetHost*			GetTargetHost();

	virtual	status_t			Attach(team_id id, thread_id threadID,
									DebuggerInterface*& _interface) const;
	virtual	status_t			CreateTeam(int commandLineArgc,
									const char* const* arguments,
									team_id& _teamID) const;
	virtual	status_t			LoadCore(const char* coreFilePath,
									DebuggerInterface*& _interface,
									thread_id& _thread) const;

	virtual	status_t			FindTeamByThread(thread_id thread,
									team_id& _teamID) const;


private:
	static	status_t			_PortLoop(void* arg);
			status_t			_HandleTeamEvent(team_id team, int32 opcode,
									bool& addToWaiters);

private:
			TargetHost*			fTargetHost;
			port_id				fDataPort;
			thread_id			fPortWorker;
};

#endif	// LOCAL_TARGET_HOST_INTERFACE_H
