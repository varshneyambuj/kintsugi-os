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
 *   Copyright 2007-2016, Haiku, Inc. All rights reserved.
 *   Copyright 2001-2002 Dr. Zoidberg Enterprises. All rights reserved.
 *   Copyright 2011, Clemens Zeidler <haiku@clemens-zeidler.de>
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file FilterConfigView.cpp
 * @brief Implements FiltersConfigView and its supporting drag-aware
 *        listview.
 *
 * Filters live in two ordered chains (inbound and outbound) on a
 * BMailAccountSettings; this view exposes one chain at a time with
 * add/remove buttons and a drag-to-reorder listview that translates drops
 * into MoveFilterSettings() calls.
 */


#include "FilterConfigView.h"

#include <stdio.h>

#include <Alert.h>
#include <Bitmap.h>
#include <Box.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <ScrollView.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Config Views"


// FiltersConfigView
/** @brief Posted by DragListView once it has reordered an item locally;
           the view then mirrors the change into the protocol settings. */
const uint32 kMsgFilterMoved = 'flmv';
/** @brief Posted when the user picks Inbound or Outbound from the chain
           pop-up menu. */
const uint32 kMsgChainSelected = 'chsl';
/** @brief Posted when the user picks a filter from the Add menu. */
const uint32 kMsgAddFilter = 'addf';
/** @brief Posted when the Remove button is clicked. */
const uint32 kMsgRemoveFilter = 'rmfi';
/** @brief Posted when the listview selection changes. */
const uint32 kMsgFilterSelected = 'fsel';

/** @brief Internal drag-message what code carrying the source-row index. */
const uint32 kMsgItemDragged = 'itdr';


/**
 * @brief BListView subclass that turns row drag-and-drop into a custom
 *        "item moved" notification message.
 *
 * Renders a translucent snapshot of the dragged row, paints a one-pixel
 * insertion indicator while the drag is in flight, and finally posts
 * @c fItemMovedMessage with @c "from" and @c "to" indices when the drop
 * lands inside the same listview.
 */
class DragListView : public BListView {
public:
	/**
	 * @brief Constructs a drag-aware listview.
	 *
	 * @param name           View name.
	 * @param type           Selection mode forwarded to BListView.
	 * @param itemMovedMsg   Template message sent on successful reorder;
	 *                       ownership is transferred to this view.
	 */
	DragListView(const char* name,
			list_view_type type = B_SINGLE_SELECTION_LIST,
			 BMessage* itemMovedMsg = NULL)
		:
		BListView(name, type),
		fDragging(false),
		fItemMovedMessage(itemMovedMsg)
	{
	}

	/**
	 * @brief Builds the drag bitmap from row @a index and starts the drag.
	 *
	 * @param point        Mouse-down location in view coordinates.
	 * @param index        Row being picked up.
	 * @param wasSelected  Whether the row was selected before the drag
	 *                     started (forwarded to the BListView base).
	 * @return Always @c true; the drag is always accepted.
	 */
	virtual bool InitiateDrag(BPoint point, int32 index, bool wasSelected)
	{
		BRect frame(ItemFrame(index));
		BBitmap *bitmap = new BBitmap(frame.OffsetToCopy(B_ORIGIN), B_RGBA32,
			true);
		BView *view = new BView(bitmap->Bounds(), NULL, 0, 0);
		bitmap->AddChild(view);

		if (view->LockLooper()) {
			BListItem *item = ItemAt(index);
			bool selected = item->IsSelected();

			view->SetLowColor(225, 225, 225, 128);
			view->FillRect(view->Bounds());

			if (selected)
				item->Deselect();
			ItemAt(index)->DrawItem(view, view->Bounds(), true);
			if (selected)
				item->Select();

			view->UnlockLooper();
		}
		fLastDragTarget = -1;
		fDragIndex = index;
		fDragging = true;

		BMessage drag(kMsgItemDragged);
		drag.AddInt32("index", index);
		DragMessage(&drag, bitmap, B_OP_ALPHA, point - frame.LeftTop(), this);

		return true;
	}

	/**
	 * @brief Toggles the one-pixel insertion-line indicator at row
	 *        @a target using XOR drawing so it can be erased by a second
	 *        call.
	 *
	 * @param target  Row index where the indicator should appear; values
	 *                past the end snap to the last row's bottom edge.
	 */
	void DrawDragTargetIndicator(int32 target)
	{
		PushState();
		SetDrawingMode(B_OP_INVERT);

		bool last = false;
		if (target >= CountItems())
			target = CountItems() - 1, last = true;

		BRect frame = ItemFrame(target);
		if (last)
			frame.OffsetBy(0,frame.Height());
		frame.bottom = frame.top + 1;

		FillRect(frame);

		PopState();
	}

