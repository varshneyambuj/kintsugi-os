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

/** @file JsonTextWriter.cpp
 *  @brief Serialises JSON events to a BDataIO stream as compact UTF-8 encoded
 *         JSON text, using an internal stack to track container nesting.
 */


#include "JsonTextWriter.h"

#include <stdio.h>
#include <stdlib.h>

#include <UnicodeChar.h>


namespace BPrivate {


/** @brief Returns true if the byte is a printable 7-bit ASCII character.
 *  @param c  Byte to test.
 *  @return true when 0x20 <= c < 0x7f.
 */
static bool
b_json_is_7bit_clean(uint8 c)
{
	return c >= 0x20 && c < 0x7f;
}


/** @brief Returns true if the byte is an illegal JSON character (control or DEL).
 *  @param c  Byte to test.
 *  @return true when c < 0x20 or c == 0x7f.
 */
static bool
b_json_is_illegal(uint8 c)
{
	return c < 0x20 || c == 0x7f;
}


/** @brief Returns the two-character JSON escape sequence for special ASCII chars.
 *
 *  Covers: '"', '\\', '/', '\\b', '\\f', '\\n', '\\r', '\\t'.
 *
 *  @param c  Character to look up.
 *  @return Pointer to a NUL-terminated two-character escape string, or NULL
 *          if no simple escape exists for @p c.
 */
static const char*
b_json_simple_esc_sequence(char c)
{
	switch (c) {
		case '"':
			return "\\\"";
		case '\\':
			return "\\\\";
		case '/':
			return "\\/";
		case '\b':
			return "\\b";
		case '\f':
			return "\\f";
		case '\n':
			return "\\n";
		case '\r':
			return "\\r";
		case '\t':
			return "\\t";
		default:
			return NULL;
	}
}


/** @brief Counts the leading run of bytes that are 7-bit clean and need no escape.
 *
 *  Stops at the first byte that either is not 7-bit clean or has a simple
 *  escape sequence.
 *
 *  @param c       Pointer to the start of the byte sequence.
 *  @param length  Maximum number of bytes to examine.
 *  @return The length of the leading unescaped run.
 */
static size_t
b_json_len_7bit_clean_non_esc(uint8* c, size_t length) {
	size_t result = 0;

	while (result < length
		&& b_json_is_7bit_clean(c[result])
		&& b_json_simple_esc_sequence(c[result]) == NULL) {
		result++;
	}

	return result;
}


/*! The class and sub-classes of it are used as a stack internal to the
    BJsonTextWriter class.  As the JSON is parsed, the stack of these
    internal listeners follows the stack of the JSON parsing in terms of
    containers; arrays and objects.
*/

/** @brief Base stacked event listener used by BJsonTextWriter.
 *
 *  BJsonTextWriter maintains a stack of these listeners that mirrors the
 *  nesting of JSON containers (arrays and objects). Each listener handles
 *  events appropriate to its context and delegates streaming operations back
 *  to the parent BJsonTextWriter.
 */
class BJsonTextWriterStackedEventListener : public BJsonEventListener {
public:
								BJsonTextWriterStackedEventListener(
									BJsonTextWriter* writer,
									BJsonTextWriterStackedEventListener* parent);
								~BJsonTextWriterStackedEventListener();

				bool			Handle(const BJsonEvent& event);
				void			HandleError(status_t status, int32 line,
									const char* message);
				void			Complete();

				status_t		ErrorStatus();

				BJsonTextWriterStackedEventListener*
								Parent();

protected:

			status_t			StreamNumberNode(const BJsonEvent& event);

			status_t			StreamStringVerbatim(const char* string);
			status_t			StreamStringVerbatim(const char* string,
									off_t offset, size_t length);

			status_t			StreamStringEncoded(const char* string);
			status_t			StreamStringEncoded(const char* string,
									off_t offset, size_t length);

			status_t			StreamQuotedEncodedString(const char* string);
			status_t			StreamQuotedEncodedString(const char* string,
									off_t offset, size_t length);

			status_t			StreamChar(char c);

		virtual	bool			WillAdd();
		virtual void			DidAdd();

			void				SetStackedListenerOnWriter(
									BJsonTextWriterStackedEventListener*
									stackedListener);

