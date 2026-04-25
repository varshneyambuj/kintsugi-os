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
 * @file CLanguageFamilySyntaxHighlightInfo.cpp
 * @brief Per-document highlight cache produced by the C-family highlighter.
 *
 * Lazily tokenises every line on the first GetLineHighlightRanges() call,
 * stashing column/highlight-type pairs in a per-line LineInfo. Subsequent
 * queries are served from the cache. Identifiers are classified as
 * keywords, known type names (via TeamTypeInformation), or plain text;
 * comment-block state is carried across line boundaries so multi-line
 * slash-star block comments highlight correctly across line breaks.
 */


#include "CLanguageFamilySyntaxHighlightInfo.h"

#include <AutoDeleter.h>

#include "CLanguageTokenizer.h"
#include "LineDataSource.h"
#include "TeamTypeInformation.h"
#include "TypeLookupConstraints.h"


using namespace CLanguage;


/** @brief Sorted list of C/C++ keywords searched by binary search. */
static const char* kLanguageKeywords[] = {
	"NULL",
	"asm",
	"auto",
	"bool",
	"break",
	"case",
	"catch",
	"char",
	"class",
	"const",
	"const_cast",
	"constexpr",
	"continue",
	"default",
	"delete",
	"do",
	"double",
	"dynamic_cast",
	"else",
	"enum",
	"explicit",
	"extern",
	"false",
	"float",
	"for",
	"goto",
	"if",
	"inline",
	"int",
	"long",
	"mutable",
	"namespace",
	"new",
	"operator",
	"private",
	"protected",
	"public",
	"register",
	"reinterpret_cast",
	"return",
	"short",
	"signed",
	"sizeof",
	"static",
	"static_cast",
	"struct",
	"switch",
	"template",
	"this",
	"throw",
	"true",
	"try",
	"typedef",
	"typeid",
	"typename",
	"union",
	"unsigned",
	"using",
	"virtual",
	"void",
	"volatile",
	"while"
};


/**
 * @brief Returns @c true when @a token's text is a known C/C++ keyword.
 *
 * Performs a binary search against @c kLanguageKeywords.
 *
 * @param token  Identifier token to test.
 * @return @c true when the token text matches an entry in the keyword table.
 */
static bool IsLanguageKeyword(const Token& token)
{
	int lower = 0;
	int upper = (sizeof(kLanguageKeywords)/sizeof(char*)) - 1;

	while (lower < upper) {
		int mid = (lower + upper + 1) / 2;

		int cmp = token.string.Compare(kLanguageKeywords[mid]);
		if (cmp == 0)
			return true;
		else if (cmp < 0)
			upper = mid - 1;
		else
			lower = mid;
	}

	return token.string.Compare(kLanguageKeywords[lower]) == 0;
}


// #pragma mark - CLanguageFamilySyntaxHighlightInfo::SyntaxPair


/**
 * @brief Column-anchored highlight transition stored per line.
 *
 * Each pair records the column where a new highlight type starts; the type
 * remains in effect until the next pair (or end of line).
 */
struct CLanguageFamilySyntaxHighlightInfo::SyntaxPair {
	int32 column;
	syntax_highlight_type type;

	/**
	 * @brief Construct a SyntaxPair.
	 *
	 * @param column  Column at which the highlight type starts.
	 * @param type    Highlight type to apply from @a column onward.
	 */
	SyntaxPair(int32 column, syntax_highlight_type type)
		:
		column(column),
		type(type)
	{
	}
};


// #pragma mark - CLanguageFamilySyntaxHighlightInfo::LineInfo


/**
 * @brief Cached highlight transitions for a single source line.
 */
class CLanguageFamilySyntaxHighlightInfo::LineInfo {
public:
	/**
	 * @brief Construct an empty LineInfo for line @a line.
	 *
	 * @param line  Zero-based line index this object describes.
	 */
	LineInfo(int32 line)
		:
		fLine(line),
		fPairs(5)
	{
	}

	/** @brief Returns the number of recorded transitions. */
	inline int32 CountPairs() const
	{
		return fPairs.CountItems();
	}

	/**
	 * @brief Returns the transition at @a index.
	 *
	 * @param index  Zero-based index.
	 * @return Pointer to the pair or @c NULL when out of range.
	 */
	SyntaxPair* PairAt(int32 index) const
	{
		return fPairs.ItemAt(index);
	}

	/**
	 * @brief Appends a (column, type) transition.
	 *
	 * @param column  Column where @a type starts.
	 * @param type    Highlight type from @a column onward.
	 * @return @c true on success, @c false on allocation failure.
	 */
	bool AddPair(int32 column, syntax_highlight_type type)
	{
		SyntaxPair* pair = new(std::nothrow) SyntaxPair(column, type);
		if (pair == NULL)
			return false;

		ObjectDeleter<SyntaxPair> pairDeleter(pair);
		if (!fPairs.AddItem(pair))
			return false;

		pairDeleter.Detach();
		return true;
	}

private:
	typedef BObjectList<SyntaxPair, true> SyntaxPairList;

private:
	int32 fLine;
	SyntaxPairList fPairs;
};


