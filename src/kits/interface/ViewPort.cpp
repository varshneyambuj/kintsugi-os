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
 *   Copyright 2013 Ingo Weinhold. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold, ingo_weinhold@gmx.de
 */


/**
 * @file ViewPort.cpp
 * @brief Implementation of BViewPort, a scrollable viewport over a child view
 *
 * BViewPort clips a child view to a smaller visible area and attaches optional
 * BScrollBar controls. It translates scroll-bar position changes into child
 * view offsets, implementing a lightweight scrollable container without
 * BScrollView overhead.
 *
 * @see BView, BScrollBar, BAbstractLayout
 */


#include <ViewPort.h>

#include <algorithm>

#include <AbstractLayout.h>
#include <ScrollBar.h>

#include "ViewLayoutItem.h"


namespace BPrivate {


// #pragma mark - ViewPortLayout


/**
 * @brief Internal BAbstractLayout implementation that positions the single
 *        child view within the viewport and updates the attached scroll bars.
 *
 * ViewPortLayout owns the BViewLayoutItem wrapper when a BView is added via
 * SetChildView(), but does not own externally supplied BLayoutItem objects.
 * The minimum size of the viewport is reduced to -1 along any axis that has a
 * corresponding scroll bar, allowing the viewport to be smaller than its child.
 */
class BViewPort::ViewPortLayout : public BAbstractLayout {
public:
	/**
	 * @brief Construct a ViewPortLayout attached to the given BViewPort.
	 *
	 * @param viewPort  The owning BViewPort whose scroll bars are updated
	 *                  during DoLayout().
	 */
	ViewPortLayout(BViewPort* viewPort)
		:
		BAbstractLayout(),
		fViewPort(viewPort),
		fHasViewChild(false),
		fIsCacheValid(false),
		fMin(),
		fMax(),
		fPreferred()
	{
	}

	/**
	 * @brief Return the BView child currently managed by this layout, if any.
	 *
	 * @return Pointer to the child BView, or NULL if no view child is set or
	 *         the layout item is not a BViewLayoutItem.
	 */
	BView* ChildView() const
	{
		if (!fHasViewChild)
			return NULL;
		if (BViewLayoutItem* item = dynamic_cast<BViewLayoutItem*>(ItemAt(0)))
			return item->View();
		return NULL;
	}

	/**
	 * @brief Set the child view, replacing any existing child.
	 *
	 * Creates an owned BViewLayoutItem wrapper around @a view and inserts it
	 * at index 0. The wrapper is deleted when the child is unset.
	 *
	 * @param view  The BView to install as the child, or NULL to clear.
	 */
	void SetChildView(BView* view)
	{
		_UnsetChild();

		if (view != NULL && AddView(0, view) != NULL)
			fHasViewChild = true;
	}

	/**
	 * @brief Return the layout item at index 0, or NULL if there is none.
	 *
	 * @return Pointer to the child BLayoutItem, or NULL.
	 */
	BLayoutItem* ChildItem() const
	{
		return ItemAt(0);
	}

	/**
	 * @brief Set an externally owned layout item as the single child.
	 *
	 * The layout does not take ownership of the item; the caller is
	 * responsible for its lifetime.
	 *
	 * @param item  The BLayoutItem to install, or NULL to clear.
	 */
	void SetChildItem(BLayoutItem* item)
	{
		_UnsetChild();

		if (item != NULL)
			AddItem(0, item);
	}

	/**
	 * @brief Return the minimum size for the viewport layout.
	 *
	 * Along any scrollable axis the minimum is reduced to -1, allowing the
	 * viewport to shrink below the child's natural minimum.
	 *
	 * @return The computed minimum BSize.
	 */
	virtual BSize BaseMinSize()
	{
		_ValidateMinMax();
		return fMin;
	}

	/**
	 * @brief Return the maximum size for the viewport layout.
	 *
	 * Mirrors the child's maximum size without modification.
	 *
	 * @return The computed maximum BSize.
	 */
	virtual BSize BaseMaxSize()
	{
		_ValidateMinMax();
		return fMax;
	}

	/**
	 * @brief Return the preferred size for the viewport layout.
	 *
	 * Mirrors the child's preferred size without modification.
	 *
	 * @return The computed preferred BSize.
	 */
	virtual BSize BasePreferredSize()
	{
		_ValidateMinMax();
		return fPreferred;
	}

	/**
	 * @brief Return the base alignment for the viewport layout.
	 *
	 * Delegates to BAbstractLayout::BaseAlignment().
	 *
	 * @return The default BAlignment.
	 */
	virtual BAlignment BaseAlignment()
	{
		return BAbstractLayout::BaseAlignment();
	}

	/**
	 * @brief Return whether the viewport supports height-for-width layout.
	 *
	 * Currently always returns false; height-for-width is not yet implemented.
	 *
	 * @return false.
	 */
	virtual bool HasHeightForWidth()
	{
		_ValidateMinMax();
		return false;
		// TODO: Support height-for-width!
	}

