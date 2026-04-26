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
 *   Copyright 2002-2009 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Jerome Duval, jerome.duval@free.fr
 */


/**
 * @file ImageFilePanel.cpp
 * @brief BFilePanel specialization with an image preview for the
 *        Backgrounds preferences app.
 *
 * Augments the standard file panel with a small thumbnail view plus
 * resolution and image-type readouts using BTranslationUtils to decode the
 * selected file. ImageFilter restricts the visible entries to directories
 * and files with an image MIME supertype.
 *
 * @see BFilePanel, BTranslationUtils
 */


#include "ImageFilePanel.h"

#include <Bitmap.h>
#include <Catalog.h>
#include <Locale.h>
#include <NodeInfo.h>
#include <String.h>
#include <StringView.h>
#include <TranslationUtils.h>
#include <Window.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Image Filepanel"


//	#pragma mark - ImageFilePanel


/**
 * @brief Forwards all parameters to BFilePanel and zeroes preview view
 *        pointers.
 *
 * The preview views are created lazily in Show() so they are only added if
 * the panel is actually displayed.
 *
 * @param mode                    File panel mode (open, save, etc.).
 * @param target                  Messenger that will receive selection events.
 * @param startDirectory          Initial directory; may be NULL.
 * @param nodeFlavors             Allowed node flavors (file, directory, ...).
 * @param allowMultipleSelection  Whether the user can select more than one.
 * @param message                 Custom message template; may be NULL.
 * @param filter                  Optional ref filter (e.g. ImageFilter).
 * @param modal                   Whether the panel runs modally.
 * @param hideWhenDone            Whether to hide instead of close on accept.
 */
ImageFilePanel::ImageFilePanel(file_panel_mode mode, BMessenger* target,
	const entry_ref* startDirectory, uint32 nodeFlavors,
	bool allowMultipleSelection, BMessage* message, BRefFilter* filter,
	bool modal, bool hideWhenDone)
	:
	BFilePanel(mode, target, startDirectory, nodeFlavors,
		allowMultipleSelection, message, filter, modal, hideWhenDone),
	fImageView(NULL),
	fResolutionView(NULL),
	fImageTypeView(NULL)
{
}


/**
 * @brief Deletes the ref filter installed via the constructor, if any.
 */
ImageFilePanel::~ImageFilePanel()
{
	if (RefFilter())
		delete RefFilter();
}


/**
 * @brief Adds the preview, resolution and image-type views on first show.
 *
 * Locks the underlying panel window, temporarily clamps the resizing modes
 * of the standard file panel views so growing the window only affects the
 * panel itself, then resizes downward to make room for the preview strip.
 * Subsequent calls just defer to BFilePanel::Show().
 */
void
ImageFilePanel::Show()
{
	if (fImageView == NULL) {
		Window()->Lock();
		BView* background = Window()->ChildAt(0);
		uint32 poseViewResizingMode
			= background->FindView("PoseView")->ResizingMode();
		uint32 countVwResizingMode
			= background->FindView("CountVw")->ResizingMode();
		uint32 vScrollBarResizingMode
			= background->FindView("VScrollBar")->ResizingMode();
		uint32 hScrollBarResizingMode
			= background->FindView("HScrollBar")->ResizingMode();

		background->FindView("PoseView")
			->SetResizingMode(B_FOLLOW_LEFT | B_FOLLOW_TOP);
		background->FindView("CountVw")
			->SetResizingMode(B_FOLLOW_LEFT | B_FOLLOW_TOP);
		background->FindView("VScrollBar")
			->SetResizingMode(B_FOLLOW_LEFT | B_FOLLOW_TOP);
		background->FindView("HScrollBar")
			->SetResizingMode(B_FOLLOW_LEFT | B_FOLLOW_TOP);
		Window()->ResizeBy(0, 70);
		background->FindView("PoseView")->SetResizingMode(poseViewResizingMode);
		background->FindView("CountVw")->SetResizingMode(countVwResizingMode);
		background->FindView("VScrollBar")
			->SetResizingMode(vScrollBarResizingMode);
		background->FindView("HScrollBar")
			->SetResizingMode(hScrollBarResizingMode);

		BRect rect(background->Bounds().left + 15,
			background->Bounds().bottom - 94, background->Bounds().left + 122,
			background->Bounds().bottom - 15);
		fImageView = new BView(rect, "ImageView",
			B_FOLLOW_LEFT | B_FOLLOW_BOTTOM, B_SUBPIXEL_PRECISE);
		fImageView->SetViewColor(background->ViewColor());
		background->AddChild(fImageView);

		rect = BRect(background->Bounds().left + 132,
			background->Bounds().bottom - 85, background->Bounds().right,
			background->Bounds().bottom - 65);
		fResolutionView = new BStringView(rect, "ResolutionView", NULL,
			B_FOLLOW_LEFT | B_FOLLOW_BOTTOM);
		background->AddChild(fResolutionView);

		rect.OffsetBy(0, -16);
		fImageTypeView = new BStringView(rect, "ImageTypeView", NULL,
			B_FOLLOW_LEFT | B_FOLLOW_BOTTOM);
		background->AddChild(fImageTypeView);

		Window()->Unlock();
	}

	BFilePanel::Show();
}


