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
 *   Copyright 2003-2013 Haiku, Inc. All rights reserved
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 *       Jérôme Duval, jerome.duval@free.fr
 *       Michael Phipps
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file ScreenSaverRunner.cpp
 * @brief Manages loading, running, and unloading a screen saver add-on.
 *
 * ScreenSaverRunner handles the lifecycle of a BScreenSaver add-on: it loads
 * the shared library from one of the standard Screen Savers directories,
 * instantiates the saver via instantiate_screen_saver(), spawns a dedicated
 * low-priority rendering thread, and drives the Draw()/DirectDraw() animation
 * loop according to the saver's tick size and loop cadence.
 *
 * @see BScreenSaver, ScreenSaverSettings
 */


#include "ScreenSaverRunner.h"

#include <stdio.h>

#include <DirectWindow.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Window.h>


/**
 * @brief Construct a ScreenSaverRunner and immediately load the add-on.
 *
 * Stores references to the window and view, determines whether direct drawing
 * is available, and calls _LoadAddOn() to locate and instantiate the screen
 * saver module specified in @a settings.
 *
 * @param window    The BWindow (or BDirectWindow) the saver will draw into.
 * @param view      The BView the saver will use for BView-based drawing.
 * @param settings  The ScreenSaverSettings object providing the module name
 *                  and any previously saved module state.
 */
ScreenSaverRunner::ScreenSaverRunner(BWindow* window, BView* view,
	ScreenSaverSettings& settings)
	:
	fWindow(window),
	fView(view),
	fIsDirectDraw(dynamic_cast<BDirectWindow*>(window) != NULL),
	fSettings(settings),
	fSaver(NULL),
	fAddonImage(-1),
	fThread(-1),
	fQuitting(false)
{
	_LoadAddOn();
}


/**
 * @brief Destructor — stops the rendering thread and unloads the add-on.
 *
 * Calls Quit() if the thread has not already been stopped, then delegates to
 * _CleanUp() to delete the BScreenSaver instance and unload the add-on image.
 */
ScreenSaverRunner::~ScreenSaverRunner()
{
	if (!fQuitting)
		Quit();

	_CleanUp();
}


/**
 * @brief Spawn the rendering thread and begin the animation loop.
 *
 * Creates a low-priority thread running _ThreadFunc() and resumes it. The
 * thread will call the saver's Draw() and/or DirectDraw() methods at the
 * rate determined by BScreenSaver::TickSize().
 *
 * @return B_OK on success, or the negative thread spawn error on failure.
 */
status_t
ScreenSaverRunner::Run()
{
	fThread = spawn_thread(&_ThreadFunc, "ScreenSaverRenderer", B_LOW_PRIORITY,
		this);
	Resume();

	return fThread >= B_OK ? B_OK : fThread;
}


/**
 * @brief Signal the rendering thread to stop and wait for it to exit.
 *
 * Sets fQuitting, wakes the thread via Resume(), and blocks until the thread
 * terminates. Safe to call from any thread.
 */
void
ScreenSaverRunner::Quit()
{
	fQuitting = true;
	Resume();

	if (fThread >= 0) {
		status_t returnValue;
		wait_for_thread(fThread, &returnValue);
	}
}


/**
 * @brief Suspend the rendering thread.
 *
 * @return B_OK on success, or a kernel error if the thread cannot be suspended.
 */
status_t
ScreenSaverRunner::Suspend()
{
	return suspend_thread(fThread);
}


/**
 * @brief Resume the rendering thread after a Suspend() call.
 *
 * @return B_OK on success, or a kernel error if the thread cannot be resumed.
 */
status_t
ScreenSaverRunner::Resume()
{
	return resume_thread(fThread);
}


/**
 * @brief Load and instantiate the screen saver add-on from disk.
 *
 * Searches the standard add-on directories (user non-packaged, user, system
 * non-packaged, system) for a "Screen Savers/<moduleName>" add-on, loads it,
 * resolves the instantiate_screen_saver() entry point, and creates the saver
 * instance. Logs error messages to stderr on failure.
 *
 * @note This is called once from the constructor and is not designed for
 *       reuse after construction.
 */