	/**
	 * @brief Tracks the cursor while a row is being dragged and updates
	 *        the insertion indicator.
	 *
	 * @param point    Cursor location in view coordinates.
	 * @param transit  Standard BView transit code.
	 * @param msg      Drag message attached to the in-flight drag.
	 */
	virtual void MouseMoved(BPoint point, uint32 transit, const BMessage *msg)
	{
		BListView::MouseMoved(point, transit, msg);

		if ((transit != B_ENTERED_VIEW && transit != B_INSIDE_VIEW)
			|| !fDragging)
			return;

		int32 target = IndexOf(point);
		if (target == -1)
			target = CountItems();

		// correct the target insertion index
		if (target == fDragIndex || target == fDragIndex + 1)
			target = -1;

		if (target == fLastDragTarget)
			return;

		// remove old target indicator
		if (fLastDragTarget != -1)
			DrawDragTargetIndicator(fLastDragTarget);

		// draw new one
		fLastDragTarget = target;
		if (target != -1)
			DrawDragTargetIndicator(target);
	}

	/**
	 * @brief Clears the drag state and erases the insertion indicator
	 *        when the user releases the mouse.
	 *
	 * @param point  Mouse-up location.
	 */
	virtual void MouseUp(BPoint point)
	{
		if (fDragging) {
			fDragging = false;
			if (fLastDragTarget != -1)
				DrawDragTargetIndicator(fLastDragTarget);
		}
		BListView::MouseUp(point);
	}

	/**
	 * @brief Handles the internal drop notification, moves the row, and
	 *        emits the user-supplied "moved" message with @c from and @c
	 *        to indices.
	 *
	 * @param msg  Incoming BMessage; only @c kMsgItemDragged is consumed
	 *             here, others fall through to BListView.
	 */
	virtual void MessageReceived(BMessage *msg)
	{
		switch (msg->what) {
			case kMsgItemDragged:
			{
				int32 source = msg->FindInt32("index");
				BPoint point = msg->FindPoint("_drop_point_");
				ConvertFromScreen(&point);
				int32 to = IndexOf(point);
				if (to > fDragIndex)
					to--;
				if (to == -1)
					to = CountItems() - 1;

				if (source != to) {
					MoveItem(source,to);

					if (fItemMovedMessage != NULL) {
						BMessage msg(fItemMovedMessage->what);
						msg.AddInt32("from",source);
						msg.AddInt32("to",to);
						Messenger().SendMessage(&msg);
					}
				}
				break;
			}
		}
		BListView::MessageReceived(msg);
	}

private:
	bool		fDragging;
	int32		fLastDragTarget,fDragIndex;
	BMessage	*fItemMovedMessage;
};


//	#pragma mark -


/**
 * @brief BBox that frames the BMailSettingsView supplied by a filter
 *        add-on and forwards SaveInto() requests to it.
 */
class FilterSettingsView : public BBox {
public:
	/**
	 * @brief Wraps @a settingsView in a labelled BBox.
	 *
	 * @param label         Title shown on the BBox border.
	 * @param settingsView  View provided by the filter add-on; ownership is
	 *                      transferred and it is added as a child.
	 */
	FilterSettingsView(const BString& label, BMailSettingsView* settingsView)
		:
		BBox("filter"),
		fSettingsView(settingsView)
	{
		SetLabel(label);

		BView* contents = new BView("contents", 0);
		AddChild(contents);

		BLayoutBuilder::Group<>(contents, B_VERTICAL)
			.SetInsets(B_USE_DEFAULT_SPACING)
			.Add(fSettingsView);
	}

	/**
	 * @brief Forwards persistence to the wrapped settings view.
	 *
	 * @param settings  Add-on settings to write into.
	 * @return Whatever the underlying SaveInto() implementation returns.
	 */
	status_t SaveInto(BMailAddOnSettings& settings) const
	{
		return fSettingsView->SaveInto(settings);
	}

private:
			BMailSettingsView*	fSettingsView;
};


//	#pragma mark -


/**
 * @brief Builds the filters tab for @a account starting on the inbound
 *        chain.
 *
 * Creates the chain pop-up, the listview, and the Add/Remove menu
 * buttons, then calls _SetDirection() to populate them.
 *
 * @param account  Backing settings; not owned and must outlive this view.
 */
