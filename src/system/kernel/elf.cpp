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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2018, Jérôme Duval, jerome.duval@gmail.com.
 *   Copyright 2009-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2002-2009, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2001, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the NewOS License.
 */

/**
 * @file elf.cpp
 * @brief Kernel ELF loader — loads and links ELF executables and shared libraries.
 *
 * Implements load_kernel_add_on() and the low-level ELF loading path used
 * to bring up the runtime loader and kernel add-ons. Handles ELF header
 * validation, program header mapping, symbol resolution, relocation, and
 * the BSS zeroing. Also provides elf_debug_* routines for the kernel debugger.
 *
 * @see image.cpp, module.cpp
 */

/*!	Contains the ELF loader */


#include <elf.h>

#include <OS.h>

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include <algorithm>

#include <AutoDeleter.h>
#include <BytePointer.h>
#include <commpage.h>
#include <driver_settings.h>
#include <boot/kernel_args.h>
#include <debug.h>
#include <image_defs.h>
#include <kernel.h>
#include <kimage.h>
#include <syscalls.h>
#include <team.h>
#include <thread.h>
#include <runtime_loader.h>
#include <util/AutoLock.h>
#include <StackOrHeapArray.h>
#include <vfs.h>
#include <vm/vm.h>
#include <vm/vm_types.h>
#include <vm/VMAddressSpace.h>
#include <vm/VMArea.h>

#include <arch/cpu.h>
#include <arch/elf.h>
#include <elf_priv.h>
#include <boot/elf.h>

//#define TRACE_ELF
#ifdef TRACE_ELF
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


namespace {

#define IMAGE_HASH_SIZE 16

struct ImageHashDefinition {
	typedef struct elf_image_info ValueType;
	typedef image_id KeyType;

	size_t Hash(ValueType* entry) const
		{ return HashKey(entry->id); }
	ValueType*& GetLink(ValueType* entry) const
		{ return entry->next; }

	size_t HashKey(KeyType key) const
	{
		return (size_t)key;
	}

	bool Compare(KeyType key, ValueType* entry) const
	{
		return key == entry->id;
	}
};

typedef BOpenHashTable<ImageHashDefinition> ImageHash;

} // namespace


#ifndef ELF32_COMPAT

static ImageHash *sImagesHash;

static struct elf_image_info *sKernelImage = NULL;
static mutex sImageMutex = MUTEX_INITIALIZER("kimages_lock");
	// guards sImagesHash
static mutex sImageLoadMutex = MUTEX_INITIALIZER("kimages_load_lock");
	// serializes loading/unloading add-ons locking order
	// sImageLoadMutex -> sImageMutex
static bool sLoadElfSymbols = false;
static bool sInitialized = false;


static elf_sym *elf_find_symbol(struct elf_image_info *image, const char *name,
	const elf_version_info *version, bool lookupDefault);


/**
 * @brief Remove an ELF image from the kernel image registry and hash table.
 *
 * @param image Pointer to the elf_image_info to unregister.
 */
static void
unregister_elf_image(struct elf_image_info *image)
{
	unregister_image(team_get_kernel_team(), image->id);
	sImagesHash->Remove(image);
}


/**
 * @brief Register an ELF image with the kernel image registry and insert it
 *        into the global images hash table.
 *
 * Populates extended_image_info fields (text/data regions, API/ABI version)
 * from the image's symbol table when available, then calls register_image()
 * and inserts the entry into sImagesHash.
 *
 * @param image Pointer to the elf_image_info to register.
 */
static void
register_elf_image(struct elf_image_info *image)
{
	extended_image_info imageInfo;

	memset(&imageInfo, 0, sizeof(imageInfo));
	imageInfo.basic_info.id = image->id;
	imageInfo.basic_info.type = B_SYSTEM_IMAGE;
	strlcpy(imageInfo.basic_info.name, image->name,
		sizeof(imageInfo.basic_info.name));

	imageInfo.basic_info.text = (void *)image->text_region.start;
	imageInfo.basic_info.text_size = image->text_region.size;
	imageInfo.basic_info.data = (void *)image->data_region.start;
	imageInfo.basic_info.data_size = image->data_region.size;

	if (image->text_region.id >= 0) {
		// evaluate the API/ABI version symbols

		// Haiku API version
		imageInfo.basic_info.api_version = 0;
		elf_sym* symbol = elf_find_symbol(image,
			B_SHARED_OBJECT_HAIKU_VERSION_VARIABLE_NAME, NULL, true);
		if (symbol != NULL && symbol->st_shndx != SHN_UNDEF
			&& symbol->st_value > 0
			&& symbol->Type() == STT_OBJECT
			&& symbol->st_size >= sizeof(uint32)) {
			addr_t symbolAddress = symbol->st_value + image->text_region.delta;
			if (symbolAddress >= image->text_region.start
				&& symbolAddress - image->text_region.start + sizeof(uint32)
					<= image->text_region.size) {
				imageInfo.basic_info.api_version = *(uint32*)symbolAddress;
			}
		}

		// Haiku ABI
		imageInfo.basic_info.abi = 0;
		symbol = elf_find_symbol(image,
			B_SHARED_OBJECT_HAIKU_ABI_VARIABLE_NAME, NULL, true);
		if (symbol != NULL && symbol->st_shndx != SHN_UNDEF
			&& symbol->st_value > 0
			&& symbol->Type() == STT_OBJECT
			&& symbol->st_size >= sizeof(uint32)) {
			addr_t symbolAddress = symbol->st_value + image->text_region.delta;
			if (symbolAddress >= image->text_region.start
				&& symbolAddress - image->text_region.start + sizeof(uint32)
					<= image->text_region.size) {
				imageInfo.basic_info.api_version = *(uint32*)symbolAddress;
			}
		}
	} else {
		// in-memory image -- use the current values
		imageInfo.basic_info.api_version = B_HAIKU_VERSION;
		imageInfo.basic_info.abi = B_HAIKU_ABI;
	}

	image->id = register_image(team_get_kernel_team(), &imageInfo,
		sizeof(imageInfo));
	sImagesHash->Insert(image);
}


/**
 * @brief Find the kernel ELF image whose text or data region contains the
 *        given virtual address.
 *
 * @note The caller must hold sImageMutex (or be running inside the kernel
 *       debugger).
 *
 * @param address Virtual address to search for.
 * @return Pointer to the matching elf_image_info, or NULL if not found.
 */
/*!	Note, you must lock the image mutex when you call this function. */
static struct elf_image_info *
find_image_at_address(addr_t address)
{
#if KDEBUG
	if (!debug_debugger_running())
		ASSERT_LOCKED_MUTEX(&sImageMutex);
#endif

	ImageHash::Iterator iterator(sImagesHash);

	// get image that may contain the address

	while (iterator.HasNext()) {
		struct elf_image_info* image = iterator.Next();
		if ((address >= image->text_region.start && address
				<= (image->text_region.start + image->text_region.size))
			|| (address >= image->data_region.start
				&& address
					<= (image->data_region.start + image->data_region.size)))
			return image;
	}

	return NULL;
}


/**
 * @brief Kernel debugger command: print the symbol and image name for a
 *        given address.
 *
 * Usage: @c ls @c <address>
 *
 * @param argc Argument count (must be 2).
 * @param argv Argument vector; argv[1] is the hex address to look up.
 * @return 0 always.
 */
static int
dump_address_info(int argc, char **argv)
{
	const char *symbol, *imageName;
	bool exactMatch;
	addr_t address, baseAddress;

	if (argc < 2) {
		kprintf("usage: ls <address>\n");
		return 0;
	}

	address = strtoul(argv[1], NULL, 16);

	status_t error;

	if (IS_KERNEL_ADDRESS(address)) {
		error = elf_debug_lookup_symbol_address(address, &baseAddress, &symbol,
			&imageName, &exactMatch);
	} else {
		error = elf_debug_lookup_user_symbol_address(
			debug_get_debugged_thread()->team, address, &baseAddress, &symbol,
			&imageName, &exactMatch);
	}

	if (error == B_OK) {
		kprintf("%p = %s + 0x%lx (%s)%s\n", (void*)address, symbol,
			address - baseAddress, imageName, exactMatch ? "" : " (nearest)");
	} else
		kprintf("There is no image loaded at this address!\n");

	return 0;
}


/**
 * @brief Look up an ELF image by its image_id in the global hash table.
 *
 * @param id The image_id to search for.
 * @return Pointer to the matching elf_image_info, or NULL if not found.
 */
static struct elf_image_info *
find_image(image_id id)
{
	return sImagesHash->Lookup(id);
}


/**
 * @brief Find a loaded kernel ELF image by its backing vnode pointer.
 *
 * Acquires sImageMutex internally before iterating the hash table.
 *
 * @param vnode Vnode pointer to search for.
 * @return Pointer to the matching elf_image_info, or NULL if not found.
 */
static struct elf_image_info *
find_image_by_vnode(void *vnode)
{
	MutexLocker locker(sImageMutex);

	ImageHash::Iterator iterator(sImagesHash);
	while (iterator.HasNext()) {
		struct elf_image_info* image = iterator.Next();
		if (image->vnode == vnode)
			return image;
	}

	return NULL;
}


#endif // ELF32_COMPAT


/**
 * @brief Allocate and zero-initialise a new elf_image_info structure.
 *
 * Sets text_region.id and data_region.id to -1 and ref_count to 1.
 *
 * @return Pointer to the new elf_image_info, or NULL on allocation failure.
 */
static struct elf_image_info *
create_image_struct()
{
	struct elf_image_info *image
		= (struct elf_image_info *)malloc(sizeof(struct elf_image_info));
	if (image == NULL)
		return NULL;

	memset(image, 0, sizeof(struct elf_image_info));

	image->text_region.id = -1;
	image->data_region.id = -1;
	image->ref_count = 1;

	return image;
}


/**
 * @brief Release all resources owned by an elf_image_info and free it.
 *
 * Deletes VM areas, releases the vnode reference, and frees all heap
 * allocations (versions, debug symbols, string table, ELF header, name).
 *
 * @param image Pointer to the elf_image_info to destroy.
 */
static void
delete_elf_image(struct elf_image_info *image)
{
	if (image->text_region.id >= 0)
		delete_area(image->text_region.id);

	if (image->data_region.id >= 0)
		delete_area(image->data_region.id);

	if (image->vnode)
		vfs_put_vnode(image->vnode);

	free(image->versions);
	free(image->debug_symbols);
	free((void*)image->debug_string_table);
	free(image->elf_header);
	free(image->name);
	free(image);
}


/**
 * @brief Return a short human-readable string for an ELF symbol type.
 *
 * @param symbol Pointer to the ELF symbol whose type is queried.
 * @return One of "func", " obj", "file", or "----".
 */
static const char *
get_symbol_type_string(elf_sym *symbol)
{
	switch (symbol->Type()) {
		case STT_FUNC:
			return "func";
		case STT_OBJECT:
			return " obj";
		case STT_FILE:
			return "file";
		default:
			return "----";
	}
}


/**
 * @brief Return a short human-readable string for an ELF symbol binding.
 *
 * @param symbol Pointer to the ELF symbol whose binding is queried.
 * @return One of "loc ", "glob", "weak", or "----".
 */
static const char *
get_symbol_bind_string(elf_sym *symbol)
{
	switch (symbol->Bind()) {
		case STB_LOCAL:
			return "loc ";
		case STB_GLOBAL:
			return "glob";
		case STB_WEAK:
			return "weak";
		default:
			return "----";
	}
}


#ifndef ELF32_COMPAT


/**
 * @brief Kernel debugger command: search all kernel images for symbols
 *        matching a name pattern and print their addresses.
 *
 * Usage: @c symbol @c <symbol-name>
 *
 * @param argc Argument count (must be 2).
 * @param argv Argument vector; argv[1] is the symbol name (sub-string) to
 *             search for.
 * @return 0 always.
 */
/*!	Searches a symbol (pattern) in all kernel images */
static int
dump_symbol(int argc, char **argv)
{
	if (argc != 2 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s <symbol-name>\n", argv[0]);
		return 0;
	}

	struct elf_image_info *image = NULL;
	const char *pattern = argv[1];

	void* symbolAddress = NULL;

	ImageHash::Iterator iterator(sImagesHash);
	while (iterator.HasNext()) {
		image = iterator.Next();
		if (image->num_debug_symbols > 0) {
			// search extended debug symbol table (contains static symbols)
			for (uint32 i = 0; i < image->num_debug_symbols; i++) {
				elf_sym *symbol = &image->debug_symbols[i];
				const char *name = image->debug_string_table + symbol->st_name;

				if (symbol->st_value > 0 && strstr(name, pattern) != 0) {
					symbolAddress
						= (void*)(symbol->st_value + image->text_region.delta);
					kprintf("%p %5lu %s:%s\n", symbolAddress,
						(long unsigned int)(symbol->st_size),
						image->name, name);
				}
			}
		} else {
			// search standard symbol lookup table
			for (uint32 i = 0; i < HASHTABSIZE(image); i++) {
				for (uint32 j = HASHBUCKETS(image)[i]; j != STN_UNDEF;
						j = HASHCHAINS(image)[j]) {
					elf_sym *symbol = &image->syms[j];
					const char *name = SYMNAME(image, symbol);

					if (symbol->st_value > 0 && strstr(name, pattern) != 0) {
						symbolAddress = (void*)(symbol->st_value
							+ image->text_region.delta);
						kprintf("%p %5lu %s:%s\n", symbolAddress,
							(long unsigned int)(symbol->st_size),
							image->name, name);
					}
				}
			}
		}
	}

	if (symbolAddress != NULL)
		set_debug_variable("_", (addr_t)symbolAddress);

	return 0;
}


