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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2010, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Function.cpp
 * @brief Implementation of Function, the source-level entity that aggregates
 *        one or more FunctionInstance objects across loaded images.
 *
 * A Function owns a shared FileSourceCode (when loaded), tracks its loading
 * state, and notifies registered listeners when source code becomes
 * available or changes. It also subscribes to LocatableFile path changes so
 * cached source can be invalidated if the on-disk location of the source
 * file moves.
 *
 * @see FunctionInstance, FileSourceCode, LocatableFile
 */


#include "Function.h"

#include "FileSourceCode.h"
#include "FunctionID.h"


/**
 * @brief Constructs a Function with no source code and no listeners.
 */
Function::Function()
	:
	fSourceCode(NULL),
	fSourceCodeState(FUNCTION_SOURCE_NOT_LOADED),
	fNotificationsDisabled(0)
{
}


/**
 * @brief Destroys the Function, releases cached source, and detaches the
 *        LocatableFile listener registered for the first instance.
 */
Function::~Function()
{
	SetSourceCode(NULL, FUNCTION_SOURCE_NOT_LOADED);
	if (FirstInstance() != NULL) {
		FirstInstance()->SourceFile()->RemoveListener(this);
		FirstInstance()->SourceFile()->ReleaseReference();
	}
}


/**
 * @brief Sets or clears the cached file-level source code.
 *
 * Replaces any previously cached source, propagates a clear to all child
 * FunctionInstance objects, and finally notifies listeners.
 *
 * @param source  New shared source object, or @c NULL to clear.
 * @param state   Loading-state value associated with @a source.
 */
void
Function::SetSourceCode(FileSourceCode* source, function_source_state state)
{
	if (source == fSourceCode && state == fSourceCodeState)
		return;

	if (fSourceCode != NULL)
		fSourceCode->ReleaseReference();

	fSourceCode = source;
	fSourceCodeState = state;

	if (fSourceCode != NULL) {
		fSourceCode->AcquireReference();

		// unset all instances' source codes
		fNotificationsDisabled++;
		for (FunctionInstanceList::Iterator it = fInstances.GetIterator();
				FunctionInstance* instance = it.Next();) {
			instance->SetSourceCode(NULL, FUNCTION_SOURCE_NOT_LOADED);
		}
		fNotificationsDisabled--;
	}

	// notify listeners
	NotifySourceCodeChanged();
}


/**
 * @brief Registers a listener for source-code change notifications.
 *
 * @param listener  Listener to add. Ownership remains with the caller.
 */
void
Function::AddListener(Listener* listener)
{
	fListeners.Add(listener);
}


/**
 * @brief Unregisters a previously added listener.
 *
 * @param listener  Listener to remove.
 */
void
Function::RemoveListener(Listener* listener)
{
	fListeners.Remove(listener);
}


/**
 * @brief Adds a FunctionInstance to this logical Function.
 *
 * On the first added instance, also attaches a LocatableFile listener so
 * later path changes can invalidate cached source.
 *
 * @param instance  Instance to register; ownership stays with caller.
 */
void
Function::AddInstance(FunctionInstance* instance)
{
	bool firstInstance = fInstances.IsEmpty();
	fInstances.Add(instance);
	if (firstInstance && SourceFile() != NULL) {
		instance->SourceFile()->AcquireReference();
		instance->SourceFile()->AddListener(this);
	}
}


/**
 * @brief Removes an instance and detaches the file listener if it was the
 *        last one.
 *
 * @param instance  Instance to remove.
 */
void
Function::RemoveInstance(FunctionInstance* instance)
{
	fInstances.Remove(instance);
	if (fInstances.IsEmpty() && instance->SourceFile() != NULL) {
		instance->SourceFile()->RemoveListener(this);
		instance->SourceFile()->ReleaseReference();
	}
}


/**
 * @brief Broadcasts a source-code-changed notification to all listeners.
 *
 * Suppressed when @c fNotificationsDisabled is non-zero (used to coalesce
 * cascading updates).
 */
void
Function::NotifySourceCodeChanged()
{
	if (fNotificationsDisabled > 0)
		return;

	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->FunctionSourceCodeChanged(this);
	}
}


/**
 * @brief Reacts to a LocatableFile reporting a change in its located path.
 *
 * If the new on-disk path differs from the canonical path, the cached
 * source is dropped on this Function and on every instance so the next
 * access reloads it.
 *
 * @param file  File whose location changed.
 */
void
Function::LocatableFileChanged(LocatableFile* file)
{
	BString locatedPath;
	BString path;
	file->GetPath(path);
	if (file->GetLocatedPath(locatedPath) && locatedPath != path) {
		SetSourceCode(NULL, FUNCTION_SOURCE_NOT_LOADED);
		for (FunctionInstanceList::Iterator it = fInstances.GetIterator();
				FunctionInstance* instance = it.Next();) {
			instance->SetSourceCode(NULL, FUNCTION_SOURCE_NOT_LOADED);
		}
	}
}


// #pragma mark - Listener


/**
 * @brief Default virtual destructor for the Listener interface.
 */
Function::Listener::~Listener()
{
}


/**
 * @brief Default no-op implementation of source-code-change notification.
 *
 * @param function  Function whose source state changed.
 */
void
Function::Listener::FunctionSourceCodeChanged(Function* function)
{
}
