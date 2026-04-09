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
 *   Copyright 2002-2007, Haiku Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Wilber
 *       Axel Dörfler, axeld@pinc-software.de
 */


/**
 * @file TranslationUtils.cpp
 * @brief Convenience utilities for the Translation Kit
 *
 * Implements BTranslationUtils, a collection of static helper functions for
 * the most common translation tasks: loading bitmaps from files or application
 * resources, reading and writing styled text, building "Save As" menus from
 * the available translator output formats, and retrieving per-translator
 * default settings.
 *
 * @see BitmapStream.cpp, TranslatorRoster.cpp
 */


#include <Application.h>
#include <Bitmap.h>
#include <BitmapStream.h>
#include <CharacterSet.h>
#include <CharacterSetRoster.h>
#include <Entry.h>
#include <File.h>
#include <MenuItem.h>
#include <NodeInfo.h>
#include <ObjectList.h>
#include <Path.h>
#include <Resources.h>
#include <Roster.h>
#include <String.h>
#include <TextView.h>
#include <TranslationUtils.h>
#include <TranslatorFormats.h>
#include <TranslatorRoster.h>
#include <UTF8.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>


using namespace BPrivate;


/** @brief Constructs a BTranslationUtils object (not meant to be instantiated). */
BTranslationUtils::BTranslationUtils()
{
}


/** @brief Destroys the BTranslationUtils object. */
BTranslationUtils::~BTranslationUtils()
{
}


/** @brief Copy constructor (no-op; BTranslationUtils holds no state). */
BTranslationUtils::BTranslationUtils(const BTranslationUtils &kUtils)
{
}


/** @brief Assignment operator (no-op; BTranslationUtils holds no state). */
BTranslationUtils &
BTranslationUtils::operator=(const BTranslationUtils &kUtils)
{
	return *this;
}


/**
 * @brief Loads a bitmap by name, trying a file path first then an app resource.
 *
 * First attempts GetBitmapFile(\a kName), then falls back to loading an
 * application resource of type B_TRANSLATOR_BITMAP named \a kName.
 * The caller owns the returned object.
 *
 * @param kName File path or resource name identifying the bitmap.
 * @param roster The translator roster to use, or NULL for the default.
 * @return A new BBitmap, or NULL on failure.
 */
BBitmap *
BTranslationUtils::GetBitmap(const char *kName, BTranslatorRoster *roster)
{
	BBitmap *pBitmap = GetBitmapFile(kName, roster);
		// Try loading a bitmap from the file named name

	// Try loading the bitmap as an application resource
	if (pBitmap == NULL)
		pBitmap = GetBitmap(B_TRANSLATOR_BITMAP, kName, roster);

	return pBitmap;
}


/**
 * @brief Loads a bitmap from an application resource identified by type and id.
 *
 * Retrieves the resource via BApplication::AppResources(), wraps the raw data
 * in a BMemoryIO, and translates it to B_TRANSLATOR_BITMAP.
 * The caller owns the returned object.
 *
 * @param type The resource type constant.
 * @param id The resource id.
 * @param roster The translator roster to use, or NULL for the default.
 * @return A new BBitmap, or NULL on failure.
 */
BBitmap *
BTranslationUtils::GetBitmap(uint32 type, int32 id, BTranslatorRoster *roster)
{
	BResources *pResources = BApplication::AppResources();
		// Remember: pResources must not be freed because
		// it belongs to the application
	if (pResources == NULL || pResources->HasResource(type, id) == false)
		return NULL;

	// Load the bitmap resource from the application file
	// pRawData should be NULL if the resource is an
	// unknown type or not available
	size_t bitmapSize = 0;
	const void *kpRawData = pResources->LoadResource(type, id, &bitmapSize);
	if (kpRawData == NULL || bitmapSize == 0)
		return NULL;

	BMemoryIO memio(kpRawData, bitmapSize);
		// Put the pointer to the raw image data into a BMemoryIO object
		// so that it can be used with BTranslatorRoster->Translate() in
		// the GetBitmap(BPositionIO *, BTranslatorRoster *) function

	return GetBitmap(&memio, roster);
		// Translate the data in memio using the BTranslatorRoster roster
}


