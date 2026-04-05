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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2005 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marc Flerackers (mflerackers@androme.be)
 */


/**
 * @file PropertyInfo.cpp
 * @brief Implementation of BPropertyInfo for scripting property metadata.
 *
 * BPropertyInfo stores and manages property_info and value_info arrays that
 * describe the scripting interface of a BHandler. It supports matching
 * incoming scripting messages against declared properties, and implements
 * the BFlattenable interface for serialization and deserialization of
 * property metadata with cross-endian support.
 */

#include <ByteOrder.h>
#include <DataIO.h>
#include <Message.h>
#include <PropertyInfo.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>


/** @brief Constructs a BPropertyInfo from property and value info arrays.
 *
 *  The arrays are expected to be null-terminated (the last entry has a
 *  NULL name field). The counts are computed automatically by iterating
 *  each array.
 *
 *  @param propertyInfo Pointer to the property_info array, or NULL.
 *  @param valueInfo Pointer to the value_info array, or NULL.
 *  @param freeOnDelete If true, the arrays (and their string members) will
 *                      be freed when this object is destroyed.
 */
BPropertyInfo::BPropertyInfo(property_info* propertyInfo, value_info* valueInfo,
	bool freeOnDelete)
	:
	fPropInfo(propertyInfo),
	fValueInfo(valueInfo),
	fPropCount(0),
	fInHeap(freeOnDelete),
	fValueCount(0)
{
	if (fPropInfo != NULL) {
		while (fPropInfo[fPropCount].name)
			fPropCount++;
	}

	if (fValueInfo != NULL) {
		while (fValueInfo[fValueCount].name)
			fValueCount++;
	}
}


/** @brief Destroys the BPropertyInfo, freeing heap-allocated data if applicable. */
BPropertyInfo::~BPropertyInfo()
{
	FreeMem();
}


/** @brief Finds a property matching the given scripting message parameters.
 *
 *  Iterates through the property_info array looking for an entry whose
 *  name matches the property, whose commands match the message's what
 *  code (at the given specifier index), and whose specifiers match the
 *  given form.
 *
 *  @param message The scripting message to match against.
 *  @param index The specifier index in the message (0 for direct property).
 *  @param specifier The specifier BMessage (unused in the matching logic).
 *  @param form The specifier form (e.g., B_DIRECT_SPECIFIER).
 *  @param property The property name to look up.
 *  @param data If non-NULL and a match is found, receives the extra_data
 *              field of the matched property_info entry.
 *  @return The index of the matching property, or B_ERROR if no match.
 */
int32 BPropertyInfo::FindMatch(BMessage* message, int32 index,
	BMessage* specifier, int32 form, const char* property, void* data) const
{
	int32 propertyIndex = 0;

	while (fPropInfo != NULL && fPropInfo[propertyIndex].name != NULL) {
		property_info* propertyInfo = fPropInfo + propertyIndex;

		if (!strcmp(propertyInfo->name, property)
			&& FindCommand(message->what, index, propertyInfo)
			&& FindSpecifier(form, propertyInfo)) {
			if (data)
				*((uint32*)data) = propertyInfo->extra_data;

			return propertyIndex;
		}
		propertyIndex++;
	}

	return B_ERROR;
}


/** @brief Returns whether the flattened representation has a fixed size.
 *
 *  Always returns false since the flattened size depends on the number
 *  and contents of the property and value info entries.
 *
 *  @return false always.
 */
bool
BPropertyInfo::IsFixedSize() const
{
	return false;
}


/** @brief Returns the type code for the flattened data format.
 *  @return B_PROPERTY_INFO_TYPE.
 */
type_code
BPropertyInfo::TypeCode() const
{
	return B_PROPERTY_INFO_TYPE;
}


/** @brief Computes the number of bytes needed to flatten this object.
 *
 *  Calculates the total serialization size including the header, all
 *  property_info entries (names, usage strings, commands, specifiers,
 *  types, and compound types), and all value_info entries.
 *
 *  @return The total flattened size in bytes.
 */
