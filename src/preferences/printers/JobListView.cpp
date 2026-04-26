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
 *   Copyright 2001-2010, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Pfeiffer
 */


/**
 * @file JobListView.cpp
 * @brief BListView-derived widgets for displaying spooled print jobs.
 *
 * Defines JobListView (the list itself, watching a SpoolFolder) and
 * JobItem (a per-row drawable with name, status, page count and size).
 */


#include "JobListView.h"

#include <stdio.h>

#include <Catalog.h>
#include <ControlLook.h>
#include <Locale.h>
#include <MimeType.h>
#include <Roster.h>
#include <StringFormat.h>
#include <Window.h>

#include "pr_server.h"
#include "Globals.h"
#include "Jobs.h"
#include "Messages.h"
#include "SpoolFolder.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "JobListView"


// #pragma mark -- JobListView


/**
 * @brief Constructs an empty single-selection job list view.
 *
 * @param frame Initial layout frame.
 */
JobListView::JobListView(BRect frame)
	:
	Inherited(frame, "jobs_list", B_SINGLE_SELECTION_LIST, B_FOLLOW_ALL,
		B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE | B_FULL_UPDATE_ON_RESIZE)
{
}


/**
 * @brief Destructor; deletes every owned JobItem.
 */
JobListView::~JobListView()
{
	while (!IsEmpty())
		delete RemoveItem((int32)0);
}


/**
 * @brief Attaches selection routing once the view is in a window.
 *
 * Sets the selection message to kMsgJobSelected and points it at the
 * containing window so PrintersWindow can update its buttons.
 */
void
JobListView::AttachedToWindow()
{
	Inherited::AttachedToWindow();

	SetSelectionMessage(new BMessage(kMsgJobSelected));
	SetTarget(Window());
}


/**
 * @brief Replaces the contents of the list with jobs from @a folder.
 *
 * Clears the existing items, then adds one JobItem per Job in @a folder.
 * Passing NULL just clears the list.
 *
 * @param folder Spool folder to mirror, or NULL to clear.
 */
void
JobListView::SetSpoolFolder(SpoolFolder* folder)
{
	// clear list
	while (!IsEmpty())
		delete RemoveItem((int32)0);

	if (folder == NULL)
		return;

	// Find directory containing printer definition nodes
	for (int32 i = 0; i < folder->CountJobs(); i++)
		AddJob(folder->JobAt(i));
}


/**
 * @brief Looks up the JobItem that wraps @a job.
 *
 * @param job Job to search for.
 *
 * @return Matching JobItem or NULL if no row owns @a job.
 */
JobItem*
JobListView::FindJob(Job* job) const
{
	const int32 n = CountItems();
	for (int32 i = 0; i < n; i++) {
		JobItem* item = dynamic_cast<JobItem*>(ItemAt(i));
		if (item && item->GetJob() == job)
			return item;
	}
	return NULL;
}


/**
 * @brief Returns the currently selected JobItem.
 *
 * @return Selected JobItem, or NULL if nothing is selected.
 */
JobItem*
JobListView::SelectedItem() const
{
	return dynamic_cast<JobItem*>(ItemAt(CurrentSelection()));
}


/**
 * @brief Appends a JobItem wrapping @a job and redraws the view.
 *
 * @param job Job to display; ownership stays with the SpoolFolder.
 */
void
JobListView::AddJob(Job* job)
{
	AddItem(new JobItem(job));
	Invalidate();
}


/**
 * @brief Removes the row that wraps @a job.
 *
 * @param job Job whose row should be removed; no-op if not present.
 */
void
JobListView::RemoveJob(Job* job)
{
	JobItem* item = FindJob(job);
	if (item) {
		RemoveItem(item);
		delete item;
		Invalidate();
	}
}


/**
 * @brief Refreshes the row associated with @a job after metadata changes.
 *
 * @param job Job whose attributes have changed; no-op if not in the list.
 */
void
JobListView::UpdateJob(Job* job)
{
	JobItem* item = FindJob(job);
	if (item) {
		item->Update();
		InvalidateItem(IndexOf(item));
	}
}


/**
 * @brief Re-queues the currently selected job if it failed.
 *
 * Sets the job's status back to kWaiting; the SpoolFolder watcher will
 * pick up the resulting attribute change and notify the UI.
 */
void
JobListView::RestartJob()
{
	JobItem* item = SelectedItem();
	if (item && item->GetJob()->Status() == kFailed) {
		// setting the state changes the file attribute and
		// we will receive a notification from SpoolFolder
		item->GetJob()->SetStatus(kWaiting);
	}
}


/**
 * @brief Cancels the currently selected job if it is not already
 *        processing.
 *
 * Marks the job as failed and removes it from the spool. Jobs in the
 * kProcessing state are left alone because print_server is actively using
 * them.
 */
void
JobListView::CancelJob()
{
	JobItem* item = SelectedItem();
	if (item && item->GetJob()->Status() != kProcessing) {
		item->GetJob()->SetStatus(kFailed);
		item->GetJob()->Remove();
	}
}


// #pragma mark -- JobItem


/**
 * @brief Constructs a row tied to @a job.
 *
 * Acquires a reference on @a job so the SpoolFolder cannot delete it
 * underneath us, then calls Update() to read attributes off the spool
 * file.
 *
 * @param job Job to display; reference counted by this item.
 */
JobItem::JobItem(Job* job)
	:
	BListItem(0, false),
	fJob(job),
	fIcon(NULL)
{
	fJob->Acquire();
	Update();
}


/**
 * @brief Destructor; releases the held Job reference and frees the cached
 *        icon.
 */
JobItem::~JobItem()
{
	fJob->Release();
	delete fIcon;
}


