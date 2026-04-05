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
 *   Copyright 2010, Haiku, Inc. All rights reserved.
 *   Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file AbstractLayoutItem.cpp
 * @brief Implementation of BAbstractLayoutItem, the base class for non-view layout items
 *
 * BAbstractLayoutItem provides default implementations of the BLayoutItem
 * interface for items that are not directly backed by a BView, including
 * alignment, frame management, and serialization support.
 *
 * @see BLayoutItem, BLayout
 */


#include <AbstractLayoutItem.h>

#include <LayoutUtils.h>
#include <Message.h>


namespace {
	/** @brief Archive field name used to store the three packed size constraints (min, max, preferred). */
	const char* const kSizesField = "BAbstractLayoutItem:sizes";
		// kSizesField == {min, max, preferred}
	/** @brief Archive field name used to store the explicit alignment constraint. */
	const char* const kAlignmentField = "BAbstractLayoutItem:alignment";
}


/**
 * @brief Default constructor; initializes all explicit size and alignment constraints to unset.
 *
 * The item will report sizes and alignment derived entirely from its Base*()
 * virtual methods until explicit overrides are applied via SetExplicit*().
 */
BAbstractLayoutItem::BAbstractLayoutItem()
	:
	fMinSize(),
	fMaxSize(),
	fPreferredSize(),
	fAlignment()
{
}


/**
 * @brief Unarchiving constructor; restores explicit size and alignment constraints from a BMessage.
 *
 * Reads up to three BSize entries stored under @c kSizesField (min, max,
 * preferred in index order) and the alignment stored under @c kAlignmentField.
 * All other state is delegated to BLayoutItem's unarchiving constructor.
 *
 * @param from The archive message to restore state from.
 * @see Archive()
 */
BAbstractLayoutItem::BAbstractLayoutItem(BMessage* from)
	:
	BLayoutItem(from),
	fMinSize(),
	fMaxSize(),
	fPreferredSize(),
	fAlignment()
{
	from->FindSize(kSizesField, 0, &fMinSize);
	from->FindSize(kSizesField, 1, &fMaxSize);
	from->FindSize(kSizesField, 2, &fPreferredSize);
	from->FindAlignment(kAlignmentField, &fAlignment);
}


/**
 * @brief Destructor.
 *
 * No heap resources are owned directly by BAbstractLayoutItem; cleanup of the
 * layout association is handled by the BLayoutItem base destructor.
 */
BAbstractLayoutItem::~BAbstractLayoutItem()
{
}


/**
 * @brief Returns the effective minimum size for this item.
 *
 * Composes the explicit minimum size (set via SetExplicitMinSize()) with the
 * value returned by BaseMinSize(). An unset explicit dimension falls back to
 * the corresponding base dimension.
 *
 * @return The composed minimum BSize.
 * @see SetExplicitMinSize(), BaseMinSize(), BLayoutUtils::ComposeSize()
 */
BSize
BAbstractLayoutItem::MinSize()
{
	return BLayoutUtils::ComposeSize(fMinSize, BaseMinSize());
}


/**
 * @brief Returns the effective maximum size for this item.
 *
 * Composes the explicit maximum size (set via SetExplicitMaxSize()) with the
 * value returned by BaseMaxSize(). An unset explicit dimension falls back to
 * the corresponding base dimension.
 *
 * @return The composed maximum BSize.
 * @see SetExplicitMaxSize(), BaseMaxSize(), BLayoutUtils::ComposeSize()
 */
BSize
BAbstractLayoutItem::MaxSize()
{
	return BLayoutUtils::ComposeSize(fMaxSize, BaseMaxSize());
}


/**
 * @brief Returns the effective preferred size for this item.
 *
 * Composes the explicit maximum size with the value returned by
 * BasePreferredSize(). An unset explicit dimension falls back to the
 * corresponding base dimension.
 *
 * @return The composed preferred BSize.
 * @see SetExplicitPreferredSize(), BasePreferredSize(), BLayoutUtils::ComposeSize()
 */
BSize
BAbstractLayoutItem::PreferredSize()
{
	return BLayoutUtils::ComposeSize(fMaxSize, BasePreferredSize());
}


/**
 * @brief Returns the effective alignment for this item.
 *
 * Composes the explicit alignment (set via SetExplicitAlignment()) with the
 * value returned by BaseAlignment(). An unset explicit axis falls back to the
 * corresponding base axis.
 *
 * @return The composed BAlignment.
 * @see SetExplicitAlignment(), BaseAlignment(), BLayoutUtils::ComposeAlignment()
 */
