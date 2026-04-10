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
 *   Copyright 2002-2008, Haiku Inc.
 *   Authors:
 *       Tyler Dauwalder
 *       Ingo Weinhold, bonefish@users.sf.net
 *       Axel Dörfler, axeld@pinc-software.de
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file Mime.cpp
 * @brief Implementation of MIME utility functions for the Storage Kit.
 *
 * This file provides free functions for updating MIME information on
 * filesystem entries, creating application meta-MIME records, and
 * retrieving icons associated with mounted devices or named icon resources.
 * Icon retrieval supports legacy B_CMAP8 bitmaps, vector icons stored via
 * ioctl, and named icon files located in well-known data directories.
 *
 * @see BMimeType, BIconUtils
 */

#include <errno.h>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <AutoDeleter.h>
#include <Bitmap.h>
#include <Drivers.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <fs_attr.h>
#include <fs_info.h>
#include <IconUtils.h>
#include <Mime.h>
#include <MimeType.h>
#include <Node.h>
#include <Path.h>
#include <RegistrarDefs.h>
#include <Roster.h>
#include <RosterPrivate.h>


using namespace BPrivate;


/**
 * @brief Internal helper that contacts the registrar to perform a MIME
 *        update operation on a directory tree.
 *
 * Constructs and sends a BMessage of type \a what to the registrar,
 * passing the target path (or "/" when \a path is NULL), the recursion
 * flag, the synchronous flag, and the force flag.
 *
 * @param what        The registrar message constant identifying the operation.
 * @param path        The filesystem path to operate on, or NULL for "/".
 * @param recursive   Non-zero to recurse into subdirectories.
 * @param synchronous Non-zero to wait for completion before returning.
 * @param force       Non-zero to force re-processing even if up-to-date.
 * @return B_OK on success, or an error code on failure.
 */
status_t
do_mime_update(int32 what, const char* path, int recursive,
	int synchronous, int force)
{
	BEntry root;
	entry_ref ref;

	status_t err = root.SetTo(path ? path : "/");
	if (!err)
		err = root.GetRef(&ref);
	if (!err) {
		BMessage msg(what);
		BMessage reply;
		status_t result;

		// Build and send the message, read the reply
		if (!err)
			err = msg.AddRef("entry", &ref);
		if (!err)
			err = msg.AddBool("recursive", recursive);
		if (!err)
			err = msg.AddBool("synchronous", synchronous);
		if (!err)
			err = msg.AddInt32("force", force);
		if (!err)
			err = BRoster::Private().SendTo(&msg, &reply, true);
		if (!err)
			err = reply.what == B_REG_RESULT ? B_OK : B_BAD_VALUE;
		if (!err)
			err = reply.FindInt32("result", &result);
		if (!err)
			err = result;
	}
	return err;
}


/**
 * @brief Updates the MIME information (i.e. MIME type) for one or more files.
 *
 * When \a path is NULL, recursion is forced so the entire filesystem is
 * scanned.
 *
 * @param path        Path to the file or directory to update, or NULL for
 *                    the entire filesystem.
 * @param recursive   Non-zero to recurse into subdirectories.
 * @param synchronous Non-zero to block until the update completes.
 * @param force       Non-zero to force re-processing of already-typed files.
 * @return B_OK on success, or an error code on failure.
 */
int
update_mime_info(const char* path, int recursive, int synchronous, int force)
{
	// Force recursion when given a NULL path
	if (!path)
		recursive = true;

	return do_mime_update(B_REG_MIME_UPDATE_MIME_INFO, path, recursive,
		synchronous, force);
}


/**
 * @brief Creates a MIME database entry for one or more applications.
 *
 * When \a path is NULL, recursion is forced so all applications on the
 * filesystem are processed.
 *
 * @param path        Path to the application or directory, or NULL for all.
 * @param recursive   Non-zero to recurse into subdirectories.
 * @param synchronous Non-zero to block until the operation completes.
 * @param force       Non-zero to overwrite existing database entries.
 * @return B_OK on success, or an error code on failure.
 */
