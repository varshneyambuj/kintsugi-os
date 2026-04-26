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
 * MIT License. Copyright 2002-2009 Haiku, Inc.
 * Original authors: Jerome Duval.
 */

/** @file BackgroundsView.h
    @brief Main view of the Backgrounds preference application. */

#ifndef BACKGROUNDS_VIEW_H
#define BACKGROUNDS_VIEW_H


#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <ColorControl.h>
#include <Control.h>
#include <Cursor.h>
#include <Entry.h>
#include <FilePanel.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <Picture.h>
#include <Screen.h>
#include <ScrollView.h>
#include <ScrollBar.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <View.h>

#include "BackgroundImage.h"


#define SETTINGS_FILE		"Backgrounds_settings"


class ImageFilePanel;


/**
 * @brief BMenuItem that remembers an index into the cached image list.
 */
class BGImageMenuItem : public BMenuItem {
public:
							BGImageMenuItem(const char* label, int32 imageIndex,
								BMessage* message, char shortcut = 0,
								uint32 modifiers = 0);

			int32			ImageIndex() { return fImageIndex; }

private:
			int32			fImageIndex;
};


/** @brief Indexes into the 3x3 grid that decorates the preview area. */
enum frame_parts {
	FRAME_TOP_LEFT = 0,
	FRAME_TOP,
	FRAME_TOP_RIGHT,
	FRAME_LEFT_SIDE,
	FRAME_RIGHT_SIDE,
	FRAME_BOTTOM_LEFT,
	FRAME_BOTTOM,
	FRAME_BOTTOM_RIGHT,
};


class FramePart : public BView {
public:
								FramePart(int32 part);

	virtual	void				Draw(BRect rect);
			void				SetDesktop(bool isDesktop);

private:
			void				_SetSizeAndAlignment();

			int32				fFramePart;
			bool				fIsDesktop;
};


class Preview : public BControl {
public:
								Preview();

			BPoint				fPoint;
			BRect				fImageBounds;

protected:
	virtual	void				MouseDown(BPoint point);
	virtual	void				MouseUp(BPoint point);
	virtual	void				MouseMoved(BPoint point, uint32 transit,
									const BMessage* message);
	virtual	void				AttachedToWindow();

			BPoint				fOldPoint;
			float				fXRatio;
			float				fYRatio;
			display_mode		fMode;
};


class BackgroundsView : public BBox {
public:
								BackgroundsView();
								~BackgroundsView();

			void				AllAttached();
			void				MessageReceived(BMessage* message);

			void				RefsReceived(BMessage* message);

			void				SaveSettings();
			void				WorkspaceActivated(uint32 oldWorkspaces,
									bool active);
			int32				AddImage(BPath path);
			Image*				GetImage(int32 imageIndex);

			bool				FoundPositionSetting();

protected:
			void				_Save();
			void				_NotifyServer();
			void				_NotifyScreenPreflet();
			void				_LoadSettings();
			void				_LoadDesktopFolder();
			void				_LoadDefaultFolder();
			void				_LoadFolder(bool isDesktop);
			void				_LoadRecentFolder(BPath path);
			void				_UpdateWithCurrent();
			void				_UpdatePreview();
			void				_UpdateButtons();
			void				_SetDesktop(bool isDesktop);
			void				_AddRecentFolder(BPath path,
									bool notifyApp = false);

	static	int32				_NotifyThread(void* data);

			BGImageMenuItem*	_FindImageItem(const int32 imageIndex);

			bool				_AddItem(BGImageMenuItem* item);

			BackgroundImage::Mode	_FindPlacementMode();

			BColorControl*		fPicker;
			BButton*			fApply;
			BButton*			fRevert;
			BCheckBox*			fIconLabelOutline;
			BMenu*				fPlacementMenu;
			BMenu*				fImageMenu;
			BMenu*				fWorkspaceMenu;
			BTextControl*		fXPlacementText;
			BTextControl*		fYPlacementText;
			Preview*			fPreview;
			BFilePanel*			fFolderPanel;
			ImageFilePanel*		fPanel;

			BackgroundImage*	fCurrent;

			BackgroundImage::BackgroundImageInfo*	fCurrentInfo;

			entry_ref			fCurrentRef;
			int32				fLastImageIndex;
			int32				fRecentFoldersLimit;
			BMessage			fSettings;

			BObjectList<BPath, true> fPathList;
			BObjectList<Image, true> fImageList;

			FramePart*			fTopLeft;
			FramePart*			fTop;
			FramePart*			fTopRight;
			FramePart*			fLeft;
			FramePart*			fRight;
			FramePart*			fBottomLeft;
			FramePart*			fBottom;
			FramePart*			fBottomRight;

			bool				fFoundPositionSetting;
};

#endif	// BACKGROUNDS_VIEW_H
