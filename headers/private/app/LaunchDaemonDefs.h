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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2015-2018, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 */
#ifndef LAUNCH_DAEMON_DEFS_H
#define LAUNCH_DAEMON_DEFS_H


//!	launch_daemon interface


#include <Errors.h>
#include <Roster.h>


namespace BPrivate {


#define kLaunchDaemonSignature "application/x-vnd.Haiku-launch_daemon"
#ifdef TEST_MODE
#	define B_LAUNCH_DAEMON_PORT_NAME "test:launch_daemon"
#else
#	define B_LAUNCH_DAEMON_PORT_NAME "system:launch_daemon"
#endif


// Message constants
enum {
	B_GET_LAUNCH_DATA			= 'lnda',
	B_LAUNCH_TARGET				= 'lntg',
	B_STOP_LAUNCH_TARGET		= 'lnst',
	B_LAUNCH_JOB				= 'lnjo',
	B_ENABLE_LAUNCH_JOB			= 'lnje',
	B_STOP_LAUNCH_JOB			= 'lnsj',
	B_LAUNCH_SESSION			= 'lnse',
	B_REGISTER_SESSION_DAEMON	= 'lnrs',
	B_REGISTER_LAUNCH_EVENT		= 'lnre',
	B_UNREGISTER_LAUNCH_EVENT	= 'lnue',
	B_NOTIFY_LAUNCH_EVENT		= 'lnne',
	B_RESET_STICKY_LAUNCH_EVENT	= 'lnRe',
	B_GET_LAUNCH_TARGETS		= 'lngt',
	B_GET_LAUNCH_JOBS			= 'lngj',
	B_GET_LAUNCH_TARGET_INFO	= 'lntI',
	B_GET_LAUNCH_JOB_INFO		= 'lnjI',
	B_GET_LAUNCH_LOG			= 'lnLL',
};


}	// namespace BPrivate


#endif	// LAUNCH_DAEMON_DEFS_H

