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
 *   Copyright 2006-2010 Ingo Weinhold. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 */


/**
 * @file TwoDimensionalLayout.cpp
 * @brief Implementation of BTwoDimensionalLayout, the two-axis layout engine
 *
 * BTwoDimensionalLayout extends BLayout with row/column constraint solving. It
 * computes row heights and column widths from item size constraints and feeds
 * the results back to concrete subclasses (BGridLayout, BGroupLayout) for
 * final item placement.
 *
 * @see BGridLayout, BGroupLayout, BLayoutItem
 */


#include <TwoDimensionalLayout.h>

#include <stdio.h>

#include <ControlLook.h>
#include <LayoutContext.h>
#include <LayoutItem.h>
#include <LayoutUtils.h>
#include <List.h>
#include <Message.h>
#include <View.h>

#include <Referenceable.h>

#include "CollapsingLayouter.h"


// Some words of explanation:
//
// This class is the base class for BLayouts that organize their items
// on a grid, with each item covering one or more grid cells (always a
// rectangular area). The derived classes only need to implement the
// hooks reporting the constraints for the items and additional constraints
// for the rows and columns. This class does all the layouting.
//
// The basic idea of the layout process is simple. The horizontal and the
// vertical dimensions are laid out independently and the items are set to the
// resulting locations and sizes. The "height for width" feature makes the
// height depend on the width, which makes things a bit more complicated.
// The horizontal dimension must be laid out first and and the results are
// fed into the vertical layout process.
//
// The AlignLayoutWith() feature, which allows to align layouts for different
// views with each other, causes the need for the three inner *Layouter classes.
// For each set of layouts aligned with each other with respect to one
// dimension that dimension must be laid out together. The class responsible
// is CompoundLayouter; one instance exists per such set. The derived class
// VerticalCompoundLayouter is a specialization for the vertical dimension
// which additionally takes care of the "height for width" feature. Per layout
// a single LocalLayouter exists, which comprises the required glue layout
// code and serves as a proxy for the layout, providing service methods
// needed by the CompoundLayouter.

// TODO: Check for memory leaks!

//#define DEBUG_LAYOUT

// CompoundLayouter
class BTwoDimensionalLayout::CompoundLayouter : public BReferenceable {
public:
								CompoundLayouter(orientation orientation);
	virtual						~CompoundLayouter();

			orientation			Orientation();

	virtual	Layouter*			GetLayouter(bool minMax);

			LayoutInfo*			GetLayoutInfo();

			void				AddLocalLayouter(LocalLayouter* localLayouter);
			void				RemoveLocalLayouter(
									LocalLayouter* localLayouter);

			status_t			AddAlignedLayoutsToArchive(BArchiver* archiver,
									LocalLayouter* requestedBy);

			void				AbsorbCompoundLayouter(CompoundLayouter* other);

	virtual	void				InvalidateLayout();
			bool				IsMinMaxValid();
			void				ValidateMinMax();
			void				Layout(float size, LocalLayouter* localLayouter,
									BLayoutContext* context);

protected:
	virtual	void				DoLayout(float size,
									LocalLayouter* localLayouter,
									BLayoutContext* context);

			Layouter*			fLayouter;
			LayoutInfo*			fLayoutInfo;
			orientation			fOrientation;
			BList				fLocalLayouters;
			BLayoutContext*		fLayoutContext;
			float				fLastLayoutSize;

			void				_PrepareItems();

			int32				_CountElements();
			bool				_HasMultiElementItems();

			void				_AddConstraints(Layouter* layouter);

			float				_Spacing();
};

// VerticalCompoundLayouter
class BTwoDimensionalLayout::VerticalCompoundLayouter
	: public CompoundLayouter, private BLayoutContextListener {
public:
								VerticalCompoundLayouter();

	virtual	Layouter*			GetLayouter(bool minMax);

	virtual	void				InvalidateLayout();

			void				InvalidateHeightForWidth();

			void				InternalGetHeightForWidth(
									LocalLayouter* localLayouter,
									BLayoutContext* context,
									bool realLayout, float* minHeight,
									float* maxHeight, float* preferredHeight);

protected:
	virtual	void				DoLayout(float size,
									LocalLayouter* localLayouter,
									BLayoutContext* context);

private:
			Layouter*			fHeightForWidthLayouter;
			float				fCachedMinHeightForWidth;
			float				fCachedMaxHeightForWidth;
			float				fCachedPreferredHeightForWidth;
			BLayoutContext*		fHeightForWidthLayoutContext;

			bool				_HasHeightForWidth();

			bool				_SetHeightForWidthLayoutContext(
									BLayoutContext* context);

	// BLayoutContextListener
	virtual	void				LayoutContextLeft(BLayoutContext* context);
};

// LocalLayouter
class BTwoDimensionalLayout::LocalLayouter : private BLayoutContextListener {
public:
								LocalLayouter(BTwoDimensionalLayout* layout);
								~LocalLayouter();

	// interface for the BTwoDimensionalLayout class

			BSize				MinSize();
			BSize				MaxSize();
			BSize				PreferredSize();

			void				InvalidateLayout();
			void				Layout(BSize size);

			BRect				ItemFrame(Dimensions itemDimensions);

			void				ValidateMinMax();

			void				DoHorizontalLayout(float width);

			void				InternalGetHeightForWidth(float width,
									float* minHeight, float* maxHeight,
									float* preferredHeight);

			void				AlignWith(LocalLayouter* other,
									orientation orientation);

	// Archiving stuff
			status_t			AddAlignedLayoutsToArchive(BArchiver* archiver);
			status_t			AddOwnerToArchive(BArchiver* archiver,
									CompoundLayouter* requestedBy,
									bool& _wasAvailable);
			status_t			AlignLayoutsFromArchive(BUnarchiver* unarchiver,
									orientation posture);


	// interface for the compound layout context

			void				PrepareItems(
									CompoundLayouter* compoundLayouter);
			int32				CountElements(
									CompoundLayouter* compoundLayouter);
			bool				HasMultiElementItems(
									CompoundLayouter* compoundLayouter);

			void				AddConstraints(
									CompoundLayouter* compoundLayouter,
									Layouter* layouter);

			float				Spacing(CompoundLayouter* compoundLayouter);

			bool				HasHeightForWidth();

			bool				AddHeightForWidthConstraints(
									VerticalCompoundLayouter* compoundLayouter,
									Layouter* layouter,
									BLayoutContext* context);
			void				SetHeightForWidthConstraintsAdded(bool added);

			void				SetCompoundLayouter(
									CompoundLayouter* compoundLayouter,
									orientation orientation);

			void				InternalInvalidateLayout(
									CompoundLayouter* compoundLayouter);

	// implementation private
private:
			BTwoDimensionalLayout* fLayout;
			CompoundLayouter*	fHLayouter;
			VerticalCompoundLayouter* fVLayouter;
			BList				fHeightForWidthItems;

	// active layout context when doing last horizontal layout
			BLayoutContext*		fHorizontalLayoutContext;
			float				fHorizontalLayoutWidth;
			bool				fHeightForWidthConstraintsAdded;

			void				_SetHorizontalLayoutContext(
									BLayoutContext* context, float width);

	// BLayoutContextListener
	virtual	void				LayoutContextLeft(BLayoutContext* context);
};


// #pragma mark -

// archiving constants
namespace {
	/** @brief Archive field listing layouts horizontally aligned with this one. */
	const char* const kHAlignedLayoutField = "BTwoDimensionalLayout:"
		"halignedlayout";
	/** @brief Archive field listing layouts vertically aligned with this one. */
	const char* const kVAlignedLayoutField = "BTwoDimensionalLayout:"
		"valignedlayout";
	/** @brief Archive field storing the four border insets as a BRect. */
	const char* const kInsetsField = "BTwoDimensionalLayout:insets";
	/** @brief Archive field storing horizontal then vertical spacing (two floats). */
	const char* const kSpacingField = "BTwoDimensionalLayout:spacing";
		// kSpacingField = {fHSpacing, fVSpacing}
}


/**
 * @brief Default constructor — creates a layout with zero insets and spacing.
 *
 * Allocates the internal LocalLayouter which, in turn, creates a
 * CompoundLayouter for each axis. Subclasses should set insets and spacing
 * after construction.
 */
