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
 *   Copyright 2011, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold
 *       Rene Gollent
 */

/** @file Variant.cpp
 *  @brief BVariant implementation: a discriminated union capable of holding
 *         any of the standard BeAPI scalar types, strings, rects, and
 *         referenceable objects, with conversions between them.
 */


#include <Variant.h>

#include <stdlib.h>
#include <string.h>

#include <ByteOrder.h>
#include <Message.h>


/** @brief Internal helper template that converts the stored value to @p NumberType.
 *
 *  Returns zero for non-numeric types.
 *
 *  @tparam NumberType  The target numeric type (int8, uint64, float, double, …).
 *  @return The stored value cast to @p NumberType, or 0 for incompatible types.
 */
template<typename NumberType>
inline NumberType
BVariant::_ToNumber() const
{
	switch (fType) {
		case B_BOOL_TYPE:
			return fBool ? 1 : 0;
		case B_INT8_TYPE:
			return (NumberType)fInt8;
		case B_UINT8_TYPE:
			return (NumberType)fUInt8;
		case B_INT16_TYPE:
			return (NumberType)fInt16;
		case B_UINT16_TYPE:
			return (NumberType)fUInt16;
		case B_INT32_TYPE:
			return (NumberType)fInt32;
		case B_UINT32_TYPE:
			return (NumberType)fUInt32;
		case B_INT64_TYPE:
			return (NumberType)fInt64;
		case B_UINT64_TYPE:
			return (NumberType)fUInt64;
		case B_FLOAT_TYPE:
			return (NumberType)fFloat;
		case B_DOUBLE_TYPE:
			return (NumberType)fDouble;
		default:
			return 0;
	}
}


/** @brief Destroys the variant, releasing any owned resources. */
BVariant::~BVariant()
{
	Unset();
}


/** @brief Initialises the variant from a raw typed data buffer.
 *
 *  Interprets @p data according to @p type. Strings are duplicated unless
 *  the caller sets B_VARIANT_DONT_COPY_DATA flags via _SetTo().
 *
 *  @param data  Pointer to the raw value bytes.
 *  @param type  BeAPI type code identifying the data format.
 *  @return B_OK on success, B_BAD_TYPE for unsupported types, B_NO_MEMORY
 *          if string duplication fails.
 */
status_t
BVariant::SetToTypedData(const void* data, type_code type)
{
	Unset();

	switch (type) {
		case B_BOOL_TYPE:
			fBool = *(bool*)data;
			break;
		case B_INT8_TYPE:
			fInt8 = *(int8*)data;
			break;
		case B_UINT8_TYPE:
			fUInt8 = *(uint8*)data;
			break;
		case B_INT16_TYPE:
			fInt16 = *(int16*)data;
			break;
		case B_UINT16_TYPE:
			fUInt16 = *(uint16*)data;
			break;
		case B_INT32_TYPE:
			fInt32 = *(int32*)data;
			break;
		case B_UINT32_TYPE:
			fUInt32 = *(uint32*)data;
			break;
		case B_INT64_TYPE:
			fInt64 = *(int64*)data;
			break;
		case B_UINT64_TYPE:
			fUInt64 = *(uint64*)data;
			break;
		case B_FLOAT_TYPE:
			fFloat = *(float*)data;
			break;
		case B_DOUBLE_TYPE:
			fDouble = *(double*)data;
			break;
		case B_POINTER_TYPE:
			fPointer = *(void**)data;
			break;
		case B_STRING_TYPE:
			return _SetTo((const char*)data, 0) ? B_OK : B_NO_MEMORY;
		case B_RECT_TYPE:
		{
			BRect *rect = (BRect *)data;
			_SetTo(rect->left, rect->top, rect->right, rect->bottom);
			break;
		}
		default:
			return B_BAD_TYPE;
	}

	fType = type;
	return B_OK;
}


/** @brief Resets the variant to an empty/unset state.
 *
 *  Frees any owned string memory and releases any held BReferenceable
 *  reference before zeroing the type and flags fields.
 */
