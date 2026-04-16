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
 *   Copyright 2005-2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file KMessage.cpp
 * @brief Kernel-side flat message container used for IPC.
 *
 * Implements KMessage and its helper KMessageField. A KMessage is a
 * self-describing, flat byte buffer laid out as:
 *   [Header][FieldHeader + name + (fixed | FieldValueHeader+value)*]*
 * Fields are appended linearly; lookup walks the chain via GetNextField().
 * The on-the-wire format is portable enough to be shipped across ports
 * (see SendTo()/ReceiveFrom()) and interpreted in-place by readers via
 * KMESSAGE_INIT_FROM_BUFFER.
 *
 * Buffer ownership is tracked via KMESSAGE_OWNS_BUFFER. Until a field is
 * added, KMessage lazily uses its embedded Header member (fHeader) as the
 * backing buffer (see Unset() and _AllocateSpace()), and only promotes to
 * a heap buffer on first allocation. Buffers may also be read-only
 * (KMESSAGE_READ_ONLY) or cloned on attach (KMESSAGE_CLONE_BUFFER).
 */

#include <util/KMessage.h>

#include <stdlib.h>
#include <string.h>

#include <ByteOrder.h>
#include <Debug.h>
#include <KernelExport.h>
#include <TypeConstants.h>


#if defined(_BOOT_MODE) || defined(_LOADER_MODE)
#	include <util/kernel_cpp.h>
#else
#	include <new>
#endif


// TODO: Add a field index using a hash map, so that lookup improves to O(1)
// (is now O(n)).


// define the PANIC macro
#ifndef PANIC
#	if defined(_KERNEL_MODE) || defined(_BOOT_MODE)
#		define PANIC(str)	panic(str)
#	else
#		define PANIC(str)	debugger(str)
#	endif
#endif


#if !defined(HAIKU_TARGET_PLATFORM_HAIKU) || defined(_BOOT_MODE) \
	|| defined(_LOADER_MODE)
#	define MEMALIGN(alignment, size)	malloc(size)
	// Built as part of a build tool or the boot or runtime loader.
#else
#	include <malloc.h>
#	define MEMALIGN(alignment, size)	memalign(alignment, size)
	// Built as part of the kernel or userland. Using memalign allows use of
	// special heap implementations that might otherwise return unaligned
	// buffers for debugging purposes.
#endif


static const int32 kMessageReallocChunkSize = 64;
static const size_t kMessageBufferAlignment = 4;

const uint32 KMessage::kMessageHeaderMagic = 'kMsG';


// #pragma mark - Helper Functions

/**
 * @brief Rounds an integer offset up to the message buffer alignment.
 *
 * @param offset Raw byte offset within the message buffer.
 * @return The smallest value >= @a offset that is aligned to
 *         kMessageBufferAlignment.
 */
static inline int32
_Align(int32 offset)
{
	return (offset + kMessageBufferAlignment - 1)
		& ~(kMessageBufferAlignment - 1);
}


/**
 * @brief Rounds a pointer up to the message buffer alignment.
 *
 * @param address Base address.
 * @param offset  Optional extra bytes to add before aligning (defaults to 0).
 * @return The smallest aligned address >= @a address + @a offset.
 */
static inline void*
_Align(void* address, int32 offset = 0)
{
	return (void*)(((addr_t)address + offset + kMessageBufferAlignment - 1)
		& ~(kMessageBufferAlignment - 1));
}


// #pragma mark - FieldValueHeader


struct KMessage::FieldValueHeader {
	int32		size;

	/**
	 * @brief Returns a pointer to the value bytes following this header.
	 *
	 * @return Aligned pointer to the start of this value's data region.
	 */
	void* Data()
	{
		return _Align(this, sizeof(FieldValueHeader));
	}

	/**
	 * @brief Advances to the next FieldValueHeader in a non-fixed-size field.
	 *
	 * @return Pointer to the next value header immediately after this one's
	 *         data, aligned to the buffer alignment.
	 */
	FieldValueHeader* NextFieldValueHeader()
	{
		return (FieldValueHeader*)_Align(Data(), size);
	}
};


// #pragma mark - FieldHeader


struct KMessage::FieldHeader {
	type_code	type;
	int32		elementSize;	// if < 0: non-fixed size
	int32		elementCount;
	int32		fieldSize;
	int16		headerSize;
	char		name[1];

	/**
	 * @brief Returns a pointer to the field's element area.
	 *
	 * @return Pointer to the bytes that follow the field header and name.
	 */
	void* Data()
	{
		return (uint8*)this + headerSize;
	}

	/**
	 * @brief Reports whether the field stores fixed-size elements.
	 *
	 * @return true if every element occupies exactly @c elementSize bytes;
	 *         false if elements carry per-value FieldValueHeader entries.
	 */
	bool HasFixedElementSize()
	{
		return elementSize >= 0;
	}

	/**
	 * @brief Locates the @a index'th element within this field.
	 *
	 * For fixed-size fields the lookup is O(1); for variable-size fields it
	 * walks the chain of FieldValueHeader entries.
	 *
	 * @param index Zero-based element index.
	 * @param size  Out: the element's size in bytes when found.
	 * @return Pointer to the element data, or NULL if @a index is out of
	 *         range.
	 */
	void* ElementAt(int32 index, int32* size)
	{
		if (index < 0 || index >= elementCount)
			return NULL;
		uint8* data = (uint8*)this + headerSize;
		if (HasFixedElementSize()) {
			*size = elementSize;
			return data + elementSize * index;
		}
		// non-fixed element size: we need to iterate
		FieldValueHeader* valueHeader = (FieldValueHeader*)data;
		for (int i = 0; i < index; i++)
			valueHeader = valueHeader->NextFieldValueHeader();
		*size = valueHeader->size;
		return valueHeader->Data();
	}

	/**
	 * @brief Advances to the next field header in the buffer.
	 *
	 * @return Pointer to the FieldHeader that begins immediately after this
	 *         field's declared @c fieldSize, properly aligned.
	 */
	FieldHeader* NextFieldHeader()
	{
		return (FieldHeader*)_Align(this, fieldSize);
	}
};


// #pragma mark - KMessage


/**
 * @brief Constructs an empty KMessage.
 *
 * Lazily initialises @c fBuffer to point at the embedded @c fHeader so no
 * heap allocation is performed until a field is added.
 */
KMessage::KMessage()
	:
	fBuffer(NULL),
	fBufferCapacity(0),
	fFlags(0),
	fLastFieldOffset(0)
{
	Unset();
}


/**
 * @brief Constructs an empty KMessage with a preset @c what code.
 *
 * @param what Initial message @c what value stored in the header.
 */
