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
 *   Copyright 2003-2007, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file image.cpp
 * @brief Kernel-side image (shared library/executable) registration for user-space runtime loader.
 *
 * Tracks all loaded images (executables and shared libraries) for each team.
 * Provides get_image_info() and get_next_image_info() syscalls. Image records
 * are created by the runtime loader and stored in the kernel.
 *
 * @see elf.cpp, team.cpp
 */


#include <KernelExport.h>

#include <kernel.h>
#include <kimage.h>
#include <kscheduler.h>
#include <lock.h>
#include <Notifications.h>
#include <team.h>
#include <thread.h>
#include <thread_types.h>
#include <user_debugger.h>
#include <util/AutoLock.h>
#include <util/ThreadAutoLock.h>

#include <stdlib.h>
#include <string.h>


//#define TRACE_IMAGE
#ifdef TRACE_IMAGE
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

#define ADD_DEBUGGER_COMMANDS

#ifdef ADD_DEBUGGER_COMMANDS
static int dump_images_list(int argc, char **argv);
#endif


namespace {

struct ImageTableDefinition {
	typedef image_id		KeyType;
	typedef struct image	ValueType;

	size_t HashKey(image_id key) const { return key; }
	size_t Hash(struct image* value) const { return value->info.basic_info.id; }
	bool Compare(image_id key, struct image* value) const
		{ return value->info.basic_info.id == key; }
	struct image*& GetLink(struct image* value) const
		{ return value->hash_link; }
};

typedef BOpenHashTable<ImageTableDefinition> ImageTable;


class ImageNotificationService : public DefaultNotificationService {
public:
	ImageNotificationService()
		: DefaultNotificationService("images")
	{
	}

	void Notify(uint32 eventCode, struct image* image)
	{
		char eventBuffer[128];
		KMessage event;
		event.SetTo(eventBuffer, sizeof(eventBuffer), IMAGE_MONITOR);
		event.AddInt32("event", eventCode);
		event.AddInt32("image", image->info.basic_info.id);
		event.AddPointer("imageStruct", image);

		DefaultNotificationService::Notify(event, eventCode);
	}
};

} // namespace


static image_id sNextImageID = 1;
static mutex sImageMutex = MUTEX_INITIALIZER("image");
static ImageTable* sImageTable;
static ImageNotificationService sNotificationService;


/**
 * @brief Initialise the image subsystem.
 *
 * Allocates and initialises the global image hash table and registers the
 * image notification service. Also installs the "team_images" kernel
 * debugger command when ADD_DEBUGGER_COMMANDS is defined.
 *
 * @return B_OK on success, B_NO_MEMORY if the hash table could not be
 *         allocated, or another error code if initialisation fails.
 */
status_t
image_init(void)
{
	sImageTable = new(std::nothrow) ImageTable;
	if (sImageTable == NULL) {
		panic("image_init(): Failed to allocate image table!");
		return B_NO_MEMORY;
	}

	status_t error = sImageTable->Init();
	if (error != B_OK) {
		panic("image_init(): Failed to init image table: %s", strerror(error));
		return error;
	}

	new(&sNotificationService) ImageNotificationService();

	sNotificationService.Register();

#ifdef ADD_DEBUGGER_COMMANDS
	add_debugger_command("team_images", &dump_images_list, "Dump all registered images from the current team");
#endif

	return B_OK;
}


/**
 * @brief Register a loaded image with the specified team (internal, lock-optional).
 *
 * Creates a new image record, assigns it a unique image ID, and appends it to
 * the team's image list. App images (B_APP_IMAGE) are inserted at the head of
 * the list so they are returned first by get_next_image_info(). Notifies all
 * registered image listeners.
 *
 * @param team   The team that owns the image.
 * @param info   Extended image information supplied by the runtime loader.
 * @param size   Size of the @p info structure (must equal sizeof(extended_image_info)).
 * @param locked If @c true the caller already holds @c sImageMutex; the
 *               function will not attempt to acquire or release it.
 * @return The new image_id (>= 1) on success, or B_NO_MEMORY if allocation fails.
 */