			BJsonTextWriter*
								fWriter;
			BJsonTextWriterStackedEventListener*
								fParent;
				uint32			fCount;

};


/** @brief Stacked listener that handles events inside a JSON array context. */
class BJsonTextWriterArrayStackedEventListener
	: public BJsonTextWriterStackedEventListener {
public:
								BJsonTextWriterArrayStackedEventListener(
									BJsonTextWriter* writer,
									BJsonTextWriterStackedEventListener* parent);
								~BJsonTextWriterArrayStackedEventListener();

				bool			Handle(const BJsonEvent& event);

protected:
				bool			WillAdd();
};


/** @brief Stacked listener that handles events inside a JSON object context. */
class BJsonTextWriterObjectStackedEventListener
	: public BJsonTextWriterStackedEventListener {
public:
								BJsonTextWriterObjectStackedEventListener(
									BJsonTextWriter* writer,
									BJsonTextWriterStackedEventListener* parent);
								~BJsonTextWriterObjectStackedEventListener();

				bool			Handle(const BJsonEvent& event);
};

} // namespace BPrivate


using BPrivate::BJsonTextWriterStackedEventListener;
using BPrivate::BJsonTextWriterArrayStackedEventListener;
using BPrivate::BJsonTextWriterObjectStackedEventListener;


// #pragma mark - BJsonTextWriterStackedEventListener


/** @brief Constructs the stacked listener.
 *  @param writer  The owning BJsonTextWriter.
 *  @param parent  The parent listener on the stack (NULL for the root).
 */
BJsonTextWriterStackedEventListener::BJsonTextWriterStackedEventListener(
	BJsonTextWriter* writer,
	BJsonTextWriterStackedEventListener* parent)
{
	fWriter = writer;
	fParent = parent;
	fCount = 0;
}


/** @brief Destroys the stacked listener. */
BJsonTextWriterStackedEventListener::~BJsonTextWriterStackedEventListener()
{
}


/** @brief Handles a JSON event by streaming the corresponding text.
 *
 *  Calls WillAdd() before writing and DidAdd() after a successful write.
 *  Object/array start events push a new stacked listener onto the writer.
 *
 *  @param event  The JSON event to handle.
 *  @return true to continue parsing, false on error.
 */
bool
BJsonTextWriterStackedEventListener::Handle(const BJsonEvent& event)
{
	status_t writeResult = B_OK;

	if (fWriter->ErrorStatus() != B_OK)
		return false;

	switch (event.EventType()) {

		case B_JSON_NUMBER:
		case B_JSON_STRING:
		case B_JSON_TRUE:
		case B_JSON_FALSE:
		case B_JSON_NULL:
		case B_JSON_OBJECT_START:
		case B_JSON_ARRAY_START:
			if (!WillAdd())
				return false;
			break;

		default:
			break;
	}

	switch (event.EventType()) {

		case B_JSON_NUMBER:
			writeResult = StreamNumberNode(event);
			break;

		case B_JSON_STRING:
			writeResult = StreamQuotedEncodedString(event.Content());
			break;

		case B_JSON_TRUE:
			writeResult = StreamStringVerbatim("true", 0, 4);
			break;

		case B_JSON_FALSE:
			writeResult = StreamStringVerbatim("false", 0, 5);
			break;

		case B_JSON_NULL:
			writeResult = StreamStringVerbatim("null", 0, 4);
			break;

		case B_JSON_OBJECT_START:
		{
			writeResult = StreamChar('{');

			if (writeResult == B_OK) {
				SetStackedListenerOnWriter(
					new BJsonTextWriterObjectStackedEventListener(
						fWriter, this));
			}
			break;
		}

		case B_JSON_ARRAY_START:
		{
			writeResult = StreamChar('[');

			if (writeResult == B_OK) {
				SetStackedListenerOnWriter(
					new BJsonTextWriterArrayStackedEventListener(
						fWriter, this));
			}
			break;
		}

		default:
		{
			HandleError(B_NOT_ALLOWED, JSON_EVENT_LISTENER_ANY_LINE,
				"unexpected type of json item to add to container");
			return false;
		}
	}

	if (writeResult == B_OK)
		DidAdd();
	else {
		HandleError(writeResult, JSON_EVENT_LISTENER_ANY_LINE,
			"error writing output");
	}

	return ErrorStatus() == B_OK;
}


/** @brief Forwards an error to the owning BJsonTextWriter.
 *  @param status   The error code.
 *  @param line     Source line of the error.
 *  @param message  Human-readable description.
 */
