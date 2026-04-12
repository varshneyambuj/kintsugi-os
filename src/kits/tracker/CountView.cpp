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
 * @file CountView.cpp
 * @brief Status-bar view drawn in the bottom-left corner of every Tracker window.
 *
 * BCountView displays the number of items and the number of selected items in the
 * associated BPoseView. It also hosts a barber-pole busy indicator and can show
 * type-ahead and filter strings overlaid on the count text.
 *
 * @see BPoseView, BContainerWindow
 */


#include "CountView.h"

#include <Application.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Locale.h>
#include <StringFormat.h>

#include "AutoLock.h"
#include "Bitmaps.h"
#include "ContainerWindow.h"
#include "DirMenu.h"
#include "PoseView.h"
#include "Utilities.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "CountView"


const bigtime_t kBarberPoleDelay = 500000;
static const float kMinFontSize = 8.0f;


//	#pragma mark - BCountView


/**
 * @brief Construct the count view and load the barber-pole bitmap from Tracker resources.
 *
 * @param view  The BPoseView this status bar is associated with.
 */
BCountView::BCountView(BPoseView* view)
	:
	BView("CountVw", B_PULSE_NEEDED | B_WILL_DRAW),
	fLastCount(-1),
	fLastCountSelected(-1),
	fPoseView(view),
	fShowingBarberPole(false),
	fBarberPoleMap(NULL),
	fLastBarberPoleOffset(5),
	fStartSpinningAfter(0),
	fTypeAheadString(""),
	fFilterString("")
{
	GetTrackerResources()->GetBitmapResource(B_MESSAGE_TYPE, R_BarberPoleBitmap, &fBarberPoleMap);

	SetFont(be_plain_font);
	SetFontSize(std::max(kMinFontSize, ceilf(be_plain_font->Size() * 0.75f)));

	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowUIColor(ViewUIColor());
}


/**
 * @brief Destructor; frees the barber-pole bitmap.
 */
BCountView::~BCountView()
{
	delete fBarberPoleMap;
}


/**
 * @brief Advance and invalidate the barber-pole animation if it is currently visible.
 *
 * Called from Pulse(); the pole is not animated until after kBarberPoleDelay.
 */
void
BCountView::TrySpinningBarberPole()
{
	if (!fShowingBarberPole)
		return;

	if (fStartSpinningAfter && system_time() < fStartSpinningAfter)
		return;

	// When the barber pole just starts spinning we need to invalidate
	// the whole rectangle of text and barber pole.
	// After this the text needs no updating since only the pole changes.
	if (fStartSpinningAfter) {
		fStartSpinningAfter = 0;
		Invalidate(TextAndBarberPoleRect());
	} else
		Invalidate(BarberPoleInnerRect());
}


/**
 * @brief BView pulse handler; drives the barber-pole animation.
 */
void
BCountView::Pulse()
{
	TrySpinningBarberPole();
}


/**
 * @brief Stop the barber-pole animation and redraw the view.
 */
void
BCountView::EndBarberPole()
{
	if (!fShowingBarberPole)
		return;

	fShowingBarberPole = false;
	Invalidate();
}


/**
 * @brief Begin the barber-pole animation after the standard delay.
 */
void
BCountView::StartBarberPole()
{
	AutoLock<BWindow> lock(Window());
	if (fShowingBarberPole)
		return;

	fShowingBarberPole = true;
	fStartSpinningAfter = system_time() + kBarberPoleDelay;
		// wait a bit before showing the barber pole
}


/**
 * @brief Return the inner rectangle used to clip the scrolling barber-pole bitmap.
 *
 * @return The clipping rectangle in view coordinates.
 */
BRect
BCountView::BarberPoleInnerRect() const
{
	BRect result = Bounds();
	result.InsetBy(3, 4);
	result.left = result.right - 7;
	result.bottom = result.top + 7;
	return result;
}


/**
 * @brief Return the outer rectangle drawn as a border around the barber pole.
 *
 * @return The border rectangle in view coordinates (one pixel larger than BarberPoleInnerRect).
 */
BRect
BCountView::BarberPoleOuterRect() const
{
	BRect result(BarberPoleInnerRect());
	result.InsetBy(-1, -1);
	return result;
}


/**
 * @brief Return the rectangle that needs to be invalidated when the count text changes.
 *
 * Excludes the barber-pole area when the pole is visible.
 *
 * @return The text invalidation rectangle.
 */
