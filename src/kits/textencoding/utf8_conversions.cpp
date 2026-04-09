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
 *   Copyright 2003-2008, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *   Authors:
 *       Andrew Bachmann
 */


/**
 * @file utf8_conversions.cpp
 * @brief iconv-backed UTF-8 conversion routines exposed by the Text Encoding Kit.
 *
 * Implements three public functions:
 *  - convert_encoding()   — general-purpose encoding conversion via iconv
 *  - convert_to_utf8()    — convert an arbitrary encoding to UTF-8
 *  - convert_from_utf8()  — convert UTF-8 to an arbitrary encoding
 *
 * Invalid or incomplete input sequences are handled with a best-effort
 * strategy: unrepresentable characters are replaced with a caller-supplied
 * substitute byte, and incomplete multibyte sequences at the end of input
 * are silently discarded.
 *
 * @see BCharacterSetRoster, BCharacterSet
 */


#include <CharacterSet.h>
#include <CharacterSetRoster.h>
#include <UTF8.h>

#include <errno.h>
#include <iconv.h>
#include <stdio.h>


//#define DEBUG_CONV 1

#ifdef DEBUG_CONV
#	define DEBPRINT(ARGS) printf ARGS;
#else
#	define DEBPRINT(ARGS) ;
#endif

using namespace BPrivate;

int iconvctl(iconv_t icd, int request, void* argument);


/**
 * @brief Skips the smallest invalid input sequence that iconv cannot convert.
 *
 * When iconv returns EILSEQ the caller does not know how many bytes form the
 * offending sequence.  This helper probes increasing prefix lengths until
 * iconv either succeeds or returns EILSEQ again, then advances @p inputBuffer
 * and @p inputLeft past exactly those bytes.
 *
 * The iconv internal state is reset before each probe so that subsequent
 * conversions start clean.
 *
 * @param conversion   Pointer to the active iconv descriptor.
 * @param inputBuffer  In/out: pointer to the current read position in the
 *                     source buffer; advanced past the skipped bytes on
 *                     return.
 * @param inputLeft    In/out: number of bytes remaining; decremented by the
 *                     number of skipped bytes on return.
 */
static void
discard_invalid_input_character(iconv_t* conversion, char** inputBuffer,
	size_t* inputLeft)
{
	if (*inputLeft == 0)
		return;

	char outputBuffer[1];

	// skip the invalid input character only
	size_t left = 1;
	for (; left <= *inputLeft; left ++) {
		// reset internal state
		iconv(*conversion, NULL, NULL, NULL, NULL);

		char* buffer = *inputBuffer;
		char* output = outputBuffer;
		size_t outputLeft = 1;
		size_t size = iconv(*conversion, &buffer, &left,
			&output, &outputLeft);

		if (size != (size_t)-1) {
			// should not reach here
			break;
		}

		if (errno == EINVAL) {
			// too few input bytes provided,
			// increase input buffer size and try again
			continue;
		}

		if (errno == EILSEQ) {
			// minimal size of input buffer found
			break;
		}

		// should not reach here
	};

	*inputBuffer += left;
	*inputLeft -= left;
}


/**
 * @brief Converts a byte buffer from one character encoding to another.
 *
 * Uses iconv to transcode @p srcLen bytes from @p src in the @p from encoding
 * into @p dst in the @p to encoding.  On return, @p srcLen is updated to the
 * number of source bytes consumed and @p dstLen to the number of output bytes
 * written.
 *
 * Unrepresentable characters (EILSEQ) are replaced with @p substitute,
 * converted from ISO-8859-1 to the target encoding.  Incomplete trailing
 * multibyte sequences (EINVAL) are silently dropped.  Conversion stops early
 * when the output buffer is full (E2BIG) without returning an error.
 *
 * @note The @p state parameter is not fully implemented; multi-call stateful
 *       conversion does not currently work correctly.
 *
 * @param from       IANA name of the source encoding (e.g. "ISO-8859-1").
 * @param to         IANA name of the target encoding (e.g. "UTF-8").
 * @param src        Source byte buffer.
 * @param srcLen     In: number of bytes to read from @p src.
 *                   Out: number of bytes actually consumed.
 * @param dst        Destination byte buffer.
 * @param dstLen     In: capacity of @p dst in bytes.
 *                   Out: number of bytes written to @p dst.
 * @param state      Opaque state variable for multi-call conversions.
 *                   Pass NULL or a pointer to a zero-initialised int32 for
 *                   a fresh conversion.
 * @param substitute Replacement byte (in ISO-8859-1) used when a source
 *                   character cannot be represented in the target encoding.
 * @return \c B_OK on success, or a POSIX errno value on a fatal iconv error.
 */