void
BJsonTextWriterStackedEventListener::HandleError(status_t status, int32 line,
	const char* message)
{
	fWriter->HandleError(status, line, message);
}


/** @brief Not valid to call on a stacked listener; records an error.
 *
 *  Complete() should only be called on the top-level BJsonTextWriter.
 */
void
BJsonTextWriterStackedEventListener::Complete()
{
		// illegal state.
	HandleError(JSON_EVENT_LISTENER_ANY_LINE, B_NOT_ALLOWED,
		"Complete() called on stacked message listener");
}


/** @brief Queries the error status from the owning writer.
 *  @return The writer's current error status.
 */
status_t
BJsonTextWriterStackedEventListener::ErrorStatus()
{
	return fWriter->ErrorStatus();
}


/** @brief Returns the parent listener on the stack.
 *  @return Pointer to the parent, or NULL for the root listener.
 */
BJsonTextWriterStackedEventListener*
BJsonTextWriterStackedEventListener::Parent()
{
	return fParent;
}


/** @brief Forwards StreamNumberNode to the owning writer.
 *  @param event  The B_JSON_NUMBER event whose Content() is the number text.
 *  @return Write result status.
 */
status_t
BJsonTextWriterStackedEventListener::StreamNumberNode(const BJsonEvent& event)
{
	return fWriter->StreamNumberNode(event);
}


/** @brief Forwards StreamStringVerbatim (whole string) to the owning writer.
 *  @param string  NUL-terminated string to write without encoding.
 *  @return Write result status.
 */
status_t
BJsonTextWriterStackedEventListener::StreamStringVerbatim(const char* string)
{
	return fWriter->StreamStringVerbatim(string);
}


/** @brief Forwards StreamStringVerbatim (substring) to the owning writer.
 *  @param string  Base pointer of the string.
 *  @param offset  Byte offset from which to start writing.
 *  @param length  Number of bytes to write.
 *  @return Write result status.
 */
status_t
BJsonTextWriterStackedEventListener::StreamStringVerbatim(const char* string,
	off_t offset, size_t length)
{
	return fWriter->StreamStringVerbatim(string, offset, length);
}


/** @brief Forwards StreamStringEncoded (whole string) to the owning writer.
 *  @param string  UTF-8 string to JSON-encode and write.
 *  @return Write result status.
 */
status_t
BJsonTextWriterStackedEventListener::StreamStringEncoded(const char* string)
{
	return fWriter->StreamStringEncoded(string);
}


/** @brief Forwards StreamStringEncoded (substring) to the owning writer.
 *  @param string  Base pointer of the UTF-8 string.
 *  @param offset  Byte offset from which to start encoding.
 *  @param length  Number of bytes to encode.
 *  @return Write result status.
 */
status_t
BJsonTextWriterStackedEventListener::StreamStringEncoded(const char* string,
	off_t offset, size_t length)
{
	return fWriter->StreamStringEncoded(string, offset, length);
}


/** @brief Forwards StreamQuotedEncodedString (whole string) to the writer.
 *  @param string  UTF-8 string to quote and JSON-encode.
 *  @return Write result status.
 */
status_t
BJsonTextWriterStackedEventListener::StreamQuotedEncodedString(
	const char* string)
{
	return fWriter->StreamQuotedEncodedString(string);
}


/** @brief Forwards StreamQuotedEncodedString (substring) to the writer.
 *  @param string  Base pointer of the UTF-8 string.
 *  @param offset  Byte offset from which to start encoding.
 *  @param length  Number of bytes to encode.
 *  @return Write result status.
 */
status_t
BJsonTextWriterStackedEventListener::StreamQuotedEncodedString(
	const char* string, off_t offset, size_t length)
{
	return fWriter->StreamQuotedEncodedString(string, offset, length);
}


/** @brief Forwards StreamChar to the owning writer.
 *  @param c  A single character to write (e.g. '{', '}', ',', ':').
 *  @return Write result status.
 */
status_t
BJsonTextWriterStackedEventListener::StreamChar(char c)
{
	return fWriter->StreamChar(c);
}


/** @brief Called before writing a new value; returns true to continue.
 *
 *  The base implementation simply allows the write. Subclasses may insert
 *  commas or validate state before writing.
 *
 *  @return true to allow the write, false to abort.
 */
