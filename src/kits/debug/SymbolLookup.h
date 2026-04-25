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
 * MIT License. Copyright 2005-2009, Ingo Weinhold; Copyright 2013, Rene Gollent.
 */

/** @file SymbolLookup.h
    @brief Cross-team symbol resolution helpers backed by remote memory
           reads through a debug_context. */

#ifndef SYMBOL_LOOKUP_H
#define SYMBOL_LOOKUP_H

#include <stdio.h>

#include <image.h>
#include <OS.h>

#include <util/DoublyLinkedList.h>


struct debug_context;
struct image_t;
struct runtime_loader_debug_area;


namespace BPrivate {
namespace Debug {

class Image;


/** @brief Lightweight throwable carrying a status_t error from
 *         RemoteMemoryAccessor and friends out to the calling code. */
class Exception {
public:
	/** @brief Constructs an Exception wrapping the given error code. */
	Exception(status_t error)
		: fError(error)
	{
	}

	/** @brief Copy constructor. */
	Exception(const Exception &other)
		: fError(other.fError)
	{
	}

	/** @brief Returns the wrapped status_t error code. */
	status_t Error() const	{ return fError; }

private:
	status_t	fError;
};


/** @brief Locally-cloned slice of a remote team's address space, used by
 *         RemoteMemoryAccessor to read symbol tables and image data. */
class Area : public DoublyLinkedListLinkImpl<Area> {
public:
	/** @brief Constructs an Area mapping a remote range to a local clone.
	 *
	 *  @param localID       Local area_id of the cloned area, or -1 if none.
	 *  @param remoteAddress Base address in the target team.
	 *  @param localAddress  Base address of the local clone.
	 *  @param size          Length of the mapped range in bytes.
	 */
	Area(area_id localID, addr_t remoteAddress, const void* localAddress, int32 size)
		: fLocalID(localID),
		  fRemoteAddress(remoteAddress),
		  fLocalAddress(localAddress),
		  fSize(size)
	{
	}

	/** @brief Destroys the Area and releases the underlying clone if owned. */
	~Area()
	{
		if (fLocalID >= 0)
			delete_area(fLocalID);
	}

	/** @brief Returns the base address of the Area in the remote team. */
	addr_t		RemoteAddress() const	{ return fRemoteAddress; }
	/** @brief Returns the base address of the locally cloned mapping. */
	const void* LocalAddress() const	{ return fLocalAddress; }
	/** @brief Returns the size of the Area in bytes. */
	int32 Size() const					{ return fSize; }

	/** @brief Tests whether the remote range [@a address, @a address + @a size)
	 *         fits inside this Area. */
	bool ContainsAddress(const void *address, int32 size) const
	{
		return (fRemoteAddress <= (addr_t)address
			&& (addr_t)address + size <= (fRemoteAddress + fSize));
	}

	/** @brief Tests whether @a address is inside the local clone of this Area. */
	bool ContainsLocalAddress(const void* address) const
	{
		return (addr_t)address >= (addr_t)fLocalAddress
			&& (addr_t)address < (addr_t)fLocalAddress + fSize;
	}

	const void *TranslateAddress(const void *remoteAddress);

private:
	area_id		fLocalID;
	addr_t		fRemoteAddress;
	const void	*fLocalAddress;
	int32		fSize;
};


/** @brief Caches Area clones so that callers can read remote memory through
 *         ordinary pointer dereferences instead of debug_read_memory(). */
class RemoteMemoryAccessor {
public:
	RemoteMemoryAccessor(debug_context* debugContext);
	~RemoteMemoryAccessor();

	status_t InitCheck() const;

	const void *PrepareAddress(const void *remoteAddress, int32 size);
	const void *PrepareAddressNoThrow(const void *remoteAddress,
		int32 size);

	/** @brief Returns a reference to remote data of type @c Type, transparently
	 *         cloning the backing area on first access. */
	template<typename Type> inline const Type &Read(
		const Type &remoteData)
	{
		const void *remoteAddress = &remoteData;
		const void *localAddress = PrepareAddress(remoteAddress,
			sizeof(remoteData));
		return *(const Type*)localAddress;
	}

	Area* AreaForLocalAddress(const void* address) const;

private:
	Area& _GetArea(const void *address, int32 size);
	status_t _GetAreaNoThrow(const void *address, int32 size, Area *&_area);

	typedef DoublyLinkedList<Area>	AreaList;

protected:
	debug_context* fDebugContext;

private:
	AreaList	fAreas;
};


/** @brief Cursor used by SymbolLookup::NextSymbol() to walk an Image's
 *         symbol table one entry at a time. */
struct SymbolIterator {
	const Image*		image;
	int32				currentIndex;
};


/** @brief Resolves addresses in a debugged team to symbol names by inspecting
 *         the runtime loader's debug area and per-image symbol tables. */
class SymbolLookup : private RemoteMemoryAccessor {
public:
	SymbolLookup(debug_context* debugContext, image_id image);
	~SymbolLookup();

	status_t Init();

	status_t LookupSymbolAddress(addr_t address, addr_t *_baseAddress,
		const char **_symbolName, size_t *_symbolNameLen,
		const char **_imageName, bool *_exactMatch) const;

	status_t InitSymbolIterator(image_id imageID,
		SymbolIterator& iterator) const;
	status_t InitSymbolIteratorByAddress(addr_t address,
		SymbolIterator& iterator) const;
	status_t NextSymbol(SymbolIterator& iterator, const char** _symbolName,
		size_t* _symbolNameLen, addr_t* _symbolAddress, size_t* _symbolSize,
		int32* _symbolType) const;

	status_t GetSymbol(image_id imageID, const char* name, int32 symbolType,
		void** _symbolLocation, size_t* _symbolSize, int32* _symbolType) const;

private:
	class LoadedImage;
	friend class LoadedImage;

private:
	const image_t* _FindLoadedImageAtAddress(addr_t address);
	const image_t* _FindLoadedImageByID(image_id id);
	Image* _FindImageAtAddress(addr_t address) const;
	Image* _FindImageByID(image_id id) const;
	size_t _SymbolNameLen(const char* address) const;
	status_t _LoadImageInfo(const image_info& imageInfo);

private:
	const runtime_loader_debug_area	*fDebugArea;
	DoublyLinkedList<Image>	fImages;
	image_id fImageID;
};

}	// namespace Debug
}	// namespace BPrivate

using BPrivate::Debug::SymbolLookup;

#endif	// SYMBOL_LOOKUP_H
