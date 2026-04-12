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
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *   Distributed under the terms of the Be Sample Code License.
 */

/**
 * @file TextWidget.cpp
 * @brief BTextWidget provides inline text-editing of pose attribute columns.
 *
 * @see BPoseView, BColumn, BPose
 */


#include "TextWidget.h"

#include <string.h>
#include <stdlib.h>

#include <Alert.h>
#include <Catalog.h>
#include <Clipboard.h>
#include <Debug.h>
#include <Directory.h>
#include <MessageFilter.h>
#include <ScrollView.h>
#include <TextView.h>
#include <Volume.h>
#include <Window.h>

#include "Attributes.h"
#include "ContainerWindow.h"
#include "Commands.h"
#include "FSUtils.h"
#include "PoseView.h"
#include "Utilities.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "TextWidget"


const float kWidthMargin = 20;


//	#pragma mark - BTextWidget


/**
 * @brief Construct a BTextWidget for the attribute described by \a column on \a model.
 *
 * @param model   The filesystem model whose attribute is displayed.
 * @param column  The column describing the attribute and its formatting.
 * @param view    The BPoseView that owns this widget.
 */
BTextWidget::BTextWidget(Model* model, BColumn* column, BPoseView* view)
	:
	fText(WidgetAttributeText::NewWidgetText(model, column, view)),
	fAttrHash(column->AttrHash()),
	fAlignment(column->Alignment()),
	fEditable(column->Editable()),
	fVisible(true),
	fActive(false),
	fSymLink(model->IsSymLink()),
	fMaxWidth(0),
	fLastClickedTime(0)
{
}


/**
 * @brief Destructor; cancels any pending click-to-edit timer and deletes fText.
 */
BTextWidget::~BTextWidget()
{
	if (fLastClickedTime != 0)
		fParams.poseView->SetTextWidgetToCheck(NULL, this);

	delete fText;
}


/**
 * @brief Compare this widget's value against another for sorting.
 *
 * @param with   The widget to compare against.
 * @param view   The pose view used for locale-aware comparison.
 * @return Negative, zero, or positive as with standard comparison semantics.
 */
int
BTextWidget::Compare(const BTextWidget& with, BPoseView* view) const
{
	return fText->Compare(*with.fText, view);
}


/**
 * @brief Return the current attribute value as a C string.
 *
 * @param view  The pose view used for formatting context.
 * @return Formatted attribute text, or NULL if not a string attribute.
 */
const char*
BTextWidget::Text(const BPoseView* view) const
{
	StringAttributeText* textAttribute = dynamic_cast<StringAttributeText*>(fText);
	if (textAttribute == NULL)
		return NULL;

	return textAttribute->ValueAsText(view);
}


/**
 * @brief Return the rendered text width in the given pose view's font.
 *
 * @param pose  The pose view providing font metrics.
 * @return Width in pixels of the displayed text.
 */
float
BTextWidget::TextWidth(const BPoseView* pose) const
{
	return fText->Width(pose);
}


/**
 * @brief Return the preferred (unconstrained) text width for this widget.
 *
 * @param pose  The pose view providing font metrics.
 * @return Preferred width in pixels.
 */
float
BTextWidget::PreferredWidth(const BPoseView* pose) const
{
	return fText->PreferredWidth(pose);
}


/**
 * @brief Return the full column rectangle for this widget in list view.
 *
 * Falls back to CalcRect() when not in list mode.
 *
 * @param poseLoc  The on-screen top-left position of the owning pose.
 * @param column   The column descriptor.
 * @param view     The pose view.
 * @return The bounding rectangle of the column cell.
 */
BRect
BTextWidget::ColumnRect(BPoint poseLoc, const BColumn* column,
	const BPoseView* view)
{
	if (view->ViewMode() != kListMode) {
		// ColumnRect only makes sense in list view, return
		// CalcRect otherwise
		return CalcRect(poseLoc, column, view);
	}

	BRect rect;
	rect.left = column->Offset() + poseLoc.x;
	rect.right = rect.left + column->Width();
	rect.bottom = poseLoc.y + roundf((view->ListElemHeight() + ActualFontHeight(view)) / 2.f);
	rect.top = rect.bottom - floorf(ActualFontHeight(view));

	return rect;
}


