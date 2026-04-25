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
 *   Copyright 2013, Rene Gollent, rene@gollent.com.
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file CLanguage.cpp
 * @brief SourceLanguage tag identifying source files written in C.
 *
 * CLanguage inherits all of CLanguageFamily's syntax highlighting and
 * expression-evaluation behaviour and only contributes a name string used
 * by the UI when reporting the source language.
 */


#include "CLanguage.h"


/**
 * @brief Construct a CLanguage instance.
 */
CLanguage::CLanguage()
{
}


/**
 * @brief Destructor.
 */
CLanguage::~CLanguage()
{
}


/**
 * @brief Returns the language's display name.
 *
 * @return The literal string @c "C".
 */
const char*
CLanguage::Name() const
{
	return "C";
}