BTwoDimensionalLayout::BTwoDimensionalLayout()
	:
	fLeftInset(0),
	fRightInset(0),
	fTopInset(0),
	fBottomInset(0),
	fHSpacing(0),
	fVSpacing(0),
	fLocalLayouter(new LocalLayouter(this))
{
}


/**
 * @brief Unarchiving constructor — restores insets and spacing from a BMessage.
 *
 * Reads insets (stored as a BRect) and horizontal/vertical spacing from the
 * archive produced by Archive(). Aligned layouts are reconnected separately in
 * AllUnarchived().
 *
 * @param from The archive message to restore state from.
 * @see Archive()
 * @see AllUnarchived()
 */
BTwoDimensionalLayout::BTwoDimensionalLayout(BMessage* from)
	:
	BAbstractLayout(from),
	fLeftInset(0),
	fRightInset(0),
	fTopInset(0),
	fBottomInset(0),
	fHSpacing(0),
	fVSpacing(0),
	fLocalLayouter(new LocalLayouter(this))
{
	BRect insets;
	from->FindRect(kInsetsField, &insets);
	SetInsets(insets.left, insets.top, insets.right, insets.bottom);

	from->FindFloat(kSpacingField, 0, &fHSpacing);
	from->FindFloat(kSpacingField, 1, &fVSpacing);
}


/**
 * @brief Destructor — releases the LocalLayouter and its CompoundLayouters.
 *
 * The LocalLayouter releases its references to the horizontal and vertical
 * CompoundLayouters, which are reference-counted and may be shared across
 * aligned layouts.
 */
BTwoDimensionalLayout::~BTwoDimensionalLayout()
{
	delete fLocalLayouter;
}


/**
 * @brief Set individual border insets and invalidate the layout.
 *
 * Each value is scaled through BControlLook::ComposeSpacing() to honour the
 * current UI scaling factor.
 *
 * @param left   Left inset.
 * @param top    Top inset.
 * @param right  Right inset.
 * @param bottom Bottom inset.
 * @see GetInsets()
 */
void
BTwoDimensionalLayout::SetInsets(float left, float top, float right,
	float bottom)
{
	fLeftInset = BControlLook::ComposeSpacing(left);
	fTopInset = BControlLook::ComposeSpacing(top);
	fRightInset = BControlLook::ComposeSpacing(right);
	fBottomInset = BControlLook::ComposeSpacing(bottom);

	InvalidateLayout();
}


/**
 * @brief Set symmetric horizontal and vertical insets and invalidate the layout.
 *
 * Left and right receive @a horizontal; top and bottom receive @a vertical.
 * Both values are scaled by BControlLook::ComposeSpacing().
 *
 * @param horizontal Inset applied to the left and right edges.
 * @param vertical   Inset applied to the top and bottom edges.
 * @see GetInsets()
 */
void
BTwoDimensionalLayout::SetInsets(float horizontal, float vertical)
{
	fLeftInset = BControlLook::ComposeSpacing(horizontal);
	fRightInset = fLeftInset;

	fTopInset = BControlLook::ComposeSpacing(vertical);
	fBottomInset = fTopInset;

	InvalidateLayout();
}


/**
 * @brief Set a uniform inset on all four edges and invalidate the layout.
 *
 * The single value is scaled by BControlLook::ComposeSpacing() and applied
 * equally to left, right, top, and bottom.
 *
 * @param insets Inset to apply to all edges.
 * @see GetInsets()
 */
void
BTwoDimensionalLayout::SetInsets(float insets)
{
	fLeftInset = BControlLook::ComposeSpacing(insets);
	fRightInset = fLeftInset;
	fTopInset = fLeftInset;
	fBottomInset = fLeftInset;

	InvalidateLayout();
}


/**
 * @brief Retrieve the current border insets.
 *
 * Any output pointer may be NULL if that value is not required.
 *
 * @param left   Output: left inset, or NULL.
 * @param top    Output: top inset, or NULL.
 * @param right  Output: right inset, or NULL.
 * @param bottom Output: bottom inset, or NULL.
 * @see SetInsets()
 */
void
BTwoDimensionalLayout::GetInsets(float* left, float* top, float* right,
	float* bottom) const
{
	if (left)
		*left = fLeftInset;
	if (top)
		*top = fTopInset;
	if (right)
		*right = fRightInset;
	if (bottom)
		*bottom = fBottomInset;
}


void
BTwoDimensionalLayout::AlignLayoutWith(BTwoDimensionalLayout* other,
	orientation orientation)
{
	if (!other || other == this)
		return;

	fLocalLayouter->AlignWith(other->fLocalLayouter, orientation);

	InvalidateLayout();
}


/**
 * @brief Compute and return the minimum size of the layout including insets.
 * @return Minimum BSize after adding border insets to the inner minimum.
 * @see BaseMaxSize()
 * @see BasePreferredSize()
 */
BSize
BTwoDimensionalLayout::BaseMinSize()
{
	_ValidateMinMax();
	return AddInsets(fLocalLayouter->MinSize());
}


/**
 * @brief Compute and return the maximum size of the layout including insets.
 * @return Maximum BSize after adding border insets to the inner maximum.
 * @see BaseMinSize()
 */
BSize
BTwoDimensionalLayout::BaseMaxSize()
{
	_ValidateMinMax();
	return AddInsets(fLocalLayouter->MaxSize());
}


/**
 * @brief Compute and return the preferred size of the layout including insets.
 * @return Preferred BSize after adding border insets to the inner preferred size.
 * @see BaseMinSize()
 */
BSize
BTwoDimensionalLayout::BasePreferredSize()
{
	_ValidateMinMax();
	return AddInsets(fLocalLayouter->PreferredSize());
}


/**
 * @brief Return the layout alignment (delegates to BAbstractLayout).
 * @return Default BAlignment from BAbstractLayout::BaseAlignment().
 */
BAlignment
BTwoDimensionalLayout::BaseAlignment()
{
	return BAbstractLayout::BaseAlignment();
}


/**
 * @brief Return whether any item has a height-for-width dependency.
 *
 * Validates min/max before querying the LocalLayouter.
 *
 * @return true if at least one item reports height-for-width sensitivity.
 * @see GetHeightForWidth()
 */
bool
BTwoDimensionalLayout::HasHeightForWidth()
{
	_ValidateMinMax();
	return fLocalLayouter->HasHeightForWidth();
}


/**
 * @brief Query the height range for a given available width.
 *
 * Subtracts horizontal insets from @a width, delegates to the LocalLayouter's
 * height-for-width solver, then adds vertical insets to the results. Any output
 * pointer may be NULL.
 *
 * @param width     Available total width in pixels (including insets).
 * @param min       Output: minimum total height, or NULL.
 * @param max       Output: maximum total height, or NULL.
 * @param preferred Output: preferred total height, or NULL.
 * @see HasHeightForWidth()
 */
void
BTwoDimensionalLayout::GetHeightForWidth(float width, float* min, float* max,
	float* preferred)
{
	if (!HasHeightForWidth())
		return;

	float outerSpacing = fLeftInset + fRightInset - 1;
	fLocalLayouter->InternalGetHeightForWidth(BLayoutUtils::SubtractDistances(
		width, outerSpacing), min, max, preferred);
	AddInsets(min, max, preferred);
}


void
BTwoDimensionalLayout::SetFrame(BRect frame)
{
	BAbstractLayout::SetFrame(frame);
}


/**
 * @brief Archive the layout's insets and spacing into a BMessage.
 *
 * Aligned-layout relationships are stored separately in AllArchived().
 *
 * @param into The message to archive into.
 * @param deep If true, child objects are archived as well.
 * @return B_OK on success, or a negative error code.
 * @see AllArchived()
 * @see Instantiate()
 */
status_t
BTwoDimensionalLayout::Archive(BMessage* into, bool deep) const
{
	BArchiver archiver(into);
	status_t err = BAbstractLayout::Archive(into, deep);

	if (err == B_OK) {
		BRect insets(fLeftInset, fTopInset, fRightInset, fBottomInset);
		err = into->AddRect(kInsetsField, insets);
	}

	if (err == B_OK)
		err = into->AddFloat(kSpacingField, fHSpacing);

	if (err == B_OK)
		err = into->AddFloat(kSpacingField, fVSpacing);

	return archiver.Finish(err);
}