/**
 * @brief Compute the text bounding rectangle given a pre-computed text width.
 *
 * @param poseLoc   The on-screen top-left position of the owning pose.
 * @param column    The column descriptor providing offset and width constraints.
 * @param view      The pose view providing icon size and font metrics.
 * @param textWidth The already-computed width of the rendered text.
 * @return The bounding rectangle for the text.
 */
BRect
BTextWidget::CalcRectCommon(BPoint poseLoc, const BColumn* column,
	const BPoseView* view, float textWidth)
{
	BRect rect;
	float viewWidth;

	if (view->ViewMode() == kListMode) {
		viewWidth = ceilf(std::min(column->Width(), textWidth));

		poseLoc.x += column->Offset();

		switch (fAlignment) {
			case B_ALIGN_LEFT:
				rect.left = poseLoc.x;
				rect.right = rect.left + viewWidth;
				break;

			case B_ALIGN_CENTER:
				rect.left = poseLoc.x + roundf((column->Width() - viewWidth) / 2.f);
				if (rect.left < 0)
					rect.left = 0;

				rect.right = rect.left + viewWidth;
				break;

			case B_ALIGN_RIGHT:
				rect.right = poseLoc.x + column->Width();
				rect.left = rect.right - viewWidth;
				if (rect.left < 0)
					rect.left = 0;
				break;

			default:
				TRESPASS();
				break;
		}

		rect.bottom = poseLoc.y + roundf((view->ListElemHeight() + ActualFontHeight(view)) / 2.f);
		rect.top = rect.bottom - floorf(ActualFontHeight(view));
	} else {
		float iconSize = (float)view->IconSizeInt();
		textWidth = floorf(textWidth);
			// prevent drawing artifacts from selection rect drawing an extra pixel

		if (view->ViewMode() == kIconMode) {
			// icon mode
			viewWidth = ceilf(std::min(view->StringWidth("M") * 30, textWidth));

			rect.left = poseLoc.x + roundf((iconSize - viewWidth) / 2.f);
			rect.bottom = poseLoc.y + ceilf(view->IconPoseHeight());
			rect.top = rect.bottom - floorf(ActualFontHeight(view));
		} else {
			// mini icon mode
			viewWidth = ceilf(textWidth);

			rect.left = poseLoc.x + iconSize + kMiniIconSeparator;
			rect.bottom = poseLoc.y + roundf((iconSize + ActualFontHeight(view)) / 2.f);
			rect.top = poseLoc.y;
		}

		rect.right = rect.left + viewWidth;
	}

	return rect;
}


/**
 * @brief Compute the text bounding rectangle using the current rendered text width.
 *
 * @param poseLoc  Pose origin.
 * @param column   Column descriptor.
 * @param view     Pose view.
 * @return Current text bounding rectangle.
 */
BRect
BTextWidget::CalcRect(BPoint poseLoc, const BColumn* column, const BPoseView* view)
{
	return CalcRectCommon(poseLoc, column, view, fText->Width(view));
}


/**
 * @brief Compute the text bounding rectangle using the previously cached text width.
 *
 * Used for erasing the old location before a redraw.
 *
 * @param poseLoc  Pose origin.
 * @param column   Column descriptor.
 * @param view     Pose view.
 * @return Old (cached) text bounding rectangle.
 */
BRect
BTextWidget::CalcOldRect(BPoint poseLoc, const BColumn* column, const BPoseView* view)
{
	return CalcRectCommon(poseLoc, column, view, fText->CurrentWidth());
}


/**
 * @brief Compute a minimum-width click rectangle for this widget.
 *
 * Ensures the click target is at least kWidthMargin pixels wide for easy
 * clicking even when the text is very short.
 *
 * @param poseLoc  Pose origin.
 * @param column   Column descriptor.
 * @param view     Pose view.
 * @return Padded bounding rectangle suitable for hit-testing.
 */
