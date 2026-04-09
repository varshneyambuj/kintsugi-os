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
 *   Copyright 2008, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file notifications.cpp
 *  @brief Userland front-end to the network stack notification module.
 *         Uses the generic-syscall channel to register/deregister BMessengers
 *         that receive link status and routing events from the kernel. */

#include <net_notifications.h>

#include <MessengerPrivate.h>
#include <generic_syscall_defs.h>
#include <syscalls.h>


/** @brief Probes whether the notifications generic syscall is present.
 *  @return B_OK if the kernel module is installed, or an error otherwise. */
static status_t
check_for_notifications_syscall(void)
{
	uint32 version = 0;
	return _kern_generic_syscall(NET_NOTIFICATIONS_SYSCALLS, B_SYSCALL_INFO,
		&version, sizeof(version));
}


//	#pragma mark -


/** @brief Registers @a target to receive network events matching @a flags.
 *  @param flags  Bitmask of B_WATCH_NETWORK_* events to subscribe to. Zero
 *                unsubscribes the messenger.
 *  @param target BMessenger that will receive the notification messages.
 *  @return B_OK on success, B_NOT_SUPPORTED if the notifications module
 *          is unavailable, or an error from the kernel. */
status_t
start_watching_network(uint32 flags, const BMessenger& target)
{
	if (check_for_notifications_syscall() != B_OK)
		return B_NOT_SUPPORTED;

	BMessenger::Private targetPrivate(const_cast<BMessenger&>(target));
	net_notifications_control control;
	control.flags = flags;
	control.port = targetPrivate.Port();
	control.token = targetPrivate.Token();

	return _kern_generic_syscall(NET_NOTIFICATIONS_SYSCALLS,
		NET_NOTIFICATIONS_CONTROL_WATCHING, &control,
		sizeof(net_notifications_control));
}


/** @brief Convenience overload that subscribes an in-process BHandler/BLooper. */
status_t
start_watching_network(uint32 flags, const BHandler* handler,
	const BLooper* looper)
{
	const BMessenger target(handler, looper);
	return start_watching_network(flags, target);
}

/** @brief Unsubscribes @a target from all network notifications.
 *         Internally re-invokes start_watching_network() with flags == 0. */
status_t
stop_watching_network(const BMessenger& target)
{
	return start_watching_network(0, target);
		// start_watching_network() without flags just stops everything
}


/** @brief Convenience overload that unsubscribes an in-process BHandler/BLooper. */
status_t
stop_watching_network(const BHandler* handler, const BLooper* looper)
{
	const BMessenger target(handler, looper);
	return stop_watching_network(target);
}
