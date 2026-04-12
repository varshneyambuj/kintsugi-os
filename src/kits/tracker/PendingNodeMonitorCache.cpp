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
 *   Open Tracker License
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *   Distributed under the terms of the OpenTracker License.
 */


/**
 * @file PendingNodeMonitorCache.cpp
 * @brief Temporary cache for node-monitor messages that arrive before their pose exists.
 *
 * When a B_NODE_MONITOR notification arrives for a node that has not yet been
 * added to the pose list (e.g., during an async directory scan), the message
 * is held here.  Once the corresponding pose is created or moved, the cached
 * message is re-applied by calling BPoseView::FSNotification().  Stale entries
 * older than kDelayedNodeMonitorLifetime are purged automatically.
 *
 * @see BPoseView, BPose
 */


#include "PendingNodeMonitorCache.h"
#include "PoseView.h"


const bigtime_t kDelayedNodeMonitorLifetime = 10000000;
	// after this much the pending node monitor gets discarded as
	// too old


/**
 * @brief Construct a cache entry wrapping a node-monitor message.
 *
 * Sets the expiry time to now + kDelayedNodeMonitorLifetime.
 *
 * @param node          The node_ref the monitor message refers to.
 * @param nodeMonitor   The node-monitor BMessage to cache; copied by value.
 */
PendingNodeMonitorEntry::PendingNodeMonitorEntry(const node_ref* node,
	const BMessage* nodeMonitor)
	:
	fExpiresAfter(system_time() + kDelayedNodeMonitorLifetime),
	fNodeMonitor(*nodeMonitor),
	fNode(*node)
{
}


/**
 * @brief Return a pointer to the cached node-monitor message.
 *
 * @return Const pointer to the stored BMessage.
 */
const BMessage*
PendingNodeMonitorEntry::NodeMonitor() const
{
	return &fNodeMonitor;
}


/**
 * @brief Check whether this entry's node_ref matches @p node.
 *
 * @param node  The node_ref to compare against.
 * @return True if the node refs are equal.
 */
bool
PendingNodeMonitorEntry::Match(const node_ref* node) const
{
	return fNode == *node;
}


/**
 * @brief Return true if this entry has exceeded its maximum lifetime.
 *
 * @param now  Current system_time() in microseconds.
 * @return True if @p now is past the expiry timestamp.
 */
bool
PendingNodeMonitorEntry::TooOld(bigtime_t now) const
{
	return now > fExpiresAfter;
}


/**
 * @brief Construct an empty PendingNodeMonitorCache.
 */
PendingNodeMonitorCache::PendingNodeMonitorCache()
	:
	fList(10)
{
}


/**
 * @brief Destroy the cache and all contained entries.
 */
PendingNodeMonitorCache::~PendingNodeMonitorCache()
{
}


/**
 * @brief Add a node-monitor message to the cache.
 *
 * Extracts the device/node fields and creates a PendingNodeMonitorEntry.
 * Does nothing if the message does not contain both fields.
 *
 * @param message  The B_NODE_MONITOR BMessage to cache.
 */
void
PendingNodeMonitorCache::Add(const BMessage* message)
{
#if xDEBUG
	PRINT(("adding pending node monitor\n"));
	message->PrintToStream();
#endif
	node_ref node;
	if (message->FindInt32("device", &node.device) != B_OK
		|| message->FindInt64("node", (int64*)&node.node) != B_OK)
		return;

	fList.AddItem(new PendingNodeMonitorEntry(&node, message));
}


/**
 * @brief Remove all cached entries whose node_ref matches @p nodeRef.
 *
 * @param nodeRef  The node_ref to match and remove.
 */
void
PendingNodeMonitorCache::RemoveEntries(const node_ref* nodeRef)
{
	int32 count = fList.CountItems();
	for (int32 index = count - 1; index >= 0; index--)
		if (fList.ItemAt(index)->Match(nodeRef))
			delete fList.RemoveItemAt(index);
}


/**
 * @brief Discard any entries that have exceeded the maximum cache lifetime.
 */
void
PendingNodeMonitorCache::RemoveOldEntries()
{
	bigtime_t now = system_time();
	int32 count = fList.CountItems();
	for (int32 index = count - 1; index >= 0; index--)
		if (fList.ItemAt(index)->TooOld(now)) {
			PRINT(("removing old entry from pending node monitor cache\n"));
			delete fList.RemoveItemAt(index);
		}
}


/**
 * @brief Re-apply cached node-monitor messages for @p pose after it is created or moved.
 *
 * Iterates the cache and for any entry matching @p pose's node_ref calls
 * BPoseView::FSNotification() to replay the notification.  Expired entries
 * are pruned along the way.
 *
 * @param poseView  The BPoseView that should receive the replayed notifications.
 * @param pose      The newly created or moved BPose.
 */
void
PendingNodeMonitorCache::PoseCreatedOrMoved(BPoseView* poseView,
	const BPose* pose)
{
	bigtime_t now = system_time();
	for (int32 index = 0; index < fList.CountItems();) {
		PendingNodeMonitorEntry* item = fList.ItemAt(index);
		if (item->TooOld(now)) {
			PRINT(("removing old entry from pending node monitor cache\n"));
			delete fList.RemoveItemAt(index);
		} else if (item->Match(pose->TargetModel()->NodeRef())) {
			fList.RemoveItemAt(index);
#if DEBUG
			PRINT(("reapplying node monitor for model:\n"));
			pose->TargetModel()->PrintToStream();
			item->NodeMonitor()->PrintToStream();
			bool result =
#endif
			poseView->FSNotification(item->NodeMonitor());
			ASSERT(result);
			delete item;
		} else
			index++;
	}
}
