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

/** @file CLanguageFamilySyntaxHighlightInfo.h
    @brief Per-document highlight cache produced by the C-family
           syntax highlighter. */

#ifndef C_LANGUAGE_FAMILY_SYNTAX_HIGHLIGHT_INFO_H
#define C_LANGUAGE_FAMILY_SYNTAX_HIGHLIGHT_INFO_H


#include "SyntaxHighlighter.h"

#include <ObjectList.h>


namespace CLanguage {
	struct Token;
	class Tokenizer;
}

class TeamTypeInformation;


/**
 * @brief Lazy SyntaxHighlightInfo that tokenises C/C++ source on demand
 *        and caches per-line highlight transitions.
 */
class CLanguageFamilySyntaxHighlightInfo : public SyntaxHighlightInfo {
public:
								CLanguageFamilySyntaxHighlightInfo(
									LineDataSource* source,
									CLanguage::Tokenizer* tokenizer,
									TeamTypeInformation* info);
	virtual						~CLanguageFamilySyntaxHighlightInfo();

	virtual	int32				GetLineHighlightRanges(int32 line,
									int32* _columns,
									syntax_highlight_type* _types,
									int32 maxCount);

private:
	class LineInfo;
	typedef BObjectList<LineInfo, true> LineInfoList;
	struct SyntaxPair;

private:
			status_t			_ParseLines();
			status_t			_ParseLine(int32 line,
									syntax_highlight_type& _lastType,
									LineInfo*& _info);
			syntax_highlight_type _MapTokenToSyntaxType(
									const CLanguage::Token& token);
private:
	LineDataSource*				fHighlightSource;
	CLanguage::Tokenizer*		fTokenizer;
	TeamTypeInformation*		fTypeInfo;
	LineInfoList				fLineInfos;
};


#endif	// C_LANGUAGE_FAMILY_SYNTAX_HIGHLIGHT_INFO_H
