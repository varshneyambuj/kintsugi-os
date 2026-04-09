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
 *   Copyright 2002-2012, Haiku. All Rights Reserved.
 *   This file may be used under the terms of the MIT License.
 *
 *   Author: Zousar Shaker
 *           Axel Dörfler, axeld@pinc-software.de
 *           Marcus Overhagen
 */

/** @file ParameterWeb.cpp
 *  @brief Implements the following classes:
 *         BParameterWeb, BParameterGroup, BParameter, BNullParameter,
 *         BContinuousParameter, BDiscreteParameter, BTextParameter.
 *
 *  These classes model a directed graph of media node parameters that can be
 *  flattened/unflattened for IPC and rendered into UI controls by a BMediaTheme.
 */


/*! Implements the following classes:
	BParameterWeb, BParameterGroup, BParameter, BNullParameter,
	BContinuousParameter, BDiscreteParameter
*/


#include <ParameterWeb.h>

#include <new>
#include <string.h>

#include <MediaNode.h>
#include <MediaRoster.h>

#include "DataExchange.h"
#include "MediaDebug.h"
#include "MediaMisc.h"


/*
	The following is documentation on the flattened format
	of structures/classes in this module:

	//--------BEGIN-CORE-BPARAMETER-STRUCT---------------------
	?? (0x02040607): 4 bytes
	BParameter Struct Size (in bytes): 4 bytes
	ID: 4 bytes
	Name String Length: 1 byte (??)
		Name String: 'Name String Length' bytes
	Kind String Length: 1 byte (??)
		Kind String: 'Kind String Length' bytes
	Unit String Length: 1 byte (??)
		Unit String: 'Unit String Length' bytes
	Inputs Count: 4 bytes
		Inputs (pointers): ('Inputs Count')*4 bytes
	Outputs Count: 4 bytes
		Outputs (pointers): ('Outputs Count')*4 bytes
	Media Type: 4 bytes
	ChannelCount: 4 bytes
	Flags: 4 bytes
	//---------END-CORE-BPARAMETER-STRUCT-----------------------
	//--------BEGIN-BCONTINUOUSPARAMETER-STRUCT---------
	Min: 4 bytes (as float)
	Max: 4 bytes (as float)
	Stepping: 4 bytes (as float)
	Response: 4 bytes (as int or enum)
	Factor: 4 bytes (as float)
	Offset: 4 bytes (as float)
	//--------END-BCONTINUOUSPARAMETER-STRUCT-------------
	//--------BEGIN-BDISCRETEPARAMETER-STRUCT----------------
	NumItems: 4 bytes (as int)
		//for each item BEGIN
		Item Name String Length: 1 byte
			Item Name String: 'Item Name String Length' bytes
		Item Value: 4 bytes (as int)
		//for each item END
	//--------END-BDISCRETEPARAMETER-STRUCT-------------------

	//--------BEGIN-CORE-BPARAMETERGROUP-STRUCT-----------
	?? (0x03040507 OR 0x03040509 depending if the flags field is included or not???): 4 bytes
	(possible) Flags: 4 bytes
	Name String Length: 1 byte (??)
		Name String: 'Name String Length' bytes
	Param Count: 4 bytes
		//for each Param BEGIN
		Pointer: 4 bytes
		Parameter Type: 4 bytes
		Flattened Parameter Size: 4 bytes
		Flattened Parameter: 'Flattened Parameter Size' bytes
		//for each Param END
	Subgroup Count: 4 bytes
		//for each SubGroup BEGIN
		Pointer: 4 bytes
		MEDIA PARAMETER GROUP TYPE('BMCG' (opposite byte order in file)): 4 bytes
		Flattened Group Size: 4 bytes
		Flattened Group: 'Flattened Group Size' bytes
		//for each SubGroup END

	//---------END-CORE-BPARAMETERGROUP-STRUCT--------------

	//--------BEGIN-CORE-BPARAMETERWEB-STRUCT-----------
	?? 0x01030506: 4 bytes
	??: 4 bytes (is always 1)
	Group Count: 4 bytes
	Node (as media_node): 0x18 bytes (decimal 24 bytes)
		//for each Group BEGIN
		Flattened Group Size: 4 bytes
		Flattened Group: 'Flattened Group Size' bytes
		//for each Group END
		//for each Group BEGIN
		??: 4 bytes (never get written to (holds uninitialized value))
		//for each Group END
	//---------END-CORE-BPARAMETERWEB-STRUCT--------------

*/


const char * const B_GENERIC			 = "";
const char * const B_MASTER_GAIN		 = "Master";
const char * const B_GAIN				 = "Gain";
const char * const B_BALANCE			 = "Balance";
const char * const B_FREQUENCY			 = "Frequency";
const char * const B_LEVEL				 = "Level";
const char * const B_SHUTTLE_SPEED		 = "Speed";
const char * const B_CROSSFADE			 = "XFade";
const char * const B_EQUALIZATION		 = "EQ";
const char * const B_COMPRESSION		 = "Compression";
const char * const B_QUALITY			 = "Quality";
const char * const B_BITRATE			 = "Bitrate";
const char * const B_GOP_SIZE			 = "GOPSize";
const char * const B_MUTE				 = "Mute";
const char * const B_ENABLE				 = "Enable";
const char * const B_INPUT_MUX			 = "Input";
const char * const B_OUTPUT_MUX			 = "Output";
const char * const B_TUNER_CHANNEL		 = "Channel";
const char * const B_TRACK				 = "Track";
const char * const B_RECSTATE			 = "RecState";
const char * const B_SHUTTLE_MODE		 = "Shuttle";
const char * const B_RESOLUTION			 = "Resolution";
const char * const B_COLOR_SPACE		 = "Colorspace";
const char * const B_FRAME_RATE			 = "FrameRate";
const char * const B_VIDEO_FORMAT		 = "VideoFormat";
const char * const B_WEB_PHYSICAL_INPUT	 = "PhysInput";
const char * const B_WEB_PHYSICAL_OUTPUT = "PhysOutput";
const char * const B_WEB_ADC_CONVERTER	 = "ADC";
const char * const B_WEB_DAC_CONVERTER	 = "DAC";
const char * const B_WEB_LOGICAL_INPUT	 = "LogInput";
const char * const B_WEB_LOGICAL_OUTPUT	 = "LogOutput";
const char * const B_WEB_LOGICAL_BUS	 = "LogBus";
const char * const B_WEB_BUFFER_INPUT	 = "DataInput";
const char * const B_WEB_BUFFER_OUTPUT	 = "DataOutput";
const char * const B_SIMPLE_TRANSPORT	 = "SimpleTransport";

// Flattened data

static const int32 kCurrentParameterWebVersion = 1;
static const uint32 kParameterWebMagic = 0x01030506;
static const uint32 kBufferGroupMagic = 0x03040509;
static const uint32 kBufferGroupMagicNoFlags = 0x03040507;
static const uint32 kParameterMagic = 0x02040607;

static const ssize_t kAdditionalParameterGroupSize = 12;
static const ssize_t kAdditionalParameterSize = 23 + 3 * sizeof(ssize_t);

/* BContinuousParameter - FlattenedSize() fixed part
 *	Min: 4 bytes (as float)
 *	Max: 4 bytes (as float)
 *	Stepping: 4 bytes (as float)
 *	Response: 4 bytes (as int or enum)
 *	Factor: 4 bytes (as float)
 *	Offset: 4 bytes (as float)
 */
static const ssize_t kAdditionalContinuousParameterSize = 5 * sizeof(float)
	+ sizeof(BContinuousParameter::response);
static const ssize_t kAdditionalDiscreteParameterSize = sizeof(ssize_t);


// helper functions


/**
 * @brief Read a value of type @p Type from @p _buffer and advance the pointer.
 *
 * @tparam Type  The POD type to read.
 * @param _buffer  In/out pointer to the read position.
 * @return The value read from the buffer.
 */
template<class Type> Type
read_from_buffer(const void **_buffer)
{
	const Type *typedBuffer = static_cast<const Type *>(*_buffer);
	Type value = *typedBuffer;

	typedBuffer++;
	*_buffer = static_cast<const void *>(typedBuffer);

	return value;
}


/**
 * @brief Read a length-prefixed string from @p _buffer into a malloc'd copy.
 *
 * The first byte is the string length (0-255), followed by that many characters.
 *
 * @param _buffer  In/out pointer to the read position.
 * @param _string  In/out: existing string is freed, receives the new string.
 * @param size     Remaining bytes in the buffer (must be >= 1).
 * @return B_OK on success, B_BAD_VALUE if @p size is too small, B_NO_MEMORY on failure.
 */
static status_t
read_string_from_buffer(const void **_buffer, char **_string, ssize_t size)
{
	if (size < 1)
		return B_BAD_VALUE;

	const uint8 *buffer = static_cast<const uint8 *>(*_buffer);
	uint8 length = *buffer++;
	if (length > size - 1)
		return B_BAD_VALUE;

	char *string = (char *)malloc(length + 1);
	if (string == NULL)
		return B_NO_MEMORY;

	memcpy(string, buffer, length);
	string[length] = '\0';

	free(*_string);

	*_buffer = static_cast<const void *>(buffer + length);
	*_string = string;
	return B_OK;
}


// currently unused
#if 0
template<class Type> Type *
reserve_in_buffer(void **_buffer)
{
	Type *typedBuffer = static_cast<Type *>(*_buffer);

	*typedBuffer = 0;
	typedBuffer++;

	*_buffer = static_cast<void *>(typedBuffer);
}
#endif

/**
 * @brief Write a value of type @p Type into @p _buffer and advance the pointer.
 *
 * @tparam Type  The POD type to write.
 * @param _buffer  In/out pointer to the write position.
 * @param value    The value to write.
 */
template<class Type> void
write_to_buffer(void **_buffer, Type value)
{
	Type *typedBuffer = static_cast<Type *>(*_buffer);

	*typedBuffer = value;
	typedBuffer++;

	*_buffer = static_cast<void *>(typedBuffer);
}


/**
 * @brief Write a length-prefixed string (up to 255 chars) into @p _buffer.
 *
 * @param _buffer  In/out pointer to the write position.
 * @param string   The string to write; NULL is treated as empty.
 */
void
write_string_to_buffer(void **_buffer, const char *string)
{
	uint8 *buffer = static_cast<uint8 *>(*_buffer);
	uint32 length = string ? strlen(string) : 0;
	if (length > 255)
		length = 255;

	*buffer++ = static_cast<uint8>(length);
	if (length) {
		memcpy(buffer, string, length);
		buffer += length;
	}

	*_buffer = static_cast<void *>(buffer);
}


/**
 * @brief Advance a const buffer pointer by @p bytes without reading any data.
 *
 * @param _buffer  In/out pointer to advance.
 * @param bytes    Number of bytes to skip.
 */
static void
skip_in_buffer(const void **_buffer, uint32 bytes)
{
	const uint8 *buffer = static_cast<const uint8 *>(*_buffer);

	buffer += bytes;

	*_buffer = static_cast<const void *>(buffer);
}


/**
 * @brief Advance a mutable buffer pointer by @p bytes without writing any data.
 *
 * @param _buffer  In/out pointer to advance.
 * @param bytes    Number of bytes to skip.
 */
