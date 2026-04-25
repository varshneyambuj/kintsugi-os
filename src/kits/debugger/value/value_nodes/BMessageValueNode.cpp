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
 *   Copyright 2011-2015, Rene Gollent, rene@gollent.com
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file BMessageValueNode.cpp
 * @brief Implementation of BMessageValueNode -- renders a BMessage with one child per field.
 *
 * The most complex value-node in the debugger: it pulls the BMessage header,
 * field table, and data buffer out of target memory, replays them through
 * BMessage::Unflatten() to recover a working copy in the debugger's address
 * space, and then walks @c GetInfo() to expose one child per named field.
 * Each field child can spawn an internal "field group" node when @c count > 1
 * so multi-value fields appear as expandable arrays.
 *
 * The node also supports a "flat message" mode, where the parent location is
 * itself a BMessage flat buffer rather than a BMessage object; this is used
 * for nested BMessages stored as B_MESSAGE_TYPE field data.
 *
 * @see BMessageTypeHandler, ValueNode
 */


#include "BMessageValueNode.h"

#include <new>

#include <AutoDeleter.h>
#include <MessageAdapter.h>
#include <MessagePrivate.h>

#include "Architecture.h"
#include "StringValue.h"
#include "TeamTypeInformation.h"
#include "Tracing.h"
#include "Type.h"
#include "TypeLookupConstraints.h"
#include "ValueLoader.h"
#include "ValueLocation.h"
#include "ValueNodeContainer.h"


/** @brief Maximum number of bytes shown when rendering a B_STRING_TYPE field. */
static const int64 kMaxStringSize = 64;


// #pragma mark - BMessageWhatNodeChild


/**
 * @brief Pseudo-child that exposes the BMessage's @c what code as a sibling field.
 *
 * Resolves to the @c what data member of the parent BMessage object, or to
 * the second uint32 word inside the flat header when the parent is a flat
 * message.
 */
class BMessageWhatNodeChild : public ValueNodeChild {
public:
	/**
	 * @brief Constructs the @c what pseudo-child.
	 *
	 * @param parent  Owning BMessageValueNode.
	 * @param member  DataMember describing the @c what field (used for the non-flat case).
	 * @param type    Type of the @c what field (typically uint32).
	 */
	BMessageWhatNodeChild(BMessageValueNode* parent, DataMember* member,
		Type* type)
		:
		ValueNodeChild(),
		fMember(member),
		fName("what"),
		fParent(parent),
		fType(type)
	{
		fParent->AcquireReference();
		fType->AcquireReference();
	}

	/**
	 * @brief Releases the references held on the parent and field type.
	 */
	virtual ~BMessageWhatNodeChild()
	{
		fParent->ReleaseReference();
		fType->ReleaseReference();
	}

	/**
	 * @brief Returns the literal display name "what".
	 *
	 * @return Reference to the cached name string.
	 */
	virtual const BString& Name() const
	{
		return fName;
	}

	/**
	 * @brief Returns the @c what field's type.
	 *
	 * @return The cached Type.
	 */
	virtual Type* GetType() const
	{
		return fType;
	}

	/**
	 * @brief Returns the owning BMessageValueNode.
	 *
	 * @return The parent node.
	 */
	virtual ValueNode* Parent() const
	{
		return fParent;
	}

