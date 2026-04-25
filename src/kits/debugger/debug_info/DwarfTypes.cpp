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
 *   Copryight 2012-2014, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DwarfTypes.cpp
 * @brief Implementation of the DWARF-derived Type hierarchy and the
 *        DwarfTypeContext that supports DWARF location-expression
 *        evaluation.
 *
 * Each Dwarf*Type subclass adapts a DIE-derived structural description
 * into the abstract Type model used by the rest of the debugger. Layout
 * resolution (member offsets, array element addresses) is implemented in
 * terms of DwarfType::ResolveLocation(), which evaluates the DIE's
 * location description against the live CPU and target memory exposed
 * through DwarfTargetInterface.
 *
 * @see DwarfTypeFactory, DwarfImageDebugInfo, DwarfStackFrameDebugInfo
 */


#include "DwarfTypes.h"

#include <new>

#include "Architecture.h"
#include "ArrayIndexPath.h"
#include "CompilationUnit.h"
#include "Dwarf.h"
#include "DwarfFile.h"
#include "DwarfTargetInterface.h"
#include "DwarfUtils.h"
#include "Register.h"
#include "RegisterMap.h"
#include "Tracing.h"
#include "ValueLocation.h"


namespace {


// #pragma mark - HasBitStridePredicate


/**
 * @brief Predicate template that returns @c true when a DIE supplies a
 *        valid @c DW_AT_bit_stride attribute.
 */
template<typename EntryType>
struct HasBitStridePredicate {
	inline bool operator()(EntryType* entry) const
	{
		return entry->BitStride()->IsValid();
	}
};


// #pragma mark - HasByteStridePredicate


/**
 * @brief Predicate template that returns @c true when a DIE supplies a
 *        valid @c DW_AT_byte_stride attribute.
 */
template<typename EntryType>
struct HasByteStridePredicate {
	inline bool operator()(EntryType* entry) const
	{
		return entry->ByteStride()->IsValid();
	}
};


}	// unnamed namespace


/**
 * @brief Maps a DWARF tag to the abstract @c type_kind enumerator.
 *
 * @param tag  DWARF tag (one of the @c DW_TAG_* constants).
 * @return Matching @c type_kind, or @c TYPE_UNSPECIFIED for unrecognized
 *         tags.
 */
type_kind
dwarf_tag_to_type_kind(int32 tag)
{
	switch (tag) {
		case DW_TAG_class_type:
		case DW_TAG_structure_type:
		case DW_TAG_union_type:
		case DW_TAG_interface_type:
			return TYPE_COMPOUND;

		case DW_TAG_base_type:
			return TYPE_PRIMITIVE;

		case DW_TAG_pointer_type:
		case DW_TAG_reference_type:
			return TYPE_ADDRESS;

		case DW_TAG_const_type:
		case DW_TAG_packed_type:
		case DW_TAG_volatile_type:
		case DW_TAG_restrict_type:
		case DW_TAG_shared_type:
			return TYPE_MODIFIED;

		case DW_TAG_typedef:
			return TYPE_TYPEDEF;

		case DW_TAG_array_type:
			return TYPE_ARRAY;

		case DW_TAG_enumeration_type:
			return TYPE_ENUMERATION;

		case DW_TAG_subrange_type:
			return TYPE_SUBRANGE;

		case DW_TAG_unspecified_type:
			return TYPE_UNSPECIFIED;

		case DW_TAG_subroutine_type:
			return TYPE_FUNCTION;

		case DW_TAG_ptr_to_member_type:
			return TYPE_POINTER_TO_MEMBER;

	}

	return TYPE_UNSPECIFIED;
}


/**
 * @brief Maps a DWARF tag to its abstract subtype kind.
 *
 * Used to distinguish struct vs class vs union for compound types and
 * pointer vs reference for derived types.
 *
 * @param tag  DWARF tag.
 * @return The corresponding subtype-kind constant, or -1 if @a tag has no
 *         subtype distinction.
 */
int32
dwarf_tag_to_subtype_kind(int32 tag)
{
	switch (tag) {
		case DW_TAG_class_type:
			return COMPOUND_TYPE_CLASS;

		case DW_TAG_structure_type:
			return COMPOUND_TYPE_STRUCT;

		case DW_TAG_union_type:
			return COMPOUND_TYPE_UNION;

		case DW_TAG_interface_type:
			return COMPOUND_TYPE_INTERFACE;

		case DW_TAG_pointer_type:
			return DERIVED_TYPE_POINTER;

		case DW_TAG_reference_type:
			return DERIVED_TYPE_REFERENCE;
	}

	return -1;
}


// #pragma mark - DwarfTypeContext


/**
 * @brief Constructs a DwarfTypeContext, acquiring references on the
 *        architecture, file and (optional) target interface.
 *
 * The compilation unit and subprogram-entry pointers are borrowed; their
 * lifetime is bound to the owning DwarfFile.
 *
 * @param architecture          Target architecture.
 * @param imageID               Image ID of the containing image.
 * @param file                  DWARF file the types come from.
 * @param compilationUnit       CU governing the active scope.
 * @param subprogramEntry       Active subprogram or @c NULL when not in a
 *                              subprogram (e.g. global type construction).
 * @param instructionPointer    Live PC, used by location expressions.
 * @param framePointer          Live FP, used by location expressions.
 * @param relocationDelta       Image relocation offset.
 * @param targetInterface       Target memory/register access (may be NULL).
 * @param fromDwarfRegisterMap  DWARF-to-architecture register mapping.
 */
DwarfTypeContext::DwarfTypeContext(Architecture* architecture, image_id imageID,
	DwarfFile* file, CompilationUnit* compilationUnit,
	DIESubprogram* subprogramEntry, target_addr_t instructionPointer,
	target_addr_t framePointer, target_addr_t relocationDelta,
	DwarfTargetInterface* targetInterface, RegisterMap* fromDwarfRegisterMap)
	:
	fArchitecture(architecture),
	fImageID(imageID),
	fFile(file),
	fCompilationUnit(compilationUnit),
	fSubprogramEntry(subprogramEntry),
	fInstructionPointer(instructionPointer),
	fFramePointer(framePointer),
	fRelocationDelta(relocationDelta),
	fTargetInterface(targetInterface),
	fFromDwarfRegisterMap(fromDwarfRegisterMap)
{
	fArchitecture->AcquireReference();
	fFile->AcquireReference();
	if (fTargetInterface != NULL)
		fTargetInterface->AcquireReference();
}


/**
 * @brief Releases references on the architecture, file and target
 *        interface (if any).
 */
DwarfTypeContext::~DwarfTypeContext()
{
	fArchitecture->ReleaseReference();
	fFile->ReleaseReference();
	if (fTargetInterface != NULL)
		fTargetInterface->ReleaseReference();
}


/**
 * @brief Returns the address size in bytes for this context.
 *
 * @return Compilation-unit address size when available; otherwise the
 *         architecture's default.
 */
uint8
DwarfTypeContext::AddressSize() const
{
	return fCompilationUnit != NULL ? fCompilationUnit->AddressSize()
		: fArchitecture->AddressSize();
}


