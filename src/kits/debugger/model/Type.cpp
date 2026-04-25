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
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Type.cpp
 * @brief Vtable anchors and default-implementation methods for the Type
 *        family of debug-info type descriptors.
 *
 * The Type hierarchy mirrors the DWARF type model: primitive, compound,
 * array, modified (cv-qualified), typedef, enum, subrange, address,
 * function, pointer-to-member, etc. Most methods are pure virtual; this
 * translation unit anchors the vtables and provides a few default
 * implementations such as @c ResolveRawType() for typedefs and modified
 * types.
 */


#include "Type.h"


// #pragma mark - BaseType


/**
 * @brief Virtual destructor anchor for the BaseType interface.
 */
BaseType::~BaseType()
{
}


// #pragma mark - DataMember


/**
 * @brief Virtual destructor anchor for the DataMember interface.
 */
DataMember::~DataMember()
{
}


// #pragma mark - EnumeratorValue


/**
 * @brief Virtual destructor anchor for the EnumeratorValue interface.
 */
EnumeratorValue::~EnumeratorValue()
{
}


// #pragma mark - ArrayDimension


/**
 * @brief Virtual destructor anchor for the ArrayDimension interface.
 */
ArrayDimension::~ArrayDimension()
{
}


/**
 * @brief Computes the number of elements covered by this array dimension.
 *
 * Handles enumeration index types (count of enumerators) and subrange
 * index types (upper minus lower plus one). Other types report zero.
 *
 * @return Element count, or zero if the dimension type is not enumerable.
 */
uint64
ArrayDimension::CountElements() const
{
	Type* type = GetType();

	if (type->Kind() == TYPE_ENUMERATION)
		return dynamic_cast<EnumerationType*>(type)->CountValues();

	if (type->Kind() == TYPE_SUBRANGE) {
		SubrangeType* subrangeType = dynamic_cast<SubrangeType*>(type);
		BVariant lower = subrangeType->LowerBound();
		BVariant upper = subrangeType->UpperBound();
		bool isSigned;
		if (!lower.IsInteger(&isSigned) || !upper.IsInteger())
			return 0;

		return isSigned
			? upper.ToInt64() - lower.ToInt64() + 1
			: upper.ToUInt64() - lower.ToUInt64() + 1;
	}

	return 0;
}


// #pragma mark - FunctionParameter


/**
 * @brief Virtual destructor anchor for the FunctionParameter interface.
 */
FunctionParameter::~FunctionParameter()
{
}


// #pragma mark - TemplateParameter


/**
 * @brief Virtual destructor anchor for the TemplateParameter interface.
 */
TemplateParameter::~TemplateParameter()
{
}


// #pragma mark - Type


/**
 * @brief Virtual destructor anchor for the Type interface.
 */
Type::~Type()
{
}


/**
 * @brief Default raw-type resolver returning @c this.
 *
 * Subclasses such as TypedefType and ModifiedType override this to peel
 * a layer of indirection per call.
 *
 * @param nextOneOnly Unused at this level; honoured by overrides.
 * @return           A non-const pointer to this Type.
 */
Type*
Type::ResolveRawType(bool nextOneOnly) const
{
	return const_cast<Type*>(this);
}


/**
 * @brief Default factory for derived address types: not supported here.
 *
 * @param kind        Address-type kind requested.
 * @param _resultType Set to NULL on return.
 * @return           Always @c B_ERROR.
 */
status_t
Type::CreateDerivedAddressType(address_type_kind kind,
	AddressType*& _resultType)
{
	_resultType = NULL;
	return B_ERROR;
}


/**
 * @brief Default factory for derived array types: not supported here.
 *
 * @param lowerBound     Lower bound of the synthetic dimension.
 * @param elementCount   Element count for the new dimension.
 * @param extendExisting Whether to extend an existing array type.
 * @param _resultType    Set to NULL on return.
 * @return              Always @c B_ERROR.
 */
status_t
Type::CreateDerivedArrayType(int64 lowerBound, int64 elementCount,
	bool extendExisting, ArrayType*& _resultType)
{
	_resultType = NULL;
	return B_ERROR;
}


// #pragma mark - PrimitiveType


/**
 * @brief Virtual destructor anchor for PrimitiveType.
 */
PrimitiveType::~PrimitiveType()
{
}


/**
 * @brief Reports the type kind.
 *
 * @return Always @c TYPE_PRIMITIVE.
 */
type_kind
PrimitiveType::Kind() const
{
	return TYPE_PRIMITIVE;
}


// #pragma mark - CompoundType


/**
 * @brief Virtual destructor anchor for CompoundType.
 */
CompoundType::~CompoundType()
{
}


/**
 * @brief Reports the type kind.
 *
 * @return Always @c TYPE_COMPOUND.
 */
type_kind
CompoundType::Kind() const
{
	return TYPE_COMPOUND;
}


