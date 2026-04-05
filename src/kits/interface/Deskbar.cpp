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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2018 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Jérôme Duval
 *       Axel Dörfler
 *       Jeremy Rand, jrand@magma.ca
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file Deskbar.cpp
 * @brief Implementation of BDeskbar, the client interface to the Deskbar application
 *
 * BDeskbar lets applications add, remove, and query replicant views embedded in
 * the system Deskbar (taskbar). It communicates with the Deskbar via BMessenger.
 *
 * @see BView, BMessenger
 */


#include <Deskbar.h>

#include <string.h>

#include <Messenger.h>
#include <Message.h>
#include <View.h>
#include <Rect.h>
#include <InterfaceDefs.h>
#include <Node.h>

#include <DeskbarPrivate.h>


// TODO: in case the BDeskbar methods are called from a Deskbar add-on,
//	they will currently deadlock most of the time (only those that do
//	not need a reply will work).
//	That should be fixed in the Deskbar itself, even if the Be API found
//	a way around that (that doesn't work too well, BTW)

// The API in this file should be considered as part of OpenTracker - but
// should work with all versions of Tracker available for Haiku.


/**
 * @brief Retrieves the current on-screen frame of the Deskbar window.
 *
 * Sends a scripting message to the Deskbar requesting the frame of its
 * first window, and writes the result into @a frame.
 *
 * @param frame  Output pointer to the BRect that receives the Deskbar frame.
 * @return B_OK on success, or a BMessenger/BMessage error code on failure.
 * @retval B_OK On success.
 */
status_t
get_deskbar_frame(BRect* frame)
{
	BMessenger deskbar(kDeskbarSignature);

	status_t result;

	BMessage request(B_GET_PROPERTY);
	request.AddSpecifier("Frame");
	request.AddSpecifier("Window", (int32)0);

	BMessage reply;
	result = deskbar.SendMessage(&request, &reply);
	if (result == B_OK)
		result = reply.FindRect("result", frame);

	return result;
}


//	#pragma mark - BDeskbar


/**
 * @brief Constructs a BDeskbar object and connects to the running Deskbar.
 *
 * Allocates a BMessenger targeting the Deskbar application signature.
 * Use IsRunning() to verify that the Deskbar is actually available.
 *
 * @see IsRunning()
 */
BDeskbar::BDeskbar()
	:
	fMessenger(new BMessenger(kDeskbarSignature))
{
}


/**
 * @brief Destroys the BDeskbar object and releases the internal messenger.
 */
BDeskbar::~BDeskbar()
{
	delete fMessenger;
}


/**
 * @brief Returns whether the Deskbar application is currently running.
 *
 * @return @c true if the internal BMessenger target is valid (i.e. the
 *         Deskbar process is alive), @c false otherwise.
 */
bool
BDeskbar::IsRunning() const
{
	return fMessenger->IsValid();
}


//	#pragma mark - Item querying methods


/**
 * @brief Returns the current on-screen frame of the Deskbar window.
 *
 * Internally calls get_deskbar_frame(); returns an empty rect on failure.
 *
 * @return A BRect describing the Deskbar window bounds in screen coordinates.
 * @see get_deskbar_frame()
 */
BRect
BDeskbar::Frame() const
{
	BRect frame(0.0, 0.0, 0.0, 0.0);
	get_deskbar_frame(&frame);

	return frame;
}


/**
 * @brief Returns the current screen-edge location of the Deskbar.
 *
 * Queries the Deskbar for its dockside position.  If @a _isExpanded is
 * non-NULL it is also filled with the current expanded/collapsed state.
 * When the Deskbar target is local (i.e. this call originates inside the
 * Deskbar itself) the method returns the default value immediately to
 * avoid a deadlock.
 *
 * @param _isExpanded  Optional output: set to @c true if the Deskbar is
 *                     currently expanded, @c false if collapsed.
 *                     Unchanged on communication failure.
 * @return A deskbar_location constant describing the current edge/corner.
 * @note Calling this from inside the Deskbar process will not query the
 *       live state and always returns B_DESKBAR_RIGHT_TOP.
 * @see SetLocation()
 */
deskbar_location
BDeskbar::Location(bool* _isExpanded) const
{
	deskbar_location location = B_DESKBAR_RIGHT_TOP;
	BMessage request(kMsgLocation);
	BMessage reply;

	if (_isExpanded)
		*_isExpanded = true;

	if (fMessenger->IsTargetLocal()) {
		// ToDo: do something about this!
		// (if we just ask the Deskbar in this case, we would deadlock)
		return location;
	}

	if (fMessenger->SendMessage(&request, &reply) == B_OK) {
		int32 value;
		if (reply.FindInt32("location", &value) == B_OK)
			location = static_cast<deskbar_location>(value);

		if (_isExpanded && reply.FindBool("expanded", _isExpanded) != B_OK)
			*_isExpanded = true;
	}

	return location;
}