KMessage::KMessage(uint32 what)
	:
	fBuffer(NULL),
	fBufferCapacity(0),
	fFlags(0),
	fLastFieldOffset(0)
{
	Unset();
	SetWhat(what);
}


/**
 * @brief Destroys the message, releasing any owned buffer.
 */
KMessage::~KMessage()
{
	Unset();
}


/**
 * @brief Resets the message to an empty state with the given @c what.
 *
 * @param what  New message @c what code.
 * @param flags Reserved; currently no flags are interpreted in this form.
 * @return B_OK on success.
 */
status_t
KMessage::SetTo(uint32 what, uint32 flags)
{
	// There are no flags interesting in this case at the moment.
	Unset();
	SetWhat(what);
	return B_OK;
}


/**
 * @brief Attaches the message to a caller-supplied buffer.
 *
 * Depending on the flags the buffer is either (a) treated as empty scratch
 * storage to build a new message, (b) parsed as an already-formatted
 * message (KMESSAGE_INIT_FROM_BUFFER), possibly read-only
 * (KMESSAGE_READ_ONLY), and/or (c) cloned into a private heap copy
 * (KMESSAGE_CLONE_BUFFER). If KMESSAGE_OWNS_BUFFER is set, the message
 * takes responsibility for freeing the buffer on Unset()/destruction.
 *
 * @param buffer     Caller-provided buffer; must be non-NULL.
 * @param bufferSize Buffer size in bytes, or -1 with KMESSAGE_INIT_FROM_BUFFER
 *                   to read the size from the embedded header.
 * @param what       Initial @c what value when building a fresh message.
 * @param flags      Bitmask of KMESSAGE_* flags.
 * @return B_OK on success; B_BAD_VALUE for illegal argument combinations;
 *         another error propagated from _InitFromBuffer().
 */
status_t
KMessage::SetTo(void* buffer, int32 bufferSize, uint32 what, uint32 flags)
{
	Unset();

	if (!buffer)
		return B_BAD_VALUE;

	if (bufferSize < 0) {
		if (!(flags & KMESSAGE_INIT_FROM_BUFFER))
			return B_BAD_VALUE;
	} else if (bufferSize < (int)sizeof(Header))
		return B_BAD_VALUE;

	// if read-only, we need to init from the buffer, too
	if ((flags & KMESSAGE_READ_ONLY) != 0
		&& (flags & KMESSAGE_INIT_FROM_BUFFER) == 0) {
		return B_BAD_VALUE;
	}

	// if not initializing from the given buffer, cloning it doesn't make sense
	if ((flags & KMESSAGE_INIT_FROM_BUFFER) == 0
		&& (flags & KMESSAGE_CLONE_BUFFER) != 0) {
		return B_BAD_VALUE;
	}

	fBuffer = buffer;
	fBufferCapacity = bufferSize;
	fFlags = flags;

	status_t error = B_OK;
	if (flags & KMESSAGE_INIT_FROM_BUFFER)
		error = _InitFromBuffer(bufferSize < 0);
	else
		_InitBuffer(what);

	if (error != B_OK)
		Unset();

	return error;
}


/**
 * @brief Attaches the message to a read-only, pre-formatted buffer.
 *
 * Convenience wrapper that forces KMESSAGE_INIT_FROM_BUFFER and
 * KMESSAGE_READ_ONLY on top of any caller-provided flags.
 *
 * @param buffer     Pointer to an already-formatted message buffer.
 * @param bufferSize Buffer size, or -1 to read it from the header.
 * @param flags      Additional KMESSAGE_* flags OR-ed with the defaults.
 * @return B_OK on success, or an error from the underlying SetTo().
 */
status_t
KMessage::SetTo(const void* buffer, int32 bufferSize, uint32 flags)
{
	return SetTo(const_cast<void*>(buffer), bufferSize, 0,
		KMESSAGE_INIT_FROM_BUFFER | KMESSAGE_READ_ONLY | flags);
}


/**
 * @brief Releases the current buffer and reverts to the lazy empty state.
 *
 * If the message owned its buffer (KMESSAGE_OWNS_BUFFER) the buffer is
 * freed. The buffer pointer is then redirected to the embedded @c fHeader
 * so the object remains usable without an immediate allocation.
 */
void
KMessage::Unset()
{
	// free buffer
	if (fBuffer && fBuffer != &fHeader && (fFlags & KMESSAGE_OWNS_BUFFER))
		free(fBuffer);
	fBuffer = &fHeader;
	fBufferCapacity = sizeof(Header);
	_InitBuffer(0);
}


/**
 * @brief Sets the message's @c what identifier.
 *
 * @param what Message type code stored in the header.
 */
void
KMessage::SetWhat(uint32 what)
{
	_Header()->what = what;
}


/**
 * @brief Returns the message's @c what identifier.
 *
 * @return Current @c what code from the header.
 */
uint32
KMessage::What() const
{
	return _Header()->what;
}


/**
 * @brief Returns the underlying flat buffer.
 *
 * @return Read-only pointer to the start of the message buffer.
 */
const void*
KMessage::Buffer() const
{
	return fBuffer;
}


/**
 * @brief Returns the allocated capacity of the buffer.
 *
 * @return Buffer capacity in bytes (may exceed ContentSize()).
 */
int32
KMessage::BufferCapacity() const
{
	return fBufferCapacity;
}


/**
 * @brief Returns the in-use size of the buffer.
 *
 * @return Number of valid bytes (header + fields) currently in the buffer.
 */
int32
KMessage::ContentSize() const
{
	return _Header()->size;
}


/**
 * @brief Adds a new, initially empty field to the message.
 *
 * Fails if a field with the same name already exists.
 *
 * @param name        Null-terminated field name; must not be NULL.
 * @param type        Field type code; must not be B_ANY_TYPE.
 * @param elementSize Fixed element size, or -1 for variable-sized elements.
 * @param field       Out (optional): bound to the newly created field.
 * @return B_OK on success, B_BAD_VALUE for bad arguments,
 *         B_NAME_IN_USE if a field with this name already exists, or an
 *         allocation error.
 */
status_t
KMessage::AddField(const char* name, type_code type, int32 elementSize,
	KMessageField* field)
{
	if (!name || type == B_ANY_TYPE)
		return B_BAD_VALUE;
	KMessageField existingField;
	if (FindField(name, &existingField) == B_OK)
		return B_NAME_IN_USE;
	return _AddField(name, type, elementSize, field);
}


/**
 * @brief Finds a field by name, ignoring its type.
 *
 * @param name  Field name to match.
 * @param field Out (optional): bound to the field when found.
 * @return B_OK if found, or B_NAME_NOT_FOUND.
 */
