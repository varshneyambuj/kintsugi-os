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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2005-2017 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Lotz, mmlr@mlotz.ch
 */


/**
 * @file Message.cpp
 * @brief Implementation of the BMessage class, the core data container and
 *        inter-application messaging primitive.
 *
 * BMessage is the fundamental unit of communication in the application
 * framework. It carries a command code (@c what) and an arbitrary collection
 * of typed, named data fields. Messages are used for application scripting,
 * drag-and-drop, clipboard transfers, and general inter-thread / inter-team
 * communication via BMessenger and BLooper.
 *
 * Internally, message data is stored in three contiguous buffers: a
 * message_header, an array of field_header descriptors, and a raw data
 * segment. Fields are located by a hash table embedded in the header.
 * Large messages can be transmitted through shared memory areas instead
 * of being copied through ports, using a copy-on-write scheme.
 */


#include <Message.h>
#include <MessageAdapter.h>
#include <MessagePrivate.h>
#include <MessageUtils.h>

#include <DirectMessageTarget.h>
#include <MessengerPrivate.h>
#include <TokenSpace.h>
#include <kernel/util/KMessage.h>

#include <Alignment.h>
#include <Application.h>
#include <AppMisc.h>
#include <BlockCache.h>
#include <GraphicsDefs.h>
#include <MessageQueue.h>
#include <Messenger.h>
#include <Path.h>
#include <Point.h>
#include <String.h>
#include <StringList.h>
#include <StackOrHeapArray.h>

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tracing_config.h"
	// kernel tracing configuration

//#define VERBOSE_DEBUG_OUTPUT
#ifdef VERBOSE_DEBUG_OUTPUT
#define DEBUG_FUNCTION_ENTER	\
	debug_printf("msg thread: %ld; this: %p; header: %p; fields: %p;" \
		" data: %p; what: 0x%08lx '%.4s'; line: %d; func: %s\n", \
		find_thread(NULL), this, fHeader, fFields, fData, what, (char*)&what, \
		__LINE__, __PRETTY_FUNCTION__);

#define DEBUG_FUNCTION_ENTER2	\
	debug_printf("msg thread: %ld; line: %d: func: %s\n", find_thread(NULL), \
		__LINE__, __PRETTY_FUNCTION__);
#else
#define DEBUG_FUNCTION_ENTER	/* nothing */
#define DEBUG_FUNCTION_ENTER2	/* nothing */
#endif

#if BMESSAGE_TRACING
#	define KTRACE(format...)	ktrace_printf(format)
#else
#	define KTRACE(format...)	;
#endif


const char* B_SPECIFIER_ENTRY = "specifiers";
const char* B_PROPERTY_ENTRY = "property";
const char* B_PROPERTY_NAME_ENTRY = "name";


/** @brief Forward declaration of static helper that reads a reply from a port. */
static status_t handle_reply(port_id replyPort, int32* pCode,
	bigtime_t timeout, BMessage* reply);


extern "C" {
	// private os function to set the owning team of an area
	status_t _kern_transfer_area(area_id area, void** _address,
		uint32 addressSpec, team_id target);
}


BBlockCache* BMessage::sMsgCache = NULL;
port_id BMessage::sReplyPorts[sNumReplyPorts];
int32 BMessage::sReplyPortInUse[sNumReplyPorts];


/**
 * @brief Prints a typed value to stdout by calling its PrintToStream() method.
 * @param pointer Raw byte pointer to the typed data.
 * @tparam Type The data type (must have a PrintToStream() member).
 */
template<typename Type>
static void
print_to_stream_type(uint8* pointer)
{
	Type* item = (Type*)pointer;
	item->PrintToStream();
}


/**
 * @brief Prints a typed value to stdout using a printf-style format with two arguments.
 * @param format A printf format string expecting two values (e.g. hex and decimal).
 * @param pointer Raw byte pointer to the typed data.
 * @tparam Type The data type to interpret at the pointer location.
 */
template<typename Type>
static void
print_type(const char* format, uint8* pointer)
{
	Type* item = (Type*)pointer;
	printf(format,* item,* item);
}


/**
 * @brief Prints a typed value to stdout using a printf-style format with three arguments.
 * @param format A printf format string expecting three values (e.g. hex, decimal, char).
 * @param pointer Raw byte pointer to the typed data.
 * @tparam Type The data type to interpret at the pointer location.
 */
template<typename Type>
static void
print_type3(const char* format, uint8* pointer)
{
	Type* item = (Type*)pointer;
	printf(format, *item, *item, *item);
}


/**
 * @brief Reads a reply message from a port, with a timeout.
 *
 * Waits for data on @a replyPort up to @a timeout microseconds, reads the
 * raw buffer, and unflattens it into @a reply. Used internally by the
 * synchronous SendReply / _SendMessage paths.
 *
 * @param replyPort The port to read the reply from.
 * @param _code     Receives the port message code.
 * @param timeout   Maximum time to wait, in microseconds.
 * @param reply     The BMessage to populate with the reply data.
 * @return B_OK on success, or an error code on failure.
 */
static status_t
handle_reply(port_id replyPort, int32* _code, bigtime_t timeout,
	BMessage* reply)
{
	DEBUG_FUNCTION_ENTER2;

	ssize_t size;
	do {
		size = port_buffer_size_etc(replyPort, B_RELATIVE_TIMEOUT, timeout);
	} while (size == B_INTERRUPTED);

	if (size < 0)
		return size;

	BStackOrHeapArray<char, 4096> buffer(size);
	if (!buffer.IsValid())
		return B_NO_MEMORY;

	status_t result;
	do {
		result = read_port(replyPort, _code, buffer, size);
	} while (result == B_INTERRUPTED);

	if (result < 0 || *_code != kPortMessageCode)
		return result < 0 ? result : B_ERROR;

	result = reply->Unflatten(buffer);
	return result;
}


//	#pragma mark -


/**
 * @brief Default constructor; creates an empty message with what = 0.
 *
 * Initializes all internal buffers to NULL and sets up a fresh message
 * header via _InitCommon().
 */
BMessage::BMessage()
{
	DEBUG_FUNCTION_ENTER;
	_InitCommon(true);
}


/**
 * @brief Constructs a message by copying from a pointer to another BMessage.
 * @param other Pointer to the source message to copy.
 * @note Delivery flags (reply-required, was-delivered, etc.) are not copied.
 */
BMessage::BMessage(BMessage* other)
{
	DEBUG_FUNCTION_ENTER;
	_InitCommon(false);
	*this = *other;
}


/**
 * @brief Constructs a message with the given command code.
 * @param _what The message command code (also stored in the public @c what member).
 */
BMessage::BMessage(uint32 _what)
{
	DEBUG_FUNCTION_ENTER;
	if (_InitCommon(true))
		fHeader->what = _what;
	what = _what;
}


/**
 * @brief Copy constructor; creates a deep copy of @a other.
 * @param other The source message to copy.
 * @note Delivery flags (reply-required, was-delivered, etc.) are stripped
 *       from the clone.
 */
BMessage::BMessage(const BMessage& other)
{
	DEBUG_FUNCTION_ENTER;
	_InitCommon(false);
	*this = other;
}


/**
 * @brief Destructor.
 *
 * Clears all data. If a reply is still expected (IsSourceWaiting()), a
 * B_NO_REPLY message is sent automatically before destruction.
 */
BMessage::~BMessage()
{
	DEBUG_FUNCTION_ENTER;
	_Clear();
}


/**
 * @brief Copy-assignment operator; performs a deep copy of @a other.
 *
 * Clears the current contents, then copies the header, field descriptors,
 * and data buffer from @a other. Delivery-related flags are stripped from
 * the copy, and the shared-area reference is not inherited.
 *
 * @param other The source message to copy.
 * @return A reference to this message.
 */
BMessage&
BMessage::operator=(const BMessage& other)
{
	DEBUG_FUNCTION_ENTER;

	if (this == &other)
		return *this;

	_Clear();

	fHeader = (message_header*)malloc(sizeof(message_header));
	if (fHeader == NULL)
		return *this;

	if (other.fHeader == NULL)
		return *this;

	memcpy(fHeader, other.fHeader, sizeof(message_header));

	// Clear some header flags inherited from the original message that don't
	// apply to the clone.
	fHeader->flags &= ~(MESSAGE_FLAG_REPLY_REQUIRED | MESSAGE_FLAG_REPLY_DONE
		| MESSAGE_FLAG_IS_REPLY | MESSAGE_FLAG_WAS_DELIVERED
		| MESSAGE_FLAG_PASS_BY_AREA);
	// Note, that BeOS R5 seems to keep the reply info.

	if (fHeader->field_count > 0) {
		size_t fieldsSize = fHeader->field_count * sizeof(field_header);
		if (other.fFields != NULL)
			fFields = (field_header*)malloc(fieldsSize);

		if (fFields == NULL) {
			fHeader->field_count = 0;
			fHeader->data_size = 0;
		} else if (other.fFields != NULL)
			memcpy(fFields, other.fFields, fieldsSize);
	}

	if (fHeader->data_size > 0) {
		if (other.fData != NULL)
			fData = (uint8*)malloc(fHeader->data_size);

		if (fData == NULL) {
			fHeader->field_count = 0;
			free(fFields);
			fFields = NULL;
		} else if (other.fData != NULL)
			memcpy(fData, other.fData, fHeader->data_size);
	}

	fHeader->what = what = other.what;
	fHeader->message_area = -1;
	fFieldsAvailable = 0;
	fDataAvailable = 0;

	return *this;
}


/**
 * @brief Allocates memory for a BMessage from the block cache.
 * @param size The requested allocation size in bytes.
 * @return Pointer to the allocated memory.
 */
void*
BMessage::operator new(size_t size)
{
	DEBUG_FUNCTION_ENTER2;
	return sMsgCache->Get(size);
}


/**
 * @brief Non-throwing allocation of a BMessage from the block cache.
 * @param size    The requested allocation size in bytes.
 * @param noThrow The std::nothrow tag.
 * @return Pointer to the allocated memory, or NULL on failure.
 */
void*
BMessage::operator new(size_t size, const std::nothrow_t& noThrow)
{
	DEBUG_FUNCTION_ENTER2;
	return sMsgCache->Get(size);
}


/**
 * @brief Placement new operator; returns the provided pointer unchanged.
 * @param size    Unused allocation size.
 * @param pointer The pre-allocated memory location.
 * @return The same @a pointer passed in.
 */
void*
BMessage::operator new(size_t, void* pointer)
{
	DEBUG_FUNCTION_ENTER2;
	return pointer;
}


/**
 * @brief Returns memory for a BMessage back to the block cache.
 * @param pointer The memory to release.
 * @param size    The size of the allocation.
 */
void
BMessage::operator delete(void* pointer, size_t size)
{
	DEBUG_FUNCTION_ENTER2;
	if (pointer == NULL)
		return;
	sMsgCache->Save(pointer, size);
}


/**
 * @brief Compares the data payload of this message with another.
 *
 * Checks whether all fields in this message have identical counterparts
 * (same name, type, count, and raw data) in @a other. Optionally ignores
 * field ordering and recursively compares nested BMessages.
 *
 * @param other            The message to compare against.
 * @param ignoreFieldOrder If true, fields are matched by name regardless of
 *                         their storage order.
 * @param deep             If true, nested B_MESSAGE_TYPE fields are compared
 *                         recursively via HasSameData().
 * @return true if the data payloads are equivalent, false otherwise.
 */
bool
BMessage::HasSameData(const BMessage& other, bool ignoreFieldOrder,
	bool deep) const
{
	if (this == &other)
		return true;

	if (fHeader == NULL)
		return other.fHeader == NULL;

	if (fHeader->field_count != other.fHeader->field_count)
		return false;

	for (uint32 i = 0; i < fHeader->field_count; i++) {
		field_header* field = &fFields[i];
		field_header* otherField = NULL;

		const char* name = (const char*)fData + field->offset;
		if (ignoreFieldOrder) {
			if (other._FindField(name, B_ANY_TYPE, &otherField) != B_OK)
				return false;
		} else {
			otherField = &other.fFields[i];
			if (otherField->name_length != field->name_length)
				return false;

			const char* otherName = (const char*)other.fData
				+ otherField->offset;
			if (strncmp(name, otherName, field->name_length) != 0)
				return false;
		}

		if (otherField->type != field->type
			|| otherField->count != field->count) {
			return false;
		}

		uint8* data = fData + field->offset + field->name_length;
		uint8* otherData = other.fData + otherField->offset
			+ otherField->name_length;

		bool needsMemCompare = true;
		if (deep && field->type == B_MESSAGE_TYPE) {
			BMessage message, otherMessage;
			if (message.Unflatten((const char*)data) == B_OK
				&& otherMessage.Unflatten((const char*)otherData) == B_OK) {
				if (!message.HasSameData(ignoreFieldOrder, deep))
					return false;
				needsMemCompare = false;
			}
		}

		if (needsMemCompare) {
			if (otherField->data_size != field->data_size)
				return false;
			if (memcmp(data, otherData, field->data_size) != 0)
				return false;
		}
	}

	return true;
}


/**
 * @brief Initializes all internal member variables to their default state.
 *
 * Resets pointers (fHeader, fFields, fData, fOriginal, fQueueLink,
 * fArchivingPointer) to NULL, zeroes counters, and sets @c what to 0.
 * Optionally calls _InitHeader() to allocate and populate the message header.
 *
 * @param initHeader If true, _InitHeader() is called to allocate the header.
 * @return B_OK on success, or B_NO_MEMORY if header allocation fails.
 */
status_t
BMessage::_InitCommon(bool initHeader)
{
	DEBUG_FUNCTION_ENTER;
	what = 0;

	fHeader = NULL;
	fFields = NULL;
	fData = NULL;

	fFieldsAvailable = 0;
	fDataAvailable = 0;

	fOriginal = NULL;
	fQueueLink = NULL;

	fArchivingPointer = NULL;

	if (initHeader)
		return _InitHeader();

	return B_OK;
}


/**
 * @brief Allocates (if needed) and initializes the message_header struct.
 *
 * Sets the format to MESSAGE_FORMAT_HAIKU, marks the message as valid, copies
 * the current @c what value, resets the specifier index, and fills the hash
 * table with -1 (indicating empty buckets). Port and token fields for reply
 * routing are set to invalid/null values.
 *
 * @return B_OK on success, or B_NO_MEMORY if allocation fails.
 */
status_t
BMessage::_InitHeader()
{
	DEBUG_FUNCTION_ENTER;
	if (fHeader == NULL) {
		fHeader = (message_header*)malloc(sizeof(message_header));
		if (fHeader == NULL)
			return B_NO_MEMORY;
	}

	memset(fHeader, 0, sizeof(message_header) - sizeof(fHeader->hash_table));

	fHeader->format = MESSAGE_FORMAT_HAIKU;
	fHeader->flags = MESSAGE_FLAG_VALID;
	fHeader->what = what;
	fHeader->current_specifier = -1;
	fHeader->message_area = -1;

	fHeader->target = B_NULL_TOKEN;
	fHeader->reply_target = B_NULL_TOKEN;
	fHeader->reply_port = -1;
	fHeader->reply_team = -1;

	// initializing the hash table to -1 because 0 is a valid index
	fHeader->hash_table_size = MESSAGE_BODY_HASH_TABLE_SIZE;
	memset(&fHeader->hash_table, 255, sizeof(fHeader->hash_table));
	return B_OK;
}


/**
 * @brief Frees all internal resources and resets the message to an
 *        uninitialized state.
 *
 * If a reply is still expected (IsSourceWaiting()), a B_NO_REPLY message is
 * sent before cleanup. If data is backed by a shared area, _Dereference()
 * is called to release it. All heap-allocated buffers (header, fields, data)
 * are freed, and the cached original message is deleted.
 *
 * @return B_OK always.
 */
status_t
BMessage::_Clear()
{
	DEBUG_FUNCTION_ENTER;
	if (fHeader != NULL) {
		// We're going to destroy all information of this message. If there's
		// still someone waiting for a reply to this message, we have to send
		// one now.
		if (IsSourceWaiting())
			SendReply(B_NO_REPLY);

		if (fHeader->message_area >= 0)
			_Dereference();

		free(fHeader);
		fHeader = NULL;
	}

	free(fFields);
	fFields = NULL;
	free(fData);
	fData = NULL;

	fArchivingPointer = NULL;

	fFieldsAvailable = 0;
	fDataAvailable = 0;

	delete fOriginal;
	fOriginal = NULL;

	return B_OK;
}


