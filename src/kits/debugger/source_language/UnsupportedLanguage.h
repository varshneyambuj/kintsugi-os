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

/** @file UnsupportedLanguage.h
    @brief Fallback SourceLanguage used for unknown source languages. */

#ifndef UNSUPPORTED_LANGUAGE_H
#define UNSUPPORTED_LANGUAGE_H


#include "SourceLanguage.h"


/**
 * @brief Trivial SourceLanguage that simply reports itself as
 *        "unsupported"; used when the debug info language is unrecognised.
 */
class UnsupportedLanguage : public SourceLanguage {
public:
	virtual	const char*			Name() const;
};


#endif	// UNSUPPORTED_LANGUAGE_H
