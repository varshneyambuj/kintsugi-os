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
 *   Copyright 2010, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file AbstractLayout.cpp
 * @brief Implementation of BAbstractLayout, the base class for all layout managers
 *
 * BAbstractLayout extends BLayout with serialization support and common size
 * constraint propagation. Concrete layout subclasses derive from this class.
 *
 * @see BLayout, BTwoDimensionalLayout
 */


#include <AbstractLayout.h>
#include <LayoutUtils.h>
#include <Message.h>
#include <View.h>
#include <ViewPrivate.h>


namespace {
	/** @brief Archive field name for the min, max, and preferred sizes (indexed 0–2). */
	const char* const kSizesField = "BAbstractLayout:sizes";
		// kSizesField == {min, max, preferred}
	/** @brief Archive field name for the layout's explicit alignment. */
	const char* const kAlignmentField = "BAbstractLayout:alignment";
	/** @brief Archive field name for the layout's frame rectangle. */
	const char* const kFrameField = "BAbstractLayout:frame";
	/** @brief Archive field name for the layout's visibility flag. */
	const char* const kVisibleField = "BAbstractLayout:visible";

	enum proxy_type { VIEW_PROXY_TYPE, DATA_PROXY_TYPE };
}


/** @brief Abstract proxy interface that decouples size/frame/visibility storage from the layout.
 *
 *  Two concrete implementations exist: DataProxy stores the values internally
 *  (used when the layout is not attached to a BView), and ViewProxy delegates
 *  to a BView (used when the layout owns a view).
 */
struct BAbstractLayout::Proxy {

	/** @brief Constructs a Proxy of the specified type.
	 *  @param type Either VIEW_PROXY_TYPE or DATA_PROXY_TYPE.
	 */
	Proxy(proxy_type type)
		:
		type(type)
	{
	}

	/** @brief Virtual destructor. */
	virtual ~Proxy()
	{
	}

	virtual	BSize		MinSize() const = 0;
	virtual void		SetMinSize(const BSize&) = 0;

	virtual	BSize		MaxSize() const = 0;
	virtual void		SetMaxSize(const BSize&) = 0;

	virtual	BSize		PreferredSize() const = 0;
	virtual void		SetPreferredSize(const BSize&) = 0;

	virtual	BAlignment	Alignment() const = 0;
	virtual	void		SetAlignment(const BAlignment&) = 0;

	virtual	BRect		Frame() const = 0;
	virtual	void		SetFrame(const BRect& frame) = 0;

	virtual bool		IsVisible(bool ancestorHidden) const = 0;
	virtual	void		SetVisible(bool visible) = 0;

	virtual	status_t	AddDataToArchive(BMessage* archive,
							bool ancestorHidden) = 0;
	virtual	status_t	RestoreDataFromArchive(const BMessage* archive) = 0;

			/** @brief Discriminator tag indicating whether this is a view or data proxy. */
			proxy_type	type;
};


/** @brief In-memory proxy used when the layout is not attached to a BView.
 *
 *  All size constraints, alignment, frame, and visibility are stored as plain
 *  struct members and serialised to/from a BMessage archive.
 */
struct BAbstractLayout::DataProxy : Proxy {

	/** @brief Constructs a DataProxy with default (zero) sizes and a visible state. */
	DataProxy()
		:
		Proxy(DATA_PROXY_TYPE),
		minSize(),
		maxSize(),
		preferredSize(),
		alignment(),
		frame(-1, -1, 0, 0),
		visible(true)
	{
	}

	/** @brief Returns the stored minimum size.
	 *  @return The minimum BSize.
	 */
	BSize MinSize() const
	{
		return minSize;
	}

	/** @brief Sets the stored minimum size.
	 *  @param min The new minimum BSize.
	 */
	void SetMinSize(const BSize& min)
	{
		minSize = min;
	}

	/** @brief Returns the stored maximum size.
	 *  @return The maximum BSize.
	 */
	BSize MaxSize() const
	{
		return maxSize;
	}

	/** @brief Sets the stored maximum size.
	 *  @param max The new maximum BSize.
	 */
	void SetMaxSize(const BSize& max)
	{
		maxSize = max;
	}

	/** @brief Returns the stored preferred size.
	 *  @return The preferred BSize.
	 */
	BSize PreferredSize() const
	{
		return preferredSize;
	}

