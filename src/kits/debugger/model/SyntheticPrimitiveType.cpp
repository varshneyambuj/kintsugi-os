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
 *   Copyright 2014, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file SyntheticPrimitiveType.cpp
 * @brief Implementation of SyntheticPrimitiveType, a fabricated primitive
 *        type used when no debug-info-derived type is available.
 *
 * SyntheticPrimitiveType wraps a BVariant type constant so the value
 * inspector can render arbitrary primitive values (e.g. casts entered in
 * the expression bar) without requiring matching DWARF information.
 */


#include "SyntheticPrimitiveType.h"

#include <Variant.h>

#include "UiUtils.h"


/**
 * @brief Constructs a SyntheticPrimitiveType wrapping @a typeConstant.
 *
 * Initialises the synthetic identity and human-readable name from the
 * BVariant type constant.
 *
 * @param typeConstant BVariant type code identifying the primitive shape.
 */
SyntheticPrimitiveType::SyntheticPrimitiveType(uint32 typeConstant)
	:
	PrimitiveType(),
	fTypeConstant(typeConstant),
	fID(),
	fName()
{
	_Init();
}


/**
 * @brief Destroys the SyntheticPrimitiveType.
 */
SyntheticPrimitiveType::~SyntheticPrimitiveType()
{
}


/**
 * @brief Returns the BVariant type constant the type wraps.
 *
 * @return The captured type constant.
 */
uint32
SyntheticPrimitiveType::TypeConstant() const
{
	return fTypeConstant;
}


/**
 * @brief Returns @c -1 since synthetic types are not bound to any image.
 *
 * @return Always @c -1.
 */
image_id
SyntheticPrimitiveType::ImageID() const
{
	return -1;
}


/**
 * @brief Returns the synthetic identifier (a pointer-derived string).
 *
 * @return Stable per-instance identifier string.
 */
const BString&
SyntheticPrimitiveType::ID() const
{
	return fID;
}


/**
 * @brief Returns the human-readable type name.
 *
 * @return Type name derived from the BVariant type constant.
 */
const BString&
SyntheticPrimitiveType::Name() const
{
	return fName;
}


/**
 * @brief Reports the type kind.
 *
 * @return Always @c TYPE_PRIMITIVE.
 */
type_kind
SyntheticPrimitiveType::Kind() const
{
	return TYPE_PRIMITIVE;
}


/**
 * @brief Returns the byte size of the wrapped primitive shape.
 *
 * @return Size in bytes as reported by BVariant::SizeOfType().
 */
target_size_t
SyntheticPrimitiveType::ByteSize() const
{
	return BVariant::SizeOfType(fTypeConstant);
}


/**
 * @brief Resolves a data location relative to an existing object location.
 *
 * Synthetic types have no DWARF backing, so location resolution is
 * unsupported.
 *
 * @param objectLocation Source object location (unused).
 * @param _location      Set to NULL on return.
 * @return              Always @c B_NOT_SUPPORTED.
 */
status_t
SyntheticPrimitiveType::ResolveObjectDataLocation(
	const ValueLocation& objectLocation, ValueLocation*& _location)
{
	_location = NULL;
	return B_NOT_SUPPORTED;
}


/**
 * @brief Resolves a data location at an absolute address.
 *
 * Synthetic types have no DWARF backing, so location resolution is
 * unsupported.
 *
 * @param objectAddress Absolute address (unused).
 * @param _location     Set to NULL on return.
 * @return             Always @c B_NOT_SUPPORTED.
 */
status_t
SyntheticPrimitiveType::ResolveObjectDataLocation(target_addr_t objectAddress,
	ValueLocation*& _location)
{
	_location = NULL;
	return B_NOT_SUPPORTED;
}


/**
 * @brief Initialises the synthetic identity and display name.
 *
 * The id is derived from the @c this pointer to ensure uniqueness across
 * concurrently live instances; the name is the BVariant type-code string.
 */
void
SyntheticPrimitiveType::_Init()
{
	fID.SetToFormat("%p", this);
	fName.SetTo(UiUtils::TypeCodeToString(fTypeConstant));
}
