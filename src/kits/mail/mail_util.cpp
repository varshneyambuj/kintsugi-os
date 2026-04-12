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
 *   Copyright 2011-2016, Haiku, Inc. All rights reserved.
 *   Copyright 2001-2003 Dr. Zoidberg Enterprises. All rights reserved.
 */


/**
 * @file mail_util.cpp
 * @brief Mail kit utility functions for encoding, address handling, and header parsing.
 *
 * Provides the global mail_charsets table; character-set conversion wrappers
 * (mail_convert_to_utf8, mail_convert_from_utf8); RFC 2047 encode/decode
 * (rfc2047_to_utf8, utf8_to_rfc2047); header folding, line reading, address
 * extraction, subject-to-thread normalisation, date parsing, and read-status
 * attribute helpers. These are used throughout the mail kit and by protocol
 * add-ons.
 *
 * @see BMailComponent, BTextMailComponent, BEmailMessage
 */


#include <mail_util.h>

#include <stdlib.h>
#include <strings.h>
#include <stdio.h>
#define __USE_GNU
#include <regex.h>
#include <ctype.h>
#include <errno.h>

#include <FindDirectory.h>
#include <List.h>
#include <Locker.h>
#include <parsedate.h>
#include <Path.h>
#include <String.h>
#include <UTF8.h>

#include <mail_encoding.h>

#include <AttributeUtilities.h>
#include <CharacterSet.h>
#include <CharacterSetRoster.h>


using namespace BPrivate;


#define CRLF   "\r\n"

struct CharsetConversionEntry {
	const char *charset;
	uint32 flavor;
};

/**
 * @brief Global table mapping RFC charset names to BeOS conversion constants.
 *
 * Ordered so that the first entry for any given conversion constant is the
 * canonical MIME standard name. Terminated by a NULL charset pointer.
 */
extern const CharsetConversionEntry mail_charsets[] = {
	// In order of authority, so when searching for the name for a particular
	// numbered conversion, start at the beginning of the array.
	{"iso-8859-1",  B_ISO1_CONVERSION}, // MIME STANDARD
	{"iso-8859-2",  B_ISO2_CONVERSION}, // MIME STANDARD
	{"iso-8859-3",  B_ISO3_CONVERSION}, // MIME STANDARD
	{"iso-8859-4",  B_ISO4_CONVERSION}, // MIME STANDARD
	{"iso-8859-5",  B_ISO5_CONVERSION}, // MIME STANDARD
	{"iso-8859-6",  B_ISO6_CONVERSION}, // MIME STANDARD
	{"iso-8859-7",  B_ISO7_CONVERSION}, // MIME STANDARD
	{"iso-8859-8",  B_ISO8_CONVERSION}, // MIME STANDARD
	{"iso-8859-9",  B_ISO9_CONVERSION}, // MIME STANDARD
	{"iso-8859-10", B_ISO10_CONVERSION}, // MIME STANDARD
	{"iso-8859-13", B_ISO13_CONVERSION}, // MIME STANDARD
	{"iso-8859-14", B_ISO14_CONVERSION}, // MIME STANDARD
	{"iso-8859-15", B_ISO15_CONVERSION}, // MIME STANDARD

	{"shift_jis",	B_SJIS_CONVERSION}, // MIME STANDARD
	{"shift-jis",	B_SJIS_CONVERSION},
	{"iso-2022-jp", B_JIS_CONVERSION}, // MIME STANDARD
	{"euc-jp",		B_EUC_CONVERSION}, // MIME STANDARD

	{"euc-kr",      B_EUC_KR_CONVERSION}, // Shift encoding 7 bit and KSC-5601 if bit 8 is on. // MIME STANDARD
	{"ksc5601",		B_EUC_KR_CONVERSION},    // Not sure if 7 or 8 bit. // COMPATIBLE?
	{"ks_c_5601-1987", B_EUC_KR_CONVERSION}, // Not sure if 7 or 8 bit. // COMPATIBLE with stupid MS software

	{"koi8-r",      B_KOI8R_CONVERSION},           // MIME STANDARD
	{"windows-1251",B_MS_WINDOWS_1251_CONVERSION}, // MIME STANDARD
	{"windows-1252",B_MS_WINDOWS_CONVERSION},      // MIME STANDARD

	{"dos-437",     B_MS_DOS_CONVERSION},     // WRONG NAME : MIME STANDARD NAME = NONE ( IBM437? )
	{"dos-866",     B_MS_DOS_866_CONVERSION}, // WRONG NAME : MIME STANDARD NAME = NONE ( IBM866? )
	{"x-mac-roman", B_MAC_ROMAN_CONVERSION},  // WRONG NAME : MIME STANDARD NAME = NONE ( macintosh? + x-mac-roman? )

    {"big5",        24}, // MIME STANDARD

    {"gb18030",     25}, // WRONG NAME : MIME STANDARD NAME = NONE ( GB18030? )
    {"gb2312",      25}, // COMPATIBLE
    {"gbk",         25}, // COMPATIBLE

	/* {"utf-16",		B_UNICODE_CONVERSION}, Might not work due to NULs in text, needs testing. */
	{"us-ascii",	B_MAIL_US_ASCII_CONVERSION},                                  // MIME STANDARD
	{"utf-8",		B_MAIL_UTF8_CONVERSION /* Special code for no conversion */}, // MIME STANDARD

	{NULL, (uint32) -1} /* End of list marker, NULL string pointer is the key. */
};


/** @brief Atomic lock used to serialise one-time regex compilation in SubjectToThread(). */
static int32 gLocker = 0;

/** @brief Number of capture groups in the SubjectToThread regex. */
static size_t gNsub = 1;

/** @brief Compiled regex buffer used by SubjectToThread(). */
static re_pattern_buffer gRe;

/** @brief Pointer set to &gRe once compilation succeeds; NULL until then. */
static re_pattern_buffer *gRebuf = NULL;

/** @brief Case-folding translation table used by the subject regex. */
static unsigned char gTranslation[256];


/**
 * @brief Detects and converts non-RFC-2047 8-bit content to UTF-8.
 *
 * If the buffer contains isolated high-bit bytes (suggesting ISO-Latin-1),
 * converts the entire buffer to UTF-8 via convert_to_utf8(). If the content
 * appears to be valid UTF-8 multi-byte sequences it is left unchanged.
 *
 * @param buffer        Pointer to the buffer pointer; may be reallocated.
 * @param bufferLength  Pointer to the allocated size; updated if reallocated.
 * @param sourceLength  Pointer to the content length; updated after conversion.
 * @return Non-zero if a conversion was performed, zero if the input is already UTF-8
 *         or contains only ASCII.
 */
static int
handle_non_rfc2047_encoding(char **buffer, size_t *bufferLength,
	size_t *sourceLength)
{
	char *string = *buffer;
	int32 length = *sourceLength;
	int32 i;

	// check for 8-bit characters
	for (i = 0;i < length;i++)
		if (string[i] & 0x80)
			break;
	if (i == length)
		return false;

	// check for groups of 8-bit characters - this code is not very smart;
	// it just can detect some sort of single-byte encoded stuff, the rest
	// is regarded as UTF-8

	int32 singletons = 0,doubles = 0;

	for (i = 0;i < length;i++)
	{
		if (string[i] & 0x80)
		{
			if ((string[i + 1] & 0x80) == 0)
				singletons++;
			else doubles++;
			i++;
		}
	}

	if (singletons != 0)	// can't be valid UTF-8 anymore, so we assume ISO-Latin-1
	{
		int32 state = 0;
		// just to be sure
		int32 destLength = length * 4 + 1;
		int32 destBufferLength = destLength;
		char *dest = (char*)malloc(destLength);
		if (dest == NULL)
			return 0;

		if (convert_to_utf8(B_ISO1_CONVERSION, string, &length,dest,
			&destLength, &state) == B_OK) {
			*buffer = dest;
			*bufferLength = destBufferLength;
			*sourceLength = destLength;
			return true;
		}
		free(dest);
		return false;
	}

	// we assume a valid UTF-8 string here, but yes, we don't check it
	return true;
}


