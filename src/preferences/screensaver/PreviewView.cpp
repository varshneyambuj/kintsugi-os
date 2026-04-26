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
 *   Copyright 2003-2015 Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Phipps
 *       Jérôme Duval, jerome.duval@free.fr
 */


/**
 * @file PreviewView.cpp
 * @brief Implementation of the stylized monitor preview view.
 *
 * Draws a faux-CRT chassis with rounded edges, a power-light dot, and a
 * dark inner area. Hosts either a child preview BView (driven by the
 * screensaver add-on) or a text overlay reading "No preview available".
 *
 * @see ScreenSaverWindow
 */


#include "PreviewView.h"

#include <algorithm>
#include <iostream>

#include <CardLayout.h>
#include <Catalog.h>
#include <GroupLayout.h>
#include <Point.h>
#include <Rect.h>
#include <Size.h>
#include <StringView.h>
#include <TextView.h>

#include "Utility.h"


/** @brief Pure white text color used by the "no preview" overlay. */
static const rgb_color kWhite = (rgb_color){ 255, 255, 255 };


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PreviewView"


/** @brief Horizontal sample positions (unit square) used by the local scale2 helpers. */
static float sampleX[]
	= { 0, 0.05, 0.15, 0.7, 0.725, 0.8, 0.825, 0.85, 0.950, 1.0 };
/** @brief Vertical sample positions (unit square) used by the local scale2 helpers. */
static float sampleY[] = { 0, 0.05, 0.90, 0.95, 0.966, 0.975, 1.0 };


/**
 * @brief Indexed point lookup against the local sampleX/sampleY tables.
 *
 * @param x    Index into @c sampleX.
 * @param y    Index into @c sampleY.
 * @param area Reference rectangle for the unit square.
 * @return Scaled point inside @a area.
 */
inline BPoint
scale2(int x, int y, BRect area)
{
	return scale_direct(sampleX[x], sampleY[y], area);
}


/**
 * @brief Indexed rectangle lookup against the local sampleX/sampleY tables.
 *
 * @param x1   Left index into @c sampleX.
 * @param x2   Right index into @c sampleX.
 * @param y1   Top index into @c sampleY.
 * @param y2   Bottom index into @c sampleY.
 * @param area Reference rectangle for the unit square.
 * @return Scaled rectangle inside @a area.
 */
inline BRect
scale2(int x1, int x2, int y1, int y2, BRect area)
{
	return scale_direct(sampleX[x1], sampleX[x2], sampleY[y1], sampleY[y2],
		area);
}


//	#pragma mark - PreviewView


/**
 * @brief Constructs the preview view and its hidden "no preview" placeholder.
 *
 * Sets up the layout (a vertical group with custom insets that leave room
 * for the painted monitor frame) and creates a black-backed BTextView with
 * white text reading "No preview available", which is initially hidden.
 *
 * @param name Internal BView name.
 */
PreviewView::PreviewView(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fSaverView(NULL),
	fNoPreview(NULL)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	BGroupLayout* layout = new BGroupLayout(B_VERTICAL);
	// We draw the "monitor" around the preview, hence the strange insets.
	layout->SetInsets(7, 6, 8, 12);
	SetLayout(layout);

	// A BStringView would be enough, if only it handled word wrapping.
	fNoPreview = new BTextView("no preview");
	fNoPreview->SetText(B_TRANSLATE("No preview available"));
	fNoPreview->SetFontAndColor(be_plain_font, B_FONT_ALL, &kWhite);
	fNoPreview->MakeEditable(false);
	fNoPreview->MakeResizable(false);
	fNoPreview->MakeSelectable(false);
	fNoPreview->SetViewColor(0, 0, 0);
	fNoPreview->SetLowColor(0, 0, 0);

	fNoPreview->Hide();

	BView* container = new BView("preview container", 0);
	container->SetLayout(new BCardLayout());
	AddChild(container);
	container->SetViewColor(0, 0, 0);
	container->SetLowColor(0, 0, 0);
	container->AddChild(fNoPreview);

	fNoPreview->SetHighColor(255, 255, 255);
	fNoPreview->SetAlignment(B_ALIGN_CENTER);
}