	/** @brief Sets the stored preferred size.
	 *  @param preferred The new preferred BSize.
	 */
	void SetPreferredSize(const BSize& preferred)
	{
		preferredSize = preferred;
	}

	/** @brief Returns the stored alignment.
	 *  @return The current BAlignment.
	 */
	BAlignment Alignment() const
	{
		return this->alignment;
	}

	/** @brief Sets the stored alignment.
	 *  @param align The new BAlignment.
	 */
	void SetAlignment(const BAlignment& align)
	{
		this->alignment = align;
	}

	/** @brief Returns the stored frame rectangle.
	 *  @return The current frame as a BRect.
	 */
	BRect Frame() const
	{
		return frame;
	}

	/** @brief Sets the stored frame rectangle, ignoring no-op assignments.
	 *  @param frame The new frame BRect.
	 */
	void SetFrame(const BRect& frame)
	{
		if (frame == this->frame)
			return;
		this->frame = frame;
	}

	/** @brief Returns the stored visibility state.
	 *
	 *  The \a ancestorHidden parameter is ignored for DataProxy; visibility
	 *  is determined solely by the locally stored flag.
	 *
	 *  @param ancestorHidden Unused for this proxy type.
	 *  @return true if the item is visible, false if hidden.
	 */
	bool IsVisible(bool) const
	{
		return visible;
	}

	/** @brief Sets the stored visibility flag.
	 *  @param visible true to mark the item visible, false to hide it.
	 */
	void SetVisible(bool visible)
	{
		this->visible = visible;
	}

	/** @brief Serialises all stored constraints into \a archive.
	 *  @param archive       The target BMessage to write into.
	 *  @param ancestorHidden Unused for this proxy type.
	 *  @return B_OK on success, or an error code if any field could not be added.
	 */
	status_t AddDataToArchive(BMessage* archive, bool ancestorHidden)
	{
		status_t err = archive->AddSize(kSizesField, minSize);
		if (err == B_OK)
			err = archive->AddSize(kSizesField, maxSize);
		if (err == B_OK)
			err = archive->AddSize(kSizesField, preferredSize);
		if (err == B_OK)
			err = archive->AddAlignment(kAlignmentField, alignment);
		if (err == B_OK)
			err = archive->AddRect(kFrameField, frame);
		if (err == B_OK)
			err = archive->AddBool(kVisibleField, visible);

		return err;
	}

	/** @brief Restores all stored constraints from \a archive.
	 *  @param archive The source BMessage to read from.
	 *  @return B_OK on success, or the first error code encountered.
	 */
	status_t RestoreDataFromArchive(const BMessage* archive)
	{
		status_t err = archive->FindSize(kSizesField, 0, &minSize);
		if (err == B_OK)
			err = archive->FindSize(kSizesField, 1, &maxSize);
		if (err == B_OK)
			err = archive->FindSize(kSizesField, 2, &preferredSize);
		if (err == B_OK)
			err = archive->FindAlignment(kAlignmentField, &alignment);
		if (err == B_OK)
			err = archive->FindRect(kFrameField, &frame);
		if (err == B_OK)
			err = archive->FindBool(kVisibleField, &visible);

		return err;
	}

	/** @brief The stored minimum size constraint. */
	BSize		minSize;
	/** @brief The stored maximum size constraint. */
	BSize		maxSize;
	/** @brief The stored preferred size constraint. */
	BSize		preferredSize;
	/** @brief The stored alignment constraint. */
	BAlignment	alignment;
	/** @brief The stored frame rectangle. */
	BRect		frame;
	/** @brief The stored visibility flag. */
	bool		visible;
};


/** @brief View-backed proxy used when the layout is attached to a BView as its owner.
 *
 *  All reads and writes are forwarded to the underlying BView's explicit size
 *  and alignment properties. Archiving is a no-op because the view handles its
 *  own persistence.
 */
struct BAbstractLayout::ViewProxy : Proxy {

	/** @brief Constructs a ViewProxy delegating to the specified view.
	 *  @param target The BView whose explicit size/alignment properties will be used.
	 */
	ViewProxy(BView* target)
		:
		Proxy(VIEW_PROXY_TYPE),
		view(target)
	{
	}

	/** @brief Returns the view's explicit minimum size.
	 *  @return The minimum BSize as reported by BView::ExplicitMinSize().
	 */
	BSize MinSize() const
	{
		return view->ExplicitMinSize();
	}

	/** @brief Forwards the minimum size to the view.
	 *  @param min The new minimum BSize.
	 */
	void SetMinSize(const BSize& min)
	{
		view->SetExplicitMinSize(min);
	}

