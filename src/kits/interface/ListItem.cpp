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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2010 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ulrich Wimboeck
 *       Marc Flerackers, mflerackers@androme.be
 *       Rene Gollent
 */


/**
 * @file ListItem.cpp
 * @brief Implementation of BListItem, the base class for items in a BListView
 *
 * BListItem stores a single row of data in a BListView. It manages selection
 * state, expansion state (for BOutlineListView), and provides a DrawItem()
 * hook for custom rendering.
 *
 * @see BListView, BOutlineListView, BStringItem
 */


#include <ListItem.h>

#include <Message.h>
#include <View.h>


/**
 * @brief Constructs a BListItem with the given outline level and expansion state.
 *
 * All dimensional fields (fTop, fWidth, fHeight) are initialised to zero;
 * call Update() once the item has been added to a list view to populate them.
 *
 * @param level    The nesting depth used by BOutlineListView (0 for top-level items).
 * @param expanded Whether the item starts in the expanded state.
 *
 * @see BOutlineListView, Update()
 */
BListItem::BListItem(uint32 level, bool expanded)
	:
	fTop(0.0),
	fTemporaryList(0),
	fWidth(0),
	fHeight(0),
	fLevel(level),
	fSelected(false),
	fEnabled(true),
	fExpanded(expanded),
	fHasSubitems(false),
	fVisible(true)
{
}


/**
 * @brief Constructs a BListItem from an archived BMessage.
 *
 * Restores selection state ("_sel"), enabled state ("_disable"), expansion
 * state ("_li_expanded"), and outline level ("_li_outline_level") from the
 * archive. If "_disable" is absent, the item defaults to enabled.
 *
 * @param data The archived BMessage to restore state from.
 *
 * @see Archive(), BArchivable::BArchivable()
 */
BListItem::BListItem(BMessage* data)
	:
	BArchivable(data),
	fTop(0.0),
	fWidth(0),
	fHeight(0),
	fLevel(0),
	fSelected(false),
	fEnabled(true),
	fExpanded(false),
	fHasSubitems(false),
	fVisible(true)
{
	data->FindBool("_sel", &fSelected);

	if (data->FindBool("_disable", &fEnabled) != B_OK)
		fEnabled = true;
	else
		fEnabled = false;

	data->FindBool("_li_expanded", &fExpanded);
	data->FindInt32("_li_outline_level", (int32*)&fLevel);
}


/**
 * @brief Destroys the BListItem.
 */
BListItem::~BListItem()
{
}


/**
 * @brief Archives the BListItem's state into a BMessage.
 *
 * Stores the selection state ("_sel"), disabled state ("_disable"), expansion
 * state ("_li_expanded"), and outline level ("_li_outline_level") only when
 * their values differ from the defaults, to keep archives compact.
 *
 * @param archive The BMessage to archive into.
 * @param deep    If true, child objects are also archived (unused by BListItem).
 * @return A status code.
 * @retval B_OK On success.
 *
 * @see BListItem(BMessage*), BArchivable::Archive()
 */
status_t
BListItem::Archive(BMessage* archive, bool deep) const
{
	status_t status = BArchivable::Archive(archive, deep);
	if (status == B_OK && fSelected)
		status = archive->AddBool("_sel", true);

	if (status == B_OK && !fEnabled)
		status = archive->AddBool("_disable", true);

	if (status == B_OK && fExpanded)
		status = archive->AddBool("_li_expanded", true);

	if (status == B_OK && fLevel != 0)
		status = archive->AddInt32("_li_outline_level", fLevel);

	return status;
}


/**
 * @brief Returns the cached height of the item in pixels.
 *
 * The height is set by Update() or SetHeight().
 *
 * @return The item height in pixels.
 *
 * @see SetHeight(), Update()
 */
float
BListItem::Height() const
{
	return fHeight;
}


/**
 * @brief Returns the cached width of the item in pixels.
 *
 * The width is set by Update() or SetWidth().
 *
 * @return The item width in pixels.
 *
 * @see SetWidth(), Update()
 */
float
BListItem::Width() const
{
	return fWidth;
}


/**
 * @brief Returns whether the item is currently selected.
 *
 * @return true if the item is selected, false otherwise.
 *
 * @see Select(), Deselect()
 */
bool
BListItem::IsSelected() const
{
	return fSelected;
}


/**
 * @brief Marks the item as selected.
 *
 * Does not trigger a redraw; the owning BListView is responsible for
 * invalidating the item's row after changing selection state.
 *
 * @see Deselect(), IsSelected()
 */
void
BListItem::Select()
{
	fSelected = true;
}


/**
 * @brief Marks the item as deselected.
 *
 * Does not trigger a redraw; the owning BListView is responsible for
 * invalidating the item's row after changing selection state.
 *
 * @see Select(), IsSelected()
 */
void
BListItem::Deselect()
{
	fSelected = false;
}


/**
 * @brief Enables or disables the item.
 *
 * Disabled items are typically rendered differently (e.g. greyed out) and
 * cannot be selected by user interaction. Subclasses may check IsEnabled()
 * inside DrawItem() to adjust the appearance.
 *
 * @param on Pass true to enable the item, false to disable it.
 *
 * @see IsEnabled()
 */
