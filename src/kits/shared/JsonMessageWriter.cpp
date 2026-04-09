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
 *   Copyright 2017, Andrew Lindesay <apl@lindesay.co.nz>
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Andrew Lindesay
 */

/** @file JsonMessageWriter.cpp
 *  @brief BJsonEventListener implementation that builds a BMessage hierarchy
 *         from an incoming stream of JSON parse events.
 */


#include "JsonMessageWriter.h"


namespace BPrivate {

/*! The class and sub-classes of it are used as a stack internal to the
    BJsonMessageWriter class.  As the JSON is parsed, the stack of these
    internal listeners follows the stack of the JSON parsing in terms of
    containers; arrays and objects.
*/

/** @brief Base class for the internal listener stack used by BJsonMessageWriter.
 *
 *  As JSON containers (arrays and objects) are opened, a new subclass instance
 *  is pushed onto the stack; when they close the instance is popped and the
 *  accumulated BMessage is added to the parent.
 */
class BStackedMessageEventListener : public BJsonEventListener {
public:
								BStackedMessageEventListener(
									BJsonMessageWriter* writer,
									BStackedMessageEventListener* parent,
									uint32 messageWhat);
								BStackedMessageEventListener(
									BJsonMessageWriter* writer,
									BStackedMessageEventListener* parent,
									BMessage* message);
								~BStackedMessageEventListener();

				bool			Handle(const BJsonEvent& event);
				void			HandleError(status_t status, int32 line,
									const char* message);
				void			Complete();

				void			AddMessage(BMessage* value);

				status_t		ErrorStatus();
		virtual	const char*		NextItemName() = 0;

				BStackedMessageEventListener*
								Parent();

protected:
				void			AddBool(bool value);
				void			AddNull();
				void			AddDouble(double value);
				void			AddString(const char* value);

		virtual bool			WillAdd();
		virtual void			DidAdd();

				void			SetStackedListenerOnWriter(
									BStackedMessageEventListener*
									stackedListener);

			BJsonMessageWriter*	fWriter;
			bool				fOwnsMessage;
			BStackedMessageEventListener
								*fParent;
			BMessage*			fMessage;
};


/** @brief Stacked listener that accumulates JSON array elements into a BMessage.
 *
 *  Elements are stored using sequentially numbered field names ("0", "1", …).
 */
class BStackedArrayMessageEventListener : public BStackedMessageEventListener {
public:
								BStackedArrayMessageEventListener(
									BJsonMessageWriter* writer,
									BStackedMessageEventListener* parent);
								BStackedArrayMessageEventListener(
									BJsonMessageWriter* writer,
									BStackedMessageEventListener* parent,
									BMessage* message);
								~BStackedArrayMessageEventListener();

				bool			Handle(const BJsonEvent& event);

				const char*		NextItemName();

protected:
				void			DidAdd();

private:
				uint32			fCount;
				BString			fNextItemName;

};


/** @brief Stacked listener that accumulates JSON object members into a BMessage.
 *
 *  Member names come from B_JSON_OBJECT_NAME events and are used as BMessage
 *  field names.
 */
class BStackedObjectMessageEventListener : public BStackedMessageEventListener {
public:
								BStackedObjectMessageEventListener(
									BJsonMessageWriter* writer,
									BStackedMessageEventListener* parent);
								BStackedObjectMessageEventListener(
									BJsonMessageWriter* writer,
									BStackedMessageEventListener* parent,
									BMessage* message);
								~BStackedObjectMessageEventListener();

				bool			Handle(const BJsonEvent& event);

				const char*		NextItemName();

protected:
				bool			WillAdd();
				void			DidAdd();
private:
				BString			fNextItemName;
};

} // namespace BPrivate

using BPrivate::BStackedMessageEventListener;
using BPrivate::BStackedArrayMessageEventListener;
using BPrivate::BStackedObjectMessageEventListener;


// #pragma mark - BStackedMessageEventListener


/** @brief Constructs a stacked listener that owns a newly created BMessage.
 *  @param writer       The owning BJsonMessageWriter.
 *  @param parent       The parent listener on the stack (may be NULL).
 *  @param messageWhat  The 'what' code for the new BMessage.
 */
BStackedMessageEventListener::BStackedMessageEventListener(
	BJsonMessageWriter* writer,
	BStackedMessageEventListener* parent,
	uint32 messageWhat)
{
	fWriter = writer;
	fParent = parent;
	fOwnsMessage = true;
	fMessage = new BMessage(messageWhat);
}


