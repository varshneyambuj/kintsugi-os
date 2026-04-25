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
 *   Copyright (c) 2001-2020 Haiku, Inc. All right reserved.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       DarkWyrm <bpmagic@columbus.rr.com>
 *       Clemens Zeidler <haiku@clemens-zeidler.de>
 *       Joseph Groover <looncraz@satx.rr.com>
 *       John Scipione <jscipione@gmail.com>
 */


/**
 * @file DecorManager.cpp
 * @brief Loader and registry for window decorator add-ons.
 *
 * DecorManager owns the currently active DecorAddOn, which produces both the
 * Decorator (window-frame renderer) and the WindowBehaviour (input policy)
 * for every window the desktop creates. It also supports a transient preview
 * decorator on a single window and persists the user's selection to the
 * settings file under the user settings directory.
 */

#include "DecorManager.h"

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>
#include <Rect.h>

#include <syslog.h>

#include "AppServer.h"
#include "Desktop.h"
#include "DesktopSettings.h"
#include "ServerConfig.h"
#include "SATDecorator.h"
#include "Window.h"

/** @brief Signature for the optional decorator add-on version export. */
typedef float get_version(void);
/** @brief Signature for the decorator add-on factory entry point. */
typedef DecorAddOn* create_decor_addon(image_id id, const char* name);

// Globals
/** @brief Process-wide singleton accessed by Window and Desktop to obtain
           decorators and window behaviours. */
DecorManager gDecorManager;


/**
 * @brief Constructs a DecorAddOn wrapper for an already-loaded image.
 *
 * @param id   Loaded add-on image, or -1 for the built-in default decorator.
 * @param name Display name of the decorator (filename of the add-on).
 */
DecorAddOn::DecorAddOn(image_id id, const char* name)
	:
	fImageID(id),
 	fName(name)
{
}


/**
 * @brief Destroys the DecorAddOn wrapper.
 *
 * @note Unloading the underlying add-on image is the responsibility of
 *       DecorManager so that windows owning Decorator instances from this
 *       add-on can be torn down first.
 */
DecorAddOn::~DecorAddOn()
{
}


/**
 * @brief Validates that this add-on is usable.
 *
 * @return B_OK in the base implementation. Subclasses may report failures
 *         such as missing required symbols.
 */
status_t
DecorAddOn::InitCheck() const
{
	return B_OK;
}


/**
 * @brief Allocates a new Decorator for the given window parameters.
 *
 * Locks the desktop's single-window lock long enough to read DesktopSettings,
 * then constructs the Decorator, applies colors, adds the initial tab and
 * binds the drawing engine.
 *
 * @param desktop Desktop the window belongs to (used for locking and settings).
 * @param engine  DrawingEngine the new decorator will paint into.
 * @param rect    Initial frame of the client area.
 * @param title   Initial window title shown in the tab.
 * @param look    Window look (titled, modal, document, etc.).
 * @param flags   Initial window flags.
 * @return Newly allocated Decorator owned by the caller, or NULL on failure.
 */
Decorator*
DecorAddOn::AllocateDecorator(Desktop* desktop, DrawingEngine* engine,
	BRect rect, const char* title, window_look look, uint32 flags)
{
	if (!desktop->LockSingleWindow())
		return NULL;

	DesktopSettings settings(desktop);
	Decorator* decorator;
	decorator = _AllocateDecorator(settings, rect, desktop);
	desktop->UnlockSingleWindow();
	if (!decorator)
		return NULL;

	decorator->UpdateColors(settings);

	if (decorator->AddTab(settings, title, look, flags) == NULL) {
		delete decorator;
		return NULL;
	}

	decorator->SetDrawingEngine(engine);
	return decorator;
}


/**
 * @brief Allocates the WindowBehaviour that drives mouse/keyboard input for
 *        @a window.
 *
 * The default implementation returns a stack-and-tile aware behaviour bound
 * to the desktop's StackAndTile manager.
 *
 * @param window The window the behaviour will be installed on.
 * @return New WindowBehaviour owned by the caller, or NULL on allocation
 *         failure.
 */