ssize_t
BPropertyInfo::FlattenedSize() const
{
	size_t size = (2 * sizeof(int32)) + 1;

	if (fPropInfo) {
		// Main chunks
		for (int32 pi = 0; fPropInfo[pi].name != NULL; pi++) {
			size += strlen(fPropInfo[pi].name) + 1;

			if (fPropInfo[pi].usage)
				size += strlen(fPropInfo[pi].usage) + 1;
			else
				size += sizeof(char);

			size += sizeof(int32);

			for (int32 i = 0; i < 10 && fPropInfo[pi].commands[i] != 0; i++)
				size += sizeof(int32);
			size += sizeof(int32);

			for (int32 i = 0; i < 10 && fPropInfo[pi].specifiers[i] != 0; i++)
				size += sizeof(int32);
			size += sizeof(int32);
		}

		// Type chunks
		for (int32 pi = 0; fPropInfo[pi].name != NULL; pi++) {
			for (int32 i = 0; i < 10 && fPropInfo[pi].types[i] != 0; i++)
				size += sizeof(int32);
			size += sizeof(int32);

			for (int32 i = 0; i < 3
					&& fPropInfo[pi].ctypes[i].pairs[0].name != 0; i++) {
				for (int32 j = 0; j < 5
						&& fPropInfo[pi].ctypes[i].pairs[j].name != 0; j++) {
					size += strlen(fPropInfo[pi].ctypes[i].pairs[j].name) + 1;
					size += sizeof(int32);
				}
				size += sizeof(int32);
			}
			size += sizeof(int32);
		}
	}

	if (fValueInfo) {
		size += sizeof(int16);

		// Chunks
		for (int32 vi = 0; fValueInfo[vi].name != NULL; vi++) {
			size += sizeof(int32);
			size += sizeof(int32);

			size += strlen(fValueInfo[vi].name) + 1;

			if (fValueInfo[vi].usage)
				size += strlen(fValueInfo[vi].usage) + 1;
			else
				size += sizeof(char);

			size += sizeof(int32);
		}
	}

	return size;
}


/** @brief Serializes the property and value info into a flat buffer.
 *
 *  Writes all property_info and value_info data into the supplied buffer
 *  in a platform-aware binary format. The first byte indicates the host
 *  endianness for cross-platform deserialization.
 *
 *  @param buffer Pointer to the destination buffer.
 *  @param numBytes Size of the destination buffer in bytes.
 *  @return B_OK on success, B_NO_MEMORY if the buffer is too small,
 *          B_BAD_VALUE if buffer is NULL.
 */
