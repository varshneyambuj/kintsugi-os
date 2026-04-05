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
 *   Copyright 2005-2017 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Lotz, mmlr@mlotz.ch
 */
#ifndef _MESSAGE_H
#define _MESSAGE_H

/**
 * @file Message.h
 * @brief Defines the BMessage class -- the fundamental unit of inter-object
 *        and inter-application communication.
 */


#include <new>

#include <BeBuild.h>
#include <DataIO.h>
#include <Entry.h>
#include <Flattenable.h>
#include <OS.h>
#include <Node.h>
#include <Rect.h>
#include <Size.h>

#include <AppDefs.h>		/* For convenience */
#include <TypeConstants.h>	/* For convenience */


class BAlignment;
class BBlockCache;
class BMessenger;
class BHandler;
class BString;
class BStringList;
struct entry_ref;
struct rgb_color;


/** Maximum number of bytes allowed in a message field name. */
#define B_FIELD_NAME_LENGTH			255

/** Maximum number of bytes allowed in a scripting property name. */
#define B_PROPERTY_NAME_LENGTH		255

/**
 * @brief Scripting specifier constants.
 *
 * These constants identify the kind of specifier attached to a scripting
 * message.  They are used by AddSpecifier() and GetCurrentSpecifier().
 *
 * | Constant                     | Meaning                                  |
 * |------------------------------|------------------------------------------|
 * | B_NO_SPECIFIER               | No specifier present.                    |
 * | B_DIRECT_SPECIFIER           | Target the object itself.                |
 * | B_INDEX_SPECIFIER            | Target by zero-based index.              |
 * | B_REVERSE_INDEX_SPECIFIER    | Target by index from the end.            |
 * | B_RANGE_SPECIFIER            | Target a range of indices.               |
 * | B_REVERSE_RANGE_SPECIFIER    | Target a range counted from the end.     |
 * | B_NAME_SPECIFIER             | Target by name.                          |
 * | B_ID_SPECIFIER               | Target by unique identifier.             |
 *
 * Application-defined specifiers should start at @c B_SPECIFIERS_END + 1.
 */
enum {
	B_NO_SPECIFIER = 0,
	B_DIRECT_SPECIFIER = 1,
	B_INDEX_SPECIFIER,
	B_REVERSE_INDEX_SPECIFIER,
	B_RANGE_SPECIFIER,
	B_REVERSE_RANGE_SPECIFIER,
	B_NAME_SPECIFIER,
	B_ID_SPECIFIER,

	B_SPECIFIERS_END = 128
	// app-defined specifiers start at B_SPECIFIERS_END + 1
};


/**
 * @brief A typed, key-value container that serves as the basic unit of
 *        communication between threads, handlers, and applications.
 *
 * BMessage acts as both a data container and a delivery envelope.  Each
 * message carries a public @c what code that identifies the message type
 * and an arbitrary number of named, typed data fields.  Fields are added
 * with the Add*() family of methods and retrieved with Find*(), Get*(),
 * Has*(), Replace*(), and Set*() counterparts.
 *
 * Messages are delivered to BHandler objects via BLooper message queues
 * or sent between applications with BMessenger.  They can also be
 * serialized with Flatten() / Unflatten() for storage.
 *
 * A single field name may hold an array of values of the same type.
 * The @a index parameter in the Find/Replace overloads selects a
 * particular element from such an array.
 *
 * @note BMessage instances are reference-counted internally when copied;
 *       the actual data is duplicated lazily on write.
 *
 * @see BMessenger, BHandler, BLooper, BMessageQueue
 */
class BMessage {
public:
	/**
	 * @brief The message type code.
	 *
	 * This public member identifies the purpose of the message.  System
	 * messages use the constants defined in AppDefs.h (e.g. B_QUIT_REQUESTED);
	 * applications may define their own codes.
	 */
			uint32				what;

	/** @name Construction and destruction */
	/** @{ */

	/**
	 * @brief Creates an empty message with @c what set to 0.
	 */
								BMessage();

	/**
	 * @brief Creates a message with the specified type code.
	 *
	 * @param what The message type code.
	 */
								BMessage(uint32 what);

	/**
	 * @brief Copy constructor -- performs a shallow, reference-counted copy.
	 *
	 * Data is duplicated lazily when either copy is modified.
	 *
	 * @param other The message to copy.
	 */
								BMessage(const BMessage& other);

	/**
	 * @brief Destructor.
	 */
	virtual						~BMessage();

	/**
	 * @brief Copy-assignment operator.
	 *
	 * @param other The message to copy from.
	 * @return A reference to this message.
	 */
			BMessage&			operator=(const BMessage& other);

	/** @} */

	/** @name Statistics and metadata */
	/** @{ */

	/**
	 * @brief Retrieves information about a field by type and index.
	 *
	 * @param typeRequested The type_code to filter by, or @c B_ANY_TYPE.
	 * @param index         Zero-based index among fields of the requested
	 *                      type.
	 * @param[out] nameFound   Receives a pointer to the field name.
	 * @param[out] typeFound   Receives the field's type code.
	 * @param[out] countFound  Optional -- receives the number of data items
	 *                         stored under the field.
	 * @return B_OK on success, or an error code if the index is out of range.
	 */
			status_t			GetInfo(type_code typeRequested, int32 index,
									char** nameFound, type_code* typeFound,
									int32* countFound = NULL) const;

	/**
	 * @brief Retrieves a field's type and item count by name.
	 *
	 * @param name            The field name to look up.
	 * @param[out] typeFound  Receives the field's type code.
	 * @param[out] countFound Optional -- receives the number of items.
	 * @return B_OK on success, or B_NAME_NOT_FOUND.
	 */
			status_t			GetInfo(const char* name, type_code* typeFound,
									int32* countFound = NULL) const;

	/**
	 * @brief Retrieves a field's type and whether its items are fixed-size.
	 *
	 * @param name             The field name.
	 * @param[out] typeFound   Receives the type code.
	 * @param[out] fixedSize   Receives @c true if items are fixed-size.
	 * @return B_OK on success, or B_NAME_NOT_FOUND.
	 */
			status_t			GetInfo(const char* name, type_code* typeFound,
									bool* fixedSize) const;

	/**
	 * @brief Retrieves a field's type, item count, and fixed-size flag.
	 *
	 * @param name              The field name.
	 * @param[out] typeFound    Receives the type code.
	 * @param[out] countFound   Receives the number of items.
	 * @param[out] fixedSize    Receives @c true if items are fixed-size.
	 * @return B_OK on success, or B_NAME_NOT_FOUND.
	 */
			status_t			GetInfo(const char* name, type_code* typeFound,
									int32* countFound, bool* fixedSize) const;

	/**
	 * @brief Counts the number of fields whose type matches @a type.
	 *
	 * @param type The type_code to match, or @c B_ANY_TYPE for all fields.
	 * @return The number of matching fields.
	 */
			int32				CountNames(type_code type) const;

	/**
	 * @brief Tests whether the message contains any data fields.
	 *
	 * @return @c true if the message has no data fields.
	 */
			bool				IsEmpty() const;

	/**
	 * @brief Tests whether this is a system-defined message.
	 *
	 * @return @c true if the @c what code falls in the system-reserved range.
	 */
			bool				IsSystem() const;