	/** @brief Returns the view's explicit maximum size.
	 *  @return The maximum BSize as reported by BView::ExplicitMaxSize().
	 */
	BSize MaxSize() const
	{
		return view->ExplicitMaxSize();
	}

	/** @brief Forwards the maximum size to the view.
	 *  @param min The new maximum BSize.
	 */
	void SetMaxSize(const BSize& min)
	{
		view->SetExplicitMaxSize(min);
	}

	/** @brief Returns the view's explicit preferred size.
	 *  @return The preferred BSize as reported by BView::ExplicitPreferredSize().
	 */
	BSize PreferredSize() const
	{
		return view->ExplicitPreferredSize();
	}

	/** @brief Forwards the preferred size to the view.
	 *  @param preferred The new preferred BSize.
	 */
	void SetPreferredSize(const BSize& preferred)
	{
		view->SetExplicitPreferredSize(preferred);
	}

	/** @brief Returns the view's explicit alignment.
	 *  @return The BAlignment as reported by BView::ExplicitAlignment().
	 */
	BAlignment Alignment() const
	{
		return view->ExplicitAlignment();
	}

	/** @brief Forwards the alignment to the view.
	 *  @param alignment The new BAlignment.
	 */
	void SetAlignment(const BAlignment& alignment)
	{
		view->SetExplicitAlignment(alignment);
	}

	/** @brief Returns the view's current frame rectangle.
	 *  @return The frame as reported by BView::Frame().
	 */
	BRect Frame() const
	{
		return view->Frame();
	}

	/** @brief Moves and resizes the view to match the given frame rectangle.
	 *  @param frame The new frame BRect in the parent view's coordinate system.
	 */
	void SetFrame(const BRect& frame)
	{
		view->MoveTo(frame.LeftTop());
		view->ResizeTo(frame.Width(), frame.Height());
	}

	/** @brief Returns whether the view is effectively visible given ancestor visibility.
	 *
	 *  Reads the view's internal show level and accounts for whether ancestors
	 *  are currently visible.
	 *
	 *  @param ancestorsVisible true if all ancestor views are currently shown.
	 *  @return true if the view is effectively visible.
	 */
	bool IsVisible(bool ancestorsVisible) const
	{
		int16 showLevel = BView::Private(view).ShowLevel();
		return (showLevel - (ancestorsVisible ? 0 : 1)) <= 0;
	}

	/** @brief Shows or hides the underlying view.
	 *  @param visible true to show the view, false to hide it.
	 */
	void SetVisible(bool visible)
	{
		// No need to check that we are not re-hiding, that is done
		// for us.
		if (visible)
			view->Show();
		else
			view->Hide();
	}

	/** @brief No-op archive method; the view handles its own persistence.
	 *  @param archive       Unused.
	 *  @param ancestorHidden Unused.
	 *  @return B_OK always.
	 */
	status_t AddDataToArchive(BMessage* archive, bool ancestorHidden)
	{
		return B_OK;
	}

	/** @brief No-op restore method; the view handles its own persistence.
	 *  @param archive Unused.
	 *  @return B_OK always.
	 */
	status_t RestoreDataFromArchive(const BMessage* archive)
	{
		return B_OK;
	}

	/** @brief The BView this proxy delegates all operations to. */
	BView*	view;
};


/** @brief Default constructor. Creates a BAbstractLayout with an in-memory data proxy.
 *
 *  The layout is not attached to any view; size constraints are stored
 *  internally until the layout is adopted by a BView via OwnerChanged().
 */
BAbstractLayout::BAbstractLayout()
	:
	fExplicitData(new BAbstractLayout::DataProxy())
{
}


/** @brief Unarchiving constructor. Restores a BAbstractLayout from a BMessage archive.
 *
 *  Calls BLayout's unarchiving constructor via BUnarchiver::PrepareArchive(),
 *  allocates a fresh DataProxy, and completes the unarchive pass. Constraint
 *  data is restored later in AllUnarchived().
 *
 *  @param from The archive message produced by Archive().
 *  @see Archive(), AllUnarchived()
 */
BAbstractLayout::BAbstractLayout(BMessage* from)
	:
	BLayout(BUnarchiver::PrepareArchive(from)),
	fExplicitData(new DataProxy())
{
	BUnarchiver(from).Finish();
}


