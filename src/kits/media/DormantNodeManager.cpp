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
 *   Copyright (c) 2002, 2003 Marcus Overhagen <Marcus@Overhagen.de>
 *
 *   Permission is hereby granted, free of charge, to any person obtaining
 *   a copy of this software and associated documentation files or portions
 *   thereof (the "Software"), to deal in the Software without restriction,
 *   including without limitation the rights to use, copy, modify, merge,
 *   publish, distribute, sublicense, and/or sell copies of the Software,
 *   and to permit persons to whom the Software is furnished to do so, subject
 *   to the following conditions:
 *
 *    * Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *
 *    * Redistributions in binary form must reproduce the above copyright notice
 *      in the  binary, as well as this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided with
 *      the distribution.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 *   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *   THE SOFTWARE.
 */

/** @file DormantNodeManager.cpp
 *  @brief Implements DormantNodeManager, which loads and unloads dormant media node add-ons on demand. */


/*!	This is a management class for dormant media nodes.
	It is private to the media kit and only accessed by the BMediaRoster class
	and the media_addon_server.
	It handles loading/unloading of dormant nodes.

	Dormant media nodes can be instantiated on demand. The reside on harddisk in
	the directories /boot/beos/system/add-ons/media
	and /boot/home/config/add-ons/media.
	Multiple media nodes can be included in one file, they can be accessed using
	the BMediaAddOn that each file implements.
	The BMediaAddOn allows getting a list of supported flavors. Each flavor
	represents a media node.
	The media_addon_server does the initial scanning of files and getting the
	list of supported flavors. It uses the flavor_info to do this, and reports
	the list of flavors to the media_server packed into individual
	dormant_media_node structures.
*/


#include "DormantNodeManager.h"

#include <stdio.h>
#include <string.h>

#include <Autolock.h>
#include <Entry.h>
#include <Path.h>

#include <MediaDebug.h>
#include <MediaMisc.h>
#include <ServerInterface.h>
#include <DataExchange.h>


