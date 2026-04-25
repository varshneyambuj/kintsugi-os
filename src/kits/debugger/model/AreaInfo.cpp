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
 *   Copyright 2013, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file AreaInfo.cpp
 * @brief Implementation of AreaInfo, a value-type description of one
 *        memory area belonging to a debugged team.
 *
 * AreaInfo captures the area's kernel identifiers, base address and size,
 * resident size, locking flags, and protection bits. The debugger uses it
 * to populate the memory-areas inspector and to validate addresses
 * against mapped regions.
 */


#include "AreaInfo.h"


/**
 * @brief Constructs an empty AreaInfo with invalid identifiers.
 */
AreaInfo::AreaInfo()
	:
	fTeam(-1),
	fArea(-1),
	fName(),
	fAddress(0),
	fSize(0),
	fRamSize(0),
	fLock(0),
	fProtection(0)
{
}


/**
 * @brief Copy-constructs from another AreaInfo.
 *
 * @param other Source instance to copy.
 */
AreaInfo::AreaInfo(const AreaInfo &other)
	:
	fTeam(other.fTeam),
	fArea(other.fArea),
	fName(other.fName),
	fAddress(other.fAddress),
	fSize(other.fSize),
	fRamSize(other.fRamSize),
	fLock(other.fLock),
	fProtection(other.fProtection)
{
}


/**
 * @brief Constructs a fully-populated AreaInfo.
 *
 * @param team       Owning team identifier.
 * @param area       Kernel area identifier.
 * @param name       Human-readable area name.
 * @param address    Base address of the area in target space.
 * @param size       Virtual size of the area in bytes.
 * @param ramSize    Resident size in bytes.
 * @param lock       Locking flags as defined by @c B_*_LOCK.
 * @param protection Protection flags as defined by @c B_*_AREA.
 */
AreaInfo::AreaInfo(team_id team, area_id area, const BString& name,
	target_addr_t address, target_size_t size, target_size_t ramSize,
	uint32 lock, uint32 protection)
	:
	fTeam(team),
	fArea(area),
	fName(name),
	fAddress(address),
	fSize(size),
	fRamSize(ramSize),
	fLock(lock),
	fProtection(protection)
{
}


/**
 * @brief Replaces all fields with new values.
 *
 * @param team       Owning team identifier.
 * @param area       Kernel area identifier.
 * @param name       Human-readable area name.
 * @param address    Base address of the area in target space.
 * @param size       Virtual size of the area in bytes.
 * @param ramSize    Resident size in bytes.
 * @param lock       Locking flags as defined by @c B_*_LOCK.
 * @param protection Protection flags as defined by @c B_*_AREA.
 */
void
AreaInfo::SetTo(team_id team, area_id area, const BString& name,
	target_addr_t address, target_size_t size, target_size_t ramSize,
	uint32 lock, uint32 protection)
{
	fTeam = team;
	fArea = area;
	fName = name;
	fAddress = address;
	fSize = size;
	fRamSize = ramSize;
	fLock = lock;
	fProtection = protection;
}
