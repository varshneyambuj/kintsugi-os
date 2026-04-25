/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2011, Rene Gollent, rene@gollent.com.
 */

/** @file TeamMemoryBlockManager.h
    @brief Cache manager for TeamMemoryBlock instances retrieved from a debugged team. */

#ifndef TEAM_MEMORY_BLOCK_MANAGER_H
#define TEAM_MEMORY_BLOCK_MANAGER_H


#include <Locker.h>
#include <Referenceable.h>
#include <util/DoublyLinkedList.h>
#include <util/OpenHashTable.h>

#include "Types.h"


struct MemoryBlockHashDefinition;
class TeamMemoryBlock;


/** @brief Owns a hash-indexed cache of TeamMemoryBlock objects keyed by base
           address, recycling live blocks to share data between callers. */
class TeamMemoryBlockManager
{
public:
								TeamMemoryBlockManager();
								~TeamMemoryBlockManager();

		status_t				Init();

		TeamMemoryBlock*		GetMemoryBlock(target_addr_t address);

private:
		struct Key;
		struct MemoryBlockEntry;
		struct MemoryBlockHashDefinition;
		typedef BOpenHashTable<MemoryBlockHashDefinition> MemoryBlockTable;
		typedef DoublyLinkedList<TeamMemoryBlock> DeadBlockTable;

private:
		void					_Cleanup();
		void					_MarkDeadBlock(target_addr_t address);
		void					_RemoveBlock(target_addr_t address);

private:
		friend class TeamMemoryBlockOwner;

private:
		BLocker					fLock;
		MemoryBlockTable*		fActiveBlocks;
		DeadBlockTable*			fDeadBlocks;
};


/** @brief Per-consumer handle that participates in TeamMemoryBlockManager
           lifetime tracking so blocks can notify their owners on retire. */
class TeamMemoryBlockOwner
{
public:
								TeamMemoryBlockOwner(
									TeamMemoryBlockManager* manager);
								~TeamMemoryBlockOwner();

		void					RemoveBlock(TeamMemoryBlock* block);

private:
	TeamMemoryBlockManager* 	fBlockManager;
};


#endif // TEAM_MEMORY_BLOCK_MANAGER_H
