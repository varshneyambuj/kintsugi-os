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
 * MIT License. Copyright 1999-2010, Jeremy Friesner and Haiku, Inc.
 * Original authors: Jeremy Friesner, Fredrik Modéen.
 */

/** @file ShortcutsApp.h
    @brief BApplication subclass that hosts the Shortcuts preference window. */

#ifndef SHORTCUTS_APP_H
#define SHORTCUTS_APP_H


#include <Application.h>


/**
 * @brief Top-level application object for the Shortcuts preference panel.
 *
 * Owns the BApplication message loop and is responsible for creating and
 * showing the main ShortcutsWindow when the runtime signals readiness.
 */
class ShortcutsApp : public BApplication {
public:
							ShortcutsApp();
							~ShortcutsApp();
	virtual	void			ReadyToRun();
};


#endif	// SHORTCUTS_APP_H
