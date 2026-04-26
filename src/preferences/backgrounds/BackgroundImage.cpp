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
 *   Terms and Conditions
 *
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *
 *   Permission is hereby granted, free of charge, to any person obtaining
 *   a copy of this software and associated documentation files (the
 *   "Software"), to deal in the Software without restriction, including
 *   without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to
 *   permit persons to whom the Software is furnished to do so, subject
 *   to the following conditions:
 *
 *   The above copyright notice and this permission notice applies to all
 *   licensees and shall be included in all copies or substantial portions
 *   of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *   TITLE, MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *   NONINFRINGEMENT. IN NO EVENT SHALL BE INCORPORATED BE LIABLE FOR ANY
 *   CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 *   TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION WITH THE
 *   SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *   Except as contained in this notice, the name of Be Incorporated
 *   shall not be used in advertising or otherwise to promote the sale,
 *   use or other dealings in this Software without prior written
 *   authorization from Be Incorporated.
 *
 *   Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or
 *   registered trademarks of Be Incorporated in the United States and
 *   other countries. Other brand product names are registered trademarks
 *   or trademarks of their respective holders. All rights reserved.
 */


/**
 * @file BackgroundImage.cpp
 * @brief Persistent storage and rendering of desktop and folder wallpapers.
 *
 * Defines BackgroundImage, BackgroundImageInfo and Image -- the data
 * model used by the Backgrounds preflet (and Tracker) for serialising
 * one or more background bitmaps to a node's @c be:bgndimginfo
 * attribute and rendering them at the appropriate scale, offset, and
 * tiling for each workspace.
 */


// Classes used for setting up and managing background images.

#include "BackgroundImage.h"

#include <new>
#include <stdlib.h>

#include <Bitmap.h>
#include <Debug.h>
#include <fs_attr.h>
#include <Node.h>
#include <TranslationKit.h>
#include <View.h>
#include <Window.h>
#include <Message.h>
#include <Entry.h>
#include <Path.h>
#include <Screen.h>
#include <String.h>

#include "BackgroundsView.h"


/** @brief Top-level node attribute holding the flattened background message. */
const char* kBackgroundImageInfo 			= "be:bgndimginfo";
/** @brief Per-image offset within the flattened message. */
const char* kBackgroundImageInfoOffset 		= "be:bgndimginfooffset";
// const char* kBackgroundImageInfoTextOutline = "be:bgndimginfotextoutline";
/** @brief Per-image text-outline flag (legacy attribute name kept). */
const char* kBackgroundImageInfoTextOutline	= "be:bgndimginfoerasetext";
// NOTE: the attribute keeps the old name for backwards compatibility,
// just in case some users spend time configuring a few windows with
// this feature on or off...
/** @brief Per-image rendering mode (centered, tiled, scaled, offset). */
const char* kBackgroundImageInfoMode 		= "be:bgndimginfomode";
/** @brief Per-image workspace mask. */
const char* kBackgroundImageInfoWorkspaces 	= "be:bgndimginfoworkspaces";
/** @brief Per-image filesystem path to the bitmap. */
const char* kBackgroundImageInfoPath 		= "be:bgndimginfopath";
/** @brief Per-image set index used for slideshow grouping. */
const char* kBackgroundImageInfoSet 		= "be:bgndimginfoset";
/** @brief Per-image cache strategy. */
const char* kBackgroundImageInfoCacheMode	= "be:bgndimginfocachemode";
/** @brief Slideshow period in seconds. */
const char* kBackgroundImageSetPeriod		= "be:bgndimgsetperiod";
/** @brief Slideshow random-order flag. */
const char* kBackgroundImageRandomChange	= "be:bgndimgrandomchange";
/** @brief Global cache strategy applied to all images. */
const char* kBackgroundImageCacheMode		= "be:bgndimgcachemode";


/**
 * @brief Builds a BackgroundImage by reading the metadata from @a node.
 *
 * Parses the @c be:bgndimginfo attribute, registers each referenced
 * bitmap with @a view's image cache, and returns a populated
 * BackgroundImage. On the desktop the parser also reads the slideshow
 * period, random-order flag, and global cache mode.
 *
 * @param node      Source node carrying the background metadata.
 * @param isDesktop Pass @c true when @a node is the desktop folder.
 * @param view      BackgroundsView whose image cache should be used.
 * @return New BackgroundImage owned by the caller. Never @c NULL.
 */