/**
 * @brief Kernel debugger command: dump all symbols of a specific kernel image.
 *
 * The image may be identified by its name, numeric image id, or an address
 * within its text segment.
 *
 * @param argc Argument count.
 * @param argv Argument vector; argv[1] is the image identifier.
 * @return 0 on success, -1 if the image was not found.
 */
static int
dump_symbols(int argc, char **argv)
{
	struct elf_image_info *image = NULL;
	uint32 i;

	// if the argument looks like a hex number, treat it as such
	if (argc > 1) {
		if (isdigit(argv[1][0])) {
			addr_t num = strtoul(argv[1], NULL, 0);

			if (IS_KERNEL_ADDRESS(num)) {
				// find image at address

				ImageHash::Iterator iterator(sImagesHash);
				while (iterator.HasNext()) {
					elf_image_info* current = iterator.Next();
					if (current->text_region.start <= num
						&& current->text_region.start
							+ current->text_region.size	>= num) {
						image = current;
						break;
					}
				}

				if (image == NULL) {
					kprintf("No image covers %#" B_PRIxADDR " in the kernel!\n",
						num);
				}
			} else {
				image = sImagesHash->Lookup(num);
				if (image == NULL) {
					kprintf("image %#" B_PRIxADDR " doesn't exist in the "
						"kernel!\n", num);
				}
			}
		} else {
			// look for image by name
			ImageHash::Iterator iterator(sImagesHash);
			while (iterator.HasNext()) {
				elf_image_info* current = iterator.Next();
				if (!strcmp(current->name, argv[1])) {
					image = current;
					break;
				}
			}

			if (image == NULL)
				kprintf("No image \"%s\" found in kernel!\n", argv[1]);
		}
	} else {
		kprintf("usage: %s image_name/image_id/address_in_image\n", argv[0]);
		return 0;
	}

	if (image == NULL)
		return -1;

	// dump symbols

	kprintf("Symbols of image %" B_PRId32 " \"%s\":\n", image->id, image->name);
	kprintf("%-*s Type       Size Name\n", B_PRINTF_POINTER_WIDTH, "Address");

	if (image->num_debug_symbols > 0) {
		// search extended debug symbol table (contains static symbols)
		for (i = 0; i < image->num_debug_symbols; i++) {
			elf_sym *symbol = &image->debug_symbols[i];

			if (symbol->st_value == 0 || symbol->st_size
					>= image->text_region.size + image->data_region.size)
				continue;

			kprintf("%0*lx %s/%s %5ld %s\n", B_PRINTF_POINTER_WIDTH,
				symbol->st_value + image->text_region.delta,
				get_symbol_type_string(symbol), get_symbol_bind_string(symbol),
				(long unsigned int)(symbol->st_size),
				image->debug_string_table + symbol->st_name);
		}
	} else {
		int32 j;

		// search standard symbol lookup table
		for (i = 0; i < HASHTABSIZE(image); i++) {
			for (j = HASHBUCKETS(image)[i]; j != STN_UNDEF;
					j = HASHCHAINS(image)[j]) {
				elf_sym *symbol = &image->syms[j];

				if (symbol->st_value == 0 || symbol->st_size
						>= image->text_region.size + image->data_region.size)
					continue;

				kprintf("%08lx %s/%s %5ld %s\n",
					symbol->st_value + image->text_region.delta,
					get_symbol_type_string(symbol),
					get_symbol_bind_string(symbol),
					(long unsigned int)(symbol->st_size),
					SYMNAME(image, symbol));
			}
		}
	}

	return 0;
}


/**
 * @brief Print a single elf_region's fields to the kernel debugger console.
 *
 * @param region Pointer to the elf_region to display.
 * @param name   Human-readable label for the region (e.g. "text" or "data").
 */
static void
dump_elf_region(struct elf_region *region, const char *name)
{
	kprintf("   %s.id %" B_PRId32 "\n", name, region->id);
	kprintf("   %s.start %#" B_PRIxADDR "\n", name, region->start);
	kprintf("   %s.size %#" B_PRIxSIZE "\n", name, region->size);
	kprintf("   %s.delta %ld\n", name, region->delta);
}


/**
 * @brief Print all fields of an elf_image_info structure to the kernel
 *        debugger console.
 *
 * @param image Pointer to the elf_image_info to display.
 */
static void
dump_image_info(struct elf_image_info *image)
{
	kprintf("elf_image_info at %p:\n", image);
	kprintf(" next %p\n", image->next);
	kprintf(" id %" B_PRId32 "\n", image->id);
	dump_elf_region(&image->text_region, "text");
	dump_elf_region(&image->data_region, "data");
	kprintf(" dynamic_section %#" B_PRIxADDR "\n", image->dynamic_section);
	kprintf(" needed %p\n", image->needed);
	kprintf(" symhash %p\n", image->symhash);
	kprintf(" syms %p\n", image->syms);
	kprintf(" strtab %p\n", image->strtab);
	kprintf(" rel %p\n", image->rel);
	kprintf(" rel_len %#x\n", image->rel_len);
	kprintf(" rela %p\n", image->rela);
	kprintf(" rela_len %#x\n", image->rela_len);
	kprintf(" pltrel %p\n", image->pltrel);
	kprintf(" pltrel_len %#x\n", image->pltrel_len);

	kprintf(" debug_symbols %p (%" B_PRIu32 ")\n",
		image->debug_symbols, image->num_debug_symbols);
}


/**
 * @brief Kernel debugger command: dump info about a loaded kernel image, or
 *        list all loaded kernel images.
 *
 * When called with an argument the image is located by address or image id and
 * its full elf_image_info is printed. Without an argument all loaded images are
 * listed.
 *
 * @param argc Argument count.
 * @param argv Argument vector; optional argv[1] is a hex image pointer or id.
 * @return 0 always.
 */
static int
dump_image(int argc, char **argv)
{
	struct elf_image_info *image;

	// if the argument looks like a hex number, treat it as such
	if (argc > 1) {
		addr_t num = strtoul(argv[1], NULL, 0);

		if (IS_KERNEL_ADDRESS(num)) {
			// semi-hack
			dump_image_info((struct elf_image_info *)num);
		} else {
			image = sImagesHash->Lookup(num);
			if (image == NULL) {
				kprintf("image %#" B_PRIxADDR " doesn't exist in the kernel!\n",
					num);
			} else
				dump_image_info(image);
		}
		return 0;
	}

	kprintf("loaded kernel images:\n");

	ImageHash::Iterator iterator(sImagesHash);

	while (iterator.HasNext()) {
		image = iterator.Next();
		kprintf("%p (%" B_PRId32 ") %s\n", image, image->id, image->name);
	}

	return 0;
}


// Currently unused
/*static
void dump_symbol(struct elf_image_info *image, elf_sym *sym)
{

	kprintf("symbol at %p, in image %p\n", sym, image);

	kprintf(" name index %d, '%s'\n", sym->st_name, SYMNAME(image, sym));
	kprintf(" st_value 0x%x\n", sym->st_value);
	kprintf(" st_size %d\n", sym->st_size);
	kprintf(" st_info 0x%x\n", sym->st_info);
	kprintf(" st_other 0x%x\n", sym->st_other);
	kprintf(" st_shndx %d\n", sym->st_shndx);
}
*/


#endif // ELF32_COMPAT


/**
 * @brief Compute the ELF System V hash of a symbol name string.
 *
 * @param _name Null-terminated symbol name.
 * @return 28-bit hash value suitable for indexing an ELF symbol hash table.
 */
static uint32
elf_hash(const char* _name)
{
	const uint8* name = (const uint8*)_name;

	uint32 h = 0;
	while (*name != '\0') {
		h = (h << 4) + *name++;
		h ^= (h >> 24) & 0xf0;
	}
	return (h & 0x0fffffff);
}


/**
 * @brief Look up a symbol by name in an image's ELF hash table, respecting
 *        GNU symbol versioning rules.
 *
 * When @p lookupVersion is non-NULL the symbol must match both name and
 * version hash/name. When it is NULL the function returns the base version
 * (VER_NDX_GLOBAL / VER_NDX_INITIAL) or, if unique, the sole non-hidden
 * versioned symbol.
 *
 * @param image         Image whose symbol hash table is searched.
 * @param name          Null-terminated symbol name to find.
 * @param lookupVersion Optional version information to match, or NULL.
 * @param lookupDefault When true, VER_NDX_INITIAL symbols are not treated as
 *                      base-version candidates.
 * @return Pointer to the matching elf_sym entry, or NULL if not found.
 */
static elf_sym *
elf_find_symbol(struct elf_image_info *image, const char *name,
	const elf_version_info *lookupVersion, bool lookupDefault)
{
	if (image->dynamic_section == 0 || HASHTABSIZE(image) == 0)
		return NULL;

	elf_sym* versionedSymbol = NULL;
	uint32 versionedSymbolCount = 0;

	uint32 hash = elf_hash(name) % HASHTABSIZE(image);
	for (uint32 i = HASHBUCKETS(image)[hash]; i != STN_UNDEF;
			i = HASHCHAINS(image)[i]) {
		elf_sym* symbol = &image->syms[i];

		// consider only symbols with the right name and binding
		if (symbol->st_shndx == SHN_UNDEF
			|| ((symbol->Bind() != STB_GLOBAL) && (symbol->Bind() != STB_WEAK))
			|| strcmp(SYMNAME(image, symbol), name) != 0) {
			continue;
		}

		// check the version

		// Handle the simple cases -- the image doesn't have version
		// information -- first.
		if (image->symbol_versions == NULL) {
			if (lookupVersion == NULL) {
				// No specific symbol version was requested either, so the
				// symbol is just fine.
				return symbol;
			}

			// A specific version is requested. Since the only possible
			// dependency is the kernel itself, the add-on was obviously linked
			// against a newer kernel.
			dprintf("Kernel add-on requires version support, but the kernel "
				"is too old.\n");
			return NULL;
		}

		// The image has version information. Let's see what we've got.
		uint32 versionID = image->symbol_versions[i];
		uint32 versionIndex = VER_NDX(versionID);
		elf_version_info& version = image->versions[versionIndex];

		// skip local versions
		if (versionIndex == VER_NDX_LOCAL)
			continue;

		if (lookupVersion != NULL) {
			// a specific version is requested

			// compare the versions
			if (version.hash == lookupVersion->hash
				&& strcmp(version.name, lookupVersion->name) == 0) {
				// versions match
				return symbol;
			}

			// The versions don't match. We're still fine with the
			// base version, if it is public and we're not looking for
			// the default version.
			if ((versionID & VER_NDX_FLAG_HIDDEN) == 0
				&& versionIndex == VER_NDX_GLOBAL
				&& !lookupDefault) {
				// TODO: Revise the default version case! That's how
				// FreeBSD implements it, but glibc doesn't handle it
				// specially.
				return symbol;
			}
		} else {
			// No specific version requested, but the image has version
			// information. This can happen in either of these cases:
			//
			// * The dependent object was linked against an older version
			//   of the now versioned dependency.
			// * The symbol is looked up via find_image_symbol() or dlsym().
			//
			// In the first case we return the base version of the symbol
			// (VER_NDX_GLOBAL or VER_NDX_INITIAL), or, if that doesn't
			// exist, the unique, non-hidden versioned symbol.
			//
			// In the second case we want to return the public default
			// version of the symbol. The handling is pretty similar to the
			// first case, with the exception that we treat VER_NDX_INITIAL
			// as regular version.

			// VER_NDX_GLOBAL is always good, VER_NDX_INITIAL is fine, if
			// we don't look for the default version.
			if (versionIndex == VER_NDX_GLOBAL
				|| (!lookupDefault && versionIndex == VER_NDX_INITIAL)) {
				return symbol;
			}

			// If not hidden, remember the version -- we'll return it, if
			// it is the only one.
			if ((versionID & VER_NDX_FLAG_HIDDEN) == 0) {
				versionedSymbolCount++;
				versionedSymbol = symbol;
			}
		}
	}

	return versionedSymbolCount == 1 ? versionedSymbol : NULL;
}


/**
 * @brief Parse an ELF image's PT_DYNAMIC section and populate the
 *        elf_image_info's dynamic-linking fields.
 *
 * Iterates the DT_* entries to extract symbol hash table, string table,
 * symbol table, relocation tables, PLT info, and GNU version tables.
 *
 * @param image Image whose dynamic_section pointer is already set.
 * @retval B_OK    On success.
 * @retval B_ERROR If the dynamic section is missing or required entries
 *                 (symhash, syms, strtab) are absent.
 */
