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
 *   Copyright 2007-2009, Ingo Weinhold, bonefish@users.sf.net.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file DiskSystemAddOnManager.cpp
 * @brief Singleton manager for loading and referencing disk system add-ons.
 *
 * DiskSystemAddOnManager discovers and loads BDiskSystemAddOn plug-ins from
 * the standard add-on directories at the request of the storage kit. It
 * maintains reference counts on loaded images so that they are unloaded only
 * when no longer needed, and serialises access with an internal BLocker.
 *
 * @see BDiskSystemAddOn
 * @see BDiskSystem
 */

#include <DiskSystemAddOnManager.h>

#include <exception>
#include <new>
#include <set>
#include <string>

#include <stdio.h>
#include <pthread.h>

#include <Directory.h>
#include <Entry.h>
#include <image.h>
#include <Path.h>

#include <AutoDeleter.h>
#include <AutoLocker.h>

#include <DiskSystemAddOn.h>


#undef TRACE
#define TRACE(format...)
//#define TRACE(format...)	printf(format)


using std::nothrow;


static pthread_once_t sManagerInitOnce = PTHREAD_ONCE_INIT;
DiskSystemAddOnManager* DiskSystemAddOnManager::sManager = NULL;


// AddOnImage
struct DiskSystemAddOnManager::AddOnImage {
	AddOnImage(image_id image)
		: image(image),
		  refCount(0)
	{
	}

	~AddOnImage()
	{
		unload_add_on(image);
	}

	image_id			image;
	int32				refCount;
};


// AddOn
struct DiskSystemAddOnManager::AddOn {
	AddOn(AddOnImage* image, BDiskSystemAddOn* addOn)
		: image(image),
		  addOn(addOn),
		  refCount(1)
	{
	}

	AddOnImage*			image;
	BDiskSystemAddOn*	addOn;
	int32				refCount;
};


// StringSet
struct DiskSystemAddOnManager::StringSet : std::set<std::string> {
};


/**
 * @brief Returns the singleton DiskSystemAddOnManager instance.
 *
 * Creates the instance on first call using pthread_once to ensure
 * thread-safe one-time initialisation.
 *
 * @return Pointer to the global DiskSystemAddOnManager singleton.
 */
DiskSystemAddOnManager*
DiskSystemAddOnManager::Default()
{
	if (sManager == NULL)
		pthread_once(&sManagerInitOnce, &_InitSingleton);

	return sManager;
}


/**
 * @brief Acquires the manager's internal lock.
 *
 * Must be paired with a call to Unlock(). Returns false if the lock could
 * not be acquired.
 *
 * @return true if the lock was acquired, false otherwise.
 */
bool
DiskSystemAddOnManager::Lock()
{
	return fLock.Lock();
}


/**
 * @brief Releases the manager's internal lock.
 */
void
DiskSystemAddOnManager::Unlock()
{
	fLock.Unlock();
}


/**
 * @brief Loads all disk system add-ons from the standard add-on directories.
 *
 * Searches the user non-packaged, user, system non-packaged, and system
 * add-on directories in priority order. Already-loaded add-ons are skipped.
 * The load count is incremented on each call; add-ons are only actually
 * loaded on the first call.
 *
 * @return B_OK on success, or an error code if loading failed.
 */
status_t
DiskSystemAddOnManager::LoadDiskSystems()
{
	AutoLocker<BLocker> _(fLock);

	if (++fLoadCount > 1)
		return B_OK;

	StringSet alreadyLoaded;
	status_t error
		= _LoadAddOns(alreadyLoaded, B_USER_NONPACKAGED_ADDONS_DIRECTORY);

	if (error == B_OK)
		error = _LoadAddOns(alreadyLoaded, B_USER_ADDONS_DIRECTORY);

	if (error == B_OK) {
		error
			= _LoadAddOns(alreadyLoaded, B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY);
	}

	if (error == B_OK)
		error = _LoadAddOns(alreadyLoaded, B_SYSTEM_ADDONS_DIRECTORY);

	if (error != B_OK)
		UnloadDiskSystems();

	return error;
}


/**
 * @brief Decrements the load count and unloads add-ons when it reaches zero.
 *
 * Moves all current add-ons into the pending-unload list and decrements
 * their reference counts. An add-on image is actually unloaded once its
 * reference count drops to zero (i.e. all users have called PutAddOn()).
 */