	/**
	 * @brief Computes the location of the @c what field.
	 *
	 * For a flat message, the @c what occupies the second uint32 of the
	 * header (immediately after the format code). Otherwise, the BMessage
	 * compound type knows where to find @c what relative to the object.
	 *
	 * @param valueLoader  Unused.
	 * @param _location    Set to a freshly allocated location on success.
	 * @retval B_OK         On success.
	 * @retval B_NO_MEMORY  On allocation failure.
	 * @return Other status_t propagated from CompoundType::ResolveDataMemberLocation().
	 */
	virtual status_t ResolveLocation(ValueLoader* valueLoader,
		ValueLocation*& _location)
	{
		ValueLocation* parentLocation = fParent->Location();
		ValueLocation* location;
		CompoundType* type = dynamic_cast<CompoundType*>(fParent->GetType());
		status_t error = B_OK;
		if (fParent->fIsFlatMessage) {
			location = new ValueLocation();
			if (location == NULL)
				return B_NO_MEMORY;

			ValuePieceLocation piece;
			piece.SetToMemory(parentLocation->PieceAt(0).address
				+ sizeof(uint32));
			piece.SetSize(sizeof(uint32));
			location->AddPiece(piece);
		} else {
			error = type->ResolveDataMemberLocation(fMember,
				*parentLocation, location);
		}

		if (error != B_OK)
			return error;

		_location = location;
		return B_OK;
	}

private:
	DataMember*			fMember;
	BString				fName;
	BMessageValueNode*	fParent;
	Type*				fType;
};


// #pragma mark - BMessageValueNode


/**
 * @brief Constructs the node and references its DWARF type.
 *
 * @param nodeChild  Child this node renders for.
 * @param type       Compound type for BMessage.
 */
BMessageValueNode::BMessageValueNode(ValueNodeChild* nodeChild,
	Type* type)
	:
	ValueNode(nodeChild),
	fType(type),
	fHeader(NULL),
	fFields(NULL),
	fData(NULL),
	fIsFlatMessage(false)
{
	fType->AcquireReference();
}


/**
 * @brief Releases all children, the type, and the cached header/field/data buffers.
 */
BMessageValueNode::~BMessageValueNode()
{
	fType->ReleaseReference();
	for (int32 i = 0; i < fChildren.CountItems(); i++)
		fChildren.ItemAt(i)->ReleaseReference();

	delete fHeader;
	delete[] fFields;
	delete[] fData;
}


/**
 * @brief Returns the wrapped DWARF type.
 *
 * @return The compound BMessage type.
 */
Type*
BMessageValueNode::GetType() const
{
	return fType;
}


/**
 * @brief Pulls the BMessage's header, field table, and data buffer out of the target.
 *
 * Detects flat-message mode (when the owning child is a BMessageFieldNodeChild)
 * by dynamic_cast; in that case the parent location is the start of a flat
 * BMessage buffer and addresses are computed from offsets. Otherwise, walks
 * the BMessage compound's data members @c fHeader, @c what, @c fFields, and
 * @c fData and reads them via the loader. The complete flat byte stream is
 * then rebuilt and fed through BMessage::Unflatten() so subsequent calls
 * (CreateChildren, _FindField, _FindDataLocation) can use the standard
 * BMessage API.
 *
 * @param valueLoader  Loader used to read target memory.
 * @param _location    Receives a re-referenced copy of the parent location.
 * @param _value       Always set to NULL -- this node has no scalar value.
 * @retval B_OK             On success.
 * @retval B_BAD_VALUE      When the parent location is missing.
 * @retval B_NO_MEMORY      On allocation failure.
 * @retval B_NOT_A_MESSAGE  When the loaded header does not look like a Haiku BMessage.
 * @return Other status_t propagated from the loader or from BMessage::Unflatten().
 */
