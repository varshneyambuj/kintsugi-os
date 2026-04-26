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
 * MIT License. Copyright 2006, Axel Dörfler, axeld@pinc-software.de.
 */

/**
 * @file ApplicationTypesWindow.h
 * @brief Browser window listing every registered application MIME type and
 *        the metadata of the currently selected entry.
 */

#ifndef APPLICATION_TYPES_WINDOW_H
#define APPLICATION_TYPES_WINDOW_H


#include <Alert.h>
#include <Mime.h>
#include <Window.h>

class BButton;
class BListView;
class BMenuField;
class BMimeType;
class BOutlineListView;
class BStringView;
class BTextView;

class MimeTypeListView;
class StringView;


/**
 * @brief Window listing all installed application signatures and exposing
 *        their metadata, launch controls, and uninstall actions.
 */
class ApplicationTypesWindow : public BWindow {
	public:
		ApplicationTypesWindow(const BMessage& settings);
		virtual ~ApplicationTypesWindow();

		virtual void MessageReceived(BMessage* message);
		virtual bool QuitRequested();

	private:
		BRect _Frame(const BMessage& settings) const;
		void _SetType(BMimeType* type, int32 forceUpdate = 0);
		void _UpdateCounter();
		void _RemoveUninstalled();

	private:
		BMimeType		fCurrentType;

		MimeTypeListView* fTypeListView;
		BButton*		fRemoveTypeButton;

		StringView*		fNameView;
		StringView*		fSignatureView;
		StringView*		fPathView;

		StringView*		fVersionView;
		StringView*		fDescriptionLabel;
		BTextView*		fDescriptionView;

		BButton*		fTrackerButton;
		BButton*		fLaunchButton;
		BButton*		fEditButton;
};

#endif	// APPLICATION_TYPES_WINDOW_H
