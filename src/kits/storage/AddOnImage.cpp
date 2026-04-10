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
 *   Copyright Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file AddOnImage.cpp
 * @brief Implementation of AddOnImage, a RAII wrapper for add-on images.
 *
 * AddOnImage manages the lifecycle of a dynamically loaded add-on image
 * by wrapping the load_add_on() / unload_add_on() kernel calls. The image
 * is automatically unloaded when the object is destroyed.
 *
 * @see AddOnImage
 */

#include "AddOnImage.h"


/**
 * @brief Default constructor; initialises the object with no loaded image.
 */
AddOnImage::AddOnImage()
	: fID(-1)
{
}


/**
 * @brief Destructor. Unloads the image if one is currently loaded.
 */
AddOnImage::~AddOnImage()
{
	Unload();
}


/**
 * @brief Loads an add-on image from the given file path.
 *
 * Any previously loaded image is unloaded before the new one is loaded.
 *
 * @param path Absolute or relative path to the add-on file.
 * @return B_OK on success, B_BAD_VALUE if path is NULL, or a negative
 *         image_id error code if load_add_on() fails.
 */
status_t
AddOnImage::Load(const char* path)
{
	Unload();
	status_t error = (path ? B_OK : B_BAD_VALUE);
	if (error == B_OK) {
		image_id id = load_add_on(path);
		if (id >= 0)
			fID = id;
		else
			error = id;
	}
	return error;
}


/**
 * @brief Unloads the currently loaded add-on image, if any.
 *
 * After this call, the object is in its default (no image) state.
 */
void
AddOnImage::Unload()
{
	if (fID >= 0) {
		unload_add_on(fID);
		fID = -1;
	}
}


/**
 * @brief Adopts an already-loaded image_id, unloading any existing image first.
 *
 * If id is negative the call is a no-op (the existing image is unloaded but
 * the new id is not stored).
 *
 * @param id A valid image_id returned by a prior load_add_on() call, or a
 *           negative value to simply unload the current image.
 */
void
AddOnImage::SetID(image_id id)
{
	Unload();
	if (id >= 0)
		fID = id;
}