/**
 * @brief Loads a bitmap from an application resource identified by type and name.
 *
 * Note that a type/name pair does not uniquely identify a resource — only
 * the first match is used. The caller owns the returned object.
 *
 * @param type The resource type constant.
 * @param kName The resource name string.
 * @param roster The translator roster to use, or NULL for the default.
 * @return A new BBitmap, or NULL on failure.
 */
BBitmap *
BTranslationUtils::GetBitmap(uint32 type, const char *kName,
	BTranslatorRoster *roster)
{
	BResources *pResources = BApplication::AppResources();
		// Remember: pResources must not be freed because
		// it belongs to the application
	if (pResources == NULL || pResources->HasResource(type, kName) == false)
		return NULL;

	// Load the bitmap resource from the application file
	size_t bitmapSize = 0;
	const void *kpRawData = pResources->LoadResource(type, kName, &bitmapSize);
	if (kpRawData == NULL || bitmapSize == 0)
		return NULL;

	BMemoryIO memio(kpRawData, bitmapSize);
		// Put the pointer to the raw image data into a BMemoryIO object so
		// that it can be used with BTranslatorRoster->Translate()

	return GetBitmap(&memio, roster);
		// Translate the data in memio using the BTranslatorRoster roster
}


/**
 * @brief Loads a bitmap from a file on disk.
 *
 * Relative paths are resolved against the directory containing the
 * application's executable. The caller owns the returned object.
 *
 * @param kName Path to the bitmap file (absolute or relative to the app).
 * @param roster The translator roster to use, or NULL for the default.
 * @return A new BBitmap, or NULL if the file cannot be opened or translated.
 */
BBitmap *
BTranslationUtils::GetBitmapFile(const char *kName, BTranslatorRoster *roster)
{
	if (!be_app || !kName || kName[0] == '\0')
		return NULL;

	BPath path;
	if (kName[0] != '/') {
		// If kName is a relative path, use the path of the application's
		// executable as the base for the relative path
		app_info info;
		if (be_app->GetAppInfo(&info) != B_OK)
			return NULL;
		BEntry appRef(&info.ref);
		if (path.SetTo(&appRef) != B_OK)
			return NULL;
		if (path.GetParent(&path) != B_OK)
			return NULL;
		if (path.Append(kName) != B_OK)
			return NULL;

	} else if (path.SetTo(kName) != B_OK)
		return NULL;

	BFile bitmapFile(path.Path(), B_READ_ONLY);
	if (bitmapFile.InitCheck() != B_OK)
		return NULL;

	return GetBitmap(&bitmapFile, roster);
		// Translate the data in memio using the BTranslatorRoster roster
}


/**
 * @brief Loads a bitmap from the file identified by \a kRef.
 *
 * The caller owns the returned object.
 *
 * @param kRef Entry reference for the bitmap file.
 * @param roster The translator roster to use, or NULL for the default.
 * @return A new BBitmap, or NULL on failure.
 */
BBitmap *
BTranslationUtils::GetBitmap(const entry_ref *kRef, BTranslatorRoster *roster)
{
	BFile bitmapFile(kRef, B_READ_ONLY);
	if (bitmapFile.InitCheck() != B_OK)
		return NULL;

	return GetBitmap(&bitmapFile, roster);
		// Translate the data in bitmapFile using the BTranslatorRoster roster
}


/**
 * @brief Translates bitmap data from a BPositionIO stream into a BBitmap.
 *
 * This is the core GetBitmap() implementation used by all other overloads.
 * It asks the roster to translate \a stream to B_TRANSLATOR_BITMAP format,
 * then detaches and returns the resulting BBitmap. The caller owns the result.
 *
 * @param stream The stream containing the image data to translate.
 * @param roster The translator roster to use, or NULL for the default.
 * @return A new BBitmap, or NULL if the stream cannot be translated.
 */
