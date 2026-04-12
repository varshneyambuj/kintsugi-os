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
 *   Distributed under the terms of the OpenTracker License.
 */


/**
 * @file HeaderView.cpp
 * @brief Header view for the Tracker info window showing icon and filename.
 *
 * HeaderView renders the top section of BInfoWindow, displaying the file's
 * large icon and its name. It supports inline name editing via a BTextView,
 * drag-and-drop of the represented entry, and a contextual popup menu.
 * Symlinks are automatically resolved to show the target's icon.
 *
 * @see BInfoWindow, GeneralInfoView
 */


#include "HeaderView.h"

#include <algorithm>

#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Locale.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <Volume.h>
#include <VolumeRoster.h>
#include <Window.h>

#include "Commands.h"
#include "FSUtils.h"
#include "GeneralInfoView.h"
#include "IconMenuItem.h"
#include "Model.h"
#include "NavMenu.h"
#include "PoseView.h"
#include "Shortcuts.h"
#include "Tracker.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "InfoWindow"


// Amount you have to move the mouse before a drag starts
const float kDragSlop = 3.0f;


/**
 * @brief Construct a HeaderView for the given file system model.
 *
 * Initialises icon and title rects based on control-look metrics, resolves
 * symlink targets so the correct icon is displayed, and sets the view's
 * preferred height to match the icon.
 *
 * @param model  The Model whose entry the header represents.
 */
HeaderView::HeaderView(Model* model)
	:
	BView("header", B_WILL_DRAW),
	fModel(model),
	fIconModel(model),
	fTitleEditView(NULL),
	fTrackingState(no_track),
	fIsDropTarget(false),
	fDoubleClick(false),
	fDragging(false)
{
	const float labelSpacing = be_control_look->DefaultLabelSpacing();
	fIconRect = BRect(BPoint(labelSpacing * 3.0f, labelSpacing),
		be_control_look->ComposeIconSize(B_LARGE_ICON));
	SetExplicitSize(BSize(B_SIZE_UNSET, fIconRect.Width() + 2 * fIconRect.top));

	// The title rect
	// The magic numbers are used to properly calculate the rect so that
	// when the editing text view is displayed, the position of the text
	// does not change.
	BFont currentFont;
	font_height fontMetrics;
	GetFont(&currentFont);
	currentFont.GetHeight(&fontMetrics);

	fTitleRect.left = fIconRect.right + labelSpacing;
	fTitleRect.top = 0;
	fTitleRect.bottom = fontMetrics.ascent + 1;
	fTitleRect.right = std::min(
		fTitleRect.left + currentFont.StringWidth(fModel->Name()),
		Bounds().Width() - labelSpacing);
	// Offset so that it centers with the icon
	fTitleRect.OffsetBy(0,
		fIconRect.top + ((fIconRect.Height() - fTitleRect.Height()) / 2));
	// Make some room for the border for when we are in edit mode
	// (Negative numbers increase the size of the rect)
	fTitleRect.InsetBy(-1, -2);

	// If the model is a symlink, then we deference the model to
	// get the targets icon
	if (fModel->IsSymLink()) {
		Model* resolvedModel = new Model(model->EntryRef(), true, true);
		if (resolvedModel->InitCheck() == B_OK)
			fIconModel = resolvedModel;
		// broken link, just show the symlink
		else
			delete resolvedModel;
	}
}


/**
 * @brief Destroy the HeaderView, releasing any separately-allocated icon model.
 */
HeaderView::~HeaderView()
{
	if (fIconModel != fModel)
		delete fIconModel;
}


/**
 * @brief Update the header when the underlying model changes.
 *
 * Re-resolves the icon model (handling symlinks) and invalidates the view
 * so it is repainted with the new state.
 *
 * @param model    Pointer to the updated Model.
 * @param message  The node-monitor message that triggered the update.
 */
void
HeaderView::ModelChanged(Model* model, BMessage* message)
{
	// Update the icon stuff
	if (fIconModel != fModel) {
		delete fIconModel;
		fIconModel = NULL;
	}

	fModel = model;
	if (fModel->IsSymLink()) {
		// if we are looking at a symlink, deference the model and look
		// at the target
		Model* resolvedModel = new Model(model->EntryRef(), true, true);
		if (resolvedModel->InitCheck() == B_OK) {
			if (fIconModel != fModel)
				delete fIconModel;
			fIconModel = resolvedModel;
		} else {
			fIconModel = model;
			delete resolvedModel;
		}
	}

	Invalidate();
}


