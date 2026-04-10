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
 *   Copyright 2003-2015, Axel Doerfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file SyslogDaemon.h
 *  @brief Declaration of the SyslogDaemon server that collects system log messages. */

#ifndef _SYSLOG_DAEMON_H_
#define _SYSLOG_DAEMON_H_


#include <Locker.h>
#include <List.h>
#include <Server.h>
#include <OS.h>

#include <syslog_daemon.h>


typedef void (*handler_func)(syslog_message&);


/** @brief System-wide syslog daemon that reads log messages from a port and dispatches to handlers. */
class SyslogDaemon : public BServer {
public:
								SyslogDaemon();

	/** @brief Initialize the daemon port, register kernel syslog, and start output handlers. */
	virtual	void				ReadyToRun();
	/** @brief Display an informational about dialog. */
	virtual	void				AboutRequested();
	/** @brief Shut down the daemon thread and return true. */
	virtual	bool				QuitRequested();
	/** @brief Process listener add/remove messages. */
	virtual	void				MessageReceived(BMessage* message);

	/** @brief Register a handler function to receive syslog messages. */
			void				AddHandler(handler_func function);

private:
			void				_Daemon();
	static	int32				_DaemonThread(void* data);

private:
			thread_id			fDaemon;     /**< Thread running the message-reading loop. */
			port_id				fPort;       /**< Kernel port receiving syslog messages. */

			BLocker				fHandlerLock; /**< Lock protecting the handler list. */
			BList				fHandlers;    /**< List of registered handler_func callbacks. */
};


#endif	/* _SYSLOG_DAEMON_H_ */