// #pragma mark -


/**
 * @brief Writes the read-status flag and status string to a mail node's attributes.
 *
 * Updates B_MAIL_ATTR_READ with \a flag and (if the current status string is
 * "New", "Read", or "Seen") updates B_MAIL_ATTR_STATUS accordingly.
 *
 * @param node  BNode of the mail file to update.
 * @param flag  B_READ, B_SEEN, or B_UNREAD.
 * @return B_OK on success, B_ERROR if an attribute write fails.
 */
status_t
write_read_attr(BNode& node, read_flags flag)
{
	if (node.WriteAttr(B_MAIL_ATTR_READ, B_INT32_TYPE, 0, &flag, sizeof(int32))
			< 0)
		return B_ERROR;

	// Manage the status string only if it currently has a known state
	BString currentStatus;
	if (node.ReadAttrString(B_MAIL_ATTR_STATUS, &currentStatus) == B_OK
		&& currentStatus.ICompare("New") != 0
		&& currentStatus.ICompare("Read") != 0
		&& currentStatus.ICompare("Seen") != 0) {
		return B_OK;
	}

	BString statusString = flag == B_READ ? "Read"
		: flag == B_SEEN ? "Seen" : "New";
	if (node.WriteAttrString(B_MAIL_ATTR_STATUS, &statusString) < 0)
		return B_ERROR;

	return B_OK;
}


/**
 * @brief Reads the read-status flag from a mail node's attributes.
 *
 * Reads B_MAIL_ATTR_READ first; if absent, falls back to interpreting the
 * B_MAIL_ATTR_STATUS string ("New" maps to B_UNREAD, anything else to B_READ).
 *
 * @param node  BNode of the mail file to read.
 * @param flag  Output read_flags value.
 * @return B_OK on success, B_ERROR if neither attribute is present.
 */
status_t
read_read_attr(BNode& node, read_flags& flag)
{
	if (node.ReadAttr(B_MAIL_ATTR_READ, B_INT32_TYPE, 0, &flag, sizeof(int32))
			== sizeof(int32))
		return B_OK;

	BString statusString;
	if (node.ReadAttrString(B_MAIL_ATTR_STATUS, &statusString) == B_OK) {
		if (statusString.ICompare("New") == 0)
			flag = B_UNREAD;
		else
			flag = B_READ;

		return B_OK;
	}

	return B_ERROR;
}


// The next couple of functions are our wrapper around convert_to_utf8 and
// convert_from_utf8 so that they can also convert from UTF-8 to UTF-8 by
// specifying the B_MAIL_UTF8_CONVERSION constant as the conversion operation.
// It also lets us add new conversions, like B_MAIL_US_ASCII_CONVERSION.


/**
 * @brief Converts text from a mail-specific charset encoding to UTF-8.
 *
 * Handles B_MAIL_UTF8_CONVERSION (identity copy) and B_MAIL_US_ASCII_CONVERSION
 * (high-bit stripping) as special cases before delegating to convert_to_utf8().
 * After conversion, any spurious NUL bytes in the output are replaced with
 * \a substitute.
 *
 * @param srcEncoding  Source charset constant.
 * @param src          Input bytes in \a srcEncoding.
 * @param srcLen       Input/output: bytes available / bytes consumed.
 * @param dst          Output buffer for the UTF-8 result.
 * @param dstLen       Input/output: buffer size / bytes written.
 * @param state        Conversion state (passed to convert_to_utf8).
 * @param substitute   Replacement byte for characters that cannot be converted.
 * @return B_OK on success, or an error code from convert_to_utf8().
 */
status_t
mail_convert_to_utf8(uint32 srcEncoding, const char *src, int32 *srcLen,
	char *dst, int32 *dstLen, int32 *state, char substitute)
{
	int32 copyAmount;
	char *originalDst = dst;
	status_t returnCode = -1;

	if (srcEncoding == B_MAIL_UTF8_CONVERSION) {
		copyAmount = *srcLen;
		if (*dstLen < copyAmount)
			copyAmount = *dstLen;
		memcpy (dst, src, copyAmount);
		*srcLen = copyAmount;
		*dstLen = copyAmount;
		returnCode = B_OK;
	} else if (srcEncoding == B_MAIL_US_ASCII_CONVERSION) {
		int32 i;
		unsigned char letter;
		copyAmount = *srcLen;
		if (*dstLen < copyAmount)
			copyAmount = *dstLen;
		for (i = 0; i < copyAmount; i++) {
			letter = *src++;
			if (letter > 0x80U)
				// Invalid, could also use substitute, but better to strip high bit.
				*dst++ = letter - 0x80U;
			else if (letter == 0x80U)
				// Can't convert to 0x00 since that's NUL, which would cause problems.
				*dst++ = substitute;
			else
				*dst++ = letter;
		}
		*srcLen = copyAmount;
		*dstLen = copyAmount;
		returnCode = B_OK;
	} else
		returnCode = convert_to_utf8 (srcEncoding, src, srcLen,
			dst, dstLen, state, substitute);

	if (returnCode == B_OK) {
		// Replace spurious NUL bytes, which should normally not be in the
		// output of the decoding (not normal UTF-8 characters, and no NULs are
		// in our usual input strings).  They happen for some odd ISO-2022-JP
		// byte pair combinations which are improperly handled by the BeOS
		// routines.  Like "\e$ByD\e(B" where \e is the ESC character $1B, the
		// first ESC $ B switches to a Japanese character set, then the next
		// two bytes "yD" specify a character, then ESC ( B switches back to
		// the ASCII character set.  The UTF-8 conversion yields a NUL byte.
		int32 i;
		for (i = 0; i < *dstLen; i++)
			if (originalDst[i] == 0)
				originalDst[i] = substitute;
	}
	return returnCode;
}


/**
 * @brief Converts UTF-8 text to a mail-specific charset encoding.
 *
 * Handles B_MAIL_UTF8_CONVERSION (identity copy), B_MAIL_US_ASCII_CONVERSION
 * (strip non-ASCII), and B_JIS_CONVERSION (appends an ASCII-reset escape
 * sequence so the output is well-formed for use in e-mail headers) as
 * special cases before delegating to convert_from_utf8().
 *
 * @param dstEncoding  Destination charset constant.
 * @param src          UTF-8 input bytes.
 * @param srcLen       Input/output: bytes available / bytes consumed.
 * @param dst          Output buffer for the result.
 * @param dstLen       Input/output: buffer size / bytes written.
 * @param state        Conversion state (passed to convert_from_utf8).
 * @param substitute   Replacement byte for characters that cannot be represented.
 * @return B_OK on success, or an error code from convert_from_utf8().
 */
