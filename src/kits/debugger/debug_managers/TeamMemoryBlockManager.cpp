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
 * @file TeamMemoryBlockManager.cpp
 * @brief Cache implementation that hands out shared TeamMemoryBlock instances
 *        keyed by base address and recycles them as references drop to zero.
 *
 * Active blocks live in a hash table; once their last consumer drops them the
 * block is moved to a "dead" doubly-linked list so any in-flight operations
 * can still complete cleanly before the block is deleted by its owner.
 */


#include "TeamMemoryBlockManager.h"

#include <new>

#include <AutoDeleter.h>
#include <AutoLocker.h>

#include "TeamMemoryBlock.h"


/**
 * @brief Hash key wrapping a target-side base address.
 */
struct TeamMemoryBlockManager::Key {
	target_addr_t address;

	/** @brief Constructs a key for the given base address. */
	Key(target_addr_t address)
		:
		address(address)
	{
	}

	/** @brief Returns the hash value for this key (low 32 bits of the address). */
	uint32 HashValue() const
	{
		return (uint32)address;
	}

	/** @brief Equality compares the underlying address. */
	bool operator==(const Key& other) const
	{
		return address == other.address;
	}
};


/**
 * @brief Hash-table entry pairing a key with the live TeamMemoryBlock pointer.
 */
struct TeamMemoryBlockManager::MemoryBlockEntry : Key {
	TeamMemoryBlock*	block;
	MemoryBlockEntry*	next;

	/** @brief Wraps an existing block; the entry does not own the block. */
	MemoryBlockEntry(TeamMemoryBlock* block)
		:
		Key(block->BaseAddress()),
		block(block)
	{
	}

	/** @brief No-op destructor; block ownership lies elsewhere. */
	~MemoryBlockEntry()
	{
	}
};


/**
 * @brief Hash-table policy traits glue for the BOpenHashTable specialization.
 */
struct TeamMemoryBlockManager::MemoryBlockHashDefinition {
	typedef Key					KeyType;
	typedef	MemoryBlockEntry	ValueType;

	/** @brief Returns the hash for a freestanding key. */
	size_t HashKey(const Key& key) const
	{
		return key.HashValue();
	}

	/** @brief Returns the hash for a stored entry. */
	size_t Hash(const MemoryBlockEntry* value) const
	{
		return value->HashValue();
	}

	/** @brief Returns true if @a key matches the stored entry. */
	bool Compare(const Key& key, const MemoryBlockEntry* value) const
	{
		return key == *value;
	}

	/** @brief Provides chained-bucket linkage to the hash table. */
	MemoryBlockEntry*& GetLink(MemoryBlockEntry* value) const
	{
		return value->next;
	}
};


/**
 * @brief Default-constructs an uninitialized manager; Init() must follow.
 */
TeamMemoryBlockManager::TeamMemoryBlockManager()
	:
	fActiveBlocks(NULL),
	fDeadBlocks(NULL)
{
}


/**
 * @brief Destroys the manager after releasing the active hash table.
 */
TeamMemoryBlockManager::~TeamMemoryBlockManager()
{
	_Cleanup();
}


/**
 * @brief Allocates and initializes the active hash table and dead-block list.
 *
 * @return B_OK on success, B_NO_MEMORY if any allocation fails, or any error
 *         from BLocker::InitCheck() or the table's Init().
 */
status_t
TeamMemoryBlockManager::Init()
{
	status_t result = fLock.InitCheck();
	if (result != B_OK)
		return result;

	fActiveBlocks = new(std::nothrow) MemoryBlockTable();
	if (fActiveBlocks == NULL)
		return B_NO_MEMORY;
	ObjectDeleter<MemoryBlockTable> activeDeleter(fActiveBlocks);
	result = fActiveBlocks->Init();
	if (result != B_OK)
		return result;

	fDeadBlocks = new(std::nothrow) DeadBlockTable();
	if (fDeadBlocks == NULL)
		return B_NO_MEMORY;

	activeDeleter.Detach();

	return B_OK;
}


/**
 * @brief Retrieves (or constructs) the TeamMemoryBlock covering @a address.
 *
 * @a address is rounded down to the page boundary. If a live block exists,
 * the manager returns it after acquiring a reference; if it has zero
 * references already (i.e. about to be reclaimed), it is moved to the dead
 * list and a new block is allocated in its place.
 *
 * @param address  Target-space address inside the desired block.
 * @return Pointer to the matching TeamMemoryBlock with one reference held by
 *         the caller, or NULL on allocation failure.
 */
