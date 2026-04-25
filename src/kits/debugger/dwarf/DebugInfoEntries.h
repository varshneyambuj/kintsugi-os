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
 * MIT License. Copyright 2009, Ingo Weinhold; Copyright 2013-2018, Rene
 * Gollent.
 */

/** @file DebugInfoEntries.h
    @brief Concrete DIE class hierarchy mirroring every DWARF DW_TAG_* tag. */

#ifndef DEBUG_INFO_ENTRIES_H
#define DEBUG_INFO_ENTRIES_H


#include "DebugInfoEntry.h"

#include "AttributeValue.h"


// common:
// DW_AT_name
// // not supported by all types:
// DW_AT_allocated
// DW_AT_associated
// DW_AT_data_location
// DW_AT_start_scope

// modified:
// DW_AT_type

// declared:
// DECL
// DW_AT_accessibility		// !file, !pointer to member
// DW_AT_declaration		// !file
// DW_AT_abstract_origin	// !interface
// DW_AT_description		// !interface
// DW_AT_visibility			// !interface

// derived: declared
// DW_AT_type

// addressing: modified
// DW_AT_address_class

// compound: declared
// DW_AT_byte_size			// !interface
// DW_AT_specification		// !interface

// class base: compound

// array index: derived
// DW_AT_bit_stride
// DW_AT_byte_stride
// DW_AT_byte_size


// unspecified: common
// DECL
// DW_AT_description



// class/structure: class base

// interface: class base

// union: compound

// string: declared
// DW_AT_byte_size
// DW_AT_string_length

// subroutine: declared
// DW_AT_address_class
// DW_AT_prototyped
// DW_AT_type


// enumeration: array index
// DW_AT_specification

// pointer to member: derived
// DW_AT_address_class
// DW_AT_containing_type
// DW_AT_use_location

// set: derived
// DW_AT_byte_size

// subrange: array index
// DW_AT_count
// DW_AT_lower_bound
// DW_AT_threads_scaled
// DW_AT_upper_bound

// array: derived
// DW_AT_bit_stride
// DW_AT_byte_size
// DW_AT_ordering
// DW_AT_specification

// typedef: derived

// file: derived
// DW_AT_byte_size


// shared: modified
// DECL
// DW_AT_count

// const: modified

// packed: modified

// volatile: modified
// DECL

// restrict: modified

// pointer: addressing
// DW_AT_specification

// reference: addressing


// basetype:
// DW_AT_binary_scale
// DW_AT_bit_offset
// DW_AT_bit_size
// DW_AT_byte_size
// DW_AT_decimal_scale
// DW_AT_decimal_sign
// DW_AT_description
// DW_AT_digit_count
// DW_AT_encoding
// DW_AT_endianity
// DW_AT_picture_string
// DW_AT_small


/** @brief Common base for DW_TAG_compile_unit, DW_TAG_partial_unit, DW_TAG_type_unit. */
class DIECompileUnitBase : public DebugInfoEntry {
public:
								DIECompileUnitBase();
								~DIECompileUnitBase();

	virtual	status_t			InitAfterAttributes(
									DebugInfoEntryInitInfo& info);

	virtual	const char*			Name() const;

			const char*			CompilationDir() const
									{ return fCompilationDir; }

			uint16				Language() const	{ return fLanguage; }

			const DebugInfoEntryList& Types() const	{ return fTypes; }
			const DebugInfoEntryList& OtherChildren() const
										{ return fOtherChildren; }
			off_t				AddressRangesOffset() const
										{ return fAddressRangesOffset; }

			target_addr_t		LowPC() const	{ return fLowPC; }
			target_addr_t		HighPC() const	{ return fHighPC; }

			off_t				StatementListOffset() const
									{ return fStatementListOffset; }

			bool				ContainsMainSubprogram() const
									{ return fContainsMainSubprogram; }

	virtual	status_t			AddChild(DebugInfoEntry* child);