BRect
BTextWidget::CalcClickRect(BPoint poseLoc, const BColumn* column, const BPoseView* view)
{
	BRect rect = CalcRect(poseLoc, column, view);
	if (rect.Width() < kWidthMargin) {
		// if recting rect too narrow, make it a bit wider
		// for comfortable clicking
		if (column != NULL && column->Width() < kWidthMargin)
			rect.right = rect.left + column->Width();
		else
			rect.right = rect.left + kWidthMargin;
	}

	return rect;
}


/**
 * @brief Check whether the slow-click timer has expired and start editing if so.
 *
 * Called by the task loop: if the pose is still selected and the double-click
 * window has elapsed without another click, editing begins.
 */
void
BTextWidget::CheckExpiration()
{
	if (IsEditable() && fParams.pose->IsSelected() && fLastClickedTime) {
		bigtime_t doubleClickSpeed;
		get_click_speed(&doubleClickSpeed);

		bigtime_t delta = system_time() - fLastClickedTime;

		if (delta > doubleClickSpeed) {
			// at least 'doubleClickSpeed' microseconds ellapsed and no click
			// was registered since.
			fLastClickedTime = 0;
			StartEdit(fParams.bounds, fParams.poseView, fParams.pose);
		}
	} else {
		fLastClickedTime = 0;
		fParams.poseView->SetTextWidgetToCheck(NULL);
	}
}


/**
 * @brief Cancel the pending slow-click-to-edit timer.
 */
void
BTextWidget::CancelWait()
{
	fLastClickedTime = 0;
	fParams.poseView->SetTextWidgetToCheck(NULL);
}


/**
 * @brief Handle a mouse-up event to begin the slow-click-to-edit timer.
 *
 * Records the click time so that CheckExpiration() can start editing if no
 * further click arrives before the double-click interval elapses.
 *
 * @param bounds  Bounding rectangle of the pose.
 * @param view    The owning BPoseView.
 * @param pose    The pose that was clicked.
 */
void
BTextWidget::MouseUp(BRect bounds, BPoseView* view, BPose* pose, BPoint)
{
	// Register the time of that click.  The PoseView, through its Pulse()
	// will allow us to StartEdit() if no other click have been registered since
	// then.

	// TODO: re-enable modifiers, one should be enough
	view->SetTextWidgetToCheck(NULL);
	if (IsEditable() && pose->IsSelected()) {
		bigtime_t doubleClickSpeed;
		get_click_speed(&doubleClickSpeed);

		if (fLastClickedTime == 0) {
			fLastClickedTime = system_time();
			if (fLastClickedTime - doubleClickSpeed < pose->SelectionTime())
				fLastClickedTime = 0;
		} else
			fLastClickedTime = 0;

		if (fLastClickedTime == 0)
			return;

		view->SetTextWidgetToCheck(this);

		fParams.pose = pose;
		fParams.bounds = bounds;
		fParams.poseView = view;
	} else
		fLastClickedTime = 0;
}


static filter_result
TextViewKeyDownFilter(BMessage* message, BHandler**, BMessageFilter* filter)
{
	uchar key;
	if (message->FindInt8("byte", (int8*)&key) != B_OK)
		return B_DISPATCH_MESSAGE;

	ThrowOnAssert(filter != NULL);

	BContainerWindow* window = dynamic_cast<BContainerWindow*>(
		filter->Looper());
	ThrowOnAssert(window != NULL);

	BPoseView* view = window->PoseView();
	ThrowOnAssert(view != NULL);

	if (key == B_RETURN || key == B_ESCAPE) {
		view->CommitActivePose(key == B_RETURN);
		return B_SKIP_MESSAGE;
	}

	if (key == B_TAB) {
		if (view->ActivePose()) {
			if (message->FindInt32("modifiers") & B_SHIFT_KEY)
				view->ActivePose()->EditPreviousWidget(view);
			else
				view->ActivePose()->EditNextWidget(view);
		}

		return B_SKIP_MESSAGE;
	}

	// the BTextView doesn't respect window borders when resizing itself;
	// we try to work-around this "bug" here.

	// find the text editing view
	BView* scrollView = view->FindView("BorderView");
	if (scrollView != NULL) {
		BTextView* textView = dynamic_cast<BTextView*>(
			scrollView->FindView("WidgetTextView"));
		if (textView != NULL) {
			ASSERT(view->ActiveTextWidget() != NULL);
			float maxWidth = view->ActiveTextWidget()->MaxWidth();
			bool tooWide = textView->TextRect().Width() > maxWidth;
			textView->MakeResizable(!tooWide, tooWide ? NULL : scrollView);
		}
	}

	return B_DISPATCH_MESSAGE;
}


