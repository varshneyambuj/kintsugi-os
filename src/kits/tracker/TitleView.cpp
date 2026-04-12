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
 * @file TitleView.cpp
 * @brief BTitleView draws and handles interaction with column title bars in list view.
 *
 * @see BPoseView, BColumn
 */


//	ListView title drawing and mouse manipulation classes


#include "TitleView.h"

#include <Alert.h>
#include <Application.h>
#include <ControlLook.h>
#include <Debug.h>
#include <PopUpMenu.h>
#include <Window.h>

#include <algorithm>

#include <stdio.h>
#include <string.h>

#include "Commands.h"
#include "ContainerWindow.h"
#include "PoseView.h"
#include "Utilities.h"


#define APP_SERVER_CLEARS_BACKGROUND 1


static const float kMinFontSize = 8.0f;
static const float kMinTitleHeight = 13.0f;
static const float kTitleSpacing = 1.4f;


static void
_DrawLine(BPoseView* view, BPoint from, BPoint to)
{
	float tint = B_NO_TINT;
	color_which highColor = view->HighUIColor(&tint);
	view->SetHighUIColor(view->LowUIColor(), B_DARKEN_1_TINT);
	view->StrokeLine(from, to);
	view->SetHighUIColor(highColor, tint);
}


static void
_UndrawLine(BPoseView* view, BPoint from, BPoint to)
{
	if (!view->TargetVolumeIsReadOnly())
		view->SetLowUIColor(view->LowUIColor());
	else
		view->SetLowUIColor(view->LowUIColor(), ReadOnlyTint(view->LowUIColor()));
	view->StrokeLine(from, to, B_SOLID_LOW);
}


static void
_DrawOutline(BView* view, BRect where)
{
	where.right++;
	where.bottom--;
	float tint = B_NO_TINT;
	color_which highColor = view->HighUIColor(&tint);
	view->SetHighUIColor(B_CONTROL_HIGHLIGHT_COLOR);
	view->StrokeRect(where);
	view->SetHighUIColor(highColor, tint);
}


//	#pragma mark - BTitleView


/**
 * @brief Construct a BTitleView that displays column headers for \a view.
 *
 * @param view  The BPoseView whose columns are described by this title bar.
 */
BTitleView::BTitleView(BPoseView* view)
	:
	BView("TitleView", B_WILL_DRAW),
	fPoseView(view),
	fTitleList(10),
	fHorizontalResizeCursor(B_CURSOR_ID_RESIZE_EAST_WEST),
	fPreviouslyClickedColumnTitle(0),
	fPreviousLeftClickTime(0),
	fTrackingState(NULL)
{
	SetHighUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
#if APP_SERVER_CLEARS_BACKGROUND
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
#else
	SetViewColor(B_TRANSPARENT_COLOR);
#endif

	float fontSize = std::max(kMinFontSize,
		floorf(be_plain_font->Size() * 0.75f));

	BFont font(be_plain_font);
	font.SetSize(fontSize);
	SetFont(&font);

	fPreferredHeight = std::max(kMinTitleHeight,
		ceilf(fontSize * kTitleSpacing));

	Reset();
}


/**
 * @brief Destructor; releases the current tracking state.
 */
BTitleView::~BTitleView()
{
	delete fTrackingState;
}


/**
 * @brief Rebuild the title list from the current pose-view column set.
 *
 * Clears and repopulates fTitleList to match fPoseView->ColumnAt(), then
 * invalidates the view.
 */
void
BTitleView::Reset()
{
	fTitleList.MakeEmpty();

	for (int32 index = 0; ; index++) {
		BColumn* column = fPoseView->ColumnAt(index);
		if (!column)
			break;
		fTitleList.AddItem(new BColumnTitle(this, column));
	}
	Invalidate();
}


/**
 * @brief Insert a new column title, optionally after an existing one.
 *
 * @param column  The new column to add.
 * @param after   If non-NULL, the new title is placed immediately after this column.
 */