status_t
BPropertyInfo::Flatten(void* buffer, ssize_t numBytes) const
{
	if (numBytes < FlattenedSize())
		return B_NO_MEMORY;

	if (buffer == NULL)
		return B_BAD_VALUE;

	BMemoryIO flatData(buffer, numBytes);

	char tmpChar = B_HOST_IS_BENDIAN;
	int32 tmpInt;

	flatData.Write(&tmpChar, sizeof(tmpChar));
	flatData.Write(&fPropCount, sizeof(fPropCount));
	tmpInt = 0x01 | (fValueInfo ? 0x2 : 0x0);
	flatData.Write(&tmpInt, sizeof(tmpInt));

	if (fPropInfo) {
		// Main chunks
		for (int32 pi = 0; fPropInfo[pi].name != NULL; pi++) {
			flatData.Write(fPropInfo[pi].name, strlen(fPropInfo[pi].name) + 1);
			if (fPropInfo[pi].usage != NULL) {
				flatData.Write(fPropInfo[pi].usage, strlen(fPropInfo[pi].usage)
					+ 1);
			} else {
				tmpChar = 0;
				flatData.Write(&tmpChar, sizeof(tmpChar));
			}

			flatData.Write(&fPropInfo[pi].extra_data,
				sizeof(fPropInfo[pi].extra_data));

			for (int32 i = 0; i < 10 && fPropInfo[pi].commands[i] != 0; i++) {
				flatData.Write(&fPropInfo[pi].commands[i],
					sizeof(fPropInfo[pi].commands[i]));
			}
			tmpInt = 0;
			flatData.Write(&tmpInt, sizeof(tmpInt));

			for (int32 i = 0; i < 10 && fPropInfo[pi].specifiers[i] != 0; i++) {
				flatData.Write(&fPropInfo[pi].specifiers[i],
					sizeof(fPropInfo[pi].specifiers[i]));
			}
			tmpInt = 0;
			flatData.Write(&tmpInt, sizeof(tmpInt));
		}

		// Type chunks
		for (int32 pi = 0; fPropInfo[pi].name != NULL; pi++) {
			for (int32 i = 0; i < 10 && fPropInfo[pi].types[i] != 0; i++) {
				flatData.Write(&fPropInfo[pi].types[i],
					sizeof(fPropInfo[pi].types[i]));
			}
			tmpInt = 0;
			flatData.Write(&tmpInt, sizeof(tmpInt));

			for (int32 i = 0; i < 3
					&& fPropInfo[pi].ctypes[i].pairs[0].name != 0; i++) {
				for (int32 j = 0; j < 5
						&& fPropInfo[pi].ctypes[i].pairs[j].name != 0; j++) {
					flatData.Write(fPropInfo[pi].ctypes[i].pairs[j].name,
						strlen(fPropInfo[pi].ctypes[i].pairs[j].name) + 1);
					flatData.Write(&fPropInfo[pi].ctypes[i].pairs[j].type,
						sizeof(fPropInfo[pi].ctypes[i].pairs[j].type));
				}
				tmpInt = 0;
				flatData.Write(&tmpInt, sizeof(tmpInt));
			}
			tmpInt = 0;
			flatData.Write(&tmpInt, sizeof(tmpInt));
		}
	}

	if (fValueInfo) {
		// Value Chunks
		flatData.Write(&fValueCount, sizeof(fValueCount));
		for (int32 vi = 0; fValueInfo[vi].name != NULL; vi++) {
			flatData.Write(&fValueInfo[vi].kind, sizeof(fValueInfo[vi].kind));
			flatData.Write(&fValueInfo[vi].value, sizeof(fValueInfo[vi].value));
			flatData.Write(fValueInfo[vi].name, strlen(fValueInfo[vi].name)
				+ 1);
			if (fValueInfo[vi].usage) {
				flatData.Write(fValueInfo[vi].usage,
					strlen(fValueInfo[vi].usage) + 1);
			} else {
				tmpChar = 0;
				flatData.Write(&tmpChar, sizeof(tmpChar));
			}
			flatData.Write(&fValueInfo[vi].extra_data,
				sizeof(fValueInfo[vi].extra_data));
		}
	}

	return B_OK;
}


/** @brief Checks whether the given type code is accepted for unflattening.
 *
 *  @param code The type code to check.
 *  @return true if code is B_PROPERTY_INFO_TYPE, false otherwise.
 */
bool
BPropertyInfo::AllowsTypeCode(type_code code) const
{
	return code == B_PROPERTY_INFO_TYPE;
}


/** @brief Deserializes property and value info from a flat buffer.
 *
 *  Reconstructs the property_info and value_info arrays from serialized
 *  data. Handles byte-swapping if the data was serialized on a platform
 *  with different endianness. Any previously held data is freed first.
 *
 *  @param code The type code of the flattened data (must be B_PROPERTY_INFO_TYPE).
 *  @param buffer Pointer to the source buffer containing flattened data.
 *  @param numBytes Size of the source buffer in bytes.
 *  @return B_OK on success, B_BAD_TYPE if code is not allowed,
 *          B_BAD_VALUE if buffer is NULL.
 */