static filter_result
TextViewPasteFilter(BMessage* message, BHandler**, BMessageFilter* filter)
{
	ThrowOnAssert(filter != NULL);

	BContainerWindow* window = dynamic_cast<BContainerWindow*>(
		filter->Looper());
	ThrowOnAssert(window != NULL);

	BPoseView* view = window->PoseView();
	ThrowOnAssert(view != NULL);

	// the BTextView doesn't respect window borders when resizing itself;
	// we try to work-around this "bug" here.

	// find the text editing view
	BView* scrollView = view->FindView("BorderView");
	if (scrollView != NULL) {
		BTextView* textView = dynamic_cast<BTextView*>(
			scrollView->FindView("WidgetTextView"));
		if (textView != NULL) {
			float textWidth = textView->TextRect().Width();

			// subtract out selected text region width
			int32 start, finish;
			textView->GetSelection(&start, &finish);
			if (start != finish) {
				BRegion selectedRegion;
				textView->GetTextRegion(start, finish, &selectedRegion);
				textWidth -= selectedRegion.Frame().Width();
			}

			// add pasted text width
			if (be_clipboard->Lock()) {
				BMessage* clip = be_clipboard->Data();
				if (clip != NULL) {
					const char* text = NULL;
					ssize_t length = 0;

					if (clip->FindData("text/plain", B_MIME_TYPE,
							(const void**)&text, &length) == B_OK) {
						textWidth += textView->StringWidth(text);
					}
				}

				be_clipboard->Unlock();
			}

			// check if pasted text is too wide
			ASSERT(view->ActiveTextWidget() != NULL);
			float maxWidth = view->ActiveTextWidget()->MaxWidth();
			bool tooWide = textWidth > maxWidth;

			if (tooWide) {
				// resize text view to max width

				// move scroll view if not left aligned
				float oldWidth = textView->Bounds().Width();
				float newWidth = maxWidth;
				float right = oldWidth - newWidth;

				if (textView->Alignment() == B_ALIGN_CENTER)
					scrollView->MoveBy(roundf(right / 2), 0);
				else if (textView->Alignment() == B_ALIGN_RIGHT)
					scrollView->MoveBy(right, 0);

				// resize scroll view
				float grow = newWidth - oldWidth;
				scrollView->ResizeBy(grow, 0);
			}

			textView->MakeResizable(!tooWide, tooWide ? NULL : scrollView);
		}
	}

	return B_DISPATCH_MESSAGE;
}


/**
 * @brief Begin inline editing of this attribute widget.
 *
 * Creates a BScrollView/BTextView overlay, populates it with the current
 * attribute text, and installs it in the pose view for the user to edit.
 *
 * @param bounds  The visual bounds of the pose item.
 * @param view    The owning BPoseView.
 * @param pose    The pose being edited.
 */