// #pragma mark - CLanguageFamilySyntaxHighlightInfo;


/**
 * @brief Construct a highlight-info bound to a line data source.
 *
 * Acquires a reference on @a source and takes ownership of @a tokenizer.
 *
 * @param source     Line data source providing text to highlight.
 * @param tokenizer  Tokeniser used to lex each line; ownership transfers.
 * @param typeInfo   Type-information service used to recognise type names.
 */
CLanguageFamilySyntaxHighlightInfo::CLanguageFamilySyntaxHighlightInfo(
	LineDataSource* source, Tokenizer* tokenizer,
	TeamTypeInformation* typeInfo)
	:
	SyntaxHighlightInfo(),
	fHighlightSource(source),
	fTokenizer(tokenizer),
	fTypeInfo(typeInfo),
	fLineInfos(10)
{
	fHighlightSource->AcquireReference();
}


/**
 * @brief Releases the line data source reference and deletes the tokeniser.
 */
CLanguageFamilySyntaxHighlightInfo::~CLanguageFamilySyntaxHighlightInfo()
{
	fHighlightSource->ReleaseReference();
	delete fTokenizer;
}


/**
 * @brief Returns the highlight transitions for line @a line.
 *
 * On the first call the entire source is tokenised and cached; subsequent
 * calls are served from the cache. Up to @a maxCount transitions are
 * written into @a _columns and @a _types.
 *
 * @param line       Zero-based line index.
 * @param _columns   Out: receives the start columns of each transition.
 * @param _types     Out: receives the highlight type for each transition.
 * @param maxCount   Capacity of @a _columns / @a _types.
 * @return Number of transitions written. The last entry's column starts
 *         the highlight that runs to end-of-line.
 */
int32
CLanguageFamilySyntaxHighlightInfo::GetLineHighlightRanges(int32 line,
	int32* _columns, syntax_highlight_type* _types, int32 maxCount)
{
	if (line >= fHighlightSource->CountLines())
		return 0;

	// lazily parse the source's highlight information the first time
	// it's actually requested. Subsequently it's cached for quick retrieval.
	if (fLineInfos.CountItems() == 0) {
		if (_ParseLines() != B_OK)
			return 0;
	}

	LineInfo* info = fLineInfos.ItemAt(line);
	if (info == NULL)
		return 0;

	int32 count = 0;
	for (; count < info->CountPairs(); count++) {
		if (count == maxCount - 1)
			break;

		SyntaxPair* pair = info->PairAt(count);
		if (pair == NULL)
			break;

		_columns[count] = pair->column;
		_types[count] = pair->type;
	}

	return count;
}


/**
 * @brief Tokenises every line and caches per-line highlight transitions.
 *
 * Carries the running highlight type across line boundaries so multi-line
 * comment blocks remain coloured.
 *
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  When a line's LineInfo cannot be appended.
 * @return Otherwise the first _ParseLine() error encountered.
 */
status_t
CLanguageFamilySyntaxHighlightInfo::_ParseLines()
{
	syntax_highlight_type type = SYNTAX_HIGHLIGHT_NONE;

	for (int32 i = 0; i < fHighlightSource->CountLines(); i++) {
		const char* line = fHighlightSource->LineAt(i);
		fTokenizer->SetTo(line);
		LineInfo* info = NULL;

		status_t error = _ParseLine(i, type, info);
		if (error != B_OK)
			return error;

		ObjectDeleter<LineInfo> infoDeleter(info);
		if (!fLineInfos.AddItem(info))
			return B_NO_MEMORY;

		infoDeleter.Detach();
	}

	return B_OK;
}


/**
 * @brief Tokenises a single line, recording highlight transitions.
 *
 * Tracks block-comment and preprocessor state, and emits a SyntaxPair
 * whenever the highlight type would change. Tokeniser exceptions are
 * swallowed: the partial result is preferred over nothing.
 *
 * @param line       Zero-based line index being parsed.
 * @param _lastType  In/out: the current highlight type carried across
 *                   adjacent lines (e.g. inside a comment block).
 * @param _info      Out: receives the freshly allocated LineInfo. Caller
 *                   takes ownership through the returned status.
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  When a transition could not be recorded.
 */