BBitmap *
BTranslationUtils::GetBitmap(BPositionIO *stream, BTranslatorRoster *roster)
{
	if (stream == NULL)
		return NULL;

	// Use default Translator if none is specified
	if (roster == NULL) {
		roster = BTranslatorRoster::Default();
		if (roster == NULL)
			return NULL;
	}

	// Translate the file from whatever format it is in the file
	// to the type format so that it can be stored in a BBitmap
	BBitmapStream bitmapStream;
	if (roster->Translate(stream, NULL, NULL, &bitmapStream,
		B_TRANSLATOR_BITMAP) < B_OK)
		return NULL;

	// Detach the BBitmap from the BBitmapStream so the user
	// of this function can do what they please with it.
	BBitmap *pBitmap = NULL;
	if (bitmapStream.DetachBitmap(&pBitmap) == B_NO_ERROR)
		return pBitmap;
	else
		return NULL;
}


/**
 * @brief Reads styled text from \a source and appends it to \a intoView.
 *
 * Translates \a source to B_STYLED_TEXT_FORMAT using the specified roster,
 * then parses the STXT/TEXT/STYL record structure and inserts the text and
 * run array at the end of \a intoView's existing content.
 *
 * @param source The stream containing the styled text data.
 * @param intoView The BTextView to append the translated text into.
 * @param encoding Optional encoding name (e.g. "UTF-8"); NULL means UTF-8.
 * @param roster The translator roster to use, or NULL for the default.
 * @return B_OK on success, B_BAD_VALUE if parameters are invalid, B_BAD_TYPE
 *     if the stream header is not recognized, or another error code.
 */
status_t
BTranslationUtils::GetStyledText(BPositionIO* source, BTextView* intoView,
	const char* encoding, BTranslatorRoster* roster)
{
	if (source == NULL || intoView == NULL)
		return B_BAD_VALUE;

	// Use default Translator if none is specified
	if (roster == NULL) {
		roster = BTranslatorRoster::Default();
		if (roster == NULL)
			return B_ERROR;
	}

	BMessage config;
	if (encoding != NULL && encoding[0])
		config.AddString("be:encoding", encoding);

	// Translate the file from whatever format it is to B_STYLED_TEXT_FORMAT
	// we understand
	BMallocIO mallocIO;
	if (roster->Translate(source, NULL, &config, &mallocIO,
			B_STYLED_TEXT_FORMAT) < B_OK)
		return B_BAD_TYPE;

	const uint8* buffer = (const uint8*)mallocIO.Buffer();

	// make sure there is enough data to fill the stream header
	const size_t kStreamHeaderSize = sizeof(TranslatorStyledTextStreamHeader);
	if (mallocIO.BufferLength() < kStreamHeaderSize)
		return B_BAD_DATA;

	// copy the stream header from the mallio buffer
	TranslatorStyledTextStreamHeader header =
		*(reinterpret_cast<const TranslatorStyledTextStreamHeader *>(buffer));

	// convert the stm_header.header struct to the host format
	const size_t kRecordHeaderSize = sizeof(TranslatorStyledTextRecordHeader);
	swap_data(B_UINT32_TYPE, &header.header, kRecordHeaderSize, B_SWAP_BENDIAN_TO_HOST);
	swap_data(B_INT32_TYPE, &header.version, sizeof(int32), B_SWAP_BENDIAN_TO_HOST);

	if (header.header.magic != 'STXT')
		return B_BAD_TYPE;

	// copy the text header from the mallocIO buffer

	uint32 offset = header.header.header_size + header.header.data_size;
	const size_t kTextHeaderSize = sizeof(TranslatorStyledTextTextHeader);
	if (mallocIO.BufferLength() < offset + kTextHeaderSize)
		return B_BAD_DATA;

	TranslatorStyledTextTextHeader textHeader =
		*(const TranslatorStyledTextTextHeader *)(buffer + offset);

	// convert the stm_header.header struct to the host format
	swap_data(B_UINT32_TYPE, &textHeader.header, kRecordHeaderSize, B_SWAP_BENDIAN_TO_HOST);
	swap_data(B_INT32_TYPE, &textHeader.charset, sizeof(int32), B_SWAP_BENDIAN_TO_HOST);

	if (textHeader.header.magic != 'TEXT' || textHeader.charset != B_UNICODE_UTF8)
		return B_BAD_TYPE;

	offset += textHeader.header.header_size;
	if (mallocIO.BufferLength() < offset + textHeader.header.data_size) {
		// text buffer misses its end; handle this gracefully
		textHeader.header.data_size = mallocIO.BufferLength() - offset;
	}

	const char* text = (const char*)buffer + offset;
		// point text pointer at the actual character data
	bool hasStyles = false;

	if (mallocIO.BufferLength() > offset + textHeader.header.data_size) {
		// If the stream contains information beyond the text data
		// (which means that this data is probably styled text data)

		offset += textHeader.header.data_size;
		const size_t kStyleHeaderSize =
			sizeof(TranslatorStyledTextStyleHeader);
		if (mallocIO.BufferLength() >= offset + kStyleHeaderSize) {
			TranslatorStyledTextStyleHeader styleHeader =
				*(reinterpret_cast<const TranslatorStyledTextStyleHeader *>(buffer + offset));
			swap_data(B_UINT32_TYPE, &styleHeader.header, kRecordHeaderSize, B_SWAP_BENDIAN_TO_HOST);
			swap_data(B_UINT32_TYPE, &styleHeader.apply_offset, sizeof(uint32), B_SWAP_BENDIAN_TO_HOST);
			swap_data(B_UINT32_TYPE, &styleHeader.apply_length, sizeof(uint32), B_SWAP_BENDIAN_TO_HOST);
			if (styleHeader.header.magic == 'STYL') {
				offset += styleHeader.header.header_size;
				if (mallocIO.BufferLength() >= offset + styleHeader.header.data_size)
					hasStyles = true;
			}
		}
	}

	text_run_array *runArray = NULL;
	if (hasStyles)
		runArray = BTextView::UnflattenRunArray(buffer + offset);

	if (runArray != NULL) {
		intoView->Insert(intoView->TextLength(),
			text, textHeader.header.data_size, runArray);
#ifdef HAIKU_TARGET_PLATFORM_HAIKU
		BTextView::FreeRunArray(runArray);
#else
		free(runArray);
#endif
	} else {
		intoView->Insert(intoView->TextLength(), text,
			textHeader.header.data_size);
	}

	return B_OK;
}