status_t
mail_convert_from_utf8(uint32 dstEncoding, const char *src, int32 *srcLen,
	char *dst, int32 *dstLen, int32 *state, char substitute)
{
	int32 copyAmount;
	status_t errorCode;
	int32 originalDstLen = *dstLen;
	int32 tempDstLen;
	int32 tempSrcLen;

	if (dstEncoding == B_MAIL_UTF8_CONVERSION) {
		copyAmount = *srcLen;
		if (*dstLen < copyAmount)
			copyAmount = *dstLen;
		memcpy (dst, src, copyAmount);
		*srcLen = copyAmount;
		*dstLen = copyAmount;
		return B_OK;
	}

	if (dstEncoding == B_MAIL_US_ASCII_CONVERSION) {
		int32 characterLength;
		int32 dstRemaining = *dstLen;
		unsigned char letter;
		int32 srcRemaining = *srcLen;

		// state contains the number of source bytes to skip, left over from a
		// partial UTF-8 character split over the end of the buffer from last
		// time.
		if (srcRemaining <= *state) {
			*state -= srcRemaining;
			*dstLen = 0;
			return B_OK;
		}
		srcRemaining -= *state;
		src += *state;
		*state = 0;

		while (true) {
			if (srcRemaining <= 0 || dstRemaining <= 0)
				break;
			letter = *src;
			if (letter < 0x80)
				characterLength = 1; // Regular ASCII equivalent code.
			else if (letter < 0xC0)
				characterLength = 1; // Invalid in-between data byte 10xxxxxx.
			else if (letter < 0xE0)
				characterLength = 2;
			else if (letter < 0xF0)
				characterLength = 3;
			else if (letter < 0xF8)
				characterLength = 4;
			else if (letter < 0xFC)
				characterLength = 5;
			else if (letter < 0xFE)
				characterLength = 6;
			else
				characterLength = 1; // 0xFE and 0xFF are invalid in UTF-8.
			if (letter < 0x80)
				*dst++ = *src;
			else
				*dst++ = substitute;
			dstRemaining--;
			if (srcRemaining < characterLength) {
				// Character split past the end of the buffer.
				*state = characterLength - srcRemaining;
				srcRemaining = 0;
			} else {
				src += characterLength;
				srcRemaining -= characterLength;
			}
		}
		// Update with the amounts used.
		*srcLen = *srcLen - srcRemaining;
		*dstLen = *dstLen - dstRemaining;
		return B_OK;
	}

	errorCode = convert_from_utf8(dstEncoding, src, srcLen, dst, dstLen, state,
		substitute);
	if (errorCode != B_OK)
		return errorCode;

	if (dstEncoding != B_JIS_CONVERSION)
		return B_OK;

	// B_JIS_CONVERSION (ISO-2022-JP) works by shifting between different
	// character subsets.  For E-mail headers (and other uses), it needs to be
	// switched back to ASCII at the end (otherwise the last character gets
	// lost or other weird things happen in the headers).  Note that we can't
	// just append the escape code since the convert_from_utf8 "state" will be
	// wrong.  So we append an ASCII letter and throw it away, leaving just the
	// escape code.  Well, it actually switches to the Roman character set, not
	// ASCII, but that should be OK.

	tempDstLen = originalDstLen - *dstLen;
	if (tempDstLen < 3) // Not enough space remaining in the output.
		return B_OK; // Sort of an error, but we did convert the rest OK.
	tempSrcLen = 1;
	errorCode = convert_from_utf8(dstEncoding, "a", &tempSrcLen,
		dst + *dstLen, &tempDstLen, state, substitute);
	if (errorCode != B_OK)
		return errorCode;
	*dstLen += tempDstLen - 1 /* don't include the ASCII letter */;
	return B_OK;
}


/**
 * @brief Decodes RFC 2047 encoded words in an e-mail header buffer to UTF-8.
 *
 * Scans \a *bufp for "=?charset?encoding?text?=" sequences, decodes each one
 * (QP or base64), converts to UTF-8, and writes the result back into the same
 * buffer (in-place). White space between two adjacent encoded words is silently
 * discarded per RFC 2047 section 6.2.
 *
 * @param bufp       Pointer to the buffer pointer; the buffer is modified in place.
 * @param bufLen     Pointer to the allocated buffer size.
 * @param strLen     Number of significant bytes in the buffer (0 = use strlen).
 * @return Number of bytes in the result, or a negative error code on failure.
 */
ssize_t
rfc2047_to_utf8(char **bufp, size_t *bufLen, size_t strLen)
{
	char *head, *tail;
	char *charset, *encoding, *end;
	ssize_t ret = B_OK;

	if (bufp == NULL || *bufp == NULL)
		return -1;

	char *string = *bufp;

	//---------Handle *&&^%*&^ non-RFC compliant, 8bit mail
	if (handle_non_rfc2047_encoding(bufp,bufLen,&strLen))
		return strLen;

	// set up string length
	if (strLen == 0)
		strLen = strlen(*bufp);
	char lastChar = (*bufp)[strLen];
	(*bufp)[strLen] = '\0';

	//---------Whew! Now for RFC compliant mail
	bool encodedWordFoundPreviously = false;
	for (head = tail = string;
		((charset = strstr(tail, "=?")) != NULL)
		&& (((encoding = strchr(charset + 2, '?')) != NULL)
			&& encoding[1] && (encoding[2] == '?') && encoding[3])
		&& (end = strstr(encoding + 3, "?=")) != NULL;
		// found "=?...charset...?e?...text...?=   (e == encoding)
		//        ^charset       ^encoding    ^end
		tail = end)
	{
		// Copy non-encoded text (from tail up to charset) to the output.
		// Ignore spaces between two encoded "words".  RFC2047 says the words
		// should be concatenated without the space (designed for Asian
		// sentences which have no spaces yet need to be broken into "words" to
		// keep within the line length limits).
		bool nonSpaceFound = false;
		for (int i = 0; i < charset-tail; i++) {
			if (!isspace (tail[i])) {
				nonSpaceFound = true;
				break;
			}
		}
		if (!encodedWordFoundPreviously || nonSpaceFound) {
			if (string != tail && tail != charset)
				memmove(string, tail, charset-tail);
			string += charset-tail;
		}
		tail = charset;
		encodedWordFoundPreviously = true;

		// move things to point at what they should:
		//   =?...charset...?e?...text...?=   (e == encoding)
		//     ^charset      ^encoding     ^end
		charset += 2;
		encoding += 1;
		end += 2;

		// find the charset this text is in now
		size_t cLen = encoding - 1 - charset;
		bool base64encoded = toupper(*encoding) == 'B';

		uint32 convertID = B_MAIL_NULL_CONVERSION;
		char charsetName[cLen + 1];
		memcpy(charsetName, charset, cLen);
		charsetName[cLen] = '\0';
		if (strcasecmp(charsetName, "us-ascii") == 0) {
			convertID = B_MAIL_US_ASCII_CONVERSION;
		} else if (strcasecmp(charsetName, "utf-8") == 0) {
			convertID = B_MAIL_UTF8_CONVERSION;
		} else {
			const BCharacterSet* charSet
				= BCharacterSetRoster::FindCharacterSetByName(charsetName);
			if (charSet != NULL) {
				convertID = charSet->GetConversionID();
			}
		}
		if (convertID == B_MAIL_NULL_CONVERSION) {
			// unidentified charset
			// what to do? doing nothing skips the encoded text;
			// but we should keep it: we copy it to the output.
			if (string != tail && tail != end)
				memmove(string, tail, end-tail);
			string += end-tail;
			continue;
		}
		// else we've successfully identified the charset

		char *src = encoding+2;
		int32 srcLen = end - 2 - src;
		// encoded text: src..src+srcLen

		// decode text, get decoded length (reducing xforms)
		srcLen = !base64encoded ? decode_qp(src, src, srcLen, 1)
			: decode_base64(src, src, srcLen);

		// allocate space for the converted text
		int32 dstLen = end-string + *bufLen-strLen;
		char *dst = (char*)malloc(dstLen);
		int32 cvLen = srcLen;
		int32 convState = 0;

		//
		// do the conversion
		//
		ret = mail_convert_to_utf8(convertID, src, &cvLen, dst, &dstLen,
			&convState);
		if (ret != B_OK) {
			// what to do? doing nothing skips the encoded text
			// but we should keep it: we copy it to the output.

			free(dst);

			if (string != tail && tail != end)
				memmove(string, tail, end-tail);
			string += end-tail;
			continue;
		}
		/* convert_to_ is either returning something wrong or my
		   test data is screwed up.  Whatever it is, Not Enough
		   Space is not the only cause of the below, so we just
		   assume it succeeds if it converts anything at all.
		else if (cvLen < srcLen)
		{
			// not enough room to convert the data;
			// grow *buf and retry

			free(dst);

			char *temp = (char*)realloc(*bufp, 2*(*bufLen + 1));
			if (temp == NULL)
			{
				ret = B_NO_MEMORY;
				break;
			}

			*bufp = temp;
			*bufLen = 2*(*bufLen + 1);

			string = *bufp + (string-head);
			tail = *bufp + (tail-head);
			charset = *bufp + (charset-head);
			encoding = *bufp + (encoding-head);
			end = *bufp + (end-head);
			src = *bufp + (src-head);
			head = *bufp;
			continue;
		}
		*/
		else {
			if (dstLen > end-string) {
				// copy the string forward...
				memmove(string+dstLen, end, strLen - (end-head) + 1);
				strLen += string+dstLen - end;
				end = string + dstLen;
			}

			memcpy(string, dst, dstLen);
			string += dstLen;
			free(dst);
			continue;
		}
	}

	// copy everything that's left
	size_t tailLen = strLen - (tail - head);
	memmove(string, tail, tailLen+1);
	string += tailLen;

	// replace the last char
	(*bufp)[strLen] = lastChar;

	return ret < B_OK ? ret : string-head;
}