/**
 * @brief Reports the endianness applicable to this context.
 *
 * @return Compilation-unit endianness when available; otherwise the
 *         architecture's.
 */
bool
DwarfTypeContext::IsBigEndian() const
{
	return fCompilationUnit != NULL ? fCompilationUnit->IsBigEndian()
		: fArchitecture->IsBigEndian();
}


// #pragma mark - DwarfType


/**
 * @brief Constructs a DwarfType bound to a context and DIE entry.
 *
 * Acquires a reference on @a typeContext and computes a stable ID from
 * @a entry via GetTypeID().
 *
 * @param typeContext  Context shared by all types in the same scope.
 * @param name         Type name; may be empty for anonymous types.
 * @param entry        DIE the type was derived from.
 */
DwarfType::DwarfType(DwarfTypeContext* typeContext, const BString& name,
	const DIEType* entry)
	:
	fTypeContext(typeContext),
	fName(name),
	fByteSize(0)
{
	fTypeContext->AcquireReference();

	GetTypeID(entry, fID);
}


/**
 * @brief Releases the held DwarfTypeContext reference.
 */
DwarfType::~DwarfType()
{
	fTypeContext->ReleaseReference();
}


/**
 * @brief Builds a stable, process-unique ID for a DIEType.
 *
 * The ID is a string of the form @c "dwarf:0x...." encoding the entry
 * pointer; uniqueness lasts as long as the DIE is alive.
 *
 * @param entry  Entry to identify; may be @c NULL.
 * @param _id    Out parameter receiving the ID string.
 * @return @c true on success; @c false only if formatting produced an
 *         empty string (defensive check).
 */
/*static*/ bool
DwarfType::GetTypeID(const DIEType* entry, BString& _id)
{
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "dwarf:%p", entry);
	BString id = buffer;
	if (id.Length() == 0)
		return false;

	_id = id;
	return true;
}


/** @brief Returns the image ID of the type's image. */
image_id
DwarfType::ImageID() const
{
	return fTypeContext->ImageID();
}


/** @brief Returns the type's stable ID. */
const BString&
DwarfType::ID() const
{
	return fID;
}


/** @brief Returns the type's name. */
const BString&
DwarfType::Name() const
{
	return fName;
}


/** @brief Returns the cached byte size, or 0 if not yet known. */
target_size_t
DwarfType::ByteSize() const
{
	return fByteSize;
}


/**
 * @brief Builds a pointer or reference type whose pointee is this type.
 *
 * The result has its byte size set to the architecture's address size.
 *
 * @param addressType  Whether to produce a pointer or a reference.
 * @param _resultType  Out parameter receiving the new AddressType;
 *                     reference transferred to caller.
 * @retval B_OK         Type created.
 * @retval B_NO_MEMORY  Allocation failure.
 */
status_t
DwarfType::CreateDerivedAddressType(address_type_kind addressType,
	AddressType*& _resultType)
{
	BString derivedName;
	derivedName.SetToFormat("%s%c", fName.String(),
		addressType == DERIVED_TYPE_POINTER ? '*' : '&');
	DwarfAddressType* resultType = new(std::nothrow)
		DwarfAddressType(fTypeContext, derivedName, NULL, addressType, this);

	if (resultType == NULL)
		return B_NO_MEMORY;

	resultType->SetByteSize(fTypeContext->GetArchitecture()->AddressSize());

	_resultType = resultType;
	return B_OK;
}


/**
 * @brief Wraps this type in an array (or extends an existing one) with a
 *        new dimension.
 *
 * @param lowerBound      Lower bound of the new dimension.
 * @param elementCount    Number of elements in the dimension.
 * @param extendExisting  When @c true and this object is already a
 *                        DwarfArrayType, append the dimension to it.
 * @param _resultType     Out parameter receiving the array type;
 *                        reference transferred to caller.
 * @retval B_OK         Array type produced.
 * @retval B_NO_MEMORY  Allocation failure.
 */
status_t
DwarfType::CreateDerivedArrayType(int64 lowerBound, int64 elementCount,
	bool extendExisting, ArrayType*& _resultType)
{
	DwarfArrayType* resultType = NULL;
	BReference<DwarfType> baseTypeReference;
	if (extendExisting)
		resultType = dynamic_cast<DwarfArrayType*>(this);

	if (resultType == NULL) {
		BString derivedName;
		derivedName.SetToFormat("%s[]", fName.String());
		resultType = new(std::nothrow)
			DwarfArrayType(fTypeContext, derivedName, NULL, this);
		baseTypeReference.SetTo(resultType, true);
	}

	if (resultType == NULL)
		return B_NO_MEMORY;

	DwarfSubrangeType* subrangeType = new(std::nothrow) DwarfSubrangeType(
		fTypeContext, fName, NULL, resultType, BVariant(lowerBound),
		BVariant(lowerBound + elementCount - 1));
	if (subrangeType == NULL)
		return B_NO_MEMORY;

	BReference<DwarfSubrangeType> subrangeReference(subrangeType, true);

	DwarfArrayDimension* dimension = new(std::nothrow) DwarfArrayDimension(
		subrangeType);
	if (dimension == NULL)
		return B_NO_MEMORY;
	BReference<DwarfArrayDimension> dimensionReference(dimension, true);

	if (!resultType->AddDimension(dimension))
		return B_NO_MEMORY;

	baseTypeReference.Detach();

	_resultType = resultType;
	return B_OK;
}


/**
 * @brief Refines an object's value location for use as data of this type.
 *
 * If @a objectLocation already has explicit size or multiple pieces it is
 * cloned verbatim. A single zero-sized memory piece is upgraded to use
 * the type's byte size so callers can read the object payload.
 *
 * @param objectLocation  Source location describing where the object lives.
 * @param _location       Out parameter receiving the refined location.
 * @retval B_OK         Location was produced.
 * @retval B_BAD_VALUE  @a objectLocation has no pieces.
 * @retval B_NO_MEMORY  Allocation failure.
 */
status_t
DwarfType::ResolveObjectDataLocation(const ValueLocation& objectLocation,
	ValueLocation*& _location)
{
	// TODO: In some source languages the object address might be a pointer
	// to a descriptor, not the actual object data.

	// If the given location looks good already, just clone it.
	int32 count = objectLocation.CountPieces();
	if (count == 0)
		return B_BAD_VALUE;

	ValuePieceLocation piece;
	if (!piece.Copy(objectLocation.PieceAt(0)))
		return B_NO_MEMORY;

	if (count > 1 || piece.type != VALUE_PIECE_LOCATION_MEMORY
		|| piece.size != 0 || piece.bitSize != 0) {
		ValueLocation* location
			= new(std::nothrow) ValueLocation(objectLocation);
		if (location == NULL || location->CountPieces() != count) {
			delete location;
			return B_NO_MEMORY;
		}

		_location = location;
		return B_OK;
	}

	// The location contains just a single address piece with a zero size
	// -- set the type's size.
	piece.SetSize(ByteSize());
		// TODO: Use bit size and bit offset, if specified!

	ValueLocation* location = new(std::nothrow) ValueLocation(
		objectLocation.IsBigEndian());
	if (location == NULL || !location->AddPiece(piece)) {
		delete location;
		return B_NO_MEMORY;
	}

	_location = location;
	return B_OK;
}