static status_t
elf_parse_dynamic_section(struct elf_image_info *image)
{
	elf_dyn *d;
	ssize_t neededOffset = -1;

	TRACE(("top of elf_parse_dynamic_section\n"));

	image->symhash = 0;
	image->syms = 0;
	image->strtab = 0;

	d = (elf_dyn *)image->dynamic_section;
	if (!d)
		return B_ERROR;

	for (int32 i = 0; d[i].d_tag != DT_NULL; i++) {
		switch (d[i].d_tag) {
			case DT_NEEDED:
				neededOffset = d[i].d_un.d_ptr + image->text_region.delta;
				break;
			case DT_HASH:
				image->symhash = (uint32 *)(d[i].d_un.d_ptr
					+ image->text_region.delta);
				break;
			case DT_STRTAB:
				image->strtab = (char *)(d[i].d_un.d_ptr
					+ image->text_region.delta);
				break;
			case DT_SYMTAB:
				image->syms = (elf_sym *)(d[i].d_un.d_ptr
					+ image->text_region.delta);
				break;
			case DT_REL:
				image->rel = (elf_rel *)(d[i].d_un.d_ptr
					+ image->text_region.delta);
				break;
			case DT_RELSZ:
				image->rel_len = d[i].d_un.d_val;
				break;
			case DT_RELA:
				image->rela = (elf_rela *)(d[i].d_un.d_ptr
					+ image->text_region.delta);
				break;
			case DT_RELASZ:
				image->rela_len = d[i].d_un.d_val;
				break;
			case DT_JMPREL:
				image->pltrel = (elf_rel *)(d[i].d_un.d_ptr
					+ image->text_region.delta);
				break;
			case DT_PLTRELSZ:
				image->pltrel_len = d[i].d_un.d_val;
				break;
			case DT_PLTREL:
				image->pltrel_type = d[i].d_un.d_val;
				break;
			case DT_VERSYM:
				image->symbol_versions = (elf_versym*)
					(d[i].d_un.d_ptr + image->text_region.delta);
				break;
			case DT_VERDEF:
				image->version_definitions = (elf_verdef*)
					(d[i].d_un.d_ptr + image->text_region.delta);
				break;
			case DT_VERDEFNUM:
				image->num_version_definitions = d[i].d_un.d_val;
				break;
			case DT_VERNEED:
				image->needed_versions = (elf_verneed*)
					(d[i].d_un.d_ptr + image->text_region.delta);
				break;
			case DT_VERNEEDNUM:
				image->num_needed_versions = d[i].d_un.d_val;
				break;
			case DT_SYMBOLIC:
				image->symbolic = true;
				break;
			case DT_FLAGS:
			{
				uint32 flags = d[i].d_un.d_val;
				if ((flags & DF_SYMBOLIC) != 0)
					image->symbolic = true;
				break;
			}

			default:
				continue;
		}
	}

	// lets make sure we found all the required sections
	if (!image->symhash || !image->syms || !image->strtab)
		return B_ERROR;

	TRACE(("needed_offset = %ld\n", neededOffset));

	if (neededOffset >= 0)
		image->needed = STRING(image, neededOffset);

	return B_OK;
}


#ifndef ELF32_COMPAT


/**
 * @brief Verify that a dependency image exports a required version, failing
 *        unless the requirement is flagged weak.
 *
 * @param dependentImage Image that declares the version requirement.
 * @param image          Image expected to define the version.
 * @param neededVersion  Version info (name + hash) that must be present.
 * @param weak           When true a missing version only triggers a warning,
 *                       not a hard error.
 * @retval B_OK             Version found (or image has no version definitions).
 * @retval B_MISSING_SYMBOL Required version not found in a non-weak dependency.
 */
static status_t
assert_defined_image_version(elf_image_info* dependentImage,
	elf_image_info* image, const elf_version_info& neededVersion, bool weak)
{
	// If the image doesn't have version definitions, we print a warning and
	// succeed. Weird, but that's how glibc does it. Not unlikely we'll fail
	// later when resolving versioned symbols.
	if (image->version_definitions == NULL) {
		dprintf("%s: No version information available (required by %s)\n",
			image->name, dependentImage->name);
		return B_OK;
	}

	// iterate through the defined versions to find the given one
	BytePointer<elf_verdef> definition(image->version_definitions);
	for (uint32 i = 0; i < image->num_version_definitions; i++) {
		uint32 versionIndex = VER_NDX(definition->vd_ndx);
		elf_version_info& info = image->versions[versionIndex];

		if (neededVersion.hash == info.hash
			&& strcmp(neededVersion.name, info.name) == 0) {
			return B_OK;
		}

		definition += definition->vd_next;
	}

	// version not found -- fail, if not weak
	if (!weak) {
		dprintf("%s: version \"%s\" not found (required by %s)\n", image->name,
			neededVersion.name, dependentImage->name);
		return B_MISSING_SYMBOL;
	}

	return B_OK;
}


/**
 * @brief Allocate and populate the image's versions array from its GNU
 *        version definition and needed-version ELF sections.
 *
 * Scans VER_NDX values from both elf_verdef and elf_verneed chains to
 * determine the maximum index, then allocates and fills image->versions[].
 *
 * @param image Image whose version_definitions and needed_versions are set.
 * @retval B_OK        On success.
 * @retval B_NO_MEMORY Allocation failure.
 * @retval B_BAD_VALUE Unsupported version definition or needed revision.
 */
static status_t
init_image_version_infos(elf_image_info* image)
{
	// First find out how many version infos we need -- i.e. get the greatest
	// version index from the defined and needed versions (they use the same
	// index namespace).
	uint32 maxIndex = 0;

	if (image->version_definitions != NULL) {
		BytePointer<elf_verdef> definition(image->version_definitions);
		for (uint32 i = 0; i < image->num_version_definitions; i++) {
			if (definition->vd_version != 1) {
				dprintf("Unsupported version definition revision: %u\n",
					definition->vd_version);
				return B_BAD_VALUE;
			}

			uint32 versionIndex = VER_NDX(definition->vd_ndx);
			if (versionIndex > maxIndex)
				maxIndex = versionIndex;

			definition += definition->vd_next;
		}
	}

	if (image->needed_versions != NULL) {
		BytePointer<elf_verneed> needed(image->needed_versions);
		for (uint32 i = 0; i < image->num_needed_versions; i++) {
			if (needed->vn_version != 1) {
				dprintf("Unsupported version needed revision: %u\n",
					needed->vn_version);
				return B_BAD_VALUE;
			}

			BytePointer<elf_vernaux> vernaux(needed + needed->vn_aux);
			for (uint32 k = 0; k < needed->vn_cnt; k++) {
				uint32 versionIndex = VER_NDX(vernaux->vna_other);
				if (versionIndex > maxIndex)
					maxIndex = versionIndex;

				vernaux += vernaux->vna_next;
			}

			needed += needed->vn_next;
		}
	}

	if (maxIndex == 0)
		return B_OK;

	// allocate the version infos
	image->versions
		= (elf_version_info*)malloc(sizeof(elf_version_info) * (maxIndex + 1));
	if (image->versions == NULL) {
		dprintf("Memory shortage in init_image_version_infos()\n");
		return B_NO_MEMORY;
	}
	image->num_versions = maxIndex + 1;

	// init the version infos

	// version definitions
	if (image->version_definitions != NULL) {
		BytePointer<elf_verdef> definition(image->version_definitions);
		for (uint32 i = 0; i < image->num_version_definitions; i++) {
			if (definition->vd_cnt > 0
				&& (definition->vd_flags & VER_FLG_BASE) == 0) {
				BytePointer<elf_verdaux> verdaux(definition
					+ definition->vd_aux);

				uint32 versionIndex = VER_NDX(definition->vd_ndx);
				elf_version_info& info = image->versions[versionIndex];
				info.hash = definition->vd_hash;
				info.name = STRING(image, verdaux->vda_name);
				info.file_name = NULL;
			}

			definition += definition->vd_next;
		}
	}

	// needed versions
	if (image->needed_versions != NULL) {
		BytePointer<elf_verneed> needed(image->needed_versions);
		for (uint32 i = 0; i < image->num_needed_versions; i++) {
			const char* fileName = STRING(image, needed->vn_file);

			BytePointer<elf_vernaux> vernaux(needed + needed->vn_aux);
			for (uint32 k = 0; k < needed->vn_cnt; k++) {
				uint32 versionIndex = VER_NDX(vernaux->vna_other);
				elf_version_info& info = image->versions[versionIndex];
				info.hash = vernaux->vna_hash;
				info.name = STRING(image, vernaux->vna_name);
				info.file_name = fileName;

				vernaux += vernaux->vna_next;
			}

			needed += needed->vn_next;
		}
	}

	return B_OK;
}


/**
 * @brief Verify that all versioned symbol dependencies declared in an image's
 *        GNU verneed section are satisfied by the kernel image.
 *
 * @param image Image whose needed_versions are checked against sKernelImage.
 * @retval B_OK             All required versions present (or no requirements).
 * @retval B_MISSING_SYMBOL A required non-weak version is absent.
 */
static status_t
check_needed_image_versions(elf_image_info* image)
{
	if (image->needed_versions == NULL)
		return B_OK;

	BytePointer<elf_verneed> needed(image->needed_versions);
	for (uint32 i = 0; i < image->num_needed_versions; i++) {
		elf_image_info* dependency = sKernelImage;

		BytePointer<elf_vernaux> vernaux(needed + needed->vn_aux);
		for (uint32 k = 0; k < needed->vn_cnt; k++) {
			uint32 versionIndex = VER_NDX(vernaux->vna_other);
			elf_version_info& info = image->versions[versionIndex];

			status_t error = assert_defined_image_version(image, dependency,
				info, (vernaux->vna_flags & VER_FLG_WEAK) != 0);
			if (error != B_OK)
				return error;

			vernaux += vernaux->vna_next;
		}

		needed += needed->vn_next;
	}

	return B_OK;
}


#endif // ELF32_COMPAT


/**
 * @brief Resolve a single ELF symbol to its run-time virtual address.
 *
 * Local symbols are resolved directly to image + delta. Global/weak symbols
 * are looked up first in @p sharedImage (usually the kernel) then in
 * @p image itself, following symbolic-binding and weak-binding rules. Weak
 * undefined symbols that remain unresolved receive an address of 0.
 *
 * @param image          Image containing the symbol reference.
 * @param symbol         The ELF symbol entry to resolve.
 * @param sharedImage    Shared image (typically the kernel) to search first.
 * @param _symbolAddress On success, set to the resolved virtual address.
 * @retval B_OK             Symbol resolved successfully.
 * @retval B_MISSING_SYMBOL Symbol not found or type mismatch.
 */
/*!	Resolves the \a symbol by linking against \a sharedImage if necessary.
	Returns the resolved symbol's address in \a _symbolAddress.
*/
status_t
elf_resolve_symbol(struct elf_image_info *image, elf_sym *symbol,
	struct elf_image_info *sharedImage, elf_addr *_symbolAddress)
{
	// Local symbols references are always resolved to the given symbol.
	if (symbol->Bind() == STB_LOCAL) {
		*_symbolAddress = symbol->st_value + image->text_region.delta;
		return B_OK;
	}

	// Non-local symbols we try to resolve to the kernel image first. Unless
	// the image is linked symbolically, then vice versa.
	elf_image_info* firstImage = sharedImage;
	elf_image_info* secondImage = image;
	if (image->symbolic)
		std::swap(firstImage, secondImage);

	const char *symbolName = SYMNAME(image, symbol);

	// get the version info
	const elf_version_info* versionInfo = NULL;
	if (image->symbol_versions != NULL) {
		uint32 index = symbol - image->syms;
		uint32 versionIndex = VER_NDX(image->symbol_versions[index]);
		if (versionIndex >= VER_NDX_INITIAL)
			versionInfo = image->versions + versionIndex;
	}

	// find the symbol
	elf_image_info* foundImage = firstImage;
	elf_sym* foundSymbol = elf_find_symbol(firstImage, symbolName, versionInfo,
		false);
	if (foundSymbol == NULL
		|| foundSymbol->Bind() == STB_WEAK) {
		// Not found or found a weak definition -- try to resolve in the other
		// image.
		elf_sym* secondSymbol = elf_find_symbol(secondImage, symbolName,
			versionInfo, false);
		// If we found a symbol -- take it in case we didn't have a symbol
		// before or the new symbol is not weak.
		if (secondSymbol != NULL
			&& (foundSymbol == NULL
				|| secondSymbol->Bind() != STB_WEAK)) {
			foundImage = secondImage;
			foundSymbol = secondSymbol;
		}
	}

	if (foundSymbol == NULL) {
		// Weak undefined symbols get a value of 0, if unresolved.
		if (symbol->Bind() == STB_WEAK) {
			*_symbolAddress = 0;
			return B_OK;
		}

		dprintf("\"%s\": could not resolve symbol '%s'\n", image->name,
			symbolName);
		return B_MISSING_SYMBOL;
	}