status_t
KMessage::FindField(const char* name, KMessageField* field) const
{
	return FindField(name, B_ANY_TYPE, field);
}


/**
 * @brief Finds a field matching a name and (optional) type.
 *
 * Performs a linear scan over all fields. Pass B_ANY_TYPE to match any
 * type.
 *
 * @param name  Field name to match; must not be NULL.
 * @param type  Required type code, or B_ANY_TYPE to match any type.
 * @param field Out (optional): bound to the field when found. When NULL
 *              an internal stack-local field is used for iteration.
 * @return B_OK on success, B_BAD_VALUE if @a name is NULL, or
 *         B_NAME_NOT_FOUND.
 */
status_t
KMessage::FindField(const char* name, type_code type,
	KMessageField* field) const
{
	if (!name)
		return B_BAD_VALUE;
	KMessageField stackField;
	if (field)
		field->Unset();
	else
		field = &stackField;
	while (GetNextField(field) == B_OK) {
		if ((type == B_ANY_TYPE || field->TypeCode() == type)
			&& strcmp(name, field->Name()) == 0) {
			return B_OK;
		}
	}
	return B_NAME_NOT_FOUND;
}


/**
 * @brief Advances a KMessageField cursor to the next field in the message.
 *
 * If the supplied field is unbound, positions it at the first field of
 * this message; otherwise moves to the subsequent field in buffer order.
 *
 * @param field In/Out: cursor to advance; may not be NULL and must either
 *              be unbound or already belong to this message.
 * @return B_OK on successful advance, B_BAD_VALUE for a malformed cursor,
 *         or B_NAME_NOT_FOUND when there are no further fields.
 */
status_t
KMessage::GetNextField(KMessageField* field) const
{
	if (!field || (field->Message() != NULL && field->Message() != this))
		return B_BAD_VALUE;
	FieldHeader* fieldHeader = field->_Header();
	FieldHeader* lastField = _LastFieldHeader();
	if (!lastField)
		return B_NAME_NOT_FOUND;
	if (fieldHeader == NULL) {
		fieldHeader = _FirstFieldHeader();
	} else {
		if ((uint8*)fieldHeader < (uint8*)_FirstFieldHeader()
			|| (uint8*)fieldHeader > (uint8*)lastField) {
			return B_BAD_VALUE;
		}
		if (fieldHeader == lastField)
			return B_NAME_NOT_FOUND;
		fieldHeader = fieldHeader->NextFieldHeader();
	}
	field->SetTo(const_cast<KMessage*>(this), _BufferOffsetFor(fieldHeader));
	return B_OK;
}


/**
 * @brief Reports whether the message contains no fields.
 *
 * @return true when no field has been added; false otherwise.
 */
bool
KMessage::IsEmpty() const
{
	return _LastFieldHeader() == NULL;
}


/**
 * @brief Appends a single data element to a field, creating it if needed.
 *
 * If the field does not yet exist it is created with the specified type
 * and fixed-/variable-size classification. If it exists its type must
 * match.
 *
 * @param name        Field name.
 * @param type        Field type code (not B_ANY_TYPE).
 * @param data        Pointer to the element bytes.
 * @param numBytes    Element size in bytes.
 * @param isFixedSize When creating the field, whether the type uses a
 *                    fixed element size.
 * @return B_OK on success; B_BAD_VALUE for illegal args; B_BAD_TYPE if
 *         the existing field's type does not match; or an allocation
 *         error.
 */
status_t
KMessage::AddData(const char* name, type_code type, const void* data,
	int32 numBytes, bool isFixedSize)
{
	if (!name || type == B_ANY_TYPE || !data || numBytes < 0)
		return B_BAD_VALUE;
	KMessageField field;
	if (FindField(name, &field) == B_OK) {
		// field with that name already exists: check its type
		if (field.TypeCode() != type)
			return B_BAD_TYPE;
	} else {
		// no such field yet: add it
		status_t error = _AddField(name, type, (isFixedSize ? numBytes : -1),
			&field);
		if (error != B_OK)
			return error;
	}
	return _AddFieldData(&field, data, numBytes, 1);
}


/**
 * @brief Appends a contiguous array of fixed-size elements to a field.
 *
 * Creates the field on first use with the supplied element size; if the
 * field already exists its type must match.
 *
 * @param name         Field name.
 * @param type         Field type code (not B_ANY_TYPE).
 * @param data         Pointer to @a elementCount elements of @a elementSize
 *                     bytes each.
 * @param elementSize  Size in bytes of one element; must be >= 0.
 * @param elementCount Number of elements to append; must be >= 0.
 * @return B_OK on success, B_BAD_VALUE for illegal args, B_BAD_TYPE on a
 *         type mismatch with an existing field, or an allocation error.
 */
status_t
KMessage::AddArray(const char* name, type_code type, const void* data,
	int32 elementSize, int32 elementCount)
{
	if (!name || type == B_ANY_TYPE || !data || elementSize < 0
		|| elementCount < 0) {
		return B_BAD_VALUE;
	}
	KMessageField field;
	if (FindField(name, &field) == B_OK) {
		// field with that name already exists: check its type
		if (field.TypeCode() != type)
			return B_BAD_TYPE;
	} else {
		// no such field yet: add it
		status_t error = _AddField(name, type, elementSize, &field);
		if (error != B_OK)
			return error;
	}
	return _AddFieldData(&field, data, elementSize, elementCount);
}


/**
 * @brief Sets the single-element value of a fixed-size field, replacing
 *        an existing element in place.
 *
 * Requires the buffer to be writable. If the field does not yet exist it
 * is created; if it exists, its type and element size must match @a type
 * and @a numBytes. A single element is overwritten in place; if empty,
 * one is appended.
 *
 * @param name     Field name.
 * @param type     Field type code (not B_ANY_TYPE).
 * @param data     Pointer to @a numBytes of element data.
 * @param numBytes Element size in bytes.
 * @return B_OK on success; B_NOT_ALLOWED on read-only buffers;
 *         B_BAD_VALUE on type/size mismatch; or an allocation error.
 */
status_t
KMessage::SetData(const char* name, type_code type, const void* data,
	int32 numBytes)
{
	if (fBuffer != &fHeader && (fFlags & KMESSAGE_READ_ONLY))
		return B_NOT_ALLOWED;

	KMessageField field;

	if (FindField(name, &field) == B_OK) {
		// field already known
		if (field.TypeCode() != type || !field.HasFixedElementSize()
			|| field.ElementSize() != numBytes) {
			return B_BAD_VALUE;
		}

		// if it has an element, just replace its value
		if (field.CountElements() > 0) {
			const void* element = field.ElementAt(0);
			memcpy(const_cast<void*>(element), data, numBytes);
			return B_OK;
		}
	} else {
		// no such field yet -- add it
		status_t error = _AddField(name, type, numBytes, &field);
		if (error != B_OK)
			return error;
	}

	// we've got an empty field -- add the element
	return _AddFieldData(&field, data, numBytes, 1);
}


