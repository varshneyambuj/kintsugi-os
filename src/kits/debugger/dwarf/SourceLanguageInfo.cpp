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
 * @file SourceLanguageInfo.cpp
 * @brief Per-language defaults consulted when interpreting DWARF type info.
 *
 * The DWARF DW_AT_language attribute identifies the source language of a
 * compilation unit; downstream code (notably array index ordering and
 * subrange default lower bound) depends on language conventions.  This
 * file defines small immutable singletons for the languages supported
 * by the debugger.
 */

#include "SourceLanguageInfo.h"

#include "Dwarf.h"


/**
 * @brief Defaults used when the source language cannot be identified.
 */
UnknownSourceLanguageInfo::UnknownSourceLanguageInfo()
{
	name = "unknown";
	arrayOrdering = DW_ORD_row_major;
	subrangeLowerBound = 0;
}


/**
 * @brief Defaults shared by C-family languages (C, C89, C99, C++).
 */
CFamilySourceLanguageInfo::CFamilySourceLanguageInfo()
{
	arrayOrdering = DW_ORD_row_major;
	subrangeLowerBound = 0;
}


/**
 * @brief Marks the language as plain "C" (DW_LANG_C).
 */
CSourceLanguageInfo::CSourceLanguageInfo()
{
	name = "C";
}


/**
 * @brief Marks the language as ISO C 1989 (DW_LANG_C89).
 */
C89SourceLanguageInfo::C89SourceLanguageInfo()
{
	name = "C 89";
}


/**
 * @brief Marks the language as ISO C 1999 (DW_LANG_C99).
 */
C99SourceLanguageInfo::C99SourceLanguageInfo()
{
	name = "C 99";
}


/**
 * @brief Marks the language as C++ (DW_LANG_C_plus_plus and variants).
 */
CPlusPlusSourceLanguageInfo::CPlusPlusSourceLanguageInfo()
{
	name = "C++";
}


/** @brief Singleton describing an entirely unknown language. */
const UnknownSourceLanguageInfo		kUnknownLanguageInfo;
/** @brief Singleton describing a recognised but unsupported language. */
const UnknownSourceLanguageInfo		kUnsupportedLanguageInfo;
/** @brief Singleton describing C. */
const CSourceLanguageInfo			kCLanguageInfo;
/** @brief Singleton describing ISO C 89. */
const C89SourceLanguageInfo			kC89LanguageInfo;
/** @brief Singleton describing ISO C 99. */
const C99SourceLanguageInfo			kC99LanguageInfo;
/** @brief Singleton describing C++. */
const CPlusPlusSourceLanguageInfo	kCPlusPlusLanguageInfo;