void
BVariant::Unset()
{
	if ((fFlags & B_VARIANT_OWNS_DATA) != 0) {
		switch (fType) {
			case B_STRING_TYPE:
				free(fString);
				break;
			default:
				break;
		}
	} else if ((fFlags & B_VARIANT_REFERENCEABLE_DATA) != 0) {
		if (fReferenceable != NULL)
			fReferenceable->ReleaseReference();
	}

	fType = 0;
	fFlags = 0;
}


/** @brief Compares two BVariant values for equality.
 *
 *  Numeric types are compared by value after conversion; strings by strcmp;
 *  pointers by address; rects component-wise. An unset variant (fType == 0)
 *  is equal only to another unset variant.
 *
 *  @param other  The variant to compare against.
 *  @return true if the values are considered equal.
 */
bool
BVariant::operator==(const BVariant& other) const
{
	if (fType == 0)
		return other.fType == 0;
	if (other.fType == 0)
		return false;

	// TODO: The number comparisons are not really accurate. Particularly a
	// conversion between signed and unsigned integers might actually change the
	// value.

	switch (fType) {
		case B_BOOL_TYPE:
			return fBool == other.ToBool();
		case B_INT8_TYPE:
		case B_INT16_TYPE:
		case B_INT32_TYPE:
		case B_INT64_TYPE:
			if (!other.IsNumber())
				return false;
			return ToInt64() == other.ToInt64();
		case B_UINT8_TYPE:
		case B_UINT16_TYPE:
		case B_UINT32_TYPE:
		case B_UINT64_TYPE:
			if (!other.IsNumber())
				return false;
			return ToUInt64() == other.ToUInt64();
		case B_FLOAT_TYPE:
		case B_DOUBLE_TYPE:
			if (!other.IsNumber())
				return false;
			return ToDouble() == other.ToDouble();
		case B_POINTER_TYPE:
			return other.fType == B_POINTER_TYPE
				&& fPointer == other.fPointer;
		case B_STRING_TYPE:
			if (other.fType != B_STRING_TYPE)
				return false;
			if (fString == NULL || other.fString == NULL)
				return fString == other.fString;
			return strcmp(fString, other.fString) == 0;
		case B_RECT_TYPE:
			return BRect(fRect.left, fRect.top, fRect.right, fRect.bottom)
				== BRect(other.fRect.left, other.fRect.top, other.fRect.right,
					other.fRect.bottom);
		default:
			return false;
	}
}


/** @brief Returns the size in bytes of the stored value.
 *
 *  For strings this is strlen + 1. For referenceable types it is the size of
 *  the pointer. For all others SizeOfType() is used.
 *
 *  @return The byte size of the stored data.
 */
size_t
BVariant::Size() const
{
	if (fType == B_STRING_TYPE)
		return fString != NULL ? strlen(fString) + 1 : 0;
	if ((fFlags & B_VARIANT_REFERENCEABLE_DATA) != 0)
		return sizeof(this->fReferenceable);
	return SizeOfType(fType);
}


/** @brief Returns a pointer to the raw bytes of the stored value.
 *
 *  For strings this is the character buffer. For all others it is fBytes.
 *
 *  @return Pointer to the internal byte representation.
 */
const uint8*
BVariant::Bytes() const
{
	if (fType == B_STRING_TYPE)
		return (const uint8*)fString;
	return fBytes;
}


/** @brief Converts the stored value to bool.
 *
 *  Numbers are non-zero; pointers are non-NULL; strings are non-NULL.
 *
 *  @return The boolean interpretation of the stored value.
 */