WindowBehaviour*
DecorAddOn::AllocateWindowBehaviour(Window* window)
{
	return new (std::nothrow)SATWindowBehaviour(window,
		window->Desktop()->GetStackAndTile());
}


/**
 * @brief Returns the desktop listeners contributed by this add-on.
 *
 * @return Reference to the (possibly empty) listener list. Lifetime matches
 *         this DecorAddOn.
 */
const DesktopListenerList&
DecorAddOn::GetDesktopListeners()
{
	return fDesktopListeners;
}


/**
 * @brief Constructs the concrete Decorator instance for this add-on.
 *
 * The base class returns a SATDecorator (stack-and-tile decorator); custom
 * add-ons override this to provide their own painting.
 *
 * @param settings Currently effective desktop settings.
 * @param rect     Initial frame of the decorated window's client area.
 * @param desktop  Desktop the window will live on.
 * @return Newly allocated Decorator, or NULL on allocation failure.
 */
Decorator*
DecorAddOn::_AllocateDecorator(DesktopSettings& settings, BRect rect,
	Desktop* desktop)
{
	return new (std::nothrow)SATDecorator(settings, rect, desktop);
}


//	#pragma mark -


/**
 * @brief Constructs the singleton DecorManager and loads the persisted
 *        decorator selection from disk.
 *
 * If no settings file is present or the selection cannot be loaded, the
 * built-in default decorator remains active.
 */
DecorManager::DecorManager()
	:
	fDefaultDecor(-1, "Default"),
	fCurrentDecor(&fDefaultDecor),
	fPreviewDecor(NULL),
	fPreviewWindow(NULL),
	fCurrentDecorPath("Default")
{
	_LoadSettingsFromDisk();
}


/**
 * @brief Destroys the DecorManager.
 */
DecorManager::~DecorManager()
{
}


/**
 * @brief Allocates the decorator for @a window using either the preview or
 *        the currently active add-on.
 *
 * Ownership of the returned Decorator is transferred to the caller (typically
 * the Window itself).
 *
 * @param window Window that will own the decorator.
 * @return New Decorator, or NULL on failure.
 */
Decorator*
DecorManager::AllocateDecorator(Window* window)
{
	// Create a new instance of the current decorator.
	// Ownership is that of the caller

	if (!fCurrentDecor) {
		// We should *never* be here. If we do, it's a bug.
		debugger("DecorManager::AllocateDecorator has a NULL decorator");
		return NULL;
	}

	// Are we previewing a specific decorator?
	if (window == fPreviewWindow) {
		if (fPreviewDecor != NULL) {
			return fPreviewDecor->AllocateDecorator(window->Desktop(),
				window->GetDrawingEngine(), window->Frame(), window->Title(),
				window->Look(), window->Flags());
		} else {
			fPreviewWindow = NULL;
		}
	}

	return fCurrentDecor->AllocateDecorator(window->Desktop(),
		window->GetDrawingEngine(), window->Frame(), window->Title(),
		window->Look(), window->Flags());
}


/**
 * @brief Allocates the WindowBehaviour for @a window from the active add-on.
 *
 * @param window Window the behaviour is bound to.
 * @return New WindowBehaviour, or NULL on failure.
 */
WindowBehaviour*
DecorManager::AllocateWindowBehaviour(Window* window)
{
	if (!fCurrentDecor) {
		// We should *never* be here. If we do, it's a bug.
		debugger("DecorManager::AllocateDecorator has a NULL decorator");
		return NULL;
	}

	return fCurrentDecor->AllocateWindowBehaviour(window);
}


/**
 * @brief Releases preview state associated with a window that is being
 *        destroyed.
 *
 * Called by Desktop just before the window is deleted. If the dying window
 * is the preview target, the preview add-on image is unloaded.
 *
 * @param window Window being deleted.
 */
void
DecorManager::CleanupForWindow(Window* window)
{
	// Given window is being deleted, do any cleanup needed
	if (fPreviewWindow == window && window != NULL){
		fPreviewWindow = NULL;

		if (fPreviewDecor != NULL)
			unload_add_on(fPreviewDecor->ImageID());

		fPreviewDecor = NULL;
	}
}


