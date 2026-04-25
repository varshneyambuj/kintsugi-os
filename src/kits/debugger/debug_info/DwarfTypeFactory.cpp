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
 *   Copyright 2013, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DwarfTypeFactory.cpp
 * @brief Implementation of DwarfTypeFactory, the entry point for turning a
 *        DIEType into a DwarfType.
 *
 * The factory consults a GlobalTypeCache so that two requests for the same
 * DIE return the same DwarfType. Compound types are also resolved against
 * a cross-image GlobalTypeLookup before being constructed locally so that
 * inheritance and member types refer to a single representative across
 * the team. Various nested predicates are used together with
 * DwarfUtils::GetDIEByPredicate() to walk
 * abstract-origin/specification chains in search of attributes that the
 * direct DIE may not carry.
 *
 * @see DwarfTypes, GlobalTypeCache, GlobalTypeLookup
 */


#include "DwarfTypeFactory.h"

#include <algorithm>
#include <new>

#include <AutoLocker.h>
#include <Variant.h>

#include "ArrayIndexPath.h"
#include "Architecture.h"
#include "CompilationUnit.h"
#include "DebugInfoEntries.h"
#include "Dwarf.h"
#include "DwarfFile.h"
#include "DwarfTargetInterface.h"
#include "DwarfUtils.h"
#include "DwarfTypes.h"
#include "GlobalTypeLookup.h"
#include "Register.h"
#include "RegisterMap.h"
#include "SourceLanguageInfo.h"
#include "Tracing.h"
#include "TypeLookupConstraints.h"
#include "ValueLocation.h"


namespace {


// #pragma mark - HasTypePredicate


/**
 * @brief Predicate template selecting DIEs that carry a non-null
 *        @c DW_AT_type attribute.
 */
template<typename EntryType>
struct HasTypePredicate {
	inline bool operator()(EntryType* entry) const
	{
		return entry->GetType() != NULL;
	}
};


// #pragma mark - HasReturnTypePredicate


/**
 * @brief Predicate template selecting DIEs that carry a non-null
 *        return-type attribute.
 */
template<typename EntryType>
struct HasReturnTypePredicate {
	inline bool operator()(EntryType* entry) const
	{
		return entry->ReturnType() != NULL;
	}
};


// #pragma mark - HasEnumeratorsPredicate


/**
 * @brief Predicate selecting DIEEnumerationType DIEs that already declare
 *        their enumerators (rather than deferring to an abstract origin).
 */
struct HasEnumeratorsPredicate {
	inline bool operator()(DIEEnumerationType* entry) const
	{
		return !entry->Enumerators().IsEmpty();
	}
};


// #pragma mark - HasDimensionsPredicate


/**
 * @brief Predicate selecting DIEArrayType DIEs that already declare
 *        their dimensions.
 */
struct HasDimensionsPredicate {
	inline bool operator()(DIEArrayType* entry) const
	{
		return !entry->Dimensions().IsEmpty();
	}
};


// #pragma mark - HasMembersPredicate


/**
 * @brief Predicate selecting DIECompoundType DIEs that already declare
 *        data members.
 */
struct HasMembersPredicate {
	inline bool operator()(DIECompoundType* entry) const
	{
		return !entry->DataMembers().IsEmpty();
	}
};


// #pragma mark - HasBaseTypesPredicate


/**
 * @brief Predicate selecting DIEClassBaseType DIEs that already declare
 *        base classes.
 */
struct HasBaseTypesPredicate {
	inline bool operator()(DIEClassBaseType* entry) const
	{
		return !entry->BaseTypes().IsEmpty();
	}
};


// #pragma mark - HasTemplateParametersPredicate


/**
 * @brief Predicate selecting DIEClassBaseType DIEs that already declare
 *        template parameters.
 */
struct HasTemplateParametersPredicate {
	inline bool operator()(DIEClassBaseType* entry) const
	{
		return !entry->TemplateParameters().IsEmpty();
	}
};


// #pragma mark - HasParametersPredicate


/**
 * @brief Predicate template selecting DIEs that already declare formal
 *        parameters.
 */
template<typename EntryType>
struct HasParametersPredicate {
	inline bool operator()(EntryType* entry) const
	{
		return !entry->Parameters().IsEmpty();
	}
};


// #pragma mark - HasLowerBoundPredicate


/**
 * @brief Predicate selecting DIESubrangeType DIEs that supply a valid
 *        lower bound.
 */
struct HasLowerBoundPredicate {
	inline bool operator()(DIESubrangeType* entry) const
	{
		return entry->LowerBound()->IsValid();
	}
};


// #pragma mark - HasUpperBoundPredicate


/**
 * @brief Predicate selecting DIESubrangeType DIEs that supply a valid
 *        upper bound.
 */
struct HasUpperBoundPredicate {
	inline bool operator()(DIESubrangeType* entry) const
	{
		return entry->UpperBound()->IsValid();
	}
};


// #pragma mark - HasCountPredicate


/**
 * @brief Predicate selecting DIESubrangeType DIEs that supply a valid
 *        element count attribute.
 */
struct HasCountPredicate {
	inline bool operator()(DIESubrangeType* entry) const
	{
		return entry->Count()->IsValid();
	}
};


// #pragma mark - HasContainingTypePredicate


/**
 * @brief Predicate selecting DIEPointerToMemberType DIEs that supply a
 *        valid containing-type attribute.
 */
struct HasContainingTypePredicate {
	inline bool operator()(DIEPointerToMemberType* entry) const
	{
		return entry->ContainingType() != NULL;
	}
};


}	// unnamed namespace


// #pragma mark - ArtificialIntegerType


/**
 * @brief Synthetic PrimitiveType used as a fallback when DWARF lacks an
 *        explicit base type for an integer subrange.
 *
 * Each instance is fabricated on demand with a width-appropriate Haiku
 * type constant (e.g. @c B_INT32_TYPE) and a synthetic ID so the
 * surrounding code can treat it like any other primitive type.
 */
class DwarfTypeFactory::ArtificialIntegerType : public PrimitiveType {
public:
	/** @brief Stores the supplied identity and width parameters verbatim. */
	ArtificialIntegerType(const BString& id, const BString& name,
		target_size_t byteSize, uint32 typeConstant)
		:
		fID(id),
		fName(name),
		fByteSize(byteSize),
		fTypeConstant(typeConstant)
	{
	}

	/**
	 * @brief Creates an ArtificialIntegerType with the requested width and
	 *        signedness.
	 *
	 * @param byteSize  Width of the integer in bytes (1, 2, 4 or 8).
	 * @param isSigned  Whether the integer is signed.
	 * @param _type     Out parameter receiving the new Type.
	 * @retval B_OK         Type created.
	 * @retval B_BAD_VALUE  Unsupported byte size.
	 * @retval B_NO_MEMORY  Allocation failure.
	 */
	static status_t Create(target_size_t byteSize, bool isSigned, Type*& _type)
	{
		// get the matching type constant
		uint32 typeConstant;
		switch (byteSize) {
			case 1:
				typeConstant = isSigned ? B_INT8_TYPE : B_UINT8_TYPE;
				break;
			case 2:
				typeConstant = isSigned ? B_INT16_TYPE : B_UINT16_TYPE;
				break;
			case 4:
				typeConstant = isSigned ? B_INT32_TYPE : B_UINT32_TYPE;
				break;
			case 8:
				typeConstant = isSigned ? B_INT64_TYPE : B_UINT64_TYPE;
				break;
			default:
				return B_BAD_VALUE;
		}

		// name and ID
		char buffer[16];
		snprintf(buffer, sizeof(buffer), isSigned ? "int%d" : "uint%d",
			(int)byteSize * 8);
		BString id(buffer);
		if (id.Length() == 0)
			return B_NO_MEMORY;

		// create the type
		ArtificialIntegerType* type = new(std::nothrow) ArtificialIntegerType(
			id, id, byteSize, typeConstant);
		if (type == NULL)
			return B_NO_MEMORY;

		_type = type;
		return B_OK;
	}

