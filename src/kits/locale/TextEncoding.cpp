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
 *   Copyright 2016, Haiku, inc.
 *   Distributed under terms of the MIT license.
 */


/**
 * @file TextEncoding.cpp
 * @brief Implementation of BTextEncoding for character set conversion.
 *
 * BTextEncoding wraps two ICU UConverter instances (one for the target
 * encoding and one always set to UTF-8) to convert between arbitrary byte
 * encodings and UTF-8. It supports both auto-detection from sample data
 * (via ICU's charset detector) and construction by explicit encoding name.
 * The UTF-8 fast-path avoids ICU overhead when the source is already UTF-8.
 *
 * @see BLocale
 */


#include "TextEncoding.h"

#include <unicode/ucnv.h>
#include <unicode/ucsdet.h>

#include <algorithm>


namespace BPrivate {


/**
 * @brief Construct a BTextEncoding for the given encoding name.
 *
 * @param name  IANA charset name string (e.g. "ISO-8859-1", "Shift_JIS").
 */
BTextEncoding::BTextEncoding(BString name)
	:
	fName(name),
	fUtf8Converter(NULL),
	fConverter(NULL)
{
}


/**
 * @brief Construct a BTextEncoding by auto-detecting the encoding of sample data.
 *
 * Uses ICU's UCharsetDetector to identify the most likely charset for the
 * given byte sequence.
 *
 * @param data    Pointer to the sample byte data.
 * @param length  Number of bytes in the sample.
 */
BTextEncoding::BTextEncoding(const char* data, size_t length)
	:
	fUtf8Converter(NULL),
	fConverter(NULL)
{
	UErrorCode error = U_ZERO_ERROR;

	UCharsetDetector* detector = ucsdet_open(&error);
	ucsdet_setText(detector, data, length, &error);
	const UCharsetMatch* encoding = ucsdet_detect(detector, &error);

	fName = ucsdet_getName(encoding, &error);
	ucsdet_close(detector);
}


/**
 * @brief Destroy the BTextEncoding and close any open ICU converters.
 */
BTextEncoding::~BTextEncoding()
{
	if (fUtf8Converter != NULL)
		ucnv_close(fUtf8Converter);

	if (fConverter != NULL)
		ucnv_close(fConverter);
}


/**
 * @brief Check whether this BTextEncoding has a valid encoding name.
 *
 * @return B_OK if the name is non-empty, B_NO_INIT otherwise.
 */
status_t
BTextEncoding::InitCheck()
{
	if (fName.IsEmpty())
		return B_NO_INIT;
	else
		return B_OK;
}


/**
 * @brief Decode bytes from the source encoding into UTF-8.
 *
 * Lazily opens the ICU converters on first call. Updates \a inputLength to
 * the number of source bytes consumed and \a outputLength to the number of
 * UTF-8 bytes written.
 *
 * @param input         Pointer to the source encoded bytes.
 * @param inputLength   On entry, bytes available; on return, bytes consumed.
 * @param output        Destination buffer for UTF-8 output.
 * @param outputLength  On entry, buffer size; on return, bytes written.
 * @return B_OK on success, B_ERROR on ICU conversion failure.
 */
status_t
BTextEncoding::Decode(const char* input, size_t& inputLength, char* output,
	size_t& outputLength)
{
	const char* base = input;
	char* target = output;

	// Optimize the easy case.
	// Note: we don't check the input to be valid UTF-8 when doing that.
	if (fName == "UTF-8") {
		outputLength = std::min(inputLength, outputLength);
		inputLength = outputLength;
		memcpy(output, input, inputLength);
		return B_OK;
	}

	UErrorCode error = U_ZERO_ERROR;

	if (fUtf8Converter == NULL)
		fUtf8Converter = ucnv_open("UTF-8", &error);

	if (fConverter == NULL)
		fConverter = ucnv_open(fName.String(), &error);

	ucnv_convertEx(fUtf8Converter, fConverter, &target, output + outputLength,
		&base, input + inputLength, NULL, NULL, NULL, NULL, FALSE, TRUE,
		&error);

	// inputLength is set to the number of bytes consumed. We may not use all of
	// the input data (for example if it is cut in the middle of an utf-8 char).
	inputLength = base - input;
	outputLength = target - output;

	if (!U_SUCCESS(error))
		return B_ERROR;

	return B_OK;
}


/**
 * @brief Encode UTF-8 bytes into the target encoding.
 *
 * Lazily opens the ICU converters on first call. Updates \a inputLength to
 * the number of UTF-8 bytes consumed and \a outputLength to the number of
 * encoded bytes written.
 *
 * @param input         Pointer to the UTF-8 source bytes.
 * @param inputLength   On entry, bytes available; on return, bytes consumed.
 * @param output        Destination buffer for encoded output.
 * @param outputLength  On entry, buffer size; on return, bytes written.
 * @return B_OK on success, B_ERROR on ICU conversion failure.
 */
status_t
BTextEncoding::Encode(const char* input, size_t& inputLength, char* output,
	size_t& outputLength)
{
	const char* base = input;
	char* target = output;

	// Optimize the easy case.
	// Note: we don't check the input to be valid UTF-8 when doing that.
	if (fName == "UTF-8") {
		outputLength = std::min(inputLength, outputLength);
		inputLength = outputLength;
		memcpy(output, input, inputLength);
		return B_OK;
	}

	UErrorCode error = U_ZERO_ERROR;

	if (fUtf8Converter == NULL)
		fUtf8Converter = ucnv_open("UTF-8", &error);

	if (fConverter == NULL)
		fConverter = ucnv_open(fName.String(), &error);

	ucnv_convertEx(fConverter, fUtf8Converter, &target, output + outputLength,
		&base, input + inputLength, NULL, NULL, NULL, NULL, FALSE, TRUE,
		&error);

	// inputLength is set to the number of bytes consumed. We may not use all of
	// the input data (for example if it is cut in the middle of an utf-8 char).
	inputLength = base - input;
	outputLength = target - output;

	if (!U_SUCCESS(error))
		return B_ERROR;

	return B_OK;
}


/**
 * @brief Flush any buffered partial characters from the encoding converter.
 *
 * Must be called after the last Encode() call to ensure any partial multi-byte
 * sequences in the converter's internal buffer are written out.
 *
 * @param output        Destination buffer for the flushed bytes.
 * @param outputLength  On entry, buffer size; on return, bytes written.
 * @return B_OK on success, B_NO_INIT if converters are not open, B_ERROR on
 *         ICU failure.
 */
status_t
BTextEncoding::Flush(char* output, size_t& outputLength)
{
	char* target = output;

	if (fName == "UTF-8")
		return B_OK;

	if (fUtf8Converter == NULL || fConverter == NULL)
		return B_NO_INIT;

	UErrorCode error = U_ZERO_ERROR;

	ucnv_convertEx(fConverter, fUtf8Converter, &target, output + outputLength,
		NULL, NULL, NULL, NULL, NULL, NULL, FALSE, TRUE,
		&error);

	if (!U_SUCCESS(error))
		return B_ERROR;

	return B_OK;
}


/**
 * @brief Return the IANA encoding name for this converter.
 *
 * @return BString containing the encoding name (e.g. "UTF-8", "ISO-8859-1").
 */
BString
BTextEncoding::GetName()
{
	return fName;
}


};