BackgroundImage*
BackgroundImage::GetBackgroundImage(const BNode* node, bool isDesktop,
	BackgroundsView* view)
{
	BackgroundImage* result = new BackgroundImage(node, isDesktop, view);
	attr_info info;
	if (node->GetAttrInfo(kBackgroundImageInfo, &info) != B_OK)
		return result;

	BMessage container;
	char* buffer = new char [info.size];

	status_t error = node->ReadAttr(kBackgroundImageInfo, info.type, 0, buffer,
		(size_t)info.size);
	if (error == info.size)
		error = container.Unflatten(buffer);

	delete [] buffer;

	if (error != B_OK)
		return result;

	PRINT_OBJECT(container);

	uint32 imageSetPeriod = 0;
	uint32 globalCacheMode = 0;
	bool randomChange = false;
	uint32 maxImageSet = 0;

	if (isDesktop) {
		container.FindInt32(kBackgroundImageSetPeriod, (int32*)&imageSetPeriod);
		container.FindInt32(kBackgroundImageCacheMode,
			(int32*)&globalCacheMode);
		container.FindBool(kBackgroundImageRandomChange, &randomChange);
	}

	for (int32 index = 0; ; index++) {
		const char* path;
		uint32 workspaces = B_ALL_WORKSPACES;
		Mode mode = kTiled;
		bool textWidgetLabelOutline = false;
		BPoint offset;
		uint32 imageSet = 0;
		uint32 cacheMode = 0;
		int32 imageIndex = -1;

		if (container.FindString(kBackgroundImageInfoPath, index, &path)
			== B_OK) {
			if (strcmp(path, "")) {
				BPath bpath(path);
				imageIndex = view->AddImage(bpath);
				if (imageIndex < 0) {
					imageIndex = -imageIndex - 1;
				}
			}
		} else 
			break;

		container.FindInt32(kBackgroundImageInfoWorkspaces, index,
			(int32*)&workspaces);
		container.FindInt32(kBackgroundImageInfoMode, index, (int32*)&mode);
		container.FindBool(kBackgroundImageInfoTextOutline, index,
			&textWidgetLabelOutline);
		container.FindPoint(kBackgroundImageInfoOffset, index, &offset);

		if (isDesktop) {
			container.FindInt32(kBackgroundImageInfoSet, index,
				(int32*)&imageSet);
			container.FindInt32(kBackgroundImageInfoCacheMode, index,
				(int32*)&cacheMode);
		}

		BackgroundImage::BackgroundImageInfo* imageInfo = new
			BackgroundImage::BackgroundImageInfo(workspaces, imageIndex,
				mode, offset, textWidgetLabelOutline, imageSet, cacheMode);

		// imageInfo->UnloadBitmap(globalCacheMode);

		if (imageSet > maxImageSet)
			maxImageSet = imageSet;

		result->Add(imageInfo);
	}

	if (result) {
		result->fImageSetCount = maxImageSet + 1;
		result->fRandomChange = randomChange;
		result->fImageSetPeriod = imageSetPeriod;
		result->fCacheMode = globalCacheMode;
		if (result->fImageSetCount > 1)
			result->fShowingImageSet = random() % result->fImageSetCount;
	}

	return result;
}


/**
 * @brief Constructs a per-image record for one workspace mask.
 *
 * @param workspaces             Bitmask of workspaces this entry applies to.
 * @param imageIndex             Index into the parent view's image cache.
 * @param mode                   Rendering mode (offset, tile, center, scale).
 * @param offset                 Top-left offset for kAtOffset placement.
 * @param textWidgetLabelOutline Tracker pose-view label-outline preference.
 * @param imageSet               Slideshow set this image belongs to.
 * @param cacheMode              Per-image cache strategy.
 */
