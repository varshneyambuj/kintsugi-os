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
 *   Copyright 2002-2006, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Wilber
 *       Axel Dörfler, axeld@pinc-software.de
 */


/**
 * @file FuncTranslator.cpp
 * @brief BTranslator wrapper for C-style (function-pointer) translator add-ons
 *
 * Implements BPrivate::BFuncTranslator, an internal BTranslator subclass that
 * adapts the older C-style translator API (translator_data function pointers)
 * so it can be used uniformly alongside make_nth_translator() based add-ons
 * through BTranslatorRoster.
 *
 * @see TranslatorRoster.cpp, Translator.cpp
 */


#include "FuncTranslator.h"

#include <string.h>


namespace BPrivate {


/**
 * @brief Constructs a BFuncTranslator wrapping the given C-style translator data.
 * @param data A translator_data struct whose function pointers and metadata
 *     describe the translator's capabilities.
 */
BFuncTranslator::BFuncTranslator(const translator_data& data)
{
	fData = data;
}


/** @brief Destroys the BFuncTranslator. */
BFuncTranslator::~BFuncTranslator()
{
}


/**
 * @brief Returns the translator's name string.
 * @return A null-terminated string naming the translator.
 */
const char *
BFuncTranslator::TranslatorName() const
{
	return fData.name;
}


/**
 * @brief Returns the translator's human-readable description string.
 * @return A null-terminated description string.
 */
const char *
BFuncTranslator::TranslatorInfo() const
{
	return fData.info;
}


/**
 * @brief Returns the translator's version number.
 * @return The version as a packed int32.
 */
int32
BFuncTranslator::TranslatorVersion() const
{
	return fData.version;
}


/**
 * @brief Returns the array of input formats this translator accepts.
 *
 * Counts the zero-terminated input_formats array from the translator_data
 * and returns its length through \a _count.
 *
 * @param _count Set to the number of entries in the returned array.
 * @return Pointer to the translation_format array, or NULL on error.
 */
const translation_format *
BFuncTranslator::InputFormats(int32* _count) const
{
	if (_count == NULL || fData.input_formats == NULL)
		return NULL;

	int32 count = 0;
	while (fData.input_formats[count].type) {
		count++;
	}

	*_count = count;
	return fData.input_formats;
}


/**
 * @brief Returns the array of output formats this translator can produce.
 *
 * Counts the zero-terminated output_formats array from the translator_data
 * and returns its length through \a _count.
 *
 * @param _count Set to the number of entries in the returned array.
 * @return Pointer to the translation_format array, or NULL on error.
 */
const translation_format *
BFuncTranslator::OutputFormats(int32* _count) const
{
	if (_count == NULL || fData.output_formats == NULL)
		return NULL;

	int32 count = 0;
	while (fData.output_formats[count].type) {
		count++;
	}

	*_count = count;
	return fData.output_formats;
}


/**
 * @brief Determines whether this translator can handle the data in \a source.
 *
 * Delegates to the identify_hook function pointer from the translator_data.
 *
 * @param source The stream containing the data to identify.
 * @param format A hint about the expected format, or NULL.
 * @param ioExtension Optional extension message for extra parameters.
 * @param info Filled with identification results on success.
 * @param type The desired output type, or 0 for any.
 * @return B_OK if the data is recognized, B_NO_TRANSLATOR otherwise, or
 *     B_ERROR if no identify_hook is provided.
 */
status_t
BFuncTranslator::Identify(BPositionIO* source, const translation_format* format,
	BMessage* ioExtension, translator_info* info, uint32 type)
{
	if (fData.identify_hook == NULL)
		return B_ERROR;

	return fData.identify_hook(source, format, ioExtension, info, type);
}


/**
 * @brief Translates data from \a source to \a destination.
 *
 * Delegates to the translate_hook function pointer from the translator_data.
 *
 * @param source The stream containing the source data.
 * @param info Identification info from a prior Identify() call.
 * @param ioExtension Optional extension message for extra parameters.
 * @param type The desired output format type.
 * @param destination The stream to write translated data into.
 * @return B_OK on success, B_NO_TRANSLATOR if no translate_hook is provided,
 *     or another error code from the hook.
 */
status_t
BFuncTranslator::Translate(BPositionIO* source, const translator_info *info,
	BMessage* ioExtension, uint32 type, BPositionIO* destination)
{
	if (fData.translate_hook == NULL)
		return B_ERROR;

	return fData.translate_hook(source, info, ioExtension, type, destination);
}


/**
 * @brief Creates a configuration view for the translator, if supported.
 *
 * Delegates to the make_config_hook function pointer from the translator_data.
 *
 * @param ioExtension Optional extension message.
 * @param _view Set to the newly created configuration BView on success.
 * @param _extent Set to the preferred bounds for the view.
 * @return B_OK on success, or B_ERROR if no make_config_hook is provided.
 */
status_t
BFuncTranslator::MakeConfigurationView(BMessage* ioExtension,
	BView** _view, BRect* _extent)
{
	if (fData.make_config_hook == NULL)
		return B_ERROR;

	return fData.make_config_hook(ioExtension, _view, _extent);
}


/**
 * @brief Retrieves the current configuration settings for the translator.
 *
 * Delegates to the get_config_message_hook function pointer from the
 * translator_data.
 *
 * @param ioExtension BMessage to be populated with current settings.
 * @return B_OK on success, or B_ERROR if no get_config_message_hook is provided.
 */
status_t
BFuncTranslator::GetConfigurationMessage(BMessage* ioExtension)
{
	if (fData.get_config_message_hook == NULL)
		return B_ERROR;

	return fData.get_config_message_hook(ioExtension);
}


}	// namespace BPrivate
