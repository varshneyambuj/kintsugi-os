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
 * @file BoolValueFormatter.cpp
 * @brief Formatter that prints BoolValue as the literal "true" or "false".
 *
 * The simplest formatter: there is no configuration to surface and no
 * validation path -- a checkbox/menu in the UI handles edits directly.
 *
 * @see BoolValue
 */


#include "BoolValueFormatter.h"

#include "BoolValue.h"


/**
 * @brief Trivial constructor; BoolValueFormatter is stateless.
 */
BoolValueFormatter::BoolValueFormatter()
	:
	ValueFormatter()
{
}


/**
 * @brief Trivial destructor.
 */
BoolValueFormatter::~BoolValueFormatter()
{
}


/**
 * @brief Renders @a _value as the literal "true" or "false".
 *
 * @param _value   The BoolValue to render.
 * @param _output  Receives "true" or "false".
 * @retval B_OK         On success.
 * @retval B_BAD_VALUE  When @a _value is not a BoolValue.
 */
status_t
BoolValueFormatter::FormatValue(Value* _value, BString& _output)
{
	BoolValue* value = dynamic_cast<BoolValue*>(_value);
	if (value == NULL)
		return B_BAD_VALUE;

	_output.SetTo(value->GetValue() ? "true" : "false");

	return B_OK;
}