BAlignment
BAbstractLayoutItem::Alignment()
{
	return BLayoutUtils::ComposeAlignment(fAlignment, BaseAlignment());
}


/**
 * @brief Sets an explicit minimum size that overrides the base minimum.
 *
 * Only dimensions that are set in @a size will override the corresponding
 * dimension returned by BaseMinSize(). Pass a default-constructed BSize (all
 * dimensions unset) to clear any previous override.
 *
 * @param size The explicit minimum size to apply.
 * @see MinSize(), BaseMinSize()
 */
void
BAbstractLayoutItem::SetExplicitMinSize(BSize size)
{
	fMinSize = size;
}


/**
 * @brief Sets an explicit maximum size that overrides the base maximum.
 *
 * Only dimensions that are set in @a size will override the corresponding
 * dimension returned by BaseMaxSize(). Pass a default-constructed BSize (all
 * dimensions unset) to clear any previous override.
 *
 * @param size The explicit maximum size to apply.
 * @see MaxSize(), BaseMaxSize()
 */
void
BAbstractLayoutItem::SetExplicitMaxSize(BSize size)
{
	fMaxSize = size;
}


/**
 * @brief Sets an explicit preferred size that overrides the base preferred size.
 *
 * Only dimensions that are set in @a size will override the corresponding
 * dimension returned by BasePreferredSize(). Pass a default-constructed BSize
 * (all dimensions unset) to clear any previous override.
 *
 * @param size The explicit preferred size to apply.
 * @see PreferredSize(), BasePreferredSize()
 */
void
BAbstractLayoutItem::SetExplicitPreferredSize(BSize size)
{
	fPreferredSize = size;
}


/**
 * @brief Sets an explicit alignment that overrides the base alignment.
 *
 * Only axes that are set in @a alignment will override the corresponding axis
 * returned by BaseAlignment(). Pass a default-constructed BAlignment (all axes
 * unset) to clear any previous override.
 *
 * @param alignment The explicit alignment to apply.
 * @see Alignment(), BaseAlignment()
 */
void
BAbstractLayoutItem::SetExplicitAlignment(BAlignment alignment)
{
	fAlignment = alignment;
}


/**
 * @brief Returns the intrinsic minimum size of this item.
 *
 * Subclasses should override this method to report their natural minimum size.
 * The default implementation returns (0, 0).
 *
 * @return The base minimum BSize.
 * @see MinSize(), SetExplicitMinSize()
 */
BSize
BAbstractLayoutItem::BaseMinSize()
{
	return BSize(0, 0);
}


/**
 * @brief Returns the intrinsic maximum size of this item.
 *
 * Subclasses should override this method to report their natural maximum size.
 * The default implementation returns (B_SIZE_UNLIMITED, B_SIZE_UNLIMITED).
 *
 * @return The base maximum BSize.
 * @see MaxSize(), SetExplicitMaxSize()
 */
BSize
BAbstractLayoutItem::BaseMaxSize()
{
	return BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED);
}


/**
 * @brief Returns the intrinsic preferred size of this item.
 *
 * Subclasses should override this method to report their natural preferred size.
 * The default implementation returns (0, 0).
 *
 * @return The base preferred BSize.
 * @see PreferredSize(), SetExplicitPreferredSize()
 */
BSize
BAbstractLayoutItem::BasePreferredSize()
{
	return BSize(0, 0);
}


/**
 * @brief Returns the intrinsic alignment of this item.
 *
 * Subclasses should override this method to report their natural alignment.
 * The default implementation returns centered alignment on both axes.
 *
 * @return The base BAlignment.
 * @see Alignment(), SetExplicitAlignment()
 */
BAlignment
BAbstractLayoutItem::BaseAlignment()
{
	return BAlignment(B_ALIGN_HORIZONTAL_CENTER, B_ALIGN_VERTICAL_CENTER);
}


/**
 * @brief Archives this item's explicit size and alignment constraints into a BMessage.
 *
 * Writes three BSize values (min, max, preferred) sequentially under the
 * @c kSizesField key and the explicit alignment under @c kAlignmentField, in
 * addition to the data stored by BLayoutItem::Archive().
 *
 * @param into The message to archive into.
 * @param deep If @c true, child objects are archived recursively.
 * @return @c B_OK on success, or an error code on failure.
 * @see BAbstractLayoutItem(BMessage*)
 */
