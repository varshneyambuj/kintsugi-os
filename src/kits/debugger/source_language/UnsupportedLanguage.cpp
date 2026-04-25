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
 * @file UnsupportedLanguage.cpp
 * @brief Fallback SourceLanguage implementation for unknown languages.
 *
 * Used when the debugger encounters debug info whose language identifier is
 * not recognised. Reports the name "unsupported" and inherits the default
 * SourceLanguage behaviour (no highlighter, expression evaluation refused).
 */


#include "UnsupportedLanguage.h"


/**
 * @brief Returns the language's display name.
 *
 * @return The literal string @c "unsupported".
 */
const char*
UnsupportedLanguage::Name() const
{
	return "unsupported";
}