void
BTextWidget::StartEdit(BRect bounds, BPoseView* view, BPose* pose)
{
	ASSERT(view != NULL);
	ASSERT(view->Window() != NULL);

	view->SetTextWidgetToCheck(NULL, this);
	if (!IsEditable() || IsActive())
		return;

	// do not start edit while dragging
	if (view->IsDragging())
		return;

	view->SetActiveTextWidget(this);

	// The initial text color has to be set differently on Desktop
	rgb_color initialTextColor;
	if (view->IsDesktopView())
		initialTextColor = InvertColor(view->HighColor());
	else
		initialTextColor = view->HighColor();

	BRect rect(bounds);
	rect.OffsetBy(view->ViewMode() == kListMode ? -2 : 0, -2);
	BTextView* textView = new BTextView(rect, "WidgetTextView", rect, be_plain_font,
		&initialTextColor, B_FOLLOW_ALL, B_WILL_DRAW);

	textView->SetWordWrap(false);
	textView->SetInsets(2, 2, 2, 2);
	DisallowMetaKeys(textView);
	fText->SetupEditing(textView);

	if (view->IsDesktopView()) {
		// force text view colors to be inverse of Desktop text color, white or black
		rgb_color backColor = view->HighColor();
		rgb_color textColor = InvertColor(backColor);
		backColor = tint_color(backColor,
			view->SelectedVolumeIsReadOnly() ? ReadOnlyTint(backColor) : B_NO_TINT);

		textView->SetViewColor(backColor);
		textView->SetLowColor(backColor);
		textView->SetHighColor(textColor);
	} else {
		// document colors or tooltip colors on Open with... window
		textView->SetViewUIColor(view->ViewUIColor());
		textView->SetLowUIColor(view->LowUIColor());
		textView->SetHighUIColor(view->HighUIColor());
	}

	if (view->SelectedVolumeIsReadOnly()) {
		textView->MakeEditable(false);
		textView->MakeSelectable(true);
	}

	textView->AddFilter(new BMessageFilter(B_KEY_DOWN, TextViewKeyDownFilter));
	if (!view->SelectedVolumeIsReadOnly())
		textView->AddFilter(new BMessageFilter(B_PASTE, TextViewPasteFilter));

	// get full text length
	rect.right = rect.left + textView->LineWidth();
	rect.bottom = rect.top + textView->LineHeight() - 1 + 4;

	if (view->ViewMode() == kListMode) {
		// limit max width to column width in list mode
		BColumn* column = view->ColumnFor(fAttrHash);
		ASSERT(column != NULL);
		fMaxWidth = column->Width();
	} else {
		// limit max width to 30em in icon and mini icon mode
		fMaxWidth = textView->StringWidth("M") * 30;

		if (textView->LineWidth() > fMaxWidth
			|| view->ViewMode() == kMiniIconMode) {
			// compensate for text going over right inset
			rect.OffsetBy(-2, 0);
		}
	}

	// resize textView
	textView->MoveTo(rect.LeftTop());
	textView->ResizeTo(std::min(fMaxWidth, rect.Width()), rect.Height());
	textView->SetTextRect(rect);

	// set alignment before adding textView so it doesn't redraw
	switch (view->ViewMode()) {
		case kIconMode:
			textView->SetAlignment(B_ALIGN_CENTER);
			break;

		case kMiniIconMode:
			textView->SetAlignment(B_ALIGN_LEFT);
			break;

		case kListMode:
			textView->SetAlignment(fAlignment);
			break;
	}

	BScrollView* scrollView
		= new BScrollView("BorderView", textView, 0, 0, false, false, B_PLAIN_BORDER);
	view->AddChild(scrollView);

	bool tooWide = textView->TextRect().Width() > fMaxWidth;
	textView->MakeResizable(!tooWide, tooWide ? NULL : scrollView);

	view->SetActivePose(pose);
		// tell view about pose
	SetActive(true);
		// for widget

	textView->SelectAll();
	textView->ScrollToSelection();
		// scroll to beginning so that text is visible
	textView->MakeFocus();

	// make this text widget invisible while we edit it
	SetVisible(false);

	// force immediate redraw so TextView appears instantly
	view->Window()->UpdateIfNeeded();
}


/**
 * @brief Finish inline editing and optionally write the new attribute value.
 *
 * Removes the text-editing overlay, and if \a saveChanges is true writes the
 * new value back to the filesystem node.
 *
 * @param saveChanges  If true, commit the user's edits; if false, discard them.
 * @param poseLoc      Position of the pose in the view.
 * @param view         The owning BPoseView.
 * @param pose         The pose being edited.
 * @param poseIndex    Index of the pose in the sorted list.
 */