	/** @brief Synthetic types are not associated with any image. */
	virtual image_id ImageID() const
	{
		return -1;
	}

	/** @brief Returns the synthetic ID string. */
	virtual const BString& ID() const
	{
		return fID;
	}

	/** @brief Returns the synthetic name (matches the ID). */
	virtual const BString& Name() const
	{
		return fName;
	}

	/** @brief Returns the integer width in bytes. */
	virtual target_size_t ByteSize() const
	{
		return fByteSize;
	}

	/** @brief Location resolution is not supported for synthetic types. */
	virtual status_t ResolveObjectDataLocation(
		const ValueLocation& objectLocation, ValueLocation*& _location)
	{
		// TODO: Implement!
		return B_UNSUPPORTED;
	}

	/** @brief Location resolution is not supported for synthetic types. */
	virtual status_t ResolveObjectDataLocation(target_addr_t objectAddress,
		ValueLocation*& _location)
	{
		// TODO: Implement!
		return B_UNSUPPORTED;
	}

	/** @brief Returns the underlying Haiku-style numeric type constant. */
	virtual uint32 TypeConstant() const
	{
		return fTypeConstant;
	}

private:
	BString	fID;
	BString	fName;
	uint32	fByteSize;
	uint32	fTypeConstant;
};


// #pragma mark - DwarfTypeFactory


/**
 * @brief Constructs the factory bound to a type context, lookup and
 *        cache. References on @a typeContext and @a typeCache are
 *        acquired.
 */
DwarfTypeFactory::DwarfTypeFactory(DwarfTypeContext* typeContext,
	GlobalTypeLookup* typeLookup, GlobalTypeCache* typeCache)
	:
	fTypeContext(typeContext),
	fTypeLookup(typeLookup),
	fTypeCache(typeCache)
{
	fTypeContext->AcquireReference();
	fTypeCache->AcquireReference();
}


/**
 * @brief Destroys the factory and releases held references.
 */
DwarfTypeFactory::~DwarfTypeFactory()
{
	fTypeContext->ReleaseReference();
	fTypeCache->ReleaseReference();
}


/**
 * @brief Materializes the DwarfType corresponding to a DIEType.
 *
 * Consults the cache by ID first, returns an additional reference if
 * found, and otherwise dispatches to one of the kind-specific
 * @c _Create*Type() helpers based on the entry's tag. Every constructed
 * type is added to the cache before being returned.
 *
 * @param typeEntry  DIE describing the type.
 * @param _type      Out parameter receiving the new DwarfType; reference
 *                   transferred to caller.
 * @retval B_OK              Type created or returned from cache.
 * @retval B_BAD_VALUE       @a typeEntry is unknown or has an unsupported
 *                           tag.
 * @retval B_NO_MEMORY       Allocation failure.
 * @retval B_ENTRY_NOT_FOUND Anonymous type whose definition could not be
 *                           located.
 * @retval other             Errors from helper routines.
 */
status_t
DwarfTypeFactory::CreateType(DIEType* typeEntry, DwarfType*& _type)
{
	// try the type cache first
	BString name;
	DwarfUtils::GetFullyQualifiedDIEName(typeEntry, name);

	TypeLookupConstraints constraints(
		dwarf_tag_to_type_kind(typeEntry->Tag()));
	int32 subtypeKind = dwarf_tag_to_subtype_kind(typeEntry->Tag());
	if (subtypeKind >= 0)
		constraints.SetSubtypeKind(subtypeKind);

	AutoLocker<GlobalTypeCache> cacheLocker(fTypeCache);
	Type* globalType = name.Length() > 0
		? fTypeCache->GetType(name, constraints) : NULL;
	if (globalType == NULL) {
		// lookup by name failed -- try lookup by ID
		BString id;
		if (DwarfType::GetTypeID(typeEntry, id))
			globalType = fTypeCache->GetTypeByID(id);
	}

	if (globalType != NULL) {
		DwarfType* globalDwarfType = dynamic_cast<DwarfType*>(globalType);
		if (globalDwarfType != NULL) {
			globalDwarfType->AcquireReference();
			_type = globalDwarfType;
			return B_OK;
		}
	}

	cacheLocker.Unlock();

	// If the type entry indicates a declaration only, we try to look the
	// type up globally first.
	if (typeEntry->IsDeclaration() && name.Length() > 0
		&& fTypeLookup->GetType(fTypeCache, name,
			constraints, globalType)
			== B_OK) {
		DwarfType* globalDwarfType
			= dynamic_cast<DwarfType*>(globalType);
		if (globalDwarfType != NULL) {
			_type = globalDwarfType;
			return B_OK;
		}

		globalType->ReleaseReference();
	}

	// No luck yet -- create the type.
	DwarfType* type;
	status_t error = _CreateTypeInternal(name, typeEntry, type);
	if (error != B_OK)
		return error;
	BReference<DwarfType> typeReference(type, true);

	// Insert the type into the cache. Re-check, as the type may already
	// have been inserted (e.g. in the compound type case).
	cacheLocker.Lock();
	if (name.Length() > 0
			? fTypeCache->GetType(name, constraints) == NULL
			: fTypeCache->GetTypeByID(type->ID()) == NULL) {
		error = fTypeCache->AddType(type);
		if (error != B_OK)
			return error;
	}
	cacheLocker.Unlock();

	// try to get the type's size
	uint64 size;
	if (_ResolveTypeByteSize(typeEntry, size) == B_OK)
		type->SetByteSize(size);

	_type = typeReference.Detach();
	return B_OK;
}


/**
 * @brief Internal dispatcher that creates a DwarfType subclass from a
 *        DIEType.
 *
 * Recognizes structure/class/union, base, pointer, reference, modifier,
 * typedef, array, enumeration, subrange, unspecified, subroutine and
 * pointer-to-member tags, delegating to the corresponding
 * @c _Create*Type() member.
 *
 * @param name       Name to attach to the type.
 * @param typeEntry  DIE describing the type.
 * @param _type      Out parameter receiving the new DwarfType.
 * @retval B_OK         Construction succeeded.
 * @retval B_BAD_VALUE  Unsupported DWARF tag.
 * @retval other        Errors from the helper.
 */