status_t
BMessageValueNode::ResolvedLocationAndValue(ValueLoader* valueLoader,
	ValueLocation*& _location, Value*& _value)
{
	fIsFlatMessage = dynamic_cast<BMessageFieldNodeChild*>(NodeChild())
		!= NULL;

	// get the location
	ValueLocation* location = NodeChild()->Location();
	if (location == NULL)
		return B_BAD_VALUE;


	// get the value type
	type_code valueType;
	if (valueLoader->GetArchitecture()->AddressSize() == 4) {
		valueType = B_UINT32_TYPE;
		TRACE_LOCALS("    -> 32 bit\n");
	} else {
		valueType = B_UINT64_TYPE;
		TRACE_LOCALS("    -> 64 bit\n");
	}

	// load the value data

	status_t error = B_OK;
	ValueLocation* memberLocation = NULL;

	BVariant headerAddress;
	BVariant fieldAddress;
	BVariant what;

	CompoundType* baseType = dynamic_cast<CompoundType*>(fType);

	if (fIsFlatMessage) {
		headerAddress.SetTo(location->PieceAt(0).address);
		fieldAddress.SetTo(headerAddress.ToUInt64()
			+ sizeof(BMessage::message_header));
	} else {
		for (int32 i = 0; i < baseType->CountDataMembers(); i++) {
			DataMember* member = baseType->DataMemberAt(i);
			if (strcmp(member->Name(), "fHeader") == 0) {
				error = baseType->ResolveDataMemberLocation(member,
					*location, memberLocation);
				BReference<ValueLocation> locationRef(memberLocation, true);
				if (error != B_OK) {
					TRACE_LOCALS(
						"BMessageValueNode::ResolvedLocationAndValue(): "
						"failed to resolve location of header member: %s\n",
						strerror(error));
					return error;
				}

				error = valueLoader->LoadValue(memberLocation, valueType,
					false, headerAddress);
				if (error != B_OK)
					return error;
			} else if (strcmp(member->Name(), "what") == 0) {
				error = baseType->ResolveDataMemberLocation(member,
					*location, memberLocation);
				BReference<ValueLocation> locationRef(memberLocation, true);
				if (error != B_OK) {
					TRACE_LOCALS(
						"BMessageValueNode::ResolvedLocationAndValue(): "
						"failed to resolve location of header member: %s\n",
							strerror(error));
					return error;
				}
				error = valueLoader->LoadValue(memberLocation, B_UINT32_TYPE,
					false, what);
				if (error != B_OK)
					return error;
			} else if (strcmp(member->Name(), "fFields") == 0) {
				error = baseType->ResolveDataMemberLocation(member,
					*location, memberLocation);
				BReference<ValueLocation> locationRef(memberLocation, true);
				if (error != B_OK) {
					TRACE_LOCALS(
						"BMessageValueNode::ResolvedLocationAndValue(): "
						"failed to resolve location of field member: %s\n",
							strerror(error));
					return error;
				}
				error = valueLoader->LoadValue(memberLocation, valueType,
					false, fieldAddress);
				if (error != B_OK)
					return error;
			} else if (strcmp(member->Name(), "fData") == 0) {
				error = baseType->ResolveDataMemberLocation(member,
					*location, memberLocation);
				BReference<ValueLocation> locationRef(memberLocation, true);
				if (error != B_OK) {
					TRACE_LOCALS(
						"BMessageValueNode::ResolvedLocationAndValue(): "
						"failed to resolve location of data member: %s\n",
							strerror(error));
					return error;
				}
				error = valueLoader->LoadValue(memberLocation, valueType,
					false, fDataLocation);
				if (error != B_OK)
					return error;
			}
			memberLocation = NULL;
		}
	}

	fHeader = new(std::nothrow) BMessage::message_header();
	if (fHeader == NULL)
		return B_NO_MEMORY;
	error = valueLoader->LoadRawValue(headerAddress, sizeof(
		BMessage::message_header), fHeader);
	TRACE_LOCALS("BMessage: Header Address: 0x%" B_PRIx64 ", result: %s\n",
		headerAddress.ToUInt64(), strerror(error));
	if (error != B_OK)
		return error;

	if (fHeader->format != MESSAGE_FORMAT_HAIKU
		|| (fHeader->flags & MESSAGE_FLAG_VALID) == 0)
		return B_NOT_A_MESSAGE;

	if (fIsFlatMessage)
		what.SetTo(fHeader->what);
	else
		fHeader->what = what.ToUInt32();

	TRACE_LOCALS("BMessage: what: 0x%" B_PRIx32 ", result: %s\n",
		what.ToUInt32(), strerror(error));

	size_t fieldsSize = fHeader->field_count * sizeof(
		BMessage::field_header);
	if (fIsFlatMessage)
		fDataLocation.SetTo(fieldAddress.ToUInt64() + fieldsSize);

	size_t totalSize = sizeof(BMessage::message_header) + fieldsSize
		+ fHeader->data_size;
	uint8* messageBuffer = new(std::nothrow) uint8[totalSize];
	if (messageBuffer == NULL)
		return B_NO_MEMORY;

	ArrayDeleter<uint8> deleter(messageBuffer);

	memset(messageBuffer, 0, totalSize);
	memcpy(messageBuffer, fHeader, sizeof(BMessage::message_header));
	uint8* tempBuffer = messageBuffer + sizeof(BMessage::message_header);
	if (fieldsSize > 0) {
		fFields = new(std::nothrow)
			BMessage::field_header[fHeader->field_count];
		if (fFields == NULL)
			return B_NO_MEMORY;

		error = valueLoader->LoadRawValue(fieldAddress, fieldsSize,
			fFields);
		TRACE_LOCALS("BMessage: Field Header Address: 0x%" B_PRIx64
			", result: %s\n",	headerAddress.ToUInt64(), strerror(error));
		if (error != B_OK)
			return error;

		fData = new(std::nothrow) uint8[fHeader->data_size];
		if (fData == NULL)
			return B_NO_MEMORY;

		error = valueLoader->LoadRawValue(fDataLocation, fHeader->data_size,
			fData);
		TRACE_LOCALS("BMessage: Data Address: 0x%" B_PRIx64
			", result: %s\n",	fDataLocation.ToUInt64(), strerror(error));
		if (error != B_OK)
			return error;
		memcpy(tempBuffer, fFields, fieldsSize);
		tempBuffer += fieldsSize;
		memcpy(tempBuffer, fData, fHeader->data_size);
	}

	error = fMessage.Unflatten((const char*)messageBuffer);
	if (error != B_OK)
		return error;

	location->AcquireReference();
	_location = location;
	_value = NULL;

	return B_OK;
}