/**
 * @brief Refreshes cached display strings from the underlying spool file.
 *
 * Reads the job description, MIME type, page count, byte size and status
 * attributes; lazily fetches the application icon corresponding to the
 * MIME type the first time it is needed.
 */
void
JobItem::Update()
{
	BNode node(&fJob->EntryRef());
	if (node.InitCheck() != B_OK)
		return;

	node.ReadAttrString(PSRV_SPOOL_ATTR_DESCRIPTION, &fName);

	BString mimeType;
	node.ReadAttrString(PSRV_SPOOL_ATTR_MIMETYPE, &mimeType);

	entry_ref ref;
	if (fIcon == NULL && be_roster->FindApp(mimeType.String(), &ref) == B_OK) {
		BRect rect(BPoint(0, 0), be_control_look->ComposeIconSize(B_LARGE_ICON));
		fIcon = new BBitmap(rect, B_RGBA32);
		BMimeType type(mimeType.String());
		if (type.GetIcon(fIcon, (icon_size)(rect.IntegerHeight() + 1)) != B_OK) {
			delete fIcon;
			fIcon = NULL;
		}
	}

	fPages = "";
	int32 pages;
	static BStringFormat format(B_TRANSLATE("{0, plural, "
		"=-1{??? pages}"
		"=1{# page}"
		"other{# pages}}"));

	if (node.ReadAttr(PSRV_SPOOL_ATTR_PAGECOUNT,
		B_INT32_TYPE, 0, &pages, sizeof(pages)) == sizeof(pages)) {
		format.Format(fPages, pages);
	} else {
		// unknown page count, probably the printer is paginating without
		// software help.
		format.Format(fPages, -1);
	}

	fSize = "";
	off_t size;
	if (node.GetSize(&size) == B_OK) {
		char buffer[80];
		snprintf(buffer, sizeof(buffer), B_TRANSLATE("%.2f KB"),
			size / 1024.0);
		fSize = buffer;
	}

	fStatus = "";
	switch (fJob->Status()) {
		case kWaiting:
			fStatus = B_TRANSLATE("Waiting");
			break;

		case kProcessing:
			fStatus = B_TRANSLATE("Processing");
			break;

		case kFailed:
			fStatus = B_TRANSLATE("Failed");
			break;

		case kCompleted:
			fStatus = B_TRANSLATE("Completed");
			break;

		default:
			fStatus = B_TRANSLATE("Unknown status");
	}
}


/**
 * @brief Computes row height from the owning view's font metrics.
 *
 * Reserves space for two lines plus padding.
 *
 * @param owner View whose font metrics drive the calculation.
 * @param font  Active font used to compute line height.
 */
void
JobItem::Update(BView *owner, const BFont *font)
{
	BListItem::Update(owner, font);

	font_height height;
	font->GetHeight(&height);

	SetHeight((height.ascent + height.descent + height.leading) * 2.0 + 8.0);
}


/**
 * @brief Renders the row inside @a owner.
 *
 * Paints the selection background, optional MIME-type icon, then the job
 * description and status on the left and the page count / byte size on the
 * right. Strings that overflow are middle-truncated.
 *
 * @param owner    BListView doing the drawing.
 * @param complete When true redraw the entire row; honoured by the base
 *                 list view.
 */
void
JobItem::DrawItem(BView *owner, BRect, bool complete)
{
	BListView* list = dynamic_cast<BListView*>(owner);
	if (list) {
		BFont font;
		owner->GetFont(&font);

		font_height height;
		font.GetHeight(&height);
		float fntheight = height.ascent + height.descent + height.leading;

		BRect bounds = list->ItemFrame(list->IndexOf(this));

		rgb_color color = owner->ViewColor();
		rgb_color oldLowColor = owner->LowColor();
		rgb_color oldHighColor = owner->HighColor();

		if (IsSelected())
			color = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);

		owner->SetHighColor(color);
		owner->SetLowColor(color);

		owner->FillRect(bounds);

		owner->SetLowColor(oldLowColor);
		owner->SetHighColor(oldHighColor);

		BPoint iconPt(bounds.LeftTop() + BPoint(2.0, 2.0));
		float iconHeight = B_MINI_ICON;
#ifdef HAIKU_TARGET_PLATFORM_HAIKU
		if (fIcon)
			iconHeight = fIcon->Bounds().Height();
#endif
		BPoint leftTop(bounds.LeftTop() + BPoint(12.0 + iconHeight, 2.0));
		BPoint namePt(leftTop + BPoint(0.0, fntheight));
		BPoint statusPt(leftTop + BPoint(0.0, fntheight * 2.0));

		float x = owner->StringWidth(fPages.String()) + 32.0;
		BPoint pagePt(bounds.RightTop() + BPoint(-x, fntheight));
		BPoint sizePt(bounds.RightTop() + BPoint(-x, fntheight * 2.0));

		drawing_mode mode = owner->DrawingMode();
#ifdef HAIKU_TARGET_PLATFORM_HAIKU
	owner->SetDrawingMode(B_OP_ALPHA);
#else
	owner->SetDrawingMode(B_OP_OVER);
#endif

		if (fIcon)
			owner->DrawBitmap(fIcon, iconPt);

		// left of item
		BString name = fName;
		owner->TruncateString(&name, B_TRUNCATE_MIDDLE, pagePt.x - namePt.x);
		owner->DrawString(name.String(), name.Length(), namePt);
		BString status = fStatus;
		owner->TruncateString(&status, B_TRUNCATE_MIDDLE, sizePt.x - statusPt.x);
		owner->DrawString(status.String(), status.Length(), statusPt);

		// right of item
		owner->DrawString(fPages.String(), fPages.Length(), pagePt);
		owner->DrawString(fSize.String(), fSize.Length(), sizePt);

		owner->SetDrawingMode(mode);
	}
}
