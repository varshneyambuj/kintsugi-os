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
 * MIT License. Copyright 2001-2005, Haiku.
 * Original authors: DarkWyrm, Clemens Zeidler, Joseph Groover.
 */

/** @file DecorManager.h
    @brief Loader and registry for window decorator add-ons used by the desktop. */

#ifndef DECOR_MANAGER_H
#define DECOR_MANAGER_H


#include <image.h>
#include <String.h>
#include <Locker.h>
#include <ObjectList.h>
#include <Entry.h>
#include <DecorInfo.h>

#include "Decorator.h"

class Desktop;
class DesktopListener;
class DrawingEngine;
class Window;
class WindowBehaviour;


typedef BObjectList<DesktopListener> DesktopListenerList;


// special name to test for use of non-fs-tied default decorator
// this just keeps things clean and simple is all

/** @brief Wraps a loaded decorator add-on image and produces Decorator and
           WindowBehaviour instances on demand. */
class DecorAddOn {
public:
								DecorAddOn(image_id id, const char* name);
	virtual						~DecorAddOn();

	virtual status_t			InitCheck() const;

			/** @brief Returns the loaded add-on image identifier, or -1 for
			           the built-in default decorator. */
			image_id			ImageID() const { return fImageID; }

			Decorator*			AllocateDecorator(Desktop* desktop,
									DrawingEngine* engine, BRect rect,
									const char* title, window_look look,
									uint32 flags);

	virtual	WindowBehaviour*	AllocateWindowBehaviour(Window* window);

	virtual const DesktopListenerList& GetDesktopListeners();

protected:
	virtual	Decorator*			_AllocateDecorator(DesktopSettings& settings,
									BRect rect, Desktop* desktop);

			DesktopListenerList	fDesktopListeners;

private:
			image_id			fImageID;
			BString 			fName;
};


/** @brief Process-wide manager that owns the active decorator add-on, supports
           live preview of alternative decorators on a single window, and
           persists the user's decorator selection across sessions. */
class DecorManager {
public:
								DecorManager();
								~DecorManager();

			Decorator*			AllocateDecorator(Window *window);
			WindowBehaviour*	AllocateWindowBehaviour(Window *window);
			void				CleanupForWindow(Window *window);

			status_t			PreviewDecorator(BString path, Window *window);

			const DesktopListenerList& GetDesktopListeners();

			BString 			GetCurrentDecorator() const;
			status_t			SetDecorator(BString path, Desktop *desktop);

private:
			DecorAddOn*			_LoadDecor(BString path, status_t &error);
			bool				_LoadSettingsFromDisk();
			bool				_SaveSettingsToDisk();

private:
			DecorAddOn			fDefaultDecor;
			DecorAddOn*			fCurrentDecor;
			DecorAddOn*			fPreviewDecor;

			Window*				fPreviewWindow;
			BString				fCurrentDecorPath;
};

extern DecorManager gDecorManager;

#endif	/* DECOR_MANAGER_H */
