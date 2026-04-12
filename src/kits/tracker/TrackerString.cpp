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
 *   Open Tracker License
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *   Distributed under the terms of the Be Sample Code License.
 */


/**
 * @file TrackerString.cpp
 * @brief Implementation of TrackerString, a BString extended with pattern matching.
 *
 * TrackerString adds glob, regular-expression, starts-with, ends-with, and
 * contains matching on top of BString.  The glob engine supports Unicode
 * multi-byte glyphs, bracket expressions (including ranges and negation), and
 * the standard * / ? wildcards.
 *
 * @see BString, TrackerStringExpressionType
 */


#include "TrackerString.h"

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>


//	#pragma mark - TrackerString


/**
 * @brief Default constructor; creates an empty TrackerString.
 */
TrackerString::TrackerString()
{
}


/**
 * @brief Construct a TrackerString from a C string.
 *
 * @param string  Null-terminated string to copy.
 */
TrackerString::TrackerString(const char* string)
	:
	BString(string)
{
}


/**
 * @brief Copy constructor.
 *
 * @param string  Source TrackerString to copy.
 */
TrackerString::TrackerString(const TrackerString &string)
	:
	BString(string)
{
}


/**
 * @brief Construct a TrackerString from a bounded C string.
 *
 * @param string     Source string to copy.
 * @param maxLength  Maximum number of bytes to copy from \a string.
 */
TrackerString::TrackerString(const char* string, int32 maxLength)
	:
	BString(string, maxLength)
{
}


/**
 * @brief Destructor.
 */
TrackerString::~TrackerString()
{
}


/**
 * @brief Test whether this string matches \a string under the given expression type.
 *
 * Dispatches to the appropriate comparison helper based on \a expressionType.
 *
 * @param string          The pattern or literal to match against.
 * @param caseSensitivity true for case-sensitive comparison.
 * @param expressionType  One of kNone, kStartsWith, kEndsWith, kContains,
 *                        kGlobMatch, or kRegexpMatch.
 * @return true if the string matches, false otherwise.
 */
bool
TrackerString::Matches(const char* string, bool caseSensitivity,
	TrackerStringExpressionType expressionType) const
{
	switch (expressionType) {
		default:
		case kNone:
			return false;

		case kStartsWith:
			return StartsWith(string, caseSensitivity);

		case kEndsWith:
			return EndsWith(string, caseSensitivity);

		case kContains:
			return Contains(string, caseSensitivity);

		case kGlobMatch:
			return MatchesGlob(string, caseSensitivity);

		case kRegexpMatch:
			return MatchesRegExp(string, caseSensitivity);
	}
}


/**
 * @brief Test whether this string matches the given regular expression.
 *
 * When \a caseSensitivity is false the pattern and text are both folded to
 * lower-case before matching.
 *
 * @param pattern         POSIX extended regular expression pattern.
 * @param caseSensitivity true for case-sensitive matching.
 * @return true if the expression matches, false otherwise.
 */
bool
TrackerString::MatchesRegExp(const char* pattern, bool caseSensitivity) const
{
	BString patternString(pattern);
	BString textString(String());

	if (caseSensitivity == false) {
		patternString.ToLower();
		textString.ToLower();
	}

	RegExp expression(patternString);

	if (expression.InitCheck() != B_OK)
		return false;

	return expression.Matches(textString);
}


/**
 * @brief Test whether this string matches a shell-style glob pattern.
 *
 * @param string          Glob pattern containing *, ?, and [] wildcards.
 * @param caseSensitivity true for case-sensitive matching.
 * @return true if the pattern matches the full string.
 */
bool
TrackerString::MatchesGlob(const char* string, bool caseSensitivity) const
{
	return StringMatchesPattern(String(), string, caseSensitivity);
}


/**
 * @brief Test whether this string ends with the given suffix.
 *
 * @param string          Suffix to search for.
 * @param caseSensitivity true for case-sensitive comparison.
 * @return true if this string ends with \a string.
 */
bool
TrackerString::EndsWith(const char* string, bool caseSensitivity) const
{
	// If "string" is longer than "this",
	// we should simply return false
	int32 position = Length() - (int32)strlen(string);
	if (position < 0)
		return false;

	if (caseSensitivity)
		return FindLast(string) == position;
	else
		return IFindLast(string) == position;
}


/**
 * @brief Test whether this string starts with the given prefix.
 *
 * @param string          Prefix to search for.
 * @param caseSensitivity true for case-sensitive comparison.
 * @return true if this string starts with \a string.
 */
bool
TrackerString::StartsWith(const char* string, bool caseSensitivity) const
{
	if (caseSensitivity)
		return FindFirst(string) == 0;
	else
		return IFindFirst(string) == 0;
}