static image_id
register_image(Team *team, extended_image_info *info, size_t size, bool locked)
{
	image_id id = atomic_add(&sNextImageID, 1);
	struct image *image;

	image = (struct image*)malloc(sizeof(struct image));
	if (image == NULL)
		return B_NO_MEMORY;

	memcpy(&image->info, info, sizeof(extended_image_info));
	image->team = team->id;

	if (!locked)
		mutex_lock(&sImageMutex);

	image->info.basic_info.id = id;

	// Add the app image to the head of the list. Some code relies on it being
	// the first image to be returned by get_next_image_info().
	if (image->info.basic_info.type == B_APP_IMAGE)
		team->image_list.Add(image, false);
	else
		team->image_list.Add(image);
	sImageTable->Insert(image);

	// notify listeners
	sNotificationService.Notify(IMAGE_ADDED, image);

	if (!locked)
		mutex_unlock(&sImageMutex);

	TRACE(("register_image(team = %p, image id = %ld, image = %p\n", team, id, image));
	return id;
}


/**
 * @brief Register a loaded image with the specified team.
 *
 * Public wrapper around the internal register_image() that always acquires
 * @c sImageMutex before modifying the image list.
 *
 * @param team  The team that owns the image.
 * @param info  Extended image information supplied by the runtime loader.
 * @param size  Size of the @p info structure.
 * @return The new image_id on success, or B_NO_MEMORY if allocation fails.
 */
image_id
register_image(Team *team, extended_image_info *info, size_t size)
{
	return register_image(team, info, size, false);
}


/**
 * @brief Unregister an image from the specified team.
 *
 * Removes the image record identified by @p id from the team's image list and
 * the global image table. Notifies the user debugger and all image listeners,
 * then frees the record.
 *
 * @param team The team that owns the image.
 * @param id   The image_id to remove.
 * @retval B_OK             The image was found and removed.
 * @retval B_ENTRY_NOT_FOUND No image with the given ID exists in this team.
 */
status_t
unregister_image(Team *team, image_id id)
{
	status_t status = B_ENTRY_NOT_FOUND;

	mutex_lock(&sImageMutex);

	struct image *image = sImageTable->Lookup(id);
	if (image != NULL && image->team == team->id) {
		team->image_list.Remove(image);
		sImageTable->Remove(image);
		status = B_OK;
	}

	mutex_unlock(&sImageMutex);

	if (status == B_OK) {
		// notify the debugger
		user_debug_image_deleted(&image->info.basic_info);

		// notify listeners
		sNotificationService.Notify(IMAGE_REMOVED, image);

		free(image);
	}

	return status;
}


/**
 * @brief Copy all images from one team to another.
 *
 * Iterates the image list of @p fromTeamId and registers each image with
 * @p toTeam. Used when forking or exec-ing a team that inherits images.
 *
 * @param fromTeamId Source team whose images are to be copied.
 * @param toTeam     Destination team that will receive copies of the images.
 * @retval B_OK           All images copied successfully.
 * @retval B_BAD_TEAM_ID  @p fromTeamId does not refer to a valid team.
 * @return A negative error code if any individual register_image() call fails.
 */
status_t
copy_images(team_id fromTeamId, Team *toTeam)
{
	// get the team
	Team* fromTeam = Team::Get(fromTeamId);
	if (fromTeam == NULL)
		return B_BAD_TEAM_ID;
	BReference<Team> teamReference(fromTeam, true);

	MutexLocker locker(sImageMutex);

	for (struct image* image = fromTeam->image_list.First();
			image != NULL; image = fromTeam->image_list.GetNext(image)) {
		image_id id = register_image(toTeam, &image->info, sizeof(image->info),
			true);
		if (id < 0)
			return id;
	}

	return B_OK;
}


/**
 * @brief Count the number of images registered by a team.
 *
 * Acquires @c sImageMutex and walks the team's image list. Interrupts must be
 * enabled when this function is called.
 *
 * @param team The team whose image list is to be counted.
 * @return The number of images currently registered by @p team.
 */
int32
count_images(Team *team)
{
	MutexLocker locker(sImageMutex);

	int32 count = 0;
	for (struct image* image = team->image_list.First();
			image != NULL; image = team->image_list.GetNext(image)) {
		count++;
	}

	return count;
}


