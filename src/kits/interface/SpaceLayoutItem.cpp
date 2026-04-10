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
 *   Copyright 2010 Haiku, Inc. / Copyright 2006 Ingo Weinhold. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold <bonefish@cs.tu-berlin.de>
 */


/**
 * @file SpaceLayoutItem.cpp
 * @brief Implementation of BSpaceLayoutItem, a flexible spacer for layout managers
 *
 * BSpaceLayoutItem is a weightable layout item that expands to fill available
 * space. The static CreateGlue() and CreateVerticalStrut()/CreateHorizontalStrut()
 * factories create common spacer types used to push or pad items within a layout.
 *
 * @see BLayout, BLayoutItem
 */


#include <SpaceLayoutItem.h>

#include <new>

#include <ControlLook.h>
#include <Message.h>


namespace {
	/** @brief Archive field name for the min, max, and preferred sizes (indexed 0–2). */
	const char* const kSizesField = "BSpaceLayoutItem:sizes";
		// kSizesField = {min, max, preferred}
	/** @brief Archive field name for the item's alignment. */
	const char* const kAlignmentField = "BSpaceLayoutItem:alignment";
	/** @brief Archive field name for the item's frame rectangle. */
	const char* const kFrameField = "BSpaceLayoutItem:frame";
	/** @brief Archive field name for the item's visibility flag. */
	const char* const kVisibleField = "BSpaceLayoutItem:visible";

	/** @brief Scales both width and height of \a size through BControlLook::ComposeSpacing() in-place.
	 *  @param size The BSize to scale; modified in place.
	 *  @return Reference to the modified \a size.
	 */
	BSize& ComposeSpacingInPlace(BSize& size)
	{
		size.width = BControlLook::ComposeSpacing(size.width);
		size.height = BControlLook::ComposeSpacing(size.height);
		return size;
	}
}


/** @brief Constructs a BSpaceLayoutItem with explicit size constraints and alignment.
 *
 *  All three size arguments are run through BControlLook::ComposeSpacing() so
 *  that the spacer respects the active UI spacing scale.
 *
 *  @param minSize       The minimum size of this spacer.
 *  @param maxSize       The maximum size of this spacer.
 *  @param preferredSize The preferred size of this spacer.
 *  @param alignment     The alignment used when the item is smaller than its cell.
 */
BSpaceLayoutItem::BSpaceLayoutItem(BSize minSize, BSize maxSize,
	BSize preferredSize, BAlignment alignment)
	:
	fFrame(),
	fMinSize(ComposeSpacingInPlace(minSize)),
	fMaxSize(ComposeSpacingInPlace(maxSize)),
	fPreferredSize(ComposeSpacingInPlace(preferredSize)),
	fAlignment(alignment),
	fVisible(true)
{
}


/** @brief Unarchiving constructor. Restores a BSpaceLayoutItem from a BMessage archive.
 *  @param archive The archive message produced by Archive().
 *  @see Archive(), Instantiate()
 */
BSpaceLayoutItem::BSpaceLayoutItem(BMessage* archive)
	:
	BLayoutItem(archive)
{
	archive->FindSize(kSizesField, 0, &fMinSize);
	archive->FindSize(kSizesField, 1, &fMaxSize);
	archive->FindSize(kSizesField, 2, &fPreferredSize);

	archive->FindAlignment(kAlignmentField, &fAlignment);

	archive->FindRect(kFrameField, &fFrame);
	archive->FindBool(kVisibleField, &fVisible);
}


/** @brief Destructor. */
BSpaceLayoutItem::~BSpaceLayoutItem()
{
}


/** @brief Creates a glue item that expands freely in both directions.
 *
 *  Glue has no minimum or preferred size and an unlimited maximum size,
 *  so it absorbs all surplus space offered by the containing layout.
 *
 *  @return A newly allocated BSpaceLayoutItem configured as glue.
 *          The caller takes ownership.
 */
BSpaceLayoutItem*
BSpaceLayoutItem::CreateGlue()
{
	return new BSpaceLayoutItem(
		BSize(-1, -1),
		BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED),
		BSize(-1, -1),
		BAlignment(B_ALIGN_HORIZONTAL_CENTER, B_ALIGN_VERTICAL_CENTER));
}