/**
 * @brief Convenience overload that builds a ValueLocation for an object at
 *        a known runtime address and forwards to the location-based
 *        overload.
 *
 * @param objectAddress  Runtime address of the object.
 * @param _location      Out parameter receiving the resulting location.
 * @return Status from the underlying ResolveObjectDataLocation().
 */
status_t
DwarfType::ResolveObjectDataLocation(target_addr_t objectAddress,
	ValueLocation*& _location)
{
	ValuePieceLocation piece;
	piece.SetToMemory(objectAddress);
	piece.SetSize(0);
		// We set the piece size to 0 as an indicator that the size has to be
		// set.
		// TODO: We could set the byte size from type, but that may not be
		// accurate. We may want to add bit offset and size to Type.

	ValueLocation location(fTypeContext->GetArchitecture()->IsBigEndian());
	if (!location.AddPiece(piece))
		return B_NO_MEMORY;

	return ResolveObjectDataLocation(location, _location);
}


/**
 * @brief Evaluates a DWARF location description into a concrete
 *        ValueLocation.
 *
 * Delegates to DwarfFile::ResolveLocation() for expression interpretation
 * then post-processes each piece: register indices are mapped from the
 * DWARF numbering to the architecture's, bit offsets are flipped for
 * little-endian memory pieces, and a final unsized piece adopts the type's
 * byte size.
 *
 * @param typeContext       Context whose live PC/FP and target interface
 *                          are used during evaluation.
 * @param description       DWARF location description to evaluate; must
 *                          be valid.
 * @param objectAddress     Object address operand for the expression.
 * @param hasObjectAddress  Whether @a objectAddress should be pushed onto
 *                          the operand stack as a starting value.
 * @param _location         Filled in with the resulting pieces.
 * @retval B_OK         Evaluation succeeded.
 * @retval B_NO_MEMORY  Allocation failure during piece processing.
 * @retval other        Errors from DwarfFile::ResolveLocation().
 */
status_t
DwarfType::ResolveLocation(DwarfTypeContext* typeContext,
	const LocationDescription* description, target_addr_t objectAddress,
	bool hasObjectAddress, ValueLocation& _location)
{
	status_t error = typeContext->File()->ResolveLocation(
		typeContext->GetCompilationUnit(),
		typeContext->AddressSize(), typeContext->IsBigEndian(),
		typeContext->SubprogramEntry(), description,
		typeContext->TargetInterface(), typeContext->InstructionPointer(),
		objectAddress, hasObjectAddress, typeContext->FramePointer(),
		typeContext->RelocationDelta(), _location);
	if (error != B_OK)
		return error;

	// translate the DWARF register indices and the bit offset/size semantics
	const Register* registers = typeContext->GetArchitecture()->Registers();
	bool bigEndian = typeContext->GetArchitecture()->IsBigEndian();
	int32 count = _location.CountPieces();
	for (int32 i = 0; i < count; i++) {
		ValuePieceLocation piece;
		if (!piece.Copy(_location.PieceAt(i)))
			return B_NO_MEMORY;

		if (piece.type == VALUE_PIECE_LOCATION_REGISTER) {
			int32 reg = typeContext->FromDwarfRegisterMap()->MapRegisterIndex(
				piece.reg);
			if (reg >= 0) {
				piece.reg = reg;
				// The bit offset for registers is to the least
				// significant bit, while we want the offset to the most
				// significant bit.
				if (registers[reg].BitSize() > piece.bitSize) {
					piece.bitOffset = registers[reg].BitSize() - piece.bitSize
						- piece.bitOffset;
				}
			} else
				piece.SetToUnknown();
		} else if (piece.type == VALUE_PIECE_LOCATION_MEMORY) {
			// Whether the bit offset is to the least or most significant bit
			// is target architecture and source language specific.
			// TODO: Check whether this is correct!
			// TODO: Source language!
			if (!bigEndian && piece.size * 8 > piece.bitSize) {
				piece.bitOffset = piece.size * 8 - piece.bitSize
					- piece.bitOffset;
			}
		}

		piece.Normalize(bigEndian);
		if (!_location.SetPieceAt(i, piece))
			return B_NO_MEMORY;
	}

	// If we only have one piece and that doesn't have a size, try to retrieve
	// the size of the type.
	if (count == 1) {
		ValuePieceLocation piece;
		if (!piece.Copy(_location.PieceAt(0)))
			return B_NO_MEMORY;

		if (piece.IsValid() && piece.size == 0 && piece.bitSize == 0) {
			piece.SetSize(ByteSize());
				// TODO: Use bit size and bit offset, if specified!
			if (!_location.SetPieceAt(0, piece))
				return B_NO_MEMORY;

			TRACE_LOCALS("  set single piece size to %" B_PRIu64 "\n",
				ByteSize());
		}
	}

	return B_OK;
}


// #pragma mark - DwarfInheritance


/** @brief Constructs an inheritance node and acquires the type reference. */
DwarfInheritance::DwarfInheritance(DIEInheritance* entry, DwarfType* type)
	:
	fEntry(entry),
	fType(type)
{
	fType->AcquireReference();
}


/** @brief Releases the held DwarfType reference. */
DwarfInheritance::~DwarfInheritance()
{
	fType->ReleaseReference();
}


/** @brief Returns the base Type as required by the abstract BaseType
           interface. */
Type*
DwarfInheritance::GetType() const
{
	return fType;
}


// #pragma mark - DwarfDataMember


/** @brief Constructs a data member node and acquires the type reference. */
DwarfDataMember::DwarfDataMember(DIEMember* entry, const BString& name,
	DwarfType* type)
	:
	fEntry(entry),
	fName(name),
	fType(type)
{
	fType->AcquireReference();
}


/** @brief Releases the held DwarfType reference. */
DwarfDataMember::~DwarfDataMember()
{
	fType->ReleaseReference();
}

/** @brief Returns the member name, or @c NULL when anonymous. */
const char*
DwarfDataMember::Name() const
{
	return fName.Length() > 0 ? fName.String() : NULL;
}


/** @brief Returns the member's Type as required by DataMember. */
Type*
DwarfDataMember::GetType() const
{
	return fType;
}


// #pragma mark - DwarfEnumeratorValue


/** @brief Constructs an enumerator value node. */
DwarfEnumeratorValue::DwarfEnumeratorValue(DIEEnumerator* entry,
	const BString& name, const BVariant& value)
	:
	fEntry(entry),
	fName(name),
	fValue(value)
{
}


/** @brief Destructor; nothing to release. */
DwarfEnumeratorValue::~DwarfEnumeratorValue()
{
}

/** @brief Returns the enumerator name, or @c NULL when empty. */
const char*
DwarfEnumeratorValue::Name() const
{
	return fName.Length() > 0 ? fName.String() : NULL;
}