/**
 * @brief Store aligned-layout cross-references after all objects are archived.
 *
 * For each CompoundLayouter that this layout owns (i.e. is the primary member
 * of), records all other layouts aligned with it so that AlignLayoutWith()
 * can be reconstructed during unarchiving.
 *
 * @param into The archive message being finalised.
 * @return B_OK on success, or a negative error code.
 * @see AllUnarchived()
 */
status_t
BTwoDimensionalLayout::AllArchived(BMessage* into) const
{
	BArchiver archiver(into);

	status_t err = BLayout::AllArchived(into);
	if (err == B_OK)
		err = fLocalLayouter->AddAlignedLayoutsToArchive(&archiver);
	return err;
}


/**
 * @brief Reconnect aligned-layout relationships after all objects are
 *        unarchived.
 *
 * Reads the horizontal and vertical aligned-layout fields written by
 * AllArchived() and calls AlignLayoutWith() on each referenced layout.
 *
 * @param from The archive message to read from.
 * @return B_OK on success, or a negative error code.
 * @see AllArchived()
 */
status_t
BTwoDimensionalLayout::AllUnarchived(const BMessage* from)
{
	status_t err = BLayout::AllUnarchived(from);
	if (err != B_OK)
		return err;

	BUnarchiver unarchiver(from);
	err = fLocalLayouter->AlignLayoutsFromArchive(&unarchiver, B_HORIZONTAL);
	if (err == B_OK)
		err = fLocalLayouter->AlignLayoutsFromArchive(&unarchiver, B_VERTICAL);

	return err;
}


status_t
BTwoDimensionalLayout::ItemArchived(BMessage* into, BLayoutItem* item,
	int32 index) const
{
	return BAbstractLayout::ItemArchived(into, item, index);
}


status_t
BTwoDimensionalLayout::ItemUnarchived(const BMessage* from, BLayoutItem* item,
	int32 index)
{
	return BAbstractLayout::ItemUnarchived(from, item, index);
}




/**
 * @brief Propagate a layout invalidation to the LocalLayouter.
 *
 * Instructs the LocalLayouter to invalidate both its horizontal and vertical
 * CompoundLayouters so the constraint-solver state is rebuilt on the next
 * layout pass.
 *
 * @param children Unused; forwarded by the BLayout infrastructure.
 */
void
BTwoDimensionalLayout::LayoutInvalidated(bool children)
{
	fLocalLayouter->InvalidateLayout();
}


/**
 * @brief Perform the complete two-dimensional layout pass.
 *
 * Validates min/max constraints, runs the horizontal and vertical layouters via
 * the LocalLayouter, then iterates over all visible items to set their final
 * frames using GetItemDimensions() and AlignInFrame().
 *
 * @note Called by the BLayout infrastructure; do not call directly.
 * @see LayoutInvalidated()
 * @see GetItemDimensions()
 */
void
BTwoDimensionalLayout::DoLayout()
{
	_ValidateMinMax();

	// layout the horizontal/vertical elements
	BSize size(SubtractInsets(LayoutArea().Size()));

#ifdef DEBUG_LAYOUT
printf("BTwoDimensionalLayout::DerivedLayoutItems(): view: %p"
	" size: (%.1f, %.1f)\n", View(), size.Width(), size.Height());
#endif

	fLocalLayouter->Layout(size);

	// layout the items
	BPoint itemOffset(LayoutArea().LeftTop());
	int itemCount = CountItems();
	for (int i = 0; i < itemCount; i++) {
		BLayoutItem* item = ItemAt(i);
		if (item->IsVisible()) {
			Dimensions itemDimensions;
			GetItemDimensions(item, &itemDimensions);
			BRect frame = fLocalLayouter->ItemFrame(itemDimensions);
			frame.left += fLeftInset;
			frame.top += fTopInset;
			frame.right += fLeftInset;
			frame.bottom += fTopInset;
			frame.OffsetBy(itemOffset);
{
#ifdef DEBUG_LAYOUT
printf("  frame for item %2d (view: %p): ", i, item->View());
frame.PrintToStream();
#endif
//BSize min(item->MinSize());
//BSize max(item->MaxSize());
//printf("    min: (%.1f, %.1f), max: (%.1f, %.1f)\n", min.width, min.height,
//	max.width, max.height);
//if (item->HasHeightForWidth()) {
//float minHeight, maxHeight, preferredHeight;
//item->GetHeightForWidth(frame.Width(), &minHeight, &maxHeight,
//	&preferredHeight);
//printf("    hfw: min: %.1f, max: %.1f, pref: %.1f\n", minHeight, maxHeight,
//	preferredHeight);
//}
}

			item->AlignInFrame(frame);
		}
//else
//printf("  item %2d not visible", i);
	}
}


/**
 * @brief Add border insets to an inner BSize, returning the outer BSize.
 * @param size Inner content-area BSize.
 * @return Outer BSize with insets added to both dimensions.
 * @see SubtractInsets()
 */
BSize
BTwoDimensionalLayout::AddInsets(BSize size)
{
	size.width = BLayoutUtils::AddDistances(size.width,
		fLeftInset + fRightInset - 1);
	size.height = BLayoutUtils::AddDistances(size.height,
		fTopInset + fBottomInset - 1);
	return size;
}


/**
 * @brief Add vertical insets to a set of height values in place.
 *
 * Any of the three pointers may be NULL.
 *
 * @param minHeight       In/out: minimum height to adjust.
 * @param maxHeight       In/out: maximum height to adjust.
 * @param preferredHeight In/out: preferred height to adjust.
 */
void
BTwoDimensionalLayout::AddInsets(float* minHeight, float* maxHeight,
	float* preferredHeight)
{
	float insets = fTopInset + fBottomInset - 1;
	if (minHeight)
		*minHeight = BLayoutUtils::AddDistances(*minHeight, insets);
	if (maxHeight)
		*maxHeight = BLayoutUtils::AddDistances(*maxHeight, insets);
	if (preferredHeight)
		*preferredHeight = BLayoutUtils::AddDistances(*preferredHeight, insets);
}


/**
 * @brief Subtract border insets from an outer BSize, returning the inner BSize.
 * @param size Outer view-area BSize.
 * @return Inner content-area BSize with insets removed from both dimensions.
 * @see AddInsets()
 */
BSize
BTwoDimensionalLayout::SubtractInsets(BSize size)
{
	size.width = BLayoutUtils::SubtractDistances(size.width,
		fLeftInset + fRightInset - 1);
	size.height = BLayoutUtils::SubtractDistances(size.height,
		fTopInset + fBottomInset - 1);
	return size;
}


/**
 * @brief Hook called before constraint solving for the given axis.
 *
 * Subclasses may override this to perform per-layout-pass setup work (e.g.
 * updating cached item grid positions). The default implementation is a no-op.
 *
 * @param orientation B_HORIZONTAL or B_VERTICAL.
 */
void
BTwoDimensionalLayout::PrepareItems(orientation orientation)
{
}


/**
 * @brief Return whether any item spans more than one column.
 *
 * Overridden by subclasses (e.g. BGridLayout) that support multi-column items.
 * The default implementation returns false.
 *
 * @return false; subclasses override to return true when appropriate.
 */
bool
BTwoDimensionalLayout::HasMultiColumnItems()
{
	return false;
}


/**
 * @brief Return whether any item spans more than one row.
 *
 * Overridden by subclasses that support multi-row items. The default
 * implementation returns false.
 *
 * @return false; subclasses override to return true when appropriate.
 */
bool
BTwoDimensionalLayout::HasMultiRowItems()
{
	return false;
}


/**
 * @brief Ensure the LocalLayouter's min/max state is current.
 *
 * Delegates to LocalLayouter::ValidateMinMax(), which rebuilds both the
 * horizontal and vertical CompoundLayouters if they have been invalidated.
 */
void
BTwoDimensionalLayout::_ValidateMinMax()
{
	fLocalLayouter->ValidateMinMax();
}


// #pragma mark - CompoundLayouter


/**
 * @brief Construct a CompoundLayouter for the given axis.
 *
 * The layouter starts with no Layouter object and no LocalLayouters. They are
 * added via AddLocalLayouter() as layouts join the alignment group.
 *
 * @param orientation B_HORIZONTAL or B_VERTICAL.
 */
