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
 *   Copyright 2011-2014, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DebugInfoEntries.cpp
 * @brief Member-function bodies for every concrete DIE class.
 *
 * Each DIE subclass implements a small set of overrides that mirror the
 * DWARF attributes its tag accepts: an @c InitAfterAttributes hook that
 * runs once parsing is complete, a few accessors that expose
 * tag-specific values (e.g. @c LowPC, @c Specification, @c ConstValue),
 * and a number of @c AddAttribute_xxx setters that route attribute
 * values into typed storage.  The file is organised by class with
 * @c #pragma mark separators between sections.
 */

#include "DebugInfoEntries.h"

#include <new>

#include "AttributeValue.h"
#include "Dwarf.h"
#include "SourceLanguageInfo.h"


// #pragma mark - DIECompileUnitBase

/**
 * @brief Constructs a DIECompileUnitBase.
 */
DIECompileUnitBase::DIECompileUnitBase()
	:
	fName(NULL),
	fCompilationDir(NULL),
	fLowPC(0),
	fHighPC(0),
	fStatementListOffset(-1),
	fMacroInfoOffset(-1),
	fAddressRangesOffset(-1),
	fBaseTypesUnit(NULL),
	fLanguage(0),
	fIdentifierCase(0),
	fUseUTF8(true),
	fContainsMainSubprogram(false)
{
}

/**
 * @brief Destroys the DIECompileUnitBase.
 */
DIECompileUnitBase::~DIECompileUnitBase()
{
}


/**
 * @brief Post-parse hook invoked once every attribute has been read.
 */
status_t
DIECompileUnitBase::InitAfterAttributes(DebugInfoEntryInitInfo& info)
{
	switch (fLanguage) {
		case 0:
			info.languageInfo = &kUnknownLanguageInfo;
			return B_OK;
		case DW_LANG_C89:
			info.languageInfo = &kC89LanguageInfo;
			return B_OK;
		case DW_LANG_C:
			info.languageInfo = &kCLanguageInfo;
			return B_OK;
		case DW_LANG_C_plus_plus:
			info.languageInfo = &kCPlusPlusLanguageInfo;
			return B_OK;
		case DW_LANG_C99:
			info.languageInfo = &kC99LanguageInfo;
			return B_OK;
		default:
			info.languageInfo = &kUnsupportedLanguageInfo;
			return B_OK;
	}
}


/**
 * @brief Returns the DIE's name (or the DIE chain's name).
 */
const char*
DIECompileUnitBase::Name() const
{
	return fName;
}


/**
 * @brief Adds a child DIE during parsing.
 */
status_t
DIECompileUnitBase::AddChild(DebugInfoEntry* child)
{
	if (child->IsType())
		fTypes.Add(child);
	else
		fOtherChildren.Add(child);
	return B_OK;
}


/**
 * @brief Stores the DW_AT_name attribute value.
 */