	/**
	 * @brief Tests whether this message is a reply to a previous message.
	 *
	 * @return @c true if the message was sent via SendReply().
	 */
			bool				IsReply() const;

	/**
	 * @brief Prints the message contents to standard output for debugging.
	 */
			void				PrintToStream() const;

	/**
	 * @brief Renames a data field.
	 *
	 * @param oldEntry The current field name.
	 * @param newEntry The desired new name.
	 * @return B_OK on success, or B_NAME_NOT_FOUND.
	 */
			status_t			Rename(const char* oldEntry,
									const char* newEntry);

	/** @} */

	/** @name Delivery information */
	/** @{ */

	/**
	 * @brief Tests whether the message has been delivered to a target.
	 *
	 * @return @c true if the message was sent via BMessenger or posting.
	 */
			bool				WasDelivered() const;

	/**
	 * @brief Tests whether the sender is waiting for a reply.
	 *
	 * @return @c true if the sender called a synchronous SendMessage()
	 *         variant and is blocked waiting for a reply.
	 */
			bool				IsSourceWaiting() const;

	/**
	 * @brief Tests whether the sender is in a different application.
	 *
	 * @return @c true if the message originated from a different team.
	 */
			bool				IsSourceRemote() const;

	/**
	 * @brief Returns a messenger targeting the sender of this message.
	 *
	 * This can be used to send additional messages back to the sender.
	 *
	 * @return A BMessenger addressing the sender.
	 */
			BMessenger			ReturnAddress() const;

	/**
	 * @brief Returns the message that this message is a reply to.
	 *
	 * @return A pointer to the original message, or NULL.
	 */
			const BMessage*		Previous() const;

	/**
	 * @brief Tests whether the message resulted from a drag-and-drop
	 *        operation.
	 *
	 * @return @c true if the message was dropped onto a view.
	 */
			bool				WasDropped() const;

	/**
	 * @brief Returns the screen location where a drag message was dropped.
	 *
	 * @param[out] offset Optional pointer to receive the offset within the
	 *                    dragged bitmap or rect.
	 * @return The drop point in screen coordinates.
	 */
			BPoint				DropPoint(BPoint* offset = NULL) const;

	/** @} */

	/** @name Replying */
	/** @{ */

	/**
	 * @brief Sends a simple reply identified by a command code.
	 *
	 * @param command The @c what code for the reply message.
	 * @param replyTo Optional handler to receive a reply to the reply.
	 * @return B_OK on success, or an error code.
	 */
			status_t			SendReply(uint32 command,
									BHandler* replyTo = NULL);

	/**
	 * @brief Sends a BMessage as a reply with a handler for further replies.
	 *
	 * @param reply   The reply message to send.
	 * @param replyTo Handler to receive further replies, or NULL.
	 * @param timeout Maximum microseconds to wait for delivery.
	 * @return B_OK on success, or an error code.
	 */
			status_t			SendReply(BMessage* reply,
									BHandler* replyTo = NULL,
									bigtime_t timeout = B_INFINITE_TIMEOUT);

	/**
	 * @brief Sends a BMessage as a reply with a messenger for further
	 *        replies.
	 *
	 * @param reply   The reply message to send.
	 * @param replyTo A BMessenger for further reply delivery.
	 * @param timeout Maximum microseconds to wait for delivery.
	 * @return B_OK on success, or an error code.
	 */
			status_t			SendReply(BMessage* reply, BMessenger replyTo,
									bigtime_t timeout = B_INFINITE_TIMEOUT);

	/**
	 * @brief Sends a command-code reply and waits synchronously for a
	 *        reply to that reply.
	 *
	 * @param command       The @c what code for the reply.
	 * @param replyToReply  Pointer to a BMessage that receives the
	 *                      subsequent reply.
	 * @return B_OK on success, or an error code.
	 */
			status_t			SendReply(uint32 command,
									BMessage* replyToReply);

	/**
	 * @brief Sends a reply and waits synchronously for a reply to that
	 *        reply.
	 *
	 * @param reply        The reply message to send.
	 * @param replyToReply Pointer to a BMessage that receives the
	 *                     subsequent reply.
	 * @param sendTimeout  Maximum microseconds for delivery.
	 * @param replyTimeout Maximum microseconds to wait for the subsequent
	 *                     reply.
	 * @return B_OK on success, or an error code.
	 */
			status_t			SendReply(BMessage* reply,
									BMessage* replyToReply,
									bigtime_t sendTimeout = B_INFINITE_TIMEOUT,
									bigtime_t replyTimeout
										= B_INFINITE_TIMEOUT);

	/** @} */

	/** @name Flattening (serialization) */
	/** @{ */

	/**
	 * @brief Returns the number of bytes needed to flatten this message.
	 *
	 * @return The flattened size in bytes.
	 */
			ssize_t				FlattenedSize() const;

	/**
	 * @brief Flattens the message into a caller-supplied buffer.
	 *
	 * @param buffer Pointer to the destination buffer.
	 * @param size   Size of @a buffer in bytes.
	 * @return B_OK on success, B_NO_MEMORY if the buffer is too small.
	 */
			status_t			Flatten(char* buffer, ssize_t size) const;

	/**
	 * @brief Flattens the message to a BDataIO stream.
	 *
	 * @param stream The destination stream.
	 * @param[out] size Optional -- receives the number of bytes written.
	 * @return B_OK on success, or an error code.
	 */
			status_t			Flatten(BDataIO* stream,
									ssize_t* size = NULL) const;

	/**
	 * @brief Unflattens a message from a raw buffer.
	 *
	 * Replaces the current message contents.
	 *
	 * @param flatBuffer Pointer to a previously flattened message.
	 * @return B_OK on success, B_BAD_VALUE if the buffer is invalid.
	 */
			status_t			Unflatten(const char* flatBuffer);

	/**
	 * @brief Unflattens a message from a BDataIO stream.
	 *
	 * @param stream The source stream.
	 * @return B_OK on success, or an error code.
	 */
			status_t			Unflatten(BDataIO* stream);

	/** @} */

	/** @name Scripting specifiers */
	/** @{ */

	/**
	 * @brief Pushes a B_DIRECT_SPECIFIER for the given property.
	 *
	 * @param property The property name.
	 * @return B_OK on success.
	 */
			status_t			AddSpecifier(const char* property);

	/**
	 * @brief Pushes a B_INDEX_SPECIFIER for the given property and index.
	 *
	 * @param property The property name.
	 * @param index    The zero-based index.
	 * @return B_OK on success.
	 */
			status_t			AddSpecifier(const char* property, int32 index);

	/**
	 * @brief Pushes a B_RANGE_SPECIFIER for a property, index, and range.
	 *
	 * @param property The property name.
	 * @param index    The starting index.
	 * @param range    The number of items in the range.
	 * @return B_OK on success.
	 */
			status_t			AddSpecifier(const char* property, int32 index,
									int32 range);

	/**
	 * @brief Pushes a B_NAME_SPECIFIER for a property identified by name.
	 *
	 * @param property The property name.
	 * @param name     The name of the target element.
	 * @return B_OK on success.
	 */
			status_t			AddSpecifier(const char* property,
									const char* name);

	/**
	 * @brief Pushes an already-constructed specifier message.
	 *
	 * @param specifier The specifier message to push.
	 * @return B_OK on success.
	 */
			status_t			AddSpecifier(const BMessage* specifier);