BTwoDimensionalLayout::CompoundLayouter::CompoundLayouter(
	orientation orientation)
	:
	fLayouter(NULL),
	fLayoutInfo(NULL),
	fOrientation(orientation),
	fLocalLayouters(10),
	fLayoutContext(NULL),
	fLastLayoutSize(-1)
{
}


/**
 * @brief Destroy the CompoundLayouter and release the Layouter/LayoutInfo.
 */
BTwoDimensionalLayout::CompoundLayouter::~CompoundLayouter()
{
	delete fLayouter;
	delete fLayoutInfo;
}


/**
 * @brief Return the axis this CompoundLayouter operates on.
 * @return B_HORIZONTAL or B_VERTICAL.
 */
orientation
BTwoDimensionalLayout::CompoundLayouter::Orientation()
{
	return fOrientation;
}


/**
 * @brief Return the active Layouter object.
 *
 * The base implementation always returns fLayouter regardless of @a minMax.
 * VerticalCompoundLayouter overrides this to return the height-for-width
 * layouter when @a minMax is false and height-for-width is active.
 *
 * @param minMax true to request the min/max layouter; false for the real-layout
 *               layouter.
 * @return The Layouter, or NULL if not yet validated.
 */
Layouter*
BTwoDimensionalLayout::CompoundLayouter::GetLayouter(bool minMax)
{
	return fLayouter;
}


/**
 * @brief Return the LayoutInfo object for reading element positions and sizes.
 * @return The LayoutInfo, or NULL if not yet validated.
 */
LayoutInfo*
BTwoDimensionalLayout::CompoundLayouter::GetLayoutInfo()
{
	return fLayoutInfo;
}


/**
 * @brief Register a LocalLayouter as a member of this alignment group.
 *
 * Ignored if @a localLayouter is already a member. Invalidates the layout
 * when a new member is added.
 *
 * @param localLayouter The LocalLayouter to add; must not be NULL.
 */
void
BTwoDimensionalLayout::CompoundLayouter::AddLocalLayouter(
	LocalLayouter* localLayouter)
{
	if (localLayouter) {
		if (!fLocalLayouters.HasItem(localLayouter)) {
			fLocalLayouters.AddItem(localLayouter);
			InvalidateLayout();
		}
	}
}


/**
 * @brief Remove a LocalLayouter from this alignment group.
 *
 * Invalidates the layout when a member is successfully removed.
 *
 * @param localLayouter The LocalLayouter to remove.
 */
void
BTwoDimensionalLayout::CompoundLayouter::RemoveLocalLayouter(
	LocalLayouter* localLayouter)
{
	if (fLocalLayouters.RemoveItem(localLayouter))
		InvalidateLayout();
}


/**
 * @brief Archive all layouts in this alignment group except the primary owner.
 *
 * Only the LocalLayouter at index 0 (the primary) is responsible for writing
 * the list; all others are written as secondary references. This prevents
 * duplicate entries when multiple aligned layouts archive themselves.
 *
 * @param archiver    The BArchiver to write to.
 * @param requestedBy The LocalLayouter that initiated the archive call.
 * @return B_OK on success, or a negative error code.
 */
status_t
BTwoDimensionalLayout::CompoundLayouter::AddAlignedLayoutsToArchive(
	BArchiver* archiver, LocalLayouter* requestedBy)
{
	// The LocalLayouter* that really owns us is at index 0, layouts
	// at other indices are aligned to this one.
	if (requestedBy != fLocalLayouters.ItemAt(0))
		return B_OK;

	status_t err;
	for (int32 i = fLocalLayouters.CountItems() - 1; i > 0; i--) {
		LocalLayouter* layouter = (LocalLayouter*)fLocalLayouters.ItemAt(i);

		bool wasAvailable;
		err = layouter->AddOwnerToArchive(archiver, this, wasAvailable);
		if (err != B_OK && wasAvailable)
			return err;
	}
	return B_OK;
}


/**
 * @brief Merge all LocalLayouters from @a other into this CompoundLayouter.
 *
 * Used by AlignLayoutWith() to combine two previously independent alignment
 * groups. After absorption, each migrated LocalLayouter is updated to point to
 * this CompoundLayouter. No-op when @a other == this.
 *
 * @param other The CompoundLayouter to absorb; must be the same orientation.
 */
void
BTwoDimensionalLayout::CompoundLayouter::AbsorbCompoundLayouter(
	CompoundLayouter* other)
{
	if (other == this)
		return;

	int32 count = other->fLocalLayouters.CountItems();
	for (int32 i = count - 1; i >= 0; i--) {
		LocalLayouter* layouter
			= (LocalLayouter*)other->fLocalLayouters.ItemAt(i);
		AddLocalLayouter(layouter);
		layouter->SetCompoundLayouter(this, fOrientation);
	}

	InvalidateLayout();
}


/**
 * @brief Discard the cached Layouter and notify all member LocalLayouters.
 *
 * After invalidation IsMinMaxValid() returns false and ValidateMinMax() will
 * rebuild the Layouter from scratch. This is a no-op when no Layouter exists.
 */
void
BTwoDimensionalLayout::CompoundLayouter::InvalidateLayout()
{
	if (!fLayouter)
		return;

	delete fLayouter;
	delete fLayoutInfo;

	fLayouter = NULL;
	fLayoutInfo = NULL;
	fLayoutContext = NULL;

	// notify all local layouters to invalidate the respective views
	int32 count = fLocalLayouters.CountItems();
	for (int32 i = 0; i < count; i++) {
		LocalLayouter* layouter = (LocalLayouter*)fLocalLayouters.ItemAt(i);
		layouter->InternalInvalidateLayout(this);
	}
}


/**
 * @brief Return whether the Layouter is up to date.
 * @return true if the Layouter exists (constraints are valid); false otherwise.
 */
bool
BTwoDimensionalLayout::CompoundLayouter::IsMinMaxValid()
{
	return (fLayouter != NULL);
}


/**
 * @brief Build the Layouter from all member LocalLayouters' constraints.
 *
 * Creates a CollapsingLayouter, feeds item and column/row constraints from
 * every LocalLayouter, and allocates a fresh LayoutInfo. Does nothing if
 * IsMinMaxValid() already returns true.
 */
void
BTwoDimensionalLayout::CompoundLayouter::ValidateMinMax()
{
	if (IsMinMaxValid())
		return;

	fLastLayoutSize = -1;

	// create the layouter
	_PrepareItems();

	int elementCount = _CountElements();

	fLayouter = new CollapsingLayouter(elementCount, _Spacing());

	// tell the layouter about our constraints
	// TODO: We should probably ignore local layouters whose view is hidden.
	// It's a bit tricky to find out, whether the view is hidden, though, since
	// this doesn't necessarily mean only hidden relative to the parent, but
	// hidden relative to a common parent.
	_AddConstraints(fLayouter);

	fLayoutInfo = fLayouter->CreateLayoutInfo();
}


/**
 * @brief Run the layout pass for the given size and context.
 *
 * Validates the Layouter first, then calls DoLayout() if the size or context
 * has changed since the last run.
 *
 * @param size           Available size for this axis in pixels.
 * @param localLayouter  The LocalLayouter that initiated the layout call.
 * @param context        The current BLayoutContext.
 */
void
BTwoDimensionalLayout::CompoundLayouter::Layout(float size,
	LocalLayouter* localLayouter, BLayoutContext* context)
{
	ValidateMinMax();

	if (context != fLayoutContext || fLastLayoutSize != size) {
		DoLayout(size, localLayouter, context);
		fLayoutContext = context;
		fLastLayoutSize = size;
	}
}


/**
 * @brief Perform the actual layouter run for the given size.
 *
 * The base implementation simply calls fLayouter->Layout(). Overridden by
 * VerticalCompoundLayouter to incorporate height-for-width constraints.
 *
 * @param size          Available size for this axis in pixels.
 * @param localLayouter The initiating LocalLayouter.
 * @param context       The current BLayoutContext.
 */
void
BTwoDimensionalLayout::CompoundLayouter::DoLayout(float size,
	LocalLayouter* localLayouter, BLayoutContext* context)
{
	fLayouter->Layout(fLayoutInfo, size);
}


