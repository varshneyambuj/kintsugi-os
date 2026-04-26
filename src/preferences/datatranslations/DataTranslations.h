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
 * MIT License. Copyright 2002-2010, Haiku Inc.
 * Original authors: Oliver Siebenmarck, Andrew McCall, Michael Wilber.
 */

/** @file DataTranslations.h
    @brief BApplication subclass for the DataTranslations preferences app. */

#ifndef DATA_TRANSLATIONS_H
#define DATA_TRANSLATIONS_H


#include <Application.h>
#include <Directory.h>
#include <Entry.h>


/**
 * @brief Top-level application object for the DataTranslations preferences
 *        panel.
 *
 * Owns the single DataTranslationsWindow and reacts to file drops by
 * installing the dropped entries as translator add-ons.
 */
class DataTranslationsApplication : public BApplication {
public:
								DataTranslationsApplication();
	virtual						~DataTranslationsApplication();

	virtual void				RefsReceived(BMessage* message);

private:
			void				_InstallError(const char* name, status_t status);
			status_t			_Install(BDirectory& target, BEntry& entry);
			void				_NoTranslatorError(const char* name);
};


#endif	// DATA_TRANSLATIONS_H
