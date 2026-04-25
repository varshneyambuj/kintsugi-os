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
 *   Copyright 2010-2017, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ImageDebugInfo.cpp
 * @brief Implementation of ImageDebugInfo, the per-image aggregate of all
 *        SpecificImageDebugInfo backends and their derived FunctionInstance
 *        list.
 *
 * ImageDebugInfo merges symbol-table data and zero or more
 * SpecificImageDebugInfo backends (DWARF, debugger, ...) into a single
 * uniform view. It owns FunctionInstance objects keyed by address,
 * forwards type lookups to backends in priority order, and exposes the
 * main function when one of the backends identified it.
 *
 * @see SpecificImageDebugInfo, FunctionInstance
 */


#include "ImageDebugInfo.h"

#include <new>

#include "DebuggerInterface.h"
#include "FunctionDebugInfo.h"
#include "FunctionInstance.h"
#include "SpecificImageDebugInfo.h"
#include "SymbolInfo.h"


/**
 * @brief Constructs an empty ImageDebugInfo for the given image.
 *
 * @param imageInfo  Identity of the image whose debug info is being
 *                   collected.
 */
ImageDebugInfo::ImageDebugInfo(const ImageInfo& imageInfo)
	:
	fImageInfo(imageInfo),
	fMainFunction(NULL)
{
}


/**
 * @brief Destroys the aggregate, releasing all FunctionInstance and
 *        SpecificImageDebugInfo references.
 */
ImageDebugInfo::~ImageDebugInfo()
{
	for (int32 i = 0; FunctionInstance* function = fFunctions.ItemAt(i); i++)
		function->ReleaseReference();

	for (int32 i = 0; SpecificImageDebugInfo* info = fSpecificInfos.ItemAt(i);
			i++) {
		info->ReleaseReference();
	}
}


/**
 * @brief Appends a SpecificImageDebugInfo backend to the aggregate.
 *
 * On success the caller's reference is taken over by this object.
 *
 * @param info  Backend instance to register.
 * @return @c true on success; @c false if appending failed.
 */
bool
ImageDebugInfo::AddSpecificInfo(SpecificImageDebugInfo* info)
{
	// NB: on success we take over the caller's reference to the info object
	return fSpecificInfos.AddItem(info);
}


/**
 * @brief Completes initialization by enumerating functions across all
 *        registered backends.
 *
 * Symbols are fetched once from the live target and sorted by address.
 * For each backend in priority order, GetFunctions() is asked to materialize
 * descriptors; only addresses not already covered by a higher-priority
 * backend are added. The main function (if any) is recorded.
 *
 * @param interface  Debugger interface used to fetch symbol info.
 * @retval B_OK         Initialization succeeded.
 * @retval B_NO_MEMORY  Allocation failure during instance creation.
 * @retval other        Errors propagated from a backend's GetFunctions().
 */
status_t
ImageDebugInfo::FinishInit(DebuggerInterface* interface)
{
	BObjectList<SymbolInfo, true> symbols(50);
	status_t error = interface->GetSymbolInfos(fImageInfo.TeamID(),
		fImageInfo.ImageID(), symbols);
	if (error != B_OK)
		return error;
	symbols.SortItems(&_CompareSymbols);

	// get functions -- get them from most expressive debug info first and add
	// missing functions from less expressive debug infos
	for (int32 i = 0; SpecificImageDebugInfo* specificInfo
			= fSpecificInfos.ItemAt(i); i++) {
		BObjectList<FunctionDebugInfo> functions;
		error = specificInfo->GetFunctions(symbols, functions);
		if (error != B_OK)
			return error;

		for (int32 k = 0; FunctionDebugInfo* function = functions.ItemAt(k);
				k++) {
			if (FunctionAtAddress(function->Address()) != NULL)
				continue;

			FunctionInstance* instance = new(std::nothrow) FunctionInstance(
				this, function);
			if (instance == NULL
				|| !fFunctions.BinaryInsert(instance, &_CompareFunctions)) {
				delete instance;
				error = B_NO_MEMORY;
				break;
			}

			if (function->IsMain())
				fMainFunction = instance;
		}

		// Remove references returned by the specific debug info -- the
		// FunctionInstance objects have references, now.
		for (int32 k = 0; FunctionDebugInfo* function = functions.ItemAt(k);
				k++) {
			function->ReleaseReference();
		}

		if (error != B_OK)
			return error;
	}

	return B_OK;
}


/**
 * @brief Resolves a type by name across all registered backends.
 *
 * Backends are queried in priority order until one returns @c B_OK or
 * @c B_NO_MEMORY (the latter is returned to the caller as-is). All other
 * errors continue the search.
 *
 * @param cache       Type cache to populate.
 * @param name        Type name (canonical form).
 * @param constraints Optional kind/subkind constraints.
 * @param _type       Out parameter receiving the resolved type.
 * @retval B_OK              A type was found and returned.
 * @retval B_NO_MEMORY       Allocation failure inside a backend.
 * @retval B_ENTRY_NOT_FOUND No backend could resolve @a name.
 */