/**
 * @brief Call PrepareItems() on every registered LocalLayouter.
 *
 * Invoked at the start of ValidateMinMax() to allow subclasses to
 * refresh their internal state before constraints are read.
 */
void
BTwoDimensionalLayout::CompoundLayouter::_PrepareItems()
{
	int32 count = fLocalLayouters.CountItems();
	for (int32 i = 0; i < count; i++) {
		LocalLayouter* layouter = (LocalLayouter*)fLocalLayouters.ItemAt(i);
		layouter->PrepareItems(this);
	}
}


/**
 * @brief Return the maximum element count across all registered LocalLayouters.
 *
 * The element count is the number of columns (for B_HORIZONTAL) or rows (for
 * B_VERTICAL) in the layout grid. When multiple layouts are aligned, the
 * compound layouter uses the largest count.
 *
 * @return Maximum element count over all LocalLayouters.
 */
int32
BTwoDimensionalLayout::CompoundLayouter::_CountElements()
{
	int32 elementCount = 0;
	int32 count = fLocalLayouters.CountItems();
	for (int32 i = 0; i < count; i++) {
		LocalLayouter* layouter = (LocalLayouter*)fLocalLayouters.ItemAt(i);
		int32 layouterCount = layouter->CountElements(this);
		elementCount = max_c(elementCount, layouterCount);
	}

	return elementCount;
}


/**
 * @brief Return whether any LocalLayouter has multi-element (spanning) items.
 * @return true if at least one LocalLayouter reports multi-element items.
 */
bool
BTwoDimensionalLayout::CompoundLayouter::_HasMultiElementItems()
{
	int32 count = fLocalLayouters.CountItems();
	for (int32 i = 0; i < count; i++) {
		LocalLayouter* layouter = (LocalLayouter*)fLocalLayouters.ItemAt(i);
		if (layouter->HasMultiElementItems(this))
			return true;
	}

	return false;
}


/**
 * @brief Feed all LocalLayouters' item constraints into a Layouter object.
 *
 * Called during ValidateMinMax() so that every aligned layout contributes its
 * item min/max/preferred and weight data to the shared Layouter.
 *
 * @param layouter The Layouter to receive the constraints.
 */
void
BTwoDimensionalLayout::CompoundLayouter::_AddConstraints(Layouter* layouter)
{
	int32 count = fLocalLayouters.CountItems();
	for (int32 i = 0; i < count; i++) {
		LocalLayouter* localLayouter = (LocalLayouter*)fLocalLayouters.ItemAt(i);
		localLayouter->AddConstraints(this, layouter);
	}
}


/**
 * @brief Return the element spacing for this axis.
 *
 * Queries the first LocalLayouter (the primary member). Returns 0 when there
 * are no members.
 *
 * @return Spacing in pixels between elements on this axis.
 */
float
BTwoDimensionalLayout::CompoundLayouter::_Spacing()
{
	if (!fLocalLayouters.IsEmpty())
		return ((LocalLayouter*)fLocalLayouters.ItemAt(0))->Spacing(this);
	return 0;
}


// #pragma mark - VerticalCompoundLayouter


/**
 * @brief Construct the vertical CompoundLayouter with height-for-width support.
 *
 * Initialises the base class for B_VERTICAL and sets all height-for-width
 * cache members to their "not yet computed" sentinel values.
 */
BTwoDimensionalLayout::VerticalCompoundLayouter::VerticalCompoundLayouter()
	:
	CompoundLayouter(B_VERTICAL),
	fHeightForWidthLayouter(NULL),
	fCachedMinHeightForWidth(0),
	fCachedMaxHeightForWidth(0),
	fCachedPreferredHeightForWidth(0),
	fHeightForWidthLayoutContext(NULL)
{
}


/**
 * @brief Return the appropriate vertical Layouter object.
 *
 * When @a minMax is true or no height-for-width layouter exists, returns the
 * standard fLayouter. Otherwise returns the height-for-width clone so that
 * DoLayout() uses the correct row sizes.
 *
 * @param minMax true to get the min/max layouter; false for the real-layout one.
 * @return The appropriate Layouter for the given context.
 */
Layouter*
BTwoDimensionalLayout::VerticalCompoundLayouter::GetLayouter(bool minMax)
{
	return (minMax || !_HasHeightForWidth()
		? fLayouter : fHeightForWidthLayouter);
}


/**
 * @brief Invalidate both the base layout and the height-for-width cache.
 *
 * Extends CompoundLayouter::InvalidateLayout() by also discarding the
 * height-for-width layouter clone.
 */
void
BTwoDimensionalLayout::VerticalCompoundLayouter::InvalidateLayout()
{
	CompoundLayouter::InvalidateLayout();

	InvalidateHeightForWidth();
}


/**
 * @brief Discard the height-for-width layouter clone.
 *
 * Deletes fHeightForWidthLayouter and resets the added-constraints flags on all
 * LocalLayouters so they are re-added the next time height-for-width info is
 * requested. No-op when no clone exists.
 */
void
BTwoDimensionalLayout::VerticalCompoundLayouter::InvalidateHeightForWidth()
{
	if (fHeightForWidthLayouter != NULL) {
		delete fHeightForWidthLayouter;
		fHeightForWidthLayouter = NULL;

		// also make sure we're not reusing the old layout info
		fLastLayoutSize = -1;

		int32 count = fLocalLayouters.CountItems();
		for (int32 i = 0; i < count; i++) {
			LocalLayouter* layouter = (LocalLayouter*)fLocalLayouters.ItemAt(i);
			layouter->SetHeightForWidthConstraintsAdded(false);
		}
	}
}


/**
 * @brief Compute height-for-width values for the current horizontal layout.
 *
 * Clones the vertical layouter (or reuses an existing clone) and adds each
 * height-for-width item's constraints. When the layout context changes, or
 * when the clone does not yet exist, the clone is rebuilt from scratch.
 * Results are cached by context so repeated calls within the same layout pass
 * are cheap.
 *
 * @param localLayouter   The LocalLayouter requesting the values.
 * @param context         The current BLayoutContext.
 * @param realLayout      true when called from DoLayout() (affects caching).
 * @param minHeight       Output: minimum height, or NULL.
 * @param maxHeight       Output: maximum height, or NULL.
 * @param preferredHeight Output: preferred height, or NULL.
 */
void
BTwoDimensionalLayout::VerticalCompoundLayouter::InternalGetHeightForWidth(
	LocalLayouter* localLayouter, BLayoutContext* context, bool realLayout,
	float* minHeight, float* maxHeight, float* preferredHeight)
{
	bool updateCachedInfo = false;

	if (_SetHeightForWidthLayoutContext(context)
		|| fHeightForWidthLayouter == NULL) {
		// Either the layout context changed or we haven't initialized the
		// height for width layouter yet. We create it and init it now.

		// clone the vertical layouter
		delete fHeightForWidthLayouter;
		delete fLayoutInfo;
		fHeightForWidthLayouter = fLayouter->CloneLayouter();
		fLayoutInfo = fHeightForWidthLayouter->CreateLayoutInfo();

		// add the children's height for width constraints
		int32 count = fLocalLayouters.CountItems();
		for (int32 i = 0; i < count; i++) {
			LocalLayouter* layouter = (LocalLayouter*)fLocalLayouters.ItemAt(i);
			if (layouter->HasHeightForWidth()) {
				layouter->AddHeightForWidthConstraints(this,
					fHeightForWidthLayouter, context);
			}
		}

		updateCachedInfo = true;
	} else if (localLayouter->HasHeightForWidth()) {
		// There is a height for width layouter and it has been initialized
		// in the current layout context. So we just add the height for width
		// constraints of the calling local layouter, if they haven't been
		// added yet.
		updateCachedInfo = localLayouter->AddHeightForWidthConstraints(this,
			fHeightForWidthLayouter, context);
	}

	// update cached height for width info, if something changed
	if (updateCachedInfo) {
		// get the height for width info
		fCachedMinHeightForWidth = fHeightForWidthLayouter->MinSize();
		fCachedMaxHeightForWidth = fHeightForWidthLayouter->MaxSize();
		fCachedPreferredHeightForWidth
			= fHeightForWidthLayouter->PreferredSize();
	}

	if (minHeight)
		*minHeight = fCachedMinHeightForWidth;
	if (maxHeight)
		*maxHeight = fCachedMaxHeightForWidth;
	if (preferredHeight)
		*preferredHeight = fCachedPreferredHeightForWidth;
}