bool
BVariant::ToBool() const
{
	switch (fType) {
		case B_BOOL_TYPE:
			return fBool;
		case B_INT8_TYPE:
			return fInt8 != 0;
		case B_UINT8_TYPE:
			return fUInt8 != 0;
		case B_INT16_TYPE:
			return fInt16 != 0;
		case B_UINT16_TYPE:
			return fUInt16 != 0;
		case B_INT32_TYPE:
			return fInt32 != 0;
		case B_UINT32_TYPE:
			return fUInt32 != 0;
		case B_INT64_TYPE:
			return fInt64 != 0;
		case B_UINT64_TYPE:
			return fUInt64 != 0;
		case B_FLOAT_TYPE:
			return fFloat != 0;
		case B_DOUBLE_TYPE:
			return fDouble != 0;
		case B_POINTER_TYPE:
			return fPointer != NULL;
		case B_STRING_TYPE:
			return fString != NULL;
				// TODO: We should probably check for actual values like "true",
				// "false", "on", "off", etc.
		default:
			return false;
	}
}


/** @brief Converts the stored numeric value to int8.
 *  @return The value as int8, or 0 for non-numeric types.
 */
int8
BVariant::ToInt8() const
{
	return _ToNumber<int8>();
}


/** @brief Converts the stored numeric value to uint8.
 *  @return The value as uint8, or 0 for non-numeric types.
 */
uint8
BVariant::ToUInt8() const
{
	return _ToNumber<uint8>();
}


/** @brief Converts the stored numeric value to int16.
 *  @return The value as int16, or 0 for non-numeric types.
 */
int16
BVariant::ToInt16() const
{
	return _ToNumber<int16>();
}


/** @brief Converts the stored numeric value to uint16.
 *  @return The value as uint16, or 0 for non-numeric types.
 */
uint16
BVariant::ToUInt16() const
{
	return _ToNumber<uint16>();
}


/** @brief Converts the stored numeric value to int32.
 *  @return The value as int32, or 0 for non-numeric types.
 */
int32
BVariant::ToInt32() const
{
	return _ToNumber<int32>();
}


/** @brief Converts the stored numeric value to uint32.
 *  @return The value as uint32, or 0 for non-numeric types.
 */
uint32
BVariant::ToUInt32() const
{
	return _ToNumber<uint32>();
}


/** @brief Converts the stored numeric value to int64.
 *  @return The value as int64, or 0 for non-numeric types.
 */
int64
BVariant::ToInt64() const
{
	return _ToNumber<int64>();
}


/** @brief Converts the stored numeric value to uint64.
 *  @return The value as uint64, or 0 for non-numeric types.
 */
uint64
BVariant::ToUInt64() const
{
	return _ToNumber<uint64>();
}


/** @brief Converts the stored numeric value to float.
 *  @return The value as float, or 0 for non-numeric types.
 */
float
BVariant::ToFloat() const
{
	return _ToNumber<float>();
}


/** @brief Converts the stored numeric value to double.
 *  @return The value as double, or 0 for non-numeric types.
 */
double
BVariant::ToDouble() const
{
	return _ToNumber<double>();
}


/** @brief Returns the stored value as a BRect.
 *
 *  Only meaningful when fType == B_RECT_TYPE.
 *
 *  @return A BRect constructed from the stored left/top/right/bottom fields.
 */
BRect
BVariant::ToRect() const
{
	return BRect(fRect.left, fRect.top, fRect.right, fRect.bottom);
}


/** @brief Returns the stored pointer value, or NULL for non-pointer types.
 *  @return The void* value if fType == B_POINTER_TYPE, otherwise NULL.
 */
void*
BVariant::ToPointer() const
{
	return fType == B_POINTER_TYPE ? fString : NULL;
}


/** @brief Returns the stored C-string, or NULL for non-string types.
 *  @return Pointer to the internal character buffer, or NULL.
 */
const char*
BVariant::ToString() const
{
	return fType == B_STRING_TYPE ? fString : NULL;
}


/** @brief Copies another BVariant's value into this object.
 *
 *  For owned strings a strdup() copy is made. For referenceable types the
 *  reference count is incremented. For all other types a raw memcpy is used.
 *
 *  @param other  The source BVariant.
 */