void
BTitleView::AddTitle(BColumn* column, const BColumn* after)
{
	int32 count = fTitleList.CountItems();
	int32 index;
	if (after) {
		for (index = 0; index < count; index++) {
			BColumn* titleColumn = fTitleList.ItemAt(index)->Column();

			if (after == titleColumn) {
				index++;
				break;
			}
		}
	} else
		index = count;

	fTitleList.AddItem(new BColumnTitle(this, column), index);
	Invalidate();
}


/**
 * @brief Remove the title entry associated with \a column.
 *
 * @param column  The column whose title entry should be removed.
 */
void
BTitleView::RemoveTitle(BColumn* column)
{
	int32 count = fTitleList.CountItems();
	for (int32 index = 0; index < count; index++) {
		BColumnTitle* title = fTitleList.ItemAt(index);
		if (title->Column() == column) {
			fTitleList.RemoveItem(title);
			break;
		}
	}

	Invalidate();
}


/**
 * @brief Return the minimum size: 16 pixels wide by the preferred header height.
 *
 * @return Minimum BSize.
 */
BSize
BTitleView::MinSize()
{
	return BSize(16, fPreferredHeight);
}


/**
 * @brief Return the maximum size: unlimited width by the preferred header height.
 *
 * @return Maximum BSize.
 */
BSize
BTitleView::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, fPreferredHeight);
}


/**
 * @brief Convenience Draw() override; delegates to the full form with useOffscreen=false.
 *
 * @param rect  The dirty rectangle.
 */
void
BTitleView::Draw(BRect rect)
{
	Draw(rect, false);
}


/**
 * @brief Draw all column titles, optionally using an offscreen bitmap.
 *
 * @param updateRect       The dirty rectangle (ignored; all titles are redrawn).
 * @param useOffscreen     If true, render to the shared offscreen bitmap first.
 * @param updateOnly       If true, skip the double-buffering blit at the end.
 * @param pressedColumn    If non-NULL, this column is drawn in the pressed state.
 * @param trackRectBlitter Optional callback for drawing a tracking overlay rect.
 * @param passThru         Rectangle passed to \a trackRectBlitter.
 */
void
BTitleView::Draw(BRect /*updateRect*/, bool useOffscreen, bool updateOnly,
	const BColumnTitle* pressedColumn,
	void (*trackRectBlitter)(BView*, BRect), BRect passThru)
{
	BRect bounds(Bounds());
	BView* view;

	if (useOffscreen) {
		ASSERT(sOffscreen);
		BRect frame(bounds);
		frame.right += frame.left;
			// ToDo: this is kind of messy way of avoiding being clipped
			// by the amount the title is scrolled to the left
		view = sOffscreen->BeginUsing(frame);
		view->SetOrigin(-bounds.left, 0);
		view->SetLowColor(LowColor());
		view->SetHighColor(HighColor());
		BFont font;
		GetFont(&font);
		view->SetFont(&font);
	} else
		view = this;

	view->SetHighUIColor(B_PANEL_BACKGROUND_COLOR, B_DARKEN_2_TINT);
	view->StrokeLine(bounds.LeftBottom(), bounds.RightBottom());
	bounds.bottom--;

	rgb_color baseColor = ui_color(B_CONTROL_BACKGROUND_COLOR);
	be_control_look->DrawButtonBackground(view, bounds, bounds, baseColor, 0,
		BControlLook::B_TOP_BORDER | BControlLook::B_BOTTOM_BORDER);

	int32 count = fTitleList.CountItems();
	float minx = bounds.right;
	float maxx = bounds.left;
	for (int32 index = 0; index < count; index++) {
		BColumnTitle* title = fTitleList.ItemAt(index);
		title->Draw(view, title == pressedColumn);
		BRect titleBounds(title->Bounds());
		if (titleBounds.left < minx)
			minx = titleBounds.left;
		if (titleBounds.right > maxx)
			maxx = titleBounds.right;
	}

	bounds = Bounds();
	minx--;
	view->SetHighUIColor(B_PANEL_BACKGROUND_COLOR, B_DARKEN_1_TINT);
	view->StrokeLine(BPoint(minx, bounds.top),
		BPoint(minx, bounds.bottom - 1));

#if !(APP_SERVER_CLEARS_BACKGROUND)
	FillRect(BRect(bounds.left, bounds.top + 1, minx - 1, bounds.bottom - 1),
		B_SOLID_LOW);
	FillRect(BRect(maxx + 1, bounds.top + 1, bounds.right, bounds.bottom - 1),
		B_SOLID_LOW);
#endif

	if (useOffscreen) {
		if (trackRectBlitter)
			(trackRectBlitter)(view, passThru);

		view->Sync();
		DrawBitmap(sOffscreen->Bitmap());
		sOffscreen->DoneUsing();
	} else if (trackRectBlitter)
		(trackRectBlitter)(view, passThru);
}