/** @brief Destructor. Frees the proxy object. */
BAbstractLayout::~BAbstractLayout()
{
	delete fExplicitData;
}


/** @brief Returns the effective minimum size by composing the explicit override with the base minimum.
 *  @return The composed minimum BSize.
 *  @see BaseMinSize(), SetExplicitMinSize()
 */
BSize
BAbstractLayout::MinSize()
{
	return BLayoutUtils::ComposeSize(fExplicitData->MinSize(), BaseMinSize());
}


/** @brief Returns the effective maximum size by composing the explicit override with the base maximum.
 *  @return The composed maximum BSize.
 *  @see BaseMaxSize(), SetExplicitMaxSize()
 */
BSize
BAbstractLayout::MaxSize()
{
	return BLayoutUtils::ComposeSize(fExplicitData->MaxSize(), BaseMaxSize());
}


/** @brief Returns the effective preferred size by composing the explicit override with the base preferred.
 *  @return The composed preferred BSize.
 *  @see BasePreferredSize(), SetExplicitPreferredSize()
 */
BSize
BAbstractLayout::PreferredSize()
{
	return BLayoutUtils::ComposeSize(fExplicitData->PreferredSize(),
		BasePreferredSize());
}


/** @brief Returns the effective alignment by composing the explicit override with the base alignment.
 *  @return The composed BAlignment.
 *  @see BaseAlignment(), SetExplicitAlignment()
 */
BAlignment
BAbstractLayout::Alignment()
{
	return BLayoutUtils::ComposeAlignment(fExplicitData->Alignment(),
		BaseAlignment());
}


/** @brief Sets an explicit minimum size override for this layout item.
 *
 *  When set, this value takes precedence over BaseMinSize() during size
 *  composition. Pass an unset BSize to clear the override.
 *
 *  @param size The explicit minimum size.
 */
void
BAbstractLayout::SetExplicitMinSize(BSize size)
{
	fExplicitData->SetMinSize(size);
}


/** @brief Sets an explicit maximum size override for this layout item.
 *
 *  When set, this value takes precedence over BaseMaxSize() during size
 *  composition. Pass an unset BSize to clear the override.
 *
 *  @param size The explicit maximum size.
 */
void
BAbstractLayout::SetExplicitMaxSize(BSize size)
{
	fExplicitData->SetMaxSize(size);
}


/** @brief Sets an explicit preferred size override for this layout item.
 *
 *  When set, this value takes precedence over BasePreferredSize() during size
 *  composition. Pass an unset BSize to clear the override.
 *
 *  @param size The explicit preferred size.
 */
void
BAbstractLayout::SetExplicitPreferredSize(BSize size)
{
	fExplicitData->SetPreferredSize(size);
}


/** @brief Sets an explicit alignment override for this layout item.
 *
 *  When set, this value takes precedence over BaseAlignment() during alignment
 *  composition. Pass an unset BAlignment to clear the override.
 *
 *  @param alignment The explicit alignment.
 */
void
BAbstractLayout::SetExplicitAlignment(BAlignment alignment)
{
	fExplicitData->SetAlignment(alignment);
}


/** @brief Returns the subclass-defined base minimum size before any explicit override is applied.
 *
 *  The default implementation returns (0, 0). Subclasses should override this
 *  to return the natural minimum size derived from their children.
 *
 *  @return The base minimum BSize.
 */
BSize
BAbstractLayout::BaseMinSize()
{
	return BSize(0, 0);
}


/** @brief Returns the subclass-defined base maximum size before any explicit override is applied.
 *
 *  The default implementation returns (B_SIZE_UNLIMITED, B_SIZE_UNLIMITED).
 *  Subclasses should override this to return the natural maximum size derived
 *  from their children.
 *
 *  @return The base maximum BSize.
 */
BSize
BAbstractLayout::BaseMaxSize()
{
	return BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED);
}


/** @brief Returns the subclass-defined base preferred size before any explicit override is applied.
 *
 *  The default implementation returns (0, 0). Subclasses should override this
 *  to return the natural preferred size derived from their children.
 *
 *  @return The base preferred BSize.
 */
BSize
BAbstractLayout::BasePreferredSize()
{
	return BSize(0, 0);
}


/** @brief Returns the subclass-defined base alignment before any explicit override is applied.
 *
 *  The default implementation returns full-width and full-height alignment.
 *  Subclasses may override this to return a more specific alignment.
 *
 *  @return The base BAlignment.
 */