void
BTextWidget::StopEdit(bool saveChanges, BPoint poseLoc, BPoseView* view,
	BPose* pose, int32 poseIndex)
{
	view->SetActiveTextWidget(NULL);

	// find the text editing view
	BView* scrollView = view->FindView("BorderView");
	ASSERT(scrollView != NULL);
	if (scrollView == NULL)
		return;

	BTextView* textView = dynamic_cast<BTextView*>(scrollView->FindView("WidgetTextView"));
	ASSERT(textView != NULL);
	if (textView == NULL)
		return;

	BColumn* column = view->ColumnFor(fAttrHash);
	ASSERT(column != NULL);
	if (column == NULL)
		return;

	if (saveChanges && fText->CommitEditedText(textView)) {
		// we have an actual change, re-sort
		view->CheckPoseSortOrder(pose, poseIndex);
	}

	// make text widget visible again
	SetVisible(true);
	view->Invalidate(ColumnRect(poseLoc, column, view));

	// force immediate redraw so TEView disappears
	scrollView->RemoveSelf();
	delete scrollView;

	ASSERT(view->Window() != NULL);
	view->Window()->UpdateIfNeeded();
	view->MakeFocus();

	SetActive(false);
}


/**
 * @brief Invalidate the display if the underlying attribute has changed.
 *
 * Reads the attribute, compares it to the cached value, and triggers a
 * redraw if the display needs updating.
 *
 * @param loc      Current pose location.
 * @param column   The column being checked.
 * @param view     The owning BPoseView.
 * @param visible  If false, suppress the invalidation.
 */
void
BTextWidget::CheckAndUpdate(BPoint loc, const BColumn* column, BPoseView* view, bool visible)
{
	BRect oldRect;
	if (view->ViewMode() != kListMode)
		oldRect = CalcOldRect(loc, column, view);

	if (fText->CheckAttributeChanged() && fText->CheckViewChanged(view) && visible) {
		BRect invalidRect(ColumnRect(loc, column, view));
		if (view->ViewMode() != kListMode)
			invalidRect = invalidRect | oldRect;

		view->Invalidate(invalidRect);
	}
}


/**
 * @brief Select all text in the inline editor if it is active.
 *
 * @param view  The owning BPoseView containing the editor.
 */
void
BTextWidget::SelectAll(BPoseView* view)
{
	BTextView* text = dynamic_cast<BTextView*>(view->FindView("WidgetTextView"));
	if (text != NULL)
		text->SelectAll();
}


/**
 * @brief Render the attribute text into the pose view.
 *
 * Draws the background erase rect and then the formatted attribute text with
 * appropriate truncation, colour, and clipboard decoration.
 *
 * @param eraseRect     Rectangle to erase before drawing.
 * @param textRect      Target rectangle for the text.
 * @param view          The BPoseView providing drawing context.
 * @param drawView      The BView to draw into (may differ from \a view).
 * @param selected      true if the pose is currently selected.
 * @param clipboardMode Clipboard status flags affecting decoration.
 * @param offset        Drawing offset applied to all coordinates.
 */
