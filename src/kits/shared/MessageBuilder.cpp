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
 *   Copyright 2014, Augustin Cavalier (waddlesplash)
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Augustin Cavalier (waddlesplash)
 */

/** @file MessageBuilder.cpp
 *  @brief BMessageBuilder provides a stack-based API for incrementally
 *         constructing nested BMessage hierarchies without manual bookkeeping
 *         of parent messages.
 */


#include <MessageBuilder.h>

#include <AutoDeleter.h>
#include <String.h>


namespace BPrivate {

// #pragma mark - BMessageBuilder


/** @brief Constructs a BMessageBuilder targeting @p message as the root.
 *
 *  @p message is not owned; the caller is responsible for its lifetime.
 *
 *  @param message  The top-level BMessage to build into.
 */
BMessageBuilder::BMessageBuilder(BMessage& message)
	:
	fNameStack(20),
	fCurrentMessage(&message)
{
}


/*! Creates a new BMessage, makes it a child of the
    current one with "name", and then pushes the current
    Message onto the stack and makes the new Message the
    current one.
*/
/** @brief Pushes a new child BMessage onto the builder stack.
 *
 *  Creates a new BMessage, records the key name under which it will be added
 *  to the parent, pushes the current message onto the internal stack, and
 *  makes the new message the current one. The new message is added to its
 *  parent only when PopObject() is called.
 *
 *  @param name  The field name under which the child will be stored in the parent.
 *  @return B_OK on success, B_NO_MEMORY if any allocation fails.
 */
status_t
BMessageBuilder::PushObject(const char* name)
{
	BMessage* newMessage = new(std::nothrow) BMessage;
	if (newMessage == NULL)
		return B_NO_MEMORY;
	ObjectDeleter<BMessage> messageDeleter(newMessage);

	BString* nameString = new(std::nothrow) BString(name);
	if (nameString == NULL)
		return B_NO_MEMORY;
	ObjectDeleter<BString> stringDeleter(nameString);

	if (!fNameStack.AddItem(nameString))
		return B_NO_MEMORY;
	stringDeleter.Detach();

	if (!fStack.AddItem(fCurrentMessage))
		return B_NO_MEMORY;
	messageDeleter.Detach();

	fCurrentMessage = newMessage;
	return B_OK;
}


/*! Convenience function that converts "name"
	to a string and calls PushObject(const char*)
	with it.
*/
/** @brief Pushes a child BMessage using a uint32 as the field name.
 *
 *  The integer is formatted as a decimal string and PushObject(const char*)
 *  is called.
 *
 *  @param name  Numeric key, formatted as "0", "1", "2", … etc.
 *  @return B_OK on success, B_NO_MEMORY if any allocation fails.
 */
status_t
BMessageBuilder::PushObject(uint32 name)
{
	BString nameString;
	nameString.SetToFormat("%" B_PRIu32, name);
	return PushObject(nameString.String());
}


/*! Pops the last BMessage off the stack and makes it
    the current one.
*/
/** @brief Pops the current BMessage off the stack and attaches it to its parent.
 *
 *  The current message is added to the previous (parent) message under the
 *  name that was recorded at PushObject() time, then the parent becomes the
 *  new current message.
 *
 *  @return B_OK on success, B_ERROR if the stack is empty.
 */
status_t
BMessageBuilder::PopObject()
{
	if (fStack.CountItems() < 1)
		return B_ERROR;

	BMessage* previousMessage = fStack.LastItem();
	previousMessage->AddMessage(fNameStack.LastItem()->String(),
		fCurrentMessage);

	delete fCurrentMessage;
	fCurrentMessage = previousMessage;

	fStack.RemoveItemAt(fStack.CountItems() - 1);
	fNameStack.RemoveItemAt(fNameStack.CountItems() - 1);
	return B_OK;
}


/*! Gets the "what" of the current message.
*/
/** @brief Returns the 'what' code of the current message.
 *  @return The 'what' field of the currently active BMessage.
 */
uint32
BMessageBuilder::What()
{
	return fCurrentMessage->what;
}


/*! Sets the "what" of the current message.
*/
/** @brief Sets the 'what' code of the current message.
 *  @param what  The new 'what' value to assign.
 */
void
BMessageBuilder::SetWhat(uint32 what)
{
	fCurrentMessage->what = what;
}


/*! Gets the value of CountNames() from the current message.
*/
/** @brief Returns the number of field names of the given type in the current message.
 *  @param type  Type code to count, or B_ANY_TYPE for all fields.
 *  @return The number of matching named fields.
 */
uint32
BMessageBuilder::CountNames(type_code type)
{
	return fCurrentMessage->CountNames(type);
}


// #pragma mark - BMessageBuilder::Add (to fCurrentMessage)


/** @brief Adds a C-string field to the current message.
 *  @param name    Field name.
 *  @param string  NUL-terminated string value.
 *  @return B_OK on success, or an error code.
 */
status_t
BMessageBuilder::AddString(const char* name, const char* string)
{
	return fCurrentMessage->AddString(name, string);
}


/** @brief Adds a BString field to the current message.
 *  @param name    Field name.
 *  @param string  String value.
 *  @return B_OK on success, or an error code.
 */
status_t
BMessageBuilder::AddString(const char* name, const BString& string)
{
	return fCurrentMessage->AddString(name, string);
}


/** @brief Adds an int8 field to the current message.
 *  @param name   Field name.
 *  @param value  Value to add.
 *  @return B_OK on success, or an error code.
 */
status_t
BMessageBuilder::AddInt8(const char* name, int8 value)
{
	return fCurrentMessage->AddInt8(name, value);
}


/** @brief Adds a uint8 field to the current message.
 *  @param name   Field name.
 *  @param value  Value to add.
 *  @return B_OK on success, or an error code.
 */
status_t
BMessageBuilder::AddUInt8(const char* name, uint8 value)
{
	return fCurrentMessage->AddUInt8(name, value);
}


/** @brief Adds an int16 field to the current message.
 *  @param name   Field name.
 *  @param value  Value to add.
 *  @return B_OK on success, or an error code.
 */
status_t
BMessageBuilder::AddInt16(const char* name, int16 value)
{
	return fCurrentMessage->AddInt16(name, value);
}


/** @brief Adds a uint16 field to the current message.
 *  @param name   Field name.
 *  @param value  Value to add.
 *  @return B_OK on success, or an error code.
 */
status_t
BMessageBuilder::AddUInt16(const char* name, uint16 value)
{
	return fCurrentMessage->AddUInt16(name, value);
}


/** @brief Adds an int32 field to the current message.
 *  @param name   Field name.
 *  @param value  Value to add.
 *  @return B_OK on success, or an error code.
 */
status_t
BMessageBuilder::AddInt32(const char* name, int32 value)
{
	return fCurrentMessage->AddInt32(name, value);
}


/** @brief Adds a uint32 field to the current message.
 *  @param name   Field name.
 *  @param value  Value to add.
 *  @return B_OK on success, or an error code.
 */
status_t
BMessageBuilder::AddUInt32(const char* name, uint32 value)
{
	return fCurrentMessage->AddUInt32(name, value);
}


/** @brief Adds an int64 field to the current message.
 *  @param name   Field name.
 *  @param value  Value to add.
 *  @return B_OK on success, or an error code.
 */
status_t
BMessageBuilder::AddInt64(const char* name, int64 value)
{
	return fCurrentMessage->AddInt64(name, value);
}


/** @brief Adds a uint64 field to the current message.
 *  @param name   Field name.
 *  @param value  Value to add.
 *  @return B_OK on success, or an error code.
 */
status_t
BMessageBuilder::AddUInt64(const char* name, uint64 value)
{
	return fCurrentMessage->AddUInt64(name, value);
}


/** @brief Adds a boolean field to the current message.
 *  @param name   Field name.
 *  @param value  Value to add.
 *  @return B_OK on success, or an error code.
 */
status_t
BMessageBuilder::AddBool(const char* name, bool value)
{
	return fCurrentMessage->AddBool(name, value);
}


/** @brief Adds a float field to the current message.
 *  @param name   Field name.
 *  @param value  Value to add.
 *  @return B_OK on success, or an error code.
 */
status_t
BMessageBuilder::AddFloat(const char* name, float value)
{
	return fCurrentMessage->AddFloat(name, value);
}


/** @brief Adds a double field to the current message.
 *  @param name   Field name.
 *  @param value  Value to add.
 *  @return B_OK on success, or an error code.
 */
status_t
BMessageBuilder::AddDouble(const char* name, double value)
{
	return fCurrentMessage->AddDouble(name, value);
}


/** @brief Adds a pointer field to the current message.
 *  @param name     Field name.
 *  @param pointer  Pointer value to add.
 *  @return B_OK on success, or an error code.
 */
status_t
BMessageBuilder::AddPointer(const char* name, const void* pointer)
{
	return fCurrentMessage->AddPointer(name, pointer);
}


} // namespace BPrivate