/**
 * @brief Reads styled text from \a source and appends it to \a intoView (UTF-8).
 *
 * Convenience overload that calls GetStyledText() with NULL encoding (UTF-8).
 *
 * @param source The stream containing the styled text data.
 * @param intoView The BTextView to append the translated text into.
 * @param roster The translator roster to use, or NULL for the default.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BTranslationUtils::GetStyledText(BPositionIO* source, BTextView* intoView,
	BTranslatorRoster* roster)
{
	return GetStyledText(source, intoView, NULL, roster);
}


/**
 * @brief Writes styled text from \a fromView into \a intoStream.
 *
 * Combines the plain text and style run array from \a fromView into a single
 * B_STYLED_TEXT_FORMAT stream (STXT + TEXT + STYL record sequence). This
 * produces a non-human-readable format; use WriteStyledEditFile() if you
 * want StyledEdit-compatible output (plain text in the file, styles as an
 * attribute).
 *
 * @param fromView The BTextView containing the styled text.
 * @param intoStream The stream to write the encoded styled text data into.
 * @param roster Not used; reserved for future use.
 * @return B_OK on success, B_BAD_VALUE if parameters are NULL, or B_ERROR
 *     if any write step fails.
 */
status_t
BTranslationUtils::PutStyledText(BTextView *fromView, BPositionIO *intoStream,
	BTranslatorRoster *roster)
{
	if (fromView == NULL || intoStream == NULL)
		return B_BAD_VALUE;

	int32 textLength = fromView->TextLength();
	if (textLength < 0)
		return B_ERROR;

	const char *pTextData = fromView->Text();
		// its OK if the result of fromView->Text() is NULL

	int32 runArrayLength = 0;
	text_run_array *runArray = fromView->RunArray(0, textLength,
		&runArrayLength);
	if (runArray == NULL)
		return B_ERROR;

	int32 flatRunArrayLength = 0;
	void *pflatRunArray =
		BTextView::FlattenRunArray(runArray, &flatRunArrayLength);
	if (pflatRunArray == NULL) {
#ifdef HAIKU_TARGET_PLATFORM_HAIKU
		BTextView::FreeRunArray(runArray);
#else
		free(runArray);
#endif
		return B_ERROR;
	}

	// Rather than use a goto, I put a whole bunch of code that
	// could error out inside of a loop, and break out of the loop
	// if there is an error.

	// This block of code is where I do all of the writing of the
	// data to the stream. I've gathered all of the data that I
	// need at this point.
	bool ok = false;
	while (!ok) {
		const size_t kStreamHeaderSize =
			sizeof(TranslatorStyledTextStreamHeader);
		TranslatorStyledTextStreamHeader stm_header;
		stm_header.header.magic = 'STXT';
		stm_header.header.header_size = kStreamHeaderSize;
		stm_header.header.data_size = 0;
		stm_header.version = 100;

		// convert the stm_header.header struct to the host format
		const size_t kRecordHeaderSize =
			sizeof(TranslatorStyledTextRecordHeader);
		if (swap_data(B_UINT32_TYPE, &stm_header.header, kRecordHeaderSize,
			B_SWAP_HOST_TO_BENDIAN) != B_OK)
			break;
		if (swap_data(B_INT32_TYPE, &stm_header.version, sizeof(int32),
			B_SWAP_HOST_TO_BENDIAN) != B_OK)
			break;

		const size_t kTextHeaderSize = sizeof(TranslatorStyledTextTextHeader);
		TranslatorStyledTextTextHeader txt_header;
		txt_header.header.magic = 'TEXT';
		txt_header.header.header_size = kTextHeaderSize;
		txt_header.header.data_size = textLength;
		txt_header.charset = B_UNICODE_UTF8;

		// convert the stm_header.header struct to the host format
		if (swap_data(B_UINT32_TYPE, &txt_header.header, kRecordHeaderSize,
			B_SWAP_HOST_TO_BENDIAN) != B_OK)
			break;
		if (swap_data(B_INT32_TYPE, &txt_header.charset, sizeof(int32),
			B_SWAP_HOST_TO_BENDIAN) != B_OK)
			break;

		const size_t kStyleHeaderSize =
			sizeof(TranslatorStyledTextStyleHeader);
		TranslatorStyledTextStyleHeader stl_header;
		stl_header.header.magic = 'STYL';
		stl_header.header.header_size = kStyleHeaderSize;
		stl_header.header.data_size = flatRunArrayLength;
		stl_header.apply_offset = 0;
		stl_header.apply_length = textLength;

		// convert the stl_header.header struct to the host format
		if (swap_data(B_UINT32_TYPE, &stl_header.header, kRecordHeaderSize,
			B_SWAP_HOST_TO_BENDIAN) != B_OK)
			break;
		if (swap_data(B_UINT32_TYPE, &stl_header.apply_offset, sizeof(uint32),
			B_SWAP_HOST_TO_BENDIAN) != B_OK)
			break;
		if (swap_data(B_UINT32_TYPE, &stl_header.apply_length, sizeof(uint32),
			B_SWAP_HOST_TO_BENDIAN) != B_OK)
			break;

		// Here, you can see the structure of the styled text data by
		// observing the order that the various structs and data are
		// written to the stream
		ssize_t amountWritten = 0;
		amountWritten = intoStream->Write(&stm_header, kStreamHeaderSize);
		if ((size_t) amountWritten != kStreamHeaderSize)
			break;
		amountWritten = intoStream->Write(&txt_header, kTextHeaderSize);
		if ((size_t) amountWritten != kTextHeaderSize)
			break;
		amountWritten = intoStream->Write(pTextData, textLength);
		if (amountWritten != textLength)
			break;
		amountWritten = intoStream->Write(&stl_header, kStyleHeaderSize);
		if ((size_t) amountWritten != kStyleHeaderSize)
			break;
		amountWritten = intoStream->Write(pflatRunArray, flatRunArrayLength);
		if (amountWritten != flatRunArrayLength)
			break;

		ok = true;
			// gracefully break out of the loop
	}

	free(pflatRunArray);
#ifdef HAIKU_TARGET_PLATFORM_HAIKU
	BTextView::FreeRunArray(runArray);
#else
	free(runArray);
#endif

	return ok ? B_OK : B_ERROR;
}