/**
 * @brief Encodes a UTF-8 header field value to RFC 2047 format for use in MIME headers.
 *
 * Breaks the input into words at whitespace and special characters, converts
 * each word to \a charset, and encodes words that require it using
 * quoted-printable or base64. Adjacent encodable words that fit within 53
 * converted bytes are merged to reduce overhead. The result is written into
 * a newly allocated buffer; the original *bufp is freed.
 *
 * @param bufp     Pointer to the input buffer; replaced with the encoded output.
 * @param length   Number of bytes in the input.
 * @param charset  Target charset constant for encoding.
 * @param encoding Transfer encoding to use (quoted_printable or base64).
 * @return Number of bytes in the encoded output.
 */
ssize_t
utf8_to_rfc2047 (char **bufp, ssize_t length, uint32 charset, char encoding)
{
	struct word {
		BString	originalWord;
		BString	convertedWord;
		bool	needsEncoding;

		// Convert the word from UTF-8 to the desired character set.  The
		// converted version also includes the escape codes to return to ASCII
		// mode, if relevant.  Also note if it uses unprintable characters,
		// which means it will need that special encoding treatment later.
		void ConvertWordToCharset (uint32 charset) {
			int32 state = 0;
			int32 originalLength = originalWord.Length();
			int32 convertedLength = originalLength * 5 + 1;
			char *convertedBuffer = convertedWord.LockBuffer (convertedLength);
			mail_convert_from_utf8 (charset, originalWord.String(),
				&originalLength, convertedBuffer, &convertedLength, &state);
			for (int i = 0; i < convertedLength; i++) {
				if ((convertedBuffer[i] & (1 << 7)) ||
					(convertedBuffer[i] >= 0 && convertedBuffer[i] < 32)) {
					needsEncoding = true;
					break;
				}
			}
			convertedWord.UnlockBuffer (convertedLength);
		};
	};
	struct word *currentWord;
	BList words;

	// Break the header into words.  White space characters (including tabs and
	// newlines) separate the words.  Each word includes any space before it as
	// part of the word.  Actually, quotes and other special characters
	// (",()<>@) are treated as separate words of their own so that they don't
	// get encoded (because MIME headers get the quotes parsed before character
	// set unconversion is done).  The reader is supposed to ignore all white
	// space between encoded words, which can be inserted so that older mail
	// parsers don't have overly long line length problems.

	const char *source = *bufp;
	const char *bufEnd = *bufp + length;
	const char *specialChars = "\"()<>@,";

	while (source < bufEnd) {
		currentWord = new struct word;
		currentWord->needsEncoding = false;

		int wordEnd = 0;

		// Include leading spaces as part of the word.
		while (source + wordEnd < bufEnd && isspace (source[wordEnd]))
			wordEnd++;

		if (source + wordEnd < bufEnd &&
			strchr (specialChars, source[wordEnd]) != NULL) {
			// Got a quote mark or other special character, which is treated as
			// a word in itself since it shouldn't be encoded, which would hide
			// it from the mail system.
			wordEnd++;
		} else {
			// Find the end of the word.  Leave wordEnd pointing just after the
			// last character in the word.
			while (source + wordEnd < bufEnd) {
				if (isspace(source[wordEnd]) ||
					strchr (specialChars, source[wordEnd]) != NULL)
					break;
				if (wordEnd > 51 /* Makes Base64 ISO-2022-JP "word" a multiple of 4 bytes */ &&
					0xC0 == (0xC0 & (unsigned int) source[wordEnd])) {
					// No English words are that long (46 is the longest),
					// break up what is likely Asian text (which has no spaces)
					// at the start of the next non-ASCII UTF-8 character (high
					// two bits are both ones).  Note that two encoded words in
					// a row get joined together, even if there is a space
					// between them in the final output text, according to the
					// standard.  Next word will also be conveniently get
					// encoded due to the 0xC0 test.
					currentWord->needsEncoding = true;
					break;
				}
				wordEnd++;
			}
		}
		currentWord->originalWord.SetTo (source, wordEnd);
		currentWord->ConvertWordToCharset (charset);
		words.AddItem(currentWord);
		source += wordEnd;
	}

	// Combine adjacent words which contain unprintable text so that the
	// overhead of switching back and forth between regular text and specially
	// encoded text is reduced.  However, the combined word must be shorter
	// than the maximum of 75 bytes, including character set specification and
	// all those delimiters (worst case 22 bytes of overhead).

	struct word *run;

	for (int32 i = 0; (currentWord = (struct word *) words.ItemAt (i)) != NULL; i++) {
		if (!currentWord->needsEncoding)
			continue; // No need to combine unencoded words.
		for (int32 g = i+1; (run = (struct word *) words.ItemAt (g)) != NULL; g++) {
			if (!run->needsEncoding)
				break; // Don't want to combine encoded and unencoded words.
			if ((currentWord->convertedWord.Length() + run->convertedWord.Length() <= 53)) {
				currentWord->originalWord.Append (run->originalWord);
				currentWord->ConvertWordToCharset (charset);
				words.RemoveItem(g);
				delete run;
				g--;
			} else // Can't merge this word, result would be too long.
				break;
		}
	}

	// Combine the encoded and unencoded words into one line, doing the
	// quoted-printable or base64 encoding.  Insert an extra space between
	// words which are both encoded to make word wrapping easier, since there
	// is normally none, and you're allowed to insert space (the receiver
	// throws it away if it is between encoded words).

	BString rfc2047;
	bool	previousWordNeededEncoding = false;

	const char *charset_dec = "none-bug";
	for (int32 i = 0; mail_charsets[i].charset != NULL; i++) {
		if (mail_charsets[i].flavor == charset) {
			charset_dec = mail_charsets[i].charset;
			break;
		}
	}

	while ((currentWord = (struct word *)words.RemoveItem((int32)0)) != NULL) {
		if ((encoding != quoted_printable && encoding != base64) ||
		!currentWord->needsEncoding) {
			rfc2047.Append (currentWord->convertedWord);
		} else {
			// This word needs encoding.  Try to insert a space between it and
			// the previous word.
			if (previousWordNeededEncoding)
				rfc2047 << ' '; // Can insert as many spaces as you want between encoded words.
			else {
				// Previous word is not encoded, spaces are significant.  Try
				// to move a space from the start of this word to be outside of
				// the encoded text, so that there is a bit of space between
				// this word and the previous one to enhance word wrapping
				// chances later on.
				if (currentWord->originalWord.Length() > 1 &&
					isspace (currentWord->originalWord[0])) {
					rfc2047 << currentWord->originalWord[0];
					currentWord->originalWord.Remove (0 /* offset */, 1 /* length */);
					currentWord->ConvertWordToCharset (charset);
				}
			}

			char *encoded = NULL;
			ssize_t encoded_len = 0;
			int32 convertedLength = currentWord->convertedWord.Length ();
			const char *convertedBuffer = currentWord->convertedWord.String ();

			switch (encoding) {
				case quoted_printable:
					encoded = (char *) malloc (convertedLength * 3);
					encoded_len = encode_qp (encoded, convertedBuffer, convertedLength, true /* headerMode */);
					break;
				case base64:
					encoded = (char *) malloc (convertedLength * 2);
					encoded_len = encode_base64 (encoded, convertedBuffer, convertedLength, true /* headerMode */);
					break;
				default: // Unknown encoding type, shouldn't happen.
					encoded = (char *) convertedBuffer;
					encoded_len = convertedLength;
					break;
			}

			rfc2047 << "=?" << charset_dec << '?' << encoding << '?';
			rfc2047.Append (encoded, encoded_len);
			rfc2047 << "?=";

			if (encoding == quoted_printable || encoding == base64)
				free(encoded);
		}
		previousWordNeededEncoding = currentWord->needsEncoding;
		delete currentWord;
	}

	free(*bufp);

	ssize_t finalLength = rfc2047.Length ();
	*bufp = (char *) (malloc (finalLength + 1));
	memcpy (*bufp, rfc2047.String(), finalLength);
	(*bufp)[finalLength] = 0;

	return finalLength;
}


