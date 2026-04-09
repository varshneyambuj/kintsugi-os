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
 *   Copyright 2009, Haiku Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 * Author:
 *   Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file TranslatorAddOn.h
 *  @brief Deprecated C-style add-on interface for function-based translators; use BTranslator instead. */

#ifndef _TRANSLATOR_ADD_ON_H
#define _TRANSLATOR_ADD_ON_H


#include <TranslationDefs.h>


class BMessage;
class BView;
class BRect;
class BPositionIO;


// Deprecated, use BTranslator API instead

/** @brief Human-readable name exported by a function-based translator add-on. */
extern char translatorName[];

/** @brief Short description exported by a function-based translator add-on. */
extern char translatorInfo[];

/** @brief Version number exported by a function-based translator add-on. */
extern int32 translatorVersion;

/** @brief Optional array of input formats supported by the add-on. */
extern translation_format inputFormats[];	// optional

/** @brief Optional array of output formats supported by the add-on. */
extern	translation_format outputFormats[];	// optional


extern "C" {

/** @brief Identifies whether the source stream matches a format this add-on can translate.
 *  @param source Input stream to probe.
 *  @param format Hint about the expected format, or NULL.
 *  @param extension Optional extension BMessage with extra parameters.
 *  @param info Filled with match information on success.
 *  @param outType Desired output type code.
 *  @return B_OK if the source is recognized, or an error code. */
extern status_t	Identify(BPositionIO* source, const translation_format* format,
					BMessage* extension, translator_info* info, uint32 outType);

/** @brief Translates source data into the requested output type.
 *  @param source Input stream.
 *  @param info Identify result describing the source.
 *  @param extension Optional extension BMessage with extra parameters.
 *  @param outType Desired output type code.
 *  @param destination Output stream.
 *  @return B_OK on success, or an error code. */
extern status_t	Translate(BPositionIO* source, const translator_info* info,
					BMessage* extension, uint32 outType,
					BPositionIO* destination);

/** @brief Creates a configuration view for the add-on's settings.
 *  @param extension Optional extension BMessage.
 *  @param _view Set to the newly created configuration view.
 *  @param _frame Set to the preferred size of the view.
 *  @return B_OK on success, or an error code. */
extern status_t	MakeConfig(BMessage* extension, BView** _view, BRect* _frame);

/** @brief Fills a BMessage with the add-on's current configuration.
 *  @param extension BMessage to populate with settings.
 *  @return B_OK on success, or an error code. */
extern status_t	GetConfigMessage(BMessage* extension);

}


#endif	//_TRANSLATOR_ADD_ON_H