BRect
BCountView::TextInvalRect() const
{
	BRect result = TextAndBarberPoleRect();

	// if the barber pole is not present, use its space for text
	if (fShowingBarberPole)
		result.right -= 10;

	return result;
}


/**
 * @brief Return the inset rectangle encompassing both the count text and barber-pole area.
 *
 * @return The combined text-and-pole rectangle.
 */
BRect
BCountView::TextAndBarberPoleRect() const
{
	BRect result = Bounds();
	result.InsetBy(be_control_look->ComposeSpacing(B_USE_SMALL_SPACING) / 2,
		floorf(result.Height() * 0.25f));

	return result;
}


/**
 * @brief Synchronise the cached item/selection counts with the pose view and invalidate if changed.
 *
 * Should be called whenever the pose view's contents may have changed.
 */
void
BCountView::CheckCount()
{
	bool invalidate = false;

	if (fLastCount != fPoseView->CountItems()) {
		fLastCount = fPoseView->CountItems();
		invalidate = true;
	}

	if (fLastCountSelected != fPoseView->CountSelected()) {
		fLastCountSelected = fPoseView->CountSelected();
		invalidate = true;
	}

	// invalidate the count text area if necessary
	if (invalidate)
		Invalidate(TextInvalRect());

	// invalidate barber pole area if necessary
	TrySpinningBarberPole();
}


/**
 * @brief Paint the status bar, including the count string and optional barber pole.
 *
 * @param updateRect  The dirty rectangle passed by the rendering system.
 */
void
BCountView::Draw(BRect updateRect)
{
	BRect bounds(Bounds());

	rgb_color color = ViewColor();
	if (IsTypingAhead())
		color = ui_color(B_DOCUMENT_BACKGROUND_COLOR);

	SetLowColor(color);
	be_control_look->DrawBorder(this, bounds, updateRect,
		color, B_PLAIN_BORDER, 0,
		BControlLook::B_BOTTOM_BORDER | BControlLook::B_LEFT_BORDER);
	be_control_look->DrawMenuBarBackground(this, bounds, updateRect, color);

	BString itemString;
	if (IsTypingAhead())
		itemString << TypeAhead();
	else if (IsFiltering()) {
		BString lastCountStr;
		fNumberFormat.Format(lastCountStr, fLastCount);

		if (fLastCountSelected != 0) {
			static BStringFormat selectedFilteredFormat(B_TRANSLATE_COMMENT(
				"{0, plural, other{#/%total %filter}}",
				"Number of selected items from a filtered set: \"10/30 view\""));

			selectedFilteredFormat.Format(itemString, fLastCountSelected);
			itemString.ReplaceFirst("%total", lastCountStr);
			itemString.ReplaceFirst("%filter", Filter());
		} else
			itemString << lastCountStr << " " << Filter();
	} else {
		if (fLastCount == 0)
			itemString << B_TRANSLATE("no items");
		else if (fLastCountSelected == 0) {
			static BStringFormat itemFormat(B_TRANSLATE_COMMENT(
				"{0, plural, one{# item} other{# items}}",
				"Number of selected items: \"1 item\" or \"2 items\""));
			itemFormat.Format(itemString, fLastCount);
		} else {
			static BStringFormat selectedFormat(B_TRANSLATE_COMMENT(
				"{0, plural, other{#/%total selected}}",
				"Number of selected items out of a total: \"10/30 selected\""));

			BString lastCountStr;
			fNumberFormat.Format(lastCountStr, fLastCount);

			selectedFormat.Format(itemString, fLastCountSelected);
			itemString.ReplaceFirst("%total", lastCountStr);
		}
	}

	BRect textRect(TextInvalRect());

	TruncateString(&itemString, IsTypingAhead() ? B_TRUNCATE_BEGINNING
			: IsFiltering() ? B_TRUNCATE_MIDDLE : B_TRUNCATE_END,
		textRect.Width());

	if (IsTypingAhead()) {
		// use a muted gray for the typeahead
		SetHighUIColor(B_DOCUMENT_TEXT_COLOR);
	} else
		SetHighUIColor(B_PANEL_TEXT_COLOR);

	MovePenTo(textRect.LeftBottom());
	DrawString(itemString.String());

	bounds.top++;

	rgb_color light = tint_color(ViewColor(), B_LIGHTEN_MAX_TINT);
	rgb_color shadow = tint_color(ViewColor(), B_DARKEN_2_TINT);

	BeginLineArray(fShowingBarberPole && !fStartSpinningAfter ? 9 : 5);

	if (!fShowingBarberPole || fStartSpinningAfter) {
		EndLineArray();
		return;
	}

	BRect barberPoleRect(BarberPoleOuterRect());

	AddLine(barberPoleRect.LeftTop(), barberPoleRect.RightTop(), shadow);
	AddLine(barberPoleRect.LeftTop(), barberPoleRect.LeftBottom(), shadow);
	AddLine(barberPoleRect.LeftBottom(), barberPoleRect.RightBottom(), light);
	AddLine(barberPoleRect.RightBottom(), barberPoleRect.RightTop(), light);
	EndLineArray();

	barberPoleRect.InsetBy(1, 1);

	BRect destRect(fBarberPoleMap
		? fBarberPoleMap->Bounds() : BRect(0, 0, 0, 0));
	destRect.OffsetTo(barberPoleRect.LeftTop()
		- BPoint(0, fLastBarberPoleOffset));
	fLastBarberPoleOffset -= 1;
	if (fLastBarberPoleOffset < 0)
		fLastBarberPoleOffset = 5;

	BRegion region;
	region.Set(BarberPoleInnerRect());
	ConstrainClippingRegion(&region);

	if (fBarberPoleMap)
		DrawBitmap(fBarberPoleMap, destRect);
}