/**
 * @brief Materialises one child for each named field plus the @c what pseudo-child.
 *
 * Walks the BMessage compound type to locate the @c what data member (so a
 * BMessageWhatNodeChild can be allocated for it), then iterates fields via
 * BMessage::GetInfo() to produce one BMessageFieldNodeChild per named field.
 *
 * @param info  Type-information service used to resolve B_*_TYPE codes to
 *              concrete debug types.
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  On allocation failure.
 */
status_t
BMessageValueNode::CreateChildren(TeamTypeInformation* info)
{
	DataMember* member = NULL;
	CompoundType* messageType = dynamic_cast<CompoundType*>(fType);
	for (int32 i = 0; i < messageType->CountDataMembers(); i++) {
		member = messageType->DataMemberAt(i);
		if (strcmp(member->Name(), "what") == 0) {
			ValueNodeChild* whatNode
				= new(std::nothrow) BMessageWhatNodeChild(this, member,
					member->GetType());
			if (whatNode == NULL)
				return B_NO_MEMORY;

			whatNode->SetContainer(fContainer);
			fChildren.AddItem(whatNode);
			break;
		}
	}

	char* name;
	type_code type;
	int32 count;
	Type* fieldType = NULL;
	BReference<Type> typeRef;
	for (int32 i = 0; fMessage.GetInfo(B_ANY_TYPE, i, &name, &type,
		&count) == B_OK; i++) {
		fieldType = NULL;

		_GetTypeForTypeCode(info, type, fieldType);
		if (fieldType != NULL)
			typeRef.SetTo(fieldType, true);

		BMessageFieldNodeChild* node = new(std::nothrow)
			BMessageFieldNodeChild(this,
				fieldType != NULL ? fieldType : fType, name, type,
				count);
		if (node == NULL)
			return B_NO_MEMORY;

		node->SetContainer(fContainer);
		fChildren.AddItem(node);
	}

	fChildrenCreated = true;

	if (fContainer != NULL)
		fContainer->NotifyValueNodeChildrenCreated(this);

	return B_OK;
}


/**
 * @brief Returns the number of currently materialised children.
 *
 * @return Count of children (one per field plus the @c what pseudo-child).
 */
