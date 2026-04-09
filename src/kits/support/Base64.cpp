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
 *   Copyright 2011-2013 Haiku, Inc. All rights reserved.
 *   Copyright 2001-2003 Dr. Zoidberg Enterprises. All rights reserved.
 */


/**
 * @file Base64.cpp
 * @brief RFC 2045-compatible Base64 encoding and decoding routines.
 *
 * Provides encode_base64() for encoding arbitrary binary data to Base64
 * ASCII and decode_base64() for the reverse operation. A "header mode"
 * flag suppresses line-length wrapping so the output can be embedded
 * directly in mail header fields.
 *
 * @see encode_base64(), decode_base64()
 */


#include <mail_encoding.h>

#include <ctype.h>
#include <string.h>
#include <SupportDefs.h>


/// The standard Base64 alphabet (RFC 4648 Table 1).
static const char kBase64Alphabet[64] = {
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
	'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
	'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',
	'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
	'+',
	'/'
};


/**
 * @brief Encode binary data as a Base64 ASCII string.
 *
 * Reads \a length bytes from \a in and writes the Base64-encoded
 * representation into \a out. The output buffer must be large enough to
 * hold the encoded data (approximately ceil(length / 3) * 4 bytes plus
 * CRLF line-break characters when not in header mode).
 *
 * In non-header mode, CRLF line breaks are inserted every
 * BASE64_LINELENGTH characters to comply with MIME line-length limits.
 * In header mode (headerMode != 0), line breaks are suppressed so the
 * result can be embedded in a single mail header value.
 *
 * Padding '=' characters are appended when the input length is not a
 * multiple of three.
 *
 * @param out        Destination buffer for the encoded output. Not
 *                   NUL-terminated by this function.
 * @param in         Source binary data.
 * @param length     Number of bytes to encode from \a in.
 * @param headerMode Pass non-zero to suppress CRLF line wrapping (for
 *                   use inside mail header fields).
 * @return Number of bytes written to \a out (excluding any NUL terminator).
 * @see decode_base64()
 */
ssize_t
encode_base64(char *out, const char *in, off_t length, int headerMode)
{
	uint32 concat;
	int i = 0;
	int k = 0;
	int lineLength = 4;
		// Stop before it actually gets too long

	while (i < length) {
		concat = ((in[i] & 0xff) << 16);

		if ((i+1) < length)
			concat |= ((in[i+1] & 0xff) << 8);
		if ((i+2) < length)
			concat |= (in[i+2] & 0xff);

		i += 3;

		out[k++] = kBase64Alphabet[(concat >> 18) & 63];
		out[k++] = kBase64Alphabet[(concat >> 12) & 63];
		out[k++] = kBase64Alphabet[(concat >> 6) & 63];
		out[k++] = kBase64Alphabet[concat & 63];

		if (i >= length) {
			int v;
			for (v = 0; v <= (i - length); v++)
				out[k-v] = '=';
		}

		lineLength += 4;

		// No line breaks in header mode, since the text is part of a Subject:
		// line or some other single header line.  The header code will do word
		// wrapping separately from this encoding stuff.
		if (!headerMode && lineLength > BASE64_LINELENGTH) {
			out[k++] = '\r';
			out[k++] = '\n';

			lineLength = 4;
		}
	}

	return k;
}


/**
 * @brief Decode a Base64-encoded string back to binary data.
 *
 * Reads \a length characters from \a in, decodes the Base64 stream, and
 * writes the resulting bytes into \a out. CRLF sequences embedded in the
 * input are skipped. Lines containing invalid (non-Base64) characters are
 * discarded in their entirety and the output position is rolled back to
 * the start of that line. Padding '=' characters terminate the current
 * 4-character group early.
 *
 * The output buffer \a out must be large enough to hold the decoded data
 * (at most ceil(length / 4) * 3 bytes).
 *
 * @param out    Destination buffer for the decoded binary output.
 * @param in     Source Base64-encoded string (need not be NUL-terminated).
 * @param length Number of characters to decode from \a in.
 * @return Number of bytes written to \a out.
 * @see encode_base64()
 */
ssize_t
decode_base64(char *out, const char *in, off_t length)
{
	uint32 concat, value;
	int lastOutLine = 0;
	int i, j;
	int outIndex = 0;

	for (i = 0; i < length; i += 4) {
		concat = 0;

		for (j = 0; j < 4 && (i + j) < length; j++) {
			value = in[i + j];

			if (value == '\n' || value == '\r') {
				// jump over line breaks
				lastOutLine = outIndex;
				i++;
				j--;
				continue;
			}

			if ((value >= 'A') && (value <= 'Z'))
				value -= 'A';
			else if ((value >= 'a') && (value <= 'z'))
				value = value - 'a' + 26;
			else if ((value >= '0') && (value <= '9'))
				value = value - '0' + 52;
			else if (value == '+')
				value = 62;
			else if (value == '/')
				value = 63;
			else if (value == '=')
				break;
			else {
				// there is an invalid character in this line - we will
				// ignore the whole line and go to the next
				outIndex = lastOutLine;
				while (i < length && in[i] != '\n' && in[i] != '\r')
					i++;
				concat = 0;
			}

			value = value << ((3-j)*6);

			concat |= value;
		}

		if (j > 1)
			out[outIndex++] = (concat & 0x00ff0000) >> 16;
		if (j > 2)
			out[outIndex++] = (concat & 0x0000ff00) >> 8;
		if (j > 3)
			out[outIndex++] = (concat & 0x000000ff);
	}

	return outIndex;
}


#if __GNUC__ <= 2
	// BeOS-ABI compatible wrappers.
	ssize_t encode_base64(char *out, char *in, off_t length)
	{
		return encode_base64(out, in, length, 0);
	}

	ssize_t	decode_base64(char *out, const char *in, off_t length,
		bool /*replace_cr*/)
	{
		return decode_base64(out, in, length);
	}
#endif	//  __GNUC__ <= 2
