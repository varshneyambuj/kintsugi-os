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
 *   Copyright 2009-2012, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file TypeComponentPath.cpp
 * @brief Implementation of TypeComponent and TypeComponentPath, used to
 *        address sub-components inside a structured Type.
 *
 * A TypeComponent identifies one step into a type (base class, data
 * member, array element). TypeComponentPath chains those steps so the
 * value inspector can reference, hash, and compare locations inside
 * arbitrarily nested structures.
 */


#include "TypeComponentPath.h"

#include <stdio.h>

#include <new>


// #pragma mark - TypeComponent


/**
 * @brief Tests whether @a other is a strict prefix of (or equal to) this component.
 *
 * For array elements, @a other matches when its name is a prefix of this
 * component's name; this is the textual prefix relation used by index
 * cache invalidation.
 *
 * @param other Candidate prefix component.
 * @return     True if @a other matches or is a prefix.
 */
bool
TypeComponent::HasPrefix(const TypeComponent& other) const
{
	if (*this == other)
		return true;

	return componentKind == TYPE_COMPONENT_ARRAY_ELEMENT
		&& other.componentKind == TYPE_COMPONENT_ARRAY_ELEMENT
		&& name.Compare(other.name, other.name.Length()) == 0;
}


/**
 * @brief Computes a stable hash combining index, kinds, and name.
 *
 * @return Hash value identifying the component.
 */
uint32
TypeComponent::HashValue() const
{
	uint32 hash = ((uint32)index << 8) | (componentKind << 4) | typeKind;
	return name.HashValue() * 13 + hash;
}


/**
 * @brief Prints a human-readable representation of the component to stdout.
 *
 * Diagnostic helper; not used in production paths.
 */
void
TypeComponent::Dump() const
{
	switch (typeKind) {
		case TYPE_PRIMITIVE:
			printf("primitive");
			break;
		case TYPE_COMPOUND:
			printf("compound");
			break;
		case TYPE_MODIFIED:
			printf("modified");
			break;
		case TYPE_TYPEDEF:
			printf("typedef");
			break;
		case TYPE_ADDRESS:
			printf("address");
			break;
		case TYPE_ENUMERATION:
			printf("enum");
			break;
		case TYPE_SUBRANGE:
			printf("subrange");
			break;
		case TYPE_ARRAY:
			printf("array");
			break;
		case TYPE_UNSPECIFIED:
			printf("unspecified");
			break;
		case TYPE_FUNCTION:
			printf("function");
			break;
		case TYPE_POINTER_TO_MEMBER:
			printf("pointer to member");
			break;
	}

	printf(" ");

	switch (componentKind) {
		case TYPE_COMPONENT_UNDEFINED:
			printf("undefined");
			break;
		case TYPE_COMPONENT_BASE_TYPE:
			printf("base %" B_PRIu64 " \"%s\"", index, name.String());
			break;
		case TYPE_COMPONENT_DATA_MEMBER:
			printf("member %" B_PRIu64 " \"%s\"", index, name.String());
			break;
		case TYPE_COMPONENT_ARRAY_ELEMENT:
			printf("element %" B_PRIu64 " \"%s\"", index, name.String());
			break;
	}
}


/**
 * @brief Tests two components for equality on all four fields.
 *
 * @param other Other component.
 * @return     True if all of kind, type, index, and name match.
 */
bool
TypeComponent::operator==(const TypeComponent& other) const
{
	return componentKind == other.componentKind
		&& typeKind == other.typeKind
		&& index == other.index
		&& name == other.name;
}


// #pragma mark - TypeComponentPath


/**
 * @brief Constructs an empty TypeComponentPath with capacity 10.
 */
TypeComponentPath::TypeComponentPath()
	:
	fComponents(10)
{
}


/**
 * @brief Copy-constructs by delegating to @c operator=.
 *
 * @param other Source path to copy.
 */
TypeComponentPath::TypeComponentPath(const TypeComponentPath& other)
	:
	fComponents(10)
{
	*this = other;
}


/**
 * @brief Destroys the path; the BObjectList frees its components.
 */
