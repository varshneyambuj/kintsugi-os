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
 * @file StatusWindow.cpp
 * @brief BStatusWindow displays progress information for Tracker file operations.
 *
 * @see BWindow, BStatusView, TCustomButton
 */

/*!	A subclass of BWindow that is used to display the status of the Tracker
	operations (copying, deleting, etc.).
*/


#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Debug.h>
#include <DurationFormat.h>
#include <Locale.h>
#include <MessageFilter.h>
#include <StringView.h>
#include <String.h>
#include <TimeFormat.h>

#include <string.h>

#include "AutoLock.h"
#include "Bitmaps.h"
#include "Commands.h"
#include "StatusWindow.h"
#include "StringForSize.h"
#include "DeskWindow.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "StatusWindow"


const float kDefaultStatusViewHeight = 50;
const bigtime_t kMaxUpdateInterval = 100000LL;
const bigtime_t kSpeedReferenceInterval = 2000000LL;
const bigtime_t kShowSpeedInterval = 8000000LL;
const bigtime_t kShowEstimatedFinishInterval = 4000000LL;
const BRect kStatusRect(200, 200, 550, 200);

static bigtime_t sLastEstimatedFinishSpeedToggleTime = -1;
static bool sShowSpeed = true;
static const time_t kSecondsPerDay = 24 * 60 * 60;


class TCustomButton : public BButton {
public:
								TCustomButton(BRect frame, uint32 command);
	virtual	void				Draw(BRect updateRect);
private:
			typedef BButton _inherited;
};


class BStatusMouseFilter : public BMessageFilter {
public:
								BStatusMouseFilter();
	virtual	filter_result		Filter(BMessage* message, BHandler** target);
};


namespace BPrivate {
BStatusWindow* gStatusWindow = NULL;
}


//	#pragma mark - BStatusMouseFilter


/**
 * @brief Construct a filter that intercepts B_MOUSE_DOWN messages.
 */
BStatusMouseFilter::BStatusMouseFilter()
	:
	BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE, B_MOUSE_DOWN)
{
}


/**
 * @brief Redirect mouse-down events aimed at a BStatusBar to its parent view.
 *
 * @param message  The incoming mouse-down message.
 * @param target   In/out: the handler to receive the message.
 * @return B_DISPATCH_MESSAGE always.
 */
filter_result
BStatusMouseFilter::Filter(BMessage* message, BHandler** target)
{
	// If the target is the status bar, make sure the message goes to the
	// parent view instead.
	if ((*target)->Name() != NULL
		&& strcmp((*target)->Name(), "StatusBar") == 0) {
		BView* view = dynamic_cast<BView*>(*target);
		if (view != NULL)
			view = view->Parent();

		if (view != NULL)
			*target = view;
	}

	return B_DISPATCH_MESSAGE;
}


//	#pragma mark - TCustomButton


/**
 * @brief Construct a TCustomButton with the given frame and command constant.
 *
 * @param frame  The button's bounding rectangle.
 * @param what   Command constant (kStopButton or kPauseButton) that determines
 *               the icon drawn inside the button.
 */
TCustomButton::TCustomButton(BRect frame, uint32 what)
	:
	BButton(frame, "", "", new BMessage(what), B_FOLLOW_LEFT | B_FOLLOW_TOP,
		B_WILL_DRAW)
{
}


/**
 * @brief Draw the stop (filled square) or pause (two vertical bars) icon.
 *
 * @param updateRect  The dirty region passed by the rendering framework.
 */
void
TCustomButton::Draw(BRect updateRect)
{
	_inherited::Draw(updateRect);

	if (Message()->what == kStopButton) {
		updateRect = Bounds();
		updateRect.InsetBy(9, 8);
		SetHighColor(0, 0, 0);
		if (Value() == B_CONTROL_ON)
			updateRect.OffsetBy(1, 1);
		FillRect(updateRect);
	} else {
		updateRect = Bounds();
		updateRect.InsetBy(9, 7);
		BRect rect(updateRect);
		rect.right -= 3;

		updateRect.left += 3;
		updateRect.OffsetBy(1, 0);
		SetHighColor(0, 0, 0);
		if (Value() == B_CONTROL_ON) {
			updateRect.OffsetBy(1, 1);
			rect.OffsetBy(1, 1);
		}
		FillRect(updateRect);
		FillRect(rect);
	}
}


