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
 *   Copyright 2002-2015, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 *       Markus Himmel, markus@himmel-villmar.de
 *       Michael Wilber
 */


/**
 * @file TranslatorRoster.cpp
 * @brief Central coordinator for the Translation Kit
 *
 * Implements BTranslatorRoster, the main entry point for locating and
 * invoking translators. The public API is a thin shell over the internal
 * BTranslatorRoster::Private class, which manages translator discovery,
 * add-on loading/unloading (both make_nth_translator() and C-style function
 * pointer add-ons), directory node monitoring for hot-plug, and per-roster
 * change notification. The default singleton roster is thread-safely
 * initialized on first use.
 *
 * @see Translator.cpp, FuncTranslator.cpp, TranslationUtils.cpp
 */


#include <TranslatorRoster.h>

#include <new>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

#include <Application.h>
#include <Autolock.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <String.h>

#include <driver_settings.h>
#include <image.h>
#include <safemode_defs.h>
#include <syscalls.h>

#include "FuncTranslator.h"
#include "TranslatorRosterPrivate.h"


namespace BPrivate {


/**
 * @brief RAII guard that conditionally removes a translator entry_ref on destruction.
 *
 * Used in Private::CreateTranslators() to replace an existing translator that
 * has lower directory priority: the old translator's ref is placed into
 * quarantine, and if the new one succeeds Remove() is called so the old one
 * is evicted; if the new one fails, the destructor leaves the old one in place.
 */
class QuarantineTranslatorImage {
public:
								QuarantineTranslatorImage(
									BTranslatorRoster::Private& privateRoster);
								~QuarantineTranslatorImage();

			void				Put(const entry_ref& ref);
			void				Remove();

private:
			BTranslatorRoster::Private& fRoster;
			entry_ref			fRef;
			bool				fRemove;
};

}	// namespace BPrivate


// Extensions used in the extension BMessage, defined in TranslatorFormats.h
char B_TRANSLATOR_EXT_HEADER_ONLY[]			= "/headerOnly";
char B_TRANSLATOR_EXT_DATA_ONLY[]			= "/dataOnly";
char B_TRANSLATOR_EXT_COMMENT[]				= "/comment";
char B_TRANSLATOR_EXT_TIME[]				= "/time";
char B_TRANSLATOR_EXT_FRAME[]				= "/frame";
char B_TRANSLATOR_EXT_BITMAP_RECT[]			= "bits/Rect";
char B_TRANSLATOR_EXT_BITMAP_COLOR_SPACE[]	= "bits/space";
char B_TRANSLATOR_EXT_BITMAP_PALETTE[]		= "bits/palette";
char B_TRANSLATOR_EXT_SOUND_CHANNEL[]		= "nois/channel";
char B_TRANSLATOR_EXT_SOUND_MONO[]			= "nois/mono";
char B_TRANSLATOR_EXT_SOUND_MARKER[]		= "nois/marker";
char B_TRANSLATOR_EXT_SOUND_LOOP[]			= "nois/loop";

BTranslatorRoster* BTranslatorRoster::sDefaultRoster = NULL;


namespace BPrivate {


/**
 * @brief Constructs a QuarantineTranslatorImage for the given private roster.
 * @param privateRoster The roster instance that will perform the removal.
 */
QuarantineTranslatorImage::QuarantineTranslatorImage(
	BTranslatorRoster::Private& privateRoster)
	:
	fRoster(privateRoster),
	fRemove(false)
{
}


/**
 * @brief Destroys the guard, removing the quarantined ref from the roster if
 *     Remove() was called.
 */
QuarantineTranslatorImage::~QuarantineTranslatorImage()
{
	if (fRef.device == -1 || !fRemove)
		return;

	fRoster.RemoveTranslators(fRef);
}


/**
 * @brief Registers an entry_ref to be removed on destruction if Remove() is called.
 * @param ref The entry_ref of the translator to potentially evict.
 */
void
QuarantineTranslatorImage::Put(const entry_ref& ref)
{
	fRef = ref;
}


/**
 * @brief Marks the quarantined ref for removal when the guard is destroyed.
 */
void
QuarantineTranslatorImage::Remove()
{
	fRemove = true;
}


}	// namespace BPrivate


//	#pragma mark - BTranslatorRoster::Private


/**
 * @brief Constructs the private roster implementation.
 *
 * Detects safe-mode and user add-on disable flags via safemode options.
 * Checks for an ABI mismatch and sets fABISubDirectory so that translators
 * matching the running ABI can be found in gcc2 / gcc4 subdirectories.
 * Registers itself as a BHandler with the running BApplication so it can
 * receive node monitor and B_DELETE_TRANSLATOR messages.
 */
BTranslatorRoster::Private::Private()
	:
	BHandler("translator roster"),
	BLocker("translator list"),
	fABISubDirectory(NULL),
	fNextID(1),
	fLazyScanning(true),
	fSafeMode(false)
{
	char parameter[32];
	size_t parameterLength = sizeof(parameter);

	if (_kern_get_safemode_option(B_SAFEMODE_SAFE_MODE, parameter,
			&parameterLength) == B_OK) {
		if (!strcasecmp(parameter, "enabled") || !strcasecmp(parameter, "on")
			|| !strcasecmp(parameter, "true") || !strcasecmp(parameter, "yes")
			|| !strcasecmp(parameter, "enable") || !strcmp(parameter, "1"))
			fSafeMode = true;
	}

	if (_kern_get_safemode_option(B_SAFEMODE_DISABLE_USER_ADD_ONS, parameter,
			&parameterLength) == B_OK) {
		if (!strcasecmp(parameter, "enabled") || !strcasecmp(parameter, "on")
			|| !strcasecmp(parameter, "true") || !strcasecmp(parameter, "yes")
			|| !strcasecmp(parameter, "enable") || !strcmp(parameter, "1"))
			fSafeMode = true;
	}

	// We might run in compatibility mode on a system with a different ABI. The
	// translators matching our ABI can usually be found in respective
	// subdirectories of the translator directories.
	system_info info;
	if (get_system_info(&info) == B_OK
		&& (info.abi & B_HAIKU_ABI_MAJOR)
			!= (B_HAIKU_ABI & B_HAIKU_ABI_MAJOR)) {
			switch (B_HAIKU_ABI & B_HAIKU_ABI_MAJOR) {
				case B_HAIKU_ABI_GCC_2:
					fABISubDirectory = "gcc2";
					break;
				case B_HAIKU_ABI_GCC_4:
					fABISubDirectory = "gcc4";
					break;
			}
	}

	// we're sneaking ourselves into the BApplication, if it's running
	if (be_app != NULL && !be_app->IsLaunching() && be_app->Lock()) {
		be_app->AddHandler(this);
		be_app->Unlock();
	}
}


/**
 * @brief Destroys the private roster, releasing all translators and unloading images.
 *
 * Stops node monitoring, removes this handler from the looper, releases every
 * registered BTranslator (which may trigger B_DELETE_TRANSLATOR), and
 * unloads all add-on images that are no longer referenced.
 */
BTranslatorRoster::Private::~Private()
{
	stop_watching(this);

	if (Looper() && LockLooper()) {
		BLooper* looper = Looper();
		Looper()->RemoveHandler(this);
		looper->Unlock();
	}

	// Release all translators, so that they can delete themselves

	TranslatorMap::iterator iterator = fTranslators.begin();
	std::set<image_id> images;

	while (iterator != fTranslators.end()) {
		BTranslator* translator = iterator->second.translator;

		images.insert(iterator->second.image);
		translator->Release();

		iterator++;
	}

	// Unload all images

	std::set<image_id>::const_iterator imageIterator = images.begin();

	while (imageIterator != images.end()) {
		unload_add_on(*imageIterator);
		imageIterator++;
	}
}