/**
 * @brief Perform the vertical layout, using height-for-width if applicable.
 *
 * When height-for-width is active, retrieves the adjusted minimum height and
 * ensures @a size is at least that large before running the layouter.
 *
 * @param size          Available height in pixels.
 * @param localLayouter The initiating LocalLayouter.
 * @param context       The current BLayoutContext.
 */
void
BTwoDimensionalLayout::VerticalCompoundLayouter::DoLayout(float size,
	LocalLayouter* localLayouter, BLayoutContext* context)
{
	Layouter* layouter;
	if (_HasHeightForWidth()) {
		float minHeight, maxHeight, preferredHeight;
		InternalGetHeightForWidth(localLayouter, context, true, &minHeight,
			&maxHeight, &preferredHeight);
		size = max_c(size, minHeight);
		layouter = fHeightForWidthLayouter;
	} else
		layouter = fLayouter;

	layouter->Layout(fLayoutInfo, size);
}


/**
 * @brief Return whether any member LocalLayouter has height-for-width items.
 * @return true if height-for-width logic must be applied during layout.
 */
bool
BTwoDimensionalLayout::VerticalCompoundLayouter::_HasHeightForWidth()
{
	int32 count = fLocalLayouters.CountItems();
	for (int32 i = 0; i < count; i++) {
		LocalLayouter* layouter = (LocalLayouter*)fLocalLayouters.ItemAt(i);
		if (layouter->HasHeightForWidth())
			return true;
	}

	return false;
}


/**
 * @brief Switch to a new BLayoutContext for height-for-width tracking.
 *
 * Registers this object as a listener on @a context so that
 * LayoutContextLeft() can reset the context pointer when the context is
 * destroyed. When there is only one LocalLayouter the context bookkeeping is
 * skipped (no cross-layout coordination is needed).
 *
 * @param context The new layout context; may be NULL.
 * @return true if the context changed and the height-for-width state was reset;
 *         false otherwise.
 */
bool
BTwoDimensionalLayout::VerticalCompoundLayouter
	::_SetHeightForWidthLayoutContext(BLayoutContext* context)
{
	if (context == fHeightForWidthLayoutContext)
		return false;

	if (fHeightForWidthLayoutContext != NULL) {
		fHeightForWidthLayoutContext->RemoveListener(this);
		fHeightForWidthLayoutContext = NULL;
	}

	// We can ignore the whole context business, if we have no more than one
	// local layouter. We use the layout context only to recognize when calls
	// of different local layouters belong to the same context.
	if (fLocalLayouters.CountItems() <= 1)
		return false;

	fHeightForWidthLayoutContext = context;

	if (fHeightForWidthLayoutContext != NULL)
		fHeightForWidthLayoutContext->AddListener(this);

	InvalidateHeightForWidth();

	return true;
}


/**
 * @brief BLayoutContextListener callback — clears the stored context pointer.
 *
 * Called when @a context is about to be destroyed so that stale pointers are
 * not retained.
 *
 * @param context The layout context that is being left.
 */
void
BTwoDimensionalLayout::VerticalCompoundLayouter::LayoutContextLeft(
	BLayoutContext* context)
{
	fHeightForWidthLayoutContext = NULL;
}


// #pragma mark - LocalLayouter


/**
 * @brief Construct a LocalLayouter for the given BTwoDimensionalLayout.
 *
 * Creates fresh horizontal CompoundLayouter and vertical
 * VerticalCompoundLayouter objects, registers this LocalLayouter as a member
 * of each, and initialises all horizontal-layout-context tracking fields.
 *
 * @param layout The owning BTwoDimensionalLayout.
 */
BTwoDimensionalLayout::LocalLayouter::LocalLayouter(
		BTwoDimensionalLayout* layout)
	:
	fLayout(layout),
	fHLayouter(new CompoundLayouter(B_HORIZONTAL)),
	fVLayouter(new VerticalCompoundLayouter),
	fHeightForWidthItems(),
	fHorizontalLayoutContext(NULL),
	fHorizontalLayoutWidth(0),
	fHeightForWidthConstraintsAdded(false)
{
	fHLayouter->AddLocalLayouter(this);
	fVLayouter->AddLocalLayouter(this);
}


/**
 * @brief Destroy the LocalLayouter and release CompoundLayouter references.
 *
 * Removes this LocalLayouter from both CompoundLayouters and releases the
 * reference-counted pointers, allowing shared CompoundLayouters to remain
 * alive while other layouts still reference them.
 */
BTwoDimensionalLayout::LocalLayouter::~LocalLayouter()
{
	if (fHLayouter != NULL) {
		fHLayouter->RemoveLocalLayouter(this);
		fHLayouter->ReleaseReference();
	}

	if (fVLayouter != NULL) {
		fVLayouter->RemoveLocalLayouter(this);
		fVLayouter->ReleaseReference();
	}
}


/**
 * @brief Return the inner (pre-inset) minimum size from both CompoundLayouters.
 * @return Inner minimum BSize.
 */
BSize
BTwoDimensionalLayout::LocalLayouter::MinSize()
{
	return BSize(fHLayouter->GetLayouter(true)->MinSize(),
		fVLayouter->GetLayouter(true)->MinSize());
}


/**
 * @brief Return the inner (pre-inset) maximum size from both CompoundLayouters.
 * @return Inner maximum BSize.
 */
BSize
BTwoDimensionalLayout::LocalLayouter::MaxSize()
{
	return BSize(fHLayouter->GetLayouter(true)->MaxSize(),
		fVLayouter->GetLayouter(true)->MaxSize());
}


/**
 * @brief Return the inner (pre-inset) preferred size from both CompoundLayouters.
 * @return Inner preferred BSize.
 */
BSize
BTwoDimensionalLayout::LocalLayouter::PreferredSize()
{
	return BSize(fHLayouter->GetLayouter(true)->PreferredSize(),
		fVLayouter->GetLayouter(true)->PreferredSize());
}


/**
 * @brief Invalidate both the horizontal and vertical CompoundLayouters.
 *
 * Forces a complete rebuild of constraints and layout info on the next
 * ValidateMinMax() call.
 */
void
BTwoDimensionalLayout::LocalLayouter::InvalidateLayout()
{
	fHLayouter->InvalidateLayout();
	fVLayouter->InvalidateLayout();
}


/**
 * @brief Run both the horizontal and vertical layout for the given size.
 *
 * Performs horizontal layout first (via DoHorizontalLayout()), then runs the
 * vertical CompoundLayouter so height-for-width items receive the correct row
 * heights.
 *
 * @param size The inner content-area size to lay out into.
 */
void
BTwoDimensionalLayout::LocalLayouter::Layout(BSize size)
{
	DoHorizontalLayout(size.width);
	fVLayouter->Layout(size.height, this, fLayout->LayoutContext());
}


/**
 * @brief Compute the pixel frame for a grid item from its Dimensions.
 *
 * Queries the horizontal and vertical LayoutInfo objects to translate the
 * grid position and span into a pixel rectangle.
 *
 * @param itemDimensions The grid position and span of the item.
 * @return The item's pixel frame (relative to the content area origin).
 */
BRect
BTwoDimensionalLayout::LocalLayouter::ItemFrame(Dimensions itemDimensions)
{
	LayoutInfo* hLayoutInfo = fHLayouter->GetLayoutInfo();
	LayoutInfo* vLayoutInfo = fVLayouter->GetLayoutInfo();
	float x = hLayoutInfo->ElementLocation(itemDimensions.x);
	float y = vLayoutInfo->ElementLocation(itemDimensions.y);
	float width = hLayoutInfo->ElementRangeSize(itemDimensions.x,
		itemDimensions.width);
	float height = vLayoutInfo->ElementRangeSize(itemDimensions.y,
		itemDimensions.height);
	return BRect(x, y, x + width, y + height);
}


/**
 * @brief Ensure both CompoundLayouters have valid min/max data.
 *
 * Clears height-for-width item tracking and the horizontal layout context when
 * the horizontal layouter is invalid, then validates both CompoundLayouters and
 * resets the layout invalidation flag on the owning BTwoDimensionalLayout.
 */