status_t
DwarfTypeFactory::_CreateTypeInternal(const BString& name,
	DIEType* typeEntry, DwarfType*& _type)
{
	switch (typeEntry->Tag()) {
		case DW_TAG_class_type:
		case DW_TAG_structure_type:
		case DW_TAG_union_type:
		case DW_TAG_interface_type:
			return _CreateCompoundType(name,
				dynamic_cast<DIECompoundType*>(typeEntry),
				(compound_type_kind)dwarf_tag_to_subtype_kind(
					typeEntry->Tag()), _type);

		case DW_TAG_base_type:
			return _CreatePrimitiveType(name,
				dynamic_cast<DIEBaseType*>(typeEntry), _type);

		case DW_TAG_pointer_type:
			return _CreateAddressType(name,
				dynamic_cast<DIEAddressingType*>(typeEntry),
				DERIVED_TYPE_POINTER, _type);
		case DW_TAG_reference_type:
			return _CreateAddressType(name,
				dynamic_cast<DIEAddressingType*>(typeEntry),
				DERIVED_TYPE_REFERENCE, _type);

		case DW_TAG_const_type:
			return _CreateModifiedType(name,
				dynamic_cast<DIEModifiedType*>(typeEntry),
				TYPE_MODIFIER_CONST, _type);
		case DW_TAG_packed_type:
			return _CreateModifiedType(name,
				dynamic_cast<DIEModifiedType*>(typeEntry),
				TYPE_MODIFIER_PACKED, _type);
		case DW_TAG_volatile_type:
			return _CreateModifiedType(name,
				dynamic_cast<DIEModifiedType*>(typeEntry),
				TYPE_MODIFIER_VOLATILE, _type);
		case DW_TAG_restrict_type:
			return _CreateModifiedType(name,
				dynamic_cast<DIEModifiedType*>(typeEntry),
				TYPE_MODIFIER_RESTRICT, _type);
		case DW_TAG_shared_type:
			return _CreateModifiedType(name,
				dynamic_cast<DIEModifiedType*>(typeEntry),
				TYPE_MODIFIER_SHARED, _type);

		case DW_TAG_typedef:
			return _CreateTypedefType(name,
				dynamic_cast<DIETypedef*>(typeEntry), _type);

		case DW_TAG_array_type:
			return _CreateArrayType(name,
				dynamic_cast<DIEArrayType*>(typeEntry), _type);

		case DW_TAG_enumeration_type:
			return _CreateEnumerationType(name,
				dynamic_cast<DIEEnumerationType*>(typeEntry), _type);

		case DW_TAG_subrange_type:
			return _CreateSubrangeType(name,
				dynamic_cast<DIESubrangeType*>(typeEntry), _type);

		case DW_TAG_unspecified_type:
			return _CreateUnspecifiedType(name,
				dynamic_cast<DIEUnspecifiedType*>(typeEntry), _type);

		case DW_TAG_subroutine_type:
			return _CreateFunctionType(name,
				dynamic_cast<DIESubroutineType*>(typeEntry), _type);

		case DW_TAG_ptr_to_member_type:
			return _CreatePointerToMemberType(name,
				dynamic_cast<DIEPointerToMemberType*>(typeEntry), _type);

		case DW_TAG_string_type:
		case DW_TAG_file_type:
		case DW_TAG_set_type:
			// TODO: Implement (not relevant for C++)!
			return B_UNSUPPORTED;
	}

	return B_UNSUPPORTED;
}


/**
 * @brief Builds a DwarfCompoundType, resolving inheritances, data members
 *        and template parameters via abstract-origin/specification
 *        lookup chains.
 *
 * Cross-image type identity is preserved by consulting the
 * GlobalTypeLookup before constructing locally; on a hit the existing
 * type is wrapped/reused, on miss the locally-built type is returned and
 * inserted into the cache.
 *
 * @param name          Compound type name.
 * @param typeEntry     DIE describing the compound.
 * @param compoundKind  Whether this is a struct, class, union or
 *                      interface.
 * @param _type         Out parameter receiving the compound type.
 * @return Status from intermediate lookups and constructors.
 */
status_t
DwarfTypeFactory::_CreateCompoundType(const BString& name,
	DIECompoundType* typeEntry, compound_type_kind compoundKind, DwarfType*& _type)
{
	TRACE_LOCALS("DwarfTypeFactory::_CreateCompoundType(\"%s\", %p, %d)\n",
		name.String(), typeEntry, compoundKind);

	// create the type
	DwarfCompoundType* type = new(std::nothrow) DwarfCompoundType(fTypeContext,
		name, typeEntry, compoundKind);
	if (type == NULL)
		return B_NO_MEMORY;
	BReference<DwarfCompoundType> typeReference(type, true);

	// Already add the type at this pointer to the cache, since otherwise
	// we could run into an infinite recursion when trying to create the types
	// for the data members.
// TODO: Since access to the type lookup context is multi-threaded, the
// incomplete type could become visible to other threads. Hence we keep the
// context locked, but that essentially kills multi-threading for this context.
	AutoLocker<GlobalTypeCache> cacheLocker(fTypeCache);
	status_t error = fTypeCache->AddType(type);
	if (error != B_OK)
{
printf("  -> failed to add type to cache\n");
		return error;
}
//	cacheLocker.Unlock();

	// find the abstract origin or specification that defines the data members
	DIECompoundType* memberOwnerEntry = DwarfUtils::GetDIEByPredicate(typeEntry,
		HasMembersPredicate());

	// create the data member objects
	if (memberOwnerEntry != NULL) {
		for (DebugInfoEntryList::ConstIterator it
					= memberOwnerEntry->DataMembers().GetIterator();
				DebugInfoEntry* _memberEntry = it.Next();) {
			DIEMember* memberEntry = dynamic_cast<DIEMember*>(_memberEntry);

			TRACE_LOCALS("  member %p\n", memberEntry);

			// get the type
			DwarfType* memberType;
			if (CreateType(memberEntry->GetType(), memberType) != B_OK)
				continue;
			BReference<DwarfType> memberTypeReference(memberType, true);

			// get the name
			BString memberName;
			DwarfUtils::GetDIEName(memberEntry, memberName);

			// create and add the member object
			DwarfDataMember* member = new(std::nothrow) DwarfDataMember(
				memberEntry, memberName, memberType);
			BReference<DwarfDataMember> memberReference(member, true);
			if (member == NULL || !type->AddDataMember(member)) {
				cacheLocker.Lock();
				fTypeCache->RemoveType(type);
				return B_NO_MEMORY;
			}
		}
	}

	// If the type is a class/struct/interface type, we also need to add its
	// base types, and possibly template parameters.
	if (DIEClassBaseType* classTypeEntry
			= dynamic_cast<DIEClassBaseType*>(typeEntry)) {
		// find the abstract origin or specification that defines the base types
		classTypeEntry = DwarfUtils::GetDIEByPredicate(classTypeEntry,
			HasBaseTypesPredicate());

		// create the inheritance objects for the base types
		if (classTypeEntry != NULL) {
			for (DebugInfoEntryList::ConstIterator it
						= classTypeEntry->BaseTypes().GetIterator();
					DebugInfoEntry* _inheritanceEntry = it.Next();) {
				DIEInheritance* inheritanceEntry
					= dynamic_cast<DIEInheritance*>(_inheritanceEntry);

				// get the type
				DwarfType* baseType;
				if (CreateType(inheritanceEntry->GetType(), baseType) != B_OK)
					continue;
				BReference<DwarfType> baseTypeReference(baseType, true);

				// create and add the inheritance object
				DwarfInheritance* inheritance = new(std::nothrow)
					DwarfInheritance(inheritanceEntry, baseType);
				BReference<DwarfInheritance> inheritanceReference(inheritance,
					true);
				if (inheritance == NULL || !type->AddInheritance(inheritance)) {
					cacheLocker.Lock();
					fTypeCache->RemoveType(type);
					return B_NO_MEMORY;
				}
			}
		}

		// find the abstract origin or specification that defines the template
		// parameters
		classTypeEntry = DwarfUtils::GetDIEByPredicate(
			dynamic_cast<DIEClassBaseType*>(typeEntry),
			HasTemplateParametersPredicate());

		if (classTypeEntry != NULL) {
			for (DebugInfoEntryList::ConstIterator it
						= classTypeEntry->TemplateParameters()
							.GetIterator();
					DebugInfoEntry* _typeEntry = it.Next();) {
				DIETemplateTypeParameter* templateTypeEntry
					= dynamic_cast<DIETemplateTypeParameter*>(_typeEntry);
				DwarfType* templateType;
				if (templateTypeEntry != NULL) {
					if (templateTypeEntry->GetType() == NULL
						|| CreateType(templateTypeEntry->GetType(),
							templateType) != B_OK) {
						continue;
					}
				} else {
					DIETemplateValueParameter* templateValueEntry
						= dynamic_cast<DIETemplateValueParameter*>(_typeEntry);
					if (CreateType(templateValueEntry->GetType(), templateType)
						!= B_OK) {
						continue;
					}
				}
				BReference<DwarfType> templateTypeReference(templateType,
					true);
				DwarfTemplateParameter* parameter
					= new(std::nothrow) DwarfTemplateParameter(_typeEntry,
						templateType);
				if (parameter == NULL) {
					cacheLocker.Lock();
					fTypeCache->RemoveType(type);
					return B_NO_MEMORY;
				}

				if (!type->AddTemplateParameter(parameter)) {
					cacheLocker.Lock();
					fTypeCache->RemoveType(type);
					return B_NO_MEMORY;
				}
			}
		}
	}

	_type = typeReference.Detach();
	return B_OK;;
}