// #pragma mark - StatusBackgroundView


class StatusBackgroundView : public BView {
public:
	StatusBackgroundView(BRect frame)
		:
		BView(frame, "BackView", B_FOLLOW_ALL, B_WILL_DRAW | B_PULSE_NEEDED)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	}

	virtual void Pulse()
	{
		bigtime_t now = system_time();
		if (sShowSpeed
			&& sLastEstimatedFinishSpeedToggleTime + kShowSpeedInterval
				<= now) {
			sShowSpeed = false;
			sLastEstimatedFinishSpeedToggleTime = now;
		} else if (!sShowSpeed
			&& sLastEstimatedFinishSpeedToggleTime
				+ kShowEstimatedFinishInterval <= now) {
			sShowSpeed = true;
			sLastEstimatedFinishSpeedToggleTime = now;
		}
	}
};


//	#pragma mark - BStatusWindow


/**
 * @brief Construct the shared Tracker status window, initially hidden.
 *
 * Installs the mouse-down filter, adds the background view, and shows the
 * window off-screen so it can be made visible on demand.
 */
BStatusWindow::BStatusWindow()
	:
	BWindow(kStatusRect, B_TRANSLATE("Tracker status"),	B_TITLED_WINDOW,
		B_NOT_CLOSABLE | B_NOT_RESIZABLE | B_NOT_ZOOMABLE, B_ALL_WORKSPACES),
	fRetainDesktopFocus(false)
{
	SetSizeLimits(0, 100000, 0, 100000);
	fMouseDownFilter = new BStatusMouseFilter();
	AddCommonFilter(fMouseDownFilter);

	BView* view = new StatusBackgroundView(Bounds());
	AddChild(view);

	SetPulseRate(1000000);

	Hide();
	Show();
}


/**
 * @brief Destructor.
 */
BStatusWindow::~BStatusWindow()
{
}


/**
 * @brief Create and display a new status view for the given operation thread.
 *
 * @param thread  The thread ID of the file-operation thread to track.
 * @param type    The type of operation (copy, move, delete, etc.).
 */
void
BStatusWindow::CreateStatusItem(thread_id thread, StatusWindowState type)
{
	AutoLock<BWindow> lock(this);

	BRect rect(Bounds());
	if (BStatusView* lastView = fViewList.LastItem())
		rect.top = lastView->Frame().bottom + 1;
	else {
		// This is the first status item, reset speed/estimated finish toggle.
		sShowSpeed = true;
		sLastEstimatedFinishSpeedToggleTime = system_time();
	}
	rect.bottom = rect.top + kDefaultStatusViewHeight - 1;

	BStatusView* view = new BStatusView(rect, thread, type);
	// the BStatusView will resize itself if needed in its constructor
	ChildAt(0)->AddChild(view);
	fViewList.AddItem(view);

	ResizeTo(Bounds().Width(), view->Frame().bottom);

	// find out if the desktop is the active window
	// if the status window is the only thing to take over active state and
	// desktop was active to begin with, return focus back to desktop
	// when we are done
	bool desktopActive = false;
	{
		AutoLock<BLooper> lock(be_app);
		int32 count = be_app->CountWindows();
		for (int32 index = 0; index < count; index++) {
			BWindow* window = be_app->WindowAt(index);
			if (dynamic_cast<BDeskWindow*>(window) != NULL
				&& window->IsActive()) {
				desktopActive = true;
				break;
			}
		}
	}

	if (IsHidden()) {
		fRetainDesktopFocus = desktopActive;
		Minimize(false);
		Show();
	} else
		fRetainDesktopFocus &= desktopActive;
}


/**
 * @brief Initialise the status view for \a thread with item/size totals.
 *
 * @param thread      The thread ID whose status view should be initialised.
 * @param totalItems  Total number of items to process.
 * @param totalSize   Total byte size of all items.
 * @param destDir     Destination directory reference (may be NULL).
 * @param showCount   If true, display the item count in the progress indicator.
 */