bool
BJsonTextWriterStackedEventListener::WillAdd()
{
	return true; // carry on
}


/** @brief Called after a value has been successfully written; increments the count. */
void
BJsonTextWriterStackedEventListener::DidAdd()
{
	fCount++;
}


/** @brief Replaces the current stacked listener on the writer.
 *  @param stackedListener  The new listener to install.
 */
void
BJsonTextWriterStackedEventListener::SetStackedListenerOnWriter(
	BJsonTextWriterStackedEventListener* stackedListener)
{
	fWriter->SetStackedListener(stackedListener);
}


// #pragma mark - BJsonTextWriterArrayStackedEventListener


/** @brief Constructs the array stacked listener.
 *  @param writer  The owning BJsonTextWriter.
 *  @param parent  The parent listener on the stack.
 */
BJsonTextWriterArrayStackedEventListener::BJsonTextWriterArrayStackedEventListener(
	BJsonTextWriter* writer,
	BJsonTextWriterStackedEventListener* parent)
	:
	BJsonTextWriterStackedEventListener(writer, parent)
{
}


/** @brief Destroys the array stacked listener. */
BJsonTextWriterArrayStackedEventListener
	::~BJsonTextWriterArrayStackedEventListener()
{
}


/** @brief Handles JSON events in an array context.
 *
 *  Writes the closing ']' on B_JSON_ARRAY_END and pops the stack; delegates
 *  all other events to the base class.
 *
 *  @param event  The JSON event to process.
 *  @return true to continue, false on error.
 */
bool
BJsonTextWriterArrayStackedEventListener::Handle(const BJsonEvent& event)
{
	status_t writeResult = B_OK;

	if (fWriter->ErrorStatus() != B_OK)
		return false;

	switch (event.EventType()) {
		case B_JSON_ARRAY_END:
		{
			writeResult = StreamChar(']');

			if (writeResult == B_OK) {
				SetStackedListenerOnWriter(fParent);
				delete this;
				return true; // must exit immediately after delete this.
			}
			break;
		}

		default:
			return BJsonTextWriterStackedEventListener::Handle(event);
	}

	if(writeResult != B_OK) {
		HandleError(writeResult, JSON_EVENT_LISTENER_ANY_LINE,
			"error writing output");
	}

	return ErrorStatus() == B_OK;
}


/** @brief Writes a comma separator before all elements after the first.
 *
 *  @return true to allow the write, false on I/O error.
 */
bool
BJsonTextWriterArrayStackedEventListener::WillAdd()
{
	status_t writeResult = B_OK;

	if (writeResult == B_OK && fCount > 0)
		writeResult = StreamChar(',');

	if (writeResult != B_OK) {
		HandleError(B_IO_ERROR, JSON_EVENT_LISTENER_ANY_LINE,
			"error writing data");
		return false;
	}

	return BJsonTextWriterStackedEventListener::WillAdd();
}


// #pragma mark - BJsonTextWriterObjectStackedEventListener


/** @brief Constructs the object stacked listener.
 *  @param writer  The owning BJsonTextWriter.
 *  @param parent  The parent listener on the stack.
 */
BJsonTextWriterObjectStackedEventListener::BJsonTextWriterObjectStackedEventListener(
	BJsonTextWriter* writer,
	BJsonTextWriterStackedEventListener* parent)
	:
	BJsonTextWriterStackedEventListener(writer, parent)
{
}


/** @brief Destroys the object stacked listener. */
BJsonTextWriterObjectStackedEventListener
	::~BJsonTextWriterObjectStackedEventListener()
{
}


/** @brief Handles JSON events in an object context.
 *
 *  Writes '}' on B_JSON_OBJECT_END (popping the stack), writes the quoted
 *  name and ':' separator on B_JSON_OBJECT_NAME, and delegates value events
 *  to the base class.
 *
 *  @param event  The JSON event to process.
 *  @return true to continue, false on error.
 */