/**
 * @brief Refreshes the preview thumbnail and metadata for the selection.
 *
 * Reads the selected ref through BTranslationUtils, scales the bitmap to
 * fit the preview view while preserving aspect, and shows the resolution
 * plus a short MIME description. Non-file selections clear the preview.
 */
void
ImageFilePanel::SelectionChanged()
{
	entry_ref ref;
	Rewind();

	if (GetNextSelectedRef(&ref) == B_OK) {
		BEntry entry(&ref);
		BNode node(&ref);
		fImageView->ClearViewBitmap();

		if (node.IsFile()) {
			BBitmap* bitmap = BTranslationUtils::GetBitmap(&ref);

			if (bitmap != NULL) {
				BRect dest(fImageView->Bounds());
				if (bitmap->Bounds().Width() > bitmap->Bounds().Height()) {
					dest.InsetBy(0, (dest.Height() + 1
						- ((bitmap->Bounds().Height() + 1)
						/ (bitmap->Bounds().Width() + 1)
						* (dest.Width() + 1))) / 2);
				} else {
					dest.InsetBy((dest.Width() + 1
						- ((bitmap->Bounds().Width() + 1)
						/ (bitmap->Bounds().Height() + 1)
						* (dest.Height() + 1))) / 2, 0);
				}
				fImageView->SetViewBitmap(bitmap, bitmap->Bounds(), dest,
					B_FOLLOW_LEFT | B_FOLLOW_TOP, 0);

				BString resolution;
				resolution << B_TRANSLATE("Resolution: ")
					<< (int)(bitmap->Bounds().Width() + 1)
					<< "x" << (int)(bitmap->Bounds().Height() + 1);
				fResolutionView->SetText(resolution.String());
				delete bitmap;

				BNodeInfo nodeInfo(&node);
				char type[B_MIME_TYPE_LENGTH];
				if (nodeInfo.GetType(type) == B_OK) {
					BMimeType mimeType(type);
					mimeType.GetShortDescription(type);
					// if this fails, the MIME type will be displayed
					fImageTypeView->SetText(type);
				} else {
					BMimeType refType;
					if (BMimeType::GuessMimeType(&ref, &refType) == B_OK) {
						refType.GetShortDescription(type);
						// if this fails, the MIME type will be displayed
						fImageTypeView->SetText(type);
					} else
						fImageTypeView->SetText("");
				}
			}
		} else {
			fResolutionView->SetText("");
			fImageTypeView->SetText("");
		}
		fImageView->Invalidate();
		fResolutionView->Invalidate();
	}

	BFilePanel::SelectionChanged();
}


//	#pragma mark - ImageFilter


/**
 * @brief Constructs an image filter that may be enabled or disabled.
 *
 * @param filtering  When @c true, only directories and image MIME types are
 *                   accepted; when @c false the filter passes directories
 *                   only (and rejects all files).
 */
ImageFilter::ImageFilter(bool filtering)
	:
	fImageFiltering(filtering)
{
}


/**
 * @brief Decides whether the file panel should display an entry.
 *
 * Directories are always shown when filtering is enabled; files are shown
 * only when their sniffed MIME type is contained by the "image" supertype.
 *
 * @param ref       Entry being considered.
 * @param node      Open BNode for sniffing the MIME type.
 * @param stat      Stat block (unused here).
 * @param filetype  Type string supplied by the panel (unused here).
 * @return          @c true to display the entry, @c false to hide it.
 */
bool
ImageFilter::Filter(const entry_ref* ref, BNode* node,
	struct stat_beos* stat, const char* filetype)
{
	bool isDirectory = node->IsDirectory();
	if (!fImageFiltering || isDirectory)
		return isDirectory;

	BMimeType imageType("image");
	BMimeType refType;
	if (BMimeType::GuessMimeType(ref, &refType) == B_OK)
		return imageType.Contains(&refType);

	return false;
}