status_t
BPropertyInfo::Unflatten(type_code code, const void* buffer,
	ssize_t numBytes)
{
	if (!AllowsTypeCode(code))
		return B_BAD_TYPE;

	if (buffer == NULL)
		return B_BAD_VALUE;

	FreeMem();

	BMemoryIO flatData(buffer, numBytes);
	char tmpChar = B_HOST_IS_BENDIAN;
	int32 tmpInt;

	flatData.Read(&tmpChar, sizeof(tmpChar));
	bool swapRequired = (tmpChar != B_HOST_IS_BENDIAN);

	flatData.Read(&fPropCount, sizeof(fPropCount));

	int32 flags;
	flatData.Read(&flags, sizeof(flags));
	if (swapRequired) {
		fPropCount = B_SWAP_INT32(fPropCount);
		flags = B_SWAP_INT32(flags);
	}

	if (flags & 1) {
		fPropInfo = static_cast<property_info *>(malloc(sizeof(property_info)
			* (fPropCount + 1)));
		memset(fPropInfo, 0, (fPropCount + 1) * sizeof(property_info));

		// Main chunks
		for (int32 pi = 0; pi < fPropCount; pi++) {
			fPropInfo[pi].name = strdup(static_cast<const char*>(buffer)
				+ flatData.Position());
			flatData.Seek(strlen(fPropInfo[pi].name) + 1, SEEK_CUR);

			fPropInfo[pi].usage = strdup(static_cast<const char *>(buffer)
				+ flatData.Position());
			flatData.Seek(strlen(fPropInfo[pi].usage) + 1, SEEK_CUR);

			flatData.Read(&fPropInfo[pi].extra_data,
				sizeof(fPropInfo[pi].extra_data));
			if (swapRequired) {
				fPropInfo[pi].extra_data
					= B_SWAP_INT32(fPropInfo[pi].extra_data);
			}

			flatData.Read(&tmpInt, sizeof(tmpInt));
			for (int32 i = 0; tmpInt != 0; i++) {
				if (swapRequired) {
					tmpInt = B_SWAP_INT32(tmpInt);
				}
				fPropInfo[pi].commands[i] = tmpInt;
				flatData.Read(&tmpInt, sizeof(tmpInt));
			}

			flatData.Read(&tmpInt, sizeof(tmpInt));
			for (int32 i = 0; tmpInt != 0; i++) {
				if (swapRequired) {
					tmpInt = B_SWAP_INT32(tmpInt);
				}
				fPropInfo[pi].specifiers[i] = tmpInt;
				flatData.Read(&tmpInt, sizeof(tmpInt));
			}
		}

		// Type chunks
		for (int32 pi = 0; pi < fPropCount; pi++) {
			flatData.Read(&tmpInt, sizeof(tmpInt));
			for (int32 i = 0; tmpInt != 0; i++) {
				if (swapRequired) {
					tmpInt = B_SWAP_INT32(tmpInt);
				}
				fPropInfo[pi].types[i] = tmpInt;
				flatData.Read(&tmpInt, sizeof(tmpInt));
			}

			flatData.Read(&tmpInt, sizeof(tmpInt));
			for (int32 i = 0; tmpInt != 0; i++) {
				for (int32 j = 0; tmpInt != 0; j++) {
					flatData.Seek(-sizeof(tmpInt), SEEK_CUR);
					fPropInfo[pi].ctypes[i].pairs[j].name =
						strdup(static_cast<const char *>(buffer)
							+ flatData.Position());
					flatData.Seek(strlen(fPropInfo[pi].ctypes[i].pairs[j].name)
						+ 1, SEEK_CUR);

					flatData.Read(&fPropInfo[pi].ctypes[i].pairs[j].type,
						sizeof(fPropInfo[pi].ctypes[i].pairs[j].type));
					if (swapRequired) {
						fPropInfo[pi].ctypes[i].pairs[j].type =
							B_SWAP_INT32(fPropInfo[pi].ctypes[i].pairs[j].type);
					}
					flatData.Read(&tmpInt, sizeof(tmpInt));
				}
				flatData.Read(&tmpInt, sizeof(tmpInt));
			}
		}
	}

	if (flags & 2) {
		flatData.Read(&fValueCount, sizeof(fValueCount));
		if (swapRequired) {
			fValueCount = B_SWAP_INT16(fValueCount);
		}

		fValueInfo = static_cast<value_info *>(malloc(sizeof(value_info)
			* (fValueCount + 1)));
		memset(fValueInfo, 0, (fValueCount + 1) * sizeof(value_info));

		for (int32 vi = 0; vi < fValueCount; vi++) {
			flatData.Read(&fValueInfo[vi].kind, sizeof(fValueInfo[vi].kind));
			flatData.Read(&fValueInfo[vi].value, sizeof(fValueInfo[vi].value));

			fValueInfo[vi].name = strdup(static_cast<const char *>(buffer)
				+ flatData.Position());
			flatData.Seek(strlen(fValueInfo[vi].name) + 1, SEEK_CUR);

			fValueInfo[vi].usage = strdup(static_cast<const char *>(buffer)
				+ flatData.Position());
			flatData.Seek(strlen(fValueInfo[vi].usage) + 1, SEEK_CUR);

			flatData.Read(&fValueInfo[vi].extra_data,
				sizeof(fValueInfo[vi].extra_data));
			if (swapRequired) {
				fValueInfo[vi].kind = static_cast<value_kind>(
					B_SWAP_INT32(fValueInfo[vi].kind));
				fValueInfo[vi].value = B_SWAP_INT32(fValueInfo[vi].value);
				fValueInfo[vi].extra_data
					= B_SWAP_INT32(fValueInfo[vi].extra_data);
			}
		}
	}

	return B_OK;
}