/**
 * @brief Builds a DwarfPrimitiveType from a DIEBaseType, mapping the
 *        DWARF encoding/byte-size combination to a Haiku type constant.
 *
 * @param name      Primitive type name.
 * @param typeEntry DIE describing the base type.
 * @param _type     Out parameter receiving the new primitive type.
 * @retval B_OK         Type created.
 * @retval B_BAD_VALUE  Encoding/size combination is unsupported.
 * @retval B_NO_MEMORY  Allocation failure.
 */
status_t
DwarfTypeFactory::_CreatePrimitiveType(const BString& name,
	DIEBaseType* typeEntry, DwarfType*& _type)
{
	const DynamicAttributeValue* byteSizeValue = typeEntry->ByteSize();
//	const DynamicAttributeValue* bitOffsetValue = typeEntry->BitOffset();
	const DynamicAttributeValue* bitSizeValue = typeEntry->BitSize();

	uint32 bitSize = 0;
	if (byteSizeValue->IsValid()) {
		BVariant value;
		status_t error = fTypeContext->File()->EvaluateDynamicValue(
			fTypeContext->GetCompilationUnit(),
			fTypeContext->AddressSize(), fTypeContext->IsBigEndian(),
			fTypeContext->SubprogramEntry(), byteSizeValue,
			fTypeContext->TargetInterface(),
			fTypeContext->InstructionPointer(), fTypeContext->FramePointer(),
			value);
		if (error == B_OK && value.IsInteger())
			bitSize = value.ToUInt32() * 8;
	} else if (bitSizeValue->IsValid()) {
		BVariant value;
		status_t error = fTypeContext->File()->EvaluateDynamicValue(
			fTypeContext->GetCompilationUnit(),
			fTypeContext->AddressSize(), fTypeContext->IsBigEndian(),
			fTypeContext->SubprogramEntry(), bitSizeValue,
			fTypeContext->TargetInterface(),
			fTypeContext->InstructionPointer(), fTypeContext->FramePointer(),
			value);
		if (error == B_OK && value.IsInteger())
			bitSize = value.ToUInt32();
	}

	// determine type constant
	uint32 typeConstant = 0;
	switch (typeEntry->Encoding()) {
		case DW_ATE_boolean:
			typeConstant = B_BOOL_TYPE;
			break;

		case DW_ATE_float:
			switch (bitSize) {
				case 32:
					typeConstant = B_FLOAT_TYPE;
					break;
				case 64:
					typeConstant = B_DOUBLE_TYPE;
					break;
			}
			break;

		case DW_ATE_signed:
		case DW_ATE_signed_char:
			switch (bitSize) {
				case 8:
					typeConstant = B_INT8_TYPE;
					break;
				case 16:
					typeConstant = B_INT16_TYPE;
					break;
				case 32:
					typeConstant = B_INT32_TYPE;
					break;
				case 64:
					typeConstant = B_INT64_TYPE;
					break;
			}
			break;

		case DW_ATE_address:
		case DW_ATE_unsigned:
		case DW_ATE_unsigned_char:
			switch (bitSize) {
				case 8:
					typeConstant = B_UINT8_TYPE;
					break;
				case 16:
					typeConstant = B_UINT16_TYPE;
					break;
				case 32:
					typeConstant = B_UINT32_TYPE;
					break;
				case 64:
					typeConstant = B_UINT64_TYPE;
					break;
			}
			break;

		case DW_ATE_complex_float:
		case DW_ATE_imaginary_float:
		case DW_ATE_packed_decimal:
		case DW_ATE_numeric_string:
		case DW_ATE_edited:
		case DW_ATE_signed_fixed:
		case DW_ATE_unsigned_fixed:
		case DW_ATE_decimal_float:
		default:
			break;
	}

	// create the type
	DwarfPrimitiveType* type = new(std::nothrow) DwarfPrimitiveType(
		fTypeContext, name, typeEntry, typeConstant);
	if (type == NULL)
		return B_NO_MEMORY;

	_type = type;
	return B_OK;
}


/**
 * @brief Builds a DwarfAddressType (pointer or reference).
 *
 * Resolves the pointee type recursively; pointers to void are
 * accommodated by allowing a NULL base type.
 *
 * @param name         Address-type name.
 * @param typeEntry    DIE describing the addressing type.
 * @param addressKind  Pointer vs reference.
 * @param _type        Out parameter receiving the new type.
 * @return Status from recursive type creation.
 */
