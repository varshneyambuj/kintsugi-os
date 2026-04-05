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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Assmus, superstippi@gmx.de
 *       Axel Dörfler, axeld@pinc-software.de
 *       Marc Flerackers, mflerackers@androme.be
 *       Rene Gollent, rene@gollent.com
 *       Ulrich Wimboeck
 */


/**
 * @file ListView.cpp
 * @brief Implementation of BListView, a scrollable list of selectable items
 *
 * BListView manages an ordered list of BListItem objects, handling item
 * selection (single and multiple), keyboard navigation, drag-and-drop
 * reordering, and scroll-to-selection. It sends selection-change notifications
 * to an optional target.
 *
 * @see BListItem, BOutlineListView, BScrollView
 */


#include <ListView.h>

#include <algorithm>

#include <stdio.h>

#include <Autolock.h>
#include <LayoutUtils.h>
#include <PropertyInfo.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <Window.h>

#include <binary_compatibility/Interface.h>


/**
 * @brief Mouse-tracking state captured at each MouseDown event.
 *
 * This struct records all information needed to detect double-clicks, decide
 * whether to initiate a drag operation, and update the selection correctly
 * across MouseDown / MouseMoved / MouseUp.
 */
struct track_data {
	/** @brief View-coordinate position where the mouse button was pressed. */
	BPoint		drag_start;
	/** @brief Index of the list item that was under the pointer on MouseDown, or -1. */
	int32		item_index;
	/** @brief Button mask from the current mouse-down event. */
	int32		buttons;
	/** @brief How many consecutive clicks landed on an already-selected item. */
	uint32		selected_click_count;
	/** @brief Whether the item at item_index was selected when the button was pressed. */
	bool		was_selected;
	/** @brief True while the pointer has moved enough to consider starting a drag. */
	bool		try_drag;
	/** @brief True once InitiateDrag() has accepted the drag. */
	bool		is_dragging;
	/** @brief Timestamp (system_time()) of the most recent mouse-down. */
	bigtime_t	last_click_time;
};


/** @brief Maximum pointer travel (in pixels) between two clicks that still counts as a double-click. */
const float kDoubleClickThreshold = 6.0f;


/**
 * @brief Scripting property table for BListView.
 *
 * Defines the "Item" and "Selection" properties exposed through the BeOS
 * scripting protocol, including count, execute, get, and set operations with
 * various specifier types (direct, index, reverse-index, range, reverse-range).
 *
 * @see BListView::ResolveSpecifier(), BListView::GetSupportedSuites(),
 *      BListView::MessageReceived()
 */
static property_info sProperties[] = {
	{ "Item", { B_COUNT_PROPERTIES, 0 }, { B_DIRECT_SPECIFIER, 0 },
		"Returns the number of BListItems currently in the list.", 0,
		{ B_INT32_TYPE }
	},

	{ "Item", { B_EXECUTE_PROPERTY, 0 }, { B_INDEX_SPECIFIER,
		B_REVERSE_INDEX_SPECIFIER, B_RANGE_SPECIFIER,
		B_REVERSE_RANGE_SPECIFIER, 0 },
		"Select and invoke the specified items, first removing any existing "
		"selection."
	},

	{ "Selection", { B_COUNT_PROPERTIES, 0 }, { B_DIRECT_SPECIFIER, 0 },
		"Returns int32 count of items in the selection.", 0, { B_INT32_TYPE }
	},

	{ "Selection", { B_EXECUTE_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 },
		"Invoke items in selection."
	},

	{ "Selection", { B_GET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 },
		"Returns int32 indices of all items in the selection.", 0,
		{ B_INT32_TYPE }
	},

	{ "Selection", { B_SET_PROPERTY, 0 }, { B_INDEX_SPECIFIER,
		B_REVERSE_INDEX_SPECIFIER, B_RANGE_SPECIFIER,
		B_REVERSE_RANGE_SPECIFIER, 0 },
		"Extends current selection or deselects specified items. Boolean field "
		"\"data\" chooses selection or deselection.", 0, { B_BOOL_TYPE }
	},

	{ "Selection", { B_SET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 },
		"Select or deselect all items in the selection. Boolean field \"data\" "
		"chooses selection or deselection.", 0, { B_BOOL_TYPE }
	},

	{ 0 }
};


/**
 * @brief Constructs a BListView with an explicit frame rectangle (legacy layout).
 *
 * This constructor is intended for use without the layout system. The view is
 * placed at @a frame within its parent and resizes according to @a resizingMode.
 * @c B_SCROLL_VIEW_AWARE is OR-ed into @a flags automatically so that a
 * containing BScrollView can notify this view of its presence.
 *
 * @param frame         The view's frame rectangle in the parent's coordinate system.
 * @param name          The view's name, used for scripting and debugging.
 * @param type          @c B_SINGLE_SELECTION_LIST or @c B_MULTIPLE_SELECTION_LIST.
 * @param resizingMode  Resizing mask (e.g. @c B_FOLLOW_ALL).
 * @param flags         View flags (e.g. @c B_WILL_DRAW | @c B_FRAME_EVENTS).
 *
 * @see BListView(const char*, list_view_type, uint32)
 */
BListView::BListView(BRect frame, const char* name, list_view_type type,
	uint32 resizingMode, uint32 flags)
	:
	BView(frame, name, resizingMode, flags | B_SCROLL_VIEW_AWARE)
{
	_InitObject(type);
}


/**
 * @brief Constructs a BListView for use with the layout system.
 *
 * The view has no fixed frame; its position and size are determined by the
 * layout engine. @c B_SCROLL_VIEW_AWARE is OR-ed into @a flags automatically.
 *
 * @param name  The view's name.
 * @param type  @c B_SINGLE_SELECTION_LIST or @c B_MULTIPLE_SELECTION_LIST.
 * @param flags View flags.
 *
 * @see BListView(BRect, const char*, list_view_type, uint32, uint32)
 */
BListView::BListView(const char* name, list_view_type type, uint32 flags)
	:
	BView(name, flags | B_SCROLL_VIEW_AWARE)
{
	_InitObject(type);
}


/**
 * @brief Constructs a BListView with default flags and no name.
 *
 * Convenience constructor that applies @c B_WILL_DRAW, @c B_FRAME_EVENTS,
 * @c B_NAVIGABLE, and @c B_SCROLL_VIEW_AWARE automatically. Intended for the
 * layout system.
 *
 * @param type  @c B_SINGLE_SELECTION_LIST or @c B_MULTIPLE_SELECTION_LIST.
 */
BListView::BListView(list_view_type type)
	:
	BView(NULL, B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE
		| B_SCROLL_VIEW_AWARE)
{
	_InitObject(type);
}


/**
 * @brief Constructs a BListView from an archived BMessage.
 *
 * Restores the list type from the "_lv_type" field, re-instantiates each
 * archived BListItem stored under "_l_items", and restores the invocation
 * message ("_msg") and selection message ("_2nd_msg") if present.
 *
 * @param archive The BMessage produced by a prior call to Archive().
 *
 * @see Archive(), Instantiate()
 */
BListView::BListView(BMessage* archive)
	:
	BView(archive)
{
	int32 listType;
	archive->FindInt32("_lv_type", &listType);
	_InitObject((list_view_type)listType);

	int32 i = 0;
	BMessage subData;
	while (archive->FindMessage("_l_items", i++, &subData) == B_OK) {
		BArchivable* object = instantiate_object(&subData);
		if (object == NULL)
			continue;

		BListItem* item = dynamic_cast<BListItem*>(object);
		if (item != NULL)
			AddItem(item);
	}

	if (archive->HasMessage("_msg")) {
		BMessage* invokationMessage = new BMessage;

		archive->FindMessage("_msg", invokationMessage);
		SetInvocationMessage(invokationMessage);
	}

	if (archive->HasMessage("_2nd_msg")) {
		BMessage* selectionMessage = new BMessage;

		archive->FindMessage("_2nd_msg", selectionMessage);
		SetSelectionMessage(selectionMessage);
	}
}


/**
 * @brief Destroys the BListView and releases internal resources.
 *
 * Frees the track_data struct and deletes the selection message. The list
 * items themselves are @b not deleted; the caller retains ownership of the
 * BListItem objects, as documented in the BeBook.
 *
 * @see SetSelectionMessage(), AddItem()
 */
BListView::~BListView()
{
	// NOTE: According to BeBook, BListView does not free the items itself.
	delete fTrack;
	SetSelectionMessage(NULL);
}


// #pragma mark -


/**
 * @brief Creates a new BListView from an archived BMessage.
 *
 * This is the standard BArchivable factory function called by
 * instantiate_object().  It validates that @a archive was produced by a
 * BListView and, if so, returns a heap-allocated instance.
 *
 * @param archive The BMessage produced by Archive().
 * @return A newly allocated BListView, or NULL if @a archive is invalid.
 *
 * @see Archive(), BListView(BMessage*)
 */
BArchivable*
BListView::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "BListView"))
		return new BListView(archive);

	return NULL;
}


/**
 * @brief Archives the BListView into a BMessage.
 *
 * Stores the list type under "_lv_type". When @a deep is true, each BListItem
 * is recursively archived and appended under "_l_items". The invocation message
 * is stored under "_msg" and the selection message under "_2nd_msg".
 *
 * @param data  The BMessage to fill with the archived data.
 * @param deep  If true, archive all contained BListItem objects as well.
 * @return A status code.
 * @retval B_OK On success.
 *
 * @see Instantiate(), BListView(BMessage*)
 */
status_t
BListView::Archive(BMessage* data, bool deep) const
{
	status_t status = BView::Archive(data, deep);
	if (status < B_OK)
		return status;

	status = data->AddInt32("_lv_type", fListType);
	if (status == B_OK && deep) {
		BListItem* item;
		int32 i = 0;

		while ((item = ItemAt(i++)) != NULL) {
			BMessage subData;
			status = item->Archive(&subData, true);
			if (status >= B_OK)
				status = data->AddMessage("_l_items", &subData);

			if (status < B_OK)
				break;
		}
	}

	if (status >= B_OK && InvocationMessage() != NULL)
		status = data->AddMessage("_msg", InvocationMessage());

	if (status == B_OK && fSelectMessage != NULL)
		status = data->AddMessage("_2nd_msg", fSelectMessage);

	return status;
}


// #pragma mark -


/**
 * @brief Draws all list items that intersect with the update rectangle.
 *
 * Iterates over every BListItem, computes its frame, and calls DrawItem() for
 * each item whose frame overlaps @a updateRect. Items are drawn top-to-bottom
 * using each item's Height().
 *
 * @param updateRect The portion of the view that needs to be redrawn.
 *
 * @see DrawItem(), InvalidateItem()
 */
void
BListView::Draw(BRect updateRect)
{
	int32 count = CountItems();
	if (count == 0)
		return;

	BRect itemFrame(0, 0, Bounds().right, -1);
	for (int i = 0; i < count; i++) {
		BListItem* item = ItemAt(i);
		itemFrame.bottom = itemFrame.top + ceilf(item->Height()) - 1;

		if (itemFrame.Intersects(updateRect))
			DrawItem(item, itemFrame);

		itemFrame.top = itemFrame.bottom + 1;
	}
}


/**
 * @brief Performs post-attachment initialization when the view is added to a window.
 *
 * Calls BView::AttachedToWindow(), then updates all items with the current
 * font metrics via _UpdateItems(). If no invocation target has been set, the
 * view's own window is made the default target. Finally, adjusts the scroll
 * bar range via _FixupScrollBar().
 *
 * @see DetachedFromWindow(), _UpdateItems(), _FixupScrollBar()
 */
void
BListView::AttachedToWindow()
{
	BView::AttachedToWindow();
	_UpdateItems();

	if (!Messenger().IsValid())
		SetTarget(Window(), NULL);

	_FixupScrollBar();
}


