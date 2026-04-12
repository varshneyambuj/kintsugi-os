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
 *   Copyright 2011, Haiku, Inc. All rights reserved.
 *   Copyright 2001-2003 Dr. Zoidberg Enterprises. All rights reserved.
 */


/**
 * @file mail_encoding.cpp
 * @brief MIME transfer encoding and decoding routines for the mail kit.
 *
 * Provides encode(), decode(), max_encoded_length(), encoding_for_cte(),
 * decode_qp(), encode_qp(), uu_decode(), and encode_base64() / decode_base64()
 * (the latter pair declared in mail_encoding.h). These functions are used
 * internally by BSimpleMailAttachment and BTextMailComponent to transform
 * message body bytes between raw binary and the RFC 2045 transfer encodings.
 *
 * @see BSimpleMailAttachment, BTextMailComponent, mail_encoding.h
 */


#include <ctype.h>
#include <string.h>
#include <strings.h>

#include <SupportDefs.h>

#include <mail_encoding.h>


/** @brief Macro for UUencoding: converts an encoded byte back to its 6-bit value. */
#define	DEC(c) (((c) - ' ') & 077)


/** @brief Hex alphabet used when encoding quoted-printable escape sequences. */
static const char kHexAlphabet[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
	'8','9','A','B','C','D','E','F'};


/**
 * @brief Encodes \a length bytes from \a in into \a out using the specified encoding.
 *
 * Dispatches to encode_base64(), encode_qp(), or a plain memcpy() depending
 * on \a encoding. UUencode is not supported for encoding and returns -1.
 *
 * @param encoding    Transfer encoding to apply.
 * @param out         Output buffer; must be large enough (see max_encoded_length()).
 * @param in          Input bytes to encode.
 * @param length      Number of bytes in \a in.
 * @param headerMode  If non-zero, encode for use in an RFC 2047 header word
 *                    (spaces become underscores in QP mode).
 * @return Number of bytes written to \a out, or -1 for unsupported encodings.
 */
ssize_t
encode(mail_encoding encoding, char *out, const char *in, off_t length,
	int headerMode)
{
	switch (encoding) {
		case base64:
			return encode_base64(out,in,length,headerMode);
		case quoted_printable:
			return encode_qp(out,in,length,headerMode);
		case seven_bit:
		case eight_bit:
		case no_encoding:
			memcpy(out,in,length);
			return length;
		case uuencode:
		default:
			return -1;
	}

	return -1;
}


/**
 * @brief Decodes \a length encoded bytes from \a in into \a out.
 *
 * Dispatches to decode_base64(), uu_decode(), decode_qp(), or a plain
 * memcpy() depending on \a encoding.
 *
 * @param encoding             Transfer encoding of the input data.
 * @param out                  Output buffer; must be at least \a length bytes.
 * @param in                   Encoded input bytes.
 * @param length               Number of bytes in \a in.
 * @param underscoreIsSpace    If non-zero, treat '_' as a space (for QP header mode).
 * @return Number of decoded bytes written to \a out, or -1 on error.
 */
ssize_t
decode(mail_encoding encoding, char *out, const char *in, off_t length,
	int underscoreIsSpace)
{
	switch (encoding) {
		case base64:
			return decode_base64(out, in, length);
		case uuencode:
			return uu_decode(out, in, length);
		case seven_bit:
		case eight_bit:
		case no_encoding:
			memcpy(out, in, length);
			return length;
		case quoted_printable:
			return decode_qp(out, in, length, underscoreIsSpace);
		default:
			break;
	}

	return -1;
}


/**
 * @brief Returns the maximum number of bytes that encoding \a length bytes may produce.
 *
 * Useful for pre-allocating output buffers before calling encode().
 * UUencode and unknown encodings return -1.
 *
 * @param encoding  Transfer encoding to query.
 * @param length    Number of raw input bytes.
 * @return Upper bound on encoded output size in bytes, or -1 if unsupported.
 */
ssize_t
max_encoded_length(mail_encoding encoding, off_t length)
{
	switch (encoding) {
		case base64:
		{
			double result = length * 1.33333333333333;
			result += (result / BASE64_LINELENGTH) * 2 + 20;
			return (ssize_t)(result);
		}
		case quoted_printable:
			return length * 3;
		case seven_bit:
		case eight_bit:
		case no_encoding:
			return length;
		case uuencode:
		default:
			return -1;
	}

	return -1;
}


/**
 * @brief Maps a Content-Transfer-Encoding header string to a mail_encoding constant.
 *
 * Performs a case-insensitive comparison against the standard CTE names.
 *
 * @param cte  Null-terminated CTE string from a MIME header, or NULL.
 * @return Corresponding mail_encoding value; returns no_encoding for NULL or
 *         unrecognised values.
 */
mail_encoding
encoding_for_cte(const char *cte)
{
	if (cte == NULL)
		return no_encoding;

	if (strcasecmp(cte,"uuencode") == 0)
		return uuencode;
	if (strcasecmp(cte,"base64") == 0)
		return base64;
	if (strcasecmp(cte,"quoted-printable") == 0)
		return quoted_printable;
	if (strcasecmp(cte,"7bit") == 0)
		return seven_bit;
	if (strcasecmp(cte,"8bit") == 0)
		return eight_bit;

	return no_encoding;
}


/**
 * @brief Decodes a quoted-printable encoded byte sequence.
 *
 * Processes "=XX" escape sequences into their single-byte equivalents and
 * strips soft line breaks ("=\r\n"). If \a underscoreIsSpace is non-zero,
 * underscore characters are replaced with spaces (for header word decoding).
 *
 * @param out               Output buffer; must be at least \a length bytes.
 * @param in                QP-encoded input bytes.
 * @param length            Number of bytes in \a in.
 * @param underscoreIsSpace If non-zero, decode '_' as 0x20.
 * @return Number of decoded bytes written to \a out (null-terminated).
 */
