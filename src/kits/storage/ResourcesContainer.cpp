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
 *   Copyright Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file ResourcesContainer.cpp
 * @brief Implementation of ResourcesContainer, an ordered collection of resource items.
 *
 * ResourcesContainer manages an ordered list of ResourceItem objects and
 * provides lookup by index, type/id pair, type/name pair, and data pointer.
 * It tracks aggregate modification state and supports merging resources from
 * another container via AssimilateResources().
 *
 * @see ResourceItem
 */

#include <stdio.h>

#include "ResourcesContainer.h"

#include "ResourceItem.h"

namespace BPrivate {
namespace Storage {

/**
 * @brief Constructs an empty, unmodified ResourcesContainer.
 */
ResourcesContainer::ResourcesContainer()
				  : fResources(),
					fIsModified(false)
{
}

/**
 * @brief Destroys the container and deletes all contained ResourceItem objects.
 */
ResourcesContainer::~ResourcesContainer()
{
	MakeEmpty();
}

/**
 * @brief Adds a ResourceItem to the container at the given index.
 *
 * If @p replace is true and a resource with the same type and ID already
 * exists, the existing item is removed and deleted before insertion. If
 * @p index is out of range the item is appended at the end.
 *
 * @param item    The resource item to add; must not be NULL.
 * @param index   Position at which to insert the item.
 * @param replace If true, an existing item with the same type/id is replaced.
 * @return true on success, false if @p item is NULL or memory allocation fails.
 */
bool
ResourcesContainer::AddResource(ResourceItem *item, int32 index,
								bool replace)
{
	bool result = false;
	if (item) {
		// replace an item with the same type and id
		if (replace)
			delete RemoveResource(IndexOf(item->Type(), item->ID()));
		int32 count = CountResources();
		if (index < 0 || index > count)
			index = count;
		result = fResources.AddItem(item, index);
		SetModified(true);
	}
	return result;
}

/**
 * @brief Removes and returns the ResourceItem at the given index.
 *
 * The container's modified flag is set if an item was actually removed.
 *
 * @param index The zero-based index of the item to remove.
 * @return Pointer to the removed item, or NULL if the index is out of range.
 */
ResourceItem*
ResourcesContainer::RemoveResource(int32 index)
{
	ResourceItem* item = (ResourceItem*)fResources.RemoveItem(index);
	if (item)
		SetModified(true);
	return item;
}

/**
 * @brief Removes the specified ResourceItem from the container.
 *
 * Looks up the item by pointer and delegates to RemoveResource(int32).
 *
 * @param item Pointer to the item to remove.
 * @return true if the item was found and removed, false otherwise.
 */
bool
ResourcesContainer::RemoveResource(ResourceItem *item)
{
	return RemoveResource(IndexOf(item));
}

/**
 * @brief Removes and deletes all ResourceItem objects in the container.
 *
 * After this call CountResources() returns zero and IsModified() returns false.
 */
void
ResourcesContainer::MakeEmpty()
{
	for (int32 i = 0; ResourceItem *item = ResourceAt(i); i++)
		delete item;
	fResources.MakeEmpty();
	SetModified(false);
}

/**
 * @brief Moves all loaded resources from @p container into this container.
 *
 * Only items that are already loaded are transferred. Unloaded items are
 * deleted. After the call @p container is empty.
 *
 * @param container The source container whose resources are to be assimilated.
 */
void
ResourcesContainer::AssimilateResources(ResourcesContainer &container)
{
	// Resistance is futile! ;-)
	int32 newCount = container.CountResources();
	for (int32 i = 0; i < newCount; i++) {
		ResourceItem *item = container.ResourceAt(i);
		if (item->IsLoaded())
			AddResource(item);
		else {
			// That should not happen.
			// Delete the item to have a consistent behavior.
			delete item;
		}
	}
	container.fResources.MakeEmpty();
	container.SetModified(true);
	SetModified(true);
}

/**
 * @brief Returns the index of the given ResourceItem pointer in the container.
 *
 * @param item The item to locate.
 * @return Zero-based index, or -1 if not found.
 */
int32
ResourcesContainer::IndexOf(ResourceItem *item) const
{
	return fResources.IndexOf(item);
}

/**
 * @brief Returns the index of the resource whose data pointer matches @p data.
 *
 * @param data The data pointer to search for.
 * @return Zero-based index, or -1 if no resource has the given data pointer.
 */
int32
ResourcesContainer::IndexOf(const void *data) const
{
	int32 index = -1;
	if (data) {
		int32 count = CountResources();
		for (int32 i = 0; index == -1 && i < count; i++) {
			if (ResourceAt(i)->Data() == data)
				index = i;
		}
	}
	return index;
}

/**
 * @brief Returns the index of the resource with the given type and numeric ID.
 *
 * @param type The resource type code.
 * @param id   The resource numeric ID.
 * @return Zero-based index, or -1 if no matching resource is found.
 */
int32
ResourcesContainer::IndexOf(type_code type, int32 id) const
{
	int32 index = -1;
	int32 count = CountResources();
	for (int32 i = 0; index == -1 && i < count; i++) {
		ResourceItem *item = ResourceAt(i);
		if (item->Type() == type && item->ID() == id)
			index = i;
	}
	return index;
}

/**
 * @brief Returns the index of the resource with the given type and name.
 *
 * Both @p type and @p name must match. A NULL name matches only resources
 * that also have a NULL name.
 *
 * @param type The resource type code.
 * @param name The resource name string, or NULL.
 * @return Zero-based index, or -1 if no matching resource is found.
 */
int32
ResourcesContainer::IndexOf(type_code type, const char *name) const
{
	int32 index = -1;
	int32 count = CountResources();
	for (int32 i = 0; index == -1 && i < count; i++) {
		ResourceItem *item = ResourceAt(i);
		const char *itemName = item->Name();
		if (item->Type() == type && ((name == NULL && itemName == NULL)
									 || (name != NULL && itemName != NULL
										&& strcmp(name, itemName) == 0))) {
			index = i;
		}
	}
	return index;
}

/**
 * @brief Returns the index of the Nth resource of the given type.
 *
 * @param type      The resource type code to search for.
 * @param typeIndex Zero-based position among resources of the specified type.
 * @return Zero-based index in the container, or -1 if not found.
 */
int32
ResourcesContainer::IndexOfType(type_code type, int32 typeIndex) const
{
	int32 index = -1;
	int32 count = CountResources();
	for (int32 i = 0; index == -1 && i < count; i++) {
		ResourceItem *item = ResourceAt(i);
		if (item->Type() == type) {
			if (typeIndex == 0)
				index = i;
			typeIndex--;
		}
	}
	return index;
}

/**
 * @brief Returns the ResourceItem at the specified index.
 *
 * @param index Zero-based index of the item.
 * @return Pointer to the item, or NULL if the index is out of range.
 */
ResourceItem*
ResourcesContainer::ResourceAt(int32 index) const
{
	return (ResourceItem*)fResources.ItemAt(index);
}

/**
 * @brief Returns the number of resource items currently in the container.
 *
 * @return The resource count.
 */
int32
ResourcesContainer::CountResources() const
{
	return fResources.CountItems();
}

/**
 * @brief Sets the modified flag for the container and all contained items.
 *
 * When @p modified is false, each ResourceItem's modified flag is also cleared
 * so that they reflect the saved state.
 *
 * @param modified true to mark as modified, false to mark as clean.
 */
void
ResourcesContainer::SetModified(bool modified)
{
	fIsModified = modified;
	// If unmodified, set the resource item's modified flag as well.
	if (!modified) {
		int32 count = CountResources();
		for (int32 i = 0; i < count; i++)
			ResourceAt(i)->SetModified(false);
	}
}

/**
 * @brief Returns whether the container or any of its items have been modified.
 *
 * @return true if the container itself or at least one ResourceItem is modified.
 */
bool
ResourcesContainer::IsModified() const
{
	bool isModified = fIsModified;
	int32 count = CountResources();
	for (int32 i = 0; !isModified && i < count; i++)
		isModified |= ResourceAt(i)->IsModified();
	return isModified;
}


};	// namespace Storage
};	// namespace BPrivate
