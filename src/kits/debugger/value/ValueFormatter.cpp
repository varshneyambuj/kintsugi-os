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
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ValueFormatter.cpp
 * @brief Implementation of ValueFormatter, the abstract base for converting Values to display strings.
 *
 * A ValueFormatter renders a typed Value into a human-readable BString and,
 * optionally, validates and round-trips text the user types back into a new
 * Value. Concrete subclasses (Bool, Integer, Float, String, Enumeration) live
 * in value_formatters/.
 *
 * @see Value, ValueNode
 */


#include "ValueFormatter.h"


/**
 * @brief Destructor for the ValueFormatter abstract base.
 *
 * Defined out-of-line so the vtable has a single home translation unit.
 */
ValueFormatter::~ValueFormatter()
{
}


/**
 * @brief Reports whether this formatter can validate user-edited input.
 *
 * Default implementation returns false; subclasses that accept edits override
 * this to enable the inline-edit path in the variables view.
 *
 * @return true if ValidateFormattedValue() is meaningful, false otherwise.
 */
bool
ValueFormatter::SupportsValidation() const
{
	return false;
}


/**
 * @brief Validates a formatted text representation against a target type.
 *
 * Default implementation rejects all input; subclasses that opt into editing
 * override this to parse @a input and confirm it fits @a type.
 *
 * @param input  Formatted text the user typed.
 * @param type   Target type code the value must conform to.
 * @return true when @a input is acceptable for @a type.
 */
bool
ValueFormatter::ValidateFormattedValue(const BString& input, type_code type)
	const
{
	return false;
}


/**
 * @brief Constructs a Value object from formatted user input.
 *
 * Default implementation refuses; subclasses override to materialise a fresh
 * Value subclass populated from @a input.
 *
 * @param input    Formatted text supplied by the user.
 * @param type     Target type code for the produced value.
 * @param _output  Set to the freshly allocated Value on success.
 * @retval B_OK             On successful parse.
 * @retval B_NOT_SUPPORTED  When the formatter does not implement editing.
 */
status_t
ValueFormatter::GetValueFromFormattedInput(const BString& input,
	type_code type, Value*& _output) const
{
	return B_NOT_SUPPORTED;
}