/**
 * @brief Moves the Deskbar to a new screen-edge location and sets its
 *        expanded state.
 *
 * @param location  The desired deskbar_location constant.
 * @param expanded  Pass @c true to expand the Deskbar, @c false to collapse.
 * @return B_OK on success, or a messaging error code.
 * @retval B_OK On success.
 * @see Location()
 */
status_t
BDeskbar::SetLocation(deskbar_location location, bool expanded)
{
	BMessage request(kMsgSetLocation);
	request.AddInt32("location", static_cast<int32>(location));
	request.AddBool("expand", expanded);

	return fMessenger->SendMessage(&request);
}


//	#pragma mark - Other state methods


/**
 * @brief Returns whether the Deskbar is currently in the expanded state.
 *
 * @return @c true if expanded, @c false if collapsed.  Defaults to @c true
 *         if the Deskbar cannot be reached.
 * @see Expand()
 */
bool
BDeskbar::IsExpanded() const
{
	BMessage request(kMsgIsExpanded);
	BMessage reply;
	bool isExpanded = true;

	if (fMessenger->SendMessage(&request, &reply) == B_OK)
		reply.FindBool("expanded", &isExpanded);

	return isExpanded;
}


/**
 * @brief Expands or collapses the Deskbar.
 *
 * @param expand  Pass @c true to expand, @c false to collapse.
 * @return B_OK on success, or a messaging error code.
 * @retval B_OK On success.
 * @see IsExpanded()
 */
status_t
BDeskbar::Expand(bool expand)
{
	BMessage request(kMsgExpand);
	request.AddBool("expand", expand);

	return fMessenger->SendMessage(&request);
}


/**
 * @brief Returns whether the Deskbar window is always kept on top.
 *
 * @return @c true if always-on-top is active, @c false otherwise.
 *         Defaults to @c false if the Deskbar cannot be reached.
 * @see SetAlwaysOnTop()
 */
bool
BDeskbar::IsAlwaysOnTop() const
{
	BMessage request(kMsgIsAlwaysOnTop);
	BMessage reply;
	bool isAlwaysOnTop = false;

	if (fMessenger->SendMessage(&request, &reply) == B_OK)
		reply.FindBool("always on top", &isAlwaysOnTop);

	return isAlwaysOnTop;
}


/**
 * @brief Enables or disables the always-on-top mode for the Deskbar.
 *
 * @param alwaysOnTop  @c true to keep the Deskbar above all other windows,
 *                     @c false to allow it to be covered.
 * @return B_OK on success, or a messaging error code.
 * @retval B_OK On success.
 * @see IsAlwaysOnTop()
 */
status_t
BDeskbar::SetAlwaysOnTop(bool alwaysOnTop)
{
	BMessage request(kMsgAlwaysOnTop);
	request.AddBool("always on top", alwaysOnTop);

	return fMessenger->SendMessage(&request);
}


/**
 * @brief Returns whether the Deskbar is in auto-raise mode.
 *
 * When auto-raise is active the Deskbar rises to the top of the window
 * stack whenever the pointer moves over it.
 *
 * @return @c true if auto-raise is enabled, @c false otherwise.
 *         Defaults to @c false if the Deskbar cannot be reached.
 * @see SetAutoRaise()
 */
bool
BDeskbar::IsAutoRaise() const
{
	BMessage request(kMsgIsAutoRaise);
	BMessage reply;
	bool isAutoRaise = false;

	if (fMessenger->SendMessage(&request, &reply) == B_OK)
		reply.FindBool("auto raise", &isAutoRaise);

	return isAutoRaise;
}


/**
 * @brief Enables or disables auto-raise mode for the Deskbar.
 *
 * @param autoRaise  @c true to enable auto-raise, @c false to disable.
 * @return B_OK on success, or a messaging error code.
 * @retval B_OK On success.
 * @see IsAutoRaise()
 */
status_t
BDeskbar::SetAutoRaise(bool autoRaise)
{
	BMessage request(kMsgAutoRaise);
	request.AddBool("auto raise", autoRaise);

	return fMessenger->SendMessage(&request);
}


/**
 * @brief Returns whether the Deskbar is in auto-hide mode.
 *
 * When auto-hide is active the Deskbar slides off-screen when the pointer
 * leaves it.
 *
 * @return @c true if auto-hide is enabled, @c false otherwise.
 *         Defaults to @c false if the Deskbar cannot be reached.
 * @see SetAutoHide()
 */