void
BStatusWindow::InitStatusItem(thread_id thread, int32 totalItems,
	off_t totalSize, const entry_ref* destDir, bool showCount)
{
	AutoLock<BWindow> lock(this);

	int32 numItems = fViewList.CountItems();
	for (int32 index = 0; index < numItems; index++) {
		BStatusView* view = fViewList.ItemAt(index);
		if (view->Thread() == thread) {
			view->InitStatus(totalItems, totalSize, destDir, showCount);
			break;
		}
	}

}


/**
 * @brief Advance the progress bar for the operation running on \a thread.
 *
 * @param thread    The thread ID whose status view should be updated.
 * @param curItem   Name of the item currently being processed.
 * @param itemSize  Byte size of the current item.
 * @param optional  If true, the update may be skipped to reduce redraw frequency.
 */
void
BStatusWindow::UpdateStatus(thread_id thread, const char* curItem,
	off_t itemSize, bool optional)
{
	AutoLock<BWindow> lock(this);

	int32 numItems = fViewList.CountItems();
	for (int32 index = 0; index < numItems; index++) {
		BStatusView* view = fViewList.ItemAt(index);
		if (view->Thread() == thread) {
			view->UpdateStatus(curItem, itemSize, optional);
			break;
		}
	}
}


/**
 * @brief Remove the status view for \a thread and hide the window if empty.
 *
 * Removes the view from the list, reflows the remaining views, and hides or
 * closes the status window when no more operations are in progress.
 *
 * @param thread  The thread ID of the completed operation.
 */
void
BStatusWindow::RemoveStatusItem(thread_id thread)
{
	AutoLock<BWindow> lock(this);
	BStatusView* winner = NULL;

	int32 numItems = fViewList.CountItems();
	int32 index;
	for (index = 0; index < numItems; index++) {
		BStatusView* view = fViewList.ItemAt(index);
		if (view->Thread() == thread) {
			winner = view;
			break;
		}
	}

	if (winner != NULL) {
		// The height by which the other views will have to be moved
		// (in pixel count).
		float height = winner->Bounds().Height() + 1;
		fViewList.RemoveItem(winner);
		winner->RemoveSelf();
		delete winner;

		if (--numItems == 0 && !IsHidden()) {
			BDeskWindow* desktop = NULL;
			if (fRetainDesktopFocus) {
				AutoLock<BLooper> lock(be_app);
				int32 count = be_app->CountWindows();
				for (int32 index = 0; index < count; index++) {
					desktop = dynamic_cast<BDeskWindow*>(
						be_app->WindowAt(index));
					if (desktop != NULL)
						break;
				}
			}
			Hide();
			if (desktop != NULL) {
				// desktop was active when we first started,
				// make it active again
				desktop->Activate();
			}
		}

		for (; index < numItems; index++)
			fViewList.ItemAt(index)->MoveBy(0, -height);

		ResizeTo(Bounds().Width(), Bounds().Height() - height);
	}
}


/**
 * @brief Check whether the operation thread should stop or block due to user input.
 *
 * If the user pressed Pause, this method suspends the calling thread until
 * the user resumes it.  If the user pressed Stop, returns true.
 *
 * @param thread  The file-operation thread to check.
 * @return true if the operation has been cancelled; false otherwise.
 */
bool
BStatusWindow::CheckCanceledOrPaused(thread_id thread)
{
	bool wasCanceled = false;
	bool isPaused = false;

	BStatusView* view = NULL;

	AutoLock<BWindow> lock(this);
	// check if cancel or pause hit
	for (int32 index = fViewList.CountItems() - 1; index >= 0; index--) {
		view = fViewList.ItemAt(index);
		if (view && view->Thread() == thread) {
			isPaused = view->IsPaused();
			wasCanceled = view->WasCanceled();
			break;
		}
	}

	if (wasCanceled || !isPaused)
		return wasCanceled;

	if (isPaused && view != NULL) {
		// say we are paused
		view->Invalidate();
		thread_id thread = view->Thread();

		lock.Unlock();

		// and suspend ourselves
		// we will get resumed from BStatusView::MessageReceived
		ASSERT(find_thread(NULL) == thread);
		suspend_thread(thread);
	}

	return wasCanceled;
}


