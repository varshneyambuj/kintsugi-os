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
 * This file incorporates work from the Haiku project:
 *   Copyright 2006-2015, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 * Author:
 *   Axel Dörfler, axeld@pinc-software.de
 *   Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file TranslatorRosterPrivate.h
 *  @brief Internal implementation of BTranslatorRoster::Private: translator loading, lookup, and watching. */

#ifndef TRANSLATOR_ROSTER_PRIVATE_H
#define TRANSLATOR_ROSTER_PRIVATE_H

#include <map>
#include <set>
#include <vector>

#include <Entry.h>
#include <Handler.h>
#include <Locker.h>
#include <Messenger.h>
#include <TranslatorRoster.h>


struct translator_data;


/** @brief Metadata record associating a loaded BTranslator with its image and filesystem entry. */
struct translator_item {
	BTranslator*	translator;
	entry_ref		ref;
	ino_t			node;
	image_id		image;
};

typedef std::map<translator_id, translator_item> TranslatorMap;
typedef std::vector<BMessenger> MessengerList;
typedef std::vector<node_ref> NodeRefList;
typedef std::set<entry_ref> EntryRefSet;
typedef std::map<image_id, int32> ImageMap;
typedef std::map<BTranslator*, image_id> TranslatorImageMap;


/** @brief Private implementation class for BTranslatorRoster.
 *
 *  Handles add-on loading, translator lookup, identification, translation,
 *  and filesystem watching for dynamically added/removed translators. */
class BTranslatorRoster::Private : public BHandler, public BLocker {
public:
								Private();
	virtual						~Private();

	/** @brief Handles node-watcher and application messages for dynamic roster updates.
	 *  @param message Incoming BMessage. */
	virtual	void				MessageReceived(BMessage* message);

	/** @brief Adds the default system translator directories to the search path. */
			void				AddDefaultPaths();

	/** @brief Loads translators from a colon-separated list of directory paths.
	 *  @param paths Colon-separated directory paths.
	 *  @return B_OK on success, or an error code. */
			status_t			AddPaths(const char* paths);

	/** @brief Loads translators from a single directory path.
	 *  @param path Directory to scan.
	 *  @param _added Optionally set to the number of newly loaded translators.
	 *  @return B_OK on success, or an error code. */
			status_t			AddPath(const char* path, int32* _added = NULL);

	/** @brief Registers a BTranslator instance with optional image and filesystem metadata.
	 *  @param translator Translator to register.
	 *  @param image image_id of the hosting add-on, or -1 if built-in.
	 *  @param ref Optional filesystem entry for the add-on file.
	 *  @param node Optional inode number for node watching.
	 *  @return B_OK on success, or an error code. */
			status_t			AddTranslator(BTranslator* translator,
									image_id image = -1,
									const entry_ref* ref = NULL,
									ino_t node = 0);

	/** @brief Removes all translators loaded from the given filesystem entry.
	 *  @param ref Entry reference identifying the add-on file to remove. */
			void				RemoveTranslators(entry_ref& ref);

	/** @brief Looks up a translator by ID without acquiring a reference.
	 *  @param id Translator ID.
	 *  @return Pointer to the BTranslator, or NULL if not found. */
			BTranslator*		FindTranslator(translator_id id);

	/** @brief Archives all loaded translator references into a BMessage.
	 *  @param archive Destination BMessage.
	 *  @return B_OK on success, or an error code. */
			status_t			StoreTranslators(BMessage& archive);

	/** @brief Identifies the best translator for a source stream.
	 *  @param source Input stream.
	 *  @param ioExtension Optional extension message.
	 *  @param hintType Optional input type-code hint.
	 *  @param hintMIME Optional input MIME-type hint.
	 *  @param wantType Required output type, or 0 for any.
	 *  @param _info Filled with the best match.
	 *  @return B_OK on success, or an error code. */
			status_t			Identify(BPositionIO* source,
									BMessage* ioExtension, uint32 hintType,
									const char* hintMIME, uint32 wantType,
									translator_info* _info);