/** @brief Returns the enumerator's numeric value as a BVariant. */
BVariant
DwarfEnumeratorValue::Value() const
{
	return fValue;
}


// #pragma mark - DwarfArrayDimension


/** @brief Constructs an array dimension and acquires the index-type
           reference. */
DwarfArrayDimension::DwarfArrayDimension(DwarfType* type)
	:
	fType(type)
{
	fType->AcquireReference();
}


/** @brief Releases the held DwarfType reference. */
DwarfArrayDimension::~DwarfArrayDimension()
{
	fType->ReleaseReference();
}


/** @brief Returns the index-type Type as required by ArrayDimension. */
Type*
DwarfArrayDimension::GetType() const
{
	return fType;
}


// #pragma mark - DwarfFunctionParameter


/** @brief Constructs a function parameter and acquires the type
           reference. */
DwarfFunctionParameter::DwarfFunctionParameter(DIEFormalParameter* entry,
	const BString& name, DwarfType* type)
	:
	fEntry(entry),
	fName(name),
	fType(type)
{
	fType->AcquireReference();
}


/** @brief Releases the held DwarfType reference. */
DwarfFunctionParameter::~DwarfFunctionParameter()
{
	fType->ReleaseReference();
}


/** @brief Returns the parameter name, or @c NULL when anonymous. */
const char*
DwarfFunctionParameter::Name() const
{
	return fName.Length() > 0 ? fName.String() : NULL;
}


/** @brief Returns the parameter's Type as required by FunctionParameter. */
Type*
DwarfFunctionParameter::GetType() const
{
	return fType;
}


// #pragma mark - DwarfTemplateParameter


/**
 * @brief Constructs a template parameter, distinguishing between type and
 *        non-type parameters and capturing the constant value when present.
 *
 * @param entry  DWARF DIE: either DIETemplateTypeParameter or
 *               DIETemplateValueParameter.
 * @param type   Resolved type for the parameter (the parameter type for
 *               non-type parameters; the bound type for type parameters).
 */
DwarfTemplateParameter::DwarfTemplateParameter(DebugInfoEntry* entry,
	DwarfType* type)
	:
	fEntry(entry),
	fType(type)
{
	fType->AcquireReference();
	DIETemplateTypeParameter* typeParameter
		= dynamic_cast<DIETemplateTypeParameter *>(entry);
	if (typeParameter != NULL)
		fTemplateKind = TEMPLATE_TYPE_TYPE;
	else {
		DIETemplateValueParameter* valueParameter
			= dynamic_cast<DIETemplateValueParameter *>(entry);
		fTemplateKind = TEMPLATE_TYPE_VALUE;
		const ConstantAttributeValue* constValue = valueParameter
			->ConstValue();
		switch (constValue->attributeClass) {
			case ATTRIBUTE_CLASS_CONSTANT:
				fValue.SetTo(constValue->constant);
				break;
			case ATTRIBUTE_CLASS_STRING:
				fValue.SetTo(constValue->string);
				break;
			// TODO: ATTRIBUTE_CLASS_BLOCK_DATA
		}
	}
}


/** @brief Releases the held DwarfType reference. */
DwarfTemplateParameter::~DwarfTemplateParameter()
{
	fType->ReleaseReference();
}


// #pragma mark - DwarfPrimitiveType


/** @brief Constructs a primitive type with the given Haiku type constant. */
DwarfPrimitiveType::DwarfPrimitiveType(DwarfTypeContext* typeContext,
	const BString& name, DIEBaseType* entry, uint32 typeConstant)
	:
	DwarfType(typeContext, name, entry),
	fEntry(entry),
	fTypeConstant(typeConstant)
{
}


/** @brief Returns the underlying DIE entry. */
DIEType*
DwarfPrimitiveType::GetDIEType() const
{
	return fEntry;
}


/** @brief Returns the Haiku-style numeric type constant
           (e.g. @c B_INT32_TYPE). */
uint32
DwarfPrimitiveType::TypeConstant() const
{
	return fTypeConstant;
}


// #pragma mark - DwarfCompoundType


/** @brief Constructs a compound type with the given compound kind. */
DwarfCompoundType::DwarfCompoundType(DwarfTypeContext* typeContext,
	const BString& name, DIECompoundType* entry,
	compound_type_kind compoundKind)
	:
	DwarfType(typeContext, name, entry),
	fCompoundKind(compoundKind),
	fEntry(entry)
{
}


/** @brief Releases references on inheritances, members, and template
           parameters. */
DwarfCompoundType::~DwarfCompoundType()
{
	for (int32 i = 0;
			DwarfInheritance* inheritance = fInheritances.ItemAt(i); i++) {
		inheritance->ReleaseReference();
	}
	for (int32 i = 0; DwarfDataMember* member = fDataMembers.ItemAt(i); i++)
		member->ReleaseReference();

	for (int32 i = 0; DwarfTemplateParameter* parameter
		= fTemplateParameters.ItemAt(i); i++) {
		parameter->ReleaseReference();
	}
}


/** @brief Returns the compound kind (struct, class, union, interface). */
compound_type_kind
DwarfCompoundType::CompoundKind() const
{
	return fCompoundKind;
}


/** @brief Returns the number of registered base classes. */
int32
DwarfCompoundType::CountBaseTypes() const
{
	return fInheritances.CountItems();
}


/** @brief Returns the BaseType at @a index, or @c NULL when out of range. */
BaseType*
DwarfCompoundType::BaseTypeAt(int32 index) const
{
	return fInheritances.ItemAt(index);
}


/** @brief Returns the number of declared data members. */
int32
DwarfCompoundType::CountDataMembers() const
{
	return fDataMembers.CountItems();
}


/** @brief Returns the DataMember at @a index, or @c NULL when out of range. */
DataMember*
DwarfCompoundType::DataMemberAt(int32 index) const
{
	return fDataMembers.ItemAt(index);
}


/** @brief Returns the number of template parameters. */
int32
DwarfCompoundType::CountTemplateParameters() const
{
	return fTemplateParameters.CountItems();
}


/** @brief Returns the TemplateParameter at @a index, or @c NULL when out of
           range. */
TemplateParameter*
DwarfCompoundType::TemplateParameterAt(int32 index) const
{
	return fTemplateParameters.ItemAt(index);
}


/**
 * @brief Resolves the in-memory location of an inherited base subobject.
 *
 * @param _baseType       Inheritance node (must be a DwarfInheritance).
 * @param parentLocation  Location of the enclosing object.
 * @param _location       Out parameter receiving the base subobject's
 *                        location.
 * @retval B_OK         Base location resolved.
 * @retval B_BAD_VALUE  @a _baseType is not a DwarfInheritance.
 * @retval other        Errors from _ResolveDataMemberLocation().
 */
