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
 * Incorporates work from Haiku, Inc. covered by:
 * Copyright 2009, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/** @file MediaFiles.h
 *  @brief Defines BMediaFiles, which manages the system media-file registry.
 */

#ifndef _MEDIA_FILES_H
#define _MEDIA_FILES_H


#include <MediaDefs.h>
#include <List.h>
#include <String.h>

struct entry_ref;


/** @brief Provides access to the system registry that maps media type/item names to files.
 *
 *  BMediaFiles lets applications enumerate and modify the associations between
 *  named media categories (e.g. "beep") and the files that play for them.
 */
class BMediaFiles {
public:

	/** @brief Default constructor. */
								BMediaFiles();
	virtual						~BMediaFiles();

	/** @brief Resets the type iterator to the beginning of the type list.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			RewindTypes();

	/** @brief Advances the type iterator and returns the next media type name.
	 *  @param _type On return, the next type name string.
	 *  @return B_OK while more types exist, B_BAD_INDEX when done.
	 */
	virtual	status_t			GetNextType(BString* _type);

	/** @brief Resets the item iterator for the given type.
	 *  @param type The media type name whose items will be iterated.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			RewindRefs(const char* type);

	/** @brief Advances the item iterator and returns the next item name and optional ref.
	 *  @param _type On return, the item name string.
	 *  @param _ref If non-NULL, receives the associated entry_ref.
	 *  @return B_OK while more items exist, B_BAD_INDEX when done.
	 */
	virtual	status_t			GetNextRef(BString* _type,
									entry_ref* _ref = NULL);

	/** @brief Returns the entry_ref associated with a specific type/item pair.
	 *  @param type The media type category.
	 *  @param item The item name within that category.
	 *  @param _ref On return, the associated entry_ref.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			GetRefFor(const char* type, const char* item,
									entry_ref* _ref);

	/** @brief Associates a file with a specific type/item pair.
	 *  @param type The media type category.
	 *  @param item The item name within that category.
	 *  @param ref The entry_ref to associate.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			SetRefFor(const char* type, const char* item,
									const entry_ref& ref);

	/** @brief Reads the playback gain for a specific type/item pair.
	 *  @param type The media type category.
	 *  @param item The item name.
	 *  @param _gain On return, the gain value (0.0-1.0).
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetAudioGainFor(const char* type,
									const char* item, float* _gain);

	/** @brief Sets the playback gain for a specific type/item pair.
	 *  @param type The media type category.
	 *  @param item The item name.
	 *  @param gain The new gain value (0.0-1.0).
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetAudioGainFor(const char* type,
									const char* item, float gain);

	/** @brief Removes the file association for a specific type/item/ref combination.
	 *  @param type The media type category.
	 *  @param item The item name.
	 *  @param ref The entry_ref to disassociate.
	 *  @return B_OK on success, or an error code.
	 */
	// TODO: Rename this to "ClearRefFor" when breaking BC.
	virtual	status_t			RemoveRefFor(const char* type,
									const char* item, const entry_ref& ref);

	/** @brief Removes an item entirely from the registry.
	 *  @param type The media type category.
	 *  @param item The item name to remove.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			RemoveItem(const char* type, const char* item);

	/** @brief Well-known type name for system sound associations. */
	static	const char			B_SOUNDS[];

	// TODO: Needs Perform() for FBC reasons!

private:
	// FBC padding

			status_t			_Reserved_MediaFiles_0(void*, ...);
	virtual	status_t			_Reserved_MediaFiles_1(void*, ...);
	virtual	status_t			_Reserved_MediaFiles_2(void*, ...);
	virtual	status_t			_Reserved_MediaFiles_3(void*, ...);
	virtual	status_t			_Reserved_MediaFiles_4(void*, ...);
	virtual	status_t			_Reserved_MediaFiles_5(void*, ...);
	virtual	status_t			_Reserved_MediaFiles_6(void*, ...);
	virtual	status_t			_Reserved_MediaFiles_7(void*, ...);

			void				_ClearTypes();
			void				_ClearItems();

private:
			BList				fTypes;
			int					fTypeIndex;
			BString				fCurrentType;
			BList				fItems;
			int					fItemIndex;
};

#endif // _MEDIA_FILES_H

