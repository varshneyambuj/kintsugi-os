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
 *   Copyright 2006-2013 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus, superstippi@gmx.de
 *       Ingo Weinhold, ingo_weinhold@gmx.de
 */


/**
 * @file Icon.cpp
 * @brief Implementation of BIcon, a multi-resolution icon container
 *
 * BIcon stores an icon in both bitmap (BBitmap) and vector (BPicture-compatible)
 * formats, allowing applications to render icons at any size with appropriate
 * quality.
 *
 * @see BBitmap, BView
 */


#include <Icon.h>

#include <string.h>

#include <new>

#include <Bitmap.h>

#include <AutoDeleter.h>


namespace BPrivate {


/**
 * @brief Construct an empty BIcon with pre-allocated bitmap list capacity.
 *
 * Both the enabled and disabled bitmap lists are pre-allocated for eight
 * slots, covering the standard active, inactive, and partially-active states
 * in both enabled and disabled variants.
 */
BIcon::BIcon()
	:
	fEnabledBitmaps(8),
	fDisabledBitmaps(8)
{
}


/**
 * @brief Destroy the BIcon.
 *
 * The destructor relies on the BitmapList destructor to release the stored
 * BBitmap objects for both the enabled and disabled lists.
 */
BIcon::~BIcon()
{
}


/**
 * @brief Initialize the icon from a source BBitmap, generating all required variants.
 *
 * Validates the bitmap's color space, optionally trims transparent borders,
 * converts non-RGB32/RGBA32 bitmaps to RGBA32 for processing, then calls
 * _MakeBitmaps() to produce the active, inactive, disabled, and
 * partially-active variants as requested by @p flags.
 *
 * @param bitmap Source bitmap to build the icon from; must be valid.
 * @param flags  Combination of B_TRIM_ICON_BITMAP,
 *               B_TRIM_ICON_BITMAP_KEEP_ASPECT,
 *               B_CREATE_DISABLED_ICON_BITMAPS,
 *               B_CREATE_ACTIVE_ICON_BITMAP, and
 *               B_CREATE_PARTIALLY_ACTIVE_ICON_BITMAP flags.
 * @return B_OK on success, B_BAD_VALUE if the bitmap is invalid, or
 *         B_NO_MEMORY if an intermediate allocation fails.
 */
status_t
BIcon::SetTo(const BBitmap* bitmap, uint32 flags)
{
	if (!bitmap->IsValid())
		return B_BAD_VALUE;

	DeleteBitmaps();

	// check the color space
	bool hasAlpha = false;
	bool canUseForMakeBitmaps = false;

	switch (bitmap->ColorSpace()) {
		case B_RGBA32:
		case B_RGBA32_BIG:
			hasAlpha = true;
			// fall through
		case B_RGB32:
		case B_RGB32_BIG:
			canUseForMakeBitmaps = true;
			break;

		case B_UVLA32:
		case B_LABA32:
		case B_HSIA32:
		case B_HSVA32:
		case B_HLSA32:
		case B_CMYA32:
		case B_RGBA15:
		case B_RGBA15_BIG:
		case B_CMAP8:
			hasAlpha = true;
			break;

		default:
			break;
	}

	BBitmap* trimmedBitmap = NULL;

	// trim the bitmap, if requested and the bitmap actually has alpha
	status_t error;
	if ((flags & (B_TRIM_ICON_BITMAP | B_TRIM_ICON_BITMAP_KEEP_ASPECT)) != 0
		&& hasAlpha) {
		if (bitmap->ColorSpace() == B_RGBA32) {
			error = _TrimBitmap(bitmap,
				(flags & B_TRIM_ICON_BITMAP_KEEP_ASPECT) != 0, trimmedBitmap);
		} else {
			BBitmap* rgb32Bitmap = _ConvertToRGB32(bitmap, true);
			if (rgb32Bitmap != NULL) {
				error = _TrimBitmap(rgb32Bitmap,
					(flags & B_TRIM_ICON_BITMAP_KEEP_ASPECT) != 0,
					trimmedBitmap);
				delete rgb32Bitmap;
			} else
				error = B_NO_MEMORY;
		}

		if (error != B_OK)
			return error;

		bitmap = trimmedBitmap;
		canUseForMakeBitmaps = true;
	}

	// create the bitmaps
	if (canUseForMakeBitmaps) {
		error = _MakeBitmaps(bitmap, flags);
	} else {
		if (BBitmap* rgb32Bitmap = _ConvertToRGB32(bitmap, true)) {
			error = _MakeBitmaps(rgb32Bitmap, flags);
			delete rgb32Bitmap;
		} else
			error = B_NO_MEMORY;
	}

	delete trimmedBitmap;

	return error;
}


/**
 * @brief Store a BBitmap pointer at a specific slot in the enabled or disabled list.
 *
 * If the list is shorter than required, NULL entries are appended to pad it
 * before the bitmap is stored. An existing entry at @p which is replaced
 * without deleting the old pointer — the caller is responsible for the
 * lifetime of any bitmap being overwritten.
 *
 * @param bitmap The BBitmap to store; may be NULL to clear a slot.
 * @param which  The target slot index, optionally OR'd with
 *               B_DISABLED_ICON_BITMAP to select the disabled list.
 * @return true on success; false if a required list expansion fails.
 */
bool
BIcon::SetBitmap(BBitmap* bitmap, uint32 which)
{
	BitmapList& list = (which & B_DISABLED_ICON_BITMAP) == 0
		? fEnabledBitmaps : fDisabledBitmaps;
	which &= ~uint32(B_DISABLED_ICON_BITMAP);

	int32 count = list.CountItems();
	if ((int32)which < count) {
		list.ReplaceItem(which, bitmap);
		return true;
	}

	while (list.CountItems() < (int32)which) {
		if (!list.AddItem((BBitmap*)NULL))
			return false;
	}

	return list.AddItem(bitmap);
}


/**
 * @brief Retrieve the stored BBitmap for a given icon state slot.
 *
 * @param which The slot index, optionally OR'd with B_DISABLED_ICON_BITMAP.
 * @return The stored BBitmap pointer, or NULL if the slot is empty or
 *         out of range.
 */
BBitmap*
BIcon::Bitmap(uint32 which) const
{
	const BitmapList& list = (which & B_DISABLED_ICON_BITMAP) == 0
		? fEnabledBitmaps : fDisabledBitmaps;
	return list.ItemAt(which & ~uint32(B_DISABLED_ICON_BITMAP));
}


/**
 * @brief Allocate a new BBitmap, store it at the given slot, and return it.
 *
 * Creates a BBitmap with the specified bounds and color space, stores it via
 * SetBitmap(), and returns the pointer on success. On any failure the
 * partially constructed bitmap is deleted and NULL is returned.
 *
 * @param bounds     The pixel bounds of the new bitmap.
 * @param colorSpace The color space to use (e.g. B_RGBA32).
 * @param which      The target slot, optionally OR'd with B_DISABLED_ICON_BITMAP.
 * @return The new BBitmap on success, or NULL on allocation or storage failure.
 */
BBitmap*
BIcon::CreateBitmap(const BRect& bounds, color_space colorSpace, uint32 which)
{
	BBitmap* bitmap = new(std::nothrow) BBitmap(bounds, colorSpace);
	if (bitmap == NULL || !bitmap->IsValid() || !SetBitmap(bitmap, which)) {
		delete bitmap;
		return NULL;
	}

	return bitmap;
}


/**
 * @brief Store an externally owned BBitmap in the given slot, optionally converting it.
 *
 * If B_KEEP_ICON_BITMAP is set in @p flags the original pointer is stored
 * directly (the caller retains ownership); otherwise a converted RGBA32 copy
 * is made and stored (the icon then owns the copy).
 *
 * @param bitmap The source bitmap; passing NULL clears the slot.
 * @param which  The target slot, optionally OR'd with B_DISABLED_ICON_BITMAP.
 * @param flags  Pass B_KEEP_ICON_BITMAP to store the pointer without copying.
 * @return B_OK on success, B_BAD_VALUE if @p bitmap is non-NULL but invalid,
 *         or B_NO_MEMORY if conversion or storage fails.
 */
status_t
BIcon::SetExternalBitmap(const BBitmap* bitmap, uint32 which, uint32 flags)
{
	BBitmap* ourBitmap = NULL;
	if (bitmap != NULL) {
		if (!bitmap->IsValid())
			return B_BAD_VALUE;

		if ((flags & B_KEEP_ICON_BITMAP) != 0) {
			ourBitmap = const_cast<BBitmap*>(bitmap);
		} else {
			ourBitmap = _ConvertToRGB32(bitmap);
			if (ourBitmap == NULL)
				return B_NO_MEMORY;
		}
	}

	if (!SetBitmap(ourBitmap, which)) {
		if (ourBitmap != bitmap)
			delete ourBitmap;
		return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Clone a BBitmap into the given slot and return the clone.
 *
 * Constructs a new BBitmap by copy, stores it at @p which via SetBitmap(),
 * and returns the clone. On any failure the partially constructed clone is
 * deleted and NULL is returned.
 *
 * @param bitmapToClone The source bitmap to duplicate.
 * @param which         The target slot, optionally OR'd with B_DISABLED_ICON_BITMAP.
 * @return The cloned BBitmap on success, or NULL on failure.
 */
BBitmap*
BIcon::CopyBitmap(const BBitmap& bitmapToClone, uint32 which)
{
	BBitmap* bitmap = new(std::nothrow) BBitmap(bitmapToClone);
	if (bitmap == NULL || !bitmap->IsValid() || !SetBitmap(bitmap, which)) {
		delete bitmap;
		return NULL;
	}

	return bitmap;
}


/**
 * @brief Delete all stored bitmaps in both the enabled and disabled lists.
 *
 * After this call both lists are empty and all previously stored BBitmap
 * objects have been freed.
 */
void
BIcon::DeleteBitmaps()
{
	fEnabledBitmaps.MakeEmpty(true);
	fDisabledBitmaps.MakeEmpty(true);
}


/**
 * @brief Replace or clear the icon pointed to by @p _icon using a new source bitmap.
 *
 * If @p bitmap is NULL the existing icon is deleted and @p _icon is set to
 * NULL. Otherwise a new BIcon is allocated, initialized via SetTo(), and
 * assigned to @p _icon on success.
 *
 * @param bitmap Source bitmap, or NULL to clear the icon.
 * @param flags  Flags forwarded to BIcon::SetTo().
 * @param _icon  Reference to the BIcon pointer to update; the caller owns
 *               the resulting object.
 * @return B_OK on success, or B_NO_MEMORY if allocation fails.
 */
/*static*/ status_t
BIcon::UpdateIcon(const BBitmap* bitmap, uint32 flags, BIcon*& _icon)
{
	if (bitmap == NULL) {
		delete _icon;
		_icon = NULL;
		return B_OK;
	}

	BIcon* icon = new(std::nothrow) BIcon;
	if (icon == NULL)
		return B_NO_MEMORY;

	status_t error = icon->SetTo(bitmap, flags);
	if (error != B_OK) {
		delete icon;
		return error;
	}

	_icon = icon;
	return B_OK;
}


/**
 * @brief Set a single bitmap slot in an icon, creating the BIcon if necessary.
 *
 * If @p _icon is NULL and @p bitmap is non-NULL a new BIcon is created.
 * The bitmap is stored via SetExternalBitmap() without generating the full
 * set of state variants.
 *
 * @param bitmap Source bitmap for the slot, or NULL to clear the slot.
 * @param which  The target slot index, optionally OR'd with B_DISABLED_ICON_BITMAP.
 * @param flags  Flags forwarded to SetExternalBitmap() (e.g. B_KEEP_ICON_BITMAP).
 * @param _icon  Reference to the BIcon pointer to update; the caller owns the object.
 * @return B_OK on success, or B_NO_MEMORY if a new icon could not be allocated.
 */
/*static*/ status_t
BIcon::SetIconBitmap(const BBitmap* bitmap, uint32 which, uint32 flags,
	BIcon*& _icon)
{
	bool newIcon = false;
	if (_icon == NULL) {
		if (bitmap == NULL)
			return B_OK;

		_icon = new(std::nothrow) BIcon;
		if (_icon == NULL)
			return B_NO_MEMORY;
		newIcon = true;
	}

	status_t error = _icon->SetExternalBitmap(bitmap, which, flags);
	if (error != B_OK) {
		if (newIcon) {
			delete _icon;
			_icon = NULL;
		}
		return error;
	}

	return B_OK;
}


/**
 * @brief Convert an arbitrary BBitmap to B_RGBA32 format.
 *
 * Allocates a new BBitmap with the same bounds as @p bitmap in B_RGBA32
 * color space and imports the pixel data via ImportBits().
 *
 * @param bitmap            The source bitmap to convert; must be valid.
 * @param noAppServerLink   If true the new bitmap is created with
 *                          B_BITMAP_NO_SERVER_LINK, avoiding an app-server
 *                          round-trip (safe to use from constructors).
 * @return A newly allocated RGBA32 BBitmap on success, or NULL on failure.
 */
/*static*/ BBitmap*
BIcon::_ConvertToRGB32(const BBitmap* bitmap, bool noAppServerLink)
{
	BBitmap* rgb32Bitmap = new(std::nothrow) BBitmap(bitmap->Bounds(),
		noAppServerLink ? B_BITMAP_NO_SERVER_LINK : 0, B_RGBA32);
	if (rgb32Bitmap == NULL)
		return NULL;

	if (!rgb32Bitmap->IsValid() || rgb32Bitmap->ImportBits(bitmap)!= B_OK) {
		delete rgb32Bitmap;
		return NULL;
	}

	return rgb32Bitmap;
}


/**
 * @brief Trim fully-transparent border pixels from a B_RGBA32 bitmap.
 *
 * Scans every pixel's alpha channel to find the tightest bounding rectangle
 * that contains at least one non-transparent pixel, then copies that region
 * into a freshly allocated BBitmap.  If @p keepAspect is true the trim
 * rectangle is expanded symmetrically so that the original aspect ratio is
 * preserved.
 *
 * @param bitmap            The source bitmap; must be valid and in B_RGBA32
 *                          color space.
 * @param keepAspect        If true, apply the minimum inset uniformly on all
 *                          sides to maintain the original aspect ratio.
 * @param[out] _trimmedBitmap  Receives the newly allocated trimmed bitmap on
 *                             success; unchanged on failure.
 * @return B_OK on success, B_BAD_VALUE if @p bitmap is NULL, invalid, or not
 *         B_RGBA32, or if the bitmap is entirely transparent.
 *         B_NO_MEMORY if the output bitmap cannot be allocated.
 */
/*static*/ status_t
BIcon::_TrimBitmap(const BBitmap* bitmap, bool keepAspect,
	BBitmap*& _trimmedBitmap)
{
	if (bitmap == NULL || !bitmap->IsValid()
		|| bitmap->ColorSpace() != B_RGBA32) {
		return B_BAD_VALUE;
	}

	uint8* bits = (uint8*)bitmap->Bits();
	uint32 bpr = bitmap->BytesPerRow();
	uint32 width = bitmap->Bounds().IntegerWidth() + 1;
	uint32 height = bitmap->Bounds().IntegerHeight() + 1;
	BRect trimmed(INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN);

	for (uint32 y = 0; y < height; y++) {
		uint8* b = bits + 3;
		bool rowHasAlpha = false;
		for (uint32 x = 0; x < width; x++) {
			if (*b) {
				rowHasAlpha = true;
				if (x < trimmed.left)
					trimmed.left = x;
				if (x > trimmed.right)
					trimmed.right = x;
			}
			b += 4;
		}
		if (rowHasAlpha) {
			if (y < trimmed.top)
				trimmed.top = y;
			if (y > trimmed.bottom)
				trimmed.bottom = y;
		}
		bits += bpr;
	}

	if (!trimmed.IsValid())
		return B_BAD_VALUE;

	if (keepAspect) {
		float minInset = trimmed.left;
		minInset = min_c(minInset, trimmed.top);
		minInset = min_c(minInset, bitmap->Bounds().right - trimmed.right);
		minInset = min_c(minInset, bitmap->Bounds().bottom - trimmed.bottom);
		trimmed = bitmap->Bounds().InsetByCopy(minInset, minInset);
	}
	trimmed = trimmed & bitmap->Bounds();

	BBitmap* trimmedBitmap = new(std::nothrow) BBitmap(
		trimmed.OffsetToCopy(B_ORIGIN), B_BITMAP_NO_SERVER_LINK, B_RGBA32);
	if (trimmedBitmap == NULL)
		return B_NO_MEMORY;

	bits = (uint8*)bitmap->Bits();
	bits += 4 * (int32)trimmed.left + bpr * (int32)trimmed.top;
	uint8* dst = (uint8*)trimmedBitmap->Bits();
	uint32 trimmedWidth = trimmedBitmap->Bounds().IntegerWidth() + 1;
	uint32 trimmedHeight = trimmedBitmap->Bounds().IntegerHeight() + 1;
	uint32 trimmedBPR = trimmedBitmap->BytesPerRow();
	for (uint32 y = 0; y < trimmedHeight; y++) {
		memcpy(dst, bits, trimmedWidth * 4);
		dst += trimmedBPR;
		bits += bpr;
	}

	_trimmedBitmap = trimmedBitmap;
	return B_OK;
}


/**
 * @brief Generate all requested state bitmaps from a single source bitmap.
 *
 * Creates inactive, active, disabled, and disabled-active variants according
 * to @p flags. Color adjustments are applied per-pixel:
 * - Active (clicked) pixels are darkened by 20%.
 * - Disabled pixels are desaturated and, for RGBA32, have their alpha reduced
 *   to 30% of the original.
 * - Disabled-active pixels combine both the darkening and alpha reduction.
 *
 * Supports B_RGB32, B_RGB32_BIG, B_RGBA32, and B_RGBA32_BIG source formats.
 *
 * @param bitmap Source bitmap in one of the supported color spaces.
 * @param flags  Combination of B_CREATE_DISABLED_ICON_BITMAPS,
 *               B_CREATE_ACTIVE_ICON_BITMAP, and
 *               B_CREATE_PARTIALLY_ACTIVE_ICON_BITMAP.
 * @return B_OK on success, B_NO_MEMORY if any bitmap allocation fails, or
 *         B_BAD_VALUE if the source color space is unsupported.
 */
status_t
BIcon::_MakeBitmaps(const BBitmap* bitmap, uint32 flags)
{
	// make our own versions of the bitmap
	BRect b(bitmap->Bounds());

	color_space format = bitmap->ColorSpace();
	BBitmap* normalBitmap = CreateBitmap(b, format, B_INACTIVE_ICON_BITMAP);
	if (normalBitmap == NULL)
		return B_NO_MEMORY;

	BBitmap* disabledBitmap = NULL;
	if ((flags & B_CREATE_DISABLED_ICON_BITMAPS) != 0) {
		disabledBitmap = CreateBitmap(b, format,
			B_INACTIVE_ICON_BITMAP | B_DISABLED_ICON_BITMAP);
		if (disabledBitmap == NULL)
			return B_NO_MEMORY;
	}

	BBitmap* clickedBitmap = NULL;
	if ((flags & (B_CREATE_ACTIVE_ICON_BITMAP
			| B_CREATE_PARTIALLY_ACTIVE_ICON_BITMAP)) != 0) {
		clickedBitmap = CreateBitmap(b, format, B_ACTIVE_ICON_BITMAP);
		if (clickedBitmap == NULL)
			return B_NO_MEMORY;
	}

	BBitmap* disabledClickedBitmap = NULL;
	if (disabledBitmap != NULL && clickedBitmap != NULL) {
		disabledClickedBitmap = CreateBitmap(b, format,
			B_ACTIVE_ICON_BITMAP | B_DISABLED_ICON_BITMAP);
		if (disabledClickedBitmap == NULL)
			return B_NO_MEMORY;
	}

	// copy bitmaps from file bitmap
	uint8* nBits = normalBitmap != NULL ? (uint8*)normalBitmap->Bits() : NULL;
	uint8* dBits = disabledBitmap != NULL
		? (uint8*)disabledBitmap->Bits() : NULL;
	uint8* cBits = clickedBitmap != NULL ? (uint8*)clickedBitmap->Bits() : NULL;
	uint8* dcBits = disabledClickedBitmap != NULL
		? (uint8*)disabledClickedBitmap->Bits() : NULL;
	uint8* fBits = (uint8*)bitmap->Bits();
	int32 nbpr = normalBitmap->BytesPerRow();
	int32 fbpr = bitmap->BytesPerRow();
	int32 pixels = b.IntegerWidth() + 1;
	int32 lines = b.IntegerHeight() + 1;
	if (format == B_RGB32) {
		// nontransparent version

		// iterate over color components
		for (int32 y = 0; y < lines; y++) {
			for (int32 x = 0; x < pixels; x++) {
				int32 nOffset = 4 * x;
				int32 fOffset = 4 * x;
				nBits[nOffset + 0] = fBits[fOffset + 0];
				nBits[nOffset + 1] = fBits[fOffset + 1];
				nBits[nOffset + 2] = fBits[fOffset + 2];
				nBits[nOffset + 3] = 255;

				// clicked bits are darker (lame method...)
				if (cBits != NULL) {
					cBits[nOffset + 0] = uint8((float)nBits[nOffset + 0] * 0.8);
					cBits[nOffset + 1] = uint8((float)nBits[nOffset + 1] * 0.8);
					cBits[nOffset + 2] = uint8((float)nBits[nOffset + 2] * 0.8);
					cBits[nOffset + 3] = 255;
				}

				// disabled bits have less contrast (lame method...)
				if (dBits != NULL) {
					uint8 grey = 216;
					float dist = (nBits[nOffset + 0] - grey) * 0.4;
					dBits[nOffset + 0] = (uint8)(grey + dist);
					dist = (nBits[nOffset + 1] - grey) * 0.4;
					dBits[nOffset + 1] = (uint8)(grey + dist);
					dist = (nBits[nOffset + 2] - grey) * 0.4;
					dBits[nOffset + 2] = (uint8)(grey + dist);
					dBits[nOffset + 3] = 255;
				}

				// disabled bits have less contrast (lame method...)
				if (dcBits != NULL) {
					uint8 grey = 188;
					float dist = (nBits[nOffset + 0] - grey) * 0.4;
					dcBits[nOffset + 0] = (uint8)(grey + dist);
					dist = (nBits[nOffset + 1] - grey) * 0.4;
					dcBits[nOffset + 1] = (uint8)(grey + dist);
					dist = (nBits[nOffset + 2] - grey) * 0.4;
					dcBits[nOffset + 2] = (uint8)(grey + dist);
					dcBits[nOffset + 3] = 255;
				}
			}
			fBits += fbpr;
			nBits += nbpr;
			if (cBits != NULL)
				cBits += nbpr;
			if (dBits != NULL)
				dBits += nbpr;
			if (dcBits != NULL)
				dcBits += nbpr;
		}
	} else if (format == B_RGB32_BIG) {
		// nontransparent version

		// iterate over color components
		for (int32 y = 0; y < lines; y++) {
			for (int32 x = 0; x < pixels; x++) {
				int32 nOffset = 4 * x;
				int32 fOffset = 4 * x;
				nBits[nOffset + 3] = fBits[fOffset + 3];
				nBits[nOffset + 2] = fBits[fOffset + 2];
				nBits[nOffset + 1] = fBits[fOffset + 1];
				nBits[nOffset + 0] = 255;

				// clicked bits are darker (lame method...)
				if (cBits != NULL) {
					cBits[nOffset + 3] = uint8((float)nBits[nOffset + 3] * 0.8);
					cBits[nOffset + 2] = uint8((float)nBits[nOffset + 2] * 0.8);
					cBits[nOffset + 1] = uint8((float)nBits[nOffset + 1] * 0.8);
					cBits[nOffset + 0] = 255;
				}

				// disabled bits have less contrast (lame method...)
				if (dBits != NULL) {
					uint8 grey = 216;
					float dist = (nBits[nOffset + 3] - grey) * 0.4;
					dBits[nOffset + 3] = (uint8)(grey + dist);
					dist = (nBits[nOffset + 2] - grey) * 0.4;
					dBits[nOffset + 2] = (uint8)(grey + dist);
					dist = (nBits[nOffset + 1] - grey) * 0.4;
					dBits[nOffset + 1] = (uint8)(grey + dist);
					dBits[nOffset + 0] = 255;
				}

				// disabled bits have less contrast (lame method...)
				if (dcBits != NULL) {
					uint8 grey = 188;
					float dist = (nBits[nOffset + 3] - grey) * 0.4;
					dcBits[nOffset + 3] = (uint8)(grey + dist);
					dist = (nBits[nOffset + 2] - grey) * 0.4;
					dcBits[nOffset + 2] = (uint8)(grey + dist);
					dist = (nBits[nOffset + 1] - grey) * 0.4;
					dcBits[nOffset + 1] = (uint8)(grey + dist);
					dcBits[nOffset + 0] = 255;
				}
			}
			fBits += fbpr;
			nBits += nbpr;
			if (cBits != NULL)
				cBits += nbpr;
			if (dBits != NULL)
				dBits += nbpr;
			if (dcBits != NULL)
				dcBits += nbpr;
		}
	} else if (format == B_RGBA32) {
		// transparent version

		// iterate over color components
		for (int32 y = 0; y < lines; y++) {
			for (int32 x = 0; x < pixels; x++) {
				int32 nOffset = 4 * x;
				int32 fOffset = 4 * x;
				nBits[nOffset + 0] = fBits[fOffset + 0];
				nBits[nOffset + 1] = fBits[fOffset + 1];
				nBits[nOffset + 2] = fBits[fOffset + 2];
				nBits[nOffset + 3] = fBits[fOffset + 3];

				// clicked bits are darker (lame method...)
				if (cBits != NULL) {
					cBits[nOffset + 0] = (uint8)(nBits[nOffset + 0] * 0.8);
					cBits[nOffset + 1] = (uint8)(nBits[nOffset + 1] * 0.8);
					cBits[nOffset + 2] = (uint8)(nBits[nOffset + 2] * 0.8);
					cBits[nOffset + 3] = fBits[fOffset + 3];
				}

				// disabled bits have less opacity
				if (dBits != NULL) {
					uint8 grey = ((uint16)nBits[nOffset + 0] * 10
					    + nBits[nOffset + 1] * 60
						+ nBits[nOffset + 2] * 30) / 100;
					float dist = (nBits[nOffset + 0] - grey) * 0.3;
					dBits[nOffset + 0] = (uint8)(grey + dist);
					dist = (nBits[nOffset + 1] - grey) * 0.3;
					dBits[nOffset + 1] = (uint8)(grey + dist);
					dist = (nBits[nOffset + 2] - grey) * 0.3;
					dBits[nOffset + 2] = (uint8)(grey + dist);
					dBits[nOffset + 3] = (uint8)(fBits[fOffset + 3] * 0.3);
				}

				// disabled bits have less contrast (lame method...)
				if (dcBits != NULL) {
					dcBits[nOffset + 0] = (uint8)(dBits[nOffset + 0] * 0.8);
					dcBits[nOffset + 1] = (uint8)(dBits[nOffset + 1] * 0.8);
					dcBits[nOffset + 2] = (uint8)(dBits[nOffset + 2] * 0.8);
					dcBits[nOffset + 3] = (uint8)(fBits[fOffset + 3] * 0.3);
				}
			}
			fBits += fbpr;
			nBits += nbpr;
			if (cBits != NULL)
				cBits += nbpr;
			if (dBits != NULL)
				dBits += nbpr;
			if (dcBits != NULL)
				dcBits += nbpr;
		}
	} else if (format == B_RGBA32_BIG) {
		// transparent version

		// iterate over color components
		for (int32 y = 0; y < lines; y++) {
			for (int32 x = 0; x < pixels; x++) {
				int32 nOffset = 4 * x;
				int32 fOffset = 4 * x;
				nBits[nOffset + 3] = fBits[fOffset + 3];
				nBits[nOffset + 2] = fBits[fOffset + 2];
				nBits[nOffset + 1] = fBits[fOffset + 1];
				nBits[nOffset + 0] = fBits[fOffset + 0];

				// clicked bits are darker (lame method...)
				if (cBits != NULL) {
					cBits[nOffset + 3] = (uint8)(nBits[nOffset + 3] * 0.8);
					cBits[nOffset + 2] = (uint8)(nBits[nOffset + 2] * 0.8);
					cBits[nOffset + 1] = (uint8)(nBits[nOffset + 1] * 0.8);
					cBits[nOffset + 0] = fBits[fOffset + 0];
				}

				// disabled bits have less opacity
				if (dBits != NULL) {
					uint8 grey = ((uint16)nBits[nOffset + 3] * 10
					    + nBits[nOffset + 2] * 60
						+ nBits[nOffset + 1] * 30) / 100;
					float dist = (nBits[nOffset + 3] - grey) * 0.3;
					dBits[nOffset + 3] = (uint8)(grey + dist);
					dist = (nBits[nOffset + 2] - grey) * 0.3;
					dBits[nOffset + 2] = (uint8)(grey + dist);
					dist = (nBits[nOffset + 1] - grey) * 0.3;
					dBits[nOffset + 1] = (uint8)(grey + dist);
					dBits[nOffset + 0] = (uint8)(fBits[fOffset + 0] * 0.3);
				}

				// disabled bits have less contrast (lame method...)
				if (dcBits != NULL) {
					dcBits[nOffset + 3] = (uint8)(dBits[nOffset + 3] * 0.8);
					dcBits[nOffset + 2] = (uint8)(dBits[nOffset + 2] * 0.8);
					dcBits[nOffset + 1] = (uint8)(dBits[nOffset + 1] * 0.8);
					dcBits[nOffset + 0] = (uint8)(fBits[fOffset + 0] * 0.3);
				}
			}
			fBits += fbpr;
			nBits += nbpr;
			if (cBits != NULL)
				cBits += nbpr;
			if (dBits != NULL)
				dBits += nbpr;
			if (dcBits != NULL)
				dcBits += nbpr;
		}
	} else {
		// unsupported format
		return B_BAD_VALUE;
	}

	// make the partially-on bitmaps a copy of the on bitmaps
	if ((flags & B_CREATE_PARTIALLY_ACTIVE_ICON_BITMAP) != 0) {
		if (CopyBitmap(clickedBitmap, B_PARTIALLY_ACTIVATE_ICON_BITMAP) == NULL)
			return B_NO_MEMORY;
		if ((flags & B_CREATE_DISABLED_ICON_BITMAPS) != 0) {
			if (CopyBitmap(disabledClickedBitmap,
					B_PARTIALLY_ACTIVATE_ICON_BITMAP | B_DISABLED_ICON_BITMAP)
					== NULL) {
				return B_NO_MEMORY;
			}
		}
	}

	return B_OK;
}


}	// namespace BPrivate