status_t
DwarfCompoundType::ResolveBaseTypeLocation(BaseType* _baseType,
	const ValueLocation& parentLocation, ValueLocation*& _location)
{
	DwarfInheritance* baseType = dynamic_cast<DwarfInheritance*>(_baseType);
	if (baseType == NULL)
		return B_BAD_VALUE;

	return _ResolveDataMemberLocation(baseType->GetDwarfType(),
		baseType->Entry()->Location(), parentLocation, false, _location);
}


/**
 * @brief Resolves the in-memory location of a data member, including
 *        bit-field handling.
 *
 * The shared @c _ResolveDataMemberLocation() does the per-piece offset
 * arithmetic; this method then layers on bit-field accounting using the
 * member's byte size, bit size and bit offset attributes.
 *
 * @param _member         Data member node (must be a DwarfDataMember).
 * @param parentLocation  Location of the enclosing object.
 * @param _location       Out parameter receiving the member's location.
 * @retval B_OK         Location resolved.
 * @retval B_BAD_VALUE  @a _member is not a DwarfDataMember.
 * @retval other        Errors from byte/bit-size evaluation or location
 *                      resolution.
 */
status_t
DwarfCompoundType::ResolveDataMemberLocation(DataMember* _member,
	const ValueLocation& parentLocation, ValueLocation*& _location)
{
	DwarfDataMember* member = dynamic_cast<DwarfDataMember*>(_member);
	if (member == NULL)
		return B_BAD_VALUE;
	DwarfTypeContext* typeContext = TypeContext();

	bool isBitField = true;
	DIEMember* memberEntry = member->Entry();
	// TODO: handle DW_AT_data_bit_offset
	if (!memberEntry->ByteSize()->IsValid()
		&& !memberEntry->BitOffset()->IsValid()
		&& !memberEntry->BitSize()->IsValid()) {
		isBitField = false;
	}

	ValueLocation* location;
	status_t error = _ResolveDataMemberLocation(member->GetDwarfType(),
		member->Entry()->Location(), parentLocation, isBitField, location);
	if (error != B_OK)
		return error;

	// If the member isn't a bit field, we're done.
	if (!isBitField) {
		_location = location;
		return B_OK;
	}

	BReference<ValueLocation> locationReference(location);

	// get the byte size
	target_addr_t byteSize;
	if (memberEntry->ByteSize()->IsValid()) {
		BVariant value;
		error = typeContext->File()->EvaluateDynamicValue(
			typeContext->GetCompilationUnit(),
			typeContext->AddressSize(), typeContext->IsBigEndian(),
			typeContext->SubprogramEntry(), memberEntry->ByteSize(),
			typeContext->TargetInterface(), typeContext->InstructionPointer(),
			typeContext->FramePointer(), value);
		if (error != B_OK)
			return error;
		byteSize = value.ToUInt64();
	} else
		byteSize = ByteSize();

	// get the bit offset
	uint64 bitOffset = 0;
	if (memberEntry->BitOffset()->IsValid()) {
		BVariant value;
		error = typeContext->File()->EvaluateDynamicValue(
			typeContext->GetCompilationUnit(),
			typeContext->AddressSize(), typeContext->IsBigEndian(),
			typeContext->SubprogramEntry(), memberEntry->BitOffset(),
			typeContext->TargetInterface(), typeContext->InstructionPointer(),
			typeContext->FramePointer(), value);
		if (error != B_OK)
			return error;
		bitOffset = value.ToUInt64();
	}

	// get the bit size
	uint64 bitSize = byteSize * 8;
	if (memberEntry->BitSize()->IsValid()) {
		BVariant value;
		error = typeContext->File()->EvaluateDynamicValue(
			typeContext->GetCompilationUnit(),
			typeContext->AddressSize(), typeContext->IsBigEndian(),
			typeContext->SubprogramEntry(), memberEntry->BitSize(),
			typeContext->TargetInterface(), typeContext->InstructionPointer(),
			typeContext->FramePointer(), value);
		if (error != B_OK)
			return error;
		bitSize = value.ToUInt64();
	}

	TRACE_LOCALS("bit field: byte size: %" B_PRIu64 ", bit offset/size: %"
		B_PRIu64 "/%" B_PRIu64 "\n", byteSize, bitOffset, bitSize);

	if (bitOffset + bitSize > byteSize * 8)
		return B_BAD_VALUE;

	// create the bit field value location
	ValueLocation* bitFieldLocation = new(std::nothrow) ValueLocation;
	if (bitFieldLocation == NULL)
		return B_NO_MEMORY;
	BReference<ValueLocation> bitFieldLocationReference(bitFieldLocation, true);

	if (!bitFieldLocation->SetTo(*location, bitOffset, bitSize))
		return B_NO_MEMORY;

	_location = bitFieldLocationReference.Detach();
	return B_OK;
}


/** @brief Returns the underlying DIE entry. */
DIEType*
DwarfCompoundType::GetDIEType() const
{
	return fEntry;
}


/** @brief Appends an inheritance edge and acquires its reference. */
bool
DwarfCompoundType::AddInheritance(DwarfInheritance* inheritance)
{
	if (!fInheritances.AddItem(inheritance))
		return false;

	inheritance->AcquireReference();
	return true;
}


/** @brief Appends a data member and acquires its reference. */
bool
DwarfCompoundType::AddDataMember(DwarfDataMember* member)
{
	if (!fDataMembers.AddItem(member))
		return false;

	member->AcquireReference();
	return true;
}


/** @brief Appends a template parameter and acquires its reference. */
bool
DwarfCompoundType::AddTemplateParameter(DwarfTemplateParameter* parameter)
{
	if (!fTemplateParameters.AddItem(parameter))
		return false;

	parameter->AcquireReference();
	return true;
}


/**
 * @brief Computes a member's value location given its DWARF
 *        DW_AT_data_member_location attribute.
 *
 * Handles three forms: a constant byte offset, a DWARF expression block
 * to evaluate, and a location list pointer. Bit-field members request
 * bit-precision offsets via @a isBitField.
 *
 * @param memberType      Type of the member (drives byte size).
 * @param memberLocation  DW_AT_data_member_location attribute value.
 * @param parentLocation  Location of the enclosing object.
 * @param isBitField      Whether the member is being treated as a bit
 *                        field.
 * @param _location       Out parameter receiving the new ValueLocation;
 *                        reference transferred to caller.
 * @retval B_OK         Location resolved.
 * @retval B_NO_MEMORY  Allocation failure.
 * @retval other        Errors from expression evaluation.
 */
