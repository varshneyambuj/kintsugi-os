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
 * MIT License. Copyright 2005, Haiku.
 * Original authors: Axel Dörfler.
 */

/** @file VirtualMemory.h
    @brief BApplication subclass that owns the VirtualMemory preference UI. */

#ifndef VIRTUAL_MEMORY_H
#define VIRTUAL_MEMORY_H


#include <Application.h>


/**
 * @brief BApplication subclass that drives the VirtualMemory preflet.
 *
 * Owns no model state; on @c ReadyToRun it constructs a SettingsWindow which
 * reads and writes the swap settings file. Also handles the About request.
 */
class VirtualMemory : public BApplication {
public:
					VirtualMemory();
	virtual			~VirtualMemory();

	virtual	void	ReadyToRun();
	virtual	void	AboutRequested();
};

#endif	/* VIRTUAL_MEMORY_H */
