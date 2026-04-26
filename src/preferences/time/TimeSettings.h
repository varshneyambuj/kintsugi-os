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
 * MIT License. Copyright 2002-2010, Haiku.
 * Original authors: Andrew McCall, Mike Berg, Julun.
 */

/** @file TimeSettings.h
    @brief Persists the Time preference window's screen position. */

#ifndef _TIME_SETTINGS_H
#define _TIME_SETTINGS_H


#include <Point.h>
#include <String.h>


/**
 * @brief Tiny on-disk store for the preference window's left-top corner.
 *
 * Backed by a binary file in B_USER_SETTINGS_DIRECTORY. Errors are silent
 * because the persisted value is purely cosmetic.
 */
class TimeSettings {
public :
								TimeSettings();
								~TimeSettings();

			BPoint				LeftTop() const;
			void				SetLeftTop(const BPoint leftTop);

private:
			BString				fSettingsFile;
};


#endif	// _TIME_SETTINGS_H