/**
 * @brief Retrieves information about a field by index, optionally filtered by type.
 *
 * If @a typeRequested is B_ANY_TYPE the field at absolute @a index is
 * returned. Otherwise, the index counts only fields whose type matches
 * @a typeRequested.
 *
 * @param typeRequested Type filter (B_ANY_TYPE for no filter).
 * @param index         Zero-based index into the (filtered) field list.
 * @param nameFound     Receives the field name (may be NULL).
 * @param typeFound     Receives the field's type code (may be NULL).
 * @param countFound    Receives the number of items in the field (may be NULL).
 * @return B_OK on success, B_BAD_INDEX if @a index is out of range,
 *         B_BAD_TYPE if no fields match @a typeRequested, or B_NO_INIT.
 */
status_t
BMessage::GetInfo(type_code typeRequested, int32 index, char** nameFound,
	type_code* typeFound, int32* countFound) const
{
	DEBUG_FUNCTION_ENTER;
	if (fHeader == NULL)
		return B_NO_INIT;

	if (index < 0 || (uint32)index >= fHeader->field_count)
		return B_BAD_INDEX;

	if (typeRequested == B_ANY_TYPE) {
		if (nameFound != NULL)
			*nameFound = (char*)fData + fFields[index].offset;
		if (typeFound != NULL)
			*typeFound = fFields[index].type;
		if (countFound != NULL)
			*countFound = fFields[index].count;
		return B_OK;
	}

	int32 counter = -1;
	field_header* field = fFields;
	for (uint32 i = 0; i < fHeader->field_count; i++, field++) {
		if (field->type == typeRequested)
			counter++;

		if (counter == index) {
			if (nameFound != NULL)
				*nameFound = (char*)fData + field->offset;
			if (typeFound != NULL)
				*typeFound = field->type;
			if (countFound != NULL)
				*countFound = field->count;
			return B_OK;
		}
	}

	if (counter == -1)
		return B_BAD_TYPE;

	return B_BAD_INDEX;
}


/**
 * @brief Retrieves the type and item count of a named field.
 * @param name       The field name to look up.
 * @param typeFound  Receives the field's type code (may be NULL).
 * @param countFound Receives the number of items (may be NULL).
 * @return B_OK on success, or B_NAME_NOT_FOUND / B_NO_INIT on error.
 */
status_t
BMessage::GetInfo(const char* name, type_code* typeFound,
	int32* countFound) const
{
	DEBUG_FUNCTION_ENTER;
	if (countFound != NULL)
		*countFound = 0;

	field_header* field = NULL;
	status_t result = _FindField(name, B_ANY_TYPE, &field);
	if (result != B_OK)
		return result;

	if (typeFound != NULL)
		*typeFound = field->type;
	if (countFound != NULL)
		*countFound = field->count;

	return B_OK;
}


/**
 * @brief Retrieves the type and fixed-size flag of a named field.
 * @param name      The field name to look up.
 * @param typeFound Receives the field's type code (may be NULL).
 * @param fixedSize Receives true if items have a fixed size (may be NULL).
 * @return B_OK on success, or B_NAME_NOT_FOUND / B_NO_INIT on error.
 */
status_t
BMessage::GetInfo(const char* name, type_code* typeFound, bool* fixedSize)
	const
{
	DEBUG_FUNCTION_ENTER;
	field_header* field = NULL;
	status_t result = _FindField(name, B_ANY_TYPE, &field);
	if (result != B_OK)
		return result;

	if (typeFound != NULL)
		*typeFound = field->type;
	if (fixedSize != NULL)
		*fixedSize = (field->flags & FIELD_FLAG_FIXED_SIZE) != 0;

	return B_OK;
}


/**
 * @brief Retrieves the type, item count, and fixed-size flag of a named field.
 * @param name       The field name to look up.
 * @param typeFound  Receives the field's type code (may be NULL).
 * @param countFound Receives the number of items (may be NULL).
 * @param fixedSize  Receives true if items have a fixed size (may be NULL).
 * @return B_OK on success, or B_NAME_NOT_FOUND / B_NO_INIT on error.
 */
status_t
BMessage::GetInfo(const char* name, type_code* typeFound, int32* countFound,
	bool* fixedSize) const
{
	DEBUG_FUNCTION_ENTER;
	field_header* field = NULL;
	status_t result = _FindField(name, B_ANY_TYPE, &field);
	if (result != B_OK)
		return result;

	if (typeFound != NULL)
		*typeFound = field->type;
	if (countFound != NULL)
		*countFound = field->count;
	if (fixedSize != NULL)
		*fixedSize = (field->flags & FIELD_FLAG_FIXED_SIZE) != 0;

	return B_OK;
}


/**
 * @brief Returns the number of named fields, optionally filtered by type.
 * @param type The type code to filter by, or B_ANY_TYPE for all fields.
 * @return The number of matching fields, or 0 if the message is uninitialized.
 */
int32
BMessage::CountNames(type_code type) const
{
	DEBUG_FUNCTION_ENTER;
	if (fHeader == NULL)
		return 0;

	if (type == B_ANY_TYPE)
		return fHeader->field_count;

	int32 count = 0;
	field_header* field = fFields;
	for (uint32 i = 0; i < fHeader->field_count; i++, field++) {
		if (field->type == type)
			count++;
	}

	return count;
}


/**
 * @brief Tests whether the message contains any data fields.
 * @return true if the message has no fields or is uninitialized.
 */
bool
BMessage::IsEmpty() const
{
	DEBUG_FUNCTION_ENTER;
	return fHeader == NULL || fHeader->field_count == 0;
}


/**
 * @brief Tests whether this message is a system-defined message.
 *
 * System messages have a @c what code whose four characters follow the
 * convention: underscore followed by three uppercase letters (e.g. '_QRY').
 *
 * @return true if the message is a system-defined message.
 */
bool
BMessage::IsSystem() const
{
	DEBUG_FUNCTION_ENTER;
	char a = char(what >> 24);
	char b = char(what >> 16);
	char c = char(what >> 8);
	char d = char(what);

	// The BeBook says:
	//		... we've adopted a strict convention for assigning values to all
	//		Be-defined constants.  The value assigned will always be formed by
	//		combining four characters into a multicharacter constant, with the
	//		characters limited to uppercase letters and the underbar
	// Between that and what's in AppDefs.h, this algo seems like a safe bet:
	if (a == '_' && isupper(b) && isupper(c) && isupper(d))
		return true;

	return false;
}


/**
 * @brief Tests whether this message is a reply to a previous message.
 * @return true if the MESSAGE_FLAG_IS_REPLY flag is set.
 */
bool
BMessage::IsReply() const
{
	DEBUG_FUNCTION_ENTER;
	return fHeader != NULL && (fHeader->flags & MESSAGE_FLAG_IS_REPLY) != 0;
}


/**
 * @brief Prints a human-readable representation of the message to stdout.
 *
 * Outputs the @c what code (as a four-character constant or hex) followed by
 * each field with its name, type, and value(s). Nested BMessages are
 * printed recursively with indentation.
 */
void
BMessage::PrintToStream() const
{
	_PrintToStream("");
	printf("}\n");
}


/**
 * @brief Internal recursive helper for PrintToStream().
 *
 * Prints the message contents with the given indentation prefix for nested
 * messages. Does not print the closing brace; the caller is responsible for
 * that.
 *
 * @param indent Whitespace prefix for each output line (grows with nesting).
 */
void
BMessage::_PrintToStream(const char* indent) const
{
	DEBUG_FUNCTION_ENTER;

	int32 value = B_BENDIAN_TO_HOST_INT32(what);
	printf("BMessage(");
	if (isprint(*(char*)&value))
		printf("'%.4s'", (char*)&value);
	else
		printf("0x%" B_PRIx32, what);
	printf(") {\n");

	if (fHeader == NULL || fFields == NULL || fData == NULL)
		return;

	field_header* field = fFields;
	for (uint32 i = 0; i < fHeader->field_count; i++, field++) {
		value = B_BENDIAN_TO_HOST_INT32(field->type);
		ssize_t size = 0;
		if ((field->flags & FIELD_FLAG_FIXED_SIZE) != 0 && field->count > 0)
			size = field->data_size / field->count;

		uint8* pointer = fData + field->offset + field->name_length;
		for (uint32 j = 0; j < field->count; j++) {
			if (field->count == 1) {
				printf("%s        %s = ", indent,
					(char*)(fData + field->offset));
			} else {
				printf("%s        %s[%" B_PRIu32 "] = ", indent,
					(char*)(fData + field->offset), j);
			}

			if ((field->flags & FIELD_FLAG_FIXED_SIZE) == 0) {
				size = *(uint32*)pointer;
				pointer += sizeof(uint32);
			}

			switch (field->type) {
				case B_RECT_TYPE:
					print_to_stream_type<BRect>(pointer);
					break;

				case B_POINT_TYPE:
					print_to_stream_type<BPoint>(pointer);
					break;

				case B_STRING_TYPE:
					printf("string(\"%.*s\", %ld bytes)\n", (int)size,
						(char*)pointer, (long)size);
					break;

				case B_INT8_TYPE:
					print_type3<int8>("int8(0x%hx or %d or '%c')\n",
						pointer);
					break;

				case B_UINT8_TYPE:
					print_type3<uint8>("uint8(0x%hx or %u or '%c')\n",
						pointer);
					break;

				case B_INT16_TYPE:
					print_type<int16>("int16(0x%x or %d)\n", pointer);
					break;

				case B_UINT16_TYPE:
					print_type<uint16>("uint16(0x%x or %u)\n", pointer);
					break;

				case B_INT32_TYPE:
					print_type<int32>("int32(0x%lx or %ld)\n", pointer);
					break;

				case B_UINT32_TYPE:
					print_type<uint32>("uint32(0x%lx or %lu)\n", pointer);
					break;

				case B_INT64_TYPE:
					print_type<int64>("int64(0x%Lx or %lld)\n", pointer);
					break;

				case B_UINT64_TYPE:
					print_type<uint64>("uint64(0x%Lx or %lld)\n", pointer);
					break;

				case B_BOOL_TYPE:
					printf("bool(%s)\n", *((bool*)pointer) != 0
						? "true" : "false");
					break;

				case B_FLOAT_TYPE:
					print_type<float>("float(%.4f)\n", pointer);
					break;

				case B_DOUBLE_TYPE:
					print_type<double>("double(%.8f)\n", pointer);
					break;

				case B_REF_TYPE:
				{
					entry_ref ref;
					BPrivate::entry_ref_unflatten(&ref, (char*)pointer, size);

					printf("entry_ref(device=%d, directory=%" B_PRIdINO
						", name=\"%s\", ", (int)ref.device, ref.directory,
						ref.name);

					BPath path(&ref);
					printf("path=\"%s\")\n", path.Path());
					break;
				}

				case B_NODE_REF_TYPE:
				{
					node_ref ref;
					BPrivate::node_ref_unflatten(&ref, (char*)pointer, size);

					printf("node_ref(device=%d, node=%" B_PRIdINO ", ",
						(int)ref.device, ref.node);
					break;
				}

				case B_MESSAGE_TYPE:
				{
					char buffer[1024];
					snprintf(buffer, sizeof(buffer), "%s        ", indent);

					BMessage message;
					status_t result = message.Unflatten((const char*)pointer);
					if (result != B_OK) {
						printf("failed unflatten: %s\n", strerror(result));
						break;
					}

					message._PrintToStream(buffer);
					printf("%s        }\n", indent);
					break;
				}

				case B_RGB_32_BIT_TYPE:
				{
					rgb_color* color = (rgb_color*)pointer;
					printf("rgb_color(%u, %u, %u, %u)\n", color->red,
						color->green, color->blue, color->alpha);
					break;
				}

				default:
				{
					printf("(type = '%.4s')(size = %ld)\n", (char*)&value,
						(long)size);
					break;
				}
			}

			pointer += size;
		}
	}
}


/**
 * @brief Renames an existing field.
 *
 * Finds the field named @a oldEntry, removes it from its hash chain,
 * resizes the data buffer to accommodate the new name length, and
 * re-inserts it under the @a newEntry hash bucket.
 *
 * @param oldEntry The current field name.
 * @param newEntry The desired new field name.
 * @return B_OK on success, B_NAME_NOT_FOUND if @a oldEntry does not exist,
 *         B_BAD_VALUE if either argument is NULL, or B_NO_INIT.
 */
status_t
BMessage::Rename(const char* oldEntry, const char* newEntry)
{
	DEBUG_FUNCTION_ENTER;
	if (oldEntry == NULL || newEntry == NULL)
		return B_BAD_VALUE;

	if (fHeader == NULL)
		return B_NO_INIT;

	status_t result;
	if (fHeader->message_area >= 0) {
		result = _CopyForWrite();
		if (result != B_OK)
			return result;
	}

	uint32 hash = _HashName(oldEntry) % fHeader->hash_table_size;
	int32* nextField = &fHeader->hash_table[hash];

	while (*nextField >= 0) {
		field_header* field = &fFields[*nextField];

		if (strncmp((const char*)(fData + field->offset), oldEntry,
			field->name_length) == 0) {
			// nextField points to the field for oldEntry, save it and unlink
			int32 index = *nextField;
			*nextField = field->next_field;
			field->next_field = -1;

			hash = _HashName(newEntry) % fHeader->hash_table_size;
			nextField = &fHeader->hash_table[hash];
			while (*nextField >= 0)
				nextField = &fFields[*nextField].next_field;
			*nextField = index;

			int32 newLength = strlen(newEntry) + 1;
			result = _ResizeData(field->offset + 1,
				newLength - field->name_length);
			if (result != B_OK)
				return result;

			memcpy(fData + field->offset, newEntry, newLength);
			field->name_length = newLength;
			return B_OK;
		}

		nextField = &field->next_field;
	}

	return B_NAME_NOT_FOUND;
}


/**
 * @brief Tests whether this message was delivered to a target handler/looper.
 * @return true if the MESSAGE_FLAG_WAS_DELIVERED flag is set.
 */
bool
BMessage::WasDelivered() const
{
	DEBUG_FUNCTION_ENTER;
	return fHeader != NULL
		&& (fHeader->flags & MESSAGE_FLAG_WAS_DELIVERED) != 0;
}


/**
 * @brief Tests whether the sender is synchronously waiting for a reply.
 * @return true if a reply is required and has not yet been sent.
 */
bool
BMessage::IsSourceWaiting() const
{
	DEBUG_FUNCTION_ENTER;
	return fHeader != NULL
		&& (fHeader->flags & MESSAGE_FLAG_REPLY_REQUIRED) != 0
		&& (fHeader->flags & MESSAGE_FLAG_REPLY_DONE) == 0;
}


/**
 * @brief Tests whether the message was sent from a different team (process).
 * @return true if the message was delivered and the sender's team differs
 *         from the current team.
 */
bool
BMessage::IsSourceRemote() const
{
	DEBUG_FUNCTION_ENTER;
	return fHeader != NULL
		&& (fHeader->flags & MESSAGE_FLAG_WAS_DELIVERED) != 0
		&& fHeader->reply_team != BPrivate::current_team();
}


/**
 * @brief Returns a BMessenger that addresses the sender of this message.
 *
 * Constructs a messenger from the reply_team, reply_port, and reply_target
 * stored in the header. If the message was not delivered, an invalid
 * BMessenger is returned.
 *
 * @return A BMessenger targeting the original sender, or an invalid
 *         messenger if the message was never delivered.
 */
BMessenger
BMessage::ReturnAddress() const
{
	DEBUG_FUNCTION_ENTER;
	if (fHeader == NULL || (fHeader->flags & MESSAGE_FLAG_WAS_DELIVERED) == 0)
		return BMessenger();

	BMessenger messenger;
	BMessenger::Private(messenger).SetTo(fHeader->reply_team,
		fHeader->reply_port, fHeader->reply_target);
	return messenger;
}


/**
 * @brief Returns the previous message in a reply chain, if any.
 *
 * When a reply is sent without a required-reply flag, the original message
 * is embedded under the "_previous_" field. This method lazily unflattens
 * and caches that embedded message.
 *
 * @return A pointer to the previous BMessage, or NULL if none exists.
 */
