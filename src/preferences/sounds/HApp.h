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
 * MIT License. Copyright 2003-2006, Haiku.
 * Original authors: Atsushi Takamatsu, Jérôme Duval, Oliver Ruiz Dorantes.
 */

/** @file HApp.h
    @brief BApplication subclass for the Sounds preferences app. */

#ifndef HAPP_H
#define HAPP_H


#include <Application.h>
#include <Catalog.h>


/**
 * @brief Top-level application object for the Sounds preferences panel.
 *
 * Owns the HWindow that exposes the system event-to-wav mapping and serves
 * the standard About box.
 */
class HApp : public BApplication {
public:
								HApp();
	virtual						~HApp();
	virtual	void				AboutRequested();
};


#endif	// HAPP_H
