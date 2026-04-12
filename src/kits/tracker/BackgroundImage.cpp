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
 * @file BackgroundImage.cpp
 * @brief Classes for loading, displaying, and managing Tracker background images.
 *
 * BackgroundImage reads workspace-aware background bitmap descriptors stored as
 * a flattened BMessage in the B_BACKGROUND_INFO node attribute. It supports
 * tiled, scaled, centred, and offset placement modes, and handles workspace
 * switches and screen resolution changes for desktop windows.
 *
 * @see BPoseView, BDeskWindow
 */


#include <Bitmap.h>
#include <ControlLook.h>
#include <Node.h>
#include <TranslationKit.h>
#include <View.h>
#include <Window.h>

#include <fs_attr.h>

#include "BackgroundImage.h"

#include "Background.h"
#include "Commands.h"
#include "PoseView.h"


namespace BPrivate {

const char* kBackgroundImageInfo 			= B_BACKGROUND_INFO;
const char* kBackgroundImageInfoOffset 		= B_BACKGROUND_ORIGIN;
const char* kBackgroundImageInfoTextOutline	= B_BACKGROUND_TEXT_OUTLINE;
const char* kBackgroundImageInfoMode 		= B_BACKGROUND_MODE;
const char* kBackgroundImageInfoWorkspaces 	= B_BACKGROUND_WORKSPACES;
const char* kBackgroundImageInfoPath 		= B_BACKGROUND_IMAGE;

}	// namespace BPrivate


//	#pragma mark - BackgroundImage


/**
 * @brief Read the background-image attribute from @a node and build a BackgroundImage object.
 *
 * Parses the flattened BMessage stored under B_BACKGROUND_INFO, loads each bitmap
 * path via BTranslationUtils, and populates a BackgroundImage with one
 * BackgroundImageInfo entry per workspace descriptor found.
 *
 * @param node       The BNode whose B_BACKGROUND_INFO attribute is read.
 * @param isDesktop  true if the node represents a desktop (full-screen) directory.
 * @return A heap-allocated BackgroundImage, or NULL if the attribute is absent or invalid.
 */
BackgroundImage*
BackgroundImage::GetBackgroundImage(const BNode* node, bool isDesktop)
{
	attr_info info;
	if (node->GetAttrInfo(kBackgroundImageInfo, &info) != B_OK)
		return NULL;

	BMessage container;
	char* buffer = new char[info.size];

	status_t error = node->ReadAttr(kBackgroundImageInfo, info.type, 0,
		buffer, (size_t)info.size);
	if (error == info.size)
		error = container.Unflatten(buffer);

	delete[] buffer;

	if (error != B_OK)
		return NULL;

	BackgroundImage* backgroundImage = NULL;
	for (int32 index = 0; ; index++) {
		const char* path;
		uint32 workspaces = B_ALL_WORKSPACES;
		Mode mode = kTiled;
		bool textWidgetLabelOutline = false;
		BPoint offset;
		BBitmap* bitmap = NULL;

		if (container.FindString(kBackgroundImageInfoPath, index, &path)
				== B_OK) {
			bitmap = BTranslationUtils::GetBitmap(path);
			if (!bitmap)
				PRINT(("failed to load background bitmap from path\n"));
		} else
			break;

		if (isDesktop)
			be_control_look->SetBackgroundInfo(container);

		container.FindInt32(kBackgroundImageInfoWorkspaces, index,
			(int32*)&workspaces);
		container.FindInt32(kBackgroundImageInfoMode, index, (int32*)&mode);
		container.FindBool(kBackgroundImageInfoTextOutline, index,
			&textWidgetLabelOutline);
		container.FindPoint(kBackgroundImageInfoOffset, index, &offset);

		BackgroundImage::BackgroundImageInfo* imageInfo = new
			BackgroundImage::BackgroundImageInfo(workspaces, bitmap, mode,
				offset, textWidgetLabelOutline);

		if (backgroundImage == NULL)
			backgroundImage = new BackgroundImage(node, isDesktop);

		backgroundImage->Add(imageInfo);
	}

	return backgroundImage;
}


/**
 * @brief Construct a per-workspace background descriptor.
 *
 * @param workspaces         Bitmask of workspaces where this image should be shown.
 * @param bitmap             The decoded BBitmap to display (ownership is taken).
 * @param mode               Display mode (tiled, centred, scaled, or at-offset).
 * @param offset             Pixel offset used in kAtOffset mode.
 * @param textWidgetOutline  Whether pose-view text widgets should draw an outline.
 */
BackgroundImage::BackgroundImageInfo::BackgroundImageInfo(uint32 workspaces,
	BBitmap* bitmap, Mode mode, BPoint offset, bool textWidgetOutline)
	:
	fWorkspace(workspaces),
	fBitmap(bitmap),
	fMode(mode),
	fOffset(offset),
	fTextWidgetOutline(textWidgetOutline)
{
}


/**
 * @brief Destructor; deletes the owned BBitmap.
 */