bool
BJsonTextWriterObjectStackedEventListener::Handle(const BJsonEvent& event)
{
	status_t writeResult = B_OK;

	if (fWriter->ErrorStatus() != B_OK)
		return false;

	switch (event.EventType()) {
		case B_JSON_OBJECT_END:
		{
			writeResult = StreamChar('}');

			if (writeResult == B_OK) {
				SetStackedListenerOnWriter(fParent);
				delete this;
				return true; // just exit after delete this.
			}
			break;
		}

		case B_JSON_OBJECT_NAME:
		{
			if (writeResult == B_OK && fCount > 0)
				writeResult = StreamChar(',');

			if (writeResult == B_OK)
				writeResult = StreamQuotedEncodedString(event.Content());

			if (writeResult == B_OK)
				writeResult = StreamChar(':');

			break;
		}

		default:
			return BJsonTextWriterStackedEventListener::Handle(event);
	}

	if (writeResult != B_OK) {
		HandleError(writeResult, JSON_EVENT_LISTENER_ANY_LINE,
			"error writing data");
	}

	return ErrorStatus() == B_OK;
}


// #pragma mark - BJsonTextWriter


/** @brief Constructs the text writer and installs a root stacked listener.
 *
 *  Pre-fills the Unicode assembly buffer with the '\\u' prefix so that
 *  subsequent unicode character writes are efficient.
 *
 *  @param dataIO  Destination stream for the JSON output bytes.
 */
BJsonTextWriter::BJsonTextWriter(
	BDataIO* dataIO)
	:
	fDataIO(dataIO)
{

		// this is a preparation for this buffer to easily be used later
		// to efficiently output encoded unicode characters.

	fUnicodeAssemblyBuffer[0] = '\\';
	fUnicodeAssemblyBuffer[1] = 'u';

	fStackedListener = new BJsonTextWriterStackedEventListener(this, NULL);
}


/** @brief Destroys the writer and frees all stacked listeners. */
BJsonTextWriter::~BJsonTextWriter()
{
	BJsonTextWriterStackedEventListener* listener = fStackedListener;

	while (listener != NULL) {
		BJsonTextWriterStackedEventListener* nextListener = listener->Parent();
		delete listener;
		listener = nextListener;
	}

	fStackedListener = NULL;
}


/** @brief Forwards a JSON event to the current top-of-stack listener.
 *  @param event  The event to handle.
 *  @return true to continue, false on error.
 */
bool
BJsonTextWriter::Handle(const BJsonEvent& event)
{
	return fStackedListener->Handle(event);
}


/** @brief Called when the JSON stream ends; verifies all containers are closed.
 *
 *  If the stacked listener still has a parent at this point, at least one
 *  array or object was never closed and an error is recorded.
 */
void
BJsonTextWriter::Complete()
{
		// upon construction, this object will add one listener to the
		// stack.  On complete, this listener should still be there;
		// otherwise this implies an unterminated structure such as array
		// / object.

	if (fStackedListener->Parent() != NULL) {
		HandleError(B_BAD_DATA, JSON_EVENT_LISTENER_ANY_LINE,
			"unexpected end of input data");
	}
}


/** @brief Replaces the current top-of-stack listener.
 *  @param stackedListener  The new listener to push onto the stack.
 */
void
BJsonTextWriter::SetStackedListener(
	BJsonTextWriterStackedEventListener* stackedListener)
{
	fStackedListener = stackedListener;
}


/** @brief Writes the raw number string from a B_JSON_NUMBER event.
 *  @param event  The number event whose Content() holds the digit string.
 *  @return Write result status.
 */
status_t
BJsonTextWriter::StreamNumberNode(const BJsonEvent& event)
{
	return StreamStringVerbatim(event.Content());
}


/** @brief Writes a NUL-terminated string verbatim (no JSON encoding).
 *  @param string  NUL-terminated string to write.
 *  @return Write result status.
 */
status_t
BJsonTextWriter::StreamStringVerbatim(const char* string)
{
	return StreamStringVerbatim(string, 0, strlen(string));
}


/** @brief Writes a substring verbatim (no JSON encoding).
 *  @param string  Base pointer of the string.
 *  @param offset  Byte offset to start writing from.
 *  @param length  Number of bytes to write.
 *  @return Write result status.
 */
status_t
BJsonTextWriter::StreamStringVerbatim(const char* string,
	off_t offset, size_t length)
{
	return fDataIO->WriteExactly(&string[offset], length);
}


/** @brief JSON-encodes and writes a NUL-terminated UTF-8 string.
 *  @param string  UTF-8 string to encode.
 *  @return Write result status.
 */
status_t
BJsonTextWriter::StreamStringEncoded(const char* string)
{
	return StreamStringEncoded(string, 0, strlen(string));
}