/**
 * @brief Writes styled text from \a view to \a file in StyledEdit format.
 *
 * Writes the plain text content to the file body, then stores the style run
 * array in the "styles" extended attribute and word-wrap / alignment settings
 * in their respective attributes. If no MIME type is set, sets it to
 * "text/plain". If an \a encoding is specified, the text is converted from
 * UTF-8 before writing.
 *
 * Unlike PutStyledText(), the output is human-readable as a plain text file.
 *
 * @param view The BTextView containing the styled text.
 * @param file The BFile to write the styled text into.
 * @param encoding Optional target encoding name, or NULL for UTF-8.
 * @return B_OK on success, B_BAD_VALUE if either parameter is NULL, or an
 *     error code from file I/O.
 */
status_t
BTranslationUtils::WriteStyledEditFile(BTextView* view, BFile* file, const char *encoding)
{
	if (view == NULL || file == NULL)
		return B_BAD_VALUE;

	int32 textLength = view->TextLength();
	if (textLength < 0)
		return B_ERROR;

	const char *text = view->Text();
	if (text == NULL && textLength != 0)
		return B_ERROR;

	// move to the start of the file if not already there
	status_t status = file->Seek(0, SEEK_SET);
	if (status != B_OK)
		return status;

	const BCharacterSet* characterSet = NULL;
	if (encoding != NULL && strcmp(encoding, ""))
		characterSet = BCharacterSetRoster::FindCharacterSetByName(encoding);
	if (characterSet == NULL) {
		// default encoding - UTF-8
		// Write plain text data to file
		ssize_t bytesWritten = file->Write(text, textLength);
		if (bytesWritten != textLength) {
			if (bytesWritten < B_OK)
				return bytesWritten;

			return B_ERROR;
		}

		// be:encoding, defaults to UTF-8 (65535)
		// Note that the B_UNICODE_UTF8 constant is 0 and for some reason
		// not appropriate for use here.
		int32 value = 65535;
		file->WriteAttr("be:encoding", B_INT32_TYPE, 0, &value, sizeof(value));
	} else {
		// we need to convert the text
		uint32 id = characterSet->GetConversionID();
		const char* outText = view->Text();
		int32 sourceLength = textLength;
		int32 state = 0;

		textLength = 0;

		do {
			char buffer[32768];
			int32 length = sourceLength;
			int32 bufferSize = sizeof(buffer);
			status = convert_from_utf8(id, outText, &length, buffer, &bufferSize, &state);
			if (status != B_OK)
				return status;

			ssize_t bytesWritten = file->Write(buffer, bufferSize);
			if (bytesWritten < B_OK)
				return bytesWritten;

			sourceLength -= length;
			textLength += bytesWritten;
			outText += length;
		} while (sourceLength > 0);

		BString encodingStr(encoding);
		file->WriteAttrString("be:encoding", &encodingStr);
	}

	// truncate any extra text
	status = file->SetSize(textLength);
	if (status != B_OK)
		return status;

	// Write attributes. We don't report an error anymore after this point,
	// as attributes aren't that crucial - not all volumes support attributes.
	// However, if writing one attribute fails, no further attributes are
	// tried to be written.

	BNodeInfo info(file);
	char type[B_MIME_TYPE_LENGTH];
	if (info.GetType(type) != B_OK) {
		// This file doesn't have a file type yet, so let's set it
		if (info.SetType("text/plain") < B_OK)
			return B_OK;
	}

	// word wrap setting, turned on by default
	int32 wordWrap = view->DoesWordWrap() ? 1 : 0;
	ssize_t bytesWritten = file->WriteAttr("wrap", B_INT32_TYPE, 0,
		&wordWrap, sizeof(int32));
	if (bytesWritten != sizeof(int32))
		return B_OK;

	// alignment, default is B_ALIGN_LEFT
	int32 alignment = view->Alignment();
	bytesWritten = file->WriteAttr("alignment", B_INT32_TYPE, 0,
		&alignment, sizeof(int32));
	if (bytesWritten != sizeof(int32))
		return B_OK;

	// Write text_run_array, ie. the styles of the text

	text_run_array *runArray = view->RunArray(0, view->TextLength());
	if (runArray != NULL) {
		int32 runArraySize = 0;
		void *flattenedRunArray = BTextView::FlattenRunArray(runArray, &runArraySize);
		if (flattenedRunArray != NULL) {
			file->WriteAttr("styles", B_RAW_TYPE, 0, flattenedRunArray,
				runArraySize);
		}

		free(flattenedRunArray);
#ifdef HAIKU_TARGET_PLATFORM_HAIKU
		BTextView::FreeRunArray(runArray);
#else
		free(runArray);
#endif
	}

	return B_OK;
}