/**
 * @brief Called when the view is removed from its window.
 *
 * Delegates to BView::DetachedFromWindow(). Subclasses may override to release
 * window-specific resources.
 *
 * @see AttachedToWindow()
 */
void
BListView::DetachedFromWindow()
{
	BView::DetachedFromWindow();
}


/**
 * @brief Called after all views in the hierarchy have been attached to the window.
 *
 * Delegates to BView::AllAttached(). Subclasses may override to perform
 * initialization that depends on the entire view tree being in place.
 *
 * @see AllDetached(), AttachedToWindow()
 */
void
BListView::AllAttached()
{
	BView::AllAttached();
}


/**
 * @brief Called after all views in the hierarchy have been detached from the window.
 *
 * Delegates to BView::AllDetached(). Subclasses may override to clean up
 * resources that require the full view tree to still be present.
 *
 * @see AllAttached(), DetachedFromWindow()
 */
void
BListView::AllDetached()
{
	BView::AllDetached();
}


/**
 * @brief Responds to a change in the view's frame size.
 *
 * Adjusts the scroll bar range and proportion via _FixupScrollBar(), then
 * notifies all items of the new width so they can recompute their layout via
 * _UpdateItems().
 *
 * @param newWidth   The new width of the view's frame.
 * @param newHeight  The new height of the view's frame.
 *
 * @see _FixupScrollBar(), _UpdateItems()
 */
void
BListView::FrameResized(float newWidth, float newHeight)
{
	_FixupScrollBar();

	// notify items of new width.
	_UpdateItems();
}


/**
 * @brief Responds to the view's frame being moved.
 *
 * Delegates to BView::FrameMoved(). Subclasses may override to react to
 * position changes.
 *
 * @param newPosition The new top-left corner of the view in the parent's coordinates.
 */
void
BListView::FrameMoved(BPoint newPosition)
{
	BView::FrameMoved(newPosition);
}


/**
 * @brief Notifies the list view that it has been embedded in a BScrollView.
 *
 * Stores the pointer to the enclosing BScrollView so that MakeFocus() can
 * highlight the scroll view's border when the list gains or loses focus.
 *
 * @param view The BScrollView that now targets this list view, or NULL.
 *
 * @see MakeFocus(), _FixupScrollBar()
 */
void
BListView::TargetedByScrollView(BScrollView* view)
{
	fScrollView = view;
	// TODO: We could SetFlags(Flags() | B_FRAME_EVENTS) here, but that
	// may mess up application code which manages this by some other means
	// and doesn't want us to be messing with flags.
}


/**
 * @brief Called when the list view's window is activated or deactivated.
 *
 * Delegates to BView::WindowActivated(). Subclasses may override to change
 * visual appearance depending on the window's active state.
 *
 * @param active True if the window became active, false if it was deactivated.
 */
void
BListView::WindowActivated(bool active)
{
	BView::WindowActivated(active);
}


// #pragma mark -


/**
 * @brief Dispatches incoming messages, including scripting and built-in commands.
 *
 * Handles the BeOS scripting protocol for "Item" and "Selection" properties
 * (count, execute, get, set). Also responds to @c B_MOUSE_WHEEL_CHANGED
 * (suppressed during drags) and @c B_SELECT_ALL (selects all items in
 * multiple-selection mode). All other messages are forwarded to
 * BView::MessageReceived().
 *
 * @param message The message to process.
 *
 * @see ResolveSpecifier(), GetSupportedSuites(), Select(), Deselect()
 */
void
BListView::MessageReceived(BMessage* message)
{
	if (message->HasSpecifiers()) {
		BMessage reply(B_REPLY);
		status_t err = B_BAD_SCRIPT_SYNTAX;
		int32 index;
		BMessage specifier;
		int32 what;
		const char* property;

		if (message->GetCurrentSpecifier(&index, &specifier, &what, &property)
				!= B_OK) {
			return BView::MessageReceived(message);
		}

		BPropertyInfo propInfo(sProperties);
		switch (propInfo.FindMatch(message, index, &specifier, what,
			property)) {
			case 0: // Item: Count
				err = reply.AddInt32("result", CountItems());
				break;

			case 1: { // Item: EXECUTE
				switch (what) {
					case B_INDEX_SPECIFIER:
					case B_REVERSE_INDEX_SPECIFIER: {
						int32 index;
						err = specifier.FindInt32("index", &index);
						if (err >= B_OK) {
							if (what == B_REVERSE_INDEX_SPECIFIER)
								index = CountItems() - index;
							if (index < 0 || index >= CountItems())
								err = B_BAD_INDEX;
						}
						if (err >= B_OK) {
							Select(index, false);
							Invoke();
						}
						break;
					}
					case B_RANGE_SPECIFIER: {
					case B_REVERSE_RANGE_SPECIFIER:
						int32 beg, end, range;
						err = specifier.FindInt32("index", &beg);
						if (err >= B_OK)
							err = specifier.FindInt32("range", &range);
						if (err >= B_OK) {
							if (what == B_REVERSE_RANGE_SPECIFIER)
								beg = CountItems() - beg;
							end = beg + range;
							if (!(beg >= 0 && beg <= end && end < CountItems()))
								err = B_BAD_INDEX;
							if (err >= B_OK) {
								if (fListType != B_MULTIPLE_SELECTION_LIST
									&& end - beg > 1)
									err = B_BAD_VALUE;
								if (err >= B_OK) {
									Select(beg, end - 1, false);
									Invoke();
								}
							}
						}
						break;
					}
				}
				break;
			}
			case 2: { // Selection: COUNT
					int32 count = 0;

					for (int32 i = 0; i < CountItems(); i++) {
						if (ItemAt(i)->IsSelected())
							count++;
					}

				err = reply.AddInt32("result", count);
				break;
			}
			case 3: // Selection: EXECUTE
				err = Invoke();
				break;

			case 4: // Selection: GET
				err = B_OK;
				for (int32 i = 0; err >= B_OK && i < CountItems(); i++) {
					if (ItemAt(i)->IsSelected())
						err = reply.AddInt32("result", i);
				}
				break;

			case 5: { // Selection: SET
				bool doSelect;
				err = message->FindBool("data", &doSelect);
				if (err >= B_OK) {
					switch (what) {
						case B_INDEX_SPECIFIER:
						case B_REVERSE_INDEX_SPECIFIER: {
							int32 index;
							err = specifier.FindInt32("index", &index);
							if (err >= B_OK) {
								if (what == B_REVERSE_INDEX_SPECIFIER)
									index = CountItems() - index;
								if (index < 0 || index >= CountItems())
									err = B_BAD_INDEX;
							}
							if (err >= B_OK) {
								if (doSelect)
									Select(index,
										fListType == B_MULTIPLE_SELECTION_LIST);
								else
									Deselect(index);
							}
							break;
						}
						case B_RANGE_SPECIFIER: {
						case B_REVERSE_RANGE_SPECIFIER:
							int32 beg, end, range;
							err = specifier.FindInt32("index", &beg);
							if (err >= B_OK)
								err = specifier.FindInt32("range", &range);
							if (err >= B_OK) {
								if (what == B_REVERSE_RANGE_SPECIFIER)
									beg = CountItems() - beg;
								end = beg + range;
								if (!(beg >= 0 && beg <= end
									&& end < CountItems()))
									err = B_BAD_INDEX;
								if (err >= B_OK) {
									if (fListType != B_MULTIPLE_SELECTION_LIST
										&& end - beg > 1)
										err = B_BAD_VALUE;
									if (doSelect)
										Select(beg, end - 1, fListType
											== B_MULTIPLE_SELECTION_LIST);
									else {
										for (int32 i = beg; i < end; i++)
											Deselect(i);
									}
								}
							}
							break;
						}
					}
				}
				break;
			}
			case 6: // Selection: SET (select/deselect all)
				bool doSelect;
				err = message->FindBool("data", &doSelect);
				if (err >= B_OK) {
					if (doSelect)
						Select(0, CountItems() - 1, true);
					else
						DeselectAll();
				}
				break;

			default:
				return BView::MessageReceived(message);
		}

		if (err != B_OK) {
			reply.what = B_MESSAGE_NOT_UNDERSTOOD;
			reply.AddString("message", strerror(err));
		}

		reply.AddInt32("error", err);
		message->SendReply(&reply);
		return;
	}

	switch (message->what) {
		case B_MOUSE_WHEEL_CHANGED:
			if (!fTrack->is_dragging)
				BView::MessageReceived(message);
			break;

		case B_SELECT_ALL:
			if (fListType == B_MULTIPLE_SELECTION_LIST)
				Select(0, CountItems() - 1, false);
			break;

		default:
			BView::MessageReceived(message);
	}
}


/**
 * @brief Handles keyboard navigation and selection within the list.
 *
 * Interprets the following keys:
 * - @c B_UP_ARROW / @c B_DOWN_ARROW: Move the selection one enabled item
 *   up or down. With @c B_SHIFT_KEY held in a multiple-selection list, extends
 *   or contracts the selection.
 * - @c B_HOME / @c B_END: Jump to the first or last enabled item.
 * - @c B_PAGE_UP / @c B_PAGE_DOWN: Scroll by one visible page.
 * - @c B_RETURN / @c B_SPACE: Invoke the current selection.
 *
 * All navigation keys call ScrollToSelection() after updating the selection.
 * Unrecognized keys are forwarded to BView::KeyDown().
 *
 * @param bytes    Pointer to the raw key bytes.
 * @param numBytes Number of bytes in @a bytes.
 *
 * @see Select(), Deselect(), ScrollToSelection(), Invoke()
 */
void
BListView::KeyDown(const char* bytes, int32 numBytes)
{
	bool extend = fListType == B_MULTIPLE_SELECTION_LIST
		&& (modifiers() & B_SHIFT_KEY) != 0;

	if (fFirstSelected == -1
		&& (bytes[0] == B_UP_ARROW || bytes[0] == B_DOWN_ARROW)) {
		// nothing is selected yet, select the first enabled item
		int32 lastItem = CountItems() - 1;
		for (int32 i = 0; i <= lastItem; i++) {
			if (ItemAt(i)->IsEnabled()) {
				Select(i);
				break;
			}
		}
		return;
	}

	switch (bytes[0]) {
		case B_UP_ARROW:
		{
			if (fAnchorIndex > 0) {
				if (!extend || fAnchorIndex <= fFirstSelected) {
					for (int32 i = 1; fAnchorIndex - i >= 0; i++) {
						if (ItemAt(fAnchorIndex - i)->IsEnabled()) {
							// Select the previous enabled item
							Select(fAnchorIndex - i, extend);
							break;
						}
					}
				} else {
					Deselect(fAnchorIndex);
					do
						fAnchorIndex--;
					while (fAnchorIndex > 0
						&& !ItemAt(fAnchorIndex)->IsEnabled());
				}
			}

			ScrollToSelection();
			break;
		}

		case B_DOWN_ARROW:
		{
			int32 lastItem = CountItems() - 1;
			if (fAnchorIndex < lastItem) {
				if (!extend || fAnchorIndex >= fLastSelected) {
					for (int32 i = 1; fAnchorIndex + i <= lastItem; i++) {
						if (ItemAt(fAnchorIndex + i)->IsEnabled()) {
							// Select the next enabled item
							Select(fAnchorIndex + i, extend);
							break;
						}
					}
				} else {
					Deselect(fAnchorIndex);
					do
						fAnchorIndex++;
					while (fAnchorIndex < lastItem
						&& !ItemAt(fAnchorIndex)->IsEnabled());
				}
			}

			ScrollToSelection();
			break;
		}

		case B_HOME:
			if (extend) {
				Select(0, fAnchorIndex, true);
				fAnchorIndex = 0;
			} else {
				// select the first enabled item
				int32 lastItem = CountItems() - 1;
				for (int32 i = 0; i <= lastItem; i++) {
					if (ItemAt(i)->IsEnabled()) {
						Select(i, false);
						break;
					}
				}
			}

			ScrollToSelection();
			break;

		case B_END:
			if (extend) {
				Select(fAnchorIndex, CountItems() - 1, true);
				fAnchorIndex = CountItems() - 1;
			} else {
				// select the last enabled item
				for (int32 i = CountItems() - 1; i >= 0; i--) {
					if (ItemAt(i)->IsEnabled()) {
						Select(i, false);
						break;
					}
				}
			}

			ScrollToSelection();
			break;

		case B_PAGE_UP:
		{
			BPoint scrollOffset(LeftTop());
			scrollOffset.y = std::max(0.0f, scrollOffset.y - Bounds().Height());
			ScrollTo(scrollOffset);
			break;
		}

		case B_PAGE_DOWN:
		{
			BPoint scrollOffset(LeftTop());
			if (BListItem* item = LastItem()) {
				scrollOffset.y += Bounds().Height();
				scrollOffset.y = std::min(item->Bottom() - Bounds().Height(),
					scrollOffset.y);
			}
			ScrollTo(scrollOffset);
			break;
		}

		case B_RETURN:
		case B_SPACE:
			Invoke();
			break;

		default:
			BView::KeyDown(bytes, numBytes);
	}
}