void
BVariant::_SetTo(const BVariant& other)
{
	if ((other.fFlags & B_VARIANT_OWNS_DATA) != 0) {
		switch (other.fType) {
			case B_STRING_TYPE:
				fType = B_STRING_TYPE;
				fString = strdup(other.fString);
				fFlags = B_VARIANT_OWNS_DATA;
				return;
			default:
				break;
		}
	} else if ((other.fFlags & B_VARIANT_REFERENCEABLE_DATA) != 0) {
		if (other.fReferenceable != NULL)
			other.fReferenceable->AcquireReference();
	}

	memcpy((void*)this, (void*)&other, sizeof(BVariant));
}


/** @brief Returns the stored BReferenceable pointer, or NULL.
 *
 *  Only meaningful when the B_VARIANT_REFERENCEABLE_DATA flag is set.
 *
 *  @return Pointer to the referenced object, or NULL.
 */
BReferenceable*
BVariant::ToReferenceable() const
{
	return (fFlags & B_VARIANT_REFERENCEABLE_DATA) != 0
		? fReferenceable : NULL;
}


/** @brief Byte-swaps the stored numeric value to the opposite endianness.
 *
 *  Does nothing for non-numeric types or pointer types.
 */
void
BVariant::SwapEndianess()
{
	if (!IsNumber() || fType == B_POINTER_TYPE)
		return;

	swap_data(fType, fBytes, Size(), B_SWAP_ALWAYS);
}


/** @brief Adds the stored value as a field to @p message under @p fieldName.
 *
 *  @param message    The BMessage to add the field to.
 *  @param fieldName  The name of the new field.
 *  @return B_OK on success, B_UNSUPPORTED for unsupported types, or another error.
 */
status_t
BVariant::AddToMessage(BMessage& message, const char* fieldName) const
{
	switch (fType) {
		case B_BOOL_TYPE:
			return message.AddBool(fieldName, fBool);
		case B_INT8_TYPE:
			return message.AddInt8(fieldName, fInt8);
		case B_UINT8_TYPE:
			return message.AddUInt8(fieldName, fUInt8);
		case B_INT16_TYPE:
			return message.AddInt16(fieldName, fInt16);
		case B_UINT16_TYPE:
			return message.AddUInt16(fieldName, fUInt16);
		case B_INT32_TYPE:
			return message.AddInt32(fieldName, fInt32);
		case B_UINT32_TYPE:
			return message.AddUInt32(fieldName, fUInt32);
		case B_INT64_TYPE:
			return message.AddInt64(fieldName, fInt64);
		case B_UINT64_TYPE:
			return message.AddUInt64(fieldName, fUInt64);
		case B_FLOAT_TYPE:
			return message.AddFloat(fieldName, fFloat);
		case B_DOUBLE_TYPE:
			return message.AddDouble(fieldName, fDouble);
		case B_POINTER_TYPE:
			return message.AddPointer(fieldName, fPointer);
		case B_STRING_TYPE:
			return message.AddString(fieldName, fString);
		case B_RECT_TYPE:
			return message.AddRect(fieldName, BRect(fRect.left, fRect.top,
				fRect.right, fRect.bottom));
		default:
			return B_UNSUPPORTED;
	}
}


/** @brief Initialises this variant from a named field in @p message.
 *
 *  Retrieves the type and data of @p fieldName from @p message and delegates
 *  to SetToTypedData().
 *
 *  @param message    The source BMessage.
 *  @param fieldName  The field name to read.
 *  @return B_OK on success, or an error code if the field is not found or
 *          the type is unsupported.
 */
status_t
BVariant::SetFromMessage(const BMessage& message, const char* fieldName)
{
	// get the message field info
	type_code type;
	int32 count;
	status_t error = message.GetInfo(fieldName, &type, &count);
	if (error != B_OK)
		return error;

	// get the data
	const void* data;
	ssize_t numBytes;
	error = message.FindData(fieldName, type, &data, &numBytes);
	if (error != B_OK)
		return error;

	// init the object
	return SetToTypedData(data, type);
}


/** @brief Returns the byte size of a given BeAPI type code.
 *
 *  @param type  A BeAPI type code (B_INT32_TYPE, B_FLOAT_TYPE, …).
 *  @return The number of bytes needed to store a value of that type, or 0
 *          for unknown types.
 */
