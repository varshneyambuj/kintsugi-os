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
 *   Copyright 2001-2010 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file NodeMonitor.cpp
 * @brief Public API functions for subscribing to and stopping node monitoring.
 *
 * Provides watch_volume(), watch_node(), and stop_watching() free functions
 * that wrap the kernel's _kern_start_watching(), _kern_stop_watching(), and
 * _kern_stop_notifying() syscalls. Callers may target notifications at either
 * a BMessenger or a BHandler/BLooper pair.
 *
 * @see NodeMonitorHandler
 */

#include <Messenger.h>
#include <NodeMonitor.h>

#include <MessengerPrivate.h>

#include <syscalls.h>

#include "node_monitor_private.h"


// TODO: Tests!


/**
 * @brief Subscribes a BMessenger target to volume-level node change
 *        notifications.
 *
 * Adds B_WATCH_VOLUME to @p flags and forwards the subscription to the kernel.
 * At least one of B_WATCH_NAME, B_WATCH_STAT, or B_WATCH_ATTR must be set in
 * @p flags; otherwise B_BAD_VALUE is returned.
 *
 * @param volume  Device ID of the volume to watch.
 * @param flags   Combination of B_WATCH_* flags specifying what to monitor.
 *                Must include at least one of B_WATCH_NAME, B_WATCH_STAT, or
 *                B_WATCH_ATTR.
 * @param target  The BMessenger that will receive notification messages.
 * @return B_OK on success, B_BAD_VALUE if no valid watch flags are given, or
 *         another error code from the kernel.
 */
status_t
watch_volume(dev_t volume, uint32 flags, BMessenger target)
{
	if ((flags & (B_WATCH_NAME | B_WATCH_STAT | B_WATCH_ATTR)) == 0)
		return B_BAD_VALUE;

	flags |= B_WATCH_VOLUME;

	BMessenger::Private messengerPrivate(target);
	port_id port = messengerPrivate.Port();
	int32 token = messengerPrivate.Token();
	return _kern_start_watching(volume, (ino_t)-1, flags, port, token);
}


/**
 * @brief Subscribes a BHandler/BLooper pair to volume-level node change
 *        notifications.
 *
 * Convenience overload that constructs a BMessenger from @p handler and
 * @p looper and delegates to watch_volume(dev_t, uint32, BMessenger).
 *
 * @param volume  Device ID of the volume to watch.
 * @param flags   Combination of B_WATCH_* flags; see the BMessenger overload.
 * @param handler The BHandler (or NULL for the looper itself) to notify.
 * @param looper  The BLooper that owns @p handler.
 * @return B_OK on success or an error code.
 */
status_t
watch_volume(dev_t volume, uint32 flags, const BHandler* handler,
	const BLooper* looper)
{
	return watch_volume(volume, flags, BMessenger(handler, looper));
}


/**
 * @brief Subscribes or unsubscribes a BMessenger target from node and/or
 *        mount watching.
 *
 * When @p flags is B_STOP_WATCHING the existing subscription for @p node is
 * cancelled. Otherwise, if B_WATCH_MOUNT is set a mount-watch subscription is
 * established first; any remaining flags start a node-watch subscription on
 * @p node. Passing a NULL @p node with node-watch flags returns B_BAD_VALUE.
 *
 * @param node   Pointer to the node_ref to watch, or NULL when only mount
 *               watching is requested.
 * @param flags  B_STOP_WATCHING to cancel, or a combination of B_WATCH_*
 *               flags to subscribe.
 * @param target The BMessenger that will receive notification messages.
 * @return B_OK on success, B_BAD_VALUE for invalid arguments, or another
 *         error code from the kernel.
 */
status_t
watch_node(const node_ref* node, uint32 flags, BMessenger target)
{
	if (!target.IsValid())
		return B_BAD_VALUE;

	BMessenger::Private messengerPrivate(target);
	port_id port = messengerPrivate.Port();
	int32 token = messengerPrivate.Token();

	if (flags == B_STOP_WATCHING) {
		// unsubscribe from node node watching
		if (node == NULL)
			return B_BAD_VALUE;

		return _kern_stop_watching(node->device, node->node, port, token);
	}

	// subscribe to...
	// mount watching
	if (flags & B_WATCH_MOUNT) {
		status_t status = _kern_start_watching((dev_t)-1, (ino_t)-1,
			B_WATCH_MOUNT, port, token);
		if (status < B_OK)
			return status;

		flags &= ~B_WATCH_MOUNT;
	}

	// node watching
	if (flags != 0) {
		if (node == NULL)
			return B_BAD_VALUE;

		return _kern_start_watching(node->device, node->node, flags, port,
			token);
	}

	return B_OK;
}


/**
 * @brief Subscribes or unsubscribes a BHandler/BLooper pair from node and/or
 *        mount watching.
 *
 * Convenience overload that constructs a BMessenger from @p handler and
 * @p looper and delegates to watch_node(const node_ref*, uint32, BMessenger).
 *
 * @param node    Pointer to the node_ref to watch, or NULL for mount-only.
 * @param flags   B_STOP_WATCHING or combination of B_WATCH_* flags.
 * @param handler The BHandler (or NULL for the looper itself) to notify.
 * @param looper  The BLooper that owns @p handler.
 * @return B_OK on success or an error code.
 */
status_t
watch_node(const node_ref* node, uint32 flags, const BHandler* handler,
	const BLooper* looper)
{
	return watch_node(node, flags, BMessenger(handler, looper));
}


/**
 * @brief Unsubscribes a BMessenger target from all node and mount monitoring.
 *
 * Calls the kernel's stop-notifying syscall to remove every active
 * subscription associated with the port/token pair of @p target.
 *
 * @param target The BMessenger whose subscriptions should be cancelled.
 * @return B_OK on success, B_BAD_VALUE if @p target is not valid, or another
 *         error code from the kernel.
 */
status_t
stop_watching(BMessenger target)
{
	if (!target.IsValid())
		return B_BAD_VALUE;

	BMessenger::Private messengerPrivate(target);
	port_id port = messengerPrivate.Port();
	int32 token = messengerPrivate.Token();

	return _kern_stop_notifying(port, token);
}


/**
 * @brief Unsubscribes a BHandler/BLooper pair from all node and mount
 *        monitoring.
 *
 * Convenience overload that constructs a BMessenger from @p handler and
 * @p looper and delegates to stop_watching(BMessenger).
 *
 * @param handler The BHandler (or NULL for the looper itself) whose
 *                subscriptions should be cancelled.
 * @param looper  The BLooper that owns @p handler.
 * @return B_OK on success or an error code.
 */
status_t
stop_watching(const BHandler* handler, const BLooper* looper)
{
	return stop_watching(BMessenger(handler, looper));
}
