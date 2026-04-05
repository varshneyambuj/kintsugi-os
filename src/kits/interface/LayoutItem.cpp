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
 *   Copyright 2006-2012 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold, bonefish@cs.tu-berlin.de
 */


/**
 * @file LayoutItem.cpp
 * @brief Implementation of BLayoutItem, the base class for objects managed by a layout
 *
 * BLayoutItem represents a single slot in a BLayout. It carries min/max/preferred
 * size constraints and an alignment, and connects layout computation to actual
 * view positioning.
 *
 * @see BLayout, BAbstractLayoutItem
 */


#include <LayoutItem.h>

#include <Layout.h>
#include <LayoutUtils.h>
#include <View.h>
#include <ViewPrivate.h>

#include <algorithm>


/**
 * @brief Default constructor; initializes the item with no owning layout and no layout data.
 *
 * The item is not usable for layout purposes until it is added to a BLayout via
 * BLayout::AddItem() or an equivalent builder method.
 */
BLayoutItem::BLayoutItem()
	:
	fLayout(NULL),
	fLayoutData(NULL)
{
}


/**
 * @brief Unarchiving constructor; restores a BLayoutItem from a BMessage archive.
 *
 * Prepares the BUnarchiver for subsequent use and delegates base-class
 * unarchiving to BArchivable. The owning layout and layout data are not
 * persisted and start as NULL.
 *
 * @param from The archive message to restore state from.
 * @see Archive()
 */
BLayoutItem::BLayoutItem(BMessage* from)
	:
	BArchivable(BUnarchiver::PrepareArchive(from)),
	fLayout(NULL),
	fLayoutData(NULL)
{
	BUnarchiver(from).Finish();
}


/**
 * @brief Destructor.
 *
 * Triggers a debugger call if the item is still attached to a layout at
 * destruction time. Always call RemoveSelf() before deleting a BLayoutItem
 * that has been added to a layout.
 *
 * @note Failing to call RemoveSelf() before deletion is a programming error
 *       and will halt the application in a debug build.
 * @see RemoveSelf()
 */
BLayoutItem::~BLayoutItem()
{
	if (fLayout != NULL) {
		debugger("Deleting a BLayoutItem that is still attached to a layout. "
			"Call RemoveSelf first.");
	}
}


/**
 * @brief Returns the BLayout this item currently belongs to.
 *
 * @return A pointer to the owning BLayout, or @c NULL if the item has not
 *         been added to any layout.
 * @see SetLayout()
 */
BLayout*
BLayoutItem::Layout() const
{
	return fLayout;
}


/**
 * @brief Removes this item from its owning layout.
 *
 * Equivalent to calling @c Layout()->RemoveItem(this). Has no effect if the
 * item does not belong to any layout.
 *
 * @return @c true if the item was successfully removed, @c false if it had no
 *         owning layout or removal failed.
 */
bool
BLayoutItem::RemoveSelf()
{
	return Layout() != NULL && Layout()->RemoveItem(this);
}


/**
 * @brief Convenience method that sets identical min, max, and preferred sizes at once.
 *
 * Calls SetExplicitMinSize(), SetExplicitMaxSize(), and
 * SetExplicitPreferredSize() with the same @a size value, effectively fixing
 * the item to a single inflexible size.
 *
 * @param size The fixed size to assign to all three constraints.
 * @see SetExplicitMinSize(), SetExplicitMaxSize(), SetExplicitPreferredSize()
 */
void
BLayoutItem::SetExplicitSize(BSize size)
{
	SetExplicitMinSize(size);
	SetExplicitMaxSize(size);
	SetExplicitPreferredSize(size);
}


/**
 * @brief Returns whether this item supports height-for-width layout.
 *
 * Height-for-width items report a preferred height that depends on the width
 * they are actually given. The default implementation returns @c false.
 *
 * @return @c true if GetHeightForWidth() returns meaningful values, @c false
 *         otherwise.
 * @see GetHeightForWidth()
 */
bool
BLayoutItem::HasHeightForWidth()
{
	// no "height for width" by default
	return false;
}


/**
 * @brief Fills in height constraints for a given allocated width.
 *
 * Called by the layout engine when HasHeightForWidth() returns @c true.
 * The default implementation does nothing; all output pointers are left
 * unmodified.
 *
 * @param width     The width (in pixels) that will be allocated to this item.
 * @param min       If non-NULL, receives the minimum height for @a width.
 * @param max       If non-NULL, receives the maximum height for @a width.
 * @param preferred If non-NULL, receives the preferred height for @a width.
 * @see HasHeightForWidth()
 */