BackgroundImage::BackgroundImageInfo::BackgroundImageInfo(uint32 workspaces,
	int32 imageIndex, Mode mode, BPoint offset, bool textWidgetLabelOutline,
	uint32 imageSet, uint32 cacheMode)
	:
	fWorkspace(workspaces),
	fImageIndex(imageIndex),
	fMode(mode),
	fOffset(offset),
	fTextWidgetLabelOutline(textWidgetLabelOutline),
	fImageSet(imageSet),
	fCacheMode(cacheMode)
{
}


/**
 * @brief Destructor; the bitmap is owned by the parent view's image cache.
 */
BackgroundImage::BackgroundImageInfo::~BackgroundImageInfo()
{
}


//	#pragma mark -


/**
 * @brief Private constructor invoked only via GetBackgroundImage().
 *
 * @param node    Source node that owns the metadata.
 * @param desktop @c true when @a node is the desktop folder.
 * @param view    BackgroundsView used to resolve image indices.
 */
BackgroundImage::BackgroundImage(const BNode* node, bool desktop,
	BackgroundsView* view)
	:
	fIsDesktop(desktop),
	fDefinedByNode(*node),
	fView(NULL),
	fBackgroundsView(view),
	fShowingBitmap(NULL),
	fBitmapForWorkspaceList(1),
	fImageSetPeriod(0),
	fShowingImageSet(0),
	fImageSetCount(0),
	fCacheMode(0),
	fRandomChange(false)
{
}


/**
 * @brief Destructor; deletes any owned BackgroundImageInfo entries.
 */
BackgroundImage::~BackgroundImage()
{
}


/**
 * @brief Adds a per-image record to the workspace list.
 *
 * @param info Record to add. Ownership is transferred to this object.
 */
void
BackgroundImage::Add(BackgroundImageInfo* info)
{
	fBitmapForWorkspaceList.AddItem(info);
}


/**
 * @brief Removes a per-image record from the workspace list.
 *
 * Does not delete @a info; callers must free it themselves.
 *
 * @param info Record to remove.
 */
void
BackgroundImage::Remove(BackgroundImageInfo* info)
{
	fBitmapForWorkspaceList.RemoveItem(info);
}


/**
 * @brief Removes every record belonging to the active slideshow set.
 *
 * Records whose @c fImageSet differs from @c fShowingImageSet are
 * left in place.
 */
void
BackgroundImage::RemoveAll()
{
	for (int32 index = 0; index < fBitmapForWorkspaceList.CountItems();) {
		BackgroundImageInfo* info = fBitmapForWorkspaceList.ItemAt(index);
		if (info->fImageSet != fShowingImageSet)
			index++;
		else
			fBitmapForWorkspaceList.RemoveItemAt(index);
	}
}


/**
 * @brief Picks the right record for @a workspace and renders it on @a view.
 *
 * No-op if no record matches the workspace mask.
 *
 * @param view      Target BView (typically the Tracker pose view).
 * @param workspace Zero-based workspace index.
 */
void
BackgroundImage::Show(BView* view, int32 workspace)
{
	fView = view;

	BackgroundImageInfo* info = ImageInfoForWorkspace(workspace);
	if (info) {
		/*BPoseView* poseView = dynamic_cast<BPoseView*>(fView);
		if (poseView)
			poseView
				->SetEraseWidgetTextBackground(info->fTextWidgetLabelOutline);*/
		Show(info, fView);
	}
}


/**
 * @brief Renders @a info as the view bitmap of @a view.
 *
 * Computes destination bounds and tiling/scaling options from
 * @c info->fMode and the screen's virtual size, then assigns the
 * bitmap as @a view's view bitmap and forces a redraw.
 *
 * @param info Per-image record describing what to render.
 * @param view Target BView.
 */