/**
 * @brief Remove all images belonging to a team that has already exited.
 *
 * Must only be called with a team that has been removed from the team list
 * (typically from thread_exit()). Removes every image from the global table
 * and frees the records without sending notifications, as the team is already
 * being torn down.
 *
 * @param team The exiting team whose images are to be purged.
 * @retval B_OK Always returns B_OK.
 */
status_t
remove_images(Team *team)
{
	ASSERT(team != NULL);

	mutex_lock(&sImageMutex);

	DoublyLinkedList<struct image> images;
	images.TakeFrom(&team->image_list);

	for (struct image* image = images.First();
			image != NULL; image = images.GetNext(image)) {
		sImageTable->Remove(image);
	}

	mutex_unlock(&sImageMutex);

	while (struct image* image = images.RemoveHead())
		free(image);

	return B_OK;
}


/**
 * @brief Retrieve information about a specific image by ID.
 *
 * Looks up the image record in the global hash table and copies up to @p size
 * bytes of its basic_info into @p info.
 *
 * @param id   The image_id to look up.
 * @param info Caller-supplied buffer that receives the image_info data.
 * @param size Number of bytes to copy; must be <= sizeof(image_info).
 * @retval B_OK             Information was copied successfully.
 * @retval B_BAD_VALUE      @p size is larger than sizeof(image_info).
 * @retval B_ENTRY_NOT_FOUND No image with the given ID was found.
 */
status_t
_get_image_info(image_id id, image_info *info, size_t size)
{
	if (size > sizeof(image_info))
		return B_BAD_VALUE;

	status_t status = B_ENTRY_NOT_FOUND;

	mutex_lock(&sImageMutex);

	struct image *image = sImageTable->Lookup(id);
	if (image != NULL) {
		memcpy(info, &image->info.basic_info, size);
		status = B_OK;
	}

	mutex_unlock(&sImageMutex);

	return status;
}


/**
 * @brief Iterate over a team's image list and retrieve the next entry.
 *
 * Uses @p cookie as a positional index into the team's image list. On each
 * call the index is incremented so that successive calls enumerate all images.
 *
 * @param teamID  ID of the team whose images are to be enumerated.
 * @param cookie  In/out positional cookie (starts at 0; incremented on success).
 * @param info    Caller-supplied buffer that receives the image_info data.
 * @param size    Number of bytes to copy; must be <= sizeof(image_info).
 * @retval B_OK             The next image was found and its info copied.
 * @retval B_BAD_VALUE      @p size is larger than sizeof(image_info).
 * @retval B_BAD_TEAM_ID    @p teamID does not refer to a valid team.
 * @retval B_ENTRY_NOT_FOUND The cookie is past the end of the image list.
 */
status_t
_get_next_image_info(team_id teamID, int32 *cookie, image_info *info,
	size_t size)
{
	if (size > sizeof(image_info))
		return B_BAD_VALUE;

	// get the team
	Team* team = Team::Get(teamID);
	if (team == NULL)
		return B_BAD_TEAM_ID;
	BReference<Team> teamReference(team, true);

	// iterate through the team's images
	MutexLocker imageLocker(sImageMutex);

	int32 count = 0;

	for (struct image* image = team->image_list.First();
			image != NULL; image = team->image_list.GetNext(image)) {
		if (count == *cookie) {
			memcpy(info, &image->info.basic_info, size);
			(*cookie)++;
			return B_OK;
		}
		count++;
	}

	return B_ENTRY_NOT_FOUND;
}


#ifdef ADD_DEBUGGER_COMMANDS
static int
dump_images_list(int argc, char **argv)
{
	Team *team;

	if (argc > 1) {
		team_id id = strtol(argv[1], NULL, 0);
		team = team_get_team_struct_locked(id);
		if (team == NULL) {
			kprintf("No team with ID %" B_PRId32 " found\n", id);
			return 1;
		}
	} else
		team = thread_get_current_thread()->team;

	kprintf("Registered images of team %" B_PRId32 "\n", team->id);
	kprintf("    ID %-*s   size    %-*s   size    name\n",
		B_PRINTF_POINTER_WIDTH, "text", B_PRINTF_POINTER_WIDTH, "data");

	for (struct image* image = team->image_list.First();
			image != NULL; image = team->image_list.GetNext(image)) {
		image_info *info = &image->info.basic_info;

		kprintf("%6" B_PRId32 " %p %-7" B_PRId32 " %p %-7" B_PRId32 " %s\n",
			info->id, info->text, info->text_size, info->data, info->data_size,
			info->name);
	}

	return 0;
}
#endif


