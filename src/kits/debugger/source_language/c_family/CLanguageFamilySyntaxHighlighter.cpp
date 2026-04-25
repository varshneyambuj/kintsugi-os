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
 *   Copyright 2014, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file CLanguageFamilySyntaxHighlighter.cpp
 * @brief Factory that pairs a CLanguageTokenizer with a highlight-info
 *        cache for a given source document.
 */


#include "CLanguageFamilySyntaxHighlighter.h"

#include <new>

#include <AutoDeleter.h>

#include "CLanguageFamilySyntaxHighlightInfo.h"
#include "CLanguageTokenizer.h"


using CLanguage::Tokenizer;


/**
 * @brief Construct a C-family syntax highlighter.
 */
CLanguageFamilySyntaxHighlighter::CLanguageFamilySyntaxHighlighter()
	:
	SyntaxHighlighter()
{
}


/**
 * @brief Destructor.
 */
CLanguageFamilySyntaxHighlighter::~CLanguageFamilySyntaxHighlighter()
{
}


/**
 * @brief Builds a SyntaxHighlightInfo for the supplied source document.
 *
 * Allocates a Tokenizer and pairs it with a CLanguageFamilySyntaxHighlightInfo
 * keyed on the line data source. On success the caller takes ownership of
 * @a _info; on failure no objects leak.
 *
 * @param source     Line data source to be highlighted.
 * @param typeInfo   Type-information service for type-name highlighting.
 * @param _info      Out: receives the freshly allocated highlight info.
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  When either the tokeniser or the info cannot be
 *                      allocated.
 */
status_t
CLanguageFamilySyntaxHighlighter::ParseText(LineDataSource* source,
	TeamTypeInformation* typeInfo, SyntaxHighlightInfo*& _info)
{
	Tokenizer* tokenizer = new(std::nothrow) Tokenizer();
	if (tokenizer == NULL)
		return B_NO_MEMORY;
	ObjectDeleter<Tokenizer> deleter(tokenizer);

	_info = new(std::nothrow) CLanguageFamilySyntaxHighlightInfo(source,
		tokenizer, typeInfo);
	if (_info == NULL)
		return B_NO_MEMORY;

	deleter.Detach();
	return B_OK;
}
