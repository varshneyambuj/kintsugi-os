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
 * @file SymbolInfo.cpp
 * @brief Implementation of SymbolInfo, a value-type record describing one
 *        symbol resolved from an image's symbol table.
 *
 * SymbolInfo bundles the address, size, type, and name of a symbol so that
 * higher-level lookups and stack-trace formatters can pass the data around
 * without referring back into the underlying ELF tables.
 */

#include "SymbolInfo.h"


/**
 * @brief Constructs an empty SymbolInfo with zero address, size, and type.
 */
SymbolInfo::SymbolInfo()
	:
	fAddress(0),
	fSize(0),
	fType(0),
	fName()
{
}


/**
 * @brief Constructs a fully-initialised SymbolInfo.
 *
 * @param address Target-space address of the symbol.
 * @param size    Size of the symbol in bytes.
 * @param type    Symbol type as encoded by the ELF symbol table.
 * @param name    Demangled or raw symbol name.
 */
SymbolInfo::SymbolInfo(target_addr_t address, target_size_t size, uint32 type,
	const BString& name)
	:
	fAddress(address),
	fSize(size),
	fType(type),
	fName(name)
{
}


/**
 * @brief Destroys the SymbolInfo. The contained BString manages its own buffer.
 */
SymbolInfo::~SymbolInfo()
{
}


/**
 * @brief Resets all fields of the SymbolInfo to a new value.
 *
 * @param address Target-space address of the symbol.
 * @param size    Size of the symbol in bytes.
 * @param type    Symbol type as encoded by the ELF symbol table.
 * @param name    Demangled or raw symbol name.
 */
void
SymbolInfo::SetTo(target_addr_t address, target_size_t size, uint32 type,
	const BString& name)
{
	fAddress = address;
	fSize = size;
	fType = type;
	fName = name;
}