/**
 * @brief Finds the first element of a field by name and type.
 *
 * Convenience wrapper around the indexed form with @c index = 0.
 *
 * @param name     Field name.
 * @param type     Required type, or B_ANY_TYPE.
 * @param data     Out: pointer to element bytes within the buffer.
 * @param numBytes Out: element size in bytes.
 * @return B_OK on success, or an error from the indexed form.
 */
status_t
KMessage::FindData(const char* name, type_code type, const void** data,
	int32* numBytes) const
{
	return FindData(name, type, 0, data, numBytes);
}


/**
 * @brief Finds a specific element within a named field.
 *
 * @param name     Field name.
 * @param type     Required type, or B_ANY_TYPE.
 * @param index    Zero-based element index.
 * @param data     Out: pointer into the message buffer for the element.
 * @param numBytes Out: element size in bytes.
 * @return B_OK on success; B_BAD_VALUE for NULL outputs; B_NAME_NOT_FOUND
 *         when the field is missing; B_BAD_INDEX when @a index is out of
 *         range.
 */
status_t
KMessage::FindData(const char* name, type_code type, int32 index,
	const void** data, int32* numBytes) const
{
	if (!name || !data || !numBytes)
		return B_BAD_VALUE;
	KMessageField field;
	status_t error = FindField(name, type, &field);
	if (error != B_OK)
		return error;
	const void* foundData = field.ElementAt(index, numBytes);
	if (!foundData)
		return B_BAD_INDEX;
	if (data)
		*data = foundData;
	return B_OK;
}


/**
 * @brief Returns the team that sent this message.
 *
 * @return Sender team id as recorded in the header.
 */
team_id
KMessage::Sender() const
{
	return _Header()->sender;
}


/**
 * @brief Returns the target token (handler id) of this message.
 *
 * @return Target token as recorded in the header.
 */
int32
KMessage::TargetToken() const
{
	return _Header()->targetToken;
}


/**
 * @brief Returns the port on which a reply is expected.
 *
 * @return Reply port id, or -1 if none.
 */
port_id
KMessage::ReplyPort() const
{
	return _Header()->replyPort;
}


/**
 * @brief Returns the reply token associated with the reply port.
 *
 * @return Reply token as recorded in the header.
 */
int32
KMessage::ReplyToken() const
{
	return _Header()->replyToken;
}


/**
 * @brief Populates the header's delivery metadata.
 *
 * @param targetToken Token identifying the recipient handler.
 * @param replyPort   Port to receive replies, or -1 for none.
 * @param replyToken  Token identifying the reply handler.
 * @param senderTeam  Team id of the sender.
 */
void
KMessage::SetDeliveryInfo(int32 targetToken, port_id replyPort,
	int32 replyToken, team_id senderTeam)
{
	Header* header = _Header();
	header->sender = senderTeam;
	header->targetToken = targetToken;
	header->replyPort = replyPort;
	header->replyToken = replyToken;
	header->sender = senderTeam;
}


#ifndef KMESSAGE_CONTAINER_ONLY


/**
 * @brief Sends the message to a port using caller-supplied reply info.
 *
 * Fills in the delivery header, resolves the sender team if needed, and
 * writes the flat buffer to @a targetPort. With a negative timeout a
 * blocking write_port() is used; otherwise write_port_etc() is called
 * with a relative timeout.
 *
 * @param targetPort  Destination port id.
 * @param targetToken Recipient handler token.
 * @param replyPort   Port to accept replies on, or -1.
 * @param replyToken  Reply handler token.
 * @param timeout     Send timeout in microseconds; negative = block.
 * @param senderTeam  Team id to record as sender; -1 to autofill.
 * @return B_OK on success, or a port or thread-info error code.
 */
status_t
KMessage::SendTo(port_id targetPort, int32 targetToken, port_id replyPort,
	int32 replyToken, bigtime_t timeout, team_id senderTeam)
{
	// get the sender team
	if (senderTeam < 0) {
		thread_info info;
		status_t error = get_thread_info(find_thread(NULL), &info);
		if (error != B_OK)
			return error;

		senderTeam = info.team;
	}

	SetDeliveryInfo(targetToken, replyPort, replyToken, senderTeam);

	// send the message
	if (timeout < 0)
		return write_port(targetPort, 'KMSG', fBuffer, ContentSize());

	return write_port_etc(targetPort, 'KMSG', fBuffer, ContentSize(),
		B_RELATIVE_TIMEOUT, timeout);
}


/**
 * @brief Sends the message and synchronously receives a reply.
 *
 * Creates a private reply port (transferring ownership to the target team
 * if cross-team), sends the message, and blocks up to @a replyTimeout for
 * a reply. An inner PortDeleter guarantees the reply port is cleaned up
 * on all exit paths.
 *
 * @param targetPort      Destination port id.
 * @param targetToken     Recipient handler token.
 * @param reply           If non-NULL, filled with the received reply.
 * @param deliveryTimeout Timeout for the outbound send.
 * @param replyTimeout    Timeout for receiving the reply.
 * @param senderTeam      Team id to record as sender; -1 to autofill.
 * @return B_OK on success, or an error from port creation, sending, or
 *         receiving.
 */
status_t
KMessage::SendTo(port_id targetPort, int32 targetToken, KMessage* reply,
	bigtime_t deliveryTimeout, bigtime_t replyTimeout, team_id senderTeam)
{
	// get the team the target port belongs to
	port_info portInfo;
	status_t error = get_port_info(targetPort, &portInfo);
	if (error != B_OK)
		return error;
	team_id targetTeam = portInfo.team;
	// allocate a reply port, if a reply is desired
	port_id replyPort = -1;
	if (reply) {
		// get our team
		team_id ourTeam = B_SYSTEM_TEAM;
		#ifndef _KERNEL_MODE
			if (targetTeam != B_SYSTEM_TEAM) {
				thread_info threadInfo;
				error = get_thread_info(find_thread(NULL), &threadInfo);
				if (error != B_OK)
					return error;
				ourTeam = threadInfo.team;
			}
		#endif
		// create the port
		replyPort = create_port(1, "KMessage reply port");
		if (replyPort < 0)
			return replyPort;
		// If the target team is not our team and not the kernel team either,
		// we transfer the ownership of the port to it, so we will not block
		if (targetTeam != ourTeam && targetTeam != B_SYSTEM_TEAM)
			set_port_owner(replyPort, targetTeam);
	}
	/**
	 * @brief RAII helper that deletes a reply port on scope exit.
	 */
	struct PortDeleter {
		/**
		 * @brief Constructs a deleter bound to @a port.
		 * @param port Port id to delete on destruction, or -1 for a no-op.
		 */
		PortDeleter(port_id port) : port(port) {}

		/**
		 * @brief Deletes the bound port if its id is valid.
		 */
		~PortDeleter()
		{
			if (port >= 0)
				delete_port(port);
		}

		port_id	port;
	} replyPortDeleter(replyPort);
	// send the message
	error = SendTo(targetPort, targetToken, replyPort, 0,
		deliveryTimeout, senderTeam);
	if (error != B_OK)
		return error;
	// get the reply
	if (reply)
		return reply->ReceiveFrom(replyPort, replyTimeout);
	return B_OK;
}