BackgroundImage::BackgroundImageInfo::~BackgroundImageInfo()
{
	delete fBitmap;
}


/**
 * @brief Private constructor; called only by GetBackgroundImage().
 *
 * @param node     The source node whose attribute defined this background.
 * @param desktop  true if this background belongs to a desktop window.
 */
BackgroundImage::BackgroundImage(const BNode* node, bool desktop)
	:
	fIsDesktop(desktop),
	fDefinedByNode(*node),
	fView(NULL),
	fShowingBitmap(NULL),
	fBitmapForWorkspaceList(1)
{
}


/**
 * @brief Destructor.
 */
BackgroundImage::~BackgroundImage()
{
}


/**
 * @brief Append a BackgroundImageInfo entry to the workspace list.
 *
 * @param info  Heap-allocated info descriptor (ownership is transferred).
 */
void
BackgroundImage::Add(BackgroundImageInfo* info)
{
	fBitmapForWorkspaceList.AddItem(info);
}


/**
 * @brief Show the background image appropriate for @a workspace on @a view.
 *
 * Looks up the best BackgroundImageInfo for the given workspace index and
 * applies it. Also propagates text-outline state to any BPoseView.
 *
 * @param view       The BView to apply the background bitmap to.
 * @param workspace  The current workspace index.
 */
void
BackgroundImage::Show(BView* view, int32 workspace)
{
	fView = view;

	BackgroundImageInfo* info = ImageInfoForWorkspace(workspace);
	if (info) {
		BPoseView* poseView = dynamic_cast<BPoseView*>(fView);
		if (poseView != NULL)
			poseView->SetWidgetTextOutline(info->fTextWidgetOutline);

		Show(info, fView);
	}
}


/**
 * @brief Apply a specific BackgroundImageInfo to @a view.
 *
 * Computes destination bounds according to the display mode (tiled, scaled,
 * centred, or at-offset), then calls BView::SetViewBitmap() and Invalidate().
 *
 * @param info  The BackgroundImageInfo descriptor to display.
 * @param view  The target BView.
 */