const BMessage*
BMessage::Previous() const
{
	DEBUG_FUNCTION_ENTER;
	/* ToDo: test if the "_previous_" field is used in R5 */
	if (fOriginal == NULL) {
		fOriginal = new BMessage();

		if (FindMessage("_previous_", fOriginal) != B_OK) {
			delete fOriginal;
			fOriginal = NULL;
		}
	}

	return fOriginal;
}


/**
 * @brief Tests whether this message was delivered via a drag-and-drop operation.
 * @return true if the MESSAGE_FLAG_WAS_DROPPED flag is set.
 */
bool
BMessage::WasDropped() const
{
	DEBUG_FUNCTION_ENTER;
	return fHeader != NULL
		&& (fHeader->flags & MESSAGE_FLAG_WAS_DROPPED) != 0;
}


/**
 * @brief Returns the screen location where the message was dropped.
 *
 * The drop point and optional offset are stored internally as
 * "_drop_point_" and "_drop_offset_" fields during a drag-and-drop.
 *
 * @param offset If non-NULL, receives the offset of the drop from the
 *               dragged bitmap or rect's origin.
 * @return The screen coordinate where the drop occurred.
 */
BPoint
BMessage::DropPoint(BPoint* offset) const
{
	DEBUG_FUNCTION_ENTER;
	if (offset != NULL)
		*offset = FindPoint("_drop_offset_");

	return FindPoint("_drop_point_");
}


/**
 * @brief Sends a simple reply with only a command code.
 * @param command The what-code for the reply message.
 * @param replyTo Handler to receive replies to the reply (may be NULL).
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::SendReply(uint32 command, BHandler* replyTo)
{
	DEBUG_FUNCTION_ENTER;
	BMessage message(command);
	return SendReply(&message, replyTo);
}


/**
 * @brief Sends an asynchronous reply to the sender of this message.
 * @param reply   The reply message to send.
 * @param replyTo Handler to receive replies to the reply (may be NULL).
 * @param timeout Maximum time to wait for the reply port, in microseconds.
 * @return B_OK on success, B_DUPLICATE_REPLY if already replied,
 *         B_BAD_REPLY if the message was never delivered, or other error.
 */
status_t
BMessage::SendReply(BMessage* reply, BHandler* replyTo, bigtime_t timeout)
{
	DEBUG_FUNCTION_ENTER;
	BMessenger messenger(replyTo);
	return SendReply(reply, messenger, timeout);
}


/**
 * @brief Sends an asynchronous reply to the sender via a BMessenger.
 *
 * If the original message had a reply-required flag, the reply is marked
 * as such and a duplicate-reply check is performed. For non-required replies,
 * the original message is embedded in the reply under "_previous_".
 *
 * @param reply   The reply message to send.
 * @param replyTo Messenger to receive replies to the reply.
 * @param timeout Maximum time to wait for the reply port, in microseconds.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::SendReply(BMessage* reply, BMessenger replyTo, bigtime_t timeout)
{
	DEBUG_FUNCTION_ENTER;
	if (fHeader == NULL)
		return B_NO_INIT;

	BMessenger messenger;
	BMessenger::Private messengerPrivate(messenger);
	messengerPrivate.SetTo(fHeader->reply_team, fHeader->reply_port,
		fHeader->reply_target);
	if ((fHeader->flags & MESSAGE_FLAG_REPLY_AS_KMESSAGE) != 0)
		reply->fHeader->flags |= MESSAGE_FLAG_REPLY_AS_KMESSAGE;

	if ((fHeader->flags & MESSAGE_FLAG_REPLY_REQUIRED) != 0) {
		if ((fHeader->flags & MESSAGE_FLAG_REPLY_DONE) != 0)
			return B_DUPLICATE_REPLY;

		fHeader->flags |= MESSAGE_FLAG_REPLY_DONE;
		reply->fHeader->flags |= MESSAGE_FLAG_IS_REPLY;
		status_t result = messenger.SendMessage(reply, replyTo, timeout);
		reply->fHeader->flags &= ~MESSAGE_FLAG_IS_REPLY;

		if (result != B_OK && set_port_owner(messengerPrivate.Port(),
				messengerPrivate.Team()) == B_BAD_TEAM_ID) {
			delete_port(messengerPrivate.Port());
		}

		return result;
	}

	// no reply required
	if ((fHeader->flags & MESSAGE_FLAG_WAS_DELIVERED) == 0)
		return B_BAD_REPLY;

	reply->AddMessage("_previous_", this);
	reply->fHeader->flags |= MESSAGE_FLAG_IS_REPLY;
	status_t result = messenger.SendMessage(reply, replyTo, timeout);
	reply->fHeader->flags &= ~MESSAGE_FLAG_IS_REPLY;
	reply->RemoveName("_previous_");
	return result;
}


/**
 * @brief Sends a simple synchronous reply and waits for a reply to the reply.
 * @param command      The what-code for the reply message.
 * @param replyToReply Receives the reply to the reply (may be NULL).
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::SendReply(uint32 command, BMessage* replyToReply)
{
	DEBUG_FUNCTION_ENTER;
	BMessage message(command);
	return SendReply(&message, replyToReply);
}


/**
 * @brief Sends a synchronous reply and waits for a reply to the reply.
 *
 * Combines sending a reply to the original sender with a synchronous wait
 * for that sender's response. Both the send and the receive have
 * independent timeouts.
 *
 * @param reply        The reply message to send.
 * @param replyToReply Receives the reply-to-reply message.
 * @param sendTimeout  Maximum time to wait for the send, in microseconds.
 * @param replyTimeout Maximum time to wait for the response, in microseconds.
 * @return B_OK on success, B_DUPLICATE_REPLY if already replied, or other error.
 */
status_t
BMessage::SendReply(BMessage* reply, BMessage* replyToReply,
	bigtime_t sendTimeout, bigtime_t replyTimeout)
{
	DEBUG_FUNCTION_ENTER;
	if (fHeader == NULL)
		return B_NO_INIT;

	BMessenger messenger;
	BMessenger::Private messengerPrivate(messenger);
	messengerPrivate.SetTo(fHeader->reply_team, fHeader->reply_port,
		fHeader->reply_target);

	if ((fHeader->flags & MESSAGE_FLAG_REPLY_REQUIRED) != 0) {
		if ((fHeader->flags & MESSAGE_FLAG_REPLY_DONE) != 0)
			return B_DUPLICATE_REPLY;

		fHeader->flags |= MESSAGE_FLAG_REPLY_DONE;
		reply->fHeader->flags |= MESSAGE_FLAG_IS_REPLY;
		status_t result = messenger.SendMessage(reply, replyToReply,
			sendTimeout, replyTimeout);
		reply->fHeader->flags &= ~MESSAGE_FLAG_IS_REPLY;

		if (result != B_OK) {
			if (set_port_owner(messengerPrivate.Port(),
				messengerPrivate.Team()) == B_BAD_TEAM_ID) {
				delete_port(messengerPrivate.Port());
			}
		}

		return result;
	}

	// no reply required
	if ((fHeader->flags & MESSAGE_FLAG_WAS_DELIVERED) == 0)
		return B_BAD_REPLY;

	reply->AddMessage("_previous_", this);
	reply->fHeader->flags |= MESSAGE_FLAG_IS_REPLY
		| (fHeader->flags & MESSAGE_FLAG_REPLY_AS_KMESSAGE);
	status_t result = messenger.SendMessage(reply, replyToReply, sendTimeout,
		replyTimeout);
	reply->fHeader->flags &= ~MESSAGE_FLAG_IS_REPLY;
	reply->RemoveName("_previous_");
	return result;
}


/**
 * @brief Returns the number of bytes needed to flatten this message.
 * @return The total flattened size (header + fields + data), or B_NO_INIT
 *         if the message is uninitialized.
 */
ssize_t
BMessage::FlattenedSize() const
{
	DEBUG_FUNCTION_ENTER;
	if (fHeader == NULL)
		return B_NO_INIT;

	return sizeof(message_header) + fHeader->field_count * sizeof(field_header)
		+ fHeader->data_size;
}


/**
 * @brief Flattens the message into a pre-allocated buffer.
 *
 * Serializes the message header, field descriptors, and data into a
 * contiguous byte array. The public @c what member is synced to the
 * header before writing.
 *
 * @param buffer Destination buffer (must be at least FlattenedSize() bytes).
 * @param size   Available size in bytes.
 * @return B_OK on success, B_BUFFER_OVERFLOW if @a size is too small,
 *         B_BAD_VALUE if @a buffer is NULL, or B_NO_INIT.
 */
status_t
BMessage::Flatten(char* buffer, ssize_t size) const
{
	DEBUG_FUNCTION_ENTER;
	if (buffer == NULL || size < 0)
		return B_BAD_VALUE;

	if (fHeader == NULL)
		return B_NO_INIT;

	if (size < FlattenedSize())
		return B_BUFFER_OVERFLOW;

	/* we have to sync the what code as it is a public member */
	fHeader->what = what;

	memcpy(buffer, fHeader, sizeof(message_header));
	buffer += sizeof(message_header);

	size_t fieldsSize = fHeader->field_count * sizeof(field_header);
	memcpy(buffer, fFields, fieldsSize);
	buffer += fieldsSize;

	memcpy(buffer, fData, fHeader->data_size);

	return B_OK;
}


/**
 * @brief Flattens the message to a BDataIO stream.
 *
 * Writes the header, field descriptors, and data sequentially to @a stream.
 *
 * @param stream The output stream to write to.
 * @param size   If non-NULL, receives the total number of bytes written.
 * @return B_OK on success, B_BAD_VALUE if @a stream is NULL, or B_NO_INIT.
 */
status_t
BMessage::Flatten(BDataIO* stream, ssize_t* size) const
{
	DEBUG_FUNCTION_ENTER;
	if (stream == NULL)
		return B_BAD_VALUE;

	if (fHeader == NULL)
		return B_NO_INIT;

	/* we have to sync the what code as it is a public member */
	fHeader->what = what;

	ssize_t result1 = stream->Write(fHeader, sizeof(message_header));
	if (result1 != sizeof(message_header))
		return result1 < 0 ? result1 : B_ERROR;

	ssize_t result2 = 0;
	if (fHeader->field_count > 0) {
		ssize_t fieldsSize = fHeader->field_count * sizeof(field_header);
		result2 = stream->Write(fFields, fieldsSize);
		if (result2 != fieldsSize)
			return result2 < 0 ? result2 : B_ERROR;
	}

	ssize_t result3 = 0;
	if (fHeader->data_size > 0) {
		result3 = stream->Write(fData, fHeader->data_size);
		if (result3 != (ssize_t)fHeader->data_size)
			return result3 < 0 ? result3 : B_ERROR;
	}

	if (size)
		*size = result1 + result2 + result3;

	return B_OK;
}


/*	The concept of message sending by area:

	The traditional way of sending a message is to send it by flattening it to
	a buffer, pushing it through a port, reading it into the outputbuffer and
	unflattening it from there (copying the data again). While this works ok
	for small messages it does not make any sense for larger ones and may even
	hit some port capacity limit.
	Often in the life of a BMessage, it will be sent to someone. Almost as
	often the one receiving the message will not need to change the message
	in any way, but uses it "read only" to get information from it. This means
	that all that copying is pretty pointless in the first place since we
	could simply pass the original buffers on.
	It's obviously not exactly as simple as this, since we cannot just use the
	memory of one application in another - but we can share areas with
	eachother.
	Therefore instead of flattening into a buffer, we copy the message data
	into an area, put this information into the message header and only push
	this through the port. The receiving looper then builds a BMessage from
	the header, that only references the data in the area (not copying it),
	allowing read only access to it.
	Only if write access is necessary the message will be copyed from the area
	to its own buffers (like in the unflatten step before).
	The double copying is reduced to a single copy in most cases and we safe
	the slower route of moving the data through a port.
	Additionally we save us the reference counting with the use of areas that
	are reference counted internally. So we don't have to worry about leaving
	an area behind or deleting one that is still in use.
*/

/**
 * @brief Flattens the message data into a shared memory area for
 *        efficient inter-team transfer.
 *
 * Allocates a heap copy of the header and creates a shared area containing
 * the field descriptors and raw data. The header's message_area field is
 * set to the created area ID, and the PASS_BY_AREA flag is raised.
 * If the message has no fields or data the area is not created.
 *
 * @param _header Receives a malloc'd copy of the message header (caller
 *                must free it). Set to NULL on failure.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, B_NO_INIT
 *         if the message is uninitialized, or a negative area error code.
 */
status_t
BMessage::_FlattenToArea(message_header** _header) const
{
	DEBUG_FUNCTION_ENTER;
	if (fHeader == NULL)
		return B_NO_INIT;

	message_header* header = (message_header*)malloc(sizeof(message_header));
	if (header == NULL)
		return B_NO_MEMORY;

	memcpy(header, fHeader, sizeof(message_header));

	header->what = what;
	header->message_area = -1;
	*_header = header;

	if (header->field_count == 0 && header->data_size == 0)
		return B_OK;

	char* address = NULL;
	size_t fieldsSize = header->field_count * sizeof(field_header);
	size_t size = fieldsSize + header->data_size;
	size = (size + B_PAGE_SIZE) & ~(B_PAGE_SIZE - 1);
	area_id area = create_area("BMessage data", (void**)&address,
		B_ANY_ADDRESS, size, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);

	if (area < 0) {
		free(header);
		*_header = NULL;
		return area;
	}

	memcpy(address, fFields, fieldsSize);
	memcpy(address + fieldsSize, fData, fHeader->data_size);
	header->flags |= MESSAGE_FLAG_PASS_BY_AREA;
	header->message_area = area;
	return B_OK;
}


/**
 * @brief Sets up read-only access to message data stored in a shared area.
 *
 * After receiving a message whose data was passed by area, this method
 * makes the area read-only and points fFields and fData into the area's
 * address space. The PASS_BY_AREA flag is cleared so that subsequent
 * read operations work transparently.
 *
 * @return B_OK on success, B_NO_INIT if uninitialized, or B_BAD_VALUE
 *         if the area does not belong to the current team.
 */
status_t
BMessage::_Reference()
{
	DEBUG_FUNCTION_ENTER;
	if (fHeader == NULL)
		return B_NO_INIT;

	fHeader->flags &= ~MESSAGE_FLAG_PASS_BY_AREA;

	/* if there is no data at all we don't need the area */
	if (fHeader->field_count == 0 && fHeader->data_size == 0)
		return B_OK;

	area_info areaInfo;
	status_t result = get_area_info(fHeader->message_area, &areaInfo);
	if (result != B_OK)
		return result;

	if (areaInfo.team != BPrivate::current_team())
		return B_BAD_VALUE;

	set_area_protection(fHeader->message_area, B_READ_AREA);

	uint8* address = (uint8*)areaInfo.address;

	fFields = (field_header*)address;
	fData = address + fHeader->field_count * sizeof(field_header);
	return B_OK;
}


/**
 * @brief Releases the shared memory area backing this message's data.
 *
 * Deletes the area identified by fHeader->message_area and resets fFields
 * and fData to NULL. Called during _Clear() or before _CopyForWrite().
 *
 * @return B_OK on success, or B_NO_INIT if uninitialized.
 */
status_t
BMessage::_Dereference()
{
	DEBUG_FUNCTION_ENTER;
	if (fHeader == NULL)
		return B_NO_INIT;

	delete_area(fHeader->message_area);
	fHeader->message_area = -1;
	fFields = NULL;
	fData = NULL;
	return B_OK;
}


/**
 * @brief Copies area-backed data into private heap buffers so the message
 *        can be modified.
 *
 * This is the "copy-on-write" step. When a message references data in a
 * read-only shared area, any mutating operation must first call this method
 * to obtain writable copies of fFields and fData. The shared area is then
 * released via _Dereference().
 *
 * @return B_OK on success, B_NO_MEMORY if allocation fails, or B_NO_INIT.
 */
