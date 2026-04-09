/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * This file incorporates work from the Haiku project:
 *   Copyright 2002-2006, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 * Authors:
 *   Michael Wilber
 *   Axel Dörfler, axeld@pinc-software.de
 *
 * Author:
 *   Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file FuncTranslator.h
 *  @brief BFuncTranslator wraps the legacy function-pointer add-on API as a BTranslator subclass. */

#ifndef _FUNC_TRANSLATOR_H
#define _FUNC_TRANSLATOR_H


#include <Translator.h>


/** @brief Aggregates the function pointers and metadata exported by a function-based translator add-on. */
struct translator_data {
	const char*	name;
	const char*	info;
	int32		version;
	const translation_format* input_formats;
	const translation_format* output_formats;

	status_t	(*identify_hook)(BPositionIO* source, const translation_format* format,
					BMessage* ioExtension, translator_info* outInfo, uint32 outType);

	status_t	(*translate_hook)(BPositionIO* source, const translator_info* info,
					BMessage* ioExtension, uint32 outType, BPositionIO* destination);

	status_t	(*make_config_hook)(BMessage* ioExtension, BView** _view, BRect* _extent);
	status_t	(*get_config_message_hook)(BMessage* ioExtension);
};

namespace BPrivate {

/** @brief Adapts a function-pointer-based translator add-on to the BTranslator interface. */
class BFuncTranslator : public BTranslator {
	public:
		/** @brief Constructs a BFuncTranslator from the exported function-pointer table.
		 *  @param data Struct holding all function pointers and metadata from the add-on. */
		BFuncTranslator(const translator_data& data);

		/** @brief Returns the translator name from the add-on data.
		 *  @return Null-terminated name string. */
		virtual const char *TranslatorName() const;

		/** @brief Returns the translator info string from the add-on data.
		 *  @return Null-terminated info string. */
		virtual const char *TranslatorInfo() const;

		/** @brief Returns the translator version from the add-on data.
		 *  @return Packed version integer. */
		virtual int32 TranslatorVersion() const;

		/** @brief Returns the input formats from the add-on data.
		 *  @param out_count Set to the number of formats.
		 *  @return Pointer to translation_format array. */
		virtual const translation_format *InputFormats(int32 *out_count) const;

		/** @brief Returns the output formats from the add-on data.
		 *  @param out_count Set to the number of formats.
		 *  @return Pointer to translation_format array. */
		virtual const translation_format *OutputFormats(int32 *out_count) const;

		/** @brief Delegates identification to the add-on's identify_hook.
		 *  @param inSource Input stream.
		 *  @param inFormat Format hint, or NULL.
		 *  @param ioExtension Optional extension message.
		 *  @param outInfo Filled on success.
		 *  @param outType Desired output type.
		 *  @return B_OK if recognized, or an error code. */
		virtual status_t Identify(BPositionIO *inSource,
			const translation_format *inFormat, BMessage *ioExtension,
			translator_info *outInfo, uint32 outType);

		/** @brief Delegates translation to the add-on's translate_hook.
		 *  @param inSource Input stream.
		 *  @param inInfo Identify result.
		 *  @param ioExtension Optional extension message.
		 *  @param outType Desired output type.
		 *  @param outDestination Output stream.
		 *  @return B_OK on success, or an error code. */
		virtual status_t Translate(BPositionIO *inSource,
			const translator_info *inInfo, BMessage *ioExtension, uint32 outType,
			BPositionIO * outDestination);

		/** @brief Delegates configuration view creation to the add-on's make_config_hook.
		 *  @param ioExtension Optional extension message.
		 *  @param outView Set to the new configuration view.
		 *  @param outExtent Set to the preferred frame.
		 *  @return B_OK on success, or an error code. */
		virtual status_t MakeConfigurationView(BMessage *ioExtension,
			BView **outView, BRect *outExtent);

		/** @brief Delegates configuration message retrieval to the add-on's get_config_message_hook.
		 *  @param ioExtension BMessage to populate with settings.
		 *  @return B_OK on success, or an error code. */
		virtual status_t GetConfigurationMessage(BMessage *ioExtension);

	protected:
		virtual ~BFuncTranslator();
			// This object is deleted by calling Release(),
			// it can not be deleted directly. See BTranslator in the Be Book

	private:
		translator_data fData;
};

}	// namespace BPrivate

#endif // _FUNC_TRANSLATOR_H
