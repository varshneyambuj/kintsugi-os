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
 * MIT License. Copyright 2003-2013, Haiku.
 * Original authors: Michael Phipps, Jérôme Duval.
 */

/** @file PreviewView.h
    @brief BView that draws a stylized monitor and hosts the live screensaver preview. */

#ifndef PREVIEW_VIEW_H
#define PREVIEW_VIEW_H


#include <View.h>


class BTextView;

/**
 * @brief Stylized monitor view that frames the screensaver preview.
 *
 * The view paints a small CRT-like graphic; its contents area can be
 * filled either with the live preview (a child BView driven by the
 * screensaver add-on) or with a "no preview available" message.
 */
class PreviewView : public BView {
public:
								PreviewView(const char* name);
	virtual						~PreviewView();

	virtual	void				Draw(BRect updateRect);

			BView*				AddPreview();
			BView*				RemovePreview();
			BView*				SaverView();

			void				ShowNoPreview() const;
			void				HideNoPreview() const;

private:
			BView*				fSaverView;
			BTextView*			fNoPreview;
};


#endif	// PREVIEW_VIEW_H