TeamMemoryBlock*
TeamMemoryBlockManager::GetMemoryBlock(target_addr_t address)
{
	AutoLocker<BLocker> lock(fLock);

	address &= ~(B_PAGE_SIZE - 1);
	MemoryBlockEntry* entry = fActiveBlocks->Lookup(address);
	if (entry != NULL) {
		if (entry->block->AcquireReference() != 0)
			return entry->block;

		// this block already had its last reference released,
		// move it to the dead list and create a new one instead.
		_MarkDeadBlock(address);
	}

	TeamMemoryBlockOwner* owner = new(std::nothrow) TeamMemoryBlockOwner(this);
	if (owner == NULL)
		return NULL;
	ObjectDeleter<TeamMemoryBlockOwner> ownerDeleter(owner);

	TeamMemoryBlock* block = new(std::nothrow) TeamMemoryBlock(address,
		owner);
	if (block == NULL)
		return NULL;
	ObjectDeleter<TeamMemoryBlock> blockDeleter(block);

	entry = new(std::nothrow) MemoryBlockEntry(block);
	if (entry == NULL)
		return NULL;

	ownerDeleter.Detach();
	blockDeleter.Detach();
	fActiveBlocks->Insert(entry);

	return entry->block;
}


/**
 * @brief Destroys all active hash entries and tears down the hash table.
 *
 * Does not delete the blocks themselves; their TeamMemoryBlockOwner is
 * responsible for that lifecycle.
 */
void
TeamMemoryBlockManager::_Cleanup()
{
	if (fActiveBlocks != NULL) {
		MemoryBlockEntry* entry = fActiveBlocks->Clear(true);

		while (entry != NULL) {
			MemoryBlockEntry* next = entry->next;
			delete entry;
			entry = next;
		}

		delete fActiveBlocks;
		fActiveBlocks = NULL;
	}
}


/**
 * @brief Moves the entry at @a address from the active table into the dead list.
 *
 * @param address  Page-aligned base address of the block to retire.
 */
void
TeamMemoryBlockManager::_MarkDeadBlock(target_addr_t address)
{
	MemoryBlockEntry* entry = fActiveBlocks->Lookup(address);
	if (entry != NULL) {
		fActiveBlocks->Remove(entry);
		fDeadBlocks->Insert(entry->block);
		delete entry;
	}
}


/**
 * @brief Drops the block at @a address from whichever list currently owns it.
 *
 * Called by TeamMemoryBlockOwner during its own teardown so the manager's
 * data structures stop referring to the about-to-be-deleted block.
 *
 * @param address  Page-aligned base address of the block being destroyed.
 */
void
TeamMemoryBlockManager::_RemoveBlock(target_addr_t address)
{
	AutoLocker<BLocker> lock(fLock);
	MemoryBlockEntry* entry = fActiveBlocks->Lookup(address);
	if (entry != NULL) {
		fActiveBlocks->Remove(entry);
		delete entry;
		return;
	}

	DeadBlockTable::Iterator iterator = fDeadBlocks->GetIterator();
	while (iterator.HasNext()) {
		TeamMemoryBlock* block = iterator.Next();
		if (block->BaseAddress() == address) {
			fDeadBlocks->Remove(block);
			break;
		}
	}
}


/**
 * @brief Wraps a TeamMemoryBlock with a back-pointer to its owning manager.
 *
 * @param manager  Manager that produced the block; not owned.
 */
TeamMemoryBlockOwner::TeamMemoryBlockOwner(TeamMemoryBlockManager* manager)
	:
	fBlockManager(manager)
{
}


/**
 * @brief Destructor; the manager removes its tracking entry separately via RemoveBlock().
 */
TeamMemoryBlockOwner::~TeamMemoryBlockOwner()
{
}


/**
 * @brief Notifies the manager that @a block is about to be destroyed.
 *
 * @param block  Block whose tracking entry should be discarded.
 */
void
TeamMemoryBlockOwner::RemoveBlock(TeamMemoryBlock* block)
{
	fBlockManager->_RemoveBlock(block->BaseAddress());
}