/**
 * @brief Handles a mouse-button press over the list view.
 *
 * Acquires focus if not already focused. Detects double-clicks by comparing
 * the current click location and time against the values stored in fTrack; a
 * double-click on a selected item invokes the list without changing the
 * selection. Single-clicks update fTrack state, enable the drag-detection
 * window (@c B_POINTER_EVENTS), and call _DoSelection() to update the
 * selection according to the current modifier keys and list type.
 *
 * @param where The pointer position in the view's coordinate system.
 *
 * @see MouseUp(), MouseMoved(), InitiateDrag(), _DoSelection()
 */
void
BListView::MouseDown(BPoint where)
{
	if (!IsFocus()) {
		MakeFocus();
		Sync();
		Window()->UpdateIfNeeded();
	}

	int32 buttons = 0;
	if (Window() != NULL) {
		BMessage* currentMessage = Window()->CurrentMessage();
		if (currentMessage != NULL)
			currentMessage->FindInt32("buttons", &buttons);
	}

	int32 index = IndexOf(where);

	// If the user double (or more) clicked within the current selection,
	// we don't change the selection but invoke the selection.
	// TODO: move this code someplace where it can be shared everywhere
	// instead of every class having to reimplement it, once some sane
	// API for it is decided.
	BPoint delta = where - fTrack->drag_start;
	bigtime_t sysTime;
	Window()->CurrentMessage()->FindInt64("when", &sysTime);
	bigtime_t timeDelta = sysTime - fTrack->last_click_time;
	bigtime_t doubleClickSpeed;
	get_click_speed(&doubleClickSpeed);
	bool doubleClick = false;

	if (timeDelta < doubleClickSpeed
		&& fabs(delta.x) < kDoubleClickThreshold
		&& fabs(delta.y) < kDoubleClickThreshold
		&& fTrack->item_index == index) {
		doubleClick = true;
	}

	if (doubleClick && index >= fFirstSelected && index <= fLastSelected) {
		fTrack->drag_start.Set(INT32_MAX, INT32_MAX);
		Invoke();
		return BView::MouseDown(where);
	}

	if (!doubleClick) {
		fTrack->drag_start = where;
		fTrack->last_click_time = system_time();
		fTrack->item_index = index;
		fTrack->was_selected = index >= 0 ? ItemAt(index)->IsSelected() : false;
		fTrack->try_drag = true;

		SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
	}

	// increment/reset selected click count
	fTrack->buttons = buttons;
	if (fTrack->buttons > 0 && fTrack->was_selected)
		fTrack->selected_click_count++;
	else
		fTrack->selected_click_count = 0;

	_DoSelection(index);

	BView::MouseDown(where);
}


/**
 * @brief Handles a mouse-button release over the list view.
 *
 * Clears drag-tracking state. For single-selection lists that were not
 * dragging, finalizes the selection: if the pointer moved to a different item
 * since MouseDown(), the "faked" in-transit selection is replaced with a real
 * selection on the item under the released pointer. Multiple-selection lists
 * and completed drag operations do not alter the selection on release.
 *
 * @param where The pointer position in the view's coordinate system.
 *
 * @see MouseDown(), MouseMoved(), _DoSelection()
 */
void
BListView::MouseUp(BPoint where)
{
	bool wasDragging = fTrack->is_dragging;

	// drag is over
	fTrack->buttons = 0;
	fTrack->try_drag = false;
	fTrack->is_dragging = false;

	// selection updating on drag is for single selection lists only
	// do not alter selection on drag and drop end
	if (fListType == B_MULTIPLE_SELECTION_LIST || wasDragging)
		return BView::MouseUp(where);

	int32 index = IndexOf(where);

	// bail out if selection hasn't changed
	if (index == fTrack->item_index)
		return BView::MouseUp(where);

	// if mouse up selection is invalid reselect mouse down selection
	if (index == -1)
		index = fTrack->item_index;

	// bail out if mouse down selection invalid
	if (index == -1)
		return BView::MouseUp(where);

	// undo fake selection and select item
	BListItem* item = ItemAt(index);
	if (item != NULL) {
		item->Deselect();
		_DoSelection(index);
	}

	BView::MouseUp(where);
}


/**
 * @brief Handles pointer movement while a mouse button is pressed.
 *
 * If the pointer has traveled at least 5 pixels from the initial click
 * position and drag detection is armed (fTrack->try_drag), calls
 * InitiateDrag(). While a button is held, scrolls the list to keep the item
 * under the pointer visible. For single-selection lists that are not dragging,
 * also "fakes" a transient selection on the item under the pointer, which is
 * committed or reverted in MouseUp().
 *
 * @param where        The current pointer position in the view's coordinates.
 * @param code         Transit code (@c B_ENTERED_VIEW, @c B_INSIDE_VIEW, etc.).
 * @param dragMessage  The drag message if a drag is in progress, or NULL.
 *
 * @see MouseDown(), MouseUp(), InitiateDrag(), ScrollTo(int32)
 */
void
BListView::MouseMoved(BPoint where, uint32 code, const BMessage* dragMessage)
{
	if (fTrack->item_index >= 0 && fTrack->try_drag) {
		// initiate a drag if the mouse was moved far enough
		BPoint offset = where - fTrack->drag_start;
		float dragDistance = sqrtf(offset.x * offset.x + offset.y * offset.y);
		if (dragDistance >= 5.0f) {
			fTrack->try_drag = false;
			fTrack->is_dragging = InitiateDrag(fTrack->drag_start,
				fTrack->item_index, fTrack->was_selected);
		}
	}

	int32 index = IndexOf(where);
	if (index == -1) {
		// If where is above top, scroll to the first item,
		// else if where is below bottom scroll to the last item.
		if (where.y < Bounds().top)
			index = 0;
		else if (where.y > Bounds().bottom)
			index = CountItems() - 1;
	}

	// don't scroll if button not pressed or index is invalid
	int32 lastIndex = fFirstSelected;
	if (fTrack->buttons == 0 || index == -1)
		return BView::MouseMoved(where, code, dragMessage);

	// don't scroll if mouse is left or right of the view
	if (where.x < Bounds().left || where.x > Bounds().right)
		return BView::MouseMoved(where, code, dragMessage);

	// scroll to item under mouse while button is pressed
	ScrollTo(index);

	if (!fTrack->is_dragging && fListType != B_MULTIPLE_SELECTION_LIST
		&& lastIndex != -1 && index != lastIndex) {
		// mouse moved over unselected item, fake selection until mouse up
		BListItem* last = ItemAt(lastIndex);
		BListItem* item = ItemAt(index);
		if (last != NULL && item != NULL) {
			last->Deselect();
			item->Select();

			// update selection index
			fFirstSelected = fLastSelected = index;

			// redraw items whose selection has changed
			Invalidate(ItemFrame(lastIndex) | ItemFrame(index));
		}
	} else
		Invalidate();

	BView::MouseMoved(where, code, dragMessage);
}


/**
 * @brief Hook called when the user begins a drag gesture on a list item.
 *
 * Subclasses override this method to initiate a BView drag-and-drop operation
 * by calling DragMessage(). The base-class implementation always returns
 * false, meaning no drag is started.
 *
 * @param where        The pointer position (in the view's coordinates) where
 *                     the drag gesture started.
 * @param index        The index of the item under the pointer.
 * @param wasSelected  True if the item was already selected when the button
 *                     was pressed.
 * @return True if the drag was accepted and started, false otherwise.
 *
 * @see MouseMoved(), MouseDown()
 */
bool
BListView::InitiateDrag(BPoint where, int32 index, bool wasSelected)
{
	return false;
}


// #pragma mark -


/**
 * @brief Resizes the view to its preferred size.
 *
 * Delegates to BView::ResizeToPreferred(), which calls GetPreferredSize()
 * and resizes the view accordingly.
 *
 * @see GetPreferredSize(), PreferredSize()
 */
void
BListView::ResizeToPreferred()
{
	BView::ResizeToPreferred();
}


/**
 * @brief Returns the preferred width and height of the list view.
 *
 * The preferred width is the maximum Width() of all contained items. The
 * preferred height is the Bottom() of the last item. When the list is empty,
 * BView::GetPreferredSize() is used as a fallback.
 *
 * @param[out] _width   Set to the preferred width, or unchanged if NULL.
 * @param[out] _height  Set to the preferred height, or unchanged if NULL.
 *
 * @see MinSize(), MaxSize(), PreferredSize()
 */
void
BListView::GetPreferredSize(float *_width, float *_height)
{
	int32 count = CountItems();

	if (count > 0) {
		float maxWidth = 0.0;
		for (int32 i = 0; i < count; i++) {
			float itemWidth = ItemAt(i)->Width();
			if (itemWidth > maxWidth)
				maxWidth = itemWidth;
		}

		if (_width != NULL)
			*_width = maxWidth;
		if (_height != NULL)
			*_height = ItemAt(count - 1)->Bottom();
	} else
		BView::GetPreferredSize(_width, _height);
}


/**
 * @brief Returns the minimum size of the list view for the layout system.
 *
 * Returns the larger of any explicitly set minimum size and a hard-coded
 * floor of 10 x 10 pixels, ensuring the layout engine always has a stable,
 * non-zero minimum.
 *
 * @return The minimum BSize.
 *
 * @see MaxSize(), PreferredSize(), GetPreferredSize()
 */
BSize
BListView::MinSize()
{
	// We need a stable min size: the BView implementation uses
	// GetPreferredSize(), which by default just returns the current size.
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), BSize(10, 10));
}


/**
 * @brief Returns the maximum size of the list view for the layout system.
 *
 * Delegates to BView::MaxSize(), which returns the explicitly set maximum or
 * the default unconstrained size.
 *
 * @return The maximum BSize.
 *
 * @see MinSize(), PreferredSize()
 */
BSize
BListView::MaxSize()
{
	return BView::MaxSize();
}


/**
 * @brief Returns the preferred size of the list view for the layout system.
 *
 * Returns the larger of any explicitly set preferred size and a hard-coded
 * default of 100 x 50 pixels, ensuring the layout engine always has a stable,
 * non-zero preferred size.
 *
 * @return The preferred BSize.
 *
 * @see MinSize(), MaxSize(), GetPreferredSize()
 */
BSize
BListView::PreferredSize()
{
	// We need a stable preferred size: the BView implementation uses
	// GetPreferredSize(), which by default just returns the current size.
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), BSize(100, 50));
}


// #pragma mark -


