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
 *   Copyright 2012-2013, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file CppLanguage.cpp
 * @brief SourceLanguage tag identifying source files written in C++.
 *
 * CppLanguage shares all syntax highlighting and expression evaluation with
 * CLanguageFamily and only contributes the display name "C++".
 */


#include "CppLanguage.h"


/**
 * @brief Construct a CppLanguage instance.
 */
CppLanguage::CppLanguage()
{
}


/**
 * @brief Destructor.
 */
CppLanguage::~CppLanguage()
{
}


/**
 * @brief Returns the language's display name.
 *
 * @return The literal string @c "C++".
 */
const char*
CppLanguage::Name() const
{
	return "C++";
}
