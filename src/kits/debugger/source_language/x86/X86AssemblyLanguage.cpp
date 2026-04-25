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
 * @file X86AssemblyLanguage.cpp
 * @brief SourceLanguage implementation for x86 disassembly listings.
 *
 * X86AssemblyLanguage is the SourceLanguage attached to functions whose
 * source view shows architecture-level disassembly. It currently only
 * contributes the language name; no syntax highlighter is supplied, so
 * disassembly is rendered un-coloured.
 */


#include "X86AssemblyLanguage.h"


/**
 * @brief Construct an X86AssemblyLanguage instance.
 */
X86AssemblyLanguage::X86AssemblyLanguage()
{
}


/**
 * @brief Destructor.
 */
X86AssemblyLanguage::~X86AssemblyLanguage()
{
}


/**
 * @brief Returns the language's display name.
 *
 * @return The literal string @c "x86 assembly".
 */
const char*
X86AssemblyLanguage::Name() const
{
	return "x86 assembly";
}


/**
 * @brief Returns the syntax highlighter for x86 disassembly.
 *
 * @return Always @c NULL; x86 disassembly highlighting is not yet
 *         implemented.
 */
SyntaxHighlighter*
X86AssemblyLanguage::GetSyntaxHighlighter() const
{
	// none available yet
	return NULL;
}