/**
 * @brief Switch the header to a new model after a symlink retarget.
 *
 * Sets a new Model pointer and re-resolves its icon, then invalidates the
 * view.  Used when the user picks a new link target in the info window.
 *
 * @param model  The newly targeted Model.
 */
void
HeaderView::ReLinkTargetModel(Model* model)
{
	fModel = model;
	if (fModel->IsSymLink()) {
		Model* resolvedModel = new Model(model->EntryRef(), true, true);
		if (resolvedModel->InitCheck() == B_OK) {
			if (fIconModel != fModel)
				delete fIconModel;
			fIconModel = resolvedModel;
		} else {
			fIconModel = fModel;
			delete resolvedModel;
		}
	}
	Invalidate();
}


/**
 * @brief Enter inline filename-editing mode for the title.
 *
 * Creates a BTextView pre-populated with the model's current name,
 * installs a key filter that commits on Return and cancels on Escape,
 * and gives the text view keyboard focus.  Does nothing if editing is
 * already in progress.
 */
void
HeaderView::BeginEditingTitle()
{
	if (fTitleEditView != NULL)
		return;

	BFont font(be_plain_font);
	font.SetSize(font.Size() + 2);
	BRect textFrame(fTitleRect);
	textFrame.right = Bounds().Width() - 5;
	BRect textRect(textFrame);
	textRect.OffsetTo(0, 0);
	textRect.InsetBy(1, 1);

	// Just make it some really large size, since we don't do any line
	// wrapping. The text filter will make sure to scroll the cursor
	// into position

	textRect.right = 2000;
	fTitleEditView = new BTextView(textFrame, "text_editor",
		textRect, &font, 0, B_FOLLOW_ALL, B_WILL_DRAW);
	fTitleEditView->SetText(fModel->Name());
	DisallowFilenameKeys(fTitleEditView);

	// Reset the width of the text rect
	textRect = fTitleEditView->TextRect();
	textRect.right = fTitleEditView->LineWidth() + 20;
	fTitleEditView->SetTextRect(textRect);
	fTitleEditView->SetWordWrap(false);
	// Add filter for catching B_RETURN and B_ESCAPE key's
	fTitleEditView->AddFilter(
		new BMessageFilter(B_KEY_DOWN, HeaderView::TextViewFilter));

	BScrollView* scrollView = new BScrollView("BorderView", fTitleEditView,
		0, 0, false, false, B_PLAIN_BORDER);
	AddChild(scrollView);
	fTitleEditView->SelectAll();
	fTitleEditView->MakeFocus();

	Window()->UpdateIfNeeded();
}


/**
 * @brief Commit or discard the in-progress filename edit.
 *
 * If @p commit is true, attempts to rename the file via EditModelName() and
 * adjusts the title rect width accordingly.  Removes the text view either
 * way, and reopens editing if the rename failed with a recoverable error.
 *
 * @param commit  Pass true to apply the new name, false to discard.
 */
void
HeaderView::FinishEditingTitle(bool commit)
{
	if (fTitleEditView == NULL || !commit)
		return;

	const char* name = fTitleEditView->Text();
	size_t length = (size_t)fTitleEditView->TextLength();

	status_t result = EditModelName(fModel, name, length);
	bool reopen = (result == B_NAME_TOO_LONG || result == B_NAME_IN_USE);

	if (result == B_OK) {
		// Adjust the size of the text rect
		BFont currentFont(be_plain_font);
		currentFont.SetSize(currentFont.Size() + 2);
		float stringWidth = currentFont.StringWidth(fTitleEditView->Text());
		fTitleRect.right = std::min(fTitleRect.left + stringWidth,
			Bounds().Width() - 5);
	}

	// Remove view
	BView* scrollView = fTitleEditView->Parent();
	if (scrollView != NULL) {
		RemoveChild(scrollView);
		delete scrollView;
		fTitleEditView = NULL;
	}

	if (reopen)
		BeginEditingTitle();
}


/**
 * @brief Render the header — background, icon, and filename.
 *
 * Clears the background, draws the model icon via IconCache, then renders
 * the bold title string (truncated if needed) at the calculated position.
 * Skips the title while inline editing is active.
 */
