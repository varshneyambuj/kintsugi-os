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
 *   Copyright 2003, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 *       Siarzhuk Zharski, zharik@gmx.li
 */


/**
 * @file UnicodeChar.cpp
 * @brief Implementation of BUnicodeChar, the Unicode code-point utility class.
 *
 * BUnicodeChar is a thin wrapper over the ICU uchar.h and utf8.h APIs,
 * exposing Unicode character classification (alpha, digit, space, etc.),
 * case conversion, East Asian width queries, and UTF-8 encode/decode
 * functions. All methods are static; the class has no instance state.
 *
 * @see BCollator, BFormattingConventions
 */


#include <UnicodeChar.h>

#include <unicode/uchar.h>
#include <unicode/utf8.h>


/**
 * @brief Construct a BUnicodeChar (unused; all methods are static).
 */
BUnicodeChar::BUnicodeChar()
{
}


// Returns the general category value for the code point.
/**
 * @brief Return the Unicode general category for the given code point.
 *
 * @param c  Unicode code point.
 * @return ICU general category constant (e.g. U_LOWERCASE_LETTER).
 */
int8
BUnicodeChar::Type(uint32 c)
{
	return u_charType(c);
}


// Determines whether the specified code point is a letter character.
// True for general categories "L" (letters).
/**
 * @brief Return true if the code point belongs to the "L" (letter) category.
 *
 * @param c  Unicode code point.
 * @return true if \a c is a letter.
 */
bool
BUnicodeChar::IsAlpha(uint32 c)
{
	return u_isalpha(c);
}


// Determines whether the specified code point is an alphanumeric character
// (letter or digit).
// True for characters with general categories
// "L" (letters) and "Nd" (decimal digit numbers).
/**
 * @brief Return true if the code point is a letter or decimal digit.
 *
 * @param c  Unicode code point.
 * @return true if \a c is alphanumeric.
 */
bool
BUnicodeChar::IsAlNum(uint32 c)
{
	return u_isalnum(c);
}


// Check if a code point has the Lowercase Unicode property (UCHAR_LOWERCASE).
/**
 * @brief Return true if the code point has the Lowercase Unicode property.
 *
 * @param c  Unicode code point.
 * @return true if \a c is lowercase.
 */
bool
BUnicodeChar::IsLower(uint32 c)
{
	return u_isULowercase(c);
}


// Check if a code point has the Uppercase Unicode property (UCHAR_UPPERCASE).
/**
 * @brief Return true if the code point has the Uppercase Unicode property.
 *
 * @param c  Unicode code point.
 * @return true if \a c is uppercase.
 */
bool
BUnicodeChar::IsUpper(uint32 c)
{
	return u_isUUppercase(c);
}


// Determines whether the specified code point is a titlecase letter.
// True for general category "Lt" (titlecase letter).
/**
 * @brief Return true if the code point is in the "Lt" (titlecase) category.
 *
 * @param c  Unicode code point.
 * @return true if \a c is a titlecase letter.
 */
bool
BUnicodeChar::IsTitle(uint32 c)
{
	return u_istitle(c);
}


// Determines whether the specified code point is a digit character.
// True for characters with general category "Nd" (decimal digit numbers).
// Beginning with Unicode 4, this is the same as
// testing for the Numeric_Type of Decimal.
/**
 * @brief Return true if the code point is a decimal digit ("Nd" category).
 *
 * @param c  Unicode code point.
 * @return true if \a c is a decimal digit.
 */
bool
BUnicodeChar::IsDigit(uint32 c)
{
	return u_isdigit(c);
}


// Determines whether the specified code point is a hexadecimal digit.
// This is equivalent to u_digit(c, 16)>=0.
// True for characters with general category "Nd" (decimal digit numbers)
// as well as Latin letters a-f and A-F in both ASCII and Fullwidth ASCII.
// (That is, for letters with code points
// 0041..0046, 0061..0066, FF21..FF26, FF41..FF46.)
/**
 * @brief Return true if the code point is a hexadecimal digit.
 *
 * Includes ASCII 0-9, a-f, A-F, and their Fullwidth variants.
 *
 * @param c  Unicode code point.
 * @return true if \a c is a hex digit.
 */