/**
 * @brief Writes styled text from \a view to \a file in StyledEdit format (UTF-8).
 *
 * Convenience overload that calls WriteStyledEditFile() with NULL encoding.
 *
 * @param view The BTextView containing the styled text.
 * @param file The BFile to write to.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BTranslationUtils::WriteStyledEditFile(BTextView* view, BFile* file)
{
	return WriteStyledEditFile(view, file, NULL);
}


/**
 * @brief Returns the default configuration settings for a translator.
 *
 * Retrieves the settings stored by the "Translations" control panel for the
 * translator identified by \a forTranslator. The caller owns the returned
 * BMessage and must delete it.
 *
 * @param forTranslator The translator_id whose settings to retrieve.
 * @param roster The translator roster to use, or NULL for the default.
 * @return A new BMessage with the settings, or NULL on failure.
 */
BMessage *
BTranslationUtils::GetDefaultSettings(translator_id forTranslator,
	BTranslatorRoster *roster)
{
	// Use default Translator if none is specified
	if (roster == NULL) {
		roster = BTranslatorRoster::Default();
		if (roster == NULL)
			return NULL;
	}

	BMessage *message = new BMessage();
	if (message == NULL)
		return NULL;

	status_t result = roster->GetConfigurationMessage(forTranslator, message);
	if (result != B_OK && result != B_NO_TRANSLATOR) {
		// Be's version seems to just pass an empty BMessage
		// in case of B_NO_TRANSLATOR, well, in some cases anyway
		delete message;
		return NULL;
	}

	return message;
}


