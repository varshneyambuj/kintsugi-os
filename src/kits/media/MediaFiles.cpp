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
 *   Copyright 2002-2009, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marcus Overhagen
 *       Jérôme Duval
 */

/** @file MediaFiles.cpp
 *  @brief Implementation of BMediaFiles for querying and managing system media file associations.
 */


#include <MediaFiles.h>

#include <AppMisc.h>
#include <DataExchange.h>
#include <MediaDebug.h>


const char BMediaFiles::B_SOUNDS[] = "Sounds";


/** @brief Default constructor; initialises the type and item iteration indices. */
BMediaFiles::BMediaFiles()
	:
	fTypeIndex(-1),
	fItemIndex(-1)
{

}


/** @brief Destructor; frees cached type and item lists. */
BMediaFiles::~BMediaFiles()
{
	_ClearTypes();
	_ClearItems();
}


/** @brief Queries the media server for all registered media file types and rewinds iteration.
 *  @return B_OK on success, or an error code from the server query.
 */
status_t
BMediaFiles::RewindTypes()
{
	CALLED();

	_ClearTypes();

	server_get_media_types_request request;
	request.team = BPrivate::current_team();

	server_get_media_types_reply reply;
	status_t status = QueryServer(SERVER_GET_MEDIA_FILE_TYPES, &request,
		sizeof(request), &reply, sizeof(reply));
	if (status != B_OK) {
		ERROR("BMediaFiles::RewindTypes: failed to rewind types: %s\n",
			strerror(status));
		return status;
	}

	const char* types = (const char*)reply.address;
	for (int32 i = 0; i < reply.count; i++)
		fTypes.AddItem(new BString(types + i * B_MEDIA_NAME_LENGTH));

	delete_area(reply.area);

	fTypeIndex = 0;
	return B_OK;
}


/** @brief Returns the next media file type string from the cached list.
 *         Must call RewindTypes() before the first call.
 *  @param _type Pointer to a BString to receive the type name.
 *  @return B_OK on success, B_BAD_INDEX when the list is exhausted.
 */
status_t
BMediaFiles::GetNextType(BString* _type)
{
	CALLED();
	if (fTypeIndex < 0 || fTypeIndex >= fTypes.CountItems()) {
		_ClearTypes();
		fTypeIndex = -1;
		return B_BAD_INDEX;
	}

	*_type = *(BString*)fTypes.ItemAt(fTypeIndex);
	fTypeIndex++;

	return B_OK;
}


/** @brief Queries the media server for all items registered under the given type and rewinds iteration.
 *  @param type The type string to enumerate items for.
 *  @return B_OK on success, or an error code from the server query.
 */
status_t
BMediaFiles::RewindRefs(const char* type)
{
	CALLED();

	_ClearItems();

	TRACE("BMediaFiles::RewindRefs: sending SERVER_GET_MEDIA_FILE_ITEMS for "
		"type %s\n", type);

	server_get_media_items_request request;
	request.team = BPrivate::current_team();
	strlcpy(request.type, type, B_MEDIA_NAME_LENGTH);

	server_get_media_items_reply reply;
	status_t status = QueryServer(SERVER_GET_MEDIA_FILE_ITEMS, &request,
		sizeof(request), &reply, sizeof(reply));
	if (status != B_OK) {
		ERROR("BMediaFiles::RewindRefs: failed to rewind refs: %s\n",
			strerror(status));
		return status;
	}

	const char* items = (const char*)reply.address;
	for (int32 i = 0; i < reply.count; i++) {
		fItems.AddItem(new BString(items + i * B_MEDIA_NAME_LENGTH,
			B_MEDIA_NAME_LENGTH));
	}

	delete_area(reply.area);

	fCurrentType = type;
	fItemIndex = 0;
	return B_OK;
}


/** @brief Returns the next item name and its associated entry_ref under the current type.
 *         Must call RewindRefs() before the first call.
 *  @param _type Output BString to receive the item name.
 *  @param _ref  Output entry_ref to receive the file reference.
 *  @return B_OK on success, B_BAD_INDEX when the list is exhausted.
 */
status_t
BMediaFiles::GetNextRef(BString* _type, entry_ref* _ref)
{
	CALLED();
	if (fItemIndex < 0 || fItemIndex >= fItems.CountItems()) {
		_ClearItems();
		fItemIndex = -1;
		return B_BAD_INDEX;
	}

	*_type = *(BString*)fItems.ItemAt(fItemIndex);
	GetRefFor(fCurrentType.String(), _type->String(), _ref);

	fItemIndex++;
	return B_OK;
}


/** @brief Retrieves the entry_ref for the named item within the given type.
 *  @param type The type string.
 *  @param item The item name.
 *  @param _ref Output pointer to receive the entry_ref.
 *  @return B_OK on success, B_BAD_VALUE if any argument is NULL, or an error code.
 */
status_t
BMediaFiles::GetRefFor(const char* type, const char* item, entry_ref* _ref)
{
	CALLED();

	if (type == NULL || item == NULL || _ref == NULL)
		return B_BAD_VALUE;

	server_get_ref_for_request request;
	strlcpy(request.type, type, B_MEDIA_NAME_LENGTH);
	strlcpy(request.item, item, B_MEDIA_NAME_LENGTH);

	server_get_ref_for_reply reply;
	status_t status = QueryServer(SERVER_GET_REF_FOR, &request, sizeof(request),
		&reply, sizeof(reply));
	if (status != B_OK) {
		ERROR("BMediaFiles::GetRefFor: failed: %s\n", strerror(status));
		return status;
	}

	*_ref = reply.ref;
	return B_OK;
}