/** @brief Constructs a stacked listener wrapping an externally owned BMessage.
 *  @param writer   The owning BJsonMessageWriter.
 *  @param parent   The parent listener on the stack (may be NULL).
 *  @param message  An externally allocated BMessage; ownership is NOT taken.
 */
BStackedMessageEventListener::BStackedMessageEventListener(
	BJsonMessageWriter* writer,
	BStackedMessageEventListener* parent,
	BMessage* message)
{
	fWriter = writer;
	fParent = parent;
	fOwnsMessage = false;
	fMessage = message;
}


/** @brief Destroys the listener and, if it owns the message, deletes it. */
BStackedMessageEventListener::~BStackedMessageEventListener()
{
	if (fOwnsMessage)
		delete fMessage;
}


/** @brief Handles a JSON primitive or container-start event.
 *
 *  Scalar values (number, string, true, false, null) are added directly to
 *  the current BMessage. Object/array start events push a new stacked
 *  listener onto the writer.
 *
 *  @param event  The JSON event to handle.
 *  @return true to continue, false on error.
 */
bool
BStackedMessageEventListener::Handle(const BJsonEvent& event)
{
	if (fWriter->ErrorStatus() != B_OK)
		return false;

	switch (event.EventType()) {

		case B_JSON_NUMBER:
			AddDouble(event.ContentDouble());
			break;

		case B_JSON_STRING:
			AddString(event.Content());
			break;

		case B_JSON_TRUE:
			AddBool(true);
			break;

		case B_JSON_FALSE:
			AddBool(false);
			break;

		case B_JSON_NULL:
			AddNull();
			break;

		case B_JSON_OBJECT_START:
		{
			SetStackedListenerOnWriter(new BStackedObjectMessageEventListener(
				fWriter, this));
			break;
		}

		case B_JSON_ARRAY_START:
		{
			SetStackedListenerOnWriter(new BStackedArrayMessageEventListener(
				fWriter, this));
			break;
		}

		default:
		{
			HandleError(B_NOT_ALLOWED, JSON_EVENT_LISTENER_ANY_LINE,
				"unexpected type of json item to add to container");
			return false;
		}
	}

	return ErrorStatus() == B_OK;
}


/** @brief Forwards an error to the owning BJsonMessageWriter.
 *  @param status   Error code.
 *  @param line     Source line of the error.
 *  @param message  Human-readable description.
 */
void
BStackedMessageEventListener::HandleError(status_t status, int32 line,
	const char* message)
{
	fWriter->HandleError(status, line, message);
}


/** @brief Not valid on a stacked listener; records an illegal-state error. */
void
BStackedMessageEventListener::Complete()
{
	// illegal state.
	HandleError(B_NOT_ALLOWED, JSON_EVENT_LISTENER_ANY_LINE,
		"Complete() called on stacked message listener");
}


/** @brief Adds a sub-message to the current BMessage under the next item name.
 *  @param message  The sub-message to add (not owned after this call).
 */
void
BStackedMessageEventListener::AddMessage(BMessage* message)
{
	if (WillAdd()) {
		fMessage->AddMessage(NextItemName(), message);
		DidAdd();
	}
}


/** @brief Returns the writer's current error status.
 *  @return The stored error code, or B_OK.
 */
status_t
BStackedMessageEventListener::ErrorStatus()
{
	return fWriter->ErrorStatus();
}


/** @brief Returns the parent listener on the stack.
 *  @return Pointer to the parent, or NULL for the top-level listener.
 */
BStackedMessageEventListener*
BStackedMessageEventListener::Parent()
{
	return fParent;
}


/** @brief Adds a boolean field to the current BMessage.
 *  @param value  The boolean value to store.
 */
void
BStackedMessageEventListener::AddBool(bool value)
{
	if (WillAdd()) {
		fMessage->AddBool(NextItemName(), value);
		DidAdd();
	}
}

/** @brief Adds a null pointer field to the current BMessage. */
void
BStackedMessageEventListener::AddNull()
{
	if (WillAdd()) {
		fMessage->AddPointer(NextItemName(), (void*)NULL);
		DidAdd();
	}
}

/** @brief Adds a double field to the current BMessage.
 *  @param value  The double value to store.
 */
void
BStackedMessageEventListener::AddDouble(double value)
{
	if (WillAdd()) {
		fMessage->AddDouble(NextItemName(), value);
		DidAdd();
	}
}

/** @brief Adds a string field to the current BMessage.
 *  @param value  NUL-terminated string value to store.
 */
void
BStackedMessageEventListener::AddString(const char* value)
{
	if (WillAdd()) {
		fMessage->AddString(NextItemName(), value);
		DidAdd();
	}
}