/**
 * @brief Installs a decorator from @a path onto a single window for live
 *        preview.
 *
 * Only one window can preview at a time; previewing on a new window resets
 * the prior preview window back to the active decorator.
 *
 * @param path   Filesystem path to the decorator add-on.
 * @param window Window on which to apply the preview.
 * @retval B_OK            Preview successfully installed.
 * @retval B_BAD_VALUE     @a window was NULL.
 * @retval B_ENTRY_NOT_FOUND The path does not exist.
 * @retval B_BAD_IMAGE_ID  The add-on could not be loaded.
 * @retval B_MISSING_SYMBOL Required factory symbol was missing.
 * @retval B_ERROR         Some other failure occurred.
 */
status_t
DecorManager::PreviewDecorator(BString path, Window* window)
{
	if (fPreviewWindow != NULL && fPreviewWindow != window){
		// Reset other window to current decorator - only one can preview
		Window* oldPreviewWindow = fPreviewWindow;
		fPreviewWindow = NULL;
		oldPreviewWindow->ReloadDecor();
	}

	if (window == NULL)
		return B_BAD_VALUE;

	// We have to jump some hoops because the window must be able to
	// delete its decorator before we unload the add-on
	status_t error = B_OK;
	DecorAddOn* decorPtr = _LoadDecor(path, error);
	if (decorPtr == NULL)
		return error == B_OK ? B_ERROR : error;

	BRegion border;
	window->GetBorderRegion(&border);

	DecorAddOn* oldDecor = fPreviewDecor;
	fPreviewDecor = decorPtr;
	fPreviewWindow = window;
	// After this call, the window has deleted its decorator.
	fPreviewWindow->ReloadDecor();

	BRegion newBorder;
	window->GetBorderRegion(&newBorder);

	border.Include(&newBorder);
	window->Desktop()->RebuildAndRedrawAfterWindowChange(window, border);

	if (oldDecor != NULL)
		unload_add_on(oldDecor->ImageID());

	return B_OK;
}


/**
 * @brief Returns the desktop listeners contributed by the active decorator.
 *
 * @return Reference to the active add-on's listener list.
 */
const DesktopListenerList&
DecorManager::GetDesktopListeners()
{
	return fCurrentDecor->GetDesktopListeners();
}


/**
 * @brief Returns the path of the currently active decorator add-on.
 *
 * @return Path string, or "Default" for the built-in decorator.
 */
BString
DecorManager::GetCurrentDecorator() const
{
	return fCurrentDecorPath.String();
}


/**
 * @brief Switches all windows on @a desktop to a new decorator add-on.
 *
 * Loads the new add-on, asks the desktop to rebuild every window with the
 * new decorator, and on success unloads the old image and persists the
 * choice. On partial failure the previous decorator is reinstated.
 *
 * @param path    Filesystem path to the decorator add-on, or "Default".
 * @param desktop Desktop whose windows are to be updated.
 * @retval B_OK     Decorator switched successfully and saved.
 * @retval B_ERROR  Switch failed; previous decorator is still in effect.
 *
 * @todo If the new decorator is only partially adopted, unload the new image
 *       and recover any windows that already migrated.
 */
status_t
DecorManager::SetDecorator(BString path, Desktop* desktop)
{
	status_t error = B_OK;
	DecorAddOn* newDecor = _LoadDecor(path, error);
	if (newDecor == NULL)
		return error == B_OK ? B_ERROR : error;

	DecorAddOn* oldDecor = fCurrentDecor;

	BString oldPath = fCurrentDecorPath;
	image_id oldImage = fCurrentDecor->ImageID();

	fCurrentDecor = newDecor;
	fCurrentDecorPath = path.String();

	if (desktop->ReloadDecor(oldDecor)) {
		// now safe to unload all old decorator data
		// saves us from deleting oldDecor...
		unload_add_on(oldImage);
		_SaveSettingsToDisk();
		return B_OK;
	}

	// TODO: unloading the newDecor and its image
	// problem is we don't know how many windows failed... or why they failed...
	syslog(LOG_WARNING,
		"app_server:DecorManager:SetDecorator:\"%s\" *partly* failed\n",
		fCurrentDecorPath.String());

	fCurrentDecor = oldDecor;
	fCurrentDecorPath = oldPath;
	return B_ERROR;
}