/**
 * @brief Folds a header line at whitespace, appending CRLF after each segment.
 *
 * Wraps lines at the first available whitespace at or before column 78,
 * preferring to split after a comma+space when possible. The folded output
 * replaces \a string in-place.
 *
 * @param string  Header value string to fold; modified in place.
 */
void
FoldLineAtWhiteSpaceAndAddCRLF(BString &string)
{
	int inputLength = string.Length();
	int lineStartIndex;
	const int maxLineLength = 78; // Doesn't include CRLF.
	BString output;
	int splitIndex;
	int tempIndex;

	lineStartIndex = 0;
	while (true) {
		// If we don't need to wrap the text, just output the remainder, if any.

		if (lineStartIndex + maxLineLength >= inputLength) {
			if (lineStartIndex < inputLength) {
				output.Insert (string, lineStartIndex /* source offset */,
					inputLength - lineStartIndex /* count */,
					output.Length() /* insert at */);
				output.Append (CRLF);
			}
			break;
		}

		// Look ahead for a convenient spot to split it, between a comma and
		// space, which you often see between e-mail addresses like this:
		// "Joe Who" joe@dot.com, "Someone Else" else@blot.com

		tempIndex = lineStartIndex + maxLineLength;
		if (tempIndex > inputLength)
			tempIndex = inputLength;
		splitIndex = string.FindLast (", ", tempIndex);
		if (splitIndex >= lineStartIndex)
			splitIndex++; // Point to the space character.

		// If none of those exist, try splitting at any white space.

		if (splitIndex <= lineStartIndex)
			splitIndex = string.FindLast (" ", tempIndex);
		if (splitIndex <= lineStartIndex)
			splitIndex = string.FindLast ("\t", tempIndex);

		// If none of those exist, allow for a longer word - split at the next
		// available white space.

		if (splitIndex <= lineStartIndex)
			splitIndex = string.FindFirst (" ", lineStartIndex + 1);
		if (splitIndex <= lineStartIndex)
			splitIndex = string.FindFirst ("\t", lineStartIndex + 1);

		// Give up, the whole rest of the line can't be split, just dump it
		// out.

		if (splitIndex <= lineStartIndex) {
			if (lineStartIndex < inputLength) {
				output.Insert (string, lineStartIndex /* source offset */,
					inputLength - lineStartIndex /* count */,
					output.Length() /* insert at */);
				output.Append (CRLF);
			}
			break;
		}

		// Do the split.  The current line up to but not including the space
		// gets output, followed by a CRLF.  The space remains to become the
		// start of the next line (and that tells the message reader that it is
		// a continuation line).

		output.Insert (string, lineStartIndex /* source offset */,
			splitIndex - lineStartIndex /* count */,
			output.Length() /* insert at */);
		output.Append (CRLF);
		lineStartIndex = splitIndex;
	}
	string.SetTo (output);
}


/**
 * @brief Reads one (possibly folded) header line from a FILE stream.
 *
 * Reads characters until a line ending is found, then peeks at the next
 * character. If the next line starts with whitespace the lines are folded
 * (joined). CRLF endings are normalised to LF. The buffer is grown as needed.
 *
 * @param file    Source FILE to read from.
 * @param buffer  In/out: pointer to the buffer; reallocated as necessary.
 * @param buflen  In/out: pointer to the allocated buffer size.
 * @return Number of significant bytes in the buffer (not including NUL),
 *         0 for an empty line, or a negative errno on error.
 */
ssize_t
readfoldedline(FILE *file, char **buffer, size_t *buflen)
{
	ssize_t len = buflen && *buflen ? *buflen : 0;
	char * buf = buffer && *buffer ? *buffer : NULL;
	ssize_t cnt = 0; // Number of characters currently in the buffer.
	int c;

	while (true) {
		// Make sure there is space in the buffer for two more characters (one
		// for the next character, and one for the end of string NUL byte).
		if (buf == NULL || cnt + 2 >= len) {
			char *temp = (char *)realloc(buf, len + 64);
			if (temp == NULL) {
				// Out of memory, however existing buffer remains allocated.
				cnt = ENOMEM;
				break;
			}
			len += 64;
			buf = temp;
		}

		// Read the next character, or end of file, or IO error.
		if ((c = fgetc(file)) == EOF) {
			if (ferror (file)) {
				cnt = errno;
				if (cnt >= 0)
					cnt = -1; // Error codes must be negative.
			} else {
				// Really is end of file.  Also make it end of line if there is
				// some text already read in.  If the first thing read was EOF,
				// just return an empty string.
				if (cnt > 0) {
					buf[cnt++] = '\n';
					if (buf[cnt-2] == '\r') {
						buf[cnt-2] = '\n';
						--cnt;
					}
				}
			}
			break;
		}

		buf[cnt++] = c;

		if (c == '\n') {
			// Convert CRLF end of line to just a LF.  Do it before folding, in
			// case we don't need to fold.
			if (cnt >= 2 && buf[cnt-2] == '\r') {
				buf[cnt-2] = '\n';
				--cnt;
			}
			// If the current line is empty then return it (so that empty lines
			// don't disappear if the next line starts with a space).
			if (cnt <= 1)
				break;
			// Fold if first character on the next line is whitespace.
			c = fgetc(file); // Note it's OK to read EOF and ungetc it too.
			if (c == ' ' || c == '\t')
				buf[cnt-1] = c; // Replace \n with the white space character.
			else {
				// Not folding, we finished reading a line; break out of the loop
				ungetc(c,file);
				break;
			}
		}
	}

	if (buf != NULL && cnt >= 0)
		buf[cnt] = '\0';

	if (buffer)
		*buffer = buf;
	else if (buf)
		free(buf);

	if (buflen)
		*buflen = len;

	return cnt;
}


/**
 * @brief Reads one (possibly folded) header line from a BPositionIO stream.
 *
 * Behaves identically to the FILE-based overload but reads from a BPositionIO,
 * using single-byte Read() calls. The stream position is backed up one byte
 * if a non-whitespace character is read as a lookahead.
 *
 * @param in      Source BPositionIO to read from.
 * @param buffer  In/out: pointer to the buffer; reallocated as necessary.
 * @param buflen  In/out: pointer to the allocated buffer size.
 * @return Number of significant bytes in the buffer, 0 for an empty line,
 *         or a negative error code on failure.
 */
