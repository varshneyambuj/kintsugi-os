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
 *   Copyright 2015, Rene Gollent, rene@gollent.com.
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file StringValueFormatter.cpp
 * @brief Formatter that wraps a Value's string form in quotes and escapes control characters.
 *
 * Converts a Value's textual representation into a C-style quoted literal:
 * control bytes below 0x20 are rendered as \\0, \\a, \\b, \\t, \\r, \\n, \\f or
 * \\xNN; double quotes are escaped as \\"; everything else is passed through
 * verbatim.
 *
 * @see Value, StringValue
 */


#include "StringValueFormatter.h"

#include <stdio.h>

#include <String.h>

#include "Value.h"


/**
 * @brief Trivial constructor; StringValueFormatter is stateless.
 */
StringValueFormatter::StringValueFormatter()
	:
	ValueFormatter()
{
}


/**
 * @brief Trivial destructor.
 */
StringValueFormatter::~StringValueFormatter()
{
}


/**
 * @brief Renders @a value as a C-style quoted string with escaped control bytes.
 *
 * @param value    Value supplying the string via Value::ToString().
 * @param _output  Receives the quoted, escaped representation.
 * @retval B_OK         On success.
 * @retval B_BAD_VALUE  When @a value cannot produce a string.
 */
status_t
StringValueFormatter::FormatValue(Value* value, BString& _output)
{
	_output = "\"";
	BString tempString;
	if (!value->ToString(tempString))
		return B_BAD_VALUE;

	for (int32 i = 0; i < tempString.Length(); i++) {
		if (tempString[i] < 31) {
			switch (tempString[i]) {
				case '\0':
					_output << "\\0";
					break;
				case '\a':
					_output << "\\a";
					break;
				case '\b':
					_output << "\\b";
					break;
				case '\t':
					_output << "\\t";
					break;
				case '\r':
					_output << "\\r";
					break;
				case '\n':
					_output << "\\n";
					break;
				case '\f':
					_output << "\\f";
					break;
				default:
				{
					char buffer[5];
					snprintf(buffer, sizeof(buffer), "\\x%x",
						tempString.String()[i]);
					_output << buffer;
					break;
				}
			}
		} else if (tempString[i] == '\"')
			_output << "\\\"";
		else
			_output << tempString[i];
	}

	_output += "\"";

	return B_OK;
}