	// make sure they're the same type
	if (symbol->Type() != foundSymbol->Type()) {
		dprintf("elf_resolve_symbol: found symbol '%s' in image '%s' "
			"(requested by image '%s') but wrong type (%d vs. %d)\n",
			symbolName, foundImage->name, image->name,
			foundSymbol->Type(), symbol->Type());
		return B_MISSING_SYMBOL;
	}

	*_symbolAddress = foundSymbol->st_value + foundImage->text_region.delta;
	return B_OK;
}


/**
 * @brief Apply all ELF relocations (REL, RELA, and PLT) for a kernel image.
 *
 * Dispatches to the architecture-specific arch_elf_relocate_rel() and
 * arch_elf_relocate_rela() helpers for each relocation table present in
 * @p image, resolving symbols against @p resolveImage.
 *
 * @param image        Image whose relocations are to be applied.
 * @param resolveImage Image used as the symbol source (usually sKernelImage).
 * @retval B_NO_ERROR On success.
 * @retval <0         On the first architecture relocation error encountered.
 */
/*! Until we have shared library support, just this links against the kernel */
static int
elf_relocate(struct elf_image_info* image, struct elf_image_info* resolveImage)
{
	int status = B_NO_ERROR;

	TRACE(("elf_relocate(%p (\"%s\"))\n", image, image->name));

	// deal with the rels first
	if (image->rel) {
		TRACE(("total %i rel relocs\n", image->rel_len / (int)sizeof(elf_rel)));

		status = arch_elf_relocate_rel(image, resolveImage, image->rel,
			image->rel_len);
		if (status < B_OK)
			return status;
	}

	if (image->pltrel) {
		if (image->pltrel_type == DT_REL) {
			TRACE(("total %i plt-relocs\n",
				image->pltrel_len / (int)sizeof(elf_rel)));
			status = arch_elf_relocate_rel(image, resolveImage, image->pltrel,
				image->pltrel_len);
		} else {
			TRACE(("total %i plt-relocs\n",
				image->pltrel_len / (int)sizeof(elf_rela)));
			status = arch_elf_relocate_rela(image, resolveImage,
				(elf_rela *)image->pltrel, image->pltrel_len);
		}
		if (status < B_OK)
			return status;
	}

	if (image->rela) {
		TRACE(("total %i rel relocs\n",
			image->rela_len / (int)sizeof(elf_rela)));

		status = arch_elf_relocate_rela(image, resolveImage, image->rela,
			image->rela_len);
		if (status < B_OK)
			return status;
	}

	return status;
}


/**
 * @brief Validate the basic ELF header fields required to load a kernel image.
 *
 * Checks the ELF magic bytes, class word size, presence of a program header
 * offset, and minimum program header entry size.
 *
 * @param elfHeader Pointer to the already-read ELF file header.
 * @retval 0                  Header is valid.
 * @retval B_NOT_AN_EXECUTABLE One or more required fields are invalid.
 */
static int
verify_eheader(elf_ehdr *elfHeader)
{
	if (memcmp(elfHeader->e_ident, ELFMAG, 4) != 0)
		return B_NOT_AN_EXECUTABLE;

	if (elfHeader->e_ident[4] != ELF_CLASS)
		return B_NOT_AN_EXECUTABLE;

	if (elfHeader->e_phoff == 0)
		return B_NOT_AN_EXECUTABLE;

	if (elfHeader->e_phentsize < sizeof(elf_phdr))
		return B_NOT_AN_EXECUTABLE;

	return 0;
}


#ifndef ELF32_COMPAT


/**
 * @brief Decrement the reference count of an ELF image and, if it reaches
 *        zero, unregister and delete it.
 *
 * @param image Image whose reference count is decremented.
 */
static void
unload_elf_image(struct elf_image_info *image)
{
	if (atomic_add(&image->ref_count, -1) > 1)
		return;

	TRACE(("unload image %" B_PRId32 ", %s\n", image->id, image->name));

	unregister_elf_image(image);
	delete_elf_image(image);
}


/**
 * @brief Load the full ELF symbol and string tables from a kernel add-on file
 *        for use by the kernel debugger.
 *
 * Reads the section headers from @p fd, locates the first SHT_SYMTAB section,
 * reads both the symbol table and its associated SHT_STRTAB string table into
 * heap memory, and stores them in @p image->debug_symbols /
 * @p image->debug_string_table.
 *
 * @param fd    Open file descriptor for the ELF image file.
 * @param image Image info structure to populate with debug symbol data.
 * @retval B_OK        Tables loaded successfully.
 * @retval B_NO_MEMORY Heap allocation failed.
 * @retval B_BAD_DATA  Section link does not point to a string table.
 * @retval B_BAD_VALUE No symbol table section found.
 * @retval B_ERROR     I/O error reading section headers or tables.
 */
static status_t
load_elf_symbol_table(int fd, struct elf_image_info *image)
{
	elf_ehdr *elfHeader = image->elf_header;
	elf_sym *symbolTable = NULL;
	elf_shdr *stringHeader = NULL;
	uint32 numSymbols = 0;
	char *stringTable;
	status_t status;
	ssize_t length;
	int32 i;

	// get section headers

	ssize_t size = elfHeader->e_shnum * elfHeader->e_shentsize;
	elf_shdr *sectionHeaders = (elf_shdr *)malloc(size);
	if (sectionHeaders == NULL) {
		dprintf("error allocating space for section headers\n");
		return B_NO_MEMORY;
	}

	length = read_pos(fd, elfHeader->e_shoff, sectionHeaders, size);
	if (length < size) {
		TRACE(("error reading in program headers\n"));
		status = B_ERROR;
		goto error1;
	}

	// find symbol table in section headers

	for (i = 0; i < elfHeader->e_shnum; i++) {
		if (sectionHeaders[i].sh_type == SHT_SYMTAB) {
			stringHeader = &sectionHeaders[sectionHeaders[i].sh_link];

			if (stringHeader->sh_type != SHT_STRTAB) {
				TRACE(("doesn't link to string table\n"));
				status = B_BAD_DATA;
				goto error1;
			}

			// read in symbol table
			size = sectionHeaders[i].sh_size;
			symbolTable = (elf_sym *)malloc(size);
			if (symbolTable == NULL) {
				status = B_NO_MEMORY;
				goto error1;
			}

			length
				= read_pos(fd, sectionHeaders[i].sh_offset, symbolTable, size);
			if (length < size) {
				TRACE(("error reading in symbol table\n"));
				status = B_ERROR;
				goto error2;
			}

			numSymbols = size / sizeof(elf_sym);
			break;
		}
	}

	if (symbolTable == NULL) {
		TRACE(("no symbol table\n"));
		status = B_BAD_VALUE;
		goto error1;
	}

	// read in string table

	stringTable = (char *)malloc(size = stringHeader->sh_size);
	if (stringTable == NULL) {
		status = B_NO_MEMORY;
		goto error2;
	}

	length = read_pos(fd, stringHeader->sh_offset, stringTable, size);
	if (length < size) {
		TRACE(("error reading in string table\n"));
		status = B_ERROR;
		goto error3;
	}

	TRACE(("loaded %" B_PRId32 " debug symbols\n", numSymbols));

	// insert tables into image
	image->debug_symbols = symbolTable;
	image->num_debug_symbols = numSymbols;
	image->debug_string_table = stringTable;

	free(sectionHeaders);
	return B_OK;

error3:
	free(stringTable);
error2:
	free(symbolTable);
error1:
	free(sectionHeaders);

	return status;
}


/**
 * @brief Construct a kernel elf_image_info from a boot-loader preloaded image
 *        and register it with the kernel image subsystem.
 *
 * Copies region layout, parses the dynamic section, optionally checks version
 * requirements and performs relocations, then registers the image. For the
 * kernel image itself (kernel == true) sets sKernelImage.
 *
 * @param preloadedImage Preloaded ELF image provided by the boot loader.
 * @param kernel         True if this is the kernel image itself.
 * @retval B_OK        Image inserted successfully.
 * @retval B_NO_MEMORY Allocation failure.
 * @retval other       ELF parse, version check, or relocation error.
 */
static status_t
insert_preloaded_image(preloaded_elf_image *preloadedImage, bool kernel)
{
	status_t status;

	status = verify_eheader(&preloadedImage->elf_header);
	if (status != B_OK)
		return status;

	elf_image_info *image = create_image_struct();
	if (image == NULL)
		return B_NO_MEMORY;

	image->name = strdup(preloadedImage->name);
	image->dynamic_section = preloadedImage->dynamic_section.start;

	image->text_region.id = preloadedImage->text_region.id;
	image->text_region.start = preloadedImage->text_region.start;
	image->text_region.size = preloadedImage->text_region.size;
	image->text_region.delta = preloadedImage->text_region.delta;
	image->data_region.id = preloadedImage->data_region.id;
	image->data_region.start = preloadedImage->data_region.start;
	image->data_region.size = preloadedImage->data_region.size;
	image->data_region.delta = preloadedImage->data_region.delta;

	status = elf_parse_dynamic_section(image);
	if (status != B_OK)
		goto error1;

	status = init_image_version_infos(image);
	if (status != B_OK)
		goto error1;

	if (!kernel) {
		status = check_needed_image_versions(image);
		if (status != B_OK)
			goto error1;

		status = elf_relocate(image, sKernelImage);
		if (status != B_OK)
			goto error1;
	} else
		sKernelImage = image;

	// copy debug symbols to the kernel heap
	if (preloadedImage->debug_symbols != NULL) {
		int32 debugSymbolsSize = sizeof(elf_sym)
			* preloadedImage->num_debug_symbols;
		image->debug_symbols = (elf_sym*)malloc(debugSymbolsSize);
		if (image->debug_symbols != NULL) {
			memcpy(image->debug_symbols, preloadedImage->debug_symbols,
				debugSymbolsSize);
		}
	}
	image->num_debug_symbols = preloadedImage->num_debug_symbols;

	// copy debug string table to the kernel heap
	if (preloadedImage->debug_string_table != NULL) {
		image->debug_string_table = (char*)malloc(
			preloadedImage->debug_string_table_size);
		if (image->debug_string_table != NULL) {
			memcpy((void*)image->debug_string_table,
				preloadedImage->debug_string_table,
				preloadedImage->debug_string_table_size);
		}
	}

	register_elf_image(image);
	preloadedImage->id = image->id;
		// modules_init() uses this information to get the preloaded images

	// we now no longer need to write to the text area anymore
	set_area_protection(image->text_region.id,
		B_KERNEL_READ_AREA | B_KERNEL_EXECUTE_AREA);

	return B_OK;

error1:
	delete_elf_image(image);

	preloadedImage->id = -1;

	return status;
}


//	#pragma mark - userland symbol lookup


class UserSymbolLookup {
public:
	static UserSymbolLookup& Default()
	{
		return sLookup;
	}

	status_t Init(Team* team)
	{
		fTeam = team;
		return B_OK;
	}

	status_t LookupSymbolAddress(addr_t address, addr_t *_baseAddress,
		const char **_symbolName, const char **_imageName, bool *_exactMatch)
	{
		// Note, that this function doesn't find all symbols that we would like
		// to find. E.g. static functions do not appear in the symbol table
		// as function symbols, but as sections without name and size. The
		// .symtab section together with the .strtab section, which apparently
		// differ from the tables referred to by the .dynamic section, also
		// contain proper names and sizes for those symbols. Therefore, to get
		// completely satisfying results, we would need to read those tables
		// from the shared object.

		// get the image for the address
		struct image *image;
		status_t error = _FindImageAtAddress(address, image);
		if (error != B_OK)
			return error;

		if (image->info.basic_info.text == fTeam->commpage_address) {
			// commpage requires special treatment since kernel stores symbol
			// information
			if (*_imageName != NULL)
				*_imageName = "commpage";
			address -= (addr_t)fTeam->commpage_address;
			error = elf_debug_lookup_symbol_address(address, _baseAddress,
				_symbolName, NULL, _exactMatch);
			if (_baseAddress != NULL)
				*_baseAddress += (addr_t)fTeam->commpage_address;
			return error;
		}

		const extended_image_info& info = image->info;
		const uint32 *symhash = (uint32 *)info.symbol_hash;
		elf_sym *syms = (elf_sym *)info.symbol_table;

		strlcpy(fImageName, info.basic_info.name, sizeof(fImageName));

		// symbol hash table size
		uint32 hashTabSize;
		if (!_Read(symhash, hashTabSize))
			return B_BAD_ADDRESS;

		// remote pointers to hash buckets and chains
		const uint32* hashBuckets = symhash + 2;
		const uint32* hashChains = symhash + 2 + hashTabSize;

		const addr_t loadDelta = (addr_t)info.basic_info.text;

		// search the image for the symbol
		elf_sym symbolFound;
		addr_t deltaFound = INT_MAX;
		bool exactMatch = false;

		// to get rid of the erroneous "uninitialized" warnings
		symbolFound.st_name = 0;
		symbolFound.st_value = 0;

		for (uint32 i = 0; i < hashTabSize; i++) {
			uint32 bucket;
			if (!_Read(&hashBuckets[i], bucket))
				return B_BAD_ADDRESS;

			for (uint32 j = bucket; j != STN_UNDEF;
					_Read(&hashChains[j], j) ? 0 : j = STN_UNDEF) {

				elf_sym symbol;
				if (!_Read(syms + j, symbol))
					continue;

				// The symbol table contains not only symbols referring to
				// functions and data symbols within the shared object, but also
				// referenced symbols of other shared objects, as well as
				// section and file references. We ignore everything but
				// function and data symbols that have an st_value != 0 (0
				// seems to be an indication for a symbol defined elsewhere
				// -- couldn't verify that in the specs though).
				if ((symbol.Type() != STT_FUNC && symbol.Type() != STT_OBJECT)
					|| symbol.st_value == 0
					|| (symbol.st_value + symbol.st_size) > (elf_addr)info.basic_info.text_size) {
					continue;
				}

				// skip symbols starting after the given address
				addr_t symbolAddress = symbol.st_value + loadDelta;
				if (symbolAddress > address)
					continue;
				addr_t symbolDelta = address - symbolAddress;

				if (symbolDelta < deltaFound) {
					deltaFound = symbolDelta;
					symbolFound = symbol;

					if (symbolDelta >= 0 && symbolDelta < symbol.st_size) {
						// exact match
						exactMatch = true;
						break;
					}
				}
			}
		}

		if (_imageName)
			*_imageName = fImageName;

		if (_symbolName) {
			*_symbolName = NULL;

			if (deltaFound < INT_MAX) {
				if (_ReadString(info, symbolFound.st_name, fSymbolName,
						sizeof(fSymbolName))) {
					*_symbolName = fSymbolName;
				} else {
					// we can't get its name, so forget the symbol
					deltaFound = INT_MAX;
				}
			}
		}

		if (_baseAddress) {
			if (deltaFound < INT_MAX)
				*_baseAddress = symbolFound.st_value + loadDelta;
			else
				*_baseAddress = loadDelta;
		}

		if (_exactMatch)
			*_exactMatch = exactMatch;

		return B_OK;
	}