status_t
ImageDebugInfo::GetType(GlobalTypeCache* cache, const BString& name,
	const TypeLookupConstraints& constraints, Type*& _type)
{
	for (int32 i = 0; SpecificImageDebugInfo* specificInfo
			= fSpecificInfos.ItemAt(i); i++) {
		status_t error = specificInfo->GetType(cache, name, constraints,
			_type);
		if (error == B_OK || error == B_NO_MEMORY)
			return error;
	}

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Reports whether any backend has a type matching @a name.
 *
 * @param name        Type name (canonical form).
 * @param constraints Optional kind/subkind constraints.
 * @return @c true as soon as any backend reports a match.
 */
bool
ImageDebugInfo::HasType(const BString& name,
	const TypeLookupConstraints& constraints) const
{
	for (int32 i = 0; SpecificImageDebugInfo* specificInfo
			= fSpecificInfos.ItemAt(i); i++) {
		if (specificInfo->HasType(name, constraints))
			return true;
	}

	return false;
}


/**
 * @brief Classifies an image-relative address into a section type.
 *
 * Asks each backend in turn until one returns a non-unknown classification.
 *
 * @param address  Image-relative address to classify.
 * @return The first non-unknown AddressSectionType, or
 *         @c ADDRESS_SECTION_TYPE_UNKNOWN.
 */
AddressSectionType
ImageDebugInfo::GetAddressSectionType(target_addr_t address) const
{
	AddressSectionType type = ADDRESS_SECTION_TYPE_UNKNOWN;
	for (int32 i = 0; SpecificImageDebugInfo* specificInfo
			= fSpecificInfos.ItemAt(i); i++) {
		type = specificInfo->GetAddressSectionType(address);
		if (type != ADDRESS_SECTION_TYPE_UNKNOWN)
			break;
	}

	return type;
}


/**
 * @brief Returns the number of FunctionInstance objects materialized.
 *
 * @return Count of function entries.
 */
int32
ImageDebugInfo::CountFunctions() const
{
	return fFunctions.CountItems();
}


/**
 * @brief Returns the function at a given index.
 *
 * @param index  Zero-based index into the function list.
 * @return Borrowed pointer to the FunctionInstance, or @c NULL when out of
 *         range.
 */
FunctionInstance*
ImageDebugInfo::FunctionAt(int32 index) const
{
	return fFunctions.ItemAt(index);
}


/**
 * @brief Locates the function that covers a given runtime address.
 *
 * @param address  Image-relative address to look up.
 * @return Pointer to the covering FunctionInstance, or @c NULL.
 */
FunctionInstance*
ImageDebugInfo::FunctionAtAddress(target_addr_t address) const
{
	return fFunctions.BinarySearchByKey(address, &_CompareAddressFunction);
}


/**
 * @brief Looks up a function by exact name.
 *
 * @param name  Function name to search for.
 * @return Pointer to the first matching FunctionInstance, or @c NULL.
 * @note   Linear scan; not optimal for large images.
 */
FunctionInstance*
ImageDebugInfo::FunctionByName(const char* name) const
{
	// TODO: Not really optimal.
	for (int32 i = 0; FunctionInstance* function = fFunctions.ItemAt(i); i++) {
		if (function->Name() == name)
			return function;
	}

	return NULL;
}


/**
 * @brief Annotates a FileSourceCode with statement and line data from
 *        every backend that knows the file.
 *
 * Backends that do not cover the file return @c B_ENTRY_NOT_FOUND, which is
 * tolerated. Any backend reporting @c B_NO_MEMORY aborts the whole call.
 *
 * @param file        Source file to annotate.
 * @param sourceCode  FileSourceCode to populate.
 * @retval B_OK              At least one backend contributed data.
 * @retval B_ENTRY_NOT_FOUND No backend covered @a file.
 * @retval B_NO_MEMORY       Allocation failure inside a backend.
 */
status_t
ImageDebugInfo::AddSourceCodeInfo(LocatableFile* file,
	FileSourceCode* sourceCode) const
{
	bool addedAny = false;
	for (int32 i = 0; SpecificImageDebugInfo* specificInfo
			= fSpecificInfos.ItemAt(i); i++) {
		status_t error = specificInfo->AddSourceCodeInfo(file, sourceCode);
		if (error == B_NO_MEMORY)
			return error;
		addedAny |= error == B_OK;
	}

	return addedAny ? B_OK : B_ENTRY_NOT_FOUND;
}


/**
 * @brief Compares two FunctionInstance objects by start address.
 *
 * @param a  First instance.
 * @param b  Second instance.
 * @return -1, 0, or 1 according to address ordering.
 */
/*static*/ int
ImageDebugInfo::_CompareFunctions(const FunctionInstance* a,
	const FunctionInstance* b)
{
	return a->Address() < b->Address()
		? -1 : (a->Address() == b->Address() ? 0 : 1);
}


/**
 * @brief Compares an address against a function's [start, end) range.
 *
 * @param address   Address to test.
 * @param function  Function to test against.
 * @return -1 if before, 0 if within, 1 if after the range.
 */
/*static*/ int
ImageDebugInfo::_CompareAddressFunction(const target_addr_t* address,
	const FunctionInstance* function)
{
	if (*address < function->Address())
		return -1;
	return *address < function->Address() + function->Size() ? 0 : 1;
}


/**
 * @brief Compares two SymbolInfo records by start address (sorted view).
 *
 * @param a  First symbol.
 * @param b  Second symbol.
 * @return -1, 0, or 1 according to address ordering.
 */
/*static*/ int
ImageDebugInfo::_CompareSymbols(const SymbolInfo* a, const SymbolInfo* b)
{
	return a->Address() < b->Address()
		? -1 : (a->Address() == b->Address() ? 0 : 1);
}
