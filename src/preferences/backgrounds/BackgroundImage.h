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
 * Incorporates work from Tracker, originally licensed under the Open
 * Tracker License. Copyright (c) 1991-2000, Be Incorporated.
 * Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered
 * trademarks of Be Incorporated.
 */

/** @file BackgroundImage.h
    @brief Persistent representation of one or more desktop/folder wallpapers. */

#ifndef __BACKGROUND_IMAGE__
#define __BACKGROUND_IMAGE__


#include <GraphicsDefs.h>
#include <Node.h>
#include <Path.h>

#include "ObjectList.h"
#include "String.h"


class BView;
class BBitmap;

class BackgroundImage;
class Image;
class BackgroundsView;

extern const char* kBackgroundImageInfo;
extern const char* kBackgroundImageInfoOffset;
extern const char* kBackgroundImageInfoEraseText;
extern const char* kBackgroundImageInfoMode;
extern const char* kBackgroundImageInfoWorkspaces;
extern const char* kBackgroundImageInfoPath;
extern const char* kBackgroundImageInfoSet;
extern const char* kBackgroundImageInfoSetPeriod;

/** @brief Tracker message asking the desktop to repaint its background. */
const uint32 kRestoreBackgroundImage = 'Tbgr';
/** @brief Tracker message announcing that the background metadata changed. */
const uint32 kChangeBackgroundImage = 'Cbgr';

/**
 * @brief Workspace-aware bitmap manager for the desktop and folder windows.
 *
 * Knows which BBitmap to use for a given view and how to render it.
 * Unlike folder windows, the desktop can carry a different background
 * for each workspace.
 */
class BackgroundImage {
public:

	/** @brief Rendering mode for one image entry. */
	enum Mode {
		kAtOffset,
		kCentered,			// only works on Desktop
		kScaledToFit,		// only works on Desktop
		kTiled
	};

	/**
	 * @brief Per-workspace record describing a single bitmap entry.
	 */
	class BackgroundImageInfo {
	public:
		BackgroundImageInfo(uint32 workspace, int32 imageIndex, Mode mode,
			BPoint offset, bool textWidgetLabelOutline, uint32 imageSet,
			uint32 cacheMode);
		~BackgroundImageInfo();

		void LoadBitmap();
		void UnloadBitmap(uint32 globalCacheMode);

		uint32 fWorkspace;
		int32 fImageIndex;
		Mode fMode;
		BPoint fOffset;
		bool fTextWidgetLabelOutline;
		uint32 fImageSet;
		uint32 fCacheMode;		// image cache strategy (0 cache , 1 no cache)
	};

	static BackgroundImage* GetBackgroundImage(const BNode* node,
		bool isDesktop, BackgroundsView* view);
		// create a BackgroundImage object by reading it from a node

	virtual ~BackgroundImage();

	void Show(BView* view, int32 workspace);
		// display the right background for a given workspace
	void Remove();
		// remove the background from it's current view

	void WorkspaceActivated(BView* view, int32 workspace, bool state);
		// respond to a workspace change
	void ScreenChanged(BRect rect, color_space space);
		// respond to a screen size change
	/*static BackgroundImage* Refresh(BackgroundImage* oldBackgroundImage,
		const BNode* fromNode, bool desktop, BPoseView* poseView);
		// respond to a background image setting change
	void ChangeImageSet(BPoseView* poseView);
		// change to the next imageSet if any, no refresh*/
	BackgroundImageInfo* ImageInfoForWorkspace(int32) const;

	bool IsDesktop() { return fIsDesktop;}

	status_t SetBackgroundImage(BNode* node);

	void Show(BackgroundImageInfo*, BView* view);

	uint32 GetShowingImageSet() { return fShowingImageSet; }

	void Add(BackgroundImageInfo*);
	void Remove(BackgroundImageInfo*);
	void RemoveAll();

private:
	BackgroundImage(const BNode* node, bool isDesktop, BackgroundsView* view);
		// no public constructor, GetBackgroundImage factory function is
		// used instead

	float BRectRatio(BRect rect);
	float BRectHorizontalOverlap(BRect hostRect, BRect resizedRect);
	float BRectVerticalOverlap(BRect hostRect, BRect resizedRect);

	bool fIsDesktop;
	BNode fDefinedByNode;
	BView* fView;
	BackgroundsView* fBackgroundsView;
	BackgroundImageInfo* fShowingBitmap;

	BObjectList<BackgroundImageInfo, true> fBitmapForWorkspaceList;

	uint32 fImageSetPeriod;		// period between imagesets, 0 if none
	uint32 fShowingImageSet;	// current imageset
	uint32 fImageSetCount;		// imageset count
	uint32 fCacheMode;// image cache strategy (0 all, 1 none, 2 own strategy)
	bool fRandomChange; 		// random or sequential change
};

/**
 * @brief Lazily-decoded BBitmap together with its source path and label.
 */
class Image {
public:
	Image(BPath path);
	~Image();

	void UnloadBitmap();
	const char* GetName() { return fName.String(); }
	BBitmap* GetBitmap();
	BPath GetPath() { return fPath; }
private:
	BBitmap* fBitmap;
	BPath fPath;
	BString fName;
};

#endif