ssize_t
readfoldedline(BPositionIO &in, char **buffer, size_t *buflen)
{
	ssize_t len = buflen && *buflen ? *buflen : 0;
	char * buf = buffer && *buffer ? *buffer : NULL;
	ssize_t cnt = 0; // Number of characters currently in the buffer.
	char c;
	status_t errorCode;

	while (true) {
		// Make sure there is space in the buffer for two more characters (one
		// for the next character, and one for the end of string NUL byte).
		if (buf == NULL || cnt + 2 >= len) {
			char *temp = (char *)realloc(buf, len + 64);
			if (temp == NULL) {
				// Out of memory, however existing buffer remains allocated.
				cnt = ENOMEM;
				break;
			}
			len += 64;
			buf = temp;
		}

		errorCode = in.Read (&c,1); // A really slow way of reading - unbuffered.
		if (errorCode != 1) {
			if (errorCode < 0) {
				cnt = errorCode; // IO error encountered, just return the code.
			} else {
				// Really is end of file.  Also make it end of line if there is
				// some text already read in.  If the first thing read was EOF,
				// just return an empty string.
				if (cnt > 0) {
					buf[cnt++] = '\n';
					if (buf[cnt-2] == '\r') {
						buf[cnt-2] = '\n';
						--cnt;
					}
				}
			}
			break;
		}

		buf[cnt++] = c;

		if (c == '\n') {
			// Convert CRLF end of line to just a LF.  Do it before folding, in
			// case we don't need to fold.
			if (cnt >= 2 && buf[cnt-2] == '\r') {
				buf[cnt-2] = '\n';
				--cnt;
			}
			// If the current line is empty then return it (so that empty lines
			// don't disappear if the next line starts with a space).
			if (cnt <= 1)
				break;
			// if first character on the next line is whitespace, fold lines
			errorCode = in.Read(&c,1);
			if (errorCode == 1) {
				if (c == ' ' || c == '\t')
					buf[cnt-1] = c; // Replace \n with the white space character.
				else {
					// Not folding, we finished reading a whole line.
					in.Seek(-1,SEEK_CUR); // Undo the look-ahead character read.
					break;
				}
			} else if (errorCode < 0) {
				cnt = errorCode;
				break;
			} else // No next line; at the end of the file.  Return the line.
				break;
		}
	}

	if (buf != NULL && cnt >= 0)
		buf[cnt] = '\0';

	if (buffer)
		*buffer = buf;
	else if (buf)
		free(buf);

	if (buflen)
		*buflen = len;

	return cnt;
}


/**
 * @brief Reads one (possibly folded) header line from an in-memory string pointer.
 *
 * Advances *header through the string, folding continuation lines (those
 * starting with whitespace). CRLF endings are normalised to LF.
 *
 * @param header  In/out: pointer to the current position in the header string;
 *                advanced past the consumed characters.
 * @param buffer  In/out: pointer to the output buffer; reallocated as necessary.
 * @param buflen  In/out: pointer to the allocated buffer size.
 * @return Number of significant bytes in the buffer, 0 for an empty line,
 *         or a negative errno on allocation failure.
 */
ssize_t
nextfoldedline(const char** header, char **buffer, size_t *buflen)
{
	ssize_t len = buflen && *buflen ? *buflen : 0;
	char * buf = buffer && *buffer ? *buffer : NULL;
	ssize_t cnt = 0; // Number of characters currently in the buffer.
	char c;

	while (true)
	{
		// Make sure there is space in the buffer for two more characters (one
		// for the next character, and one for the end of string NUL byte).
		if (buf == NULL || cnt + 2 >= len)
		{
			char *temp = (char *)realloc(buf, len + 64);
			if (temp == NULL) {
				// Out of memory, however existing buffer remains allocated.
				cnt = ENOMEM;
				break;
			}
			len += 64;
			buf = temp;
		}

		// Read the next character, or end of file.
		if ((c = *(*header)++) == 0) {
			// End of file.  Also make it end of line if there is some text
			// already read in.  If the first thing read was EOF, just return
			// an empty string.
			if (cnt > 0) {
				buf[cnt++] = '\n';
				if (buf[cnt-2] == '\r') {
					buf[cnt-2] = '\n';
					--cnt;
				}
			}
			break;
		}

		buf[cnt++] = c;

		if (c == '\n') {
			// Convert CRLF end of line to just a LF.  Do it before folding, in
			// case we don't need to fold.
			if (cnt >= 2 && buf[cnt-2] == '\r') {
				buf[cnt-2] = '\n';
				--cnt;
			}
			// If the current line is empty then return it (so that empty lines
			// don't disappear if the next line starts with a space).
			if (cnt <= 1)
				break;
			// if first character on the next line is whitespace, fold lines
			c = *(*header)++;
			if (c == ' ' || c == '\t')
				buf[cnt-1] = c; // Replace \n with the white space character.
			else {
				// Not folding, we finished reading a line; break out of the loop
				(*header)--; // Undo read of the non-whitespace.
				break;
			}
		}
	}


	if (buf != NULL && cnt >= 0)
		buf[cnt] = '\0';

	if (buffer)
		*buffer = buf;
	else if (buf)
		free(buf);

	if (buflen)
		*buflen = len;

	return cnt;
}


/**
 * @brief Trims leading and trailing whitespace from a BString in-place.
 *
 * @param string  String to modify.
 */
void
trim_white_space(BString &string)
{
	int32 i;
	int32 length = string.Length();
	char *buffer = string.LockBuffer(length + 1);

	while (length > 0 && isspace(buffer[length - 1]))
		length--;
	buffer[length] = '\0';

	for (i = 0; buffer[i] && isspace(buffer[i]); i++) {}
	if (i != 0) {
		length -= i;
		memmove(buffer,buffer + i,length + 1);
	}
	string.UnlockBuffer(length);
}


/**
 * @brief Extracts a human-readable display name from a From or To header value.
 *
 * Tries four patterns in order of preference: "name" in brackets, "name" in
 * quotes, bare name before angle brackets, and the whole string. The result
 * is trimmed and \a header is replaced with the extracted name.
 *
 * @param header  In/out: From or To header value; replaced with the display name.
 */
void
extract_address_name(BString &header)
{
	BString name;
	const char *start = header.String();
	const char *stop = start + strlen (start);

	// Find a string S in the header (email foo) that matches:
	//   Old style name in brackets: foo@bar.com (S)
	//   New style quotes: "S" <foo@bar.com>
	//   New style no quotes if nothing else found: S <foo@bar.com>
	//   If nothing else found then use the whole thing: S

	for (int i = 0; i <= 3; i++) {
		// Set p1 to the first letter in the name and p2 to just past the last
		// letter in the name.  p2 stays NULL if a name wasn't found in this
		// pass.
		const char *p1 = NULL, *p2 = NULL;

		switch (i) {
			case 0: // foo@bar.com (S)
				if ((p1 = strchr(start,'(')) != NULL) {
					p1++; // Advance to first letter in the name.
					size_t nest = 1; // Handle nested brackets.
					for (p2 = p1; p2 < stop; ++p2)
					{
						if (*p2 == ')')
							--nest;
						else if (*p2 == '(')
							++nest;
						if (nest <= 0)
							break;
					}
					if (nest != 0)
						p2 = NULL; // False alarm, no terminating bracket.
				}
				break;
			case 1: // "S" <foo@bar.com>
				if ((p1 = strchr(start, '\"')) != NULL)
					p2 = strchr(++p1, '\"');
				break;
			case 2: // S <foo@bar.com>
				p1 = start;
				if (name.Length() == 0)
					p2 = strchr(start, '<');
				break;
			case 3: // S
				p1 = start;
				if (name.Length() == 0)
					p2 = stop;
				break;
		}

		// Remove leading and trailing space-like characters and save the
		// result if it is longer than any other likely names found.
		if (p2 != NULL) {
			while (p1 < p2 && (isspace (*p1)))
				++p1;

			while (p1 < p2 && (isspace (p2[-1])))
				--p2;

			int newLength = p2 - p1;
			if (name.Length() < newLength)
				name.SetTo(p1, newLength);
		}
	}

	int32 lessIndex = name.FindFirst('<');
	int32 greaterIndex = name.FindLast('>');

	if (lessIndex == 0) {
		// Have an address of the form <address> and nothing else, so remove
		// the greater and less than signs, if any.
		if (greaterIndex > 0)
			name.Remove(greaterIndex, 1);
		name.Remove(lessIndex, 1);
	} else if (lessIndex > 0 && lessIndex < greaterIndex) {
		// Yahoo stupidly inserts the e-mail address into the name string, so
		// this bit of code fixes: "Joe <joe@yahoo.com>" <joe@yahoo.com>
		name.Remove(lessIndex, greaterIndex - lessIndex + 1);
	}

	trim_white_space(name);
	header = name;
}


