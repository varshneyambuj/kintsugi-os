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

/** @file SourceLanguageInfo.h
    @brief Per-language defaults (array order, lower bound) used during DWARF interpretation. */

#ifndef SOURCE_LANGUAGE_INFO_H
#define SOURCE_LANGUAGE_INFO_H

#include <SupportDefs.h>


/**
 * @brief Common per-language attributes consulted by the type evaluator.
 */
struct SourceLanguageInfo {
	const char*	name;
	uint8		arrayOrdering;
	uint64		subrangeLowerBound;
};


/** @brief Defaults applied when the language is unknown/unsupported. */
struct UnknownSourceLanguageInfo : SourceLanguageInfo {
								UnknownSourceLanguageInfo();
};


/** @brief Common defaults shared by all C-family languages. */
struct CFamilySourceLanguageInfo : SourceLanguageInfo {
								CFamilySourceLanguageInfo();
};


/** @brief Defaults for plain C. */
struct CSourceLanguageInfo : CFamilySourceLanguageInfo {
								CSourceLanguageInfo();
};


/** @brief Defaults for ISO C 1989. */
struct C89SourceLanguageInfo : CFamilySourceLanguageInfo {
								C89SourceLanguageInfo();
};


/** @brief Defaults for ISO C 1999. */
struct C99SourceLanguageInfo : CFamilySourceLanguageInfo {
								C99SourceLanguageInfo();
};


/** @brief Defaults for C++. */
struct CPlusPlusSourceLanguageInfo : CFamilySourceLanguageInfo {
								CPlusPlusSourceLanguageInfo();
};


/** @brief Singleton describing an unidentified language. */
extern const UnknownSourceLanguageInfo		kUnknownLanguageInfo;
/** @brief Singleton describing a known-but-unsupported language. */
extern const UnknownSourceLanguageInfo		kUnsupportedLanguageInfo;
/** @brief Singleton describing C. */
extern const CSourceLanguageInfo			kCLanguageInfo;
/** @brief Singleton describing ISO C 1989. */
extern const C89SourceLanguageInfo			kC89LanguageInfo;
/** @brief Singleton describing ISO C 1999. */
extern const C99SourceLanguageInfo			kC99LanguageInfo;
/** @brief Singleton describing C++. */
extern const CPlusPlusSourceLanguageInfo	kCPlusPlusLanguageInfo;


#endif	// SOURCE_LANGUAGE_INFO_H