void
HeaderView::Draw(BRect)
{
	// Set the low color for anti-aliasing
	SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	// Clear the old contents
	SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	FillRect(Bounds());

	rgb_color labelColor = ui_color(B_PANEL_TEXT_COLOR);

	// Draw the icon, straddling the border
	SetDrawingMode(B_OP_OVER);
	IconCache::sIconCache->Draw(fIconModel, this, fIconRect.LeftTop(),
		kNormalIcon, fIconRect.Size(), true);
	SetDrawingMode(B_OP_COPY);

	// Font information
	font_height fontMetrics;
	BFont currentFont;
	float lineBase = 0;

	// Draw the main title if the user is not currently editing it
	if (fTitleEditView == NULL) {
		SetFont(be_bold_font);
		SetFontSize(be_bold_font->Size());
		GetFont(&currentFont);
		currentFont.GetHeight(&fontMetrics);
		lineBase = fTitleRect.bottom - fontMetrics.descent;
		SetHighColor(labelColor);
		MovePenTo(BPoint(fIconRect.right + 6, lineBase));

		// Recalculate the rect width
		fTitleRect.right = std::min(fTitleRect.left
				+ currentFont.StringWidth(fModel->Name()),
			Bounds().Width() - 5);
		// Check for possible need of truncation
		if (StringWidth(fModel->Name()) > fTitleRect.Width()) {
			BString nameString(fModel->Name());
			TruncateString(&nameString, B_TRUNCATE_END,
				fTitleRect.Width() - 2);
			DrawString(nameString.String());
		} else
			DrawString(fModel->Name());
	}

}


/**
 * @brief Commit the title edit when the view loses keyboard focus.
 *
 * @param focus  True when gaining focus, false when losing it.
 */
void
HeaderView::MakeFocus(bool focus)
{
	if (!focus && fTitleEditView != NULL)
		FinishEditingTitle(true);
}


/**
 * @brief Commit the title edit when the window loses activation.
 *
 * @param active  True when the window becomes active, false when it deactivates.
 */
void
HeaderView::WindowActivated(bool active)
{
	if (active)
		return;

	if (fTitleEditView != NULL)
		FinishEditingTitle(true);
}


/**
 * @brief Handle a mouse-button press in the header.
 *
 * Begins title editing on a click inside the title rect, commits any open
 * edit on a click elsewhere, and initiates icon tracking (drag or double-click
 * detection) on clicks within the icon rect.  A secondary button press over
 * the icon opens a contextual popup menu.
 *
 * @param where  The point in view coordinates where the button was pressed.
 */
void
HeaderView::MouseDown(BPoint where)
{
	// Assume this isn't part of a double click
	fDoubleClick = false;

	if (fTitleRect.Contains(where) && fTitleEditView == NULL)
		BeginEditingTitle();
	else if (fTitleEditView != NULL)
		FinishEditingTitle(true);
	else if (fIconRect.Contains(where)) {
		uint32 buttons;
		Window()->CurrentMessage()->FindInt32("buttons", (int32*)&buttons);
		if (SecondaryMouseButtonDown(modifiers(), buttons)) {
			// Show contextual menu
			BPopUpMenu* contextMenu = new BPopUpMenu("PoseContext", false, false);
			if (contextMenu != NULL) {
				BuildContextMenu(contextMenu);
				contextMenu->SetAsyncAutoDestruct(true);
				contextMenu->Go(ConvertToScreen(where), true, true,
					ConvertToScreen(fIconRect));
			}
		} else {
			// Check to see if the point is actually on part of the icon,
			// versus just in the container rect. The icons are always
			// the large version
			BPoint offsetPoint;
			offsetPoint.x = where.x - fIconRect.left;
			offsetPoint.y = where.y - fIconRect.top;
			if (IconCache::sIconCache->IconHitTest(offsetPoint, fIconModel, kNormalIcon,
					fIconRect.Size())) {
				// Can't drag the trash anywhere..
				fTrackingState = fModel->IsTrash()
					? open_only_track : icon_track;

				// Check for possible double click
				if (abs((int32)(fClickPoint.x - where.x)) < kDragSlop
					&& abs((int32)(fClickPoint.y - where.y)) < kDragSlop) {
					int32 clickCount;
					Window()->CurrentMessage()->FindInt32("clicks",
						&clickCount);

					// This checks the* previous* click point
					if (clickCount == 2) {
						offsetPoint.x = fClickPoint.x - fIconRect.left;
						offsetPoint.y = fClickPoint.y - fIconRect.top;
						fDoubleClick
							= IconCache::sIconCache->IconHitTest(offsetPoint,
							fIconModel, kNormalIcon, fIconRect.Size());
					}
				}
			}
		}
	}

	fClickPoint = where;
	SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
}