static void inline
skip_in_buffer(void **_buffer, uint32 bytes)
{
	// This actually shouldn't be necessary, but I believe it's a
	// bug in Be's gcc - it complains about "adds cv-quals without intervening `const'"
	// when passing a "const void **" pointer as the first argument.
	// Maybe I am just stupid, though -- axeld.

	skip_in_buffer((const void **)_buffer, bytes);
}


/**
 * @brief Conditionally byte-swap a 32-bit value.
 *
 * @tparam Type  A 4-byte POD type.
 * @param value   The value to conditionally swap.
 * @param doSwap  If true, byte-swap; otherwise return unchanged.
 * @return The (possibly swapped) value.
 */
template<class Type> Type
swap32(Type value, bool doSwap)
{
	STATIC_ASSERT(sizeof(Type) == 4);

	if (doSwap)
		return (Type)B_SWAP_INT32((int32)value);

	return value;
}


/**
 * @brief Conditionally byte-swap a 64-bit value.
 *
 * @tparam Type  An 8-byte POD type.
 * @param value   The value to conditionally swap.
 * @param doSwap  If true, byte-swap; otherwise return unchanged.
 * @return The (possibly swapped) value.
 */
template<class Type> Type
swap64(Type value, bool doSwap)
{
	STATIC_ASSERT(sizeof(Type) == 8);

	if (doSwap)
		return (Type)B_SWAP_INT64((int64)value);

	return value;
}


/**
 * @brief Read and conditionally swap a 32-bit value from a buffer.
 *
 * @tparam Type  A 4-byte POD type.
 * @param _buffer  In/out read position.
 * @param doSwap   If true, byte-swap after reading.
 * @return The (possibly swapped) value.
 */
template<class Type> Type
read_from_buffer_swap32(const void **_buffer, bool doSwap)
{
	return swap32<Type>(read_from_buffer<Type>(_buffer), doSwap);
}


/**
 * @brief Read and conditionally swap a pointer-sized value from a buffer.
 *
 * Selects 32- or 64-bit swap depending on the target ABI.
 *
 * @tparam Type  A pointer-sized POD type.
 * @param _buffer  In/out read position.
 * @param doSwap   If true, byte-swap after reading.
 * @return The (possibly swapped) value.
 */
template<class Type> Type
read_pointer_from_buffer_swap(const void **_buffer, bool doSwap)
{
#if B_HAIKU_32_BIT
	return swap32<Type>(read_from_buffer<Type>(_buffer), doSwap);
#elif B_HAIKU_64_BIT
	return swap64<Type>(read_from_buffer<Type>(_buffer), doSwap);
#else
#	error Interesting
#endif
}


/**
 * @brief Return the number of bytes remaining in a buffer from a given position.
 *
 * @param size         Total buffer size.
 * @param bufferStart  Start of the buffer.
 * @param buffer       Current read position.
 * @return Bytes remaining.
 */
static inline ssize_t
size_left(ssize_t size, const void *bufferStart, const void *buffer)
{
	return size - static_cast<ssize_t>((const uint8 *)buffer - (const uint8 *)bufferStart);
}


//	#pragma mark - BParameterWeb


/**
 * @brief Construct an empty BParameterWeb not yet associated with any node.
 */
BParameterWeb::BParameterWeb()
	:
	fNode(media_node::null)
		// fNode is set in BControllable::SetParameterWeb()
{
	CALLED();

	fGroups = new BList();
	fOldRefs = new BList();
	fNewRefs = new BList();
}


/**
 * @brief Destructor. Deletes all owned BParameterGroup objects.
 */
BParameterWeb::~BParameterWeb()
{
	CALLED();

	for (int32 i = fGroups->CountItems(); i-- > 0;) {
		delete static_cast<BParameterGroup*>(fGroups->ItemAt(i));
	}

	delete fGroups;
	delete fOldRefs;
	delete fNewRefs;
}


/**
 * @brief Return the media node this web is associated with.
 *
 * @return The associated media_node, or media_node::null if unset.
 */
media_node
BParameterWeb::Node()
{
	return fNode;
}


/**
 * @brief Create and add a new top-level parameter group.
 *
 * @param name  Name for the new group.
 * @return Pointer to the new BParameterGroup, or NULL on allocation failure.
 */
BParameterGroup*
BParameterWeb::MakeGroup(const char* name)
{
	CALLED();

	BParameterGroup* group = new(std::nothrow) BParameterGroup(this, name);
	if (group == NULL)
		return NULL;

	if (!fGroups->AddItem(group)) {
		delete group;
		return NULL;
	}

	return group;
}


/**
 * @brief Return the number of top-level parameter groups in this web.
 *
 * @return Group count.
 */
int32
BParameterWeb::CountGroups()
{
	return fGroups->CountItems();
}


/**
 * @brief Return the top-level group at the given index.
 *
 * @param index  Zero-based group index.
 * @return Pointer to the BParameterGroup, or NULL if out of range.
 */
BParameterGroup*
BParameterWeb::GroupAt(int32 index)
{
	return static_cast<BParameterGroup*>(fGroups->ItemAt(index));
}


/**
 * @brief Return the total number of parameters across all groups and sub-groups.
 *
 * @return Total parameter count (breadth-first traversal).
 */
int32
BParameterWeb::CountParameters()
{
	CALLED();

	// Counts over all groups (and sub-groups) in the web.
	// The "groups" list is used as count stack

	BList groups(*fGroups);
	int32 count = 0;

	for (int32 i = 0; i < groups.CountItems(); i++) {
		BParameterGroup* group
			= static_cast<BParameterGroup*>(groups.ItemAt(i));

		count += group->CountParameters();

		if (group->fGroups != NULL)
			groups.AddList(group->fGroups);
	}

	return count;
}


/**
 * @brief Return the parameter at the given global index (breadth-first order).
 *
 * @param index  Zero-based parameter index across all groups.
 * @return Pointer to the BParameter, or NULL if out of range.
 */
BParameter*
BParameterWeb::ParameterAt(int32 index)
{
	CALLED();

	// Iterates over all groups (and sub-groups) in the web.
	// The "groups" list is used as iteration stack (breadth search style)
	// Maintains the same order as the Be implementation

	BList groups(*fGroups);

	for (int32 i = 0; i < groups.CountItems(); i++) {
		BParameterGroup* group
			= static_cast<BParameterGroup*>(groups.ItemAt(i));
		int32 count = group->CountParameters();
		if (index < count)
			return group->ParameterAt(index);

		index -= count;
			// the index is always relative to the start of the current group

		if (group->fGroups != NULL)
			groups.AddList(group->fGroups);
	}

	TRACE("*** could not find parameter at %"
		B_PRId32 " (count = %" B_PRId32 ")\n", index, CountParameters());

	return NULL;
}


/**
 * @brief Return false; the flattened size of a BParameterWeb is variable.
 *
 * @return Always false.
 */
bool
BParameterWeb::IsFixedSize() const
{
	return false;
}


/**
 * @brief Return the type code used when flattening this object.
 *
 * @return B_MEDIA_PARAMETER_WEB_TYPE.
 */
type_code
BParameterWeb::TypeCode() const
{
	return B_MEDIA_PARAMETER_WEB_TYPE;
}


/**
 * @brief Return the number of bytes needed to flatten this web.
 *
 * @return Total flattened size in bytes.
 */
ssize_t
BParameterWeb::FlattenedSize() const
{
	CALLED();

/*
	//--------BEGIN-CORE-BPARAMETERWEB-STRUCT-----------
	?? 0x01030506: 4 bytes
	??: 4 bytes (is always 1)
	Group Count: 4 bytes
	Node (as media_node): 0x18 bytes (decimal 24 bytes)
		//for each Group BEGIN
		Flattened Group Size: 4 bytes
		Flattened Group: 'Flattened Group Size' bytes
		//for each Group END
		//for each Group BEGIN
		??: 4 bytes (never get written to (holds uninitialized value))
		//for each Group END
	//---------END-CORE-BPARAMETERWEB-STRUCT--------------
*/
	//36 guaranteed bytes, variable after that.
	ssize_t size = sizeof(int32) + 2 * sizeof(int32) + sizeof(media_node);

	for (int32 i = fGroups->CountItems(); i-- > 0;) {
		BParameterGroup* group
			= static_cast<BParameterGroup*>(fGroups->ItemAt(i));
		if (group != NULL) {
			size += sizeof(ssize_t) + group->FlattenedSize();
		}
	}

	return size;
}


/**
 * @brief Serialise this BParameterWeb into @p buffer.
 *
 * @param buffer  Destination buffer (must be at least FlattenedSize() bytes).
 * @param size    Capacity of @p buffer in bytes.
 * @return B_OK on success, B_NO_INIT if @p buffer is NULL, B_NO_MEMORY if too small.
 */
status_t
BParameterWeb::Flatten(void* buffer, ssize_t size) const
{
	CALLED();

	if (buffer == NULL)
		return B_NO_INIT;

	ssize_t actualSize = BParameterWeb::FlattenedSize();
	if (size < actualSize)
		return B_NO_MEMORY;

	void* bufferStart = buffer;

	write_to_buffer<int32>(&buffer, kParameterWebMagic);
	write_to_buffer<int32>(&buffer, kCurrentParameterWebVersion);

	// flatten all groups into this buffer

	int32 count = fGroups->CountItems();
	write_to_buffer<int32>(&buffer, count);

	write_to_buffer<media_node>(&buffer, fNode);

	for (int32 i = 0; i < count; i++) {
		BParameterGroup* group
			= static_cast<BParameterGroup*>(fGroups->ItemAt(i));
		if (group == NULL) {
			ERROR("BParameterWeb::Flatten(): group is NULL\n");
			continue;
		}

		ssize_t groupSize = group->FlattenedSize();
		if (groupSize > size_left(size, bufferStart, buffer)) {
			ERROR("BParameterWeb::Flatten(): buffer too small\n");
			return B_BAD_VALUE;
		}

		write_to_buffer<ssize_t>(&buffer, groupSize);

		// write the flattened sub group
		status_t status = group->Flatten(buffer, groupSize);
		if (status < B_OK)
			return status;

		skip_in_buffer(&buffer, groupSize);
	}

	return B_OK;
}


/**
 * @brief Return true if @p code matches this object's type code.
 *
 * @param code  Type code to test.
 * @return true if @p code == B_MEDIA_PARAMETER_WEB_TYPE.
 */
bool
BParameterWeb::AllowsTypeCode(type_code code) const
{
	return code == TypeCode();
}


/**
 * @brief Restore this BParameterWeb from a flat buffer.
 *
 * Clears all existing groups, reads the node and group list, and fixes
 * up cross-group parameter input/output references.
 *
 * @param code    Must be B_MEDIA_PARAMETER_WEB_TYPE.
 * @param buffer  Source buffer.
 * @param size    Size of @p buffer in bytes.
 * @return B_OK on success, or an error code on malformed data.
 */