	/**
	 * @brief Query height constraints for a given width.
	 *
	 * Not yet implemented; returns without filling the output parameters.
	 *
	 * @param width      The width to evaluate (unused).
	 * @param min        Minimum height output (unused).
	 * @param max        Maximum height output (unused).
	 * @param preferred  Preferred height output (unused).
	 */
	virtual void GetHeightForWidth(float width, float* min, float* max,
		float* preferred)
	{
		if (!HasHeightForWidth())
			return;

		// TODO: Support height-for-width!
	}

	/**
	 * @brief Invalidate the cached min/max/preferred size values.
	 *
	 * Called by the layout system whenever the layout is invalidated so that
	 * _ValidateMinMax() recomputes the values on the next query.
	 *
	 * @param children  Whether the invalidation was triggered by a child.
	 */
	virtual void LayoutInvalidated(bool children)
	{
		fIsCacheValid = false;
	}

	/**
	 * @brief Position the child view and update the scroll bars.
	 *
	 * Computes the child's layout size by clamping the viewport area to the
	 * child's min/max constraints, then calls AlignInFrame() and updates both
	 * horizontal and vertical scroll bars with the appropriate range and
	 * proportion values.
	 */
	virtual void DoLayout()
	{
		_ValidateMinMax();

		BLayoutItem* child = ItemAt(0);
		if (child == NULL)
			return;

		// Determine the layout area: LayoutArea() will only give us the size
		// of the view port's frame.
		BSize viewSize = LayoutArea().Size();
		BSize layoutSize = viewSize;

		BSize childMin = child->MinSize();
		BSize childMax = child->MaxSize();

		// apply the maximum constraints
		layoutSize.width = std::min(layoutSize.width, childMax.width);
		layoutSize.height = std::min(layoutSize.height, childMax.height);

		// apply the minimum constraints
		layoutSize.width = std::max(layoutSize.width, childMin.width);
		layoutSize.height = std::max(layoutSize.height, childMin.height);

		// TODO: Support height-for-width!

		child->AlignInFrame(BRect(BPoint(0, 0), layoutSize));

		_UpdateScrollBar(fViewPort->ScrollBar(B_HORIZONTAL), viewSize.width,
			layoutSize.width);
		_UpdateScrollBar(fViewPort->ScrollBar(B_VERTICAL), viewSize.height,
			layoutSize.height);
	}

private:
	/**
	 * @brief Remove and optionally delete the current child item.
	 *
	 * If a view child is installed (fHasViewChild is true) the wrapper
	 * BLayoutItem is deleted; externally supplied items are merely removed.
	 */
	void _UnsetChild()
	{
		if (CountItems() > 0) {
			BLayoutItem* item = RemoveItem((int32)0);
			if (fHasViewChild)
				delete item;
			fHasViewChild = false;
		}
	}

	/**
	 * @brief Recompute the cached min/max/preferred sizes from the child item.
	 *
	 * Reduces the minimum along scrollable axes to -1 so that the viewport
	 * can be smaller than the child. Falls back to sensible defaults when
	 * there is no child.
	 */
	void _ValidateMinMax()
	{
		if (fIsCacheValid)
			return;

		if (BLayoutItem* child = ItemAt(0)) {
			fMin = child->MinSize();
			if (_IsHorizontallyScrollable())
				fMin.width = -1;
			if (_IsVerticallyScrollable())
				fMin.height = -1;
			fMax = child->MaxSize();
			fPreferred = child->PreferredSize();
			// TODO: Support height-for-width!
		} else {
			fMin.Set(-1, -1);
			fMax.Set(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED);
			fPreferred.Set(20, 20);
		}

		fIsCacheValid = true;
	}

	/**
	 * @brief Return true if the viewport has a horizontal scroll bar attached.
	 *
	 * @return true if BViewPort::ScrollBar(B_HORIZONTAL) is non-NULL.
	 */
	bool _IsHorizontallyScrollable() const
	{
		return fViewPort->ScrollBar(B_HORIZONTAL) != NULL;
	}

	/**
	 * @brief Return true if the viewport has a vertical scroll bar attached.
	 *
	 * @return true if BViewPort::ScrollBar(B_VERTICAL) is non-NULL.
	 */
	bool _IsVerticallyScrollable() const
	{
		return fViewPort->ScrollBar(B_VERTICAL) != NULL;
	}

