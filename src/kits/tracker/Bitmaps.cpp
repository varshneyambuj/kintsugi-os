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
 *   Open Tracker License
 *
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of
 *   this software and associated documentation files (the "Software"), to deal in
 *   the Software without restriction, including without limitation the rights to
 *   use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *   of the Software, and to permit persons to whom the Software is furnished to do
 *   so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice applies to all licensees
 *   and shall be included in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *   BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 *   AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
 *   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *   Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered
 *   trademarks of Be Incorporated in the United States and other countries.
 *   All rights reserved.
 */


/**
 * @file Bitmaps.cpp
 * @brief Tracker resource management for icons and bitmaps embedded in the Tracker image.
 *
 * BImageResources wraps BResources opened on the running Tracker image file,
 * providing thread-safe access to icon data (vector, MICN, ICON) and archived
 * BBitmap objects. GetTrackerResources() returns the process-wide singleton.
 *
 * @see BImageResources, BResources, BBitmap
 */

#include "Bitmaps.h"
#include "Utilities.h"

#include <Autolock.h>
#include <Bitmap.h>
#include <Debug.h>
#include <DataIO.h>
#include <File.h>
#include <IconUtils.h>
#include <String.h>
#include <SupportDefs.h>


//	#pragma mark - BImageResources


/**
 * @brief Construct by opening the resource fork of the image that contains @a memAddr.
 *
 * Calls find_image() to locate the loaded image whose text or data segment
 * contains @a memAddr, then opens the corresponding BResources from the image file.
 *
 * @param memAddr  Any address known to reside in the target image.
 */
BImageResources::BImageResources(void* memAddr)
{
	image_id image = find_image(memAddr);
	image_info info;
	if (get_image_info(image, &info) == B_OK) {
#if _SUPPORTS_RESOURCES
		BFile file(&info.name[0], B_READ_ONLY);
#else
		BString name(&info.name[0]);
		name += ".rsrc";
		BFile file(name.String(), B_READ_ONLY);
#endif
		if (file.InitCheck() == B_OK)
			fResources.SetTo(&file);
	}
}


/**
 * @brief Destructor.
 */
BImageResources::~BImageResources()
{
}


/**
 * @brief Acquire the internal lock and return a read-only pointer to the BResources.
 *
 * The caller must call FinishResources() to release the lock.
 *
 * @return Pointer to the BResources object, or NULL if the lock fails.
 */
const BResources*
BImageResources::ViewResources() const
{
	if (fLock.Lock() != B_OK)
		return NULL;

	return &fResources;
}


/**
 * @brief Acquire the internal lock and return a mutable pointer to the BResources.
 *
 * The caller must call FinishResources() to release the lock.
 *
 * @return Pointer to the BResources object, or NULL if the lock fails.
 */
BResources*
BImageResources::ViewResources()
{
	if (fLock.Lock() != B_OK)
		return NULL;

	return &fResources;
}


/**
 * @brief Release the lock acquired by ViewResources().
 *
 * @param res  Must equal the BResources pointer returned by ViewResources().
 * @return B_OK on success, B_BAD_VALUE if @a res does not match.
 */
status_t
BImageResources::FinishResources(BResources* res) const
{
	ASSERT(res == &fResources);
	if (res != &fResources)
		return B_BAD_VALUE;

	fLock.Unlock();

	return B_OK;
}


/**
 * @brief Load a resource by type and numeric ID in a thread-safe manner.
 *
 * @param type      Resource type code.
 * @param id        Numeric resource ID.
 * @param out_size  Receives the size of the resource data in bytes.
 * @return Pointer to the resource data (valid until the BResources object is destroyed),
 *         or NULL if not found or if the lock cannot be acquired.
 */
const void*
BImageResources::LoadResource(type_code type, int32 id,
	size_t* out_size) const
{
	// Serialize execution.
	// Looks like BResources is not really thread safe. We should
	// clean that up in the future and remove the locking from here.
	BAutolock lock(fLock);
	if (!lock.IsLocked())
		return 0;

	// Return the resource.  Because we never change the BResources
	// object, the returned data will not change until TTracker is
	// destroyed.
	return const_cast<BResources*>(&fResources)->LoadResource(type, id,
		out_size);
}


/**
 * @brief Load a resource by type and string name in a thread-safe manner.
 *
 * @param type      Resource type code.
 * @param name      Resource name string.
 * @param out_size  Receives the size of the resource data in bytes.
 * @return Pointer to the resource data, or NULL if not found.
 */
const void*
BImageResources::LoadResource(type_code type, const char* name,
	size_t* out_size) const
{
	// Serialize execution.
	BAutolock lock(fLock);
	if (!lock.IsLocked())
		return NULL;

	// Return the resource.  Because we never change the BResources
	// object, the returned data will not change until TTracker is
	// destroyed.
	return const_cast<BResources*>(&fResources)->LoadResource(type, name,
		out_size);
}


/**
 * @brief Load an icon resource into @a dest, trying vector format before legacy ICON/MICN.
 *
 * Tries B_VECTOR_ICON_TYPE first; on failure falls back to the R5-style ICON (32x32)
 * or MICN (16x16) resource and converts from B_CMAP8.
 *
 * @param id    Numeric resource ID for the icon.
 * @param size  Requested icon size (B_MINI_ICON or B_LARGE_ICON).
 * @param dest  Destination BBitmap (must already be allocated with the correct size).
 * @return B_OK on success, B_ERROR if the resource is absent or conversion fails.
 */