status_t
BParameterWeb::Unflatten(type_code code, const void* buffer, ssize_t size)
{
	CALLED();

	if (!AllowsTypeCode(code)) {
		ERROR("BParameterWeb::Unflatten(): wrong type code\n");
		return B_BAD_TYPE;
	}

	if (buffer == NULL) {
		ERROR("BParameterWeb::Unflatten(): NULL buffer pointer\n");
		return B_NO_INIT;
	}

	// if the buffer is smaller than the size needed to read the
	// signature field, the mystery field, the group count, and the Node, then there is a problem
	if (size < static_cast<ssize_t>(sizeof(int32) + sizeof(int32)
			+ sizeof(ssize_t) + sizeof(media_node))) {
		ERROR("BParameterWeb::Unflatten(): size to small\n");
		return B_ERROR;
	}

	const void* bufferStart = buffer;

	uint32 magic = read_from_buffer<uint32>(&buffer);
	bool isSwapped = false;

	if (magic == B_SWAP_INT32(kParameterWebMagic)) {
		isSwapped = true;
		magic = B_SWAP_INT32(magic);
	}
	if (magic != kParameterWebMagic)
		return B_BAD_DATA;

	// Note, it's not completely sure that this field is the version
	// information - but it doesn't seem to have another purpose
	int32 version = read_from_buffer_swap32<int32>(&buffer, isSwapped);
	if (version != kCurrentParameterWebVersion) {
		ERROR("BParameterWeb::Unflatten(): wrong version %" B_PRId32 " (%"
			B_PRIx32 ")?!\n", version, version);
		return B_ERROR;
	}

	for (int32 i = 0; i < fGroups->CountItems(); i++) {
		delete static_cast<BParameterGroup*>(fGroups->ItemAt(i));
	}
	fGroups->MakeEmpty();

	int32 count = read_from_buffer_swap32<int32>(&buffer, isSwapped);

	fNode = read_from_buffer<media_node>(&buffer);
	if (isSwapped)
		swap_data(B_INT32_TYPE, &fNode, sizeof(media_node), B_SWAP_ALWAYS);

	for (int32 i = 0; i < count; i++) {
		ssize_t groupSize
			= read_pointer_from_buffer_swap<ssize_t>(&buffer, isSwapped);
		if (groupSize > size_left(size, bufferStart, buffer)) {
			ERROR("BParameterWeb::Unflatten(): buffer too small\n");
			return B_BAD_DATA;
		}

		BParameterGroup* group = new BParameterGroup(this, "unnamed");
		status_t status = group->Unflatten(group->TypeCode(), buffer,
			groupSize);
		if (status < B_OK) {
			ERROR("BParameterWeb::Unflatten(): unflatten group failed\n");
			delete group;
			return status;
		}

		skip_in_buffer(&buffer, groupSize);

		fGroups->AddItem(group);
	}

	// fix all references (ParameterAt() style)

	BList groups(*fGroups);

	for (int32 i = 0; i < groups.CountItems(); i++) {
		BParameterGroup* group
			= static_cast<BParameterGroup*>(groups.ItemAt(i));

		for (int32 index = group->CountParameters(); index-- > 0;) {
			BParameter* parameter
				= static_cast<BParameter*>(group->ParameterAt(index));

			parameter->FixRefs(*fOldRefs, *fNewRefs);
		}

		if (group->fGroups != NULL)
			groups.AddList(group->fGroups);
	}

	fOldRefs->MakeEmpty();
	fNewRefs->MakeEmpty();

	return B_OK;
}


/**
 * @brief Register a pointer remapping pair used during Unflatten().
 *
 * @param oldItem  Old pointer value (from the flat buffer).
 * @param newItem  New pointer value (newly allocated object).
 */
void
BParameterWeb::AddRefFix(void* oldItem, void* newItem)
{
	fOldRefs->AddItem(oldItem);
	fNewRefs->AddItem(newItem);
}


//	#pragma mark - BParameterGroup


/**
 * @brief Construct a BParameterGroup belonging to @p web with the given name.
 *
 * @param web   Owning BParameterWeb.
 * @param name  Group name (truncated to 255 characters).
 */
BParameterGroup::BParameterGroup(BParameterWeb* web, const char* name)
	:
	fWeb(web),
	fFlags(0)
{
	CALLED();
	TRACE("BParameterGroup: web = %p, name = \"%s\"\n", web, name);

	fName = strndup(name, 255);

	fControls = new BList();
	fGroups = new BList();
}


/**
 * @brief Destructor. Deletes all owned parameters and sub-groups.
 */
BParameterGroup::~BParameterGroup()
{
	CALLED();

	for (int i = fControls->CountItems(); i-- > 0;) {
		delete static_cast<BParameter*>(fControls->ItemAt(i));
	}
	delete fControls;

	for (int i = fGroups->CountItems(); i-- > 0;) {
		delete static_cast<BParameterGroup*>(fGroups->ItemAt(i));
	}
	delete fGroups;

	free(fName);
}


/**
 * @brief Return the owning BParameterWeb.
 *
 * @return Pointer to the parent web.
 */
BParameterWeb*
BParameterGroup::Web() const
{
	return fWeb;
}


/**
 * @brief Return the name of this group.
 *
 * @return Pointer to the group name string.
 */
const char*
BParameterGroup::Name() const
{
	return fName;
}


/**
 * @brief Set the group flags (e.g. B_HIDDEN_PARAMETER).
 *
 * @param flags  New flags bitmask.
 */
void
BParameterGroup::SetFlags(uint32 flags)
{
	fFlags = flags;
}


/**
 * @brief Return the current group flags.
 *
 * @return Flags bitmask.
 */
uint32
BParameterGroup::Flags() const
{
	return fFlags;
}


/**
 * @brief Create a BNullParameter and add it to this group.
 *
 * @param id         Parameter ID.
 * @param mediaType  Media type (e.g. B_MEDIA_RAW_AUDIO).
 * @param name       Parameter name.
 * @param kind       Parameter kind string (e.g. B_WEB_PHYSICAL_INPUT).
 * @return Pointer to the new BNullParameter, or NULL on allocation failure.
 */
BNullParameter*
BParameterGroup::MakeNullParameter(int32 id, media_type mediaType,
	const char* name, const char* kind)
{
	CALLED();

	BNullParameter* parameter = new(std::nothrow) BNullParameter(id, mediaType,
		fWeb, name, kind);
	if (parameter == NULL)
		return NULL;

	parameter->fGroup = this;
	fControls->AddItem(parameter);

	return parameter;
}


/**
 * @brief Create a BContinuousParameter and add it to this group.
 *
 * @param id         Parameter ID.
 * @param mediaType  Media type.
 * @param name       Parameter name.
 * @param kind       Parameter kind string (e.g. B_GAIN).
 * @param unit       Unit string (e.g. "dB").
 * @param minimum    Minimum value.
 * @param maximum    Maximum value.
 * @param stepping   Value step size.
 * @return Pointer to the new BContinuousParameter, or NULL on failure.
 */
BContinuousParameter*
BParameterGroup::MakeContinuousParameter(int32 id, media_type mediaType,
	const char* name, const char* kind, const char* unit,
	float minimum, float maximum, float stepping)
{
	CALLED();

	BContinuousParameter* parameter
		= new(std::nothrow) BContinuousParameter(id, mediaType, fWeb, name,
			kind, unit, minimum, maximum, stepping);
	if (parameter == NULL)
		return NULL;

	parameter->fGroup = this;
	fControls->AddItem(parameter);

	return parameter;
}


/**
 * @brief Create a BDiscreteParameter and add it to this group.
 *
 * @param id         Parameter ID.
 * @param mediaType  Media type.
 * @param name       Parameter name.
 * @param kind       Parameter kind string (e.g. B_MUTE).
 * @return Pointer to the new BDiscreteParameter, or NULL on failure.
 */
BDiscreteParameter*
BParameterGroup::MakeDiscreteParameter(int32 id, media_type mediaType,
	const char* name, const char* kind)
{
	CALLED();

	BDiscreteParameter* parameter = new(std::nothrow) BDiscreteParameter(id,
		mediaType, fWeb, name, kind);
	if (parameter == NULL)
		return NULL;

	parameter->fGroup = this;
	fControls->AddItem(parameter);

	return parameter;
}


/**
 * @brief Create a BTextParameter and add it to this group.
 *
 * @param id         Parameter ID.
 * @param mediaType  Media type.
 * @param name       Parameter name.
 * @param kind       Parameter kind string.
 * @param maxBytes   Maximum text length in bytes.
 * @return Pointer to the new BTextParameter, or NULL on failure.
 */
BTextParameter*
BParameterGroup::MakeTextParameter(int32 id, media_type mediaType,
	const char* name, const char* kind, size_t maxBytes)
{
	CALLED();

	BTextParameter* parameter = new(std::nothrow) BTextParameter(id, mediaType,
		fWeb, name, kind, maxBytes);
	if (parameter == NULL)
		return NULL;

	parameter->fGroup = this;
	fControls->AddItem(parameter);

	return parameter;
}


/**
 * @brief Create a child BParameterGroup and add it to this group.
 *
 * @param name  Name for the sub-group.
 * @return Pointer to the new BParameterGroup, or NULL on failure.
 */
BParameterGroup*
BParameterGroup::MakeGroup(const char* name)
{
	CALLED();

	BParameterGroup* group = new(std::nothrow) BParameterGroup(fWeb, name);
	if (group != NULL)
		fGroups->AddItem(group);

	return group;
}


/**
 * @brief Return the number of parameters directly owned by this group.
 *
 * @return Parameter count (not including sub-groups).
 */
int32
BParameterGroup::CountParameters()
{
	return fControls->CountItems();
}


/**
 * @brief Return the parameter at the given index within this group.
 *
 * @param index  Zero-based index.
 * @return Pointer to the BParameter, or NULL if out of range.
 */
BParameter*
BParameterGroup::ParameterAt(int32 index)
{
	return static_cast<BParameter*>(fControls->ItemAt(index));
}


/**
 * @brief Return the number of child groups.
 *
 * @return Sub-group count.
 */
int32
BParameterGroup::CountGroups()
{
	return fGroups->CountItems();
}


/**
 * @brief Return the child group at the given index.
 *
 * @param index  Zero-based index.
 * @return Pointer to the BParameterGroup, or NULL if out of range.
 */
BParameterGroup*
BParameterGroup::GroupAt(int32 index)
{
	return static_cast<BParameterGroup*>(fGroups->ItemAt(index));
}


/**
 * @brief Return false; group flattened size is variable.
 *
 * @return Always false.
 */
bool
BParameterGroup::IsFixedSize() const
{
	return false;
}


/**
 * @brief Return the type code used when flattening this group.
 *
 * @return B_MEDIA_PARAMETER_GROUP_TYPE.
 */
type_code
BParameterGroup::TypeCode() const
{
	return B_MEDIA_PARAMETER_GROUP_TYPE;
}


/**
 * @brief Return the number of bytes needed to flatten this group.
 *
 * @return Total flattened size including all parameters and sub-groups.
 */
