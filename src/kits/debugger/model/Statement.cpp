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


/**
 * @file Statement.cpp
 * @brief Implementation of Statement, AbstractStatement, and ContiguousStatement.
 *
 * A Statement maps a source-level statement boundary to one or more
 * target-address ranges. AbstractStatement adds the source location to
 * the abstract base; ContiguousStatement narrows that further to a single
 * contiguous address range, the common case for most compilers.
 */

#include "Statement.h"


// #pragma mark - Statement


/**
 * @brief Virtual destructor anchor for the Statement interface.
 */
Statement::~Statement()
{
}


// #pragma mark - AbstractStatement


/**
 * @brief Constructs an AbstractStatement anchored at @a start.
 *
 * @param start Source-level location of the statement's first character.
 */
AbstractStatement::AbstractStatement(const SourceLocation& start)
	:
	fStart(start)
{
}


/**
 * @brief Returns the source location stored at construction time.
 *
 * @return The starting SourceLocation.
 */
SourceLocation
AbstractStatement::StartSourceLocation() const
{
	return fStart;
}


// #pragma mark - ContiguousStatement


/**
 * @brief Constructs a ContiguousStatement covering one address range.
 *
 * @param start Source-level location of the statement.
 * @param range Single contiguous target-address range covering the statement.
 */
ContiguousStatement::ContiguousStatement(const SourceLocation& start,
	const TargetAddressRange& range)
	:
	AbstractStatement(start),
	fRange(range)
{
}


/**
 * @brief Returns the address range that covers the entire statement.
 *
 * @return The single contiguous range.
 */
TargetAddressRange
ContiguousStatement::CoveringAddressRange() const
{
	return fRange;
}


/**
 * @brief Returns the number of address ranges in this statement.
 *
 * @return Always 1 for ContiguousStatement.
 */
int32
ContiguousStatement::CountAddressRanges() const
{
	return 1;
}


/**
 * @brief Returns the @a index'th address range, or an empty range out of bounds.
 *
 * @param index Zero-based range index.
 * @return     The range at index 0; empty range otherwise.
 */
TargetAddressRange
ContiguousStatement::AddressRangeAt(int32 index) const
{
	return index == 0 ? fRange : TargetAddressRange();
}


/**
 * @brief Tests whether @a address lies within the statement's range.
 *
 * @param address Target-space address to test.
 * @return       True if @a address is in the covering range.
 */
bool
ContiguousStatement::ContainsAddress(target_addr_t address) const
{
	return fRange.Contains(address);
}