/**
 * @brief Sends @a message back to the sender of this message.
 *
 * Uses this message's recorded ReplyPort/ReplyToken as the target.
 *
 * @param message     Reply message to deliver.
 * @param replyPort   Port the replier is willing to receive a further
 *                    reply on, or -1.
 * @param replyToken  Token paired with @a replyPort.
 * @param timeout     Send timeout in microseconds; negative = block.
 * @param senderTeam  Team id to record as sender; -1 to autofill.
 * @return B_OK on success, B_BAD_VALUE if @a message is NULL, or a send
 *         error.
 */
status_t
KMessage::SendReply(KMessage* message, port_id replyPort, int32 replyToken,
	bigtime_t timeout, team_id senderTeam)
{
	if (!message)
		return B_BAD_VALUE;
	return message->SendTo(ReplyPort(), ReplyToken(), replyPort, replyToken,
		timeout, senderTeam);
}


/**
 * @brief Sends a reply and synchronously receives a further reply.
 *
 * @param message         Reply message to deliver.
 * @param reply           Out (optional): filled with the further reply.
 * @param deliveryTimeout Timeout for the outbound send.
 * @param replyTimeout    Timeout for receiving the further reply.
 * @param senderTeam      Team id to record as sender; -1 to autofill.
 * @return B_OK on success, B_BAD_VALUE if @a message is NULL, or a send
 *         error.
 */
status_t
KMessage::SendReply(KMessage* message, KMessage* reply,
	bigtime_t deliveryTimeout, bigtime_t replyTimeout, team_id senderTeam)
{
	if (!message)
		return B_BAD_VALUE;
	return message->SendTo(ReplyPort(), ReplyToken(), reply, deliveryTimeout,
		replyTimeout, senderTeam);
}


/**
 * @brief Receives a KMessage from a port into this instance.
 *
 * Queries the port for the pending message's size, allocates an aligned
 * heap buffer, reads the message, and attaches to it with
 * KMESSAGE_OWNS_BUFFER so the buffer is freed on Unset().
 *
 * @param fromPort    Port to read from.
 * @param timeout     Receive timeout in microseconds; negative = block.
 * @param messageInfo Out (optional): receives port_message_info details.
 * @return B_OK on success; B_NO_MEMORY on allocation failure; B_ERROR on
 *         size mismatch; or a port error.
 */
status_t
KMessage::ReceiveFrom(port_id fromPort, bigtime_t timeout,
	port_message_info* messageInfo)
{
	port_message_info _messageInfo;
	if (messageInfo == NULL)
		messageInfo = &_messageInfo;

	// get the port buffer size
	status_t error;
	if (timeout < 0) {
		error = get_port_message_info_etc(fromPort, messageInfo, 0, 0);
	} else {
		error = get_port_message_info_etc(fromPort, messageInfo,
			B_RELATIVE_TIMEOUT, timeout);
	}
	if (error != B_OK)
		return error;

	// allocate a buffer
	uint8* buffer = (uint8*)MEMALIGN(kMessageBufferAlignment,
		messageInfo->size);
	if (!buffer)
		return B_NO_MEMORY;

	// read the message
	int32 what;
	ssize_t realSize = read_port_etc(fromPort, &what, buffer, messageInfo->size,
		B_RELATIVE_TIMEOUT, 0);
	if (realSize < 0) {
		free(buffer);
		return realSize;
	}
	if (messageInfo->size != (size_t)realSize) {
		free(buffer);
		return B_ERROR;
	}

	// init the message
	return SetTo(buffer, messageInfo->size, 0,
		KMESSAGE_OWNS_BUFFER | KMESSAGE_INIT_FROM_BUFFER);
}


#endif	// !KMESSAGE_CONTAINER_ONLY


/**
 * @brief Pretty-prints the message to a caller-provided printf-like sink.
 *
 * Iterates every field, formatting common primitive types specially
 * (bool, int8..int64, strings) and otherwise showing a hex pointer and
 * byte count.
 *
 * @param printFunc Printf-style output function used for each line.
 */
void
KMessage::Dump(void (*printFunc)(const char*, ...)) const
{
	Header* header = _Header();
	printFunc("KMessage: buffer: %p (size/capacity: %ld/%ld), flags: %#"
		B_PRIx32 "\n", fBuffer, header->size, fBufferCapacity, fFlags);

	KMessageField field;
	while (GetNextField(&field) == B_OK) {
		type_code type = field.TypeCode();
		uint32 bigEndianType = B_HOST_TO_BENDIAN_INT32(type);
		int nameSpacing = 17 - strlen(field.Name());
		if (nameSpacing < 0)
			nameSpacing = 0;

		printFunc("  field: \"%s\"%*s (%.4s): ", field.Name(), nameSpacing, "",
			(char*)&bigEndianType);

		if (field.CountElements() != 1)
			printFunc("\n");

		int32 size;
		for (int i = 0; const void* data = field.ElementAt(i, &size); i++) {
			if (field.CountElements() != 1)
				printFunc("    [%2d] ", i);

			bool isIntType = false;
			int64 intData = 0;
			switch (type) {
				case B_BOOL_TYPE:
					printFunc("%s\n", (*(bool*)data ? "true" : "false"));
					break;
				case B_INT8_TYPE:
					isIntType = true;
					intData = *(int8*)data;
					break;
				case B_INT16_TYPE:
					isIntType = true;
					intData = *(int16*)data;
					break;
				case B_INT32_TYPE:
					isIntType = true;
					intData = *(int32*)data;
					break;
				case B_INT64_TYPE:
					isIntType = true;
					intData = *(int64*)data;
					break;
				case B_STRING_TYPE:
					printFunc("\"%s\"\n", (char*)data);
					break;
				default:
					printFunc("data at %p, %ld bytes\n", (char*)data, size);
					break;
			}
			if (isIntType)
				printFunc("%lld (0x%llx)\n", intData, intData);
		}
	}
}