/** @brief Retrieves the playback audio gain for the named item within the given type.
 *  @param type  The type string.
 *  @param item  The item name.
 *  @param _gain Output pointer to receive the gain value.
 *  @return B_OK on success, B_BAD_VALUE if any argument is NULL, or an error code.
 */
status_t
BMediaFiles::GetAudioGainFor(const char* type, const char* item, float* _gain)
{
	CALLED();

	if (type == NULL || item == NULL || _gain == NULL)
		return B_BAD_VALUE;

	server_get_item_audio_gain_request request;
	strlcpy(request.type, type, B_MEDIA_NAME_LENGTH);
	strlcpy(request.item, item, B_MEDIA_NAME_LENGTH);

	server_get_item_audio_gain_reply reply;
	status_t status = QueryServer(SERVER_GET_ITEM_AUDIO_GAIN, &request,
		sizeof(request), &reply, sizeof(reply));
	if (status != B_OK) {
		ERROR("BMediaFiles::GetRefFor: failed: %s\n", strerror(status));
		return status;
	}

	*_gain = reply.gain;
	return B_OK;
}


/** @brief Associates an entry_ref with the named item under the given type.
 *  @param type The type string.
 *  @param item The item name.
 *  @param ref  The entry_ref to associate.
 *  @return B_OK on success, or an error code from the server.
 */
status_t
BMediaFiles::SetRefFor(const char* type, const char* item,
	const entry_ref& ref)
{
	CALLED();

	server_set_ref_for_request request;
	strlcpy(request.type, type, B_MEDIA_NAME_LENGTH);
	strlcpy(request.item, item, B_MEDIA_NAME_LENGTH);
	request.ref = ref;

	server_set_ref_for_reply reply;
	status_t status = QueryServer(SERVER_SET_REF_FOR, &request, sizeof(request),
		&reply, sizeof(reply));
	if (status != B_OK) {
		ERROR("BMediaFiles::SetRefFor: failed: %s\n", strerror(status));
		return status;
	}

	return B_OK;
}


/** @brief Sets the playback audio gain for the named item under the given type.
 *  @param type The type string.
 *  @param item The item name.
 *  @param gain The new gain value.
 *  @return B_OK on success, or an error code from the server.
 */
status_t
BMediaFiles::SetAudioGainFor(const char* type, const char* item, float gain)
{
	CALLED();

	server_set_item_audio_gain_request request;
	strlcpy(request.type, type, B_MEDIA_NAME_LENGTH);
	strlcpy(request.item, item, B_MEDIA_NAME_LENGTH);
	request.gain = gain;

	server_set_item_audio_gain_reply reply;
	status_t status = QueryServer(SERVER_SET_ITEM_AUDIO_GAIN, &request,
		sizeof(request), &reply, sizeof(reply));
	if (status != B_OK) {
		ERROR("BMediaFiles::SetAudioGainFor: failed: %s\n", strerror(status));
		return status;
	}

	return B_OK;
}


/** @brief Removes (invalidates) the file reference for the named item.
 *  @param type The type string.
 *  @param item The item name.
 *  @param ref  The entry_ref to invalidate (used for identification).
 *  @return B_OK on success, or an error code from the server.
 */
status_t
BMediaFiles::RemoveRefFor(const char* type, const char* item,
	const entry_ref &ref)
{
	CALLED();

	server_invalidate_item_request request;
	strlcpy(request.type, type, B_MEDIA_NAME_LENGTH);
	strlcpy(request.item, item, B_MEDIA_NAME_LENGTH);

	server_invalidate_item_reply reply;
	status_t status = QueryServer(SERVER_INVALIDATE_MEDIA_ITEM, &request,
		sizeof(request), &reply, sizeof(reply));
	if (status != B_OK) {
		ERROR("BMediaFiles::RemoveRefFor: failed: %s\n", strerror(status));
		return status;
	}

	return B_OK;
}


/** @brief Removes the named item entirely from the given type.
 *  @param type The type string.
 *  @param item The item name to remove.
 *  @return B_OK on success, or an error code from the server.
 */
status_t
BMediaFiles::RemoveItem(const char* type, const char* item)
{
	CALLED();

	server_remove_media_item_request request;
	strlcpy(request.type, type, B_MEDIA_NAME_LENGTH);
	strlcpy(request.item, item, B_MEDIA_NAME_LENGTH);

	server_remove_media_item_reply reply;
	status_t status = QueryServer(SERVER_REMOVE_MEDIA_ITEM, &request,
		sizeof(request), &reply, sizeof(reply));
	if (status != B_OK) {
		ERROR("BMediaFiles::RemoveItem: failed: %s\n", strerror(status));
		return status;
	}

	return B_OK;
}


// #pragma mark - private


/** @brief Frees all cached type strings and empties the types list. */
void
BMediaFiles::_ClearTypes()
{
	for (int32 i = 0; i < fTypes.CountItems(); i++)
		delete (BString*)fTypes.ItemAt(i);

	fTypes.MakeEmpty();
}


/** @brief Frees all cached item strings and empties the items list. */
void
BMediaFiles::_ClearItems()
{
	for (int32 i = 0; i < fItems.CountItems(); i++)
		delete (BString*)fItems.ItemAt(i);

	fItems.MakeEmpty();
}


// #pragma mark - FBC padding


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BMediaFiles::_Reserved_MediaFiles_0(void*,...)
{
	// TODO: Someone didn't understand FBC
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFiles::_Reserved_MediaFiles_1(void*,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFiles::_Reserved_MediaFiles_2(void*,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFiles::_Reserved_MediaFiles_3(void*,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFiles::_Reserved_MediaFiles_4(void*,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFiles::_Reserved_MediaFiles_5(void*,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFiles::_Reserved_MediaFiles_6(void*,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFiles::_Reserved_MediaFiles_7(void*,...) { return B_ERROR; }