void
BTwoDimensionalLayout::LocalLayouter::ValidateMinMax()
{
	if (fHLayouter->IsMinMaxValid() && fVLayouter->IsMinMaxValid())
		return;

	if (!fHLayouter->IsMinMaxValid())
		fHeightForWidthItems.MakeEmpty();

	_SetHorizontalLayoutContext(NULL, -1);

	fHLayouter->ValidateMinMax();
	fVLayouter->ValidateMinMax();
	fLayout->ResetLayoutInvalidation();
}


/**
 * @brief Run the horizontal layout pass for the given width, if needed.
 *
 * Skips the pass when the layout context and width are unchanged since the last
 * call. When a new horizontal layout is performed the vertical
 * CompoundLayouter's height-for-width cache is invalidated.
 *
 * @param width Available inner width in pixels.
 */
void
BTwoDimensionalLayout::LocalLayouter::DoHorizontalLayout(float width)
{
	BLayoutContext* context = fLayout->LayoutContext();
	if (fHorizontalLayoutContext != context
			|| width != fHorizontalLayoutWidth) {
		_SetHorizontalLayoutContext(context, width);
		fHLayouter->Layout(width, this, context);
		fVLayouter->InvalidateHeightForWidth();
	}
}


/**
 * @brief Compute the height range for the given inner width.
 *
 * Ensures a horizontal layout has been performed for @a width, then queries
 * the vertical CompoundLayouter for the resulting height-for-width range.
 *
 * @param width         Inner width in pixels.
 * @param minHeight     Output: minimum inner height, or NULL.
 * @param maxHeight     Output: maximum inner height, or NULL.
 * @param preferredHeight Output: preferred inner height, or NULL.
 */
void
BTwoDimensionalLayout::LocalLayouter::InternalGetHeightForWidth(float width,
	float* minHeight, float* maxHeight, float* preferredHeight)
{
	DoHorizontalLayout(width);
	fVLayouter->InternalGetHeightForWidth(this, fHorizontalLayoutContext, false,
		minHeight, maxHeight, preferredHeight);
}


/**
 * @brief Merge this layout's CompoundLayouters with those of @a other.
 *
 * After alignment the two layouts share the same CompoundLayouter for the
 * requested axis, so their rows (or columns) are always sized identically.
 *
 * @param other       The LocalLayouter to align with.
 * @param orientation B_HORIZONTAL to align columns; B_VERTICAL to align rows.
 */
void
BTwoDimensionalLayout::LocalLayouter::AlignWith(LocalLayouter* other,
	orientation orientation)
{
	if (orientation == B_HORIZONTAL)
		other->fHLayouter->AbsorbCompoundLayouter(fHLayouter);
	else
		other->fVLayouter->AbsorbCompoundLayouter(fVLayouter);
}


/**
 * @brief Archive the aligned-layout lists for both axes.
 *
 * Calls AddAlignedLayoutsToArchive() on both the horizontal and vertical
 * CompoundLayouters so that AlignLayoutWith() relationships are preserved.
 *
 * @param archiver The BArchiver to write to.
 * @return B_OK on success, or a negative error code.
 */
status_t
BTwoDimensionalLayout::LocalLayouter::AddAlignedLayoutsToArchive(
	BArchiver* archiver)
{
	status_t err = fHLayouter->AddAlignedLayoutsToArchive(archiver, this);

	if (err == B_OK)
		err = fVLayouter->AddAlignedLayoutsToArchive(archiver, this);

	return err;
}


/**
 * @brief Add the owning BTwoDimensionalLayout to the archive as an aligned peer.
 *
 * Writes the owner to the appropriate alignment field (horizontal or vertical)
 * so it can be reconnected during AllUnarchived().
 *
 * @param archiver        The BArchiver to write to.
 * @param requestedBy     The CompoundLayouter that is requesting the archive.
 * @param _wasAvailable   Output: set to true if the layout was already archived.
 * @return B_OK on success, or B_NAME_NOT_FOUND if the layout is not archived.
 */
status_t
BTwoDimensionalLayout::LocalLayouter::AddOwnerToArchive(BArchiver* archiver,
	CompoundLayouter* requestedBy, bool& _wasAvailable)
{
	const char* field = kHAlignedLayoutField;
	if (requestedBy == fVLayouter)
		field = kVAlignedLayoutField;

	if ((_wasAvailable = archiver->IsArchived(fLayout)))
		return archiver->AddArchivable(field, fLayout);

	return B_NAME_NOT_FOUND;
}


/**
 * @brief Restore AlignLayoutWith() relationships from the archive for one axis.
 *
 * Reads the aligned-layout list for @a posture from the archive and calls
 * AlignLayoutWith() on each found layout. Returns B_OK when the field is not
 * present (no aligned layouts were archived for that axis).
 *
 * @param unarchiver The BUnarchiver to read from.
 * @param posture    B_HORIZONTAL or B_VERTICAL.
 * @return B_OK on success, or a negative error code.
 */
status_t
BTwoDimensionalLayout::LocalLayouter::AlignLayoutsFromArchive(
	BUnarchiver* unarchiver, orientation posture)
{
	const char* field = kHAlignedLayoutField;
	if (posture == B_VERTICAL)
		field = kVAlignedLayoutField;

	int32 count;
	status_t err = unarchiver->ArchiveMessage()->GetInfo(field, NULL, &count);
	if (err == B_NAME_NOT_FOUND)
		return B_OK;

	BTwoDimensionalLayout* retriever;
	for (int32 i = 0; i < count && err == B_OK; i++) {
		err = unarchiver->FindObject(field, i,
			BUnarchiver::B_DONT_ASSUME_OWNERSHIP, retriever);

		if (err == B_OK)
			retriever->AlignLayoutWith(fLayout, posture);
	}

	return err;
}


/**
 * @brief Delegate the PrepareItems() call to the owning layout.
 *
 * Called by CompoundLayouter::_PrepareItems() so the concrete subclass can
 * refresh any cached grid state before constraints are read.
 *
 * @param compoundLayouter The CompoundLayouter that initiated the call.
 */
void
BTwoDimensionalLayout::LocalLayouter::PrepareItems(
	CompoundLayouter* compoundLayouter)
{
	fLayout->PrepareItems(compoundLayouter->Orientation());
}


/**
 * @brief Return the number of grid elements (columns or rows) for this axis.
 * @param compoundLayouter The CompoundLayouter specifying the axis.
 * @return Column count for B_HORIZONTAL, row count for B_VERTICAL.
 */
int32
BTwoDimensionalLayout::LocalLayouter::CountElements(
	CompoundLayouter* compoundLayouter)
{
	if (compoundLayouter->Orientation() == B_HORIZONTAL)
		return fLayout->InternalCountColumns();
	else
		return fLayout->InternalCountRows();
}


/**
 * @brief Return whether any item spans multiple elements on this axis.
 * @param compoundLayouter The CompoundLayouter specifying the axis.
 * @return true if a spanning item exists on the given axis.
 */
bool
BTwoDimensionalLayout::LocalLayouter::HasMultiElementItems(
	CompoundLayouter* compoundLayouter)
{
	if (compoundLayouter->Orientation() == B_HORIZONTAL)
		return fLayout->HasMultiColumnItems();
	else
		return fLayout->HasMultiRowItems();
}


/**
 * @brief Feed all item and column/row constraints into a Layouter.
 *
 * Iterates over all visible items, translates their min/max/preferred into
 * layouter constraints on the appropriate axis, and collects height-for-width
 * items for later processing. Also applies per-column/row weight and min/max
 * from GetColumnRowConstraints().
 *
 * @param compoundLayouter The CompoundLayouter specifying the axis.
 * @param layouter         The Layouter to receive the constraints.
 */