/** @brief Returns true, allowing the add to proceed.
 *
 *  Subclasses may override to perform validation (e.g. object requires a name).
 *
 *  @return true to allow the field to be added.
 */
bool
BStackedMessageEventListener::WillAdd()
{
	return true;
}


/** @brief Hook called after a field is added; does nothing in the base class. */
void
BStackedMessageEventListener::DidAdd()
{
	// noop - present for overriding
}


/** @brief Installs @p stackedListener as the active listener on the writer.
 *  @param stackedListener  The new top-of-stack listener.
 */
void
BStackedMessageEventListener::SetStackedListenerOnWriter(
	BStackedMessageEventListener* stackedListener)
{
	fWriter->SetStackedListener(stackedListener);
}


// #pragma mark - BStackedArrayMessageEventListener


/** @brief Constructs an array listener that owns a new B_JSON_MESSAGE_WHAT_ARRAY message.
 *  @param writer  The owning BJsonMessageWriter.
 *  @param parent  The parent listener on the stack.
 */
BStackedArrayMessageEventListener::BStackedArrayMessageEventListener(
	BJsonMessageWriter* writer,
	BStackedMessageEventListener* parent)
	:
	BStackedMessageEventListener(writer, parent, B_JSON_MESSAGE_WHAT_ARRAY)
{
	fCount = 0;
}


/** @brief Constructs an array listener wrapping an externally owned BMessage.
 *
 *  Sets the message's 'what' code to B_JSON_MESSAGE_WHAT_ARRAY.
 *
 *  @param writer   The owning BJsonMessageWriter.
 *  @param parent   The parent listener on the stack.
 *  @param message  External BMessage to populate.
 */
BStackedArrayMessageEventListener::BStackedArrayMessageEventListener(
	BJsonMessageWriter* writer,
	BStackedMessageEventListener* parent,
	BMessage* message)
	:
	BStackedMessageEventListener(writer, parent, message)
{
	message->what = B_JSON_MESSAGE_WHAT_ARRAY;
	fCount = 0;
}


/** @brief Destroys the array listener. */
BStackedArrayMessageEventListener::~BStackedArrayMessageEventListener()
{
}


/** @brief Handles B_JSON_ARRAY_END by finalising and popping the listener.
 *
 *  On B_JSON_ARRAY_END the accumulated BMessage is added to the parent and
 *  this listener is deleted. All other events are forwarded to the base class.
 *
 *  @param event  The JSON event.
 *  @return true to continue, false on error.
 */
bool
BStackedArrayMessageEventListener::Handle(const BJsonEvent& event)
{
	if (fWriter->ErrorStatus() != B_OK)
		return false;

	switch (event.EventType()) {
		case B_JSON_ARRAY_END:
		{
			if (fParent != NULL)
				fParent->AddMessage(fMessage);
			SetStackedListenerOnWriter(fParent);
			delete this;
			break;
		}

		default:
			return BStackedMessageEventListener::Handle(event);
	}

	return true;
}


/** @brief Returns the sequential index name for the next array element.
 *  @return A NUL-terminated string such as "0", "1", "2", …
 */
const char*
BStackedArrayMessageEventListener::NextItemName()
{
	fNextItemName.SetToFormat("%" B_PRIu32, fCount);
	return fNextItemName.String();
}


/** @brief Increments the element counter after each successful array add. */
void
BStackedArrayMessageEventListener::DidAdd()
{
	BStackedMessageEventListener::DidAdd();
	fCount++;
}


// #pragma mark - BStackedObjectMessageEventListener


/** @brief Constructs an object listener that owns a new B_JSON_MESSAGE_WHAT_OBJECT message.
 *  @param writer  The owning BJsonMessageWriter.
 *  @param parent  The parent listener on the stack.
 */
BStackedObjectMessageEventListener::BStackedObjectMessageEventListener(
	BJsonMessageWriter* writer,
	BStackedMessageEventListener* parent)
	:
	BStackedMessageEventListener(writer, parent, B_JSON_MESSAGE_WHAT_OBJECT)
{
}


/** @brief Constructs an object listener wrapping an externally owned BMessage.
 *
 *  Sets the message's 'what' code to B_JSON_MESSAGE_WHAT_OBJECT.
 *
 *  @param writer   The owning BJsonMessageWriter.
 *  @param parent   The parent listener on the stack.
 *  @param message  External BMessage to populate.
 */
BStackedObjectMessageEventListener::BStackedObjectMessageEventListener(
	BJsonMessageWriter* writer,
	BStackedMessageEventListener* parent,
	BMessage* message)
	:
	BStackedMessageEventListener(writer, parent, message)
{
	message->what = B_JSON_MESSAGE_WHAT_OBJECT;
}