	status_t _FindImageAtAddress(addr_t address, struct image*& _image)
	{
		for (struct image* image = fTeam->image_list.First();
				image != NULL; image = fTeam->image_list.GetNext(image)) {
			image_info *info = &image->info.basic_info;

			if ((address < (addr_t)info->text
					|| address >= (addr_t)info->text + info->text_size)
				&& (address < (addr_t)info->data
					|| address >= (addr_t)info->data + info->data_size))
				continue;

			// found image
			_image = image;
			return B_OK;
		}

		return B_ENTRY_NOT_FOUND;
	}

	bool _ReadString(const extended_image_info& info, uint32 offset, char* buffer,
		size_t bufferSize)
	{
		const char* address = (char *)info.string_table + offset;

		if (!IS_USER_ADDRESS(address))
			return false;

		if (debug_debugger_running()) {
			return debug_strlcpy(B_CURRENT_TEAM, buffer, address, bufferSize)
				>= 0;
		}
		return user_strlcpy(buffer, address, bufferSize) >= 0;
	}

	template<typename T> bool _Read(const T* address, T& data);
		// gcc 2.95.3 doesn't like it defined in-place

private:
	Team*						fTeam;
	char						fImageName[B_OS_NAME_LENGTH];
	char						fSymbolName[256];
	static UserSymbolLookup		sLookup;
};


template<typename T>
bool
UserSymbolLookup::_Read(const T* address, T& data)
{
	if (!IS_USER_ADDRESS(address))
		return false;

	if (debug_debugger_running())
		return debug_memcpy(B_CURRENT_TEAM, &data, address, sizeof(T)) == B_OK;
	return user_memcpy(&data, address, sizeof(T)) == B_OK;
}


UserSymbolLookup UserSymbolLookup::sLookup;
	// doesn't need construction, but has an Init() method


//	#pragma mark - public kernel API


/**
 * @brief Look up a named symbol exported by a loaded kernel image.
 *
 * Acquires sImageMutex, finds the image by @p id, then searches its symbol
 * table for @p name. The resolved address is written to @p _symbol.
 *
 * @param id          Image id to search.
 * @param name        Null-terminated symbol name to find.
 * @param symbolClass Symbol class hint (currently unused).
 * @param _symbol     On success, receives the symbol's virtual address.
 * @retval B_OK             Symbol found.
 * @retval B_BAD_IMAGE_ID   No image with the given id exists.
 * @retval B_ENTRY_NOT_FOUND Symbol not found or is undefined.
 */
status_t
get_image_symbol(image_id id, const char *name, int32 symbolClass,
	void **_symbol)
{
	struct elf_image_info *image;
	elf_sym *symbol;
	status_t status = B_OK;

	TRACE(("get_image_symbol(%s)\n", name));

	mutex_lock(&sImageMutex);

	image = find_image(id);
	if (image == NULL) {
		status = B_BAD_IMAGE_ID;
		goto done;
	}

	symbol = elf_find_symbol(image, name, NULL, true);
	if (symbol == NULL || symbol->st_shndx == SHN_UNDEF) {
		status = B_ENTRY_NOT_FOUND;
		goto done;
	}

	// TODO: support the "symbolClass" parameter!

	TRACE(("found: %lx (%lx + %lx)\n",
		symbol->st_value + image->text_region.delta,
		symbol->st_value, image->text_region.delta));

	*_symbol = (void *)(symbol->st_value + image->text_region.delta);

done:
	mutex_unlock(&sImageMutex);
	return status;
}


//	#pragma mark - kernel private API


/**
 * @brief Look up the symbol name and image for a kernel virtual address;
 *        intended for use by the kernel debugger.
 *
 * Searches the debug (or dynamic) symbol tables of the image that contains
 * @p address and returns the nearest symbol, its base address, image name,
 * and whether the match is exact.
 *
 * @note This function must not be called with locks held outside the debugger.
 *
 * @param address       Virtual address to resolve.
 * @param _baseAddress  On success, set to the symbol's base address.
 * @param _symbolName   On success, set to the symbol name string.
 * @param _imageName    On success, set to the image name string.
 * @param _exactMatch   On success, set to true if address falls within the
 *                      symbol's st_size range.
 * @retval B_OK             Symbol (or nearest symbol) found.
 * @retval B_ENTRY_NOT_FOUND No image covers the address.
 * @retval B_ERROR          ELF subsystem not yet initialised.
 */
/*!	Looks up a symbol by address in all images loaded in kernel space.
	Note, if you need to call this function outside a debugger, make
	sure you fix locking and the way it returns its information, first!
*/
status_t
elf_debug_lookup_symbol_address(addr_t address, addr_t *_baseAddress,
	const char **_symbolName, const char **_imageName, bool *_exactMatch)
{
	struct elf_image_info *image;
	elf_sym *symbolFound = NULL;
	const char *symbolName = NULL;
	addr_t deltaFound = INT_MAX;
	bool exactMatch = false;
	status_t status;

	TRACE(("looking up %p\n", (void *)address));

	if (!sInitialized)
		return B_ERROR;

	//mutex_lock(&sImageMutex);

	image = find_image_at_address(address);
		// get image that may contain the address

	if (image != NULL) {
		addr_t symbolDelta;
		uint32 i;
		int32 j;

		TRACE((" image %p, base = %p, size = %p\n", image,
			(void *)image->text_region.start, (void *)image->text_region.size));

		if (image->debug_symbols != NULL) {
			// search extended debug symbol table (contains static symbols)

			TRACE((" searching debug symbols...\n"));

			for (i = 0; i < image->num_debug_symbols; i++) {
				elf_sym *symbol = &image->debug_symbols[i];

				if (symbol->st_value == 0 || symbol->st_size
						>= image->text_region.size + image->data_region.size)
					continue;

				symbolDelta
					= address - (symbol->st_value + image->text_region.delta);
				if (symbolDelta >= 0 && symbolDelta < symbol->st_size)
					exactMatch = true;

				if (exactMatch || symbolDelta < deltaFound) {
					deltaFound = symbolDelta;
					symbolFound = symbol;
					symbolName = image->debug_string_table + symbol->st_name;

					if (exactMatch)
						break;
				}
			}
		} else {
			// search standard symbol lookup table

			TRACE((" searching standard symbols...\n"));

			for (i = 0; i < HASHTABSIZE(image); i++) {
				for (j = HASHBUCKETS(image)[i]; j != STN_UNDEF;
						j = HASHCHAINS(image)[j]) {
					elf_sym *symbol = &image->syms[j];

					if (symbol->st_value == 0
						|| symbol->st_size >= image->text_region.size
							+ image->data_region.size)
						continue;

					symbolDelta = address - (long)(symbol->st_value
						+ image->text_region.delta);
					if (symbolDelta >= 0 && symbolDelta < symbol->st_size)
						exactMatch = true;

					if (exactMatch || symbolDelta < deltaFound) {
						deltaFound = symbolDelta;
						symbolFound = symbol;
						symbolName = SYMNAME(image, symbol);

						if (exactMatch)
							goto symbol_found;
					}
				}
			}
		}
	}
symbol_found:

	if (symbolFound != NULL) {
		if (_symbolName)
			*_symbolName = symbolName;
		if (_imageName)
			*_imageName = image->name;
		if (_baseAddress)
			*_baseAddress = symbolFound->st_value + image->text_region.delta;
		if (_exactMatch)
			*_exactMatch = exactMatch;

		status = B_OK;
	} else if (image != NULL) {
		TRACE(("symbol not found!\n"));

		if (_symbolName)
			*_symbolName = NULL;
		if (_imageName)
			*_imageName = image->name;
		if (_baseAddress)
			*_baseAddress = image->text_region.start;
		if (_exactMatch)
			*_exactMatch = false;

		status = B_OK;
	} else {
		TRACE(("image not found!\n"));
		status = B_ENTRY_NOT_FOUND;
	}

	// Note, theoretically, all information we return back to our caller
	// would have to be locked - but since this function is only called
	// from the debugger, it's safe to do it this way

	//mutex_unlock(&sImageMutex);

	return status;
}


/**
 * @brief Look up the symbol name and image for an address in a user-space team.
 *
 * The team's address space must already be active. Uses UserSymbolLookup to
 * walk the team's image list and ELF hash tables via safe cross-space reads.
 *
 * @param team          Team whose image list is searched.
 * @param address       User-space virtual address to resolve.
 * @param _baseAddress  On success, receives the symbol base address.
 * @param _symbolName   On success, receives the symbol name.
 * @param _imageName    On success, receives the image name.
 * @param _exactMatch   On success, true if address lies within the symbol.
 * @retval B_OK           Symbol resolved.
 * @retval B_BAD_VALUE    team is NULL or is the kernel team.
 * @retval B_ENTRY_NOT_FOUND No image covers the address.
 */
/*!	Tries to find a matching user symbol for the given address.
	Note that the given team's address space must already be in effect.
*/
status_t
elf_debug_lookup_user_symbol_address(Team* team, addr_t address,
	addr_t *_baseAddress, const char **_symbolName, const char **_imageName,
	bool *_exactMatch)
{
	if (team == NULL || team == team_get_kernel_team())
		return B_BAD_VALUE;

	UserSymbolLookup& lookup = UserSymbolLookup::Default();
	status_t error = lookup.Init(team);
	if (error != B_OK)
		return error;

	return lookup.LookupSymbolAddress(address, _baseAddress, _symbolName,
		_imageName, _exactMatch);
}


/**
 * @brief Find the virtual address of a named symbol across all loaded kernel
 *        images; intended for use by the kernel debugger.
 *
 * Iterates all kernel images, checking debug symbol tables first, then
 * standard ELF hash tables.
 *
 * @note Does not acquire any locks; must only be called from the debugger.
 *
 * @param searchName Null-terminated symbol name to find.
 * @return Virtual address of the symbol, or 0 if not found.
 */
/*!	Looks up a symbol in all kernel images. Note, this function is thought to
	be used in the kernel debugger, and therefore doesn't perform any locking.
*/
addr_t
elf_debug_lookup_symbol(const char* searchName)
{
	struct elf_image_info *image = NULL;

	ImageHash::Iterator iterator(sImagesHash);
	while (iterator.HasNext()) {
		image = iterator.Next();
		if (image->num_debug_symbols > 0) {
			// search extended debug symbol table (contains static symbols)
			for (uint32 i = 0; i < image->num_debug_symbols; i++) {
				elf_sym *symbol = &image->debug_symbols[i];
				const char *name = image->debug_string_table + symbol->st_name;

				if (symbol->st_value > 0 && !strcmp(name, searchName))
					return symbol->st_value + image->text_region.delta;
			}
		} else {
			// search standard symbol lookup table
			for (uint32 i = 0; i < HASHTABSIZE(image); i++) {
				for (uint32 j = HASHBUCKETS(image)[i]; j != STN_UNDEF;
						j = HASHCHAINS(image)[j]) {
					elf_sym *symbol = &image->syms[j];
					const char *name = SYMNAME(image, symbol);

					if (symbol->st_value > 0 && !strcmp(name, searchName))
						return symbol->st_value + image->text_region.delta;
				}
			}
		}
	}

	return 0;
}


