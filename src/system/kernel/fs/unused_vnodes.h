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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file unused_vnodes.h
 *  @brief Internal helpers tracking unreferenced vnodes for opportunistic eviction.
 *
 * Implements a two-tier scheme: vnodes whose ref_count drops to zero first
 * land in a small per-CPU "hot" array so a fresh access can re-use them
 * without paying for a list operation; once the hot array fills up its
 * contents are flushed onto the global LRU "unused" list, where the low
 * resource manager can recycle them under memory pressure. */

#ifndef UNUSED_VNODES_H
#define UNUSED_VNODES_H


#include <algorithm>

#include <util/AutoLock.h>
#include <util/list.h>

#include <low_resource_manager.h>

#include "Vnode.h"


/** @brief Soft cap on the number of unused vnodes the kernel keeps cached.
 *
 * If memory is plentiful the cache may grow beyond this; the limit only
 * triggers eviction when the low resource manager reports pressure. */
const static uint32 kMaxUnusedVnodes = 8192;


/*!	\brief Guards sUnusedVnodeList and sUnusedVnodes.

	Must have at least a read-lock of sHotVnodesLock when acquiring!
*/
static spinlock sUnusedVnodesLock = B_SPINLOCK_INITIALIZER;
typedef DoublyLinkedList<Vnode, DoublyLinkedListMemberGetLink<Vnode, &Vnode::unused_link> >
	UnusedVnodeList;
static UnusedVnodeList sUnusedVnodeList;
static uint32 sUnusedVnodes = 0;

static const int32 kMaxHotVnodes = 1024;
static rw_lock sHotVnodesLock = RW_LOCK_INITIALIZER("hot vnodes");
static Vnode* sHotVnodes[kMaxHotVnodes];
static int32 sNextHotVnodeIndex = 0;

static const int32 kUnusedVnodesCheckInterval = 64;
static int32 sUnusedVnodesCheckCount = 0;


/*!	Must be called with sHotVnodesLock write-locked.
*/
static void
flush_hot_vnodes_locked()
{
	// Since sUnusedVnodesLock is always acquired after sHotVnodesLock,
	// we can safely hold it for the whole duration of the flush.
	// We don't want to be descheduled while holding the write-lock, anyway.
	InterruptsSpinLocker unusedLocker(sUnusedVnodesLock);

	int32 count = std::min(sNextHotVnodeIndex, kMaxHotVnodes);
	for (int32 i = 0; i < count; i++) {
		Vnode* vnode = sHotVnodes[i];
		if (vnode == NULL)
			continue;

		if (vnode->IsHot()) {
			if (vnode->IsUnused()) {
				sUnusedVnodeList.Add(vnode);
				sUnusedVnodes++;
			}
			vnode->SetHot(false);
		}

		sHotVnodes[i] = NULL;
	}

	unusedLocker.Unlock();

	sNextHotVnodeIndex = 0;
}



/*!	To be called when the vnode's ref count drops to 0.
	Must be called with sVnodeLock at least read-locked and the vnode locked.
	\param vnode The vnode.
	\return \c true, if the caller should trigger unused vnode freeing.
*/
static bool
vnode_unused(Vnode* vnode)
{
	ReadLocker hotReadLocker(sHotVnodesLock);

	vnode->SetUnused(true);

	bool result = false;
	int32 checkCount = atomic_add(&sUnusedVnodesCheckCount, 1);
	if (checkCount == kUnusedVnodesCheckInterval) {
		uint32 unusedCount = atomic_get((int32*)&sUnusedVnodes);
		if (unusedCount > kMaxUnusedVnodes
			&& low_resource_state(
				B_KERNEL_RESOURCE_PAGES | B_KERNEL_RESOURCE_MEMORY)
					!= B_NO_LOW_RESOURCE) {
			// there are too many unused vnodes -- tell the caller to free the
			// oldest ones
			result = true;
		} else {
			// nothing urgent -- reset the counter and re-check then
			atomic_set(&sUnusedVnodesCheckCount, 0);
		}
	}

	// nothing to do, if the node is already hot
	if (vnode->IsHot())
		return result;

	// no -- enter it
	int32 index = atomic_add(&sNextHotVnodeIndex, 1);
	if (index < kMaxHotVnodes) {
		vnode->SetHot(true);
		sHotVnodes[index] = vnode;
		return result;
	}

	// the array is full -- it has to be emptied
	hotReadLocker.Unlock();
	WriteLocker hotWriteLocker(sHotVnodesLock);

	// unless someone was faster than we were, we have to flush the array
	if (sNextHotVnodeIndex >= kMaxHotVnodes)
		flush_hot_vnodes_locked();

	// enter the vnode
	index = sNextHotVnodeIndex++;
	vnode->SetHot(true);
	sHotVnodes[index] = vnode;

	return result;
}


/*!	To be called when the vnode's ref count is changed from 0 to 1.
	Must be called with sVnodeLock at least read-locked and the vnode locked.
	\param vnode The vnode.
*/
static void
vnode_used(Vnode* vnode)
{
	ReadLocker hotReadLocker(sHotVnodesLock);

	if (!vnode->IsUnused())
		return;

	vnode->SetUnused(false);

	if (!vnode->IsHot()) {
		InterruptsSpinLocker unusedLocker(sUnusedVnodesLock);
		sUnusedVnodeList.Remove(vnode);
		sUnusedVnodes--;
	}
}


/*!	To be called when the vnode's is about to be freed.
	Must be called with sVnodeLock at least read-locked and the vnode locked.
	\param vnode The vnode.
*/
static void
vnode_to_be_freed(Vnode* vnode)
{
	ReadLocker hotReadLocker(sHotVnodesLock);

	if (vnode->IsHot()) {
		// node is hot -- remove it from the array
// TODO: Maybe better completely flush the array while at it?
		int32 count = atomic_get(&sNextHotVnodeIndex);
		count = std::min(count, kMaxHotVnodes);
		for (int32 i = 0; i < count; i++) {
			if (sHotVnodes[i] == vnode) {
				sHotVnodes[i] = NULL;
				break;
			}
		}
	} else if (vnode->IsUnused()) {
		InterruptsSpinLocker unusedLocker(sUnusedVnodesLock);
		sUnusedVnodeList.Remove(vnode);
		sUnusedVnodes--;
	}

	vnode->SetUnused(false);
}


static inline void
flush_hot_vnodes()
{
	WriteLocker hotWriteLocker(sHotVnodesLock);
	flush_hot_vnodes_locked();
}


static inline void
unused_vnodes_check_started()
{
	atomic_set(&sUnusedVnodesCheckCount, kUnusedVnodesCheckInterval + 1);
}


static inline void
unused_vnodes_check_done()
{
	atomic_set(&sUnusedVnodesCheckCount, 0);
}


#endif	// UNUSED_VNODES_H