/**
 * @brief Sets or clears the keyboard focus for this list view.
 *
 * Calls BView::MakeFocus() to update the standard focus state. If the list
 * is embedded in a BScrollView, also highlights or un-highlights the scroll
 * view's border to provide a visual focus indicator.
 *
 * @param focused True to give focus to this view, false to remove it.
 *
 * @see TargetedByScrollView(), KeyDown()
 */
void
BListView::MakeFocus(bool focused)
{
	if (IsFocus() == focused)
		return;

	BView::MakeFocus(focused);

	if (fScrollView)
		fScrollView->SetBorderHighlighted(focused);
}


/**
 * @brief Sets the font used for drawing list items.
 *
 * Calls BView::SetFont() to update the view's font, then calls _UpdateItems()
 * so that all BListItem objects recompute their preferred height using the new
 * font metrics. The update is skipped if the window is currently in a view
 * transaction.
 *
 * @param font The new font to apply.
 * @param mask A bitmask of font attributes to change (see BFont).
 *
 * @see _UpdateItems(), BListItem::Update()
 */
void
BListView::SetFont(const BFont* font, uint32 mask)
{
	BView::SetFont(font, mask);

	if (Window() != NULL && !Window()->InViewTransaction())
		_UpdateItems();
}


/**
 * @brief Scrolls the list view so that @a point is at the top-left of the visible area.
 *
 * Delegates to BView::ScrollTo(). This overload is provided so subclasses can
 * intercept scroll-to-point operations.
 *
 * @param point The target scroll position in the view's own coordinate system.
 *
 * @see ScrollTo(int32), ScrollToSelection()
 */
void
BListView::ScrollTo(BPoint point)
{
	BView::ScrollTo(point);
}


// #pragma mark - List ops


/**
 * @brief Inserts a BListItem at the specified index.
 *
 * Inserts @a item at position @a index, shifting subsequent items down.
 * Selection and anchor indices are adjusted to remain consistent. When
 * attached to a window, the item's top position and metrics are computed
 * immediately, the scroll bar is updated, and the affected region is
 * invalidated.
 *
 * @param item   The item to insert. Ownership is not transferred.
 * @param index  The zero-based position at which to insert the item.
 * @return True on success, false if @a index is out of range or memory
 *         allocation fails.
 *
 * @see AddItem(BListItem*), RemoveItem(int32), AddList()
 */
bool
BListView::AddItem(BListItem* item, int32 index)
{
	if (!fList.AddItem(item, index))
		return false;

	if (fFirstSelected != -1 && index <= fFirstSelected)
		fFirstSelected++;

	if (fLastSelected != -1 && index <= fLastSelected)
		fLastSelected++;

	if (fAnchorIndex != -1 && index <= fAnchorIndex)
		fAnchorIndex++;

	if (Window()) {
		BFont font;
		GetFont(&font);
		item->SetTop((index > 0) ? ItemAt(index - 1)->Bottom() + 1.0 : 0.0);

		item->Update(this, &font);
		_RecalcItemTops(index + 1);

		_FixupScrollBar();
		_InvalidateFrom(index);
	}

	return true;
}


/**
 * @brief Appends a BListItem to the end of the list.
 *
 * Adds @a item as the last element. No selection or anchor adjustments are
 * needed since no existing items are shifted. When attached to a window, the
 * item's top position and metrics are computed, the scroll bar is updated, and
 * only the new item's cell is invalidated.
 *
 * @param item The item to append. Ownership is not transferred.
 * @return True on success, false if memory allocation fails.
 *
 * @see AddItem(BListItem*, int32), RemoveItem(int32)
 */
bool
BListView::AddItem(BListItem* item)
{
	if (!fList.AddItem(item))
		return false;

	// No need to adapt selection, as this item is the last in the list

	if (Window()) {
		BFont font;
		GetFont(&font);
		int32 index = CountItems() - 1;
		item->SetTop((index > 0) ? ItemAt(index - 1)->Bottom() + 1.0 : 0.0);

		item->Update(this, &font);

		_FixupScrollBar();
		InvalidateItem(CountItems() - 1);
	}

	return true;
}


/**
 * @brief Inserts all items from @a list starting at @a index.
 *
 * Inserts every item in @a list into this BListView beginning at @a index,
 * shifting subsequent items. Selection and anchor indices are adjusted.
 * When attached to a window, metrics are computed for the new items and the
 * entire view is invalidated.
 *
 * @param list   A BList whose items are inserted; ownership of the items is
 *               not transferred.
 * @param index  The zero-based position at which to begin inserting.
 * @return True on success, false on failure.
 *
 * @see AddList(BList*), AddItem(BListItem*, int32)
 */
bool
BListView::AddList(BList* list, int32 index)
{
	if (!fList.AddList(list, index))
		return false;

	int32 count = list->CountItems();

	if (fFirstSelected != -1 && index < fFirstSelected)
		fFirstSelected += count;

	if (fLastSelected != -1 && index < fLastSelected)
		fLastSelected += count;

	if (fAnchorIndex != -1 && index < fAnchorIndex)
		fAnchorIndex += count;

	if (Window()) {
		BFont font;
		GetFont(&font);

		for (int32 i = index; i <= (index + count - 1); i++) {
			ItemAt(i)->SetTop((i > 0) ? ItemAt(i - 1)->Bottom() + 1.0 : 0.0);
			ItemAt(i)->Update(this, &font);
		}

		_RecalcItemTops(index + count - 1);

		_FixupScrollBar();
		Invalidate(); // TODO
	}

	return true;
}


/**
 * @brief Appends all items from @a list to the end of this list view.
 *
 * Convenience overload that calls AddList(@a list, CountItems()).
 *
 * @param list A BList whose items are appended; ownership is not transferred.
 * @return True on success, false on failure.
 *
 * @see AddList(BList*, int32)
 */
bool
BListView::AddList(BList* list)
{
	return AddList(list, CountItems());
}


/**
 * @brief Removes the item at @a index and returns it.
 *
 * Deselects the item if it is selected, removes it from the internal list,
 * adjusts the selection and anchor indices, recomputes item top positions from
 * @a index onward, and invalidates the affected region. The caller becomes
 * responsible for the returned item.
 *
 * @param index The zero-based index of the item to remove.
 * @return The removed BListItem, or NULL if @a index is out of range.
 *
 * @see RemoveItem(BListItem*), RemoveItems(), AddItem(BListItem*, int32)
 */
BListItem*
BListView::RemoveItem(int32 index)
{
	BListItem* item = ItemAt(index);
	if (item == NULL)
		return NULL;

	if (item->IsSelected())
		Deselect(index);

	if (!fList.RemoveItem(item))
		return NULL;

	if (fFirstSelected != -1 && index < fFirstSelected)
		fFirstSelected--;

	if (fLastSelected != -1 && index < fLastSelected)
		fLastSelected--;

	if (fAnchorIndex != -1 && index < fAnchorIndex)
		fAnchorIndex--;

	_RecalcItemTops(index);

	_InvalidateFrom(index);
	_FixupScrollBar();

	return item;
}


/**
 * @brief Removes a specific BListItem by pointer.
 *
 * Looks up @a item via IndexOf() and delegates to RemoveItem(int32).
 *
 * @param item The item to remove.
 * @return True if the item was found and removed, false otherwise.
 *
 * @see RemoveItem(int32), IndexOf(BListItem*)
 */
bool
BListView::RemoveItem(BListItem* item)
{
	return BListView::RemoveItem(IndexOf(item)) != NULL;
}


/**
 * @brief Removes @a count items starting at @a index.
 *
 * Adjusts the anchor index to @a index if the anchor fell within the removed
 * range, removes the items from the underlying BList, recomputes item tops for
 * the remainder, and invalidates the entire view. The removed items are not
 * deleted; the caller retains ownership.
 *
 * @param index The zero-based starting index of the items to remove.
 * @param count The number of items to remove.
 * @return True on success, false if @a index is out of range.
 *
 * @see RemoveItem(int32), MakeEmpty()
 */
bool
BListView::RemoveItems(int32 index, int32 count)
{
	if (index >= fList.CountItems())
		index = -1;

	if (index < 0)
		return false;

	if (fAnchorIndex != -1 && index < fAnchorIndex)
		fAnchorIndex = index;

	fList.RemoveItems(index, count);
	if (index < fList.CountItems())
		_RecalcItemTops(index);

	Invalidate();
	return true;
}


/**
 * @brief Sets the message sent to the target whenever the selection changes.
 *
 * Replaces the current selection message with @a message (which may be NULL
 * to disable selection notifications). The old message is deleted.
 *
 * @param message The new selection-change notification message, or NULL.
 *
 * @see SelectionMessage(), SelectionCommand(), SetInvocationMessage()
 */
void
BListView::SetSelectionMessage(BMessage* message)
{
	delete fSelectMessage;
	fSelectMessage = message;
}


/**
 * @brief Sets the message sent to the target when an item is invoked.
 *
 * Stores @a message as the invocation message via BInvoker::SetMessage().
 * The invocation message is sent when the user double-clicks an item or
 * presses Return/Space.
 *
 * @param message The new invocation message, or NULL to disable invocation.
 *
 * @see InvocationMessage(), InvocationCommand(), SetSelectionMessage()
 */
void
BListView::SetInvocationMessage(BMessage* message)
{
	BInvoker::SetMessage(message);
}


/**
 * @brief Returns the current invocation message.
 *
 * @return The BMessage sent on invocation, or NULL if none is set.
 *
 * @see SetInvocationMessage(), InvocationCommand()
 */
BMessage*
BListView::InvocationMessage() const
{
	return BInvoker::Message();
}


/**
 * @brief Returns the @c what field of the current invocation message.
 *
 * @return The command code of the invocation message, or 0 if none is set.
 *
 * @see InvocationMessage(), SetInvocationMessage()
 */
uint32
BListView::InvocationCommand() const
{
	return BInvoker::Command();
}


/**
 * @brief Returns the current selection-change notification message.
 *
 * @return The BMessage sent when the selection changes, or NULL if none is set.
 *
 * @see SetSelectionMessage(), SelectionCommand()
 */
BMessage*
BListView::SelectionMessage() const
{
	return fSelectMessage;
}


/**
 * @brief Returns the @c what field of the current selection-change message.
 *
 * @return The command code of the selection message, or 0 if none is set.
 *
 * @see SelectionMessage(), SetSelectionMessage()
 */
uint32
BListView::SelectionCommand() const
{
	if (fSelectMessage)
		return fSelectMessage->what;

	return 0;
}


/**
 * @brief Changes the selection mode of the list.
 *
 * Switches between @c B_SINGLE_SELECTION_LIST and @c B_MULTIPLE_SELECTION_LIST.
 * When switching from multiple to single selection, the current first selected
 * item is preserved and all others are deselected.
 *
 * @param type The new list type.
 *
 * @see ListType(), Select()
 */
void
BListView::SetListType(list_view_type type)
{
	if (fListType == B_MULTIPLE_SELECTION_LIST
		&& type == B_SINGLE_SELECTION_LIST) {
		Select(CurrentSelection(0));
	}

	fListType = type;
}


/**
 * @brief Returns the current selection mode.
 *
 * @return @c B_SINGLE_SELECTION_LIST or @c B_MULTIPLE_SELECTION_LIST.
 *
 * @see SetListType()
 */
list_view_type
BListView::ListType() const
{
	return fListType;
}


/**
 * @brief Returns the item at the given index.
 *
 * @param index The zero-based index of the item to retrieve.
 * @return The BListItem at @a index, or NULL if @a index is out of range.
 *
 * @see FirstItem(), LastItem(), CountItems()
 */
BListItem*
BListView::ItemAt(int32 index) const
{
	return (BListItem*)fList.ItemAt(index);
}