/**
 * @brief Look up a symbol by name in the kernel image and return its address
 *        and size.
 *
 * @param name  Null-terminated symbol name to find in sKernelImage.
 * @param info  Output structure receiving the symbol address and size.
 * @retval B_OK             Symbol found; @p info populated.
 * @retval B_MISSING_SYMBOL Symbol not present in the kernel image.
 */
status_t
elf_lookup_kernel_symbol(const char* name, elf_symbol_info* info)
{
	// find the symbol
	elf_sym* foundSymbol = elf_find_symbol(sKernelImage, name, NULL, false);
	if (foundSymbol == NULL)
		return B_MISSING_SYMBOL;

	info->address = foundSymbol->st_value + sKernelImage->text_region.delta;
	info->size = foundSymbol->st_size;
	return B_OK;
}


#endif // ELF32_COMPAT


/**
 * @brief Load the ELF runtime loader (or another ELF shared object) into a
 *        userland team's address space and return its entry point.
 *
 * Opens @p path, reads and verifies the ELF header, maps each PT_LOAD
 * segment into @p team's address space, applies dynamic relocations, sets
 * final segment protections, and registers the image. The entry point
 * (e_entry + ASLR delta) is written to @p entry.
 *
 * @param path  Filesystem path of the ELF file to load.
 * @param team  Target team whose address space receives the segments.
 * @param flags Load flags (e.g. ELF_LOAD_USER_IMAGE_TEST_EXECUTABLE).
 * @param entry On success, receives the ELF entry point virtual address.
 * @retval B_OK                 Image loaded successfully.
 * @retval B_NOT_AN_EXECUTABLE  ELF header invalid or segment mapping failed.
 * @retval B_NO_MEMORY          Heap or area allocation failure.
 * @retval other                I/O or relocation error.
 */
status_t
elf_load_user_image(const char *path, Team *team, uint32 flags, addr_t *entry)
{
	elf_ehdr elfHeader;
	char baseName[B_OS_NAME_LENGTH];
	status_t status;
	ssize_t length;

	TRACE(("elf_load: entry path '%s', team %p\n", path, team));

	int fd = _kern_open(-1, path, O_RDONLY, 0);
	if (fd < 0)
		return fd;
	FileDescriptorCloser fdCloser(fd);

	struct stat st;
	status = _kern_read_stat(fd, NULL, false, &st, sizeof(st));
	if (status != B_OK)
		return status;

	// read and verify the ELF header

	length = _kern_read(fd, 0, &elfHeader, sizeof(elfHeader));
	if (length < B_OK)
		return length;

	if (length != sizeof(elfHeader)) {
		// short read
		return B_NOT_AN_EXECUTABLE;
	}
	status = verify_eheader(&elfHeader);
	if (status < B_OK)
		return status;

#ifdef ELF_LOAD_USER_IMAGE_TEST_EXECUTABLE
	if ((flags & ELF_LOAD_USER_IMAGE_TEST_EXECUTABLE) != 0)
		return B_OK;
#endif

	struct elf_image_info* image;
	image = create_image_struct();
	if (image == NULL)
		return B_NO_MEMORY;
	CObjectDeleter<elf_image_info, void, delete_elf_image> imageDeleter(image);

	struct ElfHeaderUnsetter {
		ElfHeaderUnsetter(elf_image_info* image)
			: fImage(image)
		{
		}
		~ElfHeaderUnsetter()
		{
			fImage->elf_header = NULL;
		}

		elf_image_info* fImage;
	} headerUnsetter(image);
	image->elf_header = &elfHeader;

	// read program header

	elf_phdr *programHeaders = (elf_phdr *)malloc(
		elfHeader.e_phnum * elfHeader.e_phentsize);
	if (programHeaders == NULL) {
		dprintf("error allocating space for program headers\n");
		return B_NO_MEMORY;
	}
	MemoryDeleter headersDeleter(programHeaders);

	TRACE(("reading in program headers at 0x%lx, length 0x%x\n",
		elfHeader.e_phoff, elfHeader.e_phnum * elfHeader.e_phentsize));
	length = _kern_read(fd, elfHeader.e_phoff, programHeaders,
		elfHeader.e_phnum * elfHeader.e_phentsize);
	if (length < B_OK) {
		dprintf("error reading in program headers\n");
		return length;
	}
	if (length != elfHeader.e_phnum * elfHeader.e_phentsize) {
		dprintf("short read while reading in program headers\n");
		return B_ERROR;
	}

	// construct a nice name for the region we have to create below
	{
		int32 length;

		const char *leaf = strrchr(path, '/');
		if (leaf == NULL)
			leaf = path;
		else
			leaf++;

		length = strlen(leaf);
		if (length > B_OS_NAME_LENGTH - 16)
			snprintf(baseName, B_OS_NAME_LENGTH, "...%s", leaf + length + 16 - B_OS_NAME_LENGTH);
		else
			strcpy(baseName, leaf);
	}

	// map the program's segments into memory, initially with rw access
	// correct area protection will be set after relocation

	BStackOrHeapArray<area_id, 8> mappedAreas(elfHeader.e_phnum);
	if (!mappedAreas.IsValid())
		return B_NO_MEMORY;

	extended_image_info imageInfo;
	memset(&imageInfo, 0, sizeof(imageInfo));

	addr_t delta = 0;
	uint32 addressSpec = B_RANDOMIZED_BASE_ADDRESS;
	for (int i = 0; i < elfHeader.e_phnum; i++) {
		char regionName[B_OS_NAME_LENGTH];
		char *regionAddress;
		char *originalRegionAddress;
		area_id id;

		mappedAreas[i] = -1;

		if (programHeaders[i].p_type == PT_DYNAMIC) {
			image->dynamic_section = programHeaders[i].p_vaddr;
			continue;
		}

		if (programHeaders[i].p_type != PT_LOAD)
			continue;

		regionAddress = (char *)(ROUNDDOWN(programHeaders[i].p_vaddr,
			B_PAGE_SIZE) + delta);
		originalRegionAddress = regionAddress;

		if (programHeaders[i].p_flags & PF_WRITE) {
			// rw/data segment
			size_t memUpperBound = (programHeaders[i].p_vaddr % B_PAGE_SIZE)
				+ programHeaders[i].p_memsz;
			size_t fileUpperBound = (programHeaders[i].p_vaddr % B_PAGE_SIZE)
				+ programHeaders[i].p_filesz;

			memUpperBound = ROUNDUP(memUpperBound, B_PAGE_SIZE);
			fileUpperBound = ROUNDUP(fileUpperBound, B_PAGE_SIZE);

			snprintf(regionName, B_OS_NAME_LENGTH, "%s_seg%drw", baseName, i);

			id = vm_map_file(team->id, regionName, (void **)&regionAddress,
				addressSpec, fileUpperBound,
				B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
				REGION_PRIVATE_MAP, false, fd,
				ROUNDDOWN(programHeaders[i].p_offset, B_PAGE_SIZE));
			if (id < B_OK) {
				dprintf("error mapping file data: %s!\n", strerror(id));
				return B_NOT_AN_EXECUTABLE;
			}
			mappedAreas[i] = id;

			imageInfo.basic_info.data = regionAddress;
			imageInfo.basic_info.data_size = memUpperBound;

			image->data_region.start = (addr_t)regionAddress;
			image->data_region.size = memUpperBound;

			// clean garbage brought by mmap (the region behind the file,
			// at least parts of it are the bss and have to be zeroed)
			addr_t start = (addr_t)regionAddress
				+ (programHeaders[i].p_vaddr % B_PAGE_SIZE)
				+ programHeaders[i].p_filesz;
			size_t amount = fileUpperBound
				- (programHeaders[i].p_vaddr % B_PAGE_SIZE)
				- (programHeaders[i].p_filesz);
			memset((void *)start, 0, amount);

			// Check if we need extra storage for the bss - we have to do this if
			// the above region doesn't already comprise the memory size, too.

			if (memUpperBound != fileUpperBound) {
				size_t bssSize = memUpperBound - fileUpperBound;

				snprintf(regionName, B_OS_NAME_LENGTH, "%s_bss%d", baseName, i);

				regionAddress += fileUpperBound;
				virtual_address_restrictions virtualRestrictions = {};
				virtualRestrictions.address = regionAddress;
				virtualRestrictions.address_specification = B_EXACT_ADDRESS;
				physical_address_restrictions physicalRestrictions = {};
				id = create_area_etc(team->id, regionName, bssSize, B_NO_LOCK,
					B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, 0, 0, &virtualRestrictions,
					&physicalRestrictions, (void**)&regionAddress);
				if (id < B_OK) {
					dprintf("error allocating bss area: %s!\n", strerror(id));
					return B_NOT_AN_EXECUTABLE;
				}
			}
		} else {
			// assume ro/text segment
			snprintf(regionName, B_OS_NAME_LENGTH, "%s_seg%drx", baseName, i);

			size_t segmentSize = ROUNDUP(programHeaders[i].p_memsz
				+ (programHeaders[i].p_vaddr % B_PAGE_SIZE), B_PAGE_SIZE);

			id = vm_map_file(team->id, regionName, (void **)&regionAddress,
				addressSpec, segmentSize,
				B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
				REGION_PRIVATE_MAP, false, fd,
				ROUNDDOWN(programHeaders[i].p_offset, B_PAGE_SIZE));
			if (id < B_OK) {
				dprintf("error mapping file text: %s!\n", strerror(id));
				return B_NOT_AN_EXECUTABLE;
			}

			mappedAreas[i] = id;

			imageInfo.basic_info.text = regionAddress;
			imageInfo.basic_info.text_size = segmentSize;

			image->text_region.start = (addr_t)regionAddress;
			image->text_region.size = segmentSize;
		}

		if (addressSpec != B_EXACT_ADDRESS) {
			addressSpec = B_EXACT_ADDRESS;
			delta = regionAddress - originalRegionAddress;
		}
	}

	image->data_region.delta = delta;
	image->text_region.delta = delta;

	// modify the dynamic ptr by the delta of the regions
	image->dynamic_section += image->text_region.delta;

	status = elf_parse_dynamic_section(image);
	if (status != B_OK)
		return status;

	status = elf_relocate(image, image);
	if (status != B_OK)
		return status;

	// set correct area protection
	for (int i = 0; i < elfHeader.e_phnum; i++) {
		if (mappedAreas[i] == -1)
			continue;

		uint32 protection = 0;
		if (programHeaders[i].p_flags & PF_EXECUTE)
			protection |= B_EXECUTE_AREA;
		if (programHeaders[i].p_flags & PF_WRITE)
			protection |= B_WRITE_AREA;
		if (programHeaders[i].p_flags & PF_READ)
			protection |= B_READ_AREA;

		status = vm_set_area_protection(mappedAreas[i], protection,
			true);
		if (status != B_OK)
			return status;
	}

	// register the loaded image
	imageInfo.basic_info.type = B_LIBRARY_IMAGE;
	imageInfo.basic_info.device = st.st_dev;
	imageInfo.basic_info.node = st.st_ino;
	strlcpy(imageInfo.basic_info.name, path, sizeof(imageInfo.basic_info.name));

	imageInfo.basic_info.api_version = B_HAIKU_VERSION;
	imageInfo.basic_info.abi = B_HAIKU_ABI;
		// TODO: Get the actual values for the shared object. Currently only
		// the runtime loader is loaded, so this is good enough for the time
		// being.

	imageInfo.text_delta = delta;
	imageInfo.symbol_table = image->syms;
	imageInfo.symbol_hash = image->symhash;
	imageInfo.string_table = image->strtab;

	imageInfo.basic_info.id = register_image(team, &imageInfo,
		sizeof(imageInfo));
	if (imageInfo.basic_info.id >= 0 && team_get_current_team_id() == team->id)
		user_debug_image_created(&imageInfo.basic_info);
		// Don't care, if registering fails. It's not crucial.

	TRACE(("elf_load: done!\n"));

	*entry = elfHeader.e_entry + delta;
	return B_OK;
}


#ifndef ELF32_COMPAT

/**
 * @brief Load an ELF binary as a kernel add-on and return its image_id.
 *
 * Opens @p path, reads and validates the ELF header, reserves kernel address
 * space for all PT_LOAD segments, maps them with full-lock areas, applies
 * dynamic section parsing, version checks, and relocations, then marks the
 * text segment read-only/executable and registers the image. If the add-on is
 * already loaded (same vnode) the reference count is incremented instead of
 * reloading.
 *
 * @param path Filesystem path of the kernel add-on to load.
 * @return image_id of the loaded add-on on success, or a negative error code.
 * @retval B_NO_MEMORY          Allocation failure.
 * @retval B_NOT_AN_EXECUTABLE  Invalid ELF header or segment mapping error.
 * @retval B_BAD_DATA           Unreasonable gap between segments.
 * @retval other                I/O, version, or relocation error.
 */