/**
 * @brief Attempt to cancel all in-progress operations when Tracker is quitting.
 *
 * Issues cancel requests to every active status view.
 *
 * @return true if no operations are in progress (safe to quit); false if some
 *         are still running and need more time to cancel.
 */
bool
BStatusWindow::AttemptToQuit()
{
	// called when tracker is quitting
	// try to cancel all the move/copy/empty trash threads in a nice way
	// by issuing cancels
	int32 count = fViewList.CountItems();

	if (count == 0)
		return true;

	for (int32 index = 0; index < count; index++)
		fViewList.ItemAt(index)->SetWasCanceled();

	// maybe next time everything will have been canceled
	return false;
}


/**
 * @brief Handle window activation to track whether the Desktop had focus.
 *
 * @param state  true if the window just became active.
 */
void
BStatusWindow::WindowActivated(bool state)
{
	if (!state)
		fRetainDesktopFocus = false;

	return _inherited::WindowActivated(state);
}


// #pragma mark - BStatusView


/**
 * @brief Construct a BStatusView for the given thread and operation type.
 *
 * Builds the status bar, operation icon, stop/pause buttons, and sets the
 * initial "Preparing..." caption appropriate for \a type.
 *
 * @param bounds  Initial bounding rectangle for this view.
 * @param thread  The thread ID of the file-operation thread to display.
 * @param type    The kind of operation (copy, move, delete, etc.).
 */
BStatusView::BStatusView(BRect bounds, thread_id thread, StatusWindowState type)
	:
	BView(bounds, "StatusView", B_FOLLOW_NONE, B_WILL_DRAW),
	fStatusBar(NULL),
	fType(type),
	fBitmap(NULL),
	fStopButton(NULL),
	fPauseButton(NULL),
	fThread(thread)
{
	Init();

	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowUIColor(ViewUIColor());
	SetHighColor(20, 20, 20);
	SetDrawingMode(B_OP_ALPHA);

	const float buttonWidth = 22;
	const float buttonHeight = 20;

	BRect rect(bounds);
	rect.OffsetTo(B_ORIGIN);
	rect.left += 40;
	rect.right -= buttonWidth * 2 + 12;
	rect.top += 6;
	rect.bottom = rect.top + 15;

	BString caption;
	int32 id = 0;

	switch (type) {
		case kCopyState:
			caption = B_TRANSLATE("Preparing to copy items" B_UTF8_ELLIPSIS);
			id = R_CopyStatusIcon;
			break;

		case kMoveState:
			caption = B_TRANSLATE("Preparing to move items" B_UTF8_ELLIPSIS);
			id = R_MoveStatusIcon;
			break;

		case kCreateLinkState:
			caption = B_TRANSLATE("Preparing to create links"
				B_UTF8_ELLIPSIS);
			id = R_MoveStatusIcon;
			break;

		case kTrashState:
			caption = B_TRANSLATE("Preparing to empty Trash" B_UTF8_ELLIPSIS);
			id = R_TrashIcon;
			break;

		case kVolumeState:
			caption = B_TRANSLATE("Searching for disks to mount"
				B_UTF8_ELLIPSIS);
			break;

		case kDeleteState:
			caption = B_TRANSLATE("Preparing to delete items"
				B_UTF8_ELLIPSIS);
			id = R_TrashIcon;
			break;

		case kRestoreFromTrashState:
			caption = B_TRANSLATE("Preparing to restore items"
				B_UTF8_ELLIPSIS);
			break;

		default:
			TRESPASS();
			break;
	}

	if (caption.Length() != 0) {
		fStatusBar = new BStatusBar(rect, "StatusBar", caption.String());
		fStatusBar->SetBarHeight(12);
		float width, height;
		fStatusBar->GetPreferredSize(&width, &height);
		fStatusBar->ResizeTo(fStatusBar->Frame().Width(), height);
		AddChild(fStatusBar);

		// Figure out how much room we need to display the additional status
		// message below the bar
		font_height fh;
		GetFontHeight(&fh);
		BRect f = fStatusBar->Frame();
		// Height is 3 x the "room from the top" + bar height + room for
		// string.
		ResizeTo(Bounds().Width(), f.top + f.Height() + fh.leading + fh.ascent
			+ fh.descent + f.top);
	}

	if (id != 0) {
		fBitmap = new BBitmap(BRect(0, 0, 16, 16), B_RGBA32);
		GetTrackerResources()->GetIconResource(id, B_MINI_ICON,
			fBitmap);
	}

	rect = Bounds();
	rect.left = rect.right - buttonWidth * 2 - 7;
	rect.right = rect.left + buttonWidth;
	rect.top = floorf((rect.top + rect.bottom) / 2 + 0.5) - buttonHeight / 2;
	rect.bottom = rect.top + buttonHeight;

	fPauseButton = new TCustomButton(rect, kPauseButton);
	fPauseButton->ResizeTo(buttonWidth, buttonHeight);
	AddChild(fPauseButton);

	rect.OffsetBy(buttonWidth + 2, 0);
	fStopButton = new TCustomButton(rect, kStopButton);
	fStopButton->ResizeTo(buttonWidth, buttonHeight);
	AddChild(fStopButton);
}


