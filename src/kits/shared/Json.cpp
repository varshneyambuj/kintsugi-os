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
 *   Copyright 2017-2023, Andrew Lindesay <apl@lindesay.co.nz>
 *   Copyright 2014-2017, Augustin Cavalier (waddlesplash)
 *   Copyright 2014, Stephan Aßmus <superstippi@gmx.de>
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Andrew Lindesay
 *       Augustin Cavalier (waddlesplash)
 *       Stephan Aßmus
 */

/** @file Json.cpp
 *  @brief Streaming JSON parser that converts JSON text into BMessage objects
 *         or fires events at a BJsonEventListener as data is read.
 */


#include "Json.h"

#include <cstdio>
#include <cstdlib>
#include <ctype.h>
#include <cerrno>

#include <AutoDeleter.h>
#include <DataIO.h>
#include <UnicodeChar.h>

#include "JsonEventListener.h"
#include "JsonMessageWriter.h"


// #pragma mark - Public methods

namespace BPrivate {

/*!	A buffer is used to assemble strings into. This will be the initial size
	of this buffer.
*/

static const size_t kInitialAssemblyBufferSize = 64;

/*!	A buffer is used to assemble strings into. This buffer starts off small
	but is able to grow as the string it needs to process as encountered. To
	avoid frequent reallocation of the buffer, the buffer will be retained
	between strings. This is the maximum size of buffer that will be retained.
*/

static const size_t kRetainedAssemblyBufferSize = 32 * 1024;

static const size_t kAssemblyBufferSizeIncrement = 256;

static const size_t kMaximumUtf8SequenceLength = 7;


/** @brief Growable character buffer used during JSON string parsing.
 *
 *  Accumulates characters (including multi-byte UTF-8 sequences) as they are
 *  read from the input stream. The buffer starts small and reallocates as
 *  needed, but shrinks back to kRetainedAssemblyBufferSize when Reset() is
 *  called to avoid holding large allocations between strings.
 */
class JsonParseAssemblyBuffer {
public:
	/** @brief Constructs the buffer and performs the initial allocation. */
	JsonParseAssemblyBuffer()
		:
		fAssemblyBuffer(NULL),
		fAssemblyBufferAllocatedSize(0),
		fAssemblyBufferUsedSize(0)
	{
		fAssemblyBuffer = (char*) malloc(kInitialAssemblyBufferSize);
		if (fAssemblyBuffer != NULL)
			fAssemblyBufferAllocatedSize = kInitialAssemblyBufferSize;
	}

	/** @brief Frees the underlying heap buffer. */
	~JsonParseAssemblyBuffer()
	{
		if (fAssemblyBuffer != NULL)
			free(fAssemblyBuffer);
	}

	/** @brief Returns a pointer to the raw accumulated bytes.
	 *  @return Pointer to the internal buffer (not NUL-terminated unless
	 *          AppendCharacter(0) was called explicitly).
	 */
	const char* Buffer() const
	{
		return fAssemblyBuffer;
	}

	/*! This method should be used each time that the assembly buffer has
		been finished with by some section of logic.
	*/

	/** @brief Resets the used-size counter and optionally shrinks the buffer.
	 *
	 *  If the buffer grew beyond kRetainedAssemblyBufferSize it is reallocated
	 *  down to that size so that large one-off strings do not permanently bloat
	 *  memory.
	 *
	 *  @return B_OK on success, B_NO_MEMORY if the shrink reallocation fails.
	 */
	status_t Reset()
	{
		fAssemblyBufferUsedSize = 0;

		if (fAssemblyBufferAllocatedSize > kRetainedAssemblyBufferSize) {
			fAssemblyBuffer = (char*) realloc(fAssemblyBuffer, kRetainedAssemblyBufferSize);
			if (fAssemblyBuffer == NULL) {
				fAssemblyBufferAllocatedSize = 0;
				return B_NO_MEMORY;
			}
			fAssemblyBufferAllocatedSize = kRetainedAssemblyBufferSize;
		}

		return B_OK;
	}

