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
 *   Copyright 2013, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2013, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold
 *       Rene Gollent
 */

/** @file RegExp.cpp
 *  @brief POSIX extended regular expression wrapper with optional shell-wildcard
 *         pattern conversion.  Provides reference-counted compiled expressions
 *         and match-result objects.
 */


#include <RegExp.h>

#include <new>

#include <regex.h>

#include <String.h>

#include <Referenceable.h>


// #pragma mark - RegExp::Data


/** @brief Reference-counted holder for a compiled POSIX regex_t.
 *
 *  When constructed with PATTERN_TYPE_WILDCARD the shell glob pattern is
 *  translated to a POSIX extended regular expression before compilation.
 *  On destruction the regex is freed if compilation succeeded.
 */
struct RegExp::Data : public BReferenceable {
	/** @brief Compiles a pattern into a POSIX extended regular expression.
	 *
	 *  If @p patternType is PATTERN_TYPE_WILDCARD the following translations
	 *  are applied before calling regcomp():
	 *   - '?' becomes '.'
	 *   - '*' becomes '.*'
	 *   - '[…]' bracket expressions are preserved, with '[.', '[=', '[:' sequences
	 *     replaced by '[.[.]' to avoid special POSIX collating-element syntax.
	 *   - '\\' introduces a quoted character.
	 *   - The metacharacters '^', '.', '$', '(', ')', '|', '+', '{' are escaped.
	 *
	 *  @param pattern       The pattern string (glob or POSIX ERE, depending on @p patternType).
	 *  @param patternType   PATTERN_TYPE_WILDCARD or PATTERN_TYPE_REGEXP.
	 *  @param caseSensitive When false REG_ICASE is added to the compile flags.
	 */
	Data(const char* pattern, PatternType patternType, bool caseSensitive)
		:
		BReferenceable()
	{
		// convert the shell pattern to a regular expression
		BString patternString;
		if (patternType == PATTERN_TYPE_WILDCARD) {
			while (*pattern != '\0') {
				char c = *pattern++;
				switch (c) {
					case '?':
						patternString += '.';
						continue;
					case '*':
						patternString += ".*";
						continue;
					case '[':
					{
						// find the matching ']' first
						const char* end = pattern;
						while (*end != ']') {
							if (*end++ == '\0') {
								fError = REG_EBRACK;
								return;
							}
						}

						if (pattern == end) {
							// Empty bracket expression. It will never match
							// anything. Strictly speaking this is not
							// considered an error, but we handle it like one.
							fError = REG_EBRACK;
							return;
						}

						patternString += '[';

						// We need to avoid "[." ... ".]", "[=" ... "=]", and
						// "[:" ... ":]" sequences, since those have special
						// meaning in regular expressions. If we encounter
						// a '[' followed by either of '.', '=', or ':', we
						// replace the '[' by "[.[.]".
						while (pattern < end) {
							c = *pattern++;
							if (c == '[' && pattern < end) {
								switch (*pattern) {
									case '.':
									case '=':
									case ':':
										patternString += "[.[.]";
										continue;
								}
							}
							patternString += c;
						}

						pattern++;
						patternString += ']';
						break;
					}

					case '\\':
					{
						// Quotes the next character. Works the same way for
						// regular expressions.
						if (*pattern == '\0') {
							fError = REG_EESCAPE;
							return;
						}

						patternString += '\\';
						patternString += *pattern++;
						break;
					}

					case '^':
					case '.':
					case '$':
					case '(':
					case ')':
					case '|':
					case '+':
					case '{':
						// need to be quoted
						patternString += '\\';
						// fall through
					default:
						patternString += c;
						break;
				}
			}

			pattern = patternString.String();
		}

		int flags = REG_EXTENDED;
		if (!caseSensitive)
			flags |= REG_ICASE;

		fError = regcomp(&fCompiledExpression, pattern, flags);
	}

	/** @brief Frees the compiled regex if compilation succeeded. */
	~Data()
	{
		if (fError == 0)
			regfree(&fCompiledExpression);
	}

	/** @brief Returns true if the pattern compiled without error.
	 *  @return true when the regex is ready to use.
	 */
	bool IsValid() const
	{
		return fError == 0;
	}

