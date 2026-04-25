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
 * @file BasicFunctionDebugInfo.cpp
 * @brief Implementation of BasicFunctionDebugInfo, a minimal symbol-derived
 *        function descriptor used when no richer debug info is available.
 *
 * BasicFunctionDebugInfo wraps the address, size, and (optionally demangled)
 * name of a single function discovered from an image's symbol table. It is
 * the fallback FunctionDebugInfo implementation produced when DWARF or other
 * structured debug data does not cover an address range.
 *
 * @see FunctionDebugInfo, SpecificImageDebugInfo
 */


#include "BasicFunctionDebugInfo.h"

#include "SpecificImageDebugInfo.h"


/**
 * @brief Constructs a basic function descriptor backed by symbol-table data.
 *
 * Acquires a reference on @a debugInfo so the owning SpecificImageDebugInfo
 * remains alive for the lifetime of this descriptor.
 *
 * @param debugInfo  The owning specific image debug info; reference acquired.
 * @param address    Image-relative start address of the function.
 * @param size       Length in bytes of the function's text range.
 * @param name       Raw (mangled) symbol name.
 * @param prettyName Demangled / human-readable name; may equal @a name.
 */
BasicFunctionDebugInfo::BasicFunctionDebugInfo(
	SpecificImageDebugInfo* debugInfo, target_addr_t address,
	target_size_t size, const BString& name, const BString& prettyName)
	:
	fImageDebugInfo(debugInfo),
	fAddress(address),
	fSize(size),
	fName(name),
	fPrettyName(prettyName)
{
	fImageDebugInfo->AcquireReference();
}


/**
 * @brief Destroys the descriptor, releasing its reference on the image info.
 */
BasicFunctionDebugInfo::~BasicFunctionDebugInfo()
{
	fImageDebugInfo->ReleaseReference();
}


/**
 * @brief Returns the SpecificImageDebugInfo that produced this descriptor.
 *
 * @return Borrowed pointer to the owning image debug info; not reference
 *         counted by the caller.
 */
SpecificImageDebugInfo*
BasicFunctionDebugInfo::GetSpecificImageDebugInfo() const
{
	return fImageDebugInfo;
}


/**
 * @brief Returns the function's image-relative start address.
 *
 * @return The address recorded at construction time.
 */
target_addr_t
BasicFunctionDebugInfo::Address() const
{
	return fAddress;
}


/**
 * @brief Returns the size of the function's text range in bytes.
 *
 * @return Byte count covering the function's instructions.
 */
target_size_t
BasicFunctionDebugInfo::Size() const
{
	return fSize;
}


/**
 * @brief Returns the raw (mangled) symbol name.
 *
 * @return Reference to the stored mangled name.
 */
const BString&
BasicFunctionDebugInfo::Name() const
{
	return fName;
}


/**
 * @brief Returns the demangled, human-readable name.
 *
 * @return Reference to the stored pretty name.
 */
const BString&
BasicFunctionDebugInfo::PrettyName() const
{
	return fPrettyName;
}


/**
 * @brief Reports whether this function is the program's entry point.
 *
 * @return Always false; symbol-only info cannot identify @c main.
 */
bool
BasicFunctionDebugInfo::IsMain() const
{
	return false;
}


/**
 * @brief Returns the source file declaring the function, if known.
 *
 * @return Always @c NULL; symbol info has no source mapping.
 */
LocatableFile*
BasicFunctionDebugInfo::SourceFile() const
{
	return NULL;
}


/**
 * @brief Returns the source location where the function begins.
 *
 * @return A default-constructed (invalid) SourceLocation.
 */
SourceLocation
BasicFunctionDebugInfo::SourceStartLocation() const
{
	return SourceLocation();
}


/**
 * @brief Returns the source location where the function ends.
 *
 * @return A default-constructed (invalid) SourceLocation.
 */
SourceLocation
BasicFunctionDebugInfo::SourceEndLocation() const
{
	return SourceLocation();
}
