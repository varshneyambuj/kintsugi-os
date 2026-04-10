/*
 * Copyright 2026 Kintsugi OS Project. All rights reserved.
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
 * Authors:
 *     Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2019, Adrien Destugues, pulkomandy@pulkomandy.tk.
 *   Distributed under the terms of the MIT License.
 */

/** @file DebugWindow.h
 *  @brief Declaration of the DebugWindow dialog shown when an application crashes. */

#ifndef DEBUGWINDOW_H
#define DEBUGWINDOW_H


#include <Bitmap.h>
#include <Window.h>


enum {
	kActionKillTeam,
	kActionDebugTeam,
	kActionWriteCoreFile,
	kActionSaveReportTeam,
	kActionPromptUser
};


//#define HANDOVER_USE_GDB 1
#define HANDOVER_USE_DEBUGGER 1

#define USE_GUI true
	// define to false if the debug server shouldn't use GUI (i.e. an alert)

/** @brief Modal window presenting crash-recovery options (terminate, debug, core dump, report). */
class DebugWindow : public BWindow {
public:
	/** @brief Construct the debug window for the named crashed application. */
					DebugWindow(const char* appName);
					~DebugWindow();

	/** @brief Handle user action selection and quit messages. */
	void			MessageReceived(BMessage* message);
	/** @brief Show the window modally and return the chosen action code. */
	int32			Go();

private:
	static	BRect	IconSize();

private:
	BBitmap			fBitmap;    /**< Icon bitmap displayed in the alert stripe. */
	BButton*		fOKButton;  /**< OK button whose label changes with the selected action. */
	sem_id			fSemaphore; /**< Semaphore used to block Go() until the user responds. */
	int32			fAction;    /**< Currently selected action code. */
};


#endif /* !DEBUGWINDOW_H */