	/** @brief Returns a pointer to the compiled regex_t structure.
	 *  @return Pointer to the internal regex_t; valid only when IsValid() is true.
	 */
	const regex_t* CompiledExpression() const
	{
		return &fCompiledExpression;
	}

private:
	int		fError;
	regex_t	fCompiledExpression;
};


// #pragma mark - RegExp::MatchResultData


/** @brief Reference-counted holder for the results of a single regexec() call.
 *
 *  Stores the array of regmatch_t values (one per capture group plus the
 *  whole match). A NULL fMatches / zero fMatchCount indicates no match.
 */
struct RegExp::MatchResultData : public BReferenceable {
	/** @brief Runs the compiled expression against @p string and stores results.
	 *
	 *  Allocates an array of re_nsub + 1 regmatch_t structures and fills them
	 *  via regexec(). If the match fails the array is freed and fMatchCount is
	 *  set to zero.
	 *
	 *  @param compiledExpression  The compiled regex to execute.
	 *  @param string              The NUL-terminated input string to match against.
	 */
	MatchResultData(const regex_t* compiledExpression, const char* string)
		:
		BReferenceable(),
		fMatchCount(0),
		fMatches(NULL)
	{
		// fMatchCount is always set to the number of matching groups in the
		// expression (or 0 if an error occured). Some of the "matches" in
		// the array may still point to the (-1,-1) range if they don't
		// actually match anything.
		fMatchCount = compiledExpression->re_nsub + 1;
		fMatches = new regmatch_t[fMatchCount];
		if (regexec(compiledExpression, string, fMatchCount, fMatches, 0)
				!= 0) {
			delete[] fMatches;
			fMatches = NULL;
			fMatchCount = 0;
		}
	}

	/** @brief Frees the match array. */
	~MatchResultData()
	{
		delete[] fMatches;
	}

	/** @brief Returns the number of match entries (whole match + capture groups).
	 *  @return 0 when there is no match.
	 */
	size_t MatchCount() const
	{
		return fMatchCount;
	}

	/** @brief Returns a pointer to the raw regmatch_t array.
	 *  @return Pointer to the match array, or NULL when there is no match.
	 */
	const regmatch_t* Matches() const
	{
		return fMatches;
	}

private:
	size_t		fMatchCount;
	regmatch_t*	fMatches;
};


// #pragma mark - RegExp


/** @brief Constructs an uninitialised (invalid) RegExp. */
RegExp::RegExp()
	:
	fData(NULL)
{
}


/** @brief Constructs a RegExp and compiles @p pattern immediately.
 *
 *  @param pattern       The pattern string.
 *  @param patternType   PATTERN_TYPE_WILDCARD or PATTERN_TYPE_REGEXP.
 *  @param caseSensitive When false, matching is case-insensitive.
 */
RegExp::RegExp(const char* pattern, PatternType patternType,
	bool caseSensitive)
	:
	fData(NULL)
{
	SetPattern(pattern, patternType, caseSensitive);
}


/** @brief Copy-constructs a RegExp, sharing the compiled expression data.
 *  @param other  The source RegExp; its reference count is incremented.
 */
RegExp::RegExp(const RegExp& other)
	:
	fData(other.fData)
{
	if (fData != NULL)
		fData->AcquireReference();
}


/** @brief Destroys the RegExp, releasing its reference to the compiled data. */
RegExp::~RegExp()
{
	if (fData != NULL)
		fData->ReleaseReference();
}


/** @brief Compiles a new pattern, replacing any previously compiled one.
 *
 *  The old Data object is released (and freed if no other RegExp references
 *  it). On failure the RegExp is left invalid.
 *
 *  @param pattern       The new pattern string.
 *  @param patternType   PATTERN_TYPE_WILDCARD or PATTERN_TYPE_REGEXP.
 *  @param caseSensitive When false, matching is case-insensitive.
 *  @return true if compilation succeeded, false on allocation failure or
 *          invalid pattern.
 */
bool
RegExp::SetPattern(const char* pattern, PatternType patternType,
	bool caseSensitive)
{
	if (fData != NULL) {
		fData->ReleaseReference();
		fData = NULL;
	}

	Data* newData = new(std::nothrow) Data(pattern, patternType, caseSensitive);
	if (newData == NULL)
		return false;

	BReference<Data> dataReference(newData, true);
	if (!newData->IsValid())
		return false;

	fData = dataReference.Detach();
	return true;
}