status_t
CLanguageFamilySyntaxHighlightInfo::_ParseLine(int32 line,
	syntax_highlight_type& _lastType, LineInfo*& _info)
{
	bool inCommentBlock = (_lastType == SYNTAX_HIGHLIGHT_COMMENT);
	bool inPreprocessor = false;

	_info = new(std::nothrow) LineInfo(line);
	if (_info == NULL)
		return B_NO_MEMORY;
	ObjectDeleter<LineInfo> infoDeleter(_info);
	if (inCommentBlock) {
		if (!_info->AddPair(0, SYNTAX_HIGHLIGHT_COMMENT))
			return B_NO_MEMORY;
	}

	try {
		for (;;) {
			const Token& token = fTokenizer->NextToken();
			if (token.type == TOKEN_END_OF_LINE)
				break;

			if (inCommentBlock) {
				if (token.type == TOKEN_END_COMMENT_BLOCK)
					inCommentBlock = false;
				continue;
			} else if (inPreprocessor) {
				fTokenizer->NextToken();
				inPreprocessor = false;
			} else if (token.type == TOKEN_INLINE_COMMENT) {
				if (!_info->AddPair(token.position, SYNTAX_HIGHLIGHT_COMMENT))
					return B_NO_MEMORY;
				break;
			}

			syntax_highlight_type current = _MapTokenToSyntaxType(token);
			if (_lastType == current)
				continue;

			_lastType = current;
			if (!_info->AddPair(token.position, current))
				return B_NO_MEMORY;

			if (token.type == TOKEN_BEGIN_COMMENT_BLOCK)
				inCommentBlock = true;
			else if (token.type == TOKEN_POUND)
				inPreprocessor = true;
		}
	} catch (...) {
		// if a parse exception was thrown, simply ignore it.
		// in such a case, we can't guarantee correct highlight
		// information anyhow, so simply return whatever we started
		// with.
	}

	_lastType = inCommentBlock
		? SYNTAX_HIGHLIGHT_COMMENT : SYNTAX_HIGHLIGHT_NONE;
	infoDeleter.Detach();
	return B_OK;
}


/**
 * @brief Maps a tokeniser token to the matching highlight type.
 *
 * Identifiers are first checked against the keyword list, then against
 * known type names registered with TeamTypeInformation. Operators,
 * literals, comments, and the preprocessor pound sign each map to their
 * own highlight type.
 *
 * @param token  Token whose syntactic role is being classified.
 * @return The corresponding @c syntax_highlight_type, or
 *         @c SYNTAX_HIGHLIGHT_NONE for tokens with no special role.
 */
syntax_highlight_type
CLanguageFamilySyntaxHighlightInfo::_MapTokenToSyntaxType(const Token& token)
{
	static TypeLookupConstraints constraints;

	switch (token.type) {
		case TOKEN_IDENTIFIER:
			if (IsLanguageKeyword(token))
				return SYNTAX_HIGHLIGHT_KEYWORD;
			else if (fTypeInfo->TypeExistsByName(token.string, constraints))
				return SYNTAX_HIGHLIGHT_TYPE;
			break;

		case TOKEN_CONSTANT:
			return SYNTAX_HIGHLIGHT_NUMERIC_LITERAL;

		case TOKEN_END_OF_LINE:
			break;

		case TOKEN_PLUS:
		case TOKEN_MINUS:
		case TOKEN_STAR:
		case TOKEN_SLASH:
		case TOKEN_MODULO:
		case TOKEN_OPENING_PAREN:
		case TOKEN_CLOSING_PAREN:
		case TOKEN_OPENING_SQUARE_BRACKET:
		case TOKEN_CLOSING_SQUARE_BRACKET:
		case TOKEN_OPENING_CURLY_BRACE:
		case TOKEN_CLOSING_CURLY_BRACE:
		case TOKEN_LOGICAL_AND:
		case TOKEN_LOGICAL_OR:
		case TOKEN_LOGICAL_NOT:
		case TOKEN_BITWISE_AND:
		case TOKEN_BITWISE_OR:
		case TOKEN_BITWISE_NOT:
		case TOKEN_BITWISE_XOR:
		case TOKEN_EQ:
		case TOKEN_NE:
		case TOKEN_GT:
		case TOKEN_GE:
		case TOKEN_LT:
		case TOKEN_LE:
		case TOKEN_MEMBER_PTR:
		case TOKEN_CONDITION:
		case TOKEN_COLON:
		case TOKEN_SEMICOLON:
		case TOKEN_BACKSLASH:
			return SYNTAX_HIGHLIGHT_OPERATOR;

		case TOKEN_STRING_LITERAL:
			return SYNTAX_HIGHLIGHT_STRING_LITERAL;

		case TOKEN_POUND:
			return SYNTAX_HIGHLIGHT_PREPROCESSOR_KEYWORD;

		case TOKEN_BEGIN_COMMENT_BLOCK:
		case TOKEN_END_COMMENT_BLOCK:
		case TOKEN_INLINE_COMMENT:
			return SYNTAX_HIGHLIGHT_COMMENT;
	}

	return SYNTAX_HIGHLIGHT_NONE;
}
