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
 *   Copyright 2005-2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2013, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file SymbolLookup.cpp
 * @brief Remote-memory symbol resolution for the debug kit.
 *
 * Implements the RemoteMemoryAccessor helper, which clones areas from a
 * debugged team's address space into the debugger's address space on demand,
 * and the SymbolLookup class, which builds a list of Image objects for all
 * (or one) of the team's loaded images and provides address-to-symbol
 * resolution.
 *
 * @see Image, debug_support.h
 */


#include "SymbolLookup.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <new>

#include <debug_support.h>
#include <runtime_loader.h>
#include <syscalls.h>

#include "Image.h"


#undef TRACE
//#define TRACE_DEBUG_SYMBOL_LOOKUP
#ifdef TRACE_DEBUG_SYMBOL_LOOKUP
#	define TRACE(x) printf x
#else
#	define TRACE(x) ;
#endif


using namespace BPrivate::Debug;


/**
 * @brief Translate a remote address into the locally mapped equivalent.
 *
 * Converts an address in the remote team's virtual address space to the
 * corresponding address within the cloned area held by this object.
 *
 * @param address  Address in the remote team's address space.
 * @return The translated address valid in the caller's (debugger) address space.
 */
const void *
Area::TranslateAddress(const void *address)
{
	TRACE(("Area::TranslateAddress(%p): area: %" B_PRId32 "\n", address, fLocalID));

	// translate the address
	const void *result = (const void*)((addr_t)address - (addr_t)fRemoteAddress
		+ (addr_t)fLocalAddress);

	TRACE(("Area::TranslateAddress(%p) done: %p\n", address, result));

	return result;
}


// #pragma mark -


/**
 * @brief Construct a RemoteMemoryAccessor for the given debug context.
 *
 * @param debugContext  The initialised debug_context for the target team, or
 *                      NULL for file-only symbol lookup without live memory access.
 */
RemoteMemoryAccessor::RemoteMemoryAccessor(debug_context* debugContext)
	: fDebugContext(debugContext),
	  fAreas()
{
}

/**
 * @brief Destructor — deletes all cloned Area objects.
 */
RemoteMemoryAccessor::~RemoteMemoryAccessor()
{
	// delete the areas
	while (Area *area = fAreas.Head()) {
		fAreas.Remove(area);
		delete area;
	}
}


/**
 * @brief Check whether live remote memory access is possible.
 *
 * Returns B_OK only when a valid debug context with an open nub port is
 * present. File-based symbol lookup via ImageFile still works even when this
 * returns an error.
 *
 * @return B_OK if a nub port is available, B_NO_INIT otherwise.
 */
status_t
RemoteMemoryAccessor::InitCheck() const
{
	// If we don't have a debug context, then there's nothing we can do.
	// SymbolLookup's image file functionality will still be available, though.
	if (fDebugContext == NULL || fDebugContext->nub_port < 0)
		return B_NO_INIT;

	return B_OK;
}


/**
 * @brief Translate a remote address to a local pointer, throwing on failure.
 *
 * Ensures the area containing [remoteAddress, remoteAddress+size) is cloned
 * into the debugger's address space, then returns the translated pointer. If
 * the address is NULL or the clone fails, throws an Exception.
 *
 * @param remoteAddress  Address in the target team's address space.
 * @param size           Number of bytes that must be accessible.
 * @return Translated local pointer valid in the debugger's address space.
 * @note Throws BPrivate::Debug::Exception on error.
 */
const void *
RemoteMemoryAccessor::PrepareAddress(const void *remoteAddress,
	int32 size)
{
	TRACE(("RemoteMemoryAccessor::PrepareAddress(%p, %" B_PRId32 ")\n",
		remoteAddress, size));

	if (remoteAddress == NULL) {
		TRACE(("RemoteMemoryAccessor::PrepareAddress(): Got null address!\n"));
		throw Exception(B_BAD_VALUE);
	}

	return _GetArea(remoteAddress, size).TranslateAddress(remoteAddress);
}


/**
 * @brief Translate a remote address to a local pointer without throwing.
 *
 * Like PrepareAddress(), but returns NULL instead of throwing on failure.
 * Useful in hot paths where exception overhead is undesirable.
 *
 * @param remoteAddress  Address in the target team's address space.
 * @param size           Number of bytes that must be accessible.
 * @return Translated local pointer, or NULL on failure.
 */