/** @brief Matches this expression against @p string and returns the result.
 *
 *  Returns an invalid (empty) MatchResult if the RegExp is not valid.
 *
 *  @param string  NUL-terminated string to match against.
 *  @return A MatchResult; call HasMatched() to check for success.
 */
RegExp::MatchResult
RegExp::Match(const char* string) const
{
	if (!IsValid())
		return MatchResult();

	return MatchResult(
		new(std::nothrow) MatchResultData(fData->CompiledExpression(),
			string));
}


/** @brief Assigns another RegExp to this one, sharing compiled data.
 *
 *  The old reference is released and the new one is acquired.
 *
 *  @param other  The source RegExp.
 *  @return Reference to this RegExp.
 */
RegExp&
RegExp::operator=(const RegExp& other)
{
	if (fData != NULL)
		fData->ReleaseReference();

	fData = other.fData;

	if (fData != NULL)
		fData->AcquireReference();

	return *this;
}


// #pragma mark - RegExp::MatchResult


/** @brief Constructs an empty (no-match) MatchResult. */
RegExp::MatchResult::MatchResult()
	:
	fData(NULL)
{
}


/** @brief Constructs a MatchResult that takes ownership of @p data.
 *  @param data  Heap-allocated MatchResultData; may be NULL.
 */
RegExp::MatchResult::MatchResult(MatchResultData* data)
	:
	fData(data)
{
}


/** @brief Copy-constructs a MatchResult, sharing the underlying data.
 *  @param other  The source MatchResult; its reference count is incremented.
 */
RegExp::MatchResult::MatchResult(const MatchResult& other)
	:
	fData(other.fData)
{
	if (fData != NULL)
		fData->AcquireReference();
}


/** @brief Destroys the MatchResult, releasing its reference. */
RegExp::MatchResult::~MatchResult()
{
	if (fData != NULL)
		fData->ReleaseReference();
}


/** @brief Returns whether the match was successful.
 *  @return true if the expression matched and at least one group exists.
 */
bool
RegExp::MatchResult::HasMatched() const
{
	return fData != NULL && fData->MatchCount() > 0;
}


/** @brief Returns the byte offset of the start of the whole match.
 *  @return Start offset, or 0 if there is no match.
 */
size_t
RegExp::MatchResult::StartOffset() const
{
	return fData != NULL && fData->MatchCount() > 0
		? fData->Matches()[0].rm_so : 0;
}


/** @brief Returns the byte offset one past the end of the whole match.
 *  @return End offset, or 0 if there is no match.
 */
size_t
RegExp::MatchResult::EndOffset() const
{
	return fData != NULL && fData->MatchCount() > 0
		? fData->Matches()[0].rm_eo : 0;
}


/** @brief Returns the number of capture groups in the match result.
 *  @return Number of groups (total matches minus the whole-match entry),
 *          or 0 if there is no match.
 */
size_t
RegExp::MatchResult::GroupCount() const
{
	if (fData == NULL)
		return 0;

	size_t matchCount = fData->MatchCount();
	return matchCount > 0 ? matchCount - 1 : 0;
}


/** @brief Returns the start offset of a capture group.
 *  @param index  Zero-based capture group index.
 *  @return Start byte offset of the group, or 0 if the index is out of range.
 */
size_t
RegExp::MatchResult::GroupStartOffsetAt(size_t index) const
{
	return fData != NULL && fData->MatchCount() > index + 1
		? fData->Matches()[index + 1].rm_so : 0;
}


/** @brief Returns the end offset of a capture group.
 *  @param index  Zero-based capture group index.
 *  @return One-past-end byte offset of the group, or 0 if out of range.
 */
size_t
RegExp::MatchResult::GroupEndOffsetAt(size_t index) const
{
	return fData != NULL && fData->MatchCount() > index + 1
		? fData->Matches()[index + 1].rm_eo : 0;
}


/** @brief Assignment operator; shares the source's match data.
 *  @param other  The source MatchResult.
 *  @return Reference to this MatchResult.
 */
RegExp::MatchResult&
RegExp::MatchResult::operator=(const MatchResult& other)
{
	if (fData != NULL)
		fData->ReleaseReference();

	fData = other.fData;

	if (fData != NULL)
		fData->AcquireReference();

	return *this;
}
