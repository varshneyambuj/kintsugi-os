/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2009, Ingo Weinhold.
 */

/** @file X86AssemblyLanguage.h
    @brief SourceLanguage subclass attached to x86 disassembly listings. */

#ifndef X86_ASSEMBLY_LANGUAGE_H
#define X86_ASSEMBLY_LANGUAGE_H


#include "SourceLanguage.h"


/**
 * @brief SourceLanguage implementation that tags source as x86 disassembly.
 *
 * Provides only the display name; no syntax highlighter is currently
 * available for x86 assembly.
 */
class X86AssemblyLanguage : public SourceLanguage {
public:
								X86AssemblyLanguage();
	virtual						~X86AssemblyLanguage();

	virtual	const char*			Name() const;

	virtual	SyntaxHighlighter*	GetSyntaxHighlighter() const;
};


#endif	// X86_ASSEMBLY_LANGUAGE_H