/** @brief Creates a horizontal strut with a fixed width and unlimited height.
 *
 *  Use a horizontal strut to insert a fixed horizontal gap between items in
 *  a horizontal layout while allowing the strut to grow vertically.
 *
 *  @param width The fixed width, in pixels, of the strut.
 *  @return A newly allocated BSpaceLayoutItem configured as a horizontal strut.
 *          The caller takes ownership.
 */
BSpaceLayoutItem*
BSpaceLayoutItem::CreateHorizontalStrut(float width)
{
	return new BSpaceLayoutItem(
		BSize(width, -1),
		BSize(width, B_SIZE_UNLIMITED),
		BSize(width, -1),
		BAlignment(B_ALIGN_HORIZONTAL_CENTER, B_ALIGN_VERTICAL_CENTER));
}


/** @brief Creates a vertical strut with a fixed height and unlimited width.
 *
 *  Use a vertical strut to insert a fixed vertical gap between items in a
 *  vertical layout while allowing the strut to grow horizontally.
 *
 *  @param height The fixed height, in pixels, of the strut.
 *  @return A newly allocated BSpaceLayoutItem configured as a vertical strut.
 *          The caller takes ownership.
 */
BSpaceLayoutItem*
BSpaceLayoutItem::CreateVerticalStrut(float height)
{
	return new BSpaceLayoutItem(
		BSize(-1, height),
		BSize(B_SIZE_UNLIMITED, height),
		BSize(-1, height),
		BAlignment(B_ALIGN_HORIZONTAL_CENTER, B_ALIGN_VERTICAL_CENTER));
}


/** @brief Returns the minimum size of this spacer.
 *  @return The minimum BSize as set at construction or via SetExplicitMinSize().
 */
BSize
BSpaceLayoutItem::MinSize()
{
	return fMinSize;
}


/** @brief Returns the maximum size of this spacer.
 *  @return The maximum BSize as set at construction or via SetExplicitMaxSize().
 */
BSize
BSpaceLayoutItem::MaxSize()
{
	return fMaxSize;
}


/** @brief Returns the preferred size of this spacer.
 *  @return The preferred BSize as set at construction or via SetExplicitPreferredSize().
 */
BSize
BSpaceLayoutItem::PreferredSize()
{
	return fPreferredSize;
}


/** @brief Returns the alignment of this spacer within its layout cell.
 *  @return The BAlignment as set at construction or via SetExplicitAlignment().
 */
BAlignment
BSpaceLayoutItem::Alignment()
{
	return fAlignment;
}


/** @brief Overrides the minimum size, updating only the components that are set.
 *
 *  Only the width component of \a size is applied if IsWidthSet() returns true,
 *  and similarly for height. Triggers a layout invalidation.
 *
 *  @param size The new minimum size. Unset components are left unchanged.
 */
void
BSpaceLayoutItem::SetExplicitMinSize(BSize size)
{
	if (size.IsWidthSet())
		fMinSize.width = size.width;
	if (size.IsHeightSet())
		fMinSize.height = size.height;

	InvalidateLayout();
}


/** @brief Overrides the maximum size, updating only the components that are set.
 *
 *  Only the width component of \a size is applied if IsWidthSet() returns true,
 *  and similarly for height. Triggers a layout invalidation.
 *
 *  @param size The new maximum size. Unset components are left unchanged.
 */
void
BSpaceLayoutItem::SetExplicitMaxSize(BSize size)
{
	if (size.IsWidthSet())
		fMaxSize.width = size.width;
	if (size.IsHeightSet())
		fMaxSize.height = size.height;

	InvalidateLayout();
}


/** @brief Overrides the preferred size, updating only the components that are set.
 *
 *  Only the width component of \a size is applied if IsWidthSet() returns true,
 *  and similarly for height. Triggers a layout invalidation.
 *
 *  @param size The new preferred size. Unset components are left unchanged.
 */
void
BSpaceLayoutItem::SetExplicitPreferredSize(BSize size)
{
	if (size.IsWidthSet())
		fPreferredSize.width = size.width;
	if (size.IsHeightSet())
		fPreferredSize.height = size.height;

	InvalidateLayout();
}