status_t
BMessage::_CopyForWrite()
{
	DEBUG_FUNCTION_ENTER;
	if (fHeader == NULL)
		return B_NO_INIT;

	field_header* newFields = NULL;
	uint8* newData = NULL;

	if (fHeader->field_count > 0) {
		size_t fieldsSize = fHeader->field_count * sizeof(field_header);
		newFields = (field_header*)malloc(fieldsSize);
		if (newFields == NULL)
			return B_NO_MEMORY;

		memcpy(newFields, fFields, fieldsSize);
	}

	if (fHeader->data_size > 0) {
		newData = (uint8*)malloc(fHeader->data_size);
		if (newData == NULL) {
			free(newFields);
			return B_NO_MEMORY;
		}

		memcpy(newData, fData, fHeader->data_size);
	}

	_Dereference();

	fFieldsAvailable = 0;
	fDataAvailable = 0;

	fFields = newFields;
	fData = newData;
	return B_OK;
}


/**
 * @brief Validates the internal consistency of the message after unflattening.
 *
 * Iterates over all field headers to check that next_field indices and
 * data offsets stay within bounds. If corruption is detected, the message
 * is emptied via MakeEmpty() and B_BAD_VALUE is returned.
 *
 * @return B_OK if valid, B_NO_INIT if uninitialized, or B_BAD_VALUE if corrupt.
 */
status_t
BMessage::_ValidateMessage()
{
	DEBUG_FUNCTION_ENTER;
	if (fHeader == NULL)
		return B_NO_INIT;

	if (fHeader->field_count == 0)
		return B_OK;

	if (fFields == NULL)
		return B_NO_INIT;

	for (uint32 i = 0; i < fHeader->field_count; i++) {
		field_header* field = &fFields[i];
		if ((field->next_field >= 0
				&& (uint32)field->next_field > fHeader->field_count)
			|| (field->offset + field->name_length + field->data_size
				> fHeader->data_size)) {
			// the message is corrupt
			MakeEmpty();
			return B_BAD_VALUE;
		}
	}

	return B_OK;
}


/**
 * @brief Reconstructs a message from a flat byte buffer.
 *
 * Detects the format by inspecting the first four bytes. Native
 * (MESSAGE_FORMAT_HAIKU) buffers are handled in-line; other formats
 * (e.g. R5) are dispatched to BPrivate::MessageAdapter.
 *
 * @param flatBuffer Pointer to the serialized message data.
 * @return B_OK on success, B_BAD_VALUE if @a flatBuffer is NULL or corrupt.
 */
status_t
BMessage::Unflatten(const char* flatBuffer)
{
	DEBUG_FUNCTION_ENTER;
	if (flatBuffer == NULL)
		return B_BAD_VALUE;

	uint32 format = *(uint32*)flatBuffer;
	if (format != MESSAGE_FORMAT_HAIKU)
		return BPrivate::MessageAdapter::Unflatten(format, this, flatBuffer);

	BMemoryIO io(flatBuffer, SSIZE_MAX);
	return Unflatten(&io);
}


/**
 * @brief Reconstructs a message by reading from a BDataIO stream.
 *
 * Reads the header, field descriptors, and data from @a stream. If the
 * header indicates pass-by-area, the shared area is referenced instead
 * of reading data from the stream. The message is validated after loading.
 *
 * @param stream The input stream to read from.
 * @return B_OK on success, B_BAD_VALUE if @a stream is NULL or data is
 *         corrupt, B_NO_MEMORY on allocation failure.
 */
status_t
BMessage::Unflatten(BDataIO* stream)
{
	DEBUG_FUNCTION_ENTER;
	if (stream == NULL)
		return B_BAD_VALUE;

	uint32 format = 0;
	stream->Read(&format, sizeof(uint32));
	if (format != MESSAGE_FORMAT_HAIKU)
		return BPrivate::MessageAdapter::Unflatten(format, this, stream);

	// native message unflattening

	_Clear();

	fHeader = (message_header*)malloc(sizeof(message_header));
	if (fHeader == NULL)
		return B_NO_MEMORY;

	fHeader->format = format;
	uint8* header = (uint8*)fHeader;
	ssize_t result = stream->Read(header + sizeof(uint32),
		sizeof(message_header) - sizeof(uint32));
	if (result != sizeof(message_header) - sizeof(uint32)
		|| (fHeader->flags & MESSAGE_FLAG_VALID) == 0) {
		_InitHeader();
		return result < 0 ? result : B_BAD_VALUE;
	}

	what = fHeader->what;

	if ((fHeader->flags & MESSAGE_FLAG_PASS_BY_AREA) != 0
		&& fHeader->message_area >= 0) {
		status_t result = _Reference();
		if (result != B_OK) {
			_InitHeader();
			return result;
		}
	} else {
		fHeader->message_area = -1;

		if (fHeader->field_count > 0) {
			ssize_t fieldsSize = fHeader->field_count * sizeof(field_header);
			fFields = (field_header*)malloc(fieldsSize);
			if (fFields == NULL) {
				_InitHeader();
				return B_NO_MEMORY;
			}

			result = stream->Read(fFields, fieldsSize);
			if (result != fieldsSize)
				return result < 0 ? result : B_BAD_VALUE;
		}

		if (fHeader->data_size > 0) {
			fData = (uint8*)malloc(fHeader->data_size);
			if (fData == NULL) {
				free(fFields);
				fFields = NULL;
				_InitHeader();
				return B_NO_MEMORY;
			}

			result = stream->Read(fData, fHeader->data_size);
			if (result != (ssize_t)fHeader->data_size) {
				free(fData);
				fData = NULL;
				free(fFields);
				fFields = NULL;
				_InitHeader();
				return result < 0 ? result : B_BAD_VALUE;
			}
		}
	}

	return _ValidateMessage();
}


/**
 * @brief Adds a direct specifier for the given property.
 *
 * Creates a B_DIRECT_SPECIFIER message containing the property name and
 * appends it to the specifier stack.
 *
 * @param property The property name to specify.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::AddSpecifier(const char* property)
{
	DEBUG_FUNCTION_ENTER;
	BMessage message(B_DIRECT_SPECIFIER);
	status_t result = message.AddString(B_PROPERTY_ENTRY, property);
	if (result != B_OK)
		return result;

	return AddSpecifier(&message);
}


/**
 * @brief Adds an index specifier for the given property.
 * @param property The property name to specify.
 * @param index    The zero-based index to target.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::AddSpecifier(const char* property, int32 index)
{
	DEBUG_FUNCTION_ENTER;
	BMessage message(B_INDEX_SPECIFIER);
	status_t result = message.AddString(B_PROPERTY_ENTRY, property);
	if (result != B_OK)
		return result;

	result = message.AddInt32("index", index);
	if (result != B_OK)
		return result;

	return AddSpecifier(&message);
}


/**
 * @brief Adds a range specifier for the given property.
 * @param property The property name to specify.
 * @param index    The starting index of the range.
 * @param range    The number of items in the range (must be >= 0).
 * @return B_OK on success, B_BAD_VALUE if @a range is negative, or an error code.
 */
status_t
BMessage::AddSpecifier(const char* property, int32 index, int32 range)
{
	DEBUG_FUNCTION_ENTER;
	if (range < 0)
		return B_BAD_VALUE;

	BMessage message(B_RANGE_SPECIFIER);
	status_t result = message.AddString(B_PROPERTY_ENTRY, property);
	if (result != B_OK)
		return result;

	result = message.AddInt32("index", index);
	if (result != B_OK)
		return result;

	result = message.AddInt32("range", range);
	if (result != B_OK)
		return result;

	return AddSpecifier(&message);
}


/**
 * @brief Adds a name specifier for the given property.
 * @param property The property name to specify.
 * @param name     The name to match within the property.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::AddSpecifier(const char* property, const char* name)
{
	DEBUG_FUNCTION_ENTER;
	BMessage message(B_NAME_SPECIFIER);
	status_t result = message.AddString(B_PROPERTY_ENTRY, property);
	if (result != B_OK)
		return result;

	result = message.AddString(B_PROPERTY_NAME_ENTRY, name);
	if (result != B_OK)
		return result;

	return AddSpecifier(&message);
}


/**
 * @brief Pushes a pre-built specifier message onto the specifier stack.
 *
 * Adds @a specifier under the B_SPECIFIER_ENTRY field name, increments
 * the current specifier index, and sets the HAS_SPECIFIERS flag.
 *
 * @param specifier The specifier message to add.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::AddSpecifier(const BMessage* specifier)
{
	DEBUG_FUNCTION_ENTER;
	status_t result = AddMessage(B_SPECIFIER_ENTRY, specifier);
	if (result != B_OK)
		return result;

	fHeader->current_specifier++;
	fHeader->flags |= MESSAGE_FLAG_HAS_SPECIFIERS;
	return B_OK;
}


/**
 * @brief Sets the index of the current specifier in the specifier stack.
 * @param index Zero-based index into the specifier array.
 * @return B_OK on success, B_BAD_INDEX if @a index is out of range.
 */
status_t
BMessage::SetCurrentSpecifier(int32 index)
{
	DEBUG_FUNCTION_ENTER;
	if (index < 0)
		return B_BAD_INDEX;

	type_code type;
	int32 count;
	status_t result = GetInfo(B_SPECIFIER_ENTRY, &type, &count);
	if (result != B_OK)
		return result;

	if (index >= count)
		return B_BAD_INDEX;

	fHeader->current_specifier = index;
	return B_OK;
}


/**
 * @brief Retrieves the current specifier from the specifier stack.
 *
 * Returns information about the specifier at the current_specifier index.
 * This is typically called by a scripting handler during message dispatch.
 *
 * @param index     Receives the current specifier index (may be NULL).
 * @param specifier Receives the specifier message (may be NULL).
 * @param _what     Receives the specifier's what-code (may be NULL).
 * @param property  Receives the property name from the specifier (may be NULL).
 * @return B_OK on success, B_BAD_SCRIPT_SYNTAX if no valid specifier exists,
 *         or B_NO_INIT.
 */
status_t
BMessage::GetCurrentSpecifier(int32* index, BMessage* specifier, int32* _what,
	const char** property) const
{
	DEBUG_FUNCTION_ENTER;
	if (fHeader == NULL)
		return B_NO_INIT;

	if (index != NULL)
		*index = fHeader->current_specifier;

	if (fHeader->current_specifier < 0
		|| (fHeader->flags & MESSAGE_FLAG_WAS_DELIVERED) == 0)
		return B_BAD_SCRIPT_SYNTAX;

	if (specifier) {
		if (FindMessage(B_SPECIFIER_ENTRY, fHeader->current_specifier,
			specifier) != B_OK)
			return B_BAD_SCRIPT_SYNTAX;

		if (_what != NULL)
			*_what = specifier->what;

		if (property) {
			if (specifier->FindString(B_PROPERTY_ENTRY, property) != B_OK)
				return B_BAD_SCRIPT_SYNTAX;
		}
	}

	return B_OK;
}


/**
 * @brief Tests whether this message has any specifiers.
 * @return true if the MESSAGE_FLAG_HAS_SPECIFIERS flag is set.
 */
bool
BMessage::HasSpecifiers() const
{
	DEBUG_FUNCTION_ENTER;
	return fHeader != NULL
		&& (fHeader->flags & MESSAGE_FLAG_HAS_SPECIFIERS) != 0;
}


/**
 * @brief Pops the current specifier by decrementing the specifier index.
 *
 * Used by scripting handlers after processing a specifier, so that the
 * next handler in the chain sees the previous specifier.
 *
 * @return B_OK on success, B_BAD_VALUE if no specifier is active or the
 *         message was not delivered, or B_NO_INIT.
 */
status_t
BMessage::PopSpecifier()
{
	DEBUG_FUNCTION_ENTER;
	if (fHeader == NULL)
		return B_NO_INIT;

	if (fHeader->current_specifier < 0 ||
		(fHeader->flags & MESSAGE_FLAG_WAS_DELIVERED) == 0)
		return B_BAD_VALUE;

	if (fHeader->current_specifier >= 0)
		fHeader->current_specifier--;

	return B_OK;
}


/**
 * @brief Adjusts all field offsets that are at or beyond a given position.
 *
 * Called after data is inserted or removed at @a offset to keep every
 * field_header::offset consistent with the new data layout.
 *
 * @param offset The byte offset in fData where insertion/removal occurred.
 * @param change The signed byte delta (positive for insertion, negative for removal).
 */
void
BMessage::_UpdateOffsets(uint32 offset, int32 change)
{
	// Update the header to match the new position of the fields
	if (offset < fHeader->data_size) {
		field_header* field = fFields;
		for (uint32 i = 0; i < fHeader->field_count; i++, field++) {
			if (field->offset >= offset)
				field->offset += change;
		}
	}
}


/**
 * @brief Grows or shrinks the raw data buffer at the given offset.
 *
 * Handles both in-place expansion (when fDataAvailable is sufficient) and
 * reallocation with geometric growth. For shrinks, excess pre-allocated
 * space is trimmed if it exceeds MAX_DATA_PREALLOCATION. After resizing,
 * _UpdateOffsets() is called to fix all field offsets.
 *
 * @param offset The byte position in fData at which to insert or remove bytes.
 * @param change The number of bytes to add (positive) or remove (negative).
 * @return B_OK on success, or B_NO_MEMORY if reallocation fails.
 */
status_t
BMessage::_ResizeData(uint32 offset, int32 change)
{
	if (change == 0)
		return B_OK;

	/* optimize for the most usual case: appending data */

	if (change > 0) {
		// We need to make the field bigger
		// check if there is enough free space allocated
		if (fDataAvailable >= (uint32)change) {
			// In this case, we just need to move the data after the growing
			// field to get the space at the right place
			if (offset < fHeader->data_size) {
				memmove(fData + offset + change, fData + offset,
					fHeader->data_size - offset);
			}

			_UpdateOffsets(offset, change);

			fDataAvailable -= change;
			fHeader->data_size += change;
			return B_OK;
		}

		// We need to grow the buffer. We try to optimize reallocations by
		// preallocating space for more fields.
		size_t size = fHeader->data_size * 2;
		size = min_c(size, fHeader->data_size + MAX_DATA_PREALLOCATION);
		size = max_c(size, fHeader->data_size + change);

		uint8* newData = (uint8*)realloc(fData, size);
		if (size > 0 && newData == NULL)
			return B_NO_MEMORY;

		fData = newData;
		if (offset < fHeader->data_size) {
			memmove(fData + offset + change, fData + offset,
				fHeader->data_size - offset);
		}

		fHeader->data_size += change;
		fDataAvailable = size - fHeader->data_size;
	} else {
		ssize_t length = fHeader->data_size - offset + change;
		if (length > 0)
			memmove(fData + offset, fData + offset - change, length);

		// change is negative
		fHeader->data_size += change;
		fDataAvailable -= change;

		if (fDataAvailable > MAX_DATA_PREALLOCATION) {
			ssize_t available = MAX_DATA_PREALLOCATION / 2;
			ssize_t size = fHeader->data_size + available;
			uint8* newData = (uint8*)realloc(fData, size);
			if (size > 0 && newData == NULL) {
				// this is strange, but not really fatal
				_UpdateOffsets(offset, change);
				return B_OK;
			}

			fData = newData;
			fDataAvailable = available;
		}
	}

	_UpdateOffsets(offset, change);
	return B_OK;
}


/**
 * @brief Computes a hash value for a field name.
 *
 * Uses a simple rotate-XOR algorithm to distribute names across the
 * hash table buckets in the message header.
 *
 * @param name The null-terminated field name to hash.
 * @return A 32-bit hash value (must be taken modulo hash_table_size).
 */
uint32
BMessage::_HashName(const char* name) const
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
 * @brief Looks up a field by name (and optionally type) using the hash table.
 *
 * Walks the hash chain for @a name. If a matching field is found and
 * @a type is not B_ANY_TYPE, the field's type is also verified.
 *
 * @param name   The null-terminated field name to search for.
 * @param type   Required type code, or B_ANY_TYPE to accept any type.
 * @param result Receives a pointer to the matching field_header.
 * @return B_OK on success, B_NAME_NOT_FOUND if the field does not exist,
 *         B_BAD_TYPE if the name exists but the type does not match,
 *         B_BAD_VALUE if @a name is NULL, or B_NO_INIT.
 */
status_t
BMessage::_FindField(const char* name, type_code type, field_header** result)
	const
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
		field_header* field = &fFields[nextField];
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
 * @brief Creates a new empty field with the given name and type.
 *
 * Grows the fFields array if necessary (with geometric pre-allocation),
 * appends the new field to the hash chain, writes the name into the data
 * buffer via _ResizeData(), and sets the VALID and optional FIXED_SIZE flags.
 *
 * @param name        The null-terminated field name.
 * @param type        The type code for the new field.
 * @param isFixedSize true if all items in the field will have the same size.
 * @param result      Receives a pointer to the newly created field_header.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or B_NO_INIT.
 */