/**
 * @brief Handles node monitor events and B_DELETE_TRANSLATOR messages.
 *
 * B_NODE_MONITOR with B_ENTRY_CREATED, B_ENTRY_MOVED, or B_ENTRY_REMOVED
 * opcodes update the translator map accordingly. B_DELETE_TRANSLATOR is sent
 * by a BTranslator whose refcount has reached zero; this handler deletes the
 * object and unloads the add-on image.
 *
 * @param message The incoming BMessage.
 */
void
BTranslatorRoster::Private::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_NODE_MONITOR:
		{
			BAutolock locker(this);

			printf("translator roster node monitor: ");
			message->PrintToStream();

			int32 opcode;
			if (message->FindInt32("opcode", &opcode) != B_OK)
				return;

			switch (opcode) {
				case B_ENTRY_CREATED:
				{
					const char* name;
					node_ref nodeRef;
					if (message->FindInt32("device", &nodeRef.device) != B_OK
						|| message->FindInt64("directory", &nodeRef.node)
							!= B_OK
						|| message->FindString("name", &name) != B_OK)
						break;

					// TODO: make this better (possible under Haiku)
					snooze(100000);
						// let the font be written completely before trying to
						// open it

					_EntryAdded(nodeRef, name);
					break;
				}

				case B_ENTRY_MOVED:
				{
					// has the entry been moved into a monitored directory or
					// has it been removed from one?
					const char* name;
					node_ref toNodeRef;
					node_ref fromNodeRef;
					node_ref nodeRef;

					if (message->FindInt32("device", &nodeRef.device) != B_OK
						|| message->FindInt64("to directory", &toNodeRef.node)
							!= B_OK
						|| message->FindInt64("from directory",
							(int64*)&fromNodeRef.node) != B_OK
						|| message->FindInt64("node", (int64*)&nodeRef.node)
							!= B_OK
						|| message->FindString("name", &name) != B_OK)
						break;

					fromNodeRef.device = nodeRef.device;
					toNodeRef.device = nodeRef.device;

					// Do we know this one yet?
					translator_item* item = _FindTranslator(nodeRef);
					if (item == NULL) {
						// it's a new one!
						if (_IsKnownDirectory(toNodeRef))
							_EntryAdded(toNodeRef, name);
						break;
					}

					if (!_IsKnownDirectory(toNodeRef)) {
						// translator got removed
						_RemoveTranslators(&nodeRef);
						break;
					}

					// the name may have changed
					item->ref.set_name(name);
					item->ref.directory = toNodeRef.node;

					if (_IsKnownDirectory(fromNodeRef)
						&& _IsKnownDirectory(toNodeRef)) {
						// TODO: we should rescan for the name, there might be
						// name clashes with translators in other directories
						// (as well as old ones revealed)
						break;
					}
					break;
				}

				case B_ENTRY_REMOVED:
				{
					node_ref nodeRef;
					uint64 directoryNode;
					if (message->FindInt32("device", &nodeRef.device) != B_OK
						|| message->FindInt64("directory",
							(int64*)&directoryNode) != B_OK
						|| message->FindInt64("node", &nodeRef.node) != B_OK)
						break;

					translator_item* item = _FindTranslator(nodeRef);
					if (item != NULL)
						_RemoveTranslators(&nodeRef);
					break;
				}
			}
			break;
		}

		case B_DELETE_TRANSLATOR:
		{
			// A translator's refcount has been reduced to zero and it wants
			// us to delete it.
			int32 id;
			void* self;
			if (message->FindInt32("id", &id) == B_OK
				&& message->FindPointer("ptr", &self) == B_OK) {
				_TranslatorDeleted(id, (BTranslator*)self);
			}
			break;
		}

		default:
			BHandler::MessageReceived(message);
			break;
	}
}


/**
 * @brief Adds the standard system and user translator directories to the roster.
 *
 * Directories are added in priority order (user non-packaged, user packaged,
 * system non-packaged, system packaged). In safe mode, only system directories
 * are used. Each directory is created if it does not exist.
 */
void
BTranslatorRoster::Private::AddDefaultPaths()
{
	// add user directories first, so that they can override system translators
	const directory_which paths[] = {
		B_USER_NONPACKAGED_ADDONS_DIRECTORY,
		B_USER_ADDONS_DIRECTORY,
		B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY,
		B_SYSTEM_ADDONS_DIRECTORY,
	};

	for (uint32 i = fSafeMode ? 4 : 0; i < sizeof(paths) / sizeof(paths[0]);
			i++) {
		BPath path;
		status_t status = find_directory(paths[i], &path, true);
		if (status == B_OK && path.Append("Translators") == B_OK) {
			mkdir(path.Path(), 0755);
				// make sure the directory exists before we add it
			AddPath(path.Path());
		}
	}
}


/**
 * @brief Adds a colon-separated list of directory paths to the roster.
 *
 * The order of directories matters: translators in earlier directories have
 * higher priority over those with the same name in later directories.
 *
 * @param paths Colon-delimited string of directory paths.
 * @return B_OK if at least one translator was loaded, or the last error
 *     encountered if none could be loaded. B_BAD_VALUE if \a paths is NULL.
 */
status_t
BTranslatorRoster::Private::AddPaths(const char* paths)
{
	if (paths == NULL)
		return B_BAD_VALUE;

	status_t status = B_OK;
	int32 added = 0;

	while (paths != NULL) {
		const char* end = strchr(paths, ':');
		BString path;

		if (end != NULL) {
			path.SetTo(paths, end - 1 - paths);
			paths = end + 1;
		} else {
			path.SetTo(paths);
			paths = NULL;
		}

		// Keep the last error that occured, and return it
		// but don't overwrite it, if the last path was
		// added successfully.
		int32 count;
		status_t error = AddPath(path.String(), &count);
		if (error != B_NO_ERROR)
			status = error;

		added += count;
	}

	if (added == 0)
		return status;

	return B_OK;
}


/**
 * @brief Adds a single translator directory to the roster.
 *
 * If an ABI subdirectory (gcc2/gcc4) exists under \a path, it is used
 * instead. Starts node monitoring on the directory and loads all translator
 * add-ons found within it.
 *
 * @param path Absolute path of the directory to scan.
 * @param _added If non-NULL, set to the number of translators successfully
 *     loaded from this directory.
 * @return B_OK on success, or an error code if the directory cannot be opened.
 */
status_t
BTranslatorRoster::Private::AddPath(const char* path, int32* _added)
{
	BDirectory directory(path);
	status_t status = directory.InitCheck();
	if (status != B_OK)
		return status;

	// if a subdirectory for our ABI exists, use that instead
	if (fABISubDirectory != NULL) {
		BEntry entry(&directory, fABISubDirectory);
		if (entry.IsDirectory()) {
			status = directory.SetTo(&entry);
			if (status != B_OK)
				return status;
		}
	}

	node_ref nodeRef;
	status = directory.GetNodeRef(&nodeRef);
	if (status < B_OK)
		return status;

	// do we know this directory already?
	if (_IsKnownDirectory(nodeRef))
		return B_OK;

	if (Looper() != NULL) {
		// watch that directory
		watch_node(&nodeRef, B_WATCH_DIRECTORY, this);
		fDirectories.push_back(nodeRef);
	}

	int32 count = 0;
	int32 files = 0;

	entry_ref ref;
	while (directory.GetNextRef(&ref) == B_OK) {
		BEntry entry(&ref);
		if (entry.IsDirectory())
			continue;
		if (CreateTranslators(ref, count) == B_OK)
			count++;

		files++;
	}

	if (_added)
		*_added = count;

	if (files != 0 && count == 0)
		return B_BAD_VALUE;

	return B_OK;
}