void
BackgroundImage::Show(BackgroundImageInfo* info, BView* view)
{
	BPoseView* poseView = dynamic_cast<BPoseView*>(view);
	if (poseView != NULL)
		poseView->SetWidgetTextOutline(info->fTextWidgetOutline);

	if (info->fBitmap == NULL) {
		view->ClearViewBitmap();
		view->Invalidate();
		fShowingBitmap = info;
		return;
	}
	BRect viewBounds(view->Bounds());
	BRect bitmapBounds(info->fBitmap->Bounds());
	BRect destinationBitmapBounds(bitmapBounds);

	uint32 options = 0;
	uint32 followFlags = B_FOLLOW_TOP | B_FOLLOW_LEFT;

	// figure out the display mode and the destination bounds for the bitmap
	switch (info->fMode) {
		case kCentered:
			if (fIsDesktop) {
				destinationBitmapBounds.OffsetBy(
					(viewBounds.Width() - bitmapBounds.Width()) / 2,
					(viewBounds.Height() - bitmapBounds.Height()) / 2);
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
			if (!fIsDesktop && poseView != NULL)
				destinationBitmapBounds.OffsetTo(poseView->Extent().LeftTop() + info->fOffset);
			else
				destinationBitmapBounds.OffsetTo(info->fOffset);
			break;

		case kTiled:
			if (fIsDesktop) {
				destinationBitmapBounds.OffsetBy(
					(viewBounds.Width() - bitmapBounds.Width()) / 2,
					(viewBounds.Height() - bitmapBounds.Height()) / 2);
			} else if (poseView != NULL) {
				// tile from top left of window even if scrolled over
				destinationBitmapBounds.OffsetTo(poseView->Extent().LeftTop());
			}
			options |= B_TILE_BITMAP;
			break;
	}

	// switch to the bitmap and force a redraw
	view->SetViewBitmap(info->fBitmap, bitmapBounds, destinationBitmapBounds,
		followFlags, options);
	view->Invalidate();
	fShowingBitmap = info;
}


/**
 * @brief Compute the width-to-height ratio of @a rect.
 *
 * @param rect  The rectangle whose ratio is calculated.
 * @return Width divided by height.
 */
float
BackgroundImage::BRectRatio(BRect rect)
{
	return rect.Width() / rect.Height();
}


/**
 * @brief Compute the horizontal overlap needed to scale @a resizedRect to fill @a hostRect.
 *
 * Used in kScaledToFit mode when the source image is wider than the host.
 *
 * @param hostRect     The destination rectangle (view bounds).
 * @param resizedRect  The source image bounds before scaling.
 * @return Horizontal overhang in pixels on each side.
 */
float
BackgroundImage::BRectHorizontalOverlap(BRect hostRect, BRect resizedRect)
{
	return ((hostRect.Height() / resizedRect.Height() * resizedRect.Width())
		- hostRect.Width()) / 2;
}


/**
 * @brief Compute the vertical overlap needed to scale @a resizedRect to fill @a hostRect.
 *
 * Used in kScaledToFit mode when the source image is taller than the host.
 *
 * @param hostRect     The destination rectangle (view bounds).
 * @param resizedRect  The source image bounds before scaling.
 * @return Vertical overhang in pixels on each side.
 */
float
BackgroundImage::BRectVerticalOverlap(BRect hostRect, BRect resizedRect)
{
	return ((hostRect.Width() / resizedRect.Width() * resizedRect.Height())
		- hostRect.Height()) / 2;
}


/**
 * @brief Clear the current background bitmap and reset the view to its default appearance.
 *
 * Also re-enables text-widget outline drawing on any associated BPoseView.
 */
void
BackgroundImage::Remove()
{
	if (fShowingBitmap != NULL) {
		fView->ClearViewBitmap();
		fView->Invalidate();
		BPoseView* poseView = dynamic_cast<BPoseView*>(fView);
		// make sure text widgets draw the default way, erasing
		// their background
		if (poseView != NULL)
			poseView->SetWidgetTextOutline(true);
	}

	fShowingBitmap = NULL;
}


/**
 * @brief Find the best BackgroundImageInfo for the given workspace index.
 *
 * Prefers an entry whose workspace mask exactly matches @a workspace; falls back
 * to the first entry whose mask includes the workspace bit.
 *
 * @param workspace  Zero-based workspace index.
 * @return The best matching BackgroundImageInfo, or NULL if none matches.
 */
BackgroundImage::BackgroundImageInfo*
BackgroundImage::ImageInfoForWorkspace(int32 workspace) const
{
	uint32 workspaceMask = 1;

	for ( ; workspace; workspace--)
		workspaceMask *= 2;

	int32 count = fBitmapForWorkspaceList.CountItems();

	// do a simple lookup for the most likely candidate bitmap -
	// pick the imageInfo that is only defined for this workspace over one
	// that supports multiple workspaces
	BackgroundImageInfo* result = NULL;
	for (int32 index = 0; index < count; index++) {
		BackgroundImageInfo* info = fBitmapForWorkspaceList.ItemAt(index);
		if (info->fWorkspace == workspaceMask)
			return info;

		if (info->fWorkspace & workspaceMask)
			result = info;
	}

	return result;
}


/**
 * @brief React to a workspace switch by showing the appropriate background.
 *
 * Only acts on desktop windows when entering a new workspace.
 *
 * @param view       The desktop BView.
 * @param workspace  The workspace that was activated.
 * @param state      true when entering the workspace, false when leaving.
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
		if (info != NULL)
			Show(info, view);
		else {
			BPoseView* poseView = dynamic_cast<BPoseView*>(view);
			if (poseView != NULL)
				poseView->SetWidgetTextOutline(true);

			view->ClearViewBitmap();
			view->Invalidate();
		}

		fShowingBitmap = info;
	}
}


/**
 * @brief Re-centre the background image after a screen resolution change.
 *
 * Only has an effect for desktop windows showing a centred image.
 */
void
BackgroundImage::ScreenChanged(BRect, color_space)
{
	if (!fIsDesktop || fShowingBitmap == NULL || fShowingBitmap->fBitmap == NULL)
		return;

	if (fShowingBitmap->fMode == kCentered) {
		BRect viewBounds(fView->Bounds());
		BRect bitmapBounds(fShowingBitmap->fBitmap->Bounds());
		BRect destinationBitmapBounds(bitmapBounds);
		destinationBitmapBounds.OffsetBy(
			(viewBounds.Width() - bitmapBounds.Width()) / 2,
			(viewBounds.Height() - bitmapBounds.Height()) / 2);

		fView->SetViewBitmap(fShowingBitmap->fBitmap, bitmapBounds,
			destinationBitmapBounds, B_FOLLOW_NONE, 0);
		fView->Invalidate();
	}
}


/**
 * @brief Replace @a oldBackgroundImage with a freshly loaded one from @a fromNode.
 *
 * Removes and deletes @a oldBackgroundImage, reads a new BackgroundImage from
 * @a fromNode, and immediately shows it on @a poseView if the view is in icon mode.
 *
 * @param oldBackgroundImage  Previous background to remove and delete (may be NULL).
 * @param fromNode            Node whose B_BACKGROUND_INFO attribute is parsed.
 * @param desktop             true if @a poseView is a desktop view.
 * @param poseView            The BPoseView to display the new background on.
 * @return Newly allocated BackgroundImage, or NULL if none is defined on the node.
 */
BackgroundImage*
BackgroundImage::Refresh(BackgroundImage* oldBackgroundImage,
	const BNode* fromNode, bool desktop, BPoseView* poseView)
{
	if (oldBackgroundImage != NULL) {
		oldBackgroundImage->Remove();
		delete oldBackgroundImage;
	}

	BackgroundImage* backgroundImage = GetBackgroundImage(fromNode, desktop);
	if (backgroundImage != NULL && poseView != NULL && poseView->ViewMode() != kListMode)
		backgroundImage->Show(poseView, current_workspace());

	return backgroundImage;
}