status_t
BMessage::_AddField(const char* name, type_code type, bool isFixedSize,
	field_header** result)
{
	if (fHeader == NULL)
		return B_NO_INIT;

	if (fFieldsAvailable <= 0) {
		uint32 count = fHeader->field_count * 2 + 1;
		count = min_c(count, fHeader->field_count + MAX_FIELD_PREALLOCATION);

		field_header* newFields = (field_header*)realloc(fFields,
			count * sizeof(field_header));
		if (count > 0 && newFields == NULL)
			return B_NO_MEMORY;

		fFields = newFields;
		fFieldsAvailable = count - fHeader->field_count;
	}

	uint32 hash = _HashName(name) % fHeader->hash_table_size;
	int32* nextField = &fHeader->hash_table[hash];
	while (*nextField >= 0)
		nextField = &fFields[*nextField].next_field;
	*nextField = fHeader->field_count;

	field_header* field = &fFields[fHeader->field_count];
	field->type = type;
	field->count = 0;
	field->data_size = 0;
	field->next_field = -1;
	field->offset = fHeader->data_size;
	field->name_length = strlen(name) + 1;
	status_t status = _ResizeData(field->offset, field->name_length);
	if (status != B_OK)
		return status;

	memcpy(fData + field->offset, name, field->name_length);
	field->flags = FIELD_FLAG_VALID;
	if (isFixedSize)
		field->flags |= FIELD_FLAG_FIXED_SIZE;

	fFieldsAvailable--;
	fHeader->field_count++;
	*result = field;
	return B_OK;
}


/**
 * @brief Removes a field and its data from the message.
 *
 * Shrinks the data buffer, fixes up the hash table and next_field links
 * in all remaining field headers, and compacts the fFields array. Excess
 * pre-allocated field slots are reclaimed if they exceed
 * MAX_FIELD_PREALLOCATION.
 *
 * @param field Pointer to the field_header to remove (must be valid).
 * @return B_OK on success, or an error from _ResizeData().
 */
status_t
BMessage::_RemoveField(field_header* field)
{
	status_t result = _ResizeData(field->offset, -(field->data_size
		+ field->name_length));
	if (result != B_OK)
		return result;

	int32 index = ((uint8*)field - (uint8*)fFields) / sizeof(field_header);
	int32 nextField = field->next_field;
	if (nextField > index)
		nextField--;

	int32* value = fHeader->hash_table;
	for (uint32 i = 0; i < fHeader->hash_table_size; i++, value++) {
		if (*value > index)
			*value -= 1;
		else if (*value == index)
			*value = nextField;
	}

	field_header* other = fFields;
	for (uint32 i = 0; i < fHeader->field_count; i++, other++) {
		if (other->next_field > index)
			other->next_field--;
		else if (other->next_field == index)
			other->next_field = nextField;
	}

	size_t size = (fHeader->field_count - index - 1) * sizeof(field_header);
	memmove(fFields + index, fFields + index + 1, size);
	fHeader->field_count--;
	fFieldsAvailable++;

	if (fFieldsAvailable > MAX_FIELD_PREALLOCATION) {
		ssize_t available = MAX_FIELD_PREALLOCATION / 2;
		size = (fHeader->field_count + available) * sizeof(field_header);
		field_header* newFields = (field_header*)realloc(fFields, size);
		if (size > 0 && newFields == NULL) {
			// this is strange, but not really fatal
			return B_OK;
		}

		fFields = newFields;
		fFieldsAvailable = available;
	}

	return B_OK;
}


/**
 * @brief Adds a raw data item to a named field.
 *
 * If the field does not exist it is created. For fixed-size fields the item
 * size must match any existing items. For variable-size fields a uint32
 * length prefix is stored before each item.
 *
 * @param name        The field name.
 * @param type        The type code.
 * @param data        Pointer to the data to add.
 * @param numBytes    Size of the data in bytes.
 * @param isFixedSize true if the field stores fixed-size items.
 * @param count       Hint for expected number of items (currently unused).
 * @return B_OK on success, B_BAD_VALUE if @a data is NULL or @a numBytes <= 0,
 *         B_NO_INIT, or B_NO_MEMORY.
 */
status_t
BMessage::AddData(const char* name, type_code type, const void* data,
	ssize_t numBytes, bool isFixedSize, int32 count)
{
	// Note that the "count" argument is only a hint at how many items
	// the caller expects to add to this field. Since we do no item pre-
	// allocation, we ignore this argument.
	DEBUG_FUNCTION_ENTER;
	if (numBytes <= 0 || data == NULL)
		return B_BAD_VALUE;

	if (fHeader == NULL)
		return B_NO_INIT;

	status_t result;
	if (fHeader->message_area >= 0) {
		result = _CopyForWrite();
		if (result != B_OK)
			return result;
	}

	field_header* field = NULL;
	result = _FindField(name, type, &field);
	if (result == B_NAME_NOT_FOUND)
		result = _AddField(name, type, isFixedSize, &field);

	if (result != B_OK)
		return result;

	if (field == NULL)
		return B_ERROR;

	uint32 offset = field->offset + field->name_length + field->data_size;
	if ((field->flags & FIELD_FLAG_FIXED_SIZE) != 0) {
		if (field->count) {
			ssize_t size = field->data_size / field->count;
			if (size != numBytes)
				return B_BAD_VALUE;
		}

		result = _ResizeData(offset, numBytes);
		if (result != B_OK) {
			if (field->count == 0)
				_RemoveField(field);
			return result;
		}

		memcpy(fData + offset, data, numBytes);
		field->data_size += numBytes;
	} else {
		int32 change = numBytes + sizeof(uint32);
		result = _ResizeData(offset, change);
		if (result != B_OK) {
			if (field->count == 0)
				_RemoveField(field);
			return result;
		}

		uint32 size = (uint32)numBytes;
		memcpy(fData + offset, &size, sizeof(uint32));
		memcpy(fData + offset + sizeof(uint32), data, size);
		field->data_size += change;
	}

	field->count++;
	return B_OK;
}


/**
 * @brief Removes a single data item from a named field by index.
 *
 * If the field has only one item, the entire field is removed. Otherwise
 * the item at @a index is excised and the field's count and data_size are
 * adjusted.
 *
 * @param name  The field name.
 * @param index Zero-based index of the item to remove.
 * @return B_OK on success, B_BAD_INDEX, B_NAME_NOT_FOUND, or B_NO_INIT.
 */
status_t
BMessage::RemoveData(const char* name, int32 index)
{
	DEBUG_FUNCTION_ENTER;
	if (index < 0)
		return B_BAD_INDEX;

	if (fHeader == NULL)
		return B_NO_INIT;

	status_t result;
	if (fHeader->message_area >= 0) {
		result = _CopyForWrite();
		if (result != B_OK)
			return result;
	}

	field_header* field = NULL;
	result = _FindField(name, B_ANY_TYPE, &field);
	if (result != B_OK)
		return result;

	if ((uint32)index >= field->count)
		return B_BAD_INDEX;

	if (field->count == 1)
		return _RemoveField(field);

	uint32 offset = field->offset + field->name_length;
	if ((field->flags & FIELD_FLAG_FIXED_SIZE) != 0) {
		ssize_t size = field->data_size / field->count;
		result = _ResizeData(offset + index * size, -size);
		if (result != B_OK)
			return result;

		field->data_size -= size;
	} else {
		uint8* pointer = fData + offset;
		for (int32 i = 0; i < index; i++) {
			offset += *(uint32*)pointer + sizeof(uint32);
			pointer = fData + offset;
		}

		size_t currentSize = *(uint32*)pointer + sizeof(uint32);
		result = _ResizeData(offset, -currentSize);
		if (result != B_OK)
			return result;

		field->data_size -= currentSize;
	}

	field->count--;
	return B_OK;
}


/**
 * @brief Removes an entire named field and all its items from the message.
 * @param name The field name to remove.
 * @return B_OK on success, B_NAME_NOT_FOUND, or B_NO_INIT.
 */
status_t
BMessage::RemoveName(const char* name)
{
	DEBUG_FUNCTION_ENTER;
	if (fHeader == NULL)
		return B_NO_INIT;

	status_t result;
	if (fHeader->message_area >= 0) {
		result = _CopyForWrite();
		if (result != B_OK)
			return result;
	}

	field_header* field = NULL;
	result = _FindField(name, B_ANY_TYPE, &field);
	if (result != B_OK)
		return result;

	return _RemoveField(field);
}


/**
 * @brief Removes all fields and data, resetting the message to an empty state.
 *
 * Calls _Clear() to free all buffers and then _InitHeader() to set up a
 * fresh header. The @c what code is preserved.
 *
 * @return B_OK on success.
 */
status_t
BMessage::MakeEmpty()
{
	DEBUG_FUNCTION_ENTER;
	_Clear();
	return _InitHeader();
}


/**
 * @brief Retrieves a pointer to raw data for an item within a named field.
 *
 * Returns a direct pointer into the message's data buffer. The pointer
 * remains valid only as long as the message is not modified.
 *
 * @param name     The field name.
 * @param type     Required type code, or B_ANY_TYPE.
 * @param index    Zero-based index of the item within the field.
 * @param data     Receives a pointer to the item's raw data.
 * @param numBytes If non-NULL, receives the size of the item in bytes.
 * @return B_OK on success, B_BAD_VALUE, B_BAD_INDEX, B_NAME_NOT_FOUND,
 *         or B_BAD_TYPE.
 */
status_t
BMessage::FindData(const char* name, type_code type, int32 index,
	const void** data, ssize_t* numBytes) const
{
	DEBUG_FUNCTION_ENTER;
	if (data == NULL)
		return B_BAD_VALUE;

	*data = NULL;
	field_header* field = NULL;
	status_t result = _FindField(name, type, &field);
	if (result != B_OK)
		return result;

	if (index < 0 || (uint32)index >= field->count)
		return B_BAD_INDEX;

	if ((field->flags & FIELD_FLAG_FIXED_SIZE) != 0) {
		size_t bytes = field->data_size / field->count;
		*data = fData + field->offset + field->name_length + index * bytes;
		if (numBytes != NULL)
			*numBytes = bytes;
	} else {
		uint8* pointer = fData + field->offset + field->name_length;
		for (int32 i = 0; i < index; i++)
			pointer += *(uint32*)pointer + sizeof(uint32);

		*data = pointer + sizeof(uint32);
		if (numBytes != NULL)
			*numBytes = *(uint32*)pointer;
	}

	return B_OK;
}


/**
 * @brief Replaces the raw data of an item within a named field.
 *
 * For fixed-size fields, @a numBytes must match the existing item size.
 * For variable-size fields, the data buffer is resized as needed.
 *
 * @param name     The field name.
 * @param type     Required type code.
 * @param index    Zero-based index of the item to replace.
 * @param data     Pointer to the replacement data.
 * @param numBytes Size of the replacement data in bytes.
 * @return B_OK on success, B_BAD_VALUE, B_BAD_INDEX, B_NAME_NOT_FOUND,
 *         or B_BAD_TYPE.
 */
status_t
BMessage::ReplaceData(const char* name, type_code type, int32 index,
	const void* data, ssize_t numBytes)
{
	DEBUG_FUNCTION_ENTER;
	if (numBytes <= 0 || data == NULL)
		return B_BAD_VALUE;

	status_t result;
	if (fHeader->message_area >= 0) {
		result = _CopyForWrite();
		if (result != B_OK)
			return result;
	}

	field_header* field = NULL;
	result = _FindField(name, type, &field);
	if (result != B_OK)
		return result;

	if (index < 0 || (uint32)index >= field->count)
		return B_BAD_INDEX;

	if ((field->flags & FIELD_FLAG_FIXED_SIZE) != 0) {
		ssize_t size = field->data_size / field->count;
		if (size != numBytes)
			return B_BAD_VALUE;

		memcpy(fData + field->offset + field->name_length + index * size, data,
			size);
	} else {
		uint32 offset = field->offset + field->name_length;
		uint8* pointer = fData + offset;

		for (int32 i = 0; i < index; i++) {
			offset += *(uint32*)pointer + sizeof(uint32);
			pointer = fData + offset;
		}

		size_t currentSize = *(uint32*)pointer;
		int32 change = numBytes - currentSize;
		result = _ResizeData(offset, change);
		if (result != B_OK)
			return result;

		uint32 newSize = (uint32)numBytes;
		memcpy(fData + offset, &newSize, sizeof(uint32));
		memcpy(fData + offset + sizeof(uint32), data, newSize);
		field->data_size += change;
	}

	return B_OK;
}


/**
 * @brief Tests whether a named field contains an item at the given index.
 * @param name  The field name.
 * @param type  Required type code, or B_ANY_TYPE.
 * @param index Zero-based index within the field.
 * @return true if the item exists, false otherwise.
 */
bool
BMessage::HasData(const char* name, type_code type, int32 index) const
{
	DEBUG_FUNCTION_ENTER;
	field_header* field = NULL;
	status_t result = _FindField(name, type, &field);
	if (result != B_OK)
		return false;

	if (index < 0 || (uint32)index >= field->count)
		return false;

	return true;
}


/**
 * @brief One-time static initialization of reply ports and the block cache.
 *
 * Creates three cached reply ports and the BBlockCache used by operator
 * new/delete. Called during application startup by BApplication.
 */
void
BMessage::_StaticInit()
{
	DEBUG_FUNCTION_ENTER2;
	sReplyPorts[0] = create_port(1, "tmp_rport0");
	sReplyPorts[1] = create_port(1, "tmp_rport1");
	sReplyPorts[2] = create_port(1, "tmp_rport2");

	sReplyPortInUse[0] = 0;
	sReplyPortInUse[1] = 0;
	sReplyPortInUse[2] = 0;

	sMsgCache = new BBlockCache(20, sizeof(BMessage), B_OBJECT_CACHE);
}


/**
 * @brief Re-creates reply ports after a fork(), since ports are per-team.
 *
 * Called in the child process after fork to replace the inherited (now
 * invalid) reply ports with fresh ones.
 */
void
BMessage::_StaticReInitForkedChild()
{
	DEBUG_FUNCTION_ENTER2;

	// overwrite the inherited ports with a set of our own
	sReplyPorts[0] = create_port(1, "tmp_rport0");
	sReplyPorts[1] = create_port(1, "tmp_rport1");
	sReplyPorts[2] = create_port(1, "tmp_rport2");

	sReplyPortInUse[0] = 0;
	sReplyPortInUse[1] = 0;
	sReplyPortInUse[2] = 0;
}


/**
 * @brief Deletes the cached reply ports during application shutdown.
 */
void
BMessage::_StaticCleanup()
{
	DEBUG_FUNCTION_ENTER2;
	delete_port(sReplyPorts[0]);
	sReplyPorts[0] = -1;
	delete_port(sReplyPorts[1]);
	sReplyPorts[1] = -1;
	delete_port(sReplyPorts[2]);
	sReplyPorts[2] = -1;
}


/**
 * @brief Deletes the BBlockCache used for BMessage allocations.
 */
void
BMessage::_StaticCacheCleanup()
{
	DEBUG_FUNCTION_ENTER2;
	delete sMsgCache;
	sMsgCache = NULL;
}


/**
 * @brief Atomically acquires one of the cached reply ports.
 *
 * Uses atomic_add to find a free slot among sNumReplyPorts cached ports.
 * If all cached ports are in use, returns -1 and the caller must create
 * a temporary port.
 *
 * @return Index of the acquired cached port, or -1 if none is available.
 */
int32
BMessage::_StaticGetCachedReplyPort()
{
	DEBUG_FUNCTION_ENTER2;
	int index = -1;
	for (int32 i = 0; i < sNumReplyPorts; i++) {
		int32 old = atomic_add(&(sReplyPortInUse[i]), 1);
		if (old == 0) {
			// This entry is free
			index = i;
			break;
		} else {
			// This entry is being used.
			atomic_add(&(sReplyPortInUse[i]), -1);
		}
	}

	return index;
}