/** @brief Destroys the object listener. */
BStackedObjectMessageEventListener::~BStackedObjectMessageEventListener()
{
}


/** @brief Handles object-specific events (end and member name).
 *
 *  B_JSON_OBJECT_END finalises and pops the listener.
 *  B_JSON_OBJECT_NAME stores the name for use by the next value event.
 *  All other events are forwarded to the base class.
 *
 *  @param event  The JSON event.
 *  @return true to continue, false on error.
 */
bool
BStackedObjectMessageEventListener::Handle(const BJsonEvent& event)
{
	if (fWriter->ErrorStatus() != B_OK)
		return false;

	switch (event.EventType()) {
		case B_JSON_OBJECT_END:
		{
			if (fParent != NULL)
				fParent->AddMessage(fMessage);
			SetStackedListenerOnWriter(fParent);
			delete this;
			break;
		}

		case B_JSON_OBJECT_NAME:
			fNextItemName.SetTo(event.Content());
			break;

		default:
			return BStackedMessageEventListener::Handle(event);
	}

	return true;
}


/** @brief Returns the most recently received object member name.
 *  @return NUL-terminated member name string.
 */
const char*
BStackedObjectMessageEventListener::NextItemName()
{
	return fNextItemName.String();
}


/** @brief Validates that a member name has been received before allowing an add.
 *  @return true if a name is available, false (with error) if name is missing.
 */
bool
BStackedObjectMessageEventListener::WillAdd()
{
	if (0 == fNextItemName.Length()) {
		HandleError(B_NOT_ALLOWED, JSON_EVENT_LISTENER_ANY_LINE,
				"missing name for adding value into an object");
		return false;
	}

	return true;
}


/** @brief Clears the pending member name after a value has been added. */
void
BStackedObjectMessageEventListener::DidAdd()
{
	BStackedMessageEventListener::DidAdd();
	fNextItemName.SetTo("", 0);
}


// #pragma mark - BJsonMessageWriter


/** @brief Constructs the message writer targeting @p message.
 *  @param message  The top-level BMessage to populate. Not owned.
 */
BJsonMessageWriter::BJsonMessageWriter(BMessage& message)
{
	fTopLevelMessage = &message;
	fStackedListener = NULL;
}


/** @brief Destroys the writer and frees any remaining stacked listeners. */
BJsonMessageWriter::~BJsonMessageWriter()
{
	BStackedMessageEventListener* listener = fStackedListener;

	while (listener != NULL) {
		BStackedMessageEventListener* nextListener = listener->Parent();
		delete listener;
		listener = nextListener;
	}

	fStackedListener = NULL;
}


/** @brief Routes a JSON event to the current stacked listener or handles
 *         the top-level object/array start.
 *
 *  At the top level only B_JSON_OBJECT_START and B_JSON_ARRAY_START are
 *  accepted; anything else is an error. Once a stacked listener exists all
 *  events are forwarded there.
 *
 *  @param event  The JSON event to handle.
 *  @return true to continue parsing, false on error.
 */
bool
BJsonMessageWriter::Handle(const BJsonEvent& event)
{
	if (fErrorStatus != B_OK)
		return false;

	if (fStackedListener != NULL)
		return fStackedListener->Handle(event);
	else {
		switch(event.EventType()) {
			case B_JSON_OBJECT_START:
			{
				SetStackedListener(new BStackedObjectMessageEventListener(
					this, NULL, fTopLevelMessage));
				break;
			}

			case B_JSON_ARRAY_START:
			{
				fTopLevelMessage->what = B_JSON_MESSAGE_WHAT_ARRAY;
				SetStackedListener(new BStackedArrayMessageEventListener(
					this, NULL, fTopLevelMessage));
				break;
			}

			default:
			{
				HandleError(B_NOT_ALLOWED, JSON_EVENT_LISTENER_ANY_LINE,
					"a message object can only handle an object or an array"
					"at the top level");
				return false;
			}
		}
	}

	return true; // keep going
}


/** @brief Called when the JSON stream ends; validates the stack is empty.
 *
 *  A non-NULL stacked listener at this point means an unclosed container.
 */
void
BJsonMessageWriter::Complete()
{
	if (fStackedListener != NULL) {
		HandleError(B_BAD_DATA, JSON_EVENT_LISTENER_ANY_LINE,
			"unexpected end of input data processing structure");
	}
}


/** @brief Replaces the current top-of-stack listener.
 *  @param listener  The new listener to make active.
 */
void
BJsonMessageWriter::SetStackedListener(
	BStackedMessageEventListener* listener)
{
	fStackedListener = listener;
}
