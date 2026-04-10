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
 *   Copyright 2013, Jerome Duval, korli@users.berlios.de.
 *   Copyright 2014, Rene Gollent, rene@gollent.com.
 *   Copyright 2005, Nathan Whitehorn.
 *
 *   Distributed under the terms of the MIT License.
 */

/** @file power_daemon.cpp
 *  @brief Power management daemon that monitors power buttons and lid events. */


#include "lid_monitor.h"
#include "power_button_monitor.h"

#include <Server.h>

#include <map>


class PowerManagementDaemon : public BServer {
public:
								PowerManagementDaemon();
	virtual 					~PowerManagementDaemon();
private:
			void				_EventLoop();
	static	status_t			_EventLooper(void *arg);

			thread_id			fEventThread;
			PowerMonitor*		fPowerMonitors[2];
			uint32				fMonitorCount;

			bool				fQuitRequested;
};


/**
 * @brief Entry point for the power management daemon.
 *
 * Creates the PowerManagementDaemon application, runs its message loop,
 * and cleans up before exiting.
 *
 * @return 0 on normal exit.
 */
int
main(void)
{
	new PowerManagementDaemon();
	be_app->Run();
	delete be_app;
	return 0;
}


/**
 * @brief Constructs the daemon and initializes power monitors.
 *
 * Creates PowerButtonMonitor and LidMonitor instances. Only monitors that
 * have at least one valid file descriptor are kept. Spawns and resumes
 * the event loop thread that polls these monitors for activity.
 */
PowerManagementDaemon::PowerManagementDaemon()
	:
	BServer("application/x-vnd.Haiku-powermanagement", false, NULL),
	fMonitorCount(0),
	fQuitRequested(false)
{
	PowerMonitor* powerButtonMonitor = new PowerButtonMonitor;
	if (powerButtonMonitor->FDs().size() > 0)
		fPowerMonitors[fMonitorCount++] = powerButtonMonitor;
	else
		delete powerButtonMonitor;

	PowerMonitor* lidMonitor = new LidMonitor;
	if (lidMonitor->FDs().size() > 0)
		fPowerMonitors[fMonitorCount++] = lidMonitor;
	else
		delete lidMonitor;

	fEventThread = spawn_thread(_EventLooper, "_power_daemon_event_loop_",
		B_NORMAL_PRIORITY, this);
	if (fEventThread < B_OK)
		return;
	if (resume_thread(fEventThread) < B_OK) {
		kill_thread(fEventThread);
		fEventThread = -1;
		return;
	}
}


/**
 * @brief Destroys the daemon and waits for the event loop thread to finish.
 *
 * Sets the quit flag, deletes all power monitors, and joins the event
 * loop thread.
 */
PowerManagementDaemon::~PowerManagementDaemon()
{
	fQuitRequested = true;
	for (uint32 i = 0; i < fMonitorCount; i++)
		delete fPowerMonitors[i];
	status_t status;
	wait_for_thread(fEventThread, &status);
}


/**
 * @brief Static thread entry point that delegates to _EventLoop().
 *
 * @param arg Pointer to the PowerManagementDaemon instance.
 * @return B_OK always.
 */
status_t
PowerManagementDaemon::_EventLooper(void* arg)
{
	PowerManagementDaemon* self = (PowerManagementDaemon*)arg;
	self->_EventLoop();
	return B_OK;
}


/**
 * @brief Main event loop that waits for power-related file descriptor events.
 *
 * Builds an array of object_wait_info entries from all monitored file
 * descriptors and enters a loop calling wait_for_objects(). When a
 * descriptor becomes readable, the corresponding PowerMonitor's
 * HandleEvent() is invoked. Exits when fQuitRequested is set.
 */
void
PowerManagementDaemon::_EventLoop()
{
	if (fMonitorCount == 0)
		return;

	std::map<int, PowerMonitor*> descriptorMap;

	size_t fdCount = 0;
	for (uint32 i = 0; i < fMonitorCount; i++)
		fdCount += fPowerMonitors[i]->FDs().size();

	object_wait_info info[fdCount];
	uint32 index = 0;
	for (uint32 i = 0; i < fMonitorCount; i++) {
		const std::set<int>& fds = fPowerMonitors[i]->FDs();
		for (std::set<int>::iterator it = fds.begin(); it != fds.end(); ++it) {
			info[index].object = *it;
			info[index].type = B_OBJECT_TYPE_FD;
			info[index].events = B_EVENT_READ;
			descriptorMap[*it] = fPowerMonitors[i];
			++index;
		}
	}
	while (!fQuitRequested) {
		if (wait_for_objects(info, fdCount) < B_OK)
			continue;
		// handle events and reset events
		for (uint32 i = 0; i < fdCount; i++) {
			if (info[i].events & B_EVENT_READ)
				descriptorMap[info[i].object]->HandleEvent(info[i].object);
			else
				info[i].events = B_EVENT_READ;
		}
	}
}