/**
 * @brief Internal: sends this message asynchronously to a port.
 *
 * Chooses the most efficient delivery path:
 * - Local targets in the same team get a direct queue insertion via
 *   BDirectMessageTarget, avoiding the port entirely.
 * - Large messages (> 10 pages) are sent by shared area.
 * - Messages flagged as KMessage replies are converted and sent via KMessage.
 * - All other messages are flattened to a buffer and written to the port.
 *
 * @param port          Destination port.
 * @param portOwner     Team that owns the port (-1 to look it up).
 * @param token         Target handler token.
 * @param timeout       Send timeout in microseconds.
 * @param replyRequired If true, the REPLY_REQUIRED flag is set in the header.
 * @param replyTo       Messenger to receive replies; updated to be_app_messenger
 *                      if invalid.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::_SendMessage(port_id port, team_id portOwner, int32 token,
	bigtime_t timeout, bool replyRequired, BMessenger& replyTo) const
{
	DEBUG_FUNCTION_ENTER;

	ssize_t size = 0;
	char stackBuffer[4096];
	char* buffer = NULL;
	message_header* header = NULL;
	status_t result = B_OK;

	BPrivate::BDirectMessageTarget* direct = NULL;
	BMessage* copy = NULL;
	if (portOwner == BPrivate::current_team())
		BPrivate::gDefaultTokens.AcquireHandlerTarget(token, &direct);

	if (direct != NULL) {
		// We have a direct local message target - we can just enqueue the
		// message in its message queue. This will also prevent possible
		// deadlocks when the queue is full.
		copy = new BMessage(*this);
		if (copy != NULL) {
			header = copy->fHeader;
			header->flags = fHeader->flags;
		} else {
			direct->Release();
			return B_NO_MEMORY;
		}
#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
	} else if ((fHeader->flags & MESSAGE_FLAG_REPLY_AS_KMESSAGE) != 0) {
		KMessage toMessage;
		result = BPrivate::MessageAdapter::ConvertToKMessage(this, toMessage);
		if (result != B_OK)
			return result;

		return toMessage.SendTo(port, token);
	} else if (fHeader->data_size > B_PAGE_SIZE * 10) {
		// ToDo: bind the above size to the max port message size
		// use message passing by area for such a large message
		result = _FlattenToArea(&header);
		if (result != B_OK)
			return result;

		buffer = (char*)header;
		size = sizeof(message_header);

		if (header->message_area >= 0) {
			team_id target = portOwner;
			if (target < 0) {
				port_info info;
				result = get_port_info(port, &info);
				if (result != B_OK) {
					free(header);
					return result;
				}
				target = info.team;
			}

			void* address = NULL;
			area_id transfered = _kern_transfer_area(header->message_area,
				&address, B_ANY_ADDRESS, target);
			if (transfered < 0) {
				delete_area(header->message_area);
				free(header);
				return transfered;
			}

			header->message_area = transfered;
		}
#endif
	} else {
		size = FlattenedSize();
		if (size > (ssize_t)sizeof(stackBuffer)) {
			buffer = (char*)malloc(size);
			if (buffer == NULL)
				return B_NO_MEMORY;
		} else
			buffer = stackBuffer;

		result = Flatten(buffer, size);
		if (result != B_OK) {
			if (buffer != stackBuffer)
				free(buffer);
			return result;
		}

		header = (message_header*)buffer;
	}

	if (!replyTo.IsValid()) {
		BMessenger::Private(replyTo).SetTo(fHeader->reply_team,
			fHeader->reply_port, fHeader->reply_target);

		if (!replyTo.IsValid())
			replyTo = be_app_messenger;
	}

	BMessenger::Private replyToPrivate(replyTo);

	if (replyRequired) {
		header->flags |= MESSAGE_FLAG_REPLY_REQUIRED;
		header->flags &= ~MESSAGE_FLAG_REPLY_DONE;
	}

	header->target = token;
	header->reply_team = replyToPrivate.Team();
	header->reply_port = replyToPrivate.Port();
	header->reply_target = replyToPrivate.Token();
	header->flags |= MESSAGE_FLAG_WAS_DELIVERED;

	if (direct == NULL) {
		KTRACE("BMessage send remote: team: %ld, port: %ld, token: %ld, "
			"message: '%c%c%c%c'", portOwner, port, token,
			char(what >> 24), char(what >> 16), char(what >> 8), (char)what);

		do {
			result = write_port_etc(port, kPortMessageCode, (void*)buffer,
				size, B_RELATIVE_TIMEOUT, timeout);
		} while (result == B_INTERRUPTED);
	}

	if (result == B_OK && IsSourceWaiting()) {
		// the forwarded message will handle the reply - we must not do
		// this anymore
		fHeader->flags |= MESSAGE_FLAG_REPLY_DONE;
	}

	// we need to do this last because it is possible our
	// message might be destroyed after it's enqueued in the
	// target looper. Thus we don't want to do any ops that depend on
	// members of this after the enqueue.
	if (direct != NULL) {
		KTRACE("BMessage send direct: port: %ld, token: %ld, "
			"message: '%c%c%c%c'", port, token,
			char(what >> 24), char(what >> 16), char(what >> 8), (char)what);

		// this is a local message transmission
		direct->AddMessage(copy);
		if (direct->Queue()->IsNextMessage(copy) && port_count(port) <= 0) {
			// there is currently no message waiting, and we need to wakeup the
			// looper
			write_port_etc(port, 0, NULL, 0, B_RELATIVE_TIMEOUT, 0);
		}
		direct->Release();
	}

	if (buffer != stackBuffer)
		free(buffer);
	return result;
}


/**
 * @brief Internal: sends this message synchronously and waits for a reply.
 *
 * Acquires a cached reply port (or creates a temporary one), transfers its
 * ownership to the target team, sends the message via the asynchronous
 * _SendMessage() overload with reply-required set, and then waits on the
 * reply port via handle_reply(). The reply port is reclaimed or recreated
 * after use.
 *
 * @param port         Destination port.
 * @param portOwner    Team that owns the destination port.
 * @param token        Target handler token.
 * @param reply        Receives the reply message.
 * @param sendTimeout  Send timeout in microseconds.
 * @param replyTimeout Reply-wait timeout in microseconds.
 * @return B_OK on success, B_ERROR if already waiting for a reply, or
 *         another error code.
 */
status_t
BMessage::_SendMessage(port_id port, team_id portOwner, int32 token,
	BMessage* reply, bigtime_t sendTimeout, bigtime_t replyTimeout) const
{
	if (IsSourceWaiting()) {
		// we can't forward this message synchronously when it's already
		// waiting for a reply
		return B_ERROR;
	}

	DEBUG_FUNCTION_ENTER;
	const int32 cachedReplyPort = _StaticGetCachedReplyPort();
	port_id replyPort = B_BAD_PORT_ID;
	status_t result = B_OK;

	if (cachedReplyPort < 0) {
		// All the cached reply ports are in use; create a new one
		replyPort = create_port(1 /* for one message */, "tmp_reply_port");
		if (replyPort < 0)
			return replyPort;
	} else {
		assert(cachedReplyPort < sNumReplyPorts);
		replyPort = sReplyPorts[cachedReplyPort];
	}

	bool recreateCachedPort = false;

	team_id team = B_BAD_TEAM_ID;
	if (be_app != NULL)
		team = be_app->Team();
	else {
		port_info portInfo;
		result = get_port_info(replyPort, &portInfo);
		if (result != B_OK)
			goto error;

		team = portInfo.team;
	}

	result = set_port_owner(replyPort, portOwner);
	if (result != B_OK)
		goto error;

	// tests if the queue of the reply port is really empty
#if 0
	port_info portInfo;
	if (get_port_info(replyPort, &portInfo) == B_OK
		&& portInfo.queue_count > 0) {
		debugger("reply port not empty!");
		printf("  reply port not empty! %ld message(s) in queue\n",
			portInfo.queue_count);

		// fetch and print the messages
		for (int32 i = 0; i < portInfo.queue_count; i++) {
			char buffer[1024];
			int32 code;
			ssize_t size = read_port(replyPort, &code, buffer, sizeof(buffer));
			if (size < 0) {
				printf("failed to read message from reply port\n");
				continue;
			}
			if (size >= (ssize_t)sizeof(buffer)) {
				printf("message from reply port too big\n");
				continue;
			}

			BMemoryIO stream(buffer, size);
			BMessage reply;
			if (reply.Unflatten(&stream) != B_OK) {
				printf("failed to unflatten message from reply port\n");
				continue;
			}

			printf("message %ld from reply port:\n", i);
			reply.PrintToStream();
		}
	}
#endif

	{
		BMessenger replyTarget;
		BMessenger::Private(replyTarget).SetTo(team, replyPort,
			B_PREFERRED_TOKEN);
		// TODO: replying could also use a BDirectMessageTarget like mechanism
		// for local targets
		result = _SendMessage(port, -1, token, sendTimeout, true,
			replyTarget);
	}

	if (result != B_OK)
		goto error;

	int32 code;
	result = handle_reply(replyPort, &code, replyTimeout, reply);
	if (result != B_OK && cachedReplyPort >= 0) {
		delete_port(replyPort);
		recreateCachedPort = true;
	}

error:
	if (cachedReplyPort >= 0) {
		// Reclaim ownership of cached port, if possible
		if (!recreateCachedPort && set_port_owner(replyPort, team) == B_OK) {
			// Flag as available
			atomic_add(&sReplyPortInUse[cachedReplyPort], -1);
		} else
			sReplyPorts[cachedReplyPort] = create_port(1, "tmp_rport");

		return result;
	}

	delete_port(replyPort);
	return result;
}


/**
 * @brief Sends an already-flattened message buffer through a port.
 *
 * Detects the message format (Haiku native, Haiku swapped, R5, or KMessage)
 * by inspecting the magic bytes, patches the target token and was-delivered
 * flag into the header, and writes the buffer to the port.
 *
 * @param data    Pointer to the flattened message data.
 * @param size    Size of the data in bytes.
 * @param port    Destination port.
 * @param token   Target handler token to patch into the header.
 * @param timeout Send timeout in microseconds.
 * @return B_OK on success, B_NOT_A_MESSAGE if the format is unrecognized,
 *         B_BAD_VALUE if @a data is NULL, or a port error.
 */
status_t
BMessage::_SendFlattenedMessage(void* data, int32 size, port_id port,
	int32 token, bigtime_t timeout)
{
	DEBUG_FUNCTION_ENTER2;
	if (data == NULL)
		return B_BAD_VALUE;

	uint32 magic = *(uint32*)data;

	if (magic == MESSAGE_FORMAT_HAIKU
		|| magic == MESSAGE_FORMAT_HAIKU_SWAPPED) {
		message_header* header = (message_header*)data;
		header->target = token;
		header->flags |= MESSAGE_FLAG_WAS_DELIVERED;
	} else if (magic == MESSAGE_FORMAT_R5) {
		uint8* header = (uint8*)data;
		header += sizeof(uint32) /* magic */ + sizeof(uint32) /* checksum */
			+ sizeof(ssize_t) /* flattenedSize */ + sizeof(int32) /* what */
			+ sizeof(uint8) /* flags */;
		*(int32*)header = token;
	} else if (((KMessage::Header*)data)->magic
			== KMessage::kMessageHeaderMagic) {
		KMessage::Header* header = (KMessage::Header*)data;
		header->targetToken = token;
	} else {
		return B_NOT_A_MESSAGE;
	}

	// send the message
	status_t result;

	do {
		result = write_port_etc(port, kPortMessageCode, data, size,
			B_RELATIVE_TIMEOUT, timeout);
	} while (result == B_INTERRUPTED);

	return result;
}


/** @brief Reserved virtual function slot 1 for future binary compatibility. */
void BMessage::_ReservedMessage1() {}
/** @brief Reserved virtual function slot 2 for future binary compatibility. */
void BMessage::_ReservedMessage2() {}
/** @brief Reserved virtual function slot 3 for future binary compatibility. */
void BMessage::_ReservedMessage3() {}


// #pragma mark - Macro definitions for data access methods


/**
 * @brief Macro-generated type-specific accessor functions.
 *
 * The DEFINE_FUNCTIONS macro expands to Add##typeName, Find##typeName (two
 * overloads), Replace##typeName (two overloads), Get##typeName (two
 * overloads), Set##typeName, and Has##typeName for each primitive type.
 *
 * For each generated function:
 * - @b Add adds a value to the message.
 * - @b Find retrieves a value by name and optional index.
 * - @b Replace replaces a value at a given name and optional index.
 * - @b Has tests for existence of a value at a given name and index.
 *
 * All functions delegate to AddData / FindData / ReplaceData / HasData.
 *
 * @param name  The field name.
 * @param value / p  The value to add/replace, or pointer to receive the found value.
 * @param index Zero-based item index (defaults to 0 in single-index overloads).
 * @return B_OK on success, or an error code (for status_t-returning functions).
 */

/* Relay functions from here on (Add... -> AddData, Find... -> FindData) */

#define DEFINE_FUNCTIONS(type, typeName, typeCode)							\
status_t																	\
BMessage::Add##typeName(const char* name, type val)							\
{																			\
	return AddData(name, typeCode, &val, sizeof(type), true);				\
}																			\
																			\
																			\
status_t																	\
BMessage::Find##typeName(const char* name, type* p) const					\
{																			\
	return Find##typeName(name, 0, p);										\
}																			\
																			\
																			\
status_t																	\
BMessage::Find##typeName(const char* name, int32 index, type* p) const		\
{																			\
	type* ptr = NULL;														\
	ssize_t bytes = 0;														\
	status_t error = B_OK;													\
																			\
	*p = type();															\
	error = FindData(name, typeCode, index, (const void**)&ptr, &bytes);	\
																			\
	if (error == B_OK)														\
		memcpy((void *)p, ptr, sizeof(type));								\
																			\
	return error;															\
}																			\
																			\
																			\
status_t																	\
BMessage::Replace##typeName(const char* name, type value)					\
{																			\
	return ReplaceData(name, typeCode, 0, &value, sizeof(type));			\
}																			\
																			\
																			\
status_t																	\
BMessage::Replace##typeName(const char* name, int32 index, type value)		\
{																			\
	return ReplaceData(name, typeCode, index, &value, sizeof(type));		\
}																			\
																			\
																			\
bool																		\
BMessage::Has##typeName(const char* name, int32 index) const				\
{																			\
	return HasData(name, typeCode, index);									\
}

DEFINE_FUNCTIONS(BPoint, Point, B_POINT_TYPE);
DEFINE_FUNCTIONS(BRect, Rect, B_RECT_TYPE);
DEFINE_FUNCTIONS(BSize, Size, B_SIZE_TYPE);
DEFINE_FUNCTIONS(int8, Int8, B_INT8_TYPE);
DEFINE_FUNCTIONS(uint8, UInt8, B_UINT8_TYPE);
DEFINE_FUNCTIONS(int16, Int16, B_INT16_TYPE);
DEFINE_FUNCTIONS(uint16, UInt16, B_UINT16_TYPE);
DEFINE_FUNCTIONS(int32, Int32, B_INT32_TYPE);
DEFINE_FUNCTIONS(uint32, UInt32, B_UINT32_TYPE);
DEFINE_FUNCTIONS(int64, Int64, B_INT64_TYPE);
DEFINE_FUNCTIONS(uint64, UInt64, B_UINT64_TYPE);
DEFINE_FUNCTIONS(bool, Bool, B_BOOL_TYPE);
DEFINE_FUNCTIONS(float, Float, B_FLOAT_TYPE);
DEFINE_FUNCTIONS(double, Double, B_DOUBLE_TYPE);
DEFINE_FUNCTIONS(rgb_color, Color, B_RGB_32_BIT_TYPE);

#undef DEFINE_FUNCTIONS

/**
 * @brief Macro-generated Has##typeName functions for types that do not use
 *        DEFINE_FUNCTIONS (e.g. Alignment, String, Pointer, Messenger, etc.).
 */
#define DEFINE_HAS_FUNCTION(typeName, typeCode)								\
bool																		\
BMessage::Has##typeName(const char* name, int32 index) const				\
{																			\
	return HasData(name, typeCode, index);									\
}