status_t
convert_encoding(const char* from, const char* to, const char* src,
	int32* srcLen, char* dst, int32* dstLen, int32* state,
	char substitute)
{
	if (*srcLen == 0) {
		// nothing to do!
		*dstLen = 0;
		return B_OK;
	}

	// TODO: this doesn't work, as the state is reset every time!
	iconv_t conversion = iconv_open(to, from);
	if (conversion == (iconv_t)-1) {
		DEBPRINT(("iconv_open failed\n"));
		return B_ERROR;
	}

	size_t outputLeft = *dstLen;

	if (state == NULL || *state == 0) {
		if (state != NULL)
			*state = 1;

		iconv(conversion, NULL, NULL, &dst, &outputLeft);
	}

	char** inputBuffer = const_cast<char**>(&src);
	size_t inputLeft = *srcLen;
	do {
		size_t nonReversibleConversions = iconv(conversion, inputBuffer,
			&inputLeft, &dst, &outputLeft);
		if (nonReversibleConversions == (size_t)-1) {
			if (errno == E2BIG) {
				// Not enough room in the output buffer for the next converted character
				// This is not a "real" error, we just quit out.
				break;
			}

			switch (errno) {
				case EILSEQ: // unable to generate a corresponding character
				{
					discard_invalid_input_character(&conversion, inputBuffer,
						&inputLeft);

					// prepare to convert the substitute character to target encoding
					char original = substitute;
					size_t len = 1;
					char* copy = &original;

					// Perform the conversion
					// We ignore any errors during this as part of robustness/best-effort
					// We use ISO-8859-1 as a source because it is a single byte encoding
					// It also overlaps UTF-8 for the lower 128 characters.  It is also
					// likely to have a mapping to almost any target encoding.
					iconv_t iso8859_1to = iconv_open(to,"ISO-8859-1");
					if (iso8859_1to != (iconv_t)-1) {
						iconv(iso8859_1to, 0, 0, 0, 0);
						iconv(iso8859_1to, &copy, &len, &dst, &outputLeft);
						iconv_close(iso8859_1to);
					}
					break;
				}

				case EINVAL: // incomplete multibyte sequence at the end of the input
					// TODO inputLeft bytes from inputBuffer should
					// be stored in state variable, so that conversion
					// can continue when the caller provides the missing
					// bytes with the next call of this method

					// we just eat bad bytes, as part of robustness/best-effort
					inputBuffer++;
					inputLeft--;
					break;

				default:
					// unknown error, completely bail
					status_t status = errno;
					iconv_close(conversion);
					return status;
			}
		}
	} while (inputLeft > 0 && outputLeft > 0);

	*srcLen -= inputLeft;
	*dstLen -= outputLeft;
	iconv_close(conversion);

	return B_OK;
}


/**
 * @brief Converts a buffer from a legacy encoding into UTF-8.
 *
 * Looks up the IANA name for @p srcEncoding via BCharacterSetRoster and
 * delegates to convert_encoding().
 *
 * @param srcEncoding Conversion ID of the source encoding as returned by
 *                    BCharacterSet::GetConversionID().
 * @param src         Source byte buffer in the legacy encoding.
 * @param srcLen      In: bytes to read; out: bytes consumed.
 * @param dst         Destination buffer to receive UTF-8 output.
 * @param dstLen      In: capacity of @p dst; out: bytes written.
 * @param state       Opaque conversion state; pass NULL for a fresh
 *                    conversion.
 * @param substitute  Replacement byte used for unconvertible characters.
 * @return \c B_OK on success; \c B_ERROR if @p srcEncoding is unknown.
 * @see convert_from_utf8(), convert_encoding()
 */
status_t
convert_to_utf8(uint32 srcEncoding, const char* src, int32* srcLen,
	char* dst, int32* dstLen, int32* state, char substitute)
{
	const BCharacterSet* charset = BCharacterSetRoster::GetCharacterSetByConversionID(
		srcEncoding);
	if (charset == NULL)
		return B_ERROR;

#if DEBUG_CONV
	fprintf(stderr, "convert_to_utf8(%s) : \"", charset->GetName());
	for (int i = 0 ; i < *srcLen ; i++) {
		fprintf(stderr, "%c", src[i]);
	}
	fprintf(stderr, "\"\n");
#endif

	return convert_encoding(charset->GetName(), "UTF-8", src, srcLen,
		dst, dstLen, state, substitute);
}


/**
 * @brief Converts a UTF-8 buffer into a legacy encoding.
 *
 * Looks up the IANA name for @p dstEncoding via BCharacterSetRoster and
 * delegates to convert_encoding().
 *
 * @param dstEncoding Conversion ID of the destination encoding as returned
 *                    by BCharacterSet::GetConversionID().
 * @param src         Source UTF-8 byte buffer.
 * @param srcLen      In: bytes to read; out: bytes consumed.
 * @param dst         Destination buffer to receive output in the legacy
 *                    encoding.
 * @param dstLen      In: capacity of @p dst; out: bytes written.
 * @param state       Opaque conversion state; pass NULL for a fresh
 *                    conversion.
 * @param substitute  Replacement byte used for unconvertible characters.
 * @return \c B_OK on success; \c B_ERROR if @p dstEncoding is unknown.
 * @see convert_to_utf8(), convert_encoding()
 */
status_t
convert_from_utf8(uint32 dstEncoding, const char* src, int32* srcLen,
	char* dst, int32* dstLen, int32* state, char substitute)
{
	const BCharacterSet* charset = BCharacterSetRoster::GetCharacterSetByConversionID(
		dstEncoding);
	if (charset == NULL)
		return B_ERROR;

#if DEBUG_CONV
	fprintf(stderr, "convert_from_utf8(%s) : \"", charset->GetName());
	for (int i = 0 ; i < *srcLen ; i++) {
		fprintf(stderr, "%c", src[i]);
	}
	fprintf(stderr, "\"\n");
#endif

	return convert_encoding("UTF-8", charset->GetName(), src, srcLen,
		dst, dstLen, state, substitute);
}