status_t
create_app_meta_mime(const char* path, int recursive, int synchronous,
	int force)
{
	// Force recursion when given a NULL path
	if (!path)
		recursive = true;

	return do_mime_update(B_REG_MIME_CREATE_APP_META_MIME, path, recursive,
		synchronous, force);
}


/**
 * @brief Retrieves a legacy (B_CMAP8) bitmap icon associated with a device,
 *        falling back to a vector icon if the legacy ioctl fails.
 *
 * @param device The path to the device (e.g. "/dev/disk/...").
 * @param icon   Caller-allocated buffer of the appropriate size to receive
 *               the icon pixel data.
 * @param size   The requested icon size: B_LARGE_ICON (32) or B_MINI_ICON (16).
 * @return B_OK on success, B_BAD_VALUE if parameters are invalid, or an
 *         error code on failure.
 */
status_t
get_device_icon(const char* device, void* icon, int32 size)
{
	if (device == NULL || icon == NULL
		|| (size != B_LARGE_ICON && size != B_MINI_ICON))
		return B_BAD_VALUE;

	int fd = open(device, O_RDONLY);
	if (fd < 0)
		return errno;

	// ToDo: The mounted directories for volumes can also have META:X:STD_ICON
	// attributes. Should those attributes override the icon returned by
	// ioctl(,B_GET_ICON,)?
	device_icon iconData = {size, icon};
	if (ioctl(fd, B_GET_ICON, &iconData, sizeof(device_icon)) != 0) {
		// legacy icon was not available, try vector icon
		close(fd);

		uint8* data;
		size_t dataSize;
		type_code type;
		status_t status = get_device_icon(device, &data, &dataSize, &type);
		if (status == B_OK) {
			BBitmap* icon32 = new(std::nothrow) BBitmap(
				BRect(0, 0, size - 1, size - 1), B_BITMAP_NO_SERVER_LINK,
				B_RGBA32);
			BBitmap* icon8 = new(std::nothrow) BBitmap(
				BRect(0, 0, size - 1, size - 1), B_BITMAP_NO_SERVER_LINK,
				B_CMAP8);

			ArrayDeleter<uint8> dataDeleter(data);
			ObjectDeleter<BBitmap> icon32Deleter(icon32);
			ObjectDeleter<BBitmap> icon8Deleter(icon8);

			if (icon32 == NULL || icon32->InitCheck() != B_OK || icon8 == NULL
				|| icon8->InitCheck() != B_OK) {
				return B_NO_MEMORY;
			}

			status = BIconUtils::GetVectorIcon(data, dataSize, icon32);
			if (status == B_OK)
				status = BIconUtils::ConvertToCMAP8(icon32, icon8);
			if (status == B_OK)
				memcpy(icon, icon8->Bits(), icon8->BitsLength());

			return status;
		}
		return errno;
	}

	close(fd);
	return B_OK;
}


/**
 * @brief Retrieves an icon associated with a device into a BBitmap,
 *        preferring a vector icon and falling back to a legacy bitmap.
 *
 * @param device The path to the device.
 * @param icon   Pointer to the BBitmap that will receive the icon; must
 *               not be NULL.
 * @param which  The requested icon size (B_MINI_ICON or B_LARGE_ICON).
 * @return B_OK on success, B_BAD_VALUE if \a device or \a icon is NULL,
 *         B_NO_MEMORY on allocation failure, or another error code.
 */