/**
 * @brief Test whether this string contains the given substring.
 *
 * @param string          Substring to search for.
 * @param caseSensitivity true for case-sensitive comparison.
 * @return true if \a string appears anywhere in this string.
 */
bool
TrackerString::Contains(const char* string, bool caseSensitivity) const
{
	if (caseSensitivity)
		return FindFirst(string) > -1;
	else
		return IFindFirst(string) > -1;
}


/**
 * @brief Match a single character in \a string against a bracket expression.
 *
 * The caller must advance \a pattern past the leading '[' before calling
 * this method; encountering a '[' inside the expression is treated literally
 * (enabling '[[]' to match a literal '['). Supports character ranges,
 * Unicode glyphs, and negation via '^' or '!'.
 *
 * @param string          Pointer to the character being tested.
 * @param pattern         Pointer to the first character inside the '[...]'.
 * @param caseSensitivity true for case-sensitive matching.
 * @return true if the character at \a string matches the bracket expression.
 */
bool
TrackerString::MatchesBracketExpression(const char* string,
	const char* pattern, bool caseSensitivity) const
{
	bool GlyphMatch = IsStartOfGlyph(string[0]);

	if (IsInsideGlyph(string[0]))
		return false;

	char testChar = ConditionalToLower(string[0], caseSensitivity);
	bool match = false;

	bool inverse = *pattern == '^' || *pattern == '!';
		// We allow both ^ and ! as a initial inverting character.

	if (inverse)
		pattern++;

	while (!match && *pattern != ']' && *pattern != '\0') {
		switch (*pattern) {
			case '-':
			{
				char start = ConditionalToLower(*(pattern - 1),
					caseSensitivity);
				char stop = ConditionalToLower(*(pattern + 1),
					caseSensitivity);

				if (IsGlyph(start) || IsGlyph(stop))
					return false;
						// Not a valid range!

				if ((islower(start) && islower(stop))
					|| (isupper(start) && isupper(stop))
					|| (isdigit(start) && isdigit(stop))) {
					// Make sure 'start' and 'stop' are of the same type.
					match = start <= testChar && testChar <= stop;
				} else {
					// If no valid range, we've got a syntax error.
					return false;
				}

				break;
			}

			default:
				if (GlyphMatch)
					match = UTF8CharsAreEqual(string, pattern);
				else
					match = CharsAreEqual(testChar, *pattern, caseSensitivity);
				break;
		}

		if (!match) {
			pattern++;
			if (IsInsideGlyph(pattern[0]))
				pattern = MoveToEndOfGlyph(pattern);
		}
	}

	// Consider an unmatched bracket a failure
	// (i.e. when detecting a '\0' instead of a ']'.)
	if (*pattern == '\0')
		return false;

	return (match ^ inverse) != 0;
}


/**
 * @brief Core glob engine matching \a string against \a pattern.
 *
 * Handles ?, *, [bracket] wildcards with full Unicode glyph awareness.
 * Backtracking is performed via a fixed-depth stack (up to
 * kWildCardMaximum levels).
 *
 * @param string          The input string to match.
 * @param pattern         The glob pattern to match against.
 * @param caseSensitivity true for case-sensitive matching.
 * @return true if \a pattern matches \a string completely.
 */