image_id
load_kernel_add_on(const char *path)
{
	elf_phdr *programHeaders;
	elf_ehdr *elfHeader;
	struct elf_image_info *image;
	const char *fileName;
	void *reservedAddress;
	size_t reservedSize;
	status_t status;
	ssize_t length;
	bool textSectionWritable = false;
	int executableHeaderCount = 0;

	TRACE(("elf_load_kspace: entry path '%s'\n", path));

	int fd = _kern_open(-1, path, O_RDONLY, 0);
	if (fd < 0)
		return fd;

	struct vnode *vnode;
	status = vfs_get_vnode_from_fd(fd, true, &vnode);
	if (status < B_OK)
		goto error0;

	// get the file name
	fileName = strrchr(path, '/');
	if (fileName == NULL)
		fileName = path;
	else
		fileName++;

	// Prevent someone else from trying to load this image
	mutex_lock(&sImageLoadMutex);

	// make sure it's not loaded already. Search by vnode
	image = find_image_by_vnode(vnode);
	if (image) {
		atomic_add(&image->ref_count, 1);
		goto done;
	}

	elfHeader = (elf_ehdr *)malloc(sizeof(*elfHeader));
	if (!elfHeader) {
		status = B_NO_MEMORY;
		goto error;
	}

	length = _kern_read(fd, 0, elfHeader, sizeof(*elfHeader));
	if (length < B_OK) {
		status = length;
		goto error1;
	}
	if (length != sizeof(*elfHeader)) {
		// short read
		status = B_NOT_AN_EXECUTABLE;
		goto error1;
	}
	status = verify_eheader(elfHeader);
	if (status < B_OK)
		goto error1;

	image = create_image_struct();
	if (!image) {
		status = B_NO_MEMORY;
		goto error1;
	}
	image->vnode = vnode;
	image->elf_header = elfHeader;
	image->name = strdup(path);
	vnode = NULL;

	programHeaders = (elf_phdr *)malloc(elfHeader->e_phnum
		* elfHeader->e_phentsize);
	if (programHeaders == NULL) {
		dprintf("%s: error allocating space for program headers\n", fileName);
		status = B_NO_MEMORY;
		goto error2;
	}

	TRACE(("reading in program headers at 0x%lx, length 0x%x\n",
		elfHeader->e_phoff, elfHeader->e_phnum * elfHeader->e_phentsize));

	length = _kern_read(fd, elfHeader->e_phoff, programHeaders,
		elfHeader->e_phnum * elfHeader->e_phentsize);
	if (length < B_OK) {
		status = length;
		TRACE(("%s: error reading in program headers\n", fileName));
		goto error3;
	}
	if (length != elfHeader->e_phnum * elfHeader->e_phentsize) {
		TRACE(("%s: short read while reading in program headers\n", fileName));
		status = B_ERROR;
		goto error3;
	}

	// determine how much space we need for all loaded segments

	reservedSize = 0;
	length = 0;

	for (int32 i = 0; i < elfHeader->e_phnum; i++) {
		size_t end;

		if (programHeaders[i].p_type != PT_LOAD)
			continue;

		length += ROUNDUP(programHeaders[i].p_memsz
			+ (programHeaders[i].p_vaddr % B_PAGE_SIZE), B_PAGE_SIZE);

		end = ROUNDUP(programHeaders[i].p_memsz + programHeaders[i].p_vaddr,
			B_PAGE_SIZE);
		if (end > reservedSize)
			reservedSize = end;

		if (programHeaders[i].IsExecutable())
			executableHeaderCount++;
	}

	// Check whether the segments have an unreasonable amount of unused space
	// inbetween.
	if ((ssize_t)reservedSize > length + 8 * 1024) {
		status = B_BAD_DATA;
		goto error1;
	}

	// reserve that space and allocate the areas from that one
	if (vm_reserve_address_range(VMAddressSpace::KernelID(), &reservedAddress,
			B_ANY_KERNEL_ADDRESS, reservedSize, 0) < B_OK) {
		status = B_NO_MEMORY;
		goto error3;
	}

	image->data_region.size = 0;
	image->text_region.size = 0;

	for (int32 i = 0; i < elfHeader->e_phnum; i++) {
		char regionName[B_OS_NAME_LENGTH];
		elf_region *region;

		TRACE(("looking at program header %" B_PRId32 "\n", i));

		switch (programHeaders[i].p_type) {
			case PT_LOAD:
				break;
			case PT_DYNAMIC:
				image->dynamic_section = programHeaders[i].p_vaddr;
				continue;
			case PT_INTERP:
				// should check here for appropriate interpreter
				continue;
			case PT_PHDR:
			case PT_STACK:
				// we don't use it
				continue;
			case PT_EH_FRAME:
				// not implemented yet, but can be ignored
				continue;
			case PT_ARM_UNWIND:
				continue;
			case PT_RISCV_ATTRIBUTES:
				// TODO: check ABI compatibility attributes
				continue;
			default:
				dprintf("%s: unhandled pheader type %#" B_PRIx32 "\n", fileName,
					programHeaders[i].p_type);
				continue;
		}

		// we're here, so it must be a PT_LOAD segment

		// Usually add-ons have two PT_LOAD headers: one for .data one or .text.
		// x86 and PPC may differ in permission bits for .data's PT_LOAD header
		// x86 is usually RW, PPC is RWE

		// Some add-ons may have .text and .data concatenated in a single
		// PT_LOAD RWE header and we must map that to .text.
		if (programHeaders[i].IsReadWrite()
			&& (!programHeaders[i].IsExecutable()
				|| executableHeaderCount > 1)) {
			// this is the writable segment
			if (image->data_region.size != 0) {
				// we've already created this segment
				continue;
			}
			region = &image->data_region;

			snprintf(regionName, B_OS_NAME_LENGTH, "%s_data", fileName);
		} else if (programHeaders[i].IsExecutable()) {
			// this is the non-writable segment
			if (image->text_region.size != 0) {
				// we've already created this segment
				continue;
			}
			region = &image->text_region;

			// some programs may have .text and .data concatenated in a
			// single PT_LOAD section which is readable/writable/executable
			textSectionWritable = programHeaders[i].IsReadWrite();
			snprintf(regionName, B_OS_NAME_LENGTH, "%s_text", fileName);
		} else {
			dprintf("%s: weird program header flags %#" B_PRIx32 "\n", fileName,
				programHeaders[i].p_flags);
			continue;
		}

		region->start = (addr_t)reservedAddress + ROUNDDOWN(
			programHeaders[i].p_vaddr, B_PAGE_SIZE);
		region->size = ROUNDUP(programHeaders[i].p_memsz
			+ (programHeaders[i].p_vaddr % B_PAGE_SIZE), B_PAGE_SIZE);
		region->id = create_area(regionName, (void **)&region->start,
			B_EXACT_ADDRESS, region->size, B_FULL_LOCK,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
		if (region->id < B_OK) {
			dprintf("%s: error allocating area: %s\n", fileName,
				strerror(region->id));
			status = B_NOT_AN_EXECUTABLE;
			goto error4;
		}
		region->delta = -ROUNDDOWN(programHeaders[i].p_vaddr, B_PAGE_SIZE);

		TRACE(("elf_load_kspace: created area \"%s\" at %p\n",
			regionName, (void *)region->start));

		length = _kern_read(fd, programHeaders[i].p_offset,
			(void *)(region->start + (programHeaders[i].p_vaddr % B_PAGE_SIZE)),
			programHeaders[i].p_filesz);
		if (length < B_OK) {
			status = length;
			dprintf("%s: error reading in segment %" B_PRId32 "\n", fileName,
				i);
			goto error5;
		}
	}

	image->data_region.delta += image->data_region.start;
	image->text_region.delta += image->text_region.start;

	// modify the dynamic ptr by the delta of the regions
	image->dynamic_section += image->text_region.delta;

	status = elf_parse_dynamic_section(image);
	if (status < B_OK)
		goto error5;

	status = init_image_version_infos(image);
	if (status != B_OK)
		goto error5;

	status = check_needed_image_versions(image);
	if (status != B_OK)
		goto error5;

	status = elf_relocate(image, sKernelImage);
	if (status < B_OK)
		goto error5;

	// We needed to read in the contents of the "text" area, but
	// now we can protect it read-only/execute, unless this is a
	// special image with concatenated .text and .data, when it
	// will also need write access.
	set_area_protection(image->text_region.id,
		B_KERNEL_READ_AREA | B_KERNEL_EXECUTE_AREA
		| (textSectionWritable ? B_KERNEL_WRITE_AREA : 0));

	// There might be a hole between the two segments, and we don't need to
	// reserve this any longer
	vm_unreserve_address_range(VMAddressSpace::KernelID(), reservedAddress,
		reservedSize);

	if (sLoadElfSymbols)
		load_elf_symbol_table(fd, image);

	free(programHeaders);
	mutex_lock(&sImageMutex);
	register_elf_image(image);
	mutex_unlock(&sImageMutex);

done:
	_kern_close(fd);
	mutex_unlock(&sImageLoadMutex);

	return image->id;

error5:
error4:
	vm_unreserve_address_range(VMAddressSpace::KernelID(), reservedAddress,
		reservedSize);
error3:
	free(programHeaders);
error2:
	delete_elf_image(image);
	elfHeader = NULL;
error1:
	free(elfHeader);
error:
	mutex_unlock(&sImageLoadMutex);
error0:
	dprintf("Could not load kernel add-on \"%s\": %s\n", path,
		strerror(status));

	if (vnode)
		vfs_put_vnode(vnode);
	_kern_close(fd);

	return status;
}


/**
 * @brief Unload a kernel add-on previously loaded with load_kernel_add_on().
 *
 * Acquires both sImageLoadMutex and sImageMutex, looks up the image by @p id,
 * and calls unload_elf_image() to decrement its reference count (and destroy
 * it when it reaches zero).
 *
 * @param id image_id returned by load_kernel_add_on().
 * @retval B_OK           Add-on unloaded (or reference count decremented).
 * @retval B_BAD_IMAGE_ID No image with the given id is loaded.
 */
status_t
unload_kernel_add_on(image_id id)
{
	MutexLocker _(sImageLoadMutex);
	MutexLocker _2(sImageMutex);

	elf_image_info *image = find_image(id);
	if (image == NULL)
		return B_BAD_IMAGE_ID;

	unload_elf_image(image);
	return B_OK;
}


/**
 * @brief Return a pointer to the elf_image_info for the kernel itself.
 *
 * @return Pointer to sKernelImage (never NULL after elf_init()).
 */
struct elf_image_info*
elf_get_kernel_image()
{
	return sKernelImage;
}


/**
 * @brief Fill in a generic image_info for the kernel ELF image that contains
 *        the given virtual address.
 *
 * @param address Virtual address that must lie within the image.
 * @param info    Output image_info structure to populate.
 * @retval B_OK             Image found; @p info populated.
 * @retval B_ENTRY_NOT_FOUND No kernel image covers @p address.
 */
status_t
elf_get_image_info_for_address(addr_t address, image_info* info)
{
	MutexLocker _(sImageMutex);
	struct elf_image_info* elfInfo = find_image_at_address(address);
	if (elfInfo == NULL)
		return B_ENTRY_NOT_FOUND;

	info->id = elfInfo->id;
	info->type = B_SYSTEM_IMAGE;
	info->sequence = 0;
	info->init_order = 0;
	info->init_routine = NULL;
	info->term_routine = NULL;
	info->device = -1;
	info->node = -1;
		// TODO: We could actually fill device/node in.
	strlcpy(info->name, elfInfo->name, sizeof(info->name));
	info->text = (void*)elfInfo->text_region.start;
	info->data = (void*)elfInfo->data_region.start;
	info->text_size = elfInfo->text_region.size;
	info->data_size = elfInfo->data_region.size;

	return B_OK;
}


/**
 * @brief Create a kernel image record for an ELF object that is already
 *        mapped into memory (e.g. the commpage or JIT regions).
 *
 * Allocates an elf_image_info, attaches empty debug symbol and string tables
 * (so that elf_debug_lookup_symbol_address() skips the absent dynamic table),
 * and registers the image. Symbols can be added afterwards with
 * elf_add_memory_image_symbol().
 *
 * @param imageName Name string for the image.
 * @param text      Start address of the text (code) region.
 * @param textSize  Size of the text region in bytes.
 * @param data      Start address of the data region.
 * @param dataSize  Size of the data region in bytes.
 * @return image_id of the new image, or B_NO_MEMORY on allocation failure.
 */
image_id
elf_create_memory_image(const char* imageName, addr_t text, size_t textSize,
	addr_t data, size_t dataSize)
{
	// allocate the image
	elf_image_info* image = create_image_struct();
	if (image == NULL)
		return B_NO_MEMORY;
	MemoryDeleter imageDeleter(image);

	// allocate symbol and string tables -- we allocate an empty symbol table,
	// so that elf_debug_lookup_symbol_address() won't try the dynamic symbol
	// table, which we don't have.
	elf_sym* symbolTable = (elf_sym*)malloc(0);
	char* stringTable = (char*)malloc(1);
	MemoryDeleter symbolTableDeleter(symbolTable);
	MemoryDeleter stringTableDeleter(stringTable);
	if (symbolTable == NULL || stringTable == NULL)
		return B_NO_MEMORY;

	// the string table always contains the empty string
	stringTable[0] = '\0';

	image->debug_symbols = symbolTable;
	image->num_debug_symbols = 0;
	image->debug_string_table = stringTable;

	// dup image name
	image->name = strdup(imageName);
	if (image->name == NULL)
		return B_NO_MEMORY;

	// data and text region
	image->text_region.id = -1;
	image->text_region.start = text;
	image->text_region.size = textSize;
	image->text_region.delta = 0;

	image->data_region.id = -1;
	image->data_region.start = data;
	image->data_region.size = dataSize;
	image->data_region.delta = 0;

	mutex_lock(&sImageMutex);
	register_elf_image(image);
	image_id imageID = image->id;
	mutex_unlock(&sImageMutex);

	// keep the allocated memory
	imageDeleter.Detach();
	symbolTableDeleter.Detach();
	stringTableDeleter.Detach();

	return imageID;
}


/**
 * @brief Add a named symbol entry to an in-memory image's debug symbol table.
 *
 * Grows the image's debug_symbols and debug_string_table arrays and appends
 * a new elf_sym. The image must have been created with
 * elf_create_memory_image().
 *
 * @param id      image_id of the target memory image.
 * @param name    Null-terminated symbol name (may be NULL for an unnamed entry).
 * @param address Virtual address of the symbol.
 * @param size    Size of the symbol in bytes.
 * @param type    B_SYMBOL_TYPE_DATA or B_SYMBOL_TYPE_TEXT.
 * @retval B_OK             Symbol added.
 * @retval B_ENTRY_NOT_FOUND No image with the given id.
 * @retval B_NO_MEMORY      Reallocation of symbol or string table failed.
 */
status_t
elf_add_memory_image_symbol(image_id id, const char* name, addr_t address,
	size_t size, int32 type)
{
	MutexLocker _(sImageMutex);

	// get the image
	struct elf_image_info* image = find_image(id);
	if (image == NULL)
		return B_ENTRY_NOT_FOUND;

	// get the current string table size
	size_t stringTableSize = 1;
	if (image->num_debug_symbols > 0) {
		for (int32 i = image->num_debug_symbols - 1; i >= 0; i--) {
			int32 nameIndex = image->debug_symbols[i].st_name;
			if (nameIndex != 0) {
				stringTableSize = nameIndex
					+ strlen(image->debug_string_table + nameIndex) + 1;
				break;
			}
		}
	}

	// enter the name in the string table
	char* stringTable = (char*)image->debug_string_table;
	size_t stringIndex = 0;
	if (name != NULL) {
		size_t nameSize = strlen(name) + 1;
		stringIndex = stringTableSize;
		stringTableSize += nameSize;
		stringTable = (char*)realloc((char*)image->debug_string_table,
			stringTableSize);
		if (stringTable == NULL)
			return B_NO_MEMORY;
		image->debug_string_table = stringTable;
		memcpy(stringTable + stringIndex, name, nameSize);
	}

	// resize the symbol table
	int32 symbolCount = image->num_debug_symbols + 1;
	elf_sym* symbolTable = (elf_sym*)realloc(
		(elf_sym*)image->debug_symbols, sizeof(elf_sym) * symbolCount);
	if (symbolTable == NULL)
		return B_NO_MEMORY;
	image->debug_symbols = symbolTable;

	// enter the symbol
	elf_sym& symbol = symbolTable[symbolCount - 1];
	symbol.SetInfo(STB_GLOBAL,
		type == B_SYMBOL_TYPE_DATA ? STT_OBJECT : STT_FUNC);
	symbol.st_name = stringIndex;
	symbol.st_value = address;
	symbol.st_size = size;
	symbol.st_other = 0;
	symbol.st_shndx = 0;
	image->num_debug_symbols++;

	return B_OK;
}


/**
 * @brief Read the symbol and string tables of a loaded kernel image into
 *        caller-supplied buffers.
 *
 * Both @p _symbolCount and @p _stringTableSize are in/out: on entry they
 * give the buffer capacities; on return they hold the actual sizes (which
 * may exceed the capacities). As much data as fits is copied. Passing NULL
 * for either table retrieves only the required sizes.
 *
 * @param id               image_id to read symbols from.
 * @param symbolTable      Output buffer for elf_sym entries (may be NULL).
 * @param _symbolCount     In: buffer capacity in symbols; out: total symbols.
 * @param stringTable      Output buffer for the string table (may be NULL).
 * @param _stringTableSize In: buffer size in bytes; out: total string table size.
 * @param _imageDelta      On success, receives the image load delta (may be NULL).
 * @param kernel           True if called from kernel context (uses memcpy),
 *                         false if from user context (uses user_memcpy).
 * @retval B_OK           Data (partially) copied; sizes updated.
 * @retval B_BAD_VALUE    NULL required output pointer.
 * @retval B_BAD_ADDRESS  Non-kernel caller passed a kernel pointer.
 * @retval B_ENTRY_NOT_FOUND No image with the given id.
 */
/*!	Reads the symbol and string table for the kernel image with the given ID.
	\a _symbolCount and \a _stringTableSize are both in- and output parameters.
	When called they call the size of the buffers given by \a symbolTable and
	\a stringTable respectively. When the function returns successfully, they
	will contain the actual sizes (which can be greater than the original ones).
	The function will copy as much as possible into the buffers. For only
	getting the required buffer sizes, it can be invoked with \c NULL buffers.
	On success \a _imageDelta will contain the offset to be added to the symbol
	values in the table to get the actual symbol addresses.
*/
status_t
elf_read_kernel_image_symbols(image_id id, elf_sym* symbolTable,
	int32* _symbolCount, char* stringTable, size_t* _stringTableSize,
	addr_t* _imageDelta, bool kernel)
{
	// check params
	if (_symbolCount == NULL || _stringTableSize == NULL)
		return B_BAD_VALUE;
	if (!kernel) {
		if (!IS_USER_ADDRESS(_symbolCount) || !IS_USER_ADDRESS(_stringTableSize)
			|| (_imageDelta != NULL && !IS_USER_ADDRESS(_imageDelta))
			|| (symbolTable != NULL && !IS_USER_ADDRESS(symbolTable))
			|| (stringTable != NULL && !IS_USER_ADDRESS(stringTable))) {
			return B_BAD_ADDRESS;
		}
	}

	// get buffer sizes
	int32 maxSymbolCount;
	size_t maxStringTableSize;
	if (kernel) {
		maxSymbolCount = *_symbolCount;
		maxStringTableSize = *_stringTableSize;
	} else {
		if (user_memcpy(&maxSymbolCount, _symbolCount, sizeof(maxSymbolCount))
				!= B_OK
			|| user_memcpy(&maxStringTableSize, _stringTableSize,
				sizeof(maxStringTableSize)) != B_OK) {
			return B_BAD_ADDRESS;
		}
	}

	// find the image
	MutexLocker _(sImageMutex);
	struct elf_image_info* image = find_image(id);
	if (image == NULL)
		return B_ENTRY_NOT_FOUND;

	// get the tables and infos
	addr_t imageDelta = image->text_region.delta;
	const elf_sym* symbols;
	int32 symbolCount;
	const char* strings;

	if (image->debug_symbols != NULL) {
		symbols = image->debug_symbols;
		symbolCount = image->num_debug_symbols;
		strings = image->debug_string_table;
	} else {
		symbols = image->syms;
		symbolCount = image->symhash[1];
		strings = image->strtab;
	}

	// The string table size isn't stored in the elf_image_info structure. Find
	// out by iterating through all symbols.
	size_t stringTableSize = 0;
	for (int32 i = 0; i < symbolCount; i++) {
		size_t index = symbols[i].st_name;
		if (index > stringTableSize)
			stringTableSize = index;
	}
	stringTableSize += strlen(strings + stringTableSize) + 1;
		// add size of the last string

	// copy symbol table
	int32 symbolsToCopy = min_c(symbolCount, maxSymbolCount);
	if (symbolTable != NULL && symbolsToCopy > 0) {
		if (kernel) {
			memcpy(symbolTable, symbols, sizeof(elf_sym) * symbolsToCopy);
		} else if (user_memcpy(symbolTable, symbols,
				sizeof(elf_sym) * symbolsToCopy) != B_OK) {
			return B_BAD_ADDRESS;
		}
	}

	// copy string table
	size_t stringsToCopy = min_c(stringTableSize, maxStringTableSize);
	if (stringTable != NULL && stringsToCopy > 0) {
		if (kernel) {
			memcpy(stringTable, strings, stringsToCopy);
		} else {
			if (user_memcpy(stringTable, strings, stringsToCopy)
					!= B_OK) {
				return B_BAD_ADDRESS;
			}
		}
	}

	// copy sizes
	if (kernel) {
		*_symbolCount = symbolCount;
		*_stringTableSize = stringTableSize;
		if (_imageDelta != NULL)
			*_imageDelta = imageDelta;
	} else {
		if (user_memcpy(_symbolCount, &symbolCount, sizeof(symbolCount)) != B_OK
			|| user_memcpy(_stringTableSize, &stringTableSize,
					sizeof(stringTableSize)) != B_OK
			|| (_imageDelta != NULL && user_memcpy(_imageDelta, &imageDelta,
					sizeof(imageDelta)) != B_OK)) {
			return B_BAD_ADDRESS;
		}
	}

	return B_OK;
}


/**
 * @brief Initialise the kernel ELF loader subsystem.
 *
 * Called early in kernel startup. Reads the "load_symbols" driver setting,
 * allocates and initialises the global image hash table, inserts the kernel
 * image and all boot-loader preloaded images, and registers kernel debugger
 * commands (ls, symbols, symbol, image).
 *
 * @param args Kernel arguments passed from the boot loader.
 * @retval B_OK        Initialisation successful.
 * @retval B_NO_MEMORY Hash table allocation failed.
 */
status_t
elf_init(kernel_args* args)
{
	struct preloaded_image* image;

	image_init();

	if (void* handle = load_driver_settings("kernel")) {
		sLoadElfSymbols = get_driver_boolean_parameter(handle, "load_symbols",
			false, false);

		unload_driver_settings(handle);
	}

	sImagesHash = new(std::nothrow) ImageHash();
	if (sImagesHash == NULL)
		return B_NO_MEMORY;
	status_t init = sImagesHash->Init(IMAGE_HASH_SIZE);
	if (init != B_OK)
		return init;

	// Build a image structure for the kernel, which has already been loaded.
	// The preloaded_images were already prepared by the VM.
	image = args->kernel_image;
	if (insert_preloaded_image(static_cast<preloaded_elf_image *>(image),
			true) < B_OK)
		panic("could not create kernel image.\n");

	// Build image structures for all preloaded images.
	for (image = args->preloaded_images; image != NULL; image = image->next)
		insert_preloaded_image(static_cast<preloaded_elf_image *>(image),
			false);

	add_debugger_command("ls", &dump_address_info,
		"lookup symbol for a particular address");
	add_debugger_command("symbols", &dump_symbols, "dump symbols for image");
	add_debugger_command("symbol", &dump_symbol, "search symbol in images");
	add_debugger_command_etc("image", &dump_image, "dump image info",
		"Prints info about the specified image.\n"
		"  <image>  - pointer to the semaphore structure, or ID\n"
		"           of the image to print info for.\n", 0);

	sInitialized = true;
	return B_OK;
}


// #pragma mark -


/**
 * @brief Syscall wrapper: read the symbol and string table for a kernel image.
 *
 * Thin wrapper around elf_read_kernel_image_symbols() with kernel=false,
 * performing user-address validation and user_memcpy transfers.
 *
 * @param id               image_id to read symbols from.
 * @param symbolTable      User-space output buffer for elf_sym entries.
 * @param _symbolCount     User-space in/out: buffer capacity / total count.
 * @param stringTable      User-space output buffer for the string table.
 * @param _stringTableSize User-space in/out: buffer size / total size.
 * @param _imageDelta      User-space output for the image load delta.
 * @retval B_OK           Success; see elf_read_kernel_image_symbols().
 * @retval B_BAD_ADDRESS  Non-user pointer passed for an output parameter.
 */
/*!	Reads the symbol and string table for the kernel image with the given ID.
	\a _symbolCount and \a _stringTableSize are both in- and output parameters.
	When called they call the size of the buffers given by \a symbolTable and
	\a stringTable respectively. When the function returns successfully, they
	will contain the actual sizes (which can be greater than the original ones).
	The function will copy as much as possible into the buffers. For only
	getting the required buffer sizes, it can be invoked with \c NULL buffers.
	On success \a _imageDelta will contain the offset to be added to the symbol
	values in the table to get the actual symbol addresses.
*/
status_t
_user_read_kernel_image_symbols(image_id id, elf_sym* symbolTable,
	int32* _symbolCount, char* stringTable, size_t* _stringTableSize,
	addr_t* _imageDelta)
{
	return elf_read_kernel_image_symbols(id, symbolTable, _symbolCount,
		stringTable, _stringTableSize, _imageDelta, false);
}

#endif // ELF32_COMPAT