status_t
DwarfTypeFactory::_CreateAddressType(const BString& name,
	DIEAddressingType* typeEntry, address_type_kind addressKind,
	DwarfType*& _type)
{
	// get the base type entry
	DIEAddressingType* baseTypeOwnerEntry = DwarfUtils::GetDIEByPredicate(
		typeEntry, HasTypePredicate<DIEAddressingType>());

	// create the base type
	DwarfType* baseType;
	if (baseTypeOwnerEntry != NULL) {
		status_t error = CreateType(baseTypeOwnerEntry->GetType(), baseType);
		if (error != B_OK)
			return error;
	} else {
		// According to the DWARF 3 specs a modified type *has* a base type.
		// GCC 4 doesn't (always?) bother to add one for "void".
		// TODO: We should probably search for a respective type by name. ATM
		// we just create a DwarfUnspecifiedType without DIE.
		TRACE_LOCALS("no base type for address type entry -- creating "
			"unspecified type\n");
		baseType = new(std::nothrow) DwarfUnspecifiedType(fTypeContext, "void",
			NULL);
		if (baseType == NULL)
			return B_NO_MEMORY;
	}
	BReference<Type> baseTypeReference(baseType, true);

	DwarfAddressType* type = new(std::nothrow) DwarfAddressType(fTypeContext,
		name, typeEntry, addressKind, baseType);
	if (type == NULL)
		return B_NO_MEMORY;

	_type = type;
	return B_OK;
}


/**
 * @brief Builds a DwarfModifiedType wrapping a base type with one or more
 *        modifier flags.
 *
 * Modifier DIEs may be chained (e.g. @c const @c volatile T); this helper
 * recurses, accumulating the flags.
 *
 * @param name      Combined type name.
 * @param typeEntry DIE describing the modifier.
 * @param modifiers Already-accumulated modifier mask.
 * @param _type     Out parameter receiving the modified type.
 * @return Status from recursive type creation.
 */
status_t
DwarfTypeFactory::_CreateModifiedType(const BString& name,
	DIEModifiedType* typeEntry, uint32 modifiers, DwarfType*& _type)
{
	// Get the base type entry. If it is a modified type too or a typedef,
	// collect all modifiers and iterate until hitting an actual base type.
	DIEType* baseTypeEntry = NULL;
	DwarfType* baseType = NULL;
	while (true) {
		DIEModifiedType* baseTypeOwnerEntry = DwarfUtils::GetDIEByPredicate(
			typeEntry, HasTypePredicate<DIEModifiedType>());
		if (baseTypeOwnerEntry == NULL) {
			if (typeEntry->GetType() == NULL) {
				// in the case of a modified type that points to an
				// unspecified type (i.e. const void* in C/C++),
				// gcc appears to omit the base type attribute entirely.
				status_t result = _CreateUnspecifiedType(name,
					NULL, baseType);
				if (result != B_OK)
					return result;
				break;
			} else
				return B_BAD_VALUE;
		} else
			baseTypeEntry = baseTypeOwnerEntry->GetType();

		// resolve a typedef
		if (baseTypeEntry->Tag() == DW_TAG_typedef) {
			status_t error = _ResolveTypedef(
				dynamic_cast<DIETypedef*>(baseTypeEntry), baseTypeEntry);
			if (error != B_OK)
				return error;
		}

		if (baseTypeEntry == NULL)
			return B_BAD_VALUE;

		// If the base type is a modified type, too, resolve it.
		switch (baseTypeEntry->Tag()) {
			case DW_TAG_const_type:
				modifiers |= TYPE_MODIFIER_CONST;
				baseTypeOwnerEntry
					= dynamic_cast<DIEModifiedType*>(baseTypeEntry);
				continue;
			case DW_TAG_packed_type:
				modifiers |= TYPE_MODIFIER_PACKED;
				baseTypeOwnerEntry
					= dynamic_cast<DIEModifiedType*>(baseTypeEntry);
				continue;
			case DW_TAG_volatile_type:
				modifiers |= TYPE_MODIFIER_VOLATILE;
				baseTypeOwnerEntry
					= dynamic_cast<DIEModifiedType*>(baseTypeEntry);
				continue;
			case DW_TAG_restrict_type:
				modifiers |= TYPE_MODIFIER_RESTRICT;
				baseTypeOwnerEntry
					= dynamic_cast<DIEModifiedType*>(baseTypeEntry);
				continue;
			case DW_TAG_shared_type:
				modifiers |= TYPE_MODIFIER_SHARED;
				baseTypeOwnerEntry
					= dynamic_cast<DIEModifiedType*>(baseTypeEntry);
				continue;

			default:
				break;
		}

		// If we get here, we've found an actual base type.
		break;
	}

	if (baseType == NULL) {
		// create the base type
		status_t error = CreateType(baseTypeEntry, baseType);
		if (error != B_OK)
			return error;
	}

	BReference<Type> baseTypeReference(baseType, true);

	DwarfModifiedType* type = new(std::nothrow) DwarfModifiedType(fTypeContext,
		name, typeEntry, modifiers, baseType);
	if (type == NULL)
		return B_NO_MEMORY;

	_type = type;
	return B_OK;
}


/**
 * @brief Builds a DwarfTypedefType aliasing a previously-resolved base
 *        type.
 *
 * @param name       Typedef name.
 * @param typeEntry  DIE describing the typedef.
 * @param _type      Out parameter receiving the new typedef type.
 * @return Status from base-type resolution.
 */
status_t
DwarfTypeFactory::_CreateTypedefType(const BString& name,
	DIETypedef* typeEntry, DwarfType*& _type)
{
	// resolve the base type
	DIEType* baseTypeEntry;
	status_t error = _ResolveTypedef(typeEntry, baseTypeEntry);
	if (error != B_OK)
		return error;

	// create the base type
	DwarfType* baseType;
	error = CreateType(baseTypeEntry, baseType);
	if (error != B_OK)
		return error;
	BReference<Type> baseTypeReference(baseType, true);

	DwarfTypedefType* type = new(std::nothrow) DwarfTypedefType(fTypeContext,
		name, typeEntry, baseType);
	if (type == NULL)
		return B_NO_MEMORY;

	_type = type;
	return B_OK;
}


/**
 * @brief Builds a DwarfArrayType, resolving the element type and walking
 *        the chain of dimension DIEs to install one DwarfArrayDimension
 *        per dimension.
 *
 * @param name      Array type name.
 * @param typeEntry DIE describing the array.
 * @param _type     Out parameter receiving the new array type.
 * @return Status from element/dimension resolution.
 */
