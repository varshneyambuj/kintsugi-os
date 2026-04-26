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
 * MIT License. Copyright 2002-2006, Haiku, Inc.
 * Original author: Oliver Siebenmarck.
 */

/** @file DataTranslationsSettings.h
    @brief Singleton storing the persisted window position for the app. */

#ifndef DATA_TRANSLATIONS_SETTINGS_H
#define DATA_TRANSLATIONS_SETTINGS_H


#include <Point.h>


/**
 * @brief Persistent settings holder for the DataTranslations preferences app.
 *
 * Loads the saved window corner on construction and writes it back to the
 * user settings directory on destruction. Access the shared instance via
 * Instance().
 */
class DataTranslationsSettings {
public:
							DataTranslationsSettings();
							~DataTranslationsSettings();

			/** @brief Returns the persisted top-left window corner. */
			BPoint			WindowCorner() const { return fCorner; }
			void			SetWindowCorner(BPoint corner);

	static DataTranslationsSettings*	Instance();

private:
			BPoint			fCorner;
};


#endif	// DATA_TRANSLATIONS_SETTINGS_H