const void *
RemoteMemoryAccessor::PrepareAddressNoThrow(const void *remoteAddress,
	int32 size)
{
	if (remoteAddress == NULL)
		return NULL;

	Area* area;
	status_t status = _GetAreaNoThrow(remoteAddress, size, area);
	if (status != B_OK)
		return NULL;

	return area->TranslateAddress(remoteAddress);
}


/**
 * @brief Find the locally mapped Area that covers the given local address.
 *
 * @param address  Address in the debugger's local address space (after translation).
 * @return The matching Area, or NULL if no cloned area contains @a address.
 */
Area*
RemoteMemoryAccessor::AreaForLocalAddress(const void* address) const
{
	if (address == NULL)
		return NULL;

	for (AreaList::ConstIterator it = fAreas.GetIterator(); it.HasNext();) {
		Area* area = it.Next();
		if (area->ContainsLocalAddress(address))
			return area;
	}

	return NULL;
}


/**
 * @brief Obtain the Area covering a remote address range, throwing on failure.
 *
 * Delegates to _GetAreaNoThrow(); on error throws Exception with the status.
 *
 * @param address  Start address in the remote team.
 * @param size     Number of bytes required.
 * @return Reference to the covering Area.
 * @note Throws BPrivate::Debug::Exception on failure.
 */
Area &
RemoteMemoryAccessor::_GetArea(const void *address, int32 size)
{
	TRACE(("RemoteMemoryAccessor::_GetArea(%p, %" B_PRId32 ")\n", address,
		size));

	Area* area;
	status_t status = _GetAreaNoThrow(address, size, area);
	if (status != B_OK) {
		TRACE(("RemoteMemoryAccessor::_GetArea(): Failed to get address %p\n",
			address));
		throw Exception(status);
	}

	return *area;
}


/**
 * @brief Obtain the Area covering a remote address range without throwing.
 *
 * Searches the existing cloned area list first. On a miss, sends a
 * B_DEBUG_MESSAGE_CLONE_AREA request to the nub port, maps the resulting area,
 * and adds it to the list.
 *
 * @param address  Start address in the remote team.
 * @param size     Number of bytes required.
 * @param _area    Output — set to the covering Area on success.
 * @return B_OK on success; B_NO_INIT if no nub port; B_NO_MEMORY on allocation
 *         failure; or a nub error code.
 */
status_t
RemoteMemoryAccessor::_GetAreaNoThrow(const void *address, int32 size, Area *&_area)
{
	for (AreaList::Iterator it = fAreas.GetIterator(); it.HasNext();) {
		Area *area = it.Next();
		if (area->ContainsAddress(address, size)) {
			_area = area;
			return B_OK;
		}
	}

	if (InitCheck() != B_OK)
		return B_NO_INIT;

	// we need to clone a new area
	debug_nub_clone_area message;
	message.reply_port = fDebugContext->reply_port;
	message.address = address;

	debug_nub_clone_area_reply reply;
	status_t error = send_debug_message(fDebugContext, B_DEBUG_MESSAGE_CLONE_AREA,
		&message, sizeof(message), &reply, sizeof(reply));
	if (error != B_OK)
		return error;

	area_id localID = reply.area;
	if (localID < 0) {
		TRACE(("RemoteMemoryAccessor: Failed to clone area for %p: %s\n",
			address, strerror(localID)));
		return localID;
	}

	area_info areaInfo;
	error = get_area_info(localID, &areaInfo);
	if (error < 0) {
		TRACE(("RemoteMemoryAccessor: Failed to get info for %" B_PRId32
			": %s\n", localID, strerror(error)));
		return error;
	}

	const addr_t remoteBaseAddress = (addr_t)address
		- ((addr_t)reply.address - (addr_t)areaInfo.address);

	Area *area = new(std::nothrow) Area(localID,
		remoteBaseAddress, areaInfo.address, areaInfo.size);
	if (area == NULL)
		return B_NO_MEMORY;

	fAreas.Add(area);

	_area = area;
	return B_OK;
}


// #pragma mark -


