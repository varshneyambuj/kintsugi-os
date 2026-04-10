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
 *   Copyright 2005-2008, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file boot_item.cpp
 *  @brief Named bag of bootloader-supplied data items handed to kernel subsystems. */


#include <boot_item.h>
#include <util/DoublyLinkedList.h>
#include <util/kernel_cpp.h>

#include <string.h>


// ToDo: the boot items are not supposed to be changed after kernel startup
//		so no locking is done. So for now, we need to be careful with adding
//		new items.

/** @brief One named bootloader-supplied data record. */
struct boot_item : public DoublyLinkedListLinkImpl<boot_item> {
	const char	*name;  /**< NUL-terminated item name; not owned by the record. */
	void		*data;  /**< Opaque payload, owned by the bootloader / kernel data area. */
	size_t		size;   /**< Size of the payload in bytes. */
};

typedef DoublyLinkedList<boot_item> ItemList;


static ItemList sItemList;


/** @brief Registers a new boot item under @p name.
 *  @param name NUL-terminated item name (not copied).
 *  @param data Pointer to the payload (not copied).
 *  @param size Size of @p data in bytes.
 *  @return B_OK on success, or B_NO_MEMORY if the descriptor cannot be allocated. */
status_t
add_boot_item(const char *name, void *data, size_t size)
{
	boot_item *item = new(nothrow) boot_item;
	if (item == NULL)
		return B_NO_MEMORY;

	item->name = name;
	item->data = data;
	item->size = size;

	sItemList.Add(item);
	return B_OK;
}


/** @brief Looks up a boot item by name.
 *  @param name  NUL-terminated item name.
 *  @param _size Optional pointer that receives the payload size on success.
 *  @return Pointer to the payload, or NULL if @p name is not registered. */
void *
get_boot_item(const char *name, size_t *_size)
{
	if (name == NULL || name[0] == '\0')
		return NULL;

	// search item
	for (ItemList::Iterator it = sItemList.GetIterator(); it.HasNext();) {
		boot_item *item = it.Next();

		if (!strcmp(name, item->name)) {
			if (_size != NULL)
				*_size = item->size;

			return item->data;
		}
	}

	return NULL;
}


/** @brief Initialises the boot item subsystem during kernel startup.
 *  @return Always B_OK. */
status_t
boot_item_init(void)
{
	new(&sItemList) ItemList;
		// static initializers do not work in the kernel,
		// so we have to do it here, manually

	return B_OK;
}