void
BackgroundImage::Show(BackgroundImageInfo* info, BView* view)
{
	BBitmap* bitmap
		= fBackgroundsView->GetImage(info->fImageIndex)->GetBitmap();

	if (!bitmap)
		return;

	BRect viewBounds(view->Bounds());

	display_mode mode;
	BScreen().GetMode(&mode);
	float x_ratio = viewBounds.Width() / mode.virtual_width;
	float y_ratio = viewBounds.Height() / mode.virtual_height;

	BRect bitmapBounds(bitmap->Bounds());
	BRect destinationBitmapBounds(bitmapBounds);
	destinationBitmapBounds.right *= x_ratio;
	destinationBitmapBounds.bottom *= y_ratio;
	BPoint offset(info->fOffset);
	offset.x *= x_ratio;
	offset.y *= y_ratio;

	uint32 options = 0;
	uint32 followFlags = B_FOLLOW_TOP | B_FOLLOW_LEFT;

	// figure out the display mode and the destination bounds for the bitmap
	switch (info->fMode) {
		case kCentered:
			if (fIsDesktop) {
				destinationBitmapBounds.OffsetBy(
					(viewBounds.Width() - destinationBitmapBounds.Width()) / 2,
					(viewBounds.Height() - destinationBitmapBounds.Height())
					/ 2);
				break;
			}
			// else fall thru
		case kScaledToFit:
			if (fIsDesktop) {
				if (BRectRatio(destinationBitmapBounds)
					>= BRectRatio(viewBounds)) {
					float overlap = BRectHorizontalOverlap(viewBounds,
						destinationBitmapBounds);
					destinationBitmapBounds.Set(-overlap, 0,
						viewBounds.Width() + overlap, viewBounds.Height());
				} else {
					float overlap = BRectVerticalOverlap(viewBounds,
						destinationBitmapBounds);
					destinationBitmapBounds.Set(0, -overlap,
						viewBounds.Width(), viewBounds.Height() + overlap);
				}
				followFlags = B_FOLLOW_ALL;
				options |= B_FILTER_BITMAP_BILINEAR;
				break;
			}
			// else fall thru
		case kAtOffset:
		{
			destinationBitmapBounds.OffsetTo(offset);
			break;
		}
		case kTiled:
			// Original Backgrounds Preferences center the tiled paper
			// but Tracker doesn't do that
			//if (fIsDesktop) {
			destinationBitmapBounds.OffsetBy(
				(viewBounds.Width() - destinationBitmapBounds.Width()) / 2,
				(viewBounds.Height() - destinationBitmapBounds.Height()) / 2);
			//}
			options |= B_TILE_BITMAP;
			break;
	}

	// switch to the bitmap and force a redraw
	view->SetViewBitmap(bitmap, bitmapBounds, destinationBitmapBounds,
		followFlags, options);
	view->Invalidate();

	/*if (fShowingBitmap != info) {
		if (fShowingBitmap)
			fShowingBitmap->UnloadBitmap(fCacheMode);
		fShowingBitmap = info;
	}*/
}


/**
 * @brief Returns the width-to-height ratio of @a rect.
 *
 * @param rect Source rectangle.
 * @return @c rect.Width() / @c rect.Height().
 */
float
BackgroundImage::BRectRatio(BRect rect)
{
	return rect.Width() / rect.Height();
}


/**
 * @brief Returns half the horizontal overhang when fitting by height.
 *
 * Used by kScaledToFit to compute how much the bitmap must overflow
 * horizontally if scaled to match the host's height.
 *
 * @param hostRect    Destination rectangle.
 * @param resizedRect Resized bitmap rectangle.
 * @return Half the horizontal overhang in pixels.
 */
float
BackgroundImage::BRectHorizontalOverlap(BRect hostRect, BRect resizedRect)
{
	return ((hostRect.Height() / resizedRect.Height() * resizedRect.Width())
		- hostRect.Width()) / 2;
}


/**
 * @brief Returns half the vertical overhang when fitting by width.
 *
 * @param hostRect    Destination rectangle.
 * @param resizedRect Resized bitmap rectangle.
 * @return Half the vertical overhang in pixels.
 */
float
BackgroundImage::BRectVerticalOverlap(BRect hostRect, BRect resizedRect)
{
	return ((hostRect.Width() / resizedRect.Width() * resizedRect.Height())
		- hostRect.Height()) / 2;
}


/**
 * @brief Clears the active background bitmap from the bound view.
 */
