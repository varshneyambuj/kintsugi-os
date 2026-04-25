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
 * @file TypeHandler.cpp
 * @brief Implementation of TypeHandler, the abstract base for DWARF type-to-display dispatchers.
 *
 * A TypeHandler answers two questions about a given debugger Type: how well
 * does it match (SupportsType) and which ValueNode subclass should render it
 * (CreateValueNode). Concrete subclasses live alongside in type_handlers/ and
 * are registered with the TypeHandlerRoster.
 *
 * @see TypeHandlerRoster, ValueNode
 */


#include "TypeHandler.h"


/**
 * @brief Destructor for the TypeHandler abstract base.
 *
 * Defined out-of-line so the vtable has a single home translation unit.
 */
TypeHandler::~TypeHandler()
{
}