	/** @brief Appends a single byte to the buffer, growing it if necessary.
	 *  @param c  The byte to append.
	 *  @return B_OK on success, B_NO_MEMORY if growth fails.
	 */
	status_t AppendCharacter(char c)
	{
		status_t result = _EnsureAssemblyBufferAllocatedSize(fAssemblyBufferUsedSize + 1);

		if (result == B_OK) {
			fAssemblyBuffer[fAssemblyBufferUsedSize] = c;
			fAssemblyBufferUsedSize++;
		}

		return result;
	}

	/** @brief Appends a raw byte sequence to the buffer.
	 *  @param str  Pointer to the bytes to copy.
	 *  @param len  Number of bytes to copy.
	 *  @return B_OK on success, B_NO_MEMORY if growth fails.
	 */
	status_t AppendCharacters(char* str, size_t len)
	{
		status_t result = _EnsureAssemblyBufferAllocatedSize(fAssemblyBufferUsedSize + len);

		if (result == B_OK) {
			memcpy(&fAssemblyBuffer[fAssemblyBufferUsedSize], str, len);
			fAssemblyBufferUsedSize += len;
		}

		return result;
	}

	/** @brief Encodes a Unicode code point as UTF-8 and appends the bytes.
	 *  @param c  Unicode code point to encode and append.
	 *  @return B_OK on success, B_NO_MEMORY if growth fails.
	 */
	status_t AppendUnicodeCharacter(uint32 c)
	{
		status_t result = _EnsureAssemblyBufferAllocatedSize(
			fAssemblyBufferUsedSize + kMaximumUtf8SequenceLength);
		if (result == B_OK) {
			char* insertPtr = &fAssemblyBuffer[fAssemblyBufferUsedSize];
			char* ptr = insertPtr;
			BUnicodeChar::ToUTF8(c, &ptr);
			size_t sequenceLength = static_cast<uint32>(ptr - insertPtr);
			fAssemblyBufferUsedSize += sequenceLength;
		}

		return result;
	}

private:

	/*!	This method will return the assembly buffer ensuring that it has at
		least `minimumSize` bytes available.
	*/

	/** @brief Ensures the buffer has at least @p minimumSize bytes allocated.
	 *
	 *  Requests are rounded up to kAssemblyBufferSizeIncrement boundaries while
	 *  below kRetainedAssemblyBufferSize to reduce the number of small reallocs.
	 *
	 *  @param minimumSize  Minimum number of bytes required.
	 *  @return B_OK on success, B_NO_MEMORY if reallocation fails.
	 */
	status_t _EnsureAssemblyBufferAllocatedSize(size_t minimumSize)
	{
		if (fAssemblyBufferAllocatedSize < minimumSize) {
			size_t requestedSize = minimumSize;

			// if the requested quantity of memory is less than the retained buffer size then
			// it makes sense to request a wee bit more in order to reduce the number of small
			// requests to increment the buffer over time.

			if (requestedSize < kRetainedAssemblyBufferSize - kAssemblyBufferSizeIncrement) {
				requestedSize = ((requestedSize / kAssemblyBufferSizeIncrement) + 1)
					* kAssemblyBufferSizeIncrement;
			}

			fAssemblyBuffer = (char*) realloc(fAssemblyBuffer, requestedSize);
			if (fAssemblyBuffer == NULL) {
				fAssemblyBufferAllocatedSize = 0;
				return B_NO_MEMORY;
			}
			fAssemblyBufferAllocatedSize = requestedSize;
		}
		return B_OK;
	}

private:
	char*					fAssemblyBuffer;
	size_t					fAssemblyBufferAllocatedSize;
	size_t					fAssemblyBufferUsedSize;
};


/** @brief RAII helper that calls Reset() on a JsonParseAssemblyBuffer when
 *         it goes out of scope, ensuring the buffer is recycled after each
 *         string is parsed.
 */
class JsonParseAssemblyBufferResetter {
public:
	/** @brief Constructs the resetter, taking ownership of the reference.
	 *  @param assemblyBuffer  The buffer to reset on destruction.
	 */
	JsonParseAssemblyBufferResetter(JsonParseAssemblyBuffer* assemblyBuffer)
		:
		fAssemblyBuffer(assemblyBuffer)
	{
	}