namespace BPrivate {
namespace media {


DormantNodeManager* gDormantNodeManager;
	// initialized by BMediaRoster.


/** @brief Constructs the DormantNodeManager with an empty add-on map and a named lock. */
DormantNodeManager::DormantNodeManager()
	:
	fLock("dormant node manager locker")
{
}


/** @brief Destructor. Force-unloads all currently loaded add-on images, logging any leaks. */
DormantNodeManager::~DormantNodeManager()
{
	// force unloading all currently loaded images

	AddOnMap::iterator iterator = fAddOnMap.begin();
	for (; iterator != fAddOnMap.end(); iterator++) {
		loaded_add_on_info& info = iterator->second;

		ERROR("Forcing unload of add-on id %" B_PRId32 " with usecount %"
			B_PRId32 "\n", info.add_on->AddonID(), info.use_count);
		_UnloadAddOn(info.add_on, info.image);
	}
}


/** @brief Returns a BMediaAddOn for the given add-on @p id, loading it from disk if necessary.
 *
 *  The add-on's use-count is incremented by one.  The caller must balance each
 *  successful GetAddOn() call with a corresponding PutAddOn() or PutAddOnDelayed().
 *
 *  @param id  The media_addon_id to look up.
 *  @return Pointer to the BMediaAddOn, or NULL if the add-on could not be found or loaded. */
BMediaAddOn*
DormantNodeManager::GetAddOn(media_addon_id id)
{
	TRACE("DormantNodeManager::GetAddon, id %" B_PRId32 "\n", id);

	// first try to use a already loaded add-on
	BMediaAddOn* addOn = _LookupAddOn(id);
	if (addOn != NULL)
		return addOn;

	// Be careful, we avoid locking here!

	// ok, it's not loaded, try to get the path
	BPath path;
	if (FindAddOnPath(&path, id) != B_OK) {
		ERROR("DormantNodeManager::GetAddon: can't find path for add-on %"
			B_PRId32 "\n", id);
		return NULL;
	}

	// try to load it
	BMediaAddOn* newAddOn;
	image_id image;
	if (_LoadAddOn(path.Path(), id, &newAddOn, &image) != B_OK) {
		ERROR("DormantNodeManager::GetAddon: can't load add-on %" B_PRId32
			" from path %s\n",id, path.Path());
		return NULL;
	}

	// ok, we successfully loaded it. Now lock and insert it into the map,
	// or unload it if the map already contains one that was loaded by another
	// thread at the same time

	BAutolock _(fLock);

	addOn = _LookupAddOn(id);
	if (addOn == NULL) {
		// we use the loaded one
		addOn = newAddOn;

		// and save it into the list
		loaded_add_on_info info;
		info.add_on = newAddOn;
		info.image = image;
		info.use_count = 1;
		try {
			fAddOnMap.insert(std::make_pair(id, info));
		} catch (std::bad_alloc& exception) {
			_UnloadAddOn(newAddOn, image);
			return NULL;
		}
	} else
		_UnloadAddOn(newAddOn, image);

	ASSERT(addOn->AddonID() == id);
	return addOn;
}


/** @brief Decrements the use-count for the add-on with the given @p id.
 *
 *  When the use-count reaches zero the add-on is unloaded from memory.
 *
 *  @param id  The media_addon_id whose reference count should be decremented. */
void
DormantNodeManager::PutAddOn(media_addon_id id)
{
	TRACE("DormantNodeManager::PutAddon, id %" B_PRId32 "\n", id);

	BAutolock locker(fLock);

	AddOnMap::iterator found = fAddOnMap.find(id);
	if (found == fAddOnMap.end()) {
		ERROR("DormantNodeManager::PutAddon: failed to find add-on %" B_PRId32
			"\n", id);
		return;
	}

	loaded_add_on_info& info = found->second;

	if (--info.use_count == 0) {
		// unload add-on

		BMediaAddOn* addOn = info.add_on;
		image_id image = info.image;
		fAddOnMap.erase(found);

		locker.Unlock();
		_UnloadAddOn(addOn, image);
	}
}


/** @brief Schedules a delayed release of the add-on with the given @p id.
 *
 *  Must be called from a node destructor of the loaded add-on to ensure the
 *  add-on image remains in memory long enough for the destructor to complete.
 *  Currently unimplemented.
 *
 *  @param id  The media_addon_id to release after a delay. */
void
DormantNodeManager::PutAddOnDelayed(media_addon_id id)
{
	// Called from a node destructor of the loaded media-add-on.
	// We must make sure that the media-add-on stays in memory
	// a couple of seconds longer.

	UNIMPLEMENTED();
}


/** @brief Registers an add-on file with the media server (for use by media_addon_server only).
 *
 *  Sends a SERVER_REGISTER_ADD_ON request and returns the assigned media_addon_id.
 *
 *  @param path  Filesystem path of the add-on to register.
 *  @return The assigned media_addon_id on success, or 0 on failure. */
//!	For use by media_addon_server only
media_addon_id
DormantNodeManager::RegisterAddOn(const char* path)
{
	TRACE("DormantNodeManager::RegisterAddon, path %s\n", path);

	entry_ref ref;
	status_t status = get_ref_for_path(path, &ref);
	if (status != B_OK) {
		ERROR("DormantNodeManager::RegisterAddon failed, couldn't get ref "
			"for path %s: %s\n", path, strerror(status));
		return 0;
	}

	server_register_add_on_request request;
	request.ref = ref;

	server_register_add_on_reply reply;
	status = QueryServer(SERVER_REGISTER_ADD_ON, &request, sizeof(request),
		&reply, sizeof(reply));
	if (status != B_OK) {
		ERROR("DormantNodeManager::RegisterAddon failed, couldn't talk to "
			"media server: %s\n", strerror(status));
		return 0;
	}

	TRACE("DormantNodeManager::RegisterAddon finished with id %" B_PRId32 "\n",
		reply.add_on_id);

	return reply.add_on_id;
}


/** @brief Unregisters the add-on with @p id from the media server (for use by media_addon_server only).
 *
 *  Sends a SERVER_UNREGISTER_ADD_ON message via the media server port.
 *
 *  @param id  The media_addon_id to unregister; must be greater than zero. */
//!	For use by media_addon_server only
void
DormantNodeManager::UnregisterAddOn(media_addon_id id)
{
	TRACE("DormantNodeManager::UnregisterAddon id %" B_PRId32 "\n", id);
	ASSERT(id > 0);

	port_id port = find_port(MEDIA_SERVER_PORT_NAME);
	if (port < 0)
		return;

	server_unregister_add_on_command msg;
	msg.add_on_id = id;
	write_port(port, SERVER_UNREGISTER_ADD_ON, &msg, sizeof(msg));
}


/** @brief Resolves the filesystem path for the add-on identified by @p id by querying the media server.
 *
 *  @param path  Output BPath set to the add-on's location on success.
 *  @param id    The media_addon_id to look up.
 *  @return B_OK on success, or an error code if the server query fails. */
status_t
DormantNodeManager::FindAddOnPath(BPath* path, media_addon_id id)
{
	server_get_add_on_ref_request request;
	request.add_on_id = id;

	server_get_add_on_ref_reply reply;
	status_t status = QueryServer(SERVER_GET_ADD_ON_REF, &request,
		sizeof(request), &reply, sizeof(reply));
	if (status != B_OK)
		return status;

	entry_ref ref = reply.ref;
	return path->SetTo(&ref);
}


/** @brief Looks up @p id in the in-memory add-on map and increments its use-count if found.
 *
 *  @param id  The media_addon_id to look up.
 *  @return Pointer to the BMediaAddOn if already loaded, or NULL. */
BMediaAddOn*
DormantNodeManager::_LookupAddOn(media_addon_id id)
{
	BAutolock _(fLock);

	AddOnMap::iterator found = fAddOnMap.find(id);
	if (found == fAddOnMap.end())
		return NULL;

	loaded_add_on_info& info = found->second;

	ASSERT(id == info.add_on->AddonID());
	info.use_count++;

	return info.add_on;
}


/** @brief Loads the add-on image at @p path, resolves make_media_addon(), and instantiates the add-on.
 *
 *  Also initialises the private fAddon and fImage fields of the returned BMediaAddOn.
 *
 *  @param path       Filesystem path of the add-on image.
 *  @param id         The media_addon_id to assign to the new add-on.
 *  @param _newAddOn  Output pointer set to the instantiated BMediaAddOn.
 *  @param _newImage  Output pointer set to the loaded image_id.
 *  @return B_OK on success, or an error code. */
status_t
DormantNodeManager::_LoadAddOn(const char* path, media_addon_id id,
	BMediaAddOn** _newAddOn, image_id* _newImage)
{
	image_id image = load_add_on(path);
	if (image < 0) {
		ERROR("DormantNodeManager::LoadAddon: loading \"%s\" failed: %s\n",
			path, strerror(image));
		return image;
	}

	BMediaAddOn* (*makeAddOn)(image_id);
	status_t status = get_image_symbol(image, "make_media_addon",
		B_SYMBOL_TYPE_TEXT, (void**)&makeAddOn);
	if (status != B_OK) {
		ERROR("DormantNodeManager::LoadAddon: loading failed, function not "
			"found: %s\n", strerror(status));
		unload_add_on(image);
		return status;
	}

	BMediaAddOn* addOn = makeAddOn(image);
	if (addOn == NULL) {
		ERROR("DormantNodeManager::LoadAddon: creating BMediaAddOn failed\n");
		unload_add_on(image);
		return B_ERROR;
	}

	ASSERT(addOn->ImageID() == image);
		// this should be true for a well behaving add-ons

	// We are a friend class of BMediaAddOn and initialize these member
	// variables
	addOn->fAddon = id;
	addOn->fImage = image;

	// everything ok
	*_newAddOn = addOn;
	*_newImage = image;

	return B_OK;
}


/** @brief Deletes the BMediaAddOn object and unloads the associated image.
 *
 *  @param addOn  The add-on instance to delete; must not be NULL.
 *  @param image  The image_id returned when the add-on was loaded. */
void
DormantNodeManager::_UnloadAddOn(BMediaAddOn* addOn, image_id image)
{
	ASSERT(addOn != NULL);
	ASSERT(addOn->ImageID() == image);
		// if this fails, something bad happened to the add-on

	delete addOn;
	unload_add_on(image);
}


}	// namespace media
}	// namespace BPrivate