/*static*/ size_t
BVariant::SizeOfType(type_code type)
{
	switch (type) {
		case B_BOOL_TYPE:
			return 1;
		case B_INT8_TYPE:
			return 1;
		case B_UINT8_TYPE:
			return 1;
		case B_INT16_TYPE:
			return 2;
		case B_UINT16_TYPE:
			return 2;
		case B_INT32_TYPE:
			return 4;
		case B_UINT32_TYPE:
			return 4;
		case B_INT64_TYPE:
			return 8;
		case B_UINT64_TYPE:
			return 8;
		case B_FLOAT_TYPE:
			return sizeof(float);
		case B_DOUBLE_TYPE:
			return sizeof(double);
		case B_POINTER_TYPE:
			return sizeof(void*);
		case B_RECT_TYPE:
			return sizeof(BRect);
		default:
			return 0;
	}
}


/** @brief Returns whether the given type code represents a numeric type.
 *
 *  Numeric types include all integer and floating-point types. Bool and
 *  pointer are NOT considered numeric.
 *
 *  @param type  A BeAPI type code.
 *  @return true if @p type is a numeric type, false otherwise.
 */
/*static*/ bool
BVariant::TypeIsNumber(type_code type)
{
	switch (type) {
		case B_INT8_TYPE:
		case B_UINT8_TYPE:
		case B_INT16_TYPE:
		case B_UINT16_TYPE:
		case B_INT32_TYPE:
		case B_UINT32_TYPE:
		case B_INT64_TYPE:
		case B_UINT64_TYPE:
		case B_FLOAT_TYPE:
		case B_DOUBLE_TYPE:
			return true;
		default:
			return false;
	}
}


/** @brief Returns whether the given type code is an integer type.
 *
 *  @param type      A BeAPI type code.
 *  @param _isSigned Optional output; set to true for signed types, false for unsigned.
 *  @return true if @p type is an integer type, false otherwise.
 */
/*static*/ bool
BVariant::TypeIsInteger(type_code type, bool* _isSigned)
{
	switch (type) {
		case B_INT8_TYPE:
		case B_INT16_TYPE:
		case B_INT32_TYPE:
		case B_INT64_TYPE:
			if (_isSigned != NULL)
				*_isSigned = true;
			return true;
		case B_UINT8_TYPE:
		case B_UINT16_TYPE:
		case B_UINT32_TYPE:
		case B_UINT64_TYPE:
			if (_isSigned != NULL)
				*_isSigned = false;
			return true;
		default:
			return false;
	}
}


/** @brief Returns whether the given type code is a floating-point type.
 *
 *  @param type  A BeAPI type code.
 *  @return true for B_FLOAT_TYPE and B_DOUBLE_TYPE, false for all others.
 */
/*static*/ bool
BVariant::TypeIsFloat(type_code type)
{
	switch (type) {
		case B_FLOAT_TYPE:
		case B_DOUBLE_TYPE:
			return true;
		default:
			return false;
	}
}


/** @brief Stores a bool value.
 *  @param value  The boolean value to store.
 */
void
BVariant::_SetTo(bool value)
{
	fType = B_BOOL_TYPE;
	fFlags = 0;
	fBool = value;
}


/** @brief Stores an int8 value.
 *  @param value  The int8 value to store.
 */
void
BVariant::_SetTo(int8 value)
{
	fType = B_INT8_TYPE;
	fFlags = 0;
	fInt8 = value;
}


/** @brief Stores a uint8 value.
 *  @param value  The uint8 value to store.
 */
void
BVariant::_SetTo(uint8 value)
{
	fType = B_UINT8_TYPE;
	fFlags = 0;
	fUInt8 = value;
}


/** @brief Stores an int16 value.
 *  @param value  The int16 value to store.
 */
void
BVariant::_SetTo(int16 value)
{
	fType = B_INT16_TYPE;
	fFlags = 0;
	fInt16 = value;
}


