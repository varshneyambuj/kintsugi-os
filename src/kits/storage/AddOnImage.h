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
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License.
 */

/** @file AddOnImage.h
    @brief Thin RAII wrapper around a loaded add-on image_id. */

#ifndef _ADD_ON_IMAGE_H
#define _ADD_ON_IMAGE_H

#include <image.h>

namespace BPrivate {

/** @brief Loads, owns, and unloads a single add-on shared image. */
class AddOnImage {
public:
	AddOnImage();
	~AddOnImage();

	status_t Load(const char* path);
	void Unload();

	void SetID(image_id id);
	image_id ID() const	{ return fID; }

private:
	image_id	fID;
};

}	// namespace BPrivate

using BPrivate::AddOnImage;

#endif	// _ADD_ON_IMAGE_H