	virtual	status_t			AddAttribute_name(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_comp_dir(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_low_pc(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_high_pc(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_producer(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_stmt_list(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_macro_info(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_base_types(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_language(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_identifier_case(
									uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_use_UTF8(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_ranges(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_main_subprogram(
									uint16 attributeName,
									const AttributeValue& value);

//TODO:
//	virtual	status_t			AddAttribute_segment(uint16 attributeName,
//									const AttributeValue& value);

// TODO: DW_AT_import

protected:
			const char*			fName;
			const char*			fCompilationDir;
			target_addr_t		fLowPC;
			target_addr_t		fHighPC;
			off_t				fStatementListOffset;
			off_t				fMacroInfoOffset;
			off_t				fAddressRangesOffset;
			DIECompileUnitBase*	fBaseTypesUnit;
			DebugInfoEntryList	fTypes;
			DebugInfoEntryList	fOtherChildren;
			uint16				fLanguage;
			uint8				fIdentifierCase;
			bool				fUseUTF8;
			bool				fContainsMainSubprogram;
};


/** @brief Abstract base for every DIE that represents a type. */
class DIEType : public DebugInfoEntry {
public:
								DIEType();

	virtual	bool				IsType() const;

	virtual	const char*			Name() const;

	virtual	bool				IsDeclaration() const;
	virtual	const DynamicAttributeValue* ByteSize() const;

	virtual	status_t			AddAttribute_name(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_allocated(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_associated(uint16 attributeName,
									const AttributeValue& value);

// TODO:
// DW_AT_data_location
// DW_AT_start_scope

protected:
			const char*			fName;
			DynamicAttributeValue fAllocated;
			DynamicAttributeValue fAssociated;
};


/** @brief Type DIE that wraps another type with a modifier (const, volatile, ...). */
class DIEModifiedType : public DIEType {
public:
								DIEModifiedType();

			DIEType*			GetType() const	{ return fType; }

	virtual	status_t			AddAttribute_type(uint16 attributeName,
									const AttributeValue& value);

protected:
			DIEType*			fType;
};


/** @brief Modified type that also carries a DW_AT_address_class. */
class DIEAddressingType : public DIEModifiedType {
public:
								DIEAddressingType();

	virtual	status_t			AddAttribute_address_class(uint16 attributeName,
									const AttributeValue& value);

protected:
			uint8				fAddressClass;
};


/** @brief Type DIE with an explicit DW_AT_name and access/visibility/declaration attrs. */
class DIEDeclaredType : public DIEType {
public:
								DIEDeclaredType();

	virtual	const char*			Description() const;
	virtual	DebugInfoEntry*		AbstractOrigin() const;

	virtual DebugInfoEntry*		SignatureType() const;

	virtual	bool				IsDeclaration() const;

	virtual	status_t			AddAttribute_accessibility(uint16 attributeName,
									const AttributeValue& value);
										// TODO: !file, !pointer to member
	virtual	status_t			AddAttribute_declaration(uint16 attributeName,
									const AttributeValue& value);
										// TODO: !file
	virtual	status_t			AddAttribute_description(uint16 attributeName,
									const AttributeValue& value);
										// TODO: !interface
	virtual	status_t			AddAttribute_abstract_origin(
									uint16 attributeName,
									const AttributeValue& value);
										// TODO: !interface
	virtual	status_t			AddAttribute_signature(uint16 attributeName,
									const AttributeValue& value);

// TODO:
// DW_AT_visibility			// !interface

protected:
	virtual	DeclarationLocation* GetDeclarationLocation();

protected:
			const char*			fDescription;
			DeclarationLocation	fDeclarationLocation;
			DebugInfoEntry*		fAbstractOrigin;
			DebugInfoEntry*		fSignatureType;
			uint8				fAccessibility;
			bool				fDeclaration;
};


/** @brief Declared type derived from an underlying DW_AT_type referent. */
class DIEDerivedType : public DIEDeclaredType {
public:
								DIEDerivedType();

			DIEType*			GetType() const	{ return fType; }

	virtual	status_t			AddAttribute_type(uint16 attributeName,
									const AttributeValue& value);

protected:
			DIEType*			fType;
};


/** @brief Declared type with a DW_AT_byte_size and a list of member DIEs. */
class DIECompoundType : public DIEDeclaredType {
public:
								DIECompoundType();

	virtual	bool				IsNamespace() const;

	virtual	DebugInfoEntry*		Specification() const;

	virtual	const DynamicAttributeValue* ByteSize() const;

			const DebugInfoEntryList& DataMembers() const
									{ return fDataMembers; }

	virtual	status_t			AddChild(DebugInfoEntry* child);

	virtual	status_t			AddAttribute_byte_size(uint16 attributeName,
									const AttributeValue& value);
										// TODO: !interface
	virtual	status_t			AddAttribute_specification(uint16 attributeName,
									const AttributeValue& value);
										// TODO: !interface

protected:
			DynamicAttributeValue fByteSize;
			DIECompoundType*	fSpecification;
			DebugInfoEntryList	fDataMembers;
};


/** @brief Base for class/struct/union types adding inheritance and friend lists. */
class DIEClassBaseType : public DIECompoundType {
public:
								DIEClassBaseType();

			const DebugInfoEntryList& BaseTypes() const
									{ return fBaseTypes; }
			const DebugInfoEntryList& MemberFunctions() const
									{ return fMemberFunctions; }
			const DebugInfoEntryList& InnerTypes() const
									{ return fInnerTypes; }
			const DebugInfoEntryList& TemplateParameters() const
									{ return fTemplateParameters; }

	virtual	status_t			AddChild(DebugInfoEntry* child);

protected:
			DebugInfoEntryList	fBaseTypes;
			DebugInfoEntryList	fFriends;
			DebugInfoEntryList	fAccessDeclarations;
			DebugInfoEntryList	fMemberFunctions;
			DebugInfoEntryList	fInnerTypes;
			DebugInfoEntryList	fTemplateParameters;
};


/** @brief DIE base that only contributes a DW_AT_name. */
class DIENamedBase : public DebugInfoEntry {
public:
								DIENamedBase();

	virtual	const char*			Name() const;
	virtual	const char*			Description() const;

	virtual	status_t			AddAttribute_name(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_description(uint16 attributeName,
									const AttributeValue& value);

protected:
			const char*			fName;
			const char*			fDescription;
};


/** @brief DIE base contributing decl-file/line/column plus access/visibility flags. */
class DIEDeclaredBase : public DebugInfoEntry {
public:
								DIEDeclaredBase();

protected:
	virtual	DeclarationLocation* GetDeclarationLocation();

protected:
			DeclarationLocation	fDeclarationLocation;
};


/** @brief DIE base combining @ref DIEDeclaredBase with a DW_AT_name. */
class DIEDeclaredNamedBase : public DIEDeclaredBase {
public:
								DIEDeclaredNamedBase();

	virtual	const char*			Name() const;
	virtual	const char*			Description() const;

			uint8				Accessibility() const { return fAccessibility; }
			uint8				Visibility() const	{ return fVisibility; }
	virtual	bool				IsDeclaration() const;

	virtual	status_t			AddAttribute_name(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_description(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_accessibility(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_declaration(uint16 attributeName,
									const AttributeValue& value);

protected:
			const char*			fName;
			const char*			fDescription;
			uint8				fAccessibility;
			uint8				fVisibility;
			bool				fDeclaration;
};


/** @brief Index/range type used by array DIEs (DW_TAG_subrange_type, DW_TAG_enumeration_type). */
class DIEArrayIndexType : public DIEDerivedType {
public:
								DIEArrayIndexType();

	virtual	const DynamicAttributeValue* ByteSize() const;

			const DynamicAttributeValue* BitStride() const
									{ return &fBitStride; }
			const DynamicAttributeValue* ByteStride() const
									{ return &fByteStride; }

	virtual	status_t			AddAttribute_bit_stride(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_byte_size(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_byte_stride(uint16 attributeName,
									const AttributeValue& value);

private:
			DynamicAttributeValue fBitStride;
			DynamicAttributeValue fByteSize;
			DynamicAttributeValue fByteStride;
};


// #pragma mark -


/** @brief DW_TAG_array_type DIE describing a multi-dimensional array. */
class DIEArrayType : public DIEDerivedType {
public:
								DIEArrayType();

	virtual	uint16				Tag() const;

	virtual	status_t			InitAfterHierarchy(
									DebugInfoEntryInitInfo& info);

	virtual	DebugInfoEntry*		Specification() const;

	virtual	const DynamicAttributeValue* ByteSize() const;

			const DynamicAttributeValue* BitStride() const
									{ return &fBitStride; }

			const DebugInfoEntryList& Dimensions() const
									{ return fDimensions; }

	virtual	status_t			AddChild(DebugInfoEntry* child);

	virtual	status_t			AddAttribute_ordering(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_bit_stride(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_stride_size(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_byte_size(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_specification(uint16 attributeName,
									const AttributeValue& value);

private:
			DynamicAttributeValue fBitStride;
			DynamicAttributeValue fByteSize;
			DebugInfoEntryList	fDimensions;
			DIEArrayType*		fSpecification;
			uint8				fOrdering;
};


/** @brief DW_TAG_class_type DIE (C++ class). */
class DIEClassType : public DIEClassBaseType {
public:
								DIEClassType();

	virtual	uint16				Tag() const;
};


/** @brief DW_TAG_entry_point DIE (alternate program entry point). */
class DIEEntryPoint : public DebugInfoEntry {
public:
// TODO: Maybe introduce a common base class for DIEEntryPoint and
// DIESubprogram.
								DIEEntryPoint();

	virtual	uint16				Tag() const;

// TODO:
// DW_AT_address_class
// DW_AT_description
// DW_AT_frame_base
// DW_AT_low_pc
// DW_AT_name
// DW_AT_return_addr
// DW_AT_segment
// DW_AT_static_link
// DW_AT_type
};


/** @brief DW_TAG_enumeration_type DIE (C/C++ enum). */
class DIEEnumerationType : public DIEArrayIndexType {
public:
								DIEEnumerationType();

	virtual	uint16				Tag() const;

	virtual	DebugInfoEntry*		Specification() const;

			const DebugInfoEntryList& Enumerators() const
									{ return fEnumerators; }

	virtual	status_t			AddChild(DebugInfoEntry* child);

	virtual	status_t			AddAttribute_specification(uint16 attributeName,
									const AttributeValue& value);

private:
			DIEEnumerationType*	fSpecification;
			DebugInfoEntryList	fEnumerators;
};


/** @brief DW_TAG_formal_parameter DIE (function/method parameter). */
class DIEFormalParameter : public DIEDeclaredNamedBase {
public:
								DIEFormalParameter();

	virtual	uint16				Tag() const;

	virtual	DebugInfoEntry*		AbstractOrigin() const;
	virtual	LocationDescription* GetLocationDescription();

			DIEType*			GetType() const	{ return fType; }

			const ConstantAttributeValue* ConstValue() const
									{ return &fValue; }

			bool				IsArtificial() const { return fArtificial; }

	virtual	status_t			AddAttribute_abstract_origin(
									uint16 attributeName,
									const AttributeValue& value);
	virtual status_t			AddAttribute_artificial(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_const_value(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_type(uint16 attributeName,
									const AttributeValue& value);

// TODO:
// DW_AT_default_value
// DW_AT_endianity
// DW_AT_is_optional
// DW_AT_segment
// DW_AT_variable_parameter

private:
			LocationDescription	fLocationDescription;
			DebugInfoEntry*		fAbstractOrigin;
			DIEType*			fType;
			ConstantAttributeValue fValue;
			bool				fArtificial;
};


/** @brief DW_TAG_imported_declaration DIE (using-directive / using-declaration). */
class DIEImportedDeclaration : public DIEDeclaredNamedBase {
public:
								DIEImportedDeclaration();

	virtual	uint16				Tag() const;

// TODO:
// DW_AT_import
// DW_AT_start_scope
};


/** @brief DW_TAG_label DIE (named code label, e.g. goto target). */
class DIELabel : public DIEDeclaredNamedBase {
public:
								DIELabel();

	virtual	uint16				Tag() const;

// TODO:
// DW_AT_abstract_origin
// DW_AT_low_pc
// DW_AT_segment
// DW_AT_start_scope
};


/** @brief DW_TAG_lexical_block DIE (nested scope inside a subprogram). */
class DIELexicalBlock : public DIENamedBase {
public:
								DIELexicalBlock();

	virtual	uint16				Tag() const;

	virtual	DebugInfoEntry*		AbstractOrigin() const;

			off_t				AddressRangesOffset() const
										{ return fAddressRangesOffset; }

			target_addr_t		LowPC() const	{ return fLowPC; }
			target_addr_t		HighPC() const	{ return fHighPC; }

			const DebugInfoEntryList& Variables() const	{ return fVariables; }
			const DebugInfoEntryList& Blocks() const	{ return fBlocks; }

	virtual	status_t			AddChild(DebugInfoEntry* child);

	virtual	status_t			AddAttribute_low_pc(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_high_pc(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_ranges(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_abstract_origin(
									uint16 attributeName,
									const AttributeValue& value);

protected:
			DebugInfoEntryList	fVariables;
			DebugInfoEntryList	fBlocks;
			target_addr_t		fLowPC;
			target_addr_t		fHighPC;
			off_t				fAddressRangesOffset;
			DIELexicalBlock*	fAbstractOrigin;

// TODO:
// DW_AT_segment
};


/** @brief DW_TAG_member DIE (data member of a class/struct/union). */
class DIEMember : public DIEDeclaredNamedBase {
public:
								DIEMember();

	virtual	uint16				Tag() const;

			DIEType*			GetType() const	{ return fType; }
			const DynamicAttributeValue* ByteSize() const
									{ return &fByteSize; }
			const DynamicAttributeValue* BitOffset() const
									{ return &fBitOffset; }
			const DynamicAttributeValue* DataBitOffset() const
									{ return &fDataBitOffset; }
			const DynamicAttributeValue* BitSize() const
									{ return &fBitSize; }
			const MemberLocation* Location() const
									{ return &fLocation; }

	virtual	status_t			AddAttribute_type(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_byte_size(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_bit_size(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_bit_offset(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_data_bit_offset(
									uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_data_member_location(
									uint16 attributeName,
									const AttributeValue& value);

// TODO:
// DW_AT_mutable

private:
			DIEType*			fType;
			DynamicAttributeValue fByteSize;
			DynamicAttributeValue fBitOffset;
			DynamicAttributeValue fDataBitOffset;
			DynamicAttributeValue fBitSize;
			MemberLocation		fLocation;
};


/** @brief DW_TAG_pointer_type DIE. */
class DIEPointerType : public DIEAddressingType {
public:
								DIEPointerType();

	virtual	uint16				Tag() const;

	virtual	DebugInfoEntry*		Specification() const;

	virtual	status_t			AddAttribute_specification(uint16 attributeName,
									const AttributeValue& value);

private:
			DIEPointerType*		fSpecification;
};


/** @brief DW_TAG_reference_type DIE (C++ lvalue reference). */
class DIEReferenceType : public DIEAddressingType {
public:
								DIEReferenceType();

	virtual	uint16				Tag() const;
};


/** @brief DW_TAG_compile_unit DIE (root of a complete compilation unit). */
class DIECompileUnit : public DIECompileUnitBase {
public:
								DIECompileUnit();

	virtual	uint16				Tag() const;
};


/** @brief DW_TAG_string_type DIE (Pascal/Fortran string type). */
class DIEStringType : public DIEDeclaredType {
public:
								DIEStringType();

	virtual	uint16				Tag() const;

	virtual	const DynamicAttributeValue* ByteSize() const;

	virtual	status_t			AddAttribute_byte_size(uint16 attributeName,
									const AttributeValue& value);

private:
			DynamicAttributeValue fByteSize;
// TODO:
// DW_AT_string_length
};


/** @brief DW_TAG_structure_type DIE (C struct / C++ struct). */
class DIEStructureType : public DIEClassBaseType {
public:
								DIEStructureType();

	virtual	uint16				Tag() const;
};


/** @brief DW_TAG_subroutine_type DIE (function-pointer signature type). */
class DIESubroutineType : public DIEDeclaredType {
public:
								DIESubroutineType();

	virtual	uint16				Tag() const;

			DIEType*			ReturnType() const	{ return fReturnType; }

			const DebugInfoEntryList& Parameters() const { return fParameters; }

	virtual	status_t			AddChild(DebugInfoEntry* child);

	virtual	status_t			AddAttribute_address_class(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_prototyped(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_type(uint16 attributeName,
									const AttributeValue& value);

protected:
			DebugInfoEntryList	fParameters;
			DIEType*			fReturnType;
			uint8				fAddressClass;
			bool				fPrototyped;
};


/** @brief DW_TAG_typedef DIE (C/C++ typedef alias). */
class DIETypedef : public DIEDerivedType {
public:
								DIETypedef();

	virtual	uint16				Tag() const;
};


/** @brief DW_TAG_union_type DIE (C/C++ union). */
class DIEUnionType : public DIECompoundType {
public:
								DIEUnionType();

	virtual	uint16				Tag() const;
};


/** @brief DW_TAG_unspecified_parameters DIE (variadic "..." marker). */
class DIEUnspecifiedParameters : public DIEDeclaredBase {
public:
								DIEUnspecifiedParameters();

	virtual	uint16				Tag() const;

// TODO:
// DW_AT_abstract_origin
// DW_AT_artificial
};


/** @brief DW_TAG_variant DIE (Pascal variant case inside a variant_part). */
class DIEVariant : public DIEDeclaredBase {
public:
								DIEVariant();

	virtual	uint16				Tag() const;

// TODO:
// DW_AT_accessibility
// DW_AT_abstract_origin
// DW_AT_declaration
// DW_AT_discr_list
// DW_AT_discr_value
};


/** @brief DW_TAG_common_block DIE (Fortran COMMON block). */
class DIECommonBlock : public DIEDeclaredNamedBase {
public:
								DIECommonBlock();

	virtual	uint16				Tag() const;

	virtual	LocationDescription* GetLocationDescription();

// TODO:
// DW_AT_segment

private:
			LocationDescription	fLocationDescription;
};


/** @brief DW_TAG_common_inclusion DIE (Fortran COMMON inclusion). */
class DIECommonInclusion : public DIEDeclaredBase {
public:
								DIECommonInclusion();

	virtual	uint16				Tag() const;

// TODO:
// DW_AT_common_reference
// DW_AT_declaration
// DW_AT_visibility

};


/** @brief DW_TAG_inheritance DIE (C++ base class link). */
class DIEInheritance : public DIEDeclaredBase {
public:
								DIEInheritance();

	virtual	uint16				Tag() const;

			DIEType*			GetType() const	{ return fType; }
			const MemberLocation* Location() const
									{ return &fLocation; }

	virtual	status_t			AddAttribute_type(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_data_member_location(
									uint16 attributeName,
									const AttributeValue& value);

// TODO:
// DW_AT_accessibility
// DW_AT_virtuality

private:
			DIEType*			fType;
			MemberLocation		fLocation;
};


/** @brief DW_TAG_inlined_subroutine DIE (an inlined call site). */
class DIEInlinedSubroutine : public DebugInfoEntry {
public:
								DIEInlinedSubroutine();

	virtual	uint16				Tag() const;

// TODO:
// DW_AT_abstract_origin
// DW_AT_call_column
// DW_AT_call_file
// DW_AT_call_line
// DW_AT_entry_pc
// DW_AT_high_pc
// DW_AT_low_pc
// DW_AT_ranges
// DW_AT_return_addr
// DW_AT_segment
// DW_AT_start_scope
// DW_AT_trampoline
};


/** @brief DW_TAG_module DIE (Modula/Fortran module). */
class DIEModule : public DIEDeclaredNamedBase {
public:
								DIEModule();

	virtual	uint16				Tag() const;

// TODO:
// DW_AT_entry_pc
// DW_AT_high_pc
// DW_AT_low_pc
// DW_AT_priority
// DW_AT_ranges
// DW_AT_segment
// DW_AT_specification
};


/** @brief DW_TAG_ptr_to_member_type DIE (C++ pointer-to-member). */
class DIEPointerToMemberType : public DIEDerivedType {
public:
								DIEPointerToMemberType();

	virtual	uint16				Tag() const;

			DIECompoundType*	ContainingType() const
									{ return fContainingType; }

			const LocationDescription& UseLocation() const
									{ return fUseLocation; }

	virtual	status_t			AddAttribute_address_class(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_containing_type(
									uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_use_location(uint16 attributeName,
									const AttributeValue& value);

protected:
			DIECompoundType*	fContainingType;
			LocationDescription	fUseLocation;
			uint8				fAddressClass;
};


/** @brief DW_TAG_set_type DIE (Pascal set type). */
class DIESetType : public DIEDerivedType {
public:
								DIESetType();

	virtual	uint16				Tag() const;

	virtual	const DynamicAttributeValue* ByteSize() const;

	virtual	status_t			AddAttribute_byte_size(uint16 attributeName,
									const AttributeValue& value);

private:
			DynamicAttributeValue fByteSize;
};


/** @brief DW_TAG_subrange_type DIE (array dimension or numeric subrange). */
class DIESubrangeType : public DIEArrayIndexType {
public:
								DIESubrangeType();

	virtual	uint16				Tag() const;

			const DynamicAttributeValue* LowerBound() const
									{ return &fLowerBound; }
			const DynamicAttributeValue* UpperBound() const
									{ return &fUpperBound; }
			const DynamicAttributeValue* Count() const
									{ return &fCount; }

	virtual	status_t			AddAttribute_count(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_lower_bound(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_upper_bound(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_threads_scaled(
									uint16 attributeName,
									const AttributeValue& value);

private:
			DynamicAttributeValue fCount;
			DynamicAttributeValue fLowerBound;
			DynamicAttributeValue fUpperBound;
			bool				fThreadsScaled;
};


/** @brief DW_TAG_with_stmt DIE (Pascal with-statement scope). */
class DIEWithStatement : public DebugInfoEntry {
public:
								DIEWithStatement();

	virtual	uint16				Tag() const;

			DIEType*			GetType() const	{ return fType; }

	virtual	LocationDescription* GetLocationDescription();

	virtual	status_t			AddAttribute_type(uint16 attributeName,
									const AttributeValue& value);

// TODO:
// DW_AT_accessibility
// DW_AT_address_class
// DW_AT_declaration
// DW_AT_high_pc
// DW_AT_low_pc
// DW_AT_ranges
// DW_AT_segment
// DW_AT_visibility

private:
			DIEType*			fType;
			LocationDescription	fLocationDescription;
};


/** @brief DW_TAG_access_declaration DIE (C++ access declaration). */
class DIEAccessDeclaration : public DIEDeclaredNamedBase {
public:
								DIEAccessDeclaration();

	virtual	uint16				Tag() const;
};


/** @brief DW_TAG_base_type DIE (primitive type: int, float, ...). */
class DIEBaseType : public DIEType {
public:
								DIEBaseType();

	virtual	uint16				Tag() const;

	virtual	const DynamicAttributeValue* ByteSize() const;
			const DynamicAttributeValue* BitOffset() const
									{ return &fBitOffset; }
			const DynamicAttributeValue* DataBitOffset() const
									{ return &fDataBitOffset; }
			const DynamicAttributeValue* BitSize() const
									{ return &fBitSize; }
			uint8				Encoding() const	{ return fEncoding; }
			uint8				Endianity() const	{ return fEndianity; }

	virtual	status_t			AddAttribute_encoding(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_byte_size(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_bit_size(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_bit_offset(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_data_bit_offset(
									uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_endianity(uint16 attributeName,
									const AttributeValue& value);

// TODO:
// DW_AT_binary_scale
// DW_AT_decimal_scale
// DW_AT_decimal_sign
// DW_AT_description
// DW_AT_digit_count
// DW_AT_picture_string
// DW_AT_small

private:
			DynamicAttributeValue fByteSize;
			DynamicAttributeValue fBitOffset;
			DynamicAttributeValue fDataBitOffset;
			DynamicAttributeValue fBitSize;
			uint8				fEncoding;
			uint8				fEndianity;
};


/** @brief DW_TAG_catch_block DIE (C++ catch handler block). */
class DIECatchBlock : public DebugInfoEntry {
public:
								DIECatchBlock();

	virtual	uint16				Tag() const;

// TODO:
// DW_AT_abstract_origin
// DW_AT_high_pc
// DW_AT_low_pc
// DW_AT_ranges
// DW_AT_segment
};


/** @brief DW_TAG_const_type DIE (C/C++ const-qualified type). */
class DIEConstType : public DIEModifiedType {
public:
								DIEConstType();

	virtual	uint16				Tag() const;
};


/** @brief DW_TAG_constant DIE (named compile-time constant). */
class DIEConstant : public DIEDeclaredNamedBase {
public:
								DIEConstant();

	virtual	uint16				Tag() const;

			DIEType*			GetType() const	{ return fType; }

			const ConstantAttributeValue* ConstValue() const
									{ return &fValue; }

	virtual	status_t			AddAttribute_const_value(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_type(uint16 attributeName,
									const AttributeValue& value);

// TODO:
// DW_AT_endianity
// DW_AT_external
// DW_AT_start_scope

private:
			DIEType*			fType;
			ConstantAttributeValue fValue;
};


/** @brief DW_TAG_enumerator DIE (single enumeration constant). */
class DIEEnumerator : public DIEDeclaredNamedBase {
public:
								DIEEnumerator();

	virtual	uint16				Tag() const;

			const ConstantAttributeValue* ConstValue() const
									{ return &fValue; }

	virtual	status_t			AddAttribute_const_value(uint16 attributeName,
									const AttributeValue& value);

private:
			ConstantAttributeValue fValue;
};


/** @brief DW_TAG_file_type DIE (Pascal file type). */
class DIEFileType : public DIEDerivedType {
public:
								DIEFileType();

	virtual	uint16				Tag() const;

	virtual	const DynamicAttributeValue* ByteSize() const;

	virtual	status_t			AddAttribute_byte_size(uint16 attributeName,
									const AttributeValue& value);

private:
			DynamicAttributeValue fByteSize;
};


/** @brief DW_TAG_friend DIE (C++ friend declaration). */
class DIEFriend : public DIEDeclaredBase {
public:
								DIEFriend();

	virtual	uint16				Tag() const;

// TODO:
// DW_AT_abstract_origin
// DW_AT_friend
};


/** @brief DW_TAG_namelist DIE (Fortran NAMELIST). */
class DIENameList : public DIEDeclaredNamedBase {
public:
								DIENameList();

	virtual	uint16				Tag() const;

// TODO:
// DW_AT_abstract_origin
};


/** @brief DW_TAG_namelist_item DIE (Fortran NAMELIST element). */
class DIENameListItem : public DIEDeclaredBase {
public:
								DIENameListItem();

	virtual	uint16				Tag() const;

// TODO:
// DW_AT_namelist_item
};


/** @brief DW_TAG_namespace DIE (C++ namespace; also base for subprograms). */
class DIENamespace : public DIEDeclaredNamedBase {
public:
								DIENamespace();

	virtual	uint16				Tag() const;

	virtual	bool				IsNamespace() const;

			const DebugInfoEntryList& Children() const
										{ return fChildren; }

	virtual	status_t			AddChild(DebugInfoEntry* child);

private:
			DebugInfoEntryList	fChildren;

// TODO:
// DW_AT_extension
// DW_AT_start_scope
};


/** @brief DW_TAG_packed_type DIE (Ada/Pascal packed-storage type). */
class DIEPackedType : public DIEModifiedType {
public:
								DIEPackedType();

	virtual	uint16				Tag() const;
};


/** @brief DW_TAG_subprogram DIE (function or method). */
class DIESubprogram : public DIENamespace {
public:
								DIESubprogram();
								~DIESubprogram();

	virtual	uint16				Tag() const;

	virtual	DebugInfoEntry*		Specification() const;
	virtual	DebugInfoEntry*		AbstractOrigin() const;

			off_t				AddressRangesOffset() const
										{ return fAddressRangesOffset; }

			target_addr_t		LowPC() const	{ return fLowPC; }
			target_addr_t		HighPC() const	{ return fHighPC; }

			const LocationDescription* FrameBase() const { return &fFrameBase; }

			const DebugInfoEntryList Parameters() const	{ return fParameters; }
			const DebugInfoEntryList Variables() const	{ return fVariables; }
			const DebugInfoEntryList Blocks() const		{ return fBlocks; }
			const DebugInfoEntryList TemplateTypeParameters() const
										{ return fTemplateTypeParameters; }
			const DebugInfoEntryList TemplateValueParameters() const
										{ return fTemplateValueParameters; }
			const DebugInfoEntryList CallSites() const
										{ return fCallSites; }

			bool				IsPrototyped() const	{ return fPrototyped; }
			uint8				Inline() const			{ return fInline; }
			bool				IsArtificial() const	{ return fArtificial; }
			uint8				CallingConvention() const
										{ return fCallingConvention; }
			bool				IsMain() const			{ return fMain; }

			DIEType*			ReturnType() const		{ return fReturnType; }

	virtual	status_t			AddChild(DebugInfoEntry* child);

	virtual	status_t			AddAttribute_low_pc(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_high_pc(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_ranges(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_specification(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_address_class(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_prototyped(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_type(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_inline(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_abstract_origin(
									uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_frame_base(
									uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_artificial(
									uint16 attributeName,
									const AttributeValue& value);
	virtual status_t			AddAttribute_calling_convention(
									uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_main_subprogram(
									uint16 attributeName,
									const AttributeValue& value);


protected:
			DebugInfoEntryList	fParameters;
			DebugInfoEntryList	fVariables;
			DebugInfoEntryList	fBlocks;
			DebugInfoEntryList	fTemplateTypeParameters;
			DebugInfoEntryList	fTemplateValueParameters;
			DebugInfoEntryList	fCallSites;
			target_addr_t		fLowPC;
			target_addr_t		fHighPC;
			off_t				fAddressRangesOffset;
			DIESubprogram*		fSpecification;
			DIESubprogram*		fAbstractOrigin;
			DIEType*			fReturnType;
			LocationDescription	fFrameBase;
			uint8				fAddressClass;
			bool				fPrototyped;
			uint8				fInline;
			bool				fMain;
			bool				fArtificial;
			uint8				fCallingConvention;

// TODO:
// DW_AT_elemental
// DW_AT_entry_pc
// DW_AT_explicit
// DW_AT_external
// DW_AT_object_pointer
// DW_AT_pure
// DW_AT_recursive
// DW_AT_return_addr
// DW_AT_segment
// DW_AT_start_scope
// DW_AT_static_link
// DW_AT_trampoline
// DW_AT_virtuality
// DW_AT_vtable_elem_location
};


/** @brief DW_TAG_template_type_parameter DIE (C++ template type parameter). */
class DIETemplateTypeParameter : public DIEDeclaredNamedBase {
public:
								DIETemplateTypeParameter();

	virtual	uint16				Tag() const;

			DIEType*			GetType() const	{ return fType; }

	virtual	status_t			AddAttribute_type(uint16 attributeName,
									const AttributeValue& value);

private:
			DIEType*			fType;
};


/** @brief DW_TAG_template_value_parameter DIE (C++ non-type template parameter). */
class DIETemplateValueParameter : public DIEDeclaredNamedBase {
public:
								DIETemplateValueParameter();

	virtual	uint16				Tag() const;

			DIEType*			GetType() const	{ return fType; }

			const ConstantAttributeValue* ConstValue() const
									{ return &fValue; }

	virtual	status_t			AddAttribute_const_value(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_type(uint16 attributeName,
									const AttributeValue& value);

private:
			DIEType*			fType;
			ConstantAttributeValue fValue;
};


/** @brief DW_TAG_thrown_type DIE (exception type listed in a throw spec). */
class DIEThrownType : public DIEDeclaredBase {
public:
								DIEThrownType();

	virtual	uint16				Tag() const;

			DIEType*			GetType() const	{ return fType; }

	virtual	status_t			AddAttribute_type(uint16 attributeName,
									const AttributeValue& value);

// TODO:
// DW_AT_allocated
// DW_AT_associated
// DW_AT_data_location

private:
			DIEType*			fType;
};


/** @brief DW_TAG_try_block DIE (C++ try block). */
class DIETryBlock : public DebugInfoEntry {
public:
								DIETryBlock();

	virtual	uint16				Tag() const;

// TODO:
// DW_AT_abstract_origin
// DW_AT_high_pc
// DW_AT_low_pc
// DW_AT_ranges
// DW_AT_segment
};


/** @brief DW_TAG_variant_part DIE (Pascal variant-record part). */
class DIEVariantPart : public DIEDeclaredBase {
public:
								DIEVariantPart();

	virtual	uint16				Tag() const;

			DIEType*			GetType() const	{ return fType; }

	virtual	status_t			AddAttribute_type(uint16 attributeName,
									const AttributeValue& value);

// TODO:
// DW_AT_abstract_origin
// DW_AT_accessibility
// DW_AT_declaration
// DW_AT_discr

private:
			DIEType*			fType;
};


/** @brief DW_TAG_variable DIE (named variable, global or local). */
class DIEVariable : public DIEDeclaredNamedBase {
public:
								DIEVariable();

	virtual	uint16				Tag() const;

	virtual	DebugInfoEntry*		Specification() const;
	virtual	DebugInfoEntry*		AbstractOrigin() const;

	virtual	LocationDescription* GetLocationDescription();

			DIEType*			GetType() const	{ return fType; }

			const ConstantAttributeValue* ConstValue() const
									{ return &fValue; }

			uint64				StartScope() const	{ return fStartScope; }

			bool				IsExternal() const	{ return fIsExternal; }

	virtual	status_t			AddAttribute_const_value(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_type(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_specification(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_abstract_origin(
									uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_start_scope(
									uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_external(
									uint16 attributeName,
									const AttributeValue& value);

// TODO:
// DW_AT_endianity
// DW_AT_segment

private:
			LocationDescription	fLocationDescription;
			ConstantAttributeValue fValue;
			DIEType*			fType;
			DebugInfoEntry*		fSpecification;
			DIEVariable*		fAbstractOrigin;
			uint64				fStartScope;
			bool				fIsExternal;
};


/** @brief DW_TAG_volatile_type DIE (C/C++ volatile-qualified type). */
class DIEVolatileType : public DIEModifiedType {
public:
								DIEVolatileType();

	virtual	uint16				Tag() const;

	virtual	status_t			AddAttribute_decl_file(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_decl_line(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_decl_column(uint16 attributeName,
									const AttributeValue& value);

private:
			DeclarationLocation	fDeclarationLocation;
};


/** @brief DW_TAG_dwarf_procedure DIE (a DWARF expression-as-procedure). */
class DIEDwarfProcedure : public DebugInfoEntry {
public:
								DIEDwarfProcedure();

	virtual	uint16				Tag() const;

	virtual	LocationDescription* GetLocationDescription();

private:
			LocationDescription	fLocationDescription;
};


/** @brief DW_TAG_restrict_type DIE (C99 restrict-qualified type). */
class DIERestrictType : public DIEModifiedType {
public:
								DIERestrictType();

	virtual	uint16				Tag() const;
};


/** @brief DW_TAG_interface_type DIE (Java/COM interface). */
class DIEInterfaceType : public DIEClassBaseType {
public:
								DIEInterfaceType();

	virtual	uint16				Tag() const;
};


/** @brief DW_TAG_imported_module DIE (using-namespace / using-module). */
class DIEImportedModule : public DIEDeclaredBase {
public:
								DIEImportedModule();

	virtual	uint16				Tag() const;

// TODO:
// DW_AT_import
// DW_AT_start_scope
};


/** @brief DW_TAG_unspecified_type DIE (placeholder for unknown type). */
class DIEUnspecifiedType : public DIEType {
public:
								DIEUnspecifiedType();

	virtual	uint16				Tag() const;

	virtual	status_t			AddAttribute_decl_file(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_decl_line(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_decl_column(uint16 attributeName,
									const AttributeValue& value);

// TODO:
// DW_AT_description

private:
			DeclarationLocation	fDeclarationLocation;
};


/** @brief DW_TAG_partial_unit DIE (partial compilation unit, imported elsewhere). */
class DIEPartialUnit : public DIECompileUnitBase {
public:
								DIEPartialUnit();

	virtual	uint16				Tag() const;

// TODO:
// DW_AT_description
};


/** @brief DW_TAG_imported_unit DIE (reference to a partial unit). */
class DIEImportedUnit : public DebugInfoEntry {
public:
								DIEImportedUnit();

	virtual	uint16				Tag() const;

// TODO:
// DW_AT_import
};


/** @brief DW_TAG_condition DIE (COBOL level-88 condition). */
class DIECondition : public DIEDeclaredNamedBase {
public:
								DIECondition();

	virtual	uint16				Tag() const;
};


/** @brief DW_TAG_shared_type DIE (UPC shared-type qualifier). */
class DIESharedType : public DIEModifiedType {
public:
								DIESharedType();

	virtual	uint16				Tag() const;

	virtual	status_t			AddAttribute_count(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_decl_file(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_decl_line(uint16 attributeName,
									const AttributeValue& value);
	virtual	status_t			AddAttribute_decl_column(uint16 attributeName,
									const AttributeValue& value);

private:
			DynamicAttributeValue fBlockSize;
			DeclarationLocation	fDeclarationLocation;
};


/** @brief DW_TAG_type_unit DIE (root of a deduplicated type unit). */
class DIETypeUnit : public DIECompileUnitBase {
public:
								DIETypeUnit();

	virtual	uint16				Tag() const;
};


/** @brief DW_TAG_rvalue_reference_type DIE (C++11 rvalue reference). */
class DIERValueReferenceType : public DIEReferenceType {
public:
								DIERValueReferenceType();

	virtual	uint16				Tag() const;
};


/** @brief DW_TAG_template_alias / template-template parameter DIE. */
class DIETemplateTemplateParameter : public DIEDeclaredBase {
public:
								DIETemplateTemplateParameter();

	virtual	uint16				Tag() const;

	virtual	const char*			Name() const;

	virtual	status_t			AddAttribute_name(uint16 attributeName,
									const AttributeValue& value);

private:
			const char*			fName;
};


/** @brief DW_TAG_GNU_template_parameter_pack DIE for type parameter packs. */
class DIETemplateTypeParameterPack : public DIEDeclaredBase {
public:
								DIETemplateTypeParameterPack();

	virtual	uint16				Tag() const;

	virtual	const char*			Name() const;

	virtual	status_t			AddAttribute_name(uint16 attributeName,
									const AttributeValue& value);

			const DebugInfoEntryList& Children() const
										{ return fChildren; }

	virtual	status_t			AddChild(DebugInfoEntry* child);

private:
			const char*			fName;
			DebugInfoEntryList	fChildren;
};


/** @brief DW_TAG_GNU_formal_parameter_pack DIE for value parameter packs. */
class DIETemplateValueParameterPack : public DIEDeclaredBase {
public:
								DIETemplateValueParameterPack();

	virtual	uint16				Tag() const;

	virtual	const char*			Name() const;

	virtual	status_t			AddAttribute_name(uint16 attributeName,
									const AttributeValue& value);

			const DebugInfoEntryList& Children() const
										{ return fChildren; }

	virtual	status_t			AddChild(DebugInfoEntry* child);

private:
			const char*			fName;
			DebugInfoEntryList	fChildren;
};


/** @brief DW_TAG_call_site / DW_TAG_GNU_call_site DIE (DWARF v5 call site). */
class DIECallSite : public DIEDeclaredBase {
public:
								DIECallSite();

	virtual	uint16				Tag() const;

	virtual	const char*			Name() const;

	virtual	status_t			AddAttribute_name(uint16 attributeName,
									const AttributeValue& value);

			const DebugInfoEntryList& Children() const
										{ return fChildren; }

	virtual	status_t			AddChild(DebugInfoEntry* child);

private:
			const char*			fName;
			DebugInfoEntryList	fChildren;
};


/** @brief DW_TAG_call_site_parameter DIE (parameter passed at a call site). */
class DIECallSiteParameter : public DIEDeclaredBase {
public:
								DIECallSiteParameter();

	virtual	uint16				Tag() const;

	virtual	const char*			Name() const;

	virtual	status_t			AddAttribute_name(uint16 attributeName,
									const AttributeValue& value);

			const DebugInfoEntryList& Children() const
										{ return fChildren; }

	virtual	status_t			AddChild(DebugInfoEntry* child);

private:
			const char*			fName;
			DebugInfoEntryList	fChildren;
};


// #pragma mark - DebugInfoEntryFactory


/**
 * @brief Factory mapping DWARF tag codes to concrete DebugInfoEntry subclasses.
 *
 * Used by the parser to instantiate the right DIE class for each
 * abbreviation it encounters.
 */
class DebugInfoEntryFactory {
public:
								DebugInfoEntryFactory();

			status_t			CreateDebugInfoEntry(uint16 tag,
									DebugInfoEntry*& entry);
};


#endif	// DEBUG_INFO_ENTRIES_H