status_t
DwarfTypeFactory::_CreateArrayType(const BString& name,
	DIEArrayType* typeEntry, DwarfType*& _type)
{
	TRACE_LOCALS("DwarfTypeFactory::_CreateArrayType(\"%s\", %p)\n",
		name.String(), typeEntry);

	// create the base type
	DIEArrayType* baseTypeOwnerEntry = DwarfUtils::GetDIEByPredicate(
		typeEntry, HasTypePredicate<DIEArrayType>());
	if (baseTypeOwnerEntry == NULL) {
		WARNING("Failed to get base type for array type \"%s\"\n",
			name.String());
		return B_BAD_VALUE;
	}

	DwarfType* baseType = NULL;
	status_t error = CreateType(baseTypeOwnerEntry->GetType(), baseType);
	if (error != B_OK) {
		WARNING("Failed to create base type for array type \"%s\": %s\n",
			name.String(), strerror(error));
		return error;
	}
	BReference<Type> baseTypeReference(baseType, true);

	// create the array type
	DwarfArrayType* type = new(std::nothrow) DwarfArrayType(fTypeContext, name,
		typeEntry, baseType);
	if (type == NULL)
		return B_NO_MEMORY;
	BReference<DwarfType> typeReference(type, true);

	// add the array dimensions
	DIEArrayType* dimensionOwnerEntry = DwarfUtils::GetDIEByPredicate(
		typeEntry, HasDimensionsPredicate());

	if (dimensionOwnerEntry == NULL) {
		WARNING("Failed to get dimensions for array type \"%s\"\n",
			name.String());
		return B_BAD_VALUE;
	}

	for (DebugInfoEntryList::ConstIterator it
				= dimensionOwnerEntry->Dimensions().GetIterator();
			DebugInfoEntry* _dimensionEntry = it.Next();) {
		DIEType* dimensionEntry = dynamic_cast<DIEType*>(_dimensionEntry);

		// get/create the dimension type
		DwarfType* dimensionType = NULL;
		status_t error = CreateType(dimensionEntry, dimensionType);
		if (error != B_OK) {
			WARNING("Failed to create type for array dimension: %s\n",
				strerror(error));
			return error;
		}
		BReference<Type> dimensionTypeReference(dimensionType, true);

		// create and add the array dimension object
		DwarfArrayDimension* dimension
			= new(std::nothrow) DwarfArrayDimension(dimensionType);
		BReference<DwarfArrayDimension> dimensionReference(dimension, true);
		if (dimension == NULL || !type->AddDimension(dimension))
			return B_NO_MEMORY;
	}

	_type = typeReference.Detach();
	return B_OK;
}


/**
 * @brief Builds a DwarfEnumerationType, resolving the optional integer
 *        base type and adding one DwarfEnumeratorValue per declared
 *        enumerator.
 *
 * @param name      Enumeration type name.
 * @param typeEntry DIE describing the enumeration.
 * @param _type     Out parameter receiving the new type.
 * @return Status from base-type resolution and enumerator construction.
 */
status_t
DwarfTypeFactory::_CreateEnumerationType(const BString& name,
	DIEEnumerationType* typeEntry, DwarfType*& _type)
{
	// create the base type (it's optional)
	DIEEnumerationType* baseTypeOwnerEntry = DwarfUtils::GetDIEByPredicate(
		typeEntry, HasTypePredicate<DIEEnumerationType>());

	DwarfType* baseType = NULL;
	if (baseTypeOwnerEntry != NULL) {
		status_t error = CreateType(baseTypeOwnerEntry->GetType(), baseType);
		if (error != B_OK)
			return error;
	}
	BReference<Type> baseTypeReference(baseType, true);

	// create the enumeration type
	DwarfEnumerationType* type = new(std::nothrow) DwarfEnumerationType(
		fTypeContext, name, typeEntry, baseType);
	if (type == NULL)
		return B_NO_MEMORY;
	BReference<DwarfEnumerationType> typeReference(type, true);

	// get the enumeration values
	DIEEnumerationType* enumeratorOwnerEntry = DwarfUtils::GetDIEByPredicate(
		typeEntry, HasEnumeratorsPredicate());

	if (enumeratorOwnerEntry != NULL) {
		for (DebugInfoEntryList::ConstIterator it
					= enumeratorOwnerEntry->Enumerators().GetIterator();
				DebugInfoEntry* _enumeratorEntry = it.Next();) {
			DIEEnumerator* enumeratorEntry = dynamic_cast<DIEEnumerator*>(
				_enumeratorEntry);

			// evaluate the value
			BVariant value;
			status_t error = fTypeContext->File()->EvaluateConstantValue(
				fTypeContext->GetCompilationUnit(),
				fTypeContext->AddressSize(), fTypeContext->IsBigEndian(),
				fTypeContext->SubprogramEntry(), enumeratorEntry->ConstValue(),
				fTypeContext->TargetInterface(),
				fTypeContext->InstructionPointer(),
				fTypeContext->FramePointer(), value);
			if (error != B_OK) {
				// The value is probably not stored -- just ignore the
				// enumerator.
				TRACE_LOCALS("Failed to get value for enum type value %s::%s\n",
					name.String(), enumeratorEntry->Name());
				continue;
			}

			// create and add the enumeration value object
			DwarfEnumeratorValue* enumValue
				= new(std::nothrow) DwarfEnumeratorValue(enumeratorEntry,
					enumeratorEntry->Name(), value);
			BReference<DwarfEnumeratorValue> enumValueReference(enumValue,
				true);
			if (enumValue == NULL || !type->AddValue(enumValue))
				return B_NO_MEMORY;
		}
	}

	_type = typeReference.Detach();
	return B_OK;
}


/**
 * @brief Builds a DwarfSubrangeType, computing low/high bounds and
 *        synthesizing an integer base type when DWARF lacks an explicit
 *        one.
 *
 * Lower-bound, upper-bound and count attributes may live on
 * abstract-origin/specification chains; the helper walks them via
 * DwarfUtils::GetDIEByPredicate(). Missing base types fall back to an
 * ArtificialIntegerType chosen by the bound widths.
 *
 * @param name      Subrange type name.
 * @param typeEntry DIE describing the subrange.
 * @param _type     Out parameter receiving the new type.
 * @return Status from bound evaluation and base-type creation.
 */
