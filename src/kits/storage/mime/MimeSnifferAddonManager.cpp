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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2006-2013, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file MimeSnifferAddonManager.cpp
 * @brief Singleton manager that aggregates and queries all registered MIME sniffer add-ons.
 *
 * MimeSnifferAddonManager maintains a reference-counted list of BMimeSnifferAddon
 * instances and exposes unified GuessMimeType() operations that delegate to every
 * registered add-on, returning the result with the highest priority.  The class
 * follows a singleton pattern with explicit CreateDefault()/DeleteDefault()
 * lifecycle management so that the MIME subsystem controls the object's lifetime.
 *
 * @see BMimeSnifferAddon
 */


#include <mime/MimeSnifferAddonManager.h>

#include <new>

#include <Autolock.h>
#include <MimeType.h>

#include <MimeSnifferAddon.h>


namespace BPrivate {
namespace Storage {
namespace Mime {


// singleton instance
MimeSnifferAddonManager* MimeSnifferAddonManager::sManager = NULL;


/**
 * @brief Reference-counted wrapper around a BMimeSnifferAddon instance.
 *
 * AddonReference keeps a BMimeSnifferAddon alive while at least one caller
 * holds a reference obtained via GetReference().  When the last reference is
 * released through PutReference() the object deletes itself and the wrapped
 * addon.
 */
struct MimeSnifferAddonManager::AddonReference {
	/**
	 * @brief Constructs an AddonReference wrapping \a addon with an initial reference count of 1.
	 *
	 * @param addon Pointer to the BMimeSnifferAddon to manage; must not be NULL.
	 */
	AddonReference(BMimeSnifferAddon* addon)
		: fAddon(addon),
		  fReferenceCount(1)
	{
	}

	/**
	 * @brief Destroys the AddonReference and deletes the wrapped addon.
	 */
	~AddonReference()
	{
		delete fAddon;
	}

	/**
	 * @brief Returns the wrapped BMimeSnifferAddon pointer.
	 *
	 * @return Pointer to the managed BMimeSnifferAddon.
	 */
	BMimeSnifferAddon* Addon() const
	{
		return fAddon;
	}

	/**
	 * @brief Atomically increments the reference count.
	 */
	void GetReference()
	{
		atomic_add(&fReferenceCount, 1);
	}