int32
BMessageValueNode::CountChildren() const
{
	return fChildren.CountItems();
}


/**
 * @brief Returns the child at @a index, or NULL if out of range.
 *
 * @param index  Zero-based index.
 * @return The child reference, or NULL.
 */
ValueNodeChild*
BMessageValueNode::ChildAt(int32 index) const
{
	return fChildren.ItemAt(index);
}


/**
 * @brief Maps a BMessage @c type_code to a concrete debug Type.
 *
 * Handles all primitive type codes by name lookup against the type
 * information service. B_STRING_TYPE is special-cased to produce a derived
 * char[kMaxStringSize] array type so the variables view shows a C-style
 * string. Unknown or pointer-like codes resolve to @c void*.
 *
 * @param info   Type-information service.
 * @param type   The BMessage field's type code.
 * @param _type  Set to the resolved Type on success.
 * @return Status of the lookup; B_OK on success.
 */
status_t
BMessageValueNode::_GetTypeForTypeCode(TeamTypeInformation* info,
	type_code type, Type*& _type)
{
	BString typeName;
	TypeLookupConstraints constraints;

	switch(type) {
		case B_BOOL_TYPE:
			typeName = "bool";
			constraints.SetTypeKind(TYPE_PRIMITIVE);
			break;

		case B_INT8_TYPE:
			typeName = "int8";
			constraints.SetTypeKind(TYPE_TYPEDEF);
			break;

		case B_UINT8_TYPE:
			typeName = "uint8";
			constraints.SetTypeKind(TYPE_TYPEDEF);
			break;

		case B_INT16_TYPE:
			typeName = "int16";
			constraints.SetTypeKind(TYPE_TYPEDEF);
			break;

		case B_UINT16_TYPE:
			typeName = "uint16";
			constraints.SetTypeKind(TYPE_TYPEDEF);
			break;

		case B_INT32_TYPE:
			typeName = "int32";
			constraints.SetTypeKind(TYPE_TYPEDEF);
			break;

		case B_UINT32_TYPE:
			typeName = "uint32";
			constraints.SetTypeKind(TYPE_TYPEDEF);
			break;

		case B_INT64_TYPE:
			typeName = "int64";
			constraints.SetTypeKind(TYPE_TYPEDEF);
			break;

		case B_UINT64_TYPE:
			typeName = "uint64";
			constraints.SetTypeKind(TYPE_TYPEDEF);
			break;

		case B_FLOAT_TYPE:
			typeName = "float";
			constraints.SetTypeKind(TYPE_PRIMITIVE);
			break;

		case B_DOUBLE_TYPE:
			typeName = "double";
			constraints.SetTypeKind(TYPE_PRIMITIVE);
			break;

		case B_MESSAGE_TYPE:
			typeName = "BMessage";
			constraints.SetTypeKind(TYPE_COMPOUND);
			break;

		case B_MESSENGER_TYPE:
			typeName = "BMessenger";
			constraints.SetTypeKind(TYPE_COMPOUND);
			break;

		case B_POINT_TYPE:
			typeName = "BPoint";
			constraints.SetTypeKind(TYPE_COMPOUND);
			break;

		case B_RECT_TYPE:
			typeName = "BRect";
			constraints.SetTypeKind(TYPE_COMPOUND);
			break;

		case B_REF_TYPE:
			typeName = "entry_ref";
			constraints.SetTypeKind(TYPE_COMPOUND);
			break;

		case B_NODE_REF_TYPE:
			typeName = "node_ref";
			constraints.SetTypeKind(TYPE_COMPOUND);
			break;

		case B_RGB_COLOR_TYPE:
			typeName = "rgb_color";
			constraints.SetTypeKind(TYPE_COMPOUND);
			break;

		case B_STRING_TYPE:
		{
			typeName = "char";
			constraints.SetTypeKind(TYPE_PRIMITIVE);
			Type* baseType = NULL;
			status_t result = info->LookupTypeByName(typeName, constraints,
				baseType);
			if (result != B_OK)
				return result;
			BReference<Type> typeReference(baseType, true);
			ArrayType* arrayType;
			result = baseType->CreateDerivedArrayType(0, kMaxStringSize, true,
				arrayType);
			if (result == B_OK)
				_type = arrayType;

			return result;
			break;
		}

		case B_POINTER_TYPE:
		default:
			typeName = "void*";
			constraints.SetTypeKind(TYPE_ADDRESS);
			break;
	}

	return info->LookupTypeByName(typeName, constraints, _type);
}