/**
 * @brief Returns the index of the given BListItem pointer.
 *
 * When attached to a window, uses a binary search on the item's top position
 * for efficiency, then validates the result. Falls back to a linear search
 * on the underlying BList when there is no window.
 *
 * @param item The item to locate.
 * @return The zero-based index of @a item, or -1 if not found.
 *
 * @see IndexOf(BPoint), HasItem()
 */
int32
BListView::IndexOf(BListItem* item) const
{
	if (Window()) {
		if (item != NULL) {
			int32 index = IndexOf(BPoint(0.0, item->Top()));
			if (index >= 0 && fList.ItemAt(index) == item)
				return index;

			return -1;
		}
	}
	return fList.IndexOf(item);
}


/**
 * @brief Returns the index of the item that contains the given point.
 *
 * Uses a binary search on item top/bottom positions, so the time complexity
 * is O(log n).
 *
 * @param point A point in the view's coordinate system.
 * @return The zero-based index of the item that contains @a point, or -1 if
 *         @a point is not within any item.
 *
 * @see IndexOf(BListItem*), ItemFrame()
 */
int32
BListView::IndexOf(BPoint point) const
{
	int32 low = 0;
	int32 high = fList.CountItems() - 1;
	int32 mid = -1;
	float frameTop = -1.0;
	float frameBottom = 1.0;

	// binary search the list
	while (high >= low) {
		mid = (low + high) / 2;
		frameTop = ItemAt(mid)->Top();
		frameBottom = ItemAt(mid)->Bottom();
		if (point.y < frameTop)
			high = mid - 1;
		else if (point.y > frameBottom)
			low = mid + 1;
		else
			return mid;
	}

	return -1;
}


/**
 * @brief Returns the first item in the list.
 *
 * @return The first BListItem, or NULL if the list is empty.
 *
 * @see LastItem(), ItemAt(), CountItems()
 */
BListItem*
BListView::FirstItem() const
{
	return (BListItem*)fList.FirstItem();
}


/**
 * @brief Returns the last item in the list.
 *
 * @return The last BListItem, or NULL if the list is empty.
 *
 * @see FirstItem(), ItemAt(), CountItems()
 */
BListItem*
BListView::LastItem() const
{
	return (BListItem*)fList.LastItem();
}


/**
 * @brief Returns whether the list contains a specific item.
 *
 * @param item The item to search for.
 * @return True if @a item is present in the list, false otherwise.
 *
 * @see IndexOf(BListItem*)
 */
bool
BListView::HasItem(BListItem *item) const
{
	return IndexOf(item) != -1;
}


/**
 * @brief Returns the total number of items in the list.
 *
 * @return The number of BListItem objects currently managed by this view.
 *
 * @see IsEmpty(), ItemAt()
 */
int32
BListView::CountItems() const
{
	return fList.CountItems();
}


/**
 * @brief Removes all items from the list without deleting them.
 *
 * Deselects all items, clears the internal list, and when attached to a
 * window resets the scroll bar and invalidates the view. Item objects
 * are not freed; the caller retains ownership.
 *
 * @see RemoveItems(), IsEmpty()
 */
void
BListView::MakeEmpty()
{
	if (fList.IsEmpty())
		return;

	_DeselectAll(-1, -1);
	fList.MakeEmpty();

	if (Window()) {
		_FixupScrollBar();
		Invalidate();
	}
}


/**
 * @brief Returns whether the list contains no items.
 *
 * @return True if CountItems() == 0, false otherwise.
 *
 * @see CountItems(), MakeEmpty()
 */
bool
BListView::IsEmpty() const
{
	return fList.IsEmpty();
}


/**
 * @brief Calls @a func for each item in the list until it returns true.
 *
 * Iteration stops early if @a func returns true for any item.
 *
 * @param func A function that receives a BListItem pointer and returns a bool.
 *             Return true to stop iteration, false to continue.
 *
 * @see DoForEach(bool (*)(BListItem*, void*), void*)
 */
void
BListView::DoForEach(bool (*func)(BListItem*))
{
	fList.DoForEach(reinterpret_cast<bool (*)(void*)>(func));
}


/**
 * @brief Calls @a func for each item in the list, passing an extra argument.
 *
 * Iteration stops early if @a func returns true for any item.
 *
 * @param func A function that receives a BListItem pointer and @a arg, and
 *             returns a bool. Return true to stop iteration, false to continue.
 * @param arg  An arbitrary pointer passed as the second argument to @a func.
 *
 * @see DoForEach(bool (*)(BListItem*))
 */
void
BListView::DoForEach(bool (*func)(BListItem*, void*), void* arg)
{
	fList.DoForEach(reinterpret_cast<bool (*)(void*, void*)>(func), arg);
}


/**
 * @brief Returns a direct pointer to the internal item array.
 *
 * The returned pointer is valid only as long as no items are added or removed.
 * It may be used to iterate the list rapidly without repeated ItemAt() calls.
 *
 * @return A pointer to the first element of the internal BListItem* array,
 *         or NULL if the list is empty.
 *
 * @see ItemAt(), CountItems()
 */
const BListItem**
BListView::Items() const
{
	return (const BListItem**)fList.Items();
}


/**
 * @brief Marks the bounding rectangle of the item at @a index as needing redraw.
 *
 * Convenience wrapper around Invalidate(ItemFrame(@a index)).
 *
 * @param index The zero-based index of the item to invalidate.
 *
 * @see ItemFrame(), Draw()
 */
void
BListView::InvalidateItem(int32 index)
{
	Invalidate(ItemFrame(index));
}


/**
 * @brief Scrolls the view to make the item at @a index fully visible.
 *
 * If the item's top edge is above the current scroll position, scrolls up so
 * the item appears at the top. If the item's bottom edge is below the visible
 * area, scrolls down so the item appears at the bottom. Out-of-range indices
 * are clamped to [0, CountItems()-1].
 *
 * @param index The zero-based index of the item to scroll to.
 *
 * @see ScrollTo(BPoint), ScrollToSelection(), ItemFrame()
 */
void
BListView::ScrollTo(int32 index)
{
	if (index < 0)
		index = 0;
	if (index > CountItems() - 1)
		index = CountItems() - 1;

	BRect itemFrame = ItemFrame(index);
	BRect bounds = Bounds();
	if (itemFrame.top < bounds.top)
		BListView::ScrollTo(itemFrame.LeftTop());
	else if (itemFrame.bottom > bounds.bottom)
		BListView::ScrollTo(BPoint(0, itemFrame.bottom - bounds.Height()));
}


/**
 * @brief Scrolls the view to make the first selected item visible.
 *
 * If the selected item's top is above the visible area, or the item is taller
 * than the visible area, scrolls up to align the item's top with the view's
 * top. If the item's bottom is below the visible area, scrolls down to bring
 * the bottom into view. Does nothing if there is no selection.
 *
 * @see CurrentSelection(), ScrollTo(int32)
 */
void
BListView::ScrollToSelection()
{
	BRect itemFrame = ItemFrame(CurrentSelection(0));

	if (itemFrame.top < Bounds().top
		|| itemFrame.Height() > Bounds().Height())
		ScrollBy(0, itemFrame.top - Bounds().top);
	else if (itemFrame.bottom > Bounds().bottom)
		ScrollBy(0, itemFrame.bottom - Bounds().bottom);
}


/**
 * @brief Selects the item at @a index, optionally extending the selection.
 *
 * Calls _Select() to update the internal selection state. If the state
 * changed, fires SelectionChanged() and sends the selection notification
 * via InvokeNotify().
 *
 * @param index   The zero-based index of the item to select.
 * @param extend  If false (the default), any existing selection is cleared
 *                first. If true, the item is added to the current selection
 *                (only meaningful in @c B_MULTIPLE_SELECTION_LIST mode).
 *
 * @see Select(int32, int32, bool), Deselect(), DeselectAll(), _Select(int32, bool)
 */
void
BListView::Select(int32 index, bool extend)
{
	if (_Select(index, extend)) {
		SelectionChanged();
		InvokeNotify(fSelectMessage, B_CONTROL_MODIFIED);
	}
}


/**
 * @brief Selects a contiguous range of items from @a start to @a finish.
 *
 * Calls _Select() with the given range. If the selection state changed, fires
 * SelectionChanged() and sends the selection notification message.
 *
 * @param start   The zero-based index of the first item to select.
 * @param finish  The zero-based index of the last item to select (inclusive).
 * @param extend  If false, the existing selection is cleared before applying
 *                the range. If true, the range is added to the selection.
 *
 * @see Select(int32, bool), Deselect(), _Select(int32, int32, bool)
 */
void
BListView::Select(int32 start, int32 finish, bool extend)
{
	if (_Select(start, finish, extend)) {
		SelectionChanged();
		InvokeNotify(fSelectMessage, B_CONTROL_MODIFIED);
	}
}


/**
 * @brief Returns whether the item at @a index is currently selected.
 *
 * @param index The zero-based item index to query.
 * @return True if the item is selected, false if it is not selected or
 *         @a index is out of range.
 *
 * @see CurrentSelection(), Select()
 */
bool
BListView::IsItemSelected(int32 index) const
{
	BListItem* item = ItemAt(index);
	if (item != NULL)
		return item->IsSelected();

	return false;
}


/**
 * @brief Returns the index of the @a index-th selected item.
 *
 * Iterates through the selected range to find the @a index-th (zero-based)
 * selected item. Use @a index = 0 to get the first selected item.
 *
 * @param index  The zero-based rank of the selected item to retrieve.
 * @return The list index of the @a index-th selected item, or -1 if there
 *         are fewer than @a index + 1 selected items.
 *
 * @see IsItemSelected(), Select(), fFirstSelected, fLastSelected
 */
int32
BListView::CurrentSelection(int32 index) const
{
	if (fFirstSelected == -1)
		return -1;

	if (index == 0)
		return fFirstSelected;

	for (int32 i = fFirstSelected; i <= fLastSelected; i++) {
		if (ItemAt(i)->IsSelected()) {
			if (index == 0)
				return i;

			index--;
		}
	}

	return -1;
}


/**
 * @brief Invokes the list view, sending the invocation message to the target.
 *
 * Builds a clone of either @a message or the stored invocation message,
 * adding "when", "source", and "be:sender" fields. For a single-selection
 * list, appends the index of the first selected item as "index". For a
 * multiple-selection list, appends the index of every selected item.
 * Watch notifications are sent via SendNotices() in addition to the direct
 * message delivery.
 *
 * @param message  An optional override message. If NULL, the stored
 *                 invocation message (Message()) is used.
 * @return A status code.
 * @retval B_OK On success.
 * @retval B_BAD_VALUE If neither @a message nor the stored message is set
 *                     and no watches are registered.
 *
 * @see SetInvocationMessage(), InvocationMessage(), BInvoker::Invoke()
 */
status_t
BListView::Invoke(BMessage* message)
{
	// Note, this is more or less a copy of BControl::Invoke() and should
	// stay that way (ie. changes done there should be adopted here)

	bool notify = false;
	uint32 kind = InvokeKind(&notify);

	BMessage clone(kind);
	status_t err = B_BAD_VALUE;

	if (!message && !notify)
		message = Message();

	if (!message) {
		if (!IsWatched())
			return err;
	} else
		clone = *message;

	clone.AddInt64("when", (int64)system_time());
	clone.AddPointer("source", this);
	clone.AddMessenger("be:sender", BMessenger(this));

	if (fListType == B_SINGLE_SELECTION_LIST)
		clone.AddInt32("index", fFirstSelected);
	else {
		if (fFirstSelected >= 0) {
			for (int32 i = fFirstSelected; i <= fLastSelected; i++) {
				if (ItemAt(i)->IsSelected())
					clone.AddInt32("index", i);
			}
		}
	}

	if (message)
		err = BInvoker::Invoke(&clone);

	SendNotices(kind, &clone);

	return err;
}