ssize_t
decode_qp(char *out, const char *in, off_t length, int underscoreIsSpace)
{
	// decode Quoted Printable
	char *dataout = out;
	const char *datain = in, *dataend = in + length;

	while (datain < dataend) {
		if (*datain == '=' && dataend - datain > 2) {
			int a = toupper(datain[1]);
			a -= a >= '0' && a <= '9' ? '0' : (a >= 'A' && a <= 'F'
				? 'A' - 10 : a + 1);

			int b = toupper(datain[2]);
			b -= b >= '0' && b <= '9' ? '0' : (b >= 'A' && b <= 'F'
				? 'A' - 10 : b + 1);

			if (a >= 0 && b >= 0) {
				*dataout++ = (a << 4) + b;
				datain += 3;
				continue;
			} else if (datain[1] == '\r' && datain[2] == '\n') {
				// strip =<CR><NL>
				datain += 3;
				continue;
			}
		} else if (*datain == '_' && underscoreIsSpace) {
			*dataout++ = ' ';
			++datain;
			continue;
		}

		*dataout++ = *datain++;
	}

	*dataout = '\0';
	return dataout - out;
}


/**
 * @brief Encodes bytes using quoted-printable, optionally in header mode.
 *
 * Characters above 127, '?', '=', '_', and (in header mode) spaces and
 * control codes are encoded as "=XX". In header mode spaces become '_'.
 * The "From " sequence at line starts is also encoded to avoid mbox confusion.
 *
 * @param out         Output buffer; caller must ensure it is at least
 *                    3x the size of \a in.
 * @param in          Input bytes to encode.
 * @param length      Number of bytes in \a in.
 * @param headerMode  If non-zero, encode for use in an RFC 2047 header word.
 * @return Number of bytes written to \a out.
 */
ssize_t
encode_qp(char *out, const char *in, off_t length, int headerMode)
{
	int g = 0, i = 0;

	for (; i < length; i++) {
		if (((uint8 *)(in))[i] > 127 || in[i] == '?' || in[i] == '='
			|| in[i] == '_'
			// Also encode the letter F in "From " at the start of the line,
			// which Unix systems use to mark the start of messages in their
			// mbox files.
			|| (in[i] == 'F' && i + 5 <= length && (i == 0 || in[i - 1] == '\n')
				&& in[i + 1] == 'r' && in[i + 2] == 'o' && in[i + 3] == 'm'
				&& in[i + 4] == ' ')) {
			out[g++] = '=';
			out[g++] = kHexAlphabet[(in[i] >> 4) & 0x0f];
			out[g++] = kHexAlphabet[in[i] & 0x0f];
		} else if (headerMode && (in[i] == ' ' || in[i] == '\t')) {
			out[g++] = '_';
		} else if (headerMode && in[i] >= 0 && in[i] < 32) {
			// Control codes in headers need to be sanitized, otherwise certain
			// Japanese ISPs mangle the headers badly.  But they don't mangle
			// the body.
			out[g++] = '=';
			out[g++] = kHexAlphabet[(in[i] >> 4) & 0x0f];
			out[g++] = kHexAlphabet[in[i] & 0x0f];
		} else
			out[g++] = in[i];
	}

	return g;
}


/**
 * @brief Decodes a UUencoded data block.
 *
 * Scans for the "begin" marker and then decodes lines of UUencoded data until
 * the "end" marker is reached. Each encoded line uses a 6-bit-per-character
 * packing scheme decoded via the DEC() macro.
 *
 * @param out     Output buffer; must be large enough to hold the decoded data.
 * @param in      UUencoded input bytes (including the "begin"/"end" markers).
 * @param length  Number of bytes in \a in.
 * @return Number of decoded bytes written to \a out.
 */
ssize_t
uu_decode(char *out, const char *in, off_t length)
{
	long n;
	uint8 *p, *inBuffer = (uint8 *)in;
	uint8 *outBuffer = (uint8 *)out;

	inBuffer = (uint8 *)strstr((char *)inBuffer, "begin");
	goto enterLoop;

	while ((inBuffer - (uint8 *)in) <= length
		&& strncmp((char *)inBuffer, "end", 3)) {
		p = inBuffer;
		n = DEC(inBuffer[0]);

		for (++inBuffer; n > 0; inBuffer += 4, n -= 3) {
			if (n >= 3) {
				*outBuffer++ = DEC(inBuffer[0]) << 2 | DEC (inBuffer[1]) >> 4;
				*outBuffer++ = DEC(inBuffer[1]) << 4 | DEC (inBuffer[2]) >> 2;
				*outBuffer++ = DEC(inBuffer[2]) << 6 | DEC (inBuffer[3]);
			} else {
				if (n >= 1) {
					*outBuffer++ = DEC(inBuffer[0]) << 2
						| DEC (inBuffer[1]) >> 4;
				}
				if (n >= 2) {
					*outBuffer++ = DEC(inBuffer[1]) << 4
						| DEC (inBuffer[2]) >> 2;
				}
			}
		}
		inBuffer = p;

	enterLoop:
		while (inBuffer[0] != '\n' && inBuffer[0] != '\r' && inBuffer[0] != 0)
			inBuffer++;
		while (inBuffer[0] == '\n' || inBuffer[0] == '\r')
			inBuffer++;
	}

	return (ssize_t)(outBuffer - (uint8 *)in);
}
