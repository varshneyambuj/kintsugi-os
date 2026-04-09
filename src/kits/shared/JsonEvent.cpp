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
 */

/** @file JsonEvent.cpp
 *  @brief Implements \c BJsonEvent, a value-type representing a single event
 *         produced by the JSON streaming parser. Each event carries a type
 *         (e.g. \c B_JSON_STRING, \c B_JSON_NUMBER) and optional string
 *         content that may be owned or borrowed.
 */

#include "JsonEvent.h"

#include <stdlib.h>
#include <stdio.h>

#include <String.h>


/**
 * @brief Constructs a \c BJsonEvent with an explicit type and borrowed string
 *        content.
 *
 * The caller retains ownership of \a content; the event stores only a
 * pointer to it and will not free it.
 *
 * @param eventType The JSON event type (e.g. \c B_JSON_STRING,
 *                  \c B_JSON_OBJECT_START).
 * @param content   A \c NUL-terminated string associated with this event, or
 *                  \c NULL if not applicable.
 */
BJsonEvent::BJsonEvent(json_event_type eventType, const char* content)
	:
	fEventType(eventType),
	fContent(content),
	fOwnedContent(NULL)
{
}


/**
 * @brief Constructs a \c B_JSON_STRING event with borrowed string content.
 *
 * Convenience constructor that infers the event type as \c B_JSON_STRING.
 *
 * @param content A \c NUL-terminated string value; ownership is not taken.
 */
BJsonEvent::BJsonEvent(const char* content)
	:
	fEventType(B_JSON_STRING),
	fContent(content),
	fOwnedContent(NULL)
{
}


/**
 * @brief Constructs a \c B_JSON_NUMBER event from a \c double value.
 *
 * Formats \a content using \c snprintf() with \c "%f" and stores the result
 * in a heap-allocated buffer that is owned by this event. Terminates the
 * process if memory allocation fails.
 *
 * @param content The numeric value to represent.
 */
BJsonEvent::BJsonEvent(double content) {
	fEventType = B_JSON_NUMBER;
	fContent = NULL;

	int actualLength = snprintf(0, 0, "%f", content) + 1;
	char* buffer = (char*) malloc(sizeof(char) * actualLength);

	if (buffer == NULL) {
		fprintf(stderr, "memory exhaustion\n");
			// given the risk, this is the only sensible thing to do here.
		exit(EXIT_FAILURE);
	}

	sprintf(buffer, "%f", content);
	fOwnedContent = buffer;
}


/**
 * @brief Constructs a \c B_JSON_NUMBER event from an \c int64 value.
 *
 * For the common values \c 0 and \c 1, static string literals are used to
 * avoid allocation. All other values are formatted with \c B_PRId64 into a
 * heap-allocated buffer owned by this event. Terminates the process if
 * memory allocation fails.
 *
 * @param content The integer value to represent.
 */
BJsonEvent::BJsonEvent(int64 content) {
	fEventType = B_JSON_NUMBER;
	fContent = NULL;
	fOwnedContent = NULL;

	static const char* zeroValue = "0";
	static const char* oneValue = "1";

	switch (content) {
		case 0:
			fContent = zeroValue;
			break;
		case 1:
			fContent = oneValue;
			break;
		default:
		{
			int actualLength = snprintf(0, 0, "%" B_PRId64, content) + 1;
			char* buffer = (char*) malloc(sizeof(char) * actualLength);

			if (buffer == NULL) {
				fprintf(stderr, "memory exhaustion\n");
					// given the risk, this is the only sensible thing to do
					// here.
				exit(EXIT_FAILURE);
			}

			sprintf(buffer, "%" B_PRId64, content);
			fOwnedContent = buffer;
			break;
		}
	}
}


/**
 * @brief Constructs a content-free \c BJsonEvent with just a type.
 *
 * Useful for structural events such as \c B_JSON_OBJECT_START,
 * \c B_JSON_ARRAY_END, etc., which carry no associated value.
 *
 * @param eventType The JSON event type.
 */
BJsonEvent::BJsonEvent(json_event_type eventType)
	:
	fEventType(eventType),
	fContent(NULL),
	fOwnedContent(NULL)
{
}


/**
 * @brief Destructor. Frees any heap-allocated content buffer.
 */
BJsonEvent::~BJsonEvent()
{
	if (NULL != fOwnedContent)
		free(fOwnedContent);
}


/**
 * @brief Returns the type of this JSON event.
 *
 * @return A \c json_event_type constant identifying the kind of JSON token
 *         this event represents.
 */
json_event_type
BJsonEvent::EventType() const
{
	return fEventType;
}


/**
 * @brief Returns the raw string content associated with this event.
 *
 * If the event owns its content buffer (allocated in the constructor for
 * numeric types) that buffer is returned; otherwise the borrowed pointer
 * supplied at construction time is returned. May return \c NULL for
 * structural events that carry no value.
 *
 * @return A \c NUL-terminated C string, or \c NULL.
 */
const char*
BJsonEvent::Content() const
{
	if (NULL != fOwnedContent)
		return fOwnedContent;
	return fContent;
}


/**
 * @brief Parses and returns the string content as a \c double.
 *
 * Delegates to \c strtod(). Behaviour is undefined if \c Content() does not
 * represent a valid floating-point number.
 *
 * @return The content converted to \c double.
 */
double
BJsonEvent::ContentDouble() const
{
	return strtod(Content(), NULL);
}


/**
 * @brief Parses and returns the string content as a signed 64-bit integer.
 *
 * Delegates to \c strtoll() with base 10. Behaviour is undefined if
 * \c Content() does not represent a valid integer.
 *
 * @return The content converted to \c int64.
 */
int64
BJsonEvent::ContentInteger() const
{
	return strtoll(Content(), NULL, 10);
}