DEFINE_HAS_FUNCTION(Alignment, B_ALIGNMENT_TYPE);
DEFINE_HAS_FUNCTION(String, B_STRING_TYPE);
DEFINE_HAS_FUNCTION(Pointer, B_POINTER_TYPE);
DEFINE_HAS_FUNCTION(Messenger, B_MESSENGER_TYPE);
DEFINE_HAS_FUNCTION(Ref, B_REF_TYPE);
DEFINE_HAS_FUNCTION(NodeRef, B_NODE_REF_TYPE);
DEFINE_HAS_FUNCTION(Message, B_MESSAGE_TYPE);

#undef DEFINE_HAS_FUNCTION


/**
 * @brief Macro-generated convenience Find##typeName(name, index) functions
 *        that return a value directly (using a default on failure).
 */
#define DEFINE_LAZY_FIND_FUNCTION(type, typeName, initialize)				\
type																		\
BMessage::Find##typeName(const char* name, int32 index) const				\
{																			\
	type val = initialize;													\
	Find##typeName(name, index, &val);										\
	return val;																\
}


DEFINE_LAZY_FIND_FUNCTION(BRect, Rect, BRect());
DEFINE_LAZY_FIND_FUNCTION(BPoint, Point, BPoint());
DEFINE_LAZY_FIND_FUNCTION(const char*, String, NULL);
DEFINE_LAZY_FIND_FUNCTION(int8, Int8, 0);
DEFINE_LAZY_FIND_FUNCTION(int16, Int16, 0);
DEFINE_LAZY_FIND_FUNCTION(int32, Int32, 0);
DEFINE_LAZY_FIND_FUNCTION(int64, Int64, 0);
DEFINE_LAZY_FIND_FUNCTION(bool, Bool, false);
DEFINE_LAZY_FIND_FUNCTION(float, Float, 0);
DEFINE_LAZY_FIND_FUNCTION(double, Double, 0);

#undef DEFINE_LAZY_FIND_FUNCTION


/**
 * @brief Macro-generated Get##typeName / Set##typeName for primitive types.
 *
 * Get returns the stored value or a caller-supplied default. Set creates
 * or replaces a single-item field via SetData().
 */
#define DEFINE_SET_GET_FUNCTIONS(type, typeName, typeCode)					\
type																		\
BMessage::Get##typeName(const char* name, type defaultValue) const			\
{																			\
	return Get##typeName(name, 0, defaultValue);							\
}																			\
																			\
																			\