status_t
DwarfTypeFactory::_CreateSubrangeType(const BString& name,
	DIESubrangeType* typeEntry, DwarfType*& _type)
{
	// get the base type
	DIESubrangeType* baseTypeOwnerEntry = DwarfUtils::GetDIEByPredicate(
		typeEntry, HasTypePredicate<DIESubrangeType>());
	DIEType* baseTypeEntry = baseTypeOwnerEntry != NULL
		? baseTypeOwnerEntry->GetType() : NULL;

	// get the lower bound
	BVariant lowerBound;
	DIESubrangeType* lowerBoundOwnerEntry = DwarfUtils::GetDIEByPredicate(
		typeEntry, HasLowerBoundPredicate());
	if (lowerBoundOwnerEntry != NULL) {
		// evaluate it
		DIEType* valueType;
		status_t error = fTypeContext->File()->EvaluateDynamicValue(
			fTypeContext->GetCompilationUnit(),
			fTypeContext->AddressSize(), fTypeContext->IsBigEndian(),
			fTypeContext->SubprogramEntry(),
			lowerBoundOwnerEntry->LowerBound(),
			fTypeContext->TargetInterface(),
			fTypeContext->InstructionPointer(),
			fTypeContext->FramePointer(), lowerBound, &valueType);
		if (error != B_OK) {
			WARNING("  failed to evaluate lower bound: %s\n", strerror(error));
			return error;
		}

		// If we don't have a base type yet, and the lower bound attribute
		// refers to an object, the type of that object is our base type.
		if (baseTypeEntry == NULL)
			baseTypeEntry = valueType;
	} else {
		// that's ok -- use the language default
		lowerBound.SetTo(fTypeContext->GetCompilationUnit()->SourceLanguage()
			->subrangeLowerBound);
	}

	// get the upper bound
	BVariant upperBound;
	DIESubrangeType* upperBoundOwnerEntry = DwarfUtils::GetDIEByPredicate(
		typeEntry, HasUpperBoundPredicate());
	if (upperBoundOwnerEntry != NULL) {
		// evaluate it
		DIEType* valueType;
		status_t error = fTypeContext->File()->EvaluateDynamicValue(
			fTypeContext->GetCompilationUnit(),
			fTypeContext->AddressSize(), fTypeContext->IsBigEndian(),
			fTypeContext->SubprogramEntry(),
			upperBoundOwnerEntry->UpperBound(),
			fTypeContext->TargetInterface(),
			fTypeContext->InstructionPointer(), fTypeContext->FramePointer(),
			upperBound, &valueType);
		if (error != B_OK) {
			WARNING("  failed to evaluate upper bound: %s\n", strerror(error));
			return error;
		}

		// If we don't have a base type yet, and the upper bound attribute
		// refers to an object, the type of that object is our base type.
		if (baseTypeEntry == NULL)
			baseTypeEntry = valueType;
	} else {
		// get the count instead
		DIESubrangeType* countOwnerEntry = DwarfUtils::GetDIEByPredicate(
			typeEntry, HasCountPredicate());
		if (countOwnerEntry != NULL) {
			// evaluate it
			BVariant count;
			DIEType* valueType;
			status_t error = fTypeContext->File()->EvaluateDynamicValue(
				fTypeContext->GetCompilationUnit(),
				fTypeContext->AddressSize(), fTypeContext->IsBigEndian(),
				fTypeContext->SubprogramEntry(),
				countOwnerEntry->Count(), fTypeContext->TargetInterface(),
				fTypeContext->InstructionPointer(),
				fTypeContext->FramePointer(), count, &valueType);
			if (error != B_OK) {
				WARNING("  failed to evaluate count: %s\n", strerror(error));
				return error;
			}

			// If we don't have a base type yet, and the count attribute refers
			// to an object, the type of that object is our base type.
			if (baseTypeEntry == NULL)
				baseTypeEntry = valueType;

			// we only support integers
			bool isSigned;
			if (!lowerBound.IsInteger(&isSigned) || !count.IsInteger()) {
				WARNING("  count given for subrange type, but lower bound or "
					"count is not integer\n");
				return B_BAD_VALUE;
			}

			if (isSigned)
				upperBound.SetTo(lowerBound.ToInt64() + count.ToInt64() - 1);
			else
				upperBound.SetTo(lowerBound.ToUInt64() + count.ToUInt64() - 1);
		}
	}

	// create the base type
	Type* baseType = NULL;
	status_t error;
	if (baseTypeEntry != NULL) {
		DwarfType* dwarfBaseType;
		error = CreateType(baseTypeEntry, dwarfBaseType);
		baseType = dwarfBaseType;
	} else {
		// We still don't have a base type yet. In this case the base type is
		// supposed to be a signed integer type with the same size as an address
		// for that compilation unit.
		error = ArtificialIntegerType::Create(
			fTypeContext->GetCompilationUnit()->AddressSize(), true, baseType);
	}
	if (error != B_OK)
		return error;
	BReference<Type> baseTypeReference(baseType, true);

	// TODO: Support the thread scaling attribute!

	// create the type
	DwarfSubrangeType* type = new(std::nothrow) DwarfSubrangeType(fTypeContext,
		name, typeEntry, baseType, lowerBound, upperBound);
	if (type == NULL)
		return B_NO_MEMORY;

	_type = type;
	return B_OK;
}


/**
 * @brief Builds a DwarfUnspecifiedType placeholder.
 *
 * @param name      Type name (often a language-specific keyword).
 * @param typeEntry DIE describing the unspecified type.
 * @param _type     Out parameter receiving the new type.
 * @retval B_OK         Type created.
 * @retval B_NO_MEMORY  Allocation failure.
 */
status_t
DwarfTypeFactory::_CreateUnspecifiedType(const BString& name,
	DIEUnspecifiedType* typeEntry, DwarfType*& _type)
{
	DwarfUnspecifiedType* type = new(std::nothrow) DwarfUnspecifiedType(
		fTypeContext, name, typeEntry);
	if (type == NULL)
		return B_NO_MEMORY;

	_type = type;
	return B_OK;
}

/**
 * @brief Builds a DwarfFunctionType, resolving the return type (if any)
 *        and walking the parameter chain to install one
 *        DwarfFunctionParameter per formal parameter.
 *
 * Sets the variadic flag when an unspecified-parameters DIE is present.
 *
 * @param name      Function type name.
 * @param typeEntry DIE describing the subroutine.
 * @param _type     Out parameter receiving the new function type.
 * @return Status from return-type and parameter construction.
 */
status_t
DwarfTypeFactory::_CreateFunctionType(const BString& name,
	DIESubroutineType* typeEntry, DwarfType*& _type)
{
	// get the return type
	DIESubroutineType* returnTypeOwnerEntry = DwarfUtils::GetDIEByPredicate(
		typeEntry, HasReturnTypePredicate<DIESubroutineType>());

	// create the base type
	DwarfType* returnType = NULL;
	if (returnTypeOwnerEntry != NULL) {
		status_t error = CreateType(returnTypeOwnerEntry->ReturnType(),
			returnType);
		if (error != B_OK)
			return error;
	}
	BReference<Type> returnTypeReference(returnType, true);

	DwarfFunctionType* type = new(std::nothrow) DwarfFunctionType(fTypeContext,
		name, typeEntry, returnType);
	if (type == NULL)
		return B_NO_MEMORY;
	BReference<DwarfType> typeReference(type, true);

	// get the parameters
	DIESubroutineType* parameterOwnerEntry = DwarfUtils::GetDIEByPredicate(
		typeEntry, HasParametersPredicate<DIESubroutineType>());

	if (parameterOwnerEntry != NULL) {
		for (DebugInfoEntryList::ConstIterator it
					= parameterOwnerEntry->Parameters().GetIterator();
				DebugInfoEntry* _parameterEntry = it.Next();) {
			if (_parameterEntry->Tag() == DW_TAG_unspecified_parameters) {
				type->SetHasVariableArguments(true);
				continue;
			}

			DIEFormalParameter* parameterEntry
				= dynamic_cast<DIEFormalParameter*>(_parameterEntry);

			// get the type
			DIEFormalParameter* typeOwnerEntry = DwarfUtils::GetDIEByPredicate(
				parameterEntry, HasTypePredicate<DIEFormalParameter>());
			if (typeOwnerEntry == NULL)
				return B_BAD_VALUE;

			DwarfType* parameterType;
			status_t error = CreateType(typeOwnerEntry->GetType(),
				parameterType);
			if (error != B_OK)
				return error;
			BReference<DwarfType> parameterTypeReference(parameterType, true);

			// get the name
			BString parameterName;
			DwarfUtils::GetDIEName(parameterEntry, parameterName);

			// create and add the parameter object
			DwarfFunctionParameter* parameter
				= new(std::nothrow) DwarfFunctionParameter(parameterEntry,
					parameterName, parameterType);
			BReference<DwarfFunctionParameter> parameterReference(parameter,
				true);
			if (parameter == NULL || !type->AddParameter(parameter))
				return B_NO_MEMORY;
		}
	}


	_type = typeReference.Detach();
	return B_OK;
}