bool
TrackerString::StringMatchesPattern(const char* string, const char* pattern,
	bool caseSensitivity) const
{
	// One could do this dynamically, counting the number of *'s,
	// but then you have to free them at every exit of this
	// function, which is awkward and ugly.
	const int32 kWildCardMaximum = 100;
	const char* pStorage[kWildCardMaximum];
	const char* sStorage[kWildCardMaximum];

	int32 patternLevel = 0;

	if (string == NULL || pattern == NULL)
		return false;

	while (*pattern != '\0') {
		switch (*pattern) {
			case '?':
				pattern++;
				string++;
				if (IsInsideGlyph(string[0]))
					string = MoveToEndOfGlyph(string);
				break;

			case '*':
			{
				// Collapse any ** and *? constructions:
				while (*pattern == '*' || *pattern == '?') {
					pattern++;
					if (*pattern == '?' && *string != '\0') {
						string++;
						if (IsInsideGlyph(string[0]))
							string = MoveToEndOfGlyph(string);
					}
				}

				if (*pattern == '\0') {
					// An ending * matches all strings.
					return true;
				}

				bool match = false;
				const char* pBefore = pattern - 1;

				if (*pattern == '[') {
					pattern++;

					while (!match && *string != '\0') {
						match = MatchesBracketExpression(string++, pattern,
							caseSensitivity);
					}

					while (*pattern != ']' && *pattern != '\0') {
						// Skip the rest of the bracket:
						pattern++;
					}

					if (*pattern == '\0') {
						// Failure if no closing bracket;
						return false;
					}
				} else {
					// No bracket, just one character:
					while (!match && *string != '\0') {
						if (IsGlyph(string[0]))
							match = UTF8CharsAreEqual(string++, pattern);
						else {
							match = CharsAreEqual(*string++, *pattern,
								caseSensitivity);
						}
					}
				}

				if (!match)
					return false;
				else {
					pStorage[patternLevel] = pBefore;
					if (IsInsideGlyph(string[0]))
						string = MoveToEndOfGlyph(string);

					sStorage[patternLevel++] = string;
					if (patternLevel > kWildCardMaximum)
						return false;

					pattern++;
					if (IsInsideGlyph(pattern[0]))
						pattern = MoveToEndOfGlyph(pattern);
				}
				break;
			}

			case '[':
				pattern++;

				if (!MatchesBracketExpression(string, pattern,
						caseSensitivity)) {
					if (patternLevel > 0) {
						pattern = pStorage[--patternLevel];
						string = sStorage[patternLevel];
					} else
						return false;
				} else {
					// Skip the rest of the bracket:
					while (*pattern != ']' && *pattern != '\0')
						pattern++;

					// Failure if no closing bracket;
					if (*pattern == '\0')
						return false;

					string++;
					if (IsInsideGlyph(string[0]))
						string = MoveToEndOfGlyph(string);
					pattern++;
				}
				break;

			default:
			{
				bool equal = false;
				if (IsGlyph(string[0]))
					equal = UTF8CharsAreEqual(string, pattern);
				else
					equal = CharsAreEqual(*string, *pattern, caseSensitivity);

				if (equal) {
					pattern++;
					if (IsInsideGlyph(pattern[0]))
						pattern = MoveToEndOfGlyph(pattern);
					string++;
					if (IsInsideGlyph(string[0]))
						string = MoveToEndOfGlyph(string);
				} else if (patternLevel > 0) {
						pattern = pStorage[--patternLevel];
						string = sStorage[patternLevel];
				} else
					return false;
				break;
			}
		}

		if (*pattern == '\0' && *string != '\0' && patternLevel > 0) {
			pattern = pStorage[--patternLevel];
			string = sStorage[patternLevel];
		}
	}

	return *string == '\0' && *pattern == '\0';
}


/**
 * @brief Compare the leading UTF-8 glyph in two strings for equality.
 *
 * Advances through the multi-byte sequence of both strings and returns
 * true only if every byte of the glyph is identical.
 *
 * @param string1  Pointer to the start of the first glyph.
 * @param string2  Pointer to the start of the second glyph.
 * @return true if both strings start with the same UTF-8 glyph.
 */
bool
TrackerString::UTF8CharsAreEqual(const char* string1,
	const char* string2) const
{
	const char* s1 = string1;
	const char* s2 = string2;

	if (IsStartOfGlyph(*s1) && *s1 == *s2) {
		s1++;
		s2++;

		while (IsInsideGlyph(*s1) && *s1 == *s2) {
			s1++;
			s2++;
		}

		return !IsInsideGlyph(*s1)
			&& !IsInsideGlyph(*s2) && *(s1 - 1) == *(s2 - 1);
	} else
		return false;
}


/**
 * @brief Advance a pointer past the continuation bytes of a UTF-8 glyph.
 *
 * @param string  Pointer to the first continuation byte inside a glyph.
 * @return Pointer to the first byte that is not a continuation byte.
 */
const char*
TrackerString::MoveToEndOfGlyph(const char* string) const
{
	const char* ptr = string;

	while (IsInsideGlyph(*ptr))
		ptr++;

	return ptr;
}


/**
 * @brief Return true if \a ch is part of a multi-byte UTF-8 glyph.
 *
 * @param ch  Byte to test.
 * @return true if the high bit of \a ch is set.
 */
bool
TrackerString::IsGlyph(char ch) const
{
	return (ch & 0x80) == 0x80;
}


/**
 * @brief Return true if \a ch is a UTF-8 continuation byte (10xxxxxx).
 *
 * @param ch  Byte to test.
 * @return true if the byte is a UTF-8 continuation byte.
 */
bool
TrackerString::IsInsideGlyph(char ch) const
{
	return (ch & 0xC0) == 0x80;
}


/**
 * @brief Return true if \a ch is the first byte of a multi-byte UTF-8 glyph (11xxxxxx).
 *
 * @param ch  Byte to test.
 * @return true if the byte is a UTF-8 leading byte.
 */
bool
TrackerString::IsStartOfGlyph(char ch) const
{
	return (ch & 0xC0) == 0xC0;
}