void
BTextWidget::Draw(BRect eraseRect, BRect textRect, BPoseView* view, BView* drawView,
	bool selected, uint32 clipboardMode, BPoint offset)
{
	ASSERT(view != NULL);
	ASSERT(view->Window() != NULL);
	ASSERT(drawView != NULL);

	textRect.OffsetBy(offset);

	BRegion textRegion(textRect);
	drawView->ConstrainClippingRegion(&textRegion);

	// We are only concerned with setting the correct text color.

	// For active views the selection is drawn as inverse text
	// (background color for the text, solid black for the background).
	// For inactive windows the text is drawn normally, then the
	// selection rect is alpha-blended on top. This all happens in
	// BPose::Draw before and after calling this function.

	bool direct = drawView == view;

	if (selected) {
		// erase selection rect background
		drawView->SetDrawingMode(B_OP_COPY);
		BRect invertRect(textRect);
		invertRect.left = ceilf(invertRect.left);
		invertRect.top = ceilf(invertRect.top);
		invertRect.right = floorf(invertRect.right);
		invertRect.bottom = floorf(invertRect.bottom);
		drawView->FillRect(invertRect, B_SOLID_LOW);

		// High color is set to inverted low, then the whole thing is
		// inverted again so that the background color "shines through".
		drawView->SetHighColor(InvertColorSmart(drawView->LowColor()));
	} else if (clipboardMode == kMoveSelectionTo) {
		drawView->SetDrawingMode(B_OP_ALPHA);
		drawView->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_COMPOSITE);
		uint8 alpha = 128; // set the level of opacity by value
		if (drawView->LowColor().IsLight())
			drawView->SetHighColor(0, 0, 0, alpha);
		else
			drawView->SetHighColor(255, 255, 255, alpha);
	} else {
		drawView->SetDrawingMode(B_OP_OVER);
		if (view->IsDesktopView())
			drawView->SetHighColor(view->HighColor());
		else
			drawView->SetHighUIColor(view->HighUIColor());
	}

	float decenderHeight = roundf(view->FontInfo().descent);
	BPoint location(textRect.left, textRect.bottom - decenderHeight);

	const char* fittingText = fText->FittingText(view);

	// Draw text outline unless selected or column resizing.
	// The direct parameter is false when dragging or column resizing.
	if (!selected && direct && view->WidgetTextOutline()) {
		// draw a halo around the text by using the "false bold"
		// feature for text rendering. Either black or white is used for
		// the glow (whatever acts as contrast) with a some alpha value,
		if (direct && clipboardMode != kMoveSelectionTo) {
			drawView->SetDrawingMode(B_OP_ALPHA);
			drawView->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
		}

		BFont font;
		drawView->GetFont(&font);

		rgb_color textColor = drawView->HighColor();
		if (textColor.IsDark()) {
			// dark text on light outline
			rgb_color glowColor = ui_color(B_SHINE_COLOR);

			font.SetFalseBoldWidth(2.0);
			drawView->SetFont(&font, B_FONT_FALSE_BOLD_WIDTH);
			glowColor.alpha = 30;
			drawView->SetHighColor(glowColor);

			drawView->DrawString(fittingText, location);

			font.SetFalseBoldWidth(1.0);
			drawView->SetFont(&font, B_FONT_FALSE_BOLD_WIDTH);
			glowColor.alpha = 65;
			drawView->SetHighColor(glowColor);

			drawView->DrawString(fittingText, location);

			font.SetFalseBoldWidth(0.0);
			drawView->SetFont(&font, B_FONT_FALSE_BOLD_WIDTH);
		} else {
			// light text on dark outline
			rgb_color outlineColor = kBlack;

			font.SetFalseBoldWidth(1.0);
			drawView->SetFont(&font, B_FONT_FALSE_BOLD_WIDTH);
			outlineColor.alpha = 30;
			drawView->SetHighColor(outlineColor);

			drawView->DrawString(fittingText, location);

			font.SetFalseBoldWidth(0.0);
			drawView->SetFont(&font, B_FONT_FALSE_BOLD_WIDTH);

			outlineColor.alpha = 200;
			drawView->SetHighColor(outlineColor);

			drawView->DrawString(fittingText, location + BPoint(1, 1));
		}

		if (direct && clipboardMode != kMoveSelectionTo)
			drawView->SetDrawingMode(B_OP_OVER);

		drawView->SetHighColor(textColor);
	}

	drawView->DrawString(fittingText, location);

	if (fSymLink && (fAttrHash == view->FirstColumn()->AttrHash())) {
		// TODO:
		// this should be exported to the WidgetAttribute class, probably
		// by having a per widget kind style
		if (direct && clipboardMode != kMoveSelectionTo) {
			rgb_color underlineColor = drawView->HighColor();
			underlineColor.alpha = 180;

			drawView->SetDrawingMode(B_OP_ALPHA);
			drawView->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
			drawView->SetHighColor(underlineColor);
		}

		BRect lineRect(textRect.OffsetByCopy(0, decenderHeight > 2 ? -(decenderHeight - 2) : 0));
			// move underline 2px under text
		lineRect.InsetBy(roundf(textRect.Width() - fText->Width(view)), 0);
			// only underline text part
		drawView->StrokeLine(lineRect.LeftBottom(), lineRect.RightBottom(), B_MIXED_COLORS);

		if (direct && clipboardMode != kMoveSelectionTo)
			drawView->SetDrawingMode(B_OP_OVER);
	}

	drawView->ConstrainClippingRegion(NULL);
}
