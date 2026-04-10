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
 *   Copyright 2002, Marcus Overhagen. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file NotificationManager.h
 *  @brief Dispatches media node change notifications to subscribed clients. */

#ifndef NOTIFICATION_MANAGER_H
#define NOTIFICATION_MANAGER_H


#include <Locker.h>
#include <MediaNode.h>
#include <Messenger.h>

#include "Queue.h"
#include "TList.h"


struct Notification {
	BMessenger	messenger; /**< Target messenger for the notification */
	media_node	node;      /**< Node to filter notifications for */
	int32		what;      /**< Notification type filter (or B_MEDIA_WILDCARD) */
	team_id		team;      /**< Subscribing team */
};

/** @brief Queues and broadcasts media node notifications to subscribers. */
class NotificationManager {
public:
	/** @brief Construct the notification manager and start the worker thread. */
								NotificationManager();
	/** @brief Terminate the queue and wait for the worker thread to exit. */
								~NotificationManager();

	/** @brief Dump the list of notification subscribers to stdout. */
			void				Dump();

	/** @brief Enqueue a notification message for asynchronous processing. */
			void				EnqueueMessage(BMessage* message);

	/** @brief Remove all notification subscriptions for the given team. */
			void				CleanupTeam(team_id team);

private:
			void				RequestNotifications(BMessage* message);
			void				CancelNotifications(BMessage* message);
			void				SendNotifications(BMessage* message);

			void				WorkerThread();
	static	int32				worker_thread(void* arg);

private:
			Queue				fNotificationQueue;
			thread_id			fNotificationThreadId;
			BLocker				fLocker;
			List<Notification>	fNotificationList;
};

#endif	// NOTIFICATION_MANAGER_H