/**
 * @brief Resolves a path to a DecorAddOn, loading the image if necessary.
 *
 * The literal string "Default" returns the built-in default decorator without
 * loading any image. Otherwise the file is loaded as an add-on and the
 * factory symbol "instantiate_decor_addon" is invoked to construct the
 * wrapper.
 *
 * @param _path Filesystem path or the literal string "Default".
 * @param error Out-parameter receiving a B_* error code on failure.
 * @return Pointer to the resulting DecorAddOn, or NULL on failure.
 *
 * @note On failure the loaded image (if any) is unloaded before returning.
 */
DecorAddOn*
DecorManager::_LoadDecor(BString _path, status_t& error )
{
	if (_path == "Default") {
		error = B_OK;
		return &fDefaultDecor;
	}

	BEntry entry(_path.String(), true);
	if (!entry.Exists()) {
		error = B_ENTRY_NOT_FOUND;
		return NULL;
	}

	BPath path(&entry);
	image_id image = load_add_on(path.Path());
	if (image < 0) {
		error = B_BAD_IMAGE_ID;
		return NULL;
	}

	create_decor_addon*	createFunc;
	if (get_image_symbol(image, "instantiate_decor_addon", B_SYMBOL_TYPE_TEXT,
			(void**)&createFunc) != B_OK) {
		unload_add_on(image);
		error = B_MISSING_SYMBOL;
		return NULL;
	}

	char name[B_FILE_NAME_LENGTH];
	entry.GetName(name);
	DecorAddOn* newDecor = createFunc(image, name);
	if (newDecor == NULL || newDecor->InitCheck() != B_OK) {
		unload_add_on(image);
		error = B_ERROR;
		return NULL;
	}

	return newDecor;
}


/** @brief Subdirectory under the user settings directory for app_server data. */
static const char* kSettingsDir = "system/app_server";
/** @brief Filename inside kSettingsDir holding the persisted decorator choice. */
static const char* kSettingsFile = "decorator_settings";


/**
 * @brief Reads the persisted decorator selection from disk and applies it.
 *
 * @return true if a decorator path was loaded and successfully applied,
 *         false otherwise (the built-in default remains active).
 */
bool
DecorManager::_LoadSettingsFromDisk()
{
	// get the user settings directory
	BPath path;
	status_t error = find_directory(B_USER_SETTINGS_DIRECTORY, &path, true);
	if (error != B_OK)
		return false;

	path.Append(kSettingsDir);
	path.Append(kSettingsFile);
	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return false;

	BMessage settings;
	if (settings.Unflatten(&file) == B_OK) {
		BString itemPath;
		if (settings.FindString("decorator", &itemPath) == B_OK) {
			status_t error = B_OK;
			DecorAddOn* decor = _LoadDecor(itemPath, error);
			if (decor != NULL) {
				fCurrentDecor = decor;
				fCurrentDecorPath = itemPath;
				return true;
			} else {
				//TODO: do something with the reported error
			}
		}
	}

	return false;
}


/**
 * @brief Persists the currently active decorator path under the user
 *        settings directory.
 *
 * @return true on success, false if the directory could not be created or the
 *         settings could not be flattened to disk.
 */
bool
DecorManager::_SaveSettingsToDisk()
{
	// get the user settings directory
	BPath path;
	status_t error = find_directory(B_USER_SETTINGS_DIRECTORY, &path, true);
	if (error != B_OK)
		return false;

	path.Append(kSettingsDir);
	if (create_directory(path.Path(), 777) != B_OK)
		return false;

	path.Append(kSettingsFile);
	BFile file(path.Path(), B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return false;

	BMessage settings;
	if (settings.AddString("decorator", fCurrentDecorPath.String()) != B_OK)
		return false;
	if (settings.Flatten(&file) != B_OK)
		return false;

	return true;
}

