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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Value.cpp
 * @brief Implementation of Value, the reference-counted abstract holder for a debugger value.
 *
 * Value is the polymorphic container that stores the result of resolving a
 * variable's bytes against a known DWARF type. Concrete subclasses (BoolValue,
 * IntegerValue, FloatValue, AddressValue, EnumerationValue, StringValue) live
 * in values/.
 *
 * @see ValueNode, ValueFormatter
 */


#include "Value.h"


/**
 * @brief Destructor for the Value abstract base.
 *
 * Defined out-of-line so the vtable has a single home translation unit.
 */
Value::~Value()
{
}