void
BackgroundImage::Remove()
{
	if (fShowingBitmap) {
		fView->ClearViewBitmap();
		fView->Invalidate();
		/*BPoseView* poseView = dynamic_cast<BPoseView*>(fView);
		// make sure text widgets draw the default way, erasing their background
		if (poseView)
			poseView->SetEraseWidgetTextBackground(true);*/
	}
	fShowingBitmap = NULL;
}


/**
 * @brief Picks the per-image record best matching @a workspace.
 *
 * Prefers a record whose mask is exactly @a workspace's bit; falls
 * back to a record whose mask merely contains the bit. Folder
 * (non-desktop) backgrounds always return their first record.
 *
 * @param workspace Zero-based workspace index.
 * @return Matching record, or @c NULL if none applies.
 */
BackgroundImage::BackgroundImageInfo*
BackgroundImage::ImageInfoForWorkspace(int32 workspace) const
{
	uint32 workspaceMask = 1;

	for (; workspace; workspace--)
		workspaceMask *= 2;

	int32 count = fBitmapForWorkspaceList.CountItems();

	// do a simple lookup for the most likely candidate bitmap -
	// pick the imageInfo that is only defined for this workspace over one
	// that supports multiple workspaces
	BackgroundImageInfo* result = NULL;
	for (int32 index = 0; index < count; index++) {
		BackgroundImageInfo* info = fBitmapForWorkspaceList.ItemAt(index);
		if (info->fImageSet != fShowingImageSet)
			continue;

		if (fIsDesktop) {
			if (info->fWorkspace == workspaceMask)
				return info;

			if (info->fWorkspace & workspaceMask)
				result = info;
		} else
			return info;
	}
	return result;
}


/**
 * @brief Updates the desktop bitmap when entering a workspace.
 *
 * Folder backgrounds and workspace deactivation events are ignored.
 * If the new workspace has no matching record, the existing view
 * bitmap is cleared.
 *
 * @param view      Target BView.
 * @param workspace Workspace being entered.
 * @param state     @c true when entering, @c false when leaving.
 */
void
BackgroundImage::WorkspaceActivated(BView* view, int32 workspace, bool state)
{
	if (!fIsDesktop) {
		// we only care for desktop bitmaps
		return;
	}

	if (!state) {
		// we only care comming into a new workspace, not leaving one
		return;
	}

	BackgroundImageInfo* info = ImageInfoForWorkspace(workspace);
	if (info != fShowingBitmap) {
		if (info)
			Show(info, view);
		else {
			/*if (BPoseView* poseView = dynamic_cast<BPoseView*>(view))
				poseView->SetEraseWidgetTextBackground(true);*/
			view->ClearViewBitmap();
			view->Invalidate();
		}
		fShowingBitmap = info;
	}
}


/**
 * @brief Re-renders the desktop bitmap after a screen-mode change.
 *
 * The current implementation is a stub; the original Tracker
 * recompute path is preserved as a commented-out reference.
 */
void
BackgroundImage::ScreenChanged(BRect, color_space)
{
	if (!fIsDesktop || !fShowingBitmap)
		return;

	/*if (fShowingBitmap->fMode == kCentered) {
		BRect viewBounds(fView->Bounds());
		BRect bitmapBounds(fShowingBitmap->fBitmap->Bounds());
		BRect destinationBitmapBounds(bitmapBounds);
		destinationBitmapBounds.OffsetBy(
			(viewBounds.Width() - bitmapBounds.Width()) / 2,
			(viewBounds.Height() - bitmapBounds.Height()) / 2);

		fView->SetViewBitmap(fShowingBitmap->fBitmap, bitmapBounds,
			destinationBitmapBounds, B_FOLLOW_NONE, 0);
		fView->Invalidate();
	}*/
}


/**
 * @brief Flattens the workspace list and writes it to @a node's attribute.
 *
 * Constructs a BMessage containing one entry per record (path,
 * workspace mask, mode, offset, image-set index), flattens it, and
 * writes the result to @c kBackgroundImageInfo.
 *
 * @param node Target node.
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  When the flatten buffer cannot be allocated.
 * @retval B_ERROR      When the partial write does not match the
 *                      flattened size.
 */