void
DiskSystemAddOnManager::UnloadDiskSystems()
{
	AutoLocker<BLocker> _(fLock);

	if (fLoadCount == 0 || --fLoadCount > 0)
		return;

	fAddOnsToBeUnloaded.AddList(&fAddOns);
	fAddOns.MakeEmpty();

	// put all add-ons -- that will cause them to be deleted as soon as they're
	// unused
	for (int32 i = fAddOnsToBeUnloaded.CountItems() - 1; i >= 0; i--)
		_PutAddOn(i);
}


/**
 * @brief Returns the number of currently loaded add-ons.
 *
 * @return The count of add-ons in the active list.
 */
int32
DiskSystemAddOnManager::CountAddOns() const
{
	return fAddOns.CountItems();
}


/**
 * @brief Returns the add-on at the given index without incrementing its
 *        reference count.
 *
 * @param index Zero-based index into the active add-on list.
 * @return Pointer to the BDiskSystemAddOn, or NULL if the index is out of
 *         range.
 */
BDiskSystemAddOn*
DiskSystemAddOnManager::AddOnAt(int32 index) const
{
	AddOn* addOn = _AddOnAt(index);
	return addOn ? addOn->addOn : NULL;
}


/**
 * @brief Looks up an add-on by name and increments its reference count.
 *
 * The caller must eventually pass the returned pointer to PutAddOn() to
 * balance the reference increment.
 *
 * @param name The canonical disk-system name to search for.
 * @return Pointer to the matching BDiskSystemAddOn with an incremented
 *         reference count, or NULL if not found.
 */
BDiskSystemAddOn*
DiskSystemAddOnManager::GetAddOn(const char* name)
{
	if (!name)
		return NULL;

	AutoLocker<BLocker> _(fLock);

	for (int32 i = 0; AddOn* addOn = _AddOnAt(i); i++) {
		if (strcmp(addOn->addOn->Name(), name) == 0) {
			addOn->refCount++;
			return addOn->addOn;
		}
	}

	return NULL;
}


/**
 * @brief Decrements the reference count of a previously obtained add-on.
 *
 * Must be called once for each successful call to GetAddOn(). Calling with
 * an unbalanced pointer triggers a debugger call.
 *
 * @param _addOn The add-on pointer previously returned by GetAddOn().
 */
void
DiskSystemAddOnManager::PutAddOn(BDiskSystemAddOn* _addOn)
{
	if (!_addOn)
		return;

	AutoLocker<BLocker> _(fLock);

	for (int32 i = 0; AddOn* addOn = _AddOnAt(i); i++) {
		if (_addOn == addOn->addOn) {
			if (addOn->refCount > 1) {
				addOn->refCount--;
			} else {
				debugger("Unbalanced call to "
					"DiskSystemAddOnManager::PutAddOn()");
			}
			return;
		}
	}

	for (int32 i = 0;
		 AddOn* addOn = (AddOn*)fAddOnsToBeUnloaded.ItemAt(i); i++) {
		if (_addOn == addOn->addOn) {
			_PutAddOn(i);
			return;
		}
	}

	debugger("DiskSystemAddOnManager::PutAddOn(): disk system not found");
}


/**
 * @brief Constructs the DiskSystemAddOnManager singleton.
 *
 * Initialises the lock, the add-on lists, and the load counter.
 */
DiskSystemAddOnManager::DiskSystemAddOnManager()
	: fLock("disk system add-ons manager"),
	  fAddOns(),
	  fAddOnsToBeUnloaded(),
	  fLoadCount(0)
{
}


/**
 * @brief pthread_once callback that allocates the singleton instance.
 */
/*static*/ void
DiskSystemAddOnManager::_InitSingleton()
{
	sManager = new DiskSystemAddOnManager();
}


/**
 * @brief Returns the internal AddOn wrapper at the given index.
 *
 * @param index Zero-based index into the active add-on list.
 * @return Pointer to the AddOn wrapper, or NULL if out of range.
 */
DiskSystemAddOnManager::AddOn*
DiskSystemAddOnManager::_AddOnAt(int32 index) const
{
	return (AddOn*)fAddOns.ItemAt(index);
}


/**
 * @brief Decrements the reference count of the pending-unload add-on at
 *        the given index and frees it when the count reaches zero.
 *
 * @param index Index into fAddOnsToBeUnloaded.
 */