	/**
	 * @brief Atomically decrements the reference count, deleting this object when it reaches zero.
	 */
	void PutReference()
	{
		if (atomic_add(&fReferenceCount, -1) == 1)
			delete this;
	}

private:
	BMimeSnifferAddon*	fAddon;
	int32				fReferenceCount;
};


/**
 * @brief Constructs the MimeSnifferAddonManager with an empty add-on list.
 */
MimeSnifferAddonManager::MimeSnifferAddonManager()
	: fLock("mime sniffer manager"),
	  fAddons(20),
	  fMinimalBufferSize(0)
{
}

/**
 * @brief Destroys the MimeSnifferAddonManager.
 */
MimeSnifferAddonManager::~MimeSnifferAddonManager()
{
}

/**
 * @brief Returns a pointer to the default singleton MimeSnifferAddonManager.
 *
 * @return Pointer to the singleton instance, or NULL if CreateDefault() has
 *         not yet been called.
 */
MimeSnifferAddonManager*
MimeSnifferAddonManager::Default()
{
	return sManager;
}

/**
 * @brief Creates and installs the default singleton MimeSnifferAddonManager.
 *
 * Allocates a new MimeSnifferAddonManager and stores it as the singleton.
 * Call DeleteDefault() when the singleton is no longer needed.
 *
 * @return B_OK on success, B_NO_MEMORY if the allocation fails.
 */
status_t
MimeSnifferAddonManager::CreateDefault()
{
	MimeSnifferAddonManager* manager
		= new(std::nothrow) MimeSnifferAddonManager;
	if (!manager)
		return B_NO_MEMORY;

	sManager = manager;

	return B_OK;
}

/**
 * @brief Destroys and clears the default singleton MimeSnifferAddonManager.
 */
void
MimeSnifferAddonManager::DeleteDefault()
{
	MimeSnifferAddonManager* manager = sManager;
	sManager = NULL;

	delete manager;
}

/**
 * @brief Registers a BMimeSnifferAddon with the manager.
 *
 * Takes ownership of \a addon by wrapping it in an AddonReference.  Also
 * updates the cached minimal buffer size if the new add-on requires a larger
 * buffer than any previously registered one.
 *
 * @param addon Pointer to the BMimeSnifferAddon to register; must not be NULL.
 * @return B_OK on success, B_BAD_VALUE if \a addon is NULL, B_NO_MEMORY on
 *         allocation failure, or B_ERROR if the lock cannot be acquired.
 */
status_t
MimeSnifferAddonManager::AddMimeSnifferAddon(BMimeSnifferAddon* addon)
{
	if (!addon)
		return B_BAD_VALUE;

	BAutolock locker(fLock);
	if (!locker.IsLocked())
		return B_ERROR;

	// create a reference for the addon
	AddonReference* reference = new(std::nothrow) AddonReference(addon);
	if (!reference)
		return B_NO_MEMORY;

	// add the reference
	if (!fAddons.AddItem(reference)) {
		delete reference;
		return B_NO_MEMORY;
	}

	// update minimal buffer size
	size_t minBufferSize = addon->MinimalBufferSize();
	if (minBufferSize > fMinimalBufferSize)
		fMinimalBufferSize = minBufferSize;

	return B_OK;
}

/**
 * @brief Returns the largest minimal buffer size required by any registered add-on.
 *
 * @return Minimum buffer size in bytes that must be provided to GuessMimeType().
 */
size_t
MimeSnifferAddonManager::MinimalBufferSize()
{
	return fMinimalBufferSize;
}

/**
 * @brief Guesses a MIME type from a filename by polling all registered add-ons.
 *
 * Iterates over every registered add-on and returns the MIME type associated
 * with the highest priority score.  Returns -1 if no add-on recognises the file.
 *
 * @param fileName The filename to examine.
 * @param type     Pointer to a BMimeType object that receives the best guess.
 * @return The best priority score in [0, 1], or -1 if no type could be guessed.
 */
float
MimeSnifferAddonManager::GuessMimeType(const char* fileName, BMimeType* type)
{
	// get addons
	AddonReference** addons = NULL;
	int32 count = 0;
	status_t error = _GetAddons(addons, count);
	if (error != B_OK)
		return -1;

	// iterate over the addons and find the most fitting type
	float bestPriority = -1;
	for (int32 i = 0; i < count; i++) {
		BMimeType currentType;
		float priority = addons[i]->Addon()->GuessMimeType(fileName,
			&currentType);
		if (priority > bestPriority) {
			type->SetTo(currentType.Type());
			bestPriority = priority;
		}
	}

	// release addons
	_PutAddons(addons, count);

	return bestPriority;
}

/**
 * @brief Guesses a MIME type from file content by polling all registered add-ons.
 *
 * Iterates over every registered add-on and returns the MIME type associated
 * with the highest priority score.  Returns -1 if no add-on recognises the content.
 *
 * @param file   Pointer to the open BFile being examined.
 * @param buffer Pointer to the leading bytes of the file's content.
 * @param length Number of bytes available in \a buffer.
 * @param type   Pointer to a BMimeType object that receives the best guess.
 * @return The best priority score in [0, 1], or -1 if no type could be guessed.
 */
float
MimeSnifferAddonManager::GuessMimeType(BFile* file, const void* buffer,
	int32 length, BMimeType* type)
{
	// get addons
	AddonReference** addons = NULL;
	int32 count = 0;
	status_t error = _GetAddons(addons, count);
	if (error != B_OK)
		return -1;

	// iterate over the addons and find the most fitting type
	float bestPriority = -1;
	for (int32 i = 0; i < count; i++) {
		BMimeType currentType;
		float priority = addons[i]->Addon()->GuessMimeType(file, buffer,
			length, &currentType);
		if (priority > bestPriority) {
			type->SetTo(currentType.Type());
			bestPriority = priority;
		}
	}

	// release addons
	_PutAddons(addons, count);

	return bestPriority;
}

/**
 * @brief Acquires references to all currently registered add-ons under the lock.
 *
 * Allocates an array of AddonReference pointers and bumps the reference count
 * of each registered add-on so they remain valid while the lock is released.
 *
 * @param references Output parameter receiving the allocated array of pointers.
 * @param count      Output parameter receiving the number of entries in the array.
 * @return B_OK on success, B_ERROR if the lock cannot be acquired, or
 *         B_NO_MEMORY if the array allocation fails.
 */
status_t
MimeSnifferAddonManager::_GetAddons(AddonReference**& references, int32& count)
{
	BAutolock locker(fLock);
	if (!locker.IsLocked())
		return B_ERROR;

	count = fAddons.CountItems();
	references = new(std::nothrow) AddonReference*[count];
	if (!references)
		return B_NO_MEMORY;

	for (int32 i = 0; i < count; i++) {
		references[i] = (AddonReference*)fAddons.ItemAt(i);
		references[i]->GetReference();
	}

	return B_OK;
}

/**
 * @brief Releases references previously acquired by _GetAddons() and frees the array.
 *
 * @param references Pointer to the array returned by _GetAddons().
 * @param count      Number of entries in \a references.
 */
void
MimeSnifferAddonManager::_PutAddons(AddonReference** references, int32 count)
{
	for (int32 i = 0; i < count; i++)
		references[i]->PutReference();

	delete[] references;
}


}	// namespace Mime
}	// namespace Storage
}	// namespace BPrivate