/**
 * @brief Returns the buffer interpreted as its Header.
 *
 * @return Pointer to the Header at the start of @c fBuffer.
 */
KMessage::Header*
KMessage::_Header() const
{
	return (Header*)fBuffer;
}


/**
 * @brief Computes the byte offset of a pointer within the buffer.
 *
 * @param data Pointer that must lie within the current buffer (or NULL).
 * @return Offset in bytes from the buffer start, or -1 when @a data is
 *         NULL.
 */
int32
KMessage::_BufferOffsetFor(const void* data) const
{
	if (!data)
		return -1;
	return (uint8*)data - (uint8*)fBuffer;
}


/**
 * @brief Returns the address of the first field header.
 *
 * @return Aligned pointer just past the fixed Header.
 */
KMessage::FieldHeader*
KMessage::_FirstFieldHeader() const
{
	return (FieldHeader*)_Align(fBuffer, sizeof(Header));
}


/**
 * @brief Returns the address of the most recently added field header.
 *
 * @return Pointer to the last field, or NULL when the message is empty.
 */
KMessage::FieldHeader*
KMessage::_LastFieldHeader() const
{
	return _FieldHeaderForOffset(fLastFieldOffset);
}


/**
 * @brief Resolves a field header by its byte offset.
 *
 * @param offset Offset within the buffer; must lie after the Header and
 *               inside the valid content size.
 * @return Field header pointer, or NULL when @a offset is out of range.
 */
KMessage::FieldHeader*
KMessage::_FieldHeaderForOffset(int32 offset) const
{
	if (offset <= 0 || offset >= _Header()->size)
		return NULL;
	return (FieldHeader*)((uint8*)fBuffer + offset);
}


/**
 * @brief Allocates and initialises a new, empty field header in the buffer.
 *
 * Reserves space for a FieldHeader plus the field name, then fills in the
 * type, element-size classification, and size fields, and records the
 * buffer offset in @c fLastFieldOffset.
 *
 * @param name        Null-terminated field name.
 * @param type        Type code stored in the header.
 * @param elementSize Element size; >= 0 for fixed, -1 for variable.
 * @param field       Out (optional): bound to the newly created field.
 * @return B_OK on success, or an error from _AllocateSpace().
 */
status_t
KMessage::_AddField(const char* name, type_code type, int32 elementSize,
	KMessageField* field)
{
	FieldHeader* fieldHeader;
	int32 alignedSize;
	status_t error = _AllocateSpace(sizeof(FieldHeader) + strlen(name), true,
		true, (void**)&fieldHeader, &alignedSize);
	if (error != B_OK)
		return error;
	fieldHeader->type = type;
	fieldHeader->elementSize = elementSize;
	fieldHeader->elementCount = 0;
	fieldHeader->fieldSize = alignedSize;
	fieldHeader->headerSize = alignedSize;
	strcpy(fieldHeader->name, name);
	fLastFieldOffset = _BufferOffsetFor(fieldHeader);
	if (field)
		field->SetTo(this, _BufferOffsetFor(fieldHeader));
	return B_OK;
}


/**
 * @brief Appends element data to the most recently added field.
 *
 * Only the last field can be extended (further fields would overlap new
 * data). Fixed-size fields are appended in bulk; variable-size fields
 * append one FieldValueHeader + value per element. @c field->_Header()
 * is re-resolved after each allocation in case the buffer was relocated.
 *
 * @param field        Target field; must be bound and be the last field.
 * @param data         Pointer to the element(s).
 * @param elementSize  Per-element size in bytes.
 * @param elementCount Number of elements to append.
 * @return B_OK on success, B_BAD_VALUE on illegal args / non-last field,
 *         or an allocation error.
 */
status_t
KMessage::_AddFieldData(KMessageField* field, const void* data,
	int32 elementSize, int32 elementCount)
{
	if (!field)
		return B_BAD_VALUE;
	FieldHeader* fieldHeader = field->_Header();
	FieldHeader* lastField = _LastFieldHeader();
	if (!fieldHeader || fieldHeader != lastField || !data
		|| elementSize < 0 || elementCount < 0) {
		return B_BAD_VALUE;
	}
	if (elementCount == 0)
		return B_OK;
	// fixed size values
	if (fieldHeader->HasFixedElementSize()) {
		if (elementSize != fieldHeader->elementSize)
			return B_BAD_VALUE;
		void* address;
		int32 alignedSize;
		status_t error = _AllocateSpace(elementSize * elementCount,
			(fieldHeader->elementCount == 0), false, &address, &alignedSize);
		if (error != B_OK)
			return error;
		fieldHeader = field->_Header();	// might have been relocated
		memcpy(address, data, elementSize * elementCount);
		fieldHeader->elementCount += elementCount;
		fieldHeader->fieldSize = (uint8*)address + alignedSize
			- (uint8*)fieldHeader;
		return B_OK;
	}
	// non-fixed size values
	// add the elements individually (TODO: Optimize!)
	int32 valueHeaderSize = _Align(sizeof(FieldValueHeader));
	int32 entrySize = valueHeaderSize + elementSize;
	for (int32 i = 0; i < elementCount; i++) {
		void* address;
		int32 alignedSize;
		status_t error = _AllocateSpace(entrySize, true, false, &address,
			&alignedSize);
		if (error != B_OK)
			return error;
		fieldHeader = field->_Header();	// might have been relocated
		FieldValueHeader* valueHeader = (FieldValueHeader*)address;
		valueHeader->size = elementSize;
		memcpy(valueHeader->Data(), (const uint8*)data + i * elementSize,
			elementSize);
		fieldHeader->elementCount++;
		fieldHeader->fieldSize = (uint8*)address + alignedSize
			- (uint8*)fieldHeader;
	}
	return B_OK;
}


/**
 * @brief Validates and binds an already-formatted buffer.
 *
 * Optionally clones the buffer if requested or if the original is
 * misaligned, then checks the magic and size fields and walks the full
 * chain of FieldHeader/FieldValueHeader structures to ensure every
 * offset, size, and name length is within bounds. Updates
 * @c fLastFieldOffset to the last valid field seen.
 *
 * @param sizeFromBuffer When true, the authoritative buffer size is
 *                       taken from the embedded Header::size rather than
 *                       from @c fBufferCapacity.
 * @return B_OK on a valid buffer; B_BAD_DATA for corruption; B_NO_MEMORY
 *         when cloning fails.
 */