void
BLayoutItem::GetHeightForWidth(float width, float* min, float* max,
	float* preferred)
{
	// no "height for width" by default
}


/**
 * @brief Returns the BView associated with this layout item.
 *
 * The base implementation returns @c NULL. Concrete subclasses that wrap a
 * BView (e.g. BViewLayoutItem) override this to return the associated view.
 *
 * @return Pointer to the associated BView, or @c NULL if none.
 */
BView*
BLayoutItem::View()
{
	return NULL;
}


/**
 * @brief Invalidates cached layout information for this item and its owning layout.
 *
 * Calls the LayoutInvalidated() hook on this item, then propagates the
 * invalidation upward to the owning BLayout so that the next layout pass
 * recomputes sizes.
 *
 * @param children If @c true, child layouts are also invalidated.
 * @see LayoutInvalidated()
 */
void
BLayoutItem::InvalidateLayout(bool children)
{
	LayoutInvalidated(children);
	if (fLayout)
		fLayout->InvalidateLayout(children);
}


/**
 * @brief Requests that the item's view be repositioned without blocking.
 *
 * If @a immediate is @c false, posts a relayout request to the view's looper.
 * If @a immediate is @c true, performs the layout synchronously via
 * BView::Layout(). Has no effect if the item has no associated view.
 *
 * @param immediate If @c true, perform the relayout synchronously.
 * @see BView::Relayout(), BView::Layout()
 */
void
BLayoutItem::Relayout(bool immediate)
{
	BView* view = View();
	if (view && !immediate)
		view->Relayout();
	else if (view && immediate)
		view->Layout(false);
}


/**
 * @brief Returns the opaque per-item data pointer stored by the owning layout.
 *
 * BLayout subclasses use this slot to associate private data with each item
 * without subclassing BLayoutItem itself.
 *
 * @return The opaque data pointer, or @c NULL if none has been set.
 * @see SetLayoutData()
 */
void*
BLayoutItem::LayoutData() const
{
	return fLayoutData;
}


/**
 * @brief Stores an opaque per-item data pointer on behalf of the owning layout.
 *
 * This pointer is not interpreted by BLayoutItem; it is entirely managed by
 * the BLayout subclass that owns this item.
 *
 * @param data The opaque data pointer to store.
 * @see LayoutData()
 */
void
BLayoutItem::SetLayoutData(void* data)
{
	fLayoutData = data;
}


/**
 * @brief Positions and sizes the item within a frame, respecting its alignment and max size.
 *
 * Computes the actual frame the item should occupy inside @a frame using the
 * item's MaxSize() and Alignment(). For height-for-width items, horizontal
 * alignment is resolved first and the preferred height is used to constrain the
 * vertical extent.  Calls SetFrame() with the resulting rectangle.
 *
 * @param frame The available area offered by the layout engine.
 * @see SetFrame(), BLayoutUtils::AlignInFrame()
 */
void
BLayoutItem::AlignInFrame(BRect frame)
{
	BSize maxSize = MaxSize();
	BAlignment alignment = Alignment();

	if (HasHeightForWidth()) {
		// The item has height for width, so we do the horizontal alignment
		// ourselves and restrict the height max constraint respectively.
		if (maxSize.width < frame.Width()
			&& alignment.horizontal != B_ALIGN_USE_FULL_WIDTH) {
			frame.left += (int)((frame.Width() - maxSize.width)
				* alignment.horizontal);
			frame.right = frame.left + maxSize.width;
		}
		alignment.horizontal = B_ALIGN_USE_FULL_WIDTH;

		float minHeight;
		GetHeightForWidth(frame.Width(), &minHeight, NULL, NULL);

		frame.bottom = frame.top + max_c(frame.Height(), minHeight);
		maxSize.height = minHeight;
	}

	SetFrame(BLayoutUtils::AlignInFrame(frame, maxSize, alignment));
}


/**
 * @brief Archives this layout item into a BMessage.
 *
 * Delegates to BArchivable::Archive() and then finalizes the BArchiver.
 * Subclasses should call this base implementation and then add their own
 * fields.
 *
 * @param into The message to archive into.
 * @param deep If @c true, child objects are archived recursively.
 * @return @c B_OK on success, or an error code on failure.
 * @see BLayoutItem(BMessage*)
 */
status_t
BLayoutItem::Archive(BMessage* into, bool deep) const
{
	BArchiver archiver(into);
	status_t err = BArchivable::Archive(into, deep);

	if (err == B_OK)
		err = archiver.Finish();

	return err;
}


