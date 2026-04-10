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
 *   Copyright 2018, Jérôme Duval, jerome.duval@gmail.com.
 *   Copyright 2007, Travis Geiselbrecht. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file commpage.cpp
 *  @brief Communication page shared between the kernel and every user team.
 *
 * The commpage is a small read-only/execute area mapped at a known address
 * in every team's address space. The kernel exposes a fixed table of slots
 * (e.g. fast syscall stubs, vDSO-style helpers) and user code dispatches
 * through them. This file allocates the page, fills the table, and clones
 * it into new teams as they are created. */


#ifdef COMMPAGE_COMPAT
#include <commpage_compat.h>
#else
#include <commpage.h>
#endif

#include <string.h>

#include <KernelExport.h>

#include <elf.h>
#include <vm/vm.h>
#include <vm/vm_types.h>

#ifndef ADDRESS_TYPE
#define ADDRESS_TYPE addr_t
#endif

static area_id	sCommPageArea;
static ADDRESS_TYPE*	sCommPageAddress;
static void*	sFreeCommPageSpace;
static image_id	sCommPageImage;


#define ALIGN_ENTRY(pointer)	(void*)ROUNDUP((addr_t)(pointer), 8)


/** @brief Reserves @p size bytes of commpage payload and links it from slot @p entry.
 *  @param entry Index into the commpage table.
 *  @param size  Number of bytes to reserve, aligned up to 8 bytes.
 *  @return Pointer to the reserved payload inside the commpage. */
void*
allocate_commpage_entry(int entry, size_t size)
{
	void* space = sFreeCommPageSpace;
	sFreeCommPageSpace = ALIGN_ENTRY((addr_t)sFreeCommPageSpace + size);
	sCommPageAddress[entry] = (addr_t)space - (addr_t)sCommPageAddress;
	dprintf("allocate_commpage_entry(%d, %lu) -> %p\n", entry, size,
		(void*)sCommPageAddress[entry]);
	return space;
}


/** @brief Allocates a commpage slot and copies @p copyFrom into it.
 *  @return Offset of the new payload from the commpage base. */
addr_t
fill_commpage_entry(int entry, const void* copyFrom, size_t size)
{
	void* space = allocate_commpage_entry(entry, size);
	memcpy(space, copyFrom, size);
	return (addr_t)space - (addr_t)sCommPageAddress;
}


/** @brief Returns the synthetic ELF image id describing the commpage. */
image_id
get_commpage_image()
{
	return sCommPageImage;
}


/** @brief Maps the commpage into @p team's address space.
 *  @param team    The team to map into.
 *  @param address On entry the requested base (or NULL); on return the actual base.
 *  @return The new area id, or a negative error code. */
area_id
clone_commpage_area(team_id team, void** address)
{
	if (*address == NULL)
		*address = (void*)KERNEL_USER_DATA_BASE;
	return vm_clone_area(team, "commpage", address,
		B_RANDOMIZED_BASE_ADDRESS, B_READ_AREA | B_EXECUTE_AREA | B_KERNEL_AREA,
		REGION_PRIVATE_MAP, sCommPageArea, true);
}


/** @brief Creates the commpage area, fills the header, and registers its ELF image. */
status_t
commpage_init(void)
{
	// create a read/write kernel area
	sCommPageArea = create_area("kernel_commpage", (void **)&sCommPageAddress,
		B_ANY_ADDRESS, COMMPAGE_SIZE, B_FULL_LOCK,
		B_KERNEL_WRITE_AREA | B_KERNEL_READ_AREA);

	// zero it out
	memset(sCommPageAddress, 0, COMMPAGE_SIZE);

	// fill in some of the table
	sCommPageAddress[0] = COMMPAGE_SIGNATURE;
	sCommPageAddress[1] = COMMPAGE_VERSION;

	// the next slot to allocate space is after the table
	sFreeCommPageSpace = ALIGN_ENTRY(&sCommPageAddress[COMMPAGE_TABLE_ENTRIES]);

	// create the image for the commpage
	sCommPageImage = elf_create_memory_image("commpage", 0, COMMPAGE_SIZE, 0,
		0);
	elf_add_memory_image_symbol(sCommPageImage, "commpage_table",
		0, COMMPAGE_TABLE_ENTRIES * sizeof(addr_t),
		B_SYMBOL_TYPE_DATA);

	arch_commpage_init();

	return B_OK;
}


/** @brief Late-init hook called once secondary CPUs are running. */
status_t
commpage_init_post_cpus(void)
{
	return arch_commpage_init_post_cpus();
}