/**
 * @brief Returns the default settings for a translator identified by name and version.
 *
 * Scans all translators in the default roster and returns the settings for
 * the first one matching both \a kTranslatorName and \a translatorVersion.
 * The caller owns the returned BMessage and must delete it.
 *
 * @param kTranslatorName The exact name string of the translator.
 * @param translatorVersion The exact version number of the translator.
 * @return A new BMessage with the settings, or NULL on failure or if not found.
 */
BMessage *
BTranslationUtils::GetDefaultSettings(const char *kTranslatorName,
	int32 translatorVersion)
{
	BTranslatorRoster *roster = BTranslatorRoster::Default();
	translator_id *translators = NULL;
	int32 numTranslators = 0;
	if (roster == NULL
		|| roster->GetAllTranslators(&translators, &numTranslators) != B_OK)
		return NULL;

	// Cycle through all of the default translators
	// looking for a translator that matches the name and version
	// that I was given
	BMessage *pMessage = NULL;
	const char *currentTranName = NULL, *currentTranInfo = NULL;
	int32 currentTranVersion = 0;
	for (int i = 0; i < numTranslators; i++) {

		if (roster->GetTranslatorInfo(translators[i], &currentTranName,
			&currentTranInfo, &currentTranVersion) == B_OK) {

			if (currentTranVersion == translatorVersion
				&& strcmp(currentTranName, kTranslatorName) == 0) {
				pMessage = GetDefaultSettings(translators[i], roster);
				break;
			}
		}
	}

	delete[] translators;
	return pMessage;
}


/**
 * @brief Populates a BMenu with one item per translator output format.
 *
 * For each translator that accepts \a fromType as input, adds a BMenuItem for
 * every output format it supports (excluding \a fromType itself). Menu items
 * carry the translator_id and output format type in their BMessage. Items are
 * sorted alphabetically by format name.
 *
 * @param intoMenu The menu to add items to.
 * @param fromType The source data type (e.g. B_TRANSLATOR_BITMAP).
 * @param kModel BMessage template for item messages, or NULL for
 *     B_TRANSLATION_MENU.
 * @param kTranslatorIdName Key name for the translator_id field in the item
 *     message, or NULL for "be:translator".
 * @param kTranslatorTypeName Key name for the output format field in the item
 *     message, or NULL for "be:type".
 * @param roster The translator roster to use, or NULL for the default.
 * @return B_OK on success, or B_BAD_VALUE if \a intoMenu is NULL.
 */