/**
 * @brief Handle mouse movement, updating drag state and drop highlighting.
 *
 * If an external drag is in progress and the pointer is over the icon rect,
 * the icon is highlighted as a drop target.  When the pointer moves beyond
 * the slop threshold while icon-tracking, a drag-and-drop operation is
 * initiated with a composite bitmap representing the file.
 *
 * @param where        Current pointer position in view coordinates.
 * @param dragMessage  Non-NULL while a drag is being tracked by this view.
 */
void
HeaderView::MouseMoved(BPoint where, uint32, const BMessage* dragMessage)
{
	if (dragMessage != NULL && dragMessage->ReturnAddress() != BMessenger(this)
		&& dragMessage->what == B_SIMPLE_DATA
		&& BPoseView::CanHandleDragSelection(fModel, dragMessage,
			(modifiers() & B_CONTROL_KEY) != 0)) {
		// highlight drag target
		bool overTarget = fIconRect.Contains(where);
		SetDrawingMode(B_OP_OVER);
		if (overTarget != fIsDropTarget) {
			IconCache::sIconCache->Draw(fIconModel, this, fIconRect.LeftTop(),
				overTarget ? kSelectedIcon : kNormalIcon, fIconRect.Size(), true);
			fIsDropTarget = overTarget;
		}
	}

	switch (fTrackingState) {
		case icon_track:
		{
			if (fDragging)
				break;

			uint32 buttons = Window()->CurrentMessage()->GetInt32("buttons", 0);
			if (buttons == 0)
				break;

			if (abs((int32)(where.x - fClickPoint.x)) <= kDragSlop
				&& abs((int32)(where.y - fClickPoint.y)) <= kDragSlop) {
				break;
			}

			// Find the required height
			BFont font;
			GetFont(&font);
			float width = std::min(fIconRect.Width() + font.StringWidth(fModel->Name()) + 4,
				fIconRect.Width() * 3);
			float height = CurrentFontHeight() + fIconRect.Height() + 8;

			BRect rect(0, 0, width, height);
			BBitmap* dragBitmap = new BBitmap(rect, B_RGBA32, true);
			dragBitmap->Lock();

			BView* view = new BView(dragBitmap->Bounds(), "", B_FOLLOW_NONE, 0);
			dragBitmap->AddChild(view);
			view->SetOrigin(0, 0);

			BRect clipRect(view->Bounds());
			BRegion newClip;
			newClip.Set(clipRect);
			view->ConstrainClippingRegion(&newClip);

			// Transparent draw magic
			view->SetHighColor(0, 0, 0, 0);
			view->FillRect(view->Bounds());
			view->SetDrawingMode(B_OP_ALPHA);

			rgb_color textColor = ui_color(B_PANEL_TEXT_COLOR);
			textColor.alpha = 128;
				// set transparency by value
			view->SetHighColor(textColor);
			view->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_COMPOSITE);

			// Draw the icon
			float hIconOffset = (rect.Width() - fIconRect.Width()) / 2;
			IconCache::sIconCache->Draw(fIconModel, view, BPoint(hIconOffset, 0), kNormalIcon,
				fIconRect.Size(), true);

			// See if we need to truncate the string
			BString nameString(fModel->Name());
			if (view->StringWidth(fModel->Name()) > rect.Width())
				view->TruncateString(&nameString, B_TRUNCATE_END, rect.Width() - 5);

			// Draw the label
			font_height fontHeight;
			font.GetHeight(&fontHeight);
			float leftText
				= roundf((view->StringWidth(nameString.String()) - fIconRect.Width()) / 2);
			float x = hIconOffset - leftText + 2;
			float y = fIconRect.Height() + fontHeight.ascent + 2;
			view->MovePenTo(BPoint(x, y));
			view->DrawString(nameString.String());

			view->Sync();
			dragBitmap->Unlock();

			BMessage dragMessage(B_REFS_RECEIVED);
			dragMessage.AddPoint("click_pt", fClickPoint);
			BPoint tmpLoc;
			uint32 button;
			GetMouse(&tmpLoc, &button);
			if (button)
				dragMessage.AddInt32("buttons", (int32)button);

			dragMessage.AddInt32("be:actions",
				(modifiers() & B_OPTION_KEY) != 0 ? B_COPY_TARGET : B_MOVE_TARGET);
			dragMessage.AddRef("refs", fModel->EntryRef());
			x = fClickPoint.x - fIconRect.left + hIconOffset;
			y = fClickPoint.y - fIconRect.top;
			DragMessage(&dragMessage, dragBitmap, B_OP_ALPHA, BPoint(x, y), this);
			fDragging = true;
			break;
		}

		case open_only_track :
			// Special type of entry that can't be renamed or drag and dropped
			// It can only be opened by double clicking on the icon
			break;

		case no_track:
			// No mouse tracking, do nothing
			break;
	}
}