	/**
	 * @brief Sets the current specifier index for scripting traversal.
	 *
	 * @param index The specifier index to make current.
	 * @return B_OK on success.
	 */
			status_t			SetCurrentSpecifier(int32 index);

	/**
	 * @brief Retrieves the current scripting specifier.
	 *
	 * @param[out] index     Receives the current specifier index.
	 * @param[out] specifier Optional -- receives a copy of the specifier
	 *                       message.
	 * @param[out] what      Optional -- receives the specifier's @c what
	 *                       code.
	 * @param[out] property  Optional -- receives the property name.
	 * @return B_OK on success, or an error code.
	 */
			status_t			GetCurrentSpecifier(int32* index,
									BMessage* specifier = NULL,
									int32* what = NULL,
									const char** property = NULL) const;

	/**
	 * @brief Tests whether the message has any scripting specifiers.
	 *
	 * @return @c true if at least one specifier is present.
	 */
			bool				HasSpecifiers() const;

	/**
	 * @brief Pops the current specifier from the specifier stack.
	 *
	 * @return B_OK on success.
	 */
			status_t			PopSpecifier();

	/** @} */

	/** @name Adding data
	 *
	 * Each Add method appends a new data item to the named field. If
	 * the field does not yet exist it is created.  Multiple values of
	 * the same type may be stored under one name, forming an array
	 * that is accessed by index in the Find/Replace methods.
	 *
	 * Representative methods are documented below.  Similar overloads
	 * exist for every supported type (int8 through uint64, float,
	 * double, bool, rgb_color, BPoint, BRect, BSize, BAlignment,
	 * BString, entry_ref, node_ref, BMessenger, BMessage, and
	 * BFlattenable).
	 */
	/** @{ */

	/**
	 * @brief Adds a BAlignment value.
	 *
	 * @param name      The field name.
	 * @param alignment The value to add.
	 * @return B_OK on success, or an error code.
	 */
			status_t			AddAlignment(const char* name,
									const BAlignment& alignment);

	/**
	 * @brief Adds a BRect value.
	 *
	 * @param name The field name.
	 * @param rect The rectangle to add.
	 * @return B_OK on success.
	 */
			status_t			AddRect(const char* name, BRect rect);

	/**
	 * @brief Adds a BPoint value.
	 *
	 * @param name  The field name.
	 * @param point The point to add.
	 * @return B_OK on success.
	 */
			status_t			AddPoint(const char* name, BPoint point);

	/**
	 * @brief Adds a BSize value.
	 *
	 * @param name The field name.
	 * @param size The size to add.
	 * @return B_OK on success.
	 */
			status_t			AddSize(const char* name, BSize size);

	/**
	 * @brief Adds a C string value.
	 *
	 * @param name   The field name.
	 * @param string The null-terminated string to add.
	 * @return B_OK on success.
	 */
			status_t			AddString(const char* name, const char* string);

	/**
	 * @brief Adds a BString value.
	 *
	 * @param name   The field name.
	 * @param string The BString to add.
	 * @return B_OK on success.
	 */
			status_t			AddString(const char* name,
									const BString& string);

	/**
	 * @brief Adds all strings from a BStringList.
	 *
	 * @param name The field name.
	 * @param list The list of strings to add.
	 * @return B_OK on success.
	 */
			status_t			AddStrings(const char* name,
									const BStringList& list);

	/**
	 * @brief Adds an int8 value.
	 *
	 * @param name  The field name.
	 * @param value The value to add.
	 * @return B_OK on success.
	 */
			status_t			AddInt8(const char* name, int8 value);

	/** @brief Adds a uint8 value. @param name Field name. @param value Value to add. @return B_OK on success. */
			status_t			AddUInt8(const char* name, uint8 value);
	/** @brief Adds an int16 value. @param name Field name. @param value Value to add. @return B_OK on success. */
			status_t			AddInt16(const char* name, int16 value);
	/** @brief Adds a uint16 value. @param name Field name. @param value Value to add. @return B_OK on success. */
			status_t			AddUInt16(const char* name, uint16 value);
	/** @brief Adds an int32 value. @param name Field name. @param value Value to add. @return B_OK on success. */
			status_t			AddInt32(const char* name, int32 value);
	/** @brief Adds a uint32 value. @param name Field name. @param value Value to add. @return B_OK on success. */
			status_t			AddUInt32(const char* name, uint32 value);
	/** @brief Adds an int64 value. @param name Field name. @param value Value to add. @return B_OK on success. */
			status_t			AddInt64(const char* name, int64 value);
	/** @brief Adds a uint64 value. @param name Field name. @param value Value to add. @return B_OK on success. */
			status_t			AddUInt64(const char* name, uint64 value);
	/** @brief Adds a bool value. @param name Field name. @param value Value to add. @return B_OK on success. */
			status_t			AddBool(const char* name, bool value);
	/** @brief Adds a float value. @param name Field name. @param value Value to add. @return B_OK on success. */
			status_t			AddFloat(const char* name, float value);
	/** @brief Adds a double value. @param name Field name. @param value Value to add. @return B_OK on success. */
			status_t			AddDouble(const char* name, double value);
	/** @brief Adds an rgb_color value. @param name Field name. @param value Value to add. @return B_OK on success. */
			status_t			AddColor(const char* name, rgb_color value);

	/**
	 * @brief Adds a raw pointer value.
	 *
	 * @note Pointers are not valid across application boundaries.
	 *
	 * @param name    The field name.
	 * @param pointer The pointer to store.
	 * @return B_OK on success.
	 */
			status_t			AddPointer(const char* name,
									const void* pointer);

	/** @brief Adds a BMessenger. @param name Field name. @param messenger The messenger to add. @return B_OK on success. */
			status_t			AddMessenger(const char* name,
									BMessenger messenger);
	/** @brief Adds an entry_ref. @param name Field name. @param ref Pointer to the entry_ref. @return B_OK on success. */
			status_t			AddRef(const char* name, const entry_ref* ref);
	/** @brief Adds a node_ref. @param name Field name. @param ref Pointer to the node_ref. @return B_OK on success. */
			status_t			AddNodeRef(const char* name,
									const node_ref* ref);
	/** @brief Adds a nested BMessage. @param name Field name. @param message Pointer to the BMessage. @return B_OK on success. */
			status_t			AddMessage(const char* name,
									const BMessage* message);

	/**
	 * @brief Adds a BFlattenable object.
	 *
	 * The object is flattened into the message's internal buffer.
	 *
	 * @param name   The field name.
	 * @param object The flattenable object to add.
	 * @param count  Hint for the number of items that will be added under
	 *               this name.
	 * @return B_OK on success.
	 */
			status_t			AddFlat(const char* name, BFlattenable* object,
									int32 count = 1);

	/** @brief Adds a const BFlattenable object. @see AddFlat(const char*, BFlattenable*, int32) */
			status_t			AddFlat(const char* name,
									const BFlattenable* object, int32 count = 1);