ssize_t
BParameterGroup::FlattenedSize() const
{
	CALLED();

	/*
		//--------BEGIN-CORE-BPARAMETERGROUP-STRUCT-----------
		?? (0x03040507 OR 0x03040509 depending if the flags field is included or not???): 4 bytes
		(possible) Flags: 4 bytes
		Name String Length: 1 byte (??)
			Name String: 'Name String Length' bytes
		Param Count: 4 bytes
			//for each Param BEGIN
			Pointer: 4 bytes
			Parameter Type: 4 bytes
			Flattened Parameter Size: 4 bytes
			Flattened Parameter: 'Flattened Parameter Size' bytes
			//for each Param END
		Subgroup Count: 4 bytes
			//for each SubGroup BEGIN
			Pointer: 4 bytes
			MEDIA PARAMETER GROUP TYPE('BMCG' (opposite byte order in file)): 4 bytes
			Flattened Group Size: 4 bytes
			Flattened Group: 'Flattened Group Size' bytes
			//for each SubGroup END

		//---------END-CORE-BPARAMETERGROUP-STRUCT--------------
	*/
	//13 guaranteed bytes, variable after that.
	ssize_t size = 13;

	if (fFlags != 0)
		size += sizeof(uint32);

	if (fName != NULL)
		size += min_c(strlen(fName), 255);

	int limit = fControls->CountItems();
	for (int i = 0; i < limit; i++) {
		BParameter* parameter = static_cast<BParameter*>(fControls->ItemAt(i));
		if (parameter != NULL) {
			// overhead for each parameter flattened
			size += sizeof(BParameter*) + sizeof(BParameter::media_parameter_type)
				 + sizeof(ssize_t) + parameter->FlattenedSize();
		}
	}

	limit = fGroups->CountItems();
	for (int i = 0; i < limit; i++) {
		BParameterGroup* group
			= static_cast<BParameterGroup*>(fGroups->ItemAt(i));
		if (group != NULL) {
			// overhead for each group flattened
			size += sizeof(BParameterGroup*) + sizeof(type_code)
				+ sizeof(ssize_t) + group->FlattenedSize();
		}
	}

	return size;
}


/**
 * @brief Serialise this group (parameters and sub-groups) into @p buffer.
 *
 * @param buffer  Destination buffer (must be >= FlattenedSize()).
 * @param size    Capacity in bytes.
 * @return B_OK on success, B_NO_INIT or B_NO_MEMORY on error.
 */
status_t
BParameterGroup::Flatten(void* buffer, ssize_t size) const
{
	CALLED();

	if (buffer == NULL) {
		ERROR("BParameterGroup::Flatten buffer is NULL\n");
		return B_NO_INIT;
	}

	// NOTICE: It is important that this value is the size returned by
	// BParameterGroup::FlattenedSize, not by a descendent's override of this method.
	ssize_t actualSize = BParameterGroup::FlattenedSize();
	if (size < actualSize) {
		ERROR("BParameterGroup::Flatten size to small\n");
		return B_NO_MEMORY;
	}

	if (fFlags != 0) {
		write_to_buffer<int32>(&buffer, kBufferGroupMagic);
		write_to_buffer<uint32>(&buffer, fFlags);
	} else
		write_to_buffer<int32>(&buffer, kBufferGroupMagicNoFlags);

	write_string_to_buffer(&buffer, fName);

	int32 count = fControls->CountItems();
	write_to_buffer<int32>(&buffer, count);

	for (int32 i = 0; i < count; i++) {
		BParameter* parameter = static_cast<BParameter*>(fControls->ItemAt(i));
		if (parameter == NULL) {
			ERROR("BParameterGroup::Flatten(): NULL parameter\n");
			continue;
		}

		write_to_buffer<BParameter*>(&buffer, parameter);
		write_to_buffer<BParameter::media_parameter_type>(&buffer,
			parameter->Type());

		// flatten parameter into this buffer

		ssize_t parameterSize = parameter->FlattenedSize();
		write_to_buffer<ssize_t>(&buffer, parameterSize);

		status_t status = parameter->Flatten(buffer, parameterSize);
			// we have only that much bytes left to write in this buffer
		if (status < B_OK)
			return status;

		skip_in_buffer(&buffer, parameterSize);
	}

	count = fGroups->CountItems();
	write_to_buffer<int32>(&buffer, count);

	for (int32 i = 0; i < count; i++) {
		BParameterGroup* group
			= static_cast<BParameterGroup*>(fGroups->ItemAt(i));
		if (group == NULL) {
			ERROR("BParameterGroup::Flatten(): NULL group\n");
			continue;
		}

		write_to_buffer<BParameterGroup*>(&buffer, group);
		write_to_buffer<type_code>(&buffer, group->TypeCode());

		// flatten sub group into this buffer

		ssize_t groupSize = group->FlattenedSize();
		write_to_buffer<ssize_t>(&buffer, groupSize);

		status_t status = group->Flatten(buffer, groupSize);
			// we have only that much bytes left to write in this buffer
		if (status < B_OK)
			return status;

		skip_in_buffer(&buffer, groupSize);
	}

	return B_OK;
}


/**
 * @brief Return true if @p code matches this group's type code.
 *
 * @param code  Type code to test.
 * @return true if code == B_MEDIA_PARAMETER_GROUP_TYPE.
 */
bool
BParameterGroup::AllowsTypeCode(type_code code) const
{
	return code == TypeCode();
}


/**
 * @brief Restore this group from a flat buffer, creating parameters and sub-groups.
 *
 * @param code    Must match TypeCode().
 * @param buffer  Source buffer.
 * @param size    Size of @p buffer in bytes.
 * @return B_OK on success, or an error code on malformed data.
 */
status_t
BParameterGroup::Unflatten(type_code code, const void* buffer, ssize_t size)
{
	CALLED();

	if (!AllowsTypeCode(code)) {
		ERROR("BParameterGroup::Unflatten() wrong type code\n");
		return B_BAD_TYPE;
	}

	if (buffer == NULL) {
		ERROR("BParameterGroup::Unflatten() buffer is NULL\n");
		return B_NO_INIT;
	}

	// if the buffer is smaller than the size needed to read the
	// signature field, then there is a problem
	if (size < static_cast<ssize_t>(sizeof(int32))) {
		ERROR("BParameterGroup::Unflatten() size to small\n");
		return B_ERROR;
	}

	const void* bufferStart = buffer;
		// used to compute the rest length of the buffer when needed

	uint32 magic = read_from_buffer<uint32>(&buffer);
	bool isSwapped = false;

	if (magic == B_SWAP_INT32(kBufferGroupMagic)
		|| magic == B_SWAP_INT32(kBufferGroupMagicNoFlags)) {
		isSwapped = true;
		magic = B_SWAP_INT32(magic);
	}

	if (magic == kBufferGroupMagic)
		fFlags = read_from_buffer_swap32<int32>(&buffer, isSwapped);
	else if (magic == kBufferGroupMagicNoFlags)
		fFlags = 0;
	else
		return B_BAD_TYPE;

	if (read_string_from_buffer(&buffer, &fName,
			size - (ssize_t)((uint8*)buffer - (uint8*)bufferStart)) < B_OK)
		return B_BAD_VALUE;

	// Clear all existing parameters/subgroups
	for (int32 i = 0; i < fControls->CountItems(); i++) {
		delete static_cast<BParameter*>(fControls->ItemAt(i));
	}
	fControls->MakeEmpty();

	for (int32 i = 0; i < fGroups->CountItems(); i++) {
		delete static_cast<BParameterGroup*>(fGroups->ItemAt(i));
	}
	fGroups->MakeEmpty();

	// unflatten parameter list

	int32 count = read_from_buffer_swap32<int32>(&buffer, isSwapped);
	if (count < 0 || count * kAdditionalParameterSize
			> size_left(size, bufferStart, buffer))
		return B_BAD_VALUE;

	for (int32 i = 0; i < count; i++) {
		// make sure we can read as many bytes
		if (size_left(size, bufferStart, buffer) < (ssize_t)(
				sizeof(BParameter*) + sizeof(BParameter::media_parameter_type)
				+ sizeof(ssize_t))) {
			return B_BAD_VALUE;
		}

		BParameter* oldPointer = read_pointer_from_buffer_swap<BParameter*>(
			&buffer, isSwapped);
		BParameter::media_parameter_type mediaType
			= read_from_buffer_swap32<BParameter::media_parameter_type>(&buffer,
				isSwapped);

		ssize_t parameterSize = read_pointer_from_buffer_swap<ssize_t>(&buffer,
			isSwapped);
		if (parameterSize > size_left(size, bufferStart, buffer))
			return B_BAD_VALUE;

		BParameter* parameter = MakeControl(mediaType);
		if (parameter == NULL) {
			ERROR("BParameterGroup::Unflatten(): MakeControl() failed\n");
			return B_ERROR;
		}

		status_t status = parameter->Unflatten(parameter->TypeCode(), buffer,
			parameterSize);
		if (status < B_OK) {
			ERROR("BParameterGroup::Unflatten(): parameter->Unflatten() failed\n");
			delete parameter;
			return status;
		}

		skip_in_buffer(&buffer, parameterSize);

		// add the item to the list
		parameter->fGroup = this;
		parameter->fWeb = fWeb;
		fControls->AddItem(parameter);

		// add it's old pointer value to the RefFix list kept by the owner web
		if (fWeb != NULL)
			fWeb->AddRefFix(oldPointer, parameter);
	}

	// unflatten sub groups

	count = read_from_buffer_swap32<int32>(&buffer, isSwapped);
	if (count < 0 || count * kAdditionalParameterGroupSize
			> size_left(size, bufferStart, buffer))
		return B_BAD_VALUE;

	for (int32 i = 0; i < count; i++) {
		// make sure we can read as many bytes
		if (size_left(size, bufferStart, buffer) < (ssize_t)(
				sizeof(BParameterGroup*) + sizeof(type_code)
				+ sizeof(ssize_t))) {
			return B_BAD_VALUE;
		}

		BParameterGroup* oldPointer = read_pointer_from_buffer_swap<
			BParameterGroup*>(&buffer, isSwapped);
		type_code type = read_from_buffer_swap32<type_code>(&buffer, isSwapped);

		ssize_t groupSize
			= read_pointer_from_buffer_swap<ssize_t>(&buffer, isSwapped);
		if (groupSize > size_left(size, bufferStart, buffer))
			return B_BAD_VALUE;

		BParameterGroup* group = new BParameterGroup(fWeb, "sub-unnamed");
		if (group == NULL) {
			ERROR("BParameterGroup::Unflatten(): MakeGroup() failed\n");
			return B_ERROR;
		}

		status_t status = group->Unflatten(type, buffer, groupSize);
		if (status != B_OK) {
			ERROR("BParameterGroup::Unflatten(): group->Unflatten() failed\n");
			delete group;
			return status;
		}

		skip_in_buffer(&buffer, groupSize);

		fGroups->AddItem(group);

		// add it's old pointer value to the RefFix list kept by the owner web
		if (fWeb != NULL)
			fWeb->AddRefFix(oldPointer, group);
	}

	return B_OK;
}


/*!	Creates an uninitialized parameter of the specified type.
	Unlike the BParameterGroup::MakeXXXParameter() type of methods, this
	method does not add the parameter to this group automatically.
*/
/**
 * @brief Create an uninitialised parameter of the given type without adding it.
 *
 * Used internally during Unflatten() to allocate the correct subclass before
 * calling its own Unflatten().
 *
 * @param type  One of BParameter::B_NULL_PARAMETER, B_DISCRETE_PARAMETER,
 *              B_CONTINUOUS_PARAMETER, or B_TEXT_PARAMETER.
 * @return A new BParameter subclass, or NULL for unknown types.
 */
BParameter*
BParameterGroup::MakeControl(int32 type)
{
	CALLED();

	switch (type) {
		case BParameter::B_NULL_PARAMETER:
			return new BNullParameter(-1, B_MEDIA_NO_TYPE, NULL, NULL, NULL);

		case BParameter::B_DISCRETE_PARAMETER:
			return new BDiscreteParameter(-1, B_MEDIA_NO_TYPE, NULL, NULL,
				NULL);

		case BParameter::B_CONTINUOUS_PARAMETER:
			return new BContinuousParameter(-1, B_MEDIA_NO_TYPE, NULL, NULL,
				NULL, NULL, 0, 0, 0);

		case BParameter::B_TEXT_PARAMETER:
			return new BTextParameter(-1, B_MEDIA_NO_TYPE, NULL, NULL, NULL, 0);

		default:
			ERROR("BParameterGroup::MakeControl unknown type %" B_PRId32 "\n",
				type);
			return NULL;
	}
}