/** @brief Writes a single Unicode code point as a \\uXXXX JSON escape.
 *  @param c  Unicode code point to encode.
 *  @return Write result status.
 */
status_t
BJsonTextWriter::StreamStringUnicodeCharacter(uint32 c)
{
	sprintf(&fUnicodeAssemblyBuffer[2], "%04" B_PRIx32, c);
		// note that the buffer's first two bytes are populated with the JSON
		// prefix for a unicode char.
	return StreamStringVerbatim(fUnicodeAssemblyBuffer, 0, 6);
}


/*! Note that this method will expect a UTF-8 encoded string. */

/** @brief JSON-encodes and writes a substring of a UTF-8 string.
 *
 *  Characters are emitted as simple escapes (\\n, \\t, etc.), verbatim
 *  ASCII runs, or \\uXXXX sequences for multi-byte UTF-8 code points.
 *  Illegal characters are skipped with a warning to stderr.
 *
 *  @param string  Base pointer of the UTF-8 string.
 *  @param offset  Byte offset to start encoding from.
 *  @param length  Number of bytes to encode.
 *  @return Write result status.
 */
status_t
BJsonTextWriter::StreamStringEncoded(const char* string,
	off_t offset, size_t length)
{
	status_t writeResult = B_OK;
	uint8* string8bit = (uint8*)string;
	size_t i = 0;

	while (i < length && writeResult == B_OK) {
		uint8 c = string8bit[offset + i];
		const char* simpleEsc = b_json_simple_esc_sequence(c);

		if (simpleEsc != NULL) {
			// here the character to emit is something like a tab or a quote
			// in this case the output JSON should escape it so that it looks
			// like \t or \n in the output.
			writeResult = StreamStringVerbatim(simpleEsc, 0, 2);
			i++;
		} else {
			if (b_json_is_7bit_clean(c)) {
				// in this case the first character is a simple one that can be
				// output without any special handling.  Find the sequence of
				// such characters and output them as a sequence so that it's
				// included as one write operation.
				size_t l = 1 + b_json_len_7bit_clean_non_esc(
					&string8bit[offset + i + 1], length - (offset + i + 1));
				writeResult = StreamStringVerbatim(&string[offset + i], 0, l);
				i += static_cast<size_t>(l);
			} else {
				if (b_json_is_illegal(c)) {
					fprintf(stderr, "! string encoding error - illegal "
						"character [%" B_PRIu32 "]\n", static_cast<uint32>(c));
					i++;
				} else {
					// now we have a UTF-8 sequence.  Read the UTF-8 sequence
					// to get the unicode character and then encode that as
					// JSON.
					const char* unicodeStr = &string[offset + i];
					uint32 unicodeCharacter = BUnicodeChar::FromUTF8(
						&unicodeStr);
					writeResult = StreamStringUnicodeCharacter(
						unicodeCharacter);
					i += static_cast<size_t>(unicodeStr - &string[offset + i]);
				}
			}
		}
	}

	return writeResult;
}


/** @brief Writes a JSON-quoted, JSON-encoded version of a NUL-terminated string.
 *  @param string  UTF-8 string to quote and encode.
 *  @return Write result status.
 */
status_t
BJsonTextWriter::StreamQuotedEncodedString(const char* string)
{
	return StreamQuotedEncodedString(string, 0, strlen(string));
}


/** @brief Writes a JSON-quoted, JSON-encoded version of a substring.
 *
 *  Wraps StreamStringEncoded() with leading and trailing double-quote
 *  characters.
 *
 *  @param string  Base pointer of the UTF-8 string.
 *  @param offset  Byte offset to start encoding from.
 *  @param length  Number of bytes to encode.
 *  @return Write result status.
 */
status_t
BJsonTextWriter::StreamQuotedEncodedString(const char* string,
	off_t offset, size_t length)
{
	status_t write_result = B_OK;

	if (write_result == B_OK)
		write_result = StreamChar('\"');

	if (write_result == B_OK)
		write_result = StreamStringEncoded(string, offset, length);

	if (write_result == B_OK)
		write_result = StreamChar('\"');

	return write_result;
}


/** @brief Writes a single byte to the underlying BDataIO.
 *  @param c  The byte to write.
 *  @return Write result status (B_OK or I/O error).
 */
status_t
BJsonTextWriter::StreamChar(char c)
{
	return fDataIO->WriteExactly(&c, 1);
}