bool
BUnicodeChar::IsHexDigit(uint32 c)
{
	return u_isxdigit(c);
}


// Determines whether the specified code point is "defined",
// which usually means that it is assigned a character.
// True for general categories other than "Cn" (other, not assigned),
// i.e., true for all code points mentioned in UnicodeData.txt.
/**
 * @brief Return true if the code point is assigned a Unicode character.
 *
 * @param c  Unicode code point.
 * @return true if \a c is defined (not in the "Cn" category).
 */
bool
BUnicodeChar::IsDefined(uint32 c)
{
	return u_isdefined(c);
}


// Determines whether the specified code point is a base character.
// True for general categories "L" (letters), "N" (numbers),
// "Mc" (spacing combining marks), and "Me" (enclosing marks).
/**
 * @brief Return true if the code point is a base character.
 *
 * @param c  Unicode code point.
 * @return true if \a c is a letter, number, or combining/enclosing mark.
 */
bool
BUnicodeChar::IsBase(uint32 c)
{
	return u_isbase(c);
}


// Determines whether the specified code point is a control character
// (as defined by this function).
// A control character is one of the following:
// - ISO 8-bit control character (U+0000..U+001f and U+007f..U+009f)
// - U_CONTROL_CHAR (Cc)
// - U_FORMAT_CHAR (Cf)
// - U_LINE_SEPARATOR (Zl)
// - U_PARAGRAPH_SEPARATOR (Zp)
/**
 * @brief Return true if the code point is a control or format character.
 *
 * @param c  Unicode code point.
 * @return true for ISO control characters, format chars, and separators.
 */
bool
BUnicodeChar::IsControl(uint32 c)
{
	return u_iscntrl(c);
}


// Determines whether the specified code point is a punctuation character.
// True for characters with general categories "P" (punctuation).
/**
 * @brief Return true if the code point is in the "P" (punctuation) category.
 *
 * @param c  Unicode code point.
 * @return true if \a c is punctuation.
 */
bool
BUnicodeChar::IsPunctuation(uint32 c)
{
	return u_ispunct(c);
}


// Determine if the specified code point is a space character according to Java.
// True for characters with general categories "Z" (separators),
// which does not include control codes (e.g., TAB or Line Feed).
/**
 * @brief Return true if the code point is a Unicode separator (Java definition).
 *
 * Does not include ASCII control codes like TAB or LF.
 *
 * @param c  Unicode code point.
 * @return true for "Z" category separators.
 */
bool
BUnicodeChar::IsSpace(uint32 c)
{
	return u_isJavaSpaceChar(c);
}


// Determines if the specified code point is a whitespace character
// A character is considered to be a whitespace character if and only
// if it satisfies one of the following criteria:
// - It is a Unicode Separator character (categories "Z" = "Zs" or "Zl" or "Zp"),
//		but is not also a non-breaking space (U+00A0 NBSP or U+2007 Figure Space
//		or U+202F Narrow NBSP).
// - It is U+0009 HORIZONTAL TABULATION.
// - It is U+000A LINE FEED.
// - It is U+000B VERTICAL TABULATION.
// - It is U+000C FORM FEED.
// - It is U+000D CARRIAGE RETURN.
// - It is U+001C FILE SEPARATOR.
// - It is U+001D GROUP SEPARATOR.
// - It is U+001E RECORD SEPARATOR.
// - It is U+001F UNIT SEPARATOR.
/**
 * @brief Return true if the code point is Unicode whitespace.
 *
 * Includes separator categories (except non-breaking spaces) and ASCII
 * control whitespace (TAB, LF, VT, FF, CR, and file/group/record/unit
 * separators).
 *
 * @param c  Unicode code point.
 * @return true if \a c is whitespace.
 */