bool
BDeskbar::IsAutoHide() const
{
	BMessage request(kMsgIsAutoHide);
	BMessage reply;
	bool isAutoHidden = false;

	if (fMessenger->SendMessage(&request, &reply) == B_OK)
		reply.FindBool("auto hide", &isAutoHidden);

	return isAutoHidden;
}


/**
 * @brief Enables or disables auto-hide mode for the Deskbar.
 *
 * @param autoHide  @c true to enable auto-hide, @c false to disable.
 * @return B_OK on success, or a messaging error code.
 * @retval B_OK On success.
 * @see IsAutoHide()
 */
status_t
BDeskbar::SetAutoHide(bool autoHide)
{
	BMessage request(kMsgAutoHide);
	request.AddBool("auto hide", autoHide);

	return fMessenger->SendMessage(&request);
}


//	#pragma mark - Item querying methods


/**
 * @brief Retrieves the name of a Deskbar replicant by its numeric ID.
 *
 * On success, allocates a copy of the name string via strdup() and stores
 * the pointer in @a *_name.  The caller is responsible for freeing this
 * string.
 *
 * @param id     The integer identifier of the replicant to query.
 * @param _name  Output pointer to receive a newly-allocated name string.
 *               Must not be @c NULL.
 * @return B_OK on success.
 * @retval B_BAD_VALUE  If @a _name is @c NULL.
 * @retval B_NO_MEMORY  If the name string could not be duplicated.
 * @see HasItem(), CountItems()
 */
status_t
BDeskbar::GetItemInfo(int32 id, const char** _name) const
{
	if (_name == NULL)
		return B_BAD_VALUE;

	// Note: Be's implementation returns B_BAD_VALUE if *_name was NULL,
	// not just if _name was NULL.  This doesn't make much sense, so we
	// do not imitate this behaviour.

	BMessage request(kMsgGetItemInfo);
	request.AddInt32("id", id);

	BMessage reply;
	status_t result = fMessenger->SendMessage(&request, &reply);
	if (result == B_OK) {
		const char* name;
		result = reply.FindString("name", &name);
		if (result == B_OK) {
			*_name = strdup(name);
			if (*_name == NULL)
				result = B_NO_MEMORY;
		}
	}

	return result;
}


/**
 * @brief Retrieves the numeric ID of a Deskbar replicant by its name.
 *
 * @param name  The name of the replicant to look up.  Must not be @c NULL.
 * @param _id   Output pointer that receives the replicant's integer ID.
 * @return B_OK on success.
 * @retval B_BAD_VALUE  If @a name is @c NULL.
 * @see HasItem(), CountItems()
 */
status_t
BDeskbar::GetItemInfo(const char* name, int32* _id) const
{
	if (name == NULL)
		return B_BAD_VALUE;

	BMessage request(kMsgGetItemInfo);
	request.AddString("name", name);

	BMessage reply;
	status_t result = fMessenger->SendMessage(&request, &reply);
	if (result == B_OK)
		result = reply.FindInt32("id", _id);

	return result;
}


/**
 * @brief Tests whether a Deskbar replicant with the given numeric ID exists.
 *
 * @param id  The integer identifier of the replicant to test.
 * @return @c true if a replicant with that ID is present, @c false otherwise.
 * @see HasItem(const char*), GetItemInfo()
 */
bool
BDeskbar::HasItem(int32 id) const
{
	BMessage request(kMsgHasItem);
	request.AddInt32("id", id);

	BMessage reply;
	if (fMessenger->SendMessage(&request, &reply) == B_OK)
		return reply.FindBool("exists");

	return false;
}


/**
 * @brief Tests whether a Deskbar replicant with the given name exists.
 *
 * @param name  The name of the replicant to test.
 * @return @c true if a replicant with that name is present, @c false otherwise.
 * @see HasItem(int32), GetItemInfo()
 */
bool
BDeskbar::HasItem(const char* name) const
{
	BMessage request(kMsgHasItem);
	request.AddString("name", name);

	BMessage reply;
	if (fMessenger->SendMessage(&request, &reply) == B_OK)
		return reply.FindBool("exists");

	return false;
}


/**
 * @brief Returns the number of replicant items currently in the Deskbar.
 *
 * @return The item count, or 0 if the Deskbar cannot be reached.
 * @see HasItem(), GetItemInfo()
 */
uint32
BDeskbar::CountItems() const
{
	BMessage request(kMsgCountItems);
	BMessage reply;

	if (fMessenger->SendMessage(&request, &reply) == B_OK)
		return reply.FindInt32("count");

	return 0;
}


/**
 * @brief Returns the maximum allowed width for a Deskbar replicant view.
 *
 * @return The maximum width in pixels, or 129 if the Deskbar cannot be reached.
 * @see MaxItemHeight()
 */