status_t
BTranslationUtils::AddTranslationItems(BMenu *intoMenu, uint32 fromType,
	const BMessage *kModel, const char *kTranslatorIdName,
	const char *kTranslatorTypeName, BTranslatorRoster *roster)
{
	if (!intoMenu)
		return B_BAD_VALUE;

	if (!roster)
		roster = BTranslatorRoster::Default();

	if (!kTranslatorIdName)
		kTranslatorIdName = "be:translator";

	if (!kTranslatorTypeName)
		kTranslatorTypeName = "be:type";

	translator_id * ids = NULL;
	int32 count = 0;
	status_t err = roster->GetAllTranslators(&ids, &count);
	if (err < B_OK)
		return err;

	BObjectList<translator_info> infoList;

	for (int tix = 0; tix < count; tix++) {
		const translation_format *formats = NULL;
		int32 numFormats = 0;
		bool ok = false;
		err = roster->GetInputFormats(ids[tix], &formats, &numFormats);
		if (err == B_OK) {
			for (int iix = 0; iix < numFormats; iix++) {
				if (formats[iix].type == fromType) {
					ok = true;
					break;
				}
			}
		}
		if (!ok)
			continue;

		// Get supported output formats
		err = roster->GetOutputFormats(ids[tix], &formats, &numFormats);
		if (err == B_OK) {
			for (int oix = 0; oix < numFormats; oix++) {
				if (formats[oix].type != fromType) {
					infoList.AddItem(_BuildTranslatorInfo(ids[tix],
						const_cast<translation_format*>(&formats[oix])));
				}
			}
		}
	}

	// Sort alphabetically by name
	infoList.SortItems(&_CompareTranslatorInfoByName);

	// Now add the menu items
	for (int i = 0; i < infoList.CountItems(); i++) {
		translator_info* info = infoList.ItemAt(i);

		BMessage *itemmsg;
		if (kModel)
			itemmsg = new BMessage(*kModel);
		else
			itemmsg = new BMessage(B_TRANSLATION_MENU);
		itemmsg->AddInt32(kTranslatorIdName, info->translator);
		itemmsg->AddInt32(kTranslatorTypeName, info->type);
		intoMenu->AddItem(new BMenuItem(info->name, itemmsg));

		// Delete object created in _BuildTranslatorInfo
		delete info;
	}

	delete[] ids;
	return B_OK;
}


/**
 * @brief Allocates and fills a translator_info from a translator_id and format.
 *
 * Used internally by AddTranslationItems() to build sortable info objects.
 * The caller is responsible for deleting the returned object.
 *
 * @param id The translator_id to record.
 * @param format The output translation_format to record.
 * @return A new heap-allocated translator_info.
 */
translator_info*
BTranslationUtils::_BuildTranslatorInfo(const translator_id id, const translation_format* format)
{
	// Caller must delete
	translator_info* info = new translator_info;

	info->translator = id;
	info->type = format->type;
	info->group = format->group;
	info->quality = format->quality;
	info->capability = format->capability;
	strlcpy(info->name, format->name, sizeof(info->name));
	strlcpy(info->MIME, format->MIME, sizeof(info->MIME));

	return info;
}


/**
 * @brief Comparison function for sorting translator_info objects by name.
 *
 * Used by AddTranslationItems() to sort the output format list alphabetically
 * (case-insensitive) before adding menu items.
 *
 * @param info1 First translator_info to compare.
 * @param info2 Second translator_info to compare.
 * @return Negative if info1 < info2, zero if equal, positive if info1 > info2.
 */
int
BTranslationUtils::_CompareTranslatorInfoByName(const translator_info* info1, const translator_info* info2)
{
	return strcasecmp(info1->name, info2->name);
}