/**
 * @brief Image subclass that resolves symbols via cloned remote memory.
 *
 * Falls back to reading the in-memory runtime symbol table when a backing ELF
 * file cannot be loaded or parsed.
 */
class SymbolLookup::LoadedImage : public Image {
public:
								LoadedImage(SymbolLookup* symbolLookup,
									const image_info& info,
									const image_t* image, int32 symbolCount);
	virtual						~LoadedImage();

	virtual	const elf_sym*		LookupSymbol(addr_t address,
									addr_t* _baseAddress,
									const char** _symbolName,
									size_t *_symbolNameLen,
									bool *_exactMatch) const;
	virtual	status_t			NextSymbol(int32& iterator,
									const char** _symbolName,
									size_t* _symbolNameLen,
									addr_t* _symbolAddress, size_t* _symbolSize,
									int32* _symbolType) const;

private:
			SymbolLookup*			fSymbolLookup;
			const image_t*			fImage;
			int32					fSymbolCount;
			size_t					fLoadDelta;
};


// #pragma mark -


/**
 * @brief Construct a SymbolLookup for the team referenced by @a debugContext.
 *
 * @param debugContext  An initialised debug_context, or NULL for file-only use.
 * @param image         The single image_id to restrict lookups to, or -1 to
 *                      load all images for the team.
 */
SymbolLookup::SymbolLookup(debug_context* debugContext, image_id image)
	:
	RemoteMemoryAccessor(debugContext),
	fDebugArea(NULL),
	fImages(),
	fImageID(image)
{
}


/**
 * @brief Destructor — deletes all Image objects in the image list.
 */
SymbolLookup::~SymbolLookup()
{
	while (Image* image = fImages.RemoveHead())
		delete image;
}


/**
 * @brief Load image metadata and prepare for symbol resolution.
 *
 * Attempts to locate the runtime loader debug area via area enumeration; if
 * found, the in-memory image list becomes available as a fallback. Then loads
 * Image objects for either the single fImageID or all images in the team.
 *
 * @return B_OK on success, or an error from _LoadImageInfo() on failure.
 */