	/**
	 * @brief Adds raw data of an arbitrary type.
	 *
	 * This is the most general Add method and is used internally by
	 * all type-specific variants.
	 *
	 * @param name        The field name.
	 * @param type        The type_code describing the data.
	 * @param data        Pointer to the data to copy into the message.
	 * @param numBytes    Size of the data in bytes.
	 * @param isFixedSize @c true if every item under this name has the
	 *                    same size.
	 * @param count       Hint for the expected number of items.
	 * @return B_OK on success, or B_NO_MEMORY.
	 */
			status_t			AddData(const char* name, type_code type,
									const void* data, ssize_t numBytes,
									bool isFixedSize = true, int32 count = 1);

	/**
	 * @brief Appends all fields from another message into this one.
	 *
	 * @param message The source message whose fields are appended.
	 * @return B_OK on success.
	 */
			status_t			Append(const BMessage& message);

	/** @} */

	/** @name Removing data */
	/** @{ */

	/**
	 * @brief Removes a single item from a named field.
	 *
	 * @param name  The field name.
	 * @param index The zero-based item index to remove (default 0).
	 * @return B_OK on success, or B_NAME_NOT_FOUND.
	 */
			status_t			RemoveData(const char* name, int32 index = 0);

	/**
	 * @brief Removes an entire named field and all its items.
	 *
	 * @param name The field name to remove.
	 * @return B_OK on success, or B_NAME_NOT_FOUND.
	 */
			status_t			RemoveName(const char* name);

	/**
	 * @brief Removes all data fields from the message.
	 *
	 * The @c what code is not changed.
	 *
	 * @return B_OK on success.
	 */
			status_t			MakeEmpty();

	/** @} */

	/** @name Finding data
	 *
	 * Each Find method retrieves a value by field name and optional index.
	 * The first set of overloads writes the result through an output
	 * pointer and returns a status_t.  Convenience overloads that return
	 * the value directly (with a default on failure) are listed under
	 * "Legacy direct-return overloads" and "Convenience getters (Get*)".
	 *
	 * Representative methods are fully documented.  Analogous overloads
	 * exist for every supported type.
	 */
	/** @{ */

	/**
	 * @brief Finds a BAlignment value.
	 *
	 * @param name           The field name.
	 * @param[out] alignment Receives the value.
	 * @return B_OK on success, or B_NAME_NOT_FOUND.
	 */
			status_t			FindAlignment(const char* name,
									BAlignment* alignment) const;

	/**
	 * @brief Finds a BAlignment value at a specific index.
	 *
	 * @param name           The field name.
	 * @param index          Zero-based item index.
	 * @param[out] alignment Receives the value.
	 * @return B_OK on success.
	 */
			status_t			FindAlignment(const char* name, int32 index,
									BAlignment* alignment) const;

	/**
	 * @brief Finds a BRect value.
	 *
	 * @param name       The field name.
	 * @param[out] rect  Receives the value.
	 * @return B_OK on success.
	 */
			status_t			FindRect(const char* name, BRect* rect) const;
	/** @brief Finds a BRect at an index. @see FindRect(const char*, BRect*) const */
			status_t			FindRect(const char* name, int32 index,
									BRect* rect) const;

	/**
	 * @brief Finds a BPoint value.
	 *
	 * @param name        The field name.
	 * @param[out] point  Receives the value.
	 * @return B_OK on success.
	 */
			status_t			FindPoint(const char* name,
									BPoint* point) const;
	/** @brief Finds a BPoint at an index. @see FindPoint(const char*, BPoint*) const */
			status_t			FindPoint(const char* name, int32 index,
									BPoint* point) const;

	/** @brief Finds a BSize value. @param name Field name. @param[out] size Receives the value. @return B_OK on success. */
			status_t			FindSize(const char* name, BSize* size) const;
	/** @brief Finds a BSize at an index. @see FindSize(const char*, BSize*) const */
			status_t			FindSize(const char* name, int32 index,
									BSize* size) const;

	/**
	 * @brief Finds a string value as a C string pointer.
	 *
	 * @param name          The field name.
	 * @param[out] string   Receives a pointer to the internal string data.
	 *                      The pointer is valid until the message is
	 *                      modified or destroyed.
	 * @return B_OK on success.
	 */
			status_t			FindString(const char* name,
									const char** string) const;
	/** @brief Finds a string at an index as a C string pointer. */
			status_t			FindString(const char* name, int32 index,
									const char** string) const;
	/** @brief Finds a string value as a BString. @param name Field name. @param[out] string Receives the value. @return B_OK on success. */
			status_t			FindString(const char* name,
									BString* string) const;
	/** @brief Finds a string at an index as a BString. */
			status_t			FindString(const char* name, int32 index,
									BString* string) const;
	/** @brief Finds all strings under a name as a BStringList. @param name Field name. @param[out] list Receives the values. @return B_OK on success. */
			status_t			FindStrings(const char* name,
									BStringList* list) const;