type																		\
BMessage::Get##typeName(const char* name, int32 index,						\
	type defaultValue) const												\
{																			\
	type value;																\
	if (Find##typeName(name, index, &value) == B_OK)						\
		return value;														\
																			\
	return defaultValue;													\
}																			\
																			\
																			\
status_t																	\
BMessage::Set##typeName(const char* name, type value)						\
{																			\
	return SetData(name, typeCode, &value, sizeof(type));					\
}																			\


DEFINE_SET_GET_FUNCTIONS(int8, Int8, B_INT8_TYPE);
DEFINE_SET_GET_FUNCTIONS(uint8, UInt8, B_UINT8_TYPE);
DEFINE_SET_GET_FUNCTIONS(int16, Int16, B_INT16_TYPE);
DEFINE_SET_GET_FUNCTIONS(uint16, UInt16, B_UINT16_TYPE);
DEFINE_SET_GET_FUNCTIONS(int32, Int32, B_INT32_TYPE);
DEFINE_SET_GET_FUNCTIONS(uint32, UInt32, B_UINT32_TYPE);
DEFINE_SET_GET_FUNCTIONS(int64, Int64, B_INT64_TYPE);
DEFINE_SET_GET_FUNCTIONS(uint64, UInt64, B_UINT64_TYPE);
DEFINE_SET_GET_FUNCTIONS(bool, Bool, B_BOOL_TYPE);
DEFINE_SET_GET_FUNCTIONS(float, Float, B_FLOAT_TYPE);
DEFINE_SET_GET_FUNCTIONS(double, Double, B_DOUBLE_TYPE);
DEFINE_SET_GET_FUNCTIONS(rgb_color, Color, B_RGB_32_BIT_TYPE);

#undef DEFINE_SET_GET_FUNCTION


/**
 * @brief Gets a pointer value from the message, returning a default on failure.
 * @param name         The field name.
 * @param defaultValue Value to return if the field is not found.
 * @return The stored pointer, or @a defaultValue.
 */
const void*
BMessage::GetPointer(const char* name, const void* defaultValue) const
{
	return GetPointer(name, 0, defaultValue);
}


/**
 * @brief Gets a pointer value by name and index, returning a default on failure.
 * @param name         The field name.
 * @param index        Zero-based item index.
 * @param defaultValue Value to return if the item is not found.
 * @return The stored pointer, or @a defaultValue.
 */
const void*
BMessage::GetPointer(const char* name, int32 index,
	const void* defaultValue) const
{
	void* value;
	if (FindPointer(name, index, &value) == B_OK)
		return value;

	return defaultValue;
}


/**
 * @brief Sets (creates or replaces) a pointer field.
 * @param name  The field name.
 * @param value The pointer value to store.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::SetPointer(const char* name, const void* value)
{
	return SetData(name, B_POINTER_TYPE, &value, sizeof(void*));
}


/**
 * @brief Macro-generated Get/Set for types passed by const reference
 *        (BPoint, BRect, BSize, BAlignment).
 */
#define DEFINE_SET_GET_BY_REFERENCE_FUNCTIONS(type, typeName, typeCode)		\
type																		\
BMessage::Get##typeName(const char* name, const type& defaultValue) const	\
{																			\
	return Get##typeName(name, 0, defaultValue);							\
}																			\
																			\
																			\
type																		\
BMessage::Get##typeName(const char* name, int32 index,						\
	const type& defaultValue) const											\
{																			\
	type value;																\
	if (Find##typeName(name, index, &value) == B_OK)						\
		return value;														\
																			\
	return defaultValue;													\
}																			\
																			\
																			\
status_t																	\
BMessage::Set##typeName(const char* name, const type& value)				\
{																			\
	return SetData(name, typeCode, &value, sizeof(type));					\
}																			\


DEFINE_SET_GET_BY_REFERENCE_FUNCTIONS(BPoint, Point, B_POINT_TYPE);
DEFINE_SET_GET_BY_REFERENCE_FUNCTIONS(BRect, Rect, B_RECT_TYPE);
DEFINE_SET_GET_BY_REFERENCE_FUNCTIONS(BSize, Size, B_SIZE_TYPE);
DEFINE_SET_GET_BY_REFERENCE_FUNCTIONS(BAlignment, Alignment, B_ALIGNMENT_TYPE);

#undef DEFINE_SET_GET_BY_REFERENCE_FUNCTIONS


/**
 * @brief Adds a BAlignment value to the message.
 * @param name      The field name.
 * @param alignment The alignment value to add.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::AddAlignment(const char* name, const BAlignment& alignment)
{
	int32 data[2] = { alignment.horizontal, alignment.vertical };
	return AddData(name, B_ALIGNMENT_TYPE, data, sizeof(data));
}


/**
 * @brief Adds a C string to the message.
 * @param name   The field name.
 * @param string The null-terminated string to add.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::AddString(const char* name, const char* string)
{
	return AddData(name, B_STRING_TYPE, string, string ? strlen(string) + 1 : 0,
		false);
}


/**
 * @brief Adds a BString to the message.
 * @param name   The field name.
 * @param string The BString to add.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::AddString(const char* name, const BString& string)
{
	return AddData(name, B_STRING_TYPE, string.String(), string.Length() + 1,
		false);
}


/**
 * @brief Adds all strings from a BStringList to the message under one field name.
 * @param name The field name.
 * @param list The list of strings to add.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::AddStrings(const char* name, const BStringList& list)
{
	int32 count = list.CountStrings();
	for (int32 i = 0; i < count; i++) {
		status_t error = AddString(name, list.StringAt(i));
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


/**
 * @brief Adds a pointer value to the message.
 * @param name    The field name.
 * @param pointer The pointer to store.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::AddPointer(const char* name, const void* pointer)
{
	return AddData(name, B_POINTER_TYPE, &pointer, sizeof(pointer), true);
}


/**
 * @brief Adds a BMessenger value to the message.
 * @param name      The field name.
 * @param messenger The messenger to store.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::AddMessenger(const char* name, BMessenger messenger)
{
	return AddData(name, B_MESSENGER_TYPE, &messenger, sizeof(messenger), true);
}


/**
 * @brief Adds an entry_ref to the message.
 * @param name The field name.
 * @param ref  The entry_ref to add.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::AddRef(const char* name, const entry_ref* ref)
{
	size_t size = sizeof(entry_ref) + B_PATH_NAME_LENGTH;
	char buffer[size];

	status_t error = BPrivate::entry_ref_flatten(buffer, &size, ref);

	if (error >= B_OK)
		error = AddData(name, B_REF_TYPE, buffer, size, false);

	return error;
}


/**
 * @brief Adds a node_ref to the message.
 * @param name The field name.
 * @param ref  The node_ref to add.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::AddNodeRef(const char* name, const node_ref* ref)
{
	size_t size = sizeof(node_ref);
	char buffer[size];

	status_t error = BPrivate::node_ref_flatten(buffer, &size, ref);

	if (error >= B_OK)
		error = AddData(name, B_NODE_REF_TYPE, buffer, size, false);

	return error;
}


/**
 * @brief Adds a nested BMessage to the message.
 *
 * The nested message is flattened and stored as a variable-size data item
 * of type B_MESSAGE_TYPE.
 *
 * @param name    The field name.
 * @param message The BMessage to nest.
 * @return B_OK on success, B_BAD_VALUE if @a message is NULL, B_NO_MEMORY,
 *         or an error code.
 */
status_t
BMessage::AddMessage(const char* name, const BMessage* message)
{
	if (message == NULL)
		return B_BAD_VALUE;

	// TODO: This and the following functions waste time by allocating and
	// copying an extra buffer. Functions can be added that return a direct
	// pointer into the message.

	ssize_t size = message->FlattenedSize();
	BStackOrHeapArray<char, 4096> buffer(size);
	if (!buffer.IsValid())
		return B_NO_MEMORY;

	status_t error = message->Flatten(buffer, size);

	if (error >= B_OK)
		error = AddData(name, B_MESSAGE_TYPE, buffer, size, false);

	return error;
}


/**
 * @brief Adds a BFlattenable object to the message (non-const overload).
 * @param name   The field name.
 * @param object The flattenable object to add.
 * @param count  Hint for expected number of items (unused).
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::AddFlat(const char* name, BFlattenable* object, int32 count)
{
	return AddFlat(name, (const BFlattenable*)object, count);
}


/**
 * @brief Adds a BFlattenable object to the message.
 *
 * Flattens the object into a temporary buffer and adds the data under the
 * object's own TypeCode().
 *
 * @param name   The field name.
 * @param object The flattenable object to add.
 * @param count  Hint for expected number of items (unused).
 * @return B_OK on success, B_BAD_VALUE if @a object is NULL, B_NO_MEMORY,
 *         or an error code.
 */
status_t
BMessage::AddFlat(const char* name, const BFlattenable* object, int32 count)
{
	if (object == NULL)
		return B_BAD_VALUE;

	ssize_t size = object->FlattenedSize();
	BStackOrHeapArray<char, 4096> buffer(size);
	if (!buffer.IsValid())
		return B_NO_MEMORY;

	status_t error = object->Flatten(buffer, size);

	if (error >= B_OK)
		error = AddData(name, object->TypeCode(), buffer, size, false);

	return error;
}


/**
 * @brief Appends all fields and data from another message into this one.
 *
 * Iterates over every field in @a other and adds each item individually
 * via AddData(), preserving names, types, and fixed-size flags.
 *
 * @param other The source message whose fields are appended.
 * @return B_OK on success, or the first error encountered.
 */
status_t
BMessage::Append(const BMessage& other)
{
	field_header* field = other.fFields;
	for (uint32 i = 0; i < other.fHeader->field_count; i++, field++) {
		const char* name = (const char*)(other.fData + field->offset);
		const void* data = (const void*)(other.fData + field->offset
			+ field->name_length);
		bool isFixed = (field->flags & FIELD_FLAG_FIXED_SIZE) != 0;
		size_t size = field->data_size / field->count;

		for (uint32 j = 0; j < field->count; j++) {
			if (!isFixed) {
				size = *(uint32*)data;
				data = (const void*)((const char*)data + sizeof(uint32));
			}

			status_t status = AddData(name, field->type, data, size,
				isFixed, 1);
			if (status != B_OK)
				return status;

			data = (const void*)((const char*)data + size);
		}
	}
	return B_OK;
}


/**
 * @brief Finds a BAlignment value by name.
 * @param name      The field name.
 * @param alignment Receives the alignment value.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::FindAlignment(const char* name, BAlignment* alignment) const
{
	return FindAlignment(name, 0, alignment);
}


/**
 * @brief Finds a BAlignment value by name and index.
 * @param name      The field name.
 * @param index     Zero-based item index.
 * @param alignment Receives the alignment value.
 * @return B_OK on success, B_BAD_VALUE, B_ERROR if size mismatch, or an error code.
 */
status_t
BMessage::FindAlignment(const char* name, int32 index, BAlignment* alignment)
	const
{
	if (!alignment)
		return B_BAD_VALUE;

	int32* data;
	ssize_t bytes;

	status_t err = FindData(name, B_ALIGNMENT_TYPE, index,
		(const void**)&data, &bytes);

	if (err == B_OK) {
		if (bytes != sizeof(int32[2]))
			return B_ERROR;

		alignment->horizontal = (enum alignment)(*data);
		alignment->vertical = (vertical_alignment)*(data + 1);
	}

	return err;
}


/**
 * @brief Finds a string by name (first item).
 * @param name   The field name.
 * @param string Receives a pointer to the string data.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::FindString(const char* name, const char** string) const
{
	return FindString(name, 0, string);
}


/**
 * @brief Finds a string by name and index.
 * @param name   The field name.
 * @param index  Zero-based item index.
 * @param string Receives a pointer to the string data.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::FindString(const char* name, int32 index, const char** string) const
{
	ssize_t bytes;
	return FindData(name, B_STRING_TYPE, index, (const void**)string, &bytes);
}


/**
 * @brief Finds a string by name and copies it into a BString (first item).
 * @param name   The field name.
 * @param string Receives the string value.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::FindString(const char* name, BString* string) const
{
	return FindString(name, 0, string);
}


/**
 * @brief Finds a string by name and index, copying it into a BString.
 * @param name   The field name.
 * @param index  Zero-based item index.
 * @param string Receives the string value.
 * @return B_OK on success, B_BAD_VALUE if @a string is NULL, or an error code.
 */
status_t
BMessage::FindString(const char* name, int32 index, BString* string) const
{
	if (string == NULL)
		return B_BAD_VALUE;

	const char* value;
	status_t error = FindString(name, index, &value);

	// Find*() clobbers the object even on failure
	string->SetTo(value);
	return error;
}


/**
 * @brief Finds all strings under a field name and populates a BStringList.
 * @param name The field name.
 * @param list Receives the list of strings.
 * @return B_OK on success, B_BAD_VALUE if @a list is NULL,
 *         B_NAME_NOT_FOUND, B_BAD_DATA if the field is not B_STRING_TYPE,
 *         or B_NO_MEMORY.
 */
status_t
BMessage::FindStrings(const char* name, BStringList* list) const
{
	if (list == NULL)
		return B_BAD_VALUE;

	list->MakeEmpty();

	// get the number of items
	type_code type;
	int32 count;
	if (GetInfo(name, &type, &count) != B_OK)
		return B_NAME_NOT_FOUND;

	if (type != B_STRING_TYPE)
		return B_BAD_DATA;

	for (int32 i = 0; i < count; i++) {
		BString string;
		status_t error = FindString(name, i, &string);
		if (error != B_OK)
			return error;
		if (!list->Add(string))
			return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Finds a pointer value by name (first item).
 * @param name    The field name.
 * @param pointer Receives the pointer value.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::FindPointer(const char* name, void** pointer) const
{
	return FindPointer(name, 0, pointer);
}


/**
 * @brief Finds a pointer value by name and index.
 * @param name    The field name.
 * @param index   Zero-based item index.
 * @param pointer Receives the pointer value.
 * @return B_OK on success, B_BAD_VALUE if @a pointer is NULL, or an error code.
 */
status_t
BMessage::FindPointer(const char* name, int32 index, void** pointer) const
{
	if (pointer == NULL)
		return B_BAD_VALUE;

	void** data = NULL;
	ssize_t size = 0;
	status_t error = FindData(name, B_POINTER_TYPE, index,
		(const void**)&data, &size);

	if (error == B_OK)
		*pointer = *data;
	else
		*pointer = NULL;

	return error;
}


/**
 * @brief Finds a BMessenger value by name (first item).
 * @param name      The field name.
 * @param messenger Receives the messenger value.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::FindMessenger(const char* name, BMessenger* messenger) const
{
	return FindMessenger(name, 0, messenger);
}


/**
 * @brief Finds a BMessenger value by name and index.
 * @param name      The field name.
 * @param index     Zero-based item index.
 * @param messenger Receives the messenger value.
 * @return B_OK on success, B_BAD_VALUE if @a messenger is NULL, or an error code.
 */
status_t
BMessage::FindMessenger(const char* name, int32 index,
	BMessenger* messenger) const
{
	if (messenger == NULL)
		return B_BAD_VALUE;

	BMessenger* data = NULL;
	ssize_t size = 0;
	status_t error = FindData(name, B_MESSENGER_TYPE, index,
		(const void**)&data, &size);

	if (error == B_OK)
		*messenger = *data;
	else
		*messenger = BMessenger();

	return error;
}


/**
 * @brief Finds an entry_ref by name (first item).
 * @param name The field name.
 * @param ref  Receives the entry_ref.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::FindRef(const char* name, entry_ref* ref) const
{
	return FindRef(name, 0, ref);
}


/**
 * @brief Finds an entry_ref by name and index.
 * @param name  The field name.
 * @param index Zero-based item index.
 * @param ref   Receives the entry_ref.
 * @return B_OK on success, B_BAD_VALUE if @a ref is NULL, or an error code.
 */
status_t
BMessage::FindRef(const char* name, int32 index, entry_ref* ref) const
{
	if (ref == NULL)
		return B_BAD_VALUE;

	void* data = NULL;
	ssize_t size = 0;
	status_t error = FindData(name, B_REF_TYPE, index,
		(const void**)&data, &size);

	if (error == B_OK)
		error = BPrivate::entry_ref_unflatten(ref, (char*)data, size);
	else
		*ref = entry_ref();

	return error;
}


/**
 * @brief Finds a node_ref by name (first item).
 * @param name The field name.
 * @param ref  Receives the node_ref.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::FindNodeRef(const char* name, node_ref* ref) const
{
	return FindNodeRef(name, 0, ref);
}


/**
 * @brief Finds a node_ref by name and index.
 * @param name  The field name.
 * @param index Zero-based item index.
 * @param ref   Receives the node_ref.
 * @return B_OK on success, B_BAD_VALUE if @a ref is NULL, or an error code.
 */
status_t
BMessage::FindNodeRef(const char* name, int32 index, node_ref* ref) const
{
	if (ref == NULL)
		return B_BAD_VALUE;

	void* data = NULL;
	ssize_t size = 0;
	status_t error = FindData(name, B_NODE_REF_TYPE, index,
		(const void**)&data, &size);

	if (error == B_OK)
		error = BPrivate::node_ref_unflatten(ref, (char*)data, size);
	else
		*ref = node_ref();

	return error;
}


/**
 * @brief Finds a nested BMessage by name (first item).
 * @param name    The field name.
 * @param message Receives the unflattened message.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::FindMessage(const char* name, BMessage* message) const
{
	return FindMessage(name, 0, message);
}


/**
 * @brief Finds a nested BMessage by name and index.
 * @param name    The field name.
 * @param index   Zero-based item index.
 * @param message Receives the unflattened message.
 * @return B_OK on success, B_BAD_VALUE if @a message is NULL, or an error code.
 */
status_t
BMessage::FindMessage(const char* name, int32 index, BMessage* message) const
{
	if (message == NULL)
		return B_BAD_VALUE;

	void* data = NULL;
	ssize_t size = 0;
	status_t error = FindData(name, B_MESSAGE_TYPE, index,
		(const void**)&data, &size);

	if (error == B_OK)
		error = message->Unflatten((const char*)data);
	else
		*message = BMessage();

	return error;
}


/**
 * @brief Finds and unflattens a BFlattenable object by name (first item).
 * @param name   The field name.
 * @param object The flattenable object to populate.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::FindFlat(const char* name, BFlattenable* object) const
{
	return FindFlat(name, 0, object);
}


/**
 * @brief Finds and unflattens a BFlattenable object by name and index.
 * @param name   The field name.
 * @param index  Zero-based item index.
 * @param object The flattenable object to populate.
 * @return B_OK on success, B_BAD_VALUE if @a object is NULL, or an error code.
 */
status_t
BMessage::FindFlat(const char* name, int32 index, BFlattenable* object) const
{
	if (object == NULL)
		return B_BAD_VALUE;

	void* data = NULL;
	ssize_t numBytes = 0;
	status_t error = FindData(name, object->TypeCode(), index,
		(const void**)&data, &numBytes);

	if (error == B_OK)
		error = object->Unflatten(object->TypeCode(), data, numBytes);

	return error;
}


/**
 * @brief Finds raw data by name and type (first item convenience overload).
 * @param name     The field name.
 * @param type     Required type code, or B_ANY_TYPE.
 * @param data     Receives a pointer to the raw data.
 * @param numBytes Receives the size of the data in bytes.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::FindData(const char* name, type_code type, const void** data,
	ssize_t* numBytes) const
{
	return FindData(name, type, 0, data, numBytes);
}


/**
 * @brief Replaces the first BAlignment value in a named field.
 * @param name      The field name.
 * @param alignment The replacement value.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::ReplaceAlignment(const char* name, const BAlignment& alignment)
{
	int32 data[2] = {alignment.horizontal, alignment.vertical};
	return ReplaceData(name, B_ALIGNMENT_TYPE, 0, data, sizeof(data));
}


/**
 * @brief Replaces a BAlignment value at the given index in a named field.
 * @param name      The field name.
 * @param index     Zero-based item index.
 * @param alignment The replacement value.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::ReplaceAlignment(const char* name, int32 index,
	const BAlignment& alignment)
{
	int32 data[2] = {alignment.horizontal, alignment.vertical};
	return ReplaceData(name, B_ALIGNMENT_TYPE, index, data, sizeof(data));
}


/**
 * @brief Replaces the first string in a named field.
 * @param name   The field name.
 * @param string The replacement C string.
 * @return B_OK on success, B_BAD_VALUE if @a string is NULL, or an error code.
 */
status_t
BMessage::ReplaceString(const char* name, const char* string)
{
	if (string == NULL)
		return B_BAD_VALUE;

	return ReplaceData(name, B_STRING_TYPE, 0, string, strlen(string) + 1);
}


/**
 * @brief Replaces a string at the given index in a named field.
 * @param name   The field name.
 * @param index  Zero-based item index.
 * @param string The replacement C string.
 * @return B_OK on success, B_BAD_VALUE if @a string is NULL, or an error code.
 */
status_t
BMessage::ReplaceString(const char* name, int32 index, const char* string)
{
	if (string == NULL)
		return B_BAD_VALUE;

	return ReplaceData(name, B_STRING_TYPE, index, string, strlen(string) + 1);
}


/**
 * @brief Replaces the first string in a named field with a BString.
 * @param name   The field name.
 * @param string The replacement BString.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::ReplaceString(const char* name, const BString& string)
{
	return ReplaceData(name, B_STRING_TYPE, 0, string.String(),
		string.Length() + 1);
}


/**
 * @brief Replaces a string at the given index with a BString.
 * @param name   The field name.
 * @param index  Zero-based item index.
 * @param string The replacement BString.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::ReplaceString(const char* name, int32 index, const BString& string)
{
	return ReplaceData(name, B_STRING_TYPE, index, string.String(),
		string.Length() + 1);
}


/**
 * @brief Replaces the first pointer value in a named field.
 * @param name    The field name.
 * @param pointer The replacement pointer.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::ReplacePointer(const char* name, const void* pointer)
{
	return ReplaceData(name, B_POINTER_TYPE, 0, &pointer, sizeof(pointer));
}


/**
 * @brief Replaces a pointer value at the given index.
 * @param name    The field name.
 * @param index   Zero-based item index.
 * @param pointer The replacement pointer.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::ReplacePointer(const char* name, int32 index, const void* pointer)
{
	return ReplaceData(name, B_POINTER_TYPE, index, &pointer, sizeof(pointer));
}


/**
 * @brief Replaces the first BMessenger value in a named field.
 * @param name      The field name.
 * @param messenger The replacement messenger.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::ReplaceMessenger(const char* name, BMessenger messenger)
{
	return ReplaceData(name, B_MESSENGER_TYPE, 0, &messenger,
		sizeof(BMessenger));
}


/**
 * @brief Replaces a BMessenger value at the given index.
 * @param name      The field name.
 * @param index     Zero-based item index.
 * @param messenger The replacement messenger.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::ReplaceMessenger(const char* name, int32 index, BMessenger messenger)
{
	return ReplaceData(name, B_MESSENGER_TYPE, index, &messenger,
		sizeof(BMessenger));
}


/**
 * @brief Replaces the first entry_ref in a named field.
 * @param name The field name.
 * @param ref  The replacement entry_ref.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::ReplaceRef(const char* name, const entry_ref* ref)
{
	return ReplaceRef(name, 0, ref);
}


/**
 * @brief Replaces an entry_ref at the given index.
 * @param name  The field name.
 * @param index Zero-based item index.
 * @param ref   The replacement entry_ref.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::ReplaceRef(const char* name, int32 index, const entry_ref* ref)
{
	size_t size = sizeof(entry_ref) + B_PATH_NAME_LENGTH;
	char buffer[size];

	status_t error = BPrivate::entry_ref_flatten(buffer, &size, ref);

	if (error >= B_OK)
		error = ReplaceData(name, B_REF_TYPE, index, buffer, size);

	return error;
}


/**
 * @brief Replaces the first node_ref in a named field.
 * @param name The field name.
 * @param ref  The replacement node_ref.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::ReplaceNodeRef(const char* name, const node_ref* ref)
{
	return ReplaceNodeRef(name, 0, ref);
}


/**
 * @brief Replaces a node_ref at the given index.
 * @param name  The field name.
 * @param index Zero-based item index.
 * @param ref   The replacement node_ref.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::ReplaceNodeRef(const char* name, int32 index, const node_ref* ref)
{
	size_t size = sizeof(node_ref) + B_PATH_NAME_LENGTH;
	char buffer[size];

	status_t error = BPrivate::node_ref_flatten(buffer, &size, ref);

	if (error >= B_OK)
		error = ReplaceData(name, B_NODE_REF_TYPE, index, buffer, size);

	return error;
}


/**
 * @brief Replaces the first nested BMessage in a named field.
 * @param name    The field name.
 * @param message The replacement message.
 * @return B_OK on success, B_BAD_VALUE if @a message is NULL, or an error code.
 */
status_t
BMessage::ReplaceMessage(const char* name, const BMessage* message)
{
	return ReplaceMessage(name, 0, message);
}


/**
 * @brief Replaces a nested BMessage at the given index.
 * @param name    The field name.
 * @param index   Zero-based item index.
 * @param message The replacement message.
 * @return B_OK on success, B_BAD_VALUE if @a message is NULL, or an error code.
 */
status_t
BMessage::ReplaceMessage(const char* name, int32 index, const BMessage* message)
{
	if (message == NULL)
		return B_BAD_VALUE;

	ssize_t size = message->FlattenedSize();
	if (size < 0)
		return B_BAD_VALUE;

	char buffer[size];

	status_t error = message->Flatten(buffer, size);

	if (error >= B_OK)
		error = ReplaceData(name, B_MESSAGE_TYPE, index, &buffer, size);

	return error;
}


/**
 * @brief Replaces the first BFlattenable object in a named field.
 * @param name   The field name.
 * @param object The replacement flattenable object.
 * @return B_OK on success, B_BAD_VALUE if @a object is NULL, or an error code.
 */
status_t
BMessage::ReplaceFlat(const char* name, BFlattenable* object)
{
	return ReplaceFlat(name, 0, object);
}


/**
 * @brief Replaces a BFlattenable object at the given index.
 * @param name   The field name.
 * @param index  Zero-based item index.
 * @param object The replacement flattenable object.
 * @return B_OK on success, B_BAD_VALUE if @a object is NULL, or an error code.
 */
status_t
BMessage::ReplaceFlat(const char* name, int32 index, BFlattenable* object)
{
	if (object == NULL)
		return B_BAD_VALUE;

	ssize_t size = object->FlattenedSize();
	if (size < 0)
		return B_BAD_VALUE;

	char buffer[size];

	status_t error = object->Flatten(buffer, size);

	if (error >= B_OK)
		error = ReplaceData(name, object->TypeCode(), index, &buffer, size);

	return error;
}


/**
 * @brief Replaces the first raw data item in a named field (convenience overload).
 * @param name     The field name.
 * @param type     Required type code.
 * @param data     Pointer to the replacement data.
 * @param numBytes Size of the replacement data in bytes.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::ReplaceData(const char* name, type_code type, const void* data,
	ssize_t numBytes)
{
	return ReplaceData(name, type, 0, data, numBytes);
}


/**
 * @brief Tests whether a BFlattenable-compatible field exists (first item).
 * @param name   The field name.
 * @param object A flattenable whose TypeCode() is used for the type check.
 * @return true if the field exists with the matching type.
 */
bool
BMessage::HasFlat(const char* name, const BFlattenable* object) const
{
	return HasFlat(name, 0, object);
}


/**
 * @brief Tests whether a BFlattenable-compatible field exists at an index.
 * @param name   The field name.
 * @param index  Zero-based item index.
 * @param object A flattenable whose TypeCode() is used for the type check.
 * @return true if the item exists with the matching type.
 */
bool
BMessage::HasFlat(const char* name, int32 index, const BFlattenable* object)
	const
{
	return HasData(name, object->TypeCode(), index);
}


/**
 * @brief Gets a string value, returning a default on failure (first item).
 * @param name         The field name.
 * @param defaultValue Value to return if the field is not found.
 * @return The stored string, or @a defaultValue.
 */
const char*
BMessage::GetString(const char* name, const char* defaultValue) const
{
	return GetString(name, 0, defaultValue);
}


/**
 * @brief Gets a string value by name and index, returning a default on failure.
 * @param name         The field name.
 * @param index        Zero-based item index.
 * @param defaultValue Value to return if the item is not found.
 * @return The stored string, or @a defaultValue.
 */
const char*
BMessage::GetString(const char* name, int32 index,
	const char* defaultValue) const
{
	const char* value;
	if (FindString(name, index, &value) == B_OK)
		return value;

	return defaultValue;
}


/**
 * @brief Sets (creates or replaces) a string field from a BString.
 * @param name  The field name.
 * @param value The string value to store.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::SetString(const char* name, const BString& value)
{
	return SetData(name, B_STRING_TYPE, value.String(), value.Length() + 1,
		false);
}


/**
 * @brief Sets (creates or replaces) a string field from a C string.
 * @param name  The field name.
 * @param value The null-terminated string to store.
 * @return B_OK on success, or an error code.
 */
status_t
BMessage::SetString(const char* name, const char* value)
{
	return SetData(name, B_STRING_TYPE, value, strlen(value) + 1, false);
}


/**
 * @brief Creates or replaces a single-item field with the given raw data.
 *
 * Attempts ReplaceData() first; if the field does not exist, falls back to
 * AddData(). This is the common implementation behind all Set##typeName
 * convenience functions.
 *
 * @param name      The field name.
 * @param type      The type code.
 * @param data      Pointer to the data.
 * @param numBytes  Size of the data in bytes.
 * @param fixedSize true if the field stores fixed-size items.
 * @param count     Hint for expected number of items (unused).
 * @return B_OK on success, B_BAD_VALUE if @a data is NULL or @a numBytes <= 0,
 *         or an error code from ReplaceData/AddData.
 */
status_t
BMessage::SetData(const char* name, type_code type, const void* data,
	ssize_t numBytes, bool fixedSize, int count)
{
	if (numBytes <= 0 || data == NULL)
		return B_BAD_VALUE;

	if (ReplaceData(name, type, data, numBytes) == B_OK)
		return B_OK;

	return AddData(name, type, data, numBytes, fixedSize, count);
}