/**
 * @brief Strips Re:, Fwd:, list tags, and other prefixes from a subject string.
 *
 * Removes leading whitespace, mailing-list tags (e.g. [listname]), Re:/Fwd:
 * prefixes and variants, and trailing "(fwd)" markers using a compiled POSIX
 * extended regex. The regex is compiled once on first call. The result is the
 * core subject string used as the THREAD attribute for message grouping.
 *
 * @param string  Subject string to normalise in-place.
 */
void
SubjectToThread (BString &string)
{
// a regex that matches a non-ASCII UTF8 character:
#define U8C \
	"[\302-\337][\200-\277]" \
	"|\340[\302-\337][\200-\277]" \
	"|[\341-\357][\200-\277][\200-\277]" \
	"|\360[\220-\277][\200-\277][\200-\277]" \
	"|[\361-\367][\200-\277][\200-\277][\200-\277]" \
	"|\370[\210-\277][\200-\277][\200-\277][\200-\277]" \
	"|[\371-\373][\200-\277][\200-\277][\200-\277][\200-\277]" \
	"|\374[\204-\277][\200-\277][\200-\277][\200-\277][\200-\277]" \
	"|\375[\200-\277][\200-\277][\200-\277][\200-\277][\200-\277]"

#define PATTERN \
	"^ +" \
	"|^(\\[[^]]*\\])(\\<|  +| *(\\<(\\w|" U8C "){2,3} *(\\[[^\\]]*\\])? *:)+ *)" \
	"|^(  +| *(\\<(\\w|" U8C "){2,3} *(\\[[^\\]]*\\])? *:)+ *)" \
	"| *\\(fwd\\) *$"

	if (gRebuf == NULL && atomic_add(&gLocker, 1) == 0) {
		// the idea is to compile the regexp once to speed up testing

		for (int i=0; i<256; ++i) gTranslation[i]=i;
		for (int i='a'; i<='z'; ++i) gTranslation[i]=toupper(i);

		gRe.translate = gTranslation;
		gRe.regs_allocated = REGS_FIXED;
		re_syntax_options = RE_SYNTAX_POSIX_EXTENDED;

		const char *pattern = PATTERN;
		// count subexpressions in PATTERN
		for (unsigned int i=0; pattern[i] != 0; ++i)
		{
			if (pattern[i] == '\\')
				++i;
			else if (pattern[i] == '(')
				++gNsub;
		}

		const char *err = re_compile_pattern(pattern,strlen(pattern),&gRe);
		if (err == NULL)
			gRebuf = &gRe;
		else
			fprintf(stderr, "Failed to compile the regex: %s\n", err);
	} else {
		int32 tries = 200;
		while (gRebuf == NULL && tries-- > 0)
			snooze(10000);
	}

	if (gRebuf) {
		struct re_registers regs;
		// can't be static if this function is to be thread-safe

		regs.num_regs = gNsub;
		regs.start = (regoff_t*)malloc(gNsub*sizeof(regoff_t));
		regs.end = (regoff_t*)malloc(gNsub*sizeof(regoff_t));

		for (int start = 0; (start = re_search(gRebuf, string.String(),
				string.Length(), 0, string.Length(), &regs)) >= 0;) {
			//
			// we found something
			//

			// don't delete [bemaildaemon]...
			if (start == regs.start[1])
				start = regs.start[2];

			string.Remove(start,regs.end[0]-start);
			if (start)
				string.Insert(' ',1,start);

			// TODO: for some subjects this results in an endless loop, check
			// why this happen.
			if (regs.end[0] - start <= 1)
				break;
		}

		free(regs.start);
		free(regs.end);
	}

	// Finally remove leading and trailing space.  Some software, like
	// tm-edit 1.8, appends a space to the subject, which would break
	// threading if we left it in.
	trim_white_space(string);
}


/**
 * @brief Parses an RFC 2822 date string, including numeric timezone offsets.
 *
 * Strips parenthesised timezone comments, then extracts and removes any
 * trailing "+HHMM" or "-HHMM" numeric timezone, replaces it with "GMT" so
 * parsedate() can handle the string, and then applies the timezone delta
 * manually to produce a UTC time_t.
 *
 * @param DateString  Null-terminated RFC 2822 date string.
 * @return UTC Unix timestamp, or -1 on parse failure.
 */
time_t
ParseDateWithTimeZone(const char *DateString)
{
	time_t currentTime;
	time_t dateAsTime;
	char tempDateString[80];
	char tempZoneString[6];
	time_t zoneDeltaTime;
	int zoneIndex;
	char *zonePntr;

	// See if we can remove the time zone portion.  parsedate understands time
	// zone 3 letter names, but doesn't understand the numeric +9999 time zone
	// format.  To do: see if a newer parsedate exists.

	strncpy (tempDateString, DateString, sizeof (tempDateString));
	tempDateString[sizeof (tempDateString) - 1] = 0;

	// Remove trailing spaces.
	zonePntr = tempDateString + strlen (tempDateString) - 1;
	while (zonePntr >= tempDateString && isspace (*zonePntr))
		*zonePntr-- = 0;
	if (zonePntr < tempDateString)
		return -1; // Empty string.

	// Remove the trailing time zone in round brackets, like in
	// Fri, 22 Feb 2002 15:22:42 EST (-0500)
	// Thu, 25 Apr 1996 11:44:19 -0400 (EDT)
	if (tempDateString[strlen(tempDateString)-1] == ')')
	{
		zonePntr = strrchr (tempDateString, '(');
		if (zonePntr != NULL)
		{
			*zonePntr-- = 0; // Zap the '(', then remove trailing spaces.
			while (zonePntr >= tempDateString && isspace (*zonePntr))
				*zonePntr-- = 0;
			if (zonePntr < tempDateString)
				return -1; // Empty string.
		}
	}

	// Look for a numeric time zone like  Tue, 30 Dec 2003 05:01:40 +0000
	for (zoneIndex = strlen (tempDateString); zoneIndex >= 0; zoneIndex--)
	{
		zonePntr = tempDateString + zoneIndex;
		if (zonePntr[0] == '+' || zonePntr[0] == '-')
		{
			if (zonePntr[1] >= '0' && zonePntr[1] <= '9' &&
				zonePntr[2] >= '0' && zonePntr[2] <= '9' &&
				zonePntr[3] >= '0' && zonePntr[3] <= '9' &&
				zonePntr[4] >= '0' && zonePntr[4] <= '9')
				break;
		}
	}
	if (zoneIndex >= 0)
	{
		// Remove the zone from the date string and any following time zone
		// letter codes.  Also put in GMT so that the date gets parsed as GMT.
		memcpy (tempZoneString, zonePntr, 5);
		tempZoneString [5] = 0;
		strcpy (zonePntr, "GMT");
	}
	else // No numeric time zone found.
		strcpy (tempZoneString, "+0000");

	time (&currentTime);
	dateAsTime = parsedate (tempDateString, currentTime);
	if (dateAsTime == (time_t) -1)
		return -1; // Failure.

	zoneDeltaTime = 60 * atol (tempZoneString + 3); // Get the last two digits - minutes.
	tempZoneString[3] = 0;
	zoneDeltaTime += atol (tempZoneString + 1) * 60 * 60; // Get the first two digits - hours.
	if (tempZoneString[0] == '+')
		zoneDeltaTime = 0 - zoneDeltaTime;
	dateAsTime += zoneDeltaTime;

	return dateAsTime;
}


