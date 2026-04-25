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
 *   Copyright 2011, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file TypeLookupConstraints.cpp
 * @brief Implementation of TypeLookupConstraints, the optional filter
 *        passed to TeamTypeInformation lookups.
 *
 * TypeLookupConstraints lets a caller restrict a name-to-type lookup by
 * type kind, subtype kind, or expected base-type name. Each restriction
 * is independently optional so callers can scope just as much as they need.
 */


#include "TypeLookupConstraints.h"


/**
 * @brief Constructs an unconstrained instance (no filters set).
 */
TypeLookupConstraints::TypeLookupConstraints()
	:
	fTypeKindGiven(false),
	fSubtypeKindGiven(false)
{
}


/**
 * @brief Constructs a constraint set restricted to @a typeKind.
 *
 * @param typeKind Required top-level type kind.
 */
TypeLookupConstraints::TypeLookupConstraints(type_kind typeKind)
	:
	fTypeKind(typeKind),
	fTypeKindGiven(true),
	fSubtypeKindGiven(false)
{
}


/**
 * @brief Constructs a constraint set restricted to @a typeKind and @a subTypeKind.
 *
 * @param typeKind    Required top-level type kind.
 * @param subTypeKind Required subtype discriminator (kind-specific encoding).
 */
TypeLookupConstraints::TypeLookupConstraints(type_kind typeKind,
	int32 subTypeKind)
	:
	fTypeKind(typeKind),
	fSubtypeKind(subTypeKind),
	fTypeKindGiven(true),
	fSubtypeKindGiven(true),
	fBaseTypeName()
{
}


/**
 * @brief Reports whether a top-level type-kind filter is active.
 *
 * @return True if a type kind has been set.
 */
bool
TypeLookupConstraints::HasTypeKind() const
{
	return fTypeKindGiven;
}


/**
 * @brief Reports whether a subtype-kind filter is active.
 *
 * @return True if a subtype kind has been set.
 */
bool
TypeLookupConstraints::HasSubtypeKind() const
{
	return fSubtypeKindGiven;
}


/**
 * @brief Reports whether a base-type-name filter is active.
 *
 * @return True if @c BaseTypeName() is non-empty.
 */
bool
TypeLookupConstraints::HasBaseTypeName() const
{
	return fBaseTypeName.Length() > 0;
}


/**
 * @brief Returns the required top-level type kind.
 *
 * @return Type kind value; meaningful only if @c HasTypeKind() is true.
 */
type_kind
TypeLookupConstraints::TypeKind() const
{
	return fTypeKind;
}


/**
 * @brief Returns the required subtype kind discriminator.
 *
 * @return Subtype value; meaningful only if @c HasSubtypeKind() is true.
 */
int32
TypeLookupConstraints::SubtypeKind() const
{
	return fSubtypeKind;
}


/**
 * @brief Returns the expected base-type name.
 *
 * @return Reference to the stored base type name.
 */
const BString&
TypeLookupConstraints::BaseTypeName() const
{
	return fBaseTypeName;
}


/**
 * @brief Sets the required top-level type kind and marks the filter active.
 *
 * @param typeKind Required type kind.
 */
void
TypeLookupConstraints::SetTypeKind(type_kind typeKind)
{
	fTypeKind = typeKind;
	fTypeKindGiven = true;
}


/**
 * @brief Sets the required subtype kind and marks the filter active.
 *
 * @param subtypeKind Required subtype discriminator.
 */
void
TypeLookupConstraints::SetSubtypeKind(int32 subtypeKind)
{
	fSubtypeKind = subtypeKind;
	fSubtypeKindGiven = true;
}


/**
 * @brief Sets the expected base-type name; an empty string disables the filter.
 *
 * @param name New base-type name to match against.
 */
void
TypeLookupConstraints::SetBaseTypeName(const BString& name)
{
	fBaseTypeName = name;
}