void
BListItem::SetEnabled(bool on)
{
	fEnabled = on;
}


/**
 * @brief Returns whether the item is currently enabled.
 *
 * @return true if the item is enabled, false if it is disabled.
 *
 * @see SetEnabled()
 */
bool
BListItem::IsEnabled() const
{
	return fEnabled;
}


/**
 * @brief Sets the cached height of the item.
 *
 * This is typically called from within Update() by subclasses after measuring
 * the required row height against the current font metrics.
 *
 * @param height The new height in pixels.
 *
 * @see Height(), Update()
 */
void
BListItem::SetHeight(float height)
{
	fHeight = height;
}


/**
 * @brief Sets the cached width of the item.
 *
 * This is typically called from within Update() by subclasses after measuring
 * the required content width.
 *
 * @param width The new width in pixels.
 *
 * @see Width(), Update()
 */
void
BListItem::SetWidth(float width)
{
	fWidth = width;
}


/**
 * @brief Measures and caches the item's width and height for the given font.
 *
 * The default implementation sets the width to the owner view's current
 * bounds width and the height to the sum of the font's ascent, descent, and
 * leading. Subclasses should override this to measure their own content.
 *
 * @param owner The BView that owns this item; its bounds width is used.
 * @param font  The font to measure against.
 *
 * @see SetWidth(), SetHeight(), DrawItem()
 */
void
BListItem::Update(BView* owner, const BFont* font)
{
	font_height fh;
	font->GetHeight(&fh);

	SetWidth(owner->Bounds().Width());
	SetHeight(ceilf(fh.ascent + fh.descent + fh.leading));
}


/**
 * @brief Dispatches a perform code to the base BArchivable implementation.
 *
 * This hook exists to support future binary-compatible extensions. Callers
 * should not rely on any specific behavior beyond forwarding to BArchivable.
 *
 * @param d   The perform operation code.
 * @param arg An opaque argument whose meaning depends on @a d.
 * @return The status code returned by BArchivable::Perform().
 *
 * @see BArchivable::Perform()
 */
status_t
BListItem::Perform(perform_code d, void* arg)
{
	return BArchivable::Perform(d, arg);
}


/**
 * @brief Sets the item's expanded state for use in BOutlineListView.
 *
 * When an item is expanded, its subitems (if any) are visible in the outline.
 * Collapsing hides the subitems. Changing this flag does not automatically
 * redraw the list; call BOutlineListView::InvalidateItem() if needed.
 *
 * @param expanded Pass true to expand the item, false to collapse it.
 *
 * @see IsExpanded(), BOutlineListView
 */
void
BListItem::SetExpanded(bool expanded)
{
	fExpanded = expanded;
}


/**
 * @brief Returns whether the item is currently expanded.
 *
 * @return true if the item is expanded, false if it is collapsed.
 *
 * @see SetExpanded()
 */
bool
BListItem::IsExpanded() const
{
	return fExpanded;
}


/**
 * @brief Returns the item's nesting depth in an outline list.
 *
 * A level of 0 indicates a top-level item; higher values indicate progressively
 * deeper nesting within a BOutlineListView.
 *
 * @return The outline nesting level.
 *
 * @see SetOutlineLevel(), BOutlineListView
 */
uint32
BListItem::OutlineLevel() const
{
	return fLevel;
}


/**
 * @brief Sets the item's nesting depth in an outline list.
 *
 * @param level The new outline nesting level (0 for top-level).
 *
 * @see OutlineLevel(), BOutlineListView
 */
void
BListItem::SetOutlineLevel(uint32 level)
{
	fLevel = level;
}


/**
 * @brief Returns whether the item has any subitems registered with the outline list.
 *
 * @return true if there is at least one subitem, false otherwise.
 *
 * @see BOutlineListView
 */
bool
BListItem::HasSubitems() const
{
	return fHasSubitems;
}


void BListItem::_ReservedListItem1() {}
void BListItem::_ReservedListItem2() {}


/**
 * @brief Returns whether the item is currently visible in the list.
 *
 * Items can be hidden when their parent outline item is collapsed.
 *
 * @return true if the item is visible, false if it is hidden.
 *
 * @see SetItemVisible(), BOutlineListView
 */
bool
BListItem::IsItemVisible() const
{
	return fVisible;
}


/**
 * @brief Sets the top coordinate of the item within the list view.
 *
 * This is an internal bookkeeping method used by BListView and
 * BOutlineListView to track each item's vertical position during layout.
 *
 * @param top The y-coordinate of the item's top edge in the list view's coordinate system.
 */
void
BListItem::SetTop(float top)
{
	fTop = top;
}


/**
 * @brief Shows or hides the item within the list view.
 *
 * Used internally by BOutlineListView when a parent item is expanded or
 * collapsed; not intended for direct use by application code.
 *
 * @param visible Pass true to make the item visible, false to hide it.
 *
 * @see IsItemVisible(), BOutlineListView
 */
void
BListItem::SetItemVisible(bool visible)
{
	fVisible = visible;
}
