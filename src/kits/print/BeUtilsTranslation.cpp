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
 *   Copyright (c) 2001, 2002 Haiku Project
 *   Authors: Ithamar R. Adema, Michael Pfeiffer
 *   Licensed under the MIT License.
 */


/**
 * @file BeUtilsTranslation.cpp
 * @brief Bitmap loading and BPicture conversion helpers for the print kit.
 *
 * Supplements BeUtils.cpp with routines that rely on the Translation Kit.
 * LoadBitmap() can source images either from the Translation Kit directly or
 * from flattened BMessage resources embedded in the application binary.
 * The BitmapToPicture() family converts BBitmap objects into BPicture recordings
 * suitable for use with the BPicture-based print pipeline.
 *
 * @see BeUtils.cpp, PictureIterator
 */


#include <Application.h>
#include <Bitmap.h>
#include <Messenger.h>
#include <Resources.h>
#include <Roster.h>
#include <String.h>

#include "BeUtils.h"

/**
 * @brief Load a BBitmap from application resources or the Translation Kit.
 *
 * When \a type_code is B_TRANSLATOR_BITMAP the image is fetched via
 * BTranslationUtils::GetBitmap(). Otherwise the function searches the
 * application's resource fork for a resource whose type matches \a type_code
 * and whose name matches \a name, unflattens it as a BMessage, and
 * instantiates a BBitmap from that archive.
 *
 * @param name       Name of the resource or translation source to load.
 * @param type_code  Type constant identifying the bitmap encoding.
 * @return A newly allocated BBitmap on success, or NULL on failure.
 *         The caller is responsible for deleting the returned object.
 */
BBitmap* LoadBitmap(const char* name, uint32 type_code) {
	if (type_code == B_TRANSLATOR_BITMAP) {
		return BTranslationUtils::GetBitmap(type_code, name);
	} else {
		BResources *res = BApplication::AppResources();
		if (res != NULL) {
			BMessage m;
			size_t length;
			const void *bits = res->LoadResource(type_code, name, &length);
			if (bits && m.Unflatten((char*)bits) == B_OK) {
				return (BBitmap*)BBitmap::Instantiate(&m);
			}
		}
		return NULL;
	}
}

/**
 * @brief Record a BBitmap's drawing operations into a BPicture.
 *
 * Opens a new BPicture recording on \a view, draws \a bitmap, then closes
 * and returns the recording. The caller takes ownership of the returned
 * BPicture object.
 *
 * @param view    The BView used as the drawing surface for recording.
 * @param bitmap  The source bitmap to record; may be NULL, in which case
 *                NULL is returned immediately.
 * @return A new BPicture containing the bitmap draw commands, or NULL if
 *         \a bitmap is NULL.
 */
BPicture *BitmapToPicture(BView* view, BBitmap *bitmap) {
	if (bitmap) {
		view->BeginPicture(new BPicture());
		view->DrawBitmap(bitmap);
		return view->EndPicture();
	}
	return NULL;
}

/**
 * @brief Record a BBitmap followed by a translucent white overlay into a BPicture.
 *
 * Produces a "grayed-out" appearance by drawing \a bitmap and then compositing
 * a 50%-opaque white rectangle over it using B_OP_ALPHA drawing mode. The
 * resulting BPicture is returned to the caller.
 *
 * @param view    The BView used as the drawing surface for recording.
 * @param bitmap  The source bitmap to gray; may be NULL, in which case
 *                NULL is returned immediately.
 * @return A new BPicture containing the grayed bitmap draw commands, or NULL
 *         if \a bitmap is NULL.
 */
BPicture *BitmapToGrayedPicture(BView* view, BBitmap *bitmap) {
	if (bitmap) {
		BRect rect(bitmap->Bounds());
		view->BeginPicture(new BPicture());
		view->DrawBitmap(bitmap);
		view->SetHighColor(255, 255, 255, 128);
		view->SetDrawingMode(B_OP_ALPHA);
		view->FillRect(rect);
		return view->EndPicture();
	}
	return NULL;
}