status_t
get_device_icon(const char* device, BBitmap* icon, icon_size which)
{
	// check parameters
	if (device == NULL || icon == NULL)
		return B_BAD_VALUE;

	uint8* data;
	size_t size;
	type_code type;
	status_t status = get_device_icon(device, &data, &size, &type);
	if (status == B_OK) {
		status = BIconUtils::GetVectorIcon(data, size, icon);
		delete[] data;
		return status;
	}

	// Vector icon was not available, try old one

	BRect rect;
	if (which == B_MINI_ICON)
		rect.Set(0, 0, 15, 15);
	else if (which == B_LARGE_ICON)
		rect.Set(0, 0, 31, 31);

	BBitmap* bitmap = icon;
	int32 iconSize = which;

	if (icon->ColorSpace() != B_CMAP8
		|| (which != B_MINI_ICON && which != B_LARGE_ICON)) {
		if (which < B_LARGE_ICON)
			iconSize = B_MINI_ICON;
		else
			iconSize = B_LARGE_ICON;

		bitmap = new(std::nothrow) BBitmap(
			BRect(0, 0, iconSize - 1, iconSize -1), B_BITMAP_NO_SERVER_LINK,
			B_CMAP8);
		if (bitmap == NULL || bitmap->InitCheck() != B_OK) {
			delete bitmap;
			return B_NO_MEMORY;
		}
	}

	// get the icon, convert temporary data into bitmap if necessary
	status = get_device_icon(device, bitmap->Bits(), iconSize);
	if (status == B_OK && icon != bitmap)
		status = BIconUtils::ConvertFromCMAP8(bitmap, icon);

	if (icon != bitmap)
		delete bitmap;

	return status;
}


/**
 * @brief Retrieves the raw icon data associated with a device as a newly
 *        allocated byte buffer.
 *
 * Attempts to find the icon by name first (via B_GET_ICON_NAME ioctl), then
 * falls back to fetching the vector icon directly (via B_GET_VECTOR_ICON
 * ioctl). The caller is responsible for freeing the returned buffer with
 * delete[].
 *
 * @param device The path to the device.
 * @param _data  Set to a newly allocated buffer containing the icon data.
 * @param _size  Set to the size of the icon data in bytes.
 * @param _type  Set to the type code of the icon data (e.g. B_VECTOR_ICON_TYPE).
 * @return B_OK on success, B_BAD_VALUE if any parameter is NULL,
 *         B_NO_MEMORY on allocation failure, or an error code on failure.
 */
status_t
get_device_icon(const char* device, uint8** _data, size_t* _size,
	type_code* _type)
{
	if (device == NULL || _data == NULL || _size == NULL || _type == NULL)
		return B_BAD_VALUE;

	int fd = open(device, O_RDONLY);
	if (fd < 0)
		return errno;

	// Try to get the icon by name first

	char name[B_FILE_NAME_LENGTH];
	if (ioctl(fd, B_GET_ICON_NAME, name, sizeof(name)) >= 0) {
		status_t status = get_named_icon(name, _data, _size, _type);
		if (status == B_OK) {
			close(fd);
			return B_OK;
		}
	}

	// Getting the named icon failed, try vector icon next

	// NOTE: The actual icon size is unknown as of yet. After the first call
	// to B_GET_VECTOR_ICON, the actual size is known and the final buffer
	// is allocated with the correct size. If the buffer needed to be
	// larger, then the temporary buffer above will not yet contain the
	// valid icon data. In that case, a second call to B_GET_VECTOR_ICON
	// retrieves it into the final buffer.
	uint8 data[8192];
	device_icon iconData = {sizeof(data), data};
	status_t status = ioctl(fd, B_GET_VECTOR_ICON, &iconData,
		sizeof(device_icon));
	if (status != 0)
		status = errno;

	if (status == B_OK) {
		*_data = new(std::nothrow) uint8[iconData.icon_size];
		if (*_data == NULL)
			status = B_NO_MEMORY;
	}

	if (status == B_OK) {
		if (iconData.icon_size > (int32)sizeof(data)) {
			// the stack buffer does not contain the data, see NOTE above
			iconData.icon_data = *_data;
			status = ioctl(fd, B_GET_VECTOR_ICON, &iconData,
				sizeof(device_icon));
			if (status != 0)
				status = errno;
		} else
			memcpy(*_data, data, iconData.icon_size);

		*_size = iconData.icon_size;
		*_type = B_VECTOR_ICON_TYPE;
	}

	// TODO: also support getting the old icon?
	close(fd);
	return status;
}