void
BTwoDimensionalLayout::LocalLayouter::AddConstraints(
	CompoundLayouter* compoundLayouter, Layouter* layouter)
{
	enum orientation orientation = compoundLayouter->Orientation();
	int itemCount = fLayout->CountItems();
	if (itemCount > 0) {
		for (int i = 0; i < itemCount; i++) {
			BLayoutItem* item = fLayout->ItemAt(i);
			if (item->IsVisible()) {
				Dimensions itemDimensions;
				fLayout->GetItemDimensions(item, &itemDimensions);

				BSize min = item->MinSize();
				BSize max = item->MaxSize();
				BSize preferred = item->PreferredSize();

				if (orientation == B_HORIZONTAL) {
					layouter->AddConstraints(
						itemDimensions.x,
						itemDimensions.width,
						min.width,
						max.width,
						preferred.width);

					if (item->HasHeightForWidth())
						fHeightForWidthItems.AddItem(item);

				} else {
					layouter->AddConstraints(
						itemDimensions.y,
						itemDimensions.height,
						min.height,
						max.height,
						preferred.height);
				}
			}
		}

		// add column/row constraints
		ColumnRowConstraints constraints;
		int elementCount = CountElements(compoundLayouter);
		for (int element = 0; element < elementCount; element++) {
			fLayout->GetColumnRowConstraints(orientation, element,
				&constraints);
			layouter->SetWeight(element, constraints.weight);
			layouter->AddConstraints(element, 1, constraints.min,
				constraints.max, constraints.min);
		}
	}
}


/**
 * @brief Return the inter-element spacing for the given axis.
 * @param compoundLayouter The CompoundLayouter specifying the axis.
 * @return Horizontal spacing for B_HORIZONTAL; vertical spacing for B_VERTICAL.
 */
float
BTwoDimensionalLayout::LocalLayouter::Spacing(
	CompoundLayouter* compoundLayouter)
{
	return (compoundLayouter->Orientation() == B_HORIZONTAL
		? fLayout->fHSpacing : fLayout->fVSpacing);
}


/**
 * @brief Return whether any item in the owner layout is height-for-width.
 * @return true if fHeightForWidthItems is not empty.
 */
bool
BTwoDimensionalLayout::LocalLayouter::HasHeightForWidth()
{
	return !fHeightForWidthItems.IsEmpty();
}


/**
 * @brief Add this layout's height-for-width constraints to a vertical Layouter.
 *
 * Iterates over all height-for-width items and calls GetHeightForWidth() on
 * each, adding the resulting constraints to @a layouter. Skips items not in
 * the current horizontal layout context, and is idempotent within a single
 * layout context (constraints are added only once per context).
 *
 * @param compoundLayouter The VerticalCompoundLayouter managing the group.
 * @param layouter         The Layouter clone to add constraints to.
 * @param context          The current BLayoutContext.
 * @return true if constraints were added; false if already added or context
 *         mismatch.
 */
bool
BTwoDimensionalLayout::LocalLayouter::AddHeightForWidthConstraints(
	VerticalCompoundLayouter* compoundLayouter, Layouter* layouter,
	BLayoutContext* context)
{
	if (context != fHorizontalLayoutContext)
		return false;

	if (fHeightForWidthConstraintsAdded)
		return false;

	LayoutInfo* hLayoutInfo = fHLayouter->GetLayoutInfo();

	// add the children's height for width constraints
	int32 itemCount = fHeightForWidthItems.CountItems();
	for (int32 i = 0; i < itemCount; i++) {
		BLayoutItem* item = (BLayoutItem*)fHeightForWidthItems.ItemAt(i);
		Dimensions itemDimensions;
		fLayout->GetItemDimensions(item, &itemDimensions);

		float minHeight, maxHeight, preferredHeight;
		item->GetHeightForWidth(
			hLayoutInfo->ElementRangeSize(itemDimensions.x,
				itemDimensions.width),
			&minHeight, &maxHeight, &preferredHeight);
		layouter->AddConstraints(
			itemDimensions.y,
			itemDimensions.height,
			minHeight,
			maxHeight,
			preferredHeight);
	}

	SetHeightForWidthConstraintsAdded(true);

	return true;
}


/**
 * @brief Record whether height-for-width constraints have been added.
 *
 * Called by AddHeightForWidthConstraints() after successful addition, and by
 * VerticalCompoundLayouter::InvalidateHeightForWidth() to reset the flag.
 *
 * @param added true to mark constraints as added; false to clear the flag.
 */
void
BTwoDimensionalLayout::LocalLayouter::SetHeightForWidthConstraintsAdded(
	bool added)
{
	fHeightForWidthConstraintsAdded = added;
}


/**
 * @brief Replace one of this LocalLayouter's CompoundLayouters.
 *
 * Used by AbsorbCompoundLayouter() to migrate a LocalLayouter into a new
 * alignment group. Removes the reference from the old CompoundLayouter and
 * acquires one on the new one, then invalidates the layout.
 *
 * @param compoundLayouter The new CompoundLayouter to use.
 * @param orientation      B_HORIZONTAL or B_VERTICAL.
 */
void
BTwoDimensionalLayout::LocalLayouter::SetCompoundLayouter(
	CompoundLayouter* compoundLayouter, orientation orientation)
{
	CompoundLayouter* oldCompoundLayouter;
	if (orientation == B_HORIZONTAL) {
		oldCompoundLayouter = fHLayouter;
		fHLayouter = compoundLayouter;
	} else {
		oldCompoundLayouter = fVLayouter;
		fVLayouter = static_cast<VerticalCompoundLayouter*>(compoundLayouter);
	}

	if (compoundLayouter == oldCompoundLayouter)
		return;

	if (oldCompoundLayouter != NULL) {
		oldCompoundLayouter->RemoveLocalLayouter(this);
		oldCompoundLayouter->ReleaseReference();
	}

	if (compoundLayouter != NULL)
		compoundLayouter->AcquireReference();

	InternalInvalidateLayout(compoundLayouter);
}


/**
 * @brief Invalidate the owning layout in response to a CompoundLayouter change.
 *
 * Resets the horizontal layout context and calls BLayout::InvalidateLayout()
 * on the owner so the layout system schedules a redraw.
 *
 * @param compoundLayouter The CompoundLayouter that triggered the invalidation.
 */
void
BTwoDimensionalLayout::LocalLayouter::InternalInvalidateLayout(
	CompoundLayouter* compoundLayouter)
{
	_SetHorizontalLayoutContext(NULL, -1);

	fLayout->BLayout::InvalidateLayout();
}


/**
 * @brief Update the tracked horizontal layout context and width.
 *
 * Manages BLayoutContextListener registration so this object is notified when
 * @a context is destroyed.
 *
 * @param context The new horizontal BLayoutContext; may be NULL.
 * @param width   The width used for the horizontal layout pass.
 */
void
BTwoDimensionalLayout::LocalLayouter::_SetHorizontalLayoutContext(
	BLayoutContext* context, float width)
{
	if (context != fHorizontalLayoutContext) {
		if (fHorizontalLayoutContext != NULL)
			fHorizontalLayoutContext->RemoveListener(this);

		fHorizontalLayoutContext = context;

		if (fHorizontalLayoutContext != NULL)
			fHorizontalLayoutContext->AddListener(this);
	}

	fHorizontalLayoutWidth = width;
}


/**
 * @brief BLayoutContextListener callback — clears the horizontal context pointer.
 *
 * Called when the horizontal BLayoutContext is about to be destroyed so that
 * the stale pointer is not retained.
 *
 * @param context The layout context that is being left.
 */
void
BTwoDimensionalLayout::LocalLayouter::LayoutContextLeft(BLayoutContext* context)
{
	fHorizontalLayoutContext = NULL;
	fHorizontalLayoutWidth = -1;
}


status_t
BTwoDimensionalLayout::Perform(perform_code code, void* _data)
{
	return BAbstractLayout::Perform(code, _data);
}


void BTwoDimensionalLayout::_ReservedTwoDimensionalLayout1() {}
void BTwoDimensionalLayout::_ReservedTwoDimensionalLayout2() {}
void BTwoDimensionalLayout::_ReservedTwoDimensionalLayout3() {}
void BTwoDimensionalLayout::_ReservedTwoDimensionalLayout4() {}
void BTwoDimensionalLayout::_ReservedTwoDimensionalLayout5() {}
void BTwoDimensionalLayout::_ReservedTwoDimensionalLayout6() {}
void BTwoDimensionalLayout::_ReservedTwoDimensionalLayout7() {}
void BTwoDimensionalLayout::_ReservedTwoDimensionalLayout8() {}
void BTwoDimensionalLayout::_ReservedTwoDimensionalLayout9() {}
void BTwoDimensionalLayout::_ReservedTwoDimensionalLayout10() {}