/**
 * @brief Handle a mouse click by activating the window and popping up a directory menu.
 *
 * If the window's target model is a regular directory, a BDirMenu is presented so
 * the user can navigate up the hierarchy.
 */
void
BCountView::MouseDown(BPoint)
{
	BContainerWindow* window = dynamic_cast<BContainerWindow*>(Window());
	ThrowOnAssert(window != NULL);

	window->Activate();
	window->UpdateIfNeeded();

	if (fPoseView->IsFilePanel() || fPoseView->TargetModel() == NULL)
		return;

	if (window->TargetModel()->IsRoot())
		return;

	BDirMenu menu(NULL, be_app, B_REFS_RECEIVED);
	BEntry entry;
	if (entry.SetTo(window->TargetModel()->EntryRef()) == B_OK)
		menu.Populate(&entry, Window(), false, false, true, false, true);
	else
		menu.Populate(NULL, Window(), false, false, true, false, true);

	BPoint point = Bounds().LeftBottom();
	point.y += 3;
	ConvertToScreen(&point);
	BRect clickToOpenRect(Bounds());
	ConvertToScreen(&clickToOpenRect);
	menu.Go(point, true, true, clickToOpenRect);
}


/**
 * @brief Perform an initial count check when the view is attached to its window.
 */
void
BCountView::AttachedToWindow()
{
	CheckCount();
}


/**
 * @brief Set the type-ahead string displayed in place of the item count.
 *
 * @param string  The current type-ahead input string.
 */
void
BCountView::SetTypeAhead(const char* string)
{
	fTypeAheadString = string;
	Invalidate();
}


/**
 * @brief Return the current type-ahead string.
 *
 * @return Pointer to the null-terminated type-ahead string.
 */
const char*
BCountView::TypeAhead() const
{
	return fTypeAheadString.String();
}


/**
 * @brief Return whether a non-empty type-ahead string is currently set.
 *
 * @return true if a type-ahead string is active.
 */
bool
BCountView::IsTypingAhead() const
{
	return fTypeAheadString.Length() != 0;
}


/**
 * @brief Append one Unicode character to the filter string and redraw.
 *
 * @param character  UTF-8 encoded character to append.
 */
void
BCountView::AddFilterCharacter(const char* character)
{
	fFilterString.AppendChars(character, 1);
	Invalidate();
}


/**
 * @brief Remove the last Unicode character from the filter string and redraw.
 */
void
BCountView::RemoveFilterCharacter()
{
	fFilterString.TruncateChars(fFilterString.CountChars() - 1);
	Invalidate();
}


/**
 * @brief Clear the filter string and redraw the view.
 */
void
BCountView::CancelFilter()
{
	fFilterString.Truncate(0);
	Invalidate();
}


/**
 * @brief Return the current filter string.
 *
 * @return Pointer to the null-terminated filter string.
 */
const char*
BCountView::Filter() const
{
	return fFilterString.String();
}


/**
 * @brief Return whether a non-empty filter string is currently active.
 *
 * @return true if the filter string has one or more characters.
 */
bool
BCountView::IsFiltering() const
{
	return fFilterString.Length() > 0;
}