/**
 * @brief Deselects all items in the list.
 *
 * Calls _DeselectAll() with no exceptions. If any item was deselected, fires
 * SelectionChanged() and sends the selection notification message.
 *
 * @see DeselectExcept(), Deselect(), Select()
 */
void
BListView::DeselectAll()
{
	if (_DeselectAll(-1, -1)) {
		SelectionChanged();
		InvokeNotify(fSelectMessage, B_CONTROL_MODIFIED);
	}
}


/**
 * @brief Deselects all items outside the range [@a exceptFrom, @a exceptTo].
 *
 * Items in the inclusive range [@a exceptFrom, @a exceptTo] are preserved;
 * all others are deselected. If any change occurs, SelectionChanged() is fired
 * and the selection notification is sent.
 *
 * @param exceptFrom  The first index (inclusive) of the preserved range.
 * @param exceptTo    The last index (inclusive) of the preserved range.
 *
 * @see DeselectAll(), Deselect()
 */
void
BListView::DeselectExcept(int32 exceptFrom, int32 exceptTo)
{
	if (exceptFrom > exceptTo || exceptFrom < 0 || exceptTo < 0)
		return;

	if (_DeselectAll(exceptFrom, exceptTo)) {
		SelectionChanged();
		InvokeNotify(fSelectMessage, B_CONTROL_MODIFIED);
	}
}


/**
 * @brief Deselects the item at @a index.
 *
 * Calls _Deselect() to clear the item's selected state. If the state changed,
 * fires SelectionChanged() and sends the selection notification message.
 *
 * @param index The zero-based index of the item to deselect.
 *
 * @see DeselectAll(), DeselectExcept(), Select()
 */
void
BListView::Deselect(int32 index)
{
	if (_Deselect(index)) {
		SelectionChanged();
		InvokeNotify(fSelectMessage, B_CONTROL_MODIFIED);
	}
}


/**
 * @brief Hook called whenever the selection changes.
 *
 * The base-class implementation does nothing. Subclasses override this method
 * to react to selection changes without registering a separate selection
 * message target.
 *
 * @note This method is called @e after the internal selection state has been
 *       updated and @e before the selection notification message is sent.
 *
 * @see Select(), Deselect(), DeselectAll()
 */
void
BListView::SelectionChanged()
{
	// Hook method to be implemented by subclasses
}


/**
 * @brief Sorts the list items using the supplied comparator and redraws.
 *
 * Clears the entire selection before sorting (firing SelectionChanged() and
 * the selection notification if needed), then sorts the internal BList using
 * @a cmp, recomputes all item top positions, and invalidates the view.
 *
 * @param cmp A comparator function suitable for qsort(); receives pointers
 *            to BListItem* pointers and returns a negative, zero, or positive
 *            integer.
 *
 * @see SwapItems(), MoveItem()
 */
void
BListView::SortItems(int (*cmp)(const void *, const void *))
{
	if (_DeselectAll(-1, -1)) {
		SelectionChanged();
		InvokeNotify(fSelectMessage, B_CONTROL_MODIFIED);
	}

	fList.SortItems(cmp);
	_RecalcItemTops(0);
	Invalidate();
}


/**
 * @brief Swaps the items at indices @a a and @a b.
 *
 * Delegates to DoMiscellaneous() with the @c B_SWAP_OP code, which calls
 * _SwapItems() to perform the actual swap and handle redrawing.
 *
 * @param a The index of the first item.
 * @param b The index of the second item.
 * @return True on success, false if either index is out of range.
 *
 * @see MoveItem(), ReplaceItem(), _SwapItems()
 */
bool
BListView::SwapItems(int32 a, int32 b)
{
	MiscData data;

	data.swap.a = a;
	data.swap.b = b;

	return DoMiscellaneous(B_SWAP_OP, &data);
}


/**
 * @brief Moves the item at @a from to the position @a to.
 *
 * Delegates to DoMiscellaneous() with the @c B_MOVE_OP code, which calls
 * _MoveItem() to reposition the item and update the display.
 *
 * @param from The current zero-based index of the item.
 * @param to   The target zero-based index.
 * @return True on success, false if either index is out of range.
 *
 * @see SwapItems(), ReplaceItem(), _MoveItem()
 */
bool
BListView::MoveItem(int32 from, int32 to)
{
	MiscData data;

	data.move.from = from;
	data.move.to = to;

	return DoMiscellaneous(B_MOVE_OP, &data);
}


/**
 * @brief Replaces the item at @a index with @a item.
 *
 * Delegates to DoMiscellaneous() with the @c B_REPLACE_OP code, which calls
 * _ReplaceItem() to substitute the item, update selection state, recompute
 * top positions, and redraw the affected area.
 *
 * @param index The zero-based index of the item to replace.
 * @param item  The new BListItem to insert at @a index. Ownership is not
 *              transferred; the old item is @b not deleted.
 * @return True on success, false if @a index is out of range or @a item is NULL.
 *
 * @see SwapItems(), MoveItem(), _ReplaceItem()
 */
bool
BListView::ReplaceItem(int32 index, BListItem* item)
{
	MiscData data;

	data.replace.index = index;
	data.replace.item = item;

	return DoMiscellaneous(B_REPLACE_OP, &data);
}


/**
 * @brief Returns the bounding rectangle of the item at @a index.
 *
 * The returned rect spans the full width of the view. Its top and bottom
 * are taken from the item's cached top/bottom positions. Returns an
 * empty rect (top=0, bottom=-1) if @a index is out of range.
 *
 * @param index The zero-based index of the item.
 * @return The item's frame rectangle in the view's coordinate system.
 *
 * @see InvalidateItem(), IndexOf(BPoint), Draw()
 */
BRect
BListView::ItemFrame(int32 index)
{
	BRect frame = Bounds();
	if (index < 0 || index >= CountItems()) {
		frame.top = 0;
		frame.bottom = -1;
	} else {
		BListItem* item = ItemAt(index);
		frame.top = item->Top();
		frame.bottom = item->Bottom();
	}
	return frame;
}


// #pragma mark -


/**
 * @brief Resolves a scripting specifier to the appropriate handler.
 *
 * Checks whether the given @a property and @a specifier match one of the
 * entries in sProperties. If so, returns @c this so that MessageReceived()
 * processes the scripting command. Otherwise, delegates to
 * BView::ResolveSpecifier().
 *
 * @param message    The scripting message being resolved.
 * @param index      The specifier index within @a message.
 * @param specifier  The specifier sub-message.
 * @param what       The specifier type constant.
 * @param property   The property name string.
 * @return The BHandler that should process the message.
 *
 * @see GetSupportedSuites(), MessageReceived()
 */
BHandler*
BListView::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 what, const char* property)
{
	BPropertyInfo propInfo(sProperties);

	if (propInfo.FindMatch(message, 0, specifier, what, property) < 0) {
		return BView::ResolveSpecifier(message, index, specifier, what,
			property);
	}

	// TODO: msg->AddInt32("_match_code_", );

	return this;
}


/**
 * @brief Reports the scripting suites supported by BListView.
 *
 * Adds the suite name "suite/vnd.Be-list-view" and the flat-packed property
 * info table (sProperties) to @a data, then chains to BView::GetSupportedSuites().
 *
 * @param data The BMessage to populate with suite and property information.
 * @return A status code.
 * @retval B_OK On success.
 * @retval B_BAD_VALUE If @a data is NULL.
 *
 * @see ResolveSpecifier(), MessageReceived()
 */
status_t
BListView::GetSupportedSuites(BMessage* data)
{
	if (data == NULL)
		return B_BAD_VALUE;

	status_t err = data->AddString("suites", "suite/vnd.Be-list-view");

	BPropertyInfo propertyInfo(sProperties);
	if (err == B_OK)
		err = data->AddFlat("messages", &propertyInfo);

	if (err == B_OK)
		return BView::GetSupportedSuites(data);
	return err;
}


/**
 * @brief Executes a binary-compatibility perform operation.
 *
 * Handles layout-related perform codes (MinSize, MaxSize, PreferredSize,
 * LayoutAlignment, HasHeightForWidth, GetHeightForWidth, SetLayout,
 * LayoutInvalidated, DoLayout) by dispatching to the corresponding BListView
 * virtual methods and storing the result in the data struct. Unrecognized
 * codes are forwarded to BView::Perform().
 *
 * @param code   The perform operation code (e.g. @c PERFORM_CODE_MIN_SIZE).
 * @param _data  A pointer to the operation-specific data struct.
 * @return A status code.
 * @retval B_OK On success.
 *
 * @see MinSize(), MaxSize(), PreferredSize()
 */
status_t
BListView::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_MIN_SIZE:
			((perform_data_min_size*)_data)->return_value
				= BListView::MinSize();
			return B_OK;
		case PERFORM_CODE_MAX_SIZE:
			((perform_data_max_size*)_data)->return_value
				= BListView::MaxSize();
			return B_OK;
		case PERFORM_CODE_PREFERRED_SIZE:
			((perform_data_preferred_size*)_data)->return_value
				= BListView::PreferredSize();
			return B_OK;
		case PERFORM_CODE_LAYOUT_ALIGNMENT:
			((perform_data_layout_alignment*)_data)->return_value
				= BListView::LayoutAlignment();
			return B_OK;
		case PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH:
			((perform_data_has_height_for_width*)_data)->return_value
				= BListView::HasHeightForWidth();
			return B_OK;
		case PERFORM_CODE_GET_HEIGHT_FOR_WIDTH:
		{
			perform_data_get_height_for_width* data
				= (perform_data_get_height_for_width*)_data;
			BListView::GetHeightForWidth(data->width, &data->min, &data->max,
				&data->preferred);
			return B_OK;
		}
		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BListView::SetLayout(data->layout);
			return B_OK;
		}
		case PERFORM_CODE_LAYOUT_INVALIDATED:
		{
			perform_data_layout_invalidated* data
				= (perform_data_layout_invalidated*)_data;
			BListView::LayoutInvalidated(data->descendants);
			return B_OK;
		}
		case PERFORM_CODE_DO_LAYOUT:
		{
			BListView::DoLayout();
			return B_OK;
		}
	}

	return BView::Perform(code, _data);
}


/**
 * @brief Dispatches miscellaneous list operations (replace, move, swap).
 *
 * Acts as a central dispatch point called by ReplaceItem(), MoveItem(), and
 * SwapItems(). Subclasses such as BOutlineListView override this method to
 * intercept these operations and maintain their own tree structure.
 *
 * @param code The operation code: @c B_NO_OP, @c B_REPLACE_OP,
 *             @c B_MOVE_OP, or @c B_SWAP_OP.
 * @param data A union containing the operation-specific parameters.
 * @return True if the operation succeeded, false otherwise.
 *
 * @see SwapItems(), MoveItem(), ReplaceItem()
 */
bool
BListView::DoMiscellaneous(MiscCode code, MiscData* data)
{
	if (code > B_SWAP_OP)
		return false;

	switch (code) {
		case B_NO_OP:
			break;

		case B_REPLACE_OP:
			return _ReplaceItem(data->replace.index, data->replace.item);

		case B_MOVE_OP:
			return _MoveItem(data->move.from, data->move.to);

		case B_SWAP_OP:
			return _SwapItems(data->swap.a, data->swap.b);
	}

	return false;
}


// #pragma mark -


/** @brief Reserved virtual slot 2 for future binary-compatible extensions. */
void BListView::_ReservedListView2() {}
/** @brief Reserved virtual slot 3 for future binary-compatible extensions. */
void BListView::_ReservedListView3() {}
/** @brief Reserved virtual slot 4 for future binary-compatible extensions. */
void BListView::_ReservedListView4() {}


/**
 * @brief Assignment operator (not implemented; copying a BListView is unsupported).
 *
 * @return A reference to @c *this, unchanged.
 */