status_t
KMessage::_InitFromBuffer(bool sizeFromBuffer)
{
	if (fBuffer == NULL)
		return B_BAD_DATA;

	// clone the buffer, if requested
	if ((fFlags & KMESSAGE_CLONE_BUFFER) != 0 || _Align(fBuffer) != fBuffer) {
		if (sizeFromBuffer) {
			int32 size = fBufferCapacity;
			memcpy(&size, &_Header()->size, 4);
			fBufferCapacity = size;
		}

		void* buffer = MEMALIGN(kMessageBufferAlignment, fBufferCapacity);
		if (buffer == NULL)
			return B_NO_MEMORY;

		memcpy(buffer, fBuffer, fBufferCapacity);

		if ((fFlags & KMESSAGE_OWNS_BUFFER) != 0)
			free(fBuffer);

		fBuffer = buffer;
		fFlags &= ~(uint32)(KMESSAGE_READ_ONLY | KMESSAGE_CLONE_BUFFER);
		fFlags |= KMESSAGE_OWNS_BUFFER;
	}

	if (_Align(fBuffer) != fBuffer)
		return B_BAD_DATA;

	Header* header = _Header();

	if (sizeFromBuffer)
		fBufferCapacity = header->size;

	if (fBufferCapacity < (int)sizeof(Header))
		return B_BAD_DATA;

	// check header
	if (header->magic != kMessageHeaderMagic)
		return B_BAD_DATA;
	if (header->size < (int)sizeof(Header) || header->size > fBufferCapacity)
		return B_BAD_DATA;

	// check the fields
	FieldHeader* fieldHeader = NULL;
	uint8* data = (uint8*)_FirstFieldHeader();
	int32 remainingBytes = (uint8*)fBuffer + header->size - data;
	while (remainingBytes > 0) {
		if (remainingBytes < (int)sizeof(FieldHeader))
			return B_BAD_DATA;
		fieldHeader = (FieldHeader*)data;
		// check field header
		if (fieldHeader->type == B_ANY_TYPE)
			return B_BAD_DATA;
		if (fieldHeader->elementCount < 0)
			return B_BAD_DATA;
		if (fieldHeader->fieldSize < (int)sizeof(FieldHeader)
			|| fieldHeader->fieldSize > remainingBytes) {
			return B_BAD_DATA;
		}
		if (fieldHeader->headerSize < (int)sizeof(FieldHeader)
			|| fieldHeader->headerSize > fieldHeader->fieldSize) {
			return B_BAD_DATA;
		}
		int32 maxNameLen = data + fieldHeader->headerSize
			- (uint8*)fieldHeader->name;
		int32 nameLen = strnlen(fieldHeader->name, maxNameLen);
		if (nameLen == maxNameLen || nameLen == 0)
			return B_BAD_DATA;
		int32 fieldSize =  fieldHeader->headerSize;
		if (fieldHeader->HasFixedElementSize()) {
			// fixed element size
			int32 dataSize = fieldHeader->elementSize
				* fieldHeader->elementCount;
			fieldSize = (uint8*)fieldHeader->Data() + dataSize - data;
		} else {
			// non-fixed element size
			FieldValueHeader* valueHeader
				= (FieldValueHeader*)fieldHeader->Data();
			for (int32 i = 0; i < fieldHeader->elementCount; i++) {
				remainingBytes = (uint8*)fBuffer + header->size
					- (uint8*)valueHeader;
				if (remainingBytes < (int)sizeof(FieldValueHeader))
					return B_BAD_DATA;
				uint8* value = (uint8*)valueHeader->Data();
				remainingBytes = (uint8*)fBuffer + header->size - (uint8*)value;
				if (remainingBytes < valueHeader->size)
					return B_BAD_DATA;
				fieldSize = value + valueHeader->size - data;
				valueHeader = valueHeader->NextFieldValueHeader();
			}
			if (fieldSize > fieldHeader->fieldSize)
				return B_BAD_DATA;
		}
		data = (uint8*)fieldHeader->NextFieldHeader();
		remainingBytes = (uint8*)fBuffer + header->size - data;
	}
	fLastFieldOffset = _BufferOffsetFor(fieldHeader);
	return B_OK;
}


/**
 * @brief Writes a fresh Header into the current buffer.
 *
 * Stamps magic and size, clears all delivery info (sender / tokens /
 * reply port), and resets @c fLastFieldOffset to mark an empty message.
 *
 * @param what Initial @c what code stored in the header.
 */
void
KMessage::_InitBuffer(uint32 what)
{
	Header* header = _Header();
	header->magic = kMessageHeaderMagic;
	header->size = sizeof(Header);
	header->what = what;
	header->sender = -1;
	header->targetToken = -1;
	header->replyPort = -1;
	header->replyToken = -1;
	fLastFieldOffset = 0;
}


/**
 * @brief Debug self-check: re-parses the buffer and panics on mismatch.
 *
 * Verifies that parsing the buffer produces the same @c fLastFieldOffset
 * that was cached, catching silent corruption during development.
 */
void
KMessage::_CheckBuffer()
{
	int32 lastFieldOffset = fLastFieldOffset;
	if (_InitFromBuffer(false) != B_OK) {
		PANIC("internal data mangled");
	}
	if (fLastFieldOffset != lastFieldOffset) {
		PANIC("fLastFieldOffset changed during KMessage::_CheckBuffer()");
	}
}


/**
 * @brief Reserves @a size bytes at the tail of the buffer, growing as
 *        needed.
 *
 * Lazily promotes the embedded fHeader-backed buffer to a heap buffer on
 * first use, then extends via realloc() when more capacity is required.
 * Read-only buffers are rejected. Supports optional alignment of both
 * the returned address and the consumed region.
 *
 * @param size         Minimum number of bytes needed.
 * @param alignAddress When true, the returned address is aligned.
 * @param alignSize    When true, the reserved region is aligned.
 * @param address      Out: pointer to the reserved region.
 * @param alignedSize  Out: actual (possibly aligned) number of bytes
 *                     consumed.
 * @return B_OK on success, B_NOT_ALLOWED on read-only buffers,
 *         B_BUFFER_OVERFLOW on non-owned buffers with insufficient room,
 *         or B_NO_MEMORY on allocation failure.
 */
