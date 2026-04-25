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
 * MIT License. Copyright 2014, Rene Gollent.
 */

/** @file CLanguageFamilySyntaxHighlighter.h
    @brief SyntaxHighlighter producing CLanguageFamilySyntaxHighlightInfo
           caches for C/C++ source documents. */

#ifndef C_LANGUAGE_FAMILY_SYNTAX_HIGHLIGHTER_H
#define C_LANGUAGE_FAMILY_SYNTAX_HIGHLIGHTER_H


#include "SyntaxHighlighter.h"


/**
 * @brief Concrete SyntaxHighlighter for the C/C++ language family.
 *
 * ParseText() pairs a CLanguageTokenizer with a per-document
 * CLanguageFamilySyntaxHighlightInfo cache.
 */
class CLanguageFamilySyntaxHighlighter : public SyntaxHighlighter {
public:
								CLanguageFamilySyntaxHighlighter();
	virtual						~CLanguageFamilySyntaxHighlighter();

	virtual	status_t			ParseText(LineDataSource* source,
									TeamTypeInformation* typeInfo,
									SyntaxHighlightInfo*& _info);
										// caller owns the returned info
};


#endif	// C_LANGUAGE_FAMILY_SYNTAX_HIGHLIGHTER_H