/**
 * @brief Iterate over every image in the global image table.
 *
 * Calls @p callback for each image until the callback returns @c true or the
 * table is exhausted. The image mutex is held for the entire iteration.
 *
 * @param callback Function called for each image; return @c true to stop early.
 * @param cookie   Opaque value forwarded to @p callback on every call.
 * @return The image that caused @p callback to return @c true, or @c NULL if
 *         the table was fully traversed.
 */
struct image*
image_iterate_through_images(image_iterator_callback callback, void* cookie)
{
	MutexLocker locker(sImageMutex);

	ImageTable::Iterator it = sImageTable->GetIterator();
	struct image* image = NULL;
	while ((image = it.Next()) != NULL) {
		if (callback(image, cookie))
			break;
	}

	return image;
}


/**
 * @brief Iterate over the images of a specific team.
 *
 * Acquires a reference to the team identified by @p teamID, then walks its
 * image list calling @p callback for each entry until the callback returns
 * @c true or the list is exhausted.
 *
 * @param teamID   ID of the team whose images are to be iterated.
 * @param callback Function called for each image; return @c true to stop early.
 * @param cookie   Opaque value forwarded to @p callback on every call.
 * @return The image that caused @p callback to return @c true, or @c NULL if
 *         @p teamID was invalid or the list was fully traversed.
 */
struct image*
image_iterate_through_team_images(team_id teamID,
	image_iterator_callback callback, void* cookie)
{
	// get the team
	Team* team = Team::Get(teamID);
	if (team == NULL)
		return NULL;
	BReference<Team> teamReference(team, true);

	// iterate through the team's images
	MutexLocker imageLocker(sImageMutex);

	struct image *image = NULL;
	for (image = team->image_list.First();
			image != NULL; image = team->image_list.GetNext(image)) {
		if (callback(image, cookie))
			break;
	}

	return image;
}


static void
notify_loading_app(status_t result, bool suspend)
{
	Team* team = thread_get_current_thread()->team;

	TeamLocker teamLocker(team);

	if (team->loading_info != NULL) {
		// there's indeed someone waiting

		thread_prepare_suspend();

		// wake up the waiting thread
		team->loading_info->result = result;
		team->loading_info->condition.NotifyAll();
		team->loading_info = NULL;

		// we're done with the team stuff
		teamLocker.Unlock();

		// suspend ourselves, if desired
		if (suspend)
			thread_suspend(true);
	}
}


//	#pragma mark -
//	Functions exported for the user space


/**
 * @brief Syscall: unregister the image with the given ID from the calling team.
 *
 * Delegates to unregister_image() using the current thread's team.
 *
 * @param id The image_id to unregister.
 * @retval B_OK             The image was removed.
 * @retval B_ENTRY_NOT_FOUND No such image exists in the calling team.
 */
status_t
_user_unregister_image(image_id id)
{
	return unregister_image(thread_get_current_thread()->team, id);
}


/**
 * @brief Syscall: register a new image on behalf of the calling team.
 *
 * Copies the extended_image_info structure from user space and calls
 * register_image() for the current thread's team.
 *
 * @param userInfo User-space pointer to the extended_image_info to register.
 * @param size     Size of the structure; must equal sizeof(extended_image_info).
 * @return The new image_id on success.
 * @retval B_BAD_VALUE   @p size does not match the expected structure size.
 * @retval B_BAD_ADDRESS @p userInfo is not a valid user-space pointer.
 * @retval B_NO_MEMORY   Allocation of the kernel image record failed.
 */
image_id
_user_register_image(extended_image_info *userInfo, size_t size)
{
	extended_image_info info;

	if (size != sizeof(info))
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userInfo)
		|| user_memcpy(&info, userInfo, size) < B_OK)
		return B_BAD_ADDRESS;

	return register_image(thread_get_current_thread()->team, &info, size);
}


