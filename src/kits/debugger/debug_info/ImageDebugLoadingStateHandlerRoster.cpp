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
 *   Copyright 2014, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ImageDebugLoadingStateHandlerRoster.cpp
 * @brief Implementation of the global registry for
 *        ImageDebugLoadingStateHandler instances.
 *
 * Handlers are matched against a SpecificImageDebugInfoLoadingState via
 * SupportsState() so the orchestrator can ask the right one to interact
 * with the user (e.g. install a missing debug-info package).
 *
 * @see ImageDebugLoadingStateHandler, DwarfLoadingStateHandler
 */


#include "ImageDebugLoadingStateHandlerRoster.h"

#include <new>

#include <AutoDeleter.h>
#include <AutoLocker.h>

#include "DwarfLoadingStateHandler.h"
#include "ImageDebugInfoLoadingState.h"
#include "ImageDebugLoadingStateHandler.h"
#include "SpecificImageDebugInfoLoadingState.h"


/** @brief Process-wide singleton; created via CreateDefault(). */
/*static*/ ImageDebugLoadingStateHandlerRoster*
	ImageDebugLoadingStateHandlerRoster::sDefaultInstance = NULL;


/**
 * @brief Constructs an empty roster with a labeled lock.
 */
ImageDebugLoadingStateHandlerRoster::ImageDebugLoadingStateHandlerRoster()
	:
	fLock("loading state handler roster"),
	fStateHandlers(20)
{
}


/**
 * @brief Destroys the roster and releases references on all handlers.
 */
ImageDebugLoadingStateHandlerRoster::~ImageDebugLoadingStateHandlerRoster()
{
	for (int32 i = 0; ImageDebugLoadingStateHandler* handler
			= fStateHandlers.ItemAt(i); i++) {
		handler->ReleaseReference();
	}
}


/**
 * @brief Returns the process-wide default roster.
 *
 * @return The default instance, or @c NULL before CreateDefault().
 */
/*static*/ ImageDebugLoadingStateHandlerRoster*
ImageDebugLoadingStateHandlerRoster::Default()
{
	return sDefaultInstance;
}


/**
 * @brief Creates and initializes the default roster, registering built-in
 *        handlers.
 *
 * @retval B_OK         The default roster is now available.
 * @retval B_NO_MEMORY  Allocation failure.
 * @retval other        Errors from Init() or RegisterDefaultHandlers().
 */
/*static*/ status_t
ImageDebugLoadingStateHandlerRoster::CreateDefault()
{
	if (sDefaultInstance != NULL)
		return B_OK;

	ImageDebugLoadingStateHandlerRoster* roster
		= new(std::nothrow) ImageDebugLoadingStateHandlerRoster;
	if (roster == NULL)
		return B_NO_MEMORY;
	ObjectDeleter<ImageDebugLoadingStateHandlerRoster> rosterDeleter(roster);

	status_t error = roster->Init();
	if (error != B_OK)
		return error;

	error = roster->RegisterDefaultHandlers();
	if (error != B_OK)
		return error;

	sDefaultInstance = rosterDeleter.Detach();
	return B_OK;
}


/**
 * @brief Deletes and clears the default roster.
 */
/*static*/ void
ImageDebugLoadingStateHandlerRoster::DeleteDefault()
{
	ImageDebugLoadingStateHandlerRoster* roster = sDefaultInstance;
	sDefaultInstance = NULL;
	delete roster;
}


/**
 * @brief Verifies that the internal lock initialized correctly.
 *
 * @return Status from BLocker::InitCheck().
 */
status_t
ImageDebugLoadingStateHandlerRoster::Init()
{
	return fLock.InitCheck();
}


/**
 * @brief Registers the default set of handlers (currently DWARF only).
 *
 * @retval B_OK         All defaults registered successfully.
 * @retval B_NO_MEMORY  Allocation failure.
 */
status_t
ImageDebugLoadingStateHandlerRoster::RegisterDefaultHandlers()
{
	ImageDebugLoadingStateHandler* handler;
	BReference<ImageDebugLoadingStateHandler> handlerReference;

	handler = new(std::nothrow) DwarfLoadingStateHandler();
	if (handler == NULL)
		return B_NO_MEMORY;
	handlerReference.SetTo(handler, true);

	if (!RegisterHandler(handler))
		return B_NO_MEMORY;

	return B_OK;
}


/**
 * @brief Finds a registered handler that recognizes a given backend state.
 *
 * @param state     Backend-specific loading state to dispatch.
 * @param _handler  Out parameter; on success holds a new reference to the
 *                  matching handler.
 * @retval B_OK              A matching handler was found and referenced.
 * @retval B_ENTRY_NOT_FOUND No handler advertises support for @a state.
 */
status_t
ImageDebugLoadingStateHandlerRoster::FindStateHandler(
	SpecificImageDebugInfoLoadingState* state,
	ImageDebugLoadingStateHandler*& _handler)
{
	AutoLocker<BLocker> locker(fLock);

	bool found = false;
	ImageDebugLoadingStateHandler* handler = NULL;
	for (int32 i = 0; (handler = fStateHandlers.ItemAt(i)); i++) {
		if ((found = handler->SupportsState(state)))
			break;
	}

	if (!found)
		return B_ENTRY_NOT_FOUND;

	handler->AcquireReference();
	_handler = handler;
	return B_OK;
}


/**
 * @brief Adds a handler to the roster and acquires a reference.
 *
 * @param handler  Handler to register.
 * @return @c true on success, @c false on allocation failure.
 */
bool
ImageDebugLoadingStateHandlerRoster::RegisterHandler(
	ImageDebugLoadingStateHandler* handler)
{
	if (!fStateHandlers.AddItem(handler))
		return false;

	handler->AcquireReference();
	return true;
}


/**
 * @brief Removes a previously registered handler and releases its reference.
 *
 * @param handler  Handler to unregister; no-op if not currently registered.
 */
void
ImageDebugLoadingStateHandlerRoster::UnregisterHandler(
	ImageDebugLoadingStateHandler* handler)
{
	if (fStateHandlers.RemoveItem(handler))
		handler->ReleaseReference();
}