/**
 * @brief Registers a BTranslator object in the roster's internal map.
 *
 * Thread-safe. Assigns the next available ID to the translator and records
 * its image ID, node inode, and entry_ref for future lookup.
 *
 * @param translator The translator to register (not yet Acquire()'d here).
 * @param image The image_id of the add-on, or -1 for built-in translators.
 * @param ref Optional entry_ref of the add-on file.
 * @param node Inode number of the add-on file for node-ref matching.
 * @return B_OK on success, or B_NO_MEMORY if the map cannot be extended.
 */
status_t
BTranslatorRoster::Private::AddTranslator(BTranslator* translator,
	image_id image, const entry_ref* ref, ino_t node)
{
	BAutolock locker(this);

	translator_item item;
	item.translator = translator;
	item.image = image;
	item.node = node;
	if (ref != NULL)
		item.ref = *ref;

	try {
		fTranslators[fNextID] = item;
	} catch (...) {
		return B_NO_MEMORY;
	}

	translator->fOwningRoster = this;
	translator->fID = fNextID++;
	return B_OK;
}


/**
 * @brief Removes all translators loaded from the given entry_ref.
 * @param ref The entry_ref identifying the add-on file to evict.
 */
void
BTranslatorRoster::Private::RemoveTranslators(entry_ref& ref)
{
	_RemoveTranslators(NULL, &ref);
}


/**
 * @brief Looks up a BTranslator by its assigned ID.
 *
 * The roster must be locked by the caller before calling this function.
 *
 * @param id The translator_id to look up.
 * @return Pointer to the BTranslator, or NULL if not found.
 */
BTranslator*
BTranslatorRoster::Private::FindTranslator(translator_id id)
{
	if (!IsLocked()) {
		debugger("translator must be locked!");
		return NULL;
	}

	const translator_item* item = _FindTranslator(id);
	if (item != NULL)
		return item->translator;

	return NULL;
}


/**
 * @brief Reads the required C-style function symbols from a loaded add-on image.
 *
 * Looks for translatorName, translatorInfo, translatorVersion, inputFormats,
 * outputFormats, Identify, and Translate. MakeConfig and GetConfigMessage are
 * optional and silently skipped if absent.
 *
 * @param image The loaded add-on image to inspect.
 * @param data Filled with the symbol pointers and metadata on success.
 * @return B_OK if all required symbols were found, or B_BAD_TYPE if any
 *     required symbol is missing.
 */
status_t
BTranslatorRoster::Private::GetTranslatorData(image_id image,
	translator_data& data)
{
	// If this is a translator add-on, it is in the C format
	memset(&data, 0, sizeof(translator_data));

	// find all the symbols

	int32* version;
	if (get_image_symbol(image, "translatorName", B_SYMBOL_TYPE_DATA,
			(void**)&data.name) < B_OK
		|| get_image_symbol(image, "translatorInfo", B_SYMBOL_TYPE_DATA,
			(void**)&data.info) < B_OK
		|| get_image_symbol(image, "translatorVersion", B_SYMBOL_TYPE_DATA,
			(void**)&version) < B_OK || version == NULL
		|| get_image_symbol(image, "inputFormats", B_SYMBOL_TYPE_DATA,
			(void**)&data.input_formats) < B_OK
		|| get_image_symbol(image, "outputFormats", B_SYMBOL_TYPE_DATA,
			(void**)&data.output_formats) < B_OK
		|| get_image_symbol(image, "Identify", B_SYMBOL_TYPE_TEXT,
			(void**)&data.identify_hook) < B_OK
		|| get_image_symbol(image, "Translate", B_SYMBOL_TYPE_TEXT,
			(void**)&data.translate_hook) < B_OK) {
		return B_BAD_TYPE;
	}

	data.version = *version;

	// those calls are optional
	get_image_symbol(image, "MakeConfig", B_SYMBOL_TYPE_TEXT,
		(void**)&data.make_config_hook);
	get_image_symbol(image, "GetConfigMessage", B_SYMBOL_TYPE_TEXT,
		(void**)&data.get_config_message_hook);

	return B_OK;
}


/**
 * @brief Loads the add-on at \a ref and registers the translator(s) it provides.
 *
 * If an existing translator with the same filename has higher directory
 * priority, the new add-on is ignored. If the new one has higher priority,
 * the old translator is quarantined and replaced on success. Supports both
 * make_nth_translator() (post-R4.5) and C-style add-ons. Thread-safe.
 *
 * @param ref entry_ref of the add-on file to load.
 * @param count Incremented for each translator successfully registered.
 * @param update If non-NULL, translator IDs of newly added translators are
 *     appended to this message for change notification.
 * @return B_OK on success, or an error code if the add-on cannot be loaded.
 */
status_t
BTranslatorRoster::Private::CreateTranslators(const entry_ref& ref,
	int32& count, BMessage* update)
{
	BAutolock locker(this);

	BPrivate::QuarantineTranslatorImage quarantine(*this);

	const translator_item* item = _FindTranslator(ref.name);
	if (item != NULL) {
		// check if the known translator has a higher priority
		if (_CompareTranslatorDirectoryPriority(item->ref, ref) <= 0) {
			// keep the existing add-on
			return B_OK;
		}

		// replace existing translator(s) if the new translator succeeds
		quarantine.Put(item->ref);
	}

	BEntry entry(&ref);
	node_ref nodeRef;
	status_t status = entry.GetNodeRef(&nodeRef);
	if (status < B_OK)
		return status;

	BPath path(&ref);
	image_id image = load_add_on(path.Path());
	if (image < B_OK)
		return image;

	// Function pointer used to create post R4.5 style translators
	BTranslator *(*makeNthTranslator)(int32 n, image_id you, uint32 flags, ...);

	status = get_image_symbol(image, "make_nth_translator",
		B_SYMBOL_TYPE_TEXT, (void**)&makeNthTranslator);
	if (status == B_OK) {
		// If the translator add-on supports the post R4.5
		// translator creation mechanism, keep loading translators
		// until MakeNthTranslator stops returning them.
		BTranslator* translator = NULL;
		int32 created = 0;
		for (int32 n = 0; (translator = makeNthTranslator(n, image, 0)) != NULL;
				n++) {
			if (AddTranslator(translator, image, &ref, nodeRef.node) == B_OK) {
				if (update)
					update->AddInt32("translator_id", translator->fID);
				fImageOrigins.insert(std::make_pair(translator, image));
				count++;
				created++;
			} else {
				translator->Release();
					// this will delete the translator
			}
		}

		if (created == 0) {
			unload_add_on(image);
		} else {
			// Initial refcount for the image that was just loaded
			fKnownImages.insert(std::make_pair(image, created));
		}

		quarantine.Remove();
		return B_OK;
	}

	// If this is a translator add-on, it is in the C format
	translator_data translatorData;
	status = GetTranslatorData(image, translatorData);

	// add this translator to the list
	BPrivate::BFuncTranslator* translator = NULL;
	if (status == B_OK) {
		translator = new (std::nothrow) BPrivate::BFuncTranslator(
			translatorData);
		if (translator == NULL)
			status = B_NO_MEMORY;
	}

	if (status == B_OK)
		status = AddTranslator(translator, image, &ref, nodeRef.node);

	if (status == B_OK) {
		if (update)
			update->AddInt32("translator_id", translator->fID);
		quarantine.Remove();
		count++;
	} else
		unload_add_on(image);

	return status;
}


/**
 * @brief Registers a messenger to receive change notifications.
 *
 * Once at least one watcher is registered, lazy scanning is disabled and
 * any pending rescan entries are processed immediately.
 *
 * @param target The messenger to notify on translator additions/removals.
 * @return B_OK on success, or B_NO_MEMORY if the list cannot be extended.
 */
status_t
BTranslatorRoster::Private::StartWatching(BMessenger target)
{
	try {
		fMessengers.push_back(target);
	} catch (...) {
		return B_NO_MEMORY;
	}

	if (fLazyScanning) {
		fLazyScanning = false;
			// Since we now have someone to report to, we cannot lazily
			// adopt changes to the translator any longer

		_RescanChanged();
	}

	return B_OK;
}


