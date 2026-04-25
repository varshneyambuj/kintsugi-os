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
 *   Copyright 2012, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/** @file ReturnValueID.cpp
    @brief Out-of-line destructor anchor for the ReturnValueID identity type. */


#include "ReturnValueID.h"


/** @brief Virtual destructor anchor; subclasses provide the actual identity payload. */
ReturnValueID::~ReturnValueID()
{
}