status_t
DwarfCompoundType::_ResolveDataMemberLocation(DwarfType* memberType,
	const MemberLocation* memberLocation,
	const ValueLocation& parentLocation, bool isBitField,
	ValueLocation*& _location)
{
	// create the value location object for the member
	ValueLocation* location = new(std::nothrow) ValueLocation(
		parentLocation.IsBigEndian());
	if (location == NULL)
		return B_NO_MEMORY;
	BReference<ValueLocation> locationReference(location, true);

	switch (memberLocation->attributeClass) {
		case ATTRIBUTE_CLASS_CONSTANT:
		{
			if (isBitField) {
				if (!location->SetTo(parentLocation,
					memberLocation->constant * 8,
					memberType->ByteSize() * 8)) {
					return B_NO_MEMORY;
				}
			} else {
				if (!location->SetToByteOffset(parentLocation,
					memberLocation->constant,
					memberType->ByteSize())) {
					return B_NO_MEMORY;
				}
			}

			break;
		}
		case ATTRIBUTE_CLASS_BLOCK:
		case ATTRIBUTE_CLASS_LOCLISTPTR:
		{
			// The attribute is a location description. Since we need to push
			// the parent object value onto the stack, we require the parent
			// location to be a memory location.
			if (parentLocation.CountPieces() != 1)
				return B_BAD_VALUE;
			const ValuePieceLocation& piece = parentLocation.PieceAt(0);

			if (piece.type != VALUE_PIECE_LOCATION_MEMORY)
				return B_BAD_VALUE;

			// convert member location to location description
			LocationDescription locationDescription;
			if (memberLocation->attributeClass == ATTRIBUTE_CLASS_BLOCK) {
				locationDescription.SetToExpression(
					memberLocation->expression.data,
					memberLocation->expression.length);
			} else {
				locationDescription.SetToLocationList(
					memberLocation->listOffset);
			}

			// evaluate the location description
			status_t error = memberType->ResolveLocation(TypeContext(),
				&locationDescription, piece.address, true, *location);
			if (error != B_OK)
				return error;

			break;
		}
		default:
		{
			// for unions the member location can be omitted -- all members
			// start at the beginning of the parent object
			if (fEntry->Tag() != DW_TAG_union_type)
				return B_BAD_VALUE;

			// since all members start at the same location, set up
			// the location by hand since we don't want the size difference
			// between the overall union and the member being
			// factored into the assigned address.
			ValuePieceLocation piece;
			if (!piece.Copy(parentLocation.PieceAt(0)))
				return B_NO_MEMORY;

			piece.SetSize(memberType->ByteSize());
			if (!location->AddPiece(piece))
				return B_NO_MEMORY;

			break;
		}
	}

	_location = locationReference.Detach();
	return B_OK;
}


// #pragma mark - DwarfArrayType


/** @brief Constructs an array type and acquires the element-type
           reference. */
DwarfArrayType::DwarfArrayType(DwarfTypeContext* typeContext,
	const BString& name, DIEArrayType* entry, DwarfType* baseType)
	:
	DwarfType(typeContext, name, entry),
	fEntry(entry),
	fBaseType(baseType)
{
	fBaseType->AcquireReference();
}


/** @brief Releases dimensions and the element-type reference. */
DwarfArrayType::~DwarfArrayType()
{
	for (int32 i = 0;
		DwarfArrayDimension* dimension = fDimensions.ItemAt(i); i++) {
		dimension->ReleaseReference();
	}

	fBaseType->ReleaseReference();
}


/** @brief Returns the element type. */
Type*
DwarfArrayType::BaseType() const
{
	return fBaseType;
}


/** @brief Returns the number of registered dimensions. */
int32
DwarfArrayType::CountDimensions() const
{
	return fDimensions.CountItems();
}


/** @brief Returns the ArrayDimension at @a index, or @c NULL when out of
           range. */
ArrayDimension*
DwarfArrayType::DimensionAt(int32 index) const
{
	return fDimensions.ItemAt(index);
}


/**
 * @brief Computes the in-memory location of the array element identified
 *        by @a indexPath relative to a parent array location.
 *
 * Honors per-array, per-dimension and per-index-type bit/byte strides as
 * defined by the DWARF spec, falling back to the element type size when
 * not provided.
 *
 * @param indexPath       Multi-dimensional index of the element.
 * @param parentLocation  Location describing the array as a whole.
 * @param _location       Out parameter receiving the element location.
 * @retval B_OK         Element location resolved.
 * @retval B_BAD_VALUE  Index path arity disagrees with dimensions or
 *                      stride is unknown for a non-zero index.
 * @retval B_NO_MEMORY  Allocation failure.
 * @retval other        Errors from stride evaluation.
 */
