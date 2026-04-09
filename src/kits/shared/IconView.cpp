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
 *   Copyright 2004-2020, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Adrien Destugues, pulkomandy@pulkomandy.tk
 *       Michael Wilber
 */

/** @file IconView.cpp
 *  @brief Implements IconView, a simple BView subclass that loads and renders
 *         an icon bitmap from a file path, raw vector data, or an existing
 *         BBitmap.
 */


#include "IconView.h"

#include <new>
#include <stdio.h>
#include <string.h>

#include <ControlLook.h>
#include <Entry.h>
#include <IconUtils.h>
#include <Node.h>
#include <NodeInfo.h>


using std::nothrow;


/** @brief Constructs an IconView with the specified icon size.
 *  @param iconSize  The desired icon size constant (e.g. B_LARGE_ICON).
 */
IconView::IconView(icon_size iconSize)
	:
	BView("IconView", B_WILL_DRAW),
	fIconSize(iconSize),
	fIconBitmap(NULL),
	fDrawIcon(false)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
}


/** @brief Destructor — frees the icon bitmap. */
IconView::~IconView()
{
	delete fIconBitmap;
	fIconBitmap = NULL;
}


/** @brief Loads the tracker icon for a file and stores it for drawing.
 *
 *  Allocates a new bitmap at the composed icon size, retrieves the
 *  tracker icon from the file's node info, and marks the view for
 *  drawing.
 *
 *  @param path      Path to the file whose icon should be displayed.
 *  @param iconSize  Desired icon size; if different from the current size a
 *                   new bitmap is allocated.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
IconView::SetIcon(const BPath& path, icon_size iconSize)
{
	fDrawIcon = false;

	if (iconSize != fIconSize || fIconBitmap == NULL) {
		BBitmap* bitmap = new BBitmap(BRect(BPoint(0, 0),
			be_control_look->ComposeIconSize(iconSize)), B_RGBA32);
		if (bitmap == NULL)
			return B_NO_MEMORY;

		delete fIconBitmap;
		fIconBitmap = bitmap;
		fIconSize = iconSize;
	}

	status_t status = fIconBitmap->InitCheck();
	if (status != B_OK)
		return status;

	BEntry entry(path.Path());
	BNode node(&entry);
	BNodeInfo info(&node);

	status = info.GetTrackerIcon(fIconBitmap,
		(icon_size)fIconBitmap->Bounds().IntegerWidth());
	if (status != B_OK)
		return status;

	if (!fIconBitmap->IsValid())
		return fIconBitmap->InitCheck();

	_SetSize();

	fDrawIcon = true;
	Invalidate();
	return B_OK;
}


/** @brief Loads an icon from raw HVIF (vector icon) data.
 *
 *  Allocates a bitmap at the composed icon size, rasterizes the vector
 *  icon into it, and marks the view for drawing.
 *
 *  @param data      Pointer to the raw HVIF data buffer.
 *  @param size      Size in bytes of the data buffer.
 *  @param iconSize  Desired icon size; if different from the current size a
 *                   new bitmap is allocated.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
IconView::SetIcon(const uint8_t* data, size_t size, icon_size iconSize)
{
	fDrawIcon = false;

	if (iconSize != fIconSize || fIconBitmap == NULL) {
		BBitmap* bitmap = new BBitmap(BRect(BPoint(0, 0),
			be_control_look->ComposeIconSize(iconSize)), B_RGBA32);
		if (bitmap == NULL)
			return B_NO_MEMORY;

		delete fIconBitmap;
		fIconBitmap = bitmap;
		fIconSize = iconSize;
	}

	status_t status = fIconBitmap->InitCheck();
	if (status != B_OK)
		return status;

	status = BIconUtils::GetVectorIcon(data, size, fIconBitmap);
	if (status != B_OK)
		return status;

	if (!fIconBitmap->IsValid())
		return fIconBitmap->InitCheck();

	_SetSize();

	fDrawIcon = true;
	Invalidate();
	return B_OK;
}


/** @brief Sets the displayed icon from an existing BBitmap.
 *
 *  Clones the provided bitmap.  Passing NULL clears the current icon
 *  without displaying anything.
 *
 *  @param icon  Pointer to the source bitmap, or NULL to clear.
 *  @return B_OK on success, B_NO_MEMORY if the clone could not be allocated,
 *          or another error code if the cloned bitmap is invalid.
 */
status_t
IconView::SetIcon(const BBitmap* icon)
{
	if (icon == NULL) {
		fDrawIcon = false;
		return B_OK;
	}

	delete fIconBitmap;
	fIconBitmap = new BBitmap(icon);
	if (fIconBitmap == NULL)
		return B_NO_MEMORY;

	status_t status = fIconBitmap->InitCheck();
	if (status != B_OK)
		return status;

	fIconSize = (icon_size)-1;
	_SetSize();

	fDrawIcon = true;
	Invalidate();
	return B_OK;
}


/** @brief Controls whether the icon bitmap is drawn.
 *  @param draw  True to draw the icon; false to suppress drawing.
 */
void
IconView::DrawIcon(bool draw)
{
	if (draw == fDrawIcon)
		return;

	fDrawIcon = draw;
	Invalidate();
}


/** @brief Draws the icon bitmap using alpha-blending, or falls back to BView::Draw.
 *  @param area  The update rectangle (unused when delegating to the bitmap).
 */
void
IconView::Draw(BRect area)
{
	if (fDrawIcon && fIconBitmap != NULL) {
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		DrawBitmap(fIconBitmap);
		SetDrawingMode(B_OP_COPY);
	} else
		BView::Draw(area);
}


/** @brief Returns the initialisation status of the icon bitmap.
 *  @return B_NO_MEMORY if no bitmap exists, otherwise the result of
 *          BBitmap::InitCheck().
 */
status_t
IconView::InitCheck() const
{
	if (fIconBitmap == NULL)
		return B_NO_MEMORY;

	return fIconBitmap->InitCheck();
}


/** @brief Updates the view's explicit min/max/preferred sizes to match the bitmap. */
void
IconView::_SetSize()
{
	SetExplicitMinSize(fIconBitmap->Bounds().Size());
	SetExplicitMaxSize(fIconBitmap->Bounds().Size());
	SetExplicitPreferredSize(fIconBitmap->Bounds().Size());
}