FiltersConfigView::FiltersConfigView(BMailAccountSettings& account)
	:
	BGroupView(B_VERTICAL),
	fAccount(account),
	fDirection(kIncoming),
	fInboundFilters(kIncoming),
	fOutboundFilters(kOutgoing),
	fFilterView(NULL),
	fCurrentIndex(-1)
{
	BBox* box = new BBox("filters");
	AddChild(box);

	BView* contents = new BView(NULL, 0);
	box->AddChild(contents);

	BMessage* msg = new BMessage(kMsgChainSelected);
	msg->AddInt32("direction", kIncoming);
	BMenuItem* item = new BMenuItem(B_TRANSLATE("Incoming mail filters"), msg);
	item->SetMarked(true);
	BPopUpMenu* menu = new BPopUpMenu(B_EMPTY_STRING);
	menu->AddItem(item);

	msg = new BMessage(kMsgChainSelected);
	msg->AddInt32("direction", kOutgoing);
	item = new BMenuItem(B_TRANSLATE("Outgoing mail filters"), msg);
	menu->AddItem(item);

	fChainsField = new BMenuField(NULL, NULL, menu);
	fChainsField->ResizeToPreferred();
	box->SetLabel(fChainsField);

	fListView = new DragListView(NULL, B_SINGLE_SELECTION_LIST,
		new BMessage(kMsgFilterMoved));
	fListView->SetSelectionMessage(new BMessage(kMsgFilterSelected));

	menu = new BPopUpMenu(B_TRANSLATE("Add filter"));
	menu->SetRadioMode(false);

	fAddField = new BMenuField(NULL, NULL, menu);

	fRemoveButton = new BButton(NULL, B_TRANSLATE("Remove"),
		new BMessage(kMsgRemoveFilter));

	BLayoutBuilder::Group<>(contents, B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(new BScrollView(NULL, fListView, 0, false, true))
		.AddGroup(B_HORIZONTAL)
			.Add(fAddField)
			.Add(fRemoveButton)
			.AddGlue();

	_SetDirection(fDirection);
}


/**
 * @brief Removes the embedded filter view before unloading its add-on so
 *        destructors run while the code is still mapped.
 */
FiltersConfigView::~FiltersConfigView()
{
	// We need to remove the filter manually, as their add-on
	// is not available anymore in the parent destructor.
	if (fFilterView != NULL) {
		RemoveChild(fFilterView);
		delete fFilterView;
	}
}


/**
 * @brief Swaps the embedded filter settings view to match the row at
 *        @a index.
 *
 * Saves any pending changes from the previous filter, drops the old view,
 * loads the new add-on's settings view, and stashes the new index.
 *
 * @param index  Row in the listview; @c -1 means "show no filter".
 */
void
FiltersConfigView::_SelectFilter(int32 index)
{
	Hide();

	// remove old config view
	if (fFilterView != NULL) {
		RemoveChild(fFilterView);
		_SaveConfig(fCurrentIndex);
		delete fFilterView;
		fFilterView = NULL;
	}

	if (index >= 0) {
		// add new config view
		BMailAddOnSettings* filterSettings
			= _MailSettings()->FilterSettingsAt(index);
		if (filterSettings != NULL) {
			::FilterList* filters = _FilterList();
			BMailSettingsView* view = filters->CreateSettingsView(fAccount,
				*filterSettings);
			if (view != NULL) {
				fFilterView = new FilterSettingsView(
					filters->DescriptiveName(filterSettings->AddOnRef(),
						fAccount, NULL), view);
				AddChild(fFilterView);
			}
		}
	}

	fCurrentIndex = index;
	Show();
}


/**
 * @brief Switches the visible chain to @a direction and rebuilds the row
 *        list and Add menu accordingly.
 *
 * Filters whose add-ons have disappeared from disk are silently removed
 * from the account settings during this pass.
 *
 * @param direction  Either @c kIncoming or @c kOutgoing.
 */
void
FiltersConfigView::_SetDirection(direction direction)
{
	// remove the filter config view
	_SelectFilter(-1);

	for (int32 i = fListView->CountItems(); i-- > 0;) {
		BStringItem *item = (BStringItem *)fListView->RemoveItem(i);
		delete item;
	}

	fDirection = direction;
	BMailProtocolSettings* protocolSettings = _MailSettings();
	::FilterList* filters = _FilterList();
	filters->Reload();

	for (int32 i = 0; i < protocolSettings->CountFilterSettings(); i++) {
		BMailAddOnSettings* settings = protocolSettings->FilterSettingsAt(i);
		if (filters->InfoIndexFor(settings->AddOnRef()) < 0) {
			fprintf(stderr, "Removed missing filter: %s\n",
				settings->AddOnRef().name);
			protocolSettings->RemoveFilterSettings(i);
			i--;
			continue;
		}

		fListView->AddItem(new BStringItem(filters->DescriptiveName(
			settings->AddOnRef(), fAccount, settings)));
	}

	// remove old filter items
	BMenu* menu = fAddField->Menu();
	for (int32 i = menu->CountItems(); i-- > 0;) {
		BMenuItem *item = menu->RemoveItem(i);
		delete item;
	}

	for (int32 i = 0; i < filters->CountInfos(); i++) {
		const FilterInfo& info = filters->InfoAt(i);

		BMessage* msg = new BMessage(kMsgAddFilter);
		msg->AddRef("filter", &info.ref);
		BMenuItem* item = new BMenuItem(filters->SimpleName(i, fAccount), msg);
		menu->AddItem(item);
	}

	menu->SetTargetForItems(this);
}


/**
 * @brief Retargets every menu item, the listview, and the Remove button
 *        at this view once the BLooper is available.
 */
void
FiltersConfigView::AttachedToWindow()
{
	fChainsField->Menu()->SetTargetForItems(this);
	fListView->SetTarget(this);
	fAddField->Menu()->SetTargetForItems(this);
	fRemoveButton->SetTarget(this);
}


/**
 * @brief Persists the currently selected filter's settings before this
 *        view goes away.
 */
void
FiltersConfigView::DetachedFromWindow()
{
	_SaveConfig(fCurrentIndex);
}


/**
 * @brief Handles chain-switch, add, remove, selection, and reorder
 *        events.
 *
 * On a failed reorder (most likely a malformed move) the affected row is
 * removed from the listview to keep state consistent with the underlying
 * BMailProtocolSettings.
 *
 * @param msg  Incoming BMessage.
 */
void
FiltersConfigView::MessageReceived(BMessage *msg)
{
	switch (msg->what) {
		case kMsgChainSelected:
		{
			direction dir;
			if (msg->FindInt32("direction", (int32*)&dir) != B_OK)
				break;

			if (fDirection == dir)
				break;

			_SetDirection(dir);
			break;
		}
		case kMsgAddFilter:
		{
			entry_ref ref;
			if (msg->FindRef("filter", &ref) != B_OK)
				break;

			int32 index = _MailSettings()->AddFilterSettings(&ref);
			if (index < 0)
				break;

			fListView->AddItem(new BStringItem(_FilterList()->DescriptiveName(
				ref, fAccount, _MailSettings()->FilterSettingsAt(index))));
			break;
		}
		case kMsgRemoveFilter:
		{
			int32 index = fListView->CurrentSelection();
			if (index < 0)
				break;
			BStringItem* item = (BStringItem*)fListView->RemoveItem(index);
			delete item;

			_SelectFilter(-1);
			_MailSettings()->RemoveFilterSettings(index);
			break;
		}
		case kMsgFilterSelected:
		{
			int32 index = -1;
			if (msg->FindInt32("index",&index) != B_OK)
				break;

			_SelectFilter(index);
			break;
		}
		case kMsgFilterMoved:
		{
			int32 from = msg->FindInt32("from");
			int32 to = msg->FindInt32("to");
			if (from == to)
				break;

			if (!_MailSettings()->MoveFilterSettings(from, to)) {
				BAlert* alert = new BAlert("E-mail",
					B_TRANSLATE("The filter could not be moved. Deleting "
						"filter."), B_TRANSLATE("OK"));
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				alert->Go();
				fListView->RemoveItem(to);
				break;
			}

			break;
		}
		default:
			BView::MessageReceived(msg);
			break;
	}
}


/**
 * @brief Returns the protocol-settings half (inbound or outbound) of the
 *        backing account, depending on the current direction.
 *
 * @return Pointer into @c fAccount; never @c NULL.
 */
BMailProtocolSettings*
FiltersConfigView::_MailSettings()
{
	return fDirection == kIncoming
		? &fAccount.InboundSettings() : &fAccount.OutboundSettings();
}


/**
 * @brief Returns the filter add-on catalogue for the currently displayed
 *        chain.
 *
 * @return Pointer to @c fInboundFilters or @c fOutboundFilters; never
 *         @c NULL.
 */
FilterList*
FiltersConfigView::_FilterList()
{
	return fDirection == kIncoming ? &fInboundFilters : &fOutboundFilters;
}


/**
 * @brief If a filter is currently being shown, writes its settings back
 *        into the row at @a index.
 *
 * @param index  Row whose backing BMailAddOnSettings should be updated;
 *               negative values are silently ignored.
 */
void
FiltersConfigView::_SaveConfig(int32 index)
{
	if (fFilterView != NULL) {
		BMailAddOnSettings* settings = _MailSettings()->FilterSettingsAt(index);
		if (settings != NULL)
			fFilterView->SaveInto(*settings);
	}
}