/** @brief Stores a uint16 value.
 *  @param value  The uint16 value to store.
 */
void
BVariant::_SetTo(uint16 value)
{
	fType = B_UINT16_TYPE;
	fFlags = 0;
	fUInt16 = value;
}


/** @brief Stores an int32 value.
 *  @param value  The int32 value to store.
 */
void
BVariant::_SetTo(int32 value)
{
	fType = B_INT32_TYPE;
	fFlags = 0;
	fInt32 = value;
}


/** @brief Stores a uint32 value.
 *  @param value  The uint32 value to store.
 */
void
BVariant::_SetTo(uint32 value)
{
	fType = B_UINT32_TYPE;
	fFlags = 0;
	fUInt32 = value;
}


/** @brief Stores an int64 value.
 *  @param value  The int64 value to store.
 */
void
BVariant::_SetTo(int64 value)
{
	fType = B_INT64_TYPE;
	fFlags = 0;
	fInt64 = value;
}


/** @brief Stores a uint64 value.
 *  @param value  The uint64 value to store.
 */
void
BVariant::_SetTo(uint64 value)
{
	fType = B_UINT64_TYPE;
	fFlags = 0;
	fUInt64 = value;
}


/** @brief Stores a float value.
 *  @param value  The float value to store.
 */
void
BVariant::_SetTo(float value)
{
	fType = B_FLOAT_TYPE;
	fFlags = 0;
	fFloat = value;
}


/** @brief Stores a double value.
 *  @param value  The double value to store.
 */
void
BVariant::_SetTo(double value)
{
	fType = B_DOUBLE_TYPE;
	fFlags = 0;
	fDouble = value;
}


/** @brief Stores a BRect as four float components.
 *  @param left    Left edge.
 *  @param top     Top edge.
 *  @param right   Right edge.
 *  @param bottom  Bottom edge.
 */
void
BVariant::_SetTo(float left, float top, float right, float bottom)
{
	fType = B_RECT_TYPE;
	fFlags = 0;
	fRect.left = left;
	fRect.top = top;
	fRect.right = right;
	fRect.bottom = bottom;
}


/** @brief Stores a pointer value.
 *  @param value  The pointer to store.
 */
void
BVariant::_SetTo(const void* value)
{
	fType = B_POINTER_TYPE;
	fFlags = 0;
	fPointer = (void*)value;
}


/** @brief Stores a string value, optionally duplicating it.
 *
 *  If B_VARIANT_DONT_COPY_DATA is not set in @p flags a strdup() copy is
 *  made and B_VARIANT_OWNS_DATA is recorded in fFlags. If the string is NULL
 *  fString is set to NULL with no copy.
 *
 *  @param value  NUL-terminated string to store (may be NULL).
 *  @param flags  B_VARIANT_DONT_COPY_DATA suppresses duplication;
 *                B_VARIANT_OWNS_DATA is propagated when not copying.
 *  @return true on success, false if strdup() returns NULL.
 */
bool
BVariant::_SetTo(const char* value, uint32 flags)
{
	fType = B_STRING_TYPE;
	fFlags = 0;

	if (value != NULL) {
		if ((flags & B_VARIANT_DONT_COPY_DATA) == 0) {
			fString = strdup(value);
			fFlags |= B_VARIANT_OWNS_DATA;
			if (fString == NULL)
				return false;
		} else {
			fString = (char*)value;
			fFlags |= flags & B_VARIANT_OWNS_DATA;
		}
	} else
		fString = NULL;

	return true;
}


/** @brief Stores a BReferenceable pointer and acquires a reference.
 *
 *  @param value  The referenceable object to store (may be NULL).
 *  @param type   The type code to associate (application-defined).
 */
void
BVariant::_SetTo(BReferenceable* value, type_code type)
{
	fType = type;
	fFlags = B_VARIANT_REFERENCEABLE_DATA;
	fReferenceable = value;

	if (fReferenceable != NULL)
		fReferenceable->AcquireReference();
}