/**
 * @brief Unregisters a messenger from change notifications.
 *
 * If the watcher list becomes empty, lazy scanning is re-enabled.
 *
 * @param target The messenger to remove.
 * @return B_OK on success, or B_BAD_VALUE if \a target is not registered.
 */
status_t
BTranslatorRoster::Private::StopWatching(BMessenger target)
{
	MessengerList::iterator iterator = fMessengers.begin();

	while (iterator != fMessengers.end()) {
		if (*iterator == target) {
			fMessengers.erase(iterator);
			if (fMessengers.empty())
				fLazyScanning = true;

			return B_OK;
		}

		iterator++;
	}

	return B_BAD_VALUE;
}


/**
 * @brief Archives the file paths of all registered disk-based translators.
 *
 * Appends "be:translator_path" strings to \a archive, one per translator.
 * Used by BTranslatorRoster::Archive().
 *
 * @param archive The BMessage to append paths to.
 * @return B_OK.
 */
status_t
BTranslatorRoster::Private::StoreTranslators(BMessage& archive)
{
	BAutolock locker(this);

	TranslatorMap::const_iterator iterator = fTranslators.begin();

	while (iterator != fTranslators.end()) {
		const translator_item& item = iterator->second;
		BPath path(&item.ref);
		if (path.InitCheck() == B_OK)
			archive.AddString("be:translator_path", path.Path());

		iterator++;
	}

	return B_OK;
}


/**
 * @brief Identifies the best translator for the data in \a source.
 *
 * Iterates all registered translators, calling Identify() on each. Returns
 * the translator_info for the one with the highest quality × capability score.
 *
 * @param source The stream to identify.
 * @param ioExtension Optional extension message; updated with the winning
 *     translator's extension data if non-NULL.
 * @param hintType Type hint (0 = unknown).
 * @param hintMIME MIME hint (NULL = unknown).
 * @param wantType Desired output type (0 = any).
 * @param _info Set to the best translator_info on success.
 * @return B_OK on success, or B_NO_TRANSLATOR if no translator matched.
 */
status_t
BTranslatorRoster::Private::Identify(BPositionIO* source,
	BMessage* ioExtension, uint32 hintType, const char* hintMIME,
	uint32 wantType, translator_info* _info)
{
	BAutolock locker(this);

	_RescanChanged();

	TranslatorMap::const_iterator iterator = fTranslators.begin();
	BMessage baseExtension;
	if (ioExtension != NULL)
		baseExtension = *ioExtension;

	float bestWeight = 0.0f;

	while (iterator != fTranslators.end()) {
		BTranslator& translator = *iterator->second.translator;

		off_t pos = source->Seek(0, SEEK_SET);
		if (pos != 0)
			return pos < 0 ? (status_t)pos : B_IO_ERROR;

		int32 formatsCount = 0;
		const translation_format* formats = translator.InputFormats(
			&formatsCount);
		const translation_format* format = _CheckHints(formats, formatsCount,
			hintType, hintMIME);

		BMessage extension(baseExtension);
		translator_info info;
		if (translator.Identify(source, format, &extension, &info, wantType)
				== B_OK) {
			float weight = info.quality * info.capability;
			if (weight > bestWeight) {
				if (ioExtension != NULL)
					*ioExtension = extension;
				bestWeight = weight;

				info.translator = iterator->first;
				memcpy(_info, &info, sizeof(translator_info));
			}
		}

		iterator++;
	}

	if (bestWeight > 0.0f)
		return B_OK;

	return B_NO_TRANSLATOR;
}


/**
 * @brief Returns all translators capable of handling \a source.
 *
 * The returned array is sorted by quality × capability (best first). The
 * caller must delete[] the array when done.
 *
 * @param source The stream to identify.
 * @param ioExtension Optional extension message.
 * @param hintType Type hint (0 = unknown).
 * @param hintMIME MIME hint (NULL = unknown).
 * @param wantType Desired output type (0 = any).
 * @param _info Set to a newly allocated array of matching translator_info.
 * @param _numInfo Set to the number of entries in \a _info.
 * @return B_OK on success, or an error code.
 */
status_t
BTranslatorRoster::Private::GetTranslators(BPositionIO* source,
	BMessage* ioExtension, uint32 hintType, const char* hintMIME,
	uint32 wantType, translator_info** _info, int32* _numInfo)
{
	BAutolock locker(this);

	_RescanChanged();

	int32 arraySize = fTranslators.size();
	translator_info* array = new (std::nothrow) translator_info[arraySize];
	if (array == NULL)
		return B_NO_MEMORY;

	TranslatorMap::const_iterator iterator = fTranslators.begin();
	int32 count = 0;

	while (iterator != fTranslators.end()) {
		BTranslator& translator = *iterator->second.translator;

		off_t pos = source->Seek(0, SEEK_SET);
		if (pos != 0) {
			delete[] array;
			return pos < 0 ? status_t(pos) : B_IO_ERROR;
		}

		int32 formatsCount = 0;
		const translation_format* formats = translator.InputFormats(
			&formatsCount);
		const translation_format* format = _CheckHints(formats, formatsCount,
			hintType, hintMIME);

		translator_info info;
		if (translator.Identify(source, format, ioExtension, &info, wantType)
				== B_OK) {
			info.translator = iterator->first;
			array[count++] = info;
		}

		iterator++;
	}

	*_info = array;
	*_numInfo = count;
	qsort(array, count, sizeof(translator_info),
		BTranslatorRoster::Private::_CompareSupport);
		// translators are sorted by best support

	return B_OK;
}


/**
 * @brief Returns an array of all registered translator IDs.
 *
 * The caller must delete[] the array when done.
 *
 * @param _ids Set to a newly allocated array of translator_id values.
 * @param _count Set to the number of entries in \a _ids.
 * @return B_OK on success, or B_NO_MEMORY.
 */
status_t
BTranslatorRoster::Private::GetAllTranslators(translator_id** _ids,
	int32* _count)
{
	BAutolock locker(this);

	_RescanChanged();

	int32 arraySize = fTranslators.size();
	translator_id* array = new (std::nothrow) translator_id[arraySize];
	if (array == NULL)
		return B_NO_MEMORY;

	TranslatorMap::const_iterator iterator = fTranslators.begin();
	int32 count = 0;

	while (iterator != fTranslators.end()) {
		array[count++] = iterator->first;
		iterator++;
	}

	*_ids = array;
	*_count = count;
	return B_OK;
}


/**
 * @brief Returns the entry_ref for the add-on file of a translator.
 * @param id The translator to look up.
 * @param ref Set to the entry_ref on success.
 * @return B_OK on success, B_NO_TRANSLATOR if not found, or B_ERROR if the
 *     add-on file no longer exists on disk.
 */
status_t
BTranslatorRoster::Private::GetRefFor(translator_id id, entry_ref& ref)
{
	BAutolock locker(this);

	const translator_item* item = _FindTranslator(id);
	if (item == NULL)
		return B_NO_TRANSLATOR;

	BEntry entry(&item->ref);
	if (entry.InitCheck() == B_OK && entry.Exists() && entry.IsFile()) {
		ref = item->ref;
		return B_OK;
	}

	return B_ERROR;
}


/**
 * @brief Deletes a translator whose refcount has reached zero and unloads its image.
 *
 * Called from MessageReceived() in response to a B_DELETE_TRANSLATOR message.
 * Erases the translator from the map, deletes the object, decrements the
 * per-image reference count, and unloads the image if it reaches zero.
 *
 * @param id The translator_id being deleted.
 * @param self Pointer to the BTranslator object to delete.
 */