	/** @brief Finds an int8 value. @param name Field name. @param[out] value Receives the value. @return B_OK on success. */
			status_t			FindInt8(const char* name, int8* value) const;
	/** @brief Finds an int8 at an index. */
			status_t			FindInt8(const char* name, int32 index,
									int8* value) const;
	/** @brief Finds a uint8 value. @param name Field name. @param[out] value Receives the value. @return B_OK on success. */
			status_t			FindUInt8(const char* name, uint8* value) const;
	/** @brief Finds a uint8 at an index. */
			status_t			FindUInt8(const char* name, int32 index,
									uint8* value) const;
	/** @brief Finds an int16 value. @param name Field name. @param[out] value Receives the value. @return B_OK on success. */
			status_t			FindInt16(const char* name, int16* value) const;
	/** @brief Finds an int16 at an index. */
			status_t			FindInt16(const char* name, int32 index,
									int16* value) const;
	/** @brief Finds a uint16 value. @param name Field name. @param[out] value Receives the value. @return B_OK on success. */
			status_t			FindUInt16(const char* name,
									uint16* value) const;
	/** @brief Finds a uint16 at an index. */
			status_t			FindUInt16(const char* name, int32 index,
									uint16* value) const;
	/** @brief Finds an int32 value. @param name Field name. @param[out] value Receives the value. @return B_OK on success. */
			status_t			FindInt32(const char* name, int32* value) const;
	/** @brief Finds an int32 at an index. */
			status_t			FindInt32(const char* name, int32 index,
									int32* value) const;
	/** @brief Finds a uint32 value. @param name Field name. @param[out] value Receives the value. @return B_OK on success. */
			status_t			FindUInt32(const char* name,
									uint32* value) const;
	/** @brief Finds a uint32 at an index. */
			status_t			FindUInt32(const char* name, int32 index,
									uint32* value) const;
	/** @brief Finds an int64 value. @param name Field name. @param[out] value Receives the value. @return B_OK on success. */
			status_t			FindInt64(const char* name, int64* value) const;
	/** @brief Finds an int64 at an index. */
			status_t			FindInt64(const char* name, int32 index,
									int64* value) const;
	/** @brief Finds a uint64 value. @param name Field name. @param[out] value Receives the value. @return B_OK on success. */
			status_t			FindUInt64(const char* name,
									uint64* value) const;
	/** @brief Finds a uint64 at an index. */
			status_t			FindUInt64(const char* name, int32 index,
									uint64* value) const;
	/** @brief Finds a bool value. @param name Field name. @param[out] value Receives the value. @return B_OK on success. */
			status_t			FindBool(const char* name, bool* value) const;
	/** @brief Finds a bool at an index. */
			status_t			FindBool(const char* name, int32 index,
									bool* value) const;
	/** @brief Finds a float value. @param name Field name. @param[out] value Receives the value. @return B_OK on success. */
			status_t			FindFloat(const char* name, float* value) const;
	/** @brief Finds a float at an index. */
			status_t			FindFloat(const char* name, int32 index,
									float* value) const;
	/** @brief Finds a double value. @param name Field name. @param[out] value Receives the value. @return B_OK on success. */
			status_t			FindDouble(const char* name,
									double* value) const;
	/** @brief Finds a double at an index. */
			status_t			FindDouble(const char* name, int32 index,
									double* value) const;
	/** @brief Finds an rgb_color value. @param name Field name. @param[out] value Receives the value. @return B_OK on success. */
			status_t			FindColor(const char* name,
									rgb_color* value) const;
	/** @brief Finds an rgb_color at an index. */
			status_t			FindColor(const char* name, int32 index,
									rgb_color* value) const;
	/** @brief Finds a pointer value. @param name Field name. @param[out] pointer Receives the pointer. @return B_OK on success. */
			status_t			FindPointer(const char* name,
									void** pointer) const;
	/** @brief Finds a pointer at an index. */
			status_t			FindPointer(const char* name, int32 index,
									void** pointer) const;
	/** @brief Finds a BMessenger value. @param name Field name. @param[out] messenger Receives the value. @return B_OK on success. */
			status_t			FindMessenger(const char* name,
									BMessenger* messenger) const;
	/** @brief Finds a BMessenger at an index. */
			status_t			FindMessenger(const char* name, int32 index,
									BMessenger* messenger) const;
	/** @brief Finds an entry_ref. @param name Field name. @param[out] ref Receives the value. @return B_OK on success. */
			status_t			FindRef(const char* name, entry_ref* ref) const;
	/** @brief Finds an entry_ref at an index. */
			status_t			FindRef(const char* name, int32 index,
									entry_ref* ref) const;
	/** @brief Finds a node_ref. @param name Field name. @param[out] ref Receives the value. @return B_OK on success. */
			status_t			FindNodeRef(const char* name,
									node_ref* ref) const;
	/** @brief Finds a node_ref at an index. */
			status_t			FindNodeRef(const char* name, int32 index,
									node_ref* ref) const;
	/** @brief Finds a nested BMessage. @param name Field name. @param[out] message Receives the value. @return B_OK on success. */
			status_t			FindMessage(const char* name,
									BMessage* message) const;
	/** @brief Finds a nested BMessage at an index. */
			status_t			FindMessage(const char* name, int32 index,
									BMessage* message) const;
	/** @brief Finds and unflattens a BFlattenable object. @param name Field name. @param[out] object Receives the unflattened object. @return B_OK on success. */
			status_t			FindFlat(const char* name,
									BFlattenable* object) const;
	/** @brief Finds and unflattens a BFlattenable at an index. */
			status_t			FindFlat(const char* name, int32 index,
									BFlattenable* object) const;

	/**
	 * @brief Finds raw data by name and type.
	 *
	 * @param name          The field name.
	 * @param type          Expected type_code (use B_ANY_TYPE to skip check).
	 * @param[out] data     Receives a pointer to the internal data buffer.
	 * @param[out] numBytes Receives the size of the data in bytes.
	 * @return B_OK on success, B_NAME_NOT_FOUND, or B_BAD_TYPE.
	 */
			status_t			FindData(const char* name, type_code type,
									const void** data, ssize_t* numBytes) const;

	/**
	 * @brief Finds raw data at a specific index.
	 *
	 * @param name          The field name.
	 * @param type          Expected type_code.
	 * @param index         Zero-based item index.
	 * @param[out] data     Receives a pointer to the internal data buffer.
	 * @param[out] numBytes Receives the size in bytes.
	 * @return B_OK on success.
	 */
			status_t			FindData(const char* name, type_code type,
									int32 index, const void** data,
									ssize_t* numBytes) const;

	/** @} */

	/** @name Replacing data
	 *
	 * Each Replace method overwrites an existing item in a named field.
	 * The field must already exist and contain an item at the given index.
	 * Representative methods are fully documented; analogous overloads
	 * exist for every supported type.
	 */
	/** @{ */

	/**
	 * @brief Replaces a BAlignment value.
	 *
	 * @param name      The field name.
	 * @param alignment The new value.
	 * @return B_OK on success, or B_NAME_NOT_FOUND.
	 */
			status_t			ReplaceAlignment(const char* name,
									const BAlignment& alignment);

	/**
	 * @brief Replaces a BAlignment value at a specific index.
	 *
	 * @param name      The field name.
	 * @param index     Zero-based item index.
	 * @param alignment The new value.
	 * @return B_OK on success.
	 */
			status_t			ReplaceAlignment(const char* name, int32 index,
									const BAlignment& alignment);

	/** @brief Replaces a BRect value. @param name Field name. @param rect New value. @return B_OK on success. */
			status_t			ReplaceRect(const char* name, BRect rect);
	/** @brief Replaces a BRect at an index. */
			status_t			ReplaceRect(const char* name, int32 index,
									BRect rect);

	/** @brief Replaces a BPoint value. @param name Field name. @param aPoint New value. @return B_OK on success. */
			status_t			ReplacePoint(const char* name, BPoint aPoint);
	/** @brief Replaces a BPoint at an index. */
			status_t			ReplacePoint(const char* name, int32 index,
									BPoint aPoint);
	/** @brief Replaces a BSize value. @param name Field name. @param aSize New value. @return B_OK on success. */
			status_t			ReplaceSize(const char* name, BSize aSize);
	/** @brief Replaces a BSize at an index. */
			status_t			ReplaceSize(const char* name, int32 index,
									BSize aSize);