/**
 * @brief Handle a mouse-down event to initiate column dragging or resizing.
 *
 * @param where  The mouse position in view coordinates.
 */
void
BTitleView::MouseDown(BPoint where)
{
	BContainerWindow* window = dynamic_cast<BContainerWindow*>(Window());
	if (window == NULL)
		return;

	if (!window->IsActive()) {
		// window wasn't active, activate it and bail
		window->Activate();
		return;
	}

	// finish any pending edits
	fPoseView->CommitActivePose();

	BColumnTitle* title = FindColumnTitle(where);
	BColumnTitle* resizedTitle = InColumnResizeArea(where);

	uint32 buttons;
	GetMouse(&where, &buttons);

	// Check if the user clicked the secondary mouse button.
	// if so, display the attribute menu:

	if (SecondaryMouseButtonDown(modifiers(), buttons)) {
		BPopUpMenu* menu = new BPopUpMenu("Attributes", false, false);
		window->NewAttributesMenu(menu);
		window->AddMimeTypesToMenu(menu);
		window->MarkAttributesMenu(menu);
		menu->SetTargetForItems(window->PoseView());
		menu->Go(ConvertToScreen(where), true, false);
		return;
	}

	bigtime_t doubleClickSpeed;
	get_click_speed(&doubleClickSpeed);

	if (resizedTitle) {
		bool force = static_cast<bool>(buttons & B_TERTIARY_MOUSE_BUTTON);
		if (force || (buttons & B_PRIMARY_MOUSE_BUTTON) != 0) {
			if (force || fPreviouslyClickedColumnTitle != 0) {
				if (force || system_time() - fPreviousLeftClickTime < doubleClickSpeed) {
					if (fPoseView->ResizeColumnToWidest(resizedTitle->Column())) {
						Invalidate();
						return;
					}
				}
			}
			fPreviousLeftClickTime = system_time();
			fPreviouslyClickedColumnTitle = resizedTitle;
		}
	} else if (!title)
		return;

	SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY | B_LOCK_WINDOW_FOCUS);

	// track the mouse
	if (resizedTitle) {
		fTrackingState = new ColumnResizeState(this, resizedTitle, where,
			system_time() + doubleClickSpeed);
	} else {
		fTrackingState = new ColumnDragState(this, title, where,
			system_time() + doubleClickSpeed);
	}
}


/**
 * @brief Finish a column drag or resize tracking operation.
 *
 * @param where  The mouse release position in view coordinates.
 */
void
BTitleView::MouseUp(BPoint where)
{
	if (fTrackingState == NULL)
		return;

	fTrackingState->MouseUp(where);

	delete fTrackingState;
	fTrackingState = NULL;
}


/**
 * @brief Update the cursor and delegate mouse movement to the current tracking state.
 *
 * @param where        Current mouse position.
 * @param code         B_ENTERED_VIEW, B_INSIDE_VIEW, or B_EXITED_VIEW.
 * @param dragMessage  Non-NULL if a drag is in progress.
 */