	/** @brief Resets the buffer. */
	~JsonParseAssemblyBufferResetter()
	{
		fAssemblyBuffer->Reset();
	}

private:
	JsonParseAssemblyBuffer*
							fAssemblyBuffer;
};


/*! This class carries state around the parsing process. */

/** @brief Holds all mutable state needed by the recursive-descent JSON parser.
 *
 *  Wraps the input BDataIO, the event listener, the current line counter, a
 *  single-character pushback slot, and the shared assembly buffer.
 */
class JsonParseContext {
public:
	/** @brief Constructs the parse context.
	 *  @param data      Source of raw JSON bytes.
	 *  @param listener  Receives parse events (string, number, object start, …).
	 */
	JsonParseContext(BDataIO* data, BJsonEventListener* listener)
		:
		fListener(listener),
		fData(data),
		fLineNumber(1), // 1 is the first line
		fPushbackChar(0),
		fHasPushbackChar(false),
		fAssemblyBuffer(new JsonParseAssemblyBuffer())
	{
	}


	/** @brief Destroys the context and frees the assembly buffer. */
	~JsonParseContext()
	{
		delete fAssemblyBuffer;
	}


	/** @brief Returns the associated event listener. */
	BJsonEventListener* Listener() const
	{
		return fListener;
	}


	/** @brief Returns the underlying data source. */
	BDataIO* Data() const
	{
		return fData;
	}


	/** @brief Returns the current 1-based line number. */
	int LineNumber() const
	{
		return fLineNumber;
	}


	/** @brief Increments the line counter (called on newline characters). */
	void IncrementLineNumber()
	{
		fLineNumber++;
	}

	/** @brief Reads the next byte from input, honouring the pushback slot.
	 *  @param buffer  Output parameter; receives the next character.
	 *  @return B_OK, B_PARTIAL_READ (EOF), or an I/O error code.
	 */
	status_t NextChar(char* buffer)
	{
		if (fHasPushbackChar) {
			buffer[0] = fPushbackChar;
			fHasPushbackChar = false;
			return B_OK;
		}

		return Data()->ReadExactly(buffer, 1);
	}

	/** @brief Pushes a character back so the next NextChar() call returns it.
	 *
	 *  Only one character may be pushed back at a time; a second call
	 *  triggers a debugger panic.
	 *
	 *  @param c  The character to push back.
	 */
	void PushbackChar(char c)
	{
		if (fHasPushbackChar)
			debugger("illegal state - more than one character pushed back");
		fPushbackChar = c;
		fHasPushbackChar = true;
	}