/**
 * @brief Hook called after the entire object graph has been archived.
 *
 * Delegates to BArchivable::AllArchived(). Subclasses may override to archive
 * cross-object references after every individual object has been archived.
 *
 * @param into The archive message being built.
 * @return @c B_OK on success, or an error code on failure.
 */
status_t
BLayoutItem::AllArchived(BMessage* into) const
{
	BArchiver archiver(into);
	return BArchivable::AllArchived(into);
}


/**
 * @brief Hook called after the entire object graph has been unarchived.
 *
 * Delegates to BArchivable::AllUnarchived(). Subclasses may override to
 * perform cross-object initialization that requires all archived objects to
 * already exist.
 *
 * @param from The original archive message.
 * @return @c B_OK on success, or an error code on failure.
 */
status_t
BLayoutItem::AllUnarchived(const BMessage* from)
{
	return BArchivable::AllUnarchived(from);
}


/**
 * @brief Sets the owning layout for this item, managing attachment hooks and view registration.
 *
 * Handles the full lifecycle transition: calls DetachedFromLayout() on the old
 * layout (if any), updates view registration with BView::Private so the view
 * knows which layout items it is associated with, and calls AttachedToLayout()
 * on the new layout.  Passing @c NULL detaches the item from its current layout.
 *
 * @param layout The new owning BLayout, or @c NULL to detach.
 * @see AttachedToLayout(), DetachedFromLayout()
 */
void
BLayoutItem::SetLayout(BLayout* layout)
{
	if (layout == fLayout)
		return;

	BLayout* oldLayout = fLayout;
	fLayout = layout;

	if (oldLayout)
		DetachedFromLayout(oldLayout);

	if (BView* view = View()) {
		if (oldLayout && !fLayout) {
			BView::Private(view).DeregisterLayoutItem(this);
		} else if (fLayout && !oldLayout) {
			BView::Private(view).RegisterLayoutItem(this);
		}
	}

	if (fLayout)
		AttachedToLayout();
}


/**
 * @brief Internal perform hook for binary-compatible virtual dispatch.
 *
 * Delegates to BArchivable::Perform(). This method is not intended for direct
 * use by application code.
 *
 * @param code The perform code identifying the operation.
 * @param _data Pointer to operation-specific argument data.
 * @return @c B_OK on success, or an error code on failure.
 */
status_t
BLayoutItem::Perform(perform_code code, void* _data)
{
	return BArchivable::Perform(code, _data);
}


/**
 * @brief Hook called when this item's cached layout information is invalidated.
 *
 * The default implementation does nothing. Subclasses may override to discard
 * any cached sizing state they maintain.
 *
 * @param children If @c true, child layouts should also be invalidated.
 */
void
BLayoutItem::LayoutInvalidated(bool children)
{
	// hook method
}


/**
 * @brief Hook called when this item is added to a BLayout.
 *
 * The default implementation does nothing. Subclasses may override to perform
 * initialization that requires a valid layout association.
 *
 * @see DetachedFromLayout()
 */
void
BLayoutItem::AttachedToLayout()
{
	// hook method
}


/**
 * @brief Hook called when this item is removed from its owning BLayout.
 *
 * The default implementation does nothing. Subclasses may override to release
 * resources that were acquired in AttachedToLayout().
 *
 * @param oldLayout The layout this item is being detached from.
 * @see AttachedToLayout()
 */
void
BLayoutItem::DetachedFromLayout(BLayout* oldLayout)
{
	// hook method
}


/**
 * @brief Hook called when an ancestor view's visibility changes.
 *
 * The default implementation does nothing. Subclasses may override to show or
 * hide content in response to an ancestor being shown or hidden, independently
 * of this item's own visibility state.
 *
 * @param shown @c true if an ancestor became visible, @c false if it was hidden.
 */
void
BLayoutItem::AncestorVisibilityChanged(bool shown)
{
	// hook method
}


// Binary compatibility stuff


void BLayoutItem::_ReservedLayoutItem1() {}
void BLayoutItem::_ReservedLayoutItem2() {}
void BLayoutItem::_ReservedLayoutItem3() {}
void BLayoutItem::_ReservedLayoutItem4() {}
void BLayoutItem::_ReservedLayoutItem5() {}
void BLayoutItem::_ReservedLayoutItem6() {}
void BLayoutItem::_ReservedLayoutItem7() {}
void BLayoutItem::_ReservedLayoutItem8() {}
void BLayoutItem::_ReservedLayoutItem9() {}
void BLayoutItem::_ReservedLayoutItem10() {}