BListView&
BListView::operator=(const BListView& /*other*/)
{
	return *this;
}


// #pragma mark -


/**
 * @brief Common initialization shared by all constructors.
 *
 * Sets the list type, resets all selection tracking indices to -1,
 * allocates and zero-initializes the fTrack struct, and configures the
 * view's background and low colors.
 *
 * @param type  @c B_SINGLE_SELECTION_LIST or @c B_MULTIPLE_SELECTION_LIST.
 *
 * @see fTrack, fFirstSelected, fLastSelected, fAnchorIndex
 */
void
BListView::_InitObject(list_view_type type)
{
	fListType = type;
	fFirstSelected = -1;
	fLastSelected = -1;
	fAnchorIndex = -1;
	fSelectMessage = NULL;
	fScrollView = NULL;

	fTrack = new track_data;
	fTrack->drag_start = B_ORIGIN;
	fTrack->item_index = -1;
	fTrack->buttons = 0;
	fTrack->selected_click_count = 0;
	fTrack->was_selected = false;
	fTrack->try_drag = false;
	fTrack->is_dragging = false;
	fTrack->last_click_time = 0;

	SetViewUIColor(B_LIST_BACKGROUND_COLOR);
	SetLowUIColor(B_LIST_BACKGROUND_COLOR);
}


/**
 * @brief Adjusts the scroll bar range and proportion to match the current content.
 *
 * For the vertical scroll bar: if the total item height fits within the view,
 * the range is set to [0, 0] and the scroll position is reset to the top.
 * Otherwise, the range is set to allow scrolling to the last item, and the
 * proportion is set accordingly. If the list has scrolled past the content
 * bottom, the view is scrolled back up. Step sizes are set to one item height
 * (small) and one view height (large).
 *
 * For the horizontal scroll bar: behaves similarly based on the maximum item
 * width returned by GetPreferredSize().
 *
 * @see AttachedToWindow(), FrameResized(), AddItem(), RemoveItem()
 */
void
BListView::_FixupScrollBar()
{

	BScrollBar* vertScroller = ScrollBar(B_VERTICAL);
	if (vertScroller != NULL) {
		BRect bounds = Bounds();
		int32 count = CountItems();

		float itemHeight = 0.0;

		if (CountItems() > 0)
			itemHeight = ItemAt(CountItems() - 1)->Bottom();

		if (bounds.Height() > itemHeight) {
			// no scrolling
			vertScroller->SetRange(0.0, 0.0);
			vertScroller->SetValue(0.0);
				// also scrolls ListView to the top
		} else {
			vertScroller->SetRange(0.0, itemHeight - bounds.Height() - 1.0);
			vertScroller->SetProportion(bounds.Height () / itemHeight);
			// scroll up if there is empty room on bottom
			if (itemHeight < bounds.bottom)
				ScrollBy(0.0, bounds.bottom - itemHeight);
		}

		if (count != 0)
			vertScroller->SetSteps(
				ceilf(FirstItem()->Height()), bounds.Height());
	}

	BScrollBar* horizontalScroller = ScrollBar(B_HORIZONTAL);
	if (horizontalScroller != NULL) {
		float w;
		GetPreferredSize(&w, NULL);
		BRect scrollBarSize = horizontalScroller->Bounds();

		if (w <= scrollBarSize.Width()) {
			// no scrolling
			horizontalScroller->SetRange(0.0, 0.0);
			horizontalScroller->SetValue(0.0);
		} else {
			horizontalScroller->SetRange(0, w - scrollBarSize.Width());
			horizontalScroller->SetProportion(scrollBarSize.Width() / w);
		}
	}
}


/**
 * @brief Invalidates the region from @a index to the bottom of the view.
 *
 * Computes the dirty rectangle starting just below the item preceding
 * @a index (to account for items that may already have been removed from
 * the list) and extending to the bottom of the view's bounds.
 *
 * @param index The first item index whose visual row (and all below) needs
 *              to be redrawn.
 *
 * @see InvalidateItem(), AddItem(), RemoveItem()
 */
void
BListView::_InvalidateFrom(int32 index)
{
	// make sure index is behind last valid index
	int32 count = CountItems();
	if (index >= count)
		index = count;

	// take the item before the wanted one,
	// because that might already be removed
	index--;
	BRect dirty = Bounds();
	if (index >= 0)
		dirty.top = ItemFrame(index).bottom + 1;

	Invalidate(dirty);
}


/**
 * @brief Recomputes the top position and metrics for every item using the current font.
 *
 * Calls BListItem::SetTop() and BListItem::Update() for each item in order,
 * so that item heights and widths reflect the current view font. Called when
 * the view is first attached to a window, when the font changes, and when
 * the frame is resized.
 *
 * @see AttachedToWindow(), SetFont(), FrameResized()
 */
void
BListView::_UpdateItems()
{
	BFont font;
	GetFont(&font);
	for (int32 i = 0; i < CountItems(); i++) {
		ItemAt(i)->SetTop((i > 0) ? ItemAt(i - 1)->Bottom() + 1.0 : 0.0);
		ItemAt(i)->Update(this, &font);
	}
}


/**
 * @brief Selects the item at @a index and returns true if the selection changed.
 *
 * Acquires the window lock if a window is present. If @a extend is false,
 * all existing selections are cleared first via _DeselectAll(). The anchor
 * index is always updated to @a index. If the item is already selected or
 * disabled, no visual change is made but the method still returns true if a
 * prior deselect caused a change.
 *
 * @param index   The zero-based index of the item to select.
 * @param extend  If false, clears the existing selection before selecting.
 * @return True if the selection state changed, false otherwise.
 *
 * @see _Select(int32, int32, bool), _Deselect(), _DeselectAll()
 */
bool
BListView::_Select(int32 index, bool extend)
{
	if (index < 0 || index >= CountItems())
		return false;

	// only lock the window when there is one
	BAutolock locker(Window());
	if (Window() != NULL && !locker.IsLocked())
		return false;

	bool changed = false;

	if (!extend && fFirstSelected != -1)
		changed = _DeselectAll(index, index);

	fAnchorIndex = index;

	BListItem* item = ItemAt(index);
	if (!item->IsEnabled() || item->IsSelected()) {
		// if the item is already selected, or can't be selected,
		// we're done here
		return changed;
	}

	// keep track of first and last selected item
	if (fFirstSelected == -1) {
		// no previous selection
		fFirstSelected = index;
		fLastSelected = index;
	} else if (index < fFirstSelected) {
		fFirstSelected = index;
	} else if (index > fLastSelected) {
		fLastSelected = index;
	}

	item->Select();
	if (Window() != NULL)
		InvalidateItem(index);

	return true;
}


/**
 * @brief Selects the items in the range [@a from, @a to] and returns true if the
 *        selection changed.
 *
 * Acquires the window lock if a window is present. If @a extend is false,
 * any prior selection is cleared before the range is applied. Updates
 * fFirstSelected and fLastSelected to encompass the new range, then marks
 * each enabled, previously-unselected item in the range as selected and
 * invalidates it.
 *
 * @param from    The zero-based index of the first item to select.
 * @param to      The zero-based index of the last item to select (inclusive).
 * @param extend  If false, clears the existing selection before applying the range.
 * @return True if any item's selection state changed, false otherwise.
 *
 * @see _Select(int32, bool), _DeselectAll()
 */
bool
BListView::_Select(int32 from, int32 to, bool extend)
{
	if (to < from)
		return false;

	BAutolock locker(Window());
	if (Window() && !locker.IsLocked())
		return false;

	bool changed = false;

	if (fFirstSelected != -1 && !extend)
		changed = _DeselectAll(from, to);

	if (fFirstSelected == -1) {
		fFirstSelected = from;
		fLastSelected = to;
	} else {
		if (from < fFirstSelected)
			fFirstSelected = from;
		if (to > fLastSelected)
			fLastSelected = to;
	}

	for (int32 i = from; i <= to; ++i) {
		BListItem* item = ItemAt(i);
		if (item != NULL && !item->IsSelected() && item->IsEnabled()) {
			item->Select();
			if (Window() != NULL)
				InvalidateItem(i);
			changed = true;
		}
	}

	return changed;
}


/**
 * @brief Deselects the item at @a index and redraws it.
 *
 * Acquires the window lock if a window is present. Clears the item's selected
 * state, redraws the item within the view's bounds, and updates fFirstSelected
 * / fLastSelected by calling _CalcFirstSelected() / _CalcLastSelected() if the
 * deselected item was at one of the boundary positions.
 *
 * @param index The zero-based index of the item to deselect.
 * @return True if the item's state changed (i.e. it was selected),
 *         false if the index was out of range or the item was not selected.
 *
 * @see _DeselectAll(), _CalcFirstSelected(), _CalcLastSelected()
 */
bool
BListView::_Deselect(int32 index)
{
	if (index < 0 || index >= CountItems())
		return false;

	BWindow* window = Window();
	BAutolock locker(window);
	if (window != NULL && !locker.IsLocked())
		return false;

	BListItem* item = ItemAt(index);

	if (item != NULL && item->IsSelected()) {
		BRect frame(ItemFrame(index));
		BRect bounds(Bounds());

		item->Deselect();

		if (fFirstSelected == index && fLastSelected == index) {
			fFirstSelected = -1;
			fLastSelected = -1;
		} else {
			if (fFirstSelected == index)
				fFirstSelected = _CalcFirstSelected(index);

			if (fLastSelected == index)
				fLastSelected = _CalcLastSelected(index);
		}

		if (window && bounds.Intersects(frame))
			DrawItem(ItemAt(index), frame, true);
	}

	return true;
}


/**
 * @brief Deselects all items, optionally preserving a range.
 *
 * Iterates from fFirstSelected to fLastSelected and deselects every item
 * not within the range [@a exceptFrom, @a exceptTo]. If @a exceptFrom and
 * @a exceptTo are both -1, all selected items are deselected unconditionally.
 * After deselection, fFirstSelected and fLastSelected are recomputed.
 *
 * @param exceptFrom  First index of the range to preserve, or -1 for none.
 * @param exceptTo    Last index of the range to preserve, or -1 for none.
 * @return True if any item was deselected, false if nothing changed.
 *
 * @see _Deselect(), _CalcFirstSelected(), _CalcLastSelected()
 */
bool
BListView::_DeselectAll(int32 exceptFrom, int32 exceptTo)
{
	if (fFirstSelected == -1)
		return false;

	BAutolock locker(Window());
	if (Window() && !locker.IsLocked())
		return false;

	bool changed = false;

	for (int32 index = fFirstSelected; index <= fLastSelected; index++) {
		// don't deselect the items we shouldn't deselect
		if (exceptFrom != -1 && exceptFrom <= index && exceptTo >= index)
			continue;

		BListItem* item = ItemAt(index);
		if (item != NULL && item->IsSelected()) {
			item->Deselect();
			InvalidateItem(index);
			changed = true;
		}
	}

	if (!changed)
		return false;

	if (exceptFrom != -1) {
		fFirstSelected = _CalcFirstSelected(exceptFrom);
		fLastSelected = _CalcLastSelected(exceptTo);
	} else
		fFirstSelected = fLastSelected = -1;

	return true;
}


/**
 * @brief Finds the first selected item at or after @a after.
 *
 * Scans forward from @a after to the end of the list looking for the first
 * item whose IsSelected() returns true.
 *
 * @param after The index from which to begin scanning (inclusive).
 * @return The index of the first selected item at or after @a after,
 *         or -1 if none exists.
 *
 * @see _CalcLastSelected(), _Deselect(), _DeselectAll()
 */
int32
BListView::_CalcFirstSelected(int32 after)
{
	if (after >= CountItems())
		return -1;

	int32 count = CountItems();
	for (int32 i = after; i < count; i++) {
		if (ItemAt(i)->IsSelected())
			return i;
	}

	return -1;
}