status_t
KMessage::_AllocateSpace(int32 size, bool alignAddress, bool alignSize,
	void** address, int32* alignedSize)
{
	if (fBuffer != &fHeader && (fFlags & KMESSAGE_READ_ONLY))
		return B_NOT_ALLOWED;

	int32 offset = ContentSize();
	if (alignAddress)
		offset = _Align(offset);
	int32 newSize = offset + size;
	if (alignSize)
		newSize = _Align(newSize);
	// reallocate if necessary
	if (fBuffer == &fHeader) {
		int32 newCapacity = _CapacityFor(newSize);
		void* newBuffer = MEMALIGN(kMessageBufferAlignment, newCapacity);
		if (!newBuffer)
			return B_NO_MEMORY;
		fBuffer = newBuffer;
		fBufferCapacity = newCapacity;
		fFlags |= KMESSAGE_OWNS_BUFFER;
		memcpy(fBuffer, &fHeader, sizeof(fHeader));
	} else {
		if (newSize > fBufferCapacity) {
			// if we don't own the buffer, we can't resize it
			if (!(fFlags & KMESSAGE_OWNS_BUFFER)) {
#if defined(_KERNEL_MODE) && 0
				// optional debugging to find insufficiently sized KMessage
				// buffers (e.g. for in-kernel notifications)
				panic("KMessage: out of space: available: %" B_PRId32
					", needed: %" B_PRId32 "\n", fBufferCapacity, newSize);
#endif
				return B_BUFFER_OVERFLOW;
			}

			int32 newCapacity = _CapacityFor(newSize);
			void* newBuffer = realloc(fBuffer, newCapacity);
			if (!newBuffer)
				return B_NO_MEMORY;
			fBuffer = newBuffer;
			fBufferCapacity = newCapacity;
		}
	}
	_Header()->size = newSize;
	*address = (char*)fBuffer + offset;
	*alignedSize = newSize - offset;
	return B_OK;
}


/**
 * @brief Rounds a requested byte count up to the reallocation chunk size.
 *
 * @param size Minimum number of bytes the buffer must hold.
 * @return A multiple of @c kMessageReallocChunkSize that is >= @a size.
 */
int32
KMessage::_CapacityFor(int32 size)
{
	return (size + kMessageReallocChunkSize - 1) / kMessageReallocChunkSize
		* kMessageReallocChunkSize;
}


// #pragma mark - KMessageField


/**
 * @brief Constructs an unbound KMessageField.
 *
 * The field is not associated with any message until SetTo() is called.
 */
KMessageField::KMessageField()
	:
	fMessage(NULL),
	fHeaderOffset(0)
{
}


/**
 * @brief Detaches the field from any message.
 */
void
KMessageField::Unset()
{
	fMessage = NULL;
	fHeaderOffset = 0;
}


/**
 * @brief Returns the message this field is bound to.
 *
 * @return Owning KMessage pointer, or NULL if unbound.
 */
KMessage*
KMessageField::Message() const
{
	return fMessage;
}


/**
 * @brief Returns the field's name.
 *
 * @return Null-terminated name pointer inside the buffer, or NULL if
 *         unbound or invalid.
 */
const char*
KMessageField::Name() const
{
	KMessage::FieldHeader* header = _Header();
	return header ? header->name : NULL;
}


/**
 * @brief Returns the field's type code.
 *
 * @return Type code from the field header, or 0 if unbound.
 */
type_code
KMessageField::TypeCode() const
{
	KMessage::FieldHeader* header = _Header();
	return header ? header->type : 0;
}


/**
 * @brief Reports whether the bound field uses fixed-size elements.
 *
 * @return true if fixed-size, false otherwise or when unbound.
 */
bool
KMessageField::HasFixedElementSize() const
{
	KMessage::FieldHeader* header = _Header();
	return header ? header->HasFixedElementSize() : false;
}


/**
 * @brief Returns the fixed element size.
 *
 * @return Element size in bytes; -1 for variable-size fields or when
 *         unbound.
 */
int32
KMessageField::ElementSize() const
{
	KMessage::FieldHeader* header = _Header();
	return header ? header->elementSize : -1;
}


/**
 * @brief Appends a single element to this field.
 *
 * @param data Pointer to the element bytes.
 * @param size Element size; -1 to use the field's fixed ElementSize().
 * @return B_OK on success; B_BAD_VALUE for illegal args; or an error
 *         from the underlying KMessage::_AddFieldData().
 */
status_t
KMessageField::AddElement(const void* data, int32 size)
{
	KMessage::FieldHeader* header = _Header();
	if (!header || !data)
		return B_BAD_VALUE;
	if (size < 0) {
		size = ElementSize();
		if (size < 0)
			return B_BAD_VALUE;
	}
	return fMessage->_AddFieldData(this, data, size, 1);
}


/**
 * @brief Appends multiple contiguous elements to this field.
 *
 * @param data        Pointer to @a count elements of @a elementSize bytes.
 * @param count       Number of elements to append; must be >= 0.
 * @param elementSize Element size in bytes; -1 to use ElementSize().
 * @return B_OK on success, B_BAD_VALUE on illegal args, or an underlying
 *         append error.
 */
status_t
KMessageField::AddElements(const void* data, int32 count, int32 elementSize)
{
	KMessage::FieldHeader* header = _Header();
	if (!header || !data || count < 0)
		return B_BAD_VALUE;
	if (elementSize < 0) {
		elementSize = ElementSize();
		if (elementSize < 0)
			return B_BAD_VALUE;
	}
	return fMessage->_AddFieldData(this, data, elementSize, count);
}


/**
 * @brief Returns a pointer to a specific element of this field.
 *
 * @param index Zero-based element index.
 * @param size  Out: element size in bytes when found.
 * @return Pointer to the element data, or NULL when unbound or out of
 *         range.
 */
const void*
KMessageField::ElementAt(int32 index, int32* size) const
{
	KMessage::FieldHeader* header = _Header();
	return header ? header->ElementAt(index, size) : NULL;
}


/**
 * @brief Returns the number of elements stored in this field.
 *
 * @return Element count, or 0 when unbound.
 */
int32
KMessageField::CountElements() const
{
	KMessage::FieldHeader* header = _Header();
	return header ? header->elementCount : 0;
}


/**
 * @brief Binds this field to a message at a given header offset.
 *
 * @param message      Owning KMessage.
 * @param headerOffset Byte offset within the message buffer to the
 *                     target FieldHeader.
 */
void
KMessageField::SetTo(KMessage* message, int32 headerOffset)
{
	fMessage = message;
	fHeaderOffset = headerOffset;
}


/**
 * @brief Resolves the underlying FieldHeader for this bound field.
 *
 * @return Pointer to the FieldHeader, or NULL when unbound or when the
 *         cached offset is now invalid.
 */
KMessage::FieldHeader*
KMessageField::_Header() const
{
	return fMessage ? fMessage->_FieldHeaderForOffset(fHeaderOffset) : NULL;
}