void
BTitleView::MouseMoved(BPoint where, uint32 code, const BMessage* dragMessage)
{
	BContainerWindow* window = dynamic_cast<BContainerWindow*>(Window());
	if (window == NULL)
		return;

	if (fTrackingState != NULL) {
		int32 buttons = 0;
		if (Looper() != NULL && Looper()->CurrentMessage() != NULL)
			Looper()->CurrentMessage()->FindInt32("buttons", &buttons);
		fTrackingState->MouseMoved(where, buttons);
		return;
	}

	switch (code) {
		default:
			if (InColumnResizeArea(where) && window->IsActive())
				SetViewCursor(&fHorizontalResizeCursor);
			else
				SetViewCursor(B_CURSOR_SYSTEM_DEFAULT);
			break;

		case B_EXITED_VIEW:
			SetViewCursor(B_CURSOR_SYSTEM_DEFAULT);
			break;
	}

	_inherited::MouseMoved(where, code, dragMessage);
}


/**
 * @brief Return the column title whose resize handle contains \a where.
 *
 * @param where  Point in view coordinates to test.
 * @return The matching BColumnTitle, or NULL if no resize area was hit.
 */
BColumnTitle*
BTitleView::InColumnResizeArea(BPoint where) const
{
	int32 count = fTitleList.CountItems();
	for (int32 index = 0; index < count; index++) {
		BColumnTitle* title = fTitleList.ItemAt(index);
		if (title->InColumnResizeArea(where))
			return title;
	}

	return NULL;
}


/**
 * @brief Return the column title hit-tested at \a where.
 *
 * @param where  Point in view coordinates.
 * @return The BColumnTitle whose bounds contain \a where, or NULL.
 */
BColumnTitle*
BTitleView::FindColumnTitle(BPoint where) const
{
	int32 count = fTitleList.CountItems();
	for (int32 index = 0; index < count; index++) {
		BColumnTitle* title = fTitleList.ItemAt(index);
		if (title->Bounds().Contains(where))
			return title;
	}

	return NULL;
}


/**
 * @brief Return the title entry for a given BColumn pointer.
 *
 * @param column  The column to look up.
 * @return The corresponding BColumnTitle, or NULL if not found.
 */
BColumnTitle*
BTitleView::FindColumnTitle(const BColumn* column) const
{
	int32 count = fTitleList.CountItems();
	for (int32 index = 0; index < count; index++) {
		BColumnTitle* title = fTitleList.ItemAt(index);
		if (title->Column() == column)
			return title;
	}

	return NULL;
}


//	#pragma mark - BColumnTitle


/**
 * @brief Construct a BColumnTitle pairing a column with its title view.
 *
 * @param view    The parent BTitleView.
 * @param column  The BColumn this title represents.
 */
BColumnTitle::BColumnTitle(BTitleView* view, BColumn* column)
	:
	fColumn(column),
	fParent(view)
{
}


/**
 * @brief Return true if \a where falls within the column's resize handle.
 *
 * @param where  Point in title-view coordinates.
 * @return true if \a where is in the kEdgeSize pixel resize zone.
 */
bool
BColumnTitle::InColumnResizeArea(BPoint where) const
{
	BRect edge(Bounds());
	edge.left = edge.right - kEdgeSize;
	edge.right += kEdgeSize;

	return edge.Contains(where);
}


/**
 * @brief Return the bounding rectangle of this column title.
 *
 * @return The rectangle spanning from the column's offset to the right edge
 *         plus margin, full height of the title bar.
 */
BRect
BColumnTitle::Bounds() const
{
	BRect bounds(fColumn->Offset() - kTitleColumnLeftExtraMargin, 0, 0,
		fParent->Bounds().Height());
	bounds.right = bounds.left + fColumn->Width() + kTitleColumnExtraMargin;

	return bounds;
}


/**
 * @brief Render this column title into \a view.
 *
 * Draws the background, truncated label, separator line, and sort indicator.
 *
 * @param view     The BView to draw into.
 * @param pressed  If true, draw in the depressed / highlighted state.
 */