void
DiskSystemAddOnManager::_PutAddOn(int32 index)
{
	AddOn* addOn = (AddOn*)fAddOnsToBeUnloaded.ItemAt(index);
	if (!addOn)
		return;

	if (--addOn->refCount == 0) {
		if (--addOn->image->refCount == 0)
			delete addOn->image;

		fAddOnsToBeUnloaded.RemoveItem(index);
		delete addOn;
	}
}


/**
 * @brief Loads all disk system add-ons found in the given directory constant.
 *
 * Resolves the directory path, opens the "disk_systems" subdirectory, and
 * attempts to load each entry as an add-on image. Already-loaded names
 * (tracked via alreadyLoaded) are skipped. On success each add-on name is
 * inserted into alreadyLoaded to prevent duplicate loads from lower-priority
 * directories.
 *
 * @param alreadyLoaded Set of add-on file names that have already been loaded.
 * @param addOnDir      Directory constant identifying the search directory.
 * @return B_OK on success, or an error code if a critical failure occurred.
 */
status_t
DiskSystemAddOnManager::_LoadAddOns(StringSet& alreadyLoaded,
	directory_which addOnDir)
{
	// get the add-on directory path
	BPath path;
	status_t error = find_directory(addOnDir, &path, false);
	if (error != B_OK)
		return error;

	TRACE("DiskSystemAddOnManager::_LoadAddOns(): %s\n", path.Path());

	error = path.Append("disk_systems");
	if (error != B_OK)
		return error;

	if (!BEntry(path.Path()).Exists())
		return B_OK;

	// open the directory and iterate through its entries
	BDirectory directory;
	error = directory.SetTo(path.Path());
	if (error != B_OK)
		return error;

	entry_ref ref;
	while (directory.GetNextRef(&ref) == B_OK) {
		// skip, if already loaded
		if (alreadyLoaded.find(ref.name) != alreadyLoaded.end()) {
			TRACE("  skipping \"%s\" -- already loaded\n", ref.name);
			continue;
		}

		// get the entry path
		BPath entryPath;
		error = entryPath.SetTo(&ref);
		if (error != B_OK) {
			if (error == B_NO_MEMORY)
				return error;
			TRACE("  skipping \"%s\" -- failed to get path\n", ref.name);
			continue;
		}

		// load the add-on
		image_id image = load_add_on(entryPath.Path());
		if (image < 0) {
			TRACE("  skipping \"%s\" -- failed to load add-on\n", ref.name);
			continue;
		}

		AddOnImage* addOnImage = new(nothrow) AddOnImage(image);
		if (!addOnImage) {
			unload_add_on(image);
			return B_NO_MEMORY;
		}
		ObjectDeleter<AddOnImage> addOnImageDeleter(addOnImage);

		// get the add-on objects
		status_t (*getAddOns)(BList*);
		error = get_image_symbol(image, "get_disk_system_add_ons",
			B_SYMBOL_TYPE_TEXT, (void**)&getAddOns);
		if (error != B_OK) {
			TRACE("  skipping \"%s\" -- function symbol not found\n", ref.name);
			continue;
		}

		BList addOns;
		error = getAddOns(&addOns);
		if (error != B_OK || addOns.IsEmpty()) {
			TRACE("  skipping \"%s\" -- getting add-ons failed\n", ref.name);
			continue;
		}

		// create and add AddOn objects
		int32 count = addOns.CountItems();
		for (int32 i = 0; i < count; i++) {
			BDiskSystemAddOn* diskSystemAddOn
				= (BDiskSystemAddOn*)addOns.ItemAt(i);
			AddOn* addOn = new(nothrow) AddOn(addOnImage, diskSystemAddOn);
			if (!addOn)
				return B_NO_MEMORY;

			if (fAddOns.AddItem(addOn)) {
				addOnImage->refCount++;
				addOnImageDeleter.Detach();
			} else {
				delete addOn;
				return B_NO_MEMORY;
			}
		}

		TRACE("  got %ld BDiskSystemAddOn(s) from add-on \"%s\"\n", count,
			ref.name);

		// add the add-on name to the set of already loaded add-ons
		try {
			alreadyLoaded.insert(ref.name);
		} catch (std::bad_alloc& exception) {
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}