status_t
DwarfArrayType::ResolveElementLocation(const ArrayIndexPath& indexPath,
	const ValueLocation& parentLocation, ValueLocation*& _location)
{
	if (indexPath.CountIndices() != CountDimensions())
		return B_BAD_VALUE;
	DwarfTypeContext* typeContext = TypeContext();

	// If the array entry has a bit stride, get it. Otherwise fall back to the
	// element type size.
	int64 bitStride;
	DIEArrayType* bitStrideOwnerEntry = NULL;
	if (fEntry != NULL && (bitStrideOwnerEntry = DwarfUtils::GetDIEByPredicate(
			fEntry, HasBitStridePredicate<DIEArrayType>()))) {
		BVariant value;
		status_t error = typeContext->File()->EvaluateDynamicValue(
			typeContext->GetCompilationUnit(),
			typeContext->AddressSize(), typeContext->IsBigEndian(),
			typeContext->SubprogramEntry(), bitStrideOwnerEntry->BitStride(),
			typeContext->TargetInterface(), typeContext->InstructionPointer(),
			typeContext->FramePointer(), value);
		if (error != B_OK)
			return error;
		if (!value.IsInteger())
			return B_BAD_VALUE;
		bitStride = value.ToInt64();
	} else
		bitStride = BaseType()->ByteSize() * 8;

	// Iterate backward through the dimensions and compute the total offset of
	// the element.
	int64 elementOffset = 0;
	DwarfArrayDimension* previousDimension = NULL;
	int64 previousDimensionStride = 0;
	for (int32 dimensionIndex = CountDimensions() - 1;
			dimensionIndex >= 0; dimensionIndex--) {
		DwarfArrayDimension* dimension = DwarfDimensionAt(dimensionIndex);
		int64 index = indexPath.IndexAt(dimensionIndex);

		// If the dimension has a special bit/byte stride, get it.
		int64 dimensionStride = 0;
		DwarfType* dimensionType = dimension->GetDwarfType();
		DIEArrayIndexType* dimensionTypeEntry = dimensionType != NULL
			? dynamic_cast<DIEArrayIndexType*>(dimensionType->GetDIEType())
			: NULL;
		if (dimensionTypeEntry != NULL) {
			DIEArrayIndexType* bitStrideOwnerEntry
				= DwarfUtils::GetDIEByPredicate(dimensionTypeEntry,
					HasBitStridePredicate<DIEArrayIndexType>());
			if (bitStrideOwnerEntry != NULL) {
				BVariant value;
				status_t error = typeContext->File()->EvaluateDynamicValue(
					typeContext->GetCompilationUnit(),
					typeContext->AddressSize(), typeContext->IsBigEndian(),
					typeContext->SubprogramEntry(),
					bitStrideOwnerEntry->BitStride(),
					typeContext->TargetInterface(),
					typeContext->InstructionPointer(),
					typeContext->FramePointer(), value);
				if (error != B_OK)
					return error;
				if (!value.IsInteger())
					return B_BAD_VALUE;
				dimensionStride = value.ToInt64();
			} else {
				DIEArrayIndexType* byteStrideOwnerEntry
					= DwarfUtils::GetDIEByPredicate(dimensionTypeEntry,
						HasByteStridePredicate<DIEArrayIndexType>());
				if (byteStrideOwnerEntry != NULL) {
					BVariant value;
					status_t error = typeContext->File()->EvaluateDynamicValue(
						typeContext->GetCompilationUnit(),
						typeContext->AddressSize(), typeContext->IsBigEndian(),
						typeContext->SubprogramEntry(),
						byteStrideOwnerEntry->ByteStride(),
						typeContext->TargetInterface(),
						typeContext->InstructionPointer(),
						typeContext->FramePointer(), value);
					if (error != B_OK)
						return error;
					if (!value.IsInteger())
						return B_BAD_VALUE;
					dimensionStride = value.ToInt64() * 8;
				}
			}
		}

		// If we don't have a stride for the dimension yet, use the stride of
		// the previous dimension multiplied by the size of the dimension.
		if (dimensionStride == 0) {
			if (previousDimension != NULL) {
				dimensionStride = previousDimensionStride
					* previousDimension->CountElements();
			} else {
				// the last dimension -- use the element bit stride
				dimensionStride = bitStride;
			}
		}

		// If the dimension stride is still 0 (that can happen, if the dimension
		// doesn't have a stride and the previous dimension's element count is
		// not known), we can only resolve the first element.
		if (dimensionStride == 0 && index != 0) {
			WARNING("No dimension bit stride for dimension %" B_PRId32 " and "
				"element index is not 0.\n", dimensionIndex);
			return B_BAD_VALUE;
		}

		elementOffset += dimensionStride * index;

		previousDimension = dimension;
		previousDimensionStride = dimensionStride;
	}

	TRACE_LOCALS("total element bit offset: %" B_PRId64 "\n", elementOffset);

	// create the value location object for the element
	ValueLocation* location = new(std::nothrow) ValueLocation(
		parentLocation.IsBigEndian());
	if (location == NULL)
		return B_NO_MEMORY;
	BReference<ValueLocation> locationReference(location, true);

	// If we have a single memory piece location for the array, we compute the
	// element's location by hand -- not uncommonly the array size isn't known.
	if (parentLocation.CountPieces() == 1) {
		ValuePieceLocation piece;
		if (!piece.Copy(parentLocation.PieceAt(0)))
			return B_NO_MEMORY;

		if (piece.type == VALUE_PIECE_LOCATION_MEMORY) {
			int64 byteOffset = elementOffset >= 0
				? elementOffset / 8 : (elementOffset - 7) / 8;
			piece.SetToMemory(piece.address + byteOffset);
			piece.SetSize(BaseType()->ByteSize());
			// TODO: Support bit offsets correctly!
			// TODO: Support bit fields (primitive types) correctly!

			if (!location->AddPiece(piece))
				return B_NO_MEMORY;

			_location = locationReference.Detach();
			return B_OK;
		}
	}

	// We can't deal with negative element offsets at this point. It doesn't
	// make a lot of sense anyway, if the array location consists of multiple
	// pieces or lives in a register.
	if (elementOffset < 0) {
		WARNING("Negative element offset unsupported for multiple location "
			"pieces or register pieces.\n");
		return B_UNSUPPORTED;
	}

	if (!location->SetTo(parentLocation, elementOffset,
			BaseType()->ByteSize() * 8)) {
		return B_NO_MEMORY;
	}

	_location = locationReference.Detach();
	return B_OK;
}



/** @brief Returns the underlying DIE entry. */
DIEType*
DwarfArrayType::GetDIEType() const
{
	return fEntry;
}


/** @brief Appends a dimension and acquires its reference. */
bool
DwarfArrayType::AddDimension(DwarfArrayDimension* dimension)
{
	if (!fDimensions.AddItem(dimension))
		return false;

	dimension->AcquireReference();
	return true;
}


// #pragma mark - DwarfModifiedType


/** @brief Constructs a modified type and acquires the base-type
           reference. */
DwarfModifiedType::DwarfModifiedType(DwarfTypeContext* typeContext,
	const BString& name, DIEModifiedType* entry, uint32 modifiers,
	DwarfType* baseType)
	:
	DwarfType(typeContext, name, entry),
	fEntry(entry),
	fModifiers(modifiers),
	fBaseType(baseType)
{
	fBaseType->AcquireReference();
}


/** @brief Releases the base-type reference. */
DwarfModifiedType::~DwarfModifiedType()
{
	fBaseType->ReleaseReference();
}


/** @brief Returns the modifier bit mask (const, volatile, ...). */
uint32
DwarfModifiedType::Modifiers() const
{
	return fModifiers;
}


/** @brief Returns the modified base type. */
Type*
DwarfModifiedType::BaseType() const
{
	return fBaseType;
}


/** @brief Returns the underlying DIE entry. */
DIEType*
DwarfModifiedType::GetDIEType() const
{
	return fEntry;
}


// #pragma mark - DwarfTypedefType


/** @brief Constructs a typedef alias and acquires the base-type
           reference. */
DwarfTypedefType::DwarfTypedefType(DwarfTypeContext* typeContext,
	const BString& name, DIETypedef* entry, DwarfType* baseType)
	:
	DwarfType(typeContext, name, entry),
	fEntry(entry),
	fBaseType(baseType)
{
	fBaseType->AcquireReference();
}


/** @brief Releases the base-type reference. */
DwarfTypedefType::~DwarfTypedefType()
{
	fBaseType->ReleaseReference();
}


/** @brief Returns the underlying type the typedef aliases. */
Type*
DwarfTypedefType::BaseType() const
{
	return fBaseType;
}


/** @brief Returns the underlying DIE entry. */
DIEType*
DwarfTypedefType::GetDIEType() const
{
	return fEntry;
}


// #pragma mark - DwarfAddressType


/** @brief Constructs a pointer/reference type and acquires the pointee
           reference. */
DwarfAddressType::DwarfAddressType(DwarfTypeContext* typeContext,
	const BString& name, DIEAddressingType* entry,
	address_type_kind addressKind, DwarfType* baseType)
	:
	DwarfType(typeContext, name, entry),
	fEntry(entry),
	fAddressKind(addressKind),
	fBaseType(baseType)
{
	fBaseType->AcquireReference();
}


/** @brief Releases the pointee-type reference. */
DwarfAddressType::~DwarfAddressType()
{
	fBaseType->ReleaseReference();
}


/** @brief Returns whether this is a pointer or a reference. */
address_type_kind
DwarfAddressType::AddressKind() const
{
	return fAddressKind;
}


/** @brief Returns the pointee/referent type. */
Type*
DwarfAddressType::BaseType() const
{
	return fBaseType;
}


/** @brief Returns the underlying DIE entry. */
DIEType*
DwarfAddressType::GetDIEType() const
{
	return fEntry;
}


// #pragma mark - DwarfEnumerationType


/** @brief Constructs an enumeration type and (optionally) acquires the
           base-type reference. */