float
BDeskbar::MaxItemWidth() const
{
	BMessage request(kMsgMaxItemSize);
	BMessage reply;

	if (fMessenger->SendMessage(&request, &reply) == B_OK)
		return reply.GetFloat("width", 129);

	return 129;
}


/**
 * @brief Returns the maximum allowed height for a Deskbar replicant view.
 *
 * @return The maximum height in pixels, or 16 if the Deskbar cannot be reached.
 * @see MaxItemWidth()
 */
float
BDeskbar::MaxItemHeight() const
{
	BMessage request(kMsgMaxItemSize);
	BMessage reply;

	if (fMessenger->SendMessage(&request, &reply) == B_OK)
		return reply.GetFloat("height", 16);

	return 16;
}


//	#pragma mark - Item modification methods


/**
 * @brief Adds a replicant view to the Deskbar by archiving the given BView.
 *
 * Archives @a view into a BMessage and sends it to the Deskbar, which
 * instantiates it as a replicant.  On success the numeric ID assigned by the
 * Deskbar is stored in @a *_id when @a _id is non-NULL.
 *
 * @param view  The BView to replicate inside the Deskbar.  The view must be
 *              archivable (its Archive() method must succeed).
 * @param _id   Optional output pointer that receives the new replicant's ID.
 * @return B_OK on success, or an error code on failure.
 * @retval B_OK On success.
 * @see RemoveItem(), AddItem(entry_ref*, int32*)
 */
status_t
BDeskbar::AddItem(BView* view, int32* _id)
{
	BMessage archive;
	status_t result = view->Archive(&archive);
	if (result < B_OK)
		return result;

	BMessage request(kMsgAddView);
	request.AddMessage("view", &archive);

	BMessage reply;
	result = fMessenger->SendMessage(&request, &reply);
	if (result == B_OK) {
		if (_id != NULL)
			result = reply.FindInt32("id", _id);
		else
			reply.FindInt32("error", &result);
	}

	return result;
}


/**
 * @brief Adds a replicant to the Deskbar by loading an add-on from a file.
 *
 * Sends the entry_ref of the add-on file to the Deskbar, which loads and
 * instantiates it.  On success the numeric ID assigned by the Deskbar is
 * stored in @a *_id when @a _id is non-NULL.
 *
 * @param addon  Reference to the add-on file containing the replicant view.
 * @param _id    Optional output pointer that receives the new replicant's ID.
 * @return B_OK on success, or an error code on failure.
 * @retval B_OK On success.
 * @see RemoveItem(), AddItem(BView*, int32*)
 */
status_t
BDeskbar::AddItem(entry_ref* addon, int32* _id)
{
	BMessage request(kMsgAddAddOn);
	request.AddRef("addon", addon);

	BMessage reply;
	status_t status = fMessenger->SendMessage(&request, &reply);
	if (status == B_OK) {
		if (_id != NULL)
			status = reply.FindInt32("id", _id);
		else
			reply.FindInt32("error", &status);
	}

	return status;
}


/**
 * @brief Removes the Deskbar replicant with the given numeric ID.
 *
 * Sends a removal request to the Deskbar.  Because the Deskbar does not
 * reply to this message, the return value only confirms delivery, not
 * actual removal.
 *
 * @param id  The integer identifier of the replicant to remove.
 * @return B_OK if the message was delivered, or a messaging error code.
 * @retval B_OK On successful message delivery.
 * @note The return value does not guarantee the replicant was removed.
 * @see RemoveItem(const char*), AddItem()
 */
status_t
BDeskbar::RemoveItem(int32 id)
{
	BMessage request(kMsgRemoveItem);
	request.AddInt32("id", id);

	// ToDo: the Deskbar does not reply to this message, so we don't
	// know if it really succeeded - we can just acknowledge that the
	// message was sent to the Deskbar

	return fMessenger->SendMessage(&request);
}


/**
 * @brief Removes the Deskbar replicant with the given name.
 *
 * Sends a removal request to the Deskbar.  Because the Deskbar does not
 * reply to this message, the return value only confirms delivery, not
 * actual removal.
 *
 * @param name  The name of the replicant to remove.
 * @return B_OK if the message was delivered, or a messaging error code.
 * @retval B_OK On successful message delivery.
 * @note The return value does not guarantee the replicant was removed.
 * @see RemoveItem(int32), AddItem()
 */
status_t
BDeskbar::RemoveItem(const char* name)
{
	BMessage request(kMsgRemoveItem);
	request.AddString("name", name);

	// ToDo: the Deskbar does not reply to this message, so we don't
	// know if it really succeeded - we can just acknowledge that the
	// message was sent to the Deskbar

	return fMessenger->SendMessage(&request);
}