// #pragma mark - ModifiedType


/**
 * @brief Virtual destructor anchor for ModifiedType.
 */
ModifiedType::~ModifiedType()
{
}


/**
 * @brief Reports the type kind.
 *
 * @return Always @c TYPE_MODIFIED.
 */
type_kind
ModifiedType::Kind() const
{
	return TYPE_MODIFIED;
}


/**
 * @brief Peels the modifier and returns the underlying base type.
 *
 * @param nextOneOnly If true, returns just the immediate base type;
 *                    otherwise recursively resolves until a non-typedef,
 *                    non-modified type is reached.
 * @return           The resolved underlying type.
 */
Type*
ModifiedType::ResolveRawType(bool nextOneOnly) const
{
	Type* baseType = BaseType();
	return nextOneOnly ? baseType : baseType->ResolveRawType(true);
}


// #pragma mark - TypedefType


/**
 * @brief Virtual destructor anchor for TypedefType.
 */
TypedefType::~TypedefType()
{
}


/**
 * @brief Reports the type kind.
 *
 * @return Always @c TYPE_TYPEDEF.
 */
type_kind
TypedefType::Kind() const
{
	return TYPE_TYPEDEF;
}


/**
 * @brief Peels the typedef and returns the aliased type.
 *
 * @param nextOneOnly If true, returns just the immediate aliased type;
 *                    otherwise recursively resolves further.
 * @return           The resolved aliased type.
 */
Type*
TypedefType::ResolveRawType(bool nextOneOnly) const
{
	Type* baseType = BaseType();
	return nextOneOnly ? baseType : baseType->ResolveRawType(true);
}


// #pragma mark - AddressType


/**
 * @brief Virtual destructor anchor for AddressType.
 */
AddressType::~AddressType()
{
}


/**
 * @brief Reports the type kind.
 *
 * @return Always @c TYPE_ADDRESS.
 */
type_kind
AddressType::Kind() const
{
	return TYPE_ADDRESS;
}


// #pragma mark - EnumerationType


/**
 * @brief Virtual destructor anchor for EnumerationType.
 */
EnumerationType::~EnumerationType()
{
}


/**
 * @brief Reports the type kind.
 *
 * @return Always @c TYPE_ENUMERATION.
 */
type_kind
EnumerationType::Kind() const
{
	return TYPE_ENUMERATION;
}


/**
 * @brief Returns the EnumeratorValue whose stored value equals @a value.
 *
 * @param value Value to look up.
 * @return     Matching EnumeratorValue, or NULL if no match exists.
 * @todo Optimise via a lookup table when the enum has many values.
 */
EnumeratorValue*
EnumerationType::ValueFor(const BVariant& value) const
{
	// TODO: Optimize?
	for (int32 i = 0; EnumeratorValue* enumValue = ValueAt(i); i++) {
		if (enumValue->Value() == value)
			return enumValue;
	}

	return NULL;
}


// #pragma mark - SubrangeType


/**
 * @brief Virtual destructor anchor for SubrangeType.
 */
SubrangeType::~SubrangeType()
{
}


/**
 * @brief Reports the type kind.
 *
 * @return Always @c TYPE_SUBRANGE.
 */
type_kind
SubrangeType::Kind() const
{
	return TYPE_SUBRANGE;
}


// #pragma mark - ArrayType


/**
 * @brief Virtual destructor anchor for ArrayType.
 */
ArrayType::~ArrayType()
{
}


/**
 * @brief Reports the type kind.
 *
 * @return Always @c TYPE_ARRAY.
 */
type_kind
ArrayType::Kind() const
{
	return TYPE_ARRAY;
}


// #pragma mark - UnspecifiedType


/**
 * @brief Virtual destructor anchor for UnspecifiedType.
 */
UnspecifiedType::~UnspecifiedType()
{
}


/**
 * @brief Reports the type kind.
 *
 * @return Always @c TYPE_UNSPECIFIED.
 */
type_kind
UnspecifiedType::Kind() const
{
	return TYPE_UNSPECIFIED;
}


// #pragma mark - FunctionType


/**
 * @brief Virtual destructor anchor for FunctionType.
 */
FunctionType::~FunctionType()
{
}


/**
 * @brief Reports the type kind.
 *
 * @return Always @c TYPE_FUNCTION.
 */
type_kind
FunctionType::Kind() const
{
	return TYPE_FUNCTION;
}


// #pragma mark - PointerToMemberType


/**
 * @brief Virtual destructor anchor for PointerToMemberType.
 */
PointerToMemberType::~PointerToMemberType()
{
}


/**
 * @brief Reports the type kind.
 *
 * @return Always @c TYPE_POINTER_TO_MEMBER.
 */
type_kind
PointerToMemberType::Kind() const
{
	return TYPE_POINTER_TO_MEMBER;
}