/** @brief Returns the property_info array.
 *  @return Pointer to the null-terminated property_info array, or NULL.
 */
const property_info*
BPropertyInfo::Properties() const
{
	return fPropInfo;
}


/** @brief Returns the value_info array.
 *  @return Pointer to the null-terminated value_info array, or NULL.
 */
const value_info*
BPropertyInfo::Values() const
{
	return fValueInfo;
}


/** @brief Returns the number of property_info entries.
 *  @return The count of entries in the property_info array.
 */
int32
BPropertyInfo::CountProperties() const
{
	return fPropCount;
}


/** @brief Returns the number of value_info entries.
 *  @return The count of entries in the value_info array.
 */
int32
BPropertyInfo::CountValues() const
{
	return fValueCount;
}


/** @brief Prints all property info entries to standard output.
 *
 *  Outputs a formatted table showing each property's name, commands
 *  (as four-character codes), types, and specifiers.
 */
void
BPropertyInfo::PrintToStream() const
{
	printf("      property   commands                       types              "
		"     specifiers\n");
	printf("-------------------------------------------------------------------"
		"-------------\n");

	for (int32 pi = 0; fPropInfo[pi].name != 0; pi++) {
		// property
		printf("%14s", fPropInfo[pi].name);
		// commands
		for (int32 i = 0; i < 10 && fPropInfo[pi].commands[i] != 0; i++) {
			uint32 command = fPropInfo[pi].commands[i];

			printf("   %c%c%c%-28c", int(command & 0xFF000000) >> 24,
				int(command & 0xFF0000) >> 16, int(command & 0xFF00) >> 8,
				int(command) & 0xFF);
		}
		// types
		for (int32 i = 0; i < 10 && fPropInfo[pi].types[i] != 0; i++) {
			uint32 type = fPropInfo[pi].types[i];

			printf("%c%c%c%c", int(type & 0xFF000000) >> 24,
				int(type & 0xFF0000) >> 16, int(type & 0xFF00) >> 8,
					(int)type & 0xFF);
		}
		// specifiers
		for (int32 i = 0; i < 10 && fPropInfo[pi].specifiers[i] != 0; i++) {
			uint32 spec = fPropInfo[pi].specifiers[i];
			printf("%" B_PRIu32, spec);
		}
		printf("\n");
	}
}