	/**
	 * @brief Update a scroll bar's range, proportion, and large step.
	 *
	 * When the data extent exceeds the viewport extent, the range is set to
	 * the overflow amount and the proportion reflects the visible fraction.
	 * When the data fits entirely, the range is collapsed to [0, 0] and the
	 * proportion is set to 1.
	 *
	 * @param scrollBar     The BScrollBar to update, or NULL (no-op).
	 * @param viewPortSize  The visible extent along this axis.
	 * @param dataSize      The total child extent along this axis.
	 */
	void _UpdateScrollBar(BScrollBar* scrollBar, float viewPortSize,
		float dataSize)
	{
		if (scrollBar == NULL)
			return;

		if (viewPortSize < dataSize) {
			scrollBar->SetRange(0, dataSize - viewPortSize);
			scrollBar->SetProportion(viewPortSize / dataSize);
			float smallStep;
			scrollBar->GetSteps(&smallStep, NULL);
			scrollBar->SetSteps(smallStep, viewPortSize);
		} else {
			scrollBar->SetRange(0, 0);
			scrollBar->SetProportion(1);
		}
	}

private:
	/** @brief The owning BViewPort whose scroll bars are managed by this layout. */
	BViewPort*	fViewPort;
	/** @brief True when the current child was added via SetChildView() and is owned by this layout. */
	bool		fHasViewChild;
	/** @brief True when fMin, fMax, and fPreferred reflect the current child's constraints. */
	bool		fIsCacheValid;
	/** @brief Cached minimum size, valid when fIsCacheValid is true. */
	BSize		fMin;
	/** @brief Cached maximum size, valid when fIsCacheValid is true. */
	BSize		fMax;
	/** @brief Cached preferred size, valid when fIsCacheValid is true. */
	BSize		fPreferred;
};


// #pragma mark - BViewPort


/**
 * @brief Construct a BViewPort with an anonymous name and a BView child.
 *
 * @param child  The BView to display inside the viewport, or NULL.
 * @see   SetChildView()
 */
BViewPort::BViewPort(BView* child)
	:
	BView(NULL, 0),
	fChild(NULL)
{
	_Init();
	SetChildView(child);
}


/**
 * @brief Construct a BViewPort with an anonymous name and a BLayoutItem child.
 *
 * @param child  The BLayoutItem to display inside the viewport, or NULL.
 * @see   SetChildItem()
 */
BViewPort::BViewPort(BLayoutItem* child)
	:
	BView(NULL, 0),
	fChild(NULL)
{
	_Init();
	SetChildItem(child);
}


/**
 * @brief Construct a named BViewPort with a BView child.
 *
 * @param name   The view name used for identification.
 * @param child  The BView to display inside the viewport, or NULL.
 * @see   SetChildView()
 */
BViewPort::BViewPort(const char* name, BView* child)
	:
	BView(name, 0),
	fChild(NULL)
{
	_Init();
	SetChildView(child);
}


/**
 * @brief Construct a named BViewPort with a BLayoutItem child.
 *
 * @param name   The view name used for identification.
 * @param child  The BLayoutItem to display inside the viewport, or NULL.
 * @see   SetChildItem()
 */
BViewPort::BViewPort(const char* name, BLayoutItem* child)
	:
	BView(name, 0),
	fChild(NULL)
{
	_Init();
	SetChildItem(child);
}


/**
 * @brief Destroy the BViewPort.
 *
 * The internal ViewPortLayout is owned by the BView layout system and is
 * deleted automatically; child views follow normal BView ownership rules.
 */
BViewPort::~BViewPort()
{
}


/**
 * @brief Return the BView currently displayed inside the viewport.
 *
 * @return Pointer to the child BView, or NULL if none is set or the child
 *         was added as a raw BLayoutItem.
 * @see   SetChildView()
 */
BView*
BViewPort::ChildView() const
{
	return fLayout->ChildView();
}


/**
 * @brief Replace the viewport's child with the given BView.
 *
 * The previous child (if any) is removed, and the layout is invalidated so
 * that the viewport recomputes its size and scroll-bar state.
 *
 * @param child  The new child BView, or NULL to clear the current child.
 * @see   ChildView(), SetChildItem()
 */
void
BViewPort::SetChildView(BView* child)
{
	fLayout->SetChildView(child);
	InvalidateLayout();
}


/**
 * @brief Return the BLayoutItem currently managed by the viewport.
 *
 * @return Pointer to the child BLayoutItem, or NULL if none is set.
 * @see   SetChildItem()
 */
BLayoutItem*
BViewPort::ChildItem() const
{
	return fLayout->ChildItem();
}


/**
 * @brief Replace the viewport's child with the given BLayoutItem.
 *
 * The previous child (if any) is removed, and the layout is invalidated so
 * that the viewport recomputes its size and scroll-bar state.
 *
 * @param child  The new child BLayoutItem, or NULL to clear the current child.
 * @see   ChildItem(), SetChildView()
 */
void
BViewPort::SetChildItem(BLayoutItem* child)
{
	fLayout->SetChildItem(child);
	InvalidateLayout();
}


/**
 * @brief Initialise the internal ViewPortLayout and attach it to this view.
 *
 * Called once from every constructor. Creates the ViewPortLayout and installs
 * it as the view's layout via BView::SetLayout().
 */
void
BViewPort::_Init()
{
	fLayout = new ViewPortLayout(this);
	SetLayout(fLayout);
}


}	// namespace BPrivate


using ::BPrivate::BViewPort;