/**
 * @brief Destructor; releases the operation-type icon bitmap.
 */
BStatusView::~BStatusView()
{
	delete fBitmap;
}


/**
 * @brief Zero-initialise all counters and state variables.
 */
void
BStatusView::Init()
{
	fTotalSize = fItemSize = fSizeProcessed = fLastSpeedReferenceSize
		= fEstimatedFinishReferenceSize = 0;
	fCurItem = 0;
	fLastUpdateTime = fLastSpeedReferenceTime = fProcessStartTime
		= fLastSpeedUpdateTime = fEstimatedFinishReferenceTime
		= system_time();
	fCurrentBytesPerSecondSlot = 0;
	for (size_t i = 0; i < kBytesPerSecondSlots; i++)
		fBytesPerSecondSlot[i] = 0.0;

	fBytesPerSecond = 0.0;
	fShowCount = fWasCanceled = fIsPaused = false;
	fDestDir.SetTo("");
	fPendingStatusString[0] = '\0';
}


/**
 * @brief Set the totals and destination for an in-progress operation.
 *
 * Called by BStatusWindow::InitStatusItem() after the thread has determined
 * how much work is needed.
 *
 * @param totalItems  Total number of items to process.
 * @param totalSize   Total byte size of all items.
 * @param destDir     Destination directory reference, or NULL.
 * @param showCount   If true, the item count is shown in the status bar label.
 */
void
BStatusView::InitStatus(int32 totalItems, off_t totalSize,
	const entry_ref* destDir, bool showCount)
{
	Init();
	fTotalSize = totalSize;
	fShowCount = showCount;

	BEntry entry;
	char name[B_FILE_NAME_LENGTH];
	if (destDir != NULL && entry.SetTo(destDir) == B_OK) {
		entry.GetName(name);
		fDestDir.SetTo(name);
	}

	BString buffer;
	if (totalItems > 0) {
		char totalStr[32];
		buffer.SetTo(B_TRANSLATE("of %items"));
		snprintf(totalStr, sizeof(totalStr), "%" B_PRId32, totalItems);
		buffer.ReplaceFirst("%items", totalStr);
	}

	switch (fType) {
		case kCopyState:
			fStatusBar->Reset(B_TRANSLATE("Copying: "), buffer.String());
			break;

		case kCreateLinkState:
			fStatusBar->Reset(B_TRANSLATE("Creating links: "),
				buffer.String());
			break;

		case kMoveState:
			fStatusBar->Reset(B_TRANSLATE("Moving: "), buffer.String());
			break;

		case kTrashState:
			fStatusBar->Reset(
				B_TRANSLATE("Emptying Trash" B_UTF8_ELLIPSIS " "),
				buffer.String());
			break;

		case kDeleteState:
			fStatusBar->Reset(B_TRANSLATE("Deleting: "), buffer.String());
			break;

		case kRestoreFromTrashState:
			fStatusBar->Reset(B_TRANSLATE("Restoring: "), buffer.String());
			break;
	}

	fStatusBar->SetMaxValue(1);
		// SetMaxValue has to be here because Reset changes it to 100
	Invalidate();
}