status_t
BAbstractLayoutItem::Archive(BMessage* into, bool deep) const
{
	BArchiver archiver(into);
	status_t err = BLayoutItem::Archive(into, deep);

	if (err == B_OK)
		err = into->AddSize(kSizesField, fMinSize);

	if (err == B_OK)
		err = into->AddSize(kSizesField, fMaxSize);

	if (err == B_OK)
		err = into->AddSize(kSizesField, fPreferredSize);

	if (err == B_OK)
		err = into->AddAlignment(kAlignmentField, fAlignment);

	return archiver.Finish(err);
}


/**
 * @brief Hook called after the entire object graph has been unarchived.
 *
 * Delegates to BLayoutItem::AllUnarchived(). Subclasses may override to
 * perform cross-object initialization that requires all archived objects to
 * already exist.
 *
 * @param archive The original archive message.
 * @return @c B_OK on success, or an error code on failure.
 */
status_t
BAbstractLayoutItem::AllUnarchived(const BMessage* archive)
{
	return BLayoutItem::AllUnarchived(archive);
}


/**
 * @brief Hook called after the entire object graph has been archived.
 *
 * Delegates to BLayoutItem::AllArchived(). Subclasses may override to
 * archive cross-object references after all individual objects have been
 * archived.
 *
 * @param archive The archive message being built.
 * @return @c B_OK on success, or an error code on failure.
 */
status_t
BAbstractLayoutItem::AllArchived(BMessage* archive) const
{
	return BLayoutItem::AllArchived(archive);
}


/**
 * @brief Hook called when the layout's cached size information is invalidated.
 *
 * Delegates to BLayoutItem::LayoutInvalidated(). Subclasses may override to
 * discard any cached sizing state they maintain.
 *
 * @param children If @c true, child layouts should also be invalidated.
 */
void
BAbstractLayoutItem::LayoutInvalidated(bool children)
{
	BLayoutItem::LayoutInvalidated(children);
}


/**
 * @brief Hook called when this item is added to a BLayout.
 *
 * Delegates to BLayoutItem::AttachedToLayout(). Subclasses may override to
 * perform initialization that requires a valid layout association.
 */
void
BAbstractLayoutItem::AttachedToLayout()
{
	BLayoutItem::AttachedToLayout();
}


/**
 * @brief Hook called when this item is removed from its BLayout.
 *
 * Delegates to BLayoutItem::DetachedFromLayout(). Subclasses may override to
 * release resources that were acquired in AttachedToLayout().
 *
 * @param layout The layout this item is being detached from.
 */
void
BAbstractLayoutItem::DetachedFromLayout(BLayout* layout)
{
	BLayoutItem::DetachedFromLayout(layout);
}


/**
 * @brief Hook called when an ancestor view's visibility changes.
 *
 * Delegates to BLayoutItem::AncestorVisibilityChanged(). Subclasses may
 * override to show or hide content in response to an ancestor being shown or
 * hidden without this item's own visibility state changing.
 *
 * @param shown @c true if an ancestor became visible, @c false if it was hidden.
 */
void
BAbstractLayoutItem::AncestorVisibilityChanged(bool shown)
{
	BLayoutItem::AncestorVisibilityChanged(shown);
}


/**
 * @brief Internal perform hook for binary-compatible virtual dispatch.
 *
 * Delegates to BLayoutItem::Perform(). This method is not intended for direct
 * use by application code.
 *
 * @param d The perform code identifying the operation.
 * @param arg Pointer to operation-specific argument data.
 * @return @c B_OK on success, or an error code on failure.
 */
status_t
BAbstractLayoutItem::Perform(perform_code d, void* arg)
{
	return BLayoutItem::Perform(d, arg);
}


void BAbstractLayoutItem::_ReservedAbstractLayoutItem1() {}
void BAbstractLayoutItem::_ReservedAbstractLayoutItem2() {}
void BAbstractLayoutItem::_ReservedAbstractLayoutItem3() {}
void BAbstractLayoutItem::_ReservedAbstractLayoutItem4() {}
void BAbstractLayoutItem::_ReservedAbstractLayoutItem5() {}
void BAbstractLayoutItem::_ReservedAbstractLayoutItem6() {}
void BAbstractLayoutItem::_ReservedAbstractLayoutItem7() {}
void BAbstractLayoutItem::_ReservedAbstractLayoutItem8() {}
void BAbstractLayoutItem::_ReservedAbstractLayoutItem9() {}
void BAbstractLayoutItem::_ReservedAbstractLayoutItem10() {}