void
BTranslatorRoster::Private::_TranslatorDeleted(translator_id id, BTranslator* self)
{
	BAutolock locker(this);

	TranslatorMap::iterator iterator = fTranslators.find(id);
	if (iterator != fTranslators.end())
		fTranslators.erase(iterator);

	image_id image = fImageOrigins[self];

	delete self;

	int32 former = atomic_add(&fKnownImages[image], -1);
	if (former == 1) {
		unload_add_on(image);
		fImageOrigins.erase(self);
		fKnownImages.erase(image);
	}
}


/**
 * @brief Compares two translator_info structs by quality × capability weight.
 *
 * Used as a qsort comparator in GetTranslators() to sort results best-first.
 *
 * @param _a Pointer to the first translator_info.
 * @param _b Pointer to the second translator_info.
 * @return -1 if a is better, 1 if b is better, 0 if equal.
 */
/*static*/ int
BTranslatorRoster::Private::_CompareSupport(const void* _a, const void* _b)
{
	const translator_info* infoA = (const translator_info*)_a;
	const translator_info* infoB = (const translator_info*)_b;

	float weightA = infoA->quality * infoA->capability;
	float weightB = infoB->quality * infoB->capability;

	if (weightA == weightB)
		return 0;
	if (weightA > weightB)
		return -1;

	return 1;
}


/**
 * @brief Processes all entries queued for rescanning since the last translation call.
 *
 * In lazy mode, newly installed translators are not loaded immediately; they
 * are added to fRescanEntries. This method loads them when a translation
 * method is next called. It does not notify listeners (lazy mode implies no
 * watchers).
 */
void
BTranslatorRoster::Private::_RescanChanged()
{
	while (!fRescanEntries.empty()) {
		EntryRefSet::iterator iterator = fRescanEntries.begin();
		int32 count;
		CreateTranslators(*iterator, count);

		fRescanEntries.erase(iterator);
	}
}


/**
 * @brief Checks whether any of the provided formats match the given type/MIME hints.
 *
 * A MIME hint without a '/' is treated as a super-type prefix match.
 *
 * @param formats The array of translation_format entries to check.
 * @param formatsCount Number of entries in \a formats.
 * @param hintType Type constant to match, or 0 to skip type matching.
 * @param hintMIME MIME string to match, or NULL to skip MIME matching.
 * @return Pointer to the first matching format, or NULL if none matched.
 */
const translation_format*
BTranslatorRoster::Private::_CheckHints(const translation_format* formats,
	int32 formatsCount, uint32 hintType, const char* hintMIME)
{
	if (formats == NULL || formatsCount <= 0 || (!hintType && hintMIME == NULL))
		return NULL;

	// The provided MIME type hint may be a super type
	int32 super = 0;
	if (hintMIME && !strchr(hintMIME, '/'))
		super = strlen(hintMIME);

	// scan for suitable format
	for (int32 i = 0; i < formatsCount && formats[i].type; i++) {
		if (formats[i].type == hintType
			|| (hintMIME
				&& ((super && !strncmp(formats[i].MIME, hintMIME, super))
					|| !strcmp(formats[i].MIME, hintMIME))))
			return &formats[i];
	}

	return NULL;
}


/**
 * @brief Looks up a translator_item by numeric ID.
 * @param id The translator_id to find.
 * @return Pointer to the translator_item, or NULL if not found.
 */
const translator_item*
BTranslatorRoster::Private::_FindTranslator(translator_id id) const
{
	TranslatorMap::const_iterator iterator = fTranslators.find(id);
	if (iterator == fTranslators.end())
		return NULL;

	return &iterator->second;
}


/**
 * @brief Looks up a translator_item by add-on filename.
 * @param name The filename (not full path) of the add-on.
 * @return Pointer to the translator_item, or NULL if not found.
 */
const translator_item*
BTranslatorRoster::Private::_FindTranslator(const char* name) const
{
	if (name == NULL)
		return NULL;

	TranslatorMap::const_iterator iterator = fTranslators.begin();

	while (iterator != fTranslators.end()) {
		const translator_item& item = iterator->second;
		if (item.ref.name != NULL && !strcmp(item.ref.name, name))
			return &item;

		iterator++;
	}

	return NULL;
}


/**
 * @brief Looks up a translator_item by entry_ref.
 * @param ref The entry_ref to match against.
 * @return Pointer to the translator_item, or NULL if not found.
 */
const translator_item*
BTranslatorRoster::Private::_FindTranslator(entry_ref& ref) const
{
	if (ref.name == NULL)
		return NULL;

	TranslatorMap::const_iterator iterator = fTranslators.begin();

	while (iterator != fTranslators.end()) {
		const translator_item& item = iterator->second;
		if (item.ref == ref)
			return &item;

		iterator++;
	}

	return NULL;
}


/**
 * @brief Looks up a translator_item by node_ref (device + inode).
 * @param nodeRef The node_ref to match against.
 * @return Mutable pointer to the translator_item, or NULL if not found.
 */
translator_item*
BTranslatorRoster::Private::_FindTranslator(node_ref& nodeRef)
{
	if (nodeRef.device < 0)
		return NULL;

	TranslatorMap::iterator iterator = fTranslators.begin();

	while (iterator != fTranslators.end()) {
		translator_item& item = iterator->second;
		if (item.ref.device == nodeRef.device
			&& item.node == nodeRef.node)
			return &item;

		iterator++;
	}

	return NULL;
}


/**
 * @brief Compares the directory priorities of two translator entry_refs.
 *
 * Priority is determined by the order directories were added to the roster;
 * the first-added directory has the highest priority.
 *
 * @param a The first entry_ref.
 * @param b The second entry_ref.
 * @return -1 if a's directory has higher priority, 1 if b's does, 0 if equal.
 */
int32
BTranslatorRoster::Private::_CompareTranslatorDirectoryPriority(
	const entry_ref& a, const entry_ref& b) const
{
	// priority is determined by the order in the list

	node_ref nodeRefA;
	nodeRefA.device = a.device;
	nodeRefA.node = a.directory;

	node_ref nodeRefB;
	nodeRefB.device = b.device;
	nodeRefB.node = b.directory;

	NodeRefList::const_iterator iterator = fDirectories.begin();

	while (iterator != fDirectories.end()) {
		if (*iterator == nodeRefA)
			return -1;
		if (*iterator == nodeRefB)
			return 1;

		iterator++;
	}

	return 0;
}


/**
 * @brief Returns whether the given node_ref corresponds to a monitored directory.
 * @param nodeRef The node_ref to check.
 * @return \c true if the directory is in the watch list.
 */
bool
BTranslatorRoster::Private::_IsKnownDirectory(const node_ref& nodeRef) const
{
	NodeRefList::const_iterator iterator = fDirectories.begin();

	while (iterator != fDirectories.end()) {
		if (*iterator == nodeRef)
			return true;

		iterator++;
	}

	return false;
}


/**
 * @brief Removes all translators matching the given node_ref and/or entry_ref.
 *
 * Releases each matched translator, records its ID in a B_TRANSLATOR_REMOVED
 * message, erases it from the map, and notifies all registered listeners.
 *
 * @param nodeRef If non-NULL, matches translators by device + inode.
 * @param ref If non-NULL, matches translators by entry_ref.
 */
void
BTranslatorRoster::Private::_RemoveTranslators(const node_ref* nodeRef,
	const entry_ref* ref)
{
	if (ref == NULL && nodeRef == NULL)
		return;

	TranslatorMap::iterator iterator = fTranslators.begin();
	BMessage update(B_TRANSLATOR_REMOVED);

	while (iterator != fTranslators.end()) {
		TranslatorMap::iterator next = iterator;
		next++;

		const translator_item& item = iterator->second;
		if ((ref != NULL && item.ref == *ref)
			|| (nodeRef != NULL && item.ref.device == nodeRef->device
				&& item.node == nodeRef->node)) {
			item.translator->Release();
			update.AddInt32("translator_id", iterator->first);

			fTranslators.erase(iterator);
		}

		iterator = next;
	}

	_NotifyListeners(update);
}