/**
 * @brief Syscall: notify the kernel that an image has been relocated.
 *
 * Called by the runtime loader once relocation of an image is complete.
 * Notifies the user debugger. If the image is the application image
 * (B_APP_IMAGE), also wakes up the thread waiting for the team to finish
 * loading and suspends the current thread until that thread is ready.
 *
 * @param id The image_id of the image that has been relocated.
 */
void
_user_image_relocated(image_id id)
{
	image_info info;
	status_t error;

	// get an image info
	error = _get_image_info(id, &info, sizeof(image_info));
	if (error != B_OK) {
		dprintf("_user_image_relocated(%" B_PRId32 "): Failed to get image "
			"info: %" B_PRIx32 "\n", id, error);
		return;
	}

	// notify the debugger
	user_debug_image_created(&info);

	// If the image is the app image, loading is done. We need to notify the
	// thread who initiated the process and is now waiting for us to be done.
	if (info.type == B_APP_IMAGE)
		notify_loading_app(B_OK, true);
}


/**
 * @brief Syscall: signal that loading the application image has failed.
 *
 * Called by the runtime loader when it cannot finish loading the application.
 * Ensures @p error carries a failure code, wakes the waiting loader thread
 * with the error, then calls _user_exit_team() to terminate the team.
 *
 * @param error The error code describing the failure; coerced to B_ERROR if
 *              a non-negative value is passed.
 */
void
_user_loading_app_failed(status_t error)
{
	if (error >= B_OK)
		error = B_ERROR;

	notify_loading_app(error, false);

	_user_exit_team(error);
}


/**
 * @brief Syscall: retrieve image information by image ID.
 *
 * Copies up to @p size bytes of image_info for the image identified by @p id
 * into the user-space buffer @p userInfo.
 *
 * @param id       The image_id to query.
 * @param userInfo User-space buffer that receives the image_info.
 * @param size     Number of bytes to copy; must be <= sizeof(image_info).
 * @retval B_OK             Information copied successfully.
 * @retval B_BAD_VALUE      @p size exceeds sizeof(image_info).
 * @retval B_BAD_ADDRESS    @p userInfo is not a valid user-space pointer.
 * @retval B_ENTRY_NOT_FOUND No image with @p id was found.
 */
status_t
_user_get_image_info(image_id id, image_info *userInfo, size_t size)
{
	image_info info;
	status_t status;

	if (size > sizeof(image_info))
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userInfo))
		return B_BAD_ADDRESS;

	status = _get_image_info(id, &info, sizeof(image_info));

	if (user_memcpy(userInfo, &info, size) < B_OK)
		return B_BAD_ADDRESS;

	return status;
}


/**
 * @brief Syscall: enumerate the images of a team, one per call.
 *
 * Uses a caller-maintained cookie to iterate over all images registered by
 * @p team. On each call the cookie is advanced; callers should initialise it
 * to 0 before the first call and stop when B_ENTRY_NOT_FOUND is returned.
 *
 * @param team     The team whose image list is to be enumerated.
 * @param _cookie  User-space in/out cookie (positional index, start at 0).
 * @param userInfo User-space buffer that receives the image_info.
 * @param size     Number of bytes to copy; must be <= sizeof(image_info).
 * @retval B_OK             The next image info was copied successfully.
 * @retval B_BAD_VALUE      @p size exceeds sizeof(image_info).
 * @retval B_BAD_ADDRESS    A pointer argument is not a valid user-space address.
 * @retval B_BAD_TEAM_ID    @p team does not refer to a valid team.
 * @retval B_ENTRY_NOT_FOUND No more images; the list has been fully enumerated.
 */
status_t
_user_get_next_image_info(team_id team, int32 *_cookie, image_info *userInfo,
	size_t size)
{
	image_info info;
	status_t status;
	int32 cookie;

	if (size > sizeof(image_info))
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userInfo) || !IS_USER_ADDRESS(_cookie)
		|| user_memcpy(&cookie, _cookie, sizeof(int32)) < B_OK) {
		return B_BAD_ADDRESS;
	}

	status = _get_next_image_info(team, &cookie, &info, sizeof(image_info));

	if (user_memcpy(userInfo, &info, size) < B_OK
		|| user_memcpy(_cookie, &cookie, sizeof(int32)) < B_OK) {
		return B_BAD_ADDRESS;
	}

	return status;
}