void
BColumnTitle::Draw(BView* view, bool pressed)
{
	BRect bounds(Bounds());

	font_height fontHeight;
	view->GetFontHeight(&fontHeight);

	float baseline = floor(bounds.top + fontHeight.ascent
		+ (bounds.Height() + 1 - (fontHeight.ascent + fontHeight.descent)) / 2);
	BPoint titleLocation(0, baseline);

	rgb_color baseColor = ui_color(B_PANEL_BACKGROUND_COLOR);

	if (pressed) {
		bounds.bottom--;
		BRect rect(bounds);
		rect.right--;
		baseColor = tint_color(baseColor, B_DARKEN_1_TINT);

		be_control_look->DrawButtonBackground(view, rect, rect, baseColor, 0,
			BControlLook::B_TOP_BORDER | BControlLook::B_BOTTOM_BORDER);
	}

	BString titleString(fColumn->Title());
	view->TruncateString(&titleString, B_TRUNCATE_END,
		bounds.Width() - kTitleColumnExtraMargin);
	float resultingWidth = view->StringWidth(titleString.String());

	switch (fColumn->Alignment()) {
		case B_ALIGN_LEFT:
		default:
			titleLocation.x = bounds.left + 1 + kTitleColumnLeftExtraMargin;
			break;

		case B_ALIGN_CENTER:
			titleLocation.x = bounds.left + (bounds.Width() / 2)
				- (resultingWidth / 2);
			break;

		case B_ALIGN_RIGHT:
			titleLocation.x = bounds.right - resultingWidth
				- kTitleColumnRightExtraMargin;
			break;
	}

	view->SetHighUIColor(B_PANEL_TEXT_COLOR, pressed ? B_DARKEN_1_TINT : 1.0f);
	view->SetLowColor(baseColor);
	view->DrawString(titleString.String(), titleLocation);

	// show sort columns
	bool secondary
		= (fColumn->AttrHash() == fParent->PoseView()->SecondarySort());
	if (secondary
		|| (fColumn->AttrHash() == fParent->PoseView()->PrimarySort())) {

		BPoint center(titleLocation.x - 6,
			roundf((bounds.top + bounds.bottom) / 2.0));
		BPoint triangle[3];
		if (fParent->PoseView()->ReverseSort()) {
			triangle[0] = center + BPoint(-3.5, 1.5);
			triangle[1] = center + BPoint(3.5, 1.5);
			triangle[2] = center + BPoint(0.0, -2.0);
		} else {
			triangle[0] = center + BPoint(-3.5, -1.5);
			triangle[1] = center + BPoint(3.5, -1.5);
			triangle[2] = center + BPoint(0.0, 2.0);
		}

		uint32 flags = view->Flags();
		view->SetFlags(flags | B_SUBPIXEL_PRECISE);

		if (secondary) {
			view->SetHighUIColor(B_PANEL_BACKGROUND_COLOR, 1.3);
			view->FillTriangle(triangle[0], triangle[1], triangle[2]);
		} else {
			view->SetHighUIColor(B_PANEL_BACKGROUND_COLOR, 1.6);
			view->FillTriangle(triangle[0], triangle[1], triangle[2]);
		}

		view->SetFlags(flags);
	}

	view->SetHighUIColor(B_PANEL_BACKGROUND_COLOR, B_DARKEN_1_TINT);
	view->StrokeLine(bounds.RightTop(), bounds.RightBottom());
}


//	#pragma mark - ColumnTrackState


ColumnTrackState::ColumnTrackState(BTitleView* view, BColumnTitle* title,
	BPoint where, bigtime_t pastClickTime)
	:
	fTitleView(view),
	fTitle(title),
	fFirstClickPoint(where),
	fPastClickTime(pastClickTime),
	fHasMoved(false)
{
}


void
ColumnTrackState::MouseUp(BPoint where)
{
	// if it is pressed shortly and not moved, it is a click
	// else it is a track
	if (system_time() <= fPastClickTime && !fHasMoved)
		Clicked(where);
	else
		Done(where);
}


void
ColumnTrackState::MouseMoved(BPoint where, uint32 buttons)
{
	if (!fHasMoved && system_time() < fPastClickTime) {
		BRect moveMargin(fFirstClickPoint, fFirstClickPoint);
		moveMargin.InsetBy(-1, -1);

		if (moveMargin.Contains(where))
			return;
	}

	Moved(where, buttons);
	fHasMoved = true;
}


//	#pragma mark - ColumnResizeState


