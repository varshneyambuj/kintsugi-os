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
 * MIT License. Copyright 2002-2009, Haiku, Inc.
 * Original author: Jerome Duval.
 */

/** @file ImageFilePanel.h
    @brief BFilePanel specialization with an inline image preview. */

#ifndef IMAGE_FILE_PANEL_H
#define IMAGE_FILE_PANEL_H


#include <FilePanel.h>
#include <Node.h>


class BStringView;
class BView;


/**
 * @brief BRefFilter that admits only directories and image MIME types.
 *
 * When @ref fImageFiltering is false the filter only admits directories,
 * which is useful for "save into folder" panels that should not list files.
 */
class ImageFilter: public BRefFilter {
public:
							ImageFilter(bool filtering);
	virtual					~ImageFilter() {};

			bool			Filter(const entry_ref* ref, BNode* node,
								struct stat_beos* st, const char* filetype);

protected:
			bool			fImageFiltering;
								// true for images
								// false for directory
};


/**
 * @brief BFilePanel that adds a thumbnail preview, resolution string, and
 *        MIME description for the selected image.
 *
 * Used by the Backgrounds preferences app to let the user pick desktop
 * images while seeing a preview of each candidate.
 */
class ImageFilePanel: public BFilePanel {
public:
							ImageFilePanel(file_panel_mode mode = B_OPEN_PANEL,
								BMessenger* target = NULL,
								const entry_ref* startDirectory = NULL,
								uint32 nodeFlavors = 0,
								bool allowMultipleSelection = true,
								BMessage* message = NULL,
								BRefFilter* filter = NULL,
								bool modal = false,
								bool hideWhenDone = true);
							~ImageFilePanel();

	virtual	void			SelectionChanged();
			void			Show();

protected:
			BView*			fImageView;
			BStringView*	fResolutionView;
			BStringView*	fImageTypeView;
};

#endif	// IMAGE_FILE_PANEL_H