/**
 * @brief Looks up the field-header for a name/type pair using the cached header's hash table.
 *
 * Mirrors BMessage's own internal lookup so the debugger can compute byte
 * offsets without re-running BMessage code on the cached buffer.
 *
 * @param name    Field name.
 * @param type    Expected field type, or B_ANY_TYPE.
 * @param result  Set to the matching field_header on success.
 * @retval B_OK              On a match.
 * @retval B_BAD_VALUE       When @a name is NULL.
 * @retval B_NO_INIT         When the header has not been loaded yet.
 * @retval B_NAME_NOT_FOUND  When no field with that name exists.
 * @retval B_BAD_TYPE        When the matching field has a different type than @a type.
 */
status_t
BMessageValueNode::_FindField(const char* name, type_code type,
	BMessage::field_header** result) const
{
	if (name == NULL)
		return B_BAD_VALUE;

	if (fHeader == NULL)
		return B_NO_INIT;

	if (fHeader->field_count == 0 || fFields == NULL || fData == NULL)
		return B_NAME_NOT_FOUND;

	uint32 hash = _HashName(name) % fHeader->hash_table_size;
	int32 nextField = fHeader->hash_table[hash];

	while (nextField >= 0) {
		BMessage::field_header* field = &fFields[nextField];
		if ((field->flags & FIELD_FLAG_VALID) == 0)
			break;

		if (strncmp((const char*)(fData + field->offset), name,
			field->name_length) == 0) {
			if (type != B_ANY_TYPE && field->type != type)
				return B_BAD_TYPE;

			*result = field;
			return B_OK;
		}

		nextField = field->next_field;
	}

	return B_NAME_NOT_FOUND;
}


/**
 * @brief Computes the BMessage hash of @a name (matches private BMessage code).
 *
 * @param name  NUL-terminated field name.
 * @return Hash value used to probe the field hash table.
 */
uint32
BMessageValueNode::_HashName(const char* name) const
{
	char ch;
	uint32 result = 0;

	while ((ch = *name++) != 0) {
		result = (result << 7) ^ (result >> 24);
		result ^= ch;
	}

	result ^= result << 12;
	return result;
}


/**
 * @brief Builds an in-target ValueLocation for a particular field/index payload byte range.
 *
 * Handles both fixed-size fields (offsets advance by @c data_size / count)
 * and variable-size fields (each element is preceded by a uint32 length
 * prefix, walked element-by-element).
 *
 * @param name      Field name.
 * @param type      Field type, or B_ANY_TYPE.
 * @param index     Element index within the field.
 * @param location  Cleared and populated with one memory piece on success.
 * @retval B_OK              On success.
 * @retval B_BAD_INDEX       When @a index is out of range.
 * @retval B_NAME_NOT_FOUND  When the field is missing.
 * @retval B_BAD_TYPE        When the field's type does not match @a type.
 */
status_t
BMessageValueNode::_FindDataLocation(const char* name, type_code type,
	int32 index, ValueLocation& location) const
{
	BMessage::field_header* field = NULL;
	int32 offset = 0;
	int32 size = 0;
	status_t result = _FindField(name, type, &field);
	if (result != B_OK)
		return result;

	if (index < 0 || (uint32)index >= field->count)
		return B_BAD_INDEX;

	if ((field->flags & FIELD_FLAG_FIXED_SIZE) != 0) {
		size = field->data_size / field->count;
		offset = field->offset + field->name_length + index * size;
	} else {
		offset = field->offset + field->name_length;
		uint8 *pointer = fData + field->offset + field->name_length;
		for (int32 i = 0; i < index; i++) {
			pointer += *(uint32*)pointer + sizeof(uint32);
			offset += *(uint32*)pointer + sizeof(uint32);
		}

		size = *(uint32*)pointer;
		offset += sizeof(uint32);
	}

	ValuePieceLocation piece;
	piece.SetToMemory(fDataLocation.ToUInt64() + offset);
	piece.SetSize(size);
	location.Clear();
	location.AddPiece(piece);

	return B_OK;
}