status_t
SymbolLookup::Init()
{
	TRACE(("SymbolLookup::Init()\n"));

	status_t error = 0;

	if (RemoteMemoryAccessor::InitCheck() == B_OK) {
		TRACE(("SymbolLookup::Init(): searching debug area...\n"));

		// find the runtime loader debug area
		runtime_loader_debug_area *remoteDebugArea = NULL;
		ssize_t cookie = 0;
		area_info areaInfo;
		while (get_next_area_info(fDebugContext->team, &cookie, &areaInfo) == B_OK) {
			if (strcmp(areaInfo.name, RUNTIME_LOADER_DEBUG_AREA_NAME) == 0) {
				remoteDebugArea = (runtime_loader_debug_area*)areaInfo.address;
				break;
			}
		}

		if (remoteDebugArea) {
			TRACE(("SymbolLookup::Init(): found debug area, translating "
				"address...\n"));
		} else {
			TRACE(("SymbolLookup::Init(): Couldn't find debug area!\n"));
		}

		// translate the address
		try {
			if (remoteDebugArea != NULL) {
				fDebugArea = &Read(*remoteDebugArea);

				TRACE(("SymbolLookup::Init(): translated debug area is at: %p, "
					"loaded_images: %p\n", fDebugArea, fDebugArea->loaded_images));
			}
		} catch (Exception& exception) {
			// we can live without the debug area
		}
	}

	image_info imageInfo;
	if (fImageID < 0) {
		// create a list of the team's images
		int32 cookie = 0;
		while (get_next_image_info(fDebugContext->team, &cookie, &imageInfo) == B_OK) {
			error = _LoadImageInfo(imageInfo);
			if (error != B_OK)
				return error;
		}
	} else {
		error = get_image_info(fImageID, &imageInfo);
		if (error != B_OK)
			return error;

		error = _LoadImageInfo(imageInfo);
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


/**
 * @brief Resolve a runtime address to a symbol name and image.
 *
 * Finds the image that covers @a address, then performs a symbol lookup within
 * that image. If no containing symbol is found, the image's own text base and
 * name are returned as the best available information.
 *
 * @param address       Runtime address to resolve.
 * @param _baseAddress  Output — receives the symbol's load address.
 * @param _symbolName   Output — receives a pointer to the symbol name, or NULL.
 * @param _symbolNameLen Output — receives the symbol name length.
 * @param _imageName    Output — receives the image path string.
 * @param _exactMatch   Output — true when the address falls within the symbol.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no image covers @a address.
 */
status_t
SymbolLookup::LookupSymbolAddress(addr_t address, addr_t *_baseAddress,
	const char **_symbolName, size_t *_symbolNameLen, const char **_imageName,
	bool *_exactMatch) const
{
	TRACE(("SymbolLookup::LookupSymbolAddress(%p)\n", (void*)address));

	Image* image = _FindImageAtAddress(address);
	if (!image)
		return B_ENTRY_NOT_FOUND;

	if (_imageName != NULL)
		*_imageName = image->Name();

	const elf_sym* symbolFound = image->LookupSymbol(address, _baseAddress,
		_symbolName, _symbolNameLen, _exactMatch);

	TRACE(("SymbolLookup::LookupSymbolAddress(): done: symbol: %p, image name: "
		"%s, exact match: %d\n", symbolFound, image->Name(), _exactMatch ? *_exactMatch : -1));

	if (symbolFound != NULL)
		return B_OK;

	// symbol not found -- return the image itself

	if (_baseAddress)
		*_baseAddress = image->TextAddress();

	if (_imageName)
		*_imageName = image->Name();

	if (_symbolName)
		*_symbolName = NULL;

	if (_exactMatch)
		*_exactMatch = false;

	if (_symbolNameLen != NULL)
		*_symbolNameLen = 0;

	return B_OK;
}


/**
 * @brief Initialise a SymbolIterator for the image identified by @a imageID.
 *
 * @param imageID    The image_id to iterate.
 * @param iterator   Output — receives the initialised iterator state.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if the image is not loaded.
 */
status_t
SymbolLookup::InitSymbolIterator(image_id imageID,
	SymbolIterator& iterator) const
{
	TRACE(("SymbolLookup::InitSymbolIterator(): image ID: %" B_PRId32 "\n",
		imageID));

	// find the image
	iterator.image = _FindImageByID(imageID);

	// If that didn't work, find the loaded image.
	if (iterator.image == NULL) {
		TRACE(("SymbolLookup::InitSymbolIterator() done: image not "
			"found\n"));
		return B_ENTRY_NOT_FOUND;
	}

	iterator.currentIndex = -1;

	return B_OK;
}


/**
 * @brief Initialise a SymbolIterator by searching for the image at a text address.
 *
 * Used as a fallback when image IDs are stale (e.g. after fork()).
 *
 * @param address   Runtime address expected to fall within the image's text segment.
 * @param iterator  Output — receives the initialised iterator state.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no image covers @a address.
 */
status_t
SymbolLookup::InitSymbolIteratorByAddress(addr_t address,
	SymbolIterator& iterator) const
{
	TRACE(("SymbolLookup::InitSymbolIteratorByAddress(): base address: %#lx\n",
		address));

	// find the image
	iterator.image = _FindImageAtAddress(address);
	if (iterator.image == NULL) {
		TRACE(("SymbolLookup::InitSymbolIteratorByAddress() done: image "
			"not found\n"));
		return B_ENTRY_NOT_FOUND;
	}

	iterator.currentIndex = -1;

	return B_OK;
}


/**
 * @brief Advance an iterator and return the next symbol's attributes.
 *
 * Delegates to the underlying Image::NextSymbol() call.
 *
 * @param iterator       Iterator state; currentIndex is updated in-place.
 * @param _symbolName    Output — pointer to the symbol name.
 * @param _symbolNameLen Output — length of the symbol name.
 * @param _symbolAddress Output — runtime address of the symbol.
 * @param _symbolSize    Output — size of the symbol in bytes.
 * @param _symbolType    Output — B_SYMBOL_TYPE_TEXT or B_SYMBOL_TYPE_DATA.
 * @return B_OK when a symbol is returned, B_ENTRY_NOT_FOUND at end-of-table.
 */
status_t
SymbolLookup::NextSymbol(SymbolIterator& iterator, const char** _symbolName,
	size_t* _symbolNameLen, addr_t* _symbolAddress, size_t* _symbolSize,
	int32* _symbolType) const
{
	return iterator.image->NextSymbol(iterator.currentIndex, _symbolName,
		_symbolNameLen, _symbolAddress, _symbolSize, _symbolType);
}


/**
 * @brief Look up a symbol by name and type within a specific image.
 *
 * @param imageID         The image to search.
 * @param name            Null-terminated symbol name.
 * @param symbolType      Required type (B_SYMBOL_TYPE_ANY, _TEXT, or _DATA).
 * @param _symbolLocation Output — receives the runtime address of the symbol.
 * @param _symbolSize     Output — receives the symbol size in bytes.
 * @param _symbolType     Output — receives the resolved symbol type.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if not found.
 */
status_t
SymbolLookup::GetSymbol(image_id imageID, const char* name, int32 symbolType,
	void** _symbolLocation, size_t* _symbolSize, int32* _symbolType) const
{
	Image* image = _FindImageByID(imageID);
	if (image == NULL)
		return B_ENTRY_NOT_FOUND;

	return image->GetSymbol(name, symbolType, _symbolLocation, _symbolSize,
		_symbolType);
}


/**
 * @brief Find the runtime-loader image_t record that contains @a address.
 *
 * Walks the runtime loader's loaded_images list (accessed via remote memory)
 * looking for an image whose text region covers @a address.
 *
 * @param address  Runtime address to search for.
 * @return Pointer to the matching image_t (in translated local memory), or
 *         NULL if no image covers the address or the debug area is unavailable.
 */
const image_t *
SymbolLookup::_FindLoadedImageAtAddress(addr_t address)
{
	TRACE(("SymbolLookup::_FindLoadedImageAtAddress(%p)\n", (void*)address));

	if (fDebugArea == NULL)
		return NULL;

	// iterate through the loaded images
	const image_t *_image = Read(fDebugArea->loaded_images->head);
	while (_image != NULL) {
		const image_t *image = &Read(*_image);
		_image = image->next;

		if (image->regions[0].vmstart <= address
			&& address < image->regions[0].vmstart + image->regions[0].size) {
			return image;
		}
	}

	return NULL;
}


/**
 * @brief Find the runtime-loader image_t record by image_id.
 *
 * @param id  The image_id to search for.
 * @return Pointer to the matching image_t (in translated local memory), or
 *         NULL if not found or if the debug area is unavailable.
 */
const image_t*
SymbolLookup::_FindLoadedImageByID(image_id id)
{
	TRACE(("SymbolLookup::_FindLoadedImageByID(%" B_PRId32 ")\n", id));

	if (fDebugArea == NULL)
		return NULL;

	// iterate through the loaded images
	const image_t *_image = Read(fDebugArea->loaded_images->head);
	while (_image != NULL) {
		const image_t *image = &Read(*_image);
		_image = image->next;

		if (image->id == id)
			return image;
	}

	return NULL;
}


/**
 * @brief Find the Image object whose text segment covers @a address.
 *
 * @param address  Runtime address to search for.
 * @return The matching Image, or NULL if no image covers the address.
 */
Image*
SymbolLookup::_FindImageAtAddress(addr_t address) const
{
	DoublyLinkedList<Image>::ConstIterator it = fImages.GetIterator();
	while (Image* image = it.Next()) {
		addr_t textAddress = image->TextAddress();
		if (address >= textAddress && address < textAddress + image->TextSize())
			return image;
	}

	return NULL;
}


/**
 * @brief Find the Image object with the given image_id.
 *
 * @param id  The image_id to look for.
 * @return The matching Image, or NULL if not found.
 */
Image*
SymbolLookup::_FindImageByID(image_id id) const
{
	DoublyLinkedList<Image>::ConstIterator it = fImages.GetIterator();
	while (Image* image = it.Next()) {
		if (image->ID() == id)
			return image;
	}

	return NULL;
}


/**
 * @brief Return the safe string length of a symbol name in a cloned area.
 *
 * Bounds the strnlen search to the end of the containing area to prevent
 * overreads when the string table is accessed via remote memory.
 *
 * @param address  Pointer into a locally mapped Area (i.e., after translation).
 * @return Length of the symbol name, or 0 if the pointer is not covered.
 */
size_t
SymbolLookup::_SymbolNameLen(const char* address) const
{
	Area* area = AreaForLocalAddress(address);
	if (area == NULL)
		return 0;

	return strnlen(address, (addr_t)area->LocalAddress() + area->Size()
		- (addr_t)address);
}


/**
 * @brief Load the Image object(s) needed to resolve symbols in one image.
 *
 * Tries to create an ImageFile from the on-disk ELF binary. If that fails,
 * falls back to a LoadedImage wrapping the in-memory runtime symbol table. For
 * the kernel team, uses KernelImage; for the commpage, uses CommPageImage.
 *
 * @param imageInfo  The image_info record for the image to load.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or a load error
 *         if even the fallback path fails.
 */
status_t
SymbolLookup::_LoadImageInfo(const image_info& imageInfo)
{
	status_t error = B_OK;

	Image* image;
	if (fDebugContext->team == B_SYSTEM_TEAM) {
		// kernel image
		KernelImage* kernelImage = new(std::nothrow) KernelImage;
		if (kernelImage == NULL)
			return B_NO_MEMORY;

		error = kernelImage->Init(imageInfo);
		image = kernelImage;
	} else if (!strcmp("commpage", imageInfo.name)) {
		// commpage image
		CommPageImage* commPageImage = new(std::nothrow) CommPageImage;
		if (commPageImage == NULL)
			return B_NO_MEMORY;

		error = commPageImage->Init(imageInfo);
		image = commPageImage;
	} else {
		// userland image -- try to load an image file
		ImageFile* imageFile = new(std::nothrow) ImageFile;
		if (imageFile == NULL)
			return B_NO_MEMORY;

		error = imageFile->Init(imageInfo);
		image = imageFile;
	}

	if (error != B_OK) {
		// initialization error -- fall back to the loaded image
		delete image;

		const image_t* loadedImage = _FindLoadedImageByID(imageInfo.id);
		if (loadedImage == NULL)
			return B_OK;

		image = new(std::nothrow) LoadedImage(this, imageInfo,
			loadedImage, Read(loadedImage->symhash[1]));
		if (image == NULL)
			return B_NO_MEMORY;

	}

	fImages.Add(image);

	return B_OK;
}

// #pragma mark - LoadedImage


/**
 * @brief Construct a LoadedImage wrapping the in-memory runtime symbol table.
 *
 * @param symbolLookup  The owning SymbolLookup (used for remote memory access).
 * @param info          The image_info record for this image.
 * @param image         Pointer to the translated image_t structure.
 * @param symbolCount   Number of symbols in the runtime symbol table.
 */
SymbolLookup::LoadedImage::LoadedImage(SymbolLookup* symbolLookup,
	const image_info& info, const image_t* image, int32 symbolCount)
	:
	fSymbolLookup(symbolLookup),
	fImage(image),
	fSymbolCount(symbolCount),
	fLoadDelta(image->regions[0].delta)
{
	fInfo = info;
}


/**
 * @brief Destructor — no additional resources to release.
 */
SymbolLookup::LoadedImage::~LoadedImage()
{
}


/**
 * @brief Find the symbol best covering @a address via the runtime symbol table.
 *
 * Scans the image's in-memory symbol table through remote memory access,
 * preferring exact matches and skipping non-function/non-data symbols.
 *
 * @param address       Runtime address to resolve.
 * @param _baseAddress  Output — receives the symbol's runtime base address.
 * @param _symbolName   Output — pointer to the symbol name (valid until the
 *                      backing area is released).
 * @param _symbolNameLen Output — length of the symbol name.
 * @param _exactMatch   Output — true when the address falls within the symbol.
 * @return Pointer to the matching elf_sym, or NULL if not found.
 */
const elf_sym*
SymbolLookup::LoadedImage::LookupSymbol(addr_t address, addr_t* _baseAddress,
	const char** _symbolName, size_t *_symbolNameLen, bool *_exactMatch) const
{
	TRACE(("LoadedImage::LookupSymbol(): found image: ID: %" B_PRId32 ", text: "
		"address: %p, size: %ld\n", fImage->id,
		(void*)fImage->regions[0].vmstart, fImage->regions[0].size));

	// search the image for the symbol
	const elf_sym *symbolFound = NULL;
	addr_t deltaFound = INT_MAX;
	bool exactMatch = false;
	const char *symbolName = NULL;

	for (int32 i = 0; i < fSymbolCount; i++) {
		const elf_sym *symbol = &fSymbolLookup->Read(fImage->syms[i]);

		// The symbol table contains not only symbols referring to functions
		// and data symbols within the shared object, but also referenced
		// symbols of other shared objects, as well as section and file
		// references. We ignore everything but function and data symbols
		// that have an st_value != 0 (0 seems to be an indication for a
		// symbol defined elsewhere -- couldn't verify that in the specs
		// though).
		if ((symbol->Type() != STT_FUNC && symbol->Type() != STT_OBJECT)
			|| symbol->st_value == 0
			|| (symbol->st_value + symbol->st_size) > (size_t)fInfo.text_size) {
			continue;
		}

		// skip symbols starting after the given address
		addr_t symbolAddress = symbol->st_value + fLoadDelta;

		if (symbolAddress > address)
			continue;
		addr_t symbolDelta = address - symbolAddress;

		if (!symbolFound || symbolDelta < deltaFound) {
			symbolName = (const char*)fSymbolLookup->PrepareAddressNoThrow(
				SYMNAME(fImage, symbol), 1);
			if (symbolName == NULL)
				continue;

			deltaFound = symbolDelta;
			symbolFound = symbol;

			if (symbolDelta >= 0 && symbolDelta < symbol->st_size) {
				// exact match
				exactMatch = true;
				break;
			}
		}
	}

	TRACE(("LoadedImage::LookupSymbol(): done: symbol: %p, image name: "
		"%s, exact match: %d\n", symbolFound, fImage->name, exactMatch));

	if (symbolFound != NULL) {
		if (_baseAddress)
			*_baseAddress = symbolFound->st_value + fLoadDelta;
		if (_symbolName)
			*_symbolName = symbolName;
		if (_exactMatch)
			*_exactMatch = exactMatch;
		if (_symbolNameLen != NULL)
			*_symbolNameLen = fSymbolLookup->_SymbolNameLen(symbolName);
	}

	return symbolFound;
}


/**
 * @brief Advance the iterator to the next function or data symbol.
 *
 * Reads each elf_sym via remote memory and skips non-function/non-object
 * entries and those with a zero st_value.
 *
 * @param iterator       Current iteration index; updated on each call.
 * @param _symbolName    Output — pointer to the symbol name.
 * @param _symbolNameLen Output — length of the symbol name.
 * @param _symbolAddress Output — runtime address of the symbol.
 * @param _symbolSize    Output — size of the symbol in bytes.
 * @param _symbolType    Output — B_SYMBOL_TYPE_TEXT or B_SYMBOL_TYPE_DATA.
 * @return B_OK when a symbol is returned, B_ENTRY_NOT_FOUND at end-of-table.
 */
status_t
SymbolLookup::LoadedImage::NextSymbol(int32& iterator, const char** _symbolName,
	size_t* _symbolNameLen, addr_t* _symbolAddress, size_t* _symbolSize,
	int32* _symbolType) const
{
	while (true) {
		if (++iterator >= fSymbolCount)
			return B_ENTRY_NOT_FOUND;

		const elf_sym* symbol
			= &fSymbolLookup->Read(fImage->syms[iterator]);
		if ((symbol->Type() != STT_FUNC && symbol->Type() != STT_OBJECT)
			|| symbol->st_value == 0) {
			continue;
		}

		*_symbolName = (const char*)fSymbolLookup->PrepareAddressNoThrow(
			SYMNAME(fImage, symbol), 1);
		*_symbolNameLen = fSymbolLookup->_SymbolNameLen(*_symbolName);
		*_symbolAddress = symbol->st_value + fLoadDelta;
		*_symbolSize = symbol->st_size;
		*_symbolType = symbol->Type() == STT_FUNC ? B_SYMBOL_TYPE_TEXT
			: B_SYMBOL_TYPE_DATA;

		return B_OK;
	}
}