/** @brief Overrides the alignment, updating only the components that are set.
 *
 *  Only the horizontal component of \a alignment is applied if
 *  IsHorizontalSet() returns true, and similarly for vertical. Triggers a
 *  layout invalidation.
 *
 *  @param alignment The new alignment. Unset components are left unchanged.
 */
void
BSpaceLayoutItem::SetExplicitAlignment(BAlignment alignment)
{
	if (alignment.IsHorizontalSet())
		fAlignment.horizontal = alignment.horizontal;
	if (alignment.IsVerticalSet())
		fAlignment.vertical = alignment.vertical;

	InvalidateLayout();
}


/** @brief Returns whether this spacer is currently visible to the layout.
 *  @return true if the spacer participates in layout, false if it is hidden.
 */
bool
BSpaceLayoutItem::IsVisible()
{
	return fVisible;
}


/** @brief Sets the visibility of this spacer.
 *
 *  A hidden spacer is excluded from layout calculations and takes up no space.
 *
 *  @param visible true to make the spacer participate in layout, false to hide it.
 */
void
BSpaceLayoutItem::SetVisible(bool visible)
{
	fVisible = visible;
}


/** @brief Returns the frame rectangle most recently assigned by the layout.
 *  @return The current BRect frame of this spacer.
 */
BRect
BSpaceLayoutItem::Frame()
{
	return fFrame;
}


/** @brief Sets the frame rectangle of this spacer as assigned by the layout engine.
 *  @param frame The new frame rectangle in the parent view's coordinate system.
 */
void
BSpaceLayoutItem::SetFrame(BRect frame)
{
	fFrame = frame;
}


/** @brief Archives this BSpaceLayoutItem into the provided BMessage.
 *
 *  Stores the frame, min/max/preferred sizes, alignment, and visibility flag.
 *  Calls BLayoutItem::Archive() first to archive the base class.
 *
 *  @param into The target archive message.
 *  @param deep If true, child objects are also archived (passed to the base class).
 *  @return B_OK on success, or an error code if any field could not be added.
 */
status_t
BSpaceLayoutItem::Archive(BMessage* into, bool deep) const
{
	status_t err = BLayoutItem::Archive(into, deep);

	if (err == B_OK)
		err = into->AddRect(kFrameField, fFrame);

	if (err == B_OK)
		err = into->AddSize(kSizesField, fMinSize);

	if (err == B_OK)
		err = into->AddSize(kSizesField, fMaxSize);

	if (err == B_OK)
		err = into->AddSize(kSizesField, fPreferredSize);

	if (err == B_OK)
		err = into->AddAlignment(kAlignmentField, fAlignment);

	if (err == B_OK)
		err = into->AddBool(kVisibleField, fVisible);

	return err;
}


/** @brief Instantiates a BSpaceLayoutItem from an archive message.
 *
 *  Validates the archive class name before constructing. Returns NULL if
 *  validation fails.
 *
 *  @param from The archive message previously produced by Archive().
 *  @return A newly allocated BSpaceLayoutItem, or NULL on failure.
 *  @see Archive()
 */
BArchivable*
BSpaceLayoutItem::Instantiate(BMessage* from)
{
	if (validate_instantiation(from, "BSpaceLayoutItem"))
		return new(std::nothrow) BSpaceLayoutItem(from);
	return NULL;
}


void BSpaceLayoutItem::_ReservedSpaceLayoutItem1() {}
void BSpaceLayoutItem::_ReservedSpaceLayoutItem2() {}
void BSpaceLayoutItem::_ReservedSpaceLayoutItem3() {}
void BSpaceLayoutItem::_ReservedSpaceLayoutItem4() {}
void BSpaceLayoutItem::_ReservedSpaceLayoutItem5() {}
void BSpaceLayoutItem::_ReservedSpaceLayoutItem6() {}
void BSpaceLayoutItem::_ReservedSpaceLayoutItem7() {}
void BSpaceLayoutItem::_ReservedSpaceLayoutItem8() {}
void BSpaceLayoutItem::_ReservedSpaceLayoutItem9() {}
void BSpaceLayoutItem::_ReservedSpaceLayoutItem10() {}