status_t
BackgroundImage::SetBackgroundImage(BNode* node)
{
	status_t err;
	BMessage container;
	int32 count = fBitmapForWorkspaceList.CountItems();

	for (int32 index = 0; index < count; index++) {
		BackgroundImageInfo* info = fBitmapForWorkspaceList.ItemAt(index);

		container.AddBool(kBackgroundImageInfoTextOutline,
			info->fTextWidgetLabelOutline);
		if (fBackgroundsView->GetImage(info->fImageIndex) != NULL) {
			container.AddString(kBackgroundImageInfoPath,
				fBackgroundsView
					->GetImage(info->fImageIndex)->GetPath().Path());
		} else 
			container.AddString(kBackgroundImageInfoPath, "");

		container.AddInt32(kBackgroundImageInfoWorkspaces, info->fWorkspace);
		container.AddPoint(kBackgroundImageInfoOffset, info->fOffset);
		container.AddInt32(kBackgroundImageInfoMode, info->fMode);

		if (fIsDesktop)
			container.AddInt32(kBackgroundImageInfoSet, info->fImageSet);
	}

	PRINT_OBJECT(container);

	ssize_t flattenedSize = container.FlattenedSize();
	if (flattenedSize < B_OK)
		return flattenedSize;

	char* buffer = new(std::nothrow) char[flattenedSize];
	if (buffer == NULL)
		return B_NO_MEMORY;

	if ((err = container.Flatten(buffer, flattenedSize)) != B_OK) {
		delete[] buffer;
		return err;
	}

	ssize_t size = node->WriteAttr(kBackgroundImageInfo, B_MESSAGE_TYPE,
		0, buffer, flattenedSize);

	delete[] buffer;

	if (size < B_OK)
		return size;
	if (size != flattenedSize)
		return B_ERROR;

	return B_OK;
}


/*BackgroundImage*
BackgroundImage::Refresh(BackgroundImage* oldBackgroundImage,
	const BNode* fromNode, bool desktop, BPoseView* poseView)
{
	if (oldBackgroundImage) {
		oldBackgroundImage->Remove();
		delete oldBackgroundImage;
	}

	BackgroundImage* result = GetBackgroundImage(fromNode, desktop);
	if (result && poseView->ViewMode() != kListMode)
		result->Show(poseView, current_workspace());
	return result;
}


void
BackgroundImage::ChangeImageSet(BPoseView* poseView)
{
	if (fRandomChange) {
		if (fImageSetCount > 1) {
			uint32 oldShowingImageSet = fShowingImageSet;
			while (oldShowingImageSet == fShowingImageSet)
				fShowingImageSet = random()%fImageSetCount;
		} else
			fShowingImageSet = 0;
	} else {
		fShowingImageSet++;
		if (fShowingImageSet > fImageSetCount - 1)
			fShowingImageSet = 0;
	}

	this->Show(poseView, current_workspace());
}*/


//	#pragma mark -


/**
 * @brief Caches the leaf name of @a path, truncating it if it is long.
 *
 * Names longer than 40 characters are shortened with a UTF-8 ellipsis
 * while preserving the file extension.
 *
 * @param path Path to the bitmap on disk.
 */
Image::Image(BPath path)
	:
	fBitmap(NULL),
	fPath(path)
{
	const int32 kMaxNameChars = 40;
	fName = path.Leaf();
	int extra = fName.CountChars() - kMaxNameChars;
	if (extra > 0) {
		BString extension;
		int offset = fName.FindLast('.');
		if (offset > 0)
			fName.CopyInto(extension, ++offset, -1);
		fName.TruncateChars(kMaxNameChars) << B_UTF8_ELLIPSIS << extension;
	}
}


/**
 * @brief Destructor; frees the cached BBitmap if any.
 */
Image::~Image()
{
	delete fBitmap;
}


/**
 * @brief Lazily decodes the bitmap from disk on first request.
 *
 * @return Pointer to the cached BBitmap; ownership stays with this
 *         Image.
 */
BBitmap*
Image::GetBitmap()
{
	if (!fBitmap)
		fBitmap = BTranslationUtils::GetBitmap(fPath.Path());

	return fBitmap;
}