//	#pragma mark - BParameter


/**
 * @brief Return the media parameter type of this parameter.
 *
 * @return One of B_NULL_PARAMETER, B_CONTINUOUS_PARAMETER, B_DISCRETE_PARAMETER,
 *         or B_TEXT_PARAMETER.
 */
BParameter::media_parameter_type
BParameter::Type() const
{
	return fType;
}


/**
 * @brief Return the owning BParameterWeb.
 *
 * @return Pointer to the parent web, or NULL.
 */
BParameterWeb*
BParameter::Web() const
{
	return fWeb;
}


/**
 * @brief Return the owning BParameterGroup.
 *
 * @return Pointer to the parent group, or NULL.
 */
BParameterGroup*
BParameter::Group() const
{
	return fGroup;
}


/**
 * @brief Return the human-readable name of this parameter.
 *
 * @return Pointer to the name string.
 */
const char*
BParameter::Name() const
{
	return fName;
}


/**
 * @brief Return the kind string of this parameter (e.g. B_GAIN).
 *
 * @return Pointer to the kind string.
 */
const char*
BParameter::Kind() const
{
	return fKind;
}


/**
 * @brief Return the unit string of this parameter (e.g. "dB").
 *
 * @return Pointer to the unit string, or NULL.
 */
const char*
BParameter::Unit() const
{
	return fUnit;
}


/**
 * @brief Return the unique ID of this parameter within its web.
 *
 * @return Parameter ID.
 */
int32
BParameter::ID() const
{
	return fID;
}


/**
 * @brief Set the parameter display flags (e.g. B_HIDDEN_PARAMETER).
 *
 * @param flags  New flags bitmask.
 */
void
BParameter::SetFlags(uint32 flags)
{
	fFlags = flags;
}


/**
 * @brief Return the current display flags.
 *
 * @return Flags bitmask.
 */
uint32
BParameter::Flags() const
{
	return fFlags;
}


/**
 * @brief Retrieve the current value of this parameter from the node.
 *
 * Sends a CONTROLLABLE_GET_PARAMETER_DATA IPC request to the node.
 * Uses an area for transfers larger than MAX_PARAMETER_DATA bytes.
 *
 * @param buffer  Destination buffer for the parameter value.
 * @param _size   In: buffer capacity; out: actual bytes written.
 * @param _when   If non-NULL, receives the timestamp of the last value change.
 * @return B_OK on success, or an IPC/Media Kit error code.
 */