/**
 * @brief Retrieves a named icon into a BBitmap by searching well-known data
 *        directories for a matching icon file.
 *
 * Searches B_USER_NONPACKAGED_DATA_DIRECTORY, B_USER_DATA_DIRECTORY,
 * B_SYSTEM_NONPACKAGED_DATA_DIRECTORY, and B_SYSTEM_DATA_DIRECTORY (in that
 * order) under an "icons" subdirectory for a file named \a name.
 *
 * @param name  The icon name (filename within an "icons" subdirectory).
 * @param icon  Pointer to the BBitmap that will receive the rendered icon.
 * @param which The icon size (B_MINI_ICON or B_LARGE_ICON).
 * @return B_OK on success, B_BAD_VALUE if parameters are invalid or the
 *         bitmap bounds do not match the requested size, or an error code
 *         on failure.
 */
status_t
get_named_icon(const char* name, BBitmap* icon, icon_size which)
{
	// check parameters
	if (name == NULL || icon == NULL)
		return B_BAD_VALUE;

	BRect rect;
	if (which == B_MINI_ICON)
		rect.Set(0, 0, 15, 15);
	else if (which == B_LARGE_ICON)
		rect.Set(0, 0, 31, 31);
	else
		return B_BAD_VALUE;

	if (icon->Bounds() != rect)
		return B_BAD_VALUE;

	uint8* data;
	size_t size;
	type_code type;
	status_t status = get_named_icon(name, &data, &size, &type);
	if (status == B_OK) {
		status = BIconUtils::GetVectorIcon(data, size, icon);
		delete[] data;
	}

	return status;
}


/**
 * @brief Retrieves the raw data of a named icon as a newly allocated byte
 *        buffer.
 *
 * Searches the standard data directories under "icons/" for a file named
 * \a name. The caller is responsible for freeing the returned buffer with
 * delete[].
 *
 * @param name  The icon file name (relative to each "icons/" directory).
 * @param _data Set to a newly allocated buffer containing the icon data.
 * @param _size Set to the size of the icon data in bytes.
 * @param _type Set to the type code of the icon data (B_VECTOR_ICON_TYPE).
 * @return B_OK on success, B_BAD_VALUE if any parameter is NULL,
 *         B_ENTRY_NOT_FOUND if no matching icon file is found,
 *         B_NO_MEMORY on allocation failure, or another error code.
 */
status_t
get_named_icon(const char* name, uint8** _data, size_t* _size, type_code* _type)
{
	if (name == NULL || _data == NULL || _size == NULL || _type == NULL)
		return B_BAD_VALUE;

	directory_which kWhich[] = {
		B_USER_NONPACKAGED_DATA_DIRECTORY,
		B_USER_DATA_DIRECTORY,
		B_SYSTEM_NONPACKAGED_DATA_DIRECTORY,
		B_SYSTEM_DATA_DIRECTORY,
	};

	status_t status = B_ENTRY_NOT_FOUND;
	BFile file;
	off_t size;

	for (uint32 i = 0; i < sizeof(kWhich) / sizeof(kWhich[0]); i++) {
		BPath path;
		if (find_directory(kWhich[i], &path) != B_OK)
			continue;

		path.Append("icons");
		path.Append(name);

		status = file.SetTo(path.Path(), B_READ_ONLY);
		if (status == B_OK) {
			status = file.GetSize(&size);
			if (size > 1024 * 1024)
				status = B_ERROR;
		}
		if (status == B_OK)
			break;
	}

	if (status != B_OK)
		return status;

	*_data = new(std::nothrow) uint8[size];
	if (*_data == NULL)
		return B_NO_MEMORY;

	if (file.Read(*_data, size) != size) {
		delete[] *_data;
		return B_ERROR;
	}

	*_size = size;
	*_type = B_VECTOR_ICON_TYPE;
		// TODO: for now

	return B_OK;
}