/**
 * @brief Complete a mouse-tracking sequence, launching the entry on double-click.
 *
 * On a double-click over the icon, posts a B_REFS_RECEIVED message to be_app
 * to open the file.  Resets tracking state and drag flag.
 *
 * @param where  The point in view coordinates where the button was released.
 */
void
HeaderView::MouseUp(BPoint where)
{
	if ((fTrackingState == icon_track || fTrackingState == open_only_track)
		&& fIconRect.Contains(where)) {
		// If it was a double click, then tell Tracker to open the item
		// The CurrentMessage() here does* not* have a "clicks" field,
		// which is why we are tracking the clicks with this temp var
		if (fDoubleClick) {
			// Double click, launch.
			BMessage message(B_REFS_RECEIVED);
			message.AddRef("refs", fModel->EntryRef());

			// add a messenger to the launch message that will be used to
			// dispatch scripting calls from apps to the PoseView
			message.AddMessenger("TrackerViewToken", BMessenger(this));
			be_app->PostMessage(&message);
			fDoubleClick = false;
		}
	}

	// End mouse tracking
	fDragging = false;
	fTrackingState = no_track;
}


/**
 * @brief Accept dropped entries onto the header icon.
 *
 * When a compatible drag is dropped on the icon rect, delegates to
 * BPoseView::HandleDropCommon() to perform the copy/move/link operation,
 * then invalidates the icon rect.
 *
 * @param message  The received BMessage, which may be a drop or other message.
 */
void
HeaderView::MessageReceived(BMessage* message)
{
	if (message->WasDropped()
		&& message->what == B_SIMPLE_DATA
		&& message->ReturnAddress() != BMessenger(this)
		&& fIconRect.Contains(ConvertFromScreen(message->DropPoint()))
		&& BPoseView::CanHandleDragSelection(fModel, message,
			(modifiers() & B_CONTROL_KEY) != 0)) {
		BPoseView::HandleDropCommon(message, fModel, 0, this,
			message->DropPoint());
		Invalidate(fIconRect);
		return;
	}

	BView::MessageReceived(message);
}



/**
 * @brief Populate a contextual menu for the header icon.
 *
 * Adds navigation, open, rename, unmount, identify, and permissions menu
 * items appropriate for the current model type.  A navigation submenu is
 * added for directories and volumes.
 *
 * @param parent  The BMenu to populate; must not be NULL.
 * @return B_OK on success, B_BAD_VALUE if @p parent is NULL.
 */