status_t
BParameter::GetValue(void* buffer, size_t* _size, bigtime_t* _when)
{
	CALLED();

	if (buffer == NULL || _size == NULL)
		return B_BAD_VALUE;

	size_t size = *_size;
	if (size <= 0)
		return B_NO_MEMORY;

	if (fWeb == NULL) {
		ERROR("BParameter::GetValue: no parent BParameterWeb\n");
		return B_NO_INIT;
	}

	media_node node = fWeb->Node();
	if (IS_INVALID_NODE(node)) {
		ERROR("BParameter::GetValue: the parent BParameterWeb is not assigned to a BMediaNode\n");
		return B_NO_INIT;
	}

	controllable_get_parameter_data_request request;
	controllable_get_parameter_data_reply reply;

	area_id area;
	void* data;
	if (size > MAX_PARAMETER_DATA) {
		// create an area if large data needs to be transfered
		area = create_area("get parameter data", &data, B_ANY_ADDRESS,
			ROUND_UP_TO_PAGE(size), B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
		if (area < B_OK) {
			ERROR("BParameter::GetValue can't create area of %ld bytes\n",
				size);
			return B_NO_MEMORY;
		}
	} else {
		area = -1;
		data = reply.raw_data;
	}

	request.parameter_id = fID;
	request.request_size = size;
	request.area = area;

	status_t status = QueryPort(node.port, CONTROLLABLE_GET_PARAMETER_DATA,
		&request, sizeof(request), &reply, sizeof(reply));
	if (status == B_OK) {
		// we don't want to copy more than the buffer provides
		if (reply.size < size)
			size = reply.size;

		memcpy(buffer, data, size);

		// store reported values

		*_size = reply.size;
		if (_when != NULL)
			*_when = reply.last_change;
	} else {
		ERROR("BParameter::GetValue parameter '%s' querying node %d, "
			"port %d failed: %s\n",  fName, (int)node.node, (int)node.port,
			strerror(status));
	}

	if (area >= B_OK)
		delete_area(area);

	return status;
}


/**
 * @brief Send a new value for this parameter to the node.
 *
 * Sends a CONTROLLABLE_SET_PARAMETER_DATA IPC request to the node.
 * Uses an area for transfers larger than MAX_PARAMETER_DATA bytes.
 *
 * @param buffer  Source buffer containing the new value.
 * @param size    Size of @p buffer in bytes.
 * @param when    Performance time at which the value should take effect.
 * @return B_OK on success, or an IPC/Media Kit error code.
 */
status_t
BParameter::SetValue(const void* buffer, size_t size, bigtime_t when)
{
	CALLED();

	if (buffer == 0)
		return B_BAD_VALUE;
	if (size <= 0)
		return B_NO_MEMORY;

	if (fWeb == 0) {
		ERROR("BParameter::SetValue: no parent BParameterWeb\n");
		return B_NO_INIT;
	}

	media_node node = fWeb->Node();
	if (IS_INVALID_NODE(node)) {
		ERROR("BParameter::SetValue: the parent BParameterWeb is not assigned "
			"to a BMediaNode\n");
		return B_NO_INIT;
	}

	controllable_set_parameter_data_request request;
	controllable_set_parameter_data_reply reply;
	area_id area;
	void* data;

	if (size > MAX_PARAMETER_DATA) {
		// create an area if large data needs to be transfered
		area = create_area("set parameter data", &data, B_ANY_ADDRESS,
			ROUND_UP_TO_PAGE(size), B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
		if (area < B_OK) {
			ERROR("BParameter::SetValue can't create area of %ld bytes\n", size);
			return B_NO_MEMORY;
		}
	} else {
		area = -1;
		data = request.raw_data;
	}

	memcpy(data, buffer, size);
	request.parameter_id = fID;
	request.when = when;
	request.area = area;
	request.size = size;

	status_t status = QueryPort(node.port, CONTROLLABLE_SET_PARAMETER_DATA,
		&request, sizeof(request), &reply, sizeof(reply));
	if (status != B_OK) {
		ERROR("BParameter::SetValue querying node failed: %s\n",
			strerror(status));
	}

	if (area != -1)
		delete_area(area);

	return status;
}


/**
 * @brief Return the number of audio/video channels this parameter controls.
 *
 * @return Channel count.
 */
int32
BParameter::CountChannels()
{
	return fChannels;
}


/**
 * @brief Set the number of channels this parameter controls.
 *
 * @param count  New channel count.
 */
void
BParameter::SetChannelCount(int32 count)
{
	fChannels = count;
}


/**
 * @brief Return the media type associated with this parameter.
 *
 * @return The media_type value.
 */
media_type
BParameter::MediaType()
{
	return fMediaType;
}


/**
 * @brief Set the media type associated with this parameter.
 *
 * @param type  New media type.
 */
void
BParameter::SetMediaType(media_type type)
{
	fMediaType = type;
}


/**
 * @brief Return the number of input parameters feeding this parameter.
 *
 * @return Input count.
 */
int32
BParameter::CountInputs()
{
	return fInputs->CountItems();
}


/**
 * @brief Return the input parameter at the given index.
 *
 * @param index  Zero-based index.
 * @return Pointer to the input BParameter, or NULL.
 */
BParameter*
BParameter::InputAt(int32 index)
{
	return static_cast<BParameter*>(fInputs->ItemAt(index));
}


/**
 * @brief Add @p input as an input to this parameter (and this as an output of @p input).
 *
 * Silently ignores NULL inputs and duplicates.
 *
 * @param input  The parameter to add as an input.
 */
void
BParameter::AddInput(BParameter* input)
{
	CALLED();

	// BeBook has this method returning a status value,
	// but it should be updated
	if (input == NULL)
		return;

	if (fInputs->HasItem(input)) {
		// if already in input list, don't duplicate.
		return;
	}

	fInputs->AddItem(input);
	input->AddOutput(this);
}


/**
 * @brief Return the number of output parameters driven by this parameter.
 *
 * @return Output count.
 */
int32
BParameter::CountOutputs()
{
	return fOutputs->CountItems();
}


/**
 * @brief Return the output parameter at the given index.
 *
 * @param index  Zero-based index.
 * @return Pointer to the output BParameter, or NULL.
 */
BParameter*
BParameter::OutputAt(int32 index)
{
	return static_cast<BParameter*>(fOutputs->ItemAt(index));
}


/**
 * @brief Add @p output as an output of this parameter (and this as an input of @p output).
 *
 * Silently ignores NULL outputs and duplicates.
 *
 * @param output  The parameter to add as an output.
 */
void
BParameter::AddOutput(BParameter* output)
{
	CALLED();

	// BeBook has this method returning a status value,
	// but it should be updated
	if (output == NULL)
		return;

	if (fOutputs->HasItem(output)) {
		// if already in output list, don't duplicate.
		return;
	}

	fOutputs->AddItem(output);
	output->AddInput(this);
}


/**
 * @brief Return false; parameter flattened size is variable.
 *
 * @return Always false.
 */
bool
BParameter::IsFixedSize() const
{
	return false;
}


/**
 * @brief Return the type code used when flattening this parameter.
 *
 * @return B_MEDIA_PARAMETER_TYPE.
 */
type_code
BParameter::TypeCode() const
{
	return B_MEDIA_PARAMETER_TYPE;
}


/**
 * @brief Return the number of bytes needed to flatten this parameter.
 *
 * @return Total flattened size including name, kind, unit, and pointer lists.
 */
ssize_t
BParameter::FlattenedSize() const
{
	CALLED();
	/*
		?? (0x02040607): 4 bytes
		BParameter Struct Size (in bytes): 4 bytes
		ID: 4 bytes
		Name String Length: 1 byte (??)
			Name String: 'Name String Length' bytes
		Kind String Length: 1 byte (??)
			Kind String: 'Kind String Length' bytes
		Unit String Length: 1 byte (??)
			Unit String: 'Unit String Length' bytes
		Inputs Count: 4 bytes
			Inputs (pointers): ('Inputs Count')*4 bytes
		Outputs Count: 4 bytes
			Outputs (pointers): ('Outputs Count')*4 bytes
		Media Type: 4 bytes
		ChannelCount: 4 bytes
		Flags: 4 bytes
	*/
	//35 bytes are guaranteed, after that, add the variable length parts.
	ssize_t size = kAdditionalParameterSize;

	if (fName != NULL)
		size += strlen(fName);
	if (fKind != NULL)
		size += strlen(fKind);
	if (fUnit != NULL)
		size += strlen(fUnit);

	size += fInputs->CountItems() * sizeof(BParameter*);
	size += fOutputs->CountItems() * sizeof(BParameter*);

	return size;
}


/**
 * @brief Serialise this parameter's base fields into @p buffer.
 *
 * @param buffer  Destination buffer (must be >= FlattenedSize()).
 * @param size    Capacity in bytes.
 * @return B_OK on success, B_NO_INIT or B_NO_MEMORY on error.
 */
status_t
BParameter::Flatten(void* buffer, ssize_t size) const
{
	CALLED();

	if (buffer == NULL) {
		ERROR("BParameter::Flatten buffer is NULL\n");
		return B_NO_INIT;
	}

	// NOTICE: It is important that this value is the size returned by
	// BParameter::FlattenedSize(), not by a descendent's override of this method.
	ssize_t actualSize = BParameter::FlattenedSize();
	if (size < actualSize) {
		ERROR("BParameter::Flatten(): size too small\n");
		return B_NO_MEMORY;
	}

	write_to_buffer<uint32>(&buffer, kParameterMagic);
	write_to_buffer<ssize_t>(&buffer, actualSize);
	write_to_buffer<int32>(&buffer, fID);

	write_string_to_buffer(&buffer, fName);
	write_string_to_buffer(&buffer, fKind);
	write_string_to_buffer(&buffer, fUnit);

	// flatten and write the list of inputs
	ssize_t count = fInputs->CountItems();
	write_to_buffer<ssize_t>(&buffer, count);

	if (count > 0) {
		memcpy(buffer, fInputs->Items(), sizeof(BParameter*) * count);
		skip_in_buffer(&buffer, sizeof(BParameter*) * count);
	}

	// flatten and write the list of outputs
	count = fOutputs->CountItems();
	write_to_buffer<ssize_t>(&buffer, count);

	if (count > 0) {
		memcpy(buffer, fOutputs->Items(), sizeof(BParameter*) * count);
		skip_in_buffer(&buffer, sizeof(BParameter*) * count);
	}

	write_to_buffer<media_type>(&buffer, fMediaType);
	write_to_buffer<int32>(&buffer, fChannels);
	write_to_buffer<uint32>(&buffer, fFlags);

	return B_OK;
}


/**
 * @brief Return true if @p code matches this parameter's type code.
 *
 * @param code  Type code to test.
 * @return true if code == B_MEDIA_PARAMETER_TYPE.
 */
bool
BParameter::AllowsTypeCode(type_code code) const
{
	return code == TypeCode();
}


/**
 * @brief Restore this parameter's base fields from a flat buffer.
 *
 * Input and output pointer lists are stored as raw pointer values and must be
 * fixed up later via FixRefs().
 *
 * @param code    Must match TypeCode().
 * @param buffer  Source buffer.
 * @param size    Size of @p buffer in bytes.
 * @return B_OK on success, or an error code on malformed data.
 */
status_t
BParameter::Unflatten(type_code code, const void* buffer, ssize_t size)
{
	CALLED();

	if (!AllowsTypeCode(code)) {
		ERROR("BParameter::Unflatten(): wrong type code\n");
		return B_BAD_TYPE;
	}

	if (buffer == NULL) {
		ERROR("BParameter::Unflatten(): buffer is NULL\n");
		return B_NO_INIT;
	}

	// if the buffer is smaller than the size needed to read the
	// signature and struct size fields, then there is a problem
	if (size < static_cast<ssize_t>(sizeof(int32) + sizeof(ssize_t))) {
		ERROR("BParameter::Unflatten() size too small\n");
		return B_BAD_VALUE;
	}

	const void* bufferStart = buffer;

	// check magic

	uint32 magic = read_from_buffer<uint32>(&buffer);
	if (magic == B_SWAP_INT32(kParameterMagic))
		fSwapDetected = true;
	else if (magic == kParameterMagic)
		fSwapDetected = false;
	else {
		ERROR("BParameter::Unflatten(): bad magic\n");
		return B_BAD_TYPE;
	}

	ssize_t parameterSize = read_pointer_from_buffer_swap<ssize_t>(&buffer,
		fSwapDetected);
	if (parameterSize > size) {
		ERROR("BParameter::Unflatten(): buffer too small (%ld > %ld)\n",
			parameterSize, size);
		return B_BAD_VALUE;
	}

	// if the struct doesn't meet the minimum size for
	// a flattened BParameter, then return an error.
	// MinFlattenedParamSize =
	// ID (4 bytes)
	// Name String Length (1 byte)
	// Kind String Length (1 byte)
	// Unit String Length (1 byte)
	// Inputs Count (4 bytes)
	// Outputs Count (4 bytes)
	// Media Type (4 bytes)
	// Channel Count (4 bytes)
	// Flags (4 bytes)
	// TOTAL: 27 bytes
	const ssize_t kMinFlattenedParamSize = 15 + 3 * sizeof(ssize_t);
	if (parameterSize < kMinFlattenedParamSize) {
		ERROR("BParameter::Unflatten out of memory (2)\n");
		return B_ERROR;
	}

	fID = read_from_buffer_swap32<int32>(&buffer, fSwapDetected);

	if (read_string_from_buffer(&buffer, &fName,
				size_left(size, bufferStart, buffer)) < B_OK
		|| read_string_from_buffer(&buffer, &fKind,
				size_left(size, bufferStart, buffer)) < B_OK
		|| read_string_from_buffer(&buffer, &fUnit,
				size_left(size, bufferStart, buffer)) < B_OK)
		return B_NO_MEMORY;

	// read the list of inputs

	// it will directly add the pointers in the flattened message to the list;
	// these will be fixed to point to the real inputs/outputs later in FixRefs()

	ssize_t count = read_pointer_from_buffer_swap<ssize_t>(&buffer,
		fSwapDetected);

	fInputs->MakeEmpty();
	for (ssize_t i = 0; i < count; i++) {
		fInputs->AddItem(read_pointer_from_buffer_swap<BParameter * const>(
			&buffer, fSwapDetected));
	}

	// read the list of outputs

	count = read_pointer_from_buffer_swap<ssize_t>(&buffer, fSwapDetected);

	fOutputs->MakeEmpty();
	for (ssize_t i = 0; i < count; i++) {
		fOutputs->AddItem(read_pointer_from_buffer_swap<BParameter * const>(
			&buffer, fSwapDetected));
	}

	fMediaType = read_from_buffer_swap32<media_type>(&buffer, fSwapDetected);
	fChannels = read_from_buffer_swap32<int32>(&buffer, fSwapDetected);
	fFlags = read_from_buffer_swap32<uint32>(&buffer, fSwapDetected);

	return B_OK;
}


/**
 * @brief Protected constructor for BParameter subclasses.
 *
 * @param id         Parameter ID.
 * @param mediaType  Media type.
 * @param type       Parameter type (null, continuous, discrete, or text).
 * @param web        Owning parameter web (may be NULL during Unflatten).
 * @param name       Parameter name (truncated to 255 chars).
 * @param kind       Kind string (truncated to 255 chars).
 * @param unit       Unit string (truncated to 255 chars).
 */
BParameter::BParameter(int32 id, media_type mediaType,
		media_parameter_type type, BParameterWeb* web, const char* name,
		const char* kind, const char* unit)
	:
	fID(id),
	fType(type),
	fWeb(web),
	fGroup(NULL),
	fSwapDetected(true),
	fMediaType(mediaType),
	fChannels(1),
	fFlags(0)
{
	CALLED();

	fName = strndup(name, 255);
	fKind = strndup(kind, 255);
	fUnit = strndup(unit, 255);

	// create empty input/output lists
	fInputs = new BList();
	fOutputs = new BList();
}


/**
 * @brief Destructor. Frees name, kind, unit strings and input/output lists.
 */
BParameter::~BParameter()
{
	CALLED();

	// don't worry about the fWeb/fGroup properties, you don't need
	// to remove yourself from a web/group since the only way in which
	// a parameter is destroyed is when the owner web/group destroys it

	free(fName);
	free(fKind);
	free(fUnit);

	delete fInputs;
	delete fOutputs;
}


/*!	Replaces references to items in the old list with the corresponding
	items in the updated list. The references are replaced in the input
	and output lists.
	This is called by BParameterWeb::Unflatten().
*/
/**
 * @brief Remap serialised pointer values in input/output lists to real objects.
 *
 * After Unflatten(), the input and output lists contain raw pointer values from
 * the flat buffer. This method replaces each such value with the corresponding
 * live object found in @p updated by matching against @p old.
 * Entries that cannot be mapped are removed.
 *
 * @param old      List of original (serialised) pointer values.
 * @param updated  List of corresponding live object pointers.
 */
void
BParameter::FixRefs(BList& old, BList& updated)
{
	CALLED();

	// update inputs

	void** items = static_cast<void**>(fInputs->Items());
	int32 count = fInputs->CountItems();

	for (int32 i = 0; i < count; i++) {
		int32 index = old.IndexOf(items[i]);
		if (index >= 0)
			items[i] = updated.ItemAt(index);
		else {
			ERROR("BParameter::FixRefs(): No mapping found for input");
			items[i] = NULL;
		}
	}

	// remove all NULL inputs (those which couldn't be mapped)

	for (int32 i = count; i-- > 0;) {
		if (items[i] == NULL)
			fInputs->RemoveItem(i);
	}

	// update outputs

	items = static_cast<void **>(fOutputs->Items());
	count = fOutputs->CountItems();

	for (int32 i = 0; i < count; i++) {
		int32 index = old.IndexOf(items[i]);
		if (index >= 0)
			items[i] = updated.ItemAt(index);
		else {
			ERROR("BParameter::FixRefs(): No mapping found for output");
			items[i] = NULL;
		}
	}

	// remove all NULL outputs (those which couldn't be mapped)

	for (int32 i = count; i-- > 0;) {
		if (items[i] == NULL)
			fOutputs->RemoveItem(i);
	}
}


//	#pragma mark - public BContinuousParameter


/**
 * @brief Return the value type used for this parameter (always B_FLOAT_TYPE).
 *
 * @return B_FLOAT_TYPE.
 */
type_code
BContinuousParameter::ValueType()
{
	return B_FLOAT_TYPE;
}


/**
 * @brief Return the minimum allowed value.
 *
 * @return Minimum value as a float.
 */
float
BContinuousParameter::MinValue()
{
	return fMinimum;
}


/**
 * @brief Return the maximum allowed value.
 *
 * @return Maximum value as a float.
 */
float
BContinuousParameter::MaxValue()
{
	return fMaximum;
}


/**
 * @brief Return the value stepping increment.
 *
 * @return Step size as a float.
 */
float
BContinuousParameter::ValueStep()
{
	return fStepping;
}


/**
 * @brief Set the response curve type, factor, and offset for this parameter.
 *
 * @param resp    Response type (B_LINEAR, B_POLYNOMIAL, etc.).
 * @param factor  Scaling factor for the response curve.
 * @param offset  Offset applied to the response curve.
 */
void
BContinuousParameter::SetResponse(int resp, float factor, float offset)
{
	fResponse = static_cast<response>(resp);
	fFactor = factor;
	fOffset = offset;
}


/**
 * @brief Retrieve the response curve type, factor, and offset.
 *
 * @param _resp    If non-NULL, receives the response type.
 * @param _factor  If non-NULL, receives the factor.
 * @param _offset  If non-NULL, receives the offset.
 */
void
BContinuousParameter::GetResponse(int* _resp, float* _factor, float* _offset)
{
	if (_resp != NULL)
		*_resp = fResponse;
	if (_factor != NULL)
		*_factor = fFactor;
	if (_offset != NULL)
		*_offset = fOffset;
}


/**
 * @brief Return the flattened size of this parameter including continuous fields.
 *
 * @return BParameter::FlattenedSize() plus 6 floats/enums.
 */
ssize_t
BContinuousParameter::FlattenedSize() const
{
	CALLED();

	// only adds a fixed amount of bytes
	return BParameter::FlattenedSize() + kAdditionalContinuousParameterSize;
}


/**
 * @brief Serialise this BContinuousParameter (base + min/max/step/response/factor/offset).
 *
 * @param buffer  Destination buffer.
 * @param size    Capacity in bytes.
 * @return B_OK on success, or an error code.
 */
status_t
BContinuousParameter::Flatten(void* buffer, ssize_t size) const
{
	CALLED();

	if (buffer == NULL) {
		ERROR("BContinuousParameter::Flatten(): buffer is NULL\n");
		return B_NO_INIT;
	}

	ssize_t parameterSize = BParameter::FlattenedSize();
	if (size < (parameterSize + kAdditionalContinuousParameterSize)) {
		ERROR("BContinuousParameter::Flatten(): size to small\n");
		return B_NO_MEMORY;
	}

	status_t status = BParameter::Flatten(buffer, size);
	if (status != B_OK) {
		ERROR("BContinuousParameter::Flatten(): BParameter::Flatten() failed\n");
		return status;
	}

	// add our data to the general flattened BParameter

	skip_in_buffer(&buffer, parameterSize);

	write_to_buffer<float>(&buffer, fMinimum);
	write_to_buffer<float>(&buffer, fMaximum);
	write_to_buffer<float>(&buffer, fStepping);
	write_to_buffer<response>(&buffer, fResponse);
	write_to_buffer<float>(&buffer, fFactor);
	write_to_buffer<float>(&buffer, fOffset);

	return B_OK;
}


/**
 * @brief Restore a BContinuousParameter from a flat buffer.
 *
 * @param code    Must match TypeCode().
 * @param buffer  Source buffer.
 * @param size    Size of @p buffer in bytes.
 * @return B_OK on success, or an error code.
 */
status_t
BContinuousParameter::Unflatten(type_code code, const void* buffer,
	ssize_t size)
{
	CALLED();

	// we try to check if the buffer size is long enough to hold an object
	// as early as possible.

	if (!AllowsTypeCode(code)) {
		ERROR("BContinuousParameter::Unflatten wrong type code\n");
		return B_BAD_TYPE;
	}

	if (buffer == NULL) {
		ERROR("BContinuousParameter::Unflatten buffer is NULL\n");
		return B_NO_INIT;
	}

	// if the buffer is smaller than the size needed to read the
	// signature and struct size fields, then there is a problem
	if (size < static_cast<ssize_t>(sizeof(int32) + sizeof(ssize_t))) {
		ERROR("BContinuousParameter::Unflatten size too small\n");
		return B_ERROR;
	}

	status_t status = BParameter::Unflatten(code, buffer, size);
	if (status != B_OK) {
		ERROR("BContinuousParameter::Unflatten(): BParameter::Unflatten "
			"failed: %s\n", strerror(status));
		return status;
	}

	ssize_t parameterSize = BParameter::FlattenedSize();
	skip_in_buffer(&buffer, parameterSize);

	if (size < (parameterSize + kAdditionalContinuousParameterSize)) {
		ERROR("BContinuousParameter::Unflatten(): buffer too small\n");
		return B_BAD_VALUE;
	}

	fMinimum = read_from_buffer_swap32<float>(&buffer, SwapOnUnflatten());
	fMaximum = read_from_buffer_swap32<float>(&buffer, SwapOnUnflatten());
	fStepping = read_from_buffer_swap32<float>(&buffer, SwapOnUnflatten());
	fResponse = read_from_buffer_swap32<response>(&buffer, SwapOnUnflatten());
	fFactor = read_from_buffer_swap32<float>(&buffer, SwapOnUnflatten());
	fOffset = read_from_buffer_swap32<float>(&buffer, SwapOnUnflatten());

	return B_OK;
}


/**
 * @brief Construct a BContinuousParameter with the given range and step.
 *
 * @param id        Parameter ID.
 * @param mediaType Media type.
 * @param web       Owning web.
 * @param name      Name string.
 * @param kind      Kind string.
 * @param unit      Unit string.
 * @param minimum   Minimum value.
 * @param maximum   Maximum value.
 * @param stepping  Value step size.
 */
BContinuousParameter::BContinuousParameter(int32 id, media_type mediaType,
		BParameterWeb* web, const char* name, const char* kind,
		const char* unit, float minimum, float maximum, float stepping)
	: BParameter(id, mediaType, B_CONTINUOUS_PARAMETER, web, name, kind, unit),
	fMinimum(minimum),
	fMaximum(maximum),
	fStepping(stepping),
	fResponse(B_LINEAR),
	fFactor(1.0),
	fOffset(0.0)
{
	CALLED();
}


/**
 * @brief Destructor.
 */
BContinuousParameter::~BContinuousParameter()
{
	CALLED();
}


//	#pragma mark - public BDiscreteParameter


/**
 * @brief Return the value type used for this parameter (always B_INT32_TYPE).
 *
 * @return B_INT32_TYPE.
 */
type_code
BDiscreteParameter::ValueType()
{
	return B_INT32_TYPE;
}


/**
 * @brief Return the number of named value/label pairs.
 *
 * @return Item count.
 */
int32
BDiscreteParameter::CountItems()
{
	return fValues->CountItems();
}


/**
 * @brief Return the label string for the item at @p index.
 *
 * @param index  Zero-based item index.
 * @return Pointer to the label string.
 */
const char*
BDiscreteParameter::ItemNameAt(int32 index)
{
	return reinterpret_cast<const char*>(fSelections->ItemAt(index));
}


/**
 * @brief Return the integer value associated with the item at @p index.
 *
 * @param index  Zero-based item index.
 * @return Integer value, or 0 if the index is out of range.
 */
int32
BDiscreteParameter::ItemValueAt(int32 index)
{
	int32* item = static_cast<int32*>(fValues->ItemAt(index));
	if (item == NULL)
		return 0;

	return *item;
}


/**
 * @brief Add a value/label pair to this parameter.
 *
 * @param value  Integer value for this item.
 * @param name   Display label for this item (truncated to 255 chars).
 * @return B_OK on success, B_NO_MEMORY on allocation failure.
 */
status_t
BDiscreteParameter::AddItem(int32 value, const char* name)
{
	CALLED();

	int32* valueCopy = new(std::nothrow) int32(value);
	if (valueCopy == NULL)
		return B_NO_MEMORY;
	char* nameCopy = strndup(name, 255);
	if (name != NULL && nameCopy == NULL) {
		delete valueCopy;
		return B_NO_MEMORY;
	}

	if (!fValues->AddItem(valueCopy))
		goto err;
	if (!fSelections->AddItem(nameCopy)) {
		fValues->RemoveItem(valueCopy);
		goto err;
	}
	return B_OK;

err:
	free(nameCopy);
	delete valueCopy;
	return B_NO_MEMORY;
}


/**
 * @brief Populate the item list from the names of connected input parameters.
 *
 * @return B_OK always.
 */
status_t
BDiscreteParameter::MakeItemsFromInputs()
{
	CALLED();

	int32 count = fInputs->CountItems();
	for (int32 i = 0; i < count; i++) {
		BParameter* parameter = static_cast<BParameter*>(fInputs->ItemAt(i));
		AddItem(i, parameter->Name());
	}

	return B_OK;
}


/**
 * @brief Populate the item list from the names of connected output parameters.
 *
 * @return B_OK always.
 */
status_t
BDiscreteParameter::MakeItemsFromOutputs()
{
	CALLED();

	int32 count = fOutputs->CountItems();
	for (int32 i = 0; i < count; i++) {
		BParameter* parameter = static_cast<BParameter*>(fOutputs->ItemAt(i));
		AddItem(i, parameter->Name());
	}

	return B_OK;
}


/**
 * @brief Remove all value/label pairs from this parameter.
 */
void
BDiscreteParameter::MakeEmpty()
{
	CALLED();

	for (int32 i = fValues->CountItems(); i-- > 0;) {
		delete static_cast<int32*>(fValues->ItemAt(i));
	}
	fValues->MakeEmpty();

	for (int32 i = fSelections->CountItems(); i-- > 0;) {
		free(static_cast<char*>(fSelections->ItemAt(i)));
	}
	fSelections->MakeEmpty();
}


/**
 * @brief Return the flattened size of this parameter including the item list.
 *
 * @return BParameter::FlattenedSize() plus item count overhead and item data.
 */
ssize_t
BDiscreteParameter::FlattenedSize() const
{
	CALLED();

	ssize_t size = BParameter::FlattenedSize()
		+ kAdditionalDiscreteParameterSize;

	int32 count = fValues->CountItems();
	for (int32 i = 0; i < count; i++) {
		char* selection = static_cast<char*>(fSelections->ItemAt(i));

		if (selection != NULL)
			size += min_c(strlen(selection), 255);

		size += 5;
			// string length + value
	}

	return size;
}


/**
 * @brief Serialise this BDiscreteParameter (base + item list).
 *
 * @param buffer  Destination buffer.
 * @param size    Capacity in bytes.
 * @return B_OK on success, or an error code.
 */
status_t
BDiscreteParameter::Flatten(void* buffer, ssize_t size) const
{
	CALLED();

	if (buffer == NULL) {
		ERROR("BDiscreteParameter::Flatten(): buffer is NULL\n");
		return B_NO_INIT;
	}

	ssize_t parameterSize = BParameter::FlattenedSize();

	if (size < FlattenedSize()) {
		ERROR("BDiscreteParameter::Flatten(): size too small\n");
		return B_NO_MEMORY;
	}

	status_t status = BParameter::Flatten(buffer, size);
	if (status != B_OK) {
		ERROR("BDiscreteParameter::Flatten(): BParameter::Flatten failed\n");
		return status;
	}

	skip_in_buffer(&buffer, parameterSize);

	int32 count = fValues->CountItems();
	write_to_buffer<int32>(&buffer, count);

	// write out all value/name pairs
	for (int32 i = 0; i < count; i++) {
		const char* selection = static_cast<char*>(fSelections->ItemAt(i));
		const int32* value = static_cast<int32*>(fValues->ItemAt(i));

		write_string_to_buffer(&buffer, selection);
		write_to_buffer<int32>(&buffer, value ? *value : 0);
	}

	return B_OK;
}


/**
 * @brief Restore a BDiscreteParameter from a flat buffer.
 *
 * @param code    Must match TypeCode().
 * @param buffer  Source buffer.
 * @param size    Size of @p buffer in bytes.
 * @return B_OK on success, or an error code.
 */
status_t
BDiscreteParameter::Unflatten(type_code code, const void* buffer, ssize_t size)
{
	CALLED();

	if (!AllowsTypeCode(code)) {
		ERROR("BDiscreteParameter::Unflatten(): bad type code\n");
		return B_BAD_TYPE;
	}

	if (buffer == NULL) {
		ERROR("BDiscreteParameter::Unflatten(): buffer is NULL\n");
		return B_NO_INIT;
	}

	// if the buffer is smaller than the size needed to read the
	// signature and struct size fields, then there is a problem
	if (size < static_cast<ssize_t>(sizeof(int32) + sizeof(ssize_t))) {
		ERROR("BDiscreteParameter::Unflatten(): size too small\n");
		return B_ERROR;
	}

	const void* bufferStart = buffer;

	status_t status = BParameter::Unflatten(code, buffer, size);
	if (status != B_OK) {
		ERROR("BDiscreteParameter::Unflatten(): BParameter::Unflatten failed\n");
		return status;
	}

	ssize_t parameterSize = BParameter::FlattenedSize();
	skip_in_buffer(&buffer, parameterSize);

	if (size < (parameterSize + kAdditionalDiscreteParameterSize)) {
		ERROR("BDiscreteParameter::Unflatten(): buffer too small\n");
		return B_BAD_VALUE;
	}

	int32 count = read_from_buffer_swap32<int32>(&buffer, SwapOnUnflatten());

	// clear any existing name/value pairs
	MakeEmpty();

	for (int32 i = 0; i < count; i++) {
		char* name = NULL;
		if (read_string_from_buffer(&buffer, &name, size_left(size, bufferStart,
				buffer)) < B_OK)
			return B_BAD_DATA;

		if (size_left(size, bufferStart, buffer) < (int)sizeof(int32)) {
			free(name);
			return B_BAD_DATA;
		}

		int32 value = read_from_buffer_swap32<int32>(&buffer,
			SwapOnUnflatten());

		AddItem(value, name);
		free(name);
	}

	return B_OK;
}


/**
 * @brief Construct a BDiscreteParameter with empty item lists.
 *
 * @param id         Parameter ID.
 * @param mediaType  Media type.
 * @param web        Owning web.
 * @param name       Name string.
 * @param kind       Kind string.
 */
BDiscreteParameter::BDiscreteParameter(int32 id, media_type mediaType,
	BParameterWeb* web, const char* name, const char* kind)
	:	BParameter(id, mediaType, B_DISCRETE_PARAMETER, web, name, kind, NULL)
{
	CALLED();

	fSelections = new BList();
	fValues = new BList();
}


/**
 * @brief Destructor. Clears and deletes item lists.
 */
BDiscreteParameter::~BDiscreteParameter()
{
	CALLED();

	MakeEmpty();

	delete fSelections;
	delete fValues;
}


//	#pragma mark - public BTextParameter


/**
 * @brief Return the maximum text value size in bytes.
 *
 * @return Maximum bytes allowed for the text value.
 */
size_t
BTextParameter::MaxBytes() const
{
	return fMaxBytes;
}


/**
 * @brief Return the value type for this parameter (B_FLOAT_TYPE, historical).
 *
 * @return B_FLOAT_TYPE.
 */
type_code
BTextParameter::ValueType()
{
	return B_FLOAT_TYPE;
}


/**
 * @brief Return the flattened size including the MaxBytes field.
 *
 * @return BParameter::FlattenedSize() + sizeof(fMaxBytes).
 */
ssize_t
BTextParameter::FlattenedSize() const
{
	return BParameter::FlattenedSize() + sizeof(fMaxBytes);
}


/**
 * @brief Serialise this BTextParameter (base + MaxBytes).
 *
 * @param buffer  Destination buffer.
 * @param size    Capacity in bytes.
 * @return B_OK on success, or an error code.
 */
status_t
BTextParameter::Flatten(void* buffer, ssize_t size) const
{
	if (buffer == NULL) {
		ERROR("BTextParameter::Flatten(): buffer is NULL\n");
		return B_NO_INIT;
	}

	ssize_t parameterSize = BParameter::FlattenedSize();
	if (size < static_cast<ssize_t>(parameterSize + sizeof(fMaxBytes))) {
		ERROR("BContinuousParameter::Flatten(): size to small\n");
		return B_NO_MEMORY;
	}

	status_t status = BParameter::Flatten(buffer, size);
	if (status != B_OK) {
		ERROR("BTextParameter::Flatten(): BParameter::Flatten() failed\n");
		return status;
	}

	// add our data to the general flattened BParameter

	skip_in_buffer(&buffer, parameterSize);

	write_to_buffer<uint32>(&buffer, fMaxBytes);

	return B_OK;
}


/**
 * @brief Restore a BTextParameter from a flat buffer.
 *
 * @param code    Must match TypeCode().
 * @param buffer  Source buffer.
 * @param size    Size of @p buffer in bytes.
 * @return B_OK on success, or an error code.
 */
status_t
BTextParameter::Unflatten(type_code code, const void* buffer, ssize_t size)
{
	// we try to check if the buffer size is long enough to hold an object
	// as early as possible.

	if (!AllowsTypeCode(code)) {
		ERROR("BTextParameter::Unflatten wrong type code\n");
		return B_BAD_TYPE;
	}

	if (buffer == NULL) {
		ERROR("BTextParameter::Unflatten buffer is NULL\n");
		return B_NO_INIT;
	}

	if (size < static_cast<ssize_t>(sizeof(fMaxBytes))) {
		ERROR("BTextParameter::Unflatten size too small\n");
		return B_ERROR;
	}

	status_t status = BParameter::Unflatten(code, buffer, size);
	if (status != B_OK) {
		ERROR("BTextParameter::Unflatten(): BParameter::Unflatten failed\n");
		return status;
	}

	ssize_t parameterSize = BParameter::FlattenedSize();
	skip_in_buffer(&buffer, parameterSize);

	if (size < static_cast<ssize_t>(parameterSize + sizeof(fMaxBytes))) {
		ERROR("BTextParameter::Unflatten(): buffer too small\n");
		return B_BAD_VALUE;
	}

	fMaxBytes = read_from_buffer_swap32<uint32>(&buffer, SwapOnUnflatten());

	return B_OK;
}


/**
 * @brief Construct a BTextParameter with the given maximum text size.
 *
 * @param id         Parameter ID.
 * @param mediaType  Media type.
 * @param web        Owning web.
 * @param name       Name string.
 * @param kind       Kind string.
 * @param maxBytes   Maximum number of bytes for the text value.
 */
BTextParameter::BTextParameter(int32 id, media_type mediaType,
		BParameterWeb* web, const char* name, const char* kind,
		size_t maxBytes)
	: BParameter(id, mediaType, B_TEXT_PARAMETER, web, name, kind, NULL)
{
	fMaxBytes = maxBytes;
}


/**
 * @brief Destructor.
 */
BTextParameter::~BTextParameter()
{
}


//	#pragma mark - public BNullParameter


/**
 * @brief Return 0; null parameters carry no value.
 *
 * @return 0.
 */
type_code
BNullParameter::ValueType()
{
	// NULL parameters have no value type
	return 0;
}


/**
 * @brief Return the same flattened size as the base BParameter.
 *
 * @return BParameter::FlattenedSize().
 */
ssize_t
BNullParameter::FlattenedSize() const
{
	return BParameter::FlattenedSize();
}


/**
 * @brief Delegate flattening to BParameter::Flatten().
 *
 * @param buffer  Destination buffer.
 * @param size    Capacity in bytes.
 * @return B_OK on success.
 */
status_t
BNullParameter::Flatten(void* buffer, ssize_t size) const
{
	return BParameter::Flatten(buffer, size);
}


/**
 * @brief Delegate unflattening to BParameter::Unflatten().
 *
 * @param code    Type code.
 * @param buffer  Source buffer.
 * @param size    Size of @p buffer.
 * @return B_OK on success.
 */
status_t
BNullParameter::Unflatten(type_code code, const void* buffer, ssize_t size)
{
	return BParameter::Unflatten(code, buffer, size);
}


/**
 * @brief Construct a BNullParameter (a label/separator with no value).
 *
 * @param id         Parameter ID.
 * @param mediaType  Media type.
 * @param web        Owning web.
 * @param name       Name string.
 * @param kind       Kind string.
 */
BNullParameter::BNullParameter(int32 id, media_type mediaType,
		BParameterWeb* web, const char* name, const char* kind)
	: BParameter(id, mediaType, B_NULL_PARAMETER, web, name, kind, NULL)
{
}


/**
 * @brief Destructor.
 */
BNullParameter::~BNullParameter()
{
}


//	#pragma mark - reserved functions


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameterWeb::_Reserved_ControlWeb_0(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameterWeb::_Reserved_ControlWeb_1(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameterWeb::_Reserved_ControlWeb_2(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameterWeb::_Reserved_ControlWeb_3(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameterWeb::_Reserved_ControlWeb_4(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameterWeb::_Reserved_ControlWeb_5(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameterWeb::_Reserved_ControlWeb_6(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameterWeb::_Reserved_ControlWeb_7(void *) { return B_ERROR; }

/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameterGroup::_Reserved_ControlGroup_0(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameterGroup::_Reserved_ControlGroup_1(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameterGroup::_Reserved_ControlGroup_2(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameterGroup::_Reserved_ControlGroup_3(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameterGroup::_Reserved_ControlGroup_4(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameterGroup::_Reserved_ControlGroup_5(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameterGroup::_Reserved_ControlGroup_6(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameterGroup::_Reserved_ControlGroup_7(void *) { return B_ERROR; }

/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameter::_Reserved_Control_0(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameter::_Reserved_Control_1(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameter::_Reserved_Control_2(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameter::_Reserved_Control_3(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameter::_Reserved_Control_4(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameter::_Reserved_Control_5(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameter::_Reserved_Control_6(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BParameter::_Reserved_Control_7(void *) { return B_ERROR; }

/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BContinuousParameter::_Reserved_ContinuousParameter_0(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BContinuousParameter::_Reserved_ContinuousParameter_1(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BContinuousParameter::_Reserved_ContinuousParameter_2(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BContinuousParameter::_Reserved_ContinuousParameter_3(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BContinuousParameter::_Reserved_ContinuousParameter_4(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BContinuousParameter::_Reserved_ContinuousParameter_5(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BContinuousParameter::_Reserved_ContinuousParameter_6(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BContinuousParameter::_Reserved_ContinuousParameter_7(void *) { return B_ERROR; }

/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BDiscreteParameter::_Reserved_DiscreteParameter_0(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BDiscreteParameter::_Reserved_DiscreteParameter_1(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BDiscreteParameter::_Reserved_DiscreteParameter_2(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BDiscreteParameter::_Reserved_DiscreteParameter_3(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BDiscreteParameter::_Reserved_DiscreteParameter_4(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BDiscreteParameter::_Reserved_DiscreteParameter_5(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BDiscreteParameter::_Reserved_DiscreteParameter_6(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BDiscreteParameter::_Reserved_DiscreteParameter_7(void *) { return B_ERROR; }

/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BNullParameter::_Reserved_NullParameter_0(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BNullParameter::_Reserved_NullParameter_1(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BNullParameter::_Reserved_NullParameter_2(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BNullParameter::_Reserved_NullParameter_3(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BNullParameter::_Reserved_NullParameter_4(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BNullParameter::_Reserved_NullParameter_5(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BNullParameter::_Reserved_NullParameter_6(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BNullParameter::_Reserved_NullParameter_7(void *) { return B_ERROR; }

/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BTextParameter::_Reserved_TextParameter_0(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BTextParameter::_Reserved_TextParameter_1(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BTextParameter::_Reserved_TextParameter_2(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BTextParameter::_Reserved_TextParameter_3(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BTextParameter::_Reserved_TextParameter_4(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BTextParameter::_Reserved_TextParameter_5(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BTextParameter::_Reserved_TextParameter_6(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BTextParameter::_Reserved_TextParameter_7(void *) { return B_ERROR; }