/**
 * @brief Draw the operation icon and the current file name / path text.
 *
 * @param updateRect  The dirty region to redraw.
 */
void
BStatusView::Draw(BRect updateRect)
{
	if (fBitmap != NULL) {
		BPoint location;
		location.x = (fStatusBar->Frame().left
			- fBitmap->Bounds().Width()) / 2;
		location.y = (Bounds().Height()- fBitmap->Bounds().Height()) / 2;
		DrawBitmap(fBitmap, location);
	}

	BRect bounds(Bounds());
	be_control_look->DrawRaisedBorder(this, bounds, updateRect, ViewColor());

	SetHighUIColor(B_PANEL_TEXT_COLOR);

	BPoint tp = fStatusBar->Frame().LeftBottom();
	font_height fh;
	GetFontHeight(&fh);
	tp.y += ceilf(fh.leading) + ceilf(fh.ascent);
	if (IsPaused()) {
		DrawString(B_TRANSLATE("Paused: click to resume or stop"), tp);
		return;
	}

	BFont font;
	GetFont(&font);
	float normalFontSize = font.Size();
	float smallFontSize = max_c(normalFontSize * 0.8f, 8.0f);
	float availableSpace = fStatusBar->Frame().Width();
	availableSpace -= be_control_look->DefaultLabelSpacing();
		// subtract to provide some room between our two strings

	float destinationStringWidth = 0.f;
	BString destinationString(_DestinationString(&destinationStringWidth));
	availableSpace -= destinationStringWidth;

	float statusStringWidth = 0.f;
	BString statusString(_StatusString(availableSpace, smallFontSize,
		&statusStringWidth));

	if (statusStringWidth > availableSpace) {
		TruncateString(&destinationString, B_TRUNCATE_MIDDLE,
			availableSpace + destinationStringWidth - statusStringWidth);
	}

	BPoint textPoint = fStatusBar->Frame().LeftBottom();
	textPoint.y += ceilf(fh.leading) + ceilf(fh.ascent);

	if (destinationStringWidth > 0) {
		DrawString(destinationString.String(), textPoint);
	}

	if (LowColor().IsLight())
		SetHighColor(tint_color(LowColor(), B_DARKEN_4_TINT));
	else
		SetHighColor(tint_color(LowColor(), B_LIGHTEN_2_TINT));

	font.SetSize(smallFontSize);
	SetFont(&font, B_FONT_SIZE);

	textPoint.x = fStatusBar->Frame().right - statusStringWidth;
	DrawString(statusString.String(), textPoint);

	font.SetSize(normalFontSize);
	SetFont(&font, B_FONT_SIZE);
}


BString
BStatusView::_DestinationString(float* _width)
{
	if (fDestDir.Length() > 0) {
		BString buffer(B_TRANSLATE("To: %dir"));
		buffer.ReplaceFirst("%dir", fDestDir);

		*_width = ceilf(StringWidth(buffer.String()));
		return buffer;
	} else {
		*_width = 0;
		return BString();
	}
}


BString
BStatusView::_StatusString(float availableSpace, float fontSize,
	float* _width)
{
	BFont font;
	GetFont(&font);
	float oldSize = font.Size();
	font.SetSize(fontSize);
	SetFont(&font, B_FONT_SIZE);

	BString status;
	if (sShowSpeed) {
		status = _SpeedStatusString(availableSpace, _width);
	} else
		status = _TimeStatusString(availableSpace, _width);

	font.SetSize(oldSize);
	SetFont(&font, B_FONT_SIZE);
	return status;
}


BString
BStatusView::_SpeedStatusString(float availableSpace, float* _width)
{
	BString string(_FullSpeedString());
	*_width = StringWidth(string.String());
	if (*_width > availableSpace) {
		string.SetTo(_ShortSpeedString());
		*_width = StringWidth(string.String());
	}
	*_width = ceilf(*_width);
	return string;
}