	/** @brief Replaces a string (C string). @param name Field name. @param string New value. @return B_OK on success. */
			status_t			ReplaceString(const char* name,
									const char* string);
	/** @brief Replaces a string at an index (C string). */
			status_t			ReplaceString(const char* name, int32 index,
									const char* string);
	/** @brief Replaces a string (BString). @param name Field name. @param string New value. @return B_OK on success. */
			status_t			ReplaceString(const char* name,
									const BString& string);
	/** @brief Replaces a string at an index (BString). */
			status_t			ReplaceString(const char* name, int32 index,
									const BString& string);
	/** @brief Replaces an int8. @param name Field name. @param value New value. @return B_OK on success. */
			status_t			ReplaceInt8(const char* name, int8 value);
	/** @brief Replaces an int8 at an index. */
			status_t			ReplaceInt8(const char* name, int32 index,
									int8 value);
	/** @brief Replaces a uint8. @param name Field name. @param value New value. @return B_OK on success. */
			status_t			ReplaceUInt8(const char* name, uint8 value);
	/** @brief Replaces a uint8 at an index. */
			status_t			ReplaceUInt8(const char* name, int32 index,
									uint8 value);
	/** @brief Replaces an int16. @param name Field name. @param value New value. @return B_OK on success. */
			status_t			ReplaceInt16(const char* name, int16 value);
	/** @brief Replaces an int16 at an index. */
			status_t			ReplaceInt16(const char* name, int32 index,
									int16 value);
	/** @brief Replaces a uint16. @param name Field name. @param value New value. @return B_OK on success. */
			status_t			ReplaceUInt16(const char* name, uint16 value);
	/** @brief Replaces a uint16 at an index. */
			status_t			ReplaceUInt16(const char* name, int32 index,
									uint16 value);
	/** @brief Replaces an int32. @param name Field name. @param value New value. @return B_OK on success. */
			status_t			ReplaceInt32(const char* name, int32 value);
	/** @brief Replaces an int32 at an index. */
			status_t			ReplaceInt32(const char* name, int32 index,
									int32 value);
	/** @brief Replaces a uint32. @param name Field name. @param value New value. @return B_OK on success. */
			status_t			ReplaceUInt32(const char* name, uint32 value);
	/** @brief Replaces a uint32 at an index. */
			status_t			ReplaceUInt32(const char* name, int32 index,
									uint32 value);
	/** @brief Replaces an int64. @param name Field name. @param value New value. @return B_OK on success. */
			status_t			ReplaceInt64(const char* name, int64 value);
	/** @brief Replaces an int64 at an index. */
			status_t			ReplaceInt64(const char* name, int32 index,
									int64 value);
	/** @brief Replaces a uint64. @param name Field name. @param value New value. @return B_OK on success. */
			status_t			ReplaceUInt64(const char* name, uint64 value);
	/** @brief Replaces a uint64 at an index. */
			status_t			ReplaceUInt64(const char* name, int32 index,
									uint64 value);
	/** @brief Replaces a bool. @param name Field name. @param aBoolean New value. @return B_OK on success. */
			status_t			ReplaceBool(const char* name, bool aBoolean);
	/** @brief Replaces a bool at an index. */
			status_t			ReplaceBool(const char* name, int32 index,
									bool value);
	/** @brief Replaces a float. @param name Field name. @param value New value. @return B_OK on success. */
			status_t			ReplaceFloat(const char* name, float value);
	/** @brief Replaces a float at an index. */
			status_t			ReplaceFloat(const char* name, int32 index,
									float value);
	/** @brief Replaces a double. @param name Field name. @param value New value. @return B_OK on success. */
			status_t			ReplaceDouble(const char* name, double value);
	/** @brief Replaces a double at an index. */
			status_t			ReplaceDouble(const char* name, int32 index,
									double value);
	/** @brief Replaces an rgb_color. @param name Field name. @param value New value. @return B_OK on success. */
			status_t			ReplaceColor(const char* name,
									rgb_color value);
	/** @brief Replaces an rgb_color at an index. */
			status_t			ReplaceColor(const char* name, int32 index,
									rgb_color value);
	/** @brief Replaces a pointer. @param name Field name. @param pointer New pointer. @return B_OK on success. */
			status_t			ReplacePointer(const char* name,
									const void* pointer);
	/** @brief Replaces a pointer at an index. */
			status_t			ReplacePointer(const char* name, int32 index,
									const void* pointer);
	/** @brief Replaces a BMessenger. @param name Field name. @param messenger New value. @return B_OK on success. */
			status_t			ReplaceMessenger(const char* name,
									BMessenger messenger);
	/** @brief Replaces a BMessenger at an index. */
			status_t			ReplaceMessenger(const char* name, int32 index,
									BMessenger messenger);
	/** @brief Replaces an entry_ref. @param name Field name. @param ref Pointer to the new value. @return B_OK on success. */
			status_t			ReplaceRef(const char* name,
									const entry_ref* ref);
	/** @brief Replaces an entry_ref at an index. */
			status_t			ReplaceRef(const char* name, int32 index,
									const entry_ref* ref);
	/** @brief Replaces a node_ref. @param name Field name. @param ref Pointer to the new value. @return B_OK on success. */
			status_t			ReplaceNodeRef(const char* name,
									const node_ref* ref);
	/** @brief Replaces a node_ref at an index. */
			status_t			ReplaceNodeRef(const char* name, int32 index,
									const node_ref* ref);
	/** @brief Replaces a nested BMessage. @param name Field name. @param message Pointer to the new message. @return B_OK on success. */
			status_t			ReplaceMessage(const char* name,
									const BMessage* message);
	/** @brief Replaces a nested BMessage at an index. */
			status_t			ReplaceMessage(const char* name, int32 index,
									const BMessage* message);
	/** @brief Replaces a BFlattenable object. @param name Field name. @param object The replacement object. @return B_OK on success. */
			status_t			ReplaceFlat(const char* name,
									BFlattenable* object);
	/** @brief Replaces a BFlattenable at an index. */
			status_t			ReplaceFlat(const char* name, int32 index,
									BFlattenable* object);

	/**
	 * @brief Replaces raw data by name and type.
	 *
	 * @param name     The field name.
	 * @param type     The type_code of the field.
	 * @param data     Pointer to the replacement data.
	 * @param numBytes Size of the replacement data in bytes.
	 * @return B_OK on success.
	 */
			status_t			ReplaceData(const char* name, type_code type,
									const void* data, ssize_t numBytes);

	/**
	 * @brief Replaces raw data at a specific index.
	 *
	 * @param name     The field name.
	 * @param type     The type_code of the field.
	 * @param index    Zero-based item index.
	 * @param data     Pointer to the replacement data.
	 * @param numBytes Size of the replacement data in bytes.
	 * @return B_OK on success.
	 */
			status_t			ReplaceData(const char* name, type_code type,
									int32 index, const void* data,
									ssize_t numBytes);

	/** @} */

	/** @name Comparing data */
	/** @{ */

	/**
	 * @brief Tests whether this message contains the same data as another.
	 *
	 * @note This is a Haiku experimental API.
	 *
	 * @param other            The message to compare against.
	 * @param ignoreFieldOrder If @c true, fields may appear in different
	 *                         order and still be considered equal.
	 * @param deep             If @c true, nested BMessages are compared
	 *                         recursively.
	 * @return @c true if both messages have identical data.
	 */
			bool				HasSameData(const BMessage& other,
									bool ignoreFieldOrder = true,
									bool deep = false) const;

	/** @} */

	/** @name Memory operators */
	/** @{ */
			void*				operator new(size_t size);
			void*				operator new(size_t, void* pointer);
			void*				operator new(size_t,
									const std::nothrow_t& noThrow);
			void				operator delete(void* pointer, size_t size);
	/** @} */