bool
BUnicodeChar::IsWhitespace(uint32 c)
{
	return u_isWhitespace(c);
}


// Determines whether the specified code point is a printable character.
// True for general categories other than "C" (controls).
/**
 * @brief Return true if the code point is printable (not in the "C" category).
 *
 * @param c  Unicode code point.
 * @return true if \a c is printable.
 */
bool
BUnicodeChar::IsPrintable(uint32 c)
{
	return u_isprint(c);
}


//	#pragma mark -

/**
 * @brief Convert a code point to its lowercase equivalent.
 *
 * @param c  Unicode code point.
 * @return Lowercase equivalent, or \a c if no case mapping exists.
 */
uint32
BUnicodeChar::ToLower(uint32 c)
{
	return u_tolower(c);
}


/**
 * @brief Convert a code point to its uppercase equivalent.
 *
 * @param c  Unicode code point.
 * @return Uppercase equivalent, or \a c if no case mapping exists.
 */
uint32
BUnicodeChar::ToUpper(uint32 c)
{
	return u_toupper(c);
}


/**
 * @brief Convert a code point to its titlecase equivalent.
 *
 * @param c  Unicode code point.
 * @return Titlecase equivalent, or \a c if no case mapping exists.
 */
uint32
BUnicodeChar::ToTitle(uint32 c)
{
	return u_totitle(c);
}


/**
 * @brief Return the decimal digit value of a code point in base 10.
 *
 * @param c  Unicode code point.
 * @return Digit value in [0, 9], or -1 if \a c is not a decimal digit.
 */
int32
BUnicodeChar::DigitValue(uint32 c)
{
	return u_digit(c, 10);
}


/**
 * @brief Return the East Asian width property for the given code point.
 *
 * @param c  Unicode code point.
 * @return One of the unicode_east_asian_width enum values.
 */
unicode_east_asian_width
BUnicodeChar::EastAsianWidth(uint32 c)
{
	return (unicode_east_asian_width)u_getIntPropertyValue(c,
			UCHAR_EAST_ASIAN_WIDTH);
}


/**
 * @brief Encode a Unicode code point as UTF-8 bytes and advance the pointer.
 *
 * @param c    Unicode code point to encode.
 * @param out  Pointer-to-pointer; updated to point past the written bytes.
 */
void
BUnicodeChar::ToUTF8(uint32 c, char** out)
{
	int i = 0;
	U8_APPEND_UNSAFE(*out, i, c);
	*out += i;
}


/**
 * @brief Decode the next UTF-8 code point from the string and advance the pointer.
 *
 * @param in  Pointer-to-pointer to the input UTF-8 string; updated past the
 *            decoded sequence.
 * @return The decoded Unicode code point.
 */
uint32
BUnicodeChar::FromUTF8(const char** in)
{
	int i = 0;
	uint32 c = 0;
	U8_NEXT_UNSAFE(*in, i, c);
	*in += i;

	return c;
}


/**
 * @brief Count the number of Unicode code points in a null-terminated UTF-8 string.
 *
 * @param string  Null-terminated UTF-8 string.
 * @return Number of code points (not bytes).
 */
size_t
BUnicodeChar::UTF8StringLength(const char* string)
{
	size_t len = 0;
	while (*string) {
		FromUTF8(&string);
		len++;
	}
	return len;
}


/**
 * @brief Count the Unicode code points in a UTF-8 string up to a maximum.
 *
 * @param string     Null-terminated UTF-8 string.
 * @param maxLength  Maximum number of code points to count.
 * @return Number of code points counted, at most \a maxLength.
 */
size_t
BUnicodeChar::UTF8StringLength(const char* string, size_t maxLength)
{
	size_t len = 0;
	while (len < maxLength && *string) {
		FromUTF8(&string);
		len++;
	}
	return len;
}