/**
 * @brief Builds a DwarfPointerToMemberType, resolving both the pointee
 *        type and the containing compound type.
 *
 * @param name      Type name.
 * @param typeEntry DIE describing the pointer-to-member.
 * @param _type     Out parameter receiving the new type.
 * @retval B_OK         Type created.
 * @retval B_BAD_VALUE  The DIE lacks a containing-type attribute or it
 *                      does not refer to a compound.
 * @retval other        Errors from recursive type creation.
 */
status_t
DwarfTypeFactory::_CreatePointerToMemberType(const BString& name,
	DIEPointerToMemberType* typeEntry, DwarfType*& _type)
{
	// get the containing and base type entries
	DIEPointerToMemberType* containingTypeOwnerEntry
		= DwarfUtils::GetDIEByPredicate(typeEntry,
			HasContainingTypePredicate());
	DIEPointerToMemberType* baseTypeOwnerEntry = DwarfUtils::GetDIEByPredicate(
		typeEntry, HasTypePredicate<DIEPointerToMemberType>());

	if (containingTypeOwnerEntry == NULL || baseTypeOwnerEntry == NULL) {
		WARNING("Failed to get containing or base type for pointer to member "
			"type \"%s\"\n", name.String());
		return B_BAD_VALUE;
	}

	// create the containing type
	DwarfType* containingType;
	status_t error = CreateType(containingTypeOwnerEntry->ContainingType(),
		containingType);
	if (error != B_OK)
		return error;
	BReference<Type> containingTypeReference(containingType, true);

	DwarfCompoundType* compoundContainingType
		= dynamic_cast<DwarfCompoundType*>(containingType);
	if (compoundContainingType == NULL) {
		WARNING("Containing type for pointer to member type \"%s\" is not a "
			"compound type.\n", name.String());
		return B_BAD_VALUE;
	}

	// create the base type
	DwarfType* baseType;
	error = CreateType(baseTypeOwnerEntry->GetType(), baseType);
	if (error != B_OK)
		return error;
	BReference<Type> baseTypeReference(baseType, true);

	// create the type object
	DwarfPointerToMemberType* type = new(std::nothrow) DwarfPointerToMemberType(
		fTypeContext, name, typeEntry, compoundContainingType, baseType);
	if (type == NULL)
		return B_NO_MEMORY;

	_type = type;
	return B_OK;
}


/**
 * @brief Walks a chain of DIETypedef entries until a non-typedef base is
 *        found.
 *
 * @param entry           First typedef in the chain.
 * @param _baseTypeEntry  Out parameter receiving the underlying type DIE.
 * @retval B_OK              The chain terminated in a non-typedef DIE.
 * @retval B_ENTRY_NOT_FOUND The chain dead-ended without a target type.
 */
status_t
DwarfTypeFactory::_ResolveTypedef(DIETypedef* entry,
	DIEType*& _baseTypeEntry)
{
	while (true) {
		// resolve the base type, possibly following abstract origin or
		// specification
		DIETypedef* baseTypeOwnerEntry = DwarfUtils::GetDIEByPredicate(
			entry, HasTypePredicate<DIETypedef>());
		if (baseTypeOwnerEntry == NULL)
			return B_BAD_VALUE;

		DIEType* baseTypeEntry = baseTypeOwnerEntry->GetType();
		if (baseTypeEntry->Tag() != DW_TAG_typedef) {
			_baseTypeEntry = baseTypeEntry;
			return B_OK;
		}

		entry = dynamic_cast<DIETypedef*>(baseTypeEntry);
	}
}


/**
 * @brief Recursively determines the byte size of a DIE-described type.
 *
 * Honors @c DW_AT_byte_size when present, follows typedefs, computes
 * compound size from member layout, and uses pointee size for derived
 * pointer/reference types.
 *
 * @param typeEntry  DIE describing the type.
 * @param _size      Out parameter receiving the resolved size.
 * @retval B_OK              Size resolved.
 * @retval B_BAD_VALUE       Type cannot be sized statically.
 * @retval other             Errors from attribute evaluation.
 */
status_t
DwarfTypeFactory::_ResolveTypeByteSize(DIEType* typeEntry,
	uint64& _size)
{
	TRACE_LOCALS("DwarfTypeFactory::_ResolveTypeByteSize(%p)\n",
		typeEntry);

	// get the size attribute
	const DynamicAttributeValue* sizeValue;

	while (true) {
		// resolve a typedef
		if (typeEntry->Tag() == DW_TAG_typedef) {
			TRACE_LOCALS("  resolving typedef...\n");

			status_t error = _ResolveTypedef(
				dynamic_cast<DIETypedef*>(typeEntry), typeEntry);
			if (error != B_OK)
				return error;
		}

		sizeValue = typeEntry->ByteSize();
		if (sizeValue != NULL && sizeValue->IsValid())
			break;

		// resolve abstract origin
		if (DIEType* abstractOrigin = dynamic_cast<DIEType*>(
				typeEntry->AbstractOrigin())) {
			TRACE_LOCALS("  resolving abstract origin (%p)...\n",
				abstractOrigin);

			typeEntry = abstractOrigin;
			sizeValue = typeEntry->ByteSize();
			if (sizeValue != NULL && sizeValue->IsValid())
				break;
		}

		// resolve specification
		if (DIEType* specification = dynamic_cast<DIEType*>(
				typeEntry->Specification())) {
			TRACE_LOCALS("  resolving specification (%p)...\n", specification);

			typeEntry = specification;
			sizeValue = typeEntry->ByteSize();
			if (sizeValue != NULL && sizeValue->IsValid())
				break;
		}

		// For some types we have a special handling. For modified types we
		// follow the base type, for address types we know the size anyway.
		TRACE_LOCALS("  nothing yet, special type handling\n");

		switch (typeEntry->Tag()) {
			case DW_TAG_const_type:
			case DW_TAG_packed_type:
			case DW_TAG_volatile_type:
			case DW_TAG_restrict_type:
			case DW_TAG_shared_type:
				typeEntry = dynamic_cast<DIEModifiedType*>(typeEntry)
					->GetType();

				TRACE_LOCALS("  following modified type -> %p\n", typeEntry);

				if (typeEntry == NULL)
					return B_ENTRY_NOT_FOUND;
				break;
			case DW_TAG_pointer_type:
			case DW_TAG_reference_type:
			case DW_TAG_ptr_to_member_type:
				_size = fTypeContext->GetCompilationUnit()->AddressSize();

				TRACE_LOCALS("  pointer/reference type: size: %" B_PRIu64 "\n",
					_size);

				return B_OK;
			default:
				return B_ENTRY_NOT_FOUND;
		}
	}

	TRACE_LOCALS("  found attribute\n");

	// get the actual value
	BVariant size;
	status_t error = fTypeContext->File()->EvaluateDynamicValue(
		fTypeContext->GetCompilationUnit(),
		fTypeContext->AddressSize(), fTypeContext->IsBigEndian(),
		fTypeContext->SubprogramEntry(), sizeValue,
		fTypeContext->TargetInterface(), fTypeContext->InstructionPointer(),
		fTypeContext->FramePointer(), size);
	if (error != B_OK) {
		TRACE_LOCALS("  failed to resolve attribute: %s\n", strerror(error));
		return error;
	}

	_size = size.ToUInt64();

	TRACE_LOCALS("  -> size: %" B_PRIu64 "\n", _size);

	return B_OK;
}