status_t
HeaderView::BuildContextMenu(BMenu* parent)
{
	if (parent == NULL)
		return B_BAD_VALUE;

	// Add navigation menu if this is not a symlink
	// Symlink's to directories are OK however!
	BEntry entry(fModel->EntryRef());
	entry_ref ref;
	entry.GetRef(&ref);
	Model model(&entry);
	bool navigate = false;
	if (model.InitCheck() == B_OK) {
		if (model.IsSymLink()) {
			// Check if it's to a directory
			if (entry.SetTo(model.EntryRef(), true) == B_OK) {
				navigate = entry.IsDirectory();
				entry.GetRef(&ref);
			}
		} else if (model.IsDirectory() || model.IsVolume())
			navigate = true;
	}
	ModelMenuItem* navigationItem = NULL;
	if (navigate) {
		navigationItem = new ModelMenuItem(new Model(model),
			new BNavMenu(model.Name(), B_REFS_RECEIVED, be_app, Window()));

		// setup a navigation menu item which will dynamically load items
		// as menu items are traversed
		BNavMenu* navMenu = dynamic_cast<BNavMenu*>(navigationItem->Submenu());
		if (navMenu != NULL)
			navMenu->SetNavDir(&ref);

		navigationItem->SetLabel(model.Name());
		navigationItem->SetEntry(&entry);

		parent->AddItem(navigationItem, 0);
		parent->AddItem(new BSeparatorItem(), 1);

		BMessage* message = new BMessage(B_REFS_RECEIVED);
		message->AddRef("refs", &ref);
		navigationItem->SetMessage(message);
		navigationItem->SetTarget(be_app);
	}

	parent->AddItem(TShortcuts().OpenItem());

	if (!model.IsDesktop() && !model.IsRoot() && !model.IsTrash()) {
		parent->AddItem(TShortcuts().EditNameItem());
		parent->AddSeparatorItem();

		if (fModel->IsVolume()) {
			BMenuItem* item = TShortcuts().UnmountItem();
			parent->AddItem(item);
			// volume model, enable/disable the Unmount item
			BVolume boot;
			BVolumeRoster().GetBootVolume(&boot);
			BVolume volume;
			volume.SetTo(fModel->NodeRef()->device);
			if (volume == boot)
				item->SetEnabled(false);
		}
	}

	if (!model.IsRoot() && !model.IsVolume() && !model.IsTrash())
		parent->AddItem(TShortcuts().IdentifyItem());

	if (model.IsTrash())
		parent->AddItem(TShortcuts().EmptyTrashItem());

	BMenuItem* sizeItem = NULL;
	if (model.IsDirectory() && !model.IsVolume() && !model.IsRoot())  {
		parent->AddItem(sizeItem
				= new BMenuItem(B_TRANSLATE("Recalculate folder size"),
			new BMessage(kRecalculateSize)));
	}

	if (model.IsSymLink()) {
		parent->AddItem(sizeItem
				= new BMenuItem(B_TRANSLATE("Set new link target"),
			new BMessage(kSetLinkTarget)));
	}

	parent->AddItem(new BSeparatorItem());
	parent->AddItem(new BMenuItem(B_TRANSLATE("Permissions"),
		new BMessage(kPermissionsSelected), 'P'));

	parent->SetFont(be_plain_font);
	parent->SetTargetForItems(this);

	// Reset the nav menu to be_app
	if (navigate)
		navigationItem->SetTarget(be_app);
	if (sizeItem)
		sizeItem->SetTarget(Window());

	return B_OK;
}


/**
 * @brief Message filter for the inline title-editing BTextView.
 *
 * Adjusts the text rect width on every keystroke and scrolls the cursor into
 * view.  Intercepts Return (commit) and Escape (cancel) keys so the editing
 * session can be closed without dispatching the key to the text view.
 *
 * @param message  The key-down BMessage being filtered.
 * @param filter   The owning BMessageFilter (used to locate the HeaderView).
 * @return B_SKIP_MESSAGE for Return/Escape, B_DISPATCH_MESSAGE otherwise.
 */
filter_result
HeaderView::TextViewFilter(BMessage* message, BHandler**,
	BMessageFilter* filter)
{
	uchar key;
	HeaderView* attribView = static_cast<HeaderView*>(
		static_cast<BWindow*>(filter->Looper())->FindView("header"));

	// Adjust the size of the text rect
	BRect nuRect(attribView->TextView()->TextRect());
	nuRect.right = attribView->TextView()->LineWidth() + 20;
	attribView->TextView()->SetTextRect(nuRect);

	// Make sure the cursor is in view
	attribView->TextView()->ScrollToSelection();
	if (message->FindInt8("byte", (int8*)&key) != B_OK)
		return B_DISPATCH_MESSAGE;

	if (key == B_RETURN || key == B_ESCAPE) {
		attribView->FinishEditingTitle(key == B_RETURN);
		return B_SKIP_MESSAGE;
	}

	return B_DISPATCH_MESSAGE;
}


/**
 * @brief Return the full line height of the view's current font.
 *
 * @return Ascent + descent + leading + 2 pixels, in view coordinates.
 */
float
HeaderView::CurrentFontHeight()
{
	BFont font;
	GetFont(&font);
	font_height fontHeight;
	font.GetHeight(&fontHeight);

	return fontHeight.ascent + fontHeight.descent + fontHeight.leading + 2;
}