ColumnResizeState::ColumnResizeState(BTitleView* view, BColumnTitle* title,
		BPoint where, bigtime_t pastClickTime)
	:
	ColumnTrackState(view, title, where, pastClickTime),
	fLastLineDrawPos(-1),
	fInitialTrackOffset((title->fColumn->Offset() + title->fColumn->Width())
		- where.x)
{
	DrawLine();
}


bool
ColumnResizeState::ValueChanged(BPoint where)
{
	float newWidth = where.x + fInitialTrackOffset
		- fTitle->fColumn->Offset();
	if (newWidth < kMinColumnWidth)
		newWidth = kMinColumnWidth;

	return newWidth != fTitle->fColumn->Width();
}


void
ColumnResizeState::Moved(BPoint where, uint32)
{
	float newWidth = where.x + fInitialTrackOffset
		- fTitle->fColumn->Offset();
	if (newWidth < kMinColumnWidth)
		newWidth = kMinColumnWidth;

	BPoseView* poseView = fTitleView->PoseView();

	//bool shrink = (newWidth < fTitle->fColumn->Width());

	// resize the column
	poseView->ResizeColumn(fTitle->fColumn, newWidth, &fLastLineDrawPos, _DrawLine, _UndrawLine);

	BRect bounds(fTitleView->Bounds());
	bounds.left = fTitle->fColumn->Offset();

	// force title redraw
	fTitleView->Draw(bounds, true, false);
}


void
ColumnResizeState::Done(BPoint /*where*/)
{
	UndrawLine();
}


void
ColumnResizeState::Clicked(BPoint /*where*/)
{
	UndrawLine();
}


void
ColumnResizeState::DrawLine()
{
	BPoseView* poseView = fTitleView->PoseView();
	ASSERT(!poseView->IsDesktopView());

	BRect poseViewBounds(poseView->Bounds());
	// remember the line location
	poseViewBounds.left = fTitle->Bounds().right;
	fLastLineDrawPos = poseViewBounds.left;

	// draw the line in the new location
	_DrawLine(poseView, poseViewBounds.LeftTop(), poseViewBounds.LeftBottom());
}


void
ColumnResizeState::UndrawLine()
{
	if (fLastLineDrawPos < 0)
		return;

	BRect poseViewBounds(fTitleView->PoseView()->Bounds());
	poseViewBounds.left = fLastLineDrawPos;

	_UndrawLine(fTitleView->PoseView(), poseViewBounds.LeftTop(), poseViewBounds.LeftBottom());
}


//	#pragma mark - ColumnDragState


ColumnDragState::ColumnDragState(BTitleView* view, BColumnTitle* columnTitle,
	BPoint where, bigtime_t pastClickTime)
	:
	ColumnTrackState(view, columnTitle, where, pastClickTime),
	fInitialMouseTrackOffset(where.x),
	fTrackingRemovedColumn(false)
{
	ASSERT(columnTitle);
	ASSERT(fTitle);
	ASSERT(fTitle->Column());
	DrawPressNoOutline();
}