/**
 * @brief Parses all RFC 822 headers from a BPositionIO stream into a BMessage.
 *
 * Reads folded header lines until a blank line is encountered, decodes each
 * line with rfc2047_to_utf8(), and stores the field/value pairs in \a headers.
 * Field names are capitalised with CapitalizeEachWord() for uniform lookup.
 *
 * @param headers  Output BMessage to receive the parsed header fields.
 * @param input    Stream positioned at the start of the headers.
 * @return B_OK on success.
 */
status_t
parse_header(BMessage &headers, BPositionIO &input)
{
	char *buffer = NULL;
	size_t bufferSize = 0;
	int32 length;

	while ((length = readfoldedline(input, &buffer, &bufferSize)) >= 2) {
		--length;
			// Don't include the \n at the end of the buffer.

		// convert to UTF-8 and null-terminate the buffer
		length = rfc2047_to_utf8(&buffer, &bufferSize, length);
		buffer[length] = '\0';

		const char *delimiter = strstr(buffer, ":");
		if (delimiter == NULL)
			continue;

		BString header(buffer, delimiter - buffer);
		header.CapitalizeEachWord();
			// unified case for later fetch

		delimiter++; // Skip the colon.
		// Skip over leading white space and tabs.
		// TODO: (comments in brackets).
		while (isspace(*delimiter))
			delimiter++;

		// TODO: implement joining of multiple header tags (i.e. multiple "Cc:"s)
		headers.AddString(header.String(), delimiter);
	}
	free(buffer);

	return B_OK;
}


/**
 * @brief Extracts the value of a named field from a raw RFC 822 header block.
 *
 * Searches \a header for a line starting with \a field followed by ':',
 * collects continuation lines, decodes rfc2047 encoding, and trims whitespace.
 *
 * @param header  Complete raw header block as a BString (e.g. read from the file).
 * @param field   Header field name to search for (case-insensitive).
 * @param target  Output BString to receive the decoded field value.
 * @return B_OK if found, B_BAD_VALUE if \a field is not present.
 */
status_t
extract_from_header(const BString& header, const BString& field,
	BString& target)
{
	int32 headerLength = header.Length();
	int32 fieldEndPos = 0;
	while (true) {
		int32 pos = header.IFindFirst(field, fieldEndPos);
		if (pos < 0)
			return B_BAD_VALUE;
		fieldEndPos = pos + field.Length();

		if (pos != 0 && header.ByteAt(pos - 1) != '\n')
			continue;
		if (header.ByteAt(fieldEndPos) == ':')
			break;
	}
	fieldEndPos++;

	int32 crPos = fieldEndPos;
	while (true) {
		fieldEndPos = crPos;
		crPos = header.FindFirst('\n', crPos);
		if (crPos < 0)
			crPos = headerLength;
		BString temp;
		header.CopyInto(temp, fieldEndPos, crPos - fieldEndPos);
		if (header.ByteAt(crPos - 1) == '\r') {
			temp.Truncate(temp.Length() - 1);
			temp += " ";
		}
		target += temp;
		crPos++;
		if (crPos >= headerLength)
			break;
		char nextByte = header.ByteAt(crPos);
		if (nextByte != ' ' && nextByte != '\t')
			break;
		crPos++;
	}

	size_t bufferSize = target.Length();
	char* buffer = target.LockBuffer(bufferSize);
	size_t length = rfc2047_to_utf8(&buffer, &bufferSize, bufferSize);
	target.UnlockBuffer(length);

	trim_white_space(target);

	return B_OK;
}


/**
 * @brief Extracts a bare e-mail address from a header string, modifying it in place.
 *
 * Removes quoted strings, angle-bracket notation, and parenthesised display
 * names, leaving only the raw address (e.g. "user\@example.com").
 *
 * @param address  In/out: header value replaced with the bare address.
 */
void
extract_address(BString &address)
{
	const char *string = address.String();
	int32 first;

	// first, remove all quoted text

	if ((first = address.FindFirst('"')) >= 0) {
		int32 last = first + 1;
		while (string[last] && string[last] != '"')
			last++;

		if (string[last] == '"')
			address.Remove(first, last + 1 - first);
	}

	// try to extract the address now

	if ((first = address.FindFirst('<')) >= 0) {
		// the world likes us and we can just get the address the easy way...
		int32 last = address.FindFirst('>');
		if (last >= 0) {
			address.Truncate(last);
			address.Remove(0, first + 1);

			return;
		}
	}

	// then, see if there is anything in parenthesis to throw away

	if ((first = address.FindFirst('(')) >= 0) {
		int32 last = first + 1;
		while (string[last] && string[last] != ')')
			last++;

		if (string[last] == ')')
			address.Remove(first, last + 1 - first);
	}

	// now, there shouldn't be much else left

	trim_white_space(address);
}


/**
 * @brief Splits a comma-separated address list and appends each address to \a list.
 *
 * Handles quoted strings (which may contain commas) correctly. After splitting,
 * each address is trimmed and optionally transformed by \a cleanupFunc before
 * being heap-duplicated and added to \a list.
 *
 * @param list         BList to receive heap-allocated (strdup) address strings.
 * @param string       Comma-separated address list string, or NULL.
 * @param cleanupFunc  Optional function to apply to each address (e.g. extract_address).
 */
void
get_address_list(BList &list, const char *string,
	void (*cleanupFunc)(BString &))
{
	if (string == NULL || !string[0])
		return;

	const char *start = string;

	while (true) {
		if (string[0] == '"') {
			const char *quoteEnd = ++string;

			while (quoteEnd[0] && quoteEnd[0] != '"')
				quoteEnd++;

			if (!quoteEnd[0])	// string exceeds line!
				quoteEnd = string;

			string = quoteEnd + 1;
		}

		if (string[0] == ',' || string[0] == '\0') {
			BString address(start, string - start);
			trim_white_space(address);

			if (cleanupFunc)
				cleanupFunc(address);

			list.AddItem(strdup(address.String()));

			start = string + 1;
		}

		if (!string[0])
			break;

		string++;
	}
}


/**
 * @brief Copies the mail-folder Tracker query template attributes to a target path.
 *
 * Reads attribute metadata from the system's DefaultQueryTemplates/text_x-email
 * file and copies them to \a targetPath so that new mail folders show the
 * correct columns in Tracker.
 *
 * @param targetPath  Null-terminated path of the target mail folder.
 * @return B_OK on success, or an error code from find_directory() or CopyAttributes().
 */
status_t
CopyMailFolderAttributes(const char* targetPath)
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;

	path.Append("Tracker");
	path.Append("DefaultQueryTemplates");
	path.Append("text_x-email");

	BNode source(path.Path());
	BNode target(targetPath);
	return BPrivate::CopyAttributes(source, target);
}