status_t
BImageResources::GetIconResource(int32 id, icon_size size,
	BBitmap* dest) const
{
	size_t length = 0;
	const void* data;

	// try to load vector icon
	data = LoadResource(B_VECTOR_ICON_TYPE, id, &length);
	if (data != NULL
		&& BIconUtils::GetVectorIcon((uint8*)data, length, dest) == B_OK) {
		return B_OK;
	}

	// fall back to R5 icon
	length = 0;
	size = (size == B_MINI_ICON ? B_MINI_ICON : B_LARGE_ICON);

	data = LoadResource(size == B_MINI_ICON ? 'MICN' : 'ICON', id, &length);
	if (data == NULL || length != (size_t)(size * size)) {
		return B_ERROR;
	}

	if (dest->ColorSpace() == B_RGBA32) {
		// fill with transparent
		uint8* destBits = (uint8*)dest->Bits();
		int32 i = 0;
		while (i < dest->BitsLength()) {
			destBits[i++] = B_TRANSPARENT_32_BIT.red;
			destBits[i++] = B_TRANSPARENT_32_BIT.green;
			destBits[i++] = B_TRANSPARENT_32_BIT.blue;
			destBits[i++] = B_TRANSPARENT_32_BIT.alpha;
		}

		// scale and convert from B_CMAP8 to B_RGBA32
		if (BIconUtils::ConvertFromCMAP8((uint8*)data, size, size, size,
				dest) == B_OK) {
			return B_OK;
		}
	} else { // assume B_CMAP8
		// fill with transparent
		uint8* destBits = (uint8*)dest->Bits();
		for (int32 i = 0; i < dest->BitsLength(); i++)
			destBits[i] = B_TRANSPARENT_MAGIC_CMAP8;
	}

	// import bits into the middle of dest without scaling
	// color space is converted from B_CMAP8 to B_RGBA32
	float x = roundf((dest->Bounds().Width() - size) / 2);
	float y = roundf((dest->Bounds().Height() - size) / 2);
	return dest->ImportBits(data, (int32)length, size, B_CMAP8,
		BPoint(0, 0), BPoint(x, y), BSize(size - 1, size - 1));
}


/**
 * @brief Return a direct pointer to the raw vector icon data for the given resource ID.
 *
 * @param id        Numeric resource ID.
 * @param iconData  Receives a pointer to the raw HVIF byte stream.
 * @param iconSize  Receives the number of bytes in the HVIF stream.
 * @return B_OK on success, B_ERROR if the resource is absent.
 */
status_t
BImageResources::GetIconResource(int32 id, const uint8** iconData,
	size_t* iconSize) const
{
	// try to load vector icon data from resources
	size_t length = 0;
	const void* data = LoadResource(B_VECTOR_ICON_TYPE, id, &length);
	if (data == NULL)
		return B_ERROR;

	*iconData = (const uint8*)data;
	*iconSize = length;

	return B_OK;
}


/**
 * @brief Walk the loaded-image list to find the image that owns @a memAddr.
 *
 * @param memAddr  Address to search for within any loaded image's text or data segment.
 * @return The image_id of the matching image, or -1 if not found.
 */
image_id
BImageResources::find_image(void* memAddr) const
{
	image_info info;
	int32 cookie = 0;
	while (get_next_image_info(0, &cookie, &info) == B_OK) {
		if ((info.text <= memAddr
			&& (((uint8*)info.text)+info.text_size) > memAddr)
				|| (info.data <= memAddr
				&& (((uint8*)info.data)+info.data_size) > memAddr)) {
			// Found the image.
			return info.id;
		}
	}

	return -1;
}


/**
 * @brief Unarchive a BBitmap from a flattened BMessage resource.
 *
 * @param type  Resource type code.
 * @param id    Numeric resource ID.
 * @param out   Receives a heap-allocated BBitmap; caller takes ownership.
 * @return B_OK on success, or an error code if the resource is missing or malformed.
 */
status_t
BImageResources::GetBitmapResource(type_code type, int32 id,
	BBitmap** out) const
{
	*out = NULL;

	size_t len = 0;
	const void* data = LoadResource(type, id, &len);

	if (data == NULL) {
		TRESPASS();
		return B_ERROR;
	}

	BMemoryIO stream(data, len);

	// Try to read as an archived bitmap.
	stream.Seek(0, SEEK_SET);
	BMessage archive;
	status_t result = archive.Unflatten(&stream);
	if (result != B_OK)
		return result;

	*out = new BBitmap(&archive);
	if (*out == NULL)
		return B_ERROR;

	result = (*out)->InitCheck();
	if (result != B_OK) {
		delete *out;
		*out = NULL;
	}

	return result;
}


static BLocker resLock;
static BImageResources* resources = NULL;

// This class is used as a static instance to delete the resources
// global object when the image is getting unloaded.
class _TTrackerCleanupResources {
public:
	_TTrackerCleanupResources()
	{
	}

	~_TTrackerCleanupResources()
	{
		delete resources;
		resources = NULL;
	}
};


namespace BPrivate {

static _TTrackerCleanupResources CleanupResources;

/**
 * @brief Return the process-wide BImageResources singleton for the Tracker image.
 *
 * Creates the singleton on first call (with locking). The global is destroyed
 * when the Tracker image is unloaded via the _TTrackerCleanupResources destructor.
 *
 * @return Pointer to the singleton BImageResources instance.
 */
BImageResources* GetTrackerResources()
{
	if (!resources) {
		BAutolock lock(&resLock);
		resources = new BImageResources(&resources);
	}

	return resources;
}

} // namespace BPrivate