BAlignment
BAbstractLayout::BaseAlignment()
{
	return BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_USE_FULL_HEIGHT);
}


/** @brief Returns the current frame rectangle assigned to this layout item.
 *  @return The current BRect frame via the active proxy.
 */
BRect
BAbstractLayout::Frame()
{
	return fExplicitData->Frame();
}


/** @brief Assigns a new frame rectangle to this layout item and triggers a relayout if needed.
 *
 *  If the new frame differs from the current frame and the layout has no owner
 *  view, Relayout() is called to update child positions.
 *
 *  @param frame The new frame BRect in the parent view's coordinate system.
 */
void
BAbstractLayout::SetFrame(BRect frame)
{
	if (frame != fExplicitData->Frame()) {
		fExplicitData->SetFrame(frame);
		if (!Owner())
			Relayout();
	}
}


/** @brief Returns whether this layout item is currently visible.
 *
 *  Delegates to the active proxy, passing the current ancestor visibility
 *  state so that view-backed proxies can account for the show level.
 *
 *  @return true if the layout item is visible, false if hidden.
 */
bool
BAbstractLayout::IsVisible()
{
	return fExplicitData->IsVisible(AncestorsVisible());
}


/** @brief Shows or hides this layout item and notifies the parent layout.
 *
 *  Only takes effect when the new visibility differs from the current state.
 *  Invalidates the parent layout and calls VisibilityChanged() on a change.
 *
 *  @param visible true to show the item, false to hide it.
 */
void
BAbstractLayout::SetVisible(bool visible)
{
	if (visible != fExplicitData->IsVisible(AncestorsVisible())) {
		fExplicitData->SetVisible(visible);
		if (Layout())
			Layout()->InvalidateLayout(false);
		VisibilityChanged(visible);
	}
}


/** @brief Archives this BAbstractLayout into the provided BMessage.
 *
 *  Uses BArchiver to correctly handle circular references and deferred
 *  archiving. Calls BLayout::Archive() to archive the base class.
 *
 *  @param into The target archive message.
 *  @param deep If true, child layout items are also archived.
 *  @return B_OK on success, or an error code if archiving failed.
 */
status_t
BAbstractLayout::Archive(BMessage* into, bool deep) const
{
	BArchiver archiver(into);
	status_t err = BLayout::Archive(into, deep);

	return archiver.Finish(err);
}


/** @brief Called after all objects in the archive graph have been archived.
 *
 *  Delegates to BLayout::AllArchived(). Subclasses that need to archive
 *  cross-references should override this.
 *
 *  @param archive The completed archive message.
 *  @return B_OK on success, or an error code.
 */
status_t
BAbstractLayout::AllArchived(BMessage* archive) const
{
	return BLayout::AllArchived(archive);
}


/** @brief Called after all objects in the archive graph have been unarchived.
 *
 *  Restores the explicit size, alignment, frame, and visibility data from
 *  the archive before delegating to BLayout::AllUnarchived().
 *
 *  @param from The source archive message.
 *  @return B_OK on success, or the first error encountered during restoration.
 */
status_t
BAbstractLayout::AllUnarchived(const BMessage* from)
{
	status_t err = fExplicitData->RestoreDataFromArchive(from);
	if (err != B_OK)
		return err;

	return BLayout::AllUnarchived(from);
}


/** @brief Called when a layout item is being archived.
 *
 *  Delegates to BLayout::ItemArchived(). Subclasses may override to store
 *  per-item data alongside each item's archive entry.
 *
 *  @param into  The archive message for the item.
 *  @param item  The layout item being archived.
 *  @param index The item's index in this layout.
 *  @return B_OK on success, or an error code.
 */
status_t
BAbstractLayout::ItemArchived(BMessage* into, BLayoutItem* item,
	int32 index) const
{
	return BLayout::ItemArchived(into, item, index);
}


/** @brief Called when a layout item is being unarchived.
 *
 *  Delegates to BLayout::ItemUnarchived(). Subclasses may override to restore
 *  per-item data stored alongside each item's archive entry.
 *
 *  @param from  The archive message for the item.
 *  @param item  The layout item being unarchived.
 *  @param index The item's index in this layout.
 *  @return B_OK on success, or an error code.
 */
status_t
BAbstractLayout::ItemUnarchived(const BMessage* from, BLayoutItem* item,
	int32 index)
{
	return BLayout::ItemUnarchived(from, item, index);
}