	/** @brief Finds all translators able to handle a source stream.
	 *  @param source Input stream.
	 *  @param ioExtension Optional extension message.
	 *  @param hintType Optional input type-code hint.
	 *  @param hintMIME Optional input MIME-type hint.
	 *  @param wantType Required output type, or 0 for any.
	 *  @param _info Set to a newly allocated array of matches (caller must free[]).
	 *  @param _numInfo Set to the number of matches.
	 *  @return B_OK on success, or an error code. */
			status_t			GetTranslators(BPositionIO* source,
									BMessage* ioExtension, uint32 hintType,
									const char* hintMIME, uint32 wantType,
									translator_info** _info, int32* _numInfo);

	/** @brief Returns IDs for all currently loaded translators.
	 *  @param _ids Set to a newly allocated array of IDs (caller must free[]).
	 *  @param _count Set to the number of IDs.
	 *  @return B_OK on success, or an error code. */
			status_t			GetAllTranslators(translator_id** _ids,
									int32* _count);

	/** @brief Returns the filesystem entry_ref for a translator's add-on file.
	 *  @param id Translator to query.
	 *  @param ref Filled with the entry_ref.
	 *  @return B_OK on success, or an error code. */
			status_t			GetRefFor(translator_id id, entry_ref& ref);

	/** @brief Returns whether the roster is currently attached to a BLooper.
	 *  @return true if active. */
			bool				IsActive() const { return Looper(); }

	/** @brief Loads and registers all translators found in an add-on file.
	 *  @param ref Filesystem entry for the add-on.
	 *  @param count Incremented by the number of translators added.
	 *  @param update Optional BMessage to notify watchers of the changes.
	 *  @return B_OK on success, or an error code. */
			status_t			CreateTranslators(const entry_ref& ref,
									int32& count, BMessage* update = NULL);

	/** @brief Reads the translator_data export from a loaded add-on image.
	 *  @param image image_id of the add-on.
	 *  @param data Filled with the function-pointer table.
	 *  @return B_OK on success, or an error code. */
			status_t			GetTranslatorData(image_id image,
									translator_data& data);

	/** @brief Registers a BMessenger to receive roster change notifications.
	 *  @param target Messenger to add.
	 *  @return B_OK on success, or an error code. */
			status_t			StartWatching(BMessenger target);

	/** @brief Unregisters a BMessenger from roster change notifications.
	 *  @param target Messenger to remove.
	 *  @return B_OK on success, or an error code. */
			status_t			StopWatching(BMessenger target);

private:
	static	int					_CompareSupport(const void* _a, const void* _b);

			void				_RescanChanged();

			const translation_format* _CheckHints(
									const translation_format* formats,
									int32 formatsCount, uint32 hintType,
									const char* hintMIME);

			const translator_item* _FindTranslator(translator_id id) const;
			const translator_item* _FindTranslator(const char* name) const;
			const translator_item* _FindTranslator(entry_ref& ref) const;
			translator_item*	_FindTranslator(node_ref& nodeRef);

			int32				_CompareTranslatorDirectoryPriority(
									const entry_ref& a,
									const entry_ref& b) const;
			bool				_IsKnownDirectory(const node_ref& nodeRef)
									const;

			void				_RemoveTranslators(const node_ref* nodeRef,
									const entry_ref* ref = NULL);
			void				_EntryAdded(const node_ref& nodeRef,
									const char* name);
			void				_EntryAdded(const entry_ref& ref);
			void				_NotifyListeners(BMessage& update) const;
			void				_TranslatorDeleted(translator_id id,
									BTranslator *self);

			NodeRefList			fDirectories;
			TranslatorMap		fTranslators;
			MessengerList		fMessengers;
			EntryRefSet			fRescanEntries;
			ImageMap			fKnownImages;
			TranslatorImageMap	fImageOrigins;
			const char*			fABISubDirectory;
			int32				fNextID;
			bool				fLazyScanning;
			bool				fSafeMode;
};


#endif	// TRANSLATOR_ROSTER_PRIVATE_H