// #pragma mark - BMessageValueNode::BMessageFieldNode


/**
 * @brief Constructs the inner node that groups multi-element field children.
 *
 * Used when a single BMessage field carries multiple values; the inner node
 * presents one indexed child per element.
 *
 * @param child   Owning BMessageFieldNodeChild.
 * @param parent  Top-level BMessageValueNode.
 * @param name    Field name.
 * @param type    Field type.
 * @param count   Number of elements in the field.
 */
BMessageValueNode::BMessageFieldNode::BMessageFieldNode(
	BMessageFieldNodeChild *child, BMessageValueNode* parent,
	const BString &name, type_code type, int32 count)
	:
	ValueNode(child),
	fName(name),
	fType(parent->GetType()),
	fParent(parent),
	fFieldType(type),
	fFieldCount(count)
{
	fParent->AcquireReference();
	fType->AcquireReference();
}


/**
 * @brief Releases the references held on the parent and BMessage type.
 */
BMessageValueNode::BMessageFieldNode::~BMessageFieldNode()
{
	fParent->ReleaseReference();
	fType->ReleaseReference();
}


/**
 * @brief Returns the BMessage compound type from the parent.
 *
 * @return The DWARF BMessage type.
 */
Type*
BMessageValueNode::BMessageFieldNode::GetType() const
{
	return fType;
}


/**
 * @brief Materialises one indexed child per element of the underlying field.
 *
 * @param info  Type-information service used to resolve the field's element type.
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  On allocation failure.
 * @return Status_t propagated from BMessageValueNode::_GetTypeForTypeCode().
 */
status_t
BMessageValueNode::BMessageFieldNode::CreateChildren(TeamTypeInformation* info)
{
	Type* type = NULL;
	status_t error = fParent->_GetTypeForTypeCode(info, fFieldType, type);
	if (error != B_OK)
		return error;

	BReference<Type> typeRef(type, true);
	for (int32 i = 0; i < fFieldCount; i++) {
		BMessageFieldNodeChild* child = new(std::nothrow)
			BMessageFieldNodeChild(fParent, type, fName, fFieldType,
				fFieldCount, i);

		if (child == NULL)
			return B_NO_MEMORY;

		if (fContainer != NULL)
			child->SetContainer(fContainer);

		fChildren.AddItem(child);
	}

	fChildrenCreated = true;

	if (fContainer != NULL)
		fContainer->NotifyValueNodeChildrenCreated(this);

	return B_OK;
}


/**
 * @brief Returns the number of indexed element children.
 *
 * @return Count of children (one per element).
 */
int32
BMessageValueNode::BMessageFieldNode::CountChildren() const
{
	return fChildren.CountItems();
}

/**
 * @brief Returns the indexed child at @a index, or NULL if out of range.
 *
 * @param index  Zero-based index.
 * @return The child reference, or NULL.
 */
ValueNodeChild*
BMessageValueNode::BMessageFieldNode::ChildAt(int32 index) const
{
	return fChildren.ItemAt(index);
}


/**
 * @brief Inner field-group node has no scalar value of its own.
 *
 * @param loader     Unused.
 * @param _location  Set to NULL.
 * @param _value     Set to NULL.
 * @retval B_OK  Always.
 */
status_t
BMessageValueNode::BMessageFieldNode::ResolvedLocationAndValue(
	ValueLoader* loader, ValueLocation *& _location, Value*& _value)
{
	_location = NULL;
	_value = NULL;

	return B_OK;
}