	/** @brief Returns the shared string assembly buffer. */
	JsonParseAssemblyBuffer* AssemblyBuffer()
	{
		return fAssemblyBuffer;
	}


private:
	BJsonEventListener*		fListener;
	BDataIO*				fData;
	uint32					fLineNumber;
	char					fPushbackChar;
	bool					fHasPushbackChar;
	JsonParseAssemblyBuffer*
							fAssemblyBuffer;
};


/** @brief Parses a JSON string from a BString and stores the result in @p message.
 *  @param JSON     The JSON text.
 *  @param message  Output BMessage populated with the parsed data.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BJson::Parse(const BString& JSON, BMessage& message)
{
	return Parse(JSON.String(), message);
}


/** @brief Parses JSON from a raw byte buffer of known length.
 *  @param JSON     Pointer to the JSON bytes.
 *  @param length   Number of bytes to read.
 *  @param message  Output BMessage populated with the parsed data.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BJson::Parse(const char* JSON, size_t length, BMessage& message)
{
	BMemoryIO* input = new BMemoryIO(JSON, length);
	ObjectDeleter<BMemoryIO> inputDeleter(input);
	BJsonMessageWriter* writer = new BJsonMessageWriter(message);
	ObjectDeleter<BJsonMessageWriter> writerDeleter(writer);

	Parse(input, writer);
	status_t result = writer->ErrorStatus();

	return result;
}


/** @brief Parses a NUL-terminated JSON string and stores the result in @p message.
 *  @param JSON     NUL-terminated JSON text.
 *  @param message  Output BMessage populated with the parsed data.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BJson::Parse(const char* JSON, BMessage& message)
{
	return Parse(JSON, strlen(JSON), message);
}


/*! The data is read as a stream of JSON data.  As the JSON is read, events are
    raised such as;
     - string
     - number
     - true
     - array start
     - object end
    Each event is sent to the listener to process as required.
*/

/** @brief Streaming JSON parse: reads from @p data and fires events at @p listener.
 *
 *  Reads the JSON payload incrementally; for each value, object boundary, or
 *  array boundary encountered the corresponding event is forwarded to the
 *  listener. Complete() is called on the listener when the stream ends.
 *
 *  @param data      Source of raw JSON bytes.
 *  @param listener  Receives parse events as the JSON is processed.
 */
void
BJson::Parse(BDataIO* data, BJsonEventListener* listener)
{
	JsonParseContext context(data, listener);
	ParseAny(context);
	listener->Complete();
}


// #pragma mark - Specific parse logic.


/** @brief Reads the next byte from the parse context, reporting errors to the listener.
 *
 *  Wraps JsonParseContext::NextChar() and translates B_PARTIAL_READ (EOF) and
 *  other I/O errors into listener error events.
 *
 *  @param jsonParseContext  Active parse context.
 *  @param c                Output parameter; receives the character read.
 *  @return true if a character was read successfully, false on any error.
 */
bool
BJson::NextChar(JsonParseContext& jsonParseContext, char* c)
{
	status_t result = jsonParseContext.NextChar(c);

	switch (result) {
		case B_OK:
			return true;

		case B_PARTIAL_READ:
		{
			jsonParseContext.Listener()->HandleError(B_BAD_DATA,
				jsonParseContext.LineNumber(), "unexpected end of input");
			return false;
		}

		default:
		{
			jsonParseContext.Listener()->HandleError(result, -1,
				"io related read error");
			return false;
		}
	}
}


/** @brief Reads bytes until a non-whitespace character is found.
 *
 *  Newline characters (0x0a and 0x0d) also increment the line counter.
 *
 *  @param jsonParseContext  Active parse context.
 *  @param c                Output parameter; receives the first non-whitespace byte.
 *  @return true on success, false if an I/O error or premature EOF is encountered.
 */
bool
BJson::NextNonWhitespaceChar(JsonParseContext& jsonParseContext, char* c)
{
	while (true) {
		if (!NextChar(jsonParseContext, c))
			return false;

		switch (*c) {
			case 0x0a: // newline
			case 0x0d: // cr
				jsonParseContext.IncrementLineNumber();
			case ' ': // space
					// swallow whitespace as it is not syntactically
					// significant.
				break;

			default:
				return true;
		}
	}
}


/** @brief Dispatches to the appropriate parser for a single JSON value.
 *
 *  Reads the first non-whitespace character and routes to ParseObject(),
 *  ParseArray(), ParseString(), ParseNumber(), or
 *  ParseExpectedVerbatimStringAndRaiseEvent() depending on the character.
 *
 *  @param jsonParseContext  Active parse context.
 *  @return true on success, false on parse or I/O error.
 */
bool
BJson::ParseAny(JsonParseContext& jsonParseContext)
{
	char c;

	if (!NextNonWhitespaceChar(jsonParseContext, &c))
		return false;

	switch (c) {
		case 'f': // [f]alse
			return ParseExpectedVerbatimStringAndRaiseEvent(
				jsonParseContext, "alse", 4, 'f', B_JSON_FALSE);

		case 't': // [t]rue
			return ParseExpectedVerbatimStringAndRaiseEvent(
				jsonParseContext, "rue", 3, 't', B_JSON_TRUE);

		case 'n': // [n]ull
			return ParseExpectedVerbatimStringAndRaiseEvent(
				jsonParseContext, "ull", 3, 'n', B_JSON_NULL);

		case '"':
			return ParseString(jsonParseContext, B_JSON_STRING);

		case '{':
			return ParseObject(jsonParseContext);

		case '[':
			return ParseArray(jsonParseContext);

		case '+':
		case '-':
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			jsonParseContext.PushbackChar(c); // keeps the parse simple
			return ParseNumber(jsonParseContext);

		default:
		{
			BString errorMessage;
			if (c >= 0x20 && c < 0x7f) {
				errorMessage.SetToFormat("unexpected character [%" B_PRIu8 "]"
					" (%c) when parsing element", static_cast<uint8>(c), c);
			} else {
				errorMessage.SetToFormat("unexpected character [%" B_PRIu8 "]"
					" when parsing element", (uint8) c);
			}
			jsonParseContext.Listener()->HandleError(B_BAD_DATA,
				jsonParseContext.LineNumber(), errorMessage.String());
			return false;
		}
	}

	return true;
}


/*! This method captures an object name, a separator ':' and then any value. */

/** @brief Parses a single JSON object name/value pair (e.g. "key" : value).
 *
 *  Reads the name string (raising B_JSON_OBJECT_NAME), expects a ':' separator,
 *  and then delegates to ParseAny() for the value.
 *
 *  @param jsonParseContext  Active parse context.
 *  @return true on success, false on parse or I/O error.
 */
bool
BJson::ParseObjectNameValuePair(JsonParseContext& jsonParseContext)
{
	bool didParseName = false;
	char c;

	while (true) {
		if (!NextNonWhitespaceChar(jsonParseContext, &c))
			return false;

		switch (c) {
			case '\"': // name of the object
			{
				if (!didParseName) {
					if (!ParseString(jsonParseContext, B_JSON_OBJECT_NAME))
						return false;

					didParseName = true;
				} else {
					jsonParseContext.Listener()->HandleError(B_BAD_DATA,
						jsonParseContext.LineNumber(), "unexpected"
							" [\"] character when parsing object name-"
							" value separator");
					return false;
				}
				break;
			}

			case ':': // separator
			{
				if (didParseName) {
					if (!ParseAny(jsonParseContext))
						return false;
					return true;
				} else {
					jsonParseContext.Listener()->HandleError(B_BAD_DATA,
						jsonParseContext.LineNumber(), "unexpected"
							" [:] character when parsing object name-"
							" value pair");
					return false;
				}
			}

			default:
			{
				BString errorMessage;
				errorMessage.SetToFormat(
					"unexpected character [%c] when parsing object"
					" name-value pair",
					c);
				jsonParseContext.Listener()->HandleError(B_BAD_DATA,
					jsonParseContext.LineNumber(), errorMessage.String());
				return false;
			}
		}
	}
}


/** @brief Parses a complete JSON object ('{' … '}'), firing start/end events.
 *
 *  Emits B_JSON_OBJECT_START, then calls ParseObjectNameValuePair() for each
 *  member, then emits B_JSON_OBJECT_END.
 *
 *  @param jsonParseContext  Active parse context.
 *  @return true on success, false on parse or I/O error.
 */
bool
BJson::ParseObject(JsonParseContext& jsonParseContext)
{
	if (!jsonParseContext.Listener()->Handle(
			BJsonEvent(B_JSON_OBJECT_START))) {
		return false;
	}

	char c;
	bool firstItem = true;

	while (true) {
		if (!NextNonWhitespaceChar(jsonParseContext, &c))
			return false;

		switch (c) {
			case '}': // terminate the object
			{
				if (!jsonParseContext.Listener()->Handle(
						BJsonEvent(B_JSON_OBJECT_END))) {
					return false;
				}
				return true;
			}

			case ',': // next value.
			{
				if (firstItem) {
					jsonParseContext.Listener()->HandleError(B_BAD_DATA,
						jsonParseContext.LineNumber(), "unexpected"
							" item separator when parsing start of"
							" object");
					return false;
				}

				if (!ParseObjectNameValuePair(jsonParseContext))
					return false;
				break;
			}

			default:
			{
				if (firstItem) {
					jsonParseContext.PushbackChar(c);
					if (!ParseObjectNameValuePair(jsonParseContext))
						return false;
					firstItem = false;
				} else {
					jsonParseContext.Listener()->HandleError(B_BAD_DATA,
						jsonParseContext.LineNumber(), "expected"
							" separator when parsing an object");
				}
			}
		}
	}

	return true;
}


/** @brief Parses a complete JSON array ('[' … ']'), firing start/end events.
 *
 *  Emits B_JSON_ARRAY_START, calls ParseAny() for each element, then emits
 *  B_JSON_ARRAY_END.
 *
 *  @param jsonParseContext  Active parse context.
 *  @return true on success, false on parse or I/O error.
 */
bool
BJson::ParseArray(JsonParseContext& jsonParseContext)
{
	if (!jsonParseContext.Listener()->Handle(
			BJsonEvent(B_JSON_ARRAY_START))) {
		return false;
	}

	char c;
	bool firstItem = true;

	while (true) {
		if (!NextNonWhitespaceChar(jsonParseContext, &c))
			return false;

		switch (c) {
			case ']': // terminate the array
			{
				if (!jsonParseContext.Listener()->Handle(
						BJsonEvent(B_JSON_ARRAY_END))) {
					return false;
				}
				return true;
			}

			case ',': // next value.
			{
				if (firstItem) {
					jsonParseContext.Listener()->HandleError(B_BAD_DATA,
						jsonParseContext.LineNumber(), "unexpected"
							" item separator when parsing start of"
							" array");
				}

				if (!ParseAny(jsonParseContext))
					return false;
				break;
			}

			default:
			{
				if (firstItem) {
					jsonParseContext.PushbackChar(c);
					if (!ParseAny(jsonParseContext))
						return false;
					firstItem = false;
				} else {
					jsonParseContext.Listener()->HandleError(B_BAD_DATA,
						jsonParseContext.LineNumber(), "expected"
							" separator when parsing an array");
				}
			}
		}
	}

	return true;
}


/** @brief Parses exactly four hex digits following a '\u' escape and appends
 *         the resulting Unicode code point (as UTF-8) to the assembly buffer.
 *
 *  @param jsonParseContext  Active parse context.
 *  @return true on success, false on malformed input or I/O error.
 */
bool
BJson::ParseEscapeUnicodeSequence(JsonParseContext& jsonParseContext)
{
	char ch;
	uint32 unicodeCh = 0;

	for (int i = 3; i >= 0; i--) {
		if (!NextChar(jsonParseContext, &ch)) {
			jsonParseContext.Listener()->HandleError(B_ERROR, jsonParseContext.LineNumber(),
				"unable to read unicode sequence");
			return false;
		}

		if (ch >= '0' && ch <= '9')
			unicodeCh |= static_cast<uint32>(ch - '0') << (i * 4);
		else if (ch >= 'a' && ch <= 'f')
			unicodeCh |= (10 + static_cast<uint32>(ch - 'a')) << (i * 4);
		else if (ch >= 'A' && ch <= 'F')
			unicodeCh |= (10 + static_cast<uint32>(ch - 'A')) << (i * 4);
		else {
			BString errorMessage;
			errorMessage.SetToFormat(
				"malformed hex character [%c] in unicode sequence in string parsing", ch);
			jsonParseContext.Listener()->HandleError(B_BAD_DATA, jsonParseContext.LineNumber(),
				errorMessage.String());
			return false;
		}
	}

	JsonParseAssemblyBuffer* assemblyBuffer = jsonParseContext.AssemblyBuffer();
	status_t result = assemblyBuffer->AppendUnicodeCharacter(unicodeCh);

	if (result != B_OK) {
		jsonParseContext.Listener()->HandleError(result, jsonParseContext.LineNumber(),
			"unable to store unicode char as utf-8");
		return false;
	}

	return true;
}


/** @brief Parses the character following a backslash inside a JSON string.
 *
 *  Handles the standard JSON escape sequences (\\n, \\r, \\b, \\f, \\\\,
 *  \\/, \\t, \\", and \\uXXXX) and appends the resulting byte(s) to the
 *  assembly buffer.
 *
 *  @param jsonParseContext  Active parse context.
 *  @return true on success, false on unrecognised escape or I/O error.
 */
bool
BJson::ParseStringEscapeSequence(JsonParseContext& jsonParseContext)
{
	char c;

	if (!NextChar(jsonParseContext, &c))
		return false;

	JsonParseAssemblyBuffer* assemblyBuffer = jsonParseContext.AssemblyBuffer();

	switch (c) {
		case 'n':
			assemblyBuffer->AppendCharacter('\n');
			break;
		case 'r':
			assemblyBuffer->AppendCharacter('\r');
			break;
		case 'b':
			assemblyBuffer->AppendCharacter('\b');
			break;
		case 'f':
			assemblyBuffer->AppendCharacter('\f');
			break;
		case '\\':
			assemblyBuffer->AppendCharacter('\\');
			break;
		case '/':
			assemblyBuffer->AppendCharacter('/');
			break;
		case 't':
			assemblyBuffer->AppendCharacter('\t');
			break;
		case '"':
			assemblyBuffer->AppendCharacter('"');
			break;
		case 'u':
		{
				// unicode escape sequence.
			if (!ParseEscapeUnicodeSequence(jsonParseContext)) {
				return false;
			}
			break;
		}
		default:
		{
			BString errorMessage;
			errorMessage.SetToFormat("unexpected escaped character [%c] in string parsing", c);
			jsonParseContext.Listener()->HandleError(B_BAD_DATA,
				jsonParseContext.LineNumber(), errorMessage.String());
			return false;
		}
	}

	return true;
}


/** @brief Reads a JSON quoted string and fires @p eventType with its content.
 *
 *  The opening '"' has already been consumed before this is called. Characters
 *  are accumulated in the assembly buffer until a closing '"' is found. Control
 *  characters (< 0x20) cause an error.
 *
 *  @param jsonParseContext  Active parse context.
 *  @param eventType        The event type to raise (B_JSON_STRING or
 *                          B_JSON_OBJECT_NAME).
 *  @return true on success, false on parse or I/O error.
 */
bool
BJson::ParseString(JsonParseContext& jsonParseContext,
	json_event_type eventType)
{
	char c;
	JsonParseAssemblyBuffer* assemblyBuffer = jsonParseContext.AssemblyBuffer();
	JsonParseAssemblyBufferResetter assembleBufferResetter(assemblyBuffer);

	while(true) {
		if (!NextChar(jsonParseContext, &c))
    		return false;

		switch (c) {
			case '"':
			{
					// terminates the string assembled so far.
				assemblyBuffer->AppendCharacter(0);
				jsonParseContext.Listener()->Handle(
					BJsonEvent(eventType, assemblyBuffer->Buffer()));
				return true;
			}

			case '\\':
			{
				if (!ParseStringEscapeSequence(jsonParseContext))
					return false;
				break;
			}

			default:
			{
				uint8 uc = static_cast<uint8>(c);

				if(uc < 0x20) { // control characters are not allowed
					BString errorMessage;
					errorMessage.SetToFormat("illegal control character"
						" [%" B_PRIu8 "] when parsing a string", uc);
					jsonParseContext.Listener()->HandleError(B_BAD_DATA,
						jsonParseContext.LineNumber(),
						errorMessage.String());
					return false;
				}

				assemblyBuffer->AppendCharacter(c);
				break;
			}
		}
	}
}


/** @brief Verifies a verbatim literal and, if correct, fires a JSON event.
 *
 *  Delegates to ParseExpectedVerbatimString(); if that succeeds the event
 *  @p jsonEventType is raised on the listener.
 *
 *  @param jsonParseContext    Active parse context.
 *  @param expectedString     The remaining characters expected (after the
 *                            leading character already consumed).
 *  @param expectedStringLength  Length of @p expectedString.
 *  @param leadingChar        The character that was already consumed (used in
 *                            error messages).
 *  @param jsonEventType      The event to raise on a successful match.
 *  @return true on success, false on mismatch or I/O error.
 */
bool
BJson::ParseExpectedVerbatimStringAndRaiseEvent(
	JsonParseContext& jsonParseContext, const char* expectedString,
	size_t expectedStringLength, char leadingChar,
	json_event_type jsonEventType)
{
	if (ParseExpectedVerbatimString(jsonParseContext, expectedString,
			expectedStringLength, leadingChar)) {
		if (!jsonParseContext.Listener()->Handle(BJsonEvent(jsonEventType)))
			return false;
	}

	return true;
}

/*! This will make sure that the constant string is available at the input. */

/** @brief Reads and verifies a literal string from the input stream.
 *
 *  Checks that each character in @p expectedString matches the next byte
 *  from the stream. Reports an error to the listener on mismatch.
 *
 *  @param jsonParseContext     Active parse context.
 *  @param expectedString      Characters expected at the current stream
 *                             position.
 *  @param expectedStringLength  Length of @p expectedString.
 *  @param leadingChar         Already-consumed leading character (for errors).
 *  @return true if all characters match, false otherwise.
 */
bool
BJson::ParseExpectedVerbatimString(JsonParseContext& jsonParseContext,
	const char* expectedString, size_t expectedStringLength, char leadingChar)
{
	char c;
	size_t offset = 0;

	while (offset < expectedStringLength) {
		if (!NextChar(jsonParseContext, &c))
			return false;

		if (c != expectedString[offset]) {
			BString errorMessage;
			errorMessage.SetToFormat("malformed json primative literal; "
				"expected [%c%s], but got [%c] at position %" B_PRIdSSIZE,
				leadingChar, expectedString, c, offset);
			jsonParseContext.Listener()->HandleError(B_BAD_DATA,
				jsonParseContext.LineNumber(), errorMessage.String());
			return false;
		}

		offset++;
	}

	return true;
}


/*! This function checks to see that the supplied string is a well formed
    JSON number.  It does this from a string rather than a stream for
    convenience.  This is not anticipated to impact performance because
    the string values are short.
*/

/** @brief Validates that @p value is a well-formed JSON number string.
 *
 *  Implements the JSON number grammar: an optional leading '-', integer
 *  digits, an optional fractional part, and an optional exponent.
 *
 *  @param value  NUL-terminated string to validate.
 *  @return true if @p value is a valid JSON number, false otherwise.
 */
bool
BJson::IsValidNumber(const char* value)
{
	int32 offset = 0;
	int32 len = strlen(value);

	if (offset < len && value[offset] == '-')
		offset++;

	if (offset >= len)
		return false;

	if (isdigit(value[offset]) && value[offset] != '0') {
		while (offset < len && isdigit(value[offset]))
			offset++;
	} else {
		if (value[offset] == '0')
			offset++;
		else
			return false;
	}

	if (offset < len && value[offset] == '.') {
		offset++;

		if (offset >= len)
			return false;

		while (offset < len && isdigit(value[offset]))
			offset++;
	}

	if (offset < len && (value[offset] == 'E' || value[offset] == 'e')) {
		offset++;

		if(offset < len && (value[offset] == '+' || value[offset] == '-'))
		 	offset++;

		if (offset >= len)
			return false;

		while (offset < len && isdigit(value[offset]))
			offset++;
	}

	return offset == len;
}


/*! Note that this method hits the 'NextChar' method on the context directly
    and handles any end-of-file state itself because it is feasible that the
    entire JSON payload is a number and because (unlike other structures, the
    number can take the end-of-file to signify the end of the number.
*/

/** @brief Reads a JSON number from the stream and fires a B_JSON_NUMBER event.
 *
 *  Accumulates numeric characters into the assembly buffer. EOF is a valid
 *  terminator for a top-level number value. The accumulated string is
 *  validated with IsValidNumber() before the event is raised.
 *
 *  @param jsonParseContext  Active parse context.
 *  @return true on success, false on malformed number or I/O error.
 */
bool
BJson::ParseNumber(JsonParseContext& jsonParseContext)
{
	JsonParseAssemblyBuffer* assemblyBuffer = jsonParseContext.AssemblyBuffer();
	JsonParseAssemblyBufferResetter assembleBufferResetter(assemblyBuffer);

	while (true) {
		char c;
		status_t result = jsonParseContext.NextChar(&c);

		switch (result) {
			case B_OK:
			{
				if (isdigit(c) || c == '.' || c == '-' || c == 'e' || c == 'E' || c == '+') {
					assemblyBuffer->AppendCharacter(c);
					break;
				}

				jsonParseContext.PushbackChar(c);
				// intentional fall through
			}
			case B_PARTIAL_READ:
			{
				errno = 0;
				assemblyBuffer->AppendCharacter(0);

				if (!IsValidNumber(assemblyBuffer->Buffer())) {
					jsonParseContext.Listener()->HandleError(B_BAD_DATA,
						jsonParseContext.LineNumber(), "malformed number");
					return false;
				}

				jsonParseContext.Listener()->Handle(BJsonEvent(B_JSON_NUMBER,
					assemblyBuffer->Buffer()));

				return true;
			}
			default:
			{
				jsonParseContext.Listener()->HandleError(result, -1,
					"io related read error");
				return false;
			}
		}
	}
}

} // namespace BPrivate
