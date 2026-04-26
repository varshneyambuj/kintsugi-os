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
 *   Copyright 2006, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file StringView.cpp
 * @brief Implementation of the StringView label/value pair widget used as
 *        a single row in MIME-type detail panels.
 */


#include <GroupView.h>
#include <LayoutItem.h>
#include <StringView.h>

#include "StringView.h"


/**
 * @brief Builds the label and value BStringView pair without yet adding
 *        them to a layout.
 *
 * The actual BGroupView is created lazily when the BView* cast operator
 * is first invoked.
 *
 * @param label  Text rendered as the static label.
 * @param text   Initial value text rendered next to the label.
 */
StringView::StringView(const char* label, const char* text)
	:
	fView(NULL),
	fLabel(new BStringView(NULL, label)),
	fLabelItem(NULL),
	fText(new BStringView(NULL, text)),
	fTextItem(NULL)
{
}


/**
 * @brief Replaces the label text shown to the left of the value.
 *
 * @param label  New label string.
 */
void
StringView::SetLabel(const char* label)
{
	fLabel->SetText(label);
}


/**
 * @brief Replaces the value text shown to the right of the label.
 *
 * @param text  New value string.
 */
void
StringView::SetText(const char* text)
{
	fText->SetText(text);
}


/**
 * @brief Returns the BLayoutItem owning the label view in the parent grid.
 *
 * @return Layout item for the label, or NULL if the view has not been
 *         realized yet.
 */
BLayoutItem*
StringView::GetLabelLayoutItem()
{
	return fLabelItem;
}


/**
 * @brief Returns the underlying BStringView used as the label.
 *
 * @return Pointer to the label view; never NULL.
 */
BView*
StringView::LabelView()
{ return fLabel; }


/**
 * @brief Returns the BLayoutItem owning the value view in the parent grid.
 *
 * @return Layout item for the value, or NULL if the view has not been
 *         realized yet.
 */
BLayoutItem*
StringView::GetTextLayoutItem()
{
	return fTextItem;
}


/**
 * @brief Returns the underlying BStringView used as the value.
 *
 * @return Pointer to the value view; never NULL.
 */
BView*
StringView::TextView()
{ return fText; }


/**
 * @brief Tints both views to reflect a disabled state.
 *
 * @param enabled  When false, both label and value are rendered with the
 *                 disabled-label tint; otherwise the standard control text
 *                 colour is used.
 */
void
StringView::SetEnabled(bool enabled)
{

	rgb_color color;

	if (!enabled) {
		color = tint_color(
			ui_color(B_PANEL_BACKGROUND_COLOR), B_DISABLED_LABEL_TINT);
	} else
		color = ui_color(B_CONTROL_TEXT_COLOR);

	fLabel->SetHighColor(color);
	fText->SetHighColor(color);
	fLabel->Invalidate();
	fText->Invalidate();
}


/**
 * @brief Lazy-builds (on first call) and returns the composed BGroupView
 *        suitable for inclusion in a parent layout.
 *
 * @return Pointer to the BGroupView holding the label and value views.
 *         Subsequent calls return the cached instance.
 */
StringView::operator BView*()
{
	if (fView)
		return fView;
	fView = new BGroupView(B_HORIZONTAL);
	BLayout* layout = fView->GroupLayout();
	fLabelItem = layout->AddView(fLabel);
	fTextItem = layout->AddView(fText);
	return fView;
}


/**
 * @brief Returns the current label text.
 *
 * @return Pointer owned by the label BStringView; valid until the next
 *         call that mutates the label.
 */
const char*
StringView::Label() const
{
	return fLabel->Text();
}


/**
 * @brief Returns the current value text.
 *
 * @return Pointer owned by the value BStringView; valid until the next
 *         call that mutates the value.
 */
const char*
StringView::Text() const
{
	return fText->Text();
}

