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
 * @file StringView.h
 * @brief Compound layout helper that pairs a label BStringView with a value
 *        BStringView so MIME-type detail rows align in grid layouts.
 */

#ifndef STRING_VIEW_H
#define STRING_VIEW_H


class BLayoutItem;
class BGroupView;
class BStringView;

/**
 * @brief Label/value string pair used as a single logical row in property
 *        panels (signature, path, version, etc.).
 */
class StringView {
	public:
		StringView(const char* label,
			const char* text);

		void SetEnabled(bool enabled);

		void SetLabel(const char* label);
		const char* Label() const;
		void SetText(const char* text);
		const char* Text() const;

		BLayoutItem* GetLabelLayoutItem();
		BView* LabelView();
		BLayoutItem* GetTextLayoutItem();
		BView* TextView();

		operator BView*();

	private:

		BGroupView*		fView;
		BStringView*	fLabel;
		BLayoutItem*	fLabelItem;
		BStringView*	fText;
		BLayoutItem*	fTextItem;
};


#endif	// STRING_VIEW_H