// ToDo:
// Autoscroll when dragging column left/right
// fix dragging back a column before the first column (now adds as last)
// make column swaps/adds not invalidate/redraw columns to the left
void
ColumnDragState::Moved(BPoint where, uint32)
{
	// figure out where we are with the mouse
	BRect titleBounds(fTitleView->Bounds());
	bool overTitleView = titleBounds.Contains(where);
	BColumnTitle* overTitle = overTitleView ? fTitleView->FindColumnTitle(where) : 0;
	BRect titleBoundsWithMargin(titleBounds);
	titleBoundsWithMargin.InsetBy(0, -kRemoveTitleMargin);
	bool inMarginRect = overTitleView || titleBoundsWithMargin.Contains(where);

	bool drawOutline = false;
	bool undrawOutline = false;

	if (fTrackingRemovedColumn) {
		if (overTitleView) {
			// tracked back with a removed title into the title bar, add it
			// back
			fTitleView->EndRectTracking();
			fColumnArchive.Seek(0, SEEK_SET);
			BColumn* column = BColumn::InstantiateFromStream(&fColumnArchive);
			ASSERT(column);
			const BColumn* after = NULL;
			if (overTitle)
				after = overTitle->Column();

			fTitleView->PoseView()->AddColumn(column, after);
			fTrackingRemovedColumn = false;
			fTitle = fTitleView->FindColumnTitle(column);
			fInitialMouseTrackOffset += fTitle->Bounds().left;
			drawOutline = true;
		}
	} else {
		if (!inMarginRect) {
			// dragged a title out of the hysteresis margin around the
			// title bar - remove it and start dragging it as a dotted outline

			BRect rect(fTitle->Bounds());
			rect.OffsetBy(where.x - fInitialMouseTrackOffset, where.y - 5);
			fColumnArchive.Seek(0, SEEK_SET);
			fTitle->Column()->ArchiveToStream(&fColumnArchive);
			fInitialMouseTrackOffset -= fTitle->Bounds().left;
			if (fTitleView->PoseView()->RemoveColumn(fTitle->Column(), false)) {
				fTitle = 0;
				fTitleView->BeginRectTracking(rect);
				fTrackingRemovedColumn = true;
				undrawOutline = true;
			}
		} else if (overTitle && overTitle != fTitle
				// over a different column
			&& (overTitle->Bounds().left >= fTitle->Bounds().right
					// over the one to the right
				|| where.x < overTitle->Bounds().left + fTitle->Bounds().Width())) {
					// over the one to the left, far enough to not snap
					// right back
			BColumn* column = fTitle->Column();
			fInitialMouseTrackOffset -= fTitle->Bounds().left;
			// swap the columns
			fTitleView->PoseView()->MoveColumnTo(column, overTitle->Column());
			// re-grab the title object looking it up by the column
			fTitle = fTitleView->FindColumnTitle(column);
			// recalc initialMouseTrackOffset
			fInitialMouseTrackOffset += fTitle->Bounds().left;
			drawOutline = true;
		} else
			drawOutline = true;
	}

	if (drawOutline)
		DrawOutline(where.x - fInitialMouseTrackOffset);
	else if (undrawOutline)
		UndrawOutline();
}


void
ColumnDragState::Done(BPoint /*where*/)
{
	if (fTrackingRemovedColumn)
		fTitleView->EndRectTracking();
	UndrawOutline();
}


void
ColumnDragState::Clicked(BPoint /*where*/)
{
	BPoseView* poseView = fTitleView->PoseView();
	uint32 hash = fTitle->Column()->AttrHash();
	uint32 primarySort = poseView->PrimarySort();
	uint32 secondarySort = poseView->SecondarySort();
	bool shift = (modifiers() & B_SHIFT_KEY) != 0;

	// For now:
	// if we hit the primary sort field again
	// then if shift key was down, switch primary and secondary
	if (hash == primarySort) {
		if (shift && secondarySort) {
			poseView->SetPrimarySort(secondarySort);
			poseView->SetSecondarySort(primarySort);
		} else
			poseView->SetReverseSort(!poseView->ReverseSort());
	} else if (shift) {
		// hit secondary sort column with shift key, disable
		if (hash == secondarySort)
			poseView->SetSecondarySort(0);
		else
			poseView->SetSecondarySort(hash);
	} else {
		poseView->SetPrimarySort(hash);
		poseView->SetReverseSort(false);
	}

	if (poseView->PrimarySort() == poseView->SecondarySort())
		poseView->SetSecondarySort(0);

	UndrawOutline();

	poseView->SortPoses();
	poseView->Invalidate();
}


bool
ColumnDragState::ValueChanged(BPoint)
{
	return true;
}


void
ColumnDragState::DrawPressNoOutline()
{
	fTitleView->Draw(fTitleView->Bounds(), true, false, fTitle);
}


void
ColumnDragState::DrawOutline(float pos)
{
	BRect outline(fTitle->Bounds());
	outline.OffsetBy(pos, 0);
	fTitleView->Draw(fTitleView->Bounds(), true, false, fTitle, _DrawOutline,
		outline);
}


void
ColumnDragState::UndrawOutline()
{
	fTitleView->Draw(fTitleView->Bounds(), true, false);
}


OffscreenBitmap* BTitleView::sOffscreen = new OffscreenBitmap;
