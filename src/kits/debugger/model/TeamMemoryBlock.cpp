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
 *   Copyright 2011, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file TeamMemoryBlock.cpp
 * @brief Implementation of TeamMemoryBlock and its Listener interface.
 *
 * TeamMemoryBlock caches a fixed-size window of a debugged team's memory
 * along with the listener subscriptions used to notify observers when the
 * cached page becomes valid or fails to load. The block is owned by a
 * TeamMemoryBlockManager via TeamMemoryBlockOwner; its last-reference
 * release routes back through the owner so the manager can purge it from
 * its cache.
 */


#include "TeamMemoryBlock.h"


#include <AutoLocker.h>

#include "TeamMemoryBlockManager.h"


// #pragma mark - TeamMemoryBlock


/**
 * @brief Constructs an invalid block at @a baseAddress under @a owner.
 *
 * The block starts invalid until @c MarkValid() is called by the manager
 * after a successful load.
 *
 * @param baseAddress Target-space base address of the cached window.
 * @param owner       Owning manager hook; deleted by the destructor.
 */
TeamMemoryBlock::TeamMemoryBlock(target_addr_t baseAddress,
	TeamMemoryBlockOwner* owner)
	:
	fValid(false),
	fWritable(false),
	fBaseAddress(baseAddress),
	fBlockOwner(owner)
{
}


/**
 * @brief Deletes the owner hook on destruction.
 */
TeamMemoryBlock::~TeamMemoryBlock()
{
	delete fBlockOwner;
}


/**
 * @brief Subscribes @a listener for retrieval-result notifications.
 *
 * @param listener Listener to add. Caller retains ownership.
 */
void
TeamMemoryBlock::AddListener(Listener* listener)
{
	AutoLocker<BLocker> lock(fLock);
	fListeners.Add(listener);
}


/**
 * @brief Reports whether @a listener is currently subscribed.
 *
 * @param listener Listener to test for.
 * @return        True if @a listener is in the subscription list.
 */
bool
TeamMemoryBlock::HasListener(Listener* listener)
{
	AutoLocker<BLocker> lock(fLock);
	ListenerList::Iterator iterator = fListeners.GetIterator();
	while (iterator.HasNext()) {
		if (iterator.Next() == listener)
			return true;
	}

	return false;
}


/**
 * @brief Removes a previously subscribed listener.
 *
 * @param listener Listener to detach.
 */
void
TeamMemoryBlock::RemoveListener(Listener* listener)
{
	AutoLocker<BLocker> lock(fLock);
	fListeners.Remove(listener);
}


/**
 * @brief Marks the cached data valid and notifies subscribers.
 */
void
TeamMemoryBlock::MarkValid()
{
	fValid = true;
	NotifyDataRetrieved();
}


/**
 * @brief Resets the block to the invalid state without notifying listeners.
 */
void
TeamMemoryBlock::Invalidate()
{
	fValid = false;
}


/**
 * @brief Tests whether @a address falls within the valid cached window.
 *
 * @param address Target-space address to test.
 * @return       True if the block is valid and @a address is in range.
 */
bool
TeamMemoryBlock::Contains(target_addr_t address) const
{
	return fValid && address >= fBaseAddress
		&& address < (fBaseAddress + sizeof(fData));
}


/**
 * @brief Records whether the cached region is writable in the target.
 *
 * @param writable True if writes from the debugger should be allowed.
 */
void
TeamMemoryBlock::SetWritable(bool writable)
{
	fWritable = writable;
}


/**
 * @brief Notifies subscribed listeners of a retrieval result.
 *
 * @param result @c B_OK to dispatch a successful retrieval; otherwise the
 *               error code dispatched to the failure callback.
 */
void
TeamMemoryBlock::NotifyDataRetrieved(status_t result)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		if (result == B_OK)
			listener->MemoryBlockRetrieved(this);
		else
			listener->MemoryBlockRetrievalFailed(this, result);
	}
}


/**
 * @brief BReferenceable hook: detaches the block from its owner and self-deletes.
 */
void
TeamMemoryBlock::LastReferenceReleased()
{
	fBlockOwner->RemoveBlock(this);

	delete this;
}


// #pragma mark - TeamMemoryBlock


/**
 * @brief Virtual destructor anchor for the Listener interface.
 */
TeamMemoryBlock::Listener::~Listener()
{
}


/**
 * @brief Default no-op implementation invoked when a retrieval succeeds.
 *
 * @param block Block whose data is now valid (unused in default impl).
 */
void
TeamMemoryBlock::Listener::MemoryBlockRetrieved(TeamMemoryBlock* block)
{
}


/**
 * @brief Default no-op implementation invoked when a retrieval fails.
 *
 * @param block  Block whose retrieval failed (unused in default impl).
 * @param result Failure status code (unused in default impl).
 */
void
TeamMemoryBlock::Listener::MemoryBlockRetrievalFailed(TeamMemoryBlock* block,
	status_t result)
{
}