/**
 * @brief Converts a node_ref + name to an entry_ref and calls _EntryAdded(entry_ref).
 * @param nodeRef The directory node containing the new entry.
 * @param name The filename of the new entry.
 */
void
BTranslatorRoster::Private::_EntryAdded(const node_ref& nodeRef,
	const char* name)
{
	entry_ref ref;
	ref.device = nodeRef.device;
	ref.directory = nodeRef.node;
	ref.set_name(name);

	_EntryAdded(ref);
}


/**
 * @brief Handles a newly discovered translator file.
 *
 * In lazy mode, adds the entry to the rescan set for deferred processing.
 * In non-lazy mode, immediately loads the translator and notifies listeners
 * with a B_TRANSLATOR_ADDED message.
 *
 * @param ref The entry_ref of the new translator file.
 */
void
BTranslatorRoster::Private::_EntryAdded(const entry_ref& ref)
{
	BEntry entry;
	if (entry.SetTo(&ref) != B_OK || !entry.IsFile())
		return;

	if (fLazyScanning) {
		fRescanEntries.insert(ref);
		return;
	}

	BMessage update(B_TRANSLATOR_ADDED);
	int32 count = 0;
	CreateTranslators(ref, count, &update);

	_NotifyListeners(update);
}


/**
 * @brief Sends a BMessage to all registered change-notification messengers.
 * @param update The message to broadcast.
 */
void
BTranslatorRoster::Private::_NotifyListeners(BMessage& update) const
{
	MessengerList::const_iterator iterator = fMessengers.begin();

	while (iterator != fMessengers.end()) {
		(*iterator).SendMessage(&update);
		iterator++;
	}
}


//	#pragma mark - BTranslatorReleaseDelegate


/**
 * @brief Constructs a BTranslatorReleaseDelegate wrapping the given translator.
 * @param translator The translator to wrap; it must already be Acquire()'d.
 */
BTranslatorReleaseDelegate::BTranslatorReleaseDelegate(BTranslator* translator)
	:
	fUnderlying(translator)
{
}


/**
 * @brief Releases the underlying translator and destroys this delegate.
 *
 * May only be called once; the delegate deletes itself after releasing.
 */
void
BTranslatorReleaseDelegate::Release()
{
	fUnderlying->Release();
	// ReleaseDelegate is only allowed to release a translator once.
	delete this;
}


//	#pragma mark - BTranslatorRoster


/** @brief Constructs an empty BTranslatorRoster with no translators loaded. */
BTranslatorRoster::BTranslatorRoster()
{
	_Initialize();
}


/**
 * @brief Constructs a BTranslatorRoster from an archived set of translator paths.
 *
 * Reads "be:translator_path" strings from \a model and loads each add-on.
 *
 * @param model A BMessage previously created by Archive(), or NULL.
 */
BTranslatorRoster::BTranslatorRoster(BMessage* model)
{
	_Initialize();

	if (model) {
		const char* path;
		for (int32 i = 0;
			model->FindString("be:translator_path", i, &path) == B_OK; i++) {
			BEntry entry(path);
			entry_ref ref;
			if (entry.GetRef(&ref) == B_OK) {
				int32 count = 0;
				fPrivate->CreateTranslators(ref, count);
			}
		}
	}
}


/**
 * @brief Destroys the BTranslatorRoster.
 *
 * Clears the default roster pointer if this is the default instance, then
 * deletes the private implementation (which releases all translators and
 * unloads add-on images).
 */
BTranslatorRoster::~BTranslatorRoster()
{
	// If the default BTranslatorRoster is being
	// deleted, set the pointer to the default
	// BTranslatorRoster to NULL
	if (sDefaultRoster == this)
		sDefaultRoster = NULL;

	delete fPrivate;
}


/**
 * @brief Allocates and initializes the private implementation object.
 */
void
BTranslatorRoster::_Initialize()
{
	fPrivate = new BTranslatorRoster::Private();
}


/**
 * @brief Archives the roster's translator paths into \a into for later reconstruction.
 * @param into The BMessage to archive into.
 * @param deep Passed to BArchivable::Archive() (unused here).
 * @return B_OK on success.
 */
status_t
BTranslatorRoster::Archive(BMessage* into, bool deep) const
{
	status_t status = BArchivable::Archive(into, deep);
	if (status != B_OK)
		return status;

	return fPrivate->StoreTranslators(*into);
}


/**
 * @brief Instantiates a BTranslatorRoster from an archived BMessage.
 * @param from A BMessage previously created by Archive().
 * @return A new BTranslatorRoster, or NULL if \a from is invalid.
 */
BArchivable*
BTranslatorRoster::Instantiate(BMessage* from)
{
	if (!from || !validate_instantiation(from, "BTranslatorRoster"))
		return NULL;

	return new BTranslatorRoster(from);
}


/**
 * @brief Returns the default shared BTranslatorRoster, creating it if needed.
 *
 * Thread-safe via atomic spin: if two threads race to create the default
 * roster, one creates it while the other spins until fDefaultRoster is set.
 *
 * @return Pointer to the default BTranslatorRoster (never NULL after first call).
 */
BTranslatorRoster*
BTranslatorRoster::Default()
{
	static int32 lock = 0;

	if (sDefaultRoster != NULL)
		return sDefaultRoster;

	if (atomic_add(&lock, 1) != 0) {
		// Just wait for the default translator to be instantiated
		while (sDefaultRoster == NULL)
			snooze(10000);

		atomic_add(&lock, -1);
		return sDefaultRoster;
	}

	// If the default translators have not been loaded,
	// create a new BTranslatorRoster for them, and load them.
	if (sDefaultRoster == NULL) {
		BTranslatorRoster* roster = new BTranslatorRoster();
		roster->AddTranslators(NULL);

		sDefaultRoster = roster;
			// this will unlock any other threads waiting for
			// the default roster to become available
	}

	atomic_add(&lock, -1);
	return sDefaultRoster;
}


/**
 * @brief Loads translators from a colon-separated list of directory paths.
 *
 * If \a path is NULL, the TRANSLATORS environment variable is consulted; if
 * that is also unset, the standard system and user translator directories are
 * used.
 *
 * @param path Colon-separated directory paths, or NULL for defaults.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BTranslatorRoster::AddTranslators(const char* path)
{
	if (path == NULL)
		path = getenv("TRANSLATORS");
	if (path == NULL) {
		fPrivate->AddDefaultPaths();
		return B_OK;
	}

	return fPrivate->AddPaths(path);
}


/**
 * @brief Adds a single BTranslator object to the roster.
 *
 * The roster Acquire()'s the translator; it will be Release()'d when the
 * roster is destroyed.
 *
 * @param translator The BTranslator to add.
 * @return B_OK on success, or B_BAD_VALUE if \a translator is NULL.
 */
status_t
BTranslatorRoster::AddTranslator(BTranslator* translator)
{
	if (!translator)
		return B_BAD_VALUE;

	return fPrivate->AddTranslator(translator);
}


/**
 * @brief Tests whether the file at \a ref is a valid translator add-on.
 *
 * Loads the add-on temporarily and checks for either make_nth_translator()
 * or the required C-style symbols, then unloads it.
 *
 * @param ref Entry reference for the file to test.
 * @return \c true if the file is a recognized translator add-on.
 */
bool
BTranslatorRoster::IsTranslator(entry_ref* ref)
{
	if (ref == NULL)
		return false;

	BPath path(ref);
	image_id image = load_add_on(path.Path());
	if (image < B_OK)
		return false;

	// Function pointer used to create post R4.5 style translators
	BTranslator* (*makeNthTranslator)(int32 n, image_id you, uint32 flags, ...);

	status_t status = get_image_symbol(image, "make_nth_translator",
		B_SYMBOL_TYPE_TEXT, (void**)&makeNthTranslator);
	if (status < B_OK) {
		// If this is a translator add-on, it is in the C format
		translator_data translatorData;
		status = fPrivate->GetTranslatorData(image, translatorData);
	}

	unload_add_on(image);
	return status == B_OK;
}