/**
 * @brief Finds the last selected item at or before @a before.
 *
 * Scans backward from @a before (clamped to CountItems()-1) to index 0
 * looking for the last item whose IsSelected() returns true.
 *
 * @param before The index from which to begin scanning backward (inclusive).
 * @return The index of the last selected item at or before @a before,
 *         or -1 if none exists.
 *
 * @see _CalcFirstSelected(), _Deselect(), _DeselectAll()
 */
int32
BListView::_CalcLastSelected(int32 before)
{
	if (before < 0)
		return -1;

	before = std::min(CountItems() - 1, before);

	for (int32 i = before; i >= 0; i--) {
		if (ItemAt(i)->IsSelected())
			return i;
	}

	return -1;
}


/**
 * @brief Draws a single list item with the correct text color for its state.
 *
 * Sets the high color based on the item's state before delegating to
 * BListItem::DrawItem():
 * - Disabled items use a dimmed version of @c B_LIST_ITEM_TEXT_COLOR.
 * - Selected items use @c B_LIST_SELECTED_ITEM_TEXT_COLOR.
 * - Normal items use @c B_LIST_ITEM_TEXT_COLOR.
 *
 * @param item      The BListItem to draw.
 * @param itemRect  The bounding rectangle in which to draw the item.
 * @param complete  If true, the item should redraw its entire background;
 *                  if false, only the changed parts need updating.
 *
 * @see Draw(), InvalidateItem(), BListItem::DrawItem()
 */
void
BListView::DrawItem(BListItem* item, BRect itemRect, bool complete)
{
	if (!item->IsEnabled()) {
		rgb_color textColor = ui_color(B_LIST_ITEM_TEXT_COLOR);
		rgb_color disabledColor;
		if (textColor.red + textColor.green + textColor.blue > 128 * 3)
			disabledColor = tint_color(textColor, B_DARKEN_2_TINT);
		else
			disabledColor = tint_color(textColor, B_LIGHTEN_2_TINT);

		SetHighColor(disabledColor);
	} else if (item->IsSelected())
		SetHighColor(ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR));
	else
		SetHighColor(ui_color(B_LIST_ITEM_TEXT_COLOR));

	item->DrawItem(this, itemRect, complete);
}


/**
 * @brief Swaps the items at positions @a a and @a b in the underlying list.
 *
 * Records the item frames before the swap, performs the swap in fList, then
 * adjusts the anchor index and, if the selection state of the two items
 * differs, rescans the selection boundaries. Sets the top positions of the
 * swapped items to match their pre-swap frames. When the items have different
 * heights, _RecalcItemTops() is called for the range and the union of both
 * frames is invalidated; otherwise only the two individual frames are
 * invalidated.
 *
 * @param a Index of the first item.
 * @param b Index of the second item.
 * @return True on success, false if the swap in fList failed.
 *
 * @see SwapItems(), _MoveItem(), _ReplaceItem(), _RescanSelection()
 */
bool
BListView::_SwapItems(int32 a, int32 b)
{
	// remember frames of items before anything happens,
	// the tricky situation is when the two items have
	// a different height
	BRect aFrame = ItemFrame(a);
	BRect bFrame = ItemFrame(b);

	if (!fList.SwapItems(a, b))
		return false;

	if (a == b) {
		// nothing to do, but success nevertheless
		return true;
	}

	// track anchor item
	if (fAnchorIndex == a)
		fAnchorIndex = b;
	else if (fAnchorIndex == b)
		fAnchorIndex = a;

	// track selection
	// NOTE: this is only important if the selection status
	// of both items is not the same
	int32 first = std::min(a, b);
	int32 last = std::max(a, b);
	if (ItemAt(a)->IsSelected() != ItemAt(b)->IsSelected()) {
		if (first < fFirstSelected || last > fLastSelected) {
			_RescanSelection(std::min(first, fFirstSelected),
				std::max(last, fLastSelected));
		}
		// though the actually selected items stayed the
		// same, the selection has still changed
		SelectionChanged();
	}

	ItemAt(a)->SetTop(aFrame.top);
	ItemAt(b)->SetTop(bFrame.top);

	// take care of invalidation
	if (Window()) {
		// NOTE: window looper is assumed to be locked!
		if (aFrame.Height() != bFrame.Height()) {
			_RecalcItemTops(first, last);
			// items in between shifted visually
			Invalidate(aFrame | bFrame);
		} else {
			Invalidate(aFrame);
			Invalidate(bFrame);
		}
	}

	return true;
}


/**
 * @brief Moves the item at @a from to the position @a to.
 *
 * Records both item frames before the move, performs the move in fList,
 * updates the anchor index, rescans the selection if the moved item is
 * selected, recomputes top positions starting from the lower of the two
 * indices, and invalidates the union of both frames.
 *
 * @param from The current zero-based index of the item.
 * @param to   The destination zero-based index.
 * @return True on success, false if the move in fList failed.
 *
 * @see MoveItem(), _SwapItems(), _ReplaceItem(), _RescanSelection()
 */
bool
BListView::_MoveItem(int32 from, int32 to)
{
	// remember item frames before doing anything
	BRect frameFrom = ItemFrame(from);
	BRect frameTo = ItemFrame(to);

	if (!fList.MoveItem(from, to))
		return false;

	// track anchor item
	if (fAnchorIndex == from)
		fAnchorIndex = to;

	// track selection
	if (ItemAt(to)->IsSelected()) {
		_RescanSelection(from, to);
		// though the actually selected items stayed the
		// same, the selection has still changed
		SelectionChanged();
	}

	_RecalcItemTops((to > from) ? from : to);

	// take care of invalidation
	if (Window()) {
		// NOTE: window looper is assumed to be locked!
		Invalidate(frameFrom | frameTo);
	}

	return true;
}


/**
 * @brief Replaces the item at @a index with @a item.
 *
 * Verifies that @a item is non-NULL and that a current item exists at
 * @a index. Performs the replacement in fList, then if the old and new items
 * differ in selected state, rescans the selection between the known
 * boundaries and fires SelectionChanged(). Recomputes top positions from
 * @a index onward. If the item height changed, invalidates from @a index
 * down and fixes up the scroll bar; otherwise invalidates only the item frame.
 *
 * @param index The zero-based index of the item to replace.
 * @param item  The new item. Must be non-NULL.
 * @return True on success, false if @a item is NULL or @a index is out of range.
 *
 * @see ReplaceItem(), _SwapItems(), _MoveItem(), _RescanSelection()
 */
bool
BListView::_ReplaceItem(int32 index, BListItem* item)
{
	if (item == NULL)
		return false;

	BListItem* old = ItemAt(index);
	if (!old)
		return false;

	BRect frame = ItemFrame(index);

	bool selectionChanged = old->IsSelected() != item->IsSelected();

	// replace item
	if (!fList.ReplaceItem(index, item))
		return false;

	// tack selection
	if (selectionChanged) {
		int32 start = std::min(fFirstSelected, index);
		int32 end = std::max(fLastSelected, index);
		_RescanSelection(start, end);
		SelectionChanged();
	}
	_RecalcItemTops(index);

	bool itemHeightChanged = frame != ItemFrame(index);

	// take care of invalidation
	if (Window()) {
		// NOTE: window looper is assumed to be locked!
		if (itemHeightChanged)
			_InvalidateFrom(index);
		else
			Invalidate(frame);
	}

	if (itemHeightChanged)
		_FixupScrollBar();

	return true;
}


/**
 * @brief Rescans the selection boundaries in the range [@a from, @a to].
 *
 * Normalizes the range (ensuring from <= to and clamping to valid indices),
 * swaps the anchor index if it matches either boundary, then walks the range
 * to find the new fFirstSelected and fLastSelected values. Used after items
 * are swapped, moved, or replaced to keep the selection tracking consistent.
 *
 * @param from  One end of the range to rescan.
 * @param to    The other end of the range to rescan.
 *
 * @see _SwapItems(), _MoveItem(), _ReplaceItem()
 */
void
BListView::_RescanSelection(int32 from, int32 to)
{
	if (from > to) {
		int32 tmp = from;
		from = to;
		to = tmp;
	}

	from = std::max((int32)0, from);
	to = std::min(to, CountItems() - 1);

	if (fAnchorIndex != -1) {
		if (fAnchorIndex == from)
			fAnchorIndex = to;
		else if (fAnchorIndex == to)
			fAnchorIndex = from;
	}

	for (int32 i = from; i <= to; i++) {
		if (ItemAt(i)->IsSelected()) {
			fFirstSelected = i;
			break;
		}
	}

	if (fFirstSelected > from)
		from = fFirstSelected;

	fLastSelected = fFirstSelected;
	for (int32 i = from; i <= to; i++) {
		if (ItemAt(i)->IsSelected())
			fLastSelected = i;
	}
}


/**
 * @brief Recomputes the cached top position for items from @a start to @a end.
 *
 * Starts from the bottom of the item preceding @a start (or 0.0 if
 * @a start == 0) and accumulates heights using ceilf(item->Height()) to
 * assign each item's top position. When @a end is negative, all items from
 * @a start to the end of the list are updated.
 *
 * @param start  The first item index to update.
 * @param end    The last item index to update (inclusive), or -1 for all
 *               items from @a start to the end of the list.
 *
 * @see AddItem(), RemoveItem(), _SwapItems(), _MoveItem(), _ReplaceItem()
 */
void
BListView::_RecalcItemTops(int32 start, int32 end)
{
	int32 count = CountItems();
	if ((start < 0) || (start >= count))
		return;

	if (end >= 0)
		count = end + 1;

	float top = (start == 0) ? 0.0 : ItemAt(start - 1)->Bottom() + 1.0;

	for (int32 i = start; i < count; i++) {
		BListItem *item = ItemAt(i);
		item->SetTop(top);
		top += ceilf(item->Height());
	}
}


/**
 * @brief Updates the selection in response to a mouse click at @a index.
 *
 * Interprets the current modifier keys to decide how the click should
 * affect the selection:
 * - Clicking a disabled item deselects everything.
 * - In multiple-selection mode, Shift extends or contracts the selection;
 *   Command toggles the clicked item; a plain click selects only that item
 *   (unless it is the second click on an already-selected item, which is
 *   reserved for drag-and-drop).
 * - In single-selection mode, Command+click on a selected item deselects it;
 *   any other click selects only that item.
 *
 * @param index The index of the item that was clicked, or -1 if no item.
 *
 * @see MouseDown(), Select(), Deselect(), DeselectExcept()
 */
void
BListView::_DoSelection(int32 index)
{
	BListItem* item = ItemAt(index);

	// don't alter selection if invalid item clicked
	if (index < 0 || item == NULL)
		return;

	// deselect all if clicked on disabled
	if (!item->IsEnabled())
		return DeselectAll();

	if (fListType == B_MULTIPLE_SELECTION_LIST) {
		// multiple-selection list

		if ((modifiers() & B_SHIFT_KEY) != 0) {
			if (index >= fFirstSelected && index < fLastSelected) {
				// clicked inside of selected items block, deselect all
				// except from the first selected index to item index
				DeselectExcept(fFirstSelected, index);
			} else {
				// extend or contract selection
				Select(std::min(index, fFirstSelected),
					std::max(index, fLastSelected));
			}
		} else if ((modifiers() & B_COMMAND_KEY) != 0) {
			// toggle selection state
			if (item->IsSelected())
				Deselect(index);
			else
				Select(index, true);
		} else if (fTrack->selected_click_count != 1)
			Select(index); // eat a click on selected for drag and drop
	} else {
		// single-selection list

		// toggle selection state
		if ((modifiers() & B_COMMAND_KEY) != 0 && item->IsSelected())
			Deselect(index);
		else
			Select(index);
	}
}