DwarfEnumerationType::DwarfEnumerationType(DwarfTypeContext* typeContext,
	const BString& name, DIEEnumerationType* entry, DwarfType* baseType)
	:
	DwarfType(typeContext, name, entry),
	fEntry(entry),
	fBaseType(baseType)
{
	if (fBaseType != NULL)
		fBaseType->AcquireReference();
}


/** @brief Releases enumerator values and (optionally) the base-type
           reference. */
DwarfEnumerationType::~DwarfEnumerationType()
{
	for (int32 i = 0; DwarfEnumeratorValue* value = fValues.ItemAt(i); i++)
		value->ReleaseReference();

	if (fBaseType != NULL)
		fBaseType->ReleaseReference();
}


/** @brief Returns the underlying integer base type, or @c NULL if absent. */
Type*
DwarfEnumerationType::BaseType() const
{
	return fBaseType;
}


/** @brief Returns the number of declared enumerator values. */
int32
DwarfEnumerationType::CountValues() const
{
	return fValues.CountItems();
}


/** @brief Returns the EnumeratorValue at @a index, or @c NULL when out of
           range. */
EnumeratorValue*
DwarfEnumerationType::ValueAt(int32 index) const
{
	return fValues.ItemAt(index);
}


/** @brief Returns the underlying DIE entry. */
DIEType*
DwarfEnumerationType::GetDIEType() const
{
	return fEntry;
}


/** @brief Appends an enumerator value and acquires its reference. */
bool
DwarfEnumerationType::AddValue(DwarfEnumeratorValue* value)
{
	if (!fValues.AddItem(value))
		return false;

	value->AcquireReference();
	return true;
}


// #pragma mark - DwarfSubrangeType


/** @brief Constructs a subrange type with explicit low and high bounds. */
DwarfSubrangeType::DwarfSubrangeType(DwarfTypeContext* typeContext,
	const BString& name, DIESubrangeType* entry, Type* baseType,
	const BVariant& lowerBound, const BVariant& upperBound)
	:
	DwarfType(typeContext, name, entry),
	fEntry(entry),
	fBaseType(baseType),
	fLowerBound(lowerBound),
	fUpperBound(upperBound)
{
	fBaseType->AcquireReference();
}


/** @brief Releases the base-type reference. */
DwarfSubrangeType::~DwarfSubrangeType()
{
	fBaseType->ReleaseReference();
}


/** @brief Returns the integer base type the subrange refines. */
Type*
DwarfSubrangeType::BaseType() const
{
	return fBaseType;
}


/** @brief Returns the underlying DIE entry. */
DIEType*
DwarfSubrangeType::GetDIEType() const
{
	return fEntry;
}


/** @brief Returns the lower bound of the subrange. */
BVariant
DwarfSubrangeType::LowerBound() const
{
	return fLowerBound;
}


/** @brief Returns the upper bound of the subrange. */
BVariant
DwarfSubrangeType::UpperBound() const
{
	return fUpperBound;
}


// #pragma mark - DwarfUnspecifiedType


/** @brief Constructs an unspecified-type placeholder. The DIE entry may
           legitimately be @c NULL. */
DwarfUnspecifiedType::DwarfUnspecifiedType(DwarfTypeContext* typeContext,
	const BString& name, DIEUnspecifiedType* entry)
	:
	DwarfType(typeContext, name, entry),
	fEntry(entry)
{
}


/** @brief Destructor; nothing to release. */
DwarfUnspecifiedType::~DwarfUnspecifiedType()
{
}


/** @brief Returns the underlying DIE entry, or @c NULL when not bound to
           one. */
DIEType*
DwarfUnspecifiedType::GetDIEType() const
{
	return fEntry;
}


// #pragma mark - DwarfFunctionType


/** @brief Constructs a function type and (optionally) acquires the return-
           type reference. */
DwarfFunctionType::DwarfFunctionType(DwarfTypeContext* typeContext,
	const BString& name, DIESubroutineType* entry, DwarfType* returnType)
	:
	DwarfType(typeContext, name, entry),
	fEntry(entry),
	fReturnType(returnType),
	fHasVariableArguments(false)
{
	if (fReturnType != NULL)
		fReturnType->AcquireReference();
}


/** @brief Releases parameters and (optionally) the return-type
           reference. */
DwarfFunctionType::~DwarfFunctionType()
{
	for (int32 i = 0;
		DwarfFunctionParameter* parameter = fParameters.ItemAt(i); i++) {
		parameter->ReleaseReference();
	}

	if (fReturnType != NULL)
		fReturnType->ReleaseReference();
}


/** @brief Returns the return type, or @c NULL for void-returning
           functions. */
Type*
DwarfFunctionType::ReturnType() const
{
	return fReturnType;
}


/** @brief Returns the number of parameters. */
int32
DwarfFunctionType::CountParameters() const
{
	return fParameters.CountItems();
}


/** @brief Returns the FunctionParameter at @a index, or @c NULL when out of
           range. */
FunctionParameter*
DwarfFunctionType::ParameterAt(int32 index) const
{
	return fParameters.ItemAt(index);
}


/** @brief Reports whether the function uses variadic arguments. */
bool
DwarfFunctionType::HasVariableArguments() const
{
	return fHasVariableArguments;
}


/** @brief Sets the variadic flag. */
void
DwarfFunctionType::SetHasVariableArguments(bool hasVarArgs)
{
	fHasVariableArguments = hasVarArgs;
}


/** @brief Returns the underlying DIE entry. */
DIEType*
DwarfFunctionType::GetDIEType() const
{
	return fEntry;
}


/** @brief Appends a parameter and acquires its reference. */
bool
DwarfFunctionType::AddParameter(DwarfFunctionParameter* parameter)
{
	if (!fParameters.AddItem(parameter))
		return false;

	parameter->AcquireReference();
	return true;
}


// #pragma mark - DwarfPointerToMemberType


/** @brief Constructs a pointer-to-member type and acquires references on
           both the containing compound and the pointee type. */
DwarfPointerToMemberType::DwarfPointerToMemberType(
	DwarfTypeContext* typeContext, const BString& name,
	DIEPointerToMemberType* entry, DwarfCompoundType* containingType,
	DwarfType* baseType)
	:
	DwarfType(typeContext, name, entry),
	fEntry(entry),
	fContainingType(containingType),
	fBaseType(baseType)
{
	fContainingType->AcquireReference();
	fBaseType->AcquireReference();
}


/** @brief Releases the containing-type and pointee-type references. */
DwarfPointerToMemberType::~DwarfPointerToMemberType()
{
	fContainingType->ReleaseReference();
	fBaseType->ReleaseReference();
}


/** @brief Returns the compound type the pointer-to-member targets a
           member of. */
CompoundType*
DwarfPointerToMemberType::ContainingType() const
{
	return fContainingType;
}


/** @brief Returns the type of the member the pointer addresses. */
Type*
DwarfPointerToMemberType::BaseType() const
{
	return fBaseType;
}


/** @brief Returns the underlying DIE entry. */
DIEType*
DwarfPointerToMemberType::GetDIEType() const
{
	return fEntry;
}
