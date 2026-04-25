/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2009, Haiku.
 * Original authors: Ingo Weinhold.
 */

/** @file DwarfTypeFactory.h
    @brief Factory that turns DIEType nodes into DwarfType-derived objects,
           consulting and populating the global type cache. */

#ifndef DWARF_TYPE_FACTORY_H
#define DWARF_TYPE_FACTORY_H


#include <String.h>

#include "Type.h"


class CompilationUnit;
class DIEAddressingType;
class DIEArrayType;
class DIEBaseType;
class DIECompoundType;
class DIEEnumerationType;
class DIEFormalParameter;
class DIEModifiedType;
class DIEPointerToMemberType;
class DIESubprogram;
class DIESubrangeType;
class DIESubroutineType;
class DIEType;
class DIETypedef;
class DIEUnspecifiedType;
class DwarfAddressType;
class DwarfArrayDimension;
class DwarfArrayType;
class DwarfCompoundType;
class DwarfDataMember;
class DwarfEnumerationType;
class DwarfEnumeratorValue;
class DwarfFile;
class DwarfFunctionParameter;
class DwarfFunctionType;
class DwarfInheritance;
class DwarfModifiedType;
class DwarfPointerToMemberType;
class DwarfPrimitiveType;
class DwarfSubrangeType;
class DwarfTargetInterface;
class DwarfType;
class DwarfTypeContext;
class DwarfTypedefType;
struct DwarfUnspecifiedType;
class GlobalTypeCache;
class GlobalTypeLookup;
struct LocationDescription;
struct MemberLocation;
class RegisterMap;


/** @brief Constructs DwarfType-derived objects on demand from DIEType
           entries, resolving typedefs and caching results in the global
           type cache. */
class DwarfTypeFactory {
public:
								DwarfTypeFactory(DwarfTypeContext* typeContext,
									GlobalTypeLookup* typeLookup,
									GlobalTypeCache* typeCache);
								~DwarfTypeFactory();

			status_t			CreateType(DIEType* typeEntry,
									DwarfType*& _type);
									// returns reference

private:
			status_t			_CreateTypeInternal(const BString& name,
									DIEType* typeEntry, DwarfType*& _type);

			status_t			_CreateCompoundType(const BString& name,
									DIECompoundType* typeEntry,
									compound_type_kind compoundKind,
									DwarfType*& _type);
			status_t			_CreatePrimitiveType(const BString& name,
									DIEBaseType* typeEntry,
									DwarfType*& _type);
			status_t			_CreateAddressType(const BString& name,
									DIEAddressingType* typeEntry,
									address_type_kind addressKind,
									DwarfType*& _type);
			status_t			_CreateModifiedType(const BString& name,
									DIEModifiedType* typeEntry,
									uint32 modifiers, DwarfType*& _type);
			status_t			_CreateTypedefType(const BString& name,
									DIETypedef* typeEntry, DwarfType*& _type);
			status_t			_CreateArrayType(const BString& name,
									DIEArrayType* typeEntry,
									DwarfType*& _type);
			status_t			_CreateEnumerationType(const BString& name,
									DIEEnumerationType* typeEntry,
									DwarfType*& _type);
			status_t			_CreateSubrangeType(const BString& name,
									DIESubrangeType* typeEntry,
									DwarfType*& _type);
			status_t			_CreateUnspecifiedType(const BString& name,
									DIEUnspecifiedType* typeEntry,
									DwarfType*& _type);
			status_t			_CreateFunctionType(const BString& name,
									DIESubroutineType* typeEntry,
									DwarfType*& _type);
			status_t			_CreatePointerToMemberType(const BString& name,
									DIEPointerToMemberType* typeEntry,
									DwarfType*& _type);

			status_t			_ResolveTypedef(DIETypedef* entry,
									DIEType*& _baseTypeEntry);
			status_t			_ResolveTypeByteSize(DIEType* typeEntry,
									uint64& _size);

private:
			class ArtificialIntegerType;

private:
			DwarfTypeContext*	fTypeContext;
			GlobalTypeLookup*	fTypeLookup;
			GlobalTypeCache*	fTypeCache;
};


#endif	// DWARF_TYPE_FACTORY_H
