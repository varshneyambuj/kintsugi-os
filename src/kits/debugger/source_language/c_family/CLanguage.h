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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2009, Ingo Weinhold.
 */

/** @file CLanguage.h
    @brief SourceLanguage subclass that tags source as plain C. */

#ifndef C_LANGUAGE_H
#define C_LANGUAGE_H


#include "CLanguageFamily.h"


/**
 * @brief Concrete CLanguageFamily that reports its name as "C".
 *
 * All semantics (syntax highlighting, expression evaluation) come from
 * CLanguageFamily; this subclass only contributes the language name.
 */
class CLanguage : public CLanguageFamily {
public:
								CLanguage();
	virtual						~CLanguage();

	virtual	const char*			Name() const;
};


#endif	// C_LANGUAGE_H