	/** @name Checking for fields (Has*)
	 *
	 * Each Has method returns @c true if the message contains at least
	 * @a n+1 items under the given name.  These are useful for verifying
	 * the presence of expected fields before calling Find.
	 *
	 * Overloads exist for every supported type. @see FindData()
	 */
	/** @{ */
	/** @brief Tests for a BAlignment field. @param name Field name. @param n Zero-based index (default 0). @return @c true if the item exists. */
			bool				HasAlignment(const char* name,
									int32 n = 0) const;
	/** @brief Tests for a BRect field. */
			bool				HasRect(const char* name, int32 n = 0) const;
	/** @brief Tests for a BPoint field. */
			bool				HasPoint(const char* name, int32 n = 0) const;
	/** @brief Tests for a BSize field. */
			bool				HasSize(const char* name, int32 n = 0) const;
	/** @brief Tests for a string field. */
			bool				HasString(const char* name, int32 n = 0) const;
	/** @brief Tests for an int8 field. */
			bool				HasInt8(const char* name, int32 n = 0) const;
	/** @brief Tests for a uint8 field. */
			bool				HasUInt8(const char* name, int32 n = 0) const;
	/** @brief Tests for an int16 field. */
			bool				HasInt16(const char* name, int32 n = 0) const;
	/** @brief Tests for a uint16 field. */
			bool				HasUInt16(const char* name, int32 n = 0) const;
	/** @brief Tests for an int32 field. */
			bool				HasInt32(const char* name, int32 n = 0) const;
	/** @brief Tests for a uint32 field. */
			bool				HasUInt32(const char* name, int32 n = 0) const;
	/** @brief Tests for an int64 field. */
			bool				HasInt64(const char* name, int32 n = 0) const;
	/** @brief Tests for a uint64 field. */
			bool				HasUInt64(const char* name, int32 n = 0) const;
	/** @brief Tests for a bool field. */
			bool				HasBool(const char* name, int32 n = 0) const;
	/** @brief Tests for a float field. */
			bool				HasFloat(const char* name, int32 n = 0) const;
	/** @brief Tests for a double field. */
			bool				HasDouble(const char* name, int32 n = 0) const;
	/** @brief Tests for an rgb_color field. */
			bool				HasColor(const char* name, int32 n = 0) const;
	/** @brief Tests for a pointer field. */
			bool				HasPointer(const char* name, int32 n = 0) const;
	/** @brief Tests for a BMessenger field. */
			bool				HasMessenger(const char* name,
									int32 n = 0) const;
	/** @brief Tests for an entry_ref field. */
			bool				HasRef(const char* name, int32 n = 0) const;
	/** @brief Tests for a node_ref field. */
			bool				HasNodeRef(const char* name, int32 n = 0) const;
	/** @brief Tests for a nested BMessage field. */
			bool				HasMessage(const char* name, int32 n = 0) const;
	/** @brief Tests for a BFlattenable-compatible field. @param name Field name. @param object A prototype used for type comparison. @return @c true if the field exists and is compatible. */
			bool				HasFlat(const char* name,
									const BFlattenable* object) const;
	/** @brief Tests for a BFlattenable-compatible field at an index. */
			bool				HasFlat(const char* name, int32 n,
									const BFlattenable* object) const;
	/** @brief Tests for a raw data field of the given type. @param name Field name. @param type The expected type_code. @param n Zero-based index (default 0). @return @c true if the item exists. */
			bool				HasData(const char* name, type_code ,
									int32 n = 0) const;

	/** @} */

	/** @name Legacy direct-return Find overloads
	 *
	 * These overloads return the value directly instead of through an
	 * output pointer.  They return a default-constructed value on failure.
	 * Prefer the status_t-returning variants for proper error handling.
	 */
	/** @{ */
	/** @brief Returns a BRect, or a default BRect on failure. @param name Field name. @param n Item index. */
			BRect				FindRect(const char* name, int32 n = 0) const;
	/** @brief Returns a BPoint, or a default BPoint on failure. */
			BPoint				FindPoint(const char* name, int32 n = 0) const;
	/** @brief Returns a C string, or NULL on failure. */
			const char*			FindString(const char* name, int32 n = 0) const;
	/** @brief Returns an int8, or 0 on failure. */
			int8				FindInt8(const char* name, int32 n = 0) const;
	/** @brief Returns an int16, or 0 on failure. */
			int16				FindInt16(const char* name, int32 n = 0) const;
	/** @brief Returns an int32, or 0 on failure. */
			int32				FindInt32(const char* name, int32 n = 0) const;
	/** @brief Returns an int64, or 0 on failure. */
			int64				FindInt64(const char* name, int32 n = 0) const;
	/** @brief Returns a bool, or @c false on failure. */
			bool				FindBool(const char* name, int32 n = 0) const;
	/** @brief Returns a float, or 0 on failure. */
			float				FindFloat(const char* name, int32 n = 0) const;
	/** @brief Returns a double, or 0 on failure. */
			double				FindDouble(const char* name, int32 n = 0) const;
	/** @} */

	/** @name Convenience getters (Get*)
	 *
	 * These methods return the value directly and accept a
	 * @a defaultValue that is returned when the field is not found.
	 * They never report an error code.
	 *
	 * Overloads exist for every fixed-size type and for common
	 * variable-size types (string, alignment, rect, point, size).
	 */
	/** @{ */

	/**
	 * @brief Gets a bool, returning @a defaultValue if not found.
	 *
	 * @param name         The field name.
	 * @param defaultValue Value returned on failure (default @c false).
	 * @return The stored value, or @a defaultValue.
	 */
			bool				GetBool(const char* name,
									bool defaultValue = false) const;
	/** @brief Gets a bool at an index. @see GetBool(const char*, bool) const */
			bool				GetBool(const char* name, int32 index,
									bool defaultValue) const;
	/** @brief Gets an int8, returning @a defaultValue if not found. */
			int8				GetInt8(const char* name,
									int8 defaultValue) const;
	/** @brief Gets an int8 at an index. */
			int8				GetInt8(const char* name, int32 index,
									int8 defaultValue) const;
	/** @brief Gets a uint8. */
			uint8				GetUInt8(const char* name,
									uint8 defaultValue) const;
	/** @brief Gets a uint8 at an index. */
			uint8				GetUInt8(const char* name, int32 index,
									uint8 defaultValue) const;
	/** @brief Gets an int16. */
			int16				GetInt16(const char* name,
									int16 defaultValue) const;
	/** @brief Gets an int16 at an index. */
			int16				GetInt16(const char* name, int32 index,
									int16 defaultValue) const;
	/** @brief Gets a uint16. */
			uint16				GetUInt16(const char* name,
									uint16 defaultValue) const;
	/** @brief Gets a uint16 at an index. */
			uint16				GetUInt16(const char* name, int32 index,
									uint16 defaultValue) const;
	/** @brief Gets an int32. */
			int32				GetInt32(const char* name,
									int32 defaultValue) const;
	/** @brief Gets an int32 at an index. */
			int32				GetInt32(const char* name, int32 index,
									int32 defaultValue) const;
	/** @brief Gets a uint32. */
			uint32				GetUInt32(const char* name,
									uint32 defaultValue) const;
	/** @brief Gets a uint32 at an index. */
			uint32				GetUInt32(const char* name, int32 index,
									uint32 defaultValue) const;
	/** @brief Gets an int64. */
			int64				GetInt64(const char* name,
									int64 defaultValue) const;
	/** @brief Gets an int64 at an index. */
			int64				GetInt64(const char* name, int32 index,
									int64 defaultValue) const;
	/** @brief Gets a uint64. */
			uint64				GetUInt64(const char* name,
									uint64 defaultValue) const;
	/** @brief Gets a uint64 at an index. */
			uint64				GetUInt64(const char* name, int32 index,
									uint64 defaultValue) const;
	/** @brief Gets a float. */
			float				GetFloat(const char* name,
									float defaultValue) const;
	/** @brief Gets a float at an index. */
			float				GetFloat(const char* name, int32 index,
									float defaultValue) const;
	/** @brief Gets a double. */
			double				GetDouble(const char* name,
									double defaultValue) const;
	/** @brief Gets a double at an index. */
			double				GetDouble(const char* name, int32 index,
									double defaultValue) const;
	/** @brief Gets an rgb_color. */
			rgb_color			GetColor(const char* name,
									rgb_color defaultValue) const;
	/** @brief Gets an rgb_color at an index. */
			rgb_color			GetColor(const char* name, int32 index,
									rgb_color defaultValue) const;
	/** @brief Gets a pointer at an index. */
			const void*			GetPointer(const char* name, int32 index,
									const void* defaultValue = NULL) const;
	/** @brief Gets a pointer, returning @a defaultValue if not found. */
			const void*			GetPointer(const char* name,
									const void* defaultValue = NULL) const;
	/** @brief Gets a string, returning @a defaultValue if not found. */
			const char*			GetString(const char* name,
									const char* defaultValue = NULL) const;
	/** @brief Gets a string at an index. */
			const char*			GetString(const char* name, int32 index,
									const char* defaultValue) const;
	/** @brief Gets a BAlignment at an index. */
			BAlignment			GetAlignment(const char* name, int32 index,
									const BAlignment& defaultValue) const;
	/** @brief Gets a BAlignment. */
			BAlignment			GetAlignment(const char* name,
									const BAlignment& defaultValue) const;
	/** @brief Gets a BRect at an index. */
			BRect				GetRect(const char* name, int32 index,
									const BRect& defaultValue) const;
	/** @brief Gets a BRect. */
			BRect				GetRect(const char* name,
									const BRect& defaultValue) const;
	/** @brief Gets a BPoint at an index. */
			BPoint				GetPoint(const char* name, int32 index,
									const BPoint& defaultValue) const;
	/** @brief Gets a BPoint. */
			BPoint				GetPoint(const char* name,
									const BPoint& defaultValue) const;
	/** @brief Gets a BSize at an index. */
			BSize				GetSize(const char* name, int32 index,
									const BSize& defaultValue) const;
	/** @brief Gets a BSize. */
			BSize				GetSize(const char* name,
									const BSize& defaultValue) const;