/**
 * @brief Identifies the single best translator for the data in \a source.
 *
 * @param source The data stream to identify.
 * @param ioExtension Optional extension message; updated on success.
 * @param _info Set to the winning translator_info on success.
 * @param hintType Type hint (0 = unknown).
 * @param hintMIME MIME hint (NULL = unknown).
 * @param wantType Desired output type (0 = any).
 * @return B_OK on success, B_NO_TRANSLATOR if no match, B_BAD_VALUE if
 *     \a source or \a _info is NULL.
 */
status_t
BTranslatorRoster::Identify(BPositionIO* source, BMessage* ioExtension,
	translator_info* _info, uint32 hintType, const char* hintMIME,
	uint32 wantType)
{
	if (source == NULL || _info == NULL)
		return B_BAD_VALUE;

	return fPrivate->Identify(source, ioExtension, hintType, hintMIME, wantType,
		_info);
}


/**
 * @brief Returns all translators capable of handling \a source.
 *
 * The caller must delete[] the returned \a _info array.
 *
 * @param source The data stream to identify.
 * @param ioExtension Optional extension message.
 * @param _info Set to a caller-owned array of matching translator_info.
 * @param _numInfo Set to the number of entries in \a _info.
 * @param hintType Type hint (0 = unknown).
 * @param hintMIME MIME hint (NULL = unknown).
 * @param wantType Desired output type (0 = any).
 * @return B_OK on success, B_BAD_VALUE if required parameters are NULL.
 */
status_t
BTranslatorRoster::GetTranslators(BPositionIO* source, BMessage* ioExtension,
	translator_info** _info, int32* _numInfo, uint32 hintType,
	const char* hintMIME, uint32 wantType)
{
	if (source == NULL || _info == NULL || _numInfo == NULL)
		return B_BAD_VALUE;

	return fPrivate->GetTranslators(source, ioExtension, hintType, hintMIME,
		wantType, _info, _numInfo);
}


/**
 * @brief Returns an array of all registered translator IDs.
 *
 * The caller must delete[] the returned \a _ids array.
 *
 * @param _ids Set to a caller-owned array of translator_id values.
 * @param _count Set to the number of entries in \a _ids.
 * @return B_OK on success, B_BAD_VALUE if either parameter is NULL.
 */
status_t
BTranslatorRoster::GetAllTranslators(translator_id** _ids, int32* _count)
{
	if (_ids == NULL || _count == NULL)
		return B_BAD_VALUE;

	return fPrivate->GetAllTranslators(_ids, _count);
}


/**
 * @brief Returns metadata for the translator identified by \a id.
 *
 * The returned string pointers are owned by the translator and must not be freed.
 *
 * @param id The translator to query.
 * @param _name Set to the translator's name string, if non-NULL.
 * @param _info Set to the translator's description string, if non-NULL.
 * @param _version Set to the translator's version integer, if non-NULL.
 * @return B_OK on success, B_BAD_VALUE if all output pointers are NULL, or
 *     B_NO_TRANSLATOR if \a id is not recognized.
 */
status_t
BTranslatorRoster::GetTranslatorInfo(translator_id id, const char** _name,
	const char** _info, int32* _version)
{
	if (_name == NULL && _info == NULL && _version == NULL)
		return B_BAD_VALUE;

	BAutolock locker(fPrivate);

	BTranslator* translator = fPrivate->FindTranslator(id);
	if (translator == NULL)
		return B_NO_TRANSLATOR;

	if (_name)
		*_name = translator->TranslatorName();
	if (_info)
		*_info = translator->TranslatorInfo();
	if (_version)
		*_version = translator->TranslatorVersion();

	return B_OK;
}


/**
 * @brief Returns the input formats for the translator identified by \a id.
 *
 * The returned array is owned by the translator and must not be freed.
 *
 * @param id The translator to query.
 * @param _formats Set to the array of input translation_format entries.
 * @param _numFormats Set to the number of entries in \a _formats.
 * @return B_OK on success, B_BAD_VALUE if parameters are NULL, or
 *     B_NO_TRANSLATOR if \a id is not recognized.
 */
status_t
BTranslatorRoster::GetInputFormats(translator_id id,
	const translation_format** _formats, int32* _numFormats)
{
	if (_formats == NULL || _numFormats == NULL)
		return B_BAD_VALUE;

	BAutolock locker(fPrivate);

	BTranslator* translator = fPrivate->FindTranslator(id);
	if (translator == NULL)
		return B_NO_TRANSLATOR;

	*_formats = translator->InputFormats(_numFormats);
	return B_OK;
}


/**
 * @brief Returns the output formats for the translator identified by \a id.
 *
 * The returned array is owned by the translator and must not be freed.
 *
 * @param id The translator to query.
 * @param _formats Set to the array of output translation_format entries.
 * @param _numFormats Set to the number of entries in \a _formats.
 * @return B_OK on success, B_BAD_VALUE if parameters are NULL, or
 *     B_NO_TRANSLATOR if \a id is not recognized.
 */
status_t
BTranslatorRoster::GetOutputFormats(translator_id id,
	const translation_format** _formats, int32* _numFormats)
{
	if (_formats == NULL || _numFormats == NULL)
		return B_BAD_VALUE;

	BAutolock locker(fPrivate);

	BTranslator* translator = fPrivate->FindTranslator(id);
	if (translator == NULL)
		return B_NO_TRANSLATOR;

	*_formats = translator->OutputFormats(_numFormats);
	return B_OK;
}


/**
 * @brief Translates data from \a source to \a destination.
 *
 * If \a info is NULL, the source is automatically identified first. The
 * translator is Acquire()'d for the duration of the call so that it cannot
 * be unloaded mid-translation.
 *
 * @param source The data stream to translate.
 * @param info Pre-identified translator_info, or NULL to auto-identify.
 * @param ioExtension Optional extension message with translator parameters.
 * @param destination The stream to write translated output into.
 * @param wantOutType The desired output format type.
 * @param hintType Type hint for auto-identification (0 = unknown).
 * @param hintMIME MIME hint for auto-identification (NULL = unknown).
 * @return B_OK on success, B_NO_TRANSLATOR if no suitable translator was
 *     found, B_BAD_VALUE if required parameters are NULL, or an error from
 *     the translator or the I/O streams.
 */
status_t
BTranslatorRoster::Translate(BPositionIO* source, const translator_info* info,
	BMessage* ioExtension, BPositionIO* destination, uint32 wantOutType,
	uint32 hintType, const char* hintMIME)
{
	if (source == NULL || destination == NULL)
		return B_BAD_VALUE;

	translator_info infoBuffer;

	if (info == NULL) {
		// look for a suitable translator
		status_t status = fPrivate->Identify(source, ioExtension, hintType,
			hintMIME, wantOutType, &infoBuffer);
		if (status < B_OK)
			return status;

		info = &infoBuffer;
	}

	if (!fPrivate->Lock())
		return B_ERROR;

	BTranslator* translator = fPrivate->FindTranslator(info->translator);
	if (translator != NULL) {
		translator->Acquire();
			// make sure this translator is not removed while we're playing with
			// it; translating shouldn't be serialized!
	}

	fPrivate->Unlock();

	if (translator == NULL)
		return B_NO_TRANSLATOR;

	status_t status = B_OK;
	off_t pos = source->Seek(0, SEEK_SET);
	if (pos != 0)
		status = pos < 0 ? (status_t)pos : B_IO_ERROR;
	if (status == B_OK) {
		status = translator->Translate(source, info, ioExtension, wantOutType,
			destination);
	}
	translator->Release();

	return status;
}