/**
 * @brief Destroys the preview view; the child views are owned by BView.
 */
PreviewView::~PreviewView()
{
}


/**
 * @brief BView Draw hook: paints the stylized CRT chassis.
 *
 * Renders the outer rounded body, the screen bezel, the speaker grille,
 * and a green LED power indicator. The actual preview content is drawn by
 * the child views laid into the container.
 *
 * @param updateRect Region requested by the app_server; used as a hint to
 *                   skip outer-frame work when only the inner area needs
 *                   refreshing.
 */
void
PreviewView::Draw(BRect updateRect)
{
	SetHighColor(184, 184, 184);
	FillRoundRect(scale2(0, 9, 0, 3, Bounds()), 4, 4);
		// outer shape
	FillRoundRect(scale2(2, 7, 3, 6, Bounds()), 2, 2);
		// control console outline

	SetHighColor(96, 96, 96);
	StrokeRoundRect(scale2(2, 7, 3, 6, Bounds()), 2, 2);
		// control console outline
	StrokeRoundRect(scale2(0, 9, 0, 3, Bounds()), 4, 4);
		// outline outer shape

	SetHighColor(0, 0, 0);
	FillRect(scale2(1, 8, 1, 2, Bounds()));

	SetHighColor(184, 184, 184);
	BRect outerShape = scale2(2, 7, 2, 6, Bounds());
	outerShape.InsetBy(1, 1);
	FillRoundRect(outerShape, 4, 4);
		// outer shape

	SetHighColor(0, 255, 0);
	FillRect(scale2(3, 4, 4, 5, Bounds()));
	SetHighColor(96, 96, 96);
	FillRect(scale2(5, 6, 4, 5, Bounds()));
}


/**
 * @brief Creates and inserts a fresh child preview view sized to a 4:3 frame.
 *
 * The width scales with the system plain font size so previews stay
 * legible on high-DPI configurations. The "no preview" overlay is sized
 * to match.
 *
 * @return Pointer to the newly created preview BView; ownership remains
 *         with this PreviewView.
 */
BView*
PreviewView::AddPreview()
{
	fSaverView = new BView("preview", B_WILL_DRAW);
	fSaverView->SetViewColor(0, 0, 0);
	fSaverView->SetLowColor(0, 0, 0);
	ChildAt(0)->AddChild(fSaverView);

	float aspectRatio = 4.0f / 3.0f;
		// 4:3 monitor
	float previewWidth = 120.0f * std::max(1.0f, be_plain_font->Size() / 12.0f);
	float previewHeight = ceilf(previewWidth / aspectRatio);

	fSaverView->SetExplicitSize(BSize(previewWidth, previewHeight));
	fSaverView->ResizeTo(previewWidth, previewHeight);

	fNoPreview->SetExplicitSize(BSize(previewWidth, previewHeight));
	fNoPreview->ResizeTo(previewWidth, previewHeight);
	fNoPreview->SetTextRect(BRect(0, 0, previewWidth, previewHeight));
	fNoPreview->SetInsets(0, previewHeight / 3, 0, 0);

	return fSaverView;
}


/**
 * @brief Detaches the current preview child view and shows the placeholder.
 *
 * @return The detached preview BView; the caller takes ownership and is
 *         expected to delete it. Returns @c NULL when no preview existed.
 */
BView*
PreviewView::RemovePreview()
{
	ShowNoPreview();

	if (fSaverView != NULL)
		ChildAt(0)->RemoveChild(fSaverView);

	BView* saverView = fSaverView;
	fSaverView = NULL;
	return saverView;
}


/**
 * @brief Returns the current preview child view, or @c NULL when none is set.
 */
BView*
PreviewView::SaverView()
{
	return fSaverView;
}


/**
 * @brief Brings the "No preview available" overlay to the front.
 */
void
PreviewView::ShowNoPreview() const
{
	((BCardLayout*)ChildAt(0)->GetLayout())->SetVisibleItem((int32)0);
}


/**
 * @brief Hides the "No preview available" overlay and reveals the saver view.
 */
void
PreviewView::HideNoPreview() const
{
	((BCardLayout*)ChildAt(0)->GetLayout())->SetVisibleItem(1);
}