/** @brief Checks if a property_info entry matches the given command.
 *
 *  If the property has no commands specified (wildcard), it matches any
 *  command. Otherwise, the command must appear in the property's commands
 *  array and index must be 0 (direct property access).
 *
 *  @param what The message command code to match.
 *  @param index The specifier nesting index (0 for direct access).
 *  @param propertyInfo The property_info entry to check.
 *  @return true if the command matches, false otherwise.
 */
bool
BPropertyInfo::FindCommand(uint32 what, int32 index,
	property_info* propertyInfo)
{
	bool result = false;

	if (propertyInfo->commands[0] == 0) {
		result = true;
	} else if (index == 0) {
		for (int32 i = 0; i < 10 && propertyInfo->commands[i] != 0; i++) {
			if (propertyInfo->commands[i] == what) {
				result = true;
				break;
			}
		}
	}

	return result;
}


/** @brief Checks if a property_info entry matches the given specifier form.
 *
 *  If the property has no specifiers specified (wildcard), it matches any
 *  form. Otherwise, the form must appear in the property's specifiers array.
 *
 *  @param form The specifier form to match (e.g., B_DIRECT_SPECIFIER).
 *  @param propertyInfo The property_info entry to check.
 *  @return true if the specifier matches, false otherwise.
 */
bool
BPropertyInfo::FindSpecifier(uint32 form, property_info* propertyInfo)
{
	bool result = false;

	if (propertyInfo->specifiers[0] == 0) {
		result = true;
	} else {
		for (int32 i = 0; i < 10 && propertyInfo->specifiers[i] != 0; i++) {
			if (propertyInfo->specifiers[i] == form) {
				result = true;
				break;
			}
		}
	}

	return result;
}


void BPropertyInfo::_ReservedPropertyInfo1() {}
void BPropertyInfo::_ReservedPropertyInfo2() {}
void BPropertyInfo::_ReservedPropertyInfo3() {}
void BPropertyInfo::_ReservedPropertyInfo4() {}


/** @brief Private copy constructor (intentionally unimplemented).
 *
 *  BPropertyInfo objects are not copyable. This exists only to prevent
 *  accidental copying.
 */
BPropertyInfo::BPropertyInfo(const BPropertyInfo &)
{
}


/** @brief Private assignment operator (intentionally unimplemented).
 *
 *  BPropertyInfo objects are not assignable. This exists only to prevent
 *  accidental assignment.
 *
 *  @return Reference to this object (no-op).
 */
BPropertyInfo&
BPropertyInfo::operator=(const BPropertyInfo &)
{
	return *this;
}


/** @brief Frees all heap-allocated property and value info data.
 *
 *  Only frees memory if fInHeap is true (i.e., data was allocated on the
 *  heap, typically via Unflatten()). Frees all name and usage strings,
 *  compound type pair names, and the arrays themselves. Resets fInHeap
 *  to false after cleanup.
 */
void
BPropertyInfo::FreeMem()
{
	int i, j, k;

	if (!fInHeap)
		return;

	if (fPropInfo != NULL) {
		for (i = 0; i < fPropCount; i++) {
			free((char *)fPropInfo[i].name);
			free((char *)fPropInfo[i].usage);

			for (j = 0; j < 3; j++) {
				for (k = 0; k < 5; k++) {
					if (fPropInfo[i].ctypes[j].pairs[k].name == NULL)
						break;

					free((char *)fPropInfo[i].ctypes[j].pairs[k].name);
				}

				if (fPropInfo[i].ctypes[j].pairs[0].name == NULL)
					break;
			}
		}
		free(fPropInfo);
		fPropInfo = NULL;
		fPropCount = 0;
	}

	if (fValueInfo != NULL) {
		for (i = 0; i < fValueCount; i++) {
			free((char *)fValueInfo[i].name);
			free((char *)fValueInfo[i].usage);
		}
		free(fValueInfo);
		fValueInfo = NULL;
		fValueCount = 0;
	}

	fInHeap = false;
}