/** @brief Called when a layout item is added to this layout.
 *
 *  Delegates to BLayout::ItemAdded(). Subclasses may override to perform
 *  bookkeeping when a new item joins the layout.
 *
 *  @param item    The newly added layout item.
 *  @param atIndex The index at which the item was inserted.
 *  @return true to accept the item, false to reject it.
 */
bool
BAbstractLayout::ItemAdded(BLayoutItem* item, int32 atIndex)
{
	return BLayout::ItemAdded(item, atIndex);
}


/** @brief Called when a layout item is removed from this layout.
 *
 *  Delegates to BLayout::ItemRemoved(). Subclasses may override to clean up
 *  bookkeeping when an item leaves the layout.
 *
 *  @param item      The layout item that was removed.
 *  @param fromIndex The index from which the item was removed.
 */
void
BAbstractLayout::ItemRemoved(BLayoutItem* item, int32 fromIndex)
{
	BLayout::ItemRemoved(item, fromIndex);
}


/** @brief Called when the layout has been invalidated.
 *
 *  Delegates to BLayout::LayoutInvalidated(). Subclasses should override to
 *  discard any cached size information so it is recomputed on the next layout pass.
 *
 *  @param children true if child layout items were also invalidated.
 */
void
BAbstractLayout::LayoutInvalidated(bool children)
{
	BLayout::LayoutInvalidated(children);
}


/** @brief Called when the layout is adopted by or detached from a BView.
 *
 *  When a view takes ownership, the internal DataProxy is replaced with a
 *  ViewProxy that forwards all operations to the owning view. If the layout
 *  is detached from a view (Owner() returns NULL), the existing ViewProxy's
 *  view pointer is updated to the new owner instead.
 *
 *  @param was The previous owner BView, or NULL if there was none.
 */
void
BAbstractLayout::OwnerChanged(BView* was)
{
	if (was) {
		static_cast<ViewProxy*>(fExplicitData)->view = Owner();
		return;
	}

	delete fExplicitData;
	fExplicitData = new ViewProxy(Owner());
}


/** @brief Called when this layout is attached to a parent layout.
 *
 *  Delegates to BLayout::AttachedToLayout(). Subclasses may override to
 *  perform initialization that requires a parent layout context.
 */
void
BAbstractLayout::AttachedToLayout()
{
	BLayout::AttachedToLayout();
}


/** @brief Called when this layout is detached from its parent layout.
 *
 *  Delegates to BLayout::DetachedFromLayout(). Subclasses may override to
 *  release resources that depend on the parent layout context.
 *
 *  @param layout The parent layout this layout is being detached from.
 */
void
BAbstractLayout::DetachedFromLayout(BLayout* layout)
{
	BLayout::DetachedFromLayout(layout);
}


/** @brief Propagates an ancestor visibility change to the owning view and the base class.
 *
 *  When the shown state actually changes, the owning view (if any) is shown or
 *  hidden to match, and BLayout::AncestorVisibilityChanged() is called to
 *  continue propagation down the hierarchy.
 *
 *  @param shown true if the ancestors have become visible, false if hidden.
 */
void
BAbstractLayout::AncestorVisibilityChanged(bool shown)
{
	if (AncestorsVisible() == shown)
		return;

	if (BView* owner = Owner()) {
		if (shown)
			owner->Show();
		else
			owner->Hide();
	}
	BLayout::AncestorVisibilityChanged(shown);
}


// Binary compatibility stuff


/** @brief Binary compatibility dispatch hook. Delegates to BLayout::Perform().
 *  @param code  The perform operation code.
 *  @param _data Pointer to operation-specific data.
 *  @return The result from BLayout::Perform().
 */
status_t
BAbstractLayout::Perform(perform_code code, void* _data)
{
	return BLayout::Perform(code, _data);
}


void BAbstractLayout::_ReservedAbstractLayout1() {}
void BAbstractLayout::_ReservedAbstractLayout2() {}
void BAbstractLayout::_ReservedAbstractLayout3() {}
void BAbstractLayout::_ReservedAbstractLayout4() {}
void BAbstractLayout::_ReservedAbstractLayout5() {}
void BAbstractLayout::_ReservedAbstractLayout6() {}
void BAbstractLayout::_ReservedAbstractLayout7() {}
void BAbstractLayout::_ReservedAbstractLayout8() {}
void BAbstractLayout::_ReservedAbstractLayout9() {}
void BAbstractLayout::_ReservedAbstractLayout10() {}