/**
 * @brief Translates data from \a source to \a destination using a specific translator.
 *
 * Unlike the other Translate() overload, this one selects the translator by
 * \a id rather than auto-detecting from the source data. The translator is
 * still identified against the source before translating to populate a
 * translator_info.
 *
 * @param id The translator_id of the translator to use.
 * @param source The data stream to translate.
 * @param ioExtension Optional extension message with translator parameters.
 * @param destination The stream to write translated output into.
 * @param wantOutType The desired output format type.
 * @return B_OK on success, B_NO_TRANSLATOR if \a id is not found, or an
 *     error from the I/O streams or translator.
 */
status_t
BTranslatorRoster::Translate(translator_id id, BPositionIO* source,
	BMessage* ioExtension, BPositionIO* destination, uint32 wantOutType)
{
	if (source == NULL || destination == NULL)
		return B_BAD_VALUE;

	if (!fPrivate->Lock())
		return B_ERROR;

	BTranslator* translator = fPrivate->FindTranslator(id);
	if (translator != NULL) {
		translator->Acquire();
			// make sure this translator is not removed while we're playing with
			// it; translating shouldn't be serialized!
	}

	fPrivate->Unlock();

	if (translator == NULL)
		return B_NO_TRANSLATOR;

	status_t status;
	off_t pos = source->Seek(0, SEEK_SET);
	if (pos == 0) {
		translator_info info;
		status = translator->Identify(source, NULL, ioExtension, &info,
			wantOutType);
		if (status >= B_OK) {
			off_t pos = source->Seek(0, SEEK_SET);
			if (pos != 0)
				status = pos < 0 ? (status_t)pos : B_IO_ERROR;
			else {
				status = translator->Translate(source, &info, ioExtension,
					wantOutType, destination);
			}
		}
	} else
		status = pos < 0 ? (status_t)pos : B_IO_ERROR;
	translator->Release();

	return status;
}


/**
 * @brief Creates a configuration BView for the translator identified by \a id.
 *
 * Not all translators support this; those that don't return B_ERROR.
 *
 * @param id The translator to create a view for.
 * @param ioExtension Optional extension message.
 * @param _view Set to the newly created BView on success.
 * @param _extent Set to the preferred bounds for the view.
 * @return B_OK on success, B_BAD_VALUE if \a _view or \a _extent is NULL, or
 *     B_NO_TRANSLATOR if \a id is not recognized.
 */
status_t
BTranslatorRoster::MakeConfigurationView(translator_id id,
	BMessage* ioExtension, BView** _view, BRect* _extent)
{
	if (_view == NULL || _extent == NULL)
		return B_BAD_VALUE;

	BAutolock locker(fPrivate);

	BTranslator* translator = fPrivate->FindTranslator(id);
	if (translator == NULL)
		return B_NO_TRANSLATOR;

	return translator->MakeConfigurationView(ioExtension, _view, _extent);
}


/**
 * @brief Acquires the translator identified by \a id and returns a release delegate.
 *
 * The caller is responsible for calling Release() on the returned delegate
 * when done. This prevents the translator from being unloaded while in use.
 *
 * @param id The translator to acquire.
 * @return A new BTranslatorReleaseDelegate, or NULL if \a id is not found.
 */
BTranslatorReleaseDelegate*
BTranslatorRoster::AcquireTranslator(int32 id)
{
	BAutolock locker(fPrivate);

	BTranslator* translator = fPrivate->FindTranslator(id);
	if (translator == NULL)
		return NULL;

	translator->Acquire();
	return new BTranslatorReleaseDelegate(translator);
}


/**
 * @brief Retrieves the current configuration settings for a translator.
 *
 * Populates \a ioExtension with the translator's current settings as
 * name/value pairs. Not all translators support this.
 *
 * @param id The translator to query.
 * @param ioExtension BMessage to receive the settings.
 * @return B_OK on success, B_BAD_VALUE if \a ioExtension is NULL, or
 *     B_NO_TRANSLATOR if \a id is not recognized.
 */
status_t
BTranslatorRoster::GetConfigurationMessage(translator_id id,
	BMessage* ioExtension)
{
	if (!ioExtension)
		return B_BAD_VALUE;

	BAutolock locker(fPrivate);

	BTranslator* translator = fPrivate->FindTranslator(id);
	if (translator == NULL)
		return B_NO_TRANSLATOR;

	return translator->GetConfigurationMessage(ioExtension);
}


/**
 * @brief Returns the entry_ref for the add-on file of a disk-based translator.
 *
 * @param id The translator to query.
 * @param ref Set to the entry_ref on success.
 * @return B_OK on success, B_BAD_VALUE if \a ref is NULL, B_NO_TRANSLATOR if
 *     \a id is not recognized, or B_ERROR if the translator is not disk-based.
 */
status_t
BTranslatorRoster::GetRefFor(translator_id id, entry_ref* ref)
{
	if (ref == NULL)
		return B_BAD_VALUE;

	return fPrivate->GetRefFor(id, *ref);
}


/**
 * @brief Registers a messenger to receive B_TRANSLATOR_ADDED and
 *     B_TRANSLATOR_REMOVED notifications.
 * @param target The messenger to register.
 * @return B_OK on success, or B_NO_MEMORY.
 */
status_t
BTranslatorRoster::StartWatching(BMessenger target)
{
	return fPrivate->StartWatching(target);
}


/**
 * @brief Unregisters a previously registered change-notification messenger.
 * @param target The messenger to remove.
 * @return B_OK on success, or B_BAD_VALUE if not registered.
 */
status_t
BTranslatorRoster::StopWatching(BMessenger target)
{
	return fPrivate->StopWatching(target);
}


//	#pragma mark - private


/** @brief Private copy constructor (not implemented; copying is not supported). */
BTranslatorRoster::BTranslatorRoster(const BTranslatorRoster &other)
{
}


/** @brief Private assignment operator (not implemented; copying is not supported). */
BTranslatorRoster &
BTranslatorRoster::operator=(const BTranslatorRoster &tr)
{
	return *this;
}


#if __GNUC__ == 2	// gcc 2

/**
 * @brief Returns a version string for the Translation Kit (gcc 2 only).
 * @param outCurVersion Set to the current version constant.
 * @param outMinVersion Set to the minimum supported version constant.
 * @param inAppVersion Unused; reserved for future ABI checking.
 * @return A static string describing the Translation Kit version.
 */
/*static*/ const char*
BTranslatorRoster::Version(int32* outCurVersion, int32* outMinVersion,
	int32 inAppVersion)
{
	if (!outCurVersion || !outMinVersion)
		return "";

	static char vString[50];
	static char vDate[] = __DATE__;
	if (!vString[0]) {
		sprintf(vString, "Translation Kit v%d.%d.%d %s\n",
			int(B_TRANSLATION_MAJOR_VERSION(B_TRANSLATION_CURRENT_VERSION)),
			int(B_TRANSLATION_MINOR_VERSION(B_TRANSLATION_CURRENT_VERSION)),
			int(B_TRANSLATION_REVISION_VERSION(B_TRANSLATION_CURRENT_VERSION)),
			vDate);
	}
	*outCurVersion = B_TRANSLATION_CURRENT_VERSION;
	*outMinVersion = B_TRANSLATION_MIN_VERSION;
	return vString;
}

#endif	// gcc 2


//	#pragma mark - FBC protection


void BTranslatorRoster::ReservedTranslatorRoster1() {}
void BTranslatorRoster::ReservedTranslatorRoster2() {}
void BTranslatorRoster::ReservedTranslatorRoster3() {}
void BTranslatorRoster::ReservedTranslatorRoster4() {}
void BTranslatorRoster::ReservedTranslatorRoster5() {}
void BTranslatorRoster::ReservedTranslatorRoster6() {}