status_t
DIECompileUnitBase::AddAttribute_name(uint16 attributeName,
	const AttributeValue& value)
{
	fName = value.string;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_comp_dir attribute value.
 */
status_t
DIECompileUnitBase::AddAttribute_comp_dir(uint16 attributeName,
	const AttributeValue& value)
{
	fCompilationDir = value.string;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_low_pc attribute value.
 */
status_t
DIECompileUnitBase::AddAttribute_low_pc(uint16 attributeName,
	const AttributeValue& value)
{
	fLowPC = value.address;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_high_pc attribute value.
 */
status_t
DIECompileUnitBase::AddAttribute_high_pc(uint16 attributeName,
	const AttributeValue& value)
{
	fHighPC = value.address;
	if (fLowPC != 0 && fHighPC < fLowPC)
		fHighPC += fLowPC;

	return B_OK;
}


/**
 * @brief Stores the DW_AT_producer attribute value.
 */
status_t
DIECompileUnitBase::AddAttribute_producer(uint16 attributeName,
	const AttributeValue& value)
{
	// not interesting
	return B_OK;
}


/**
 * @brief Stores the DW_AT_stmt_list attribute value.
 */
status_t
DIECompileUnitBase::AddAttribute_stmt_list(uint16 attributeName,
	const AttributeValue& value)
{
	fStatementListOffset = value.pointer;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_macro_info attribute value.
 */
status_t
DIECompileUnitBase::AddAttribute_macro_info(uint16 attributeName,
	const AttributeValue& value)
{
	fMacroInfoOffset = value.pointer;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_base_types attribute value.
 */
status_t
DIECompileUnitBase::AddAttribute_base_types(uint16 attributeName,
	const AttributeValue& value)
{
	fBaseTypesUnit = dynamic_cast<DIECompileUnitBase*>(value.reference);
	return fBaseTypesUnit != NULL ? B_OK : B_BAD_DATA;
}


/**
 * @brief Stores the DW_AT_language attribute value.
 */
status_t
DIECompileUnitBase::AddAttribute_language(uint16 attributeName,
	const AttributeValue& value)
{
	fLanguage = value.constant;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_identifier_case attribute value.
 */
status_t
DIECompileUnitBase::AddAttribute_identifier_case(uint16 attributeName,
	const AttributeValue& value)
{
	fIdentifierCase = value.constant;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_use_UTF8 attribute value.
 */
status_t
DIECompileUnitBase::AddAttribute_use_UTF8(uint16 attributeName,
	const AttributeValue& value)
{
	fUseUTF8 = value.flag;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_ranges attribute value.
 */
status_t
DIECompileUnitBase::AddAttribute_ranges(uint16 attributeName,
	const AttributeValue& value)
{
	fAddressRangesOffset = value.pointer;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_main_subprogram attribute value.
 */
status_t
DIECompileUnitBase::AddAttribute_main_subprogram(uint16 attributeName,
	const AttributeValue& value)
{
	fContainsMainSubprogram = true;
	return B_OK;
}


// #pragma mark - DIEType

/**
 * @brief Constructs a DIEType.
 */
DIEType::DIEType()
	:
	fName(NULL)
{
	fAllocated.SetTo((uint64)0);
	fAssociated.SetTo((uint64)0);
}


/**
 * @brief Reports whether this DIE represents a type.
 */
bool
DIEType::IsType() const
{
	return true;
}


/**
 * @brief Returns the DIE's name (or the DIE chain's name).
 */
const char*
DIEType::Name() const
{
	return fName;
}


/**
 * @brief Reports whether this DIE is a non-defining declaration.
 */
bool
DIEType::IsDeclaration() const
{
	return false;
}


/**
 * @brief Returns the byte size of values of this type.
 */
const DynamicAttributeValue*
DIEType::ByteSize() const
{
	return NULL;
}


/**
 * @brief Stores the DW_AT_name attribute value.
 */
status_t
DIEType::AddAttribute_name(uint16 attributeName,
	const AttributeValue& value)
{
	fName = value.string;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_allocated attribute value.
 */
status_t
DIEType::AddAttribute_allocated(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fAllocated, value);
}


/**
 * @brief Stores the DW_AT_associated attribute value.
 */
status_t
DIEType::AddAttribute_associated(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fAssociated, value);
}


// #pragma mark - DIEModifiedType

/**
 * @brief Constructs a DIEModifiedType.
 */
DIEModifiedType::DIEModifiedType()
	:
	fType(NULL)
{
}


/**
 * @brief Stores the DW_AT_type attribute value.
 */
status_t
DIEModifiedType::AddAttribute_type(uint16 attributeName,
	const AttributeValue& value)
{
	fType = dynamic_cast<DIEType*>(value.reference);
	return fType != NULL ? B_OK : B_BAD_DATA;
}


// #pragma mark - DIEAddressingType

/**
 * @brief Constructs a DIEAddressingType.
 */
DIEAddressingType::DIEAddressingType()
	:
	fAddressClass(0)
{
}


/**
 * @brief Stores the DW_AT_address_class attribute value.
 */
status_t
DIEAddressingType::AddAttribute_address_class(uint16 attributeName,
	const AttributeValue& value)
{
// TODO: How is the address class handled?
	fAddressClass = value.constant;
	return B_OK;
}


// #pragma mark - DIEDeclaredType

/**
 * @brief Constructs a DIEDeclaredType.
 */
DIEDeclaredType::DIEDeclaredType()
	:
	fDescription(NULL),
	fAbstractOrigin(NULL),
	fSignatureType(NULL),
	fAccessibility(0),
	fDeclaration(false)
{
}


/**
 * @brief Returns the DIE's DW_AT_description string.
 */
const char*
DIEDeclaredType::Description() const
{
	return fDescription;
}


/**
 * @brief Returns the DW_AT_abstract_origin reference.
 */
DebugInfoEntry*
DIEDeclaredType::AbstractOrigin() const
{
	return fAbstractOrigin;
}


/**
 * @brief Returns the type unit referenced via DW_AT_signature.
 */
DebugInfoEntry*
DIEDeclaredType::SignatureType() const
{
	return fSignatureType;
}


/**
 * @brief Reports whether this DIE is a non-defining declaration.
 */
bool
DIEDeclaredType::IsDeclaration() const
{
	return fDeclaration;
}


/**
 * @brief Stores the DW_AT_accessibility attribute value.
 */
status_t
DIEDeclaredType::AddAttribute_accessibility(uint16 attributeName,
	const AttributeValue& value)
{
	fAccessibility = value.constant;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_declaration attribute value.
 */
status_t
DIEDeclaredType::AddAttribute_declaration(uint16 attributeName,
	const AttributeValue& value)
{
	fDeclaration = value.flag;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_description attribute value.
 */
status_t
DIEDeclaredType::AddAttribute_description(uint16 attributeName,
	const AttributeValue& value)
{
	fDescription = value.string;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_abstract_origin attribute value.
 */
status_t
DIEDeclaredType::AddAttribute_abstract_origin(uint16 attributeName,
	const AttributeValue& value)
{
	fAbstractOrigin = value.reference;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_signature attribute value.
 */
status_t
DIEDeclaredType::AddAttribute_signature(uint16 attributeName,
	const AttributeValue& value)
{
	fSignatureType = value.reference;
	return B_OK;
}


/**
 * @brief Returns a writable pointer to the DIE's declaration-location storage.
 */
DeclarationLocation*
DIEDeclaredType::GetDeclarationLocation()
{
	return &fDeclarationLocation;
}


// #pragma mark - DIEDerivedType

/**
 * @brief Constructs a DIEDerivedType.
 */
DIEDerivedType::DIEDerivedType()
	:
	fType(NULL)
{
}


/**
 * @brief Stores the DW_AT_type attribute value.
 */
status_t
DIEDerivedType::AddAttribute_type(uint16 attributeName,
	const AttributeValue& value)
{
	fType = dynamic_cast<DIEType*>(value.reference);
	return fType != NULL ? B_OK : B_BAD_DATA;
}




// #pragma mark - DIECompoundType

/**
 * @brief Constructs a DIECompoundType.
 */
DIECompoundType::DIECompoundType()
	:
	fSpecification(NULL)
{
}


/**
 * @brief Reports whether this DIE introduces a namespace-like scope.
 */
bool
DIECompoundType::IsNamespace() const
{
	return true;
}


/**
 * @brief Returns the DW_AT_specification reference.
 */
DebugInfoEntry*
DIECompoundType::Specification() const
{
	return fSpecification;
}


/**
 * @brief Returns the byte size of values of this type.
 */
const DynamicAttributeValue*
DIECompoundType::ByteSize() const
{
	return &fByteSize;
}


/**
 * @brief Adds a child DIE during parsing.
 */
status_t
DIECompoundType::AddChild(DebugInfoEntry* child)
{
	if (child->Tag() == DW_TAG_member) {
		// TODO: Not for interfaces!
		fDataMembers.Add(child);
		return B_OK;
	}

	return DIEDeclaredType::AddChild(child);
}


/**
 * @brief Stores the DW_AT_byte_size attribute value.
 */
status_t
DIECompoundType::AddAttribute_byte_size(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fByteSize, value);
}


/**
 * @brief Stores the DW_AT_specification attribute value.
 */
status_t
DIECompoundType::AddAttribute_specification(uint16 attributeName,
	const AttributeValue& value)
{
	fSpecification = dynamic_cast<DIECompoundType*>(value.reference);
	return fSpecification != NULL ? B_OK : B_BAD_DATA;
}


// #pragma mark - DIEClassBaseType

/**
 * @brief Constructs a DIEClassBaseType.
 */
DIEClassBaseType::DIEClassBaseType()
{
}


/**
 * @brief Adds a child DIE during parsing.
 */
status_t
DIEClassBaseType::AddChild(DebugInfoEntry* child)
{
	switch (child->Tag()) {
		case DW_TAG_inheritance:
			fBaseTypes.Add(child);
			return B_OK;
		case DW_TAG_friend:
			fFriends.Add(child);
			return B_OK;
		case DW_TAG_access_declaration:
			fAccessDeclarations.Add(child);
			return B_OK;
		case DW_TAG_subprogram:
			fMemberFunctions.Add(child);
			return B_OK;
		case DW_TAG_template_type_parameter:
		case DW_TAG_template_value_parameter:
			fTemplateParameters.Add(child);
			return B_OK;
// TODO: Variants!
		default:
		{
			if (child->IsType()) {
				fInnerTypes.Add(child);
				return B_OK;
			}

			return DIECompoundType::AddChild(child);
		}
	}
}


// #pragma mark - DIENamedBase

/**
 * @brief Constructs a DIENamedBase.
 */
DIENamedBase::DIENamedBase()
	:
	fName(NULL),
	fDescription(NULL)
{
}


/**
 * @brief Returns the DIE's name (or the DIE chain's name).
 */
const char*
DIENamedBase::Name() const
{
	return fName;
}


/**
 * @brief Returns the DIE's DW_AT_description string.
 */
const char*
DIENamedBase::Description() const
{
	return fDescription;
}


/**
 * @brief Stores the DW_AT_name attribute value.
 */
status_t
DIENamedBase::AddAttribute_name(uint16 attributeName,
	const AttributeValue& value)
{
	fName = value.string;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_description attribute value.
 */
status_t
DIENamedBase::AddAttribute_description(uint16 attributeName,
	const AttributeValue& value)
{
	fDescription = value.string;
	return B_OK;
}


// #pragma mark - DIEDeclaredBase

/**
 * @brief Constructs a DIEDeclaredBase.
 */
DIEDeclaredBase::DIEDeclaredBase()
{
}


/**
 * @brief Returns a writable pointer to the DIE's declaration-location storage.
 */
DeclarationLocation*
DIEDeclaredBase::GetDeclarationLocation()
{
	return &fDeclarationLocation;
}


// #pragma mark - DIEDeclaredNamedBase

/**
 * @brief Constructs a DIEDeclaredNamedBase.
 */
DIEDeclaredNamedBase::DIEDeclaredNamedBase()
	:
	fName(NULL),
	fDescription(NULL),
	fAccessibility(0),
	fVisibility(0),
	fDeclaration(false)
{
}


/**
 * @brief Returns the DIE's name (or the DIE chain's name).
 */
const char*
DIEDeclaredNamedBase::Name() const
{
	return fName;
}


/**
 * @brief Returns the DIE's DW_AT_description string.
 */
const char*
DIEDeclaredNamedBase::Description() const
{
	return fDescription;
}


/**
 * @brief Reports whether this DIE is a non-defining declaration.
 */
bool
DIEDeclaredNamedBase::IsDeclaration() const
{
	return fDeclaration;
}


/**
 * @brief Stores the DW_AT_name attribute value.
 */
status_t
DIEDeclaredNamedBase::AddAttribute_name(uint16 attributeName,
	const AttributeValue& value)
{
	fName = value.string;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_description attribute value.
 */
status_t
DIEDeclaredNamedBase::AddAttribute_description(uint16 attributeName,
	const AttributeValue& value)
{
	fDescription = value.string;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_accessibility attribute value.
 */
status_t
DIEDeclaredNamedBase::AddAttribute_accessibility(uint16 attributeName,
	const AttributeValue& value)
{
	fAccessibility = value.constant;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_declaration attribute value.
 */
status_t
DIEDeclaredNamedBase::AddAttribute_declaration(uint16 attributeName,
	const AttributeValue& value)
{
	fDeclaration = value.flag;
	return B_OK;
}


// #pragma mark - DIEArrayIndexType

/**
 * @brief Constructs a DIEArrayIndexType.
 */
DIEArrayIndexType::DIEArrayIndexType()
{
}


/**
 * @brief Returns the byte size of values of this type.
 */
const DynamicAttributeValue*
DIEArrayIndexType::ByteSize() const
{
	return &fByteSize;
}


/**
 * @brief Stores the DW_AT_bit_stride attribute value.
 */
status_t
DIEArrayIndexType::AddAttribute_bit_stride(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fBitStride, value);
}


/**
 * @brief Stores the DW_AT_byte_size attribute value.
 */
status_t
DIEArrayIndexType::AddAttribute_byte_size(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fByteSize, value);
}


/**
 * @brief Stores the DW_AT_byte_stride attribute value.
 */
status_t
DIEArrayIndexType::AddAttribute_byte_stride(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fByteStride, value);
}


// #pragma mark - DIEArrayType

/**
 * @brief Constructs a DIEArrayType.
 */
DIEArrayType::DIEArrayType()
	:
	fSpecification(NULL),
	fOrdering(DW_ORD_row_major)
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEArrayType::Tag() const
{
	return DW_TAG_array_type;
}


/**
 * @brief Post-parse hook invoked once parent/child links are set.
 */
status_t
DIEArrayType::InitAfterHierarchy(DebugInfoEntryInitInfo& info)
{
	fOrdering = info.languageInfo->arrayOrdering;
	return B_OK;
}


/**
 * @brief Returns the DW_AT_specification reference.
 */
DebugInfoEntry*
DIEArrayType::Specification() const
{
	return fSpecification;
}


/**
 * @brief Returns the byte size of values of this type.
 */
const DynamicAttributeValue*
DIEArrayType::ByteSize() const
{
	return &fByteSize;
}


/**
 * @brief Adds a child DIE during parsing.
 */
status_t
DIEArrayType::AddChild(DebugInfoEntry* child)
{
	// a dimension child must be of subrange or enumeration type
	uint16 tag = child->Tag();
	if (tag == DW_TAG_subrange_type || tag == DW_TAG_enumeration_type) {
		fDimensions.Add(child);
		return B_OK;
	}

	return DIEDerivedType::AddChild(child);
}


/**
 * @brief Stores the DW_AT_ordering attribute value.
 */
status_t
DIEArrayType::AddAttribute_ordering(uint16 attributeName,
	const AttributeValue& value)
{
	fOrdering = value.constant;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_bit_stride attribute value.
 */
status_t
DIEArrayType::AddAttribute_bit_stride(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fBitStride, value);
}


/**
 * @brief Stores the DW_AT_stride_size attribute value.
 */
status_t
DIEArrayType::AddAttribute_stride_size(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fBitStride, value);
}


/**
 * @brief Stores the DW_AT_byte_size attribute value.
 */
status_t
DIEArrayType::AddAttribute_byte_size(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fByteSize, value);
}


/**
 * @brief Stores the DW_AT_specification attribute value.
 */
status_t
DIEArrayType::AddAttribute_specification(uint16 attributeName,
	const AttributeValue& value)
{
	fSpecification = dynamic_cast<DIEArrayType*>(value.reference);
	return fSpecification != NULL ? B_OK : B_BAD_DATA;
}


// #pragma mark - DIEClassType

/**
 * @brief Constructs a DIEClassType.
 */
DIEClassType::DIEClassType()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEClassType::Tag() const
{
	return DW_TAG_class_type;
}


// #pragma mark - DIEEntryPoint

/**
 * @brief Constructs a DIEEntryPoint.
 */
DIEEntryPoint::DIEEntryPoint()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEEntryPoint::Tag() const
{
	return DW_TAG_entry_point;
}


// #pragma mark - DIEEnumerationType

/**
 * @brief Constructs a DIEEnumerationType.
 */
DIEEnumerationType::DIEEnumerationType()
	:
	fSpecification(NULL)
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEEnumerationType::Tag() const
{
	return DW_TAG_enumeration_type;
}


/**
 * @brief Returns the DW_AT_specification reference.
 */
DebugInfoEntry*
DIEEnumerationType::Specification() const
{
	return fSpecification;
}


/**
 * @brief Adds a child DIE during parsing.
 */
status_t
DIEEnumerationType::AddChild(DebugInfoEntry* child)
{
	if (child->Tag() == DW_TAG_enumerator) {
		fEnumerators.Add(child);
		return B_OK;
	}

	return DIEDerivedType::AddChild(child);
}


/**
 * @brief Stores the DW_AT_specification attribute value.
 */
status_t
DIEEnumerationType::AddAttribute_specification(uint16 attributeName,
	const AttributeValue& value)
{
	fSpecification = dynamic_cast<DIEEnumerationType*>(value.reference);
	return fSpecification != NULL ? B_OK : B_BAD_DATA;
}


// #pragma mark - DIEFormalParameter

/**
 * @brief Constructs a DIEFormalParameter.
 */
DIEFormalParameter::DIEFormalParameter()
	:
	fAbstractOrigin(NULL),
	fType(NULL),
	fArtificial(false)
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEFormalParameter::Tag() const
{
	return DW_TAG_formal_parameter;
}


/**
 * @brief Returns the DW_AT_abstract_origin reference.
 */
DebugInfoEntry*
DIEFormalParameter::AbstractOrigin() const
{
	return fAbstractOrigin;
}


/**
 * @brief Returns a writable pointer to the DIE's location description.
 */
LocationDescription*
DIEFormalParameter::GetLocationDescription()
{
	return &fLocationDescription;
}


/**
 * @brief Stores the DW_AT_abstract_origin attribute value.
 */
status_t
DIEFormalParameter::AddAttribute_abstract_origin(uint16 attributeName,
	const AttributeValue& value)
{
	fAbstractOrigin = value.reference;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_artificial attribute value.
 */
status_t
DIEFormalParameter::AddAttribute_artificial(uint16 attributeName,
	const AttributeValue& value)
{
	fArtificial = value.flag;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_const_value attribute value.
 */
status_t
DIEFormalParameter::AddAttribute_const_value(uint16 attributeName,
	const AttributeValue& value)
{
	return SetConstantAttributeValue(fValue, value);
}


/**
 * @brief Stores the DW_AT_type attribute value.
 */
status_t
DIEFormalParameter::AddAttribute_type(uint16 attributeName,
	const AttributeValue& value)
{
	fType = dynamic_cast<DIEType*>(value.reference);
	return fType != NULL ? B_OK : B_BAD_DATA;
}


// #pragma mark - DIEImportedDeclaration

/**
 * @brief Constructs a DIEImportedDeclaration.
 */
DIEImportedDeclaration::DIEImportedDeclaration()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEImportedDeclaration::Tag() const
{
	return DW_TAG_imported_declaration;
}


// #pragma mark - DIELabel

/**
 * @brief Constructs a DIELabel.
 */
DIELabel::DIELabel()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIELabel::Tag() const
{
	return DW_TAG_label;
}


// #pragma mark - DIELexicalBlock

/**
 * @brief Constructs a DIELexicalBlock.
 */
DIELexicalBlock::DIELexicalBlock()
	:
	fLowPC(0),
	fHighPC(0),
	fAddressRangesOffset(-1),
	fAbstractOrigin(NULL)
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIELexicalBlock::Tag() const
{
	return DW_TAG_lexical_block;
}


/**
 * @brief Returns the DW_AT_abstract_origin reference.
 */
DebugInfoEntry*
DIELexicalBlock::AbstractOrigin() const
{
	return fAbstractOrigin;
}


/**
 * @brief Adds a child DIE during parsing.
 */
status_t
DIELexicalBlock::AddChild(DebugInfoEntry* child)
{
	switch (child->Tag()) {
		case DW_TAG_variable:
			fVariables.Add(child);
			return B_OK;
		case DW_TAG_lexical_block:
			fBlocks.Add(child);
			return B_OK;
		default:
			return DIENamedBase::AddChild(child);
	}
}


/**
 * @brief Stores the DW_AT_low_pc attribute value.
 */
status_t
DIELexicalBlock::AddAttribute_low_pc(uint16 attributeName,
	const AttributeValue& value)
{
	fLowPC = value.address;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_high_pc attribute value.
 */
status_t
DIELexicalBlock::AddAttribute_high_pc(uint16 attributeName,
	const AttributeValue& value)
{
	fHighPC = value.address;
	if (fLowPC != 0 && fHighPC < fLowPC)
		fHighPC += fLowPC;

	return B_OK;
}


/**
 * @brief Stores the DW_AT_ranges attribute value.
 */
status_t
DIELexicalBlock::AddAttribute_ranges(uint16 attributeName,
	const AttributeValue& value)
{
	fAddressRangesOffset = value.pointer;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_abstract_origin attribute value.
 */
status_t
DIELexicalBlock::AddAttribute_abstract_origin(uint16 attributeName,
	const AttributeValue& value)
{
	fAbstractOrigin = dynamic_cast<DIELexicalBlock*>(value.reference);
	return fAbstractOrigin != NULL ? B_OK : B_BAD_DATA;
}


// #pragma mark - DIEMember

/**
 * @brief Constructs a DIEMember.
 */
DIEMember::DIEMember()
	:
	fType(NULL)
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEMember::Tag() const
{
	return DW_TAG_member;
}


/**
 * @brief Stores the DW_AT_type attribute value.
 */
status_t
DIEMember::AddAttribute_type(uint16 attributeName,
	const AttributeValue& value)
{
	fType = dynamic_cast<DIEType*>(value.reference);
	return fType != NULL ? B_OK : B_BAD_DATA;
}


/**
 * @brief Stores the DW_AT_byte_size attribute value.
 */
status_t
DIEMember::AddAttribute_byte_size(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fByteSize, value);
}


/**
 * @brief Stores the DW_AT_bit_size attribute value.
 */
status_t
DIEMember::AddAttribute_bit_size(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fBitSize, value);
}


/**
 * @brief Stores the DW_AT_data_member_location attribute value.
 */
status_t
DIEMember::AddAttribute_data_member_location(uint16 attributeName,
	const AttributeValue& value)
{
	return SetMemberLocation(fLocation, value);
}


/**
 * @brief Stores the DW_AT_bit_offset attribute value.
 */
status_t
DIEMember::AddAttribute_bit_offset(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fBitOffset, value);
}


/**
 * @brief Stores the DW_AT_data_bit_offset attribute value.
 */
status_t
DIEMember::AddAttribute_data_bit_offset(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fDataBitOffset, value);
}


// #pragma mark - DIEPointerType

/**
 * @brief Constructs a DIEPointerType.
 */
DIEPointerType::DIEPointerType()
	:
	fSpecification(NULL)
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEPointerType::Tag() const
{
	return DW_TAG_pointer_type;
}


/**
 * @brief Returns the DW_AT_specification reference.
 */
DebugInfoEntry*
DIEPointerType::Specification() const
{
	return fSpecification;
}


/**
 * @brief Stores the DW_AT_specification attribute value.
 */
status_t
DIEPointerType::AddAttribute_specification(uint16 attributeName,
	const AttributeValue& value)
{
	fSpecification = dynamic_cast<DIEPointerType*>(value.reference);
	return fSpecification != NULL ? B_OK : B_BAD_DATA;
}


// #pragma mark - DIEReferenceType

/**
 * @brief Constructs a DIEReferenceType.
 */
DIEReferenceType::DIEReferenceType()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEReferenceType::Tag() const
{
	return DW_TAG_reference_type;
}


// #pragma mark - DIECompileUnit

/**
 * @brief Constructs a DIECompileUnit.
 */
DIECompileUnit::DIECompileUnit()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIECompileUnit::Tag() const
{
	return DW_TAG_compile_unit;
}


// #pragma mark - DIEStringType

/**
 * @brief Constructs a DIEStringType.
 */
DIEStringType::DIEStringType()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEStringType::Tag() const
{
	return DW_TAG_string_type;
}


/**
 * @brief Returns the byte size of values of this type.
 */
const DynamicAttributeValue*
DIEStringType::ByteSize() const
{
	return &fByteSize;
}


/**
 * @brief Stores the DW_AT_byte_size attribute value.
 */
status_t
DIEStringType::AddAttribute_byte_size(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fByteSize, value);
}


// #pragma mark - DIEStructureType

/**
 * @brief Constructs a DIEStructureType.
 */
DIEStructureType::DIEStructureType()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEStructureType::Tag() const
{
	return DW_TAG_structure_type;
}


// #pragma mark - DIESubroutineType

/**
 * @brief Constructs a DIESubroutineType.
 */
DIESubroutineType::DIESubroutineType()
	:
	fReturnType(NULL),
	fAddressClass(0),
	fPrototyped(false)
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIESubroutineType::Tag() const
{
	return DW_TAG_subroutine_type;
}


/**
 * @brief Adds a child DIE during parsing.
 */
status_t
DIESubroutineType::AddChild(DebugInfoEntry* child)
{
	switch (child->Tag()) {
		case DW_TAG_formal_parameter:
		case DW_TAG_unspecified_parameters:
			fParameters.Add(child);
			return B_OK;
		default:
			return DIEDeclaredType::AddChild(child);
	}
}


/**
 * @brief Stores the DW_AT_address_class attribute value.
 */
status_t
DIESubroutineType::AddAttribute_address_class(uint16 attributeName,
	const AttributeValue& value)
{
// TODO: How is the address class handled?
	fAddressClass = value.constant;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_prototyped attribute value.
 */
status_t
DIESubroutineType::AddAttribute_prototyped(uint16 attributeName,
	const AttributeValue& value)
{
	fPrototyped = value.flag;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_type attribute value.
 */
status_t
DIESubroutineType::AddAttribute_type(uint16 attributeName,
	const AttributeValue& value)
{
	fReturnType = dynamic_cast<DIEType*>(value.reference);
	return fReturnType != NULL ? B_OK : B_BAD_DATA;
}


// #pragma mark - DIETypedef

/**
 * @brief Constructs a DIETypedef.
 */
DIETypedef::DIETypedef()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIETypedef::Tag() const
{
	return DW_TAG_typedef;
}


// #pragma mark - DIEUnionType

/**
 * @brief Constructs a DIEUnionType.
 */
DIEUnionType::DIEUnionType()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEUnionType::Tag() const
{
	return DW_TAG_union_type;
}


// #pragma mark - DIEUnspecifiedParameters

/**
 * @brief Constructs a DIEUnspecifiedParameters.
 */
DIEUnspecifiedParameters::DIEUnspecifiedParameters()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEUnspecifiedParameters::Tag() const
{
	return DW_TAG_unspecified_parameters;
}


// #pragma mark - DIEVariant

/**
 * @brief Constructs a DIEVariant.
 */
DIEVariant::DIEVariant()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEVariant::Tag() const
{
	return DW_TAG_variant;
}


// #pragma mark - DIECommonBlock

/**
 * @brief Constructs a DIECommonBlock.
 */
DIECommonBlock::DIECommonBlock()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIECommonBlock::Tag() const
{
	return DW_TAG_common_block;
}


/**
 * @brief Returns a writable pointer to the DIE's location description.
 */
LocationDescription*
DIECommonBlock::GetLocationDescription()
{
	return &fLocationDescription;
}


// #pragma mark - DIECommonInclusion

/**
 * @brief Constructs a DIECommonInclusion.
 */
DIECommonInclusion::DIECommonInclusion()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIECommonInclusion::Tag() const
{
	return DW_TAG_common_inclusion;
}


// #pragma mark - DIEInheritance

/**
 * @brief Constructs a DIEInheritance.
 */
DIEInheritance::DIEInheritance()
	:
	fType(NULL)
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEInheritance::Tag() const
{
	return DW_TAG_inheritance;
}


/**
 * @brief Stores the DW_AT_type attribute value.
 */
status_t
DIEInheritance::AddAttribute_type(uint16 attributeName,
	const AttributeValue& value)
{
	fType = dynamic_cast<DIEType*>(value.reference);
	return fType != NULL ? B_OK : B_BAD_DATA;
}


/**
 * @brief Stores the DW_AT_data_member_location attribute value.
 */
status_t
DIEInheritance::AddAttribute_data_member_location(uint16 attributeName,
	const AttributeValue& value)
{
	return SetMemberLocation(fLocation, value);
}


// #pragma mark - DIEInlinedSubroutine

/**
 * @brief Constructs a DIEInlinedSubroutine.
 */
DIEInlinedSubroutine::DIEInlinedSubroutine()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEInlinedSubroutine::Tag() const
{
	return DW_TAG_inlined_subroutine;
}


// #pragma mark - DIEModule

/**
 * @brief Constructs a DIEModule.
 */
DIEModule::DIEModule()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEModule::Tag() const
{
	return DW_TAG_module;
}


// #pragma mark - DIEPointerToMemberType

/**
 * @brief Constructs a DIEPointerToMemberType.
 */
DIEPointerToMemberType::DIEPointerToMemberType()
	:
	fContainingType(NULL),
	fAddressClass(0)
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEPointerToMemberType::Tag() const
{
	return DW_TAG_ptr_to_member_type;
}


/**
 * @brief Stores the DW_AT_address_class attribute value.
 */
status_t
DIEPointerToMemberType::AddAttribute_address_class(uint16 attributeName,
	const AttributeValue& value)
{
// TODO: How is the address class handled?
	fAddressClass = value.constant;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_containing_type attribute value.
 */
status_t
DIEPointerToMemberType::AddAttribute_containing_type(uint16 attributeName,
	const AttributeValue& value)
{
	DebugInfoEntry* type = value.reference;
	DIEModifiedType* modifiedType;
	while ((modifiedType = dynamic_cast<DIEModifiedType*>(type)) != NULL)
		type = modifiedType->GetType();

	fContainingType = dynamic_cast<DIECompoundType*>(type);
	return fContainingType != NULL ? B_OK : B_BAD_DATA;
}


/**
 * @brief Stores the DW_AT_use_location attribute value.
 */
status_t
DIEPointerToMemberType::AddAttribute_use_location(uint16 attributeName,
	const AttributeValue& value)
{
	if (value.attributeClass == ATTRIBUTE_CLASS_LOCLIST) {
		fUseLocation.SetToLocationList(value.pointer);
		return B_OK;
	}

	if (value.attributeClass == ATTRIBUTE_CLASS_BLOCK) {
		fUseLocation.SetToExpression(value.block.data, value.block.length);
		return B_OK;
	}

	return B_BAD_DATA;
}


// #pragma mark - DIESetType

/**
 * @brief Constructs a DIESetType.
 */
DIESetType::DIESetType()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIESetType::Tag() const
{
	return DW_TAG_set_type;
}


/**
 * @brief Returns the byte size of values of this type.
 */
const DynamicAttributeValue*
DIESetType::ByteSize() const
{
	return &fByteSize;
}


/**
 * @brief Stores the DW_AT_byte_size attribute value.
 */
status_t
DIESetType::AddAttribute_byte_size(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fByteSize, value);
}


// #pragma mark - DIESubrangeType

/**
 * @brief Constructs a DIESubrangeType.
 */
DIESubrangeType::DIESubrangeType()
	:
	fThreadsScaled(false)
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIESubrangeType::Tag() const
{
	return DW_TAG_subrange_type;
}


/**
 * @brief Stores the DW_AT_count attribute value.
 */
status_t
DIESubrangeType::AddAttribute_count(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fCount, value);
}


/**
 * @brief Stores the DW_AT_lower_bound attribute value.
 */
status_t
DIESubrangeType::AddAttribute_lower_bound(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fLowerBound, value);
}


/**
 * @brief Stores the DW_AT_upper_bound attribute value.
 */
status_t
DIESubrangeType::AddAttribute_upper_bound(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fUpperBound, value);
}


/**
 * @brief Stores the DW_AT_threads_scaled attribute value.
 */
status_t
DIESubrangeType::AddAttribute_threads_scaled(uint16 attributeName,
	const AttributeValue& value)
{
	fThreadsScaled = value.flag;
	return B_OK;
}


// #pragma mark - DIEWithStatement

/**
 * @brief Constructs a DIEWithStatement.
 */
DIEWithStatement::DIEWithStatement()
	:
	fType(NULL)
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEWithStatement::Tag() const
{
	return DW_TAG_with_stmt;
}


/**
 * @brief Returns a writable pointer to the DIE's location description.
 */
LocationDescription*
DIEWithStatement::GetLocationDescription()
{
	return &fLocationDescription;
}


/**
 * @brief Stores the DW_AT_type attribute value.
 */
status_t
DIEWithStatement::AddAttribute_type(uint16 attributeName,
	const AttributeValue& value)
{
	fType = dynamic_cast<DIEType*>(value.reference);
	return fType != NULL ? B_OK : B_BAD_DATA;
}


// #pragma mark - DIEAccessDeclaration

/**
 * @brief Constructs a DIEAccessDeclaration.
 */
DIEAccessDeclaration::DIEAccessDeclaration()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEAccessDeclaration::Tag() const
{
	return DW_TAG_access_declaration;
}


// #pragma mark - DIEBaseType

/**
 * @brief Constructs a DIEBaseType.
 */
DIEBaseType::DIEBaseType()
	:
	fEncoding(0),
	fEndianity(0)
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEBaseType::Tag() const
{
	return DW_TAG_base_type;
}


/**
 * @brief Returns the byte size of values of this type.
 */
const DynamicAttributeValue*
DIEBaseType::ByteSize() const
{
	return &fByteSize;
}


/**
 * @brief Stores the DW_AT_encoding attribute value.
 */
status_t
DIEBaseType::AddAttribute_encoding(uint16 attributeName,
	const AttributeValue& value)
{
	fEncoding = value.constant;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_byte_size attribute value.
 */
status_t
DIEBaseType::AddAttribute_byte_size(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fByteSize, value);
}


/**
 * @brief Stores the DW_AT_bit_size attribute value.
 */
status_t
DIEBaseType::AddAttribute_bit_size(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fBitSize, value);
}


/**
 * @brief Stores the DW_AT_bit_offset attribute value.
 */
status_t
DIEBaseType::AddAttribute_bit_offset(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fBitOffset, value);
}


/**
 * @brief Stores the DW_AT_data_bit_offset attribute value.
 */
status_t
DIEBaseType::AddAttribute_data_bit_offset(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fDataBitOffset, value);
}


/**
 * @brief Stores the DW_AT_endianity attribute value.
 */
status_t
DIEBaseType::AddAttribute_endianity(uint16 attributeName,
	const AttributeValue& value)
{
	fEndianity = value.constant;
	return B_OK;
}


// #pragma mark - DIECatchBlock

/**
 * @brief Constructs a DIECatchBlock.
 */
DIECatchBlock::DIECatchBlock()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIECatchBlock::Tag() const
{
	return DW_TAG_catch_block;
}


// #pragma mark - DIEConstType

/**
 * @brief Constructs a DIEConstType.
 */
DIEConstType::DIEConstType()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEConstType::Tag() const
{
	return DW_TAG_const_type;
}


// #pragma mark - DIEConstant

/**
 * @brief Constructs a DIEConstant.
 */
DIEConstant::DIEConstant()
	:
	fType(NULL)
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEConstant::Tag() const
{
	return DW_TAG_constant;
}


/**
 * @brief Stores the DW_AT_const_value attribute value.
 */
status_t
DIEConstant::AddAttribute_const_value(uint16 attributeName,
	const AttributeValue& value)
{
	return SetConstantAttributeValue(fValue, value);
}


/**
 * @brief Stores the DW_AT_type attribute value.
 */
status_t
DIEConstant::AddAttribute_type(uint16 attributeName,
	const AttributeValue& value)
{
	fType = dynamic_cast<DIEType*>(value.reference);
	return fType != NULL ? B_OK : B_BAD_DATA;
}


// #pragma mark - DIEEnumerator

/**
 * @brief Constructs a DIEEnumerator.
 */
DIEEnumerator::DIEEnumerator()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEEnumerator::Tag() const
{
	return DW_TAG_enumerator;
}


/**
 * @brief Stores the DW_AT_const_value attribute value.
 */
status_t
DIEEnumerator::AddAttribute_const_value(uint16 attributeName,
	const AttributeValue& value)
{
	return SetConstantAttributeValue(fValue, value);
}


// #pragma mark - DIEFileType

/**
 * @brief Constructs a DIEFileType.
 */
DIEFileType::DIEFileType()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEFileType::Tag() const
{
	return DW_TAG_file_type;
}


/**
 * @brief Returns the byte size of values of this type.
 */
const DynamicAttributeValue*
DIEFileType::ByteSize() const
{
	return &fByteSize;
}


/**
 * @brief Stores the DW_AT_byte_size attribute value.
 */
status_t
DIEFileType::AddAttribute_byte_size(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fByteSize, value);
}


// #pragma mark - DIEFriend

/**
 * @brief Constructs a DIEFriend.
 */
DIEFriend::DIEFriend()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEFriend::Tag() const
{
	return DW_TAG_friend;
}


// #pragma mark - DIENameList

/**
 * @brief Constructs a DIENameList.
 */
DIENameList::DIENameList()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIENameList::Tag() const
{
	return DW_TAG_namelist;
}


// #pragma mark - DIENameListItem

/**
 * @brief Constructs a DIENameListItem.
 */
DIENameListItem::DIENameListItem()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIENameListItem::Tag() const
{
	return DW_TAG_namelist_item;
}


// #pragma mark - DIENamespace

/**
 * @brief Constructs a DIENamespace.
 */
DIENamespace::DIENamespace()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIENamespace::Tag() const
{
	return DW_TAG_namespace;
}


/**
 * @brief Reports whether this DIE introduces a namespace-like scope.
 */
bool
DIENamespace::IsNamespace() const
{
	return true;
}


/**
 * @brief Adds a child DIE during parsing.
 */
status_t
DIENamespace::AddChild(DebugInfoEntry* child)
{
	fChildren.Add(child);
	return B_OK;
}


// #pragma mark - DIEPackedType

/**
 * @brief Constructs a DIEPackedType.
 */
DIEPackedType::DIEPackedType()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEPackedType::Tag() const
{
	return DW_TAG_packed_type;
}


// #pragma mark - DIESubprogram

/**
 * @brief Constructs a DIESubprogram.
 */
DIESubprogram::DIESubprogram()
	:
	fLowPC(0),
	fHighPC(0),
	fAddressRangesOffset(-1),
	fSpecification(NULL),
	fAbstractOrigin(NULL),
	fReturnType(NULL),
	fAddressClass(0),
	fPrototyped(false),
	fInline(DW_INL_not_inlined),
	fMain(false),
	fArtificial(false),
	fCallingConvention(DW_CC_normal)
{
}

/**
 * @brief Destroys the DIESubprogram.
 */
DIESubprogram::~DIESubprogram()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIESubprogram::Tag() const
{
	return DW_TAG_subprogram;
}


/**
 * @brief Returns the DW_AT_specification reference.
 */
DebugInfoEntry*
DIESubprogram::Specification() const
{
	return fSpecification;
}



/**
 * @brief Returns the DW_AT_abstract_origin reference.
 */
DebugInfoEntry*
DIESubprogram::AbstractOrigin() const
{
	return fAbstractOrigin;
}


/**
 * @brief Adds a child DIE during parsing.
 */
status_t
DIESubprogram::AddChild(DebugInfoEntry* child)
{
	switch (child->Tag()) {
		case DW_TAG_formal_parameter:
		case DW_TAG_unspecified_parameters:
			fParameters.Add(child);
			return B_OK;
		case DW_TAG_variable:
			fVariables.Add(child);
			return B_OK;
		case DW_TAG_lexical_block:
			fBlocks.Add(child);
			return B_OK;
		case DW_TAG_template_type_parameter:
			fTemplateTypeParameters.Add(child);
			return B_OK;
		case DW_TAG_template_value_parameter:
			fTemplateValueParameters.Add(child);
			return B_OK;
		case DW_TAG_call_site:
		case DW_TAG_GNU_call_site:
			fCallSites.Add(child);
			return B_OK;
		default:
			return DIENamespace::AddChild(child);
	}
}



/**
 * @brief Stores the DW_AT_low_pc attribute value.
 */
status_t
DIESubprogram::AddAttribute_low_pc(uint16 attributeName,
	const AttributeValue& value)
{
	fLowPC = value.address;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_high_pc attribute value.
 */
status_t
DIESubprogram::AddAttribute_high_pc(uint16 attributeName,
	const AttributeValue& value)
{
	fHighPC = value.address;
	if (fLowPC != 0 && fHighPC < fLowPC)
		fHighPC += fLowPC;

	return B_OK;
}


/**
 * @brief Stores the DW_AT_ranges attribute value.
 */
status_t
DIESubprogram::AddAttribute_ranges(uint16 attributeName,
	const AttributeValue& value)
{
	fAddressRangesOffset = value.pointer;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_specification attribute value.
 */
status_t
DIESubprogram::AddAttribute_specification(uint16 attributeName,
	const AttributeValue& value)
{
	fSpecification = dynamic_cast<DIESubprogram*>(value.reference);
	return fSpecification != NULL ? B_OK : B_BAD_DATA;
}


/**
 * @brief Stores the DW_AT_address_class attribute value.
 */
status_t
DIESubprogram::AddAttribute_address_class(uint16 attributeName,
	const AttributeValue& value)
{
// TODO: How is the address class handled?
	fAddressClass = value.constant;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_prototyped attribute value.
 */
status_t
DIESubprogram::AddAttribute_prototyped(uint16 attributeName,
	const AttributeValue& value)
{
	fPrototyped = value.flag;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_type attribute value.
 */
status_t
DIESubprogram::AddAttribute_type(uint16 attributeName,
	const AttributeValue& value)
{
	fReturnType = dynamic_cast<DIEType*>(value.reference);
	return fReturnType != NULL ? B_OK : B_BAD_DATA;
}


/**
 * @brief Stores the DW_AT_inline attribute value.
 */
status_t
DIESubprogram::AddAttribute_inline(uint16 attributeName,
	const AttributeValue& value)
{
// TODO: How is the address class handled?
	fInline = value.constant;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_abstract_origin attribute value.
 */
status_t
DIESubprogram::AddAttribute_abstract_origin(uint16 attributeName,
	const AttributeValue& value)
{
	fAbstractOrigin = dynamic_cast<DIESubprogram*>(value.reference);
	return fAbstractOrigin != NULL ? B_OK : B_BAD_DATA;
}


/**
 * @brief Stores the DW_AT_frame_base attribute value.
 */
status_t
DIESubprogram::AddAttribute_frame_base(uint16 attributeName,
	const AttributeValue& value)
{
	if (value.attributeClass == ATTRIBUTE_CLASS_LOCLIST) {
		fFrameBase.SetToLocationList(value.pointer);
		return B_OK;
	}

	if (value.attributeClass == ATTRIBUTE_CLASS_BLOCK) {
		fFrameBase.SetToExpression(value.block.data, value.block.length);
		return B_OK;
	}

	return B_BAD_DATA;
}


/**
 * @brief Stores the DW_AT_artificial attribute value.
 */
status_t
DIESubprogram::AddAttribute_artificial(uint16 attributeName,
	const AttributeValue& value)
{
	fArtificial = value.flag;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_calling_convention attribute value.
 */
status_t
DIESubprogram::AddAttribute_calling_convention(uint16 attributeName,
	const AttributeValue& value)
{
	fCallingConvention = value.constant;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_main_subprogram attribute value.
 */
status_t
DIESubprogram::AddAttribute_main_subprogram(uint16 attributeName,
	const AttributeValue& value)
{
	fMain = true;
	return B_OK;
}


// #pragma mark - DIETemplateTypeParameter

/**
 * @brief Constructs a DIETemplateTypeParameter.
 */
DIETemplateTypeParameter::DIETemplateTypeParameter()
	:
	fType(NULL)
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIETemplateTypeParameter::Tag() const
{
	return DW_TAG_template_type_parameter;
}


/**
 * @brief Stores the DW_AT_type attribute value.
 */
status_t
DIETemplateTypeParameter::AddAttribute_type(uint16 attributeName,
	const AttributeValue& value)
{
	fType = dynamic_cast<DIEType*>(value.reference);
	return fType != NULL ? B_OK : B_BAD_DATA;
}


// #pragma mark - DIETemplateValueParameter

/**
 * @brief Constructs a DIETemplateValueParameter.
 */
DIETemplateValueParameter::DIETemplateValueParameter()
	:
	fType(NULL)
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIETemplateValueParameter::Tag() const
{
	return DW_TAG_template_value_parameter;
}


/**
 * @brief Stores the DW_AT_const_value attribute value.
 */
status_t
DIETemplateValueParameter::AddAttribute_const_value(uint16 attributeName,
	const AttributeValue& value)
{
	return SetConstantAttributeValue(fValue, value);
}


/**
 * @brief Stores the DW_AT_type attribute value.
 */
status_t
DIETemplateValueParameter::AddAttribute_type(uint16 attributeName,
	const AttributeValue& value)
{
	fType = dynamic_cast<DIEType*>(value.reference);
	return fType != NULL ? B_OK : B_BAD_DATA;
}


// #pragma mark - DIEThrownType

/**
 * @brief Constructs a DIEThrownType.
 */
DIEThrownType::DIEThrownType()
	:
	fType(NULL)
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEThrownType::Tag() const
{
	return DW_TAG_thrown_type;
}


/**
 * @brief Stores the DW_AT_type attribute value.
 */
status_t
DIEThrownType::AddAttribute_type(uint16 attributeName,
	const AttributeValue& value)
{
	fType = dynamic_cast<DIEType*>(value.reference);
	return fType != NULL ? B_OK : B_BAD_DATA;
}


// #pragma mark - DIETryBlock

/**
 * @brief Constructs a DIETryBlock.
 */
DIETryBlock::DIETryBlock()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIETryBlock::Tag() const
{
	return DW_TAG_try_block;
}


// #pragma mark - DIEVariantPart

/**
 * @brief Constructs a DIEVariantPart.
 */
DIEVariantPart::DIEVariantPart()
	:
	fType(NULL)
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEVariantPart::Tag() const
{
	return DW_TAG_variant_part;
}


/**
 * @brief Stores the DW_AT_type attribute value.
 */
status_t
DIEVariantPart::AddAttribute_type(uint16 attributeName,
	const AttributeValue& value)
{
	fType = dynamic_cast<DIEType*>(value.reference);
	return fType != NULL ? B_OK : B_BAD_DATA;
}


// #pragma mark - DIEVariable

/**
 * @brief Constructs a DIEVariable.
 */
DIEVariable::DIEVariable()
	:
	fType(NULL),
	fSpecification(NULL),
	fAbstractOrigin(NULL),
	fStartScope(0)
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEVariable::Tag() const
{
	return DW_TAG_variable;
}


/**
 * @brief Returns the DW_AT_specification reference.
 */
DebugInfoEntry*
DIEVariable::Specification() const
{
	return fSpecification;
}



/**
 * @brief Returns the DW_AT_abstract_origin reference.
 */
DebugInfoEntry*
DIEVariable::AbstractOrigin() const
{
	return fAbstractOrigin;
}


/**
 * @brief Returns a writable pointer to the DIE's location description.
 */
LocationDescription*
DIEVariable::GetLocationDescription()
{
	return &fLocationDescription;
}


/**
 * @brief Stores the DW_AT_const_value attribute value.
 */
status_t
DIEVariable::AddAttribute_const_value(uint16 attributeName,
	const AttributeValue& value)
{
	return SetConstantAttributeValue(fValue, value);
}


/**
 * @brief Stores the DW_AT_type attribute value.
 */
status_t
DIEVariable::AddAttribute_type(uint16 attributeName,
	const AttributeValue& value)
{
	fType = dynamic_cast<DIEType*>(value.reference);
	return fType != NULL ? B_OK : B_BAD_DATA;
}


/**
 * @brief Stores the DW_AT_specification attribute value.
 */
status_t
DIEVariable::AddAttribute_specification(uint16 attributeName,
	const AttributeValue& value)
{
	fSpecification = dynamic_cast<DIEVariable*>(value.reference);
	// in the case of static variables declared within a compound type,
	// the specification may point to a member entry rather than
	// a variable entry
	if (fSpecification == NULL)
		fSpecification = dynamic_cast<DIEMember*>(value.reference);

	return fSpecification != NULL ? B_OK : B_BAD_DATA;
}


/**
 * @brief Stores the DW_AT_abstract_origin attribute value.
 */
status_t
DIEVariable::AddAttribute_abstract_origin(uint16 attributeName,
	const AttributeValue& value)
{
	fAbstractOrigin = dynamic_cast<DIEVariable*>(value.reference);
	return fAbstractOrigin != NULL ? B_OK : B_BAD_DATA;
}


/**
 * @brief Stores the DW_AT_start_scope attribute value.
 */
status_t
DIEVariable::AddAttribute_start_scope(uint16 attributeName,
	const AttributeValue& value)
{
	fStartScope = value.constant;
	return B_OK;
}


/**
 * @brief Stores the DW_AT_external attribute value.
 */
status_t
DIEVariable::AddAttribute_external(uint16 attributeName,
	const AttributeValue& value)
{
	fIsExternal = value.flag;
	return B_OK;
}


// #pragma mark - DIEVolatileType

/**
 * @brief Constructs a DIEVolatileType.
 */
DIEVolatileType::DIEVolatileType()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEVolatileType::Tag() const
{
	return DW_TAG_volatile_type;
}


/**
 * @brief Stores the DW_AT_decl_file attribute value.
 */
status_t
DIEVolatileType::AddAttribute_decl_file(uint16 attributeName,
	const AttributeValue& value)
{
	fDeclarationLocation.SetFile(value.constant);
	return B_OK;
}


/**
 * @brief Stores the DW_AT_decl_line attribute value.
 */
status_t
DIEVolatileType::AddAttribute_decl_line(uint16 attributeName,
	const AttributeValue& value)
{
	fDeclarationLocation.SetLine(value.constant);
	return B_OK;
}


/**
 * @brief Stores the DW_AT_decl_column attribute value.
 */
status_t
DIEVolatileType::AddAttribute_decl_column(uint16 attributeName,
	const AttributeValue& value)
{
	fDeclarationLocation.SetColumn(value.constant);
	return B_OK;
}


// #pragma mark - DIEDwarfProcedure

/**
 * @brief Constructs a DIEDwarfProcedure.
 */
DIEDwarfProcedure::DIEDwarfProcedure()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEDwarfProcedure::Tag() const
{
	return DW_TAG_dwarf_procedure;
}


/**
 * @brief Returns a writable pointer to the DIE's location description.
 */
LocationDescription*
DIEDwarfProcedure::GetLocationDescription()
{
	return &fLocationDescription;
}


// #pragma mark - DIERestrictType

/**
 * @brief Constructs a DIERestrictType.
 */
DIERestrictType::DIERestrictType()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIERestrictType::Tag() const
{
	return DW_TAG_restrict_type;
}


// #pragma mark - DIEInterfaceType

/**
 * @brief Constructs a DIEInterfaceType.
 */
DIEInterfaceType::DIEInterfaceType()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEInterfaceType::Tag() const
{
	return DW_TAG_interface_type;
}


// #pragma mark - DIEImportedModule

/**
 * @brief Constructs a DIEImportedModule.
 */
DIEImportedModule::DIEImportedModule()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEImportedModule::Tag() const
{
	return DW_TAG_imported_module;
}


// #pragma mark - DIEUnspecifiedType

/**
 * @brief Constructs a DIEUnspecifiedType.
 */
DIEUnspecifiedType::DIEUnspecifiedType()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEUnspecifiedType::Tag() const
{
	return DW_TAG_unspecified_type;
}


/**
 * @brief Stores the DW_AT_decl_file attribute value.
 */
status_t
DIEUnspecifiedType::AddAttribute_decl_file(uint16 attributeName,
	const AttributeValue& value)
{
	fDeclarationLocation.SetFile(value.constant);
	return B_OK;
}


/**
 * @brief Stores the DW_AT_decl_line attribute value.
 */
status_t
DIEUnspecifiedType::AddAttribute_decl_line(uint16 attributeName,
	const AttributeValue& value)
{
	fDeclarationLocation.SetLine(value.constant);
	return B_OK;
}


/**
 * @brief Stores the DW_AT_decl_column attribute value.
 */
status_t
DIEUnspecifiedType::AddAttribute_decl_column(uint16 attributeName,
	const AttributeValue& value)
{
	fDeclarationLocation.SetColumn(value.constant);
	return B_OK;
}


// #pragma mark - DIEPartialUnit

/**
 * @brief Constructs a DIEPartialUnit.
 */
DIEPartialUnit::DIEPartialUnit()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEPartialUnit::Tag() const
{
	return DW_TAG_partial_unit;
}


// #pragma mark - DIEImportedUnit

/**
 * @brief Constructs a DIEImportedUnit.
 */
DIEImportedUnit::DIEImportedUnit()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIEImportedUnit::Tag() const
{
	return DW_TAG_imported_unit;
}


// #pragma mark - DIECondition

/**
 * @brief Constructs a DIECondition.
 */
DIECondition::DIECondition()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIECondition::Tag() const
{
	return DW_TAG_condition;
}


// #pragma mark - DIESharedType

/**
 * @brief Constructs a DIESharedType.
 */
DIESharedType::DIESharedType()
{
	fBlockSize.SetTo(~(uint64)0);
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIESharedType::Tag() const
{
	return DW_TAG_shared_type;
}


/**
 * @brief Stores the DW_AT_count attribute value.
 */
status_t
DIESharedType::AddAttribute_count(uint16 attributeName,
	const AttributeValue& value)
{
	return SetDynamicAttributeValue(fBlockSize, value);
}


/**
 * @brief Stores the DW_AT_decl_file attribute value.
 */
status_t
DIESharedType::AddAttribute_decl_file(uint16 attributeName,
	const AttributeValue& value)
{
	fDeclarationLocation.SetFile(value.constant);
	return B_OK;
}


/**
 * @brief Stores the DW_AT_decl_line attribute value.
 */
status_t
DIESharedType::AddAttribute_decl_line(uint16 attributeName,
	const AttributeValue& value)
{
	fDeclarationLocation.SetLine(value.constant);
	return B_OK;
}


/**
 * @brief Stores the DW_AT_decl_column attribute value.
 */
status_t
DIESharedType::AddAttribute_decl_column(uint16 attributeName,
	const AttributeValue& value)
{
	fDeclarationLocation.SetColumn(value.constant);
	return B_OK;
}


// #pragma mark - DIETypeUnit

/**
 * @brief Constructs a DIETypeUnit.
 */
DIETypeUnit::DIETypeUnit()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIETypeUnit::Tag() const
{
	return DW_TAG_type_unit;
}


// #pragma mark - DIERValueReferenceType

/**
 * @brief Constructs a DIERValueReferenceType.
 */
DIERValueReferenceType::DIERValueReferenceType()
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIERValueReferenceType::Tag() const
{
	return DW_TAG_rvalue_reference_type;
}


// #pragma mark - DIETemplateTemplateParameter

/**
 * @brief Constructs a DIETemplateTemplateParameter.
 */
DIETemplateTemplateParameter::DIETemplateTemplateParameter()
	:
	fName(NULL)
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIETemplateTemplateParameter::Tag() const
{
	return DW_TAG_GNU_template_template_param;
}


/**
 * @brief Returns the DIE's name (or the DIE chain's name).
 */
const char*
DIETemplateTemplateParameter::Name() const
{
	return fName;
}


/**
 * @brief Stores the DW_AT_name attribute value.
 */
status_t
DIETemplateTemplateParameter::AddAttribute_name(uint16 attributeName,
	const AttributeValue& value)
{
	fName = value.string;
	return B_OK;
}


// #pragma mark - DIETemplateTypeParameterPack

/**
 * @brief Constructs a DIETemplateTypeParameterPack.
 */
DIETemplateTypeParameterPack::DIETemplateTypeParameterPack()
	:
	fName(NULL)
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIETemplateTypeParameterPack::Tag() const
{
	return DW_TAG_GNU_template_parameter_pack;
}


/**
 * @brief Returns the DIE's name (or the DIE chain's name).
 */
const char*
DIETemplateTypeParameterPack::Name() const
{
	return fName;
}


/**
 * @brief Stores the DW_AT_name attribute value.
 */
status_t
DIETemplateTypeParameterPack::AddAttribute_name(uint16 attributeName,
	const AttributeValue& value)
{
	fName = value.string;
	return B_OK;
}


/**
 * @brief Adds a child DIE during parsing.
 */
status_t
DIETemplateTypeParameterPack::AddChild(DebugInfoEntry* child)
{
	if (child->Tag() == DW_TAG_template_type_parameter) {
		fChildren.Add(child);
		return B_OK;
	}

	return DIEDeclaredBase::AddChild(child);
}


// #pragma mark - DIETemplateValueParameterPack

/**
 * @brief Constructs a DIETemplateValueParameterPack.
 */
DIETemplateValueParameterPack::DIETemplateValueParameterPack()
	:
	fName(NULL)
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIETemplateValueParameterPack::Tag() const
{
	return DW_TAG_GNU_formal_parameter_pack;
}


/**
 * @brief Returns the DIE's name (or the DIE chain's name).
 */
const char*
DIETemplateValueParameterPack::Name() const
{
	return fName;
}


/**
 * @brief Stores the DW_AT_name attribute value.
 */
status_t
DIETemplateValueParameterPack::AddAttribute_name(uint16 attributeName,
	const AttributeValue& value)
{
	fName = value.string;
	return B_OK;
}


/**
 * @brief Adds a child DIE during parsing.
 */
status_t
DIETemplateValueParameterPack::AddChild(DebugInfoEntry* child)
{
	if (child->Tag() == DW_TAG_formal_parameter) {
		fChildren.Add(child);
		return B_OK;
	}

	return DIEDeclaredBase::AddChild(child);
}


// #pragma mark - DIECallSite

/**
 * @brief Constructs a DIECallSite.
 */
DIECallSite::DIECallSite()
	:
	fName(NULL)
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIECallSite::Tag() const
{
	return DW_TAG_GNU_call_site;
}


/**
 * @brief Returns the DIE's name (or the DIE chain's name).
 */
const char*
DIECallSite::Name() const
{
	return fName;
}


/**
 * @brief Stores the DW_AT_name attribute value.
 */
status_t
DIECallSite::AddAttribute_name(uint16 attributeName,
	const AttributeValue& value)
{
	fName = value.string;
	return B_OK;
}


/**
 * @brief Adds a child DIE during parsing.
 */
status_t
DIECallSite::AddChild(DebugInfoEntry* child)
{
	if (child->Tag() == DW_TAG_GNU_call_site_parameter
		|| child->Tag() == DW_TAG_call_site_parameter) {
		fChildren.Add(child);
		return B_OK;
	}

	return DIEDeclaredBase::AddChild(child);
}


// #pragma mark - DIECallSiteParameter

/**
 * @brief Constructs a DIECallSiteParameter.
 */
DIECallSiteParameter::DIECallSiteParameter()
	:
	fName(NULL)
{
}


/**
 * @brief Returns the DW_TAG_* code for this DIE.
 */
uint16
DIECallSiteParameter::Tag() const
{
	return DW_TAG_GNU_call_site_parameter;
}


/**
 * @brief Returns the DIE's name (or the DIE chain's name).
 */
const char*
DIECallSiteParameter::Name() const
{
	return fName;
}


/**
 * @brief Stores the DW_AT_name attribute value.
 */
status_t
DIECallSiteParameter::AddAttribute_name(uint16 attributeName,
	const AttributeValue& value)
{
	fName = value.string;
	return B_OK;
}


/**
 * @brief Adds a child DIE during parsing.
 */
status_t
DIECallSiteParameter::AddChild(DebugInfoEntry* child)
{
	return DIEDeclaredBase::AddChild(child);
}


// #pragma mark - DebugInfoEntryFactory

/**
 * @brief Constructs a DebugInfoEntryFactory.
 */
DebugInfoEntryFactory::DebugInfoEntryFactory()
{
}


/**
 * @brief Instantiates a concrete DIE for the given DW_TAG_* code.
 */
status_t
DebugInfoEntryFactory::CreateDebugInfoEntry(uint16 tag, DebugInfoEntry*& _entry)
{
	DebugInfoEntry* entry = NULL;

	switch (tag) {
		case DW_TAG_array_type:
			entry = new(std::nothrow) DIEArrayType;
			break;
		case DW_TAG_class_type:
			entry = new(std::nothrow) DIEClassType;
			break;
		case DW_TAG_entry_point:
			entry = new(std::nothrow) DIEEntryPoint;
			break;
		case DW_TAG_enumeration_type:
			entry = new(std::nothrow) DIEEnumerationType;
			break;
		case DW_TAG_formal_parameter:
			entry = new(std::nothrow) DIEFormalParameter;
			break;
		case DW_TAG_imported_declaration:
			entry = new(std::nothrow) DIEImportedDeclaration;
			break;
		case DW_TAG_label:
			entry = new(std::nothrow) DIELabel;
			break;
		case DW_TAG_lexical_block:
			entry = new(std::nothrow) DIELexicalBlock;
			break;
		case DW_TAG_member:
			entry = new(std::nothrow) DIEMember;
			break;
		case DW_TAG_pointer_type:
			entry = new(std::nothrow) DIEPointerType;
			break;
		case DW_TAG_reference_type:
			entry = new(std::nothrow) DIEReferenceType;
			break;
		case DW_TAG_compile_unit:
			entry = new(std::nothrow) DIECompileUnit;
			break;
		case DW_TAG_string_type:
			entry = new(std::nothrow) DIEStringType;
			break;
		case DW_TAG_structure_type:
			entry = new(std::nothrow) DIEStructureType;
			break;
		case DW_TAG_subroutine_type:
			entry = new(std::nothrow) DIESubroutineType;
			break;
		case DW_TAG_typedef:
			entry = new(std::nothrow) DIETypedef;
			break;
		case DW_TAG_union_type:
			entry = new(std::nothrow) DIEUnionType;
			break;
		case DW_TAG_unspecified_parameters:
			entry = new(std::nothrow) DIEUnspecifiedParameters;
			break;
		case DW_TAG_variant:
			entry = new(std::nothrow) DIEVariant;
			break;
		case DW_TAG_common_block:
			entry = new(std::nothrow) DIECommonBlock;
			break;
		case DW_TAG_common_inclusion:
			entry = new(std::nothrow) DIECommonInclusion;
			break;
		case DW_TAG_inheritance:
			entry = new(std::nothrow) DIEInheritance;
			break;
		case DW_TAG_inlined_subroutine:
			entry = new(std::nothrow) DIEInlinedSubroutine;
			break;
		case DW_TAG_module:
			entry = new(std::nothrow) DIEModule;
			break;
		case DW_TAG_ptr_to_member_type:
			entry = new(std::nothrow) DIEPointerToMemberType;
			break;
		case DW_TAG_set_type:
			entry = new(std::nothrow) DIESetType;
			break;
		case DW_TAG_subrange_type:
			entry = new(std::nothrow) DIESubrangeType;
			break;
		case DW_TAG_with_stmt:
			entry = new(std::nothrow) DIEWithStatement;
			break;
		case DW_TAG_access_declaration:
			entry = new(std::nothrow) DIEAccessDeclaration;
			break;
		case DW_TAG_base_type:
			entry = new(std::nothrow) DIEBaseType;
			break;
		case DW_TAG_catch_block:
			entry = new(std::nothrow) DIECatchBlock;
			break;
		case DW_TAG_const_type:
			entry = new(std::nothrow) DIEConstType;
			break;
		case DW_TAG_constant:
			entry = new(std::nothrow) DIEConstant;
			break;
		case DW_TAG_enumerator:
			entry = new(std::nothrow) DIEEnumerator;
			break;
		case DW_TAG_file_type:
			entry = new(std::nothrow) DIEFileType;
			break;
		case DW_TAG_friend:
			entry = new(std::nothrow) DIEFriend;
			break;
		case DW_TAG_namelist:
			entry = new(std::nothrow) DIENameList;
			break;
		case DW_TAG_namelist_item:
			entry = new(std::nothrow) DIENameListItem;
			break;
		case DW_TAG_packed_type:
			entry = new(std::nothrow) DIEPackedType;
			break;
		case DW_TAG_subprogram:
			entry = new(std::nothrow) DIESubprogram;
			break;
		case DW_TAG_template_type_parameter:
			entry = new(std::nothrow) DIETemplateTypeParameter;
			break;
		case DW_TAG_template_value_parameter:
			entry = new(std::nothrow) DIETemplateValueParameter;
			break;
		case DW_TAG_thrown_type:
			entry = new(std::nothrow) DIEThrownType;
			break;
		case DW_TAG_try_block:
			entry = new(std::nothrow) DIETryBlock;
			break;
		case DW_TAG_variant_part:
			entry = new(std::nothrow) DIEVariantPart;
			break;
		case DW_TAG_variable:
			entry = new(std::nothrow) DIEVariable;
			break;
		case DW_TAG_volatile_type:
			entry = new(std::nothrow) DIEVolatileType;
			break;
		case DW_TAG_dwarf_procedure:
			entry = new(std::nothrow) DIEDwarfProcedure;
			break;
		case DW_TAG_restrict_type:
			entry = new(std::nothrow) DIERestrictType;
			break;
		case DW_TAG_interface_type:
			entry = new(std::nothrow) DIEInterfaceType;
			break;
		case DW_TAG_namespace:
			entry = new(std::nothrow) DIENamespace;
			break;
		case DW_TAG_imported_module:
			entry = new(std::nothrow) DIEImportedModule;
			break;
		case DW_TAG_unspecified_type:
			entry = new(std::nothrow) DIEUnspecifiedType;
			break;
		case DW_TAG_partial_unit:
			entry = new(std::nothrow) DIEPartialUnit;
			break;
		case DW_TAG_imported_unit:
			entry = new(std::nothrow) DIEImportedUnit;
			break;
		case DW_TAG_condition:
			entry = new(std::nothrow) DIECondition;
			break;
		case DW_TAG_shared_type:
			entry = new(std::nothrow) DIESharedType;
			break;
		case DW_TAG_type_unit:
			entry = new(std::nothrow) DIETypeUnit;
			break;
		case DW_TAG_rvalue_reference_type:
			entry = new(std::nothrow) DIERValueReferenceType;
			break;
		case DW_TAG_GNU_template_template_param:
			entry = new(std::nothrow) DIETemplateTemplateParameter;
			break;
		case DW_TAG_GNU_template_parameter_pack:
			entry = new(std::nothrow) DIETemplateTypeParameterPack;
			break;
		case DW_TAG_GNU_formal_parameter_pack:
			entry = new(std::nothrow) DIETemplateValueParameterPack;
			break;
		case DW_TAG_call_site:
		case DW_TAG_GNU_call_site:
			entry = new(std::nothrow) DIECallSite;
			break;
		case DW_TAG_call_site_parameter:
		case DW_TAG_GNU_call_site_parameter:
			entry = new(std::nothrow) DIECallSiteParameter;
			break;
		default:
			return B_ENTRY_NOT_FOUND;
			break;
	}

	_entry = entry;
	return B_OK;
}