// #pragma mark - BMessageValueNode::BMessageFieldNodeChild


/**
 * @brief Constructs a BMessage field child.
 *
 * Two roles depending on @a count and @a index:
 * - When @c count > 1 and @c index < 0: this child wraps a multi-element
 *   field and spawns an internal BMessageFieldNode.
 * - When @c index >= 0: this child wraps a single element of a multi-element
 *   field; the display name becomes "[index]".
 *
 * @param parent     Top-level BMessageValueNode.
 * @param nodeType   Type of the field (or its element type).
 * @param name       Field name.
 * @param type       Field type code.
 * @param count      Element count for the field.
 * @param index      Element index, or -1 for the field-group itself.
 */
BMessageValueNode::BMessageFieldNodeChild::BMessageFieldNodeChild(
	BMessageValueNode* parent, Type* nodeType, const BString &name,
	type_code type, int32 count, int32 index)
	:
	ValueNodeChild(),
	fName(name),
	fPresentationName(name),
	fType(nodeType),
	fParent(parent),
	fFieldType(type),
	fFieldCount(count),
	fFieldIndex(index)
{
	fParent->AcquireReference();
	fType->AcquireReference();

	if (fFieldIndex >= 0)
		fPresentationName.SetToFormat("[%" B_PRId32 "]", fFieldIndex);
}


/**
 * @brief Releases the references held on the parent and field type.
 */
BMessageValueNode::BMessageFieldNodeChild::~BMessageFieldNodeChild()
{
	fParent->ReleaseReference();
	fType->ReleaseReference();
}


/**
 * @brief Returns the user-visible name (field name or "[index]").
 *
 * @return Reference to the cached presentation name.
 */
const BString&
BMessageValueNode::BMessageFieldNodeChild::Name() const
{
	return fPresentationName;
}


/**
 * @brief Returns the field's element type.
 *
 * @return The cached Type.
 */
Type*
BMessageValueNode::BMessageFieldNodeChild::GetType() const
{
	return fType;
}


/**
 * @brief Returns the top-level BMessageValueNode.
 *
 * @return The parent node.
 */
ValueNode*
BMessageValueNode::BMessageFieldNodeChild::Parent() const
{
	return fParent;
}


/**
 * @brief Reports whether this child needs an internal field-group node.
 *
 * @return true when the field has more than one element and the child is not
 *         already an indexed element.
 */
bool
BMessageValueNode::BMessageFieldNodeChild::IsInternal() const
{
	return fFieldCount > 1 && fFieldIndex == -1;
}


/**
 * @brief Allocates a BMessageFieldNode to expose multi-element fields.
 *
 * @param _node  Set to the freshly allocated node on success.
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  On allocation failure.
 */
status_t
BMessageValueNode::BMessageFieldNodeChild::CreateInternalNode(
	ValueNode*& _node)
{
	BMessageFieldNode* node = new(std::nothrow)
		BMessageFieldNode(this, fParent, fName, fFieldType, fFieldCount);
	if (node == NULL)
		return B_NO_MEMORY;

	_node = node;
	return B_OK;
}


/**
 * @brief Computes the in-target byte range for this field/element.
 *
 * Defers to BMessageValueNode::_FindDataLocation() with index 0 when the
 * child wraps a multi-element field as a whole, otherwise uses fFieldIndex.
 *
 * @param valueLoader  Unused.
 * @param _location    Set to a freshly allocated ValueLocation on success.
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  On allocation failure.
 * @return Other status_t propagated from _FindDataLocation().
 */
status_t
BMessageValueNode::BMessageFieldNodeChild::ResolveLocation(
	ValueLoader* valueLoader, ValueLocation*& _location)
{
	_location = new(std::nothrow)ValueLocation();

	if (_location == NULL)
		return B_NO_MEMORY;

	return fParent->_FindDataLocation(fName, fFieldType, fFieldIndex >= 0
		? fFieldIndex : 0, *_location);
}