	/** @} */

	/** @name Convenience setters (Set*)
	 *
	 * Each Set method stores a value under the given name, replacing any
	 * existing item at index 0.  If the field does not yet exist it is
	 * created.  These are shorthand for the Add/Replace pattern when
	 * only a single value per name is needed.
	 *
	 * @note Only fixed-size types are supported by Set methods.
	 */
	/** @{ */
	/** @brief Sets a bool value. @param name Field name. @param value The value. @return B_OK on success. */
			status_t			SetBool(const char* name, bool value);
	/** @brief Sets an int8 value. */
			status_t			SetInt8(const char* name, int8 value);
	/** @brief Sets a uint8 value. */
			status_t			SetUInt8(const char* name, uint8 value);
	/** @brief Sets an int16 value. */
			status_t			SetInt16(const char* name, int16 value);
	/** @brief Sets a uint16 value. */
			status_t			SetUInt16(const char* name, uint16 value);
	/** @brief Sets an int32 value. */
			status_t			SetInt32(const char* name, int32 value);
	/** @brief Sets a uint32 value. */
			status_t			SetUInt32(const char* name, uint32 value);
	/** @brief Sets an int64 value. */
			status_t			SetInt64(const char* name, int64 value);
	/** @brief Sets a uint64 value. */
			status_t			SetUInt64(const char* name, uint64 value);
	/** @brief Sets an rgb_color value. */
			status_t			SetColor(const char* name, rgb_color value);
	/** @brief Sets a pointer value. */
			status_t			SetPointer(const char* name, const void* value);
	/** @brief Sets a string value (C string). */
			status_t			SetString(const char* name, const char* string);
	/** @brief Sets a string value (BString). */
			status_t			SetString(const char* name,
									const BString& string);
	/** @brief Sets a float value. */
			status_t			SetFloat(const char* name, float value);
	/** @brief Sets a double value. */
			status_t			SetDouble(const char* name, double value);
	/** @brief Sets a BAlignment value. */
			status_t			SetAlignment(const char* name,
									const BAlignment& value);
	/** @brief Sets a BPoint value. */
			status_t			SetPoint(const char* name, const BPoint& value);
	/** @brief Sets a BRect value. */
			status_t			SetRect(const char* name, const BRect& value);
	/** @brief Sets a BSize value. */
			status_t			SetSize(const char* name, const BSize& value);

	/**
	 * @brief Sets raw data of an arbitrary type.
	 *
	 * @param name      The field name.
	 * @param type      The type_code.
	 * @param data      Pointer to the data.
	 * @param numBytes  Size of the data in bytes.
	 * @param fixedSize @c true if all items will be the same size.
	 * @param count     Hint for expected number of items.
	 * @return B_OK on success.
	 */
			status_t			SetData(const char* name, type_code type,
									const void* data, ssize_t numBytes,
									bool fixedSize = true, int count = 1);

	/** @} */

	class Private;
	struct message_header;
	struct field_header;

private:
	friend class Private;
	friend class BMessageQueue;

			status_t			_InitCommon(bool initHeader);
			status_t			_InitHeader();
			status_t			_Clear();

			status_t			_FlattenToArea(message_header** _header) const;
			status_t			_CopyForWrite();
			status_t			_Reference();
			status_t			_Dereference();

			status_t			_ValidateMessage();

			void				_UpdateOffsets(uint32 offset, int32 change);
			status_t			_ResizeData(uint32 offset, int32 change);

			uint32				_HashName(const char* name) const;
			status_t			_FindField(const char* name, type_code type,
									field_header** _result) const;
			status_t			_AddField(const char* name, type_code type,
									bool isFixedSize, field_header** _result);
			status_t			_RemoveField(field_header* field);

			void				_PrintToStream(const char* indent) const;

private:
								BMessage(BMessage* message);
									// deprecated

	virtual	void				_ReservedMessage1();
	virtual	void				_ReservedMessage2();
	virtual	void				_ReservedMessage3();

			status_t			_SendMessage(port_id port, team_id portOwner,
									int32 token, bigtime_t timeout,
									bool replyRequired,
									BMessenger& replyTo) const;
			status_t			_SendMessage(port_id port, team_id portOwner,
									int32 token, BMessage* reply,
									bigtime_t sendTimeout,
									bigtime_t replyTimeout) const;
	static	status_t			_SendFlattenedMessage(void* data, int32 size,
									port_id port, int32 token,
									bigtime_t timeout);

	static	void				_StaticInit();
	static	void				_StaticReInitForkedChild();
	static	void				_StaticCleanup();
	static	void				_StaticCacheCleanup();
	static	int32				_StaticGetCachedReplyPort();

private:
			message_header*		fHeader;
			field_header*		fFields;
			uint8*				fData;

			uint32				fFieldsAvailable;
			size_t				fDataAvailable;

			mutable	BMessage*	fOriginal;

			BMessage*			fQueueLink;
				// fQueueLink is used by BMessageQueue to build a linked list

			void*				fArchivingPointer;

			uint32				fReserved[8];

			enum				{ sNumReplyPorts = 3 };
	static	port_id				sReplyPorts[sNumReplyPorts];
	static	int32				sReplyPortInUse[sNumReplyPorts];
	static	int32				sGetCachedReplyPort();

	static	BBlockCache*		sMsgCache;
};


#endif	// _MESSAGE_H