TypeComponentPath::~TypeComponentPath()
{
}


/**
 * @brief Returns the number of components in the path.
 *
 * @return Component count.
 */
int32
TypeComponentPath::CountComponents() const
{
	return fComponents.CountItems();
}


/**
 * @brief Returns the component at @a index, or a default component if out of range.
 *
 * @param index Zero-based component index.
 * @return     Copy of the component, or a default-constructed one.
 */
TypeComponent
TypeComponentPath::ComponentAt(int32 index) const
{
	TypeComponent* component = fComponents.ItemAt(index);
	return component != NULL ? *component : TypeComponent();
}


/**
 * @brief Appends a copy of @a component to the path.
 *
 * @param component Component to copy and append.
 * @return         True on success, false on allocation failure.
 */
bool
TypeComponentPath::AddComponent(const TypeComponent& component)
{
	TypeComponent* myComponent = new(std::nothrow) TypeComponent(component);
	if (myComponent == NULL || !fComponents.AddItem(myComponent)) {
		delete myComponent;
		return false;
	}

	return true;
}


/**
 * @brief Removes every component, leaving an empty path.
 */
void
TypeComponentPath::Clear()
{
	fComponents.MakeEmpty();
}


/**
 * @brief Creates a new path containing the first @a componentCount components.
 *
 * @param componentCount Number of components to copy. Negative or
 *                        oversized values clamp to the source path's length.
 * @return              Newly allocated TypeComponentPath with one
 *                       reference held by the caller, or NULL on failure.
 */
TypeComponentPath*
TypeComponentPath::CreateSubPath(int32 componentCount) const
{
	if (componentCount < 0 || componentCount > fComponents.CountItems())
		componentCount = fComponents.CountItems();

	TypeComponentPath* path = new(std::nothrow) TypeComponentPath;
	if (path == NULL)
		return NULL;
	BReference<TypeComponentPath> pathReference(path, true);

	for (int32 i = 0; i < componentCount; i++) {
		if (!path->AddComponent(*fComponents.ItemAt(i)))
			return NULL;
	}

	return pathReference.Detach();
}


/**
 * @brief Computes a hash over the entire component list.
 *
 * @return Combined hash value, or 0 for empty paths.
 */
uint32
TypeComponentPath::HashValue() const
{
	int32 count = fComponents.CountItems();
	if (count == 0)
		return 0;

	uint32 hash = fComponents.ItemAt(0)->HashValue();

	for (int32 i = 1; i < count; i++)
		hash = hash * 17 + fComponents.ItemAt(i)->HashValue();

	return hash;
}


/**
 * @brief Prints a human-readable representation of the path to stdout.
 *
 * Diagnostic helper; not used in production paths.
 */
void
TypeComponentPath::Dump() const
{
	int32 count = fComponents.CountItems();
	for (int32 i = 0; i < count; i++) {
		if (i == 0)
			printf("[");
		else
			printf(" -> [");
		fComponents.ItemAt(i)->Dump();
		printf("]");
	}
}


/**
 * @brief Assigns from @a other by clearing and re-appending components.
 *
 * Self-assignment is a no-op.
 *
 * @param other Source path.
 * @return     Reference to *this.
 */
TypeComponentPath&
TypeComponentPath::operator=(const TypeComponentPath& other)
{
	if (this != &other) {
		fComponents.MakeEmpty();

		for (int32 i = 0;
				TypeComponent* component = other.fComponents.ItemAt(i); i++) {
			if (!AddComponent(*component))
				break;
		}
	}

	return *this;
}


/**
 * @brief Tests two paths for equality element by element.
 *
 * @param other Other path.
 * @return     True if both paths have the same length and equal components.
 */
bool
TypeComponentPath::operator==(const TypeComponentPath& other) const
{
	int32 count = fComponents.CountItems();
	if (count != other.fComponents.CountItems())
		return false;

	for (int32 i = 0; i < count; i++) {
		if (*fComponents.ItemAt(i) != *other.fComponents.ItemAt(i))
			return false;
	}

	return true;
}