BString
BStatusView::_FullSpeedString()
{
	BString buffer;
	if (fBytesPerSecond != 0.0) {
		char sizeBuffer[128];
		buffer.SetTo(B_TRANSLATE(
			"%SizeProcessed of %TotalSize, %BytesPerSecond/s"));
		buffer.ReplaceFirst("%SizeProcessed",
			string_for_size((double)fSizeProcessed, sizeBuffer,
			sizeof(sizeBuffer)));
		buffer.ReplaceFirst("%TotalSize",
			string_for_size((double)fTotalSize, sizeBuffer,
			sizeof(sizeBuffer)));
		buffer.ReplaceFirst("%BytesPerSecond",
			string_for_size(fBytesPerSecond, sizeBuffer, sizeof(sizeBuffer)));
	}

	return buffer;
}


BString
BStatusView::_ShortSpeedString()
{
	BString buffer;
	if (fBytesPerSecond != 0.0) {
		char sizeBuffer[128];
		buffer << B_TRANSLATE("%BytesPerSecond/s");
		buffer.ReplaceFirst("%BytesPerSecond",
			string_for_size(fBytesPerSecond, sizeBuffer, sizeof(sizeBuffer)));
	}

	return buffer;
}


BString
BStatusView::_TimeStatusString(float availableSpace, float* _width)
{
	double totalBytesPerSecond = (double)(fSizeProcessed
			- fEstimatedFinishReferenceSize)
		* 1000000LL / (system_time() - fEstimatedFinishReferenceTime);
	double secondsRemaining = (fTotalSize - fSizeProcessed)
		/ totalBytesPerSecond;
	time_t now = (time_t)real_time_clock();

	BString string;
	if (secondsRemaining < 0 || (sizeof(time_t) == 4
		&& now + secondsRemaining > INT32_MAX)) {
		string = B_TRANSLATE("Finish: after several years");
	} else {
		char timeText[32];
		time_t finishTime = (time_t)(now + secondsRemaining);

		if (finishTime - now > kSecondsPerDay) {
			BDateTimeFormat().Format(timeText, sizeof(timeText), finishTime,
					B_MEDIUM_DATE_FORMAT, B_MEDIUM_TIME_FORMAT);
		} else {
			BTimeFormat().Format(timeText, sizeof(timeText), finishTime,
					B_MEDIUM_TIME_FORMAT);
		}
		string = _FullTimeRemainingString(now, finishTime, timeText);
		float width = StringWidth(string.String());
		if (width > availableSpace) {
			string.SetTo(_ShortTimeRemainingString(timeText));
		}
	}

	if (_width != NULL)
		*_width = StringWidth(string.String());

	return string;
}


BString
BStatusView::_ShortTimeRemainingString(const char* timeText)
{
	BString buffer;

	// complete string too wide, try with shorter version
	buffer.SetTo(B_TRANSLATE("Finish: %time"));
	buffer.ReplaceFirst("%time", timeText);

	return buffer;
}


BString
BStatusView::_FullTimeRemainingString(time_t now, time_t finishTime,
	const char* timeText)
{
	BDurationFormat formatter;
	BString buffer;
	BString finishStr;
	if (finishTime - now > 60 * 60) {
		buffer.SetTo(B_TRANSLATE("Finish: %time - Over %finishtime left"));
		formatter.Format(finishStr, now * 1000000LL, finishTime * 1000000LL);
	} else {
		buffer.SetTo(B_TRANSLATE("Finish: %time - %finishtime left"));
		formatter.Format(finishStr, now * 1000000LL, finishTime * 1000000LL);
	}

	buffer.ReplaceFirst("%time", timeText);
	buffer.ReplaceFirst("%finishtime", finishStr);

	return buffer;
}


/**
 * @brief Set button targets to this view when attached to the status window.
 */
void
BStatusView::AttachedToWindow()
{
	fPauseButton->SetTarget(this);
	fStopButton->SetTarget(this);
}


/**
 * @brief Handle Pause and Stop button messages.
 *
 * @param message  The incoming button-press message.
 */