void
ScreenSaverRunner::_LoadAddOn()
{
	// This is a new set of preferences. Free up what we did have
	// TODO: this is currently not meant to be used after creation
	if (fThread >= B_OK) {
		Suspend();
		if (fSaver != NULL)
			fSaver->StopSaver();
	}
	_CleanUp();

	const char* moduleName = fSettings.ModuleName();
	if (moduleName == NULL || *moduleName == '\0') {
		Resume();
		return;
	}

	BScreenSaver* (*instantiate)(BMessage*, image_id);

	// try each directory until one succeeds

	directory_which which[] = {
		B_USER_NONPACKAGED_ADDONS_DIRECTORY,
		B_USER_ADDONS_DIRECTORY,
		B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY,
		B_SYSTEM_ADDONS_DIRECTORY,
	};
	BPath path;

	for (uint32 i = 0; i < sizeof(which) / sizeof(which[0]); i++) {
		if (find_directory(which[i], &path, false) != B_OK)
			continue;
		else if (path.Append("Screen Savers") != B_OK)
			continue;
		else if (path.Append(fSettings.ModuleName()) != B_OK)
			continue;

		fAddonImage = load_add_on(path.Path());
		if (fAddonImage > 0)
			break;
	}

	if (fAddonImage > 0) {
		// look for the one C function that should exist,
		// instantiate_screen_saver()
		if (get_image_symbol(fAddonImage, "instantiate_screen_saver",
				B_SYMBOL_TYPE_TEXT, (void **)&instantiate) != B_OK) {
			fprintf(stderr, "Unable to find the instantiation function.\n");
		} else {
			BMessage state;
			fSettings.GetModuleState(moduleName, &state);
			fSaver = instantiate(&state, fAddonImage);
		}

		if (fSaver == NULL) {
			fprintf(stderr, "Screen saver initialization failed.\n");
			_CleanUp();
		} else if (fSaver->InitCheck() != B_OK) {
			fprintf(stderr, "Screen saver initialization failed: %s.\n",
				strerror(fSaver->InitCheck()));
			_CleanUp();
		}
	} else
		fprintf(stderr, "Unable to open add-on %s.\n", path.Path());

	Resume();
}


/**
 * @brief Delete the BScreenSaver instance and unload the add-on image.
 *
 * Resets fSaver to NULL and fAddonImage to -1. Called from the destructor and
 * on error paths inside _LoadAddOn().
 */
void
ScreenSaverRunner::_CleanUp()
{
	delete fSaver;
	fSaver = NULL;

	if (fAddonImage >= 0) {
		status_t result = unload_add_on(fAddonImage);
		if (result != B_OK) {
			fprintf(stderr, "Unable to unload screen saver add-on: %s.\n",
				strerror(result));
		}
		fAddonImage = -1;
	}
}


/**
 * @brief Core rendering loop executed on the dedicated saver thread.
 *
 * Sleeps in kInitialTickRate increments, waking more frequently than the
 * saver's tick interval to remain responsive to the quit signal. When a full
 * tick has elapsed, calls DirectDraw() and/or Draw() on the saver, increments
 * the frame counter, and handles the on/off loop cadence. Calls StopSaver()
 * just before returning.
 *
 * @return B_OK when fQuitting is set and the loop exits cleanly.
 */
status_t
ScreenSaverRunner::_Run()
{
	static const uint32 kInitialTickRate = 50000;

	// TODO: This code is getting awfully complicated and should
	// probably be refactored.
	uint32 tickBase = kInitialTickRate;
	int32 snoozeCount = 0;
	int32 frame = 0;
	bigtime_t lastTickTime = 0;
	bigtime_t tick = fSaver != NULL ? fSaver->TickSize() : tickBase;

	while (!fQuitting) {
		// break the idle time up into ticks so that we can evaluate
		// the quit condition with greater responsiveness
		// otherwise a screen saver that sets, say, a 30 second tick
		// will result in the screen saver not responding to deactivation
		// for that length of time
		snooze(tickBase);
		if (system_time() - lastTickTime < tick)
			continue;
		else {
			// re-evaluate the tick time after each successful wakeup
			// screensavers can adjust it on the fly, and we must be
			// prepared to accomodate that
			tick = fSaver != NULL ? fSaver->TickSize() : tickBase;

			if (tick < tickBase) {
				if (tick < 0)
					tick = 0;
				tickBase = tick;
			} else if (tickBase < kInitialTickRate
				&& tick >= kInitialTickRate) {
				tickBase = kInitialTickRate;
			}

			lastTickTime = system_time();
		}

		if (snoozeCount) {
			// if we are sleeping, do nothing
			snoozeCount--;
		} else if (fSaver != NULL) {
			if (fSaver->LoopOnCount() && frame >= fSaver->LoopOnCount()) {
				// Time to nap
				frame = 0;
				snoozeCount = fSaver->LoopOffCount();
			} else if (fWindow->LockWithTimeout(5000LL) == B_OK) {
				if (!fQuitting) {
					// NOTE: BeOS R5 really calls DirectDraw()
					// and then Draw() for the same frame
					if (fIsDirectDraw)
						fSaver->DirectDraw(frame);
					fSaver->Draw(fView, frame);
					fView->Sync();
					frame++;
				}
				fWindow->Unlock();
			}
		} else
			snoozeCount = 1000;
	}

	if (fSaver != NULL)
		fSaver->StopSaver();

	return B_OK;
}


/**
 * @brief Thread entry-point that calls _Run() on the ScreenSaverRunner instance.
 *
 * @param data  Pointer to the ScreenSaverRunner instance.
 * @return The status_t returned by _Run().
 */
status_t
ScreenSaverRunner::_ThreadFunc(void* data)
{
	ScreenSaverRunner* runner = (ScreenSaverRunner*)data;
	return runner->_Run();
}