void
BStatusView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kPauseButton:
			fIsPaused = !fIsPaused;
			fPauseButton->SetValue(fIsPaused ? B_CONTROL_ON : B_CONTROL_OFF);
			if (fBytesPerSecond != 0.0) {
				fBytesPerSecond = 0.0;
				for (size_t i = 0; i < kBytesPerSecondSlots; i++)
					fBytesPerSecondSlot[i] = 0.0;
				Invalidate();
			}
			if (!fIsPaused) {
				fEstimatedFinishReferenceTime = system_time();
				fEstimatedFinishReferenceSize = fSizeProcessed;

				// force window update
				Invalidate();

				// let 'er rip
				resume_thread(Thread());
			}
			break;

		case kStopButton:
			fWasCanceled = true;
			if (fIsPaused) {
				// resume so that the copy loop gets a chance to finish up
				fIsPaused = false;

				// force window update
				Invalidate();

				// let 'er rip
				resume_thread(Thread());
			}
			break;

		default:
			_inherited::MessageReceived(message);
			break;
	}
}


/**
 * @brief Advance the progress bar and update the displayed filename and speed.
 *
 * @param curItem   Name of the item currently being processed; may be NULL.
 * @param itemSize  Byte size of the current item.
 * @param optional  If true the redraw may be suppressed to limit update rate.
 */
void
BStatusView::UpdateStatus(const char* curItem, off_t itemSize, bool optional)
{
	if (!fShowCount) {
		fStatusBar->Update((float)fItemSize / fTotalSize);
		fItemSize = 0;
		return;
	}

	if (curItem != NULL)
		fCurItem++;

	fItemSize += itemSize;
	fSizeProcessed += itemSize;

	bigtime_t currentTime = system_time();
	if (!optional || ((currentTime - fLastUpdateTime) > kMaxUpdateInterval)) {
		if (curItem != NULL || fPendingStatusString[0]) {
			// forced update or past update time

			BString buffer;
			buffer <<  fCurItem << " ";

			// if we don't have curItem, take the one from the stash
			const char* statusItem = curItem != NULL
				? curItem : fPendingStatusString;

			fStatusBar->Update((float)fItemSize / fTotalSize, statusItem,
				buffer.String());

			// we already displayed this item, clear the stash
			fPendingStatusString[0] =  '\0';

			fLastUpdateTime = currentTime;
		} else {
			// don't have a file to show, just update the bar
			fStatusBar->Update((float)fItemSize / fTotalSize);
		}

		if (currentTime
				>= fLastSpeedReferenceTime + kSpeedReferenceInterval) {
			// update current speed every kSpeedReferenceInterval
			fCurrentBytesPerSecondSlot
				= (fCurrentBytesPerSecondSlot + 1) % kBytesPerSecondSlots;
			fBytesPerSecondSlot[fCurrentBytesPerSecondSlot]
				= (double)(fSizeProcessed - fLastSpeedReferenceSize)
					* 1000000LL / (currentTime - fLastSpeedReferenceTime);
			fLastSpeedReferenceSize = fSizeProcessed;
			fLastSpeedReferenceTime = currentTime;
			fBytesPerSecond = 0.0;
			size_t count = 0;
			for (size_t i = 0; i < kBytesPerSecondSlots; i++) {
				if (fBytesPerSecondSlot[i] != 0.0) {
					fBytesPerSecond += fBytesPerSecondSlot[i];
					count++;
				}
			}
			if (count > 0)
				fBytesPerSecond /= count;

			BString toolTip = _TimeStatusString(1024.f, NULL);
			toolTip << "\n" << _FullSpeedString();
			SetToolTip(toolTip.String());

			Invalidate();
		}

		fItemSize = 0;
	} else if (curItem != NULL) {
		// stash away the name of the item we are currently processing
		// so we can show it when the time comes
		strncpy(fPendingStatusString, curItem, 127);
		fPendingStatusString[127] = '0';
	} else
		SetToolTip((const char*)NULL);
}


/**
 * @brief Mark this operation as cancelled by the user or by Tracker quitting.
 */
void
BStatusView::SetWasCanceled()
{
	fWasCanceled = true;
}
